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
"""The rules the web product keeps in one module each, and the page's copies of two of them.

- The shard's server.ini grammar (plasma_web.server_ini, which the deployment loader applies) and the page's own
  reader (validateServerIni in the page shell's config.mjs, Sources/Plasma/Apps/plClient/emscripten/shell/), on the
  cases of shared_rules.json.
- The first-run graphics.ini table (plasma_web.deployment INITIAL_GRAPHICS_INI) and the page's copy of it (config.mjs),
  on the same file.
- The page-origin rule (plasma_web.origins) and its callers: the deployment's gateway.origins, the gateway's
  --origin and the origin server's --asset-origin.
- The per-client rate table (plasma_web.client_limits) that the gateway and the origin server both count with.
- The .fetch-manifest writer (plasma_web.manifest) of the origin server and the static kit.
- The deployment command line (python -m plasma_web.deployment).
The page's copies run under node; without node those cases are skipped, and the skip names the reason.
"""

import base64
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from plasma_web import CONFIG_MJS, client_limits, deployment, gateway, manifest, origins, server, server_ini

HERE = Path(__file__).resolve().parent
RULES = json.loads((HERE / "shared_rules.json").read_text(encoding="utf-8"))
NODE = shutil.which("node")


def ini_cases():
    """The serverIni cases with their texts expanded ($KEYS, $KEY, replace) and their shard filled in."""
    rules = RULES["serverIni"]
    key = rules["key"]
    keys = "".join(f'Server.{s}.{k} "{key}"\n' for s in ("Auth", "Game", "Gate") for k in ("N", "X"))
    out = []
    for case in rules["cases"]:
        text = case["text"].replace("$KEYS", keys)
        if "replace" in case:
            old, new = (t.replace("$KEY", key) for t in case["replace"])
            assert old in text, case["name"]
            text = text.replace(old, new, 1)
        out.append(dict(case, text=text.replace("$KEY", key), shard=case.get("shard", rules["shard"])))
    return out


def graphics_cases():
    """The graphicsIni cases (each one's values: its own, or "every setting" with its "set" applied), then the cases
    every row of the table implies: a required setting left out, each end of a range and one step past it, and a
    value of the wrong kind."""
    rules = RULES["graphicsIni"]
    full = rules["cases"][0]["values"]
    out = []
    for case in rules["cases"]:
        values = case["values"] if "values" in case else {**full, **case["set"]}
        out.append(dict(case, values=values))
    for key, kind, low, high, required in rules["table"]:
        if required:
            out.append(
                {"name": f"{key} left out", "accept": False, "values": {k: v for k, v in full.items() if k != key}}
            )
        if kind in ("int", "number"):
            step = 1 if kind == "int" else 0.01
            for value, accept in ((low, True), (high, True), (low - step, False), (high + step, False)):
                out.append({"name": f"{key} {value}", "accept": accept, "values": {**full, key: value}})
        else:
            for value, accept in ((True, True), (False, True), (0, False)):
                out.append({"name": f"{key} {value!r}", "accept": accept, "values": {**full, key: value}})
    return out


def page_results(ini, graphics):
    """What config.mjs does with each case, under node: validateServerIni(text, shard), and validateConfiguration of a
    page document's text carrying graphics.initialIni, read as the page reads it (parseDocument; then
    initialGraphicsIni for the canvas)."""
    script = (
        "import {readFileSync} from 'node:fs';"
        "const m = await import(process.argv[1]);"
        "const input = JSON.parse(readFileSync(0, 'utf8'));"
        "const out = {ini: [], graphics: []};"
        "for (const c of input.ini) {"
        "  try { m.validateServerIni(c.text, c.shard); out.ini.push({ok: true}); }"
        "  catch (e) { out.ini.push({ok: false, code: e.code ?? null}); } }"
        "for (const text of input.graphics) {"
        "  try { const doc = m.parseDocument(text);"
        "        const config = m.validateConfiguration(doc.value, input.base, input.base, doc.fraction);"
        "        out.graphics.push({ok: true, written: m.initialGraphicsIni(config.initialGraphicsIni, ...input.canvas)}); }"
        "  catch (e) { out.graphics.push({ok: false, code: e.code ?? null}); } }"
        "console.log(JSON.stringify(out));"
    )
    document = {
        "schema": 1,
        "roots": {"fonts": "/fonts", "data": "/data"},
        "gateway": "ws://127.0.0.1:9",
        "shard": {"host": "192.0.2.174", "port": 14617, "serverIni": "/runtime/server.ini", "sha256": "0" * 64},
    }
    # Each graphics case as the document's text, as the page reads it (config.mjs parseDocument): 4.0 stays 4.0.
    payload = {
        "ini": [{"text": c["text"], "shard": c["shard"]} for c in ini],
        "graphics": [json.dumps({**document, "graphics": {"initialIni": c["values"]}}) for c in graphics],
        "document": document,
        "base": "http://127.0.0.1:8613/runtime/config.json",
        "canvas": RULES["graphicsIni"]["canvas"],
    }
    r = subprocess.run(
        [NODE, "--input-type=module", "-e", script, CONFIG_MJS.as_uri()],
        input=json.dumps(payload),
        capture_output=True,
        text=True,
        timeout=60,
    )
    if r.returncode:
        raise AssertionError(r.stderr[-2000:])
    return json.loads(r.stdout)


class SharedFixture(unittest.TestCase):
    """shared_rules.json against both readers."""

    def test_fixture_rule(self):
        # One rule for both readers: no case marks the page as less strict than the tools.
        for case in RULES["serverIni"]["cases"] + RULES["graphicsIni"]["cases"]:
            with self.subTest(case=case["name"]):
                self.assertNotIn("pageAccepts", case)

    def test_client_parser_cases(self):
        # The client's own parser is pinned by Sources/Tests/FeatureTests/pfConsoleCoreTest/
        # test_pfServerIni.cpp. Its cases where the tools used to accept what the client refuses or cuts are shared
        # cases here, so both readers keep them: semicolon_and_slash_comment_lines (';' and '//' lines are unknown
        # settings) and bare_values (a bare value ends at '=' or ','; a plain bare host or name reads as written).
        pinned = {
            "a ';' comment line (test_pfServerIni.cpp semicolon_and_slash_comment_lines)": False,
            "a '//' comment line (test_pfServerIni.cpp semicolon_and_slash_comment_lines)": False,
            "bare values (test_pfServerIni.cpp bare_values)": True,
            "a bare key (test_pfServerIni.cpp bare_values)": False,
            "a bare URL with a query (test_pfServerIni.cpp bare_values)": False,
            "a bare value with a comma (test_pfServerIni.cpp bare_values)": False,
        }
        cases = {case["name"]: case for case in ini_cases()}
        for name, accept in pinned.items():
            with self.subTest(case=name):
                self.assertEqual(cases[name]["accept"], accept)
                if accept:
                    server_ini.read(cases[name]["text"])
                else:
                    with self.assertRaises(server_ini.ServerIniError):
                        server_ini.read(cases[name]["text"])

    def test_server_ini_tools(self):
        for case in ini_cases():
            with self.subTest(case=case["name"]):
                try:
                    settings = server_ini.read(case["text"])
                except server_ini.ServerIniError:
                    self.assertFalse(case["accept"])
                    with self.assertRaises(deployment.DeploymentError):  # the loader's form of the refusal
                        deployment.read_server_ini(case["text"])
                    continue
                self.assertTrue(case["accept"])
                entry, port, _ = server_ini.endpoints(settings)
                self.assertEqual((entry, port), (case["shard"]["host"], case["shard"]["port"]))
                self.assertEqual(deployment.read_server_ini(case["text"]), settings)

    def test_graphics_table_tools(self):
        table = [[key, *row] for key, row in deployment.INITIAL_GRAPHICS_INI.items()]
        self.assertEqual(table, RULES["graphicsIni"]["table"])
        order = [row[0] for row in table]
        for case in graphics_cases():
            with self.subTest(case=case["name"]):
                try:
                    out = deployment.initial_graphics_ini(case["values"])
                except deployment.DeploymentError:
                    self.assertFalse(case["accept"])
                    continue
                self.assertTrue(case["accept"])
                self.assertEqual(list(out), [k for k in order if k in case["values"]])  # the writer's order

    @unittest.skipIf(NODE is None, "node is not installed: the page's copies (config.mjs) are not run")
    def test_page_copies(self):
        ini, graphics = ini_cases(), graphics_cases()
        got = page_results(ini, graphics)
        self.assertEqual(len(got["ini"]), len(ini))
        for case, result in zip(ini, got["ini"]):
            with self.subTest(ini=case["name"]):
                self.assertEqual(result["ok"], case["accept"], result)
                if not result["ok"]:
                    self.assertEqual(result["code"], "INI-INVALID")
        for case, result in zip(graphics, got["graphics"]):
            with self.subTest(graphics=case["name"]):
                self.assertEqual(result["ok"], case["accept"], result)
                if "written" in case:
                    self.assertEqual(result["written"], case["written"])


ACCEPTED_ORIGINS = [
    "https://play.example.test",
    "https://play.example.test:8443",
    "https://example.test.",
    "https://xn--bcher-kva.example",
    "https://a-b.example",
    "http://127.0.0.1:8790",
    "http://localhost:8790",
    "http://127.0.0.1",
    "https://127.0.0.1:8792",
    "https://192.0.2.186:8792",
    "https://[2001:db8::1]",
    "https://[2001:db8::1]:8443",
    "https://" + "a" * 63 + ".example",
]
REFUSED_ORIGINS = [
    None,
    7,
    "",
    "null",
    "*",
    "https://*.example",
    "play.example.test",
    "//play.example.test",
    "http://example.com",
    "http://192.0.2.1:8790",
    "http://[::1]:8790",
    "ws://127.0.0.1:1",
    "ftp://example.com",
    "https://example.com/",
    "https://example.com/path",
    "https://example.com?q=x",
    "https://example.com#x",
    "https://user@example.com",
    "https://user:pw@example.com",
    "https://@example.com",
    "https://example.com:443",
    "http://127.0.0.1:80",
    "http://127.0.0.1:0",
    "https://example.com:65536",
    "https://example.com:abc",
    "https://example.com:",
    "https://bad_host",
    "https://-bad.example",
    "https://bad-.example",
    "https://a..example",
    "https://example.com..",
    "https://" + "a" * 64 + ".example",
    "https://" + "a." * 126 + "ab",
    "https://Example.com",
    "HTTPS://example.com",
    "https://127.1",
    "https://127.000.0.1",
    "https://[::0001]",
    "https://[2001:DB8::1]",
    "https://[fe80::1%25en0]",
    "https://[2001:db8::1",
    "https://example.com,https://evil.example",
    "https://example.com\r\nX-Test: injected",
    "https://example.com ",
    " https://example.com",
    "https://example.com\\",
    "https://example.com/" + "a" * 2040,
]


def write_deployment(tmp, **gateway):
    """A loopback harness deployment in tmp with a synthetic server.ini; gateway fields as given."""
    tmp = Path(tmp)
    (tmp / "data/dat").mkdir(parents=True, exist_ok=True)
    key = base64.b64encode(bytes(64)).decode()
    (tmp / "server.ini").write_text(
        "".join(f'Server.{s}.{k} "{key}"\n' for s in ("Auth", "Game", "Gate") for k in ("N", "X"))
        + 'Server.Auth.Host "127.0.0.1"\n'
    )
    doc = {
        "schema": 1,
        "name": "fixture",
        "serverIni": "server.ini",
        "data": {"root": "data"},
        "gateway": {"bind": "127.0.0.1", "port": 0, "resolveUpstreams": False, **gateway},
        "capabilities": {"harness": True},
    }
    (tmp / "deployment.json").write_text(json.dumps(doc))
    return tmp / "deployment.json"


class Origins(unittest.TestCase):
    def test_accepted(self):
        for value in ACCEPTED_ORIGINS:
            with self.subTest(value=value):
                self.assertEqual(origins.page_origin(value), value)

    def test_refused(self):
        for value in REFUSED_ORIGINS:
            with self.subTest(value=value), self.assertRaises(ValueError):
                origins.page_origin(value)

    def test_loopback(self):
        for value in ("http://127.0.0.1:8790", "https://localhost:8443", "http://localhost"):
            self.assertEqual(origins.loopback_origin(value), value)
        for value in ("https://example.com", "https://192.0.2.1:8790", "http://[::1]:8790", "http://127.0.0.1:80"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                origins.loopback_origin(value)

    def test_lists(self):
        many = [f"https://p{i}.example.test" for i in range(origins.MAX_ORIGINS)]
        self.assertEqual(origins.page_origins(many), tuple(many))
        self.assertEqual(
            origins.page_origins(["https://a.example", "https://b.example", "https://a.example"]),
            ("https://a.example", "https://b.example"),
        )
        for values in (many + ["https://one-more.example.test"], ["https://a.example", "http://[::1]:1"]):
            with self.subTest(values=len(values)), self.assertRaises(ValueError):
                origins.page_origins(values)

    def test_deployment_origins(self):
        # gateway.origins follows the rule; the message names the deployment key and the value
        with tempfile.TemporaryDirectory() as tmp:
            d = deployment.load(
                write_deployment(Path(tmp) / "ok", origins=["https://192.0.2.186:8792", "https://192.0.2.186:8792"])
            )
            self.assertEqual(d.gateway_origins, ["https://192.0.2.186:8792"])
            for bad in (
                ["http://[::1]:8790"],
                ["https://play.example.test:443"],
                ["https://Play.example.test"],
                "https://play.example.test",
                [f"https://p{i}.example.test" for i in range(17)],
            ):
                with self.subTest(bad=bad), self.assertRaisesRegex(deployment.DeploymentError, "^gateway.origins"):
                    deployment.load(write_deployment(Path(tmp) / "bad", origins=bad))

    def test_gateway_origins(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_deployment(tmp)
            gw, _ = gateway.configure(
                ["--deployment", str(path), "--origin", "http://localhost:8790", "--origin", "http://localhost:8790"]
            )
            self.assertEqual(gw.origins, ("http://localhost:8790",))
            for bad in ("http://[::1]:8790", "http://127.0.0.1:80", "http://localhost:8790/", "https://example.com"):
                with (
                    self.subTest(bad=bad),
                    contextlib.redirect_stderr(io.StringIO()) as err,
                    self.assertRaises(SystemExit) as e,
                ):
                    gateway.configure(["--deployment", str(path), "--origin", bad])
                self.assertEqual(e.exception.code, 2)
                self.assertIn("--origin must be a loopback origin", err.getvalue())

    def test_server_asset_origins(self):
        self.assertIs(server.validate_asset_origins, origins.page_origins)
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "data").mkdir()
            for value, ok in (
                ("http://127.0.0.1:8765", True),
                ("https://assets.example.test", True),
                ("http://[::1]:8765", False),
                ("https://assets.example.test:443", False),
            ):
                with self.subTest(value=value):
                    ap = server.options()
                    cfg = ap.parse_args(
                        [
                            "--data-root",
                            str(Path(tmp) / "data"),
                            "--access-log",
                            str(Path(tmp) / "a.log"),
                            "--asset-origin",
                            value,
                        ]
                    )
                    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                        if ok:
                            server.configure(cfg, ap)
                            self.assertEqual(cfg.asset_origins, (value,))
                        else:
                            with self.assertRaises(SystemExit):
                                server.configure(cfg, ap)


class RateTable(unittest.TestCase):
    def test_sliding_window(self):
        now = [0.0]
        table = client_limits.RateTable(2, window=60, entries=10, clock=lambda: now[0])
        for t, ok in ((0.0, True), (0.5, True), (59.9, False), (60.0, True), (60.4, False), (60.5, True), (61, False)):
            now[0] = t
            with self.subTest(t=t):
                self.assertEqual(table.hit("k"), ok)

    def test_bounded(self):
        now = [0.0]
        table = client_limits.RateTable(1, window=60, entries=3, clock=lambda: now[0])
        for k in ("a", "b", "c", "d"):
            self.assertTrue(table.hit(k))
            now[0] += 1
        self.assertEqual(list(table.table), ["b", "c", "d"])  # the least recently counted key went first
        self.assertTrue(table.hit("a"))  # so its window starts again
        self.assertFalse(client_limits.RateTable(0, clock=lambda: 0).hit("a"))

    def test_client_keys(self):
        rate = client_limits.ClientRate(
            1, trusted=[__import__("ipaddress").ip_network("127.0.0.1/32")], clock=lambda: 0
        )
        self.assertTrue(rate.hit("2001:db8:1:2::1"))
        self.assertFalse(rate.hit("2001:db8:1:2::ffff"))  # one IPv6 /64 is one client
        self.assertTrue(rate.hit("198.51.100.7"))
        self.assertFalse(rate.hit("::ffff:198.51.100.7"))  # an IPv4-mapped address is its IPv4 address
        self.assertEqual(rate.client("127.0.0.1", ["203.0.113.7"]), ("203.0.113.7", None))
        self.assertEqual(rate.client("198.51.100.1", ["203.0.113.7"]), ("198.51.100.1", None))

    def test_one_table_for_both_servers(self):
        self.assertIs(gateway.ClientRate, client_limits.ClientRate)
        self.assertIs(server.client_limits, client_limits)
        self.assertIsInstance(
            gateway.Admission(None, dict(deployment.LIMIT_DEFAULTS)).connects, client_limits.ClientRate
        )


def main_sound_manifest(data_root, sound_root):
    """The /sound listing as main's server.py derived it from the tree listing, kept here as the reference."""
    if sound_root:
        sound = (
            manifest.tree_manifest(os.path.dirname(sound_root), [os.path.basename(sound_root)]).decode().splitlines()
        )
        sound = [
            line.replace(f" {os.path.basename(sound_root)}/", " sfx/", 1)
            for line in sound
            if line != f"D {os.path.basename(sound_root)}"
        ]
    else:
        sound = manifest.tree_manifest(data_root, ["sfx"]).decode().splitlines()
    return ("\n".join(line.replace(" sfx/", " ", 1) for line in sound if line != "D sfx") + "\n").encode()


class Manifest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="rules-manifest-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        files = {
            "data/dat/b.age": 1000,
            "data/dat/a.prp": 1001,
            "data/sfx/x.ogg": 1002,
            "data/sfx/sfx/y z.ogg": 1003,
            "data/sfx/amb/q.ogg": 1004,
            "data/resource.dat": 1005,
            "sound/w.ogg": 1006,
            "sound/sub/v.ogg": 1007,
            "fonts/one.ttf": 1008,
            "fonts/two.ttf": 1009,
        }
        for rel, mtime in files.items():
            (self.tmp / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.tmp / rel).write_bytes(rel.encode())
            os.utime(self.tmp / rel, (mtime, mtime))

    def test_tree(self):
        self.assertEqual(
            manifest.tree_manifest(str(self.tmp / "data"), ["dat", "resource.dat", "missing"]),
            b"D dat\nF 1001 dat/a.prp\nF 1000 dat/b.age\nF 1005 resource.dat\n",
        )
        self.assertEqual(manifest.tree_manifest(str(self.tmp / "data"), []), b"\n")  # never an empty file
        self.assertIs(server.build_manifest, manifest.tree_manifest)

    def test_subtree(self):
        self.assertEqual(
            manifest.subtree_manifest(str(self.tmp / "data/sfx")),
            b"F 1002 x.ogg\nD amb\nF 1004 amb/q.ogg\nD sfx\nF 1003 sfx/y z.ogg\n",
        )
        self.assertEqual(manifest.subtree_manifest(str(self.tmp / "sound")), b"F 1006 w.ogg\nD sub\nF 1007 sub/v.ogg\n")
        self.assertEqual(manifest.subtree_manifest(str(self.tmp / "data/resource.dat")), b"F 1005 resource.dat\n")
        self.assertEqual(manifest.subtree_manifest(str(self.tmp / "nothing")), b"\n")
        # the same bytes as main's derivation, for a sound root of its own and for the data tree's sfx
        for data_root, sound_root in (
            (self.tmp / "data", self.tmp / "sound"),
            (self.tmp / "data", None),
            (self.tmp / "nothing", None),
        ):
            with self.subTest(sound_root=sound_root):
                ours = manifest.subtree_manifest(str(sound_root or data_root / "sfx"))
                self.assertEqual(ours, main_sound_manifest(str(data_root), str(sound_root) if sound_root else None))

    def test_files(self):
        fonts = {
            "Courier New.ttf": str(self.tmp / "fonts/one.ttf"),
            "Trebuchet MS Bold.ttf": str(self.tmp / "fonts/two.ttf"),
        }
        want = b"F 1008 Courier New.ttf\nF 1009 Trebuchet MS Bold.ttf\n"
        self.assertEqual(manifest.files_manifest(fonts), want)
        # main's two writers of the fonts listing: the origin server's and the static kit's
        self.assertEqual("".join(f"F {int(os.stat(p).st_mtime)} {n}\n" for n, p in fonts.items()).encode(), want)
        self.assertEqual(
            ("\n".join(f"F {int(Path(p).stat().st_mtime)} {n}" for n, p in fonts.items()) + "\n").encode(), want
        )
        self.assertEqual(manifest.files_manifest({}), b"\n")

    def test_server_prepare(self):
        # the origin server's listings of a configured tree come from the one writer
        ap = server.options()
        cfg = ap.parse_args(["--data-root", str(self.tmp / "data"), "--access-log", str(self.tmp / "a.log")])
        with contextlib.redirect_stdout(io.StringIO()):
            server.configure(cfg, ap)
            server.prepare(cfg, ap)
        self.assertEqual(cfg.sound_manifest, manifest.subtree_manifest(str(self.tmp / "data/sfx")))
        self.assertEqual(cfg.data_manifest, manifest.tree_manifest(str(self.tmp / "data"), cfg.data_whitelist))
        self.assertEqual(cfg.fonts_manifest, manifest.files_manifest(cfg.font_files))


class DeploymentCommand(unittest.TestCase):
    def run_command(self, *args):
        return subprocess.run(
            [sys.executable, "-m", "plasma_web.deployment", *args], capture_output=True, text=True, timeout=60
        )

    def test_usage(self):
        r = self.run_command()
        self.assertEqual(r.returncode, 2)
        self.assertTrue(r.stderr.startswith("usage:"), r.stderr)
        r = self.run_command("--help")
        self.assertEqual(r.returncode, 0)
        self.assertIn("--init-join-secret", r.stdout)
        with tempfile.TemporaryDirectory() as tmp:
            path = write_deployment(tmp)
            self.assertEqual(self.run_command(str(path), "--init-join-secret", str(path)).returncode, 2)
            self.assertEqual(self.run_command(str(path), str(path)).returncode, 2)

    def test_summary_and_join_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_deployment(tmp)
            r = self.run_command(str(path))
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(json.loads(r.stdout)["name"], "fixture")
            r = self.run_command("--init-join-secret", str(path))
            self.assertEqual((r.returncode, r.stderr.strip()), (1, "the deployment has no gateway.join"))
            missing = str(Path(tmp) / "missing.json")
            for args in ((missing,), ("--init-join-secret", missing)):
                with self.subTest(args=args):
                    r = self.run_command(*args)
                    self.assertEqual(r.returncode, 1)
                    self.assertTrue(r.stderr.startswith("invalid deployment:"), r.stderr)


if __name__ == "__main__":
    unittest.main()
