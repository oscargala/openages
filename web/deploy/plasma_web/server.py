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
"""The origin server of the browser client.

Serves the player page and one deployment's game trees over HTTP or HTTPS, with Cross-Origin-Opener-Policy and
Cross-Origin-Embedder-Policy (SharedArrayBuffer needs them), correct MIME types, strong content ETags, conditional
requests and HTTP Range support, and with --deployment the deployment's Content-Security-Policy (connect-src,
deployment.py content_security_policy):
  /             the player page: index.html and boot.mjs from --web-root, and no other file there (Handler.page_files,
                the two files a static kit carries at its root, emit_static.py PAGE_FILES)
  /build/       the client build (--build-dir): plClient.js and plClient.wasm, or the deployment's build by its own name
                (plClient-py2.* for data.gameCode server-py2)
  /input/       the page's input modules (capture.mjs, transport.mjs, fullscreen.mjs)
  /data/        the game data, read-only, whitelisted (dat, sfx, python, SDL, avi, resource.dat, server.ini); Python and
                SDL only in the local game-code layout; /data/.fetch-manifest lists the tree for WasmFS and
                /data/.asset-catalog holds every file's size and ETag
  /fonts/       the two text faces of the debug and loading text (plTextFont.cpp), each as <face>.ttf from the
                deployment's face -> file map (deployment.py FONT_FACES): by default the free substitutes in
                web/deploy/fonts, then with their licence texts (LICENSE-*.txt), as a static kit carries them
  /sound/       the sound tree (the deployment's sound.root, else the data tree's sfx/)
  /runtime/     config.mjs, the page document config.json and the shard's server.ini; status, a same-origin relay of
                the ini's Server.Status URL for the login dialog's status text (a page cannot read it cross-origin)
  /runtime/join (POST, same-origin only) one short-lived single-use gateway join token per call
                (join_token.py), when the deployment has gateway.join
  /hdpack/      the optional HD texture packs (<Age>.hdpack) of --hdpack-root or the deployment's textures.hdPack.root
                (CC BY-NC-SA 4.0 content: never from a commercial deployment)
  /source.json, /LICENSE.txt, /COPYING.txt, /THIRD_PARTY_NOTICES.md
                where the source of this page and client is published, the licence and the notices, as a static kit
                carries them (provenance.py). source.json and the notices are made once, at start,
                from the deployment's source entry and the build's release record; a release build whose record or
                checkout does not match is refused at start, as emit_static.py refuses its kit

Without --deployment it serves the data tree --data-root and, with --runtime-config, a page document written by hand.
No path of the machine it runs on is assumed: the build, the data and every other tree it serves are named on the
command line or in the deployment file, and so is a CA bundle for the status relay (--ca-file).

Every request is logged as one JSON line to --access-log: its start time t, its duration ms, the method, path, status,
bytes sent and byte range. A line whose answer ended early also has incomplete: "file changed" (the file changed while
its body was sent), "client gone" (the client closed or reset the connection) or "timed out" (a socket operation timed
out). No other line has it.

Connections: each one is served by its own thread, at most --max-connections at once (the next one is closed
unanswered), and each of its socket operations times out after --timeout seconds: an idle keep-alive connection, a
client that stops sending its request or stops reading the answer, and a TLS handshake, which runs in the connection's
own thread. A request body the server does not read is dropped before the answer, or the connection closes after it.
Every answer carries nosniff; the page and its code (/, index.html, boot.mjs, /input/, /build/) revalidate by their
ETag (no-cache), as a static kit's _headers says, the game trees are private, no-cache and everything else is no-store;
the trees the client reads uncompressed (FETCH_TREES) add no-transform.
The status relay's answer is reused for STATUS_SECONDS and the asset catalog's scan for CATALOG_RESCAN_SECONDS, so an
anonymous request costs neither a request to the shard nor a walk of the data tree. Without --deployment the server
binds loopback only: a non-loopback bind serves a deployment.

Bodies: a file's body, whole or a byte range, is read from the open file while it is sent, at most representations.CHUNK (1 MiB) at a time, and the file's identity is checked after every read. No body is
copied to memory or to a temporary file first: an answer holds at most 1 MiB of its body in memory and writes nothing
to disk, so the bodies being sent hold at most --max-connections MiB together (64 MiB by default). A file that changed
after its ETag was computed is answered 503 before the headers; one that changes while its body is sent ends that
answer incomplete and closes the connection, so a client never receives a complete body that mixes two versions under
one ETag. A file whose path is replaced by rename while its body is sent is not a change: the open file is the old,
now unlinked, inode, whose bytes are the ones the headers described, and its body goes on whole
(representations.Body.unchanged).

Handler and the functions under it are also the base of the private test harness server
(which no release carries). That server subclasses Handler and adds the harness's
routes, options and measurement fields; this one serves none of them.
"""

import argparse
import atexit
from contextlib import ExitStack
from pathlib import Path
import json
import mimetypes
import os
import tempfile
import re
import shutil
import ssl
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from plasma_web import CONFIG_MJS, INPUT_MODULES, PAGE_DIR
from plasma_web import client_limits  # the per-client rate the gateway counts its connects with; standard library only
from plasma_web import deployment as deployment_file
from plasma_web import manifest  # the .fetch-manifest writer
from plasma_web import origins  # the page-origin rule
from plasma_web import provenance  # source.json, the licence files and the notices
from plasma_web.asset_catalog import AssetCatalog
from plasma_web.representations import Body, RepresentationChanged, Representations, blob_tag, matches

HDPACK_NAME = re.compile(r"[A-Za-z0-9_-]{1,64}\.hdpack")  # /hdpack/<Age>.hdpack
DATA_WHITELIST = ["dat", "sfx", "python", "SDL", "avi", "resource.dat", "server.ini"]
# The fonts tree's default: the free substitutes, by served name ("Courier New.ttf" -> web/deploy/fonts/...).
FONT_FILES = {n: str(p) for n, p in deployment_file.free_font_files().items()}
# Their licence texts, served next to them in /fonts as a static kit carries them (emit_static.py emit_fonts):
# "LICENSE-LiberationFonts.txt" -> web/deploy/fonts/..., whenever the fonts tree serves the free substitutes.
FREE_FONT_LICENCES = {
    n["licence_file"]: str(deployment_file.FREE_FONTS_DIR / n["licence_file"])
    for n in deployment_file.FREE_FONT_NOTICES
}
# The trees the client reads uncompressed with strong ETags: the fetch backend's mounts (main.cpp MountFileSystems:
# data, sound, fonts; plEmscriptenFetchBackend.js refuses a weak ETag, a content encoding or no Content-Length) and the
# HD texture packs (plWGPUPipeline.js plWGPUTexturePack_fetch: identity and a Content-Length). A server class serves
# its own trees under the same rule (Handler.fetch_trees).
# Their answers add no-transform, so that a CDN in front of this server (Cloudflare's compresses font/ttf and others,
# then weakens the ETag) leaves them as they are (Docs/Web/Deploying.md, "What a host of the trees must serve").
FETCH_TREES = ("data", "sound", "fonts", "hdpack")
LOOPBACK = ("127.0.0.1", "localhost", "::1")
MIME = {
    ".wasm": "application/wasm",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".html": "text/html; charset=utf-8",
    ".json": "application/json",
    ".txt": "text/plain; charset=utf-8",
    ".md": "text/markdown; charset=utf-8",
}
# The provenance files at the page's root (configure_provenance), with the page's cache policy.
PROVENANCE_FILES = ("source.json", "LICENSE.txt", "COPYING.txt", "THIRD_PARTY_NOTICES.md")


# The default of --timeout, the seconds each socket operation of a connection may take
# (reading a request, writing an answer, an idle keep-alive connection, a TLS handshake), and of --max-connections, the
# connections served at once.
TIMEOUT = 30
MAX_CONNECTIONS = 64
# A request body the server does not use (every request but an issued join) is read and dropped up to this size; a
# larger, malformed or chunked one closes the connection after the answer instead.
DISCARD_BODY_LIMIT = 65536
# The seconds a status relay answer is reused. The native client fetches its status text again every 30 s
# while its login dialog is open (winmain.cpp UPDATE_STATUSMSG_SECONDS).
STATUS_SECONDS = 10
# The seconds an asset catalog scan answers for (asset_catalog.py interval).
CATALOG_RESCAN_SECONDS = 10
# A Content-Length value skip_body reads (digits only: no sign, no space inside, no exponent)
CONTENT_LENGTH = re.compile(r"[0-9]{1,18}")


CORS_READ_HEADERS = frozenset(("range", "if-match", "if-none-match", "if-range"))
# Content-Length too: the texture pack fetch sizes its one allocation by it. It is CORS-safelisted in the Fetch
# standard, and is exposed explicitly all the same.
CORS_EXPOSE_HEADERS = "ETag, Content-Length, Content-Range, Content-Encoding, Accept-Ranges, X-Plasma-Asset-Cache"


# The --asset-origin check (origins.page_origins) and the tree listing (manifest.tree_manifest), under the names the
# test harness server and the tests import from this module.
validate_asset_origins = origins.page_origins
build_manifest = manifest.tree_manifest


def target_path(urlpath):
    """The path of a request target, without its query or fragment, as sent (not percent-decoded)."""
    return urllib.parse.urlsplit(urlpath).path


def path_segments(urlpath):
    """The segments of a request target's path, percent-decoded, with empty and "." segments dropped; None for a path
    that climbs out ("..") or has a segment with a backslash or a control character."""
    parts = [p for p in urllib.parse.unquote(target_path(urlpath)).split("/") if p not in ("", ".")]
    if any(p == ".." or "\\" in p or re.search(r"[\x00-\x1f\x7f]", p) for p in parts):
        return None
    return parts


def relay_status(url, cafile=None):
    """The login dialog's status text from the deployment's own Server.Status URL (never a URL from the
    request). As the native status thread (UruLoginDialogProc's LoginDialogShardStatus thread and its CurlCallback,
    plClient/win32/winmain.cpp): the response body whatever its HTTP status, at most 255 bytes, redirects followed;
    a failed request answers with its error text.

    An https URL is verified, as libcurl verifies it, against a CA store: cafile (--ca-file), else Python's default
    store, which honours SSL_CERT_FILE and SSL_CERT_DIR. A python.org build of Python ships no store of its own: name
    the system's bundle with either: the server assumes no host path."""
    import urllib.error
    import urllib.request

    request = urllib.request.Request(url, headers={"User-Agent": "UruClient/1.0"})
    try:
        with urllib.request.urlopen(request, timeout=5, context=ssl.create_default_context(cafile=cafile)) as response:
            return response.read(255)
    except urllib.error.HTTPError as e:
        return e.read(255)
    except (urllib.error.URLError, OSError, ValueError) as e:
        return str(getattr(e, "reason", e)).encode("utf-8", "replace")[:255]


class StatusRelay:
    """The status relay (relay_status) with its answer reused for `seconds`: /runtime/status is anonymous, so a
    request must not cost a request to the shard. One fetch at a time: a request that arrives during
    one waits for its answer. A failed fetch's error text is reused the same way."""

    def __init__(self, url, cafile=None, seconds=STATUS_SECONDS, clock=time.monotonic, fetch=relay_status):
        self.url, self.cafile, self.seconds, self.clock, self.fetch = url, cafile, seconds, clock, fetch
        self.lock, self.body, self.at = threading.Lock(), None, None

    def read(self):
        with self.lock:
            if self.body is None or self.clock() - self.at >= self.seconds:
                self.body = self.fetch(self.url, self.cafile)
                self.at = self.clock()
            return self.body


class JoinIssuer:
    """Issues join tokens for one deployment (join_token.issue), at most rate's limit per client a minute.

    rate is a client_limits.ClientRate, the helper the gateway counts its connects with: the client is
    the TCP peer, or behind one of the deployment's trusted proxies the address that proxy passes on, keyed by
    client_limits.ip_key, in a table bounded to client_limits.RATE_TABLE_ENTRIES clients."""

    def __init__(self, secret, name, ttl, rate):
        from plasma_web import join_token

        self.join_token, self.secret, self.name, self.ttl, self.rate = join_token, secret, name, ttl, rate

    def admit(self, peer, headers):
        """None when the client of this request (peer and its headers) may have a token now, else the refusal
        (status, body)."""
        address, refused = self.rate.client(peer, headers.get_all(client_limits.CLIENT_IP_HEADER, []))
        if refused is not None:
            return int(refused[0]), (refused[1] + "\n").encode()
        if not self.rate.hit(address):
            return 429, b"Too many join requests\n"
        return None

    def issue(self):
        return self.join_token.issue(self.secret, self.name, self.ttl)


class Handler(BaseHTTPRequestHandler):
    """One connection's requests. What it serves is read from self.server.cfg (configure() below, or a test's own
    namespace); a subclass may add routes in resolve() and change the steps of an answer (respond, describe,
    read_catalog, send_body, log_access)."""

    protocol_version = "HTTP/1.1"
    # The Server header's product token (BaseHTTPRequestHandler sends it with Python's version).
    server_version = "plasma-web-origin/1"
    # The only files of the web root that are served: the player page and its module. The web root may hold other
    # files (by default it is this source directory, with this server's own modules and other scripts in it).
    page_files = ("index.html", "boot.mjs")
    # fonts.hostCopies names this host's own copies of the two original faces. They are licensed for the machine they
    # are on and never redistributed: a deployment that asks for them is refused unless the server class names them
    # here (a face -> file map). This one names none.
    host_fonts = None
    # capabilities.gameCodeCapture records the shard's game code in the page's OPFS, and only an export page on
    # the same origin can read it out. This server has no export page, so it refuses such a deployment.
    capture_export = False
    # The game-code modes a registered extension adds (plasma_web.deployment EXTENSIONS: the test harness's client
    # game code, test harness modes outside the scope of a release) are served only by a server class that names them
    # here (the test harness server). This one names none, and refuses such a deployment at start.
    game_codes = ()
    # The trees whose answers add no-transform (FETCH_TREES); a server class adds its own trees here.
    fetch_trees = FETCH_TREES
    # The seconds each socket operation of this connection may take (StreamRequestHandler
    # sets it on the socket). OriginServer passes --timeout; a server built without one keeps this default.
    timeout = TIMEOUT

    def setup(self):
        self.timeout = getattr(self.server, "connection_timeout", self.timeout)
        # True once this request's answer must close the connection: its body was not read (skip_body).
        self.closing = False
        super().setup()

    def handle_one_request(self):
        self.closing = False
        super().handle_one_request()

    def send_response(self, code, message=None):
        """Every answer, send_error's included, carries nosniff, and Cache-Control no-store unless its route set its
        own."""
        super().send_response(code, message)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.cache_control = False

    def send_header(self, keyword, value):
        if keyword.lower() == "cache-control":
            self.cache_control = True
        super().send_header(keyword, value)

    def end_headers(self):
        if getattr(self, "cache_control", None) is False:
            self.send_header("Cache-Control", "no-store")
        self.cache_control = None
        if self.closing and not self.close_connection:
            self.send_header("Connection", "close")
        super().end_headers()

    def skip_body(self):
        """Reads and drops the body of a request whose answer does not use it, so the connection's next request
        starts where this one ends, not inside its body. A chunked or malformed body, or one
        over DISCARD_BODY_LIMIT, is not read: the answer closes the connection instead."""
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get_all("Transfer-Encoding", []) or len(lengths) > 1:
            self.closing = True
            return
        if not lengths:
            return
        length = lengths[0].strip()
        if not CONTENT_LENGTH.fullmatch(length) or int(length) > DISCARD_BODY_LIMIT:
            self.closing = True
            return
        left = int(length)
        while left:
            chunk = self.rfile.read(min(left, 1 << 16))
            if not chunk:
                self.closing = True
                return
            left -= len(chunk)

    def log_message(self, fmt, *args):  # quiet stderr; the JSON access log is the record
        pass

    def url_parts(self, urlpath):
        """The request path's segments (path_segments), the asset prefix mapped to data; None for a path that climbs out
        (..) or has a segment with a backslash or a control character."""
        cfg = self.server.cfg
        parts = path_segments(urlpath)
        if parts is None:
            return None
        prefix = getattr(cfg, "asset_prefix", "/data").strip("/").split("/")
        if prefix != ["data"] and parts[: len(prefix)] == prefix:
            parts = ["data", *parts[len(prefix) :]]
        return parts

    def resolve(self, urlpath):
        """What a GET or HEAD of urlpath serves: (file path, None), (None, body bytes), or (None, None) for a 404."""
        cfg = self.server.cfg
        parts = self.url_parts(urlpath)
        if parts is None:
            return None, None
        if not parts:
            return os.path.join(cfg.web_root, "index.html"), None
        head, rest = parts[0], parts[1:]
        if head == "build":
            # the build by its own name (a deployment's page loads build/<name>.js, config.mjs build):
            # /build/<name>.js and /build/<name>.wasm only, plClient without a deployment
            name = getattr(cfg, "client_name", None) or "plClient"
            if getattr(cfg, "build_dir", None) and len(rest) == 1 and rest[0] in (name + ".js", name + ".wasm"):
                return os.path.join(cfg.build_dir, rest[0]), None
            return None, None
        if head == "input":
            if len(rest) == 1 and rest[0] in ("capture.mjs", "transport.mjs", "fullscreen.mjs"):
                return os.path.join(INPUT_MODULES, rest[0]), None
            return None, None
        if head == "runtime":
            if rest == ["config.mjs"]:
                return str(CONFIG_MJS), None
            if rest == ["config.json"]:
                return None, getattr(cfg, "runtime_config_bytes", None)
            if rest == ["server.ini"]:  # the deployment's own native server.ini (public values)
                return None, getattr(cfg, "runtime_server_ini", None)
            if rest == ["status"] and getattr(cfg, "status", None):  # reused for STATUS_SECONDS (StatusRelay)
                return None, cfg.status.read()
            return None, None
        if head == "sound":
            if rest == [".fetch-manifest"]:
                return None, getattr(cfg, "sound_manifest", None)
            if rest:
                return os.path.join(getattr(cfg, "sound_root", None) or os.path.join(cfg.data_root, "sfx"), *rest), None
            return None, None
        if head == "data":
            if rest == [".fetch-manifest"]:
                return None, cfg.data_manifest
            if not rest or rest[0] not in getattr(cfg, "data_whitelist", DATA_WHITELIST):
                return None, None
            return os.path.join(cfg.data_root, *rest), None
        if head == "hdpack":
            # one pack file per Age, nothing else
            root = getattr(cfg, "hdpack_root", None)
            if root and len(rest) == 1 and HDPACK_NAME.fullmatch(rest[0]):
                return os.path.join(root, rest[0]), None
            return None, None
        if head == "fonts":
            if rest == [".fetch-manifest"]:
                return None, cfg.fonts_manifest
            files = getattr(cfg, "font_files", None) or FONT_FILES
            if len(rest) == 1 and rest[0] in files:
                return files[rest[0]], None
            licences = getattr(cfg, "font_licence_files", None) or {}
            if len(rest) == 1 and rest[0] in licences:
                return licences[rest[0]], None
            return None, None
        if len(parts) == 1 and parts[0] in self.page_files:
            return os.path.join(cfg.web_root, parts[0]), None
        if len(parts) == 1 and parts[0] in getattr(cfg, "provenance_files", {}):
            return cfg.provenance_files[parts[0]], None
        return None, None

    def asset_route(self):
        """Scope CORS to the existing public read-only asset whitelist only."""
        parts = self.url_parts(self.path)
        if parts is None or len(parts) < 2:
            return False
        head, rest = parts[0], parts[1:]
        if head == "data":
            return rest in ([".fetch-manifest"], [".asset-catalog"]) or (
                rest[0] in getattr(self.server.cfg, "data_whitelist", DATA_WHITELIST)
                and (rest[0] not in ("server.ini", "resource.dat") or len(rest) == 1)
            )
        if head == "sound":
            return True
        if head == "hdpack":
            # the HD texture packs (one <Age>.hdpack per Age), read-only like the assets:
            # an exact page origin may read them cross-origin, revalidating a kept pack with If-None-Match
            return len(rest) == 1 and bool(HDPACK_NAME.fullmatch(rest[0]))
        return head == "fonts" and len(rest) == 1 and rest[0] in (*FONT_FILES, ".fetch-manifest")

    def allowed_asset_origin(self):
        values = self.headers.get_all("Origin", [])
        allowed = getattr(self.server.cfg, "asset_origins", ())
        return values[0] if len(values) == 1 and values[0] in allowed else None

    def common_headers(self, ctype, length, etag=None):
        self.send_header("Content-Type", ctype)
        if length is not None:
            self.send_header("Content-Length", str(length))
        if etag is not None:
            self.send_header("ETag", etag)
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        # (X-Content-Type-Options: nosniff comes from send_response, on every answer.)
        # --deployment: the deployment's connect-src (deployment.py content_security_policy), as the static kit's
        # _headers sends it. The page reads it from its own response; the client's workers from /build/*.js.
        csp = getattr(self.server.cfg, "content_security_policy", None)
        if csp:
            self.send_header("Content-Security-Policy", csp)
        if self.asset_route() and getattr(self.server.cfg, "asset_origins", ()):
            vary = "Origin"
            if self.command == "OPTIONS":
                vary += ", Access-Control-Request-Method, Access-Control-Request-Headers"
            self.send_header("Vary", vary)
            origin = self.allowed_asset_origin()
            if origin is not None:
                self.send_header("Access-Control-Allow-Origin", origin)
                self.send_header("Access-Control-Expose-Headers", CORS_EXPOSE_HEADERS)
                # No Allow-Credentials: immutable asset transport never needs accounts.

        # The cache policy follows the path as requested (path_segments: the asset prefix is not mapped here).
        parts = path_segments(self.path)
        if parts is None:  # a path that is never served: no-store
            parts = [None]
        cacheable = (
            len(parts) > 1 and parts[0] == "data" and parts[1] in ("dat", "sfx", "python", "SDL", "avi", "resource.dat")
        )
        # /sound is the existing read-only alias of /data/sfx, with the same policy.
        cacheable = cacheable or (len(parts) > 1 and parts[0] == "sound" and parts[-1] != ".fetch-manifest")
        cacheable = cacheable or (len(parts) == 2 and parts[0] == "hdpack")  # the HD texture packs
        # The page and its code revalidate by their ETag, as the static kit's _headers
        # has them (emit_static.py: /index.html, /boot.mjs, /build/*), the input modules with them; /runtime/* stays
        # no-store.
        page = not parts or (len(parts) == 1 and parts[0] in self.page_files)
        page = page or (len(parts) == 2 and parts[0] in ("build", "input"))
        page = page or (len(parts) == 1 and parts[0] in PROVENANCE_FILES)  # source.json, the licence, the notices
        if parts == ["data", ".asset-catalog"] and etag is not None:
            self.send_header("X-Plasma-Asset-Catalog", "1")
        # A tree's representation adds no-transform (FETCH_TREES), its asset prefix mapped to data as it is served.
        served = self.url_parts(self.path)
        keep = (
            ", no-transform"
            if (etag is not None and served is not None and len(served) > 1 and served[0] in self.fetch_trees)
            else ""
        )
        if cacheable and etag is not None:
            self.send_header("Cache-Control", "private, no-cache" + keep)
            self.send_header("X-Plasma-Asset-Cache", "chunks-v1")
        elif page and etag is not None:
            self.send_header("Cache-Control", "no-cache")
        else:
            self.send_header("Cache-Control", "no-store" + keep)

    def is_catalog(self):
        return path_segments(self.path) == ["data", ".asset-catalog"]

    def read_catalog(self):
        """The asset catalog's current body (asset_catalog.py: every whitelisted file's size and ETag)."""
        blob, _ = self.server.catalog.read()
        return blob

    def describe(self, fs_path, stream):
        """(identity, strong ETag) of an open file: its content hash, kept while the file is unchanged."""
        return self.server.representations.describe(fs_path, stream)

    def send_body(self, payload):
        """Writes a file answer's body (a representations.Body) as it is read from the file, one piece of at most
        representations.CHUNK bytes at a time."""
        while True:
            piece = payload.read()
            if not piece:
                return
            self.wfile.write(piece)
            self.outcome["bytes"] += len(piece)
            del piece  # freed before the next read: one piece in memory at a time

    def stream_file(self, body, length):
        """Sends the body of a file answer whose headers are out. When the file changes or shrinks while its body is
        sent (RepresentationChanged), the headers have already promised `length` bytes of the representation their
        ETag names, so the answer ends incomplete: the connection closes after the bytes sent so far, the client sees
        fewer bytes than Content-Length and discards the answer, and the access log keeps the status that was sent and
        the bytes the client got, marked incomplete."""
        try:
            self.send_body(body)
        except RepresentationChanged as e:
            self.outcome["incomplete"] = "file changed"
            self.close_connection = True
            print(
                f"body ended incomplete ({e}): {json.dumps(target_path(self.path))}, "
                f"{self.outcome['bytes']} of {length} bytes sent; connection closed",
                flush=True,
            )

    def handle_any(self, head_only):
        started, begin = time.time(), time.perf_counter_ns()
        # This request's access-log outcome; respond() and send_body() keep it current, so a broken connection logs
        # the bytes it got.
        self.outcome = {"status": 404, "bytes": 0, "range": None}
        try:
            self.skip_body()
            fs_path, blob = self.resolve(self.path)
            if (
                self.asset_route()
                and getattr(self.server.cfg, "asset_origins", ())
                and self.headers.get_all("Origin", [])
                and self.allowed_asset_origin() is None
            ):
                self.outcome["status"] = 403
                self.send_response(403)
                self.common_headers("text/plain", 0)
                self.end_headers()
                return
            self.respond(fs_path, blob, head_only)
        except RepresentationChanged:
            self.outcome.update(status=503, range=None)
            self.send_response(503)
            self.common_headers("text/plain", 0)
            self.end_headers()
        except (BrokenPipeError, ConnectionResetError):
            # The client is gone, in the middle of an answer or before it: the connection carries no other request, and
            # the log line says the answer ended early.
            self.outcome["incomplete"] = "client gone"
            self.close_connection = True
        except TimeoutError:
            # A client that stopped reading the answer or sending its request: http.server's request handler logs the
            # timeout and closes the connection; the log line says the answer ended early.
            self.outcome["incomplete"] = "timed out"
            raise
        finally:
            self.log_access(started, begin)

    def respond(self, fs_path, blob, head_only):
        """The answer to a GET or HEAD of a resolved file or body: 404, the conditional answers (412, 304), a byte range
        (206, 416) or the whole representation (200)."""
        out = self.outcome
        if self.is_catalog() and getattr(self.server, "catalog", None) is not None:
            blob = self.read_catalog()
        with ExitStack() as stack:
            stream = None
            if blob is None:
                try:
                    if fs_path is None or not os.path.isfile(fs_path):
                        raise FileNotFoundError
                    stream = stack.enter_context(open(fs_path, "rb"))
                except FileNotFoundError:
                    body = b"not found\n"
                    self.send_response(404)
                    self.common_headers("text/plain", len(body))
                    self.end_headers()
                    if not head_only:
                        self.wfile.write(body)
                        out["bytes"] = len(body)
                    return
                identity, etag = self.describe(fs_path, stream)
                size = identity[2]
            else:
                size, etag = len(blob), blob_tag(blob)
            ext = os.path.splitext(fs_path or "")[1].lower()
            ctype = MIME.get(ext) or mimetypes.guess_type(fs_path or "")[0] or "application/octet-stream"
            # RFC 9110: If-Match uses strong comparison and is evaluated before
            # If-None-Match and Range. A changed asset is a zero-body 412.
            if_match = self.headers.get("If-Match")
            if if_match is not None and not matches(if_match, etag):
                out["status"] = 412
                self.send_response(412)
                self.common_headers(ctype, 0, etag)
                self.end_headers()
                return
            if_none = self.headers.get("If-None-Match")
            if if_none is not None and matches(if_none, etag, weak=True):
                out["status"] = 304
                self.send_response(304)
                self.common_headers(ctype, None, etag)
                self.end_headers()
                return
            start, end = 0, size - 1
            out["status"] = 200
            range_hdr = self.headers.get("Range")
            if_range = self.headers.get("If-Range")
            # No Last-Modified validator is advertised; dates/weak tags cannot
            # prove byte identity. A mismatching If-Range gets the whole file.
            if if_range is not None and if_range != etag:
                range_hdr = None
            if range_hdr and not head_only:
                m = re.fullmatch(r"bytes=(\d*)-(\d*)", range_hdr.strip())
                if m and (m.group(1) or m.group(2)):
                    if m.group(1):
                        start = int(m.group(1))
                        end = int(m.group(2)) if m.group(2) else size - 1
                    else:
                        start = max(0, size - int(m.group(2)))
                    end = min(end, size - 1)
                    if start >= size or start > end:
                        out["status"] = 416
                        self.send_response(416)
                        self.common_headers("text/plain", 0, etag)
                        self.send_header("Content-Range", f"bytes */{size}")
                        self.end_headers()
                        return
                    out["status"], out["range"] = 206, f"{start}-{end}"
            length = end - start + 1 if size else 0
            # The body is read from the open file while it is sent, never copied ahead of the answer
            # (representations.Body). A file that changed since describe() is still a 503 before any header
            # (Body.check); one that changes while its body is sent ends the answer incomplete (stream_file).
            body = None
            if stream is not None and not head_only:
                body = Body(stream, identity, start, length)
                body.check()
            self.send_response(out["status"])
            self.common_headers(ctype, length if not head_only else size, etag)
            if out["status"] == 206:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            if head_only:
                return
            if blob is not None:
                self.wfile.write(blob[start : end + 1])
                out["bytes"] = length
            else:
                self.stream_file(body, length)

    def log_access(self, started, begin, path=None):
        """One access-log line for this request: its start time, duration, method, path and outcome."""
        self.write_access(
            {
                "t": round(started, 4),
                "ms": round((time.perf_counter_ns() - begin) / 1e6, 2),
                "method": self.command,
                "path": self.path if path is None else path,
                **self.outcome,
            }
        )

    def write_access(self, record):
        with self.server.log_lock:
            self.server.log_file.write(json.dumps(record) + "\n")
            self.server.log_file.flush()

    def do_OPTIONS(self):
        started, begin = time.time(), time.perf_counter_ns()
        self.skip_body()
        method = self.headers.get_all("Access-Control-Request-Method", [])
        raw_headers = self.headers.get_all("Access-Control-Request-Headers", [])
        requested = [h.strip().lower() for h in raw_headers[0].split(",")] if len(raw_headers) == 1 else []
        allowed = (
            self.asset_route()
            and self.allowed_asset_origin() is not None
            and len(method) == 1
            and method[0] in ("GET", "HEAD")
            and len(raw_headers) <= 1
            and all(h in CORS_READ_HEADERS for h in requested)
            and len(requested) == len(set(requested))
        )
        status = 204 if allowed else 403
        self.send_response(status)
        # A rejected preflight must not carry an otherwise-valid origin permission.
        if allowed:
            # 204 has no content; RFC 9110 8.6 forbids Content-Length on it.
            self.common_headers("text/plain", None)
            self.send_header("Access-Control-Allow-Methods", "GET, HEAD")
            self.send_header("Access-Control-Allow-Headers", ", ".join(sorted(CORS_READ_HEADERS)))
            self.send_header("Access-Control-Max-Age", "0")
        else:
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.outcome = {"status": status, "bytes": 0, "range": None}
        self.log_access(started, begin)

    def do_GET(self):
        self.handle_any(False)

    def do_POST(self):
        """POST /runtime/join -> {"token", "expires"}. Same-origin only (Origin is one of the deployment's page
        origins, cfg.join_origins, never derived from the request's Host; Sec-Fetch-Site same-origin when sent;
        Content-Type application/json, which a cross-site form cannot send without a preflight this server refuses),
        rate-limited per client (JoinIssuer.admit). The token is never logged. The body of any other POST is read and
        dropped (skip_body)."""
        started, begin = time.time(), time.perf_counter_ns()
        cfg = self.server.cfg
        status, body = 404, b"Not found\n"
        path = target_path(self.path)
        if path == "/runtime/join" and getattr(cfg, "join", None):
            join = cfg.join
            origins = self.headers.get_all("Origin", [])
            site = self.headers.get_all("Sec-Fetch-Site", [])
            lengths = self.headers.get_all("Content-Length", ["0"])
            refused = None
            if (
                len(origins) != 1
                or origins[0] not in cfg.join_origins
                or site not in ([], ["same-origin"])
                or (self.headers.get("Content-Type") or "").split(";")[0].strip().lower() != "application/json"
                or self.headers.get_all("Transfer-Encoding", [])
                or len(lengths) != 1
                or not re.fullmatch(r"[0-9]{1,4}", lengths[0].strip())
                or int(lengths[0]) > 1024
            ):
                refused = 403, b"Join refused\n"
            else:
                refused = join.admit(self.client_address[0], self.headers)
            if refused is None:
                self.rfile.read(int(lengths[0]))
                token, expires = join.issue()
                status, body = 200, json.dumps({"token": token, "expires": expires}).encode()
            else:
                status, body = refused
        if status != 200:
            self.skip_body()
        self.send_response(status)
        self.send_header("Content-Type", "application/json" if status == 200 else "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.end_headers()
        self.wfile.write(body)
        self.outcome = {"status": status, "bytes": len(body) if status != 200 else 0, "range": None}
        self.log_access(started, begin, path)

    def do_HEAD(self):
        self.handle_any(True)

    @classmethod
    def prepare_trees(cls, cfg):
        """A server class's own trees, prepared with the others (prepare): this one has none."""

    @classmethod
    def data_manifest(cls, cfg):
        """The /data/.fetch-manifest body: the whitelisted trees of the data root."""
        return manifest.tree_manifest(cfg.data_root, cfg.data_whitelist)

    @classmethod
    def asset_catalog(cls, cfg, representations):
        """The /data/.asset-catalog source: the whitelisted trees of the data root (the deployment's ini is not one)."""
        return AssetCatalog(
            cfg.data_root,
            representations,
            tops=[p for p in cfg.data_whitelist if p != "server.ini"],
            interval=CATALOG_RESCAN_SECONDS,
        )


class OriginServer(ThreadingHTTPServer):
    """The listening server: one thread per connection, at most max_connections at once,
    each connection's socket operations bounded by timeout seconds, and with tls (an ssl.SSLContext) the TLS handshake
    in the connection's own thread under that timeout. The accept loop only accepts: a client that connects and then
    sends nothing, or stalls its handshake, holds one thread until its timeout, never the server."""

    daemon_threads = True

    def __init__(self, address, handler, tls=None, timeout=TIMEOUT, max_connections=MAX_CONNECTIONS):
        self.tls, self.connection_timeout, self.max_connections = tls, timeout, max_connections
        self.slots = threading.BoundedSemaphore(max_connections)
        self.refused, self.refused_said = 0, None
        super().__init__(address, handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            # Every slot is held: this connection is closed unanswered (the client sees a reset or an empty answer)
            # and counted; the count is printed at most once a minute.
            self.refused += 1
            now = time.monotonic()
            if self.refused_said is None or now - self.refused_said >= 60:
                self.refused_said = now
                print(f"connections: {self.max_connections} open, {self.refused} refused so far", flush=True)
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

    def finish_request(self, request, client_address):
        request.settimeout(self.connection_timeout)
        if self.tls is None:
            return super().finish_request(request, client_address)
        try:
            conn = self.tls.wrap_socket(request, server_side=True, do_handshake_on_connect=False)
        except OSError:
            return
        try:
            conn.do_handshake()
            super().finish_request(conn, client_address)
        except OSError:  # a failed or timed-out handshake, or a connection the client dropped
            pass
        finally:
            # wrap_socket took over the accepted socket (shutdown_request then closes a detached one): close it here.
            try:
                conn.close()
            except OSError:
                pass


def options():
    """This server's command line. The test harness server adds its own options to the same parser."""
    ap = argparse.ArgumentParser(
        description="The browser client's origin server: the player page, the client build "
        "and one deployment's game trees."
    )
    ap.add_argument("--port", type=int, default=8613)
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument(
        "--web-root",
        default=str(PAGE_DIR),
        help="the player page's directory; only index.html and boot.mjs are "
        "served from it (default: this source directory)",
    )
    ap.add_argument(
        "--build-dir",
        help="the client build served at /build/: plClient.js and plClient.wasm, or a "
        "deployment's build by its name (plClient-py2.*); without it /build/ serves "
        "nothing",
    )
    ap.add_argument("--data-root", help="without --deployment (and then required): the game data tree served at /data")
    ap.add_argument("--access-log", required=True, help="one JSON line per request is appended to this file")
    ap.add_argument(
        "--asset-prefix", default=None, help="Additional URL prefix for the same read-only asset tree (default /data)"
    )
    ap.add_argument("--runtime-config", help="Credential-free deployment JSON served at /runtime/config.json")
    ap.add_argument(
        "--deployment",
        help="the operator deployment file (deployment.py): the data/fonts/sound "
        "trees, the game-code mode, and the shard's server.ini, served at /runtime/server.ini "
        "with the derived public page document at /runtime/config.json",
    )
    ap.add_argument(
        "--gateway-url",
        help="with --deployment: the gateway URL for the page document when the "
        "file has none (a loopback gateway started next to this server)",
    )
    ap.add_argument(
        "--asset-origin",
        action="append",
        default=[],
        help="Exact HTTPS or loopback HTTP page origin allowed to read public assets; repeat, no credentials",
    )
    ap.add_argument(
        "--fonts-root",
        help='Directory holding the two text faces as <face>.ttf ("Courier New.ttf", '
        '"Trebuchet MS Bold.ttf"); default: the free substitutes in web/deploy/fonts',
    )
    ap.add_argument("--hdpack-root", help="directory of <Age>.hdpack files served at /hdpack/")
    ap.add_argument(
        "--ca-file",
        help="PEM bundle that verifies an https Server.Status URL (/runtime/status); default: "
        "Python's default store (SSL_CERT_FILE, SSL_CERT_DIR)",
    )
    ap.add_argument("--tls-cert", help="PEM certificate chain; use a normally trusted certificate for browsers")
    ap.add_argument("--tls-key", help="Private TLS key path, never served")
    ap.add_argument(
        "--timeout",
        type=float,
        default=TIMEOUT,
        help=f"seconds each socket operation of a connection may take, the TLS handshake included (default {TIMEOUT})",
    )
    ap.add_argument(
        "--max-connections",
        type=int,
        default=MAX_CONNECTIONS,
        help=f"connections served at once; the next one is closed (default {MAX_CONNECTIONS})",
    )
    return ap


def data_whitelist(cfg):
    """The /data trees served: loose Python and SDL only in the local game-code layout, and the data tree's own
    server.ini only without a deployment (the client reads a deployment's ini from /runtime/server.ini, main.cpp
    plEmscriptenRuntime::ServerIni, never a second copy)."""
    trees = [p for p in DATA_WHITELIST if cfg.game_code == "local" or p not in ("python", "SDL")]
    return [p for p in trees if p != "server.ini"] if cfg.dep else trees


def configure(cfg, ap, handler=Handler):
    """Checks the parsed command line and fills in what the handler serves (cfg), from the deployment file when there
    is one. Errors go to ap.error. handler: the class that will serve (its host_fonts, capture_export and
    game_codes decide whether a deployment that needs them is served)."""
    cfg.dep = None
    cfg.runtime_server_ini = None
    cfg.content_security_policy = None
    cfg.sound_root = None
    cfg.status_url, cfg.status = "", None
    cfg.join, cfg.join_origins = None, ()
    # bounds of every connection (OriginServer)
    if not 0 < cfg.timeout <= 3600:
        ap.error("--timeout: seconds, more than 0 and at most 3600")
    if not 0 < cfg.max_connections <= 4096:
        ap.error("--max-connections: 1 to 4096")
    # Without a deployment the server serves a development tree (--data-root, every flag's defaults): never off loopback.
    if not cfg.deployment and cfg.bind not in LOOPBACK:
        ap.error(
            "without --deployment this server serves a local development tree: a loopback bind only (--bind 127.0.0.1)"
        )
    if cfg.deployment and cfg.hdpack_root:
        ap.error("--deployment replaces --hdpack-root (textures.hdPack.root)")
    if cfg.deployment:
        # The deployment file is the one source; the per-value flags it replaces conflict.
        if cfg.runtime_config or cfg.data_root or cfg.fonts_root or cfg.asset_prefix:
            ap.error("--deployment replaces --runtime-config, --data-root, --fonts-root and --asset-prefix")
        try:
            dep = deployment_file.load(cfg.deployment)
            public = dep.public_document(cfg.gateway_url, status_relay="/runtime/status")
            cfg.content_security_policy = deployment_file.content_security_policy(public)
        except (OSError, ValueError) as e:
            ap.error(f"Invalid deployment: {e}")
        if dep.game_code_capture and not handler.capture_export:
            ap.error(
                "capabilities.gameCodeCapture is a test-harness diagnostic: this server has no export page for "
                "its capture"
            )
        if dep.game_code not in (None,) + deployment_file.GAME_CODES and dep.game_code not in handler.game_codes:
            # a registered extension's mode (deployment.py EXTENSIONS: the test harness's client game code, which it
            # refuses without capabilities.harness); this server serves none of its trees
            ap.error(
                f"data.gameCode {dep.game_code} is a test-harness mode outside the v0.1 scope: this server does "
                "not serve it (the private test harness's server does)"
            )
        cfg.dep = dep
        # A patcher deployment may have no data tree (no client package): serve an empty one.
        cfg.data_root = str(dep.data_root) if dep.data_root is not None else tempfile.mkdtemp(prefix="empty-data-")
        cfg.asset_prefix = dep.url_prefix
        # The deployment's own game-code mode; one that names none takes the shard's (patching) layout.
        cfg.game_code = dep.game_code or "server"
        # The deployment selects the build (deployment.py CLIENT_NAMES).
        cfg.client_name = deployment_file.CLIENT_NAMES[dep.build]
        cfg.fonts_root = str(dep.fonts_root) if dep.fonts_root else None
        cfg.font_files = {n: str(p) for n, p in dep.font_files.items()} if dep.font_files else None
        if dep.fonts_source == "host":
            if handler.host_fonts is None:
                ap.error(
                    "fonts.hostCopies is a test-harness setting: this server serves no host font copies "
                    "(fonts.root names an operator's own files)"
                )
            if cfg.bind not in LOOPBACK:
                ap.error("host font copies are served on a loopback bind only (--bind 127.0.0.1)")
            cfg.font_files = dict(handler.host_fonts)
        cfg.fonts_line = {
            "free": "fonts: "
            + (
                "the page reads its fonts from " + dep.fonts_url + "; /fonts here serves"
                if dep.fonts_url
                else "the deployment names no fonts: serving"
            )
            + " the free substitutes",
            "root": f"fonts: the deployment's fonts.root {dep.fonts_root}",
            "host": "fonts: fonts.hostCopies (harness, loopback only): this host's copies of the original faces",
        }[dep.fonts_source]
        cfg.sound_root = str(dep.sound_root) if dep.sound_root else None
        cfg.runtime_server_ini = dep.server_ini_bytes
        cfg.status_url = dep.status_url
        cfg.status = StatusRelay(dep.status_url, cfg.ca_file) if dep.status_url else None
        cfg.hdpack_root = str(dep.hdpack_root) if dep.hdpack_root else None  # the HD texture packs
        cfg.runtime_config_bytes_doc = json.dumps(public, indent=1).encode()
        # The join endpoint with the deployment's key (read once, never printed) and an issuance rate per client of
        # twice the gateway's connect rate (a client's retries fetch a token each time), counted as the gateway counts
        # its connects (client_limits.ClientRate, the deployment's trusted proxies)
        try:
            secret = deployment_file.join_secret(dep)
        except (OSError, ValueError) as e:
            ap.error(
                f"Join key unavailable ({type(e).__name__}): "
                "run python -m plasma_web.deployment --init-join-secret <deployment>"
            )
        cfg.join = (
            JoinIssuer(
                secret,
                dep.name,
                dep.join_ttl,
                client_limits.ClientRate.for_deployment(dep, 2 * dep.limits["connectsPerMinutePerIp"]),
            )
            if secret
            else None
        )
        # The page origins a join request may come from: the deployment's gateway.origins (the pages its gateway
        # admits). A deployment that lists none (a local session or test, whose gateway takes a loopback --origin) is
        # served only on a loopback bind, and then its own loopback origins at the bound port (make_server).
        cfg.join_origins = tuple(dep.gateway_origins)
        if cfg.join and not cfg.join_origins and cfg.bind not in LOOPBACK:
            ap.error(
                "/runtime/join takes its page origins from the deployment's gateway.origins; a deployment "
                "without them is served on a loopback bind only"
            )
        print(
            f"deployment {dep.name}: shard {dep.entry_host}:{dep.client_port}, server.ini sha256 {dep.server_ini_sha256}, "
            f"gateway {public['gateway']}, harness {dep.harness}",
            flush=True,
        )
    elif cfg.gateway_url:
        ap.error("--gateway-url needs --deployment")
    else:
        # No default tree: the data root is named.
        if not cfg.data_root:
            ap.error("--data-root: the game data tree to serve (or --deployment)")
        cfg.game_code = "server"
        cfg.client_name = "plClient"
        cfg.font_files = {n: os.path.join(cfg.fonts_root, n) for n in FONT_FILES} if cfg.fonts_root else None
        cfg.fonts_line = (
            f"fonts: --fonts-root {cfg.fonts_root}" if cfg.fonts_root else "fonts: serving the free substitutes"
        )
    cfg.asset_prefix = cfg.asset_prefix or "/data"
    cfg.data_whitelist = data_whitelist(cfg)
    try:
        cfg.asset_origins = validate_asset_origins(cfg.asset_origin)
    except ValueError:
        ap.error("--asset-origin requires an exact HTTPS or loopback HTTP serialized origin")
    if not re.fullmatch(r"/(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+", cfg.asset_prefix) or cfg.asset_prefix.split("/")[1] in {
        "build",
        "input",
        "runtime",
        "fonts",
        "sound",
    }:
        ap.error("Invalid asset URL prefix")
    if bool(cfg.tls_cert) != bool(cfg.tls_key):
        ap.error("TLS requires both --tls-cert and --tls-key")
    cfg.runtime_config_bytes = getattr(cfg, "runtime_config_bytes_doc", None)
    cfg.runtime_doc = None
    if cfg.runtime_config:
        with open(cfg.runtime_config, "rb") as stream:
            cfg.runtime_config_bytes = stream.read(65537)
        if len(cfg.runtime_config_bytes) > 65536:
            ap.error("Runtime configuration exceeds 65536 bytes")
        cfg.runtime_doc = json.loads(cfg.runtime_config_bytes)
    configure_provenance(cfg, ap)


def configure_provenance(cfg, ap):
    """/source.json, /LICENSE.txt, /COPYING.txt and /THIRD_PARTY_NOTICES.md, the documents a static kit
    carries (emit_static.py, provenance.py), for the build this server serves. source.json and the notices are written
    once into a temporary folder of their own, removed at exit; the licence files are the repository's."""
    name = cfg.client_name
    build_files = ()
    if getattr(cfg, "build_dir", None):
        js, wasm = Path(cfg.build_dir) / f"{name}.js", Path(cfg.build_dir) / f"{name}.wasm"
        if js.is_file() and wasm.is_file():
            build_files = (js, wasm)
    dep = cfg.dep
    font_files = cfg.font_files or FONT_FILES
    fonts_source = dep.fonts_source if dep else ("root" if cfg.fonts_root else "free")
    try:
        doc = provenance.source_document(
            dep.source_repository if dep else None,
            dep.source_tag if dep else None,
            build_files,
            name,
            "2" if name.endswith("-py2") else "3",
            dep.third_party_sources if dep else None,
        )
        notices = provenance.kit_notices(
            fonts_source,
            dep.fonts_url if dep else None,
            font_files,
            deployment_file.FREE_FONT_NOTICES,
            deployment_file.font_file_name,
        )
    except (OSError, ValueError) as e:
        ap.error(f"source.json: {e}")
    if doc["commit"] and not Path(cfg.web_root).resolve().is_relative_to(provenance.ROOT):
        doc["notes"].append(
            "The page files are served from a folder outside the release checkout; the commit "
            "describes the client build only."
        )
    folder = tempfile.mkdtemp(prefix="plasma-provenance-")
    atexit.register(shutil.rmtree, folder, True)
    with open(os.path.join(folder, "source.json"), "w") as f:
        f.write(json.dumps(doc, indent=1) + "\n")
    with open(os.path.join(folder, provenance.NOTICES), "w") as f:
        f.write(notices)
    cfg.provenance_files = {
        "source.json": os.path.join(folder, "source.json"),
        provenance.NOTICES: os.path.join(folder, provenance.NOTICES),
        **{n: str(provenance.ROOT / n) for n in provenance.LICENCE_FILES},
    }
    print(
        f"source: {doc['url'] or 'no repository named'} (commit {doc['commit'] or 'none'}, "
        f"tag {doc['tag'] or 'none'}, release build {bool(doc['build'] and doc['build']['release'])})",
        flush=True,
    )


def prepare(cfg, ap, handler=Handler):
    """Builds the manifests of the served trees and checks the font files, after configure() (and a subclass's own
    configuration). Prints the fonts line and the manifest line."""
    cfg.font_files = cfg.font_files or dict(FONT_FILES)
    # The free substitutes' licence texts, whenever the fonts tree serves them (the kit's rule: emit_static.py copies
    # them when the deployment's fonts are the free ones)
    cfg.font_licence_files = dict(FREE_FONT_LICENCES) if cfg.font_files == dict(FONT_FILES) else {}
    missing = [p for p in (*cfg.font_files.values(), *cfg.font_licence_files.values()) if not os.path.isfile(p)]
    if missing:
        ap.error(f"font files not found: {', '.join(missing)}")
    print(cfg.fonts_line + ": " + ", ".join(f"{n} = {p}" for n, p in cfg.font_files.items()), flush=True)
    handler.prepare_trees(cfg)
    # /sound: the sound tree's own listing, its paths relative to the tree (the deployment's sound root, or the data
    # tree's sfx)
    cfg.sound_manifest = manifest.subtree_manifest(cfg.sound_root or os.path.join(cfg.data_root, "sfx"))
    t0 = time.time()
    cfg.data_manifest = handler.data_manifest(cfg)
    cfg.fonts_manifest = manifest.files_manifest(cfg.font_files)
    print(
        f"manifest: {cfg.data_manifest.count(b'\n')} entries, "
        f"{len(cfg.data_manifest)} bytes, built in {time.time() - t0:.2f}s",
        flush=True,
    )


def make_server(cfg, handler=Handler):
    """The listening server for a configured and prepared cfg (bound, its TLS context loaded, its access log open)."""
    context = None
    if cfg.tls_cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cfg.tls_cert, cfg.tls_key)
    srv = OriginServer(
        (cfg.bind, cfg.port),
        handler,
        tls=context,
        timeout=getattr(cfg, "timeout", TIMEOUT),
        max_connections=getattr(cfg, "max_connections", MAX_CONNECTIONS),
    )
    if cfg.join and not cfg.join_origins:
        scheme, port = "https" if cfg.tls_cert else "http", srv.server_address[1]
        cfg.join_origins = (f"{scheme}://127.0.0.1:{port}", f"{scheme}://localhost:{port}")
    srv.cfg = cfg
    srv.representations = Representations()
    srv.catalog = handler.asset_catalog(cfg, srv.representations)
    srv.log_lock = threading.Lock()
    srv.log_file = open(cfg.access_log, "a")
    return srv


def run(srv):
    """Serves until Ctrl-C (SIGINT), then closes the listening socket and returns, so the server stops with status 0
    and no traceback, as the gateway does (gateway.py cli). The ready line is inside the try: a Ctrl-C sent as soon as
    it is read can arrive before print returns. The access log is flushed after every record."""
    cfg = srv.cfg
    try:
        print(f"serving on {'https' if cfg.tls_cert else 'http'}://{cfg.bind}:{cfg.port}/", flush=True)
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()


def main():
    """The command line's entry point (plasma-web-server). A Ctrl-C while it starts also ends it quietly, as the
    gateway's cli does."""
    try:
        ap = options()
        cfg = ap.parse_args()
        configure(cfg, ap)
        prepare(cfg, ap)
        srv = make_server(cfg)
    except KeyboardInterrupt:
        return
    run(srv)


if __name__ == "__main__":
    main()
