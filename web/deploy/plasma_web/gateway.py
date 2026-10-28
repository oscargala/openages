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
"""Binary WebSocket relay from the browser client to one deployment's shard; no protocol translation.

Every setting comes from the operator's deployment file (deployment.py, --deployment): the shard's
hosts and port from its server.ini, and the bind address, port, page origins, Host authorities, TLS, join key, trusted
proxy ranges and limits from its gateway section."""

import argparse
import asyncio
import collections
import http
import ipaddress
from functools import partial
import json
import logging
from pathlib import Path
import socket
import ssl
import time
from urllib.parse import parse_qs, urlsplit
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

from plasma_web import deployment as deployment_file
from plasma_web import join_token  # the admission tokens
from plasma_web import origins  # the page-origin rule

# The per-client limits the origin server's /runtime/join shares (client_limits.py)
from plasma_web.client_limits import CLIENT_IP_HEADER, RATE_TABLE_ENTRIES, ClientRate, ip_key

# The first byte a Plasma client sends on a connection is its connection type (pnNpCommon.h EConnType): auth 10,
# game 11, file 16, gatekeeper 22. A connection that starts with any other byte is closed before it reaches the shard.
SERVICE_TYPES = frozenset({10, 11, 16, 22})
# The connection types the native client opens unbuffered, with Nagle's algorithm off: the game connection only.
# NetCliConnectAccept (pnNcCli.cpp) calls AsyncSocketEnableNagling(sock, !unbuffered), which pnAcSocket.cpp's
# AsyncSocketEnableNagling applies as asio's tcp::no_delay(!enable). The connections' AsyncNotifySocketConnectSuccess
# passes unbuffered: true in plNglGame.cpp (CliGmConn), false in plNglAuth.cpp (CliAuConn) and plNglGateKeeper.cpp
# (CliGkConn). plNglFile.cpp's (CliFileConn) opens no NetCli and never sets it, so the file connection keeps the
# system default, Nagle on.
UNBUFFERED_TYPES = frozenset({11})
CONNECT_TIMEOUT = 4  # inside the server's 5 s open_timeout, which bounds the whole handshake
DEFAULT_PORT = 8766  # when neither --port nor the deployment's gateway.port names one

# The WebSocket transport settings, and the buffer bytes they allow one connection: MAX_QUEUE received messages, the
# one being parsed and one transport read (from the browser); the send buffer and the message being sent (to the
# browser); the upstream reader's buffer (asyncio pauses it above twice its limit) and the upstream write buffer
# (asyncio's high-water mark). The deployment's limits.memoryBytes admits memoryBytes // CONNECTION_BUFFER_BYTES
# connections at once. Python object overhead is not counted.
MAX_SIZE = 1024 * 1024
MAX_QUEUE = 16
WRITE_LIMIT = 65536
READ_CHUNK = 65536
STREAM_LIMIT = 65536
DRAIN_LIMIT = 65536
CONNECTION_BUFFER_BYTES = (
    (MAX_QUEUE + 1) * MAX_SIZE + READ_CHUNK + WRITE_LIMIT + READ_CHUNK + 2 * STREAM_LIMIT + DRAIN_LIMIT
)


def upstream_target(path, upstreams, networks=None):
    """The operator's (host, port) entry a /connect request may open.

    upstreams is the set derived server-side from the shard's own server.ini (deployment.py); the
    returned target is the allowlist's own entry, never the query's strings."""
    url = urlsplit(path)
    if url.path != "/connect" or url.fragment or url.scheme or url.netloc:
        raise ValueError("Unknown shard service")
    query = parse_qs(url.query, strict_parsing=True, keep_blank_values=True)
    if set(query) != {"host", "port"} or any(len(v) != 1 for v in query.values()):
        raise ValueError("Only the authorized test shard is permitted")
    for entry in upstreams:
        if query == {"host": [entry[0]], "port": [str(entry[1])]}:
            return entry
    # A deployment's game-server ranges (extraUpstreamNetworks): an IPv4 literal inside one, at the
    # client port; the TCP target is the parsed address, never the query's text.
    if networks and query["port"] == [str(networks[0])]:
        try:
            address = ipaddress.IPv4Address(query["host"][0])
        except ValueError:
            address = None
        if address is not None and str(address) == query["host"][0] and any(address in n for n in networks[1]):
            return str(address), networks[0]
    raise ValueError("Only the authorized test shard is permitted")


class Admission:
    """The deployment's admission state: join tokens (join_token.Verifier, or None for a harness deployment
    without gateway.join), the per-address limits and the global caps (deployment.py LIMIT_DEFAULTS). One gateway
    process = one state.

    The connect rate is a client_limits.ClientRate with the deployment's trusted proxy ranges (trusted), the same
    helper the origin server's /runtime/join counts its issuance with."""

    def __init__(self, verifier, limits, clock=time.monotonic, entries=RATE_TABLE_ENTRIES, trusted=()):
        self.verifier, self.limits = verifier, limits
        self.connects = ClientRate(limits["connectsPerMinutePerIp"], trusted, 60, entries, clock)
        self.active = collections.Counter()
        self.total = 0
        # The global cap: limits.connections, and no more than limits.memoryBytes of connection buffers
        self.capacity = min(limits["connections"], limits["memoryBytes"] // CONNECTION_BUFFER_BYTES)

    def rate_ok(self, key):
        """Counts this handshake in the address's last minute unless that already holds connectsPerMinutePerIp (key:
        the client's ip_key)."""
        return self.connects.table.hit(key)

    def acquire(self, key):
        """Takes a connection slot for the address: None when admitted, else the refusal (status, reason)."""
        if self.active[key] >= self.limits["connectionsPerIp"]:
            return http.HTTPStatus.TOO_MANY_REQUESTS, "Too many concurrent connections from this address"
        if self.total >= self.capacity:
            return http.HTTPStatus.SERVICE_UNAVAILABLE, "Gateway at capacity"
        self.active[key] += 1
        self.total += 1
        return None

    def release(self, key):
        self.total -= 1
        self.active[key] -= 1
        if self.active[key] <= 0:
            del self.active[key]


class Bucket:
    """Token bucket for one direction of one connection: rate bytes/s, a burst of one second."""

    def __init__(self, rate, clock=time.monotonic):
        self.rate, self.clock = rate, clock
        self.tokens, self.t = float(rate), clock()

    async def take(self, n):
        now = self.clock()
        self.tokens = min(self.rate, self.tokens + (now - self.t) * self.rate) - n
        self.t = now
        if self.tokens < 0:
            await asyncio.sleep(-self.tokens / self.rate)


def peer_address(connection):
    address = connection.remote_address
    return address[0] if address else "?"


def resolve_client(connection, request, rate):
    """The client's address, recorded on the connection: the TCP peer, or, when the peer is inside one of the
    deployment's trusted proxy ranges, the one CF-Connecting-IP value that proxy sends (rate.client, a
    client_limits.ClientRate). From any other peer the header is ignored, since a client can send it. Returns None,
    or the refusal (status, reason) of a trusted peer that sent no valid address."""
    connection.plasma_ip, refused = rate.client(peer_address(connection), request.headers.get_all(CLIENT_IP_HEADER))
    return refused


def client_ip(connection):
    return getattr(connection, "plasma_ip", None) or peer_address(connection)


def refuse(connection, status, reason):
    return connection.respond(status, reason + "\n")


async def check_and_connect(connection, request, *, upstreams, networks=None, origins, authorities=(), admission):
    """The policy check, then the upstream TCP connect BEFORE the WebSocket upgrade.

    pnAcSocket.cpp reports AsyncNotifySocketConnectSuccess only after asio's TCP connect
    (SocketInitConnect :323, from AsyncSocketConnect's async_connect handler) and ConnectFailed
    when it fails (AsyncSocketConnect :378). The browser backend notifies
    success on the WebSocket's open (pnAcSocketEmscripten.cpp plws_on_open) and failure on
    a close before it (plws_on_close), so the upgrade is answered only once the shard
    accepted the TCP connection; an unreachable or unresolvable shard answers 502 and the
    client sees the original kNetErrConnectFailed, not a connect followed by a disconnect."""
    refused = resolve_client(connection, request, admission.connects)
    if refused is not None:
        return refuse(connection, *refused)
    key = ip_key(client_ip(connection))
    # Every handshake counts against the address's connect rate first (cheapest check; a flood is refused
    # before any parsing, HMAC or TCP work), then route, Host, Origin, join token, concurrency and the global cap, and
    # only then the TCP connect.
    if not admission.rate_ok(key):
        return refuse(
            connection, http.HTTPStatus.TOO_MANY_REQUESTS, "Too many connections per minute from this address"
        )
    try:
        host, port = upstream_target(request.path, upstreams, networks)
    except ValueError:
        return refuse(connection, http.HTTPStatus.FORBIDDEN, "Unknown or unauthorized shard service")
    local = connection.local_address[1]
    allowed = {f"127.0.0.1:{local}", f"localhost:{local}", *authorities}
    if request.headers.get_all("Host") not in [[authority] for authority in allowed]:
        return refuse(connection, http.HTTPStatus.FORBIDDEN, "Loopback host required")
    # websockets runs process_request before its own Origin check (accept), so the exact
    # origin policy is applied here too: no TCP connection is opened for a refused page.
    if request.headers.get_all("Origin") not in [[origin] for origin in origins]:
        return refuse(connection, http.HTTPStatus.FORBIDDEN, "Origin not allowed")
    if admission.verifier:
        try:
            admission.verifier.check(join_token.offered_token(request.headers.get_all("Sec-WebSocket-Protocol")))
        except join_token.Refusal as r:
            return refuse(connection, r.status, r.reason)
    refused = admission.acquire(key)
    if refused is not None:
        return refuse(connection, *refused)
    connection.plasma_slot = (admission, key)
    try:
        connection.plasma_upstream = await asyncio.wait_for(asyncio.open_connection(host, port), CONNECT_TIMEOUT)
    except (OSError, asyncio.TimeoutError):
        return refuse(connection, http.HTTPStatus.BAD_GATEWAY, "Shard connection unavailable")


def release_slot(connection):
    slot = getattr(connection, "plasma_slot", None)
    if slot:
        connection.plasma_slot = None
        slot[0].release(slot[1])


def release_unused_upstream(connection, request, response, log=None):
    """A handshake that did not end in 101 never reaches the relay: close its upstream, free its concurrency slot,
    and record the refusal (status, reason, route, origin, client IP) as one JSON line in the gateway log. The route
    holds only the upstream host and port; the join token travels in Sec-WebSocket-Protocol and is never logged."""
    opened = getattr(connection, "plasma_upstream", None)
    if response.status_code != 101:
        if opened:
            opened[1].close()
            connection.plasma_upstream = None
        release_slot(connection)
        if log:
            log.write(
                json.dumps(
                    {
                        "t": round(time.time(), 3),
                        "refused": response.status_code,
                        "reason": response.body.decode(errors="replace").strip(),
                        "path": request.path,
                        "origin": request.headers.get("Origin"),
                        "ip": client_ip(connection),
                    }
                )
                + "\n"
            )
            log.flush()


def no_delay(connection_type):
    """TCP_NODELAY for a shard connection of this type: on (Nagle off) where the native client opens it unbuffered."""
    return connection_type in UNBUFFERED_TYPES


def set_nagle(sock, connection_type):
    """Sets TCP_NODELAY on the shard connection as the native client sets it for this connection type (no_delay).
    The value is always written: asyncio turns Nagle off on every TCP transport it makes (selector_events.py
    _SelectorSocketTransport.__init__, through base_events._set_nodelay), so a connection the native client leaves
    buffered has to turn it back on."""
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, int(no_delay(connection_type)))


def make_relay(log=None, *, upstreams, networks=None, limits):
    async def relay(websocket):
        writer = None
        counts = {"sent": 0, "received": 0}
        started = time.monotonic()
        target = None
        end = "error"
        # per-direction byte rate and the idle timeout (deployment limits)
        up_bucket = Bucket(limits["bytesPerSecond"])
        down_bucket = Bucket(limits["bytesPerSecond"])
        last = [time.monotonic()]
        idled = [False]
        try:
            # The query is checked, but its strings are NEVER used as a TCP target: the target
            # is the operator's allowlist entry (upstream_target).
            target = upstream_target(websocket.request.path, upstreams, networks)
            opened = getattr(websocket, "plasma_upstream", None)  # check_and_connect, before the upgrade
            reader, writer = opened if opened else await asyncio.wait_for(asyncio.open_connection(*target), 5)
            first = True

            async def upstream():
                nonlocal first
                async for data in websocket:
                    if not isinstance(data, bytes):
                        await websocket.close(1003, "Binary Plasma protocol required")
                        return
                    if first:
                        if not data or data[0] not in SERVICE_TYPES:
                            await websocket.close(1008, "Connection type does not match route")
                            return
                        # Nagle per connection type (UNBUFFERED_TYPES), set before anything reaches the shard.
                        # Natively the connect header goes out first (pnAcSocket.cpp SocketInitConnect), and then the
                        # auth, game and gatekeeper connections' AsyncNotifySocketConnectSuccess calls
                        # NetCliConnectAccept, which sets it. Nothing is in flight before the header, so Nagle cannot
                        # hold it either way, and every later write sees the native setting.
                        sock = writer.get_extra_info("socket")
                        if sock:
                            set_nagle(sock, data[0])
                        first = False
                    last[0] = time.monotonic()
                    await up_bucket.take(len(data))
                    writer.write(data)
                    await writer.drain()
                    counts["sent"] += len(data)
                return "browser closed"

            async def downstream():
                while data := await reader.read(READ_CHUNK):
                    last[0] = time.monotonic()
                    await down_bucket.take(len(data))
                    await websocket.send(data)
                    counts["received"] += len(data)
                await websocket.close(1000, "Shard disconnected")
                return "shard closed"

            async def idle():
                # No payload byte in either direction for idleSeconds: close 4000 (the client sees a disconnect,
                # as when a native TCP connection drops).
                while True:
                    remaining = last[0] + limits["idleSeconds"] - time.monotonic()
                    if remaining <= 0:
                        idled[0] = True
                        await websocket.close(4000, "Idle timeout")
                        return "idle timeout"
                    await asyncio.sleep(remaining)

            tasks = [asyncio.create_task(upstream()), asyncio.create_task(downstream()), asyncio.create_task(idle())]
            try:
                done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in done:
                    end = task.result() or end
            finally:
                for task in tasks:
                    task.cancel()
                await asyncio.gather(*tasks, return_exceptions=True)
        except ConnectionClosed:
            end = "browser closed"
        except (OSError, asyncio.TimeoutError):
            end = "upstream error"
            await websocket.close(1011, "Test shard connection unavailable")
        finally:
            release_slot(websocket)
            if idled[0]:
                end = "idle timeout"
            if writer:
                writer.close()
                try:
                    await writer.wait_closed()
                except OSError:
                    pass
            if log:
                # Payloads, account identifiers, query strings, tokens and credentials are excluded.
                row = {
                    "t": round(time.time(), 3),
                    "ip": client_ip(websocket),
                    "upstream": f"{target[0]}:{target[1]}" if target else None,
                    **counts,
                    "seconds": round(time.monotonic() - started, 3),
                    "end": end,
                }
                log.write(json.dumps(row) + "\n")
                log.flush()

    return relay


class Gateway:
    """One deployment's gateway: the upstreams it may open, the page origins and Host authorities it serves, its
    admission state (with the trusted proxy ranges, Admission) and its TLS context."""

    def __init__(
        self,
        *,
        name,
        upstreams,
        networks=None,
        origins,
        authorities=(),
        admission,
        tls=None,
        bind="127.0.0.1",
        port=0,
        resolved=None,
        join_ttl=None,
        harness=False,
    ):
        self.name, self.upstreams, self.networks = name, upstreams, networks
        self.origins, self.authorities, self.admission = tuple(origins), tuple(authorities), admission
        self.trusted = admission.connects.trusted
        self.tls, self.bind, self.port, self.resolved, self.join_ttl, self.harness = (
            tls,
            bind,
            port,
            resolved,
            join_ttl,
            harness,
        )

    def relay(self, log=None):
        return make_relay(log, upstreams=self.upstreams, networks=self.networks, limits=self.admission.limits)

    def options(self, log=None):
        """The websockets serve() settings of this gateway."""
        return dict(
            origins=list(self.origins),
            ssl=self.tls,
            process_request=partial(
                check_and_connect,
                upstreams=self.upstreams,
                networks=self.networks,
                origins=self.origins,
                authorities=self.authorities,
                admission=self.admission,
            ),
            process_response=partial(release_unused_upstream, log=log),
            # The client offers "plasma" (and its join token as "plasma.join.<token>"); the server answers
            # "plasma". A client offering nothing (a harness deployment without join) continues without one.
            select_subprotocol=lambda connection, offered: (
                join_token.PROTOCOL if join_token.PROTOCOL in offered else None
            ),
            compression=None,
            max_size=MAX_SIZE,
            max_queue=MAX_QUEUE,
            write_limit=WRITE_LIMIT,
            open_timeout=5,
            close_timeout=2,
            ping_interval=20,
            ping_timeout=20,
        )

    def started(self, server, port_file=None):
        """Writes the bound port to port_file and prints the gateway's policy line (no secret in it)."""
        port = server.sockets[0].getsockname()[1]
        if port_file:
            port_file.write_text(str(port))
        a = self.admission
        print(
            f"Plasma gateway: {'wss' if self.tls else 'ws'}://{self.bind}:{port} deployment {self.name} → "
            + ", ".join(sorted(f"{h}:{p}" for h, p in self.upstreams))
            + ("".join(f", {n}:{self.networks[0]}" for n in self.networks[1]) if self.networks else "")
            + " only; origins "
            + ", ".join(self.origins)
            + ("; resolved " + json.dumps(self.resolved) if self.resolved else "")
            + f"; join tokens {'required (ttl ' + str(self.join_ttl) + ' s)' if a.verifier else 'off (harness)'}"
            + "; trusted proxies "
            + (", ".join(str(n) for n in self.trusted) or "none")
            + f"; limits {json.dumps(a.limits)} ({a.capacity} connections at once)",
            flush=True,
        )
        return port


def tls_context(cert, key):
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    # No TLS 1.3 session tickets. By default the server sends two NewSessionTicket messages right after the handshake,
    # while the client writes its upgrade request; a client that reads and writes one TLS socket from two threads
    # (websockets.sync) then runs OpenSSL's state machine for the ticket concurrently with its write, which OpenSSL
    # does not support on one SSL object (lost requests, internal_error alerts, crashes). Without tickets the server
    # sends nothing after its Finished until it answers. The cost, no session resumption, does not matter to a gateway
    # of long-lived sockets.
    tls.num_tickets = 0
    tls.load_cert_chain(cert, key)
    return tls


def configure(argv=None):
    """The gateway of the --deployment file, and the parsed arguments (port file, log)."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--deployment",
        type=Path,
        required=True,
        help="the operator deployment file (deployment.py); upstreams from its "
        "server.ini, origins/public hosts/bind/port/TLS/trusted proxies/limits from its gateway section",
    )
    parser.add_argument(
        "--port", type=int, help=f"Listen port (default: the deployment's gateway.port, else {DEFAULT_PORT})"
    )
    parser.add_argument("--port-file", type=Path)
    parser.add_argument(
        "--origin",
        action="append",
        help="Exact loopback page origin (http or https on 127.0.0.1 or localhost), only for a "
        "deployment that lists no gateway.origins (a local session or test); repeat as needed",
    )
    parser.add_argument(
        "--log",
        type=Path,
        help="Connections (client IP, upstream, byte counts, end) and refused handshakes (status, route, origin)",
    )
    args = parser.parse_args(argv)
    try:
        d = deployment_file.load(args.deployment)
    except (OSError, ValueError) as e:
        parser.error(f"Invalid deployment: {e}")
    upstreams, resolved = d.upstreams()
    networks = (d.client_port, tuple(d.extra_networks)) if d.extra_networks else None
    if d.gateway_origins and args.origin:
        parser.error("The deployment lists its origins; --origin is only for a deployment without them")
    # --origin: exact page origins (origins.py) on a loopback host; the deployment's own were checked when it loaded.
    for origin in args.origin or []:
        try:
            origins.loopback_origin(origin)
        except ValueError:
            parser.error("--origin must be a loopback origin (http or https on 127.0.0.1 or localhost)")
    try:
        page_origins = origins.page_origins(d.gateway_origins or args.origin or [])
    except ValueError:
        parser.error(f"--origin: at most {origins.MAX_ORIGINS} origins")
    if not page_origins:
        parser.error("The deployment lists no origins and none was given")
    port = args.port if args.port is not None else (d.gateway_port or DEFAULT_PORT)
    if not 0 <= port < 65536:
        parser.error("Invalid listen port")
    for host in d.public_hosts:
        parsed = urlsplit("https://" + host)
        if (
            not parsed.hostname
            or parsed.username
            or parsed.password
            or parsed.path
            or parsed.query
            or parsed.fragment
            or "@" in host
        ):
            parser.error("Invalid public Host authority")
    # Admission. The join key is read once here and never printed or logged.
    try:
        secret = deployment_file.join_secret(d)
    except (OSError, ValueError) as e:
        parser.error(
            f"Join key unavailable ({type(e).__name__}): "
            "run python -m plasma_web.deployment --init-join-secret <deployment>"
        )
    verifier = join_token.Verifier(secret, d.name, d.join_ttl) if secret else None
    return Gateway(
        name=d.name,
        upstreams=upstreams,
        networks=networks,
        origins=page_origins,
        authorities=d.public_hosts,
        admission=Admission(verifier, d.limits, trusted=d.trusted_proxies),
        tls=tls_context(d.tls_cert, d.tls_key) if d.tls_cert else None,
        bind=d.gateway_bind,
        port=port,
        resolved=resolved,
        join_ttl=d.join_ttl,
        harness=d.harness,
    ), args


async def main(argv=None):
    gateway, args = configure(argv)
    logging.getLogger("websockets").setLevel(logging.CRITICAL)
    log = args.log.open("a") if args.log else None
    try:
        async with serve(gateway.relay(log), gateway.bind, gateway.port, **gateway.options(log)) as server:
            gateway.started(server, args.port_file)
            await asyncio.Future()
    finally:
        if log:
            log.close()


def cli():
    """The command line's entry point (plasma-web-gateway)."""
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    cli()
