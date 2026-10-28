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
"""The origin server's connection handling and response policy (web/deploy/plasma_web/server.py), in process
on loopback sockets, with no shard contact and no browser.

Covered: the per-connection timeout (an idle connection, a client that stalls its request, a client that stops reading
its answer), the connection cap, the TLS handshake in the connection's own thread, the body of a refused request
(dropped, or the connection closed), the cache policy and nosniff on every answer, the status relay's reused answer,
the asset catalog's rescan interval, the join issuer's per-client rate (client_limits.ClientRate, as the gateway's), and
the loopback-only bind without --deployment.

File bodies are streamed, not copied first. A large body is sent without a temporary file
and with one piece of at most representations.CHUNK in memory; a client that disconnects in the middle of a body frees
its slot and the file; a file changed in place while its body is sent ends that answer incomplete, while a file
replaced by rename goes on with the old body whole; an answer that ends early has incomplete in its access-log line
(the reason), and no other line has it; and the range and conditional answers are byte for byte those recorded from origin/main 03a6a2e18 before the
change (test_origin_server_answers.json, Date and Server values aside). To record that file again from a tree: test_origin_server.py --record-answers <file> --label <what it is>.

Run it with web/tests/run.py test_origin_server, or with the web package on the import path:
  PYTHONPATH=web/deploy python web/tests/test_origin_server.py
"""

import contextlib
import email.message
import hashlib
import http.client
import http.server
import io
import ipaddress
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tracemalloc
import unittest

from plasma_web import client_limits, deployment, representations, server
from plasma_web.asset_catalog import AssetCatalog
from plasma_web.representations import Representations, blob_tag

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

PAGE = "http://page.example.test"  # the page origin the join cases post from
BIG = 32 << 20  # a file larger than the socket buffers, for the client that stops reading
# the most of a body the server reads at a time (the same size before it had this name)
CHUNK = getattr(representations, "CHUNK", 1 << 20)
ANSWERS = HERE / "test_origin_server_answers.json"  # the recorded answers (MainAnswers)


def read_exactly(f, n):
    """n bytes from the file f, or fewer only at the end of the stream. A file made by makefile("rb", buffering=0)
    returns what one recv gives, which may be less than asked for."""
    out = bytearray()
    while len(out) < n and (piece := f.read(n - len(out))):
        out += piece
    return bytes(out)


def read_response(f):
    """(status, {lower-case header: [values]}, body) of one HTTP/1.1 answer from the file f, or None at EOF."""
    line = f.readline()
    if not line:
        return None
    headers = {}
    while True:
        h = f.readline()
        if h in (b"\r\n", b"\n", b""):
            break
        k, v = h.decode("latin-1").split(":", 1)
        headers.setdefault(k.strip().lower(), []).append(v.strip())
    n = int(headers.get("content-length", ["0"])[0])
    return int(line.split()[1]), headers, read_exactly(f, n) if n else b""


def closed(sock, within):
    """Whether the server closed sock within `within` seconds without answering anything."""
    sock.settimeout(within)
    try:
        return sock.recv(1) == b""
    except (ConnectionResetError, ssl.SSLError):
        return True
    except (TimeoutError, socket.timeout):
        return False


class OriginServerCase(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="web-tests-origin-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        data = self.tmp / "data"
        (data / "dat").mkdir(parents=True)
        (data / "sfx").mkdir()
        (data / "dat/fixture.age").write_bytes(b"age bytes\n")
        (data / "sfx/fixture.ogg").write_bytes(bytes(range(64)))
        (data / "server.ini").write_text("# fixture\n")
        build = self.tmp / "build"
        build.mkdir()
        (build / "plClient.js").write_text("// fixture client\n")
        (build / "plClient.wasm").write_bytes(b"\0asm")

    def serve(self, *args, join=None, status=None):
        """server.py configured from its own command line (no deployment) and serving on a thread."""
        ap = server.options()
        cfg = ap.parse_args(
            [
                "--data-root",
                str(self.tmp / "data"),
                "--build-dir",
                str(self.tmp / "build"),
                "--access-log",
                str(self.tmp / "access.jsonl"),
                "--port",
                "0",
                *args,
            ]
        )
        server.configure(cfg, ap)
        if join is not None:
            cfg.join, cfg.join_origins = join, (PAGE,)
        if status is not None:
            cfg.status = status
        with contextlib.redirect_stdout(io.StringIO()):
            server.prepare(cfg, ap)
        srv = server.make_server(cfg)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        self.addCleanup(lambda: (srv.shutdown(), srv.server_close(), srv.log_file.close()))
        return srv

    def connect(self, srv, context=None):
        s = socket.create_connection(("127.0.0.1", srv.server_address[1]), timeout=5)
        if context is not None:
            s = context.wrap_socket(s, server_hostname="localhost")
        self.addCleanup(s.close)  # runs before the server's own cleanup (registered earlier)
        return s

    def get(self, srv, path, method="GET", headers=None, context=None):
        """(status, [(header, value)], body) of one request on its own connection."""
        port = srv.server_address[1]
        c = (
            http.client.HTTPSConnection("localhost", port, timeout=5, context=context)
            if context
            else http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        )
        try:
            c.request(method, path, headers=headers or {})
            r = c.getresponse()
            return r.status, r.getheaders(), r.read()
        finally:
            c.close()

    def access(self, until=None):
        """The access log's records, after waiting up to 3 s for `until` (a predicate over them) to hold."""
        deadline = time.time() + 3
        while True:
            path = self.tmp / "access.jsonl"
            rows = [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []
            if until is None or until(rows) or time.time() > deadline:
                return rows
            time.sleep(0.05)


class Connections(OriginServerCase):
    def test_idle_connection_closes_after_the_timeout(self):
        # An idle connection, before its first request and after an answer (keep-alive), is closed after --timeout.
        srv = self.serve("--timeout", "1")
        quiet = self.connect(srv)
        t0 = time.monotonic()
        self.assertTrue(closed(quiet, 5), "a connection that never sends a request stays open")
        self.assertLess(time.monotonic() - t0, 4)
        kept = self.connect(srv)
        kept.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
        self.assertEqual(read_response(kept.makefile("rb"))[0], 200)
        self.assertTrue(closed(kept, 5), "an idle keep-alive connection stays open")

    def test_slow_client_holds_only_its_own_connection(self):
        # A client that stalls in the middle of its request is closed after --timeout, unanswered; meanwhile every
        # other client is served.
        srv = self.serve("--timeout", "1")
        slow = self.connect(srv)
        slow.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n")  # no blank line: the request never ends
        t0 = time.monotonic()
        self.assertEqual(self.get(srv, "/")[0], 200)
        self.assertLess(time.monotonic() - t0, 0.9)
        self.assertTrue(closed(slow, 5))
        self.assertEqual([r["path"] for r in self.access(lambda rows: len(rows) >= 1)], ["/"])

    def test_client_that_stops_reading_is_dropped(self):
        # A client that asks for a large file and never reads it: the write times out, the connection closes, and its
        # slot is free again (one connection at most here).
        (self.tmp / "data/dat/big.prp").write_bytes(bytes(BIG))
        srv = self.serve("--timeout", "1", "--max-connections", "1")
        stalled = self.connect(srv)
        stalled.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        stalled.sendall(b"GET /data/dat/big.prp HTTP/1.1\r\nHost: x\r\n\r\n")
        rows = self.access(lambda rows: any(r["path"] == "/data/dat/big.prp" for r in rows))
        big = [r for r in rows if r["path"] == "/data/dat/big.prp"]
        self.assertEqual(len(big), 1, "the stalled answer never ended")
        self.assertEqual(big[0]["status"], 200)
        self.assertLess(big[0]["bytes"], BIG)
        self.assertEqual(big[0].get("incomplete"), "timed out")  # the line says it ended early
        deadline = time.time() + 3
        while True:
            try:
                if self.get(srv, "/")[0] == 200:
                    break
            except (ConnectionError, http.client.HTTPException, OSError):
                pass
            self.assertLess(time.time(), deadline, "the stalled connection's slot was not freed")
            time.sleep(0.1)

    def test_connection_cap(self):
        # At most --max-connections at once: the next connection is closed unanswered and counted; a closed connection
        # frees its slot.
        srv = self.serve("--max-connections", "2", "--timeout", "10")
        held = [self.connect(srv), self.connect(srv)]
        time.sleep(0.3)
        third = self.connect(srv)
        third.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
        self.assertTrue(closed(third, 3), "the connection over the cap was answered")
        self.assertEqual(srv.refused, 1)
        for s in held:
            s.close()
        deadline = time.time() + 3
        while True:
            try:
                if self.get(srv, "/")[0] == 200:
                    break
            except (ConnectionError, http.client.HTTPException, OSError):
                pass
            self.assertLess(time.time(), deadline, "closed connections did not free their slots")
            time.sleep(0.1)

    def test_tls_handshake_runs_in_the_connection_thread(self):
        # With TLS, a client that connects and never starts its handshake holds its own thread only: another client's
        # handshake and request are answered at once, and the silent one is closed after --timeout.
        if not shutil.which("openssl"):
            self.skipTest("openssl not found")
        cert, key = self.tmp / "fixture.crt", self.tmp / "fixture.key"
        subprocess.run(
            [
                "openssl",
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
                str(cert),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        srv = self.serve("--tls-cert", str(cert), "--tls-key", str(key), "--timeout", "1")
        silent = self.connect(srv)
        time.sleep(0.2)
        trusted = ssl.create_default_context(cafile=str(cert))
        t0 = time.monotonic()
        self.assertEqual(self.get(srv, "/", context=trusted)[0], 200)
        self.assertLess(time.monotonic() - t0, 0.9, "the handshake waited for the silent client")
        self.assertTrue(closed(silent, 5), "the silent client's handshake did not time out")
        # A client that stops in the middle of its handshake is closed the same way.
        partial = self.connect(srv)
        partial.sendall(b"\x16\x03\x01\x00\xff")  # a ClientHello record header, and no record
        self.assertTrue(closed(partial, 5))
        self.assertEqual(self.get(srv, "/", context=trusted)[0], 200)


class RequestBodies(OriginServerCase):
    SMUGGLED = b"GET /boot.mjs HTTP/1.1\r\nHost: x\r\n\r\n"

    def exchange(self, srv, first, count):
        """Sends first, then a GET / on the same connection; the answers read (at most count)."""
        s = self.connect(srv)
        s.sendall(first + b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
        f = s.makefile("rb")
        out = []
        for _ in range(count):
            r = read_response(f)
            if r is None:
                break
            out.append(r)
        return out

    def test_refused_post_body_is_dropped(self):
        # The body of a POST the server refuses is read and dropped, so the connection's next request is the client's
        # next request, not the body (a body that holds a request line would otherwise be answered as one).
        srv = self.serve()
        post = (
            b"POST /runtime/join HTTP/1.1\r\nHost: x\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n"
            % len(self.SMUGGLED)
        ) + self.SMUGGLED
        answers = self.exchange(srv, post, 2)
        self.assertEqual([a[0] for a in answers], [404, 200])
        self.assertEqual(answers[1][1]["content-type"], ["text/html; charset=utf-8"])
        rows = self.access(lambda rows: len(rows) >= 2)
        self.assertEqual([(r["method"], r["path"]) for r in rows], [("POST", "/runtime/join"), ("GET", "/")])

    def test_refused_join_body_is_dropped(self):
        rate = client_limits.ClientRate(10)
        srv = self.serve(join=server.JoinIssuer(os.urandom(32), "fixture", 60, rate))
        post = (
            b"POST /runtime/join HTTP/1.1\r\nHost: x\r\nOrigin: http://evil.example\r\n"
            b"Content-Type: application/json\r\nContent-Length: %d\r\n\r\n" % len(self.SMUGGLED)
        ) + self.SMUGGLED
        answers = self.exchange(srv, post, 2)
        self.assertEqual([a[0] for a in answers], [403, 200])
        self.assertEqual(answers[1][1]["content-type"], ["text/html; charset=utf-8"])

    def test_other_methods_drop_their_body(self):
        srv = self.serve()
        for method in (b"OPTIONS", b"GET"):
            with self.subTest(method=method):
                first = (
                    method
                    + b" /data/dat/fixture.age HTTP/1.1\r\nHost: x\r\nContent-Length: %d\r\n\r\n" % len(self.SMUGGLED)
                ) + self.SMUGGLED
                answers = self.exchange(srv, first, 2)
                self.assertEqual(len(answers), 2)
                self.assertEqual(answers[1][1]["content-type"], ["text/html; charset=utf-8"])

    def test_unread_body_closes_the_connection(self):
        # A body over the drop limit, a chunked body, or two Content-Length values: the answer closes the connection.
        srv = self.serve()
        cases = {
            "over the limit": b"Content-Length: %d\r\n" % (server.DISCARD_BODY_LIMIT + 1),
            "chunked": b"Transfer-Encoding: chunked\r\n",
            "two lengths": b"Content-Length: 3\r\nContent-Length: 4\r\n",
            "not a number": b"Content-Length: 1e3\r\n",
        }
        for name, header in cases.items():
            with self.subTest(case=name):
                s = self.connect(srv)
                s.sendall(b"POST /x HTTP/1.1\r\nHost: x\r\n" + header + b"\r\n")
                f = s.makefile("rb")
                status, headers, _ = read_response(f)
                self.assertEqual(status, 404)
                self.assertEqual(headers.get("connection"), ["close"])
                self.assertTrue(closed(s, 3))


class ResponseHeaders(OriginServerCase):
    def headers_of(self, srv, path, method="GET", headers=None):
        status, pairs, _ = self.get(srv, path, method, headers)
        named = {}
        for k, v in pairs:
            named.setdefault(k.lower(), []).append(v)
        return status, named

    def test_cache_policy_and_nosniff(self):
        # The page, its module and its code revalidate by ETag (no-cache, as the static kit's _headers); the game trees
        # private, no-cache; /runtime/* and every other answer no-store. Every answer carries nosniff once.
        srv = self.serve()
        want = {
            "/": "no-cache",
            "/index.html": "no-cache",
            "/boot.mjs": "no-cache",
            "/input/capture.mjs": "no-cache",
            "/build/plClient.js": "no-cache",
            "/build/plClient.wasm": "no-cache",
            "/runtime/config.mjs": "no-store",
            "/data/dat/fixture.age": "private, no-cache, no-transform",
            "/data/.fetch-manifest": "no-store, no-transform",
            "/fonts/.fetch-manifest": "no-store, no-transform",
            "/missing": "no-store",
            "/build/other.js": "no-store",
        }
        for path, cache in want.items():
            for method in ("GET", "HEAD"):
                with self.subTest(path=path, method=method):
                    status, h = self.headers_of(srv, path, method)
                    self.assertEqual(status, 404 if path in ("/missing", "/build/other.js") else 200)
                    self.assertEqual(h["cache-control"], [cache])
                    self.assertEqual(h["x-content-type-options"], ["nosniff"])
                    if not cache.startswith("no-store"):
                        self.assertEqual(len(h["etag"]), 1)
        # A revalidation keeps the policy: 304 with no-cache
        _, h = self.headers_of(srv, "/boot.mjs")
        status, h304 = self.headers_of(srv, "/boot.mjs", headers={"If-None-Match": h["etag"][0]})
        self.assertEqual(
            (status, h304["cache-control"], h304["x-content-type-options"]), (304, ["no-cache"], ["nosniff"])
        )

    def test_trees_add_no_transform(self):
        # The trees the client reads uncompressed with strong ETags (server.FETCH_TREES: the fetch
        # backend's data, sound, fonts and game-code mounts, the HD packs) add no-transform to their policy, so that a
        # CDN in front of the server (Cloudflare's compresses font/ttf, then weakens the ETag) leaves them as they are.
        # A tree's 404 stays no-store, and nothing outside the trees changes.
        (self.tmp / "hdpack").mkdir()
        (self.tmp / "hdpack/Fixture.hdpack").write_bytes(b"pack bytes\n")
        srv = self.serve("--hdpack-root", str(self.tmp / "hdpack"))
        want = {
            "/data/dat/fixture.age": "private, no-cache, no-transform",
            "/data/server.ini": "no-store, no-transform",
            "/data/.fetch-manifest": "no-store, no-transform",
            "/sound/fixture.ogg": "private, no-cache, no-transform",
            "/sound/.fetch-manifest": "no-store, no-transform",
            "/fonts/.fetch-manifest": "no-store, no-transform",
            "/fonts/Courier%20New.ttf": "no-store, no-transform",
            "/hdpack/Fixture.hdpack": "private, no-cache, no-transform",
            "/data/dat/missing.age": "no-store",
            "/fonts/missing.ttf": "no-store",
            "/boot.mjs": "no-cache",
            "/build/plClient.wasm": "no-cache",
            "/runtime/config.mjs": "no-store",
        }
        for path, cache in want.items():
            for method, headers in (("GET", None), ("HEAD", None), ("GET", {"Range": "bytes=0-3"})):
                with self.subTest(path=path, method=method, headers=headers):
                    status, h = self.headers_of(srv, path, method, headers)
                    self.assertEqual(status, 404 if "missing" in path else 206 if headers else 200)
                    self.assertEqual(h["cache-control"], [cache])
        # a revalidation of a tree file keeps it
        _, h = self.headers_of(srv, "/fonts/Courier%20New.ttf")
        status, h304 = self.headers_of(srv, "/fonts/Courier%20New.ttf", headers={"If-None-Match": h["etag"][0]})
        self.assertEqual((status, h304["cache-control"]), (304, ["no-store, no-transform"]))

    def test_free_font_licences_served(self):
        # The fonts tree serves the free substitutes' licence texts next to them, as a static kit
        # carries them (emit_static.py) and the Worker serves the kit: same paths, type and policy. Not in the tree's
        # listing (the kit's .fetch-manifest names the faces only), and not when the fonts are an operator's own.
        srv = self.serve()
        for notice in deployment.FREE_FONT_NOTICES:
            path = "/fonts/" + notice["licence_file"]
            want = (deployment.FREE_FONTS_DIR / notice["licence_file"]).read_bytes()
            with self.subTest(path=path):
                status, pairs, body = self.get(srv, path)
                h = {k.lower(): v for k, v in pairs}
                self.assertEqual((status, body), (200, want))
                self.assertEqual(
                    (h["content-type"], h["cache-control"]), ("text/plain; charset=utf-8", "no-store, no-transform")
                )
                self.assertTrue(h["etag"].startswith('"'))
                self.assertEqual(self.get(srv, path, "HEAD")[0], 200)
        _, _, listing = self.get(srv, "/fonts/.fetch-manifest")
        self.assertNotIn(b"LICENSE", listing)
        own = self.tmp / "own-fonts"
        own.mkdir()
        for name in server.FONT_FILES:
            (own / name).write_bytes(b"operator's own face\n")
        srv = self.serve("--fonts-root", str(own))
        self.assertEqual(self.get(srv, "/fonts/" + deployment.FREE_FONT_NOTICES[0]["licence_file"])[0], 404)

    def test_every_other_answer_is_no_store_with_nosniff(self):
        # POST, a refused preflight, and the answers http.server writes itself (send_error: a malformed request line,
        # an unknown method).
        srv = self.serve()
        for method, path in (("POST", "/x"), ("OPTIONS", "/data/dat/fixture.age"), ("BREW", "/")):
            with self.subTest(method=method):
                status, h = self.headers_of(srv, path, method)
                self.assertEqual(status, {"POST": 404, "OPTIONS": 403, "BREW": 501}[method])
                self.assertEqual((h["cache-control"], h["x-content-type-options"]), (["no-store"], ["nosniff"]))
        s = self.connect(srv)
        s.sendall(b"GARBAGE\r\n\r\n")
        status, h, _ = read_response(s.makefile("rb"))
        self.assertEqual((status, h["cache-control"], h["x-content-type-options"]), (400, ["no-store"], ["nosniff"]))


class RequestPaths(OriginServerCase):
    """One parse of the request path (server.path_segments) for what is served and for the headers it is served with."""

    def headers_of(self, srv, path):
        status, pairs, _ = self.get(srv, path)
        named = {}
        for k, v in pairs:
            named.setdefault(k.lower(), []).append(v)
        return status, named

    def test_segments(self):
        for path, parts in (
            ("/", []),
            ("/data/dat/x.age?q=1#f", ["data", "dat", "x.age"]),
            ("/data/./dat//x", ["data", "dat", "x"]),
            ("/data/dat/a%20b", ["data", "dat", "a b"]),
            ("/data%2Fdat%2Fx", ["data", "dat", "x"]),
            ("/data/../x", None),
            ("/data/%2E%2E/x", None),
            ("/data/dat/a%5Cb", None),
            ("/data/dat/a%01b", None),
            ("/data/dat/a%7Fb", None),
        ):
            with self.subTest(path=path):
                self.assertEqual(server.path_segments(path), parts)
        self.assertEqual(server.target_path("/runtime/join?x=1"), "/runtime/join")
        self.assertEqual(server.target_path("/runtime/%6Aoin"), "/runtime/%6Aoin")  # as sent: not the join endpoint

    def test_another_spelling_has_the_same_headers(self):
        # A path spelled with empty or "." segments is served as its plain spelling, and now with its cache policy too
        # (main sent no-store for these). (http.server itself already turns a leading "//" into "/".)
        srv = self.serve()
        for path, cache in (
            ("/data/./dat/fixture.age", "private, no-cache, no-transform"),
            ("/data//dat/fixture.age", "private, no-cache, no-transform"),
            ("/./boot.mjs", "no-cache"),
            ("/build//plClient.js", "no-cache"),
            ("/index.html/", "no-cache"),
        ):
            with self.subTest(path=path):
                status, h = self.headers_of(srv, path)
                self.assertEqual((status, h["cache-control"]), (200, [cache]))

    def test_backslash_and_control_segments_are_not_served(self):
        # Such a segment is refused for every route, as main refused it for the cross-origin asset routes only.
        (self.tmp / "data/dat/a\\b").write_bytes(b"x")
        (self.tmp / "data/dat/a\x01b").write_bytes(b"x")
        srv = self.serve()
        for path in ("/data/dat/a%5Cb", "/data/dat/a%01b"):
            with self.subTest(path=path):
                status, h = self.headers_of(srv, path)
                self.assertEqual((status, h["cache-control"]), (404, ["no-store"]))
        self.assertEqual(self.headers_of(srv, "/data/dat/fixture.age")[0], 200)


class StatusRelay(OriginServerCase):
    def test_answer_is_reused_for_status_seconds(self):
        calls, now = [], [100.0]

        def fetch(url, cafile):
            calls.append((url, cafile))
            return b"status %d" % len(calls)

        relay = server.StatusRelay("https://status.example.test/s", "ca.pem", clock=lambda: now[0], fetch=fetch)
        self.assertEqual([relay.read() for _ in range(3)], [b"status 1"] * 3)
        now[0] += server.STATUS_SECONDS - 0.1
        self.assertEqual(relay.read(), b"status 1")
        now[0] += 0.2
        self.assertEqual(relay.read(), b"status 2")
        self.assertEqual(calls, [("https://status.example.test/s", "ca.pem")] * 2)

    def test_one_fetch_at_a_time(self):
        calls = []

        def fetch(url, cafile):
            calls.append(url)
            time.sleep(0.3)
            return b"ok"

        relay = server.StatusRelay("http://127.0.0.1:9/s", fetch=fetch)
        out = []
        threads = [threading.Thread(target=lambda: out.append(relay.read())) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual((out, len(calls)), ([b"ok"] * 8, 1))

    def test_server_relays_through_the_cache(self):
        hits = []

        class Upstream(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                hits.append(self.path)
                body = b"Welcome to the fixture shard"
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *a):
                pass

        up = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
        threading.Thread(target=up.serve_forever, daemon=True).start()
        self.addCleanup(lambda: (up.shutdown(), up.server_close()))
        srv = self.serve(status=server.StatusRelay(f"http://127.0.0.1:{up.server_address[1]}/status"))
        bodies = [self.get(srv, "/runtime/status")[2] for _ in range(5)]
        self.assertEqual(bodies, [b"Welcome to the fixture shard"] * 5)
        self.assertEqual(hits, ["/status"])


class CatalogInterval(OriginServerCase):
    def test_rescan_at_most_once_per_interval(self):
        root = self.tmp / "data"
        now = [0.0]
        catalog = AssetCatalog(root, Representations(), tops=["dat"], interval=10, clock=lambda: now[0])

        def paths():
            return sorted(e["path"] for e in json.loads(catalog.read()[0])["entries"])

        self.assertEqual(paths(), ["dat/fixture.age"])
        (root / "dat/new.age").write_bytes(b"new")
        now[0] = 9.9
        self.assertEqual(paths(), ["dat/fixture.age"])
        now[0] = 10.0
        self.assertEqual(paths(), ["dat/fixture.age", "dat/new.age"])
        # Without an interval every read scans (the direct users of AssetCatalog).
        eager = AssetCatalog(root, Representations(), tops=["dat"])
        eager.read()
        (root / "dat/newer.age").write_bytes(b"newer")
        self.assertIn("dat/newer.age", [e["path"] for e in json.loads(eager.read()[0])["entries"]])

    def test_server_uses_the_interval(self):
        srv = self.serve()
        self.assertEqual(srv.catalog.interval, server.CATALOG_RESCAN_SECONDS)


class JoinRate(OriginServerCase):
    def post(self, srv, client=None):
        """The status of one POST /runtime/join from the page origin, naming client as a proxy would."""
        headers = {"Origin": PAGE, "Content-Type": "application/json"}
        if client is not None:
            headers["CF-Connecting-IP"] = client
        c = http.client.HTTPConnection("127.0.0.1", srv.server_address[1], timeout=5)
        try:
            c.request("POST", "/runtime/join", body=b"{}", headers=headers)
            r = c.getresponse()
            r.read()
            return r.status
        finally:
            c.close()

    def test_rate_per_client_behind_a_trusted_proxy(self):
        # The issuer counts its tokens per client as the gateway counts its connects (client_limits.ClientRate): behind
        # a trusted proxy by the address it passes on, refusing one that names no valid address.
        rate = client_limits.ClientRate(2, [ipaddress.ip_network("127.0.0.1/32")])
        srv = self.serve(join=server.JoinIssuer(os.urandom(32), "fixture", 60, rate))
        seen = [self.post(srv, "203.0.113.7") for _ in range(3)]
        self.assertEqual(seen, [200, 200, 429])
        self.assertEqual(self.post(srv, "203.0.113.8"), 200)
        self.assertEqual(self.post(srv, "2001:db8:1:2::a"), 200)
        self.assertEqual(self.post(srv, "2001:db8:1:2::b"), 200)
        self.assertEqual(self.post(srv, "2001:db8:1:2::c"), 429)  # one /64
        for bad in (None, "garbage", "203.0.113.7, 203.0.113.9"):
            with self.subTest(client=bad):
                self.assertEqual(self.post(srv, bad), 400)

    def test_untrusted_peer_counts_as_itself(self):
        # Without trusted proxies the header is the client's own claim: ignored, and the TCP peer is counted.
        srv = self.serve(join=server.JoinIssuer(os.urandom(32), "fixture", 60, client_limits.ClientRate(2)))
        self.assertEqual([self.post(srv, f"203.0.113.{i}") for i in range(3)], [200, 200, 429])

    def test_table_is_bounded(self):
        rate = client_limits.ClientRate(5, [ipaddress.ip_network("127.0.0.1/32")], entries=4)
        issuer = server.JoinIssuer(os.urandom(32), "fixture", 60, rate)
        for i in range(10):
            h = email.message.Message()
            h["CF-Connecting-IP"] = f"198.51.100.{i}"
            self.assertIsNone(issuer.admit("127.0.0.1", h))
        self.assertEqual(len(rate), 4)


def pattern(n):
    """n bytes of a fixed pseudo-random pattern (SHA-256 of a counter), with no short period a misplaced piece could hide
    in."""
    out, i = bytearray(), 0
    while len(out) < n:
        out += hashlib.sha256(i.to_bytes(8, "big")).digest()
        i += 1
    return bytes(out[:n])


def big_file(path, size):
    """A file of `size` bytes: a pseudo-random block of CHUNK + 13 bytes, repeated (so no piece starts on a block)."""
    block = pattern(CHUNK + 13)
    path.write_bytes((block * (size // len(block) + 1))[:size])
    return path


# While WRITES is a list, every file this process opens for writing and every temporary file it makes,
# from any thread (the in-process server's included), is appended to it. Audit hooks cannot be removed, so the hook is
# installed once and does nothing while WRITES is None.
WRITES = None
WRITE_FLAGS = os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC | os.O_APPEND


def _audit(event, args):
    seen = WRITES
    if seen is None:
        return
    if event in ("tempfile.mkstemp", "tempfile.mkdtemp"):
        seen.append((event, str(args[0])))
    elif event == "open" and len(args) == 3:
        path, mode, flags = args
        if (isinstance(mode, str) and any(c in mode for c in "wax+")) or (
            isinstance(flags, int) and flags & WRITE_FLAGS
        ):
            seen.append((event, str(path)))


sys.addaudithook(_audit)


def open_paths():
    """The real paths of the files this process has open, or None where the platform does not say (Linux /proc, macOS
    F_GETPATH)."""
    if os.path.isdir("/proc/self/fd"):
        found = set()
        for fd in os.listdir("/proc/self/fd"):
            with contextlib.suppress(OSError):
                found.add(os.path.realpath(os.readlink(f"/proc/self/fd/{fd}")))
        return found
    try:
        import fcntl
    except ImportError:
        return None
    if not hasattr(fcntl, "F_GETPATH") or not os.path.isdir("/dev/fd"):
        return None
    found = set()
    for fd in os.listdir("/dev/fd"):
        with contextlib.suppress(OSError, ValueError):
            raw = fcntl.fcntl(int(fd), fcntl.F_GETPATH, bytes(1024))
            found.add(os.path.realpath(raw.split(b"\0", 1)[0].decode()))
    return found


class StreamedBodies(OriginServerCase):
    """File bodies are read from the open file while they are sent."""

    def start_get(self, srv, path, rcvbuf=None):
        """A connection with a GET of path sent: (socket, its reader, status, headers)."""
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if rcvbuf:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        s.settimeout(5)
        s.connect(("127.0.0.1", srv.server_address[1]))
        self.addCleanup(s.close)
        s.sendall(f"GET {path} HTTP/1.1\r\nHost: x\r\n\r\n".encode())
        f = s.makefile("rb", buffering=0)
        status = int(f.readline().split()[1])
        headers = {}
        while (h := f.readline()) not in (b"\r\n", b""):
            k, v = h.decode("latin-1").split(":", 1)
            headers[k.strip().lower()] = v.strip()
        return s, f, status, headers

    def test_large_body_without_temporary_files(self):
        # A 32 MiB body arrives whole and exact, and no file is opened for writing and no temporary file made while it
        # is served (the copy that preceded the headers spilled such a body to a temporary file above 1 MiB).
        global WRITES
        big = big_file(self.tmp / "data/dat/big.prp", BIG)
        want = hashlib.sha256(big.read_bytes()).hexdigest()
        srv = self.serve()
        WRITES = []
        try:
            c = http.client.HTTPConnection("127.0.0.1", srv.server_address[1], timeout=10)
            c.request("GET", "/data/dat/big.prp")
            r = c.getresponse()
            digest, got = hashlib.sha256(), 0
            while piece := r.read(64 << 10):
                digest.update(piece)
                got += len(piece)
            c.close()
            rows = self.access(lambda rows: any(row["path"] == "/data/dat/big.prp" for row in rows))
            seen = list(WRITES)
        finally:
            WRITES = None
        self.assertEqual((r.status, got, digest.hexdigest()), (200, BIG, want))
        self.assertEqual(seen, [], "files written while the body was served")
        row = [row for row in rows if row["path"] == "/data/dat/big.prp"][0]
        self.assertEqual((row["bytes"], "incomplete" in row), (BIG, False))

    def test_one_piece_in_memory(self):
        # The traced memory of the whole process (server and client threads) while a 32 MiB body is served peaks below
        # 1.5 x CHUNK: the server holds one piece of at most CHUNK bytes at a time (the copy before the headers held a
        # 1 MiB in-memory spool and a 1 MiB read piece, and copied the spool when it spilled).
        big_file(self.tmp / "data/dat/big.prp", BIG)
        srv = self.serve()
        self.assertEqual(self.get(srv, "/data/dat/big.prp", "HEAD")[0], 200)  # the ETag is hashed here, not below
        tracemalloc.start()
        try:
            c = http.client.HTTPConnection("127.0.0.1", srv.server_address[1], timeout=10)
            c.request("GET", "/data/dat/big.prp")
            r = c.getresponse()
            got = 0
            while piece := r.read(64 << 10):
                got += len(piece)
            c.close()
            self.access(lambda rows: any(row["path"] == "/data/dat/big.prp" and row["method"] == "GET" for row in rows))
            peak = tracemalloc.get_traced_memory()[1]
        finally:
            tracemalloc.stop()
        self.assertEqual(got, BIG)
        self.assertLess(peak, CHUNK * 3 // 2, f"peak traced memory {peak} bytes")

    def test_disconnect_mid_body_frees_the_slot(self):
        # A client that drops its connection (a reset) in the middle of a 32 MiB body: the answer ends at once, its one
        # slot (cap 1) and the open file are freed long before --timeout, and the access log has the bytes sent.
        big = big_file(self.tmp / "data/dat/big.prp", BIG)
        srv = self.serve("--timeout", "20", "--max-connections", "1")
        before = open_paths()
        s, f, status, headers = self.start_get(srv, "/data/dat/big.prp")
        self.assertEqual((status, headers["content-length"]), (200, str(BIG)))
        self.assertEqual(len(read_exactly(f, 256 << 10)), 256 << 10)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        f.close()  # (the reader holds the descriptor: the socket closes, with a reset, only after it)
        s.close()
        t0 = time.monotonic()
        rows = self.access(lambda rows: any(r["path"] == "/data/dat/big.prp" for r in rows))
        row = [r for r in rows if r["path"] == "/data/dat/big.prp"]
        self.assertEqual(len(row), 1, "the dropped answer never ended")
        self.assertEqual(row[0]["status"], 200)
        self.assertLess(row[0]["bytes"], BIG)
        self.assertEqual(row[0].get("incomplete"), "client gone")
        while True:
            try:
                if self.get(srv, "/")[0] == 200:
                    break
            except (ConnectionError, http.client.HTTPException, OSError):
                pass
            self.assertLess(time.monotonic() - t0, 3, "the dropped connection's slot was not freed")
            time.sleep(0.1)
        after = open_paths()
        if before is not None and after is not None:
            self.assertNotIn(os.path.realpath(big), after, "the served file is still open")

    def test_change_while_sent_ends_the_body_incomplete(self):
        # The file is rewritten in place while its body is on the way: the client gets fewer bytes than Content-Length
        # and then the end of the connection, never a whole body; the log keeps the 200 with the bytes sent, and the
        # server says so. The next request is answered from the new file under its new ETag.
        big = big_file(self.tmp / "data/dat/big.prp", BIG)
        old_tag = blob_tag(big.read_bytes())
        srv = self.serve()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            s, f, status, headers = self.start_get(srv, "/data/dat/big.prp", rcvbuf=4096)
            self.assertEqual((status, headers["etag"]), (200, old_tag))
            got = len(read_exactly(f, 64 << 10))
            with big.open("r+b") as w:
                w.seek(BIG - 1)
                w.write(b"\x00")
            # up to Content-Length (a whole body) or the end of the connection, whichever comes first
            while got < BIG and (piece := f.read(min(1 << 16, BIG - got))):
                got += len(piece)
            rows = self.access(lambda rows: any(r["path"] == "/data/dat/big.prp" for r in rows))
        self.assertLess(got, BIG, "a whole body was sent from a file that changed while it was sent")
        row = [r for r in rows if r["path"] == "/data/dat/big.prp"][0]
        self.assertEqual((row["status"], row["bytes"], row.get("incomplete")), (200, got, "file changed"))
        self.assertIn("body ended incomplete", out.getvalue())
        status, pairs, body = self.get(srv, "/data/dat/big.prp")
        self.assertEqual((status, dict(pairs)["ETag"], len(body)), (200, blob_tag(big.read_bytes()), BIG))
        self.assertNotEqual(dict(pairs)["ETag"], old_tag)

    def test_replace_while_sent_keeps_the_old_body(self):
        # The file's path is replaced by rename (os.replace, as a deploy swaps a file) while its body is on the way: the
        # open file is the old inode, now unlinked, whose bytes are the ones the headers described, so the whole old
        # body arrives and its line is an ordinary one. The unlink moves the old inode's ctime on APFS and ext4, which
        # the identity check takes as a change unless the inode is unlinked and its size and mtime are unchanged. The
        # next request is answered from the new file under its new ETag.
        big = big_file(self.tmp / "data/dat/big.prp", BIG)
        old = big.read_bytes()
        srv = self.serve()
        out = io.StringIO()
        with contextlib.redirect_stdout(out), big.open("rb") as held:
            s, f, status, headers = self.start_get(srv, "/data/dat/big.prp", rcvbuf=4096)
            self.assertEqual((status, headers["etag"]), (200, blob_tag(old)))
            first = read_exactly(f, 64 << 10)
            digest = hashlib.sha256(first)
            got = len(first)
            new = self.tmp / "data/dat/big.prp.new"
            new.write_bytes(bytes(BIG))
            before = os.fstat(held.fileno())
            os.replace(new, big)
            after = os.fstat(held.fileno())
            while got < BIG and (piece := f.read(min(1 << 16, BIG - got))):
                digest.update(piece)
                got += len(piece)
            rows = self.access(lambda rows: any(r["path"] == "/data/dat/big.prp" for r in rows))
        self.assertEqual((before.st_nlink, after.st_nlink, after.st_ino), (1, 0, before.st_ino))
        self.assertEqual(
            (got, digest.hexdigest()),
            (BIG, hashlib.sha256(old).hexdigest()),
            "the old body did not arrive whole after its file was replaced by rename",
        )
        row = [r for r in rows if r["path"] == "/data/dat/big.prp"][0]
        self.assertEqual((row["status"], row["bytes"], "incomplete" in row), (200, BIG, False))
        self.assertNotIn("body ended incomplete", out.getvalue())
        status, pairs, body = self.get(srv, "/data/dat/big.prp")
        self.assertEqual((status, dict(pairs)["ETag"], body), (200, blob_tag(bytes(BIG)), bytes(BIG)))


# The requests whose answers must stay byte for byte the recorded ones, from before bodies were streamed. "{tag}"
# in a header value is the file's own strong ETag. Fixture (answer_fixture): stream is 3 MiB + 5 bytes (several CHUNKs),
# small 512 bytes, empty none. Range syntax this server does not take (several ranges, another unit) is a whole 200.
STREAM = 3 * (1 << 20) + 5
ANSWER_REQUESTS = [
    ("whole", "GET", "/data/dat/stream", {}),
    ("head", "HEAD", "/data/dat/stream", {}),
    ("range first 100", "GET", "/data/dat/stream", {"Range": "bytes=0-99"}),
    ("range across a chunk boundary", "GET", "/data/dat/stream", {"Range": "bytes=1048570-1048585"}),
    ("range across two chunk boundaries", "GET", "/data/dat/stream", {"Range": "bytes=1048000-2098000"}),
    ("range suffix", "GET", "/data/dat/stream", {"Range": "bytes=-100"}),
    ("range suffix longer than the file", "GET", "/data/dat/stream", {"Range": "bytes=-99999999"}),
    ("range open", "GET", "/data/dat/stream", {"Range": "bytes=3145000-"}),
    ("range first byte", "GET", "/data/dat/stream", {"Range": "bytes=0-0"}),
    ("range last byte", "GET", "/data/dat/stream", {"Range": f"bytes={STREAM - 1}-"}),
    ("range end past the file", "GET", "/data/dat/stream", {"Range": "bytes=3145700-99999999"}),
    ("range past the end", "GET", "/data/dat/stream", {"Range": f"bytes={STREAM}-"}),
    ("range reversed", "GET", "/data/dat/stream", {"Range": "bytes=5-2"}),
    ("range several", "GET", "/data/dat/stream", {"Range": "bytes=0-1,5-6"}),
    ("range other unit", "GET", "/data/dat/stream", {"Range": "items=0-5"}),
    ("range malformed", "GET", "/data/dat/stream", {"Range": "bytes=abc"}),
    ("range empty", "GET", "/data/dat/stream", {"Range": "bytes=-"}),
    ("head with range", "HEAD", "/data/dat/stream", {"Range": "bytes=0-99"}),
    ("if-none-match strong", "GET", "/data/dat/stream", {"If-None-Match": "{tag}"}),
    ("if-none-match weak", "GET", "/data/dat/stream", {"If-None-Match": "W/{tag}"}),
    ("if-none-match list", "GET", "/data/dat/stream", {"If-None-Match": '"other", {tag}'}),
    ("if-none-match other", "GET", "/data/dat/stream", {"If-None-Match": '"other"'}),
    ("if-none-match star", "GET", "/data/dat/stream", {"If-None-Match": "*"}),
    ("head if-none-match", "HEAD", "/data/dat/stream", {"If-None-Match": "{tag}"}),
    ("if-none-match before range", "GET", "/data/dat/stream", {"If-None-Match": "{tag}", "Range": "bytes=0-9"}),
    ("if-match other", "GET", "/data/dat/stream", {"If-Match": '"other"'}),
    ("if-match weak", "GET", "/data/dat/stream", {"If-Match": "W/{tag}"}),
    ("if-match star", "GET", "/data/dat/stream", {"If-Match": "*"}),
    ("if-match with range", "GET", "/data/dat/stream", {"If-Match": "{tag}", "Range": "bytes=10-19"}),
    ("if-match before if-none-match", "GET", "/data/dat/stream", {"If-Match": '"other"', "If-None-Match": "{tag}"}),
    ("if-range strong", "GET", "/data/dat/stream", {"If-Range": "{tag}", "Range": "bytes=10-19"}),
    ("if-range other", "GET", "/data/dat/stream", {"If-Range": '"other"', "Range": "bytes=10-19"}),
    ("if-range weak", "GET", "/data/dat/stream", {"If-Range": "W/{tag}", "Range": "bytes=10-19"}),
    ("if-range date", "GET", "/data/dat/stream", {"If-Range": "Tue, 15 Nov 1994 12:45:26 GMT", "Range": "bytes=10-19"}),
    ("accept-encoding", "GET", "/data/dat/small", {"Accept-Encoding": "gzip, br"}),
    ("small whole", "GET", "/data/dat/small", {}),
    ("small range", "GET", "/data/dat/small", {"Range": "bytes=500-"}),
    ("empty whole", "GET", "/data/dat/empty", {}),
    ("empty range", "GET", "/data/dat/empty", {"Range": "bytes=0-1"}),
    ("missing", "GET", "/data/dat/missing", {}),
    ("build", "GET", "/build/plClient.js", {}),
    ("build revalidated", "GET", "/build/plClient.js", {"If-None-Match": "{tag}"}),
    ("manifest", "GET", "/data/.fetch-manifest", {}),
    ("manifest range", "GET", "/data/.fetch-manifest", {"Range": "bytes=2-5"}),
]
# One keep-alive connection, its requests sent at once: each answer must end where the next begins.
ANSWER_PIPELINE = ["range first 100", "small whole", "range suffix", "if-none-match strong", "whole", "head"]


def answer_fixture(root):
    """OriginServerCase's tree plus the answers' files, every file with a fixed mtime (the manifest lists mtimes). Returns
    each served file's strong ETag by its URL path."""
    data = root / "data"
    (data / "dat/stream").write_bytes(pattern(STREAM))
    (data / "dat/small").write_bytes(pattern(512))
    (data / "dat/empty").write_bytes(b"")
    for d in (data, root / "build"):
        for p in d.rglob("*"):
            if p.is_file():
                os.utime(p, (1700000000, 1700000000))
    files = {"/data/dat/" + n: data / "dat" / n for n in ("stream", "small", "empty")}
    files["/build/plClient.js"] = root / "build/plClient.js"
    return {path: blob_tag(p.read_bytes()) for path, p in files.items()}


def request_bytes(name, tags):
    """The raw request of ANSWER_REQUESTS entry `name`, with {tag} replaced by its file's strong ETag; and its method."""
    _, method, path, headers = next(r for r in ANSWER_REQUESTS if r[0] == name)
    lines = [f"{method} {path} HTTP/1.1", "Host: origin.test"]
    lines += [f"{k}: {v.replace('{tag}', tags.get(path, ''))}" for k, v in headers.items()]
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1"), method


def read_answer(f, method):
    """One answer from f, recorded: its status line, its header lines in order (Date and Server values replaced), its
    body's length and SHA-256."""
    status = f.readline().decode("latin-1").rstrip("\r\n")
    lines, length = [], 0
    while (h := f.readline()) not in (b"\r\n", b""):
        line = h.decode("latin-1").rstrip("\r\n")
        k = line.split(":", 1)[0].lower()
        if k == "content-length":
            length = int(line.split(":", 1)[1])
        lines.append({"date": "Date: <date>", "server": "Server: <server>"}.get(k, line))
    code = int(status.split()[1])
    if method == "HEAD" or code in (204, 304):
        length = 0
    body = read_exactly(f, length) if length else b""
    return {
        "status": status,
        "headers": lines,
        "body_bytes": len(body),
        "body_sha256": hashlib.sha256(body).hexdigest(),
    }


def record_answers(srv, tags):
    """Every ANSWER_REQUESTS answer of srv, each on its own connection, then ANSWER_PIPELINE's on one connection."""
    port = srv.server_address[1]
    out = []
    for name, *_ in ANSWER_REQUESTS:
        raw, method = request_bytes(name, tags)
        with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
            s.sendall(raw)
            out.append({"name": name, **read_answer(s.makefile("rb"), method)})
    with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
        sent = [request_bytes(name, tags) for name in ANSWER_PIPELINE]
        s.sendall(b"".join(raw for raw, _ in sent))
        f = s.makefile("rb")
        out.append(
            {
                "name": "pipeline",
                "answers": [{"name": n, **read_answer(f, m)} for n, (_, m) in zip(ANSWER_PIPELINE, sent)],
            }
        )
    return out


class MainAnswers(OriginServerCase):
    """The range and conditional answers are byte for byte the recorded ones (test_origin_server_answers.json, recorded
    from the server before bodies were streamed, when it copied each body before its headers)."""

    def test_answers_match_main(self):
        tags = answer_fixture(self.tmp)
        recorded = json.loads(ANSWERS.read_text())
        recorded["answers"] = [since_main(a) for a in recorded["answers"]]
        got = record_answers(self.serve(), tags)
        self.assertEqual([a["name"] for a in got], [a["name"] for a in recorded["answers"]])
        for mine, theirs in zip(got, recorded["answers"]):
            with self.subTest(answer=mine["name"]):
                self.assertEqual(mine, theirs)
        # Every answer here is complete, so every access-log line has exactly the seven fields, as in the recording
        # (only the line of an answer that ends early has an eighth, incomplete).
        rows = self.access(lambda rows: len(rows) >= len(ANSWER_REQUESTS) + len(ANSWER_PIPELINE))
        self.assertEqual(len(rows), len(ANSWER_REQUESTS) + len(ANSWER_PIPELINE))
        self.assertEqual({tuple(sorted(r)) for r in rows}, {("bytes", "method", "ms", "path", "range", "status", "t")})


def since_main(answer):
    """A recorded answer as this tree answers it: the one deliberate change since the recording, no-transform on a
    tree's answers that carry an ETag (server.py FETCH_TREES, common_headers)."""
    if answer["name"] == "pipeline":
        return {**answer, "answers": [since_main(a) for a in answer["answers"]]}
    path = next(r[2] for r in ANSWER_REQUESTS if r[0] == answer["name"])
    if path.split("/")[1] not in server.FETCH_TREES or not any(h.startswith("ETag: ") for h in answer["headers"]):
        return answer
    return {
        **answer,
        "headers": [h + ", no-transform" if h.startswith("Cache-Control: ") else h for h in answer["headers"]],
    }


def record_main(out, label):
    """--record-answers: this tree's answers to ANSWER_REQUESTS, written to out (how test_origin_server_answers.json is
    made)."""
    case = MainAnswers("test_answers_match_main")
    case.setUp()
    try:
        tags = answer_fixture(case.tmp)
        answers = record_answers(case.serve(), tags)
    finally:
        case.doCleanups()
    Path(out).write_text(
        json.dumps({"recorded_from": label, "python": sys.version.split()[0], "answers": answers}, indent=1) + "\n"
    )
    print(f"{len(answers)} answers -> {out}")


class Bind(unittest.TestCase):
    def refused(self, *args):
        ap = server.options()
        cfg = ap.parse_args(["--access-log", os.devnull, *args])
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit):
            server.configure(cfg, ap)
        return err.getvalue()

    def test_non_loopback_bind_needs_a_deployment(self):
        with tempfile.TemporaryDirectory() as tmp:
            for bind in ("0.0.0.0", "192.0.2.1", "::"):
                with self.subTest(bind=bind):
                    self.assertIn("loopback bind only", self.refused("--data-root", tmp, "--bind", bind))
            for bind in server.LOOPBACK:
                ap = server.options()
                cfg = ap.parse_args(["--access-log", os.devnull, "--data-root", tmp, "--bind", bind])
                server.configure(cfg, ap)

    def test_bounds_of_the_connection_options(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIn("--timeout", self.refused("--data-root", tmp, "--timeout", "0"))
            self.assertIn("--max-connections", self.refused("--data-root", tmp, "--max-connections", "0"))


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--record-answers" and sys.argv[3] == "--label":
        record_main(sys.argv[2], sys.argv[4])
    else:
        unittest.main()
