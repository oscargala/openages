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
"""Licence texts for THIRD_PARTY_NOTICES.md, taken from the pinned sources themselves.

    notices.py extract --out <dir> --find <dir> [--find <dir> ...] --vcpkg-root <vcpkg checkout> [--write-spec]
    notices.py render  --texts <dir> [--out <file>]

extract reads texts.json (beside this script). For each entry it finds the source (an archive by its file name in
the --find directories, or the CPython 2.3.3 tree), checks it against its pin, takes the member (or an excerpt of
it), and writes <dir>/<id>.txt plus <dir>/extracted.json. A source that does not match its pin stops the run.
- lock:   a sources.lock download; its SHA-256 must be the record's.
- port:   a vcpkg port's download; its SHA-512 must appear in the port's files at the lock's vcpkg commit
          (git show in --vcpkg-root), or in Scripts/Ports/<port> for an overlay port.
- tree:   the CPython 2.3.3 tree (a directory named after the record); web/build/tree-hash.py must report the
          record's tree id and manifest.
- emport: an Emscripten port's archive; its SHA-512 must be the one in that port's file inside the pinned
          Emscripten release (the record's archive).
- registry: a file of the vcpkg registry at the lock's vcpkg commit (git show in --vcpkg-root).
- repo:   a file of this repository; when the release archive it was copied from is found, the archive's SHA-256
          must be the recorded one and the member must equal the file.
Texts are decoded as UTF-8, CRLF becomes LF, an excerpt loses its trailing blank lines, and every text ends in one
newline. --write-spec records each text's SHA-256 in texts.json.

render fills THIRD_PARTY_NOTICES.in.md: each line "@@TEXT <id>@@" becomes the text, fenced, after a line that names
its source and SHA-256, between "<!-- text:<id> -->" and "<!-- /text:<id> -->" (check_notices.py reads those);
"@@SHA <id>@@" within a line becomes the SHA-256 of a text that is named but not embedded. Every text must match the
SHA-256 that texts.json records for it.
"""

import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SPEC = HERE / "texts.json"
TEMPLATE = HERE / "THIRD_PARTY_NOTICES.in.md"
LOCK = ROOT / "web/build/sources.lock"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"


class PinError(Exception):
    pass


def read_lock(path=LOCK):
    records = {}
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, *fields = line.split()
        if name in records:
            raise PinError(f"sources.lock: record {name} appears twice")
        records[name] = dict(f.split("=", 1) for f in fields)
    return records


def digest(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find(name, dirs):
    for d in dirs:
        for sub in (Path(d), *[p for p in Path(d).iterdir() if p.is_dir()]):
            p = sub / name
            if p.exists():
                return p
    raise PinError(f"{name}: not found in {', '.join(map(str, dirs))}")


def normalize(raw, entry):
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as e:
        raise PinError(f"{entry['id']}: {entry['member']} is not UTF-8 ({e})")
    text = text.replace("\r\n", "\n")
    if "start" in entry:
        lines = text.split("\n")
        try:
            i = next(n for n, l in enumerate(lines) if l.rstrip() == entry["start"].rstrip())
        except StopIteration:
            raise PinError(f"{entry['id']}: no line {entry['start']!r} in {entry['member']}")
        stop = entry.get("end", entry.get("last"))
        if stop is None:
            j = len(lines)
        else:
            try:
                j = next(n for n in range(i + 1, len(lines)) if lines[n].rstrip() == stop.rstrip())
            except StopIteration:
                raise PinError(f"{entry['id']}: no line {stop!r} after {entry['start']!r} in {entry['member']}")
            if "last" in entry:
                j += 1
        text = "\n".join(lines[i:j])
    return text.rstrip("\n").rstrip() + "\n" if "start" in entry else text.rstrip("\n") + "\n"


class Sources:
    def __init__(self, dirs, vcpkg_root):
        self.dirs = [Path(d) for d in dirs]
        self.vcpkg_root = Path(vcpkg_root) if vcpkg_root else None
        self.lock = read_lock()
        self.verified = {}
        self.members = {}

    def lock_file(self, record):
        rec = self.lock.get(record)
        if not rec or "sha256" not in rec:
            raise PinError(f"sources.lock has no download record {record}")
        path = find(rec.get("file") or rec["url"].rsplit("/", 1)[1], self.dirs)
        key = ("lock", str(path))
        if key not in self.verified:
            got = digest(path, "sha256")
            if got != rec["sha256"]:
                raise PinError(f"{path}: SHA-256 {got} is not sources.lock {record}'s {rec['sha256']}")
            self.verified[key] = f"SHA-256 {got} = sources.lock {record}"
        return path, self.verified[key]

    def port_files(self, port, overlay):
        if overlay:
            d = ROOT / "Scripts/Ports" / port
            return {p.name: p.read_text(errors="replace") for p in d.iterdir() if p.is_file()}, "Scripts/Ports"
        if not self.vcpkg_root:
            raise PinError("--vcpkg-root is needed for vcpkg ports")
        commit = self.lock["vcpkg"]["commit"]
        names = subprocess.run(
            ["git", "-C", str(self.vcpkg_root), "ls-tree", "--name-only", f"{commit}:ports/{port}"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.split()
        files = {
            n: subprocess.run(
                ["git", "-C", str(self.vcpkg_root), "show", f"{commit}:ports/{port}/{n}"],
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            for n in names
        }
        return files, f"vcpkg {commit[:12]}"

    def port_archive(self, entry):
        path = find(entry["archive"], self.dirs)
        key = ("port", str(path))
        if key not in self.verified:
            got = digest(path, "sha512")
            files, where = self.port_files(entry["port"], entry.get("overlay", False))
            hits = [n for n, t in files.items() if got in t]
            if not hits:
                raise PinError(f"{path}: SHA-512 {got[:16]}... is not in port {entry['port']} ({where})")
            self.verified[key] = f"SHA-512 in port {entry['port']}/{hits[0]} ({where})"
        return path, self.verified[key]

    def emport_archive(self, entry):
        tar, how = self.lock_file(entry["record"])
        portfile = self.member(tar, entry["portfile"]).decode()
        m = re.search(r"^SHA512 = '([0-9a-f]{128})'", portfile, re.M)
        if not m:
            raise PinError(f"{entry['portfile']}: no SHA512")
        path = find(entry["archive"], self.dirs)
        got = digest(path, "sha512")
        if got != m.group(1):
            raise PinError(f"{path}: SHA-512 is not the one in {entry['portfile']}")
        return path, f"SHA-512 = {entry['portfile']} of the release ({how})"

    def member(self, archive, name):
        key = (str(archive), name)
        if key in self.members:
            return self.members[key]
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as z:
                data = z.read(name)
        else:
            wanted = {n for (a, n) in self.wanted if a == str(archive)} | {name}
            with tarfile.open(archive, "r:*") as t:
                for m in t:
                    if m.name in wanted and m.isfile():
                        self.members[(str(archive), m.name)] = t.extractfile(m).read()
                        if all((str(archive), w) in self.members for w in wanted):
                            break
            if key not in self.members:
                raise PinError(f"{archive}: no member {name}")
            data = self.members[key]
        self.members[key] = data
        return data

    def tree(self, entry):
        rec = self.lock[entry["record"]]
        d = find(entry["record"], self.dirs)
        r = subprocess.run(
            [
                sys.executable,
                "-I",
                str(ROOT / "web/build/tree-hash.py"),
                str(d),
                "--expect-tree",
                rec["tree"],
                "--expect-manifest",
                rec["manifest-sha256"],
            ],
            capture_output=True,
            text=True,
        )
        if r.returncode:
            raise PinError(f"{d}: not the pinned tree of {entry['record']}: {r.stdout}{r.stderr}")
        return d, f"tree {rec['tree'][:12]} and manifest = sources.lock {entry['record']}"

    def text(self, entry):
        kind = entry["kind"]
        if kind == "lock":
            path, how = self.lock_file(entry["record"])
            raw = self.member(path, entry["member"])
        elif kind == "port":
            path, how = self.port_archive(entry)
            raw = self.member(path, entry["member"])
        elif kind == "emport":
            path, how = self.emport_archive(entry)
            raw = self.member(path, entry["member"])
        elif kind == "tree":
            path, how = self.tree(entry)
            raw = (path / entry["member"]).read_bytes()
            if "repo_copy" in entry and (ROOT / entry["repo_copy"]).read_bytes() != raw:
                raise PinError(f"{entry['repo_copy']} is not {entry['record']}'s {entry['member']}")
        elif kind == "registry":
            if not self.vcpkg_root:
                raise PinError("--vcpkg-root is needed for vcpkg registry files")
            commit = self.lock["vcpkg"]["commit"]
            raw = subprocess.run(
                ["git", "-C", str(self.vcpkg_root), "show", f"{commit}:{entry['member']}"],
                check=True,
                capture_output=True,
            ).stdout
            path, how = f"vcpkg {commit[:12]}", f"vcpkg registry at sources.lock vcpkg {commit[:12]}"
        elif kind == "repo":
            path = ROOT / entry["path"]
            raw = path.read_bytes()
            how = "file of this repository"
            try:
                archive = find(entry["archive"], self.dirs)
            except PinError:
                archive = None
            if archive is not None:
                got = digest(archive, "sha256")
                if got != entry["archive_sha256"]:
                    raise PinError(f"{archive}: SHA-256 {got} is not the recorded {entry['archive_sha256']}")
                if self.member(archive, entry["member"]) != raw:
                    raise PinError(f"{entry['path']} is not {entry['member']} of {archive.name}")
                how += f", equal to {entry['member']} of {archive.name} (SHA-256 {got[:12]}...)"
        else:
            raise PinError(f"{entry['id']}: unknown kind {kind}")
        return normalize(raw, entry), str(path), how


def cmd_extract(a):
    spec = json.loads(SPEC.read_text())
    src = Sources(a.find, a.vcpkg_root)
    # Members wanted per archive, so a large archive (the Emscripten release) is read once.
    src.wanted = set()
    for e in spec["texts"]:
        try:
            if e["kind"] in ("lock", "emport"):
                rec = src.lock[e["record"]]
                tar = str(find(rec.get("file") or rec["url"].rsplit("/", 1)[1], src.dirs))
                src.wanted.add((tar, e["member"] if e["kind"] == "lock" else e["portfile"]))
        except (PinError, KeyError):
            pass
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    report = []
    for e in spec["texts"]:
        try:
            text, path, how = src.text(e)
        except (PinError, subprocess.CalledProcessError, OSError) as err:
            print(f"FAIL {e['id']}: {err}", file=sys.stderr)
            return 1
        (out / f"{e['id']}.txt").write_text(text)
        sha = hashlib.sha256(text.encode()).hexdigest()
        report.append(
            {
                "id": e["id"],
                "sha256": sha,
                "bytes": len(text.encode()),
                "source": path,
                "pin": how,
                "member": e["member"] if e["kind"] != "repo" else e["path"],
            }
        )
        if a.write_spec:
            e["sha256"] = sha
        print(f"ok   {e['id']:20} {sha[:16]} {len(text.encode()):6} B  {how}")
    (out / "extracted.json").write_text(json.dumps(report, indent=1) + "\n")
    if a.write_spec:
        SPEC.write_text(json.dumps(spec, indent=2) + "\n")
    print(f"{len(report)} texts verified and written to {out}")
    return 0


def fence_for(text):
    n = 4
    while re.search(rf"^~{{{n},}}", text, re.M):
        n += 1
    return "~" * n


def source_line(e, sha):
    where = f"`{e['member']}`"
    if "start" in e:
        where += f', the part from the line "{e["start"].strip()}"'
    return f"From {where} (SHA-256 `{sha}`):"


def load_text(texts_dir, e):
    text = (Path(texts_dir) / f"{e['id']}.txt").read_text()
    sha = hashlib.sha256(text.encode()).hexdigest()
    if sha != e.get("sha256"):
        raise PinError(f"{e['id']}: extracted text {sha} differs from texts.json's {e.get('sha256')}")
    return text, sha


def render(template_text, texts_dir, spec):
    by_id = {e["id"]: e for e in spec["texts"]}
    out = []
    for line in template_text.split("\n"):
        # "@@SHA <id>@@" inside a line: the SHA-256 of a text that is named but not embedded.
        line = re.sub(r"@@SHA ([a-z0-9-]+)@@", lambda m: load_text(texts_dir, by_id[m.group(1)])[1], line)
        m = re.fullmatch(r"@@TEXT ([a-z0-9-]+)@@", line)
        if not m:
            if "@@" in line:
                raise PinError(f"template: unknown placeholder in {line!r}")
            out.append(line)
            continue
        e = by_id[m.group(1)]
        if e.get("embed") is False:
            raise PinError(f"{e['id']}: texts.json marks it as not embedded; use @@SHA {e['id']}@@")
        text, sha = load_text(texts_dir, e)
        f = fence_for(text)
        out += [
            f"<!-- text:{e['id']} -->",
            source_line(e, sha),
            "",
            f + "text",
            text.rstrip("\n"),
            f,
            f"<!-- /text:{e['id']} -->",
        ]
    return "\n".join(out)


def cmd_render(a):
    spec = json.loads(SPEC.read_text())
    try:
        text = render(TEMPLATE.read_text(), a.texts, spec)
    except (PinError, KeyError, OSError) as err:
        print(f"FAIL: {err}", file=sys.stderr)
        return 1
    Path(a.out).write_text(text)
    print(f"wrote {a.out} ({len(text.encode())} bytes)")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    x = sub.add_parser("extract")
    x.add_argument("--out", required=True)
    x.add_argument("--find", action="append", required=True, help="a directory holding pinned archives or trees")
    x.add_argument("--vcpkg-root", help="a vcpkg git checkout that has the lock's vcpkg commit")
    x.add_argument("--write-spec", action="store_true")
    r = sub.add_parser("render")
    r.add_argument("--texts", required=True)
    r.add_argument("--out", default=str(NOTICES))
    a = ap.parse_args(argv)
    return cmd_extract(a) if a.cmd == "extract" else cmd_render(a)


if __name__ == "__main__":
    sys.exit(main())
