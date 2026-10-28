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
"""Gateway join tokens (issued by the page origin's /runtime/join, checked by gateway.py).

A token admits ONE WebSocket connection to the gateway (single use), for one deployment, until its expiry:

    v1.<payload>.<mac>      payload = base64url(JSON {"d": deployment name, "e": expiry (unix s), "n": nonce})
                            mac     = base64url(HMAC-SHA256(secret, "plasma-join-v1." + payload))

The secret is the deployment's local join key file (deployment.py gateway.join.secretFile): 32 random bytes as hex,
created 0600 by `python -m plasma_web.deployment --init-join-secret`, outside Git, never printed or logged. The
client fetches a fresh token right before each socket it opens (pnAcWebSocket.js) and offers it as the WebSocket
subprotocol "plasma.join.<token>" beside "plasma", so it never appears in a URL or an access log.
"""

import base64
import binascii
import hashlib
import heapq
import hmac
import json
import os
import re
import secrets
import time
from pathlib import Path

PREFIX = "plasma.join."
PROTOCOL = "plasma"
MAX_TOKEN = 512
SKEW = 5  # seconds of clock skew accepted on a future expiry (issuer and gateway on different hosts)
TOKEN_RE = re.compile(r"v1\.[A-Za-z0-9_-]{1,400}\.[A-Za-z0-9_-]{43}")


def _b64(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def _unb64(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def read_secret(path):
    """The key bytes from a join key file (64 hex characters); the file must not be group/world readable."""
    p = Path(path)
    if p.stat().st_mode & 0o077:
        raise ValueError("join secret file must be private (mode 0600)")
    text = p.read_text().strip()
    if not re.fullmatch(r"[0-9a-f]{64}", text):
        raise ValueError("join secret file must hold 64 lowercase hex characters")
    return bytes.fromhex(text)


def init_secret(path):
    """Create the key file (0600) if it does not exist; never prints the key. Returns True when created."""
    p = Path(path)
    if p.exists():
        read_secret(p)
        return False
    p.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(secrets.token_hex(32) + "\n")
    return True


def _mac(secret, payload):
    return hmac.new(secret, b"plasma-join-v1." + payload.encode(), hashlib.sha256).digest()


def issue(secret, deployment, ttl, now=None):
    """A new token for this deployment; returns (token, expiry)."""
    expiry = int((time.time() if now is None else now) + ttl)
    payload = _b64(
        json.dumps({"d": deployment, "e": expiry, "n": _b64(secrets.token_bytes(16))}, separators=(",", ":")).encode()
    )
    return f"v1.{payload}.{_b64(_mac(secret, payload))}", expiry


class Refusal(Exception):
    """A refused token: status (401 or 403) and the reason logged and returned to the browser."""

    def __init__(self, status, reason):
        super().__init__(reason)
        self.status, self.reason = status, reason


class Verifier:
    """Checks tokens for one deployment and remembers spent nonces until their expiry (single use).

    The spent nonces live in this object only, so single use holds within one gateway process: after a restart, or
    across two gateway processes with the same key, an unexpired token is admitted again (at most once per process,
    and only until its expiry, ttlSeconds <= 300). Run one gateway process per join key.

    A spent nonce is kept until its token expires, and no longer: an expired token is refused before the replay check.
    The nonces are also kept in a heap ordered by expiry, so each check drops the expired ones from the front only,
    without a scan of the whole set. Every nonce comes from a token with a valid MAC, so the set holds at most the tokens
    the deployment issued within one ttl."""

    def __init__(self, secret, deployment, ttl, clock=time.time):
        self.secret, self.deployment, self.ttl, self.clock = secret, deployment, ttl, clock
        self.spent = {}  # nonce -> expiry
        self.expiries = []  # heap of (expiry, nonce), one entry per spent nonce

    def _prune(self, now):
        while self.expiries and self.expiries[0][0] < now:
            _, nonce = heapq.heappop(self.expiries)
            del self.spent[nonce]

    def check(self, token):
        """Accepts and spends the token, or raises Refusal. Order: shape, MAC, deployment, expiry, replay."""
        if token is None:
            raise Refusal(401, "Join token required")
        if len(token) > MAX_TOKEN or not TOKEN_RE.fullmatch(token):
            raise Refusal(401, "Invalid join token")
        _, payload, mac = token.split(".")
        try:
            ok = hmac.compare_digest(_unb64(mac), _mac(self.secret, payload))
            claims = json.loads(_unb64(payload)) if ok else None
        except (binascii.Error, ValueError):
            ok = False
        if (
            not ok
            or not isinstance(claims, dict)
            or set(claims) != {"d", "e", "n"}
            or type(claims["e"]) is not int
            or not isinstance(claims["n"], str)
            or not isinstance(claims["d"], str)
        ):
            # type(...) is int: JSON true/false decode to bool, a subclass of int (True would read as expiry 1)
            raise Refusal(401, "Invalid join token")
        if claims["d"] != self.deployment:
            raise Refusal(403, "Join token for another deployment")
        now = self.clock()
        if claims["e"] < now:
            raise Refusal(401, "Join token expired")
        if claims["e"] > now + self.ttl + SKEW:
            raise Refusal(401, "Invalid join token")
        self._prune(now)
        if claims["n"] in self.spent:
            raise Refusal(401, "Join token already used")
        self.spent[claims["n"]] = claims["e"]
        heapq.heappush(self.expiries, (claims["e"], claims["n"]))


def offered_token(header_values):
    """The token from the Sec-WebSocket-Protocol values the browser offered (None if absent)."""
    found = None
    for value in header_values:
        for item in value.split(","):
            item = item.strip()
            if item.startswith(PREFIX):
                if found is not None:
                    return ""  # two tokens: refused as invalid
                found = item[len(PREFIX) :]
    return found
