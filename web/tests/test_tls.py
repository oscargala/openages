#!/usr/bin/env python3
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
"""Real owned HTTPS/WSS CLI servers, verified private fixture CA; no trust installation."""

import asyncio
import base64
import hashlib
import json
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import unittest
import urllib.request
from websockets.asyncio.client import connect
from websockets.exceptions import InvalidStatus

OPENSSL = shutil.which("openssl")


@unittest.skipIf(OPENSSL is None, "openssl is not installed: the fixture certificate cannot be made")
class TLSDeployment(unittest.IsolatedAsyncioTestCase):
    folders = []  # every fixture folder a test made (FixtureFolder checks that each one is gone)

    async def asyncSetUp(self):
        # the fixture's own folder, in the system's temporary directory: it holds the fixture's private key, so it goes when
        # the test ends (a cleanup runs after asyncTearDown has stopped the servers, and also when asyncSetUp fails)
        self.out = Path(tempfile.mkdtemp(prefix="web-tests-tls-"))
        self.folders.append(self.out)
        self.addCleanup(shutil.rmtree, self.out, ignore_errors=True)
        self.children = []
        self.logs = []
        self.peers = []
        # the servers this test starts stop with it: after asyncTearDown, and also when asyncSetUp fails
        self.addCleanup(self.stop_children)
        self.cert = self.out / "fixture.crt"
        key = self.out / "fixture.key"
        subprocess.run(
            [
                OPENSSL,
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost,IP:127.0.0.1",
                "-keyout",
                str(key),
                "-out",
                str(self.cert),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        key.chmod(0o600)
        self.trusted = ssl.create_default_context(cafile=str(self.cert))
        self.echo = await asyncio.start_server(self.relay, "127.0.0.1", 0)
        self.upstream = self.echo.sockets[0].getsockname()[1]
        ports = self.out / "gateway.port"
        # the gateway's TLS, origin and upstream come from a loopback harness deployment (fixture keys in its server.ini)
        ini = self.out / "server.ini"
        fixture_key = base64.b64encode(bytes(range(64))).decode()
        ini.write_text(
            f'Server.Auth.Host "127.0.0.1"\nServer.Port {self.upstream}\n'
            + "".join(f'Server.{s}.{k} "{fixture_key}"\n' for s in ("Auth", "Game", "Gate") for k in ("N", "X"))
        )
        dep = self.out / "deployment.json"
        dep.write_text(
            json.dumps(
                {
                    "schema": 1,
                    "name": "tls-fixture",
                    "serverIni": str(ini),
                    "data": {"files": "patcher", "gameCode": "server"},
                    "gateway": {
                        "origins": ["https://localhost:9443"],
                        "resolveUpstreams": False,
                        "tlsCert": str(self.cert),
                        "tlsKey": str(key),
                    },
                    "capabilities": {"harness": True},
                }
            )
        )
        self.spawn(
            [
                sys.executable,
                "-m",
                "plasma_web.gateway",
                "--deployment",
                str(dep),
                "--port",
                "0",
                "--port-file",
                str(ports),
            ],
            "gateway",
        )
        for _ in range(100):
            if ports.exists() and ports.read_text().strip():
                break
            await asyncio.sleep(0.05)
        self.base = f"wss://127.0.0.1:{int(ports.read_text())}"
        assets = self.out / "assets"
        (assets / "sfx").mkdir(parents=True)
        (assets / "sfx/probe").write_bytes(bytes(range(256)))
        (assets / "server.ini").write_text("public fixture only")
        config = self.out / "config.json"
        config.write_text('{"schema":1,"fixture":"public"}')
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.http_port = sock.getsockname()[1]
        self.spawn(
            [
                sys.executable,
                "-m",
                "plasma_web.server",
                "--port",
                str(self.http_port),
                "--data-root",
                str(assets),
                "--build-dir",
                str(self.out),
                "--runtime-config",
                str(config),
                "--access-log",
                str(self.out / "access.jsonl"),
                "--tls-cert",
                str(self.cert),
                "--tls-key",
                str(key),
            ],
            "https",
        )
        self.url = f"https://127.0.0.1:{self.http_port}"
        for _ in range(100):
            try:
                await asyncio.to_thread(self.get, "/runtime/config.json")
                break
            except OSError:
                await asyncio.sleep(0.05)

    async def relay(self, reader, writer):
        self.peers.append(writer)
        try:
            while data := await reader.read(65536):
                writer.write(data)
                await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()

    def spawn(self, args, label):
        log = (self.out / (label + ".log")).open("w")
        self.logs.append(log)
        child = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
        self.children.append(child)

    def stop_children(self):
        for child in self.children:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        for log in self.logs:
            log.close()

    def get(self, path, headers=None, context=None):
        with urllib.request.urlopen(
            urllib.request.Request(self.url + path, headers=headers or {}), context=context or self.trusted, timeout=5
        ) as r:
            return r.status, dict(r.headers), r.read()

    async def asyncTearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=5)
        self.echo.close()
        await self.echo.wait_closed()
        for peer in self.peers:
            peer.close()
            await peer.wait_closed()
        for log in self.logs:
            log.close()

    async def test_verified_wss_bytes_and_operator_target(self):
        route = f"/connect?host=127.0.0.1&port={self.upstream}"
        async with connect(self.base + route, origin="https://localhost:9443", ssl=self.trusted, proxy=None) as ws:
            expected = bytes([22]) + bytes(range(256)) * 257
            await ws.send(expected)
            got = b""
            while len(got) < len(expected):
                got += await asyncio.wait_for(ws.recv(), 5)
            self.assertEqual(got, expected)
        self.assertEqual(len(self.peers), 1)

    async def test_untrusted_and_wrong_hostname_certificates_fail(self):
        for context, hostname in [(ssl.create_default_context(), None), (self.trusted, "wrong.invalid")]:
            with self.assertRaises(ssl.SSLCertVerificationError):
                async with connect(
                    self.base + "/auth",
                    origin="https://localhost:9443",
                    ssl=context,
                    server_hostname=hostname,
                    proxy=None,
                ):
                    self.fail("Bad certificate accepted")
        self.assertFalse(self.peers)

    async def test_origin_and_wrong_target_fail_before_tcp(self):
        for route, origin in [
            ("/auth", "https://wrong.invalid"),
            ("/connect?host=198.51.100.175&port=14617", "https://localhost:9443"),
        ]:
            with self.assertRaises(InvalidStatus):
                async with connect(self.base + route, origin=origin, ssl=self.trusted, proxy=None):
                    self.fail("Unauthorized request accepted")
        self.assertFalse(self.peers)

    async def test_verified_https_config_and_conditional_sound_ranges(self):
        status, headers, body = await asyncio.to_thread(self.get, "/runtime/config.json")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), {"schema": 1, "fixture": "public"})
        self.assertEqual(headers["Cache-Control"], "no-store")
        status, headers, body = await asyncio.to_thread(self.get, "/sound/probe", {"Range": "bytes=8-31"})
        self.assertEqual((status, body), (206, bytes(range(8, 32))))
        self.assertEqual(headers["Content-Range"], "bytes 8-31/256")
        self.assertEqual(headers["ETag"], '"sha256-' + hashlib.sha256(bytes(range(256))).hexdigest() + '"')
        with self.assertRaises(urllib.error.HTTPError) as failed:
            await asyncio.to_thread(self.get, "/sound/probe", {"Range": "bytes=8-31", "If-Match": '"wrong"'})
        self.assertEqual(failed.exception.code, 412)
        failed.exception.close()
        with self.assertRaises(urllib.error.URLError):
            await asyncio.to_thread(self.get, "/runtime/config.json", None, ssl.create_default_context())

    async def test_gateway_sends_no_session_tickets(self):
        # the gateway sends no TLS 1.3 NewSessionTicket (gateway.py tls.num_tickets = 0). Tickets would arrive right
        # after the handshake, before the answer, while the client writes its upgrade request; a client that reads and writes
        # one TLS socket from two threads (websockets.sync) then processes them concurrently with its write and can lose the
        # request, send internal_error or crash.
        port = int(self.base.rsplit(":", 1)[1])

        def probe():
            with (
                socket.create_connection(("127.0.0.1", port), timeout=5) as raw,
                self.trusted.wrap_socket(raw, server_hostname="127.0.0.1") as s,
            ):
                s.sendall(
                    f"GET /auth HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: https://wrong.invalid\r\n\r\n".encode()
                )
                data = b""
                while b"\r\n\r\n" not in data and (chunk := s.recv(4096)):
                    data += chunk
                return s.version(), s.session is not None and s.session.has_ticket, data.split(b"\r\n", 1)[0]

        version, ticket, status = await asyncio.to_thread(probe)
        self.assertEqual(version, "TLSv1.3")
        self.assertTrue(status.startswith(b"HTTP/1.1 403"), status)
        self.assertFalse(ticket, "a session ticket was sent")
        self.assertFalse(self.peers)


@unittest.skipIf(OPENSSL is None, "openssl is not installed: the fixture certificate cannot be made")
class FixtureFolder(unittest.TestCase):
    def test_private_key_folder_removed(self):
        # one TLS test end to end leaves no fixture folder (fixture.key, the gateway's deployment) behind
        made = len(TLSDeployment.folders)
        result = unittest.TestResult()
        TLSDeployment("test_gateway_sends_no_session_tickets").run(result)
        self.assertTrue(result.wasSuccessful(), result.errors + result.failures)
        self.assertEqual(len(TLSDeployment.folders), made + 1)
        self.assertFalse(TLSDeployment.folders[-1].exists(), TLSDeployment.folders[-1])


if __name__ == "__main__":
    unittest.main()
