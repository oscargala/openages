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
"""Exercise the HTTP/WS boundary, the byte transport and the admission limits without contacting the shard."""

import asyncio
import io
import ipaddress
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from websockets.asyncio.client import connect
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed, InvalidStatus

from plasma_web import client_limits, deployment, gateway

# A documentation address (RFC 5737) stands for the shard: asyncio.open_connection is patched, nothing is contacted.
SHARD = ("192.0.2.10", 14617)
ROUTE = f"/connect?host={SHARD[0]}&port={SHARD[1]}"
ORIGIN = "http://127.0.0.1:8765"


def owned(trusted=(), **limits):
    """A gateway for the fake shard: one page origin, no join tokens, the deployment defaults with these limits."""
    return gateway.Gateway(
        name="test",
        upstreams=frozenset({SHARD}),
        origins=[ORIGIN],
        admission=gateway.Admission(
            None, dict(deployment.LIMIT_DEFAULTS, **limits), trusted=[ipaddress.ip_network(n) for n in trusted]
        ),
    )


class FakeSocket:
    """The upstream's socket, as far as the relay uses it: setsockopt, recorded with the bytes written before it."""

    def __init__(self, upstream):
        self.upstream, self.options = upstream, []

    def setsockopt(self, level, option, value):
        self.options.append((level, option, value, len(self.upstream.bytes)))


class FakeUpstream:
    def __init__(self):
        self.incoming = asyncio.StreamReader()
        self.bytes = bytearray()
        self.closed = False
        self.sock = FakeSocket(self)

    def write(self, data):
        self.bytes.extend(data)
        # TCP chunk boundaries may differ from websocket frame boundaries.
        self.incoming.feed_data(data[:1])
        self.incoming.feed_data(data[1:])

    async def drain(self):
        await asyncio.sleep(0)

    def get_extra_info(self, key):
        return self.sock if key == "socket" else None

    def close(self):
        self.closed = True
        self.incoming.feed_eof()

    async def wait_closed(self):
        pass


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.peers = []
        self.servers = []

        async def open_shard(host, port):
            self.assertEqual((host, port), SHARD)
            peer = FakeUpstream()
            self.peers.append(peer)
            return peer.incoming, peer

        self.mock = patch.object(gateway.asyncio, "open_connection", side_effect=open_shard)
        self.mock.start()
        self.base = await self.start(owned())

    async def start(self, gw):
        self.gw, self.log = gw, io.StringIO()
        server = await serve(gw.relay(self.log), "127.0.0.1", 0, **dict(gw.options(self.log), close_timeout=1))
        self.servers.append(server)
        return f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}"

    async def asyncTearDown(self):
        for server in self.servers:
            server.close()
            await server.wait_closed()
        self.mock.stop()
        self.assertTrue(all(p.closed for p in self.peers))

    def rows(self):
        return [json.loads(line) for line in self.log.getvalue().splitlines()]

    async def refused(self, url, **kw):
        with self.assertRaises(InvalidStatus) as cm:
            async with connect(url, origin=kw.pop("origin", ORIGIN), proxy=None, **kw):
                self.fail("refused request accepted")
        return cm.exception.response.status_code, cm.exception.response.body.decode().strip()

    async def test_reject_before_tcp(self):
        for route in [
            "/auth",
            "/gate",
            f"/auth?host={SHARD[0]}",
            f"/connect?host=192.0.2.11&port={SHARD[1]}",
            f"/connect?host={SHARD[0]}&port=80",
            f"/connect?host={SHARD[0]}&host=192.0.2.11&port={SHARD[1]}",
            f"{ROUTE}&x=1",
            "/connect",
            "/unknown",
        ]:
            with self.subTest(route=route):
                self.assertEqual((await self.refused(self.base + route))[0], 403)
        for origin in [None, "http://127.0.0.1:9999", "https://example.com"]:
            with self.subTest(origin=origin):
                self.assertEqual((await self.refused(self.base + ROUTE, origin=origin))[0], 403)
        self.assertEqual(self.peers, [])

    async def test_binary_stream(self):
        async with connect(self.base + ROUTE, origin=ORIGIN, proxy=None) as ws:
            chunks = [bytes([22, 1, 2]), bytes(range(256)), b"\0\xff\0"]
            for chunk in chunks:
                await ws.send(chunk)
            want = b"".join(chunks)
            got = b""
            while len(got) < len(want):
                got += await asyncio.wait_for(ws.recv(), 2)
            self.assertEqual(got, want)
            self.assertEqual(bytes(self.peers[-1].bytes), want)

    async def test_reject_text_and_wrong_service_handshake(self):
        for data, code in [("not binary", 1003), (bytes([99]), 1008), (b"", 1008)]:
            async with connect(self.base + ROUTE, origin=ORIGIN, proxy=None) as ws:
                await ws.send(data)
                with self.assertRaises(ConnectionClosed) as closed:
                    await ws.recv()
                self.assertEqual(closed.exception.rcvd.code, code)
                self.assertEqual(bytes(self.peers[-1].bytes), b"")

    # --- Nagle per connection type -------------------------------------------------------------------------------
    def test_no_delay_per_connection_type(self):
        # Natively only the game connection is opened unbuffered: plNglGame.cpp's
        # CliGmConn::AsyncNotifySocketConnectSuccess passes unbuffered=true to NetCliConnectAccept, the auth and
        # gatekeeper connections pass false, and the file connection never sets it.
        self.assertEqual(
            {t: gateway.no_delay(t) for t in sorted(gateway.SERVICE_TYPES)}, {10: False, 11: True, 16: False, 22: False}
        )
        self.assertLessEqual(gateway.UNBUFFERED_TYPES, gateway.SERVICE_TYPES)

    async def test_nagle_set_from_the_first_byte(self):
        for kind, value in [(10, 0), (11, 1), (16, 0), (22, 0)]:
            with self.subTest(kind=kind):
                async with connect(self.base + ROUTE, origin=ORIGIN, proxy=None) as ws:
                    for chunk in (bytes([kind, 1, 2]), b"\x03", bytes([11])):
                        await ws.send(chunk)
                    got = b""
                    while len(got) < 5:
                        got += await asyncio.wait_for(ws.recv(), 2)
                # set once, from the first byte only, before any byte reached the shard
                self.assertEqual(self.peers[-1].sock.options, [(socket.IPPROTO_TCP, socket.TCP_NODELAY, value, 0)])

    async def test_nagle_untouched_on_a_refused_first_frame(self):
        for data in ("not binary", bytes([99]), b""):
            async with connect(self.base + ROUTE, origin=ORIGIN, proxy=None) as ws:
                await ws.send(data)
                with self.assertRaises(ConnectionClosed):
                    await ws.recv()
            self.assertEqual(self.peers[-1].sock.options, [])

    # --- the client address behind a proxy -----------------------------------------------------------------------
    async def test_forged_client_ip_ignored_from_untrusted_peer(self):
        base = await self.start(owned(connectsPerMinutePerIp=1))
        async with connect(
            base + ROUTE, origin=ORIGIN, proxy=None, additional_headers={"CF-Connecting-IP": "198.51.100.7"}
        ):
            pass
        # a second forged address does not buy a second connect: the peer (127.0.0.1) is the key
        self.assertEqual(
            await self.refused(base + ROUTE, additional_headers={"CF-Connecting-IP": "198.51.100.8"}),
            (429, "Too many connections per minute from this address"),
        )
        await asyncio.sleep(0.1)
        self.assertEqual({r["ip"] for r in self.rows()}, {"127.0.0.1"})

    async def test_trusted_proxy_range_honours_client_ip(self):
        base = await self.start(owned(trusted=["127.0.0.0/8"], connectsPerMinutePerIp=1))
        for address in ["198.51.100.7", "198.51.100.8", "2001:db8:1:2::1"]:
            async with connect(
                base + ROUTE, origin=ORIGIN, proxy=None, additional_headers={"CF-Connecting-IP": address}
            ):
                pass
        seen = {
            "same address": await self.refused(base + ROUTE, additional_headers={"CF-Connecting-IP": "198.51.100.7"}),
            "same IPv6 /64": await self.refused(
                base + ROUTE, additional_headers={"CF-Connecting-IP": "2001:db8:1:2::ffff"}
            ),
            "no header": await self.refused(base + ROUTE),
            "two headers": await self.refused(
                base + ROUTE,
                additional_headers=[("CF-Connecting-IP", "198.51.100.9"), ("CF-Connecting-IP", "198.51.100.10")],
            ),
            "not an address": await self.refused(base + ROUTE, additional_headers={"CF-Connecting-IP": "evil"}),
        }
        self.assertEqual(
            seen,
            {
                "same address": (429, "Too many connections per minute from this address"),
                "same IPv6 /64": (429, "Too many connections per minute from this address"),
                "no header": (400, "Client address missing from the proxy"),
                "two headers": (400, "Client address missing from the proxy"),
                "not an address": (400, "Client address missing from the proxy"),
            },
        )
        await asyncio.sleep(0.1)
        relayed = [r["ip"] for r in self.rows() if "sent" in r]
        self.assertEqual(relayed, ["198.51.100.7", "198.51.100.8", "2001:db8:1:2::1"])
        refused = [r["ip"] for r in self.rows() if "refused" in r]
        self.assertEqual(refused, ["198.51.100.7", "2001:db8:1:2::ffff", "127.0.0.1", "127.0.0.1", "127.0.0.1"])

    def test_ip_key(self):
        self.assertEqual(gateway.ip_key("198.51.100.7"), "198.51.100.7")
        self.assertEqual(gateway.ip_key("::ffff:198.51.100.7"), "198.51.100.7")
        self.assertEqual(gateway.ip_key("2001:db8:1:2:3:4:5:6"), "2001:db8:1:2::/64")
        self.assertEqual(gateway.ip_key("2001:db8:1:2::9"), gateway.ip_key("2001:db8:1:2:ffff::1"))
        self.assertNotEqual(gateway.ip_key("2001:db8:1:2::9"), gateway.ip_key("2001:db8:1:3::9"))

    # --- bounded rate tables -------------------------------------------------------------------------------------
    def test_rate_table_is_bounded_lru_with_expiry(self):
        now = [0.0]
        table = client_limits.RateTable(2, window=60, entries=100, clock=lambda: now[0])
        for i in range(10000):  # rotating addresses: the table never grows past 100
            self.assertTrue(table.hit(f"k{i}"))
            self.assertLessEqual(len(table), 100)
            now[0] += 0.001
        self.assertEqual(len(table), 100)
        self.assertNotIn("k9899", table.table)  # least recently counted dropped first
        self.assertIn("k9900", table.table)
        self.assertTrue(table.hit("k9900"))
        self.assertFalse(table.hit("k9900"))  # two per window
        self.assertEqual(list(table.table)[-1], "k9900")  # a counted key moves to the back
        self.assertFalse(table.hit("k9900"))  # a refusal adds nothing
        self.assertEqual(len(table.table["k9900"]), 2)
        now[0] += 61  # every key's window has passed: all expire
        self.assertTrue(table.hit("fresh"))
        self.assertEqual(list(table.table), ["fresh"])
        zero = client_limits.RateTable(0, entries=100, clock=lambda: now[0])
        self.assertFalse(zero.hit("a"))  # a limit of 0 refuses and stores nothing
        self.assertEqual(len(zero), 0)

    def test_admission_rate_table_bounded(self):
        a = gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS), entries=50)
        for i in range(5000):
            a.rate_ok(f"198.51.{i // 256}.{i % 256}")
        self.assertEqual(len(a.connects), 50)
        self.assertEqual(len(gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS)).connects.table), 0)
        self.assertEqual(
            gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS)).connects.table.entries, gateway.RATE_TABLE_ENTRIES
        )

    def test_connect_rate_is_the_shared_client_rate(self):
        # the gateway counts its connects with client_limits.ClientRate, the helper the origin server's
        # join issuer counts its tokens with: the client address behind the deployment's trusted proxies, and the
        # bounded table.
        self.assertIs(gateway.ip_key, client_limits.ip_key)
        trusted = [ipaddress.ip_network("127.0.0.1/32")]
        a = gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS), trusted=trusted)
        self.assertIsInstance(a.connects, client_limits.ClientRate)
        self.assertEqual(a.connects.trusted, tuple(trusted))
        self.assertEqual(a.connects.client("127.0.0.1", ["203.0.113.7"]), ("203.0.113.7", None))
        self.assertEqual(a.connects.client("198.51.100.1", ["203.0.113.7"]), ("198.51.100.1", None))
        self.assertEqual(a.connects.client("127.0.0.1", []), ("127.0.0.1", client_limits.NO_CLIENT_ADDRESS))

    # --- global connection and memory caps -----------------------------------------------------------------------
    async def cap_case(self, base):
        a = await connect(base + ROUTE, origin=ORIGIN, proxy=None)
        b = await connect(base + ROUTE, origin=ORIGIN, proxy=None)
        third = await self.refused(base + ROUTE)
        await a.close()
        await asyncio.sleep(0.2)
        c = await connect(base + ROUTE, origin=ORIGIN, proxy=None)
        await b.close()
        await c.close()
        await asyncio.sleep(0.2)
        self.assertEqual((self.gw.admission.total, dict(self.gw.admission.active)), (0, {}))
        return third

    async def test_global_connection_cap(self):
        base = await self.start(owned(connections=2))
        self.assertEqual(self.gw.admission.capacity, 2)
        self.assertEqual(await self.cap_case(base), (503, "Gateway at capacity"))
        self.assertEqual(len(self.peers), 3)  # the refused handshake opened no shard connection

    async def test_global_memory_cap(self):
        base = await self.start(owned(memoryBytes=3 * gateway.CONNECTION_BUFFER_BYTES - 1))
        self.assertEqual(self.gw.admission.capacity, 2)
        self.assertEqual(await self.cap_case(base), (503, "Gateway at capacity"))
        self.assertEqual(len(self.peers), 3)

    def test_default_caps(self):
        a = gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS))
        self.assertEqual(gateway.CONNECTION_BUFFER_BYTES, 18219008)  # the number deployment.py's comment states
        self.assertEqual(a.capacity, 58)  # 1 GiB of buffers, under the 64-connection cap
        self.assertEqual(gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS, connections=0)).capacity, 0)

    # --- the command line: a deployment is required --------------------------------------------------------------
    def test_deployment_required_and_loopback_origin_only(self):
        gw = ["-m", "plasma_web.gateway"]  # the command, python -m with the package on the import path
        r = subprocess.run([sys.executable, *gw, "--port", "0"], capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 2)
        self.assertIn("--deployment", r.stderr)
        for legacy in (
            ["--shard-host", "192.0.2.10"],
            ["--bind", "0.0.0.0"],
            ["--public-host", "a.example"],
            ["--tls-cert", "x"],
        ):
            with self.subTest(legacy=legacy):
                r = subprocess.run([sys.executable, *gw, *legacy], capture_output=True, text=True, timeout=30)
                self.assertEqual(r.returncode, 2)
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / "server.ini"
            key = "A" * 86 + "=="
            ini.write_text(
                f'Server.Auth.Host "127.0.0.1"\n'
                + "".join(f'Server.{s}.{k} "{key}"\n' for s in ("Auth", "Game", "Gate") for k in ("N", "X"))
            )
            dep = Path(tmp) / "d.json"
            dep.write_text(
                json.dumps(
                    {
                        "schema": 1,
                        "serverIni": str(ini),
                        "data": {"files": "patcher", "gameCode": "server"},
                        "gateway": {"resolveUpstreams": False},
                        "capabilities": {"harness": True},
                    }
                )
            )
            for origin in ("https://example.com", "http://192.0.2.1:8790"):
                with self.subTest(origin=origin):
                    r = subprocess.run(
                        [sys.executable, *gw, "--deployment", str(dep), "--port", "0", "--origin", origin],
                        capture_output=True,
                        text=True,
                        timeout=30,
                    )
                    self.assertEqual(r.returncode, 2)
                    self.assertIn("--origin must be a loopback origin", r.stderr)


class NagleSocketTests(unittest.IsolatedAsyncioTestCase):
    """TCP_NODELAY on a real loopback TCP connection to a local listener standing in for the shard."""

    async def test_nagle_on_a_real_socket(self):
        headers = asyncio.Queue()

        async def shard(reader, writer):
            await headers.put(await reader.read(64))
            await reader.read()  # until the gateway closes
            writer.close()

        listener = await asyncio.start_server(shard, "127.0.0.1", 0)
        port = listener.sockets[0].getsockname()[1]
        gw = gateway.Gateway(
            name="test",
            upstreams=frozenset({("127.0.0.1", port)}),
            origins=[ORIGIN],
            admission=gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS)),
        )
        writers, real_open = [], asyncio.open_connection

        async def open_and_keep(host, port):
            reader, writer = await real_open(host, port)
            writers.append(writer)
            return reader, writer

        log = io.StringIO()
        with patch.object(gateway.asyncio, "open_connection", side_effect=open_and_keep):
            server = await serve(gw.relay(log), "127.0.0.1", 0, **dict(gw.options(log), close_timeout=1))
            url = f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}/connect?host=127.0.0.1&port={port}"
            try:
                for kind, nodelay in [(10, False), (11, True), (16, False), (22, False)]:
                    with self.subTest(kind=kind):
                        async with connect(url, origin=ORIGIN, proxy=None) as ws:
                            sock = writers[-1].get_extra_info("socket")
                            # asyncio's transport starts with Nagle off (base_events._set_nodelay)
                            self.assertTrue(sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY))
                            await ws.send(bytes([kind]) + b"header")
                            self.assertEqual(await asyncio.wait_for(headers.get(), 2), bytes([kind]) + b"header")
                            self.assertEqual(bool(sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY)), nodelay)
            finally:
                server.close()
                await server.wait_closed()
                listener.close()
                await listener.wait_closed()


if __name__ == "__main__":
    unittest.main()
