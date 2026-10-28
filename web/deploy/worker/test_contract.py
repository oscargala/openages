# /*==LICENSE==*
#
# CyanWorlds.com Engine - MMOG client, server and tools
# Copyright (C) 2011  Cyan Worlds, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Additional permissions under GNU GPL version 3 section 7
#
# If you modify this Program, or any covered work, by linking or
# combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
# NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
# JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
# (or a modified version of those libraries),
# containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
# PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
# JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
# licensors of this Program grant you additional
# permission to convey the resulting work. Corresponding Source for a
# non-source form of such a combination shall include the source code for
# the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
# work.
#
# You can contact Cyan Worlds, Inc. by email legal@cyan.com
#  or by snail mail at:
#       Cyan Worlds, Inc.
#       14617 N Newport Hwy
#       Mead, WA   99021
#
# *==LICENSE==*/
"""One gateway contract, two implementations. The same cases run against the local pair (web/deploy/plasma_web/gateway.py and
web/deploy/plasma_web/server.py, both with --deployment) and against the Worker under `wrangler dev` (local workerd only;
nothing contacts Cloudflare), each with a local fake upstream (no shard contact).

The cases are web/tests/test_admission.py's (join tokens, per-address limits, byte rate, idle timeout, logs, the
/runtime/join endpoint) and test_gateway.py's (routes refused before any TCP connection, verbatim binary relay, text and
wrong-type frames), on a deployment's /connect route, plus the client address behind a proxy, the global connection
cap and its release on every end of a connection and after a crash, the shard-disconnect close, the 502 on a failed
connect, the 1 MiB message limit, the bound on queued frames and the static headers, and the status relay
/runtime/status (server.py relay_status and StatusRelay) against a local fake status server.
AddressKeyTest compares the Worker's address key (src/address.js, run in Node) with gateway.py's; EmitWorkerTest checks
emit_worker.py.

Run with the deps venv:
  python -m unittest test_contract                                 (both backends)
  PLASMA_CONTRACT_BACKEND=gateway|worker ...                       (one)
  PLASMA_CONTRACT_OUT=<dir>   per-instance logs (gateway/wrangler) kept there; default a temporary directory
  PLASMA_WRANGLER=<path>      the pinned wrangler (default: wrangler on PATH)
  PLASMA_NODE=<path>          Node for AddressKeyTest (default: node on PATH)
Every run makes its own throwaway join key (join_token.init_secret, 0600), hands it to wrangler through an env file
(--env-file) in the run directory, and deletes both when the instance stops; the key is never printed.
"""

import asyncio
import base64
import concurrent.futures
import contextlib
import hashlib
import http.server
import ipaddress
import json
import os
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidStatus
from websockets.protocol import State

from plasma_web import deployment as deployment_file  # the web package (web/deploy: installed, or on PYTHONPATH)
from plasma_web import join_token

HERE = Path(__file__).resolve().parent
TREE = HERE.parents[2]

WRANGLER = os.environ.get("PLASMA_WRANGLER") or shutil.which("wrangler")
NODE = os.environ.get("PLASMA_NODE") or shutil.which("node")
NAME = "gateway-contract"
FOREIGN = "http://127.0.0.1:10"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def rss_kib(pid):
    return int(subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True, check=True).stdout)


def ini(port, status=None):
    """A shard's server.ini at 127.0.0.1:port, with its Server.Status URL when status is given."""
    key = lambda: base64.b64encode(secrets.token_bytes(64)).decode()
    return (
        "\n".join(
            [
                f'Server.Auth.Host "127.0.0.1"',
                f"Server.Port {port}",
                *([f'Server.Status "{status}"'] if status else []),
                *(f'Server.{s}.{k} "{key()}"' for s in ("Auth", "Game", "Gate") for k in ("N", "X")),
                "Server.Auth.G 41",
                "Server.Game.G 73",
                "Server.Gate.G 4",
            ]
        )
        + "\n"
    )


def deployment_doc(server_ini, secret_file, origin, host, limits, name=NAME, proxies=None):
    gateway = {"origins": [origin], "publicHosts": [host], "join": {"secretFile": str(secret_file)}, "limits": limits}
    if proxies:
        gateway["trustedProxies"] = proxies
    return {
        "schema": 1,
        "name": name,
        "serverIni": str(server_ini),
        "data": {"files": "patcher", "gameCode": "server"},
        "gateway": gateway,
    }


def wait_http(url, proc, seconds=60):
    deadline = time.time() + seconds
    while True:
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError(f"{url}: process exited {proc.returncode}")
            if time.time() > deadline:
                raise RuntimeError(f"{url}: not up after {seconds} s")
            time.sleep(0.2)


def spawn(cmd, out, **kw):
    """A process with its output in the file `out`; the file is closed in the parent at once."""
    with open(out, "a") as f:
        return subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, **kw)


class LocalPair:
    """gateway.py and server.py with one deployment (the local implementation). With `proxy`, the deployment trusts
    127.0.0.1 as a reverse proxy, so the gateway takes the client address from CF-Connecting-IP as it does behind
    Cloudflare."""

    kind = "gateway"

    def __init__(self, run, server_ini, secret_file, limits, proxy=False):
        self.run = run
        self.gw_port, self.page_port = free_port(), free_port()
        self.origin = f"http://127.0.0.1:{self.page_port}"
        self.page = self.origin
        self.dep = run / "deployment.json"
        self.dep.write_text(
            json.dumps(
                deployment_doc(
                    server_ini,
                    secret_file,
                    self.origin,
                    f"127.0.0.1:{self.gw_port}",
                    limits,
                    proxies=["127.0.0.1/32"] if proxy else None,
                )
            )
        )
        self.log = run / "gateway.jsonl"
        self.access = run / "access.jsonl"
        build = run / "build"
        build.mkdir()
        self.gateway = None
        self.server = spawn(
            [
                sys.executable,
                "-m",
                "plasma_web.server",
                "--deployment",
                str(self.dep),
                "--gateway-url",
                f"ws://127.0.0.1:{self.gw_port}",
                "--port",
                str(self.page_port),
                "--access-log",
                str(self.access),
                "--build-dir",
                str(build),
            ],
            run / "server.out",
        )
        try:
            wait_http(self.page + "/runtime/config.json", self.server)
            self.start_gateway()
        except RuntimeError:
            self.stop()
            raise

    def start_gateway(self):
        self.gateway = spawn(
            [
                sys.executable,
                "-m",
                "plasma_web.gateway",
                "--deployment",
                str(self.dep),
                "--port",
                str(self.gw_port),
                "--log",
                str(self.log),
            ],
            self.run / "gateway.out",
        )
        deadline = time.time() + 15
        while True:
            try:
                socket.create_connection(("127.0.0.1", self.gw_port), 1).close()
                return
            except OSError:
                if self.gateway.poll() is not None or time.time() > deadline:
                    raise RuntimeError("gateway.py did not start")
                time.sleep(0.05)

    def crash(self):
        """Kills the gateway without letting it clean up, then starts it again with the same deployment."""
        self.gateway.kill()
        self.gateway.wait(5)
        self.start_gateway()

    def ws(self, query):
        return f"ws://127.0.0.1:{self.gw_port}/connect{query}"

    def lines(self):
        return [json.loads(x) for x in self.log.read_text().splitlines()] if self.log.exists() else []

    def texts(self):
        return "".join(p.read_text() for p in (self.log, self.access) if p.exists())

    def pid(self):
        return self.gateway.pid

    async def rate_window(self):
        pass  # a sliding window: any moment is fine

    def stop(self):
        for p in (self.gateway, self.server):
            if p is not None:
                p.terminate()
                p.wait(5)


class WorkerDev:
    """The Worker under `wrangler dev` (local workerd), configured by emit_static.py + emit_worker.py from the same
    deployment file. wrangler dev passes a client's CF-Connecting-IP through (Cloudflare replaces it) and sets it to
    127.0.0.1 when the client sends none, so a case can name the client address as a proxy would."""

    kind = "worker"

    def __init__(self, run, server_ini, secret_file, limits, proxy=False):
        if not WRANGLER:
            raise RuntimeError("wrangler not found: set PLASMA_WRANGLER or put wrangler on PATH")
        self.run = run
        self.port = free_port()
        self.origin = f"http://127.0.0.1:{self.port}"
        self.page = self.origin
        dep = run / "deployment.json"
        doc = deployment_doc(server_ini, secret_file, self.origin, f"127.0.0.1:{self.port}", limits)
        dep.write_text(json.dumps(doc))
        subprocess.run(
            [
                sys.executable,
                "-m",
                "plasma_web.emit_static",
                str(dep),
                str(run / "kit"),
                "--gateway-url",
                f"ws://127.0.0.1:{self.port}",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        with open(run / "emit_worker.out", "w") as out:
            subprocess.run(
                [sys.executable, str(HERE / "emit_worker.py"), str(dep), str(run / "kit"), str(run / "worker")],
                check=True,
                stdout=out,
            )
        self.env_file = run / ".dev.vars"
        fd = os.open(self.env_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            if secret_file.exists():  # without it: the Worker runs with no JOIN_KEY (fails closed)
                f.write("JOIN_KEY=" + join_token.read_secret(secret_file).hex() + "\n")
        self.log = run / "wrangler.log"
        self.proc = None
        self.start_wrangler()

    def start_wrangler(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("CLOUDFLARE")}
        env.update(WRANGLER_SEND_METRICS="false", NO_COLOR="1")
        # Its own process group, so a crash can take workerd (wrangler's child) down with it.
        self.proc = spawn(
            [
                str(WRANGLER),
                "dev",
                "--config",
                str(self.run / "worker/wrangler.json"),
                "--env-file",
                str(self.env_file),
                "--local",
                "--ip",
                "127.0.0.1",
                "--port",
                str(self.port),
                "--inspector-port",
                str(free_port()),
                "--persist-to",
                str(self.run / "state"),
                "--show-interactive-dev-session=false",
            ],
            self.log,
            env=env,
            cwd=self.run,
            start_new_session=True,
        )
        (self.run / "pid").write_text(str(self.proc.pid))
        try:
            wait_http(self.page + "/runtime/config.json", self.proc, 90)
        except RuntimeError:
            self.stop()
            raise

    def crash(self):
        """Kills wrangler and workerd at once, as an eviction ends an isolate: no relay releases its leases. Then
        starts the Worker again on the same port and the same local Durable Object storage."""
        os.killpg(self.proc.pid, signal.SIGKILL)
        self.proc.wait(5)
        deadline = time.time() + 15
        while True:  # workerd gone: the port is free again
            try:
                socket.create_connection(("127.0.0.1", self.port), 0.5).close()
            except OSError:
                break
            if time.time() > deadline:
                raise RuntimeError("workerd still listening after the crash")
            time.sleep(0.2)
        self.start_wrangler()

    def ws(self, query):
        return f"ws://127.0.0.1:{self.port}/connect{query}"

    def lines(self):
        out = []
        for line in self.log.read_text(errors="replace").splitlines() if self.log.exists() else []:
            line = line.strip()
            if line.startswith("{"):
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
        return out

    def texts(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    async def rate_window(self):
        # The local rate-limiting simulation counts in fixed windows aligned to the wall clock (Date.now() / 60 s); a
        # rate case must not straddle a minute boundary.
        while time.time() % 60 > 35:
            await asyncio.sleep(1)

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                os.killpg(self.proc.pid, signal.SIGKILL)
                self.proc.wait(5)
        self.env_file.unlink(missing_ok=True)
        for p in ("state", "worker/.wrangler", "kit", "worker/assets"):  # local state and the page kit copies
            shutil.rmtree(self.run / p, ignore_errors=True)


BACKENDS = {"gateway": LocalPair, "worker": WorkerDev}


class Contract:
    backend = None

    async def asyncSetUp(self):
        root = os.environ.get("PLASMA_CONTRACT_OUT")
        base = Path(root) if root else Path(tempfile.mkdtemp(prefix="plat4-contract-"))
        self.tmp = base / f"{self.backend}-{self._testMethodName}-{int(time.time())}"
        self.tmp.mkdir(parents=True)
        self.sent = bytearray()
        self.accepted = 0

        async def upstream(reader, writer):
            # echo, plus 256 KiB on request byte 0xEE (the byte-rate case), and a close on 0xDD (shard disconnect)
            self.accepted += 1
            while data := await reader.read(65536):
                self.sent.extend(data)
                if data[-1:] == b"\xdd":
                    break
                writer.write(b"\x00" * 262144 if data[-1:] == b"\xee" else data)
                await writer.drain()
            writer.close()

        self.upstream = await asyncio.start_server(upstream, "127.0.0.1", 0)
        self.port = self.upstream.sockets[0].getsockname()[1]
        self.secret_file = self.tmp / "join.key"
        join_token.init_secret(self.secret_file)
        self.secret = join_token.read_secret(self.secret_file)
        self.instances = []

    async def asyncTearDown(self):
        for inst in self.instances:
            inst.stop()
        self.upstream.close()
        self.secret_file.unlink(missing_ok=True)

    def start(self, port=None, proxy=False, status=None, **limits):
        run = self.tmp / f"i{len(self.instances)}"
        run.mkdir()
        (run / "server.ini").write_text(ini(port or self.port, status))
        inst = BACKENDS[self.backend](run, run / "server.ini", self.secret_file, limits, proxy=proxy)
        self.instances.append(inst)
        self.inst = inst
        return inst.ws(f"?host=127.0.0.1&port={port or self.port}")

    def token(self, deployment=NAME, ttl=60, now=None):
        return join_token.issue(self.secret, deployment, ttl, now)[0]

    def open(self, url, token=None, origin=None, **kw):
        protocols = ["plasma"] + ([f"plasma.join.{token}"] if token else [])
        return connect(
            url,
            origin=self.inst.origin if origin is None else origin,
            subprotocols=protocols,
            compression=None,
            open_timeout=10,
            max_size=None,
            **kw,
        )

    async def refused(self, url, token=None, origin=None, **kw):
        with self.assertRaises(InvalidStatus) as cm:
            async with self.open(url, token, origin, **kw):
                pass
        return cm.exception.response.status_code, cm.exception.response.body.decode().strip()

    async def admitted(self, url, seconds=5, step=0.2, **kw):
        """Opens a connection, retrying while the gateway answers 503 (a slot the last relay released may take a
        moment to come back); returns the open connection."""
        deadline = time.monotonic() + seconds
        while True:
            try:
                return await self.open(url, self.token(), **kw)
            except InvalidStatus as e:
                if e.response.status_code != 503 or time.monotonic() > deadline:
                    raise
                await asyncio.sleep(step)

    @contextlib.asynccontextmanager
    async def closed_by_peer(self, connecting):
        """A connection the gateway is expected to close. On the way out it is closed only if the gateway has not
        closed it. close() on a connection whose TCP connection is gone calls transport.abort(), and asyncio's
        selector transport fails there (AttributeError, its loop is None) when it finished closing by draining its
        write buffer: _SelectorSocketTransport._write_ready calls _call_connection_lost without counting _conn_lost,
        so _force_close does not see the earlier close. That happens after an oversized frame: gateway.py's
        WebSocket server closes 1009 as soon as it reads the frame header, while the client is still sending the
        frame."""
        ws = await connecting
        try:
            yield ws
        finally:
            if ws.state is not State.CLOSED:
                await ws.close()

    def post_join(self, origin=None, host=None, ctype="application/json", body=b"{}", client=None):
        """POST /runtime/join; returns (status, JSON body or None, headers)."""
        headers = {"Origin": self.inst.origin if origin is None else origin, "Content-Type": ctype}
        if host is not None:
            headers["Host"] = host
        if client is not None:
            headers["CF-Connecting-IP"] = client
        req = urllib.request.Request(self.inst.page + "/runtime/join", data=body, method="POST", headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status, json.loads(r.read()), dict(r.headers)
        except urllib.error.HTTPError as e:
            return e.code, None, dict(e.headers)

    def record(self, **values):
        (self.tmp / "observed.json").write_text(json.dumps(values, indent=1) + "\n")

    # --- test_admission.py ---------------------------------------------------------------------------------------
    async def test_tokens(self):
        url = self.start()
        tok = self.token()
        async with self.open(url, tok) as ws:
            self.assertEqual(ws.subprotocol, "plasma")
            await ws.send(b"\x0aping")
            self.assertEqual(await ws.recv(), b"\x0aping")
        seen = {
            "required": await self.refused(url),
            "replay": await self.refused(url, tok),
            "expired": await self.refused(url, self.token(now=time.time() - 120)),
            "another": await self.refused(url, self.token("another")),
        }
        bad = tok[:-2] + ("AA" if tok[-2:] != "AA" else "BB")
        seen["bad_mac"] = await self.refused(url, bad)
        seen["future"] = await self.refused(url, self.token(now=time.time() + 3600))
        other = url.replace("host=127.0.0.1", "host=10.1.2.3")
        fresh = self.token()
        seen["not_allowlisted"] = await self.refused(other, fresh)  # not allowlisted: token not spent
        seen["origin"] = await self.refused(url, fresh, FOREIGN)
        self.record(**seen)
        self.assertEqual(seen["required"], (401, "Join token required"))
        self.assertEqual(seen["replay"], (401, "Join token already used"))
        self.assertEqual(seen["expired"], (401, "Join token expired"))
        self.assertEqual(seen["another"], (403, "Join token for another deployment"))
        self.assertEqual(seen["bad_mac"], (401, "Invalid join token"))
        self.assertEqual(seen["future"], (401, "Invalid join token"))
        self.assertEqual(seen["not_allowlisted"][0], 403)
        self.assertEqual(seen["origin"], (403, "Origin not allowed"))
        async with self.open(url, fresh):  # still unspent after both refusals
            pass
        await asyncio.sleep(0.5)
        rows = self.inst.lines()
        text = self.inst.texts()
        self.assertNotIn(tok.split(".")[2], text)
        self.assertNotIn(fresh.split(".")[2], text)
        self.assertNotIn(self.secret.hex(), text)
        done = [r for r in rows if "sent" in r]
        self.assertEqual(len(done), 2)
        self.assertEqual(done[0]["upstream"], f"127.0.0.1:{self.port}")
        self.assertEqual(done[0]["ip"], "127.0.0.1")
        self.assertEqual(done[0]["sent"], 5)
        self.assertEqual(sorted(r["refused"] for r in rows if "refused" in r), [401, 401, 401, 401, 401, 403, 403, 403])
        self.assertTrue(all(r["ip"] == "127.0.0.1" for r in rows))
        self.assertEqual(self.accepted, 2)

    async def test_concurrency(self):
        url = self.start(connectionsPerIp=2)
        a = await self.open(url, self.token())
        b = await self.open(url, self.token())
        self.assertEqual(
            await self.refused(url, self.token()), (429, "Too many concurrent connections from this address")
        )
        await a.close()
        await asyncio.sleep(0.5)
        c = await self.open(url, self.token())
        await b.close()
        await c.close()

    async def test_rate(self):
        url = self.start(connectsPerMinutePerIp=3)
        await self.inst.rate_window()
        for _ in range(3):
            async with self.open(url, self.token()):
                pass
        self.assertEqual(
            await self.refused(url, self.token()), (429, "Too many connections per minute from this address")
        )
        self.assertEqual(await self.refused(url), (429, "Too many connections per minute from this address"))

    async def test_idle(self):
        url = self.start(idleSeconds=1)
        async with self.open(url, self.token()) as ws:
            await ws.send(b"\x0ax")
            await ws.recv()
            t = time.monotonic()
            with self.assertRaises(ConnectionClosed) as cm:
                await ws.recv()
            self.assertEqual(cm.exception.rcvd.code, 4000)
            self.assertEqual(cm.exception.rcvd.reason, "Idle timeout")
            self.assertGreater(time.monotonic() - t, 0.8)
        await asyncio.sleep(0.5)
        self.assertEqual([r["end"] for r in self.inst.lines() if "sent" in r], ["idle timeout"])

    async def test_byte_rate(self):
        url = self.start(bytesPerSecond=65536)
        async with self.open(url, self.token()) as ws:
            t = time.monotonic()
            await ws.send(b"\x0a\xee")
            got = 0
            while got < 262144:
                got += len(await ws.recv())
            # one second of burst, then 65536 B/s: 262144 B take at least ~3 s
            elapsed = time.monotonic() - t
            self.record(elapsed=round(elapsed, 3))
            self.assertGreater(elapsed, 2.5)

    async def test_join_endpoint(self):
        self.start(connectsPerMinutePerIp=2)
        base = self.inst.page

        def post(origin=base, ctype="application/json", path="/runtime/join", body=b"{}"):
            req = urllib.request.Request(
                base + path, data=body, method="POST", headers={"Origin": origin, "Content-Type": ctype}
            )
            try:
                with urllib.request.urlopen(req, timeout=5) as r:
                    return r.status, json.loads(r.read()), dict(r.headers)
            except urllib.error.HTTPError as e:
                return e.code, None, dict(e.headers)

        with urllib.request.urlopen(base + "/runtime/config.json", timeout=5) as r:
            doc = json.loads(r.read())
        self.assertEqual(doc["join"], "/runtime/join")
        await self.inst.rate_window()
        self.assertEqual(post(origin="http://evil.example")[0], 403)
        self.assertEqual(post(ctype="text/plain")[0], 403)
        self.assertEqual(post(body=b" " * 1025)[0], 403)
        status, body, headers = post()
        self.assertEqual(status, 200)
        self.assertEqual(headers.get("Cache-Control"), "no-store")
        self.assertEqual(headers.get("Content-Type"), "application/json")
        self.assertEqual(headers.get("Cross-Origin-Embedder-Policy"), "require-corp")
        v = join_token.Verifier(self.secret, NAME, 60)
        v.check(body["token"])
        self.assertLessEqual(body["expires"], time.time() + 61)
        statuses = [post()[0] for _ in range(4)]
        self.record(after_first=statuses)
        self.assertEqual(statuses, [200, 200, 200, 429])  # 2 x connectsPerMinutePerIp = 4 per minute
        self.assertNotIn(body["token"], self.inst.texts())
        self.assertNotIn(body["token"].split(".")[2], self.inst.texts())

    async def test_join_origin_from_deployment(self):
        # The page origins come from the deployment, never from the request's Host: a listed origin is admitted
        # whatever Host the request names, and an unlisted origin is refused even when it matches Host.
        self.start()
        await self.inst.rate_window()
        other = "localhost:" + self.inst.page.rsplit(":", 1)[1]
        seen = {
            "listed origin, Host localhost": self.post_join(host=other)[0],
            "Origin = Host = evil.example": self.post_join(origin="http://evil.example", host="evil.example")[0],
            "Origin = Host = localhost": self.post_join(origin=f"http://{other}", host=other)[0],
            "listed origin": self.post_join()[0],
        }
        self.record(**seen)
        self.assertEqual(
            seen,
            {
                "listed origin, Host localhost": 200,
                "Origin = Host = evil.example": 403,
                "Origin = Host = localhost": 403,
                "listed origin": 200,
            },
        )

    async def test_client_address(self):
        # Behind Cloudflare the client's address is CF-Connecting-IP. A value that is not one IP address is refused
        # (400) instead of being counted under one shared key; an IPv6 client counts by its /64, an IPv4-mapped one as
        # its IPv4 address.
        url = self.start(proxy=True, connectionsPerIp=1, connectsPerMinutePerIp=100)

        def by(address):
            return {"additional_headers": {"CF-Connecting-IP": address}}

        seen = {}
        for value in ["garbage", "203.0.113.7, 203.0.113.8", "999.1.1.1", "2001:db8::1::2"]:
            seen[value] = await self.refused(url, self.token(), **by(value))
        held = [
            await self.open(url, self.token(), **by(v))
            for v in ["203.0.113.7", "203.0.113.8", "2001:db8:1:2::a", "2001:db8:1:3::a"]
        ]
        for value in ["203.0.113.7", "2001:db8:1:2::b", "::ffff:203.0.113.8"]:
            seen[value] = await self.refused(url, self.token(), **by(value))
        for ws in held:
            await ws.close()
        # The join endpoint takes the client address as /connect does (server.py's issuer goes through
        # client_limits.ClientRate, the gateway's helper), so it refuses the same garbage.
        seen["join garbage"] = self.post_join(client="garbage")[0]
        await asyncio.sleep(0.5)
        relayed = sorted(r["ip"] for r in self.inst.lines() if "sent" in r)
        self.record(seen=seen, relayed=relayed)
        for value in ["garbage", "203.0.113.7, 203.0.113.8", "999.1.1.1", "2001:db8::1::2"]:
            self.assertEqual(seen[value], (400, "Client address missing from the proxy"), value)
        for value in ["203.0.113.7", "2001:db8:1:2::b", "::ffff:203.0.113.8"]:
            self.assertEqual(seen[value], (429, "Too many concurrent connections from this address"), value)
        self.assertEqual(relayed, ["2001:db8:1:2::a", "2001:db8:1:3::a", "203.0.113.7", "203.0.113.8"])
        self.assertEqual(seen["join garbage"], 400)

    async def test_global_capacity(self):
        # limits.connections caps the relays open at once from all addresses (503); the per-address cap (429) is
        # checked first.
        url = self.start(proxy=True, connections=2, connectionsPerIp=2, connectsPerMinutePerIp=100)
        one = {"additional_headers": {"CF-Connecting-IP": "203.0.113.1"}}
        two = {"additional_headers": {"CF-Connecting-IP": "203.0.113.2"}}
        a = await self.open(url, self.token(), **one)
        b = await self.open(url, self.token(), **one)
        seen = {
            "same address": await self.refused(url, self.token(), **one),
            "other address": await self.refused(url, self.token(), **two),
        }
        await a.close()
        c = await self.admitted(url, **two)
        self.record(**seen)
        self.assertEqual(
            seen,
            {
                "same address": (429, "Too many concurrent connections from this address"),
                "other address": (503, "Gateway at capacity"),
            },
        )
        await b.close()
        await c.close()

    async def test_capacity_released_on_every_end(self):
        # With room for one connection, each way a connection can end must give its slot back: the next connection
        # (admitted() waits up to 5 s for it) would otherwise be refused 503.
        url = self.start(connections=1, idleSeconds=2, connectsPerMinutePerIp=1000)
        ends = {}

        async def closed_by_gateway(ws, data):
            if data is not None:
                await ws.send(data)
            with self.assertRaises(ConnectionClosed) as cm:
                while True:
                    await asyncio.wait_for(ws.recv(), 10)
            await ws.wait_closed()
            return cm.exception.rcvd.code if cm.exception.rcvd else None

        async with await self.admitted(url) as ws:  # the browser closes
            await ws.send(b"\x0ax")
            await ws.recv()
        ends["browser close"] = "closed"
        for name, data in [
            ("shard disconnect", b"\x0a\xdd"),
            ("text frame", "x"),
            ("wrong type", bytes([99])),
            ("oversized frame", b"\x0a" * (1024 * 1024 + 1)),
            ("idle timeout", None),
        ]:
            async with self.closed_by_peer(self.admitted(url)) as ws:
                ends[name] = await closed_by_gateway(ws, data)
        deadline = time.monotonic() + 5  # no upgrade headers: 426
        while True:
            req = urllib.request.Request(
                url.replace("ws://", "http://"),
                headers={"Origin": self.inst.origin, "Sec-WebSocket-Protocol": f"plasma, plasma.join.{self.token()}"},
            )
            with self.assertRaises(urllib.error.HTTPError) as cm:
                await asyncio.to_thread(urllib.request.urlopen, req, timeout=10)
            if cm.exception.code != 503 or time.monotonic() > deadline:
                break
            await asyncio.sleep(0.2)
        ends["plain GET"] = cm.exception.code
        async with await self.admitted(url):
            pass
        self.record(**ends)
        self.assertEqual(
            ends,
            {
                "browser close": "closed",
                "shard disconnect": 1000,
                "text frame": 1003,
                "wrong type": 1008,
                "oversized frame": 1009,
                "idle timeout": 4000,
                "plain GET": 426,
            },
        )

    async def test_capacity_after_crash(self):
        # A gateway that dies without closing its relays must not keep their slots. gateway.py holds them in memory,
        # so a restart frees them; the Worker's leases outlive an evicted isolate in Durable Object storage and are
        # dropped once not renewed for LEASE_STALE_MS (90 s).
        url = self.start(connections=1, connectsPerMinutePerIp=1000)
        ws = await self.open(url, self.token())
        await ws.send(b"\x0ax")
        await ws.recv()
        t = time.monotonic()
        await asyncio.to_thread(self.inst.crash)
        restarted = time.monotonic() - t
        first = None
        try:
            async with self.open(url, self.token()):
                first = "admitted"
        except InvalidStatus as e:
            first = (e.response.status_code, e.response.body.decode().strip())
        probe = await self.admitted(url, seconds=150, step=2) if first != "admitted" else None
        waited = time.monotonic() - t
        if probe is not None:
            await probe.close()
        ws.transport.abort()
        self.record(restarted=round(restarted, 1), first=first, waited=round(waited, 1))
        if self.backend == "gateway":
            self.assertEqual(first, "admitted")
        else:
            self.assertEqual(first, (503, "Gateway at capacity"))
            self.assertGreater(waited, 60)
            self.assertLess(waited, 130)

    # --- test_gateway.py, on a deployment's /connect route --------------------------------------------------------
    async def test_reject_before_tcp(self):
        url = self.start()
        p = self.port
        seen = {}
        for route in [
            "/auth",
            f"/auth?host=127.0.0.1&port={p}",
            f"/connect?host=192.0.2.10&port={p}",
            "/connect?host=127.0.0.1&port=80",
            f"/connect?host=127.0.0.1&host=127.0.0.2&port={p}",
            f"/connect?host=127.0.0.1&port={p}&x=1",
            f"/connect?host=127.0.0.1&port=0{p}",
            f"/connect?host=127.0.0.1&port={p}&",
            "/connect",
            "/unknown",
        ]:
            seen[route] = await self.refused(url.split("/connect")[0] + route, self.token())
        for origin in ["http://127.0.0.1:9999", "https://example.com"]:
            seen[f"origin {origin}"] = await self.refused(url, self.token(), origin)
        with self.assertRaises(InvalidStatus) as cm:
            async with connect(url, subprotocols=["plasma", f"plasma.join.{self.token()}"], compression=None):
                pass
        seen["no origin"] = (cm.exception.response.status_code, cm.exception.response.body.decode().strip())
        self.record(**seen)
        for k, v in seen.items():
            self.assertEqual(v[0], 403, k)
        self.assertEqual(self.accepted, 0)

    async def test_foreign_host(self):
        url = self.start()
        # a raw upgrade request, as the browser sends it, with a Host that is not the gateway's
        tok = self.token()
        reader, writer = await asyncio.open_connection("127.0.0.1", int(url.split(":")[2].split("/")[0]))
        writer.write(
            (
                f"GET /connect?host=127.0.0.1&port={self.port} HTTP/1.1\r\nHost: evil.example\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                f"Sec-WebSocket-Version: 13\r\nOrigin: {self.inst.origin}\r\n"
                f"Sec-WebSocket-Protocol: plasma, plasma.join.{tok}\r\n\r\n"
            ).encode()
        )
        await writer.drain()
        head = (await asyncio.wait_for(reader.read(4096), 10)).decode(errors="replace")
        writer.close()
        status = int(head.split(" ", 2)[1])
        body = head.split("\r\n\r\n", 1)[1].strip() if "\r\n\r\n" in head else ""
        self.record(status=status, body=body)
        self.assertEqual(status, 403)
        self.assertEqual(self.accepted, 0)
        async with self.open(url, tok):  # not spent by the refusal
            pass

    async def test_binary_stream(self):
        url = self.start()
        async with self.open(url, self.token()) as ws:
            chunks = [bytes([22, 1, 2]), bytes(range(256)), b"\0\xff\0"]
            for chunk in chunks:
                await ws.send(chunk)
            want = b"".join(chunks)
            got = b""
            while len(got) < len(want):
                got += await asyncio.wait_for(ws.recv(), 5)
            self.assertEqual(got, want)
            self.assertEqual(bytes(self.sent), want)

    async def test_reject_text_and_wrong_type(self):
        url = self.start()
        seen = []
        for data, code in [("not binary", 1003), (bytes([99]), 1008), (b"", 1008)]:
            async with self.open(url, self.token()) as ws:
                await ws.send(data)
                with self.assertRaises(ConnectionClosed) as closed:
                    await ws.recv()
                seen.append([closed.exception.rcvd.code, closed.exception.rcvd.reason])
                self.assertEqual(closed.exception.rcvd.code, code)
        self.record(closes=seen)
        await asyncio.sleep(0.3)
        self.assertEqual(bytes(self.sent), b"")

    # --- the rest of the gateway contract -----------------------------------------------------------------------
    async def test_shard_disconnect(self):
        url = self.start()
        async with self.open(url, self.token()) as ws:
            await ws.send(b"\x0a\xdd")
            with self.assertRaises(ConnectionClosed) as cm:
                await asyncio.wait_for(ws.recv(), 5)
            self.assertEqual(cm.exception.rcvd.code, 1000)
            self.assertEqual(cm.exception.rcvd.reason, "Shard disconnected")
        await asyncio.sleep(0.5)
        ends = [r["end"] for r in self.inst.lines() if "sent" in r]
        self.record(code=cm.exception.rcvd.code, reason=cm.exception.rcvd.reason, end=ends)
        # gateway.py labels this end "shard closed" or "browser closed": its upstream and downstream tasks both finish
        # on the close, and make_relay keeps the last of asyncio.wait's done set (unordered). One row either way.
        self.assertEqual(len(ends), 1)
        self.assertIn(ends[0], ("shard closed", "browser closed"))

    async def test_connect_failure_502(self):
        # an allowlisted port nobody listens on; with room for one connection the second attempt shows that the
        # failed one gave its slot back (it would be 503 otherwise)
        url = self.start(port=free_port(), connections=1)
        status, body = await self.refused(url, self.token())
        await asyncio.sleep(0.5)
        again = await self.refused(url, self.token())
        self.record(status=status, body=body, again=again)
        # the reasons differ by design: gateway.py "Shard connection unavailable", the Worker "Shard connection failed"
        self.assertEqual(status, 502)
        self.assertEqual(again, (502, body))
        await asyncio.sleep(0.3)
        self.assertEqual([r["refused"] for r in self.inst.lines() if "refused" in r], [502, 502])

    async def test_message_size_limit(self):
        url = self.start()
        async with self.closed_by_peer(self.open(url, self.token())) as ws:
            await ws.send(b"\x0a" + b"\x01" * (1024 * 1024 - 1))  # exactly 1 MiB: relayed
            got = 0
            while got < 1024 * 1024:
                got += len(await asyncio.wait_for(ws.recv(), 10))
            await ws.send(b"\x02" * (1024 * 1024 + 1))  # over: closed 1009
            with self.assertRaises(ConnectionClosed) as cm:
                await asyncio.wait_for(ws.recv(), 10)
            await ws.wait_closed()
            self.record(
                code=cm.exception.rcvd.code if cm.exception.rcvd else None,
                reason=cm.exception.rcvd.reason if cm.exception.rcvd else None,
            )
            self.assertEqual(cm.exception.rcvd.code, 1009)
        self.assertEqual(len(self.sent), 1024 * 1024)

    async def test_queue_bound(self):
        # A browser that sends faster than the upstream token bucket drains (64 KiB/s here) is held to 16 frames of
        # 1 MiB. gateway.py stops reading its socket while 16 frames wait, so the sender blocks and the gateway's memory
        # stays bounded; a Worker cannot pause a WebSocket, so it closes the socket 1008 above 16 MiB queued.
        url = self.start(bytesPerSecond=65536)
        frame = bytes([10]) + b"\x03" * (1024 * 1024 - 1)
        async with self.open(url, self.token(), close_timeout=2) as ws:
            if self.backend == "worker":
                for _ in range(18):
                    await ws.send(frame)
                with self.assertRaises(ConnectionClosed) as cm:
                    while True:
                        await asyncio.wait_for(ws.recv(), 20)
                self.record(code=cm.exception.rcvd.code, reason=cm.exception.rcvd.reason)
                self.assertEqual((cm.exception.rcvd.code, cm.exception.rcvd.reason), (1008, "Too much data queued"))
                return
            before = rss_kib(self.inst.pid())
            sent = 0

            async def flood():
                nonlocal sent
                for _ in range(64):
                    await ws.send(frame)
                    sent += 1

            task = asyncio.create_task(flood())
            await asyncio.sleep(5)
            grown = (rss_kib(self.inst.pid()) - before) // 1024
            blocked, still_open = not task.done(), ws.state.name == "OPEN"
            task.cancel()
            self.record(frames_sent=sent, rss_growth_mib=grown, blocked=blocked, open=still_open)
            self.assertTrue(blocked)
            self.assertTrue(still_open)
            self.assertLess(sent, 48)  # 64 MiB offered; 16 queued plus the socket buffers taken
            self.assertLess(grown, 40)
            ws.transport.abort()

    def test_static_headers(self):
        self.start()
        seen = {}
        paths = [
            "/",
            "/index.html",
            "/boot.mjs",
            "/input/capture.mjs",
            "/runtime/config.json",
            "/runtime/config.mjs",
            "/runtime/server.ini",
            "/source.json",
            "/LICENSE.txt",
            "/COPYING.txt",
            "/THIRD_PARTY_NOTICES.md",
            "/fonts/.fetch-manifest",
            "/fonts/Courier%20New.ttf",
            "/fonts/LICENSE-LiberationFonts.txt",
            "/missing",
            "/runtime/status",  # an ini without Server.Status: no relay (404, test_status_absent)
        ]
        for path in paths:
            try:
                with urllib.request.urlopen(self.inst.page + path, timeout=5) as r:
                    status, h = r.status, r.headers
            except urllib.error.HTTPError as e:
                status, h = e.code, e.headers
            seen[path] = {
                "status": status,
                **{
                    k: h.get(k)
                    for k in (
                        "Content-Type",
                        "Cache-Control",
                        "Cross-Origin-Opener-Policy",
                        "Cross-Origin-Embedder-Policy",
                        "Cross-Origin-Resource-Policy",
                        "X-Content-Type-Options",
                        "Content-Security-Policy",
                        "Accept-Ranges",
                    )
                },
            }
        with urllib.request.urlopen(self.inst.page + "/runtime/config.json", timeout=5) as r:
            csp = deployment_file.content_security_policy(json.loads(r.read()))
        self.record(**seen)
        for path, h in seen.items():
            self.assertEqual(h["status"], 404 if path in ("/missing", "/runtime/status") else 200, path)
            self.assertEqual(h["Content-Security-Policy"], csp, path)
            self.assertEqual(h["X-Content-Type-Options"], "nosniff", path)
            self.assertEqual(h["Cross-Origin-Opener-Policy"], "same-origin", path)
            self.assertEqual(h["Cross-Origin-Embedder-Policy"], "require-corp", path)
            self.assertEqual(h["Cross-Origin-Resource-Policy"], "same-origin", path)
            # server.py answers a range on any file; the Worker in the trees the client range-reads only
            # (test_static_ranges_outside_the_trees)
            ranges = self.backend == "gateway" or path.startswith("/fonts/")
            self.assertEqual(h["Accept-Ranges"], "bytes" if ranges else "none", path)
            # The page, its modules and the provenance files revalidate by ETag (the kit's _headers);
            # /runtime/*, the fonts tree and a 404 are no-store. Both add no-transform to the fonts tree, which a CDN
            # (Cloudflare's edge) would otherwise compress, weakening its ETag; the Worker adds it to the page document
            # too, whose ETag the edge's HTML features would otherwise remove.
            revalidate = (
                "/",
                "/index.html",
                "/boot.mjs",
                "/input/capture.mjs",
                "/source.json",
                "/LICENSE.txt",
                "/COPYING.txt",
                "/THIRD_PARTY_NOTICES.md",
            )
            want = "no-cache" if path in revalidate else "no-store"
            if path.startswith("/fonts/") or (self.backend == "worker" and path in ("/", "/index.html")):
                want += ", no-transform"
            self.assertEqual(h["Cache-Control"], want, path)
        # server.py's types: by file extension (MIME, then mimetypes), application/octet-stream for a file without a
        # type (the documents it serves from memory, the fonts tree's listing), text/plain for a 404
        self.assertEqual(
            {p: h["Content-Type"] for p, h in seen.items()},
            {
                "/": "text/html; charset=utf-8",
                "/index.html": "text/html; charset=utf-8",
                "/boot.mjs": "text/javascript",
                "/input/capture.mjs": "text/javascript",
                "/runtime/config.json": "application/octet-stream",
                "/runtime/config.mjs": "text/javascript",
                "/runtime/server.ini": "application/octet-stream",
                "/source.json": "application/json",
                "/LICENSE.txt": "text/plain; charset=utf-8",
                "/COPYING.txt": "text/plain; charset=utf-8",
                "/THIRD_PARTY_NOTICES.md": "text/markdown; charset=utf-8",
                "/fonts/.fetch-manifest": "application/octet-stream",
                "/fonts/Courier%20New.ttf": "font/ttf",
                "/fonts/LICENSE-LiberationFonts.txt": "text/plain; charset=utf-8",
                "/missing": "text/plain",
                "/runtime/status": "text/plain",
            },
        )

    def test_static_ranges_and_conditionals(self):
        # What the client's fetch backend needs from a tree file (plEmscriptenFetchBackend.js ensureSize and its range
        # reads; Docs/Web/Deploying.md, "What a host of the trees must serve"), as server.py respond() answers it: a
        # HEAD with a strong ETag and the file's Content-Length; a range with If-Match: 206 with the exact
        # Content-Range; a changed file (another If-Match): 412; past the end: 416; If-Range of another tag: the whole
        # file; a revalidation: 304. The Worker answers these itself: the asset binding ignores Range and If-Match,
        # and its HEAD answer has no Content-Length.
        self.start()
        data = Path(deployment_file.free_font_files()["Courier New.ttf"]).read_bytes()
        size, url = len(data), self.inst.page + "/fonts/Courier%20New.ttf"

        def ask(method="GET", **headers):
            req = urllib.request.Request(url, method=method, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=10) as r:
                    return r.status, r.headers, r.read()
            except urllib.error.HTTPError as e:
                return e.code, e.headers, e.read()

        st, h, body = ask("HEAD")
        tag = h["ETag"]
        seen = {"head": (st, h["Content-Length"], h.get("Content-Encoding"), len(body))}
        self.assertEqual(seen["head"], (200, str(size), None, 0))
        self.assertRegex(tag, r'^"[\x21\x23-\x7e\x80-\xff]*"$')  # strong (plEmscriptenFetchBackend.js strongTag)
        cases = {
            "range": (dict(Range="bytes=0-99", **{"If-Match": tag}), 206, f"bytes 0-99/{size}", data[:100]),
            "middle": (
                dict(Range="bytes=65536-131071", **{"If-Match": tag}),
                206,
                f"bytes 65536-131071/{size}",
                data[65536:131072],
            ),
            "open end": (dict(Range=f"bytes={size - 10}-"), 206, f"bytes {size - 10}-{size - 1}/{size}", data[-10:]),
            "suffix": (dict(Range="bytes=-100"), 206, f"bytes {size - 100}-{size - 1}/{size}", data[-100:]),
            "past the end": (dict(Range=f"bytes={size}-"), 416, f"bytes */{size}", b""),
            "other If-Match": (dict(Range="bytes=0-99", **{"If-Match": '"another"'}), 412, None, b""),
            "other If-Range": (dict(Range="bytes=0-99", **{"If-Range": '"another"'}), 200, None, data),
            "two ranges": (dict(Range="bytes=0-1,5-6"), 200, None, data),
            "revalidation": ({"If-None-Match": tag}, 304, None, b""),
            "If-Match first": ({"If-Match": '"another"', "If-None-Match": tag}, 412, None, b""),
        }
        for name, (headers, status, content_range, want) in cases.items():
            st, h, body = ask(**headers)
            seen[name] = (st, h.get("Content-Range"), h.get("ETag"), len(body))
            with self.subTest(case=name):
                self.assertEqual((st, h.get("Content-Range"), body), (status, content_range, want))
                self.assertEqual(h.get("ETag"), tag)
                self.assertIsNone(h.get("Content-Encoding"))
                if status in (206, 412, 416):
                    self.assertEqual(h["Content-Length"], str(len(want)))
        st, h, body = ask("HEAD", **{"If-Match": '"another"'})
        seen["HEAD other If-Match"] = (st, h.get("Content-Length"))
        self.assertEqual(seen["HEAD other If-Match"], (412, "0"))
        st, h, body = ask("HEAD", Range="bytes=0-99")  # a HEAD has no range: the whole file's length
        seen["HEAD with Range"] = (st, h.get("Content-Length"), h.get("Content-Range"))
        self.assertEqual(seen["HEAD with Range"], (200, str(size), None))
        self.record(**{k: list(v) for k, v in seen.items()})

    def test_static_ranges_outside_the_trees(self):
        # The client range-reads only the fetch backend's trees (plEmscriptenFetchBackend.js: data,
        # sound, fonts, game code; the HD packs are read whole, nothing else sends Range). Outside them the Worker
        # leaves Range and If-Range to the asset binding, which ignores them: the whole file, streamed, with
        # Accept-Ranges: none, so that no request makes the Worker hold a file (a /build/*.wasm) in memory. server.py
        # answers a range on any file. If-Match and If-None-Match apply everywhere on both.
        self.start()

        def ask(path, **headers):
            req = urllib.request.Request(self.inst.page + path, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=10) as r:
                    return r.status, r.headers, r.read()
            except urllib.error.HTTPError as e:
                return e.code, e.headers, e.read()

        seen = {}
        for path in ("/boot.mjs", "/input/capture.mjs", "/THIRD_PARTY_NOTICES.md"):
            _, h, whole = ask(path)
            tag = h["ETag"]
            got = {}
            for name, headers in (
                ("range", {"Range": "bytes=0-9"}),
                ("range, If-Range", {"Range": "bytes=0-9", "If-Range": tag}),
                ("range, If-Match", {"Range": "bytes=0-9", "If-Match": tag}),
                ("other If-Match", {"Range": "bytes=0-9", "If-Match": '"another"'}),
                ("revalidation", {"If-None-Match": tag}),
            ):
                st, h, body = ask(path, **headers)
                got[name] = (st, h.get("Content-Range"), h.get("Accept-Ranges"), len(body))
                with self.subTest(path=path, case=name):
                    if name == "other If-Match":
                        self.assertEqual(st, 412)
                    elif name == "revalidation":
                        self.assertEqual((st, body), (304, b""))
                    elif self.backend == "worker":
                        self.assertEqual(
                            (st, h.get("Content-Range"), h.get("Accept-Ranges"), body), (200, None, "none", whole)
                        )
                    else:
                        self.assertEqual(
                            (st, h.get("Content-Range"), body), (206, f"bytes 0-9/{len(whole)}", whole[:10])
                        )
            seen[path] = got
        self.record(**{p: {k: list(v) for k, v in g.items()} for p, g in seen.items()})

    def test_static_gamecode_is_no_tree(self):
        # No product deployment serves game code (the deployment's data.gameCode is server, server-py2 or local: from
        # the shard's file server or the data tree; main.cpp MountFileSystems mounts data, sound and fonts), so /gamecode
        # is no tree of the kit: a request there gets what a request into any tree the kit lacks gets, a 404 with
        # server.py's body and type, no-store, and no ranges on the Worker (Accept-Ranges: none) whatever it asks.
        self.start()

        def ask(path, method="GET", **headers):
            req = urllib.request.Request(self.inst.page + path, method=method, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=10) as r:
                    status, h, body = r.status, r.headers, r.read()
            except urllib.error.HTTPError as e:
                status, h, body = e.code, e.headers, e.read()
            return [
                status,
                *(
                    h.get(k)
                    for k in (
                        "Content-Type",
                        "Cache-Control",
                        "Accept-Ranges",
                        "Content-Range",
                        "ETag",
                        "X-Content-Type-Options",
                    )
                ),
                body.decode(errors="replace"),
            ]

        seen = {}
        for name, method, headers in (
            ("GET", "GET", {}),
            ("HEAD", "HEAD", {}),
            ("range", "GET", {"Range": "bytes=0-99", "If-Match": '"any"'}),
            ("revalidation", "GET", {"If-None-Match": '"any"'}),
        ):
            for tree in ("gamecode", "not-a-tree"):
                seen[f"{tree} {name}"] = ask(f"/{tree}/python/python.pak", method, **headers)
            with self.subTest(case=name):
                self.assertEqual(seen[f"gamecode {name}"], seen[f"not-a-tree {name}"])
                self.assertEqual(seen[f"gamecode {name}"][:3], [404, "text/plain", "no-store"])
                if self.backend == "worker":
                    self.assertEqual(seen[f"gamecode {name}"][3], "none")
        self.record(**seen)

    async def test_fails_closed_without_key(self):
        self.secret_file.unlink()
        if self.backend == "gateway":
            # gateway.py and server.py refuse to start ("Join key unavailable"): nothing listens
            with self.assertRaises(RuntimeError):
                self.start()
            return
        url = self.start()
        seen = {"connect": await self.refused(url, None)}
        req = urllib.request.Request(
            self.inst.page + "/runtime/join",
            data=b"{}",
            method="POST",
            headers={"Origin": self.inst.origin, "Content-Type": "application/json"},
        )
        with self.assertRaises(urllib.error.HTTPError) as cm:
            urllib.request.urlopen(req, timeout=5)
        seen["join"] = cm.exception.code
        self.record(**seen)
        self.assertEqual(seen, {"connect": (500, "Gateway not configured"), "join": 500})
        self.assertEqual(self.accepted, 0)

    async def test_plain_get(self):
        url = self.start()
        tok = self.token()
        req = urllib.request.Request(
            url.replace("ws://", "http://"),
            headers={"Origin": self.inst.origin, "Sec-WebSocket-Protocol": f"plasma, plasma.join.{tok}"},
        )
        with self.assertRaises(urllib.error.HTTPError) as cm:
            await asyncio.to_thread(urllib.request.urlopen, req, timeout=10)
        status, body = cm.exception.code, cm.exception.read().decode(errors="replace").strip()
        replay = await self.refused(url, tok)
        self.record(status=status, body=body, replay=replay, upstream_connections=self.accepted)
        self.assertEqual(status, 426)
        self.assertEqual(replay, (401, "Join token already used"))  # admission ran in full: the token is spent

    async def test_two_tokens(self):
        url = self.start()
        protocols = ["plasma", f"plasma.join.{self.token()}", f"plasma.join.{self.token()}"]
        with self.assertRaises(InvalidStatus) as cm:
            async with connect(url, origin=self.inst.origin, subprotocols=protocols, compression=None):
                pass
        seen = (cm.exception.response.status_code, cm.exception.response.body.decode().strip())
        self.record(seen=seen)
        self.assertEqual(seen, (401, "Invalid join token"))

    async def test_token_expiry_must_be_an_integer(self):
        # test_admission.py's case through a handshake: join_token.py refuses an expiry that json.loads does not read
        # as an int (a bool, a string, or a number with a fraction or an exponent, even of integral value).
        # JSON.parse reads 1792938130.0 and 1792938130e0 as the integer 1792938130, so the Worker checks the
        # expiry's literal text.
        url = self.start()

        def signed(payload_json):
            payload = join_token._b64(payload_json.encode())
            return f"v1.{payload}.{join_token._b64(join_token._mac(self.secret, payload))}"

        e = int(time.time()) + 30
        seen = {}
        for name, literal in [
            ("true", "true"),
            ("false", "false"),
            ("fraction", f"{e}.0"),
            ("exponent", f"{e}e0"),
            ("string", f'"{e}"'),
        ]:
            seen[name] = await self.refused(url, signed(f'{{"d":"{NAME}","e":{literal},"n":"n-{name}"}}'))
        async with self.open(url, signed(f'{{"d":"{NAME}","e":{e},"n":"n-integer"}}')) as ws:
            await ws.send(b"\x0aping")
            seen["integer"] = (await ws.recv()).decode()
        self.record(**seen)
        self.assertEqual(
            seen,
            {
                **{k: (401, "Invalid join token") for k in ("true", "false", "fraction", "exponent", "string")},
                "integer": "\nping",
            },
        )

    async def test_join_headers(self):
        # The join endpoint's answers, issued or refused, carry no-store, the isolation headers and nosniff.
        self.start()
        await self.inst.rate_window()
        seen = {}
        for name, kw in [("issued", {}), ("refused", {"origin": "http://evil.example"})]:
            status, _, h = self.post_join(**kw)
            seen[name] = {
                "status": status,
                **{k: h.get(k) for k in ("Cache-Control", "X-Content-Type-Options", "Cross-Origin-Resource-Policy")},
            }
        self.record(**seen)
        want = {
            "Cache-Control": "no-store",
            "X-Content-Type-Options": "nosniff",
            "Cross-Origin-Resource-Policy": "same-origin",
        }
        self.assertEqual(seen, {"issued": {"status": 200, **want}, "refused": {"status": 403, **want}})

    # --- the status relay -----------------------------------------------------------------------------------------
    def status_server(self, delay=0.0):
        """A fake shard status server (the ini's Server.Status host): /long answers 200 with a 409-byte body, any other
        path 404 with Dirtsand's text, each after `delay` seconds. Returns (port, the paths it was asked for)."""
        hits = []

        class Status(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                hits.append(self.path)
                time.sleep(delay)
                long = self.path.startswith("/long")
                body = b"Welcome! " + b"x" * 400 if long else b"No page found at /\r\n"
                try:
                    self.send_response(200 if long else 404)
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                except OSError:
                    pass  # the relay gave up (its timeout)

        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Status)
        srv.daemon_threads = True
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(srv.server_close)
        self.addCleanup(srv.shutdown)
        return srv.server_address[1], hits

    def get(self, path, method="GET", **headers):
        """(status, headers, body) of one request to the page origin."""
        req = urllib.request.Request(self.inst.page + path, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return r.status, r.headers, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.headers, e.read()

    def test_status_relay(self):
        # server.py relay_status and StatusRelay, the Worker's statusEndpoint: GET /runtime/status answers the body of
        # the ini's own Server.Status URL whatever its HTTP status, at most 255 bytes; one fetch at a time (requests on
        # a cold relay wait for it); the answer reused for STATUS_SECONDS (10 s), so the anonymous route costs no
        # request to the shard; never a URL from the request. The answer: a body from memory (application/octet-stream,
        # its sha256 ETag, If-None-Match, no-store) with the isolation headers, nosniff and the policy.
        port, hits = self.status_server(delay=1.0)
        self.start(status=f"http://127.0.0.1:{port}/long?q=1")
        status, _, raw = self.get("/runtime/config.json")
        doc = json.loads(raw)
        self.assertEqual(doc["shard"]["status"], "/runtime/status")  # the kit's (emit_static.py) and server.py's
        want = b"Welcome! " + b"x" * 246
        tag = '"sha256-' + hashlib.sha256(want).hexdigest() + '"'
        csp = deployment_file.content_security_policy(doc)
        with concurrent.futures.ThreadPoolExecutor(6) as pool:
            cold = list(pool.map(lambda _: self.get("/runtime/status"), range(6)))
        seen = {"cold": [(s, len(b)) for s, _, b in cold], "hits_after_cold": list(hits)}
        for s, h, body in cold:
            self.assertEqual((s, body), (200, want))
            self.assertEqual(
                {
                    k: h.get(k)
                    for k in (
                        "Content-Type",
                        "Content-Length",
                        "Cache-Control",
                        "ETag",
                        "Cross-Origin-Opener-Policy",
                        "Cross-Origin-Embedder-Policy",
                        "Cross-Origin-Resource-Policy",
                        "X-Content-Type-Options",
                        "Content-Security-Policy",
                        "Accept-Ranges",
                    )
                },
                {
                    "Content-Type": "application/octet-stream",
                    "Content-Length": "255",
                    "Cache-Control": "no-store",
                    "ETag": tag,
                    "Cross-Origin-Opener-Policy": "same-origin",
                    "Cross-Origin-Embedder-Policy": "require-corp",
                    "Cross-Origin-Resource-Policy": "same-origin",
                    "X-Content-Type-Options": "nosniff",
                    "Content-Security-Policy": csp,
                    # server.py answers a range on any body; the Worker in the range-read trees only
                    "Accept-Ranges": "bytes" if self.backend == "gateway" else "none",
                },
            )
        self.assertEqual(hits, ["/long?q=1"])  # one fetch for the six, of the configured URL
        # Within STATUS_SECONDS: the same answer, no request to the status host; the request's query names nothing
        s, _, body = self.get("/runtime/status?url=http%3A%2F%2F127.0.0.1%3A10%2F")
        self.assertEqual((s, body), (200, want))
        s, h, body = self.get("/runtime/status", method="HEAD")
        self.assertEqual((s, h.get("Content-Length"), body), (200, "255", b""))
        s, h, body = self.get("/runtime/status", **{"If-None-Match": tag})
        self.assertEqual((s, h.get("ETag"), body), (304, tag, b""))
        s, _, body = self.get("/runtime/status", **{"If-Match": '"sha256-0"'})
        self.assertEqual((s, body), (412, b""))
        self.assertEqual(hits, ["/long?q=1"])
        # After STATUS_SECONDS: fetched again
        time.sleep(10.5)
        s, _, body = self.get("/runtime/status")
        self.assertEqual((s, body), (200, want))
        seen["hits_at_end"] = list(hits)
        self.record(**seen)
        self.assertEqual(hits, ["/long?q=1", "/long?q=1"])

    def test_status_relay_failures(self):
        # A failed request answers its error text (200, at most 255 bytes), as relay_status and the native status
        # thread do: a refused connection and a status host that does not answer within relay_status's 5 s. The
        # status host's own error answer is its body (server.py's 404 body is Dirtsand's text).
        port, hits = self.status_server(delay=8.0)
        seen = {}
        for name, url in (("refused", FOREIGN + "/"), ("timeout", f"http://127.0.0.1:{port}/"), ("http-404", None)):
            if url is None:
                port404, _ = self.status_server()
                url = f"http://127.0.0.1:{port404}/"
            self.start(status=url)
            t0 = time.monotonic()
            s, h, body = self.get("/runtime/status")
            seen[name] = {"status": s, "bytes": len(body), "seconds": round(time.monotonic() - t0, 2)}
            self.assertEqual(s, 200, name)
            self.assertTrue(0 < len(body) <= 255, (name, body))
            self.assertEqual(h.get("Cache-Control"), "no-store", name)
            if name == "timeout":
                self.assertTrue(4.5 <= seen[name]["seconds"] < 8, seen[name])
            if name == "http-404":
                self.assertEqual(body, b"No page found at /\r\n")
            self.inst.stop()
        self.record(**seen)

    def test_status_absent(self):
        # An ini without Server.Status: no relay and no status in the page document (server.py answers 404, the Worker
        # its kit's 404).
        self.start()
        status, _, raw = self.get("/runtime/config.json")
        self.assertNotIn("status", json.loads(raw)["shard"])
        s, h, body = self.get("/runtime/status")
        self.assertEqual(
            (s, h.get("Content-Type"), h.get("Cache-Control"), body), (404, "text/plain", "no-store", b"not found\n")
        )


class AddressKeyTest(unittest.TestCase):
    """The Worker's address parsing and key (src/address.js, in Node) against gateway.py's ip_key on the same texts."""

    VALID = [
        "203.0.113.7",
        "0.0.0.0",
        "255.255.255.255",
        "2001:db8:1:2::a",
        "2001:DB8:1:2:0:0:0:A",
        "2001:0db8:0001:0002:0003:0004:0005:0006",
        "::",
        "::1",
        "1::",
        "1:2:3:4:5:6:7::",
        "2001:db8::",
        "2001:0:0:1::5",
        "0:0:1::",
        "fe80::1",
        "::ffff:203.0.113.8",
        "::FFFF:cb00:7108",
        "::1.2.3.4",
        "64:ff9b::192.0.2.33",
        "2001:db8:0:0:1::1",
    ]
    INVALID = [
        "",
        "garbage",
        "1.2.3",
        "01.2.3.4",
        "1.2.3.4.5",
        "256.1.1.1",
        "1.2.3.4 ",
        "2001:db8::1::2",
        ":::",
        "1:2:3:4:5:6:7:8:9",
        "12345::",
        "g::1",
        "1:2:3:4:5:6:7:8::",
        ":1:2:3:4:5:6:7",
        "::ffff:01.2.3.4",
        "203.0.113.7, 203.0.113.8",
        "[::1]",
    ]

    def node(self, script, data):
        if not NODE:
            self.fail("node not found: set PLASMA_NODE or put node on PATH")
        r = subprocess.run(
            [NODE, "--input-type=module", "-e", script],
            input=json.dumps(data),
            capture_output=True,
            text=True,
            timeout=30,
            cwd=HERE,
        )
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        return json.loads(r.stdout)

    def test_ip_key_matches_gateway(self):
        from plasma_web import gateway

        url = (HERE / "src/address.js").as_uri()
        script = (
            f"import {{ ipKey }} from {json.dumps(url)};"
            "let s = ''; process.stdin.on('data', d => s += d);"
            "process.stdin.on('end', () => console.log(JSON.stringify(JSON.parse(s).map(ipKey))));"
        )
        got = dict(zip(self.VALID + self.INVALID, self.node(script, self.VALID + self.INVALID)))
        for text in self.VALID:
            self.assertEqual(got[text], gateway.ip_key(text), text)
        for text in self.INVALID:
            self.assertIsNone(got[text], text)
            with self.assertRaises(ValueError, msg=text):
                ipaddress.ip_address(text)

    def test_client_address(self):
        url = (HERE / "src/address.js").as_uri()
        script = (
            f"import {{ clientAddress }} from {json.dumps(url)};"
            "let s = ''; process.stdin.on('data', d => s += d);"
            "process.stdin.on('end', () => console.log(JSON.stringify(JSON.parse(s).map(h => clientAddress("
            "{ headers: new Headers(h === null ? {} : { 'CF-Connecting-IP': h }) })))));"
        )
        values = [None, "", " 203.0.113.7 ", "2001:db8::1%eth0", "garbage", "2001:db8:1:2::a"]
        self.assertEqual(self.node(script, values), [None, None, "203.0.113.7", None, None, "2001:db8:1:2::a"])


class EmitWorkerTest(unittest.TestCase):
    """emit_worker.py: the deployment settings the Worker cannot honour are refused or left out."""

    def emit(self, root, gateway_extra, status=None, kit_status="emit"):
        """emit_static.py then emit_worker.py for a deployment of a shard ini (with its Server.Status URL when status is
        given). kit_status other than "emit" replaces the kit's shard.status before emit_worker.py reads it (None
        removes it)."""
        (root / "server.ini").write_text(ini(14617, status))
        secret = root / "join.key"
        join_token.init_secret(secret)
        doc = deployment_doc(root / "server.ini", secret, "https://page.example", "page.example", {"connections": 7})
        doc["gateway"].update(gateway_extra)
        (root / "deployment.json").write_text(json.dumps(doc))
        subprocess.run(
            [
                sys.executable,
                "-m",
                "plasma_web.emit_static",
                str(root / "deployment.json"),
                str(root / "kit"),
                "--gateway-url",
                "wss://page.example",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        if kit_status != "emit":
            page = json.loads((root / "kit/runtime/config.json").read_text())
            page["shard"].pop("status", None)
            if kit_status is not None:
                page["shard"]["status"] = kit_status
            (root / "kit/runtime/config.json").write_text(json.dumps(page))
        r = subprocess.run(
            [
                sys.executable,
                str(HERE / "emit_worker.py"),
                str(root / "deployment.json"),
                str(root / "kit"),
                str(root / "out"),
            ],
            capture_output=True,
            text=True,
        )
        secret.unlink()
        return r

    def test_emit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            r = self.emit(root, {})
            self.assertEqual(r.returncode, 0, r.stderr)
            cfg = json.loads((root / "out/wrangler.json").read_text())
            page = json.loads((root / "kit/runtime/config.json").read_text())
            self.assertEqual(
                sorted(cfg["vars"]["LIMITS"]),
                sorted(["connectionsPerIp", "connectsPerMinutePerIp", "bytesPerSecond", "idleSeconds", "connections"]),
            )
            self.assertEqual(cfg["vars"]["LIMITS"]["connections"], 7)
            self.assertEqual(cfg["vars"]["CONTENT_SECURITY_POLICY"], deployment_file.content_security_policy(page))
            self.assertEqual(
                sorted(b["class_name"] for b in cfg["durable_objects"]["bindings"]),
                ["Capacity", "IpSlots", "JoinNonces"],
            )
            self.assertNotIn("JOIN_KEY", json.dumps(cfg))
            self.assertFalse((root / "out/assets/_headers").exists())
            self.assertEqual(cfg["vars"]["STATUS_URL"], "")  # the ini names no Server.Status
            self.assertNotIn("status", page["shard"])

    def test_status_url(self):
        # The Worker relays the ini's Server.Status URL (STATUS_URL) at /runtime/status, the path the
        # kit's page document names; the page's policy names no origin for it. A kit whose shard.status is not this
        # deployment's (another deployment's kit, or one emitted before the relay) is refused.
        url = "http://status.example:8080/serverstatus"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            r = self.emit(root, {}, status=url)
            self.assertEqual(r.returncode, 0, r.stderr)
            cfg = json.loads((root / "out/wrangler.json").read_text())
            page = json.loads((root / "kit/runtime/config.json").read_text())
            self.assertEqual(page["shard"]["status"], "/runtime/status")
            self.assertEqual(cfg["vars"]["STATUS_URL"], url)
            self.assertEqual(json.loads(r.stdout)["status_url"], url)
            self.assertNotIn("status.example", cfg["vars"]["CONTENT_SECURITY_POLICY"])
        for status, kit_status in ((url, None), (url, url), (None, "/runtime/status")):
            with self.subTest(status=status, kit_status=kit_status), tempfile.TemporaryDirectory() as tmp:
                r = self.emit(Path(tmp), {}, status=status, kit_status=kit_status)
                self.assertEqual(r.returncode, 2, r.stdout)
                self.assertIn("emit the kit again from this deployment", r.stderr)

    def test_trusted_proxies_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = self.emit(Path(tmp), {"trustedProxies": ["192.0.2.0/24"]})
            self.assertEqual(r.returncode, 2)
            self.assertIn("gateway.trustedProxies does not apply to the Worker", r.stderr)


selected = os.environ.get("PLASMA_CONTRACT_BACKEND")


class GatewayPyContract(Contract, unittest.IsolatedAsyncioTestCase):
    backend = "gateway"


class WorkerContract(Contract, unittest.IsolatedAsyncioTestCase):
    backend = "worker"


if selected == "gateway":
    del WorkerContract
elif selected == "worker":
    del GatewayPyContract

if __name__ == "__main__":
    unittest.main()
