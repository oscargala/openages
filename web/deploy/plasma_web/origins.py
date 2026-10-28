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
"""Page origins: the one rule for every origin a deployment's servers compare a request's Origin header with.

The gateway admits a WebSocket handshake only from its page origins (gateway.origins, or --origin for a deployment
that lists none), the origin server issues join tokens only to them (/runtime/join), and it lets the asset origins
(--asset-origin) read its public assets cross-origin. A browser sends the Origin header in its serialized form (the
HTML standard's ASCII serialization of an origin: the scheme, the lower-case host, and the port only when it is not the
scheme's default), and the servers compare it as a string. So an origin is accepted only in that exact form: anything
else could never match a browser's header, or would hide what it matches.

An origin is accepted when it is:
  - https://, or http:// on a loopback host (127.0.0.1 or localhost) for a page on this machine;
  - a host, and nothing else: no user name or password, no path (not even "/"), no query and no fragment;
  - in serialized form: a lower-case host name of valid DNS labels, or an IP address in its canonical form (an IPv6
    address in brackets, without a zone), and a port from 1 to 65535 only when it is not the scheme's default;
  - at most 2,048 characters, with no whitespace, control character or backslash.
The loopback hosts do not include ::1: the gateway never admitted an http://[::1] page, and every reader now applies
the gateway's rule. (The page itself still runs from [::1]: config.mjs loopbackPage.)
"""

import ipaddress
import re
import urllib.parse

# The hosts a plain-HTTP page origin may name: this machine.
LOOPBACK_HOSTS = ("127.0.0.1", "localhost")
# The most origins one list may hold.
MAX_ORIGINS = 16
MAX_LENGTH = 2048
FORBIDDEN = re.compile(r"[\x00-\x20\x7f\\]")
LABEL = re.compile(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")
DEFAULT_PORTS = {"http": 80, "https": 443}


def _host_ok(host):
    """A lower-case host name of valid DNS labels (not all digits, which would be an IPv4 address in another
    spelling), or an IP address in its canonical form."""
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        address = None
    if address is not None:  # an IPv6 zone (fe80::1%en0) names an interface of one machine, never an origin
        return str(address) == host and not getattr(address, "scope_id", None)
    if len(host) > 253 or re.fullmatch(r"[0-9.]+", host):
        return False
    labels = host[:-1].split(".") if host.endswith(".") else host.split(".")
    return all(LABEL.fullmatch(label) for label in labels)


def page_origin(value):
    """value when it is an exact page origin (this module's rule); raises ValueError, naming the value, otherwise."""
    if not _origin_ok(value):
        raise ValueError(f"{value!r} is not an exact HTTPS origin or loopback HTTP origin")
    return value


def _origin_ok(value):
    if not isinstance(value, str) or len(value) > MAX_LENGTH or FORBIDDEN.search(value):
        return False
    try:
        u = urllib.parse.urlsplit(value)
        port = u.port  # a port that is not a number from 0 to 65535 raises ValueError
    except ValueError:
        return False
    host = u.hostname
    if (
        u.scheme not in DEFAULT_PORTS
        or not host
        or not _host_ok(host)
        or (u.scheme == "http" and host not in LOOPBACK_HOSTS)
        or u.username is not None
        or u.password is not None
        or u.path
        or u.query
        or u.fragment
        or port == 0
        or port == DEFAULT_PORTS[u.scheme]
    ):
        return False
    authority = (f"[{host}]" if ":" in host else host) + (f":{port}" if port is not None else "")
    return value == f"{u.scheme}://{authority}"  # the serialized form, nothing more


def loopback_origin(value):
    """value when it is an exact page origin on a loopback host (http or https); raises ValueError otherwise."""
    if urllib.parse.urlsplit(page_origin(value)).hostname not in LOOPBACK_HOSTS:
        raise ValueError(f"{value!r} is not a loopback origin")
    return value


def page_origins(values):
    """The list as a tuple without repeats, each one an exact page origin, at most MAX_ORIGINS of them; raises
    ValueError otherwise."""
    values = list(values)
    if len(values) > MAX_ORIGINS:
        raise ValueError(f"more than {MAX_ORIGINS} origins")
    return tuple(dict.fromkeys(page_origin(v) for v in values))
