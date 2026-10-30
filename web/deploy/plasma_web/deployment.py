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
"""One operator deployment file per deployment (server side, never served as is).

The file names the shard by its own native client server.ini, the local asset/fonts/sound
trees, the gateway policy and the page capabilities. Its readers:
  - the origin server, server.py --deployment: serves the trees and derives the public page
    document /runtime/config.json (schema 1, the page shell's config.mjs,
    Sources/Plasma/Apps/plClient/emscripten/shell/) and the ini itself at /runtime/server.ini, SHA-256 pinned;
  - gateway.py --deployment: allowed upstreams come from the same ini
    (the hosts the native client connects to, at its one client port), allowed origins
    and Host authorities from the gateway section;
  - emit_static.py, the deployment's static-host kit;
  - the test harness's runner, which starts its runs from a loopback harness deployment.
The client still reads, parses and applies the ini with the original pfServerIni
(main.cpp; pfServerIni.cpp). This module only checks it (server_ini.py, the grammar the page's
config.mjs validateServerIni also applies) and reads the hosts and port out of it for the
gateway allowlist. The page origins follow origins.py.

No credentials belong here: accounts are entered by the player (or by the harness from its
private env file). Unknown keys fail.
"""

import hashlib
import ipaddress
import json
import os
import re
import socket
import struct
from pathlib import Path
from urllib.parse import urlsplit

from plasma_web import FONTS_DIR, ROOT
from plasma_web import origins, server_ini

SCHEMA = 1
LIMIT = 65536
DEFAULT_CLIENT_PORT = server_ini.DEFAULT_CLIENT_PORT
HOST_RE = server_ini.HOST  # the hosts a server.ini may name, and the deployment's extra upstream hosts
KEYS = {
    "schema",
    "name",
    "serverIni",
    "data",
    "fonts",
    "sound",
    "gateway",
    "capabilities",
    "graphics",
    "textures",
    "source",
}
# Where the deployment's source code is published (provenance.py writes it into source.json, which the page's
# "Source code (GPLv3)" link reads): the repository, optionally the release tag, and the URL of the source bundle of
# the third-party components.
SOURCE_KEYS = {"repository", "tag", "thirdPartySources"}
DATA_KEYS = {"root", "urlPrefix", "gameCode", "url", "files"}
GATEWAY_KEYS = {
    "url",
    "bind",
    "port",
    "origins",
    "publicHosts",
    "extraUpstreamHosts",
    "extraUpstreamNetworks",
    "resolveUpstreams",
    "tlsCert",
    "tlsKey",
    "join",
    "limits",
    "trustedProxies",
}
# Gateway admission. join = the HMAC join-token key (join_token.py) and token lifetime;
# limits = per client IP, per connection and for the whole gateway process (gateway.py), with these defaults:
JOIN_KEYS = {"secretFile", "secretFileEnv", "ttlSeconds"}
# gateway.join.secretFileEnv names the environment variable that holds the key file's path, so a deployment file kept
# in a repository names no path of one machine.
ENV_NAME_RE = re.compile(r"[A-Z_][A-Z0-9_]{0,63}")
DEFAULT_JOIN_TTL = 60
# One connection's queue of browser frames waiting for the shard: gateway.py MAX_QUEUE frames of MAX_SIZE, the Worker's
# MAX_QUEUED. The Worker's isolate budget holds at least one, so a connection alone in its isolate meets only its own
# cap.
RELAY_QUEUE_BYTES = 16 * 1024 * 1024
# A Worker isolate may use 128 MB (128,000,000 B). Measured in a local workerd: the idle isolate holds 1,109,926 B and
# each open relay 18,557 B (JavaScript heap, ArrayBuffer backing stores, embedder heap), so the default 64 connections
# leave 125.7 MB. Half of that is for queued frames, the other half for frames already written or dropped but not yet
# collected and for buffers the budget does not count: 62.9 MB, rounded down to whole connection queues, 48 MiB.
ISOLATE_QUEUE_DEFAULT = 48 * 1024 * 1024
LIMIT_DEFAULTS = {
    # A client holds at most 4 sockets (auth, game, file; gatekeeper and the launcher's file socket briefly) and an Age
    # link opens the new game socket before the old one has closed; 8 leaves room for two clients behind one NAT.
    "connectionsPerIp": 8,
    # A fresh start opens 5 (launcher gatekeeper + file, auth, file, game) and each link one more.
    "connectsPerMinutePerIp": 30,
    # Per direction, per connection. A fresh install's first patch (to Relto) moves about 146 MB over the file socket
    # in under two minutes, with peaks near 25 MiB/s on a LAN; 8 MiB/s keeps that pace while bounding one socket's
    # bulk use.
    "bytesPerSecond": 8 * 1024 * 1024,
    # No payload byte in either direction for this long closes the socket (4000). Every Plasma connection pings every
    # 30 s (kPingIntervalMs, plNetGameLib Intern.h:67; CliFileConn::AutoPing in plNglFile.cpp), so only a dead
    # client idles.
    "idleSeconds": 120,
    # Connections relayed at once by one gateway process, from all addresses.
    "connections": 64,
    # The connection buffers one gateway process may hold: each connection reserves gateway.py CONNECTION_BUFFER_BYTES
    # (18,219,008 B, its WebSocket and TCP buffers at their limits), so 1 GiB admits 58 connections at once.
    "memoryBytes": 1024 * 1024 * 1024,
    # The Worker only (gateway.py bounds its buffers by memoryBytes): the browser frames that all connections relayed
    # by one Worker isolate may hold for their upstreams together (the Worker's queue_budget.js). A full budget closes
    # the connection that holds the most (1008, as a connection over its own 16 MiB). At least RELAY_QUEUE_BYTES.
    "isolateQueueBytes": ISOLATE_QUEUE_DEFAULT,
}
CAPABILITY_KEYS = {"harness", "gameCodeCapture"}
# The two text faces of the client's debug and loading text: "Courier New" (plDebugText's SetFont, plDebugText.h:76;
# plNetClientMgr::IShowAvatars, plNetClientMgrShow.cpp:284 and :364) and "Trebuchet MS Bold" (the external release's
# SetFont, plDebugText.h:74). A browser has no font database, so plTextFont looks a face up by
# name as /fonts/<face>.ttf, then .otf, in the client's file system (plTextFont.cpp IInitFontTexture, the Emscripten
# branch); the fonts tree serves each face as "<face>.ttf" from the file the deployment's face -> file map names.
FONT_FACES = ("Courier New", "Trebuchet MS Bold")
FONTS_KEYS = {"root", "url", "map", "hostCopies"}
FONT_FILE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9 _.+-]{0,127}")
# The default map of every deployment that names no fonts, and so of every static kit that does not: free substitutes,
# kept in web/deploy/fonts (README.md there names the releases, URLs and SHA-256s). Liberation Mono has Courier New's
# metrics; DejaVu Sans Bold is the sans-serif bold a Linux host's fontconfig falls back to for "Trebuchet MS Bold",
# which has no metric-compatible free face. Only the debug and loading text looks different.
FREE_FONTS_DIR = FONTS_DIR
FREE_FONTS = {"Courier New": "LiberationMono-Regular.ttf", "Trebuchet MS Bold": "DejaVuSans-Bold.ttf"}
# What a kit that carries the free substitutes must say about them (emit_static.py writes THIRD_PARTY_NOTICES.md and
# copies each licence file next to the fonts).
FREE_FONT_NOTICES = (
    {
        "face": "Courier New",
        "file": "LiberationMono-Regular.ttf",
        "font": "Liberation Mono Regular",
        "release": "Liberation Fonts 2.1.5",
        "url": "https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5",
        "licence": "SIL Open Font License, Version 1.1 (Reserved Font Name Liberation)",
        "licence_file": "LICENSE-LiberationFonts.txt",
        "copyright": "Digitized data copyright (c) 2010 Google Corporation; Copyright (c) 2012 Red Hat, Inc.",
    },
    {
        "face": "Trebuchet MS Bold",
        "file": "DejaVuSans-Bold.ttf",
        "font": "DejaVu Sans Bold",
        "release": "DejaVu Fonts 2.37",
        "url": "https://github.com/dejavu-fonts/dejavu-fonts/releases/tag/version_2_37",
        "licence": "Bitstream Vera Fonts licence and Arev Fonts licence; the DejaVu changes are in the public domain",
        "licence_file": "LICENSE-DejaVuFonts.txt",
        "copyright": "Copyright (c) 2003 by Bitstream, Inc.; Copyright (c) 2006 by Tavmjong Bah",
    },
)


def font_file_name(face):
    """The file name a face is served under in the fonts tree, which the client asks for (plTextFont.cpp)."""
    return face + ".ttf"


def free_font_files():
    """The default face -> file map, keyed by the served file name: {"Courier New.ttf": <path>, ...}."""
    return {font_file_name(face): FREE_FONTS_DIR / name for face, name in FREE_FONTS.items()}


# graphics.initialIni = the deployment's first-run graphics.ini, which the page installs into the profile's
# Init/graphics.ini only when the profile has none (the condition of the client's own default writer,
# plClient::IDetectAudioVideoSettings, plClient.cpp:2354-2355); an existing file is never touched. The values are the
# console settings that writer writes (plClient::IWriteDefaultGraphicsSettings) except the size, which
# the page takes from the canvas backing as the writer takes it from the default mode, plus the options menu's
# Graphics.Shadow.VisibleDistance (xIniDisplay.py). Each is (kind, low, high, required); the same table is in
# config.mjs (INITIAL_GRAPHICS_INI), which writes the file in the writer's format and order, and both are tested
# against web/tests/shared_rules.json. Kind "int-bool" is a true or false setting that the writer writes as 1 or 0
# (WriteInt), "bool" one it writes as true or false (WriteBool).
GRAPHICS_KEYS = {"initialIni"}
INITIAL_GRAPHICS_INI = {
    "Graphics.Windowed": ("bool", None, None, True),
    "Graphics.AntiAliasAmount": ("int", 0, 16, True),
    "Graphics.AnisotropicLevel": ("int", 0, 16, True),
    "Graphics.TextureQuality": ("int", 0, 2, True),  # pfConsoleCommands.cpp's Graphics.TextureQuality clamps to 0..2
    "Quality.Level": ("int", 0, 3, True),  # pfConsoleCommands.cpp's Quality.Level clamps to 0..3
    "Graphics.Shadow.Enable": ("int-bool", None, None, True),
    "Graphics.EnablePlanarReflections": ("int-bool", None, None, True),
    "Graphics.EnableVSync": ("bool", None, None, True),
    "Graphics.Shadow.VisibleDistance": ("number", 0, 1, False),  # plShadowMaster::SetGlobalShadowQuality, 0..1
}
# textures.hdPack = the optional packs of verified original high-resolution textures, one <Age>.hdpack per listed Age:
# root = a directory outside the repository that server.py serves at /hdpack, or url = the same files on another
# origin. The packs hold CC BY-NC-SA 4.0 art and must never be served by a
# commercial deployment; the player turns them on (off by default) and the page credits them.
TEXTURES_KEYS = {"hdPack"}
HDPACK_KEYS = {"root", "url", "ages"}
HDPACK_AGE = re.compile(r"[A-Za-z0-9_-]{1,64}")
# A release (v0.1) serves only the shard's own game code, "server" for an H-uru shard and "server-py2" for MOULa.
V01_GAME_CODES = ("server", "server-py2")
# The game-code modes; "server-py2" = the shard's game code (as "server") run by the py2 build
GAME_CODES = ("server", "server-py2", "local")
# The deployment rules a test-hook overlay adds (register): the test harness's client game-code modes (a release runs
# only the shard's own game code), which the private test harness registers when a harness tool imports it. The product registers none, so this module refuses those modes as unknown. An extension
# has game_codes (the data.gameCode modes it adds) and data_keys (the data keys it adds), and Deployment calls its
# load(dep, data, base) once the data section is read, check(dep) once the capabilities are, document(dep, doc) for
# the page document (public_document) and summary(dep) for the data section's summary.
EXTENSIONS = []


def register(extension):
    """Adds a test-hook overlay's deployment rules (EXTENSIONS), once: an extension whose modes are already registered
    (the same module loaded a second time, under another name) is not added again."""
    if not any(set(e.game_codes) & set(extension.game_codes) for e in EXTENSIONS):
        EXTENSIONS.append(extension)


def game_codes():
    """The data.gameCode modes this process accepts: the product's and those of the registered extensions."""
    return GAME_CODES + tuple(mode for extension in EXTENSIONS for mode in extension.game_codes)


# The client builds a deployment selects (output names of build-client.sh): py3 = PLASMA_PYTHON_ABI 3 (CPython 3.12.9,
# the default), py2 = PLASMA_PYTHON_ABI 2 (CPython 2.3.3). Only gameCode server-py2 selects py2.
CLIENT_NAMES = {"py3": "plClient", "py2": "plClient-py2"}
# The type docstrings each flavour's import machinery compiles in (ptModulePackFinder's, cyPythonModule451.cpp:144 for
# ABI 3; ptModulePackFinder2's, cyPythonModule302.cpp:134 for ABI 2): a build's .wasm holds exactly one of them.
ABI_MARKERS = {
    "py3": b"PEP 451 module finder for plPythonPack",
    "py2": b"PEP 302 module finder and loader for plPythonPack",
}


class DeploymentError(ValueError):
    pass


def _fail(message):
    raise DeploymentError(message)


def _object(value, keys, what):
    if not isinstance(value, dict):
        _fail(f"{what} must be an object")
    unknown = set(value) - keys
    if unknown:
        _fail(f"{what}: unknown keys {sorted(unknown)}")


def _path(value, base, what, optional=False):
    if value is None and optional:
        return None
    if not isinstance(value, str) or not value or "\0" in value:
        _fail(f"{what} must be a path")
    p = Path(os.path.expanduser(value))
    return (p if p.is_absolute() else base / p).resolve()


def _host(value, what):
    if not isinstance(value, str) or not HOST_RE.fullmatch(value):
        _fail(f"{what}: invalid host {value!r}")
    return value


def _origins(values):
    """gateway.origins: a list of exact page origins (origins.page_origins), without repeats."""
    if not isinstance(values, list):
        _fail("gateway.origins must be a list")
    try:
        return list(origins.page_origins(values))
    except ValueError as e:
        _fail(f"gateway.origins: {e}")


def _ws_url(value):
    u = urlsplit(value) if isinstance(value, str) else None
    loop = u is not None and u.hostname in {"127.0.0.1", "localhost", "::1"}
    if (
        u is None
        or not u.hostname
        or u.username
        or u.password
        or u.query
        or u.fragment
        or not (u.scheme == "wss" or (u.scheme == "ws" and loop))
    ):
        _fail("gateway.url must be wss://, or ws:// on loopback, without credentials/query")
    return value.rstrip("/")


def _https_url(value, what):
    if value is None:
        return None
    u = urlsplit(value) if isinstance(value, str) else None
    if u is None or u.scheme != "https" or not u.hostname or u.username or u.password or u.query or u.fragment:
        _fail(f"{what} must be an https:// URL without credentials, query or fragment")
    return value.rstrip("/")


def read_server_ini(text):
    """The ini's settings as {lowercased name: value} (server_ini.read); refuses what pfServerIni or the page would."""
    try:
        return server_ini.read(text)
    except server_ini.ServerIniError as e:
        _fail(str(e))


# (entry host, client port, {hosts the client connects to}) of read_server_ini's settings
shard_endpoints = server_ini.endpoints


class Deployment:
    def __init__(self, path):
        self.path = Path(path).expanduser().resolve()
        raw = self.path.read_bytes()
        if len(raw) > LIMIT:
            _fail("deployment file exceeds 65536 bytes")
        d = json.loads(raw)
        _object(d, KEYS, "deployment")
        if d.get("schema") != SCHEMA:
            _fail("deployment schema must be 1")
        base = self.path.parent
        self.name = d.get("name", self.path.stem)
        if not isinstance(self.name, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", self.name):
            _fail("name must be 1-64 of [A-Za-z0-9_.-]")
        self.server_ini_path = _path(d.get("serverIni"), base, "serverIni")
        self.server_ini_bytes = self.server_ini_path.read_bytes()
        if len(self.server_ini_bytes) > LIMIT:
            _fail("server.ini exceeds 65536 bytes")
        try:
            text = self.server_ini_bytes.decode("utf-8")
        except UnicodeDecodeError:
            _fail("server.ini must be plain UTF-8 text (the encrypted form is not served)")
        self.settings = read_server_ini(text)
        self.entry_host, self.client_port, self.ini_hosts = shard_endpoints(self.settings)
        self.server_ini_sha256 = hashlib.sha256(self.server_ini_bytes).hexdigest()
        # The login dialog's status URL (Server.Status, which pfServerIni::IParseOption keeps, pfServerIni.cpp:101;
        # GetServerStatusUrl in the status thread of UruLoginDialogProc, plClient/win32/winmain.cpp)
        # (relayed only when it is an http(s) URL; the client otherwise asks for the ini's value itself).
        self.status_url = self.settings.get("server.status", "")
        if not re.fullmatch(r"https?://[^\s\x00-\x1f\x7f]{1,2040}", self.status_url):
            self.status_url = ""

        # The data/sound trees are the dev/harness path (served staging data). A shard without one (e.g. the
        # MOULa template) has its game files only through the original patcher (data.files patcher).
        data = d.get("data", {})
        _object(data, DATA_KEYS.union(*(extension.data_keys for extension in EXTENSIONS)), "data")
        self.data_root = _path(data.get("root"), base, "data.root", optional=True)
        self.url_prefix = data.get("urlPrefix", "/data")
        if not re.fullmatch(r"/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+", self.url_prefix):
            _fail("data.urlPrefix must be an absolute path prefix")
        # Optional: "server" = Python/SDL from the shard's file server (original patching),
        # "local" = from this data tree; absent = the test harness chooses for each run.
        # "server-py2" = Python and SDL from the shard's file server, exactly as "server" (no local override:
        # the page passes neither /LocalPython nor /LocalSDL, so CreatePatcher requests both, RequestGameCode(true,
        # true)), run by the py2 build (plClient-py2, CPython 2.3.3) for a shard whose python.pak is Python 2.3
        # marshal data (MOULa). Patcher deployments only. The build is the deployment's, never the page's choice.
        # A registered extension's modes (EXTENSIONS) are its own to check (its load, below).
        self.game_code = data.get("gameCode")
        modes = game_codes()
        if self.game_code not in (None,) + modes:
            _fail(f"data.gameCode must be {', '.join(modes[:-1])} or {modes[-1]}")
        self.build = "py2" if self.game_code == "server-py2" else "py3"
        # "patcher" = the original patchers fill a per-deployment OPFS install directory from the shard's file
        # server; the data tree (optional) is then the client package (the files a native installer carries beyond the
        # shard's manifests). "http" (default) = the served data tree (dev/harness path).
        self.files = data.get("files", "http")
        if self.files not in ("http", "patcher"):
            _fail("data.files must be http or patcher")
        if self.files == "patcher" and self.game_code == "local":
            _fail("data.files patcher takes the game code from the shard (gameCode server or server-py2)")
        if self.game_code == "server-py2" and self.files != "patcher":
            _fail("data.gameCode server-py2 is for patcher deployments (data.files patcher)")
        for extension in EXTENSIONS:
            extension.load(self, data, base)
        # The fonts tree (FONT_FACES): no entry = the free substitutes (FREE_FONTS); root = a directory of the operator's
        # own font files, named as the faces ("Courier New.ttf") or by map (face -> file name under root), e.g. the
        # originals the operator holds a licence for; url = the same tree on another origin, which serves each face as
        # <face>.ttf with its .fetch-manifest. hostCopies = this host's own copies of the original faces, for a loopback
        # harness deployment only (the picture checks compare with the C++ client, which draws with them); server.py
        # serves them only on a loopback bind, and never in a static kit.
        fonts = d.get("fonts", {})
        _object(fonts, FONTS_KEYS, "fonts")
        self.fonts_root = _path(fonts.get("root"), base, "fonts.root", optional=True)
        self.fonts_host = fonts.get("hostCopies", False)
        if not isinstance(self.fonts_host, bool):
            _fail("fonts.hostCopies must be true or false")
        fonts_map = fonts.get("map")
        if fonts_map is not None:
            _object(fonts_map, set(FONT_FACES), "fonts.map")
            if self.fonts_root is None:
                _fail("fonts.map names files under fonts.root; a fonts.url host serves each face as <face>.ttf itself")
            for face, name in fonts_map.items():
                if not isinstance(name, str) or not FONT_FILE_NAME.fullmatch(name) or name in (".", ".."):
                    _fail(f"fonts.map: {face!r} must name one file under fonts.root")
        if self.fonts_host and (self.fonts_root is not None or fonts.get("url") is not None or fonts_map is not None):
            _fail("fonts.hostCopies excludes fonts.root, fonts.url and fonts.map")
        if self.fonts_root is not None:
            self.fonts_source = "root"
            self.font_files = {
                font_file_name(f): self.fonts_root / (fonts_map or {}).get(f, font_file_name(f)) for f in FONT_FACES
            }
            for served, path in self.font_files.items():
                if not path.is_file():
                    _fail(f"fonts.root: the file for {served} ({path.name}) is missing")
        elif self.fonts_host:
            # the private test harness's server names the host's copies (loopback only)
            self.fonts_source, self.font_files = "host", None
        else:
            self.fonts_source, self.font_files = "free", free_font_files()
        sound = d.get("sound", {})
        _object(sound, {"root", "url"}, "sound")
        self.sound_root = _path(sound.get("root"), base, "sound.root", optional=True)
        # Optional public URLs of the same trees on another origin (an object store, the shard's own HTTP
        # file host) for a static page; absent = served next to the page (server.py, dev/harness path).
        # With data.files patcher, the original patcher over the shard's file server replaces the data/sound trees.
        self.data_url, self.fonts_url, self.sound_url = (
            _https_url(x.get("url"), f"{n}.url") for n, x in (("data", data), ("fonts", fonts), ("sound", sound))
        )

        g = d.get("gateway", {})
        _object(g, GATEWAY_KEYS, "gateway")
        self.gateway_url = _ws_url(g["url"]) if g.get("url") is not None else None
        self.gateway_bind = g.get("bind", "127.0.0.1")
        if not isinstance(self.gateway_bind, str) or not self.gateway_bind:
            _fail("gateway.bind must be an address")
        self.gateway_port = g.get("port", 0)
        if not isinstance(self.gateway_port, int) or not 0 <= self.gateway_port < 65536:
            _fail("gateway.port must be 0..65535")
        self.gateway_origins = _origins(g.get("origins", []))
        self.public_hosts = list(g.get("publicHosts", []))
        for h in self.public_hosts:
            u = urlsplit("https://" + h) if isinstance(h, str) else None
            if u is None or not u.hostname or u.username or u.password or u.path or u.query or "@" in h:
                _fail(f"gateway.publicHosts: invalid authority {h!r}")
        self.extra_hosts = [_host(h, "gateway.extraUpstreamHosts") for h in g.get("extraUpstreamHosts", [])]
        # Game servers are named by the auth server at link time (an IPv4 address): a shard whose game
        # servers are not fixed hosts lists their address ranges here (CIDR, IPv4), at the client port.
        try:
            self.extra_networks = [ipaddress.IPv4Network(n) for n in g.get("extraUpstreamNetworks", [])]
        except (TypeError, ValueError):
            _fail("gateway.extraUpstreamNetworks must be IPv4 CIDR networks")
        if any(n.prefixlen < 16 for n in self.extra_networks):
            _fail("gateway.extraUpstreamNetworks: a network wider than /16 is an open relay")
        # The source ranges of the reverse proxies in front of the gateway (CIDR, IPv4 or IPv6): from a peer inside one,
        # the gateway takes the client's address from the proxy's CF-Connecting-IP header; from any other peer it
        # ignores that header and uses the peer's address.
        try:
            self.trusted_proxies = [ipaddress.ip_network(n) for n in g.get("trustedProxies", [])]
        except (TypeError, ValueError):
            _fail("gateway.trustedProxies must be CIDR networks")
        if any(n.prefixlen == 0 for n in self.trusted_proxies):
            _fail("gateway.trustedProxies: a /0 range would let any client name its own address")
        self.resolve_upstreams = g.get("resolveUpstreams", True)
        if not isinstance(self.resolve_upstreams, bool):
            _fail("gateway.resolveUpstreams must be true or false")
        self.tls_cert = _path(g.get("tlsCert"), base, "gateway.tlsCert", optional=True)
        self.tls_key = _path(g.get("tlsKey"), base, "gateway.tlsKey", optional=True)
        if bool(self.tls_cert) != bool(self.tls_key):
            _fail("gateway TLS requires both tlsCert and tlsKey")

        # Join tokens (required for any deployment that is not a loopback harness one) and limits
        join = g.get("join")
        self.join_secret_file, self.join_ttl = None, DEFAULT_JOIN_TTL
        if join is not None:
            _object(join, JOIN_KEYS, "gateway.join")
            if ("secretFile" in join) == ("secretFileEnv" in join):
                _fail("gateway.join names its key file by exactly one of secretFile and secretFileEnv")
            if "secretFileEnv" in join:
                # The key file's absolute path is the named variable's value (the value is never printed).
                env = join["secretFileEnv"]
                if not isinstance(env, str) or not ENV_NAME_RE.fullmatch(env):
                    _fail("gateway.join.secretFileEnv must be an environment variable name ([A-Z_][A-Z0-9_]*)")
                value = os.environ.get(env, "")
                if not value:
                    _fail(f"gateway.join.secretFileEnv: {env} is not set (the join key file's path)")
                if not Path(os.path.expanduser(value)).is_absolute():
                    _fail(f"gateway.join.secretFileEnv: {env} must hold an absolute path")
                self.join_secret_file = _path(value, base, f"gateway.join.secretFileEnv ({env})")
            else:
                self.join_secret_file = _path(join.get("secretFile"), base, "gateway.join.secretFile")
            if self.join_secret_file.is_relative_to(ROOT):
                _fail("gateway.join's key file must live outside the repository")
            self.join_ttl = join.get("ttlSeconds", DEFAULT_JOIN_TTL)
            if not isinstance(self.join_ttl, int) or not 10 <= self.join_ttl <= 300:
                _fail("gateway.join.ttlSeconds must be 10..300")
        limits = g.get("limits", {})
        _object(limits, set(LIMIT_DEFAULTS), "gateway.limits")
        self.limits = dict(LIMIT_DEFAULTS)
        for k, v in limits.items():
            if (
                not isinstance(v, int)
                or isinstance(v, bool)
                or v < 0
                or (k in ("bytesPerSecond", "idleSeconds") and v < 1)
            ):
                _fail(f"gateway.limits.{k} must be a non-negative integer (bytesPerSecond, idleSeconds >= 1)")
            if k == "isolateQueueBytes" and v < RELAY_QUEUE_BYTES:
                _fail(f"gateway.limits.isolateQueueBytes must be at least {RELAY_QUEUE_BYTES} (one connection's queue)")
            self.limits[k] = v
        caps = d.get("capabilities", {})
        _object(caps, CAPABILITY_KEYS, "capabilities")
        self.harness = caps.get("harness", False)
        if not isinstance(self.harness, bool):
            _fail("capabilities.harness must be true or false")
        if self.harness and self.gateway_bind not in ("127.0.0.1", "localhost", "::1"):
            _fail("capabilities.harness is for loopback test deployments only")
        if self.fonts_host and not self.harness:
            _fail(
                "fonts.hostCopies serves this host's own font copies: loopback harness deployments only "
                "(capabilities.harness)"
            )
        # A diagnostic, default off: the client writes the shard's game code into the page's own OPFS
        # (Diagnostics/gamecode/<name>/). The capture is part of the test-hook overlay (a harness build,
        # the private test harness's, armed by its page), so it needs a loopback harness
        # deployment: never a player or public one.
        self.game_code_capture = caps.get("gameCodeCapture", False)
        if not isinstance(self.game_code_capture, bool):
            _fail("capabilities.gameCodeCapture must be true or false")
        if self.game_code_capture and self.gateway_bind not in ("127.0.0.1", "localhost", "::1"):
            _fail("capabilities.gameCodeCapture is for loopback deployments only")
        # an extension's own refusals (a mode's scope first, so it is refused by mode whatever else it names)
        for extension in EXTENSIONS:
            extension.check(self)
        if self.game_code_capture and not self.harness:
            _fail(
                "capabilities.gameCodeCapture is a test harness diagnostic: it needs capabilities.harness (a harness "
                "build on the test harness server)"
            )
        self.initial_graphics_ini = None
        graphics = d.get("graphics", {})
        _object(graphics, GRAPHICS_KEYS, "graphics")
        if "initialIni" in graphics:
            self.initial_graphics_ini = initial_graphics_ini(graphics["initialIni"])
        self.hdpack_root, self.hdpack_url, self.hdpack_ages = None, None, []
        textures = d.get("textures", {})
        _object(textures, TEXTURES_KEYS, "textures")
        if "hdPack" in textures:
            hd = textures["hdPack"]
            _object(hd, HDPACK_KEYS, "textures.hdPack")
            if ("root" in hd) == ("url" in hd):
                _fail("textures.hdPack needs exactly one of root and url")
            ages = hd.get("ages")
            if (
                not isinstance(ages, list)
                or not ages
                or len(ages) > 64
                or not all(isinstance(a, str) and HDPACK_AGE.fullmatch(a) for a in ages)
                or len({a.lower() for a in ages}) != len(ages)
            ):
                _fail("textures.hdPack.ages must be 1-64 unique Age names of [A-Za-z0-9_-]")
            self.hdpack_ages = list(ages)
            if "root" in hd:
                self.hdpack_root = _path(hd["root"], base, "textures.hdPack.root")
                if self.hdpack_root.is_relative_to(ROOT):
                    _fail("textures.hdPack.root must live outside the repository (the packs never go into Git)")
                if not self.hdpack_root.is_dir():
                    _fail("textures.hdPack.root is not a directory")
                for a in ages:
                    if not (self.hdpack_root / f"{a}.hdpack").is_file():
                        _fail(f"textures.hdPack: {a}.hdpack is missing under the root")
            else:
                self.hdpack_url = _https_url(hd["url"], "textures.hdPack.url")
        source = d.get("source", {})
        _object(source, SOURCE_KEYS, "source")
        self.source_repository = _https_url(source.get("repository"), "source.repository")
        self.source_tag = source.get("tag")
        if self.source_tag is not None and not (
            isinstance(self.source_tag, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/+-]{0,127}", self.source_tag)
        ):
            _fail("source.tag must be a git tag name")
        self.third_party_sources = _https_url(source.get("thirdPartySources"), "source.thirdPartySources")
        if not self.harness and self.join_secret_file is None:
            _fail("a deployment that is not a loopback harness one needs gateway.join (the gateway's admission tokens)")

    def require_source(self):
        """Whoever serves the page and the client conveys the client's object code, and must offer its Corresponding
        Source (GPL version 3, section 6): server.py and emit_static.py refuse a deployment that names no
        source.repository, the link of every source.json, and so does the check (main), with this one rule. A
        loopback harness deployment is exempt: it serves the test harness's own runs, never players."""
        if not self.harness and self.source_repository is None:
            _fail(
                "source.repository: a deployment that is not a loopback harness one must name the repository that "
                "publishes its source (an https:// URL), the link of its source.json"
            )

    def upstreams(self, resolver=socket.getaddrinfo):
        """The (host, port) pairs the gateway may open: the ini's hosts and the extra hosts at
        the client port, plus (resolveUpstreams) their IPv4 addresses, since a game server
        address arrives from the auth server as an IPv4 address (the AgeReply's game server node,
        AgeRequestTrans::Post's m_gameSrvNode, plNglAuth.cpp:2174; NetCliGameStartConnect in
        INetCliAuthAgeRequestCallback, plNetClientComm.cpp:518)."""
        hosts = set(self.ini_hosts) | set(self.extra_hosts)
        pairs = {(h, self.client_port) for h in hosts}
        resolved = {}
        if self.resolve_upstreams:
            for h in sorted(hosts):
                try:
                    infos = resolver(h, self.client_port, socket.AF_INET, socket.SOCK_STREAM)
                except OSError:
                    infos = []
                addrs = sorted({i[4][0] for i in infos})
                resolved[h] = addrs
                pairs |= {(a, self.client_port) for a in addrs}
        return frozenset(pairs), resolved

    def public_document(self, gateway_url=None, status_relay=None):
        """The schema-1 page document (config.mjs validateConfiguration); URLs only.

        shard.name keys the login dialog's remembered login; shard.status is status_relay (a same-origin
        path that relays the ini's Server.Status URL, server.py /runtime/status) when the ini names one."""
        if self.data_root is None and not self.data_url and self.files != "patcher":
            _fail(
                "this shard has no game-file tree: its files come only through the original patcher "
                "(data.files patcher)"
            )
        url = gateway_url or self.gateway_url
        if not url:
            _fail("no gateway URL: set gateway.url or pass the harness gateway URL")
        doc = {
            "schema": 1,
            "roots": {"fonts": self.fonts_url or "/fonts"},
            "gateway": _ws_url(url),
            "shard": {
                "host": self.entry_host,
                "port": self.client_port,
                "serverIni": "/runtime/server.ini",
                "sha256": self.server_ini_sha256,
                "name": self.name,
            },
        }
        if self.data_root is not None or self.data_url:
            doc["roots"]["data"] = self.data_url or self.url_prefix
        if self.files == "patcher":
            doc["files"] = "patcher"
        for extension in EXTENSIONS:
            extension.document(self, doc)
        if self.game_code == "server-py2":
            # the page loads build/plClient-py2.js (boot.mjs); config.mjs requires build py2 with this mode and
            # refuses it with any other
            doc["gameCode"] = "server-py2"
            doc["build"] = "py2"
        if status_relay and self.status_url:
            doc["shard"]["status"] = status_relay
        if self.sound_url or self.sound_root is not None:
            doc["roots"]["sound"] = self.sound_url or "/sound"
        if self.harness:  # gameCodeCapture only with harness (checked at load)
            doc["capabilities"] = {"harness": True}
            if self.game_code_capture:
                doc["capabilities"]["gameCodeCapture"] = True
        if self.join_secret_file is not None:
            doc["join"] = "/runtime/join"  # same-origin token endpoint (server.py, or the Worker)
        if self.initial_graphics_ini is not None:
            doc["graphics"] = {"initialIni": dict(self.initial_graphics_ini)}
        if self.hdpack_ages:  # the HD texture packs
            doc["textures"] = {"hdPack": {"base": self.hdpack_url or "/hdpack", "ages": list(self.hdpack_ages)}}
        return doc

    def summary(self):
        ups, resolved = self.upstreams()
        return {
            "deployment": str(self.path),
            "name": self.name,
            "serverIni": {
                "path": str(self.server_ini_path),
                "sha256": self.server_ini_sha256,
                "entryHost": self.entry_host,
                "clientPort": self.client_port,
                "hosts": sorted(self.ini_hosts),
                "settings": sorted(k for k in self.settings),
            },
            "data": {
                "root": str(self.data_root) if self.data_root else None,
                "url": self.data_url,
                "urlPrefix": self.url_prefix,
                "gameCode": self.game_code,
                "files": self.files,
                "build": self.build,
                "client": CLIENT_NAMES[self.build],
                **{k: v for extension in EXTENSIONS for k, v in extension.summary(self).items()},
            },
            "fonts": {
                "source": self.fonts_source,
                "root": str(self.fonts_root) if self.fonts_root else None,
                "url": self.fonts_url,
                "files": {n: str(p) for n, p in self.font_files.items()} if self.font_files else None,
            },
            "sound": {"root": str(self.sound_root) if self.sound_root else None},
            "gateway": {
                "url": self.gateway_url,
                "bind": self.gateway_bind,
                "port": self.gateway_port,
                "origins": self.gateway_origins,
                "publicHosts": self.public_hosts,
                "upstreams": sorted(f"{h}:{p}" for h, p in ups),
                "resolved": resolved,
                "upstreamNetworks": [f"{n}:{self.client_port}" for n in self.extra_networks],
                "tls": bool(self.tls_cert),
                "trustedProxies": [str(n) for n in self.trusted_proxies],
                "join": {"enabled": self.join_secret_file is not None, "ttlSeconds": self.join_ttl},
                "limits": self.limits,
            },
            "capabilities": {"harness": self.harness, "gameCodeCapture": self.game_code_capture},
            "graphics": {"initialIni": self.initial_graphics_ini},
            "textures": {
                "hdPack": {
                    "root": str(self.hdpack_root) if self.hdpack_root else None,
                    "url": self.hdpack_url,
                    "ages": self.hdpack_ages,
                }
            },
        }


def connect_sources(doc):
    """The connect-src sources of the page that runs this page document (public_document): what the page and the
    client may fetch or open a socket to.

    'self' is the page's own origin. It serves the document itself (config.mjs loads it from there only), the client
    build, server.ini and the same-origin join and status endpoints. After it comes the origin of every gateway,
    join, status, server.ini and asset-root URL the document names on another origin, in document order. A relative
    URL resolves against the document on the page's origin, so it is 'self'. A root a registered extension adds
    (EXTENSIONS) follows the sound root."""
    roots, shard = doc.get("roots", {}), doc.get("shard", {})
    hd = doc.get("textures", {}).get("hdPack", {})
    urls = (
        doc.get("gateway"),
        doc.get("join"),
        shard.get("status"),
        shard.get("serverIni"),
        roots.get("data"),
        roots.get("fonts"),
        roots.get("sound"),
        *(v for k, v in roots.items() if k not in ("data", "fonts", "sound")),
        hd.get("base"),
    )
    sources = ["'self'"]
    for url in urls:
        if url is None:
            continue
        u = urlsplit(url)
        if not u.scheme and not u.netloc:
            continue
        try:
            port = u.port
        except ValueError:
            _fail(f"{url!r}: invalid port")
        # A CSP host source is a DNS name or an IPv4 address: there is no IPv6 literal form (CSP3 host-part).
        if u.scheme not in ("https", "wss", "http", "ws") or not re.fullmatch(r"[a-z0-9.-]+", u.hostname or ""):
            _fail(f"{url!r} cannot be named in the page's Content-Security-Policy")
        default = 443 if u.scheme in ("https", "wss") else 80
        source = f"{u.scheme}://{u.hostname}" + (f":{port}" if port not in (None, default) else "")
        if source not in sources:
            sources.append(source)
    return sources


def content_security_policy(doc):
    """The Content-Security-Policy header of the page that runs this page document: emit_static.py writes it into the
    kit's _headers and server.py --deployment sends it. connect-src only: it bounds every fetch and WebSocket of the
    page and of the client's workers to the deployment's own endpoints."""
    return "connect-src " + " ".join(connect_sources(doc))


def initial_graphics_ini(value):
    """graphics.initialIni checked against INITIAL_GRAPHICS_INI: every required setting, nothing else,
    each of its kind and in its range. Returns the settings in the writer's order."""
    _object(value, set(INITIAL_GRAPHICS_INI), "graphics.initialIni")
    out = {}
    for key, (kind, low, high, required) in INITIAL_GRAPHICS_INI.items():
        if key not in value:
            if required:
                _fail(f"graphics.initialIni: {key} is required")
            continue
        v = value[key]
        if kind in ("bool", "int-bool"):
            ok = isinstance(v, bool)
        elif kind == "int":
            ok = isinstance(v, int) and not isinstance(v, bool) and low <= v <= high
        else:
            ok = isinstance(v, (int, float)) and not isinstance(v, bool) and low <= v <= high
        if not ok:
            _fail(
                f"graphics.initialIni: {key} must be "
                + ("true or false" if kind in ("bool", "int-bool") else f"{kind} {low}..{high}")
            )
        out[key] = v
    return out


def load(path):
    return Deployment(path)


def pak_build_refusal(pak, build):
    """None when every entry of this python.pak runs on the build, else the refusal text (marshal families,
    leading bytes, type codes and first opcodes found)."""
    from plasma_web import analyse_gamecode

    data = Path(pak).read_bytes()
    if analyse_gamecode.secure_magic(data):
        return f"{Path(pak).name} is encrypted; a sealed pak is plaintext"
    try:
        entries = analyse_gamecode.read_pak(data)
    except (ValueError, struct.error) as e:
        return f"{Path(pak).name} is not a plPythonPack file: {e}"
    rows = []
    for name, _, _, raw in entries:
        fam, det = analyse_gamecode.classify(raw)
        rows.append({"name": name, "family": fam, "details": det})
    c = analyse_gamecode.family_check(rows, build)
    if c["ok"]:
        return None
    return (
        f"{Path(pak).name} does not run on the {build} build ({c['client']}, {c['python']}): "
        + "; ".join(f"{k} x{v}" for k, v in sorted(c["reasons"].items()))
        + f" (found families {c['found']['families']}, leading bytes {c['found']['leading_bytes']}, "
        f"first opcodes {c['found']['first_opcodes']})"
    )


def client_build_files(build_dir, build):
    """The build's (<name>.js, <name>.wasm) in build_dir, checked to exist and to be that Python ABI's build
    (ABI_MARKERS); raises DeploymentError otherwise."""
    name = CLIENT_NAMES[build]
    js, wasm = Path(build_dir) / f"{name}.js", Path(build_dir) / f"{name}.wasm"
    if not js.is_file() or not wasm.is_file():
        _fail(f"the {build} build {name}.js and {name}.wasm are not in {build_dir}")
    data = wasm.read_bytes()
    found = [b for b, marker in ABI_MARKERS.items() if marker in data]
    if found != [build]:
        _fail(f"{wasm} is not a {build} build (Python ABI markers found: {found or 'none'})")
    return js, wasm


def join_secret(dep):
    """The deployment's join key bytes (None without gateway.join)."""
    if dep.join_secret_file is None:
        return None
    from plasma_web import join_token

    return join_token.read_secret(dep.join_secret_file)


def main(argv=None):
    """The command line (plasma-web-deployment): check a deployment and print its summary, or create its join key
    file. The check refuses what the servers and the static kit refuse at start, by their own rules (load, then
    require_source); creating the key checks only what load does, so a key can precede the file's source entry."""
    import argparse
    import sys

    ap = argparse.ArgumentParser(
        description="Check a deployment file and print its summary (JSON), or create the file of its gateway join key."
    )
    what = ap.add_mutually_exclusive_group(required=True)
    what.add_argument("deployment", nargs="?", help="the deployment file whose summary is printed")
    what.add_argument(
        "--init-join-secret",
        metavar="DEPLOYMENT",
        help="create the deployment's join key file (gateway.join, mode 0600) if it does not exist; "
        "the key is never printed",
    )
    a = ap.parse_args(argv)
    try:
        dep = load(a.init_join_secret or a.deployment)
    except (OSError, ValueError) as e:
        print(f"invalid deployment: {e}", file=sys.stderr)
        raise SystemExit(1)
    if a.init_join_secret:
        from plasma_web import join_token

        if dep.join_secret_file is None:
            print("the deployment has no gateway.join", file=sys.stderr)
            raise SystemExit(1)
        created = join_token.init_secret(dep.join_secret_file)
        print(f"join key {'created' if created else 'present'}: {dep.join_secret_file}")
        raise SystemExit(0)
    try:
        dep.require_source()  # server.py and emit_static.py refuse such a deployment at start; the check does too
        print(json.dumps(dep.summary(), indent=1))
    except (OSError, ValueError) as e:
        print(f"invalid deployment: {e}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
