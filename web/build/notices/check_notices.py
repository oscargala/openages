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
"""Cross-checks THIRD_PARTY_NOTICES.md against what the browser build pins and links.

    check_notices.py [--notices F] [--lock F] [--manifest F] [--texts DIR] [--installed DIR ...]
                     [--vcpkg-root DIR] [--link py3=<build.ninja>] [--link py2=<build.ninja>]
                     [--syslibs F] [--deps F] [--json F]

Always:
- the summary table parses, every row links to a heading of the file, and its builds are known words;
- sources.lock: every record is named by a row, every record a row names exists, and a record's version is in the
  row's version;
- vcpkg.json: every dependency, feature dependency and override is named by a row as a port; "overlay port" names
  exactly the ports in Scripts/Ports; any other port a row names is in vcpkg.json or in an install tree (--installed);
- licence texts: every block's text has the SHA-256 its source line states and texts.json records; every text
  texts.json marks as embedded appears once, and every other one's SHA-256 is named; every browser or kit row's
  section holds a text, and every text sits in the section of a row;
- every repository path the file names exists, every font in web/deploy/fonts is named by a kit row, and the credits
  the FreeType and IJG licences ask for are present.
--texts DIR     the file is exactly the render of THIRD_PARTY_NOTICES.in.md from those texts (notices.py render).
--installed     a vcpkg install tree (vcpkg_installed): every package in it is named, with its version in the row,
                and a target package's row says browser, a host package's row says build.
--vcpkg-root    a vcpkg checkout that has the lock's commit: each port's version there (or vcpkg.json's override, or
                the overlay port's) is in its row, and the row's licence contains the port's declared licence.
--link          plClient's link statement in a build.ninja: every third-party library and Emscripten port on it maps
                to a row (vcpkg libraries through the install tree's file lists, so --installed is needed), and a
                row linked by one flavour only says which.
--syslibs       the "emcc -v" output of a link with the client's flags: every Emscripten system library maps to a row.
--deps          "ninja -t deps" of a build: every third-party header it compiles maps to a row.
With --link (both flavours), --syslibs and --deps, every browser row must be linked, a system library or a header.
Exit status 0 when every check passes, 1 otherwise.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import notices  # noqa: E402

BUILDS = {"browser", "browser (plClient)", "browser (plClient-py2)", "kit", "native", "build"}
BROWSER = {"browser", "browser (plClient)", "browser (plClient-py2)"}
FLAVOURS = {"py3": "browser (plClient)", "py2": "browser (plClient-py2)"}
TARGET_TRIPLET = "wasm32-emscripten-plasma"
REPO_PREFIXES = ("web/", "Scripts/", "Sources/", "cmake/")

# Libraries on the link line from outside vcpkg's install tree, by file name.
OTHER_LIBS = {
    "libpython3.12.a": ("CPython 3.12.9",),
    "libmpdec.a": ("libmpdec",),
    "libHacl_Hash_SHA2.a": ("HACL*",),
    "libpython2.3.a": ("CPython 2.3.3",),
    "libvpx.a": ("libvpx",),
    "libwebm.a": ("libwebm",),
}
# -l libraries on the link line, which Emscripten provides.
DASH_L = {"openal": ("Emscripten",), "dl": ("musl libc",), "m": ("musl libc",)}
# Emscripten ports (--use-port=<name>).
EM_PORTS = {"emdawnwebgpu": ("Emdawnwebgpu",)}
# Emscripten system libraries, by their name before the first "-" (the variant suffixes).
SYSLIBS = [
    (r"c", ("musl libc", "Emscripten")),
    (r"c\+\+|c\+\+abi|unwind|clang_rt\.builtins", ("LLVM C++ runtime libraries",)),
    (r"dlmalloc", ("dlmalloc",)),
    (r"GL|al|html5|stubs|sockets|wasmfs|crt1\w*|crtbegin", ("Emscripten",)),
    (r"emdawnwebgpu", ("Emdawnwebgpu",)),
]
# Compiled headers from outside vcpkg's install tree.
OTHER_HEADERS = [
    (r"/python/include/python3\.12/", ("CPython 3.12.9",)),
    (r"/python2/include/python2\.3/", ("CPython 2.3.3",)),
    (r"/movies/include/vpx/", ("libvpx",)),
    (r"/movies/include/webm/", ("libwebm",)),
]
# The credits the licences ask the documentation to carry.
CREDITS = [
    (
        "FreeType License",
        r"Portions of this software are copyright © \d{4} The FreeType Project \(https://freetype\.org\)\. "
        r"All rights reserved\.",
    ),
    ("IJG licence", r"(?i)this software is based in part on the work of the Independent JPEG Group"),
]


class Row:
    def __init__(self, line_no, cells):
        self.line_no = line_no
        m = re.fullmatch(r"\[(.+)\]\(#([^)]+)\)", cells[0])
        if not m:
            raise ValueError(f"line {line_no}: component {cells[0]!r} is not a [name](#anchor) link")
        self.name, self.anchor = m.groups()
        self.version, self.licence = cells[1], cells[2]
        self.builds = [b.strip() for b in re.split(r",(?![^(]*\))", cells[3])]
        self.locks, self.ports, self.files = [], [], []
        for m in re.finditer(r"(overlay port|port|file)?\s*`([^`]+)`", cells[4]):
            kind, name = m.group(1), m.group(2)
            if kind == "file":
                self.files.append(name)
            elif kind:
                self.ports.append((name, kind == "overlay port"))
            else:
                self.locks.append(name)

    def browser(self):
        return bool(BROWSER & set(self.builds))


def slug(heading):
    return re.sub(r"[^\w\- ]", "", heading.strip().lower()).replace(" ", "-")


BLOCK = re.compile(
    r"<!-- text:(?P<id>[a-z0-9-]+) -->\nFrom (?P<src>[^\n]*) \(SHA-256 `(?P<sha>[0-9a-f]{64})`\):\n\n"
    r"(?P<fence>~{4,})text\n(?P<body>.*?)\n(?P=fence)\n<!-- /text:(?P=id) -->",
    re.S,
)


class Check:
    def __init__(self, a):
        self.a = a
        self.errors = []
        self.notes = []
        self.text = Path(a.notices).read_text()
        self.spec = json.loads(Path(a.spec).read_text())
        self.lock = notices.read_lock(a.lock)
        self.manifest = json.loads(Path(a.manifest).read_text())
        self.evidence = {}  # row name -> set of how it is linked or compiled
        self.linked = {}  # row name -> flavours that link it

    def fail(self, msg):
        self.errors.append(msg)

    # --- parsing -------------------------------------------------------------------------------------------------
    def parse(self):
        self.blocks = []
        plain = self.text
        for m in BLOCK.finditer(self.text):
            line = self.text.count("\n", 0, m.start()) + 1
            self.blocks.append(dict(m.groupdict(), line=line))
            # Blank the block out (keeping its line count) so headings and paths are read from the prose only.
            plain = plain.replace(m.group(0), "\n" * m.group(0).count("\n"), 1)
        if self.text.count("<!-- text:") != len(self.blocks) or self.text.count("<!-- /text:") != len(self.blocks):
            self.fail("a <!-- text: --> block is malformed (its markers, source line or fence do not parse)")
        self.plain = plain
        self.headings = []
        seen = {}
        for i, line in enumerate(plain.split("\n"), 1):
            m = re.match(r"(#{1,6}) (.+)$", line)
            if m:
                s = slug(m.group(2))
                n = seen.get(s, 0)
                seen[s] = n + 1
                self.headings.append((i, len(m.group(1)), s if n == 0 else f"{s}-{n}"))
        lines = self.text.split("\n")
        try:
            start = lines.index("<!-- summary -->")
            end = lines.index("<!-- /summary -->")
        except ValueError:
            self.fail("no <!-- summary --> ... <!-- /summary --> table")
            self.rows = []
            return
        self.rows = []
        for i in range(start + 3, end):  # after the header and the separator row
            cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
            if len(cells) != 5:
                self.fail(f"line {i + 1}: a summary row needs 5 cells, has {len(cells)}")
                continue
            try:
                self.rows.append(Row(i + 1, cells))
            except ValueError as err:
                self.fail(str(err))
        names = [r.name for r in self.rows]
        for n in {n for n in names if names.count(n) > 1}:
            self.fail(f"row {n!r} appears twice")
        self.by_name = {r.name: r for r in self.rows}
        self.by_port = {}
        for r in self.rows:
            for p, overlay in r.ports:
                self.by_port.setdefault(p, []).append(r)

    # --- the checks ------------------------------------------------------------------------------------------------
    def rows_and_sections(self):
        slugs = {s: (line, level) for line, level, s in self.headings}
        for r in self.rows:
            for b in r.builds:
                if b not in BUILDS:
                    self.fail(f"{r.name}: unknown build {b!r} (known: {', '.join(sorted(BUILDS))})")
            if r.anchor not in slugs:
                self.fail(f"{r.name}: link #{r.anchor} names no heading of the file")
        self.section_of = {}
        for idx, (line, level, s) in enumerate(self.headings):
            end = next((l for l, lv, _ in self.headings[idx + 1 :] if lv <= level), 10**9)
            self.section_of[s] = [b["id"] for b in self.blocks if line < b["line"] < end]
        anchored = set()
        for r in self.rows:
            ids = self.section_of.get(r.anchor, [])
            anchored |= set(ids)
            if (r.browser() or "kit" in r.builds) and not ids:
                self.fail(f"{r.name}: its section #{r.anchor} holds no licence text")
        for b in self.blocks:
            if b["id"] not in anchored:
                self.fail(f"text {b['id']} (line {b['line']}) is in no row's section")

    def texts(self):
        spec = {e["id"]: e for e in self.spec["texts"]}
        count = {}
        for b in self.blocks:
            count[b["id"]] = count.get(b["id"], 0) + 1
            e = spec.get(b["id"])
            if e is None:
                self.fail(f"text {b['id']} is not in texts.json")
                continue
            sha = hashlib.sha256((b["body"] + "\n").encode()).hexdigest()
            if sha != b["sha"]:
                self.fail(f"text {b['id']}: its text has SHA-256 {sha}, its source line says {b['sha']}")
            if sha != e.get("sha256"):
                self.fail(f"text {b['id']}: its text has SHA-256 {sha}, texts.json records {e.get('sha256')}")
            if not b["src"].startswith(f"`{e['member']}`"):
                self.fail(f"text {b['id']}: the source line does not name {e['member']}")
            if e.get("embed") is False:
                self.fail(f"text {b['id']}: texts.json says it is named, not embedded")
        for i, e in spec.items():
            if e.get("embed") is False:
                if e.get("sha256", "-") not in self.plain:
                    self.fail(f"text {i}: its SHA-256 is not named in the file")
            elif count.get(i, 0) != 1:
                self.fail(f"text {i}: appears {count.get(i, 0)} times, not once")
        for name, rx in CREDITS:
            if not re.search(rx.replace(" ", r"\s+"), self.plain):  # the prose may wrap the sentence
                self.fail(f"the credit the {name} asks for is missing")

    def lock_records(self):
        named = set()
        for r in self.rows:
            for rec in r.locks:
                named.add(rec)
                if rec not in self.lock:
                    self.fail(f"{r.name}: sources.lock has no record {rec}")
                elif "version" in self.lock[rec] and self.lock[rec]["version"] not in r.version:
                    self.fail(
                        f"{r.name}: sources.lock {rec} is version {self.lock[rec]['version']}, "
                        f"the row says {r.version!r}"
                    )
        for rec in self.lock:
            if rec not in named:
                self.fail(f"sources.lock record {rec} is named by no row")

    def manifest_names(self):
        names = set()
        for d in self.manifest.get("dependencies", []):
            names.add(d if isinstance(d, str) else d["name"])
        for f in self.manifest.get("features", {}).values():
            for d in f.get("dependencies", []):
                names.add(d if isinstance(d, str) else d["name"])
        for o in self.manifest.get("overrides", []):
            names.add(o["name"])
        return names

    def ports(self):
        names = self.manifest_names()
        overlays = {p.name for p in (ROOT / "Scripts/Ports").iterdir() if p.is_dir()}
        self.installed = {}  # (port, triplet) -> version
        self.owner = {}  # installed path (from the triplet on) -> port
        for d in self.a.installed or []:
            for lst in sorted((Path(d) / "vcpkg/info").glob("*.list")):
                port, version, triplet = lst.stem.split("_")
                self.installed[(port, triplet)] = version
                for f in lst.read_text().split("\n"):
                    if f and not f.endswith("/"):
                        self.owner[f] = port
        installed_ports = {p for p, _ in self.installed}
        for n in sorted(names):
            if n not in self.by_port:
                self.fail(f"vcpkg.json names port {n}, which no row names")
        for o in sorted(overlays):
            if not any(p == o and ov for r in self.rows for p, ov in r.ports):
                self.fail(f"overlay port Scripts/Ports/{o} is named by no row as an overlay port")
        for r in self.rows:
            for p, overlay in r.ports:
                if overlay and p not in overlays:
                    self.fail(f"{r.name}: there is no overlay port Scripts/Ports/{p}")
                elif not overlay and p in overlays:
                    self.fail(f'{r.name}: {p} is an overlay port; name it "overlay port"')
                elif not overlay and p not in names and p not in installed_ports:
                    how = "" if self.a.installed else " (pass --installed for ports that dependencies pull in)"
                    self.fail(f"{r.name}: port {p} is neither in vcpkg.json nor installed{how}")
        for (p, triplet), version in sorted(self.installed.items()):
            rows = self.by_port.get(p)
            if not rows:
                self.fail(f"installed package {p}:{triplet} {version} is named by no row")
                continue
            for r in rows:
                if version not in r.version:
                    self.fail(f"{r.name}: installed {p} is version {version}, the row says {r.version!r}")
                want = "browser" if triplet == TARGET_TRIPLET else "build"
                if want == "browser" and not r.browser():
                    self.fail(f"{r.name}: {p} is installed for the browser build, the row's builds lack browser")
                if want == "build" and "build" not in r.builds:
                    self.fail(f"{r.name}: {p} is a host tool package ({triplet}), the row's builds lack build")

    def registry(self):
        root = self.a.vcpkg_root
        commit = self.lock["vcpkg"]["commit"]
        overrides = {o["name"]: re.sub(r"#\d+$", "", o["version"]) for o in self.manifest.get("overrides", [])}
        for r in self.rows:
            for p, overlay in r.ports:
                if overlay:
                    meta = json.loads((ROOT / "Scripts/Ports" / p / "vcpkg.json").read_text())
                else:
                    try:
                        meta = json.loads(
                            subprocess.run(
                                ["git", "-C", root, "show", f"{commit}:ports/{p}/vcpkg.json"],
                                check=True,
                                capture_output=True,
                                text=True,
                            ).stdout
                        )
                    except subprocess.CalledProcessError:
                        self.fail(f"{r.name}: port {p} does not exist at vcpkg {commit[:12]}")
                        continue
                version = next(
                    meta[k] for k in ("version", "version-semver", "version-date", "version-string") if k in meta
                )
                if not overlay and p in overrides:
                    version = overrides[p]
                if version not in r.version:
                    self.fail(f"{r.name}: port {p} is version {version}, the row says {r.version!r}")
                if meta.get("license") and meta["license"] not in r.licence:
                    self.fail(f"{r.name}: port {p} declares licence {meta['license']!r}, the row says {r.licence!r}")

    def add(self, rows, how, flavour=None):
        for n in rows:
            if n not in self.by_name:
                self.fail(f"{how} belongs to {n!r}, which has no row")
                continue
            self.evidence.setdefault(n, set()).add(how.split(":")[0])
            if flavour:
                self.linked.setdefault(n, set()).add(flavour)

    def rows_of_port(self, port, what):
        rows = [r.name for r in self.by_port.get(port, [])]
        if not rows:
            self.fail(f"{what} is installed by port {port}, which no row names")
        return rows

    def link(self):
        if not self.a.installed:
            self.fail("--link needs --installed, to tell which package installed each library")
            return
        for spec in self.a.link:
            flavour, _, path = spec.partition("=")
            if flavour not in FLAVOURS:
                self.fail(f"--link {spec}: the flavour is py3 or py2")
                continue
            target, vars_ = link_statement(path)
            for tok in [t.strip("\"'") for t in vars_.get("LINK_LIBRARIES", "").split()]:
                if tok.endswith(".a"):
                    if not tok.startswith("/"):
                        continue  # one of the engine's own libraries, relative to the build directory
                    if "/vcpkg_installed/" in tok:
                        f = tok.split("/vcpkg_installed/", 1)[1]
                        port = self.owner.get(f)
                        if port is None:
                            self.fail(f"{flavour}: {f} on the link line is in no installed package's file list")
                            continue
                        self.add(self.rows_of_port(port, f), f"linked:{f}", flavour)
                    elif Path(tok).name in OTHER_LIBS:
                        self.add(OTHER_LIBS[Path(tok).name], f"linked:{Path(tok).name}", flavour)
                    else:
                        self.fail(f"{flavour}: library {tok} on the link line maps to no row")
                elif tok.startswith("-l"):
                    if tok[2:] in DASH_L:
                        self.add(DASH_L[tok[2:]], f"linked:{tok}", flavour)
                    else:
                        self.fail(f"{flavour}: {tok} on the link line maps to no row")
            for tok in [t.strip("\"'") for t in vars_.get("LINK_FLAGS", "").split()]:
                m = re.match(r"--use-port=([\w-]+)", tok)
                if m:
                    if m.group(1) in EM_PORTS:
                        self.add(EM_PORTS[m.group(1)], f"linked:{tok}", flavour)
                    else:
                        self.fail(f"{flavour}: Emscripten port {m.group(1)} maps to no row")
                elif re.match(r"-sUSE_[A-Z0-9_]+(=\w+)?$", tok) or tok.startswith("-l"):
                    self.fail(f"{flavour}: {tok} in the link flags maps to no row")

    def syslibs(self):
        out = Path(self.a.syslibs).read_text()
        cmd = next((line for line in out.split("\n") if "wasm-ld" in line), None)
        if cmd is None:
            self.fail(f"{self.a.syslibs}: no wasm-ld command")
            return
        for tok in cmd.split():
            if tok.startswith("-l"):
                name = tok[2:]
            elif "/cache/sysroot/" in tok and tok.endswith((".a", ".o")):
                name = re.sub(r"^lib", "", Path(tok).stem) if tok.endswith(".a") else Path(tok).stem
            else:
                continue
            base = name.split("-")[0]
            hit = next((rows for rx, rows in SYSLIBS if re.fullmatch(rx, base)), None)
            if hit is None:
                self.fail(f"system library {name} maps to no row")
            else:
                self.add(hit, f"system:{name}")

    def deps(self):
        for line in Path(self.a.deps).read_text().split("\n"):
            if not line.startswith(" "):
                continue
            path = line.strip()
            if "/vcpkg_installed/" in path:
                f = path.split("/vcpkg_installed/", 1)[1]
                port = self.owner.get(f)
                if port is None:
                    self.fail(f"header {f} is in no installed package's file list")
                    continue
                self.add(self.rows_of_port(port, f), f"header:{port}")
                continue
            hit = next((rows for rx, rows in OTHER_HEADERS if re.search(rx, path)), None)
            if hit:
                self.add(hit, f"header:{hit[0]}")

    def flavours(self):
        given = {s.partition("=")[0] for s in self.a.link or []}
        for n, flv in self.linked.items():
            r = self.by_name[n]
            if given == {"py3", "py2"}:
                if flv == given and "browser" not in r.builds:
                    self.fail(f"{n}: both flavours link it, the row's builds lack browser")
                elif flv != given:
                    want = FLAVOURS[next(iter(flv))]
                    if want not in r.builds or "browser" in r.builds:
                        self.fail(f"{n}: only {want[9:-1]} links it, the row's builds should say {want!r}")
            elif not (r.browser()):
                self.fail(f"{n}: the client links it, the row's builds lack browser")
        if given == {"py3", "py2"} and self.a.syslibs and self.a.deps:
            for r in self.rows:
                if r.browser() and r.name not in self.evidence:
                    self.fail(f"{r.name}: a browser row, but neither linked, a system library nor a compiled header")
                if "browser (plClient-py2)" in r.builds and "py2" not in self.linked.get(r.name, set()):
                    self.fail(f"{r.name}: the row says plClient-py2, which does not link it")
                if "browser (plClient)" in r.builds and "py3" not in self.linked.get(r.name, set()):
                    self.fail(f"{r.name}: the row says plClient, which does not link it")
        else:
            self.notes.append("not every input given: the check that each browser row is linked or compiled is skipped")

    def paths(self):
        for m in re.finditer(r"`([^`\s]+)`", self.plain):
            p = m.group(1)
            if p.startswith(REPO_PREFIXES):
                hits = list(ROOT.glob(p)) if "*" in p else [ROOT / p] if (ROOT / p).exists() else []
                if not hits:
                    self.fail(f"the file names {p}, which does not exist")
        kit_files = set()
        for r in self.rows:
            for f in r.files:
                if not (ROOT / f).is_file():
                    self.fail(f"{r.name}: file {f} does not exist")
                if "kit" in r.builds:
                    kit_files.add(f)
        for ttf in sorted((ROOT / "web/deploy/fonts").glob("*.ttf")):
            rel = str(ttf.relative_to(ROOT))
            if rel not in kit_files:
                self.fail(f"font {rel} is named by no kit row")

    def render(self):
        rendered = notices.render(notices.TEMPLATE.read_text(), self.a.texts, self.spec)
        if rendered != self.text:
            a, b = rendered.split("\n"), self.text.split("\n")
            i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            self.fail(
                f"{self.a.notices} is not the render of {notices.TEMPLATE.name} (first difference at line {i + 1})"
            )

    def run(self):
        self.parse()
        steps = [self.rows_and_sections, self.texts, self.lock_records, self.ports, self.paths]
        if self.a.texts:
            steps.append(self.render)
        if self.a.vcpkg_root:
            steps.append(self.registry)
        if self.a.link:
            steps.append(self.link)
        if self.a.syslibs:
            steps.append(self.syslibs)
        if self.a.deps:
            if not self.a.installed:
                self.fail("--deps needs --installed")
            else:
                steps.append(self.deps)
        if self.a.link or self.a.syslibs or self.a.deps:
            steps.append(self.flavours)
        for step in steps:
            try:
                step()
            except (OSError, ValueError, KeyError, notices.PinError) as err:
                self.fail(f"{step.__name__}: {err}")
        return self.errors


def link_statement(path):
    lines = Path(path).read_text().split("\n")
    for i, line in enumerate(lines):
        m = re.match(r"build (\S*plClient[\w-]*\.js): CXX_EXECUTABLE_LINKER", line)
        if m:
            vars_ = {}
            for v in lines[i + 1 :]:
                if not v.startswith("  "):
                    break
                k, _, val = v.strip().partition(" = ")
                vars_[k] = val
            return m.group(1), vars_
    raise ValueError(f"{path}: no plClient link statement")


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--notices", default=str(ROOT / "THIRD_PARTY_NOTICES.md"))
    ap.add_argument("--lock", default=str(ROOT / "web/build/sources.lock"))
    ap.add_argument("--manifest", default=str(ROOT / "vcpkg.json"))
    ap.add_argument("--spec", default=str(HERE / "texts.json"))
    ap.add_argument("--texts")
    ap.add_argument("--installed", action="append")
    ap.add_argument("--vcpkg-root")
    ap.add_argument("--link", action="append")
    ap.add_argument("--syslibs")
    ap.add_argument("--deps")
    ap.add_argument("--json")
    return ap.parse_args(argv)


def main(argv=None):
    a = parse_args(argv)
    c = Check(a)
    errors = c.run()
    for e in errors:
        print(f"FAIL: {e}")
    for n in c.notes:
        print(f"note: {n}")
    summary = {
        "rows": len(c.rows),
        "texts": len(c.blocks),
        "lock_records": len(c.lock),
        "installed_packages": len(getattr(c, "installed", {})),
        "evidenced_rows": len(c.evidence),
        "errors": len(errors),
    }
    if a.json:
        Path(a.json).write_text(
            json.dumps(
                {
                    "summary": summary,
                    "errors": errors,
                    "notes": c.notes,
                    "evidence": {k: sorted(v) for k, v in sorted(c.evidence.items())},
                    "linked": {k: sorted(v) for k, v in sorted(c.linked.items())},
                },
                indent=1,
            )
            + "\n"
        )
    print(("FAIL" if errors else "PASS") + ": " + ", ".join(f"{k} {v}" for k, v in summary.items()))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
