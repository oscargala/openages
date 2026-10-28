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
"""The operator deployment file and its three consumers, without the shard.

Synthetic public DH values (64 zero bytes) only; loopback sockets only. Run it with web/tests/run.py test_deployment, or
with the web package on the import path and the gateway's packages installed (web/deploy/requirements.txt):
PYTHONPATH=web/deploy python web/tests/test_deployment.py. The page's own validator (config.mjs) runs under node; without
node those tests are skipped, and the skip names the reason.
"""

import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock
import urllib.error
import urllib.parse
import urllib.request

from plasma_web import deployment

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PACKAGE_ROOT = ROOT / "web/deploy"  # the package's directory: python -m runs from here
SHELL = "Sources/Plasma/Apps/plClient/emscripten/shell"  # the page shell: index.html, boot.mjs, config.mjs
CONFIG_MJS = ROOT / SHELL / "config.mjs"  # the page's runtime configuration module
TEMPLATE = ROOT / "web/deploy/deployments/moula.template.json"  # the MOULa deployment template
# The product's commands, run as python -m <module> (the package on the import path, as for these tests)
PRODUCT_SERVER = ["-m", "plasma_web.server"]  # the product's origin server
EMIT_STATIC = ["-m", "plasma_web.emit_static"]
DEPLOYMENT_CLI = ["-m", "plasma_web.deployment"]
GATEWAY = ["-m", "plasma_web.gateway"]
NODE = shutil.which("node")


def need_node():
    """node, for the page's own modules (config.mjs, plEmscriptenAssetCache.js); the test is skipped without it."""
    if NODE is None:
        raise unittest.SkipTest("node is not installed: the page's own validator (config.mjs) runs under node")
    return NODE


# The modules of the two builders no release carries: the HD texture-pack builder and the client game-code tool
BUILDER_MODULES = {"build_client_game_code", "build_pack", "build_all", "bc", "lambda_pack", "mark_pack"}
# An operating-system font folder, as a path names one
OS_FONTS = re.compile(r"/System/Library/Fonts|/Library/Fonts|/usr/share/fonts|Windows[\\/]+Fonts", re.I)
# The global pages no per-Age pack may list
GLOBAL_PAGES = {
    "GUI",
    "GlobalClothing",
    "GlobalAvatars",
    "GlobalMarkers",
    "CustomAvatars",
    "GlobalAnimations",
    "AvatarCustomization",
}

KEY = base64.b64encode(bytes(64)).decode()
# A player deployment as an operator writes one for a loopback page: no capabilities.harness, no fonts entry (the free
# substitutes), and gateway.join with the key file's path from the environment. serverIni and data.root are the
# fixture's (test_player_deployment).
PLAYER_DEPLOYMENT = {
    "schema": 1,
    "name": "player-fixture",
    "serverIni": "server.ini",
    "data": {"root": "data", "urlPrefix": "/data"},
    "gateway": {
        "bind": "127.0.0.1",
        "port": 0,
        "origins": [],
        "publicHosts": [],
        "extraUpstreamHosts": [],
        "resolveUpstreams": True,
        "join": {"secretFileEnv": "PLASMA_JOIN_SECRET_FILE", "ttlSeconds": 60},
    },
    "capabilities": {},
}


def ini_text(auth="127.0.0.1", gate="127.0.0.1", port=None, extra=""):
    lines = [f'Server.{s}.{k} "{KEY}"' for s in ("Auth", "Game", "Gate") for k in ("N", "X")]
    if auth is not None:
        lines.append(f'Server.Auth.Host "{auth}"')
    if gate is not None:
        lines.append(f'Server.Gate.Host "{gate}"')
    if port is not None:
        lines.append(f"Server.Port {port}")
    return "# synthetic\n" + "\n".join(lines) + "\n" + extra


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Fixture:
    def __init__(self, tmp, ini, **overrides):
        self.dir = Path(tmp)
        (self.dir / "data/dat").mkdir(parents=True, exist_ok=True)
        (self.dir / "data/sfx").mkdir(exist_ok=True)
        (self.dir / "data/dat/fixture.age").write_bytes(b"age")
        (self.dir / "data/sfx/fixture.ogg").write_bytes(bytes(range(64)))
        (self.dir / "server.ini").write_text(ini)
        doc = {
            "schema": 1,
            "name": "fixture",
            "serverIni": "server.ini",
            "data": {"root": "data"},
            "gateway": {"bind": "127.0.0.1", "port": 0, "origins": []},
            "capabilities": {"harness": True},
        }
        for key, value in overrides.items():
            doc[key] = value
        self.path = self.dir / "deployment.json"
        self.path.write_text(json.dumps(doc))


def node_config(doc, page="http://127.0.0.1:2/"):
    """The page's own validator (config.mjs, under node): the build it names, or None when it refuses the document."""
    script = (
        "import {validateConfiguration} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
        "const [doc, page] = JSON.parse(process.argv[1]);"
        "try { const c = validateConfiguration(doc, new URL('/runtime/config.json', page).href, page);"
        " console.log(JSON.stringify({build: c.build, client: c.client, capture: c.capabilities.gameCodeCapture})); }"
        "catch (e) { console.log('null'); }"
    )
    r = subprocess.run(
        [need_node(), "--input-type=module", "-e", script, json.dumps([doc, page])],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return json.loads(r.stdout)


def node_refusal(doc, page="http://127.0.0.1:2/"):
    """The page's validator (config.mjs): the refusal's code (e.code), or None when it accepts the document."""
    script = (
        "import {validateConfiguration} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
        "const [doc, page] = JSON.parse(process.argv[1]);"
        "try { validateConfiguration(doc, new URL('/runtime/config.json', page).href, page); console.log('null'); }"
        "catch (e) { console.log(JSON.stringify(e.code)); }"
    )
    r = subprocess.run(
        [need_node(), "--input-type=module", "-e", script, json.dumps([doc, page])],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return json.loads(r.stdout)


def capture_refusal():
    """deployment.py's start-up error for capabilities.gameCodeCapture outside a harness deployment."""
    return (
        r"^capabilities\.gameCodeCapture is a test harness diagnostic: it needs capabilities\.harness \(a harness "
        r"build on the test harness server\)$"
    )


def node_initial_ini(doc, width, height, page="https://play.example.test/"):
    """The graphics.ini the page installs for this document at this canvas backing (config.mjs)."""
    script = (
        "import {validateConfiguration, initialGraphicsIni} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
        "const [doc, page, w, h] = JSON.parse(process.argv[1]);"
        "const c = validateConfiguration(doc, new URL('/runtime/config.json', page).href, page);"
        "console.log(JSON.stringify(initialGraphicsIni(c.initialGraphicsIni, w, h)));"
    )
    r = subprocess.run(
        [need_node(), "--input-type=module", "-e", script, json.dumps([doc, page, width, height])],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return json.loads(r.stdout)


def node_hd_pack(doc, page="http://127.0.0.1:2/"):
    """The page's validator (config.mjs): the client's hdPack value for this document, or "refused"."""
    script = (
        "import {validateConfiguration} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
        "const [doc, page] = JSON.parse(process.argv[1]);"
        "try { const c = validateConfiguration(doc, new URL('/runtime/config.json', page).href, page);"
        " console.log(JSON.stringify(c.hdPack)); } catch (e) { console.log('\"refused\"'); }"
    )
    r = subprocess.run(
        [need_node(), "--input-type=module", "-e", script, json.dumps([doc, page])],
        capture_output=True,
        text=True,
        timeout=60,
    )
    return json.loads(r.stdout)


def check_hd_pack_list(case, ages):
    """A content set's list of Ages with a pack is a valid deployment list end to end: no global page is listed,
    deployment.py loads it with a root holding every pack, the page document carries it unchanged and config.mjs
    accepts it. case: the TestCase that asserts."""
    case.assertTrue(0 < len(ages) <= 64 and not set(ages) & GLOBAL_PAGES)
    with tempfile.TemporaryDirectory() as tmp:
        packs = Path(tmp) / "packs"
        packs.mkdir()
        for age in ages:
            (packs / f"{age}.hdpack").write_bytes(b"PLHDPK01")
        f = Fixture(Path(tmp) / "f", ini_text(), textures={"hdPack": {"root": str(packs), "ages": ages}})
        doc = deployment.load(f.path).public_document("ws://127.0.0.1:9")
        case.assertEqual(doc["textures"], {"hdPack": {"base": "/hdpack", "ages": ages}})
        case.assertEqual(node_hd_pack(doc), {"base": "http://127.0.0.1:2/hdpack", "ages": ages})


def hd_pack_ages():
    """Each content set's list of Ages with an HD texture pack that this tree ships: the MOULa template's."""
    return {"moula": json.loads(TEMPLATE.read_text())["textures"]["hdPack"]["ages"]}


def fake_build(root, build, marker=None):
    """A build directory holding <name>.js and <name>.wasm whose bytes carry one Python ABI marker."""
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    name = deployment.CLIENT_NAMES[build]
    (root / f"{name}.js").write_text(f"// {name}.js loads {name}.wasm\n")
    (root / f"{name}.wasm").write_bytes(b"\0asm" + deployment.ABI_MARKERS[marker or build] + b"\0")
    return root


class DeploymentFileTests(unittest.TestCase):
    def test_test_shard_file_derives_one_shard(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text("192.0.2.174", "192.0.2.174"))
            d = deployment.load(f.path)
            self.assertEqual((d.entry_host, d.client_port), ("192.0.2.174", 14617))
            ups, _ = d.upstreams(resolver=lambda *a: [])
            self.assertEqual(ups, {("192.0.2.174", 14617)})
            doc = d.public_document("ws://127.0.0.1:9")
            self.assertEqual(doc["shard"]["sha256"], hashlib.sha256((f.dir / "server.ini").read_bytes()).hexdigest())
            self.assertEqual(doc["shard"]["serverIni"], "/runtime/server.ini")
            self.assertEqual(doc["capabilities"], {"harness": True})
            self.assertNotIn("sound", doc["roots"])

    def test_gatekeeper_only_and_split_hosts_and_port(self):
        # NetCommConnect: no Auth.Host -> the gatekeeper is the entry; File.Host may differ.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(
                tmp,
                ini_text(None, "gate.example.test", 14618, 'Server.File.Host "files.example.test"\n'),
                gateway={
                    "origins": ["https://play.example.test"],
                    "extraUpstreamHosts": ["198.51.100.7"],
                    "resolveUpstreams": False,
                    "join": {"secretFile": str(Path(tmp) / "join.key")},
                },
                capabilities={},
            )
            d = deployment.load(f.path)
            self.assertEqual((d.entry_host, d.client_port), ("gate.example.test", 14618))
            ups, _ = d.upstreams()
            self.assertEqual(
                ups, {("gate.example.test", 14618), ("files.example.test", 14618), ("198.51.100.7", 14618)}
            )
            self.assertNotIn("capabilities", d.public_document("wss://gw.example.test/connect-root"))

    def test_public_roots_and_static_kit(self):
        # A static page: roots on another origin, gateway URL in the file; the kit holds no game tree.
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(
                tmp,
                ini_text("shard.example.test", "shard.example.test"),
                capabilities={},
                data={"root": "data", "url": "https://assets.example.test/data/"},
                fonts={"url": "https://assets.example.test/fonts"},
                sound={"url": "https://assets.example.test/sound"},
                gateway={
                    "url": "wss://gw.example.test",
                    "origins": ["https://play.example.test"],
                    "join": {"secretFile": str(Path(tmp) / "join.key")},
                },
            )
            doc = deployment.load(f.path).public_document()
            self.assertEqual(doc["join"], "/runtime/join")
            self.assertEqual(
                doc["roots"],
                {
                    "data": "https://assets.example.test/data",
                    "fonts": "https://assets.example.test/fonts",
                    "sound": "https://assets.example.test/sound",
                },
            )
            out = f.dir / "kit"
            r = subprocess.run([sys.executable, *EMIT_STATIC, str(f.path), str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(json.loads((out / "runtime/config.json").read_text()), doc)
            self.assertEqual((out / "runtime/server.ini").read_bytes(), (f.dir / "server.ini").read_bytes())
            self.assertIn("Cross-Origin-Embedder-Policy: require-corp", (out / "_headers").read_text())
            self.assertFalse((out / "data").exists())
            report = json.loads((f.dir / "kit.publish-report.json").read_text())
            self.assertEqual(report["kit_over_limit"], [])
            self.assertEqual(report["join_endpoint"], "/runtime/join")
            for bad in ({"url": "http://assets.example.test/data"}, {"url": "https://u:p@assets.example.test/"}):
                with self.subTest(bad=bad), tempfile.TemporaryDirectory() as tmp2:
                    with self.assertRaises(deployment.DeploymentError):
                        deployment.load(Fixture(tmp2, ini_text(), data={"root": "data", **bad}).path)
        with tempfile.TemporaryDirectory() as tmp:  # a harness deployment never becomes a static public page
            f = Fixture(tmp, ini_text(), gateway={"url": "ws://127.0.0.1:9"})
            r = subprocess.run(
                [sys.executable, *EMIT_STATIC, str(f.path), str(f.dir / "kit")], capture_output=True, text=True
            )
            self.assertEqual(r.returncode, 2)

    def test_static_kit_names_the_status_relay(self):
        # The kit's page document names the status relay /runtime/status on the page's own origin when the ini has a
        # Server.Status URL, as server.py's document does, so the login dialog never fetches the ini's URL (which the
        # kit's Content-Security-Policy blocks: the dialog showed "Failed to fetch"); the policy names no origin for
        # it. Without an http(s) Server.Status, the document has no status, as server.py's has none.
        status_line = 'Server.Status "http://status.example.test:8080/serverstatus"\n'
        for extra, relayed in (
            (status_line, True),
            ("", False),
            ('Server.Status "ftp://status.example.test/"\n', False),
        ):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as tmp:
                f = Fixture(
                    tmp,
                    ini_text("shard.example.test", "shard.example.test", extra=extra),
                    capabilities={},
                    gateway={
                        "url": "wss://play.example.test",
                        "origins": ["https://play.example.test"],
                        "publicHosts": ["play.example.test"],
                        "join": {"secretFile": str(Path(tmp) / "join.key")},
                    },
                )
                d = deployment.load(f.path)
                out = f.dir / "kit"
                r = subprocess.run(
                    [sys.executable, *EMIT_STATIC, str(f.path), str(out)], capture_output=True, text=True
                )
                self.assertEqual(r.returncode, 0, r.stderr)
                doc = json.loads((out / "runtime/config.json").read_text())
                # the document server.py serves for the same deployment (configure: status_relay="/runtime/status")
                self.assertEqual(doc, d.public_document(None, status_relay="/runtime/status"))
                self.assertEqual(doc["shard"].get("status"), "/runtime/status" if relayed else None)
                self.assertEqual(d.status_url, "http://status.example.test:8080/serverstatus" if relayed else "")
                # the relay is on the page's origin: the policy is the one of the document without it
                csp = deployment.content_security_policy(doc)
                self.assertEqual(csp, deployment.content_security_policy(d.public_document(None)))
                self.assertNotIn("status.example.test", csp)
                self.assertIn(f"Content-Security-Policy: {csp}\n", (out / "_headers").read_text())
                report = json.loads((f.dir / "kit.publish-report.json").read_text())
                if relayed:
                    self.assertEqual(report["status_endpoint"], "/runtime/status")
                    self.assertEqual(report["status_url"], d.status_url)
                    self.assertIn("Worker", report["status_needs"])
                else:
                    self.assertNotIn("status_endpoint", report)
                if NODE is not None:  # the page's own validator: the relay on the page's origin
                    script = (
                        "import {validateConfiguration} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
                        "const [doc, page] = JSON.parse(process.argv[1]);"
                        "const c = validateConfiguration(doc, new URL('/runtime/config.json', page).href, page);"
                        "console.log(JSON.stringify(c.shard.status));"
                    )
                    page = "https://play.example.test/"
                    v = subprocess.run(
                        [NODE, "--input-type=module", "-e", script, json.dumps([doc, page])],
                        capture_output=True,
                        text=True,
                        timeout=60,
                    )
                    self.assertEqual(
                        json.loads(v.stdout), "https://play.example.test/runtime/status" if relayed else None, v.stderr
                    )

    def test_admission_settings(self):
        # A public deployment needs join tokens; limits default and validate; the key file stays out of Git.
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(deployment.DeploymentError):
                deployment.load(
                    Fixture(tmp, ini_text(), gateway={"origins": ["https://play.example.test"]}, capabilities={}).path
                )
            d = deployment.load(Fixture(tmp, ini_text()).path)
            self.assertEqual(d.limits, deployment.LIMIT_DEFAULTS)
            self.assertIsNone(d.join_secret_file)
            self.assertIsNone(deployment.join_secret(d))
            self.assertNotIn("join", d.public_document("ws://127.0.0.1:9"))
            key = Path(tmp) / "k" / "join.key"
            d = deployment.load(
                Fixture(
                    tmp,
                    ini_text(),
                    gateway={"join": {"secretFile": str(key), "ttlSeconds": 30}, "limits": {"connectionsPerIp": 0}},
                ).path
            )
            self.assertEqual((d.join_ttl, d.limits["connectionsPerIp"], d.limits["idleSeconds"]), (30, 0, 120))
            r = subprocess.run(
                [sys.executable, *DEPLOYMENT_CLI, "--init-join-secret", str(d.path)], capture_output=True, text=True
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)
            self.assertNotIn(key.read_text().strip(), r.stdout + r.stderr)
            self.assertEqual(len(deployment.join_secret(d)), 32)
            for bad in (
                {"join": {"secretFile": str(HERE / "x.key")}},
                {"join": {"secretFile": str(key), "ttlSeconds": 5}},
                {"join": {"secretFile": str(key), "extra": 1}},
                {"limits": {"bytesPerSecond": 0}},
                {"limits": {"connectionsPerIp": -1}},
                {"limits": {"perIp": 3}},
                {"limits": {"idleSeconds": True}},
            ):
                with self.subTest(bad=bad):
                    with self.assertRaises(deployment.DeploymentError):
                        deployment.load(Fixture(tmp, ini_text(), gateway=bad).path)

    def test_join_key_path_from_the_environment(self):
        # gateway.join.secretFileEnv names the environment variable that holds the key file's absolute
        # path, so a committed deployment names no path of one machine. Exactly one of secretFile and secretFileEnv.
        var = "FIXTURE_JOIN_KEY_FILE"
        with tempfile.TemporaryDirectory() as tmp:
            key = Path(tmp) / "k" / "join.key"
            f = Fixture(
                tmp,
                ini_text(),
                gateway={
                    "bind": "127.0.0.1",
                    "port": 0,
                    "origins": [],
                    "join": {"secretFileEnv": var, "ttlSeconds": 60},
                },
            )
            with mock.patch.dict(os.environ, {var: str(key)}):
                d = deployment.load(f.path)
                self.assertEqual(d.join_secret_file, key.resolve())
                r = subprocess.run(
                    [sys.executable, *DEPLOYMENT_CLI, "--init-join-secret", str(f.path)], capture_output=True, text=True
                )
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(key.stat().st_mode & 0o777, 0o600)
                self.assertNotIn(key.read_text().strip(), r.stdout + r.stderr)
                self.assertEqual(len(deployment.join_secret(d)), 32)
                self.assertEqual(d.public_document("ws://127.0.0.1:9")["join"], "/runtime/join")
            with mock.patch.dict(os.environ, {}, clear=False):
                os.environ.pop(var, None)
                with self.assertRaisesRegex(deployment.DeploymentError, f"{var} is not set"):
                    deployment.load(f.path)
            for value in ("", "relative/join.key", str(HERE / "x.key")):
                with self.subTest(value=value), mock.patch.dict(os.environ, {var: value}):
                    with self.assertRaises(deployment.DeploymentError) as cm:
                        deployment.load(f.path)
                    if value:
                        self.assertNotIn(value, str(cm.exception))  # the path is never printed
            for bad in (
                {"secretFileEnv": "lower_case"},
                {"secretFileEnv": 3},
                {"ttlSeconds": 60},
                {"secretFile": str(key), "secretFileEnv": var},
            ):
                with self.subTest(bad=bad), mock.patch.dict(os.environ, {var: str(key)}):
                    with self.assertRaises(deployment.DeploymentError):
                        deployment.load(Fixture(tmp, ini_text(), gateway={"join": bad}).path)

    def test_player_deployment(self):
        # A player deployment (PLAYER_DEPLOYMENT: no capabilities.harness, no fonts entry, so the free substitutes, and
        # gateway.join with the key file's path from the environment) loads with this fixture's ini and data.
        doc = json.loads(json.dumps(PLAYER_DEPLOYMENT))
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text())
            doc.update(serverIni=str(f.dir / "server.ini"), data={"root": str(f.dir / "data"), "urlPrefix": "/data"})
            mine = Path(tmp) / "player.json"
            mine.write_text(json.dumps(doc))
            with mock.patch.dict(os.environ, {"PLASMA_JOIN_SECRET_FILE": str(Path(tmp) / "k/join.key")}):
                d = deployment.load(mine)
            self.assertEqual((d.harness, d.game_code_capture, d.fonts_source), (False, False, "free"))
            self.assertEqual(d.join_secret_file, (Path(tmp) / "k/join.key").resolve())

    def test_server_py2(self):
        # gameCode server-py2 = the shard's Python and SDL (as server, no local override) run by the py2 build;
        # the page document names build py2 and the page's validator returns plClient-py2.
        with tempfile.TemporaryDirectory() as tmp:
            d = deployment.load(Fixture(tmp, ini_text(), data={"files": "patcher", "gameCode": "server-py2"}).path)
            self.assertEqual((d.game_code, d.build), ("server-py2", "py2"))
            doc = d.public_document("ws://127.0.0.1:9")
            self.assertEqual((doc["files"], doc["gameCode"], doc["build"]), ("patcher", "server-py2", "py2"))
            self.assertNotIn("gameCode", doc["roots"])  # nothing local: no /gamecode tree
            self.assertEqual((d.summary()["data"]["build"], d.summary()["data"]["client"]), ("py2", "plClient-py2"))
            self.assertEqual(node_config(doc), {"build": "py2", "client": "plClient-py2", "capture": False})
            # the page refuses a py3 build for server-py2, and the py2 build for any other mode
            for bad in (
                {k: v for k, v in doc.items() if k != "build"},
                {**doc, "build": "py3"},
                {**doc, "gameCode": "server"},
                {k: v for k, v in doc.items() if k != "gameCode"},
                {k: v for k, v in doc.items() if k != "files"},
                {**doc, "roots": {**doc["roots"], "gameCode": "/gamecode"}},
                {**doc, "gameCode": "client-python", "roots": {**doc["roots"], "gameCode": "/gamecode"}},
            ):
                with self.subTest(bad=bad):
                    self.assertIsNone(node_config(bad))
        with tempfile.TemporaryDirectory() as tmp:  # the diagnostic capture stays allowed (it records the shard's code)
            d = deployment.load(
                Fixture(
                    tmp,
                    ini_text(),
                    data={"files": "patcher", "gameCode": "server-py2"},
                    capabilities={"harness": True, "gameCodeCapture": True},
                ).path
            )
            self.assertEqual(node_config(d.public_document("ws://127.0.0.1:9"))["capture"], True)
        for data in (
            {"root": "data", "gameCode": "server-py2"},  # files http
            {"files": "patcher", "gameCode": "server-py2", "gameCodeDir": "seal"},
            {"files": "patcher", "gameCode": "py2"},
        ):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(deployment.DeploymentError):
                    deployment.load(Fixture(tmp, ini_text(), data=data).path)
        for data in (
            {"root": "data"},
            {"files": "patcher", "gameCode": "server"},
            {"root": "data", "gameCode": "local"},
        ):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as tmp:  # every other mode: the py3 build
                d = deployment.load(Fixture(tmp, ini_text(), data=data).path)
                doc = d.public_document("ws://127.0.0.1:9")
                self.assertEqual((d.build, "build" in doc), ("py3", False))
                self.assertEqual(node_config(doc)["client"], "plClient")

    def test_client_game_code_is_not_the_products(self):
        # A release ships server-delivered game code only (server, server-py2). The client game-code modes are test
        # harness modes that the test-hook overlay adds (deployment.register, from the overlay's deployment rules),
        # so a product process refuses them as unknown, harness deployment or not (the harness's own tests check its
        # rules), and the page's own validator, without the test harness page's extension, refuses the document as
        # invalid.
        with mock.patch.object(deployment, "EXTENSIONS", []):
            for mode in ("client", "client-python"):
                for caps in ({}, {"harness": True}):
                    with self.subTest(mode=mode, caps=caps), tempfile.TemporaryDirectory() as tmp:
                        for data in (
                            {"root": "data", "files": "patcher", "gameCode": mode},
                            {"root": "data", "files": "patcher", "gameCode": mode, "gameCodeDir": "seal"},
                        ):
                            with self.assertRaisesRegex(
                                deployment.DeploymentError,
                                r"^data(\.gameCode must be server, server-py2 or local|: unknown "
                                r"keys \['gameCodeDir'\])$",
                            ):
                                deployment.load(Fixture(tmp, ini_text(), capabilities=caps, data=data).path)
            with tempfile.TemporaryDirectory() as tmp:
                base = deployment.load(
                    Fixture(tmp, ini_text(), data={"root": "data", "files": "patcher", "gameCode": "server"}).path
                )
                base = base.public_document("wss://gw.example.test/")
        for mode in ("client", "client-python"):
            doc = {**base, "gameCode": mode, "roots": {**base["roots"], "gameCode": "/gamecode"}}
            for caps in ({}, {"harness": True}):
                with self.subTest(page_mode=mode, caps=caps):
                    self.assertEqual(node_refusal({**doc, "capabilities": caps}), "CONFIG-INVALID")
            self.assertEqual(node_refusal({**base, "gameCode": mode}), "CONFIG-INVALID")
        self.assertIsNone(node_refusal(base))  # server: a release's mode

    def test_static_kit_refuses_client_game_code(self):
        # A static kit is a player deployment: emit_static.py refuses the client game-code modes, which the product does
        # not know (test_client_game_code_is_not_the_products), at start, and writes nothing
        for mode in ("client", "client-python"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                f = Fixture(
                    tmp,
                    ini_text("shard.example.test", "shard.example.test"),
                    capabilities={},
                    data={"root": "data", "files": "patcher", "gameCode": mode},
                    gateway={
                        "url": "wss://gw.example.test",
                        "origins": ["https://play.example.test"],
                        "join": {"secretFile": str(Path(tmp) / "join.key")},
                    },
                )
                out = Path(tmp) / "kit"
                r = subprocess.run(
                    [sys.executable, *EMIT_STATIC, str(f.path), str(out)], capture_output=True, text=True, timeout=60
                )
                self.assertEqual(r.returncode, 2, r.stderr)
                self.assertIn("invalid deployment: data.gameCode must be server, server-py2 or local", r.stderr)
                self.assertFalse(out.exists())

    def test_server_py2_static_kit(self):
        # emit_static.py ships the deployment's build only, checked by its Python ABI marker
        def kit(tmp, data, build_dir, name="kit"):
            f = Fixture(
                tmp,
                ini_text("shard.example.test", "shard.example.test"),
                capabilities={},
                data=data,
                gateway={
                    "url": "wss://gw.example.test",
                    "origins": ["https://play.example.test"],
                    "join": {"secretFile": str(Path(tmp) / "join.key")},
                },
            )
            out = Path(tmp) / name
            r = subprocess.run(
                [sys.executable, *EMIT_STATIC, str(f.path), str(out), "--build-dir", str(build_dir)],
                capture_output=True,
                text=True,
            )
            return r, out

        py2 = {"files": "patcher", "gameCode": "server-py2"}
        with tempfile.TemporaryDirectory() as tmp:
            b = fake_build(Path(tmp) / "b", "py2")
            fake_build(b, "py3")  # a directory holding both builds
            r, out = kit(tmp, py2, b)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(
                sorted(p.name for p in (out / "build").iterdir()), ["plClient-py2.js", "plClient-py2.wasm"]
            )
            self.assertEqual((out / "build/plClient-py2.wasm").read_bytes(), (b / "plClient-py2.wasm").read_bytes())
            self.assertEqual(json.loads((out / "runtime/config.json").read_text())["build"], "py2")
            self.assertFalse((out / "gamecode").exists())
            r, out = kit(tmp, {"files": "patcher", "gameCode": "server"}, b, "kit3")  # py3 mode: plClient only
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(sorted(p.name for p in (out / "build").iterdir()), ["plClient.js", "plClient.wasm"])
        with tempfile.TemporaryDirectory() as tmp:  # plClient-py2.wasm that is a py3 build; a missing build
            r, _ = kit(tmp, py2, fake_build(Path(tmp) / "b", "py2", marker="py3"))
            self.assertEqual(r.returncode, 2)
            self.assertIn("is not a py2 build (Python ABI markers found: ['py3'])", r.stderr)
            r, _ = kit(tmp, py2, fake_build(Path(tmp) / "b3", "py3"), "kit2")
            self.assertEqual(r.returncode, 2)
            self.assertIn("plClient-py2.js and plClient-py2.wasm are not in", r.stderr)

    def test_static_kit_refuses_a_harness_build(self):
        # emit_static.py packs product builds only: a client built with PLASMA_TEST_HOOKS=ON is refused by its mark
        # or by the CMakeCache.txt of the build tree it sits in
        from plasma_web import emit_static

        def kit(tmp, build_dir, name):
            f = Fixture(
                tmp,
                ini_text("shard.example.test", "shard.example.test"),
                capabilities={},
                data={"files": "patcher", "gameCode": "server"},
                gateway={
                    "url": "wss://gw.example.test",
                    "origins": ["https://play.example.test"],
                    "join": {"secretFile": str(Path(tmp) / "join.key")},
                },
            )
            return subprocess.run(
                [sys.executable, *EMIT_STATIC, str(f.path), str(Path(tmp) / name), "--build-dir", str(build_dir)],
                capture_output=True,
                text=True,
            )

        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "tree"
            app = fake_build(tree / "Sources/Plasma/Apps/plClient", "py3")
            r = kit(tmp, app, "kit-no-cache")  # no cache: the product build
            self.assertEqual(r.returncode, 0, r.stderr)
            (tree / "CMakeCache.txt").write_text("PLASMA_PIPELINE_WGPU:BOOL=ON\nPLASMA_TEST_HOOKS:BOOL=OFF\n")
            r = kit(tmp, app, "kit-off")
            self.assertEqual(r.returncode, 0, r.stderr)
            for value in ("ON", "TRUE", "1"):
                (tree / "CMakeCache.txt").write_text(f"PLASMA_TEST_HOOKS:BOOL={value}\n")
                r = kit(tmp, app, f"kit-{value}")
                self.assertEqual(r.returncode, 2)
                self.assertIn(f"is configured with PLASMA_TEST_HOOKS={value}: a harness build", r.stderr)
                self.assertFalse((Path(tmp) / f"kit-{value}").exists())
            (tree / "CMakeCache.txt").unlink()
            wasm = app / "plClient.wasm"
            wasm.write_bytes(wasm.read_bytes() + emit_static.TEST_HOOKS_MARK + b": never deployed\0")
            r = kit(tmp, app, "kit-mark")  # the mark alone, copied files
            self.assertEqual(r.returncode, 2)
            self.assertIn("is a harness build (PLASMA_TEST_HOOKS=ON)", r.stderr)

    def test_initial_graphics_ini(self):
        # graphics.initialIni, checked here and by the page (config.mjs), reaches the page document and
        # the static kit unchanged; the page composes the file in the client's default-writer format.
        good = {
            "Graphics.Windowed": True,
            "Graphics.AntiAliasAmount": 6,
            "Graphics.AnisotropicLevel": 16,
            "Graphics.TextureQuality": 2,
            "Quality.Level": 3,
            "Graphics.Shadow.Enable": True,
            "Graphics.EnablePlanarReflections": True,
            "Graphics.EnableVSync": False,
            "Graphics.Shadow.VisibleDistance": 1.0,
        }
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(
                tmp,
                ini_text("shard.example.test", "shard.example.test"),
                capabilities={},
                data={"files": "patcher", "gameCode": "server-py2"},
                graphics={"initialIni": good},
                gateway={
                    "url": "wss://gw.example.test",
                    "origins": ["https://play.example.test"],
                    "join": {"secretFile": str(Path(tmp) / "join.key")},
                },
            )
            d = deployment.load(f.path)
            doc = d.public_document()
            self.assertEqual(doc["graphics"], {"initialIni": good})
            self.assertEqual(d.summary()["graphics"], {"initialIni": good})
            out = f.dir / "kit"
            r = subprocess.run([sys.executable, *EMIT_STATIC, str(f.path), str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(json.loads((out / "runtime/config.json").read_text())["graphics"], {"initialIni": good})
            self.assertEqual(
                node_initial_ini(doc, 2880, 1620),
                "Graphics.Width 2880\r\nGraphics.Height 1620\r\nGraphics.ColorDepth 32\r\n"
                "Graphics.Windowed true\r\nGraphics.AntiAliasAmount 6\r\nGraphics.AnisotropicLevel 16\r\n"
                "Graphics.TextureQuality 2\r\nQuality.Level 3\r\nGraphics.Shadow.Enable 1\r\n"
                "Graphics.EnablePlanarReflections 1\r\nGraphics.EnableVSync false\r\n"
                "Graphics.Shadow.VisibleDistance 1\r\n",
            )
            optional = {k: v for k, v in good.items() if k != "Graphics.Shadow.VisibleDistance"}
            f2 = Fixture(Path(tmp) / "f2", ini_text(), graphics={"initialIni": optional})
            self.assertEqual(
                deployment.load(f2.path).public_document("ws://127.0.0.1:9")["graphics"]["initialIni"], optional
            )
            self.assertNotIn(
                "graphics",
                deployment.load(Fixture(Path(tmp) / "f3", ini_text()).path).public_document("ws://127.0.0.1:9"),
            )
        bad = [
            {"Graphics.Windowed": 1},
            {"Graphics.AntiAliasAmount": 17},
            {"Graphics.AnisotropicLevel": -1},
            {"Graphics.TextureQuality": 3},
            {"Quality.Level": 2.5},
            {"Graphics.Shadow.Enable": "true"},
            {"Graphics.Shadow.VisibleDistance": 1.5},
            {"Graphics.Width": 800},
            {"Account.Username": "x"},
        ]
        page_doc = {
            "schema": 1,
            "roots": {"fonts": "/fonts"},
            "gateway": "ws://127.0.0.1:9",
            "files": "patcher",
            "shard": {"host": "127.0.0.1", "port": 14617, "serverIni": "/runtime/server.ini", "sha256": "0" * 64},
        }
        self.assertIsNotNone(node_config(dict(page_doc, graphics={"initialIni": good})))
        for change in bad + [None]:
            value = dict(good, **change) if change else {k: v for k, v in good.items() if k != "Quality.Level"}
            with self.subTest(change=change), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(deployment.DeploymentError):
                    deployment.load(Fixture(tmp, ini_text(), graphics={"initialIni": value}).path)
                self.assertIsNone(node_config(dict(page_doc, graphics={"initialIni": value})))
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(deployment.DeploymentError):
                deployment.load(Fixture(tmp, ini_text(), graphics={"initialIni": good, "other": 1}).path)

    def test_asset_origin_pins(self):
        # One asset-origin pin limit, config.mjs ASSET_ORIGIN_PINS (one per root), in the one line the client build reads (plClient CMakeLists.txt: asset-origin-pins.js
        # defines PlasmaAssetOriginPins for plEmscriptenAssetCache.js's pin rule). Under node, a document whose five roots name
        # five origins yields five pins and the cache's rule, given the build's value, admits them; a sixth origin or a
        # duplicate rejects the whole list, and without the build's value no pin is admitted. A product document has at
        # most four roots (data, fonts, sound, the HD packs); the fifth is the one a test-hook extension adds
        # (validateConfiguration's extension: the test harness page's roots.gameCode), here a fixture of its own.
        text = CONFIG_MJS.read_text()
        self.assertEqual(re.findall(r"^export const ASSET_ORIGIN_PINS = ([0-9]+);$", text, re.M), ["5"])
        cache_js = ROOT / "Sources/Plasma/Apps/plClient/emscripten/plEmscriptenAssetCache.js"
        script = (
            "import * as m from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
            "import {readFileSync} from 'node:fs'; import vm from 'node:vm';"
            "const [doc, page, cacheJs] = JSON.parse(process.argv[1]);"
            "let lib; globalThis.addToLibrary = v => lib = v; globalThis.self = {location: {origin: page}};"
            "vm.runInThisContext(readFileSync(cacheJs, 'utf8'));"
            "const A = lib.$PlasmaAssetCache, set = v => v === null ? null : [...v];"
            "const unset = set(A.pinnedOrigins(['https://d.example.test'], page));"
            "vm.runInThisContext('var PlasmaAssetOriginPins = ' + m.ASSET_ORIGIN_PINS + ';');"
            "const extension = {gameCodes: ['fixture'], roots: ['extra'], validate: (raw, r) =>"
            " ({roots: {extra: r.endpoint(raw.roots.extra, r.base)}})};"
            "const c = m.validateConfiguration(doc, new URL('/runtime/config.json', page).href, page, undefined,"
            " extension);"
            "const pins = m.assetOrigins(c);"
            "console.log(JSON.stringify({limit: m.ASSET_ORIGIN_PINS, pins, unset,"
            " admitted: set(A.pinnedOrigins(pins, page)),"
            " six: set(A.pinnedOrigins([...pins, 'https://x.example.test'], page)),"
            " dup: set(A.pinnedOrigins([pins[0], pins[0]], page))}));"
        )
        doc = {
            "schema": 1,
            "files": "patcher",
            "gameCode": "fixture",
            "roots": {
                "data": "https://d.example.test/data",
                "fonts": "https://f.example.test/fonts",
                "sound": "https://s.example.test/sfx",
                "extra": "https://g.example.test/code",
            },
            "textures": {"hdPack": {"base": "https://p.example.test/hd", "ages": ["Personal"]}},
            "gateway": "wss://gw.example.test/ws",
            "capabilities": {"harness": True},
            "shard": {"host": "127.0.0.1", "port": 14617, "serverIni": "/runtime/server.ini", "sha256": "0" * 64},
        }
        r = subprocess.run(
            [need_node(), "--input-type=module", "-e", script, json.dumps([doc, "http://127.0.0.1:2/", str(cache_js)])],
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        five = [f"https://{h}.example.test" for h in "dfsgp"]
        self.assertEqual(
            json.loads(r.stdout), {"limit": 5, "pins": five, "unset": None, "admitted": five, "six": None, "dup": None}
        )

    def test_hd_texture_pack(self):
        # textures.hdPack (deployment.py, config.mjs): a root outside the repository holding every
        # listed Age's pack reaches the page as base /hdpack; a url reaches it as is; config.mjs hands the client the
        # base and the Ages and adds the base's origin to the asset-cache origins.
        page_env = node_hd_pack
        with tempfile.TemporaryDirectory() as tmp:
            packs = Path(tmp) / "packs"
            packs.mkdir()
            (packs / "Personal.hdpack").write_bytes(b"PLHDPK01")
            f = Fixture(Path(tmp) / "f", ini_text(), textures={"hdPack": {"root": str(packs), "ages": ["Personal"]}})
            d = deployment.load(f.path)
            self.assertEqual(d.hdpack_root, packs.resolve())
            doc = d.public_document("ws://127.0.0.1:9")
            self.assertEqual(doc["textures"], {"hdPack": {"base": "/hdpack", "ages": ["Personal"]}})
            self.assertEqual(d.summary()["textures"]["hdPack"]["ages"], ["Personal"])
            self.assertEqual(page_env(doc), {"base": "http://127.0.0.1:2/hdpack", "ages": ["Personal"]})
            f2 = Fixture(
                Path(tmp) / "f2",
                ini_text(),
                textures={"hdPack": {"url": "https://packs.example.test/hd", "ages": ["Personal", "Cleft"]}},
            )
            d2 = deployment.load(f2.path)
            self.assertIsNone(d2.hdpack_root)
            self.assertEqual(
                d2.public_document("ws://127.0.0.1:9")["textures"]["hdPack"]["base"], "https://packs.example.test/hd"
            )
            self.assertNotIn(
                "textures",
                deployment.load(Fixture(Path(tmp) / "f3", ini_text()).path).public_document("ws://127.0.0.1:9"),
            )
            bad = [
                {"ages": ["Personal"]},
                {"root": str(packs), "url": "https://x.test", "ages": ["Personal"]},
                {"root": str(packs), "ages": []},
                {"root": str(packs), "ages": ["Personal", "personal"]},
                {"root": str(packs), "ages": ["../x"]},
                {"root": str(packs), "ages": ["Cleft"]},
                {"root": str(HERE), "ages": ["Personal"]},
                {"url": "http://packs.example.test", "ages": ["Personal"]},
                {"root": str(packs), "ages": ["Personal"], "other": 1},
            ]
            for n, value in enumerate(bad):
                with self.subTest(value=value):
                    with self.assertRaises(deployment.DeploymentError):
                        deployment.load(Fixture(Path(tmp) / f"b{n}", ini_text(), textures={"hdPack": value}).path)
            with self.assertRaises(deployment.DeploymentError):
                deployment.load(Fixture(Path(tmp) / "b-top", ini_text(), textures={"other": {}}).path)
        page_doc = {
            "schema": 1,
            "roots": {"fonts": "/fonts"},
            "gateway": "ws://127.0.0.1:9",
            "files": "patcher",
            "shard": {"host": "127.0.0.1", "port": 14617, "serverIni": "/runtime/server.ini", "sha256": "0" * 64},
        }
        self.assertIsNone(page_env(page_doc))
        # A content set's list of Ages with a pack (the MOULa template's) is a valid deployment list end to end:
        # deployment.py loads it with a root holding every pack, the page document carries it unchanged and config.mjs
        # accepts it; no global page is listed.
        for name, ages in hd_pack_ages().items():
            with self.subTest(content_set=name):
                check_hd_pack_list(self, ages)
        for value in (
            {"base": "/hdpack"},
            {"base": "/hdpack", "ages": []},
            {"base": "/hdpack", "ages": ["a/b"]},
            {"base": "/hdpack", "ages": ["A", "a"]},
            {"base": "ftp://x.test/", "ages": ["A"]},
            {"base": "/hdpack", "ages": ["A"], "x": 1},
        ):
            with self.subTest(page=value):
                self.assertEqual(page_env(dict(page_doc, textures={"hdPack": value})), "refused")

    def test_moula_template(self):
        # The MOULa template (no secrets, paths only) is a server-py2 patcher deployment; with fixture paths in
        # place of the operator's ~/.config files it loads and derives a py2 page document.
        t = json.loads(TEMPLATE.read_text())
        self.assertEqual(t["data"], {"files": "patcher", "gameCode": "server-py2"})
        # The template lists the Ages with a pack in MOULa's content set, served from the operator's pack root.
        moula_ages = hd_pack_ages()["moula"]
        self.assertEqual(
            t["textures"]["hdPack"], {"root": "~/.config/plasma-web/shards/moula/hdpack", "ages": moula_ages}
        )
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "server.ini").write_text(ini_text("moula.example.test", "moula.example.test"))
            t["serverIni"] = str(Path(tmp) / "server.ini")
            t["gateway"]["join"]["secretFile"] = str(Path(tmp) / "join.key")
            packs = Path(tmp) / "hdpack"
            packs.mkdir()
            for age in t["textures"]["hdPack"]["ages"]:
                (packs / f"{age}.hdpack").write_bytes(b"PLHDPK01")
            t["textures"]["hdPack"]["root"] = str(packs)
            (Path(tmp) / "moula.json").write_text(json.dumps(t))
            d = deployment.load(Path(tmp) / "moula.json")
            self.assertEqual((d.name, d.build, d.files, d.harness), ("moula", "py2", "patcher", False))
            doc = d.public_document("wss://gw.example.test")
            self.assertEqual(node_config(doc, "https://play.example.test/")["client"], "plClient-py2")
            self.assertEqual(doc["textures"], {"hdPack": {"base": "/hdpack", "ages": moula_ages}})
            # the template ships its first-run graphics.ini (windowed, anisotropy 16, top texture and quality levels,
            # shadows at full distance, planar reflections, no vsync, 4x MSAA: AntiAliasAmount 4)
            self.assertEqual(doc["graphics"]["initialIni"], t["graphics"]["initialIni"])
            self.assertEqual(
                node_initial_ini(doc, 2880, 1620),
                "".join(
                    f"{line}\r\n"
                    for line in (
                        "Graphics.Width 2880",
                        "Graphics.Height 1620",
                        "Graphics.ColorDepth 32",
                        "Graphics.Windowed true",
                        "Graphics.AntiAliasAmount 4",
                        "Graphics.AnisotropicLevel 16",
                        "Graphics.TextureQuality 2",
                        "Quality.Level 3",
                        "Graphics.Shadow.Enable 1",
                        "Graphics.EnablePlanarReflections 1",
                        "Graphics.EnableVSync false",
                        "Graphics.Shadow.VisibleDistance 1",
                    )
                ),
            )

    def test_fonts_defaults_are_the_free_substitutes(self):
        # No operating-system font path in a product default. The fonts tree's default map (deployment.py, and
        # server.py's FONT_FILES without a deployment) is the free pair kept in web/deploy/fonts; the MOULa template names
        # no fonts, so it gets that pair too.
        free = {
            "Courier New.ttf": ROOT / "web/deploy/fonts/LiberationMono-Regular.ttf",
            "Trebuchet MS Bold.ttf": ROOT / "web/deploy/fonts/DejaVuSans-Bold.ttf",
        }
        self.assertEqual(deployment.free_font_files(), free)
        for path in free.values():
            self.assertTrue(path.is_file(), path)
        from plasma_web import server as srv

        self.assertEqual({n: Path(p) for n, p in srv.FONT_FILES.items()}, free)
        # The product's server.py, deployment.py, emit_static.py, config.mjs and the template name no
        # operating-system font path (the test harness's host copies are its own: fonts.hostCopies).
        os_fonts = OS_FONTS
        for rel in (
            "web/deploy/plasma_web/server.py",
            "web/deploy/plasma_web/deployment.py",
            "web/deploy/plasma_web/emit_static.py",
            SHELL + "/config.mjs",
            "web/deploy/deployments/moula.template.json",
        ):
            self.assertEqual(os_fonts.findall((ROOT / rel).read_text()), [], rel)
        template = json.loads(TEMPLATE.read_text())
        self.assertNotIn("fonts", template)
        with tempfile.TemporaryDirectory() as tmp:
            for i, fonts in enumerate((None, {"root": None}, {"url": "https://fonts.example.test/f"})):
                with self.subTest(fonts=fonts):
                    d = deployment.load(
                        Fixture(Path(tmp) / f"d{i}", ini_text(), **({"fonts": fonts} if fonts is not None else {})).path
                    )
                    self.assertEqual((d.fonts_source, d.font_files), ("free", free))

    def test_fonts_map(self):
        # An operator's fonts.root, named as the faces or through fonts.map (face -> file under the root)
        with tempfile.TemporaryDirectory() as tmp:
            own = Path(tmp) / "own"
            own.mkdir()
            for name in ("cour.ttf", "trebucbd.ttf", "Courier New.ttf", "Trebuchet MS Bold.ttf"):
                (own / name).write_bytes(name.encode())
            cases = [
                (
                    {"root": str(own)},
                    {"Courier New.ttf": "Courier New.ttf", "Trebuchet MS Bold.ttf": "Trebuchet MS Bold.ttf"},
                ),
                (
                    {"root": str(own), "map": {"Courier New": "cour.ttf", "Trebuchet MS Bold": "trebucbd.ttf"}},
                    {"Courier New.ttf": "cour.ttf", "Trebuchet MS Bold.ttf": "trebucbd.ttf"},
                ),
                (
                    {"root": str(own), "map": {"Courier New": "cour.ttf"}},
                    {"Courier New.ttf": "cour.ttf", "Trebuchet MS Bold.ttf": "Trebuchet MS Bold.ttf"},
                ),
            ]
            for i, (fonts, expected) in enumerate(cases):
                with self.subTest(fonts=fonts):
                    d = deployment.load(Fixture(Path(tmp) / f"ok{i}", ini_text(), fonts=fonts).path)
                    self.assertEqual(d.fonts_source, "root")
                    self.assertEqual({n: p.name for n, p in d.font_files.items()}, expected)
                    self.assertEqual(d.summary()["fonts"]["source"], "root")
            bad = [
                {"root": str(own), "map": {"Courier": "cour.ttf"}},  # not one of the client's faces
                {"root": str(own), "map": {"Courier New": "../cour.ttf"}},  # not a file under the root
                {"root": str(own), "map": {"Courier New": "sub/cour.ttf"}},
                {"root": str(own), "map": {"Courier New": 7}},
                {"root": str(own), "map": {"Courier New": "missing.ttf"}},  # no such file
                {"root": str(Path(tmp) / "nowhere")},
                {"map": {"Courier New": "cour.ttf"}},  # a map names files under a root
                {"url": "https://fonts.example.test", "map": {"Courier New": "cour.ttf"}},
                {"url": "http://fonts.example.test"},
                {"files": {}},
            ]  # unknown key
            for i, fonts in enumerate(bad):
                with self.subTest(fonts=fonts), self.assertRaises(deployment.DeploymentError):
                    deployment.load(Fixture(Path(tmp) / f"bad{i}", ini_text(), fonts=fonts).path)

    def test_fonts_host_copies_are_harness_only(self):
        # fonts.hostCopies (this host's own copies of the original faces) is an explicit harness setting: a
        # deployment without capabilities.harness, or with another font tree beside it, is refused.
        with tempfile.TemporaryDirectory() as tmp:
            d = deployment.load(Fixture(Path(tmp) / "h", ini_text(), fonts={"hostCopies": True}).path)
            self.assertEqual((d.fonts_source, d.font_files, d.harness), ("host", None, True))
            join = {"join": {"secretFile": str(Path(tmp) / "join.key")}}
            for i, (fonts, caps, gw) in enumerate(
                [
                    ({"hostCopies": True}, {}, join),  # a player deployment
                    ({"hostCopies": True}, {"harness": False}, join),
                    ({"hostCopies": True, "root": str(Path(tmp))}, {"harness": True}, {}),
                    ({"hostCopies": True, "url": "https://fonts.example.test"}, {"harness": True}, {}),
                    ({"hostCopies": "yes"}, {"harness": True}, {}),
                ]
            ):
                with (
                    self.subTest(fonts=fonts, caps=caps),
                    self.assertRaisesRegex(deployment.DeploymentError, "hostCopies"),
                ):
                    deployment.load(
                        Fixture(
                            Path(tmp) / f"b{i}",
                            ini_text(),
                            fonts=fonts,
                            capabilities=caps,
                            gateway={"bind": "127.0.0.1", **gw},
                        ).path
                    )
            with self.assertRaises(deployment.DeploymentError):  # harness is loopback-only, hence so are host copies
                deployment.load(
                    Fixture(
                        Path(tmp) / "lan", ini_text(), fonts={"hostCopies": True}, gateway={"bind": "192.0.2.1"}
                    ).path
                )

    def test_static_kit_fonts(self):
        # A static kit serves its fonts from its own origin (roots.fonts /fonts): it carries the tree, each face
        # as <face>.ttf with its .fetch-manifest, the licence files, and THIRD_PARTY_NOTICES.md naming both fonts and
        # their licences. With fonts.url it carries none; with an operator's root and map, the operator's files.
        def kit(tmp, name, **fields):
            f = Fixture(
                Path(tmp) / name,
                ini_text("shard.example.test", "shard.example.test"),
                capabilities={},
                data={"files": "patcher", "gameCode": "server"},
                gateway={
                    "url": "wss://gw.example.test",
                    "origins": ["https://play.example.test"],
                    "join": {"secretFile": str(Path(tmp) / name / "join.key")},
                },
                **fields,
            )
            out = f.dir / "kit"
            r = subprocess.run([sys.executable, *EMIT_STATIC, str(f.path), str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            return out, json.loads((f.dir / "kit.publish-report.json").read_text())

        free = deployment.free_font_files()
        with tempfile.TemporaryDirectory() as tmp:
            out, report = kit(tmp, "free")
            self.assertEqual(json.loads((out / "runtime/config.json").read_text())["roots"]["fonts"], "/fonts")
            for served, src in free.items():
                self.assertEqual((out / "fonts" / served).read_bytes(), src.read_bytes(), served)
            manifest = (out / "fonts/.fetch-manifest").read_text().splitlines()
            self.assertEqual([line.split(" ", 2)[2] for line in manifest], ["Courier New.ttf", "Trebuchet MS Bold.ttf"])
            self.assertTrue(all(re.fullmatch(r"F \d+ .+", line) for line in manifest), manifest)
            for name in ("LICENSE-LiberationFonts.txt", "LICENSE-DejaVuFonts.txt"):
                self.assertEqual((out / "fonts" / name).read_bytes(), (ROOT / "web/deploy/fonts" / name).read_bytes())
            self.assertIn("SIL OPEN FONT LICENSE Version 1.1", (out / "fonts/LICENSE-LiberationFonts.txt").read_text())
            self.assertIn("Bitstream Vera Fonts Copyright", (out / "fonts/LICENSE-DejaVuFonts.txt").read_text())
            notices = (out / "THIRD_PARTY_NOTICES.md").read_text()
            for text in (
                "Liberation Mono Regular",
                "Liberation Fonts 2.1.5",
                "SIL Open Font License, Version 1.1",
                "fonts/LICENSE-LiberationFonts.txt",
                "DejaVu Sans Bold",
                "DejaVu Fonts 2.37",
                "Bitstream Vera Fonts licence",
                "fonts/LICENSE-DejaVuFonts.txt",
                "fonts/Courier New.ttf",
                "fonts/Trebuchet MS Bold.ttf",
            ):
                self.assertIn(text, notices)
            self.assertEqual((report["fonts"]["in_kit"], report["fonts"]["source"]), (True, "free"))
            self.assertEqual(
                {n: v["sha256"] for n, v in report["fonts"]["files"].items()},
                {n: hashlib.sha256(p.read_bytes()).hexdigest() for n, p in free.items()},
            )
            self.assertNotIn("fonts", report["roots_served_next_to_the_page"])
            self.assertEqual(report["kit_over_limit"], [])

            out, report = kit(tmp, "url", fonts={"url": "https://fonts.example.test/f"})
            self.assertFalse((out / "fonts").exists())
            self.assertIn("https://fonts.example.test/f", (out / "THIRD_PARTY_NOTICES.md").read_text())
            self.assertEqual(
                report["fonts"], {"in_kit": False, "url": "https://fonts.example.test/f", "source": "free"}
            )

            own = Path(tmp) / "own"
            own.mkdir()
            (own / "cour.ttf").write_bytes(b"operator courier")
            (own / "Trebuchet MS Bold.ttf").write_bytes(b"operator trebuchet")
            out, report = kit(tmp, "own", fonts={"root": str(own), "map": {"Courier New": "cour.ttf"}})
            self.assertEqual((out / "fonts/Courier New.ttf").read_bytes(), b"operator courier")
            self.assertEqual((out / "fonts/Trebuchet MS Bold.ttf").read_bytes(), b"operator trebuchet")
            self.assertFalse((out / "fonts/LICENSE-LiberationFonts.txt").exists())
            self.assertIn("the operator's file `cour.ttf`", (out / "THIRD_PARTY_NOTICES.md").read_text())
            self.assertEqual(report["fonts"]["source"], "root")

    def test_resolved_addresses_join_the_allowlist(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text("auth.example.test", "auth.example.test"))
            ups, resolved = deployment.load(f.path).upstreams(
                resolver=lambda h, p, *a: [(0, 0, 0, "", ("203.0.113.3", p))]
            )
            self.assertEqual(ups, {("auth.example.test", 14617), ("203.0.113.3", 14617)})
            self.assertEqual(resolved, {"auth.example.test": ["203.0.113.3"]})

    def test_rejections(self):
        cases = [
            (ini_text() + "Account.Username fixture\n", {}),  # not a pfServerIni setting
            (ini_text() + 'Server.Auth.Host "127.0.0.2"\n', {}),  # duplicate
            (ini_text(None, None), {}),  # no entry host
            (ini_text().replace(f'Server.Auth.X "{KEY}"', 'Server.Auth.X "AAAA"'), {}),  # not a 64-byte key
            (ini_text(), {"username": "x"}),  # unknown deployment key
            (ini_text(), {"gateway": {"origins": ["http://play.example.test"]}}),  # plain HTTP off loopback
            (ini_text(), {"gateway": {"bind": "0.0.0.0"}}),  # harness off loopback
            (ini_text(), {"gateway": {"url": "wss://gw.example.test/?token=x"}}),
            (ini_text(), {"schema": 2}),
            (ini_text(), {"gateway": {"extraUpstreamNetworks": ["10.0.0.0/8"]}}),  # too wide: an open relay
        ]
        for ini, overrides in cases:
            with self.subTest(overrides=overrides, ini=ini[-40:]), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(deployment.DeploymentError):
                    deployment.load(Fixture(tmp, ini, **overrides).path)
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text())
            (f.dir / "server.ini").write_bytes(b"whatdoyousee\x01\x02\xff")  # the encrypted form
            with self.assertRaises(deployment.DeploymentError):
                deployment.load(f.path)


# The files of the repository that emit_static.py reads, copied into a checkout of their own so a test
# can commit it, change it and tag it (the kit's provenance follows the checkout emit_static.py runs from).
KIT_SOURCES = [
    "web/deploy/plasma_web/__init__.py",
    "web/deploy/plasma_web/emit_static.py",
    "web/deploy/plasma_web/deployment.py",
    "web/deploy/plasma_web/provenance.py",
    "web/deploy/plasma_web/server_ini.py",
    "web/deploy/plasma_web/origins.py",
    "web/deploy/plasma_web/manifest.py",
    SHELL + "/config.mjs",
    SHELL + "/index.html",
    SHELL + "/boot.mjs",
    "Sources/Plasma/Apps/plClient/emscripten/input/capture.mjs",
    "Sources/Plasma/Apps/plClient/emscripten/input/transport.mjs",
    "Sources/Plasma/Apps/plClient/emscripten/input/fullscreen.mjs",
    "LICENSE.txt",
    "COPYING.txt",
    "THIRD_PARTY_NOTICES.md",
    "web/build/sources.lock",
    ".gitignore",
]


def git(repo, *args):
    return subprocess.run(
        [
            "git",
            "-C",
            str(repo),
            "-c",
            "user.name=Kit Test",
            "-c",
            "user.email=kit@example.test",
            "-c",
            "commit.gpgsign=false",
            "-c",
            "tag.gpgsign=false",
            *args,
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def release_record(build_dir, build, commit, tag=None, lock=ROOT / "web/build/sources.lock"):
    """The record build-client.sh RELEASE=1 writes beside a release build (<name>.release.json)."""
    name = deployment.CLIENT_NAMES[build]
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()  # noqa: E731
    rec = {
        "schema": 1,
        "commit": commit,
        "tag": tag,
        "productTag": tag or commit[:12],
        "sourcesLock": {"path": "web/build/sources.lock", "sha256": sha(lock)},
        "client": name,
        "pythonAbi": "2" if build == "py2" else "3",
        "files": {
            f: {"bytes": (Path(build_dir) / f).stat().st_size, "sha256": sha(Path(build_dir) / f)}
            for f in (name + ".js", name + ".wasm")
        },
    }
    (Path(build_dir) / f"{name}.release.json").write_text(json.dumps(rec, indent=1) + "\n")
    return rec


class ProvenanceTests(unittest.TestCase):
    """Every kit carries LICENSE.txt, COPYING.txt, the repository's THIRD_PARTY_NOTICES.md (with the
    kit's fonts after it) and source.json, which names the exact source of a release build and nothing it cannot
    check."""

    def player(self, tmp, name="player", **fields):
        return Fixture(
            Path(tmp) / name,
            ini_text("shard.example.test", "shard.example.test"),
            capabilities={},
            data={"files": "patcher", "gameCode": "server"},
            gateway={
                "url": "wss://gw.example.test",
                "origins": ["https://play.example.test"],
                "join": {"secretFile": str(Path(tmp) / name / "join.key")},
            },
            **fields,
        )

    def emit(self, f, out, *extra, package=PACKAGE_ROOT):
        # emit_static of the checkout whose package directory is package, isolated from the environment (-E -s; -I
        # would also leave out the working directory, from which python -m finds the package). -E also drops
        # PYTHONDONTWRITEBYTECODE, so -B keeps the package's bytecode out of the tree.
        return subprocess.run(
            [sys.executable, "-B", "-E", "-s", "-m", "plasma_web.emit_static", str(f.path), str(out), *extra],
            capture_output=True,
            text=True,
            timeout=120,
            cwd=package,
        )

    def test_kit_carries_the_licence_notices_and_source(self):
        lock_sha = hashlib.sha256((ROOT / "web/build/sources.lock").read_bytes()).hexdigest()
        notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text()
        with tempfile.TemporaryDirectory() as tmp:
            # no repository named, no build: the files are there and source.json says what it cannot name
            f = self.player(tmp)
            out = f.dir / "kit"
            r = self.emit(f, out)
            self.assertEqual(r.returncode, 0, r.stderr)
            for name in ("LICENSE.txt", "COPYING.txt"):
                self.assertEqual((out / name).read_bytes(), (ROOT / name).read_bytes(), name)
            self.assertIn("GNU GENERAL PUBLIC LICENSE", (out / "LICENSE.txt").read_text())
            self.assertIn("Version 3, 29 June 2007", (out / "LICENSE.txt").read_text())
            self.assertIn("Additional permissions under GNU GPL version 3 section 7", (out / "COPYING.txt").read_text())
            kit_notices = (out / "THIRD_PARTY_NOTICES.md").read_text()
            # the repository's file unchanged (every component and its licence text), then this kit's fonts
            self.assertTrue(
                kit_notices.startswith(notices.rstrip("\n") + "\n"), "the kit's notices start with the file"
            )
            tail = kit_notices[len(notices.rstrip("\n")) :]
            for want in (
                "## This kit",
                "fonts/Courier New.ttf",
                "Liberation Mono Regular",
                "fonts/Trebuchet MS Bold.ttf",
                "DejaVu Sans Bold",
                "fonts/LICENSE-LiberationFonts.txt",
                "fonts/LICENSE-DejaVuFonts.txt",
            ):
                self.assertIn(want, tail)
            for section in ("## Browser client", "### OpenSSL", "### CPython 2.3.3", "### Emscripten", "## Page kit"):
                self.assertIn(section, kit_notices)
            src = json.loads((out / "source.json").read_text())
            self.assertEqual(
                set(src),
                {
                    "schema",
                    "licence",
                    "repository",
                    "tag",
                    "commit",
                    "url",
                    "sourcesLock",
                    "build",
                    "thirdPartySources",
                    "notes",
                },
            )
            self.assertEqual(
                (src["schema"], src["repository"], src["tag"], src["commit"], src["url"], src["build"]),
                (1, None, None, None, None, None),
            )
            self.assertIn("GPL-3.0-or-later", src["licence"])
            self.assertEqual(src["sourcesLock"], {"path": "web/build/sources.lock", "sha256": lock_sha})
            self.assertTrue(any("names no repository" in n for n in src["notes"]), src["notes"])
            self.assertTrue(any("carries no client build" in n for n in src["notes"]), src["notes"])
            headers = (out / "_headers").read_text()
            for name in ("/source.json", "/LICENSE.txt", "/COPYING.txt", "/THIRD_PARTY_NOTICES.md"):
                self.assertIn(name + "\n  Cache-Control: no-cache\n", headers)
            report = json.loads((f.dir / "kit.publish-report.json").read_text())
            self.assertEqual(report["source"], src)
            for name in ("LICENSE.txt", "COPYING.txt", "THIRD_PARTY_NOTICES.md", "source.json"):
                self.assertIn(name, report["kit_files"])
            # a repository and an ordinary build: the link is the repository, and source.json says why it is no more
            b = fake_build(Path(tmp) / "b", "py3")
            f = self.player(
                tmp,
                "repo",
                source={
                    "repository": "https://code.example.test/plasma-web",
                    "thirdPartySources": "https://code.example.test/sources-v1",
                },
            )
            r = self.emit(f, f.dir / "kit", "--build-dir", str(b))
            self.assertEqual(r.returncode, 0, r.stderr)
            src = json.loads((f.dir / "kit/source.json").read_text())
            self.assertEqual(
                (src["repository"], src["url"], src["commit"], src["tag"], src["thirdPartySources"]),
                (
                    "https://code.example.test/plasma-web",
                    "https://code.example.test/plasma-web",
                    None,
                    None,
                    "https://code.example.test/sources-v1",
                ),
            )
            self.assertEqual(
                (src["build"]["release"], src["build"]["client"], src["build"]["pythonAbi"]), (False, "plClient", "3")
            )
            self.assertEqual(
                src["build"]["files"]["plClient.wasm"]["sha256"],
                hashlib.sha256((b / "plClient.wasm").read_bytes()).hexdigest(),
            )
            self.assertTrue(any("no release build" in n for n in src["notes"]), src["notes"])
            # operator fonts and fonts.url: the notices say what this kit serves instead of the free pair
            own = Path(tmp) / "own"
            own.mkdir()
            (own / "cour.ttf").write_bytes(b"\0\1\0\0own")
            (own / "trebucbd.ttf").write_bytes(b"\0\1\0\0own")
            f = self.player(
                tmp,
                "own",
                fonts={"root": str(own), "map": {"Courier New": "cour.ttf", "Trebuchet MS Bold": "trebucbd.ttf"}},
            )
            self.assertEqual(self.emit(f, f.dir / "kit").returncode, 0)
            tail = (f.dir / "kit/THIRD_PARTY_NOTICES.md").read_text()[len(notices.rstrip("\n")) :]
            self.assertIn("does not serve the free fonts", tail)
            self.assertIn("the operator's file `cour.ttf`", tail)
            f = self.player(tmp, "url", fonts={"url": "https://fonts.example.test/f"})
            self.assertEqual(self.emit(f, f.dir / "kit").returncode, 0)
            self.assertIn(
                "from https://fonts.example.test/f, which the operator serves",
                (f.dir / "kit/THIRD_PARTY_NOTICES.md").read_text(),
            )

    def test_source_entry_is_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            for i, bad in enumerate(
                (
                    {"repository": "http://code.example.test/r"},
                    {"repository": "https://u:p@x.test/r"},
                    {"tag": "-bad"},
                    {"tag": 5},
                    {"repo": "https://x.test/r"},
                    {"thirdPartySources": "ftp://x.test/s"},
                )
            ):
                with self.subTest(source=bad), self.assertRaises(deployment.DeploymentError):
                    deployment.load(self.player(tmp, f"bad{i}", source=bad).path)
            # a tag names exact source: without a release build there is no commit for it to name, so the kit is refused
            f = self.player(tmp, "tag", source={"repository": "https://code.example.test/r", "tag": "v1.0"})
            r = self.emit(f, f.dir / "kit", "--build-dir", str(fake_build(Path(tmp) / "b", "py3")))
            self.assertEqual(r.returncode, 2)
            self.assertIn("only a release build", r.stderr)

    def test_release_kit_names_its_commit_and_refuses_a_modified_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "checkout"
            for rel in KIT_SOURCES:
                (repo / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / rel, repo / rel)
            shutil.copytree(ROOT / "web/deploy/fonts", repo / "web/deploy/fonts")
            git(tmp, "init", "-q", str(repo))
            git(repo, "add", "-A")
            git(repo, "commit", "-q", "-m", "kit")
            head = git(repo, "rev-parse", "HEAD")
            package = repo / "web/deploy"
            b = fake_build(Path(tmp) / "b", "py3")
            release_record(b, "py3", head)
            repository = "https://code.example.test/plasma-web"
            f = self.player(tmp, source={"repository": repository})

            def kit(name, *extra, dep=f):
                out = Path(tmp) / f"kit-{name}"
                r = self.emit(dep, out, "--build-dir", str(b), *extra, package=package)
                return r, (json.loads((out / "source.json").read_text()) if r.returncode == 0 else None)

            # a clean checkout at the release commit: source.json names the commit, and the link points at it
            r, src = kit("clean")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(
                (src["commit"], src["tag"], src["url"], src["build"]["release"], src["build"]["productTag"]),
                (head, None, f"{repository}/tree/{head}", True, head[:12]),
            )
            self.assertTrue(any("no tag" in n for n in src["notes"]), src["notes"])
            # a tag at the release commit, named by the deployment: the link names the tag
            git(repo, "tag", "v0.1.0")
            tagged = self.player(tmp, "tagged", source={"repository": repository, "tag": "v0.1.0"})
            r, src = kit("tagged", dep=tagged)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual((src["tag"], src["url"]), ("v0.1.0", f"{repository}/tree/v0.1.0"))
            # ... or recorded by the build (git describe at build time); a deployment tag that differs is refused
            release_record(b, "py3", head, tag="v0.1.0")
            r, src = kit("recorded")
            self.assertEqual((r.returncode, src["tag"]), (0, "v0.1.0"))
            other = self.player(tmp, "other", source={"repository": repository, "tag": "v0.2.0"})
            r, _ = kit("other", dep=other)
            self.assertEqual(r.returncode, 2)
            self.assertIn("is not the tag v0.1.0", r.stderr)
            release_record(b, "py3", head)
            r, _ = kit("wrongtag", dep=other)  # a tag that does not name the release commit
            self.assertEqual(r.returncode, 2)
            self.assertIn("does not name the release commit", r.stderr)
            # a modified tracked file, then an untracked one: the page would not be the release commit's
            page = repo / SHELL / "index.html"
            page.write_text(page.read_text() + "<!-- local change -->\n")
            r, _ = kit("dirty")
            self.assertEqual(r.returncode, 2)
            self.assertIn("changes that are not committed", r.stderr)
            self.assertIn(SHELL + "/index.html", r.stderr)
            self.assertFalse((Path(tmp) / "kit-dirty").exists())
            git(repo, "checkout", "--", SHELL + "/index.html")
            (repo / "notes.txt").write_text("untracked\n")
            r, _ = kit("untracked")
            self.assertEqual(r.returncode, 2)
            self.assertIn("notes.txt", r.stderr)
            (repo / "notes.txt").unlink()
            self.assertEqual(kit("clean-again")[0].returncode, 0)
            # a later commit: the release build is of another commit than the page files
            (repo / "notes.txt").write_text("committed\n")
            git(repo, "add", "notes.txt")
            git(repo, "commit", "-q", "-m", "later")
            r, _ = kit("later")
            self.assertEqual(r.returncode, 2)
            self.assertIn(f"is of commit {head}", r.stderr)
            # a build changed after its record (or a record copied beside another build)
            release_record(b, "py3", git(repo, "rev-parse", "HEAD"))
            (b / "plClient.wasm").write_bytes((b / "plClient.wasm").read_bytes() + b"changed")
            r, _ = kit("stale")
            self.assertEqual(r.returncode, 2)
            self.assertIn("not the file this release record describes", r.stderr)


def kit_header_rules(text):
    """The kit's _headers as [(pattern, [(name, value)])]: a path pattern at the start of a line, its headers on the
    indented lines below it, # comments (the Pages-style format)."""
    blocks = []
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line[0].isspace():
            blocks.append((line.strip(), []))
        else:
            name, value = line.strip().split(":", 1)
            blocks[-1][1].append((name.strip().lower(), value.strip()))
    return blocks


def kit_header(blocks, path, name):
    """What a Pages-style host sends as header `name` for `path`: every block whose pattern matches applies (an exact
    path, or a prefix before a trailing * splat), and a header that several blocks name is joined with a comma
    (Cloudflare Pages, "Headers"). None when no block names it: the host's own default then applies."""
    values = [
        v
        for pattern, headers in blocks
        if (path.startswith(pattern[:-1]) if pattern.endswith("*") else path == pattern)
        for n, v in headers
        if n == name
    ]
    return ", ".join(values) if values else None


def emit_fixture_kit(tmp):
    """A player deployment's kit, with a fabricated py3 product build: (deployment fixture, build folder, kit folder)."""
    key = tmp / "keys/join.key"
    importlib.import_module("plasma_web.join_token").init_secret(key)
    f = Fixture(
        tmp / "dep",
        ini_text("shard.example.test", "shard.example.test"),
        capabilities={},
        data={"files": "patcher", "gameCode": "server"},
        gateway={
            "url": "wss://gw.example.test",
            "origins": ["https://play.example.test"],
            "join": {"secretFile": str(key)},
        },
    )
    build = fake_build(tmp / "build", "py3")
    kit = tmp / "kit"
    r = subprocess.run(
        [sys.executable, *EMIT_STATIC, str(f.path), str(kit), "--build-dir", str(build)],
        capture_output=True,
        text=True,
        timeout=120,
    )
    if r.returncode:
        raise AssertionError(r.stderr)
    return f, build, kit


class KitCachePolicyTests(unittest.TestCase):
    """A static host that reads the kit's _headers gives each kit file the Cache-Control the origin server sends for
    it (Docs/Web/Deploying.md, "The kit": the cache policy; server.py common_headers): the page at / and the input
    modules revalidate by ETag as the page itself does, and the fonts tree is no-store. The fonts tree adds
    no-transform on both, so that a CDN in front of the host serves it uncompressed with its strong ETag (Deploying.md,
    "What a host of the trees must serve"), and server.py serves the free fonts' licence texts the kit carries."""

    def test_kit_headers_give_every_file_the_origin_servers_cache_control(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            f, build, kit = emit_fixture_kit(tmp)
            blocks = kit_header_rules((kit / "_headers").read_text())
            port = free_port()
            p = subprocess.Popen(
                [
                    sys.executable,
                    *PRODUCT_SERVER,
                    "--port",
                    str(port),
                    "--deployment",
                    str(f.path),
                    "--build-dir",
                    str(build),
                    "--access-log",
                    str(tmp / "access.jsonl"),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )
            self.addCleanup(lambda: (p.terminate(), p.wait(5), p.stdout.close()))
            lines = []
            for line in p.stdout:
                lines.append(line)
                if "serving on" in line:
                    break
            self.assertIn("serving on", "".join(lines), "".join(lines)[-800:])
            paths = ["/"] + [
                "/" + str(x.relative_to(kit)) for x in sorted(kit.rglob("*")) if x.is_file() and x.name != "_headers"
            ]
            # the files the cache rules are for: the page, its modules, the client, the document, fonts, provenance
            for want in (
                "/",
                "/index.html",
                "/boot.mjs",
                "/runtime/config.mjs",
                "/input/capture.mjs",
                "/input/transport.mjs",
                "/input/fullscreen.mjs",
                "/build/plClient.js",
                "/build/plClient.wasm",
                "/runtime/config.json",
                "/runtime/server.ini",
                "/fonts/.fetch-manifest",
                "/fonts/Courier New.ttf",
                "/fonts/LICENSE-LiberationFonts.txt",
                "/fonts/LICENSE-DejaVuFonts.txt",
                "/source.json",
                "/LICENSE.txt",
                "/COPYING.txt",
                "/THIRD_PARTY_NOTICES.md",
            ):
                self.assertIn(want, paths)
            seen = {}
            for path in paths:
                try:
                    with urllib.request.urlopen(
                        f"http://127.0.0.1:{port}" + urllib.parse.quote(path), timeout=10
                    ) as res:
                        status, origin = res.status, res.headers.get_all("Cache-Control")
                except urllib.error.HTTPError as e:
                    status, origin = e.code, e.headers.get_all("Cache-Control")
                seen[path] = (status, origin, kit_header(blocks, path, "cache-control"))
            for path, (status, origin, kit_value) in seen.items():
                with self.subTest(path=path):
                    self.assertEqual(len(origin), 1)
                    self.assertEqual(status, 200)
                    self.assertEqual(kit_value, origin[0])


class ProcessTests(unittest.TestCase):
    """The origin server and gateway.py run as an operator runs them, from one deployment file."""

    PY = Path(sys.executable)  # the gateway needs websockets, which this test imports too

    def start(self, args, ready):
        p = subprocess.Popen([str(self.PY), *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.addCleanup(lambda: (p.terminate(), p.wait(5), p.stdout.close()))
        deadline = time.time() + 30
        lines = []
        while time.time() < deadline:
            line = p.stdout.readline()
            if not line:
                break
            lines.append(line)
            if ready in line:
                return p, lines
        self.fail("did not start: " + "".join(lines)[-800:])

    def start_echo(self):
        srv = socket.socket()
        srv.bind(("127.0.0.1", 0))
        srv.listen()
        srv.settimeout(0.2)
        self.accepted = 0
        stop = [False]

        def run():
            while not stop[0]:
                try:
                    c, _ = srv.accept()
                except OSError:
                    continue
                self.accepted += 1
                c.settimeout(5)
                try:
                    data = c.recv(65536)
                    if data:
                        c.sendall(data[::-1])
                except OSError:
                    pass
                finally:
                    c.close()

        import threading

        threading.Thread(target=run, daemon=True).start()
        self.addCleanup(lambda: (stop.__setitem__(0, True), srv.close()))
        return srv.getsockname()[1]

    def test_server_derives_the_page_document_and_serves_the_ini(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text())
            port = free_port()
            self.start(
                [
                    *PRODUCT_SERVER,
                    "--port",
                    str(port),
                    "--access-log",
                    str(f.dir / "a.jsonl"),
                    "--deployment",
                    str(f.path),
                    "--gateway-url",
                    "ws://127.0.0.1:9",
                ],
                "serving on",
            )
            base = f"http://127.0.0.1:{port}"
            doc = json.loads(urllib.request.urlopen(base + "/runtime/config.json").read())
            ini = urllib.request.urlopen(base + "/runtime/server.ini").read()
            self.assertEqual(ini, (f.dir / "server.ini").read_bytes())
            self.assertEqual(
                doc["shard"],
                {
                    "host": "127.0.0.1",
                    "port": 14617,
                    "serverIni": "/runtime/server.ini",
                    "sha256": hashlib.sha256(ini).hexdigest(),
                    "name": "fixture",
                },
            )
            with self.assertRaises(urllib.error.HTTPError) as e:  # no Server.Status, no relay
                urllib.request.urlopen(base + "/runtime/status")
            self.assertEqual(e.exception.code, 404)
            self.assertEqual(doc["gateway"], "ws://127.0.0.1:9")
            with self.assertRaises(urllib.error.HTTPError) as e:  # one ini only: not also in the data tree
                urllib.request.urlopen(base + "/data/server.ini")
            self.assertEqual(e.exception.code, 404)
            self.assertIn(b"dat/fixture.age", urllib.request.urlopen(base + "/data/.fetch-manifest").read())
            # The page's own validator accepts the served document and ini (node, config.mjs).
            node = subprocess.run(
                [
                    need_node(),
                    "--input-type=module",
                    "-e",
                    "import {validateConfiguration, validateServerIni} from " + json.dumps(CONFIG_MJS.as_uri()) + ";"
                    "const [d, i, b] = JSON.parse(process.argv[1]);"
                    "const c = validateConfiguration(d, b + '/runtime/config.json', b + '/');"
                    "validateServerIni(i, c.shard); console.log(c.capabilities.harness);"
                    "let refused = false; try { validateConfiguration(d, 'https://play.example.test/runtime/config.json',"
                    " 'https://play.example.test/'); } catch (_) { refused = true; } console.log(refused);",
                    json.dumps([doc, ini.decode(), base]),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(node.stdout.split(), ["true", "true"], node.stderr)

    def test_ctrl_c_stops_the_server_and_the_gateway(self):
        # Ctrl-C (SIGINT) stops the origin server as it stops the gateway: status 0, no traceback, the port closed.
        # The child installs Python's own SIGINT handler first, as a terminal session has it, because a process started
        # in the background of a non-interactive shell inherits SIGINT ignored.
        sigint = "import runpy, signal, sys; signal.signal(signal.SIGINT, signal.default_int_handler); "
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(
                tmp,
                ini_text("127.0.0.1", "127.0.0.1", free_port()),
                gateway={"origins": ["http://127.0.0.1:8613"], "resolveUpstreams": False},
            )
            port, gw = free_port(), free_port()
            for module, args, ready, listen in (
                (
                    "plasma_web.server",
                    [
                        "--port",
                        str(port),
                        "--access-log",
                        str(f.dir / "a.jsonl"),
                        "--deployment",
                        str(f.path),
                        "--gateway-url",
                        "ws://127.0.0.1:9",
                    ],
                    "serving on",
                    port,
                ),
                (
                    "plasma_web.gateway",
                    ["--deployment", str(f.path), "--port", str(gw), "--log", str(f.dir / "gw.jsonl")],
                    "Plasma gateway:",
                    gw,
                ),
            ):
                with self.subTest(module=module):
                    code = sigint + f"sys.argv[1:] = {args!r}; runpy.run_module({module!r}, run_name='__main__')"
                    p, _ = self.start(["-c", code], ready)
                    p.send_signal(signal.SIGINT)
                    rest = p.stdout.read()
                    self.assertEqual(p.wait(10), 0, rest)
                    self.assertNotIn("Traceback", rest)
                    with self.assertRaises(ConnectionRefusedError):
                        socket.create_connection(("127.0.0.1", listen), timeout=2).close()

    def test_status_relay(self):
        # The login dialog's status text: server.py relays the ini's own Server.Status URL (any HTTP status, at
        # most 255 bytes) at /runtime/status, and the document names the relay; a failed request answers its error.
        import http.server
        import threading

        class Status(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                body = (b"Welcome! " + b"x" * 400) if self.path == "/long" else b"No page found at /"
                self.send_response(200 if self.path == "/long" else 404)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        upstream = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Status)
        threading.Thread(target=upstream.serve_forever, daemon=True).start()
        self.addCleanup(upstream.shutdown)
        for path, expect in (("/", b"No page found at /"), ("/long", b"Welcome! " + b"x" * 246), (None, None)):
            with self.subTest(path=path), tempfile.TemporaryDirectory() as tmp:
                url = f"http://127.0.0.1:{upstream.server_port}{path}" if path else f"http://127.0.0.1:{free_port()}/"
                f = Fixture(tmp, ini_text(extra=f'Server.Status "{url}"\n'))
                self.assertEqual(
                    deployment.load(f.path).public_document("ws://127.0.0.1:9")["shard"].get("status"), None
                )
                port = free_port()
                self.start(
                    [
                        *PRODUCT_SERVER,
                        "--port",
                        str(port),
                        "--access-log",
                        str(f.dir / "a.jsonl"),
                        "--deployment",
                        str(f.path),
                        "--gateway-url",
                        "ws://127.0.0.1:9",
                    ],
                    "serving on",
                )
                base = f"http://127.0.0.1:{port}"
                doc = json.loads(urllib.request.urlopen(base + "/runtime/config.json").read())
                self.assertEqual(doc["shard"]["status"], "/runtime/status")
                body = urllib.request.urlopen(base + "/runtime/status").read()
                if expect is None:
                    self.assertTrue(body and len(body) <= 255, body)
                else:
                    self.assertEqual(body, expect)

    def test_gateway_policy_from_the_deployment(self):
        from websockets.sync.client import connect
        from websockets.exceptions import InvalidStatus

        echo = self.start_echo()
        dead = free_port()  # not the ini port, so not allowlisted
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(
                tmp,
                ini_text("127.0.0.1", "127.0.0.1", echo),
                gateway={
                    "origins": ["http://127.0.0.1:8613"],
                    "extraUpstreamHosts": ["localhost"],
                    "resolveUpstreams": False,
                },
            )
            gw = free_port()
            log = Path(tmp) / "gw.jsonl"
            _, out = self.start(
                [*GATEWAY, "--deployment", str(f.path), "--port", str(gw), "--log", str(log)], "Plasma gateway:"
            )
            self.assertIn(f"127.0.0.1:{echo}", out[-1])
            url = f"ws://127.0.0.1:{gw}/connect?host=127.0.0.1&port={echo}"
            with connect(url, origin="http://127.0.0.1:8613", proxy=None) as ws:  # allowed: bytes relayed
                ws.send(bytes([10, 1, 2, 3]))
                self.assertEqual(ws.recv(timeout=5), bytes([3, 2, 1, 10]))
            before = self.accepted
            for target, origin, status in [
                (url, "http://127.0.0.1:9999", 403),  # origin not listed
                (f"ws://127.0.0.1:{gw}/connect?host=127.0.0.2&port={echo}", "http://127.0.0.1:8613", 403),
                (f"ws://127.0.0.1:{gw}/connect?host=127.0.0.1&port={dead}", "http://127.0.0.1:8613", 403),
                (f"ws://127.0.0.1:{gw}/auth", "http://127.0.0.1:8613", 403),  # diagnostic route
            ]:
                with self.subTest(target=target, origin=origin):
                    with self.assertRaises(InvalidStatus) as e:
                        connect(target, origin=origin, proxy=None, open_timeout=10).close()
                    self.assertEqual(e.exception.response.status_code, status)
            time.sleep(0.5)
            self.assertEqual(self.accepted, before, "a refused request opened a shard connection")
            refusals = [r for r in map(json.loads, log.read_text().splitlines()) if "refused" in r]
            self.assertEqual(
                [(r["refused"], r["origin"]) for r in refusals],
                [(403, "http://127.0.0.1:9999")] + [(403, "http://127.0.0.1:8613")] * 3,
            )
            self.assertEqual(refusals[0]["reason"], "Origin not allowed")
        with tempfile.TemporaryDirectory() as tmp:  # a game-server range (CIDR) at the client port
            f = Fixture(
                tmp,
                ini_text("shard.example.test", "shard.example.test", echo),
                gateway={
                    "origins": ["http://127.0.0.1:8613"],
                    "extraUpstreamNetworks": ["127.0.0.0/24"],
                    "resolveUpstreams": False,
                },
            )
            gw = free_port()
            self.start([*GATEWAY, "--deployment", str(f.path), "--port", str(gw)], "Plasma gateway:")
            with connect(
                f"ws://127.0.0.1:{gw}/connect?host=127.0.0.1&port={echo}", origin="http://127.0.0.1:8613", proxy=None
            ) as ws:
                ws.send(bytes([11, 5]))
                self.assertEqual(ws.recv(timeout=5), bytes([5, 11]))
            for host in ("127.000.0.1", "192.0.2.1", "localhost"):
                with self.subTest(host=host), self.assertRaises(InvalidStatus) as e:
                    connect(
                        f"ws://127.0.0.1:{gw}/connect?host={host}&port={echo}",
                        origin="http://127.0.0.1:8613",
                        proxy=None,
                        open_timeout=10,
                    ).close()
                self.assertEqual(e.exception.response.status_code, 403)
        with tempfile.TemporaryDirectory() as tmp:  # an allowlisted shard that does not exist: 502 before the upgrade
            f = Fixture(tmp, ini_text("shard.invalid", "shard.invalid"), gateway={"origins": ["http://127.0.0.1:8613"]})
            gw = free_port()
            log = Path(tmp) / "gw.jsonl"
            self.start([*GATEWAY, "--deployment", str(f.path), "--port", str(gw), "--log", str(log)], "Plasma gateway:")
            with self.assertRaises(InvalidStatus) as e:
                connect(
                    f"ws://127.0.0.1:{gw}/connect?host=shard.invalid&port=14617",
                    origin="http://127.0.0.1:8613",
                    proxy=None,
                    open_timeout=10,
                ).close()
            self.assertEqual(e.exception.response.status_code, 502)
            time.sleep(0.2)
            self.assertEqual(json.loads(log.read_text().splitlines()[-1])["refused"], 502)

    def test_server_serves_the_deployment_build(self):
        # server.py --deployment serves only the deployment's build by name; a different --client-name fails.
        with tempfile.TemporaryDirectory() as tmp:
            b = fake_build(Path(tmp) / "b", "py2")
            fake_build(b, "py3")
            f = Fixture(tmp, ini_text(), data={"files": "patcher", "gameCode": "server-py2"})
            port = free_port()
            self.start(
                [
                    *PRODUCT_SERVER,
                    "--port",
                    str(port),
                    "--access-log",
                    str(f.dir / "a.jsonl"),
                    "--deployment",
                    str(f.path),
                    "--gateway-url",
                    "ws://127.0.0.1:9",
                    "--build-dir",
                    str(b),
                ],
                "serving on",
            )
            base = f"http://127.0.0.1:{port}"
            self.assertEqual(json.loads(urllib.request.urlopen(base + "/runtime/config.json").read())["build"], "py2")
            for name in ("plClient-py2.js", "plClient-py2.wasm"):
                self.assertEqual(urllib.request.urlopen(f"{base}/build/{name}").read(), (b / name).read_bytes())
            for name in ("plClient.js", "plClient.wasm"):
                with self.subTest(name=name), self.assertRaises(urllib.error.HTTPError) as e:
                    urllib.request.urlopen(f"{base}/build/{name}")
                self.assertEqual(e.exception.code, 404)
            manifest = urllib.request.urlopen(base + "/data/.fetch-manifest").read()  # server mode: no loose game code
            self.assertNotIn(b" python/", manifest)
            # --client-name and --game-code are the test harness server's options: the product's takes neither
            for extra, want in (
                (["--client-name", "plClient-py2"], "unrecognized arguments: --client-name"),
                (["--game-code", "server-py2"], "unrecognized arguments: --game-code"),
            ):
                with self.subTest(extra=extra):
                    r = subprocess.run(
                        [
                            str(self.PY),
                            *PRODUCT_SERVER,
                            "--port",
                            "0",
                            "--access-log",
                            str(f.dir / "b.jsonl"),
                            "--deployment",
                            str(f.path),
                            "--gateway-url",
                            "ws://127.0.0.1:9",
                            *extra,
                        ],
                        capture_output=True,
                        text=True,
                        timeout=30,
                    )
                    self.assertEqual(r.returncode, 2, r.stderr)
                    self.assertIn(want, r.stderr)

    def test_server_serves_the_provenance_files(self):
        # server.py serves /source.json, /LICENSE.txt, /COPYING.txt and /THIRD_PARTY_NOTICES.md as a kit
        # carries them, revalidated by ETag like the page; a release build that does not match is refused at start.
        with tempfile.TemporaryDirectory() as tmp:
            b = fake_build(Path(tmp) / "b", "py3")
            f = Fixture(
                tmp,
                ini_text(),
                data={"files": "patcher", "gameCode": "server"},
                source={"repository": "https://code.example.test/plasma-web"},
            )
            port = free_port()
            _, lines = self.start(
                [
                    *PRODUCT_SERVER,
                    "--port",
                    str(port),
                    "--access-log",
                    str(f.dir / "a.jsonl"),
                    "--deployment",
                    str(f.path),
                    "--gateway-url",
                    "ws://127.0.0.1:9",
                    "--build-dir",
                    str(b),
                ],
                "serving on",
            )
            self.assertIn(
                "source: https://code.example.test/plasma-web (commit none, tag none, release build False)",
                "".join(lines),
            )
            base = f"http://127.0.0.1:{port}"
            r = urllib.request.urlopen(base + "/source.json")
            src = json.loads(r.read())
            self.assertEqual((r.headers["Content-Type"], r.headers["Cache-Control"]), ("application/json", "no-cache"))
            self.assertEqual(
                (src["repository"], src["url"], src["commit"], src["build"]["release"]),
                ("https://code.example.test/plasma-web", "https://code.example.test/plasma-web", None, False),
            )
            for name, ctype in (
                ("LICENSE.txt", "text/plain; charset=utf-8"),
                ("COPYING.txt", "text/plain; charset=utf-8"),
            ):
                r = urllib.request.urlopen(f"{base}/{name}")
                self.assertEqual(
                    (r.read(), r.headers["Content-Type"], r.headers["Cache-Control"]),
                    ((ROOT / name).read_bytes(), ctype, "no-cache"),
                )
            r = urllib.request.urlopen(base + "/THIRD_PARTY_NOTICES.md")
            text = r.read().decode()
            self.assertEqual(r.headers["Content-Type"], "text/markdown; charset=utf-8")
            self.assertTrue(text.startswith((ROOT / "THIRD_PARTY_NOTICES.md").read_text().rstrip("\n") + "\n"))
            self.assertIn("## This kit", text)
            # a release record of another commit than this checkout's, and one that does not describe the build
            for commit, damage, want in (
                ("0" * 40, False, "a release kit takes its page from the same commit"),
                ("0" * 40, True, "not the file this release record describes"),
            ):
                with self.subTest(damage=damage):
                    release_record(b, "py3", commit)
                    if damage:
                        (b / "plClient.js").write_text("// another build\n")
                    r = subprocess.run(
                        [
                            str(self.PY),
                            *PRODUCT_SERVER,
                            "--port",
                            "0",
                            "--access-log",
                            str(f.dir / "b.jsonl"),
                            "--deployment",
                            str(f.path),
                            "--gateway-url",
                            "ws://127.0.0.1:9",
                            "--build-dir",
                            str(b),
                        ],
                        capture_output=True,
                        text=True,
                        timeout=60,
                    )
                    self.assertEqual(r.returncode, 2, r.stderr)
                    self.assertIn(want, r.stderr)

    def serve_fonts(self, args, script=PRODUCT_SERVER):
        """An origin server started with args: its /fonts tree (the manifest's names, the two faces' bytes) and its
        start lines about fonts. script: the server's arguments after the interpreter."""
        port = free_port()
        _, lines = self.start([*script, "--port", str(port), "--access-log", os.devnull, *args], "serving on")
        base = f"http://127.0.0.1:{port}"
        manifest = urllib.request.urlopen(base + "/fonts/.fetch-manifest").read().decode()
        got = {
            n: urllib.request.urlopen(base + "/fonts/" + urllib.request.quote(n)).read()
            for n in ("Courier New.ttf", "Trebuchet MS Bold.ttf")
        }
        head = urllib.request.urlopen(urllib.request.Request(base + "/fonts/Courier%20New.ttf", method="HEAD"))
        self.assertTrue(re.fullmatch(r'"[^"]+"', head.headers["ETag"]), head.headers["ETag"])  # a strong tag
        self.assertEqual(int(head.headers["Content-Length"]), len(got["Courier New.ttf"]))
        return [line.split(" ", 2)[2] for line in manifest.splitlines()], got, [l for l in lines if "fonts" in l]

    def refused_start(self, args, script=PRODUCT_SERVER):
        """The stderr of an origin server that refuses to start with args (exit status 2)."""
        r = subprocess.run(
            [str(self.PY), *script, "--port", "0", "--access-log", os.devnull, *args],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        return r.stderr

    def test_server_fonts(self):
        # What the product's origin server serves at /fonts, and the one line it logs about it at start. The host
        # copies are the test harness server's alone (its own suite checks them).
        free = deployment.free_font_files()
        serve, refused = self.serve_fonts, self.refused_start
        with tempfile.TemporaryDirectory() as tmp:
            # a deployment that names no fonts, and one with the old {"root": null}: the free pair, said once
            for i, extra in enumerate(({}, {"fonts": {"root": None}})):
                with self.subTest(extra=extra):
                    f = Fixture(Path(tmp) / f"free{i}", ini_text(), **extra)
                    names, got, lines = serve(["--deployment", str(f.path), "--gateway-url", "ws://127.0.0.1:9"])
                    self.assertEqual(names, ["Courier New.ttf", "Trebuchet MS Bold.ttf"])
                    self.assertEqual(got, {n: p.read_bytes() for n, p in free.items()})
                    self.assertEqual(len(lines), 1, lines)
                    self.assertIn("the deployment names no fonts: serving the free substitutes", lines[0])
            # no deployment: the free pair too
            names, got, lines = serve(["--data-root", str(f.dir / "data")])
            self.assertEqual(got, {n: p.read_bytes() for n, p in free.items()})
            self.assertEqual(len(lines), 1, lines)
            # an operator's root and map
            own = Path(tmp) / "own"
            own.mkdir()
            (own / "cour.ttf").write_bytes(b"operator courier")
            (own / "trebucbd.ttf").write_bytes(b"operator trebuchet")
            f = Fixture(
                Path(tmp) / "own-dep",
                ini_text(),
                fonts={"root": str(own), "map": {"Courier New": "cour.ttf", "Trebuchet MS Bold": "trebucbd.ttf"}},
            )
            names, got, lines = serve(["--deployment", str(f.path), "--gateway-url", "ws://127.0.0.1:9"])
            self.assertEqual(
                got, {"Courier New.ttf": b"operator courier", "Trebuchet MS Bold.ttf": b"operator trebuchet"}
            )
            self.assertIn(f"fonts.root {own.resolve()}", lines[0])
            # the host copies: the product's server serves none (it refuses the deployment that asks, and has no flag)
            host = Fixture(Path(tmp) / "host", ini_text(), fonts={"hostCopies": True})
            self.assertIn(
                "fonts.hostCopies is a test-harness setting",
                refused(["--deployment", str(host.path), "--gateway-url", "ws://127.0.0.1:9"]),
            )
            self.assertIn(
                "unrecognized arguments: --host-fonts", refused(["--data-root", str(f.dir / "data"), "--host-fonts"])
            )

    def test_client_game_code_refused_at_start(self):
        # The product's origin server and gateway refuse a deployment that names a client game-code mode at start: a
        # product process does not know the test harness's modes (the harness server's runs are the overlay's own tests)
        for mode in ("client", "client-python"):
            with tempfile.TemporaryDirectory() as tmp:
                f = Fixture(
                    tmp,
                    ini_text(),
                    capabilities={},
                    data={"root": "data", "files": "patcher", "gameCode": mode},
                    gateway={
                        "bind": "127.0.0.1",
                        "port": 0,
                        "origins": [],
                        "join": {"secretFile": str(Path(tmp) / "join.key"), "ttlSeconds": 60},
                    },
                )
                for name, args in (
                    (
                        "server.py",
                        [
                            *PRODUCT_SERVER,
                            "--port",
                            "0",
                            "--access-log",
                            str(f.dir / "a.jsonl"),
                            "--deployment",
                            str(f.path),
                            "--gateway-url",
                            "ws://127.0.0.1:9",
                        ],
                    ),
                    ("gateway.py", [*GATEWAY, "--deployment", str(f.path), "--port", "0"]),
                ):
                    with self.subTest(mode=mode, program=name):
                        r = subprocess.run([str(self.PY), *args], capture_output=True, text=True, timeout=30)
                        self.assertEqual(r.returncode, 2, r.stderr)
                        self.assertIn("nvalid deployment: data.gameCode must be server, server-py2 or local", r.stderr)

    def test_hd_pack_player_imports_no_builder(self):
        # A player deployment may name HD packs that already exist (textures.hdPack). What reads them
        # at run time is deployment.py, emit_static.py (the kit names the base, copies no pack) and server.py (/hdpack);
        # none of them imports a module of the HD texture-pack builder or of the client game-code tool (BUILDER_MODULES).
        def imported(stderr):
            names = {line.split("|")[-1].strip() for line in stderr.splitlines() if line.startswith("import time:")}
            return sorted(n for n in names if n.removeprefix("plasma_web.").split(".")[0] in BUILDER_MODULES)

        with tempfile.TemporaryDirectory() as tmp:
            packs = Path(tmp) / "packs"
            packs.mkdir()
            (packs / "Personal.hdpack").write_bytes(b"PLHDPK01")
            gateway = {
                "bind": "127.0.0.1",
                "port": 0,
                "origins": ["https://play.example.test"],
                "url": "wss://gw.example.test",
                "join": {"secretFile": str(Path(tmp) / "join.key")},
            }
            f = Fixture(
                tmp,
                ini_text(),
                capabilities={},
                gateway=gateway,
                data={"root": "data", "files": "patcher"},
                textures={"hdPack": {"root": str(packs), "ages": ["Personal"]}},
            )
            r = subprocess.run(
                [str(self.PY), *DEPLOYMENT_CLI, "--init-join-secret", str(f.path)],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run(
                [str(self.PY), "-X", "importtime", *EMIT_STATIC, str(f.path), str(Path(tmp) / "kit")],
                capture_output=True,
                text=True,
                timeout=60,
            )
            self.assertEqual(r.returncode, 0, r.stderr[-800:])
            self.assertIn("plasma_web.deployment", {line.split("|")[-1].strip() for line in r.stderr.splitlines()})
            self.assertEqual(imported(r.stderr), [])
            kit = Path(tmp) / "kit"
            self.assertEqual(
                json.loads((kit / "runtime/config.json").read_text())["textures"],
                {"hdPack": {"base": "/hdpack", "ages": ["Personal"]}},
            )
            self.assertEqual(list(kit.rglob("*.hdpack")), [])
            port, log = free_port(), Path(tmp) / "server.log"
            with open(log, "w") as out:
                p = subprocess.Popen(
                    [
                        str(self.PY),
                        "-X",
                        "importtime",
                        *PRODUCT_SERVER,
                        "--port",
                        str(port),
                        "--access-log",
                        str(Path(tmp) / "a.jsonl"),
                        "--deployment",
                        str(f.path),
                    ],
                    stdout=out,
                    stderr=subprocess.STDOUT,
                    text=True,
                )
            self.addCleanup(lambda: (p.poll() is None and p.terminate(), p.wait(5)))
            deadline = time.time() + 30
            while "serving on" not in log.read_text() and time.time() < deadline and p.poll() is None:
                time.sleep(0.1)
            self.assertIn("serving on", log.read_text())
            body = urllib.request.urlopen(f"http://127.0.0.1:{port}/hdpack/Personal.hdpack").read()
            self.assertEqual(body, b"PLHDPK01")
            p.terminate()
            p.wait(5)
            self.assertIn(
                "plasma_web.deployment", {line.split("|")[-1].strip() for line in log.read_text().splitlines()}
            )
            self.assertEqual(imported(log.read_text()), [])

    def test_gateway_refuses_mixed_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Fixture(tmp, ini_text(), gateway={"origins": ["http://127.0.0.1:8613"]})
            for extra in (["--origin", "http://127.0.0.1:1"], ["--shard-host", "127.0.0.2"], ["--bind", "0.0.0.0"]):
                with self.subTest(extra=extra):
                    r = subprocess.run(
                        [str(self.PY), *GATEWAY, "--deployment", str(f.path), "--port", "0", *extra],
                        capture_output=True,
                        text=True,
                        timeout=30,
                    )
                    self.assertEqual(r.returncode, 2, r.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
