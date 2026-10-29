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
"""The player page and its runtime configuration, against real HTTP, in Chromium and under node.

  The loader (config.mjs): a served deployment document and its SHA-256-pinned server.ini reach the client's
    configuration, with synthetic public bootstrap keys; bad documents, a wrong pin, a redirect or an oversized
    document fail closed, each refusal with its reason code; the document comes only from the page's own origin.
  The player page (index.html, boot.mjs) with a fixture client: it reads only renderscale, whatever its query carries
    and whatever its deployment's capabilities, and does not start without a deployment; its one object
    (plasma), status line, error and refusal pages, controls, render-scale limit, texture-pack setting, first-run
    graphics.ini and source notice.
  The origin server (plasma_web.server) and the static kit: the deployment's Content-Security-Policy on every route and
    once in the kit's _headers, enforced in Chromium; the free font substitutes; the web root an allowlist; none of the
    test harness's routes, options or deployments; no test-hook file in a kit.

Run it with run.py, which puts web/deploy and this folder on PYTHONPATH, or:

    PYTHONPATH=web/deploy:web/tests python web/tests/test_config.py [--build-dir DIR] [--out DIR] [unittest options]

Prerequisites: Playwright and its Chromium (requirements.txt, then python -m playwright install chromium), node and
openssl. A check whose prerequisite is missing is skipped, and says which. Three checks read a built client
(--build-dir, or PLASMA_WASM_BUILD: the build directory of web/build/build-client.sh) and are skipped without one.
The checks write their evidence (screenshots, JSON) into a temporary folder removed at the end; with --out, into a new
folder under it, kept with summary.json.

A negative control runs the same cases against other copies of the page and the loader: PLASMA_TEST_PAGE (an
index.html), PLASMA_TEST_BOOT_MJS (its boot.mjs) and PLASMA_TEST_CONFIG_MJS (a config.mjs) replace the tree's files in
every request and node run, and --omit-configuration loads no document in the first check (expect it to fail).

The test harness page and its server are not part of this tree. Their own tests reuse this module's rig (start, with
the harness server's handler and pre-js) and helpers.
"""

import argparse
import base64
import contextlib
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
import urllib.error
from urllib.parse import urlencode
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
from plasma_web import PAGE_DIR, deployment
from plasma_web import server as origin_server
from plasma_web.representations import Representations

# The in-process server is the product origin server's Handler (it serves the player page and the fixture's own files
# below); the product's own process runs in the origin-server checks.
Handler, build_manifest, DATA_WHITELIST = (
    origin_server.Handler,
    origin_server.build_manifest,
    origin_server.DATA_WHITELIST,
)
SHELL = ROOT / "Sources/Plasma/Apps/plClient/emscripten/shell"
REAL_PAGE = (SHELL / "index.html").resolve()
PAGE = Path(os.environ.get("PLASMA_TEST_PAGE", REAL_PAGE)).resolve()
REAL_CONFIG = (SHELL / "config.mjs").resolve()
CONFIG_MJS = Path(os.environ.get("PLASMA_TEST_CONFIG_MJS", REAL_CONFIG)).resolve()
# The page's logic is boot.mjs beside index.html; PLASMA_TEST_BOOT_MJS replaces it (the negative control's
# page, an index.html with its logic inline, has none beside it: then /boot.mjs is not served)
REAL_BOOT = (SHELL / "boot.mjs").resolve()
BOOT = Path(os.environ.get("PLASMA_TEST_BOOT_MJS", PAGE.parent / "boot.mjs")).resolve()
REFUSED = "Invalid runtime deployment configuration"
KEY = base64.b64encode(bytes(64)).decode()

# The query parameters of the pages. The player page (index.html, boot.mjs) reads PLAYER alone (config.mjs
# PLAYER_PARAMETERS); the inventory check derives its list from the sources, so a new parameter fails this test until
# it is classified here. FORMER: parameters no page reads any more (their client readers are gone).
PLAYER = ["renderscale"]
# noyield, pyprobe and yield: the frame loop's deleted switches; chunk: the fetch chunk is the client's constant.
FORMER = ["captureevidence", "chunk", "d13", "losedeviceat", "noyield", "pyprobe", "smoke", "userfiles", "yield"]
# The hostile query: every parameter the test harness page reads (its own tests classify them) and every former one,
# each with a value that would change what the page or the client does, were it read. The player page ignores them all.
HOSTILE = {
    "agecmd": "Cleft",
    "agecmdafter": "5",
    "agecmds": "Cleft",
    "ageseconds": "9",
    "args": "/PlayerId 1 /Age Cleft",
    "audioprobe": "1",
    "canvas": "640x480",
    "captureevidence": "1",
    "chunk": "4096",
    "clientenv": "PLASMA_TEST_X=1",
    "config": "/runtime/config.json",
    "d13": "1",
    "dumpminframes": "3",
    "gamecodedir": "/tmp/code",
    "graphicsini": "Graphics.Width 640\r\n",
    "harnesscli": "1",
    "hdpack": "1",
    "hdpackages": "Cleft",
    "hdpackcachecorrupt": "1",
    "hdpackurl": "/hdpack",
    "initini": "dev.ini\nx 1",
    "inputobserve": "1",
    "inputtrace": "1",
    "linkafter": "5",
    "linkcmd": "Gira",
    "login": "1",
    "losedeviceat": "3",
    "movielifecycle": "1",
    "movieobserve": "1",
    "msgbox": "manual",
    "noyield": "1",
    "pickingobserve": "1",
    "pyprobe": "1",
    "reentryselftest": "1",
    "rendercap": "1",
    "seconds": "7",
    "smoke": "1",
    "stage": "320x240",
    "test-capability-fail": "webgpu",
    "userfiles": "Diag/x.txt",
    "userfs": "memfs",
    "wsproxy": "ws://127.0.0.1:9/",
    "yield": "1",
    "campose": "1,2,3,4,5,6",
    "camfov": "60",
    "avpose": "1,2,3,0",
    "fixedtime": "30",
    "tod": "0.5",
    "vcampin": "1",
    "ageinst": "x",
    "renderdump": "1",
    "atcepoch": "1",
    "pyseed": "1",
    "scenecapture": "5",
    "physacc": "3a83126f",
}
# The keys of the client's configuration (config.mjs clientConfiguration, pre.js CONFIG)
CLIENT_KEYS = [
    "assetOrigins",
    "data",
    "files",
    "fonts",
    "gateway",
    "hdPack",
    "join",
    "name",
    "serverIniText",
    "sound",
    "status",
]
# The fixture client (/build/plClient.js of the page legs). As the real client (-sMODULARIZE), the script defines the
# factory createPlasmaClient(moduleArg), whose Module is the page's moduleArg (boot.mjs) and carries a wasm memory
# (Module.wasmMemory, which the real client exports); the factory runs the client's own pre-js (emscripten/pre.js, which
# makes the client's environment from Module.plasma.config), then the run's preRun callbacks against an empty ENV, as
# the real client runs them; it records the environment, the configuration and the arguments (fixtureClient), keeps its
# Module for the tests that act as the client (fixtureModule), then runs the fixture's own steps (body). (A harness
# build adds its own pre-js after the client's: start's pre_js.)
PRE_JS = [ROOT / "Sources/Plasma/Apps/plClient/emscripten/pre.js"]


def fixture_client(pre_js, body=""):
    return (
        "var createPlasmaClient = async (moduleArg) => {\n"
        "var Module = moduleArg;\n"
        "Module.wasmMemory = new WebAssembly.Memory({initial: 1});\n"
        "var ENV = {};\n"
        + "".join(Path(p).read_text() + "\n" for p in pre_js)
        + "for (const f of [].concat(Module.preRun)) f();\n"
        "globalThis.fixtureClient = {env: {...ENV}, config: Module.plasma.config, args: [...Module.arguments]};\n"
        "globalThis.fixtureModule = Module;\n" + body + "return Module;\n};\n"
    )


# The Ages of the texture-pack deployments below: the MOULa template's list (every Age with a pack), read as JSON text
HD_AGES = json.loads((ROOT / "web/deploy/deployments/moula.template.json").read_text())["textures"]["hdPack"]["ages"]


def ok(condition, detail=None):
    """assert, also under python -O"""
    if not condition:
        raise AssertionError(detail)


def equal(x, y):
    if x != y:
        raise AssertionError((x, y))


NODE = shutil.which("node")


def need_node():
    if NODE is None:
        raise unittest.SkipTest("node is not installed: the page's loader (config.mjs) and boot.mjs run under node")
    return NODE


def page_parameters(text):
    """The parameter names a page source reads: q.get/q.has('name'), and the keys of its parameter maps (the harness
    page's PARITY, PASS and ON; the former page's parity map)."""
    names = set(re.findall(r"\bq\.(?:get|has)\(\s*'([a-z0-9-]+)'\s*\)", text))
    for body in re.findall(r"const (?:parity|PARITY|PASS|ON) = (?:Object\.freeze\()?\{(.*?)\}\)?;", text, re.S):
        names |= set(re.findall(r"\b([a-z0-9]+):\s*'PLASMA_", body))
    return names


def node_run(script, arg):
    r = subprocess.run(
        [need_node(), "--input-type=module", "-e", script, json.dumps(arg)], capture_output=True, text=True, timeout=60
    )
    if r.returncode:
        raise AssertionError(r.stderr[-800:])
    return json.loads(r.stdout)


# The loader under node with a recording fetch, and the deployments
# deployment.py derives (public_document) for a static public page, a LAN page on its own origin and a loopback page.
def ini_text(host, status=None):
    lines = [f'Server.{s}.{k} "{KEY}"' for s in ("Auth", "Game", "Gate") for k in ("N", "X")]
    lines += [f'Server.Auth.Host "{host}"', f'Server.Gate.Host "{host}"']
    if status:
        lines.append(f'Server.Status "{status}"')
    return "# synthetic\n" + "\n".join(lines) + "\n"


def write_deployment(tmp, host="shard.example.test", status=None, **fields):
    """A deployment file in tmp (outside the repository) with a synthetic server.ini; fields override the defaults."""
    tmp = Path(tmp)
    (tmp / "data/dat").mkdir(parents=True, exist_ok=True)
    (tmp / "data/dat/fixture.age").write_bytes(b"age")
    (tmp / "server.ini").write_text(ini_text(host, status))
    doc = {
        "schema": 1,
        "name": "fixture",
        "serverIni": "server.ini",
        "data": {"root": "data"},
        "gateway": {"bind": "127.0.0.1", "port": 0, "origins": []},
        "capabilities": {"harness": True},
        **fields,
    }
    path = tmp / "deployment.json"
    path.write_text(json.dumps(doc))
    return path


def static_page_deployment(tmp):
    """A public static page: asset roots on another origin, the gateway URL in the file, join tokens."""
    return write_deployment(
        tmp,
        capabilities={},
        data={"root": "data", "url": "https://assets.example.test/data"},
        fonts={"url": "https://assets.example.test/fonts"},
        sound={"url": "https://sound.example.test/sfx"},
        gateway={
            "url": "wss://gw.example.test",
            "origins": ["https://play.example.test"],
            "join": {"secretFile": str(Path(tmp) / "join.key")},
        },
    )


def own_origin_deployment(tmp, bind, gateway_url, page_origin):
    """The MOULa template's shape (web/deploy/deployments/moula.template.json: patcher files, the shard's Python
    2.3 game code, join tokens, a first-run graphics.ini, HD texture packs served next to the page), with the gateway
    bound where the page is: on a LAN address with TLS, or on loopback."""
    tmp = Path(tmp)
    (tmp / "hdpack").mkdir(exist_ok=True)
    for age in ("Personal", "Cleft"):
        (tmp / "hdpack" / f"{age}.hdpack").write_bytes(b"pack")
    gateway = {
        "bind": bind,
        "port": 0,
        "url": gateway_url,
        "origins": [page_origin],
        "resolveUpstreams": False,
        "join": {"secretFile": str(tmp / "join.key")},
    }
    if not bind.startswith("127."):
        gateway.update(
            publicHosts=[gateway_url.split("//", 1)[1]], tlsCert=str(tmp / "lan.crt"), tlsKey=str(tmp / "lan.key")
        )
    return write_deployment(
        tmp,
        host="shard.example.test",
        status="https://status.example.test/serverstatus",
        data={"files": "patcher", "gameCode": "server-py2"},
        gateway=gateway,
        capabilities={"harness": False},
        graphics={
            "initialIni": {
                "Graphics.Windowed": True,
                "Graphics.AntiAliasAmount": 4,
                "Graphics.AnisotropicLevel": 16,
                "Graphics.TextureQuality": 2,
                "Quality.Level": 3,
                "Graphics.Shadow.Enable": True,
                "Graphics.EnablePlanarReflections": True,
                "Graphics.EnableVSync": False,
            }
        },
        textures={"hdPack": {"root": str(tmp / "hdpack"), "ages": ["Personal", "Cleft"]}},
    )


def served_files(dep, base, doc):
    """URL -> bytes of what an origin serves for this document: /runtime/config.json and /runtime/server.ini."""
    return {base + "/runtime/config.json": json.dumps(doc).encode(), base + "/runtime/server.ini": dep.server_ini_bytes}


NODE_FETCH = (
    "const requested = [];"
    "globalThis.fetch = async url => { requested.push(String(url)); const f = files[String(url)];"
    "  if (f === -1) throw new TypeError('network');"
    "  if (typeof f === 'number') return new Response('status', {status: f});"
    "  return f === undefined ? new Response('not found', {status: 404})"
    "                         : new Response(Buffer.from(f, 'base64'), {status: 200}); };"
)


def node_files(files):
    return {u: (b if isinstance(b, int) else base64.b64encode(b).decode()) for u, b in files.items()}


def node_load(page, value, files):
    """loadRuntimeConfig(value, page) under node. fetch is replaced by a recorder that serves `files` (URL -> bytes,
    or a status number) and answers 404 otherwise. Returns {ok, requested, env | error}."""
    script = (
        "const {loadRuntimeConfig} = await import(" + json.dumps(CONFIG_MJS.as_uri()) + ");"
        "const [page, value, files] = JSON.parse(process.argv[1]);"
        + NODE_FETCH
        + "try { const r = await loadRuntimeConfig(value, page);"
        "      console.log(JSON.stringify({ok: true, requested, client: r && r.client})); }"
        "catch (e) { console.log(JSON.stringify({ok: false, requested, error: e.message, code: e.code ?? null})); }"
    )
    return node_run(script, [page, value, node_files(files)])


def node_page(page, files, absent=False):
    """What a page at `page` (its query included) does under node: loadPageDeployment (absent: the
    harness page's call, a loopback 404 meaning no deployment), then the player parameters of its query. Returns
    {ok, requested, deployment, harness (the capability), params, ignored, player | error, code}."""
    script = (
        "const m = await import(" + json.dumps(CONFIG_MJS.as_uri()) + ");"
        "const [page, files, absent] = JSON.parse(process.argv[1]);"
        + NODE_FETCH
        + "try { const d = await m.loadPageDeployment(page, absent);"
        "      const p = m.playerParameters(new URL(page).search);"
        "      console.log(JSON.stringify({ok: true, requested, deployment: d !== null, harness: d ? d.config.capabilities.harness : null,"
        "                                  params: [...p.params], ignored: p.ignored, player: m.PLAYER_PARAMETERS})); }"
        "catch (e) { console.log(JSON.stringify({ok: false, requested, error: e.message, code: e.code ?? null})); }"
    )
    return node_run(script, [page, node_files(files), absent])


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PRODUCT_SERVER = ("-m", "plasma_web.server")  # the product's origin server, by module


def program(script):
    return list(script) if isinstance(script, tuple) else [str(script)]


class ServerProcess:
    """An origin server process as the runner starts it: the product's (plasma_web.server), or another script
    (a path); stopped when the stack closes."""

    def __init__(self, stack, *args, script=PRODUCT_SERVER, log=os.devnull):
        self.port = free_port()
        self.base = f"http://127.0.0.1:{self.port}"
        self.proc = subprocess.Popen(
            [sys.executable, *program(script), "--port", str(self.port), "--access-log", str(log), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        stack.callback(self.stop)
        lines, deadline = [], time.time() + 30
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if not line:
                break
            lines.append(line)
            if "serving on" in line:
                return
        raise AssertionError("server.py did not start: " + "".join(lines)[-800:])

    def stop(self):
        self.proc.terminate()
        self.proc.wait(5)
        self.proc.stdout.close()

    def headers(self, path):
        try:
            with urllib.request.urlopen(self.base + path, timeout=5) as r:
                return r.headers
        except urllib.error.HTTPError as e:
            return e.headers


def write_page(tmp):
    """A page root and a build directory: index.html, and a plClient.js that runs as a worker and reports, for each
    URL it is sent, whether its fetch got a response."""
    web, build = Path(tmp) / "web", Path(tmp) / "build"
    web.mkdir()
    build.mkdir()
    (web / "index.html").write_text("<!doctype html><title>CSP fixture</title>")
    (build / "plClient.js").write_text(
        "self.addEventListener('securitypolicyviolation', e => postMessage({violation: e.blockedURI}));\n"
        "onmessage = async e => { const out = {};\n"
        "  for (const url of e.data) { try { await fetch(url, {cache: 'no-store'}); out[url] = 'fetched'; }\n"
        "                             catch (_) { out[url] = 'blocked'; } }\n"
        "  postMessage({results: out}); };\n"
    )
    return web, build


class Listener:
    """A loopback TCP port that counts the connections it accepts (and closes them)."""

    def __init__(self, stack):
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen()
        self.sock.settimeout(0.2)
        self.port, self.accepted, self.stop = self.sock.getsockname()[1], 0, False
        threading.Thread(target=self.run, daemon=True).start()
        stack.callback(self.close)

    def run(self):
        while not self.stop:
            try:
                c, _ = self.sock.accept()
            except OSError:
                continue
            self.accepted += 1
            c.close()

    def close(self):
        self.stop = True
        self.sock.close()


class OtherOrigin:
    """Another loopback origin that would serve anything (CORS-approved for any page) and counts every request."""

    def __init__(self, stack, files):
        owner = self
        self.requests = []

        class H(BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                owner.requests.append(self.path)
                body = files.get(self.path, b"probe")
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
                self.end_headers()
                self.wfile.write(body)

        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
        self.base = f"http://127.0.0.1:{self.srv.server_port}"
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
        stack.callback(self.srv.shutdown)


# ---- the rig: an in-process origin server and one Chromium, shared by the checks ----
BUILD = (
    Path(os.environ["PLASMA_WASM_BUILD"]) if os.environ.get("PLASMA_WASM_BUILD") else None
)  # a built client (--build-dir)
KEEP = None  # --out: keep the evidence in a new folder there
OMIT = False  # --omit-configuration: the first check's negative control
BROWSER_MISSING = None  # why the checks in Chromium are skipped: Playwright or its Chromium is not installed
out = web = data = cfg = server = Tested = pw = browser = page = fixture_build = base = FIXTURE_PRE_JS = None
ini = (
    "\n".join(f'Server.{service}.{key} "' + KEY + '"' for service in ("Auth", "Game", "Gate") for key in ("N", "X"))
    + '\nServer.Auth.Host "192.0.2.174"\nServer.Gate.Host "192.0.2.174"\n'
).encode()
document = {
    "schema": 1,
    "roots": {"data": "/data", "fonts": "/fonts", "sound": "/sound"},
    "gateway": "ws://127.0.0.1:8766",
    "shard": {
        "host": "192.0.2.174",
        "port": 14617,
        "serverIni": "/data/server.ini",
        "sha256": hashlib.sha256(ini).hexdigest(),
    },
}
# The player page reads source.json beside it, which server.py makes (configure_provenance); the
# fixture's names a repository and a release commit, so its link is that commit's
source_url = "https://code.example.test/plasma-web/tree/" + "0" * 40


def publish(value):
    cfg.runtime_config_bytes = json.dumps(value).encode()


def write_source(url):
    (out / "source.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "repository": "https://code.example.test/plasma-web",
                "tag": None,
                "commit": "0" * 40,
                "url": url,
                "notes": [],
            }
        )
    )


IMPORT_LOADER = "async()=>{globalThis.runtimeModule=await import('/runtime/config.mjs');}"


def tested_handler(handler):
    class Tested(handler):
        # the fixture's own scripts, written into its web root (the texture-pack checks), next to the page's two files
        page_files = handler.page_files + ("asset-origin-pins.js", "asset-cache.js", "plWGPUPipeline.js")

        def resolve(self, path):
            if path == "/oversized":
                return None, b" " * 65537
            fs, blob = super().resolve(path)
            if fs and Path(fs).resolve() == REAL_PAGE:
                fs = str(PAGE)  # the negative control's page
            elif fs and Path(fs).resolve() == REAL_BOOT:
                fs = str(BOOT) if BOOT.is_file() else None  # ... its logic
            elif fs and Path(fs).resolve() == REAL_CONFIG:
                fs = str(CONFIG_MJS)  # ... and loader
            return fs, blob

        def handle_any(self, head):
            if self.path == "/redirect":
                self.send_response(302)
                self.send_header("Location", "/runtime/config.json")
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            super().handle_any(head)

    return Tested


def start(handler=Handler, pre_js=(), **settings):
    """The rig: the fixture's web root, data and builds in a new folder, the in-process server (a subclass of handler,
    with settings in its configuration) and, when Playwright and its Chromium are installed, the browser and its
    shared page. pre_js: the fixture client's pre-js after the client's own."""
    global out, web, data, cfg, server, Tested, pw, browser, page, fixture_build, base, FIXTURE_PRE_JS, BROWSER_MISSING
    out = Path(tempfile.mkdtemp(prefix="config-", dir=KEEP) if KEEP else tempfile.mkdtemp(prefix="web-tests-config-"))
    web = out / "web"
    data = out / "assets"
    web.mkdir(parents=True)
    (data / "sfx").mkdir(parents=True)
    (web / "index.html").write_text("<!doctype html><title>Runtime configuration fixture</title>")
    # the asset cache and the pack fetch, served beside the fixture page (asset_origin_pins, kept_pack_cap)
    lib = ROOT / "Sources/Plasma"
    (web / "asset-cache.js").write_bytes((lib / "Apps/plClient/emscripten/plEmscriptenAssetCache.js").read_bytes())
    (web / "plWGPUPipeline.js").write_bytes((lib / "FeatureLib/pfWGPUPipeline/plWGPUPipeline.js").read_bytes())
    (data / "server.ini").write_bytes(ini)
    (data / "sfx/fixture.ogg").write_bytes(bytes(range(256)))
    cfg = SimpleNamespace(
        web_root=str(web),
        data_root=str(data),
        build_dir=str(web),
        data_manifest=build_manifest(str(data), DATA_WHITELIST),
        fonts_manifest=b"",
        sound_manifest=b"F 0 fixture.ogg\n",
        head_delay_ms=0,
        **settings,
    )
    publish(document)
    write_source(source_url)
    cfg.provenance_files = {"source.json": str(out / "source.json")}
    # The actual pages with a fixture client (/build/plClient.js, fixture_client): the client's own pre-js files make its
    # environment from what the page hands over (Module.plasma), and it records that environment and Module.arguments.
    FIXTURE_PRE_JS = [*PRE_JS, *pre_js]
    fixture_build = out / "fixture-build"
    fixture_build.mkdir()
    (fixture_build / "plClient.js").write_text(
        fixture_client(FIXTURE_PRE_JS, "console.log('[fixture] client started');\n")
    )
    Tested = tested_handler(handler)
    server = ThreadingHTTPServer(("127.0.0.1", 0), Tested)
    server.cfg = cfg
    server.representations = Representations()
    server.catalog = None
    server.log_lock = threading.Lock()
    server.log_file = io.StringIO()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        BROWSER_MISSING = "Playwright is not installed (web/tests/requirements.txt)"
        return
    pw = sync_playwright().start()
    try:
        browser = pw.chromium.launch(headless=True)
    except Exception as e:
        BROWSER_MISSING = (
            "Playwright's Chromium does not start (python -m playwright install chromium): "
            + str(e).strip().splitlines()[0][:200]
        )
        pw.stop()
        pw = None
        return
    page = browser.new_page()


def stop():
    global browser, pw, server
    if browser:
        browser.close()
        browser = None
    if pw:
        pw.stop()
        pw = None
    if server:
        server.shutdown()
        server.server_close()
        server = None
    if out and not KEEP:
        shutil.rmtree(out, ignore_errors=True)


def enter(stage):
    """The state a check starts from: its stage's web root and build (each check puts back what it changes, but a
    failed one may not), the fixture's document, source.json and server.ini. The loader's checks run in the fixture's
    own page with the loader imported (runtimeModule); the player page's in the actual page; the policy's checks run
    their own servers."""
    publish(document)
    write_source(source_url)
    (data / "server.ini").write_bytes(ini)
    cfg.web_root, cfg.build_dir = {"loader": (web, web), "page": (PAGE_DIR, fixture_build), "policy": (PAGE_DIR, web)}[
        stage
    ]
    cfg.web_root, cfg.build_dir = str(cfg.web_root), str(cfg.build_dir)
    if page and stage == "loader":
        page.goto(base)
        page.evaluate(IMPORT_LOADER)


def need_build():
    """The built client's directory: the build directory of web/build/build-client.sh."""
    if BUILD is None:
        raise unittest.SkipTest("no client build: give --build-dir, or PLASMA_WASM_BUILD (run.py --build-dir)")
    return BUILD


class Checks:
    """The checks in their order, each with its stage and whether it needs the browser. tests() makes them a TestCase's
    test methods (test_NN_<function>, the check's text as docstring)."""

    def __init__(self):
        self.list, self.stage, self.results = [], "loader", []

    def __call__(self, name, fn, key=None, browser=True):
        self.list.append((self.stage, key or fn.__name__, name, fn, browser))

    def run(self, stage, name, fn, browser):
        if browser and BROWSER_MISSING:
            raise unittest.SkipTest(BROWSER_MISSING)
        enter(stage)
        try:
            detail = fn()
        except unittest.SkipTest:
            raise
        except BaseException as e:
            self.results.append(dict(name=name, result="FAIL", error=str(e)))
            raise
        self.results.append(dict(name=name, result="PASS", detail=detail))

    def tests(self, cls):
        for i, (stage, key, name, fn, browser) in enumerate(self.list, 1):

            def test(self_, stage=stage, name=name, fn=fn, browser=browser):
                self.run(stage, name, fn, browser)

            test.__name__ = test.__qualname__ = f"test_{i:02d}_{key}"
            test.__doc__ = name
            setattr(cls, test.__name__, test)
        return cls

    def summary(self, scope):
        if KEEP and out:
            (out / "summary.json").write_text(
                json.dumps({"scope": scope, "checks": self.results}, indent=2, default=str)
            )
            print(out)


check = Checks()


# ---- the deployment loader: the fixture page with the loader imported ----
def load(path="/runtime/config.json"):
    return page.evaluate(
        "async path=>{try{const r=await runtimeModule.loadRuntimeConfig(path);return {ok:true,client:r?.client,config:r?.config};}catch(e){return {ok:false,error:e.message};}}",
        path,
    )


def valid():
    r = load(None if OMIT else "/runtime/config.json")
    ok(r["ok"], r)
    equal(r["client"]["serverIniText"], ini.decode())
    equal(r["client"]["sound"], f"http://127.0.0.1:{server.server_port}/sound")
    equal(r["client"]["gateway"], document["gateway"])
    return {"sha256": document["shard"]["sha256"]}


check("Real served configuration and SHA-pinned original ini produce deployment environment", valid)


def omitted():
    equal(load(None), {"ok": True, "client": None, "config": None})


check("Absent configuration retains explicit legacy mode without invented defaults", omitted)


def bad_documents():
    import copy

    cases = []
    for path, value in [
        (("username",), "fixture-private"),
        (("roots", "password"), "fixture-private"),
        (("roots", "data"), "https://fixture-private@example.invalid/assets"),
        (("gateway",), "wss://example.invalid/connect?password=fixture-private"),
        (("schema",), 2),
        (("shard", "sha256"), "0" * 64),
        (("shard", "host"), "192.0.2.175"),
        (("shard", "port"), 14618),
    ]:
        d = copy.deepcopy(document)
        cursor = d
        for k in path[:-1]:
            cursor = cursor[k]
        cursor[path[-1]] = value
        cases.append(d)
    try:
        for d in cases:
            publish(d)
            r = load()
            equal(r, {"ok": False, "error": "Invalid runtime deployment configuration"})
    finally:
        publish(document)
    return {"rejected": len(cases)}


check("Credentials/unknown fields/userinfo/query/schema/integrity/shard mismatches fail closed", bad_documents)


def missing():
    for path in ["/missing", "/redirect", "/oversized"]:
        equal(load(path), {"ok": False, "error": "Invalid runtime deployment configuration"})


check("Missing, redirected and oversized documents cannot fall back to another shard", missing)


def pins():
    for suffix in [
        "\nAccount.Username fixture-private\n",
        '\nServer.Auth.Host "192.0.2.174"\n',
        '\nServer.File.Host "files.example!"\n',
    ]:
        changed = ini + suffix.encode()
        (data / "server.ini").write_bytes(changed)
        d = {**document, "shard": {**document["shard"], "sha256": hashlib.sha256(changed).hexdigest()}}
        publish(d)
        equal(load(), {"ok": False, "error": "Invalid runtime deployment configuration"})
    (data / "server.ini").write_bytes(ini)
    publish(document)


check("Even hash-matching ini rejects account commands, duplicate hosts and malformed hosts", pins)


def split_hosts():
    # A File/Gate host other than the entry host is the shard's own layout (NetCommConnect);
    # the gateway's server-side allowlist, derived from the same ini, decides what is reachable.
    changed = ini + b'\nServer.File.Host "192.0.2.175"\n'
    (data / "server.ini").write_bytes(changed)
    try:
        publish({**document, "shard": {**document["shard"], "sha256": hashlib.sha256(changed).hexdigest()}})
        r = load()
        ok(r["ok"], r)
        gate_only = ini.replace(b'Server.Auth.Host "192.0.2.174"\n', b"")
        (data / "server.ini").write_bytes(gate_only)
        publish({**document, "shard": {**document["shard"], "sha256": hashlib.sha256(gate_only).hexdigest()}})
        r = load()
        ok(r["ok"], r)
    finally:
        (data / "server.ini").write_bytes(ini)
        publish(document)


check("split File host and gatekeeper-only ini are accepted; entry host still pinned", split_hosts)


def harness_capability():
    publish({**document, "capabilities": {"harness": True}})
    r = load()
    ok(r["ok"] and r["config"]["capabilities"]["harness"], r)
    rejected = page.evaluate(
        'd=>{try{runtimeModule.validateConfiguration(d,"https://play.example/config.json","https://play.example/");return false;}catch(_){return true;}}',
        {
            **document,
            "gateway": "wss://gateway.example/relay",
            "roots": {"data": "https://play.example/data", "fonts": "https://play.example/fonts"},
            "shard": {**document["shard"], "serverIni": "https://play.example/s.ini"},
            "capabilities": {"harness": True},
        },
    )
    publish(document)
    ok(rejected)


check("harness capability only on a loopback page", harness_capability)


def login_keys():
    # shard.name keys the login dialog's remembered login, shard.status names the status relay; both optional.
    r = load()
    equal((r["client"]["name"], r["client"]["status"]), (None, None))
    publish({**document, "shard": {**document["shard"], "name": "dirtsand-test", "status": "/runtime/status"}})
    r = load()
    ok(r["ok"], r)
    equal(
        (r["client"]["name"], r["client"]["status"]),
        ("dirtsand-test", f"http://127.0.0.1:{server.server_port}/runtime/status"),
    )
    try:
        for shard in (
            {"name": "../x"},
            {"name": ""},
            {"name": 7},
            {"status": "http://status.example.invalid/"},
            {"status": "/s?account=fixture-private"},
        ):
            publish({**document, "shard": {**document["shard"], **shard}})
            equal(load(), {"ok": False, "error": "Invalid runtime deployment configuration"})
    finally:
        publish(document)


check("deployment name and status relay reach the client environment; bad values fail closed", login_keys)


def secure():
    value = page.evaluate(
        'd=>runtimeModule.validateConfiguration(d,"https://play.example/config.json")',
        {
            **document,
            "roots": {
                "data": "https://assets.example/release",
                "fonts": "https://assets.example/fonts",
                "sound": "https://sound.example/release",
            },
            "gateway": "wss://gateway.example/relay",
            "shard": {**document["shard"], "serverIni": "https://play.example/shard.ini"},
        },
    )
    equal(value["gateway"], "wss://gateway.example/relay")
    rejected = page.evaluate(
        'd=>{try{runtimeModule.validateConfiguration(d,"https://play.example/config.json");return false;}catch(_){return true;}}',
        document,
    )
    ok(rejected)


check("HTTPS/WSS roots supported; HTTPS page refuses HTTP/WS loopback downgrade", secure)


def isolation():
    records = [json.loads(line) for line in server.log_file.getvalue().splitlines()]
    ok(all("fixture-private" not in json.dumps(r) for r in records))
    r = page.evaluate(
        'async()=>{const r=await fetch("/runtime/config.json");const ini=await fetch("/data/server.ini");return [r.headers.get("Cache-Control"),r.headers.get("X-Plasma-Asset-Cache"),ini.headers.get("Cache-Control"),ini.headers.get("X-Plasma-Asset-Cache")];}'
    )
    equal(r, ["no-store", None, "no-store, no-transform", None])  # a data tree file: no-transform


check("Bootstrap is no-store, excluded from immutable asset policy and absent from access logs", isolation)


def sound():
    r = page.evaluate(
        'async()=>{const r=await fetch("/sound/fixture.ogg",{headers:{Range:"bytes=10-29"}});return {status:r.status,tag:r.headers.get("ETag"),range:r.headers.get("Content-Range"),bytes:[...new Uint8Array(await r.arrayBuffer())]};}'
    )
    equal(r["status"], 206)
    equal(r["bytes"], list(range(10, 30)))
    equal(r["range"], "bytes 10-29/256")
    ok(r["tag"].startswith('"sha256-'))


check("Separate sound root preserves actual strong-tag range bytes", sound)


def bootstrap_reject():
    cfg.web_root = str(PAGE_DIR)
    publish({**document, "shard": {**document["shard"], "sha256": "0" * 64}})
    before = server.log_file.getvalue().count("\n")
    page.goto(f"http://127.0.0.1:{server.server_port}/")
    page.wait_for_function("globalThis.plasma?.state?.done === true")
    actual = page.evaluate("({done:plasma.state.done,cause:plasma.state.cause,exit:plasma.state.exitCode})")
    equal(actual, {"done": True, "cause": "INI-PIN", "exit": None})
    rows = [json.loads(line) for line in server.log_file.getvalue().splitlines()[before:]]
    ok(not any(r["path"].startswith("/build/") for r in rows), rows)


check("Actual production page rejects a wrong ini pin before loading any client binary", bootstrap_reject)
py2doc = {**document, "files": "patcher", "gameCode": "server-py2", "build": "py2"}


def server_py2():
    # gameCode server-py2 names the py2 build (plClient-py2) and passes no local game code to the client;
    # the loader refuses it without build py2, and build py2 with any other mode. (The page check above left the
    # actual page loaded: import the loader into it again.)
    page.evaluate("async()=>{globalThis.runtimeModule=await import('/runtime/config.mjs');}")
    publish(py2doc)
    r = load()
    ok(r["ok"], r)
    equal(
        (
            r["config"]["build"],
            r["config"]["client"],
            r["config"]["extension"],
            sorted(set(r["client"]) - set(CLIENT_KEYS)),
            r["client"]["files"],
        ),
        ("py2", "plClient-py2", None, [], "patcher"),
    )
    publish(document)
    r = load()
    equal((r["config"]["build"], r["config"]["client"]), ("py3", "plClient"))
    nobuild = {k: v for k, v in py2doc.items() if k != "build"}
    nopatcher = {k: v for k, v in py2doc.items() if k != "files"}
    # (the last two: a test harness mode and its root, which a player page does not know)
    cases = [
        nobuild,
        nopatcher,
        {**py2doc, "build": "py3"},
        {**document, "build": "py2"},
        {**document, "files": "patcher", "gameCode": "server", "build": "py2"},
        {
            **document,
            "files": "patcher",
            "gameCode": "client-python",
            "build": "py2",
            "roots": {**document["roots"], "gameCode": "/gamecode"},
        },
        {**py2doc, "roots": {**document["roots"], "gameCode": "/gamecode"}},
    ]
    try:
        for d in cases:
            publish(d)
            equal(load(), {"ok": False, "error": "Invalid runtime deployment configuration"})
    finally:
        publish(document)
    return {"rejected": len(cases)}


check("server-py2 names the py2 build; a py3 build for it and the py2 build for another mode fail closed", server_py2)


def page_build():
    # The actual page loads the deployment's build by name: build/plClient-py2.js for server-py2,
    # build/plClient.js otherwise; a server-py2 document without build py2 loads no client binary at all.
    import time

    cfg.web_root = str(PAGE_DIR)
    seen = {}
    for label, d, want in (
        ("server-py2", py2doc, ["/build/plClient-py2.js"]),
        ("default", document, ["/build/plClient.js"]),
        ("server-py2 without build", {k: v for k, v in py2doc.items() if k != "build"}, []),
    ):
        publish(d)
        before = server.log_file.getvalue().count("\n")
        page.goto(f"http://127.0.0.1:{server.server_port}/")
        deadline = time.time() + 30
        builds = []
        while time.time() < deadline:
            rows = [json.loads(line) for line in server.log_file.getvalue().splitlines()[before:]]
            builds = [r["path"] for r in rows if r["path"].startswith("/build/")]
            done = page.evaluate("globalThis.plasma?.state?.aborted || null")
            if builds or done:
                break
            page.wait_for_timeout(200)
        page.wait_for_timeout(500)
        rows = [json.loads(line) for line in server.log_file.getvalue().splitlines()[before:]]
        builds = [r["path"] for r in rows if r["path"].startswith("/build/")]
        equal(builds, want)
        if not want:
            equal(page.evaluate("globalThis.plasma?.state.cause"), "CONFIG-INVALID")
        seen[label] = builds
    publish(document)
    return seen


check("the actual page loads build/plClient-py2.js for server-py2 and build/plClient.js otherwise", page_build)


def initial_graphics_ini():
    # The actual page installs the deployment's graphics.initialIni as Init/graphics.ini only when
    # the profile has none (plClient::IDetectAudioVideoSettings, plClient.cpp:2325-2326), in the writer's format at the
    # canvas backing (a player page: the window's 1280x720 at dpr 1), before the client binary is requested; a profile
    # that has one keeps it byte for byte.
    # A bad value fails the bootstrap closed.
    import time

    cfg.web_root = str(PAGE_DIR)
    values = {
        "Graphics.Windowed": True,
        "Graphics.AntiAliasAmount": 4,
        "Graphics.AnisotropicLevel": 8,
        "Graphics.TextureQuality": 2,
        "Quality.Level": 3,
        "Graphics.Shadow.Enable": True,
        "Graphics.EnablePlanarReflections": False,
        "Graphics.EnableVSync": False,
    }
    gdoc = {**document, "graphics": {"initialIni": values}}
    read = "async()=>{try{const d=await(await navigator.storage.getDirectory()).getDirectoryHandle('Init');return await(await(await d.getFileHandle('graphics.ini')).getFile()).text();}catch(e){return null;}}"

    def run(d, clear):
        page.goto(f"http://127.0.0.1:{server.server_port}/runtime/config.mjs")
        if clear:
            page.evaluate(
                "async()=>{const r=await navigator.storage.getDirectory();try{await r.removeEntry('Init',{recursive:true});}catch(e){}}"
            )
        publish(d)
        msgs = []
        handler = lambda m: msgs.append(m.text)
        page.on("console", handler)
        before = server.log_file.getvalue().count("\n")
        page.goto(f"http://127.0.0.1:{server.server_port}/")
        deadline = time.time() + 30
        while (
            time.time() < deadline
            and not any("graphics.ini" in m for m in msgs)
            and not page.evaluate("globalThis.plasma?.state?.aborted || null")
        ):
            page.wait_for_timeout(100)
        page.wait_for_timeout(300)
        page.remove_listener("console", handler)
        rows = [json.loads(line) for line in server.log_file.getvalue().splitlines()[before:]]
        return (
            [m for m in msgs if "graphics.ini" in m],
            page.evaluate(read),
            [r["path"] for r in rows if r["path"].startswith("/build/")],
        )

    try:
        msgs, text, builds = run(gdoc, True)
        equal(msgs, [f"[page] Init/graphics.ini installed from the deployment ({len(text or '')} chars)"])
        # the size is the canvas backing (the window, 1280x720 here, less the page's control strip)
        w, h = page.evaluate("()=>[document.getElementById('canvas').width,document.getElementById('canvas').height]")
        ok(w == 1280 and 680 <= h <= 720, (w, h))
        equal(
            text,
            f"Graphics.Width {w}\r\nGraphics.Height {h}\r\nGraphics.ColorDepth 32\r\nGraphics.Windowed true\r\n"
            "Graphics.AntiAliasAmount 4\r\nGraphics.AnisotropicLevel 8\r\nGraphics.TextureQuality 2\r\nQuality.Level 3\r\n"
            "Graphics.Shadow.Enable 1\r\nGraphics.EnablePlanarReflections 0\r\nGraphics.EnableVSync false\r\n",
        )
        equal(builds, ["/build/plClient.js"])
        page.evaluate(
            "async()=>{const d=await(await navigator.storage.getDirectory()).getDirectoryHandle('Init');const w=await(await d.getFileHandle('graphics.ini',{create:true})).createWritable();await w.write('Graphics.AntiAliasAmount 0\\r\\n');await w.close();}"
        )
        msgs, text, _ = run(gdoc, False)
        equal(msgs, ["[page] Init/graphics.ini exists: the deployment initial graphics.ini is not installed"])
        equal(text, "Graphics.AntiAliasAmount 0\r\n")
        msgs, text, _ = run(document, True)
        equal((msgs, text), ([], None))
        msgs, text, builds = run({**document, "graphics": {"initialIni": {**values, "Quality.Level": 4}}}, True)
        equal(
            (msgs, text, builds, page.evaluate("globalThis.plasma?.state.cause")),
            ([], None, [], "CONFIG-INVALID"),
        )
    finally:
        publish(document)


check("the actual page installs the deployment graphics.ini only into a profile without one", initial_graphics_ini)
# A texture-pack deployment: the MOULa template's Ages (HD_AGES), the packs beside the page
HD_DOC = {**document, "textures": {"hdPack": {"base": "/hdpack", "ages": HD_AGES}}}
TEXPACK_READ = "async()=>{try{const d=await(await navigator.storage.getDirectory()).getDirectoryHandle('Init');return await(await(await d.getFileHandle('webtextures.txt')).getFile()).text();}catch(e){return null;}}"
TEXPACK_UI = "()=>({offered:!document.getElementById('texpack').hidden,on:document.getElementById('texpack-on').checked,credit:!document.getElementById('texpack-credit').hidden,text:document.getElementById('texpack-credit').textContent.trim()})"


def texpack_visit(d, clear, extra="", path="/"):
    """The actual page at path for the document d (its profile's Init folder removed first when clear): its texture
    console lines, the setting's state and Init/webtextures.txt."""
    page.goto(f"http://127.0.0.1:{server.server_port}/runtime/config.mjs")
    if clear:
        page.evaluate(
            "async()=>{const r=await navigator.storage.getDirectory();try{await r.removeEntry('Init',{recursive:true});}catch(e){}}"
        )
    publish(d)
    msgs = []
    handler = lambda m: msgs.append(m.text)
    page.on("console", handler)
    page.goto(f"http://127.0.0.1:{server.server_port}{path}?canvas=1152x720{extra}")
    deadline = time.time() + 30
    while (
        time.time() < deadline
        and not any("high-res textures" in m or "setting failed" in m for m in msgs)
        and not page.evaluate("globalThis.plasma?.state?.aborted || null")
    ):
        page.wait_for_timeout(100)
    page.wait_for_timeout(300)
    page.remove_listener("console", handler)
    return [m for m in msgs if "textures" in m], page.evaluate(TEXPACK_UI), page.evaluate(TEXPACK_READ)


def hd_texture_setting():
    # The actual page offers the high-res texture setting only for a deployment with textures.hdPack,
    # off by default; turning it on writes Init/webtextures.txt (HDTextures 1) and shows the CC BY-NC-SA attribution;
    # the next page load finds it on. The player page ignores the test harness's seed hdpack=1 (the harness page's own
    # tests check that it writes it).
    cfg.web_root = str(PAGE_DIR)
    try:
        msgs, state, text = texpack_visit(HD_DOC, True, "&hdpack=1")
        equal(
            (msgs, state["offered"], state["on"], state["credit"], text),
            (["[page] high-res textures off"], True, False, False, None),
        )
        page.click("#texpack-on")
        page.wait_for_timeout(500)
        equal((page.evaluate(TEXPACK_UI)["credit"], page.evaluate(TEXPACK_READ)), (True, "HDTextures 1\r\n"))
        equal(page.evaluate(TEXPACK_UI)["text"], "· High-resolution textures: Cyan Worlds, CC BY-NC-SA 4.0")
        msgs, state, text = texpack_visit(HD_DOC, False)
        equal((msgs, state["on"], state["credit"]), (["[page] high-res textures on"], True, True))
        msgs, state, text = texpack_visit(document, True)
        equal((msgs, state["offered"]), ([], False))
    finally:
        publish(document)


check(
    "the actual page offers the high-res texture setting off by default, persists it and credits the art",
    hd_texture_setting,
)


def render_scale_limit():
    # The actual player page offers "Supersample 2x" only while its backing (2 x the window's
    # device pixels) stays within 3840x2160: area at most 8,294,400 and no side above 3840. Past it the option is
    # disabled with the reason in its title, the client is told scale 1 and the capped default backing applies; a window
    # that outgrows the limit while 2x is on falls back to Standard, logs it once and keeps the saved choice, and 2x
    # returns by itself when the window fits again (the limit is a function of the device pixels alone, so the backing
    # the client sizes cannot feed back into it).
    # the client is an idle one that loads and stays (a client that fails to load ends the run, CLIENT-LOAD,
    # and an ended page hides the menu)
    # The menu is one button per scale (data-scale) in the control strip below the canvas, which
    # takes S CSS pixels of the window's height (measured on the page under test: 0 where the controls lie over the
    # canvas). Each window below is S taller than the canvas it is about, so the rule's cases keep their device sizes.
    idle = out / "idle-build"
    idle.mkdir(exist_ok=True)
    (idle / "plClient.js").write_text("// idle fixture client\n")
    cfg.web_root = str(PAGE_DIR)
    cfg.build_dir = str(idle)
    publish(document)
    url = f"http://127.0.0.1:{server.server_port}/?"
    ui = """()=>{const s=document.getElementById('render-scale'),sel=s.tagName==='SELECT',o=s.querySelector(sel?'option[value="2"]':'button[data-scale="2"]'),m=plasma.display.metrics,c=document.getElementById('canvas');
  return {device:m.devW+'x'+m.devH,off:o.disabled,optTitle:o.title,menuTitle:s.title,shown:sel?s.value:(s.querySelector('[aria-checked="true"]')?.dataset.scale??null),
          scale:m.scale,chosen:m.scaleChosen,saved:localStorage.getItem('plasma.renderScale'),backing:c.width+'x'+c.height}}"""
    S = [0]

    def pick(pg, v):  # the player's own pick of scale v (a trusted click on its button; on the select, select_option)
        if pg.evaluate("()=>document.getElementById('render-scale').tagName") == "SELECT":
            pg.select_option("#render-scale", v)
        else:
            pg.click(f'#render-scale button[data-scale="{v}"]')

    seq = "()=>plasma.display.metrics.seq"
    reason = lambda w, h: f"Supersample 2x needs a smaller window on this display (would render {w}x{h})"
    fall = lambda w, h: (
        f"[page] render scale 2x falls back to Standard: its backing would be {w}x{h}, over the 3840x2160 limit (the choice is kept; it returns when the window fits)"
    )
    again = "[page] render scale 2x available again: its backing is 3840x2160, within the 3840x2160 limit (supersampling resumes)"

    def window(vw, vh, dpr, query="", saved=None):
        ctx = browser.new_context(viewport={"width": vw, "height": vh + S[0]}, device_scale_factor=dpr)
        pg = ctx.new_page()
        msgs = []
        pg.on("console", lambda m: msgs.append(m.text))
        if saved:
            pg.add_init_script(f"try{{localStorage.setItem('plasma.renderScale','{saved}')}}catch(e){{}}")
        pg.goto(url + query)
        pg.wait_for_function("()=>globalThis.plasma?.display?.metrics?.seq>=1", timeout=30000)
        return ctx, pg, msgs

    def settle(pg, after):
        pg.wait_for_function("n=>plasma.display.metrics.seq>n", arg=after, timeout=15000)
        pg.wait_for_timeout(200)

    def limited(msgs):
        return [m for m in msgs if m.startswith("[page] render scale 2x")]

    ctx, pg, msgs = window(960, 540, 1)
    try:
        S[0] = pg.evaluate(
            "()=>Math.round(innerHeight-document.getElementById('canvas').getBoundingClientRect().height)"
        )
    finally:
        ctx.close()
    # the rule: (viewport CSS, dpr) -> the 2x backing and whether it is offered
    table = [
        ((1920, 1080), 2, (7680, 4320), False),
        ((960, 540), 2, (3840, 2160), True),
        ((1920, 1080), 1, (3840, 2160), True),
        ((1920, 1200), 1, (3840, 2400), False),  # sides within 3840, area 9.2M over the limit
        ((2000, 540), 1, (4000, 1080), False),  # area 4.3M within the limit, a side over 3840
        ((540, 2000), 1, (1080, 4000), False),  # the same, portrait
        ((1080, 1920), 1, (2160, 3840), True),  # portrait at the limit: 2160x3840 = 8,294,400
        ((1081, 1920), 1, (2162, 3840), False),
    ]  # portrait, one column over
    seen = {}
    for (vw, vh), dpr, (bw, bh), offered in table:
        ctx, pg, msgs = window(vw, vh, dpr, "renderscale=2")
        try:
            st = pg.evaluate(ui)
            key = f"{vw}x{vh}@{dpr}"
            seen[key] = (st["off"], st["scale"])
            equal(
                (st["device"], st["off"], st["shown"], st["scale"], st["chosen"]),
                (f"{round(vw * dpr)}x{round(vh * dpr)}", not offered, "2" if offered else "1", 2 if offered else 1, 2),
            )
            if offered:
                equal(
                    (st["optTitle"], st["menuTitle"].startswith("Render scale"), limited(msgs), st["backing"]),
                    ("", True, [], f"{bw}x{bh}"),
                )
            else:
                equal(
                    (st["optTitle"], st["menuTitle"], limited(msgs)),
                    (reason(bw, bh), reason(bw, bh) + "; your choice returns when the window fits", [fall(bw, bh)]),
                )
                cap = min(dpr, 1.5) / dpr
                equal(st["backing"], f"{round(vw * dpr * cap)}x{round(vh * dpr * cap)}")  # the capped default backing
                equal(st["saved"], None)  # renderscale= in the query is for one visit: nothing saved
                if key == "1920x1080@2":  # evidence: the disabled option greyed (a select is opened as a list for it)
                    pg.evaluate(
                        "()=>{const s=document.getElementById('render-scale');if(s.tagName==='SELECT'){s.size=2;s.style.opacity=1}}"
                    )
                    b = pg.locator("#render-scale").bounding_box()
                    x0 = max(0, b["x"] - 90)
                    pg.screenshot(
                        path=str(out / "render-scale-disabled-1920x1080-dpr2.png"),
                        clip={
                            "x": x0,
                            "y": max(0, b["y"] - 8),
                            "width": min(vw - x0, b["width"] + 180),
                            "height": b["height"] + 16,
                        },
                    )
        finally:
            ctx.close()
    # a saved 2x on a window past the limit: Standard shown and sent, the choice kept in the profile
    ctx, pg, msgs = window(1920, 1080, 2, "", saved="2")
    try:
        st = pg.evaluate(ui)
        equal(
            (st["off"], st["shown"], st["scale"], st["chosen"], st["saved"], st["backing"], limited(msgs)),
            (True, "1", 1, 2, "2", "2880x1620", [fall(7680, 4320)]),
        )
    finally:
        ctx.close()
    # the player's own pick in the menu, then the window grows past the limit, grows more, shrinks back, and the dpr rises
    ctx, pg, msgs = window(960, 540, 1)
    try:
        equal(pg.evaluate(ui)["off"], False)
        s0 = pg.evaluate(seq)
        pick(pg, "2")
        settle(pg, s0)
        st = pg.evaluate(ui)
        equal(
            (st["shown"], st["scale"], st["saved"], limited(msgs)), ("2", 2, "2", [])
        )  # the backing itself is the client's from here (the page sizes it once, at seq 1)
        s0 = pg.evaluate(seq)
        pg.set_viewport_size({"width": 1921, "height": 1081 + S[0]})
        settle(pg, s0)
        st = pg.evaluate(ui)
        equal(
            (st["off"], st["shown"], st["scale"], st["chosen"], st["saved"], limited(msgs)),
            (True, "1", 1, 2, "2", [fall(3842, 2162)]),
        )
        s0 = pg.evaluate(seq)
        pg.set_viewport_size({"width": 2560, "height": 1440 + S[0]})
        settle(pg, s0)
        st = pg.evaluate(ui)
        equal((st["scale"], st["saved"], limited(msgs)), (1, "2", [fall(3842, 2162)]))  # still limited: no second line
        s0 = pg.evaluate(seq)
        pg.set_viewport_size({"width": 1920, "height": 1080 + S[0]})
        settle(pg, s0)
        st = pg.evaluate(ui)
        equal(
            (st["off"], st["shown"], st["scale"], st["saved"], limited(msgs)),
            (False, "2", 2, "2", [fall(3842, 2162), again]),
        )
        cdp = ctx.new_cdp_session(pg)
        s0 = pg.evaluate(seq)
        for h in (1079, 1080):
            cdp.send(
                "Emulation.setDeviceMetricsOverride",
                {"width": 1920, "height": h + S[0], "deviceScaleFactor": 2, "mobile": False},
            )
        settle(pg, s0)
        st = pg.evaluate(ui)  # dpr 1 -> 2 at the same CSS window: 3840x2160 device, 2x would be 7680x4320
        equal(
            (st["device"], st["off"], st["shown"], st["scale"], st["saved"], limited(msgs)[2:]),
            ("3840x2160", True, "1", 1, "2", [fall(7680, 4320)]),
        )
    finally:
        ctx.close()
        cfg.build_dir = str(web)
    seen["strip"] = S[0]
    return seen


check(
    "the page offers 2x supersampling only while its backing fits 3840x2160, falls back and returns by itself",
    render_scale_limit,
)


def asset_origin_pins():
    # One pin limit, config.mjs ASSET_ORIGIN_PINS (5), which the client build turns into asset-origin-pins.js (plClient
    # CMakeLists.txt). A document whose four roots (data, fonts, sound and the texture packs) name four origins yields
    # four pins (the fifth root, roots.gameCode, is a test harness mode, which the harness's own tests check);
    # plEmscriptenAssetCache.js's rule admits five, as does the pack fetch's origin check (plWGPUPipeline.js, here with
    # string-table stand-ins for the wasm heap), which refuses a duplicate, a sixth pin and an unpinned origin.
    (web / "asset-origin-pins.js").write_bytes(
        (need_build() / "Sources/Plasma/Apps/plClient/asset-origin-pins.js").read_bytes()
    )
    o = [f"http://127.0.0.1:{n}" for n in (1, 7, 9, 11, 13, 15)]  # Chromium refuses these ports: nothing is contacted
    four = {
        **document,
        "roots": {"data": o[0] + "/data", "fonts": o[1] + "/fonts", "sound": o[2] + "/sfx"},
        "textures": {"hdPack": {"base": o[3] + "/hd", "ages": ["Personal"]}},
    }
    equal(
        page.evaluate(
            'd=>runtimeModule.assetOrigins(runtimeModule.validateConfiguration(d,location.origin+"/runtime/config.json",location.href))',
            four,
        ),
        o[:4],
    )
    pins = o[:5]
    got = page.evaluate(
        """async ({pins,six})=>{
  const lib={};globalThis.addToLibrary=v=>Object.assign(lib,v);
  for(const f of ['/asset-origin-pins.js','/asset-cache.js','/plWGPUPipeline.js'])(0,eval)(await (await fetch(f,{cache:'no-store'})).text());
  const A=lib.$PlasmaAssetCache,page=location.origin,strs=[null];let env='';
  Object.assign(globalThis,{PlasmaAssetCache:A,plWGPUTexturePackCache:lib.$plWGPUTexturePackCache,
   stringToNewUTF8:s=>strs.push(s)-1,UTF8ToString:p=>strs[p]??'',_free:()=>{},_malloc:()=>0,
   _getenv:p=>strs[p]==='PLASMA_RUNTIME_ASSET_CACHE_ORIGINS'?strs.push(env)-1:0});
  globalThis.HEAP32=new Int32Array(4096);globalThis.HEAPU32=new Uint32Array(HEAP32.buffer);globalThis.HEAPU8=new Uint8Array(HEAP32.buffer);
  let block=0;const started=[];
  const fetchCheck=(list,url)=>{env=list;block+=64;const rc=lib.plWGPUTexturePack_fetch(strs.push(url)-1,block,1<<20,0);if(rc===0)started.push(block>>>2);return rc;};
  const set=v=>v===null?null:[...v];
  const out={limit:PlasmaAssetOriginPins,module:runtimeModule.ASSET_ORIGIN_PINS,
   cache:{five:set(A.pinnedOrigins(pins,page)),six:set(A.pinnedOrigins(six,page)),dup:set(A.pinnedOrigins([pins[4],pins[4]],page))},
   fetch:{five:fetchCheck(JSON.stringify(pins),pins[4]+'/hd/Personal.hdpack'),
    dup:fetchCheck(JSON.stringify([pins[4],pins[4]]),pins[4]+'/hd/Personal.hdpack'),
    six:fetchCheck(JSON.stringify(six),pins[4]+'/hd/Personal.hdpack'),
    unpinned:fetchCheck(JSON.stringify([pins[0]]),pins[4]+'/hd/Personal.hdpack'),
    query:fetchCheck(JSON.stringify(pins),pins[4]+'/hd/Personal.hdpack?x=1'),
    page:fetchCheck('not json','/hdpack/Personal.hdpack')}};
  const t0=performance.now();      // the started fetches end (unreachable port: -2; no pack on the page origin: -1)
  while(started.some(w=>Atomics.load(HEAP32,w)===0)&&performance.now()-t0<15000)await new Promise(r=>setTimeout(r,20));
  out.ended=started.map(w=>Atomics.load(HEAP32,w));return out;}""",
        {"pins": pins, "six": o},
    )
    equal((got["limit"], got["module"]), (5, 5))
    equal(got["cache"], {"five": o[:5], "six": None, "dup": None})
    equal(got["fetch"], {"five": 0, "dup": -3, "six": -3, "unpinned": -3, "query": -3, "page": 0})
    equal(got["ended"], [-2, -1])
    js = (
        need_build() / "Sources/Plasma/Apps/plClient/plClient.js"
    ).read_text()  # the built client carries the same value
    ok(re.search(r"var PlasmaAssetOriginPins\s*=\s*5\s*;", js), "built plClient.js: no PlasmaAssetOriginPins = 5")
    ok(
        re.search(r"PlasmaAssetCache\.pinnedOrigins\(\s*pins\s*,\s*page\.origin\s*\)", js),
        "built plClient.js: the pack fetch lacks the pin rule",
    )
    return got


check(
    "one asset-origin pin limit (config.mjs, 5) in the loader, asset-cache.js and the pack fetch, which refuses duplicates",
    asset_origin_pins,
)


def kept_pack_cap():
    # The kept packs' byte cap (plWGPUPipeline.js $plWGPUTexturePackCache.capBytes, least
    # recently used evicted first). The production plWGPUTexturePack_store runs in this real page against real Cache
    # Storage, with heap stand-ins for the client's block and buffer and the cap lowered to 1000 bytes; the clock is a
    # counter, so every use time is distinct and ordered. Each scenario runs with Web Locks, then without them (the
    # promise chain): the results must be the same.
    page.goto(f"http://127.0.0.1:{server.server_port}")
    got = page.evaluate("""async ()=>{
  const lib={};globalThis.addToLibrary=v=>Object.assign(lib,v);
  (0,eval)(await (await fetch('/plWGPUPipeline.js',{cache:'no-store'})).text());
  const C=lib.$plWGPUTexturePackCache,built=C.capBytes,locks=navigator.locks;C.capBytes=1000;
  let clock=1e12;Date.now=()=>++clock;
  const strs=[null];
  Object.assign(globalThis,{plWGPUTexturePackCache:C,UTF8ToString:p=>strs[p]??'',_free:()=>{}});
  globalThis.HEAP32=new Int32Array(1<<16);globalThis.HEAPU32=new Uint32Array(HEAP32.buffer);globalThis.HEAPU8=new Uint8Array(HEAP32.buffer);
  const base=location.origin+'/hdpack/';let next=0;
  const store=async(age,size)=>{      // one verified pack, as the client's IKeep hands it over
   const url=base+age+'.hdpack',block=next,w=block>>>2,data=next+64;next=data+((size+63)&~63);
   HEAPU32[w+1]=data;HEAPU32[w+2]=size;for(let i=0;i<32;i++)HEAPU8[block+12+i]=(age.length*31+age.charCodeAt(0)+i)&255;
   HEAPU8.fill(age.charCodeAt(0),data,data+size);C.etags[url]='"'+age+'"';
   const rc=lib.plWGPUTexturePack_store(strs.push(url)-1,block),t0=performance.now();
   while(rc===0&&Atomics.load(HEAP32,w)===0&&performance.now()-t0<10000)await new Promise(r=>setTimeout(r,5));
   return {rc,state:HEAP32[w],evicted:HEAPU32[w+11],evictedBytes:HEAPU32[w+12],cap:HEAPU32[w+13]};};
  const name=u=>u.slice(base.length).split('.hdpack')[0];
  const kept=async()=>{const c=await caches.open(C.name),out=[];
   for(const k of await c.keys()){if(k.url===C.recencyUrl())continue;const r=await c.match(k);
    out.push(name(k.url)+':'+r.headers.get('X-Plasma-HDPack-Bytes')+':'+(await r.arrayBuffer()).byteLength);}
   return out.sort();};
  const rec=async()=>Object.keys(await C.readRecency(await caches.open(C.name))).map(name).sort();
  const midWrite=(age,other,size)=>{   // another pack's whole write lands while this one's put is in flight
   const put=Cache.prototype.put;let res=null;
   Cache.prototype.put=async function(req,resp){
    if(String(req.url||req).includes('/'+age+'.hdpack?sha256=')){Cache.prototype.put=put;res=await store(other,size);}
    return put.call(this,req,resp);};
   return ()=>res;};
  const run=async()=>{
   const o={};await caches.delete(C.name);
   o.order=[await store('A',400),await store('B',400)];
   await C.touch(base+'A.hdpack');               // A revalidated (304): now used after B
   o.order.push(await store('C',400));o.orderKept=await kept();o.orderRec=await rec();
   o.over=await store('D',1001);o.overKept=await kept();
   o.atCap=await store('E',1000);o.atCapKept=await kept();
   await caches.delete(C.name);
   o.mid=[await store('H',300)];let other=midWrite('F','G',500);o.mid.push(await store('F',500),other());
   o.midKept=await kept();o.midRec=await rec();
   await caches.delete(C.name);
   other=midWrite('F2','G2',500);o.self=[await store('F2',600),other()];o.selfKept=await kept();o.selfRec=await rec();
   await caches.delete(C.name);return o;};
  const withLocks=await run();
  Object.defineProperty(navigator,'locks',{value:undefined,configurable:true});
  const chained=await run();
  Object.defineProperty(navigator,'locks',{value:locks,configurable:true});
  return {built,withLocks,chained};}""")
    w = got["withLocks"]
    equal(got["chained"], w)
    equal(got["built"], 256 * 1024 * 1024)
    k = lambda state, evicted=0, evictedBytes=0, rc=0: {
        "rc": rc,
        "state": state,
        "evicted": evicted,
        "evictedBytes": evictedBytes,
        "cap": 1000,
    }
    # eviction order: B, used least recently once A was revalidated, makes room for C
    equal(w["order"], [k(1), k(1), k(1, 1, 400)])
    equal(w["orderKept"], ["A:400:400", "C:400:400"])
    equal(w["orderRec"], ["A", "C"])
    # larger than the cap: never written (-3; the client logs it once, IKeep), and the kept packs are untouched
    equal(w["over"], k(0, rc=-3))
    equal(w["overKept"], ["A:400:400", "C:400:400"])
    equal(w["atCap"], k(1, 2, 800))
    equal(w["atCapKept"], ["E:1000:1000"])
    # the cap reached mid-write: G's write lands during F's; after F's, the least recently used of all (H) goes
    equal(w["mid"], [k(1), k(1, 1, 300), k(1)])
    equal(w["midKept"], ["F:500:500", "G:500:500"])
    equal(w["midRec"], ["F", "G"])
    # ... or this pack itself, used before G (state -2: written, then evicted; no other pack evicted)
    equal(w["self"], [k(-2), k(1)])
    equal(w["selfKept"], ["G2:500:500"])
    equal(w["selfRec"], ["G2"])
    return w


check(
    "kept packs share a 256 MiB cap; least recently used evicted first, larger never kept, mid-write cap", kept_pack_cap
)


def kept_pack_cap_built():
    # The built client links the same pack cache: its cap and the lock fallback's chain as plWGPUPipeline.js has them
    js = (need_build() / "Sources/Plasma/Apps/plClient/plClient.js").read_text()
    ok(
        re.search(r"capBytes:\s*(268435456|256\s*\*\s*1024\s*\*\s*1024)\b", js),
        "built plClient.js: no kept packs cap of 256 MiB",
    )
    ok(re.search(r"chain:\s*null", js), "built plClient.js: the lock fallback chain is not null at link time")


check(
    "the built client keeps packs under the same 256 MiB cap, with the lock fallback chain null at link time",
    kept_pack_cap_built,
    browser=False,
)
# ---- the player page reads renderscale alone, whatever its query and its deployment's capabilities ----
# The actual page with a fixture client (/build/plClient.js, fixture_client): the client's own pre-js files make its
# environment from what the page hands over (Module.plasma), and it records that environment and Module.arguments.
check.stage = "page"
hostile = list(HOSTILE.items()) + [("utm_source", "x"), ("renderscale", "2")]


def booted(pg, base, query, log, path="/"):
    """Load the actual page at base+path?query (the player page /, or another page of the server: the test harness's) on
    a clean profile and report what it did."""
    pg.goto(base + "/runtime/config.mjs")
    pg.evaluate(
        "async()=>{const r=await navigator.storage.getDirectory(),names=[];for await(const n of r.keys())names.push(n);"
        "for(const n of names)await r.removeEntry(n,{recursive:true});for(const k of await caches.keys())await caches.delete(k);localStorage.clear();}"
    )
    msgs = []
    handler = lambda m: msgs.append(m.text)
    pg.on("console", handler)
    before = log.getvalue().count("\n")
    pg.goto(base + path + "?" + query)
    deadline = time.time() + 30
    while time.time() < deadline and not pg.evaluate("!!(globalThis.fixtureClient || globalThis.plasma?.state?.done)"):
        pg.wait_for_timeout(100)
    pg.wait_for_timeout(700)
    pg.remove_listener("console", handler)
    rows = [json.loads(line) for line in log.getvalue().splitlines()[before:]]
    st = pg.evaluate("""async()=>{const out=[];async function walk(d,p){for await(const [n,h] of d.entries()){out.push(p+n+(h.kind==='directory'?'/':''));
    if(h.kind==='directory')await walk(h,p+n+'/');}}
   await walk(await navigator.storage.getDirectory(),'');const c=document.getElementById('canvas'),d=globalThis.plasma?.display;
   return {mode:document.documentElement.dataset.plasmaMode??null,client:globalThis.fixtureClient??null,
    aborted:plasma.state.aborted,cause:plasma.state.cause,failure:plasma.state.capabilityFailure??null,backing:c.width+'x'+c.height,
    scale:d?d.metrics.scale:null,stage:getComputedStyle(document.getElementById('stage')).visibility,
    opfs:out.sort(),caches:await caches.keys(),saved:localStorage.getItem('plasma.renderScale'),title:document.title};}""")
    st["console"] = [m for m in msgs if m.startswith("[page]")]
    st["requests"] = sorted({r["path"].split("?")[0] for r in rows})
    return st


ignored_line = lambda names: (
    "[page] query parameters ignored on a player page (it reads only renderscale): "
    + ", ".join(names[:24])
    + (f" (+{len(names) - 24})" if len(names) > 24 else "")
)
# The player page passes the deployment's configuration alone: none of the four variables it set
# before (the run time, the user folder, the player display, the login dialog: the client's own defaults now).
player_env = {
    "PLASMA_HARNESS_RUN_SECONDS": None,
    "PLASMA_HARNESS_USER_FS": None,
    "PLASMA_DISPLAY_PLAYER": None,
    "PLASMA_HARNESS_LOGIN": None,
}
brief = [
    ("config", "/runtime/config.json"),
    ("renderscale", "2"),
    ("fixedtime", "30"),
    ("vcampin", "1"),
    ("campose", "1,2,3,4,5,6"),
    ("utm_source", "x"),
]


def differ(key, a, b):
    """The visit with a hostile query against the clean visit: the same, or the difference (env keys and arguments)."""
    if a == b:
        return
    if key == "client" and a and b:
        ea, eb = a["env"], b["env"]
        raise AssertionError(
            {
                "env added": {k: ea[k] for k in ea.keys() - eb.keys()},
                "env removed": sorted(eb.keys() - ea.keys()),
                "env changed": {k: (eb[k], ea[k]) for k in ea.keys() & eb.keys() if ea[k] != eb[k]},
                "args": (b["args"], a["args"]),
            }
        )
    raise AssertionError((key, "hostile", a, "clean", b))


def same_as_clean(pg, base, log, label):
    """The player page: hostile queries against the clean visit (config= and renderscale=2, the visit the negative
    control's page also reads its deployment for): every observable the same, the claim checked first."""
    clean = booted(pg, base, urlencode([("config", "/runtime/config.json"), ("renderscale", "2")]), log)
    visits = {}
    for name, query in (("brief", brief), ("every parameter", hostile)):
        seen = visits[name] = booted(pg, base, urlencode(query), log)
        for key in ("client", "aborted", "failure", "backing", "scale", "opfs", "caches", "saved", "requests", "stage"):
            differ(key, seen[key], clean[key])
        names = [n for n, _ in query if n != "renderscale"]
        equal([m for m in seen["console"] if "ignored" in m], [ignored_line(list(dict.fromkeys(names)))])
    equal(
        (clean["mode"], clean["aborted"], clean["failure"], clean["scale"], clean["stage"], clean["saved"]),
        ("player", None, None, 2, "visible", None),
    )
    ok(clean["client"], clean)
    equal({k: clean["client"]["env"].get(k) for k in player_env}, player_env)
    equal(clean["client"]["args"], [])
    ok(
        not any(
            k.startswith(
                ("PLASMA_PARITY_", "PLASMA_RENDER_DUMP", "PLASMA_F2", "PLASMA_HARNESS", "PLASMA_S3_", "PLASMA_S4_")
            )
            for k in clean["client"]["env"]
        ),
        clean["client"]["env"],
    )
    ok(not any(p.startswith(("/userfile/", "/hdpack/", "/harness")) for p in clean["requests"]), clean["requests"])
    equal([m for m in clean["console"] if "ignored" in m], [ignored_line(["config"])])
    equal([m for m in clean["console"] if m.startswith("[page] player page") or "harness" in m], ["[page] player page"])
    return {
        "label": label,
        "env": clean["client"]["env"],
        "backing": clean["backing"],
        "requests": clean["requests"],
        "ignored": {
            k: len(dict.fromkeys(n for n, _ in q if n != "renderscale"))
            for k, q in (("brief", brief), ("every parameter", hostile))
        },
    }


def player_parameters():
    # Every parameter the player page reads is classified: renderscale alone, the list config.mjs keeps
    # (PLAYER_PARAMETERS), and no former one. (The test harness page's own tests classify its parameters.)
    player = page_parameters(PAGE.read_text() + (BOOT.read_text() if BOOT.is_file() else ""))
    equal(sorted(player), PLAYER)
    equal(sorted(set(FORMER) & player), [])
    equal(
        node_run(
            "const m = await import("
            + json.dumps(CONFIG_MJS.as_uri())
            + ");console.log(JSON.stringify(m.PLAYER_PARAMETERS));",
            [],
        ),
        PLAYER,
    )
    return {"player": PLAYER, "former": FORMER}


check(
    "every query parameter of the player page is classified: it reads renderscale alone, no former one",
    player_parameters,
    browser=False,
)


def node_mode():
    tmp = Path(tempfile.mkdtemp(dir=out))
    seen = {}
    (tmp / "static").mkdir()
    (tmp / "own").mkdir()
    dep = deployment.load(static_page_deployment(tmp / "static"))
    pub = dep.public_document()
    own = "https://play.example.test"
    q = "?" + urlencode(hostile + [("config", "https://attacker.example.test/runtime/config.json")])
    # a loopback page without a deployment document (404): the player page refuses to start (CONFIG-NONE); the harness
    # page's call (absent) starts without one (the development server's own data tree)
    for pg in ("http://127.0.0.1:8790/", "http://localhost:8790/", "http://[::1]:8790/"):
        r = node_page(pg + q, {})
        o = pg.rstrip("/")
        equal((r["ok"], r.get("code"), r["requested"]), (False, "CONFIG-NONE", [o + "/runtime/config.json"]))
        r = node_page(pg + q, {}, absent=True)
        equal(
            (r["ok"], r["deployment"], r["params"], r["requested"]),
            (True, False, [["renderscale", "2"]], [o + "/runtime/config.json"]),
        )
    # a page off loopback: the document beside it, whatever the query names; renderscale alone is read
    r = node_page(own + "/" + q, served_files(dep, own, pub))
    equal(
        (r["ok"], r["harness"], r["params"], r["player"], r["requested"]),
        (True, False, [["renderscale", "2"]], PLAYER, [own + "/runtime/config.json", own + "/runtime/server.ini"]),
    )
    equal(r["ignored"], [n for n, _ in hostile if n != "renderscale"])  # config named once, in query order
    seen["player"] = len(r["ignored"])
    # ... which refuses to start without a document (404, also on the harness page's call), with a failing one, or with
    # a document carrying the harness capability
    for files in (
        {},
        {own + "/runtime/config.json": 500},
        {own + "/runtime/config.json": b"{"},
        served_files(dep, own, {**pub, "capabilities": {"harness": True}}),
    ):
        for absent in (False, True):
            r = node_page(own + "/" + q, files, absent)
            equal((r["ok"], r.get("error"), r["requested"][:1]), (False, REFUSED, [own + "/runtime/config.json"]))
    # loopback: a failing document fails closed too (only 404 means none, and only to the harness page)
    for status in (500, 403):
        for absent in (False, True):
            r = node_page("http://127.0.0.1:8790/", {"http://127.0.0.1:8790/runtime/config.json": status}, absent)
            equal((r["ok"], r.get("error")), (False, REFUSED))
    # loopback with a document: renderscale alone, with or without the harness capability (the harness page's to read)
    lo = "http://127.0.0.1:8790"
    plain = deployment.load(own_origin_deployment(tmp / "own", "127.0.0.1", "ws://127.0.0.1:8791", lo))
    pdoc = plain.public_document(status_relay="/runtime/status")
    r = node_page(lo + "/?fixedtime=30&fixedtime=31&renderscale=2&renderscale=1", served_files(plain, lo, pdoc))
    equal(
        (r["ok"], r["harness"], r["params"], r["ignored"]),
        (True, False, [["renderscale", "2"], ["renderscale", "1"]], ["fixedtime"]),
    )
    r = node_page(
        lo + "/?fixedtime=30&renderscale=2", served_files(plain, lo, {**pdoc, "capabilities": {"harness": True}})
    )
    equal((r["ok"], r["harness"], r["params"], r["ignored"]), (True, True, [["renderscale", "2"]], ["fixedtime"]))
    shutil.rmtree(tmp)
    return seen


check(
    "config.mjs reads renderscale alone; no document is refused on every page but the loopback harness call",
    node_mode,
    browser=False,
)


def player_page():
    seen = {}
    for label, doc in (
        ("player deployment", document),
        ("harness deployment", {**document, "capabilities": {"harness": True}}),
    ):
        publish(doc)
        try:
            r = same_as_clean(page, base, server.log_file, label)
            r.pop("env")
            seen[label] = r
        finally:
            publish(document)
    return seen


check(
    "on the player page a hostile query (every former harness parameter, utm_source) changes nothing, also on a harness deployment; renderscale applies",
    player_page,
)


def capture_not_on_player_page():
    # A deployment with capabilities.gameCodeCapture (a test-harness diagnostic, with capabilities.harness): the player
    # page passes nothing for it (PLASMA_GAME_CODE_CAPTURE). The harness page's own tests check that it arms it.
    publish({**document, "capabilities": {"harness": True, "gameCodeCapture": True}})
    try:
        st = booted(page, base, "", server.log_file)
    finally:
        publish(document)
    ok(st["client"], st)
    equal(
        (st["mode"], st["client"]["env"].get("PLASMA_GAME_CODE_CAPTURE"), [m for m in st["console"] if "capture" in m]),
        ("player", None, []),
    )


check("the player page never passes the game-code capture, also for a capture deployment", capture_not_on_player_page)


def no_deployment():
    # The player page does not start without a deployment document, on loopback too, whatever its query. (Without one
    # the test harness page runs on loopback: the harness's own tests check that.)
    cfg.runtime_config_bytes = None
    try:
        pl = booted(page, base, "wsproxy=ws%3A%2F%2F127.0.0.1%3A9%2F&seconds=3", server.log_file)
    finally:
        publish(document)
    equal(
        (pl["client"], pl["cause"], pl["mode"], [p for p in pl["requests"] if p.startswith(("/build/", "/data/"))]),
        (None, "CONFIG-NONE", None, []),
    )


check("without a deployment document the player page refuses to start (CONFIG-NONE), on loopback too", no_deployment)


@contextlib.contextmanager
def off_loopback_page():
    """A page on a public name (play.example.test, resolved to loopback by Chromium) over TLS with a throwaway
    self-signed certificate: the same server handler and page as above, so the only difference is the page's origin.
    Yields (its page, its base URL, its server, the document it serves)."""
    if shutil.which("openssl") is None:
        raise unittest.SkipTest("openssl is not installed: the page off loopback needs a fixture certificate")
    tls = Path(tempfile.mkdtemp(dir=out))
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-keyout",
            str(tls / "k.pem"),
            "-out",
            str(tls / "c.pem"),
            "-days",
            "1",
            "-subj",
            "/CN=play.example.test",
        ],
        check=True,
        capture_output=True,
    )
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(tls / "c.pem", tls / "k.pem")
    shutil.rmtree(tls)

    class TLSServer(ThreadingHTTPServer):
        def finish_request(self, request, client_address):  # the handshake in the request's own thread
            conn = ctx.wrap_socket(request, server_side=True)
            try:
                self.RequestHandlerClass(conn, client_address, self)
            finally:
                conn.close()

        def handle_error(self, request, client_address):
            pass  # Chromium's speculative connections that never handshake

    srv = TLSServer(("127.0.0.1", 0), Tested)
    srv.cfg = cfg
    srv.representations = Representations()
    srv.catalog = None
    srv.log_lock = threading.Lock()
    srv.log_file = io.StringIO()
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    b2 = pw.chromium.launch(
        headless=True, args=["--host-resolver-rules=MAP play.example.test 127.0.0.1", "--ignore-certificate-errors"]
    )
    try:
        tpage = b2.new_context(ignore_https_errors=True).new_page()
        tbase = f"https://play.example.test:{srv.server_port}"
        tdoc = {**document, "gateway": "wss://gw.example.test"}
        publish(tdoc)
        yield tpage, tbase, srv, tdoc
    finally:
        publish(document)
        b2.close()
        srv.shutdown()
        srv.server_close()


# the test harness's parameters for a run without a deployment document
OFF_LOOPBACK_QUERY = "wsproxy=wss%3A%2F%2Fgw.example.test&harnesscli=1&seconds=3"


def off_loopback_player():
    with off_loopback_page() as (tpage, tbase, srv, tdoc):
        r = same_as_clean(tpage, tbase, srv.log_file, "off loopback")
        equal(r.pop("env")["PLASMA_WS_PROXY"], "wss://gw.example.test")
        return r


check("a player page off loopback (TLS) ignores the query; renderscale applies", off_loopback_player)


def off_loopback_refused():
    # the player page: no deployment document, or one with the harness capability: it refuses to start, whatever the
    # query says
    seen = {}
    with off_loopback_page() as (tpage, tbase, srv, tdoc):
        for label, value in (
            ("none", None),
            ("harness", json.dumps({**tdoc, "capabilities": {"harness": True}}).encode()),
        ):
            for q in (OFF_LOOPBACK_QUERY, ""):
                cfg.runtime_config_bytes = value
                st = booted(tpage, tbase, q, srv.log_file)
                equal(
                    (
                        label,
                        q,
                        st["client"],
                        st["cause"],
                        [p for p in st["requests"] if p.startswith(("/build/", "/data/", "/userfile/"))],
                    ),
                    (label, q, None, "CONFIG-NONE" if value is None else "CONFIG-INVALID", []),
                )
                equal((st["mode"], st["stage"]), (None, "hidden"))
                ok("/runtime/config.json" in st["requests"], st["requests"])
            seen[label] = "refused"
    return seen


check(
    "a page off loopback (TLS) refuses to start without a deployment, or with the harness one, whatever the query",
    off_loopback_refused,
)
# ---- the page and module's one object, its cause codes and its status line (boot.mjs) ----
# The page's one object, globalThis.plasma (read-only; the client reads it as Module.plasma), and no other global of
# the page's own: no plasma-prefixed one (none of the six former page globals, nor the harness's two,
# plasmaS4Credentials and plasmaS3Files, nor the module handles that moved under it), and no Module: the client is a
# -sMODULARIZE factory, and its module stays inside it
OLD_GLOBALS = [
    "plasmaS3",
    "plasmaPlayerDisplay",
    "plasmaCanvasBacking",
    "plasmaSetStage",
    "plasmaSetFullscreenStage",
    "plasmaBootFailure",
    "plasmaS4Credentials",
    "plasmaS3Files",
    "Module",
]
MEMBERS = [
    "audioActivationError",
    "audioBackend",
    "bootFailure",
    "canvasBacking",
    "config",
    "display",
    "fetch",
    "firstFrame",
    "fullscreen",
    "fullscreenStatus",
    "input",
    "launcher",
    "login",
    "setFullscreenStage",
    "setStage",
    "state",
    "storage",
]
# what a player's status line must not say: ticket and stage names, and the page's inner workings
JARGON = re.compile(
    r"\b(PR|PUB|PLAT|TEX|OB|F\d\w*|S[1-9]|J\d+|D\d+|R\d+|harness|parity|pin|deployment|config\w*|wsproxy|OPFS|"
    r"wasm|WebAssembly|runtime|module|ENV|null|undefined|loopback|query|pthread|worker|Emscripten|plClient|ini|JSON|HTTP|\d{3})\b",
    re.I,
)


def boot_module():
    if not BOOT.is_file():
        raise AssertionError(f"no boot.mjs beside the page under test ({PAGE})")
    return node_run(
        "const m = await import("
        + json.dumps(BOOT.as_uri())
        + ");console.log(JSON.stringify({causes: m.CAUSES, status: m.STATUS,"
        " texts: Object.fromEntries(Object.keys(m.CAUSES).filter(c => m.CAUSES[c].fatal).map(c => [c, m.statusText(c)]))}));",
        [],
    )


def player_texts():
    r = boot_module()
    causes = r["causes"]
    for code, c in causes.items():
        ok(re.fullmatch(r"[A-Z]+-[A-Z]+", code), code)
        ok(not JARGON.search(c["text"]) and c["text"].endswith("."), (code, c["text"]))
    for code, text in r["texts"].items():
        equal(text, causes[code]["text"] + " Error code: " + code)
    for k, v in r["status"].items():
        ok(not JARGON.search(v), (k, v))
    # every code config.mjs raises (fail('X'), and kind + '-FETCH' / '-HTTP' / '-SIZE' for CONFIG and INI) is a fatal
    # cause with a text; every code the page raises (report, causeError) has one; no cause is raised nowhere
    cm, bm = CONFIG_MJS.read_text(), BOOT.read_text()
    loader = (
        set(re.findall(r"fail\('((?:CONFIG|INI)-[A-Z]+)'\)", cm))
        | set(re.findall(r"fail\([^;]*\? '((?:CONFIG|INI)-[A-Z]+)' :", cm))
        | {k + "-" + s for k in ("CONFIG", "INI") for s in ("FETCH", "HTTP", "SIZE")}
        | {"CONFIG-INVALID"}
    )
    ok(
        {"kind + '-FETCH'", "kind + '-HTTP'", "kind + '-SIZE'"} <= {m for m in re.findall(r"kind \+ '-[A-Z]+'", cm)},
        "config.mjs kind codes",
    )
    page = (
        set(re.findall(r"(?:report|causeError)\('([A-Z]+-[A-Z]+)'", bm))
        | set(re.findall(r"\.fatal \? e\.code : '([A-Z]+-[A-Z]+)'", bm))
        | {"BROWSER-MISSING", "BROWSER-GPU"}
    )
    equal(sorted(loader - {c for c in causes if causes[c]["fatal"]}), [])
    equal(sorted(page - set(causes)), [])
    equal(sorted(set(causes) - loader - page), [])
    return {
        "fatal": sum(c["fatal"] for c in causes.values()),
        "logged": sum(not c["fatal"] for c in causes.values()),
        "loader": len(loader),
    }


check(
    "every cause code has a player text without internal words; the loader and page raise only known codes",
    player_texts,
    browser=False,
)


def source_notice():
    # The player page reads source.json beside it (provenance.py) and shows "Source code (GPLv3)" linking its url
    # and "Notices" linking the kit's THIRD_PARTY_NOTICES.md; without a url (a site that names no repository) the words
    # show without a link
    seen = {}
    try:
        for label, url in (("url", source_url), ("none", None)):
            write_source(url)
            publish(document)
            st = booted(page, base, "", server.log_file)
            seen[label] = page.evaluate(
                "()=>{const b=document.getElementById('source-notice'),l=document.getElementById('source-link');"
                "return {shown:!b.hidden&&getComputedStyle(b).display!=='none',href:l.hidden?null:l.getAttribute('href'),"
                "none:!document.getElementById('source-none').hidden,notices:document.getElementById('notices-link').getAttribute('href')};}"
            )
            seen[label]["console"] = [m for m in st["console"] if m.startswith("[page] source code")]
            if label == "url":
                page.screenshot(path=str(out / "source-notice.png"))
    finally:
        write_source(source_url)
    ref = " (commit " + "0" * 40 + ")"
    equal(
        seen["url"],
        {
            "shown": True,
            "href": source_url,
            "none": False,
            "notices": "THIRD_PARTY_NOTICES.md",
            "console": ["[page] source code: " + source_url + ref],
        },
    )
    equal(
        seen["none"],
        {
            "shown": True,
            "href": None,
            "none": True,
            "notices": "THIRD_PARTY_NOTICES.md",
            "console": ["[page] source code: no repository named" + ref],
        },
    )
    return seen


check(
    'the player page links the source of its release from source.json ("Source code (GPLv3)") and the notices',
    source_notice,
)


def loader_codes():
    # config.mjs: each refusal keeps the one message (REFUSED) and names its reason in e.code, before or without
    # echoing a URL or a body
    tmp = Path(tempfile.mkdtemp(dir=out))
    dep = deployment.load(static_page_deployment(tmp))
    own = "https://play.example.test"
    pub = dep.public_document()
    good = served_files(dep, own, pub)
    doc_url, ini_url = own + "/runtime/config.json", own + "/runtime/server.ini"
    bad = dep.server_ini_bytes + b"\nAccount.Username x\n"
    cases = {
        "CONFIG-NONE": {},
        "CONFIG-HTTP": {doc_url: 500},
        "CONFIG-FETCH": {doc_url: -1},
        "CONFIG-SIZE": {doc_url: b" " * 65537},
        "CONFIG-JSON": {doc_url: b"{"},
        "CONFIG-INVALID": served_files(dep, own, {**pub, "capabilities": {"harness": True}}),
        # (CONFIG-GAMECODE, a client-side game-code mode without the harness capability, is the test harness page's,
        # which the harness's own tests check)
        "INI-HTTP": {**good, ini_url: 404},
        "INI-FETCH": {**good, ini_url: -1},
        "INI-SIZE": {**good, ini_url: b"#" * 65537},
        "INI-PIN": {**good, ini_url: dep.server_ini_bytes + b"\n"},
        "INI-INVALID": {
            doc_url: json.dumps({**pub, "shard": {**pub["shard"], "sha256": hashlib.sha256(bad).hexdigest()}}).encode(),
            ini_url: bad,
        },
    }
    seen = {}
    for want, files in cases.items():
        r = node_page(own + "/?renderscale=2", files)
        equal((want, r["ok"], r.get("error"), r.get("code")), (want, False, REFUSED, want))
        seen[want] = r["requested"]
    r = node_load(own + "/", "https://attacker.example.test/runtime/config.json", good)
    equal((r["ok"], r.get("code"), r["requested"]), (False, "CONFIG-ORIGIN", []))
    r = node_page(own + "/", good)
    ok(r["ok"], r)  # the same files, unbroken, load
    shutil.rmtree(tmp)
    return {k: len(v) for k, v in seen.items()}


check(
    "config.mjs names the reason of each refusal (CONFIG-*, INI-*) and keeps the one message",
    loader_codes,
    browser=False,
)
GLOBAL = (
    "()=>{const p=globalThis.plasma,d=Object.getOwnPropertyDescriptor(globalThis,'plasma');"
    "return {names:Object.getOwnPropertyNames(globalThis).filter(n=>/^plasma/i.test(n)),old:"
    + json.dumps(OLD_GLOBALS)
    + ".filter(n=>n in globalThis),"
    "writable:d?d.writable:null,configurable:d?d.configurable:null,sealed:p?Object.isSealed(p):null,members:p?Object.keys(p).sort():null,"
    "config:p?p.config:null,storage:p&&p.storage?Object.keys(p.storage):null,"
    "fns:p?Object.keys(p).filter(k=>typeof p[k]==='function').sort():null,display:p&&p.display?p.display.metrics.seq:null,"
    "state:p?Object.keys(p.state).sort():null,factory:typeof globalThis.createPlasmaClient};}"
)


def global_row(label, doc, query, path="/", extra_members=()):
    """The page's one object on a visit (label 'refused': a refused deployment): globalThis.plasma, read-only and
    sealed with its members and functions, no other global of the page's own and no Module, the client's factory once
    its script loaded (never on a refused visit), and the configuration (one frozen JSON value of the deployment's
    roles) and the client's storage member once it ran. Returns what booted saw and the object's survey."""
    publish(doc)
    try:
        st = booted(page, base, query, server.log_file, path)
        g = page.evaluate(GLOBAL)
    finally:
        publish(document)
    equal(
        (
            label,
            g["names"],
            g["old"],
            g["writable"],
            g["configurable"],
            g["sealed"],
            g["members"],
            g["fns"],
            g["factory"],
        ),
        (
            label,
            ["plasma"],
            [],
            False,
            False,
            True,
            sorted(MEMBERS + list(extra_members)),
            ["bootFailure", "canvasBacking", "firstFrame", "setFullscreenStage", "setStage"],
            "undefined" if label == "refused" else "function",
        ),
    )
    if label != "refused":
        equal(
            (sorted(g["config"]), g["config"]["gateway"], g["storage"]),
            (sorted(CLIENT_KEYS), document["gateway"], ["login"]),
        )
    else:
        equal((g["config"], g["storage"]), (None, None))
    ok({"aborted", "cause", "done", "exitCode", "lines", "t0"} <= set(g["state"]), g["state"])
    return st, g


def one_global():
    seen = {}
    for label, doc, query in (
        ("player", document, "renderscale=2&utm_source=x"),
        ("refused", {**document, "shard": {**document["shard"], "sha256": "0" * 64}}, ""),
    ):
        st, g = global_row(label, doc, query)
        seen[label] = {"display_seq": g["display"], "cause": st["cause"], "mode": st["mode"]}
    equal(
        (seen["player"]["mode"], seen["player"]["display_seq"] >= 1, seen["refused"]["cause"]),
        ("player", True, "INI-PIN"),
    )
    return seen


check(
    "the page and module meet in one sealed object (the page's read-only plasma, the client's Module.plasma), and the"
    " page has no other global of its own and no Module",
    one_global,
)


def fixture(name, tail):
    d = out / ("fixture-" + name)
    d.mkdir()
    (d / "plClient.js").write_text(
        fixture_client(
            FIXTURE_PRE_JS,
            "Module.print('[fixture] a client line'); Module.printErr('[fixture] a client error line');\n" + tail,
        )
    )
    return d


UI = (
    "()=>{const s=document.getElementById('status'),cs=getComputedStyle(s);return {text:s.textContent,shown:cs.display!=='none',position:cs.position,"
    "classes:[...document.documentElement.classList].sort(),log:document.getElementById('log')?.children.length??null,title:document.title,"
    "cause:plasma.state.cause,aborted:plasma.state.aborted,exit:plasma.state.exitCode,firstFrame:plasma.state.firstFrame};}"
)


def status_builds(prefix=""):
    """Fixture clients that stop at each step of a run: the download, the runtime ready, the first frame, the exits."""
    builds = {
        "downloading": fixture(prefix + "downloading", ""),
        "running": fixture(prefix + "running", "Module.onRuntimeInitialized();\n"),
        "presented": fixture(prefix + "presented", "Module.onRuntimeInitialized();\nModule.plasma.firstFrame();\n"),
        "left": fixture(
            prefix + "left", "Module.onRuntimeInitialized();\nModule.wasmMemory.grow(2);\nModule.onExit(0);\n"
        ),
        "left-presented": fixture(
            prefix + "left-presented",
            "Module.onRuntimeInitialized();\nModule.plasma.firstFrame();\nModule.onExit(0);\n",
        ),
        "exit": fixture(prefix + "exit", "Module.onRuntimeInitialized();\nModule.onExit(3);\n"),
        "abort": fixture(prefix + "abort", 'Module.onRuntimeInitialized();\nModule.onAbort("fixture abort");\n'),
        "missing": out / f"fixture-{prefix}missing",
    }
    builds["missing"].mkdir()
    return builds


# The page's exit line for each fixture that exits: the exit code, and the size of the client's wasm memory at the exit
# (the fixture's one 64 KiB page, three after "left" grows it by two).
EXIT_LINES = {
    "left": ["[page] runtime exited with code 0 (wasm memory 196608 bytes)"],
    "left-presented": ["[page] runtime exited with code 0 (wasm memory 65536 bytes)"],
    "exit": ["[page] runtime exited with code 3 (wasm memory 65536 bytes)"],
}


def status_rows(plan, builds, path="/", title="Uru Live"):
    """Each row of plan, (label, build, document, query, the status line, its position when shown over the stage, the
    cause, the DOM log's lines), on the page at path: what the status line says, where, the error logged once, and the
    exit line of a client that exits."""
    rows = {}
    try:
        for label, build, doc, query, text, position, cause, log in plan:
            cfg.build_dir = str(builds[build])
            publish(doc)
            st = booted(page, base, query, server.log_file, path)
            u = page.evaluate(UI)
            errors = [m for m in st["console"] if m.startswith("[page] error ")]
            row = rows[label] = {"status": u["text"], "cause": u["cause"], "errors": errors}
            equal(
                (label, u["text"], u["cause"], u["shown"], u["position"] if u["shown"] else None, u["log"], u["title"]),
                (label, text, cause, position is not None, position, log, title),
            )
            # the first frame is recorded once, and only where the client presented one
            equal((label, u["firstFrame"] is not None), (label, "presented" in build))
            exits = [m for m in st["console"] if m.startswith("[page] runtime exited")]
            equal((label, exits), (label, EXIT_LINES.get(build, [])))
            if cause:
                equal((label, u["aborted"], len(errors)), (label, text, 1))
                ok(errors[0].startswith(f"[page] error {cause}: ") and "://" not in errors[0], errors)
            else:
                equal((label, errors, u["aborted"]), (label, [], None))
            if label in ("downloading", "running", "presented", "ini-pin", "abort"):
                page.screenshot(path=str(out / f"status-{label}.png"))
    finally:
        cfg.build_dir = str(fixture_build)
        publish(document)
    return rows


def status_line():
    texts = boot_module()["texts"]
    status = boot_module()["status"]
    with urllib.request.urlopen(base + "/", timeout=5) as r:
        html = r.read().decode()
    ok("<title>Uru Live</title>" in html and "Loading the game&hellip;</div>" in html, "title or first status line")
    # label: (build, document, query, the status line, shown over the stage, the cause, the DOM log's lines: none on the
    # player page, which has no log)
    # After onRuntimeInitialized the line says the game loads and stays over the stage until the client's first
    # frame (Module.plasma.firstFrame); then a player page hides it, and a run's end brings it back
    plan = [
        ("downloading", "downloading", document, "", status["download"], "fixed", None, None),
        ("running", "running", document, "", status["loading"], "fixed", None, None),
        ("presented", "presented", document, "", status["running"], None, None, None),
        ("left", "left", document, "", status["left"], "static", None, None),
        ("left after a frame", "left-presented", document, "", status["left"], "static", None, None),
        ("exit", "exit", document, "", texts["CLIENT-EXIT"], "static", "CLIENT-EXIT", None),
        ("abort", "abort", document, "", texts["CLIENT-ABORT"], "static", "CLIENT-ABORT", None),
        ("missing", "missing", document, "", texts["CLIENT-LOAD"], "static", "CLIENT-LOAD", None),
        (
            "ini-pin",
            "running",
            {**document, "shard": {**document["shard"], "sha256": "0" * 64}},
            "",
            texts["INI-PIN"],
            "static",
            "INI-PIN",
            None,
        ),
    ]
    return status_rows(plan, status_builds())


check(
    "the status line speaks to players, names the cause code of a fatal error and logs it once; no DOM log on a player page",
    status_line,
)


# ---- the loading line until the client's first frame; the error and refusal pages are dark ----
def rgb(css):
    m = re.fullmatch(r"rgba?\((\d+), (\d+), (\d+)(?:, ([\d.]+))?\)", css)
    ok(m, css)
    return tuple(int(v) for v in m.groups()[:3]), (float(m.group(4)) if m.group(4) else 1.0)


def contrast(a, b):
    def lum(c):
        c = [v / 255 for v in c]
        c = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]

    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


LOOK = (
    "()=>{const g=e=>getComputedStyle(e),s=document.getElementById('status'),c=document.getElementById('compatibility');"
    "const b=[g(document.body).backgroundColor,g(document.documentElement).backgroundColor];"
    "return {page:b.find(v=>v!=='rgba(0, 0, 0, 0)')||'rgba(0, 0, 0, 0)',status:g(s).color,shown:g(s).display!=='none'&&g(s).visibility!=='hidden',"
    "text:s.textContent,compat:c.hidden?null:{color:g(c.querySelector('p')).color,title:g(c.querySelector('h2')).color,border:g(c).borderTopColor}};}"
)


def first_frame_line():
    # The fixture client's runtime is ready (Module.onRuntimeInitialized, main() starts) and stays without a frame: the
    # loading line stays over the black stage, however long; the client's first frame (Module.plasma.firstFrame, as
    # plWGPUPipeline.js calls it) hides it on a player page, once.
    status = boot_module()["status"]
    seen = {}
    hold = fixture("hold", "Module.onRuntimeInitialized();\n")
    try:
        cfg.build_dir = str(hold)
        publish(document)
        booted(page, base, "", server.log_file)
        page.wait_for_timeout(1500)
        before = page.evaluate(LOOK)
        u = page.evaluate(UI)
        equal(
            (
                "before the first frame",
                before["text"],
                before["shown"],
                u["firstFrame"],
                "plasma-presented" in u["classes"],
            ),
            ("before the first frame", status["loading"], True, None, False),
        )
        page.screenshot(path=str(out / "first-frame-before.png"))
        page.evaluate("()=>{plasma.firstFrame();plasma.firstFrame();}")
        after = page.evaluate(LOOK)
        u2 = page.evaluate(UI)
        equal(
            ("after the first frame", after["text"], after["shown"], "plasma-presented" in u2["classes"]),
            ("after the first frame", status["running"], False, True),
        )
        ok(u2["firstFrame"] is not None and u2["firstFrame"] > 0, u2)
        page.evaluate("()=>plasma.firstFrame()")
        equal(page.evaluate(UI)["firstFrame"], u2["firstFrame"])  # once per run
        seen = {"before": before["text"], "after shown": after["shown"], "first frame ms": round(u2["firstFrame"])}
    finally:
        cfg.build_dir = str(fixture_build)
        publish(document)
    return seen


check(
    "the loading line stays after the client starts until its first frame is on the canvas, then steps aside",
    first_frame_line,
)


def dark_errors():
    # The error and refusal pages have the player page's look: its black background, light text that reads on it
    # (contrast at least 7:1), and the cause code in the status line, whatever the browser's colour scheme.
    texts = boot_module()["texts"]
    seen = {}
    builds = {
        "abort": fixture("dark-abort", 'Module.onRuntimeInitialized();\nModule.onAbort("fixture abort");\n'),
        "missing": out / "fixture-dark-missing",
        "running": fixture("dark-running", "Module.onRuntimeInitialized();\n"),
        "gpu": fixture(
            "dark-gpu",
            'Module.onRuntimeInitialized();\nModule.plasma.bootFailure(["worker-gpu"], "fixture device failure");\n',
        ),
    }
    builds["missing"].mkdir()
    cases = [
        ("player abort", "abort", document, "CLIENT-ABORT"),
        ("player client missing", "missing", document, "CLIENT-LOAD"),
        ("refused ini-pin", "running", {**document, "shard": {**document["shard"], "sha256": "0" * 64}}, "INI-PIN"),
        ("refused config", "running", {**document, "capabilities": {"harness": "yes"}}, "CONFIG-INVALID"),
        ("player gpu", "gpu", document, "BROWSER-GPU"),
    ]
    try:
        publish(document)
        booted(page, base, "", server.log_file)
        player = page.evaluate(LOOK)["page"]  # the player page itself
        equal(rgb(player), ((0, 0, 0), 1.0))
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            for label, build, doc, cause in cases:
                cfg.build_dir = str(builds[build])
                publish(doc)
                booted(page, base, "", server.log_file)
                v = page.evaluate(LOOK)
                key = f"{label} ({scheme})"
                ratio = contrast(rgb(v["status"])[0], rgb(v["page"])[0])
                seen[key] = {"page": v["page"], "status": v["status"], "contrast": round(ratio, 1)}
                # every case is measured; the failures are listed together
                if (v["page"], v["shown"], v["text"]) != (player, True, texts[cause]):
                    seen[key]["fail"] = (v["page"], v["shown"], v["text"])
                elif not v["text"].endswith(" Error code: " + cause) or ratio < 7:
                    seen[key]["fail"] = ("status", v["status"], round(ratio, 2))
                elif cause == "BROWSER-GPU":
                    parts = (
                        {p: contrast(rgb(v["compat"][p])[0], rgb(v["page"])[0]) for p in ("color", "title", "border")}
                        if v["compat"]
                        else {}
                    )
                    seen[key]["compatibility"] = {p: round(r, 1) for p, r in parts.items()}
                    if not parts or min(parts["color"], parts["title"]) < 7 or parts["border"] < 3:
                        seen[key]["fail"] = ("compatibility", v["compat"], seen[key]["compatibility"])
                if scheme == "light" and label in ("refused ini-pin", "player gpu"):
                    page.screenshot(path=str(out / f"dark-{label.replace(' ', '-')}.png"))
    finally:
        page.emulate_media(color_scheme="null")
        cfg.build_dir = str(fixture_build)
        publish(document)
    failed = {k: v["fail"] for k, v in seen.items() if "fail" in v}
    ok(not failed, failed)
    return seen


check("the error and refusal pages are dark like the player page, readable, with the cause code", dark_errors)
# ---- the page's own controls never cover the game, nor take its clicks or its keys ----
# The game's screens use every part of the canvas: the avatar customization's QUIT and
# EXPLORE URU at the top right (QUIT at 942-1012 x 11-40 in the 1152x720 player window),
# the KI at the top left, the Big KI in all four corners of a 4:3 or taller window, an Age's clickables anywhere. The
# fixture client runs as a client does once its first frame is up (the status line hidden), and the canvas is
# focusable as the client's input layer makes it (input/capture.mjs: tabIndex 0). A texture-pack deployment, so the
# page shows every control it has.
PAGECTL_DOC = HD_DOC
QUIT_1152 = (977, 25)  # the ACA's QUIT, in window pixels of the 1152x720 player page
QUIT_FRAC = (977 / 1152, 25 / 720)  # the same as fractions of the game's screen
# every 8 CSS px over the canvas and its last row and column: the element there (the canvas, or what lies over it),
# and every visible page control whose box meets the canvas
COVER = (
    "()=>{const c=document.getElementById('canvas'),r=c.getBoundingClientRect(),bad=[],xs=[],ys=[];"
    "for(let x=r.left+1;x<r.right-1;x+=8)xs.push(x);xs.push(r.right-1);for(let y=r.top+1;y<r.bottom-1;y+=8)ys.push(y);ys.push(r.bottom-1);"
    "for(const x of xs)for(const y of ys){const e=document.elementFromPoint(x,y);if(e!==c)bad.push([Math.round(x),Math.round(y),e?(e.id||e.tagName):null]);}"
    "const over=[...document.querySelectorAll('#page-controls, #page-controls *, #fullscreen-button, #render-scale, #render-scale *, #texpack, #texpack *')].filter(e=>{"
    "const b=e.getBoundingClientRect(),s=getComputedStyle(e);return b.width>0&&b.height>0&&s.visibility!=='hidden'&&s.display!=='none'&&"
    "b.right>r.left&&b.left<r.right&&b.bottom>r.top&&b.top<r.bottom;}).map(e=>e.id||e.tagName);"
    "return {window:[innerWidth,innerHeight],canvas:[r.left,r.top,r.width,r.height],points:xs.length*ys.length,covered:bad.length,first:bad.slice(0,6),over:[...new Set(over)]};}"
)


def pagectl_open(pg):
    booted(pg, base, "", server.log_file)
    pg.evaluate(
        "()=>{const c=document.getElementById('canvas');if(!c.hasAttribute('tabindex'))c.tabIndex=0;globalThis.pagectl={down:[],keys:[],blur:0};"
        "addEventListener('mousedown',e=>pagectl.down.push(e.target.id||e.target.tagName),true);"
        "document.addEventListener('keydown',e=>pagectl.keys.push(e.target.id||e.target.tagName),true);"
        "c.addEventListener('blur',()=>pagectl.blur++);}"
    )


def pagectl_cover():
    # Three window shapes: the 16:10 window of the ACA leg, 4:3 and portrait. Nothing of the page lies over any canvas
    # pixel; a trusted click at each corner and at the ACA's QUIT reaches the canvas, which keeps the focus and draws no
    # focus ring over the game's edge; the canvas spans the window's width from its top, the strip (if any) below it.
    running = fixture("pagectl-cover", "Module.onRuntimeInitialized();\nModule.plasma.firstFrame();\n")
    seen = {}
    try:
        cfg.build_dir = str(running)
        publish(PAGECTL_DOC)
        for vw, vh in ((1152, 720), (1024, 768), (720, 1100)):
            ctx = browser.new_context(viewport={"width": vw, "height": vh})
            pg = ctx.new_page()
            try:
                pagectl_open(pg)
                v = pg.evaluate(COVER)
                u = pg.evaluate(UI)
                r = v["canvas"]
                # the client's input layer focuses the canvas from script (capture.mjs canvas.focus), which shows the browser's
                # focus ring where a mouse focus does not
                ring = pg.evaluate(
                    "()=>{const c=document.getElementById('canvas');c.focus();const o=getComputedStyle(c);"
                    "return o.outlineStyle==='none'||o.outlineWidth==='0px'?null:o.outlineStyle+' '+o.outlineWidth+' '+o.outlineColor;}"
                )
                pts = {
                    "top left": (r[0] + 3, r[1] + 3),
                    "top right": (r[0] + r[2] - 4, r[1] + 3),
                    "bottom left": (r[0] + 3, r[1] + r[3] - 4),
                    "bottom right": (r[0] + r[2] - 4, r[1] + r[3] - 4),
                    "QUIT (ACA)": (r[0] + r[2] * QUIT_FRAC[0], r[1] + r[3] * QUIT_FRAC[1]),
                }
                if (vw, vh) == (1152, 720):
                    pts["QUIT (ACA, window px)"] = QUIT_1152
                for x, y in pts.values():
                    pg.mouse.click(x, y)
                    pg.wait_for_timeout(50)
                got = pg.evaluate(
                    "()=>({down:pagectl.down,focus:document.activeElement?.id||null,offered:!document.getElementById('texpack').hidden})"
                )
                key = f"{vw}x{vh}"
                hits = dict(zip(pts, got["down"]))
                seen[key] = {
                    "canvas": r,
                    "points": v["points"],
                    "covered": v["covered"],
                    "over": v["over"],
                    "clicks": hits,
                    "strip": vh - r[3],
                }
                if (vw, vh) == (1152, 720):
                    pg.screenshot(path=str(out / "pagectl-1152x720.png"))
                fails = []
                if v["covered"] or v["over"]:
                    fails.append(("covered", v["covered"], v["first"], v["over"]))
                if len(got["down"]) != len(pts) or any(t != "canvas" for t in got["down"]):
                    fails.append(("clicks", hits))
                if got["focus"] != "canvas":
                    fails.append(("focus", got["focus"]))
                if ring:
                    fails.append(("the focused canvas is framed by a focus ring", ring))
                if (r[0], r[1], r[2]) != (0, 0, vw) or not 0 <= vh - r[3] <= 40:
                    fails.append(("layout", r))
                if "plasma-presented" not in u["classes"] or not got["offered"]:
                    fails.append(("fixture", u["classes"], got["offered"]))
                if fails:
                    seen[key]["fail"] = fails
            finally:
                ctx.close()
    finally:
        cfg.build_dir = str(fixture_build)
        publish(document)
    failed = {k: v["fail"] for k, v in seen.items() if "fail" in v}
    ok(not failed, failed)
    return seen


check(
    "no page control lies over the canvas or takes a click at its corners or the ACA QUIT (16:10, 4:3, portrait)",
    pagectl_cover,
)


def pagectl_focus():
    # The player clicks into the game, then uses each page control with ordinary clicks. Each acts (the texture setting
    # turns on by its box and off by its label, the render scale goes to 2x and back, full screen enters), while the
    # canvas keeps the focus throughout (it is never blurred, so the client's input layer never drops its held keys) and
    # the next key goes to the canvas. A press on the strip between the controls takes nothing either. In full screen
    # the stage alone fills the window: no page control shows, nothing covers the canvas.
    running = fixture("pagectl-focus", "Module.onRuntimeInitialized();\nModule.plasma.firstFrame();\n")
    steps = []
    seen = {}
    STATE = (
        "()=>({focus:document.activeElement?.id||document.activeElement?.tagName||null,blur:pagectl.blur,key:pagectl.keys.slice(-1)[0]??null,"
        "texpack:document.getElementById('texpack-on').checked,chosen:plasma.display.metrics.scaleChosen,fullscreen:document.fullscreenElement?.id||null})"
    )
    try:
        cfg.build_dir = str(running)
        publish(PAGECTL_DOC)
        ctx = browser.new_context(viewport={"width": 1152, "height": 720})
        pg = ctx.new_page()
        try:
            pagectl_open(pg)
            r = pg.evaluate(COVER)["canvas"]
            pg.mouse.click(r[0] + r[2] / 2, r[1] + r[3] / 2)
            pg.wait_for_timeout(100)  # the player clicks into the game
            select = pg.evaluate("()=>document.getElementById('render-scale').tagName==='SELECT'")

            def center(sel, fx=0.5):
                b = pg.locator(sel).first.bounding_box()
                return (b["x"] + b["width"] * fx, b["y"] + b["height"] / 2) if b else None

            def step(label, point, want, escape=False):
                if point is None:
                    steps.append({"step": label, "fail": "no such control"})
                    return
                pg.mouse.click(*point)
                pg.wait_for_timeout(300)
                if escape:
                    pg.keyboard.press("Escape")
                    pg.wait_for_timeout(100)  # closes the select's list
                pg.keyboard.press("KeyW")
                pg.wait_for_timeout(100)
                st = pg.evaluate(STATE)
                st["step"] = label
                got = {k: st[k] for k in want}
                if (st["focus"], st["blur"], st["key"]) != ("canvas", 0, "canvas") or got != want:
                    st["fail"] = {"want": want, "got": got}
                steps.append(st)

            step("texture setting, its box", center("#texpack-on"), {"texpack": True})
            step("texture setting, its label", center("#texpack label", 0.7), {"texpack": False})
            if select:
                step("render scale menu (a select)", center("#render-scale"), {"chosen": 2}, escape=True)
            else:
                step("render scale Supersample 2x", center('#render-scale button[data-scale="2"]'), {"chosen": 2})
                step("render scale Standard", center('#render-scale button[data-scale="1"]'), {"chosen": 1})
                b = pg.locator("#render-scale").bounding_box()
                s = pg.locator("#page-controls").bounding_box()
                step(
                    "the strip between the controls",
                    (b["x"] - 4, s["y"] + s["height"] / 2),
                    {"texpack": False, "chosen": 1},
                )
            step("Full screen", center("#fullscreen-button"), {"fullscreen": "stage"})
            # (the strip keeps its layout box behind the fullscreen stage: what counts is the element at each point)
            full = pg.evaluate(COVER)
            pg.screenshot(path=str(out / "pagectl-fullscreen.png"))
            hidden = pg.evaluate(
                "()=>[...document.querySelectorAll('#page-controls button, #page-controls input')].map(e=>{const b=e.getBoundingClientRect();"
                "return document.elementFromPoint(b.left+b.width/2,b.top+b.height/2)?.id||null;})"
            )
            seen["fullscreen"] = {"canvas": full["canvas"], "covered": full["covered"], "at the controls": hidden}
            if full["covered"] or full["canvas"][2:] != full["window"] or any(h != "canvas" for h in hidden):
                seen["fullscreen"]["fail"] = full
            pg.keyboard.press("Escape")
            pg.wait_for_timeout(300)
            seen["escape leaves (headless Chromium ignores it)"] = pg.evaluate("()=>!document.fullscreenElement")
            pg.evaluate("async()=>{if(document.fullscreenElement)await document.exitFullscreen();}")
            pg.wait_for_timeout(300)
            back = pg.evaluate(COVER)
            seen["windowed again"] = {"canvas": back["canvas"], "focus": pg.evaluate(STATE)["focus"]}
            if back["canvas"] != r or back["covered"]:
                seen["windowed again"]["fail"] = back
        finally:
            ctx.close()
    finally:
        cfg.build_dir = str(fixture_build)
        publish(document)
    seen["steps"] = steps
    failed = [s for s in steps if "fail" in s] + [v for v in seen.values() if isinstance(v, dict) and "fail" in v]
    ok(not failed, failed)
    return seen


check(
    "the page controls act on ordinary clicks and the keyboard stays with the canvas; full screen shows the game alone",
    pagectl_focus,
)
# what a page that ended shows of the source notice (notice_on_boot_failure)
NOTICE_LOOK = (
    "()=>{const vis=e=>{if(!e)return false;const r=e.getBoundingClientRect(),s=getComputedStyle(e);"
    "return r.width>0&&r.height>0&&s.display!=='none'&&s.visibility!=='hidden'&&r.top>=0&&r.left>=0&&r.bottom<=innerHeight&&r.right<=innerWidth;};"
    "const top=e=>{const r=e.getBoundingClientRect(),h=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);return !!h&&(h===e||e.contains(h));};"
    "const link=id=>{const e=document.getElementById(id);return vis(e)&&top(e)?e.getAttribute('href'):null;};"
    "const f=plasma.state.capabilityFailure;"
    "return {source:link('source-link'),notices:link('notices-link'),compatibility:vis(document.getElementById('compatibility')),"
    "shown:['stage','texpack','render-scale','fullscreen-button'].filter(id=>vis(document.getElementById(id))),"
    "cause:plasma.state.cause,failure:f?f.source:null};}"
)


def notice_on_boot_failure():
    # A player page that ends before the game runs still shows "Source code (GPLv3)" and "Notices": visible, on top at
    # their centres, and reached by Tab, whatever the ending hides (the stage and the strip's settings). Two browser
    # failures (Module.plasma.bootFailure): the client's WebGPU device (a fixture client, as the dark error pages'
    # check, on a texture-pack deployment, so the page had shown its texture setting), and the page's own preflight
    # before its deployment (a browser without WebGPU: an init script removes navigator.gpu), which still requests no
    # deployment and no client. Two refusals before the deployment (the dark error pages' documents): a deployment
    # document (CONFIG-INVALID) and its server.ini (INI-PIN), which request no client. (The test harness page shows no
    # player notice: the harness's own tests check that.)
    def look(pg, label, st):
        deadline = time.time() + 5  # source.json is fetched without waiting for it (boot.mjs sourceNotice)
        while time.time() < deadline and pg.evaluate("()=>document.getElementById('source-notice').hidden"):
            pg.wait_for_timeout(100)
        v = pg.evaluate(NOTICE_LOOK)
        pg.screenshot(path=str(out / f"notice-boot-failure-{label}.png"))
        pg.evaluate("()=>{if(document.activeElement)document.activeElement.blur();}")
        v["tab"] = []
        for _ in range(2):
            pg.keyboard.press("Tab")
            v["tab"].append(pg.evaluate("()=>document.activeElement?.id||document.activeElement?.tagName||null"))
        v["console"] = [m for m in st["console"] if m.startswith("[page] source code")]
        return v

    gpu = fixture(
        "notice-gpu",
        'Module.onRuntimeInitialized();\nModule.plasma.bootFailure(["worker-gpu"], "fixture device failure");\n',
    )
    want = lambda cause, source: {
        "source": source_url,
        "notices": "THIRD_PARTY_NOTICES.md",
        "compatibility": source is not None,
        "shown": [],
        "cause": cause,
        "failure": source,
        "tab": ["source-link", "notices-link"],
        "console": ["[page] source code: " + source_url + " (commit " + "0" * 40 + ")"],
    }
    bad_ini = {**document["shard"], "sha256": "0" * 64}
    seen = {}
    try:
        cfg.build_dir = str(gpu)
        publish(PAGECTL_DOC)
        st = booted(page, base, "", server.log_file)
        seen["worker"] = look(page, "worker", st)
        ctx = browser.new_context(viewport={"width": 1152, "height": 720})
        pg = ctx.new_page()
        try:
            pg.add_init_script("delete Navigator.prototype.gpu;")
            st = booted(pg, base, "", server.log_file)
            seen["preflight"] = look(pg, "preflight", st)
            seen["preflight"]["requests"] = [p for p in st["requests"] if p.startswith(("/build/", "/runtime/"))]
        finally:
            ctx.close()
        cfg.build_dir = str(fixture_build)
        for label, doc in (
            ("config", {**document, "capabilities": {"harness": "yes"}}),
            ("ini", {**document, "shard": bad_ini}),
        ):
            publish(doc)
            st = booted(page, base, "", server.log_file)
            seen[label] = look(page, label, st)
            seen[label]["client"] = [p for p in st["requests"] if p.startswith("/build/")]
    finally:
        cfg.build_dir = str(fixture_build)
        publish(document)
    equal(seen["worker"], want("BROWSER-GPU", "worker"))
    equal(seen["preflight"], {**want("BROWSER-MISSING", "preflight"), "requests": []})
    equal(seen["config"], {**want("CONFIG-INVALID", None), "client": []})
    equal(seen["ini"], {**want("INI-PIN", None), "client": []})
    return seen


check(
    "a page that ends before the game runs still shows the source and notices links, visible and reachable (device, preflight, document, server.ini)",
    notice_on_boot_failure,
)
check.stage = "policy"


# ---- the deployment document's origin and the Content-Security-Policy ----
def assert_refused_unrequested(r):
    equal((r["ok"], r.get("error"), r["requested"]), (False, REFUSED, []))


def public_page_own_origin():
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        dep = deployment.load(static_page_deployment(tmp))
        pg, own = "https://play.example.test/", "https://play.example.test"
        doc = dep.public_document()
        # Another site serves a document that is valid in every other respect: its own gateway and shard, with the
        # SHA-256 pin of its own server.ini, under the deployment's name (the remembered login's key).
        other = {
            **doc,
            "gateway": "wss://attacker.example.test/relay",
            "roots": {"data": "https://attacker.example.test/data", "fonts": "https://attacker.example.test/fonts"},
        }
        files = {**served_files(dep, own, doc)}
        for b in (
            "https://attacker.example.test",
            "https://play.example.test:8443",
            "https://cdn.play.example.test",
            "http://play.example.test",
        ):
            files.update(served_files(dep, b, other))
        for value in ("runtime/config.json", "/runtime/config.json", own + "/runtime/config.json"):
            r = node_load(pg, value, files)
            ok(r["ok"], (value, r))
            equal(r["requested"], [own + "/runtime/config.json", own + "/runtime/server.ini"])
            equal(r["client"]["gateway"], "wss://gw.example.test")
        for value in (
            "https://attacker.example.test/runtime/config.json",  # another site
            "https://play.example.test:8443/runtime/config.json",  # the same host on another port
            "https://cdn.play.example.test/runtime/config.json",  # a subdomain
            "http://play.example.test/runtime/config.json",  # the same host without TLS
            "//attacker.example.test/runtime/config.json",
        ):  # scheme-relative
            assert_refused_unrequested(node_load(pg, value, files))


check("a public page takes the deployment document from its own origin only", public_page_own_origin, browser=False)


def loopback_other_origin():
    # Development: the origin rule does not apply to a loopback page's loader. The page itself never names another
    # origin (always runtime/config.json beside it); on a page served by server.py --deployment, the
    # Content-Security-Policy still blocks such a request (the Chromium case below).
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        dep = deployment.load(write_deployment(tmp))
        for pg, b, gateway in (
            ("http://127.0.0.1:8790/", "http://127.0.0.1:9", "ws://127.0.0.1:8791"),
            ("http://localhost:8790/", "http://127.0.0.1:9", "ws://127.0.0.1:8791"),
            ("http://[::1]:8790/", "http://localhost:9", "ws://localhost:8791"),
            ("http://127.0.0.1:8790/", "https://assets.example.test", "wss://gw.example.test"),
        ):
            doc = dep.public_document(gateway, status_relay="/runtime/status")
            r = node_load(pg, b + "/runtime/config.json", served_files(dep, b, doc))
            ok(r["ok"], (pg, b, r))
            equal(r["requested"], [b + "/runtime/config.json", b + "/runtime/server.ini"])


check("the loader on a loopback page may name another origin", loopback_other_origin, browser=False)


def own_origin_pass():
    # A LAN page on its own origin (TLS, an address on the LAN, the gateway on the next port) and a loopback page,
    # both loading the page's runtime/config.json, as index.html does (config.mjs loadPageDeployment).
    for bind, pg, gateway in (
        ("192.0.2.186", "https://192.0.2.186:8792/", "wss://192.0.2.186:8793"),
        ("127.0.0.1", "http://127.0.0.1:8790/", "ws://127.0.0.1:8791"),
    ):
        with tempfile.TemporaryDirectory(dir=out) as tmp:
            dep = deployment.load(own_origin_deployment(tmp, bind, gateway, pg.rstrip("/")))
            doc = dep.public_document(status_relay="/runtime/status")
            own = pg.rstrip("/")
            r = node_load(pg, "runtime/config.json", served_files(dep, own, doc))
            ok(r["ok"], (pg, r))
            equal(r["requested"], [own + "/runtime/config.json", own + "/runtime/server.ini"])
            equal(
                (r["client"]["gateway"], r["client"]["join"], r["client"]["status"], r["client"]["hdPack"]["base"]),
                (gateway, own + "/runtime/join", own + "/runtime/status", own + "/hdpack"),
            )
            equal(deployment.content_security_policy(doc), f"connect-src 'self' {gateway}")
            if bind != "127.0.0.1":  # the LAN page refuses the same document from another page on its host
                twin = "https://192.0.2.186:8794"
                assert_refused_unrequested(node_load(pg, twin + "/runtime/config.json", served_files(dep, twin, doc)))


check("own-origin deployments (a LAN page over TLS, a loopback page) load", own_origin_pass, browser=False)


def connect_sources():
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        doc = deployment.load(static_page_deployment(tmp)).public_document()
        equal(
            deployment.content_security_policy(doc),
            "connect-src 'self' wss://gw.example.test https://assets.example.test https://sound.example.test",
        )
    b = {"schema": 1, "roots": {"fonts": "/fonts"}, "shard": {"serverIni": "/runtime/server.ini"}}
    for gateway, extra, sources in (
        ("ws://127.0.0.1:8791", {}, ["'self'", "ws://127.0.0.1:8791"]),
        ("wss://GW.Example.TEST:443/relay", {}, ["'self'", "wss://gw.example.test"]),  # default port, case
        (
            "wss://gw.example.test",
            {
                "roots": {"fonts": "https://gw.example.test:8443/f", "data": "/data"},
                "textures": {"hdPack": {"base": "https://code.example.test/hd", "ages": ["Cleft"]}},
                "join": "/runtime/join",
                "shard": {"serverIni": "/runtime/server.ini", "status": "https://status.example.test/s"},
            },
            [
                "'self'",
                "wss://gw.example.test",
                "https://status.example.test",
                "https://gw.example.test:8443",
                "https://code.example.test",
            ],
        ),
    ):
        equal(deployment.connect_sources({**b, "gateway": gateway, **extra}), sources)
    for bad in ("ws://[::1]:8791", "ftp://gw.example.test", "wss://gw.example.test:99999"):
        try:
            deployment.connect_sources({**b, "gateway": bad})
        except deployment.DeploymentError:
            continue
        raise AssertionError("accepted " + bad)


check("connect-src is self plus the origins the document names", connect_sources, browser=False)


def static_kit_headers():
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        path = static_page_deployment(tmp)
        kit = Path(tmp) / "kit"
        r = subprocess.run(
            [sys.executable, "-m", "plasma_web.emit_static", str(path), str(kit)], capture_output=True, text=True
        )
        ok(r.returncode == 0, r.stderr[-800:])
        doc = json.loads((kit / "runtime/config.json").read_text())
        blocks, current = {}, None
        for line in (kit / "_headers").read_text().splitlines():
            if line.startswith("/"):
                current = blocks.setdefault(line, [])
            elif line.startswith("  ") and current is not None:
                current.append(line.strip())
        ok(f"Content-Security-Policy: {deployment.content_security_policy(doc)}" in blocks["/*"], blocks)
        equal(sum(h.startswith("Content-Security-Policy:") for hs in blocks.values() for h in hs), 1)


check("the static kit's _headers carry the deployment's policy once", static_kit_headers, browser=False)


def free_fonts_in_chromium():
    # A player deployment that names no fonts. Chromium reads its /fonts tree from server.py --deployment as
    # the client's fetch backend does (plEmscriptenFetchBackend.js: the .fetch-manifest, a HEAD for a strong ETag and the
    # length, then Range reads under If-Match) and gets the free substitutes; the static kit of the same deployment
    # carries the same bytes under the same names, with both licence files and THIRD_PARTY_NOTICES.md.
    free = {n: p.read_bytes() for n, p in deployment.free_font_files().items()}
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        path = write_deployment(
            tmp,
            capabilities={},
            data={"root": "data", "files": "patcher", "gameCode": "server"},
            gateway={
                "bind": "127.0.0.1",
                "port": 0,
                "url": "ws://127.0.0.1:8791",
                "origins": [],
                "join": {"secretFile": str(Path(tmp) / "join.key")},
            },
        )
        r = subprocess.run(
            [sys.executable, "-m", "plasma_web.deployment", "--init-join-secret", str(path)],
            capture_output=True,
            text=True,
        )
        ok(r.returncode == 0, r.stderr[-800:])
        pweb, build = write_page(tmp)
        srv = ServerProcess(stack, "--deployment", str(path), "--web-root", str(pweb), "--build-dir", str(build))
        ctx = browser.new_context()
        stack.callback(ctx.close)
        pg = ctx.new_page()
        pg.goto(srv.base + "/")
        seen = pg.evaluate(
            """async names => {
      const hex = async b => [...new Uint8Array(await crypto.subtle.digest('SHA-256', b))]
          .map(x => x.toString(16).padStart(2, '0')).join('');
      const manifest = await (await fetch('/fonts/.fetch-manifest', {cache: 'no-store'})).text();
      const out = {crossOriginIsolated, manifest: manifest.trim().split('\\n').map(l => l.split(' ').slice(2).join(' ')),
                   files: {}};
      for (const name of names) {
          const url = 'fonts/' + name;     // the client's fetch tree URL: base + '/' + the manifest name
          const head = await fetch(url, {method: 'HEAD', cache: 'no-store', redirect: 'error'});
          const etag = head.headers.get('ETag'), size = Number(head.headers.get('Content-Length'));
          const r = await fetch(url, {cache: 'no-store', redirect: 'error',
                                      headers: {'Range': `bytes=0-${size - 1}`, 'If-Match': etag}});
          const body = await r.arrayBuffer();
          out.files[name] = {status: r.status, strong: /^"[^"]*"$/.test(etag), same: r.headers.get('ETag') === etag,
                             size, bytes: body.byteLength, sha256: await hex(body)};
      }
      return out; }""",
            list(free),
        )
        (out / "fonts-chromium.json").write_text(json.dumps(seen, indent=1))
        equal(seen["crossOriginIsolated"], True)
        equal(seen["manifest"], list(free))
        for n, b in free.items():
            equal(
                seen["files"][n],
                {
                    "status": 206,
                    "strong": True,
                    "same": True,
                    "size": len(b),
                    "bytes": len(b),
                    "sha256": hashlib.sha256(b).hexdigest(),
                },
            )
        kit = Path(tmp) / "kit"
        r = subprocess.run(
            [sys.executable, "-m", "plasma_web.emit_static", str(path), str(kit)], capture_output=True, text=True
        )
        ok(r.returncode == 0, r.stderr[-800:])
        equal(json.loads((kit / "runtime/config.json").read_text())["roots"]["fonts"], "/fonts")
        equal({n: (kit / "fonts" / n).read_bytes() for n in free}, free)
        equal(
            sorted(p.name for p in (kit / "fonts").iterdir()),
            sorted([".fetch-manifest", *free, "LICENSE-DejaVuFonts.txt", "LICENSE-LiberationFonts.txt"]),
        )
        notices = (kit / "THIRD_PARTY_NOTICES.md").read_text()
        for text in (
            "Liberation Mono Regular",
            "SIL Open Font License",
            "DejaVu Sans Bold",
            "Bitstream Vera Fonts licence",
        ):
            ok(text in notices, text)
        return {n: v["sha256"][:12] for n, v in seen["files"].items()}


check(
    "a deployment that names no fonts serves the free substitutes to Chromium, and its static kit carries them with their licences",
    free_fonts_in_chromium,
)
# every route of a run, served or not (the test harness's routes are 404s here, and served by its own server)
POLICY_ROUTES = (
    "/",
    "/harness.html",
    "/runtime/gamecode-export.html",
    "/runtime/config.json",
    "/runtime/config.mjs",
    "/runtime/server.ini",
    "/build/plClient.js",
    "/data/.fetch-manifest",
    "/missing",
)


def policy_on_routes(script=PRODUCT_SERVER):
    """The origin server script sends the kit's policy with --deployment on every route, and none without a deployment."""
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        path = write_deployment(tmp)
        pweb, build = write_page(tmp)
        srv = ServerProcess(
            stack,
            "--deployment",
            str(path),
            "--gateway-url",
            "ws://127.0.0.1:8791",
            "--web-root",
            str(pweb),
            "--build-dir",
            str(build),
            script=script,
        )
        expected = deployment.content_security_policy(deployment.load(path).public_document("ws://127.0.0.1:8791"))
        equal(expected, "connect-src 'self' ws://127.0.0.1:8791")
        for p in POLICY_ROUTES:
            equal((p, srv.headers(p).get_all("Content-Security-Policy")), (p, [expected]))
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        pweb, build = write_page(tmp)
        (Path(tmp) / "data").mkdir()
        srv = ServerProcess(
            stack,
            "--data-root",
            str(Path(tmp) / "data"),
            "--web-root",
            str(pweb),
            "--build-dir",
            str(build),
            script=script,
        )
        equal(srv.headers("/").get("Content-Security-Policy"), None)


def server_policy():
    policy_on_routes(PRODUCT_SERVER)


check(
    "the origin server sends the kit policy with --deployment on every route, and none without a deployment",
    server_policy,
    browser=False,
)


def player_deployment(tmp):
    """A loopback deployment without capabilities.harness (so with gateway.join, its key created as an operator does)."""
    path = write_deployment(
        tmp,
        capabilities={},
        gateway={
            "bind": "127.0.0.1",
            "port": 0,
            "url": "ws://127.0.0.1:8791",
            "origins": [],
            "join": {"secretFile": str(Path(tmp) / "join.key")},
        },
    )
    r = subprocess.run(
        [sys.executable, "-m", "plasma_web.deployment", "--init-join-secret", str(path)], capture_output=True, text=True
    )
    ok(r.returncode == 0, r.stderr[-800:])
    return path


def status_of(srv, path, method="GET"):
    try:
        with urllib.request.urlopen(urllib.request.Request(srv.base + path, method=method), timeout=5) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, b""


def refused(script, *args):
    """An origin server that refuses to start: its exit status and its last error line."""
    r = subprocess.run(
        [sys.executable, *program(script), "--port", str(free_port()), "--access-log", os.devnull, *args],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return r.returncode, (r.stderr.strip().splitlines() or [""])[-1]


# the player page, and the test harness's page, modules and export page
HARNESS_ROUTES = ("/", "/harness.html", "/harness.mjs", "/harness-gamecode.mjs", "/runtime/gamecode-export.html")


def route_runs(tmp):
    """The origin server runs of the route checks, in tmp: a harness deployment, a player deployment, none, and a page
    document with and without capabilities.harness. Returns {label: arguments}, the web root and the build."""
    for d in ("h", "p", "w"):
        (tmp / d).mkdir()
    hpath = write_deployment(tmp / "h")
    ppath = player_deployment(tmp / "p")
    pweb, build = write_page(tmp / "w")
    (tmp / "data").mkdir()
    (tmp / "hdoc.json").write_text(json.dumps({**document, "capabilities": {"harness": True}}))
    (tmp / "pdoc.json").write_text(json.dumps(document))
    return (
        {
            "harness deployment": ["--deployment", str(hpath), "--gateway-url", "ws://127.0.0.1:8791"],
            "player deployment": ["--deployment", str(ppath)],
            "no deployment": ["--data-root", str(tmp / "data")],
            "harness page document": ["--runtime-config", str(tmp / "hdoc.json"), "--data-root", str(tmp / "data")],
            "player page document": ["--runtime-config", str(tmp / "pdoc.json"), "--data-root", str(tmp / "data")],
        },
        pweb,
        build,
    )


def no_harness_routes():
    # The product's origin server serves none of the test harness's routes on any run, a harness deployment's included;
    # the player page is served on every run. (The harness server's own tests check that it serves them, its page for a
    # harness run only, and refuses a player deployment.)
    seen = {}
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        runs, pweb, build = route_runs(Path(tmp))
        for label, args in runs.items():
            srv = ServerProcess(stack, *args, "--web-root", str(pweb), "--build-dir", str(build))
            seen[label] = {p: status_of(srv, p)[0] for p in HARNESS_ROUTES}
    equal(seen, {label: {"/": 200, **{p: 404 for p in HARNESS_ROUTES[1:]}} for label in runs})
    return seen


check(
    "the product server serves no test harness route on any run, a harness deployment's included",
    no_harness_routes,
    browser=False,
)


def options_fixture(tmp):
    """A data tree with loose game code (python/, SDL/), a sealed game-code folder, a page, a pack root, the test
    harness's options, and the two deployments only it serves (with the product server's refusal of each)."""
    f = SimpleNamespace(data=tmp / "data", seal=tmp / "seal", hd=tmp / "hd")
    for d in ("python", "SDL", "dat"):
        (f.data / d).mkdir(parents=True)
    (f.data / "python/fixture.py").write_text("x=1\n")
    (f.data / "SDL/fixture.sdl").write_text("# sdl\n")
    (f.data / "dat/fixture.age").write_bytes(b"age")
    (f.seal / "python").mkdir(parents=True)
    (f.seal / "SDL").mkdir()
    (f.seal / "python/fixture.pak").write_bytes(b"pak")
    (f.seal / "SDL/fixture.sdl").write_text("# sealed\n")
    f.web, f.build = write_page(tmp)
    f.hd.mkdir()
    f.options = [
        ["--game-code", "local"],
        ["--game-code-dir", str(f.seal)],
        ["--client-name", "plClient-py2"],
        ["--host-fonts"],
        ["--head-delay-ms", "5"],
        ["--no-asset-catalog"],
        ["--hdpack-throttle", "2048:1"],
    ]
    f.deployments = [
        (
            "fonts.hostCopies",
            write_deployment(tmp / "f", fonts={"hostCopies": True}),
            "fonts.hostCopies is a test-harness setting",
        ),
        (
            "capabilities.gameCodeCapture",
            write_deployment(
                tmp / "c",
                data={"root": "data", "files": "patcher", "gameCode": "server"},
                capabilities={"harness": True, "gameCodeCapture": True},
            ),
            "capabilities.gameCodeCapture is a test-harness diagnostic",
        ),
    ]
    return f


LAYOUT = ("/data/python/fixture.py", "/data/SDL/fixture.sdl", "/data/dat/fixture.age")


def harness_options_refused():
    # The test harness's options and deployments stay with the harness server (its own tests check that it takes them).
    # The product's server takes none of its options (argparse: unrecognized), serves the server game-code layout (no
    # loose Python or SDL) and refuses the deployments that need the harness: fonts.hostCopies (this host's licensed font
    # copies) and capabilities.gameCodeCapture (its export page).
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        f = options_fixture(Path(tmp))
        product_options = {
            o[0]: refused(PRODUCT_SERVER, "--data-root", str(f.data), "--hdpack-root", str(f.hd), *o) for o in f.options
        }
        for o, (code, line) in product_options.items():
            equal((o, code, "unrecognized arguments: " + o in line), (o, 2, True))
        srv = ServerProcess(stack, "--data-root", str(f.data), "--web-root", str(f.web), "--build-dir", str(f.build))
        layout = [status_of(srv, u)[0] for u in LAYOUT]
        equal(
            (layout, "python/fixture.py" in status_of(srv, "/data/.fetch-manifest")[1].decode()),
            ([404, 404, 200], False),
        )
        for label, path, want in f.deployments:
            code, line = refused(
                PRODUCT_SERVER,
                "--deployment",
                str(path),
                "--gateway-url",
                "ws://127.0.0.1:8791",
                "--web-root",
                str(f.web),
                "--build-dir",
                str(f.build),
            )
            equal((label, code, want in line), (label, 2, True))
    return {"product refuses": sorted(product_options), "layout": layout}


check(
    "the product server takes none of the test harness's options, serves no loose game code and refuses the harness's deployments",
    harness_options_refused,
    browser=False,
)


def access_rows(script, tmp, stack):
    """The access log rows of three requests (a GET, a HEAD, a 404) to the origin server script."""
    tmp.mkdir(exist_ok=True)
    (tmp / "data").mkdir()
    pweb, build = write_page(tmp)
    log = tmp / "access.jsonl"
    srv = ServerProcess(
        stack,
        "--data-root",
        str(tmp / "data"),
        "--web-root",
        str(pweb),
        "--build-dir",
        str(build),
        script=script,
        log=log,
    )
    status_of(srv, "/")
    status_of(srv, "/index.html", "HEAD")
    status_of(srv, "/missing")
    rows = [json.loads(line) for line in log.read_text().splitlines()]
    equal(len(rows), 3)
    return rows


ACCESS_FIELDS = ["bytes", "method", "ms", "path", "range", "status", "t"]


def access_log_fields():
    # The product's access log records each request's start time, duration, method, path and outcome, and nothing else
    # (the test harness server adds the timing fields its measurements read; its own tests check them).
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        fields = sorted({k for r in access_rows(PRODUCT_SERVER, Path(tmp), stack) for k in r})
    equal(fields, ACCESS_FIELDS)
    return fields


check(
    "the product access log records the request and its outcome, and no timing field of the test harness",
    access_log_fields,
    browser=False,
)


def web_root_allowlist():
    # The web root is an allowlist (server.Handler.page_files): index.html and boot.mjs, no other file there.
    # By default the web root is the page's directory (plasma_web.PAGE_DIR), which holds the runtime configuration
    # module too; it is served only as /runtime/config.mjs, never from the web root.
    others = ("config.mjs",)
    for n in others:
        ok((PAGE_DIR / n).is_file(), n)  # present: a 404 is the allowlist's
    seen = {}
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        tmp = Path(tmp)
        (tmp / "w").mkdir()
        ppath = player_deployment(tmp / "p")
        pweb, build = write_page(tmp / "w")
        (pweb / "boot.mjs").write_text("// fixture\n")
        (pweb / "notes.txt").write_text("private\n")
        (pweb / "sub").mkdir()
        (pweb / "sub/index.html").write_text("x")
        player = ServerProcess(stack, "--deployment", str(ppath), "--build-dir", str(build))
        seen["player run, default web root"] = {
            p: status_of(player, p)[0] for p in ("/", "/index.html", "/boot.mjs", *("/" + n for n in others))
        }
        own = ServerProcess(stack, "--deployment", str(ppath), "--web-root", str(pweb), "--build-dir", str(build))
        seen["player run, own web root"] = {
            p: status_of(own, p)[0] for p in ("/", "/index.html", "/boot.mjs", "/notes.txt", "/sub/index.html")
        }
    equal(
        seen["player run, default web root"],
        {"/": 200, "/index.html": 200, "/boot.mjs": 200, **{"/" + n: 404 for n in others}},
    )
    equal(
        seen["player run, own web root"],
        {"/": 200, "/index.html": 200, "/boot.mjs": 200, "/notes.txt": 404, "/sub/index.html": 404},
    )
    return seen


check("the web root is an allowlist: the player page and its module only", web_root_allowlist, browser=False)


def player_kit(tmp):
    """The static kit of a player deployment, emitted in tmp: its folder and its files (relative paths)."""
    path = player_deployment(tmp)
    kit = Path(tmp) / "kit"
    r = subprocess.run(
        [sys.executable, "-m", "plasma_web.emit_static", str(path), str(kit)], capture_output=True, text=True
    )
    ok(r.returncode == 0, r.stderr[-800:])
    return kit, sorted(str(f.relative_to(kit)) for f in kit.rglob("*") if f.is_file())


def kit_without_test_hooks():
    # A static kit carries the player page only: no harness.html or harness.mjs, no file of the test-hook overlay
    # (the private test harness, which this tree does not carry; its own tests scan a kit for its files' content too).
    # emit_static.py refuses a page list that names one, by name or by a source under the overlay (the guard's own
    # negative control: the same call with such a file added).
    from plasma_web import TEST_HOOKS_DIR, emit_static

    with tempfile.TemporaryDirectory(dir=out) as tmp:
        kit, files = player_kit(tmp)
        equal([f for f in files if Path(f).name.startswith("harness.") or f.startswith("testhooks")], [])
        ok("index.html" in files and "boot.mjs" in files, files)
    refused = {}
    for label, extra in (
        ("by name", {"harness.html": REAL_PAGE}),
        ("by source", {"page.mjs": TEST_HOOKS_DIR / "web/harness.mjs"}),
    ):
        try:
            emit_static.refuse_test_hook_files({**emit_static.PAGE_FILES, **extra})
            refused[label] = False
        except SystemExit as e:
            refused[label] = "never part of a kit" in str(e)
    emit_static.refuse_test_hook_files(emit_static.PAGE_FILES)  # the real page list passes
    equal(refused, {"by name": True, "by source": True})
    return {"kit_files": len(files)}


check(
    "the static kit never contains the harness page or any test-hook file; emit_static.py refuses one",
    kit_without_test_hooks,
    browser=False,
)


def policy_in_chromium():
    with tempfile.TemporaryDirectory(dir=out) as tmp, contextlib.ExitStack() as stack:
        path = write_deployment(tmp)
        pweb, build = write_page(tmp)
        gateway, elsewhere = Listener(stack), Listener(stack)
        dep = deployment.load(path)
        doc = dep.public_document(f"ws://127.0.0.1:{gateway.port}", status_relay="/runtime/status")
        other = OtherOrigin(
            stack, {"/runtime/config.json": json.dumps(doc).encode(), "/runtime/server.ini": dep.server_ini_bytes}
        )
        srv = ServerProcess(
            stack,
            "--deployment",
            str(path),
            "--gateway-url",
            f"ws://127.0.0.1:{gateway.port}",
            "--web-root",
            str(pweb),
            "--build-dir",
            str(build),
        )
        ctx = browser.new_context()
        stack.callback(ctx.close)
        pg = ctx.new_page()
        response = pg.goto(srv.base + "/")
        equal(response.headers.get("content-security-policy"), f"connect-src 'self' ws://127.0.0.1:{gateway.port}")
        pg.evaluate("""() => { globalThis.violations = [];
      document.addEventListener('securitypolicyviolation', e => violations.push({uri: e.blockedURI, directive: e.effectiveDirective})); }""")
        seen = pg.evaluate(
            """async ([other, gw, elsewhere]) => {
      const {loadRuntimeConfig} = await import('/runtime/config.mjs');
      const load = async v => { try { const r = await loadRuntimeConfig(v); return {ok: true, gateway: r.client.gateway}; }
                                catch (e) { return {ok: false, error: e.message}; } };
      const out = {own: await load('runtime/config.json'), foreign: await load(other + '/runtime/config.json')};
      try { await fetch(other + '/probe'); out.fetch = 'fetched'; } catch (_) { out.fetch = 'blocked'; }
      const socket = url => { try { new WebSocket(url); return 'constructed'; } catch (e) { return 'threw ' + e.name; } };
      out.gatewaySocket = socket(gw + '/'); out.otherSocket = socket(elsewhere + '/');
      const worker = new Worker('/build/plClient.js'), messages = [];
      out.worker = await new Promise(resolve => {
          worker.onmessage = e => { messages.push(e.data); if (e.data.results) resolve(e.data.results); };
          worker.postMessage(['/runtime/config.json', other + '/probe-worker']); });
      await new Promise(r => setTimeout(r, 1000));
      out.workerViolations = messages.filter(m => m.violation).map(m => m.violation);
      out.violations = violations; return out; }""",
            [other.base, f"ws://127.0.0.1:{gateway.port}", f"ws://127.0.0.1:{elsewhere.port}"],
        )
        time.sleep(0.5)
        (out / "csp-chromium.json").write_text(
            json.dumps(
                {
                    **seen,
                    "otherOriginRequests": other.requests,
                    "gatewayAccepted": gateway.accepted,
                    "otherSocketAccepted": elsewhere.accepted,
                },
                indent=1,
            )
        )
        equal(seen["own"], {"ok": True, "gateway": f"ws://127.0.0.1:{gateway.port}"})
        # The loopback page passes the origin rule; the policy stops the request itself.
        equal(seen["foreign"], {"ok": False, "error": REFUSED})
        equal(seen["fetch"], "blocked")
        equal(seen["worker"], {"/runtime/config.json": "fetched", other.base + "/probe-worker": "blocked"})
        equal(other.requests, [])
        ok(gateway.accepted >= 1, gateway.accepted)
        equal(elsewhere.accepted, 0)
        blocked = {(v["uri"].rstrip("/"), v["directive"]) for v in seen["violations"]}
        equal(
            blocked,
            {
                (other.base + "/runtime/config.json", "connect-src"),
                (other.base + "/probe", "connect-src"),
                (f"ws://127.0.0.1:{elsewhere.port}", "connect-src"),
            },
        )
        equal([u.rstrip("/") for u in seen["workerViolations"]], [other.base + "/probe-worker"])


check("Chromium enforces the policy on the page and the client worker", policy_in_chromium)


@check.tests
class RuntimeConfiguration(unittest.TestCase):
    """The checks above, in their order; each starts from its stage's state (enter)."""


def setUpModule():
    start()


def tearDownModule():
    check.summary(
        "the deployment loader, the player page and the origin server over real HTTP, with synthetic public keys; no engine, no shard"
    )
    stop()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=BUILD,
        help="a built client: the build directory of web/build/build-client.sh (default: $PLASMA_WASM_BUILD); the checks of the built client are skipped without one",
    )
    ap.add_argument(
        "--out", type=Path, help="keep the evidence (screenshots, JSON, summary.json) in a new folder under this one"
    )
    ap.add_argument(
        "--omit-configuration",
        action="store_true",
        help="negative control: the first check loads no document (expect it to fail)",
    )
    a, rest = ap.parse_known_args()
    BUILD, KEEP, OMIT = a.build_dir, a.out, a.omit_configuration
    if KEEP:
        KEEP.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[sys.argv[0], *rest], verbosity=2)
