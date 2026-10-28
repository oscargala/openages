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
"""The shard's server.ini as the browser client accepts it: the one grammar of the deployment tools.

The client itself reads, parses and applies the file with the original pfServerIni (pfServerIni.cpp); nothing here
replaces that. The deployment loader (deployment.py) checks the file with read() before anything serves it, and takes
from it the hosts and the port the gateway may open (endpoints()). The page checks the same file again when it loads it
(validateServerIni in the page shell's config.mjs, Sources/Plasma/Apps/plClient/emscripten/shell/). Both readers are tested
against the same cases (web/tests/shared_rules.json, web/tests/test_rules.py), so a file that the tools accept is one
that the page accepts.

The accepted form. Where the two readers used to differ, this is the stricter of the two. A file it accepts is one that
the client reads too, with the values read here: the shared cases where the tools once accepted more than the client
are pinned on the client's side by Sources/Tests/FeatureTests/pfConsoleCoreTest/test_pfServerIni.cpp.
  - Lines end at LF or CRLF. A line holds one setting, or nothing, or a comment: a line whose first character is #.
    pfServerIni::Parse reads lines with hsStream::ReadLn, which skips a comment only where '#' is a line's first
    character; any other line goes to pfConsoleParser, which reads a line that starts with ';' or "//", or an indented
    '#', as a setting of that name, and pfServerIni::IParseOption refuses it ("Unknown server.ini setting").
  - A setting is one that pfServerIni::IParseOption accepts: Server.Status, Server.Signup, Server.DispName, Server.Port,
    Server.File.Host, Server.Auth.Host/N/X/G, Server.Game.N/X/G and Server.Gate.Host/N/X/G. Names are matched without
    regard to (ASCII) case. A setting has one value, quoted or bare, and may end with a comment (#, ; or //).
  - The name, the value and the comment are separated by spaces or tabs (pfConsoleParser's separators). Other ASCII
    whitespace is trimmed from the ends of a line only. A bare value or a separator holds no other whitespace, not
    even the Unicode spaces that the page's reader would take as one: pfConsoleParser would read them as part of a
    name or value.
  - A quoted value is in double quotes. A bare value holds no '=' or ',' and does not start with a quote:
    pfConsoleTokenizer::NextArgument ends a bare value at '=' and ',' as at a space (a bare base-64 key would lose its
    "==", a bare URL its query) and reads text in single quotes as quoted. A bare value holds no '#' or ';' either, and
    a comment after it starts after a space or tab: the client reads "value#text" as one value.
  - The client reads a trailing comment as more arguments (pfConsoleParser::ParseArguments), and refuses the line when
    one of them starts with a quote that the line does not close ("unterminated quoted parameter"); so do these readers.
  - Each setting appears once.
  - A host is 1 to 253 letters, digits, dots and hyphens, or empty.
  - Server.Port is a decimal port number, 1 to 65535, without leading zeros.
  - The N and X keys are 64 bytes (NetDhKey) in canonical base64: 88 characters, ending in '=='. pfServerIni's
    ParseBase64KeyInto checks the length only.
  - The G values are positive decimal integers without leading zeros. pfServerIni's ParseIntegerInto reads an
    unsigned integer.
  - All six keys are present, and Server.Auth.Host or Server.Gate.Host names the host the client connects to first.
"""

import re

# kDefaultClientPort (pnNbConst.h): the port of every connection when the file has no Server.Port (pfServerIni's fPort).
DEFAULT_CLIENT_PORT = 14617
# A host name or IPv4 address the file may name (and the deployment's extra upstream hosts).
HOST = re.compile(r"[A-Za-z0-9.-]{1,253}")
# The Unicode characters other than ASCII whitespace that the page's reader takes as whitespace (JavaScript's \s).
UNICODE_SPACE = "\u00a0\u1680\u2000-\u200a\u2028\u2029\u202f\u205f\u3000\ufeff"
# One setting line: its name, then its value, quoted (group 2) or bare (group 3), then an optional comment (group 4).
# A bare value ends where pfConsoleTokenizer::NextArgument ends it (a space, a tab, '=' or ','), does not start with a
# single quote, holds no '#' or ';', and is followed by the line's end or a space or tab.
LINE = re.compile(
    r"(Server\.(?:Status|Signup|DispName|Port|File\.Host|Auth\.(?:Host|N|X|G)|Game\.(?:N|X|G)|"
    r'Gate\.(?:Host|N|X|G)))[ \t]+(?:"([^"\r\n]*)"[ \t]*|'
    r"([^\s\"'=,;#" + UNICODE_SPACE + r"][^\s\"=,;#" + UNICODE_SPACE + r"]*)(?:[ \t]+|$))"
    r"((?:#|;|//)[^\r\n\u2028\u2029]*)?",
    re.I | re.ASCII,
)
# The line ends (LF or CRLF), the whitespace stripped from each line, and the comment lines.
LINE_END = re.compile(r"\r?\n")
WHITESPACE = " \t\n\r\f\v"
COMMENT = "#"  # only at a line's first character (hsStream::ReadLn)
# pfConsoleTokenizer::NextArgument: the characters that end a bare argument, and the whitespace before an argument
# (isspace in the C locale).
ARGUMENT_END = " =\r\n\t,"
ARGUMENT_SPACE = " \t\n\v\f\r"
PORT = re.compile(r"[1-9][0-9]{0,4}")
# 64 bytes in canonical base64: 85 characters, one that carries the last 2 bits (its other 4 bits zero), then '=='.
KEY = re.compile(r"[A-Za-z0-9+/]{85}[AQgw]==")
GENERATOR = re.compile(r"[1-9][0-9]*")
REQUIRED = ("server.auth.n", "server.auth.x", "server.game.n", "server.game.x", "server.gate.n", "server.gate.x")


class ServerIniError(ValueError):
    """A server.ini that the browser client refuses; the message names the setting, never a key."""


def read(text):
    """The file's settings as {lowercased name: value}; raises ServerIniError for a file the client refuses."""
    if not isinstance(text, str) or not text or "\0" in text:
        raise ServerIniError("server.ini is empty or not text")
    seen = {}
    for line in LINE_END.split(text):
        if line.startswith(COMMENT):
            continue
        s = line.strip(WHITESPACE)
        if not s:
            continue
        m = LINE.fullmatch(s)
        if not m or not arguments_close(m.group(4) or ""):
            raise ServerIniError("server.ini: a line is not one of pfServerIni's settings")
        name = m.group(1)
        key, value = name.lower(), m.group(2) if m.group(2) is not None else m.group(3)
        if key in seen:
            raise ServerIniError(f"server.ini: duplicate {name}")
        seen[key] = value
        if key.endswith(".host") and value and not HOST.fullmatch(value):
            raise ServerIniError(f"{name}: invalid host {value!r}")
        if key == "server.port" and not (PORT.fullmatch(value) and int(value) <= 65535):
            raise ServerIniError("server.ini: Server.Port is not a port")
        if key.endswith((".n", ".x")) and not KEY.fullmatch(value):
            raise ServerIniError(f"server.ini: {name} is not a 64-byte base64 key")
        if key.endswith(".g") and not GENERATOR.fullmatch(value):
            raise ServerIniError(f"server.ini: {name} is not a positive integer")
    for key in REQUIRED:
        if key not in seen:
            raise ServerIniError(f"server.ini lacks {key}")
    if not seen.get("server.auth.host") and not seen.get("server.gate.host"):
        raise ServerIniError("server.ini names neither Server.Auth.Host nor Server.Gate.Host")
    return seen


def arguments_close(text):
    """Whether every argument in text that starts with a quote is closed, as pfConsoleTokenizer::NextArgument reads the
    rest of a line: an argument that starts with " or ' runs to the next of the same quote, which must exist; any other
    runs to the first of ARGUMENT_END, and a run of that character is skipped."""
    i, n = 0, len(text)
    while True:
        while i < n and text[i] in ARGUMENT_SPACE:
            i += 1
        if i == n:
            return True
        if text[i] in "\"'":
            end = text.find(text[i], i + 1)
            if end < 0:
                return False
            i = end + 1
            continue
        while i < n and text[i] not in ARGUMENT_END:
            i += 1
        if i < n:
            sep = text[i]
            while i < n and text[i] == sep:
                i += 1


def endpoints(settings):
    """(entry host, client port, {hosts the client connects to}) of read()'s settings.

    NetCommConnect (plNetClientComm.cpp) connects to Server.Auth.Host directly when it is set, and otherwise asks the
    gatekeeper (Server.Gate.Host) for the auth server's address; likewise Server.File.Host, or the gatekeeper, for the
    file server. Every connection uses the one client port, GetClientPort() = Server.Port (pfServerIni::Apply,
    SetClientPort; default kDefaultClientPort), the game server that the auth server names at a link included.
    """
    port = int(settings.get("server.port", DEFAULT_CLIENT_PORT))
    entry = settings.get("server.auth.host") or settings["server.gate.host"]
    hosts = {v for k, v in settings.items() if k.endswith(".host") and v}
    return entry, port, hosts
