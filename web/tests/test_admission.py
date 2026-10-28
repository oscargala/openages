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
"""Gateway admission (join tokens, per-IP limits, byte rate, idle timeout, logs) and the page origin's /runtime/join
endpoint, end to end through the real gateway.py and server.py processes, against a local fake upstream (no shard
contact). Run with a Python that has the websockets package: python -m unittest test_admission"""

import asyncio
import base64
import json
import secrets
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidStatus

from plasma_web import join_token

ORIGIN = "http://127.0.0.1:9"


def ini(port):
    key = lambda: base64.b64encode(secrets.token_bytes(64)).decode()
    return (
        "\n".join(
            [
                f'Server.Auth.Host "127.0.0.1"',
                f"Server.Port {port}",
                *(f'Server.{s}.{k} "{key()}"' for s in ("Auth", "Game", "Gate") for k in ("N", "X")),
                "Server.Auth.G 41",
                "Server.Game.G 73",
                "Server.Gate.G 4",
            ]
        )
        + "\n"
    )


class Admission(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="admission-test-"))
        self.sent = bytearray()

        async def upstream(reader, writer):
            # echo, plus 256 KiB on request byte 0xEE (the byte-rate case)
            while data := await reader.read(65536):
                self.sent.extend(data)
                writer.write(b"\x00" * 262144 if data[-1:] == b"\xee" else data)
                await writer.drain()
            writer.close()

        self.upstream = await asyncio.start_server(upstream, "127.0.0.1", 0)
        self.port = self.upstream.sockets[0].getsockname()[1]
        (self.tmp / "server.ini").write_text(ini(self.port))
        self.secret_file = self.tmp / "join.key"
        join_token.init_secret(self.secret_file)
        self.secret = join_token.read_secret(self.secret_file)
        self.procs = []

    async def asyncTearDown(self):
        for p in self.procs:
            p.terminate()
            p.wait(5)
        self.upstream.close()

    def deployment(self, name="admission-test", **limits):
        path = self.tmp / f"{name}-{len(self.procs)}.json"
        path.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "name": name,
                    "serverIni": str(self.tmp / "server.ini"),
                    "data": {"files": "patcher", "gameCode": "server"},
                    "gateway": {"origins": [], "join": {"secretFile": str(self.secret_file)}, "limits": limits},
                    "capabilities": {"harness": True},
                }
            )
        )
        return path

    def gateway(self, **limits):
        dep = self.deployment(**limits)
        port_file, self.log = self.tmp / f"port{len(self.procs)}", self.tmp / f"gw{len(self.procs)}.jsonl"
        p = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "plasma_web.gateway",
                "--deployment",
                str(dep),
                "--port",
                "0",
                "--port-file",
                str(port_file),
                "--origin",
                ORIGIN,
                "--log",
                str(self.log),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        self.procs.append(p)
        deadline = time.time() + 15
        while not (port_file.exists() and port_file.read_text().strip()):
            self.assertIsNone(p.poll(), "gateway exited")
            self.assertLess(time.time(), deadline)
            time.sleep(0.05)
        return f"ws://127.0.0.1:{port_file.read_text().strip()}/connect?host=127.0.0.1&port={self.port}"

    def token(self, deployment="admission-test", ttl=60, now=None):
        return join_token.issue(self.secret, deployment, ttl, now)[0]

    def open(self, url, token=None, origin=ORIGIN):
        protocols = ["plasma"] + ([f"plasma.join.{token}"] if token else [])
        return connect(url, origin=origin, subprotocols=protocols, compression=None, open_timeout=5)

    async def refused(self, url, token=None, origin=ORIGIN):
        with self.assertRaises(InvalidStatus) as cm:
            async with self.open(url, token, origin):
                pass
        return cm.exception.response.status_code, cm.exception.response.body.decode().strip()

    def lines(self):
        return [json.loads(x) for x in self.log.read_text().splitlines()] if self.log.exists() else []

    async def test_tokens(self):
        url = self.gateway()
        tok = self.token()
        async with self.open(url, tok) as ws:
            self.assertEqual(ws.subprotocol, "plasma")
            await ws.send(b"\x0aping")
            self.assertEqual(await ws.recv(), b"\x0aping")
        self.assertEqual(await self.refused(url), (401, "Join token required"))
        self.assertEqual(await self.refused(url, tok), (401, "Join token already used"))
        self.assertEqual(await self.refused(url, self.token(now=time.time() - 120)), (401, "Join token expired"))
        self.assertEqual(await self.refused(url, self.token("another")), (403, "Join token for another deployment"))
        bad = tok[:-2] + ("AA" if tok[-2:] != "AA" else "BB")
        self.assertEqual(await self.refused(url, bad), (401, "Invalid join token"))
        self.assertEqual(await self.refused(url, self.token(now=time.time() + 3600)), (401, "Invalid join token"))
        other = url.replace("host=127.0.0.1", "host=203.0.113.3")
        fresh = self.token()
        self.assertEqual((await self.refused(other, fresh))[0], 403)  # not allowlisted: token not spent
        self.assertEqual(await self.refused(url, fresh, "http://127.0.0.1:10"), (403, "Origin not allowed"))
        async with self.open(url, fresh):  # still unspent after both refusals
            pass
        await asyncio.sleep(0.3)
        rows = self.lines()
        text = self.log.read_text()
        self.assertNotIn(tok.split(".")[2], text)
        self.assertNotIn(fresh.split(".")[2], text)
        done = [r for r in rows if "sent" in r]
        self.assertEqual(len(done), 2)
        self.assertEqual(done[0]["upstream"], f"127.0.0.1:{self.port}")
        self.assertEqual(done[0]["ip"], "127.0.0.1")
        self.assertEqual(done[0]["sent"], 5)
        self.assertEqual(sorted(r["refused"] for r in rows if "refused" in r), [401, 401, 401, 401, 401, 403, 403, 403])
        self.assertTrue(all(r["ip"] == "127.0.0.1" for r in rows))

    async def test_concurrency(self):
        url = self.gateway(connectionsPerIp=2)
        a = await self.open(url, self.token())
        b = await self.open(url, self.token())
        self.assertEqual(
            await self.refused(url, self.token()), (429, "Too many concurrent connections from this address")
        )
        await a.close()
        await asyncio.sleep(0.3)
        c = await self.open(url, self.token())
        await b.close()
        await c.close()

    async def test_rate(self):
        url = self.gateway(connectsPerMinutePerIp=3)
        for _ in range(3):
            async with self.open(url, self.token()):
                pass
        self.assertEqual(
            await self.refused(url, self.token()), (429, "Too many connections per minute from this address")
        )
        self.assertEqual(await self.refused(url), (429, "Too many connections per minute from this address"))

    async def test_idle(self):
        url = self.gateway(idleSeconds=1)
        async with self.open(url, self.token()) as ws:
            await ws.send(b"\x0ax")
            await ws.recv()
            t = time.monotonic()
            with self.assertRaises(ConnectionClosed) as cm:
                await ws.recv()
            self.assertEqual(cm.exception.rcvd.code, 4000)
            self.assertGreater(time.monotonic() - t, 0.8)
        await asyncio.sleep(0.3)
        self.assertEqual([r["end"] for r in self.lines() if "sent" in r], ["idle timeout"])

    async def test_byte_rate(self):
        url = self.gateway(bytesPerSecond=65536)
        async with self.open(url, self.token()) as ws:
            t = time.monotonic()
            await ws.send(b"\x0a\xee")
            got = 0
            while got < 262144 + 0:
                got += len(await ws.recv())
            # one second of burst, then 65536 B/s: 262144 B take at least ~3 s
            self.assertGreater(time.monotonic() - t, 2.5)

    def page_server(self, dep, port, *extra):
        build = self.tmp / f"build{len(self.procs)}"
        build.mkdir()
        p = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "plasma_web.server",
                "--deployment",
                str(dep),
                "--gateway-url",
                "ws://127.0.0.1:9",
                "--port",
                str(port),
                "--access-log",
                str(self.tmp / "access.jsonl"),
                "--build-dir",
                str(build),
                *extra,
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        self.procs.append(p)
        base = f"http://127.0.0.1:{port}"

        def post(origin=base, ctype="application/json", path="/runtime/join", host=None):
            headers = {"Origin": origin, "Content-Type": ctype, **({"Host": host} if host else {})}
            req = urllib.request.Request(base + path, data=b"{}", method="POST", headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=5) as r:
                    return r.status, json.loads(r.read())
            except urllib.error.HTTPError as e:
                return e.code, None

        deadline = time.time() + 30
        while True:
            try:
                with urllib.request.urlopen(base + "/runtime/config.json", timeout=2) as r:
                    return json.loads(r.read()), post
            except OSError:
                self.assertIsNone(p.poll(), "server exited")
                self.assertLess(time.time(), deadline)
                time.sleep(0.2)

    def test_join_origin_from_the_deployment(self):
        # A deployment that lists its page origins: only those, whatever Host the request names.
        port = 18000 + secrets.randbelow(2000)
        listed = f"http://localhost:{port}"
        path = self.tmp / "listed.json"
        doc = json.loads(self.deployment().read_text())
        doc["gateway"]["origins"] = [listed]
        path.write_text(json.dumps(doc))
        _, post = self.page_server(path, port)
        self.assertEqual(post(origin=listed)[0], 200)
        self.assertEqual(post(origin=f"http://127.0.0.1:{port}")[0], 403)  # this server's own origin, not listed
        self.assertEqual(post(origin="http://evil.example", host="evil.example")[0], 403)  # Origin = Host: refused
        # Without listed origins, the loopback origins of the bound port only; a forged Host does not widen them.
        port = 20000 + secrets.randbelow(2000)
        _, post = self.page_server(self.deployment(), port)
        self.assertEqual(post(origin=f"http://localhost:{port}")[0], 200)
        self.assertEqual(post(origin="http://evil.example", host="evil.example")[0], 403)
        self.assertEqual(post(origin=f"http://127.0.0.1:{port + 1}", host=f"127.0.0.1:{port + 1}")[0], 403)
        # ... and only on a loopback bind: elsewhere the page origin must come from the deployment (refused at start).
        r = subprocess.run(
            [
                sys.executable,
                "-m",
                "plasma_web.server",
                "--deployment",
                str(self.deployment()),
                "--gateway-url",
                "ws://127.0.0.1:9",
                "--port",
                "0",
                "--bind",
                "0.0.0.0",
                "--access-log",
                str(self.tmp / "refused.jsonl"),
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(r.returncode, 2)
        self.assertIn("loopback bind only", r.stderr)

    def test_token_expiry_must_be_an_integer(self):
        v = join_token.Verifier(self.secret, "admission-test", 60)

        def signed(claims):
            payload = join_token._b64(json.dumps(claims, separators=(",", ":")).encode())
            return f"v1.{payload}.{join_token._b64(join_token._mac(self.secret, payload))}"

        for e in (True, False, float(int(time.time()) + 30), str(int(time.time()) + 30)):
            with self.subTest(e=e), self.assertRaises(join_token.Refusal) as cm:
                v.check(signed({"d": "admission-test", "e": e, "n": "n" + str(e)}))
            self.assertEqual((cm.exception.status, cm.exception.reason), (401, "Invalid join token"))
        v.check(signed({"d": "admission-test", "e": int(time.time()) + 30, "n": "int"}))  # an int expiry is accepted

    def test_spent_nonces_are_kept_until_expiry(self):
        # a spent nonce is kept while its token is unexpired (its replay is refused), and the check
        # after its expiry drops it, from the front of the expiry heap: the set never grows past the unexpired tokens.
        now = [1_000_000.0]
        v = join_token.Verifier(self.secret, "admission-test", 60, clock=lambda: now[0])
        first = [join_token.issue(self.secret, "admission-test", 60, now[0])[0] for _ in range(300)]
        for t in first:
            v.check(t)
        with self.assertRaises(join_token.Refusal) as cm:
            v.check(first[0])
        self.assertEqual(cm.exception.reason, "Join token already used")
        now[0] += 30
        later = [join_token.issue(self.secret, "admission-test", 60, now[0])[0] for _ in range(5)]
        for t in later[:4]:
            v.check(t)
        self.assertEqual((len(v.spent), len(v.expiries)), (304, 304))  # all unexpired: all kept
        now[0] += 31  # the first 300 have expired
        v.check(later[4])
        self.assertEqual((len(v.spent), len(v.expiries)), (5, 5))
        self.assertEqual(sorted(e for e, _ in v.expiries), sorted(v.spent.values()))
        with self.assertRaises(join_token.Refusal) as cm:
            v.check(first[1])
        self.assertEqual(cm.exception.reason, "Join token expired")

    def test_join_endpoint(self):
        dep = self.deployment(connectsPerMinutePerIp=2)
        port = 18000 + secrets.randbelow(2000)
        doc, post = self.page_server(dep, port)
        self.assertEqual(doc["join"], "/runtime/join")
        self.assertEqual(post(origin="http://evil.example")[0], 403)
        self.assertEqual(post(ctype="text/plain")[0], 403)
        status, body = post()
        self.assertEqual(status, 200)
        v = join_token.Verifier(self.secret, "admission-test", 60)
        v.check(body["token"])
        self.assertLessEqual(body["expires"], time.time() + 61)
        for _ in range(4):
            status, _ = post()
        self.assertEqual(status, 429)  # 2 x connectsPerMinutePerIp = 4 per minute
        self.assertEqual(
            post(origin="http://evil.example", host="evil.example")[0], 403
        )  # Host does not set the origin
        self.assertNotIn(body["token"], (self.tmp / "access.jsonl").read_text())


if __name__ == "__main__":
    unittest.main()
