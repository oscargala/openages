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
"""The per-client limits of the two servers a deployment runs: the WebSocket gateway (gateway.py, its
connect rate) and the page's origin server (server.py, the join-token issuance rate of /runtime/join).

Both count events per client in a bounded sliding window (RateTable), keyed by the client's address (ip_key), where
the client's address is the TCP peer, or, behind one of the deployment's trusted reverse proxies
(gateway.trustedProxies), the address that proxy passes on (client_address). ClientRate puts the three together for one
deployment. This module needs only the standard library: the origin server runs without the gateway's dependencies.
"""

import collections
import http
import ipaddress
import time

# At most this many client addresses are tracked by one rate table (least recently counted dropped first).
RATE_TABLE_ENTRIES = 16384
# The client address a trusted proxy (the deployment's gateway.trustedProxies) passes on: Cloudflare's header.
CLIENT_IP_HEADER = "CF-Connecting-IP"
# The refusal of a trusted proxy's request that names no valid client address.
NO_CLIENT_ADDRESS = (http.HTTPStatus.BAD_REQUEST, "Client address missing from the proxy")


def ip_key(address):
    """The per-address limits' key: an IPv4 address as is, an IPv6 address by its /64 (one host often holds many
    addresses of one /64), an IPv4-mapped IPv6 address as its IPv4 address (the Worker's ipKey, gateway.js)."""
    try:
        a = ipaddress.ip_address(address)
    except ValueError:
        return address
    if a.version == 6:
        if a.ipv4_mapped:
            return str(a.ipv4_mapped)
        return str(ipaddress.ip_network((a, 64), strict=False))
    return str(a)


class RateTable:
    """Events per key in a sliding window (the last `window` seconds), at most `limit` per key.

    Bounded: a key is dropped once its newest event has left the window, and when `entries` keys are live the least
    recently counted one is dropped (that key starts a new window). Keys are kept in the order of their newest event,
    so expiry only looks at the front."""

    def __init__(self, limit, window=60, entries=RATE_TABLE_ENTRIES, clock=time.monotonic):
        self.limit, self.window, self.entries, self.clock = limit, window, entries, clock
        self.table = collections.OrderedDict()

    def hit(self, key):
        """Counts one event for key unless the window already holds `limit`; False when refused."""
        now = self.clock()
        while self.table and next(iter(self.table.values()))[-1] <= now - self.window:
            self.table.popitem(last=False)
        q = self.table.get(key)
        if q is not None:
            while q[0] <= now - self.window:
                q.popleft()
        if self.limit <= 0 or (q is not None and len(q) >= self.limit):
            return False
        if q is None:
            while len(self.table) >= self.entries:
                self.table.popitem(last=False)
            q = self.table[key] = collections.deque()
        q.append(now)
        self.table.move_to_end(key)
        return True

    def __len__(self):
        return len(self.table)


def client_address(peer, header_values, trusted):
    """(address, refusal): the client's address and None, or the peer's address and the refusal (status, reason) of a
    trusted peer that sent no valid address.

    peer is the TCP peer's address and header_values the request's CLIENT_IP_HEADER values. The header counts only
    from a peer inside one of the trusted proxy ranges, which must send exactly one valid address; from any other
    peer it is ignored, since a client can send it."""
    if not trusted:
        return peer, None
    try:
        address = ipaddress.ip_address(peer)
    except ValueError:
        return peer, None
    if address.version == 6 and address.ipv4_mapped:
        address = address.ipv4_mapped
    if not any(address in network for network in trusted):
        return peer, None
    values = list(header_values or ())
    try:
        client = ipaddress.ip_address(values[0].strip()) if len(values) == 1 else None
    except ValueError:
        client = None
    if client is None:
        return peer, NO_CLIENT_ADDRESS
    return str(client), None


class ClientRate:
    """One deployment's rate of one kind of event per client: the deployment's trusted proxy ranges decide which
    address is the client's (client_address), and a bounded RateTable counts each client's events by ip_key."""

    def __init__(self, limit, trusted=(), window=60, entries=RATE_TABLE_ENTRIES, clock=time.monotonic):
        self.trusted = tuple(trusted)
        self.table = RateTable(limit, window, entries, clock)

    @classmethod
    def for_deployment(cls, deployment, limit, **kw):
        """The rate for a loaded deployment file (deployment.py): its gateway.trustedProxies."""
        return cls(limit, deployment.trusted_proxies, **kw)

    def client(self, peer, header_values):
        """(address, refusal) of a request from peer with these CLIENT_IP_HEADER values (client_address)."""
        return client_address(peer, header_values, self.trusted)

    def hit(self, address):
        """Counts one event for the client at address unless its window is full; False when refused."""
        return self.table.hit(ip_key(address))

    def __len__(self):
        return len(self.table)
