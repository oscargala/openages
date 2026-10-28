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
"""The third-party source bundle of a browser release, assembled from the pinned sources and checked against
web/build/sources.lock.

    source_bundle.py assemble --out <dir> --find <dir> [--find <dir> ...] --vcpkg-root <vcpkg checkout>
                              --cypython-tree <dir> [--link]
    source_bundle.py check <dir> [--vcpkg-root <vcpkg checkout>] [--installed <vcpkg_installed>] [--json <file>]

The browser client (plClient, plClient-py2) is GPL version 3 object code. Its Corresponding Source is this repository
at the release's tag plus the source of every third-party component compiled or linked into it; COPYING.txt's
section 7 permissions name OpenSSL's and the IJG JPEG library's source explicitly. The repository holds only the pins
(web/build/sources.lock and the vcpkg commit it names); this bundle holds the sources themselves, so that a release
can offer them next to its tag. Every sources.lock record is in one class (LOCK_CLASSES below), and a record in no
class stops both commands, so a new pin needs a decision here:

  download   the pinned archive itself, under lock/, with the record's SHA-256: CPython 3.12.9, libvpx, libwebm, and
             the Emscripten 6.0.12 release, whose system/ and src/ trees are the source of the C library, the C++
             runtime, compiler-rt, dlmalloc and the JavaScript library code that the client links
  tree       the CPython 2.3.3 tree of the py2 flavour, under cypython-2.3.3/, with the record's git tree id and
             manifest (web/build/tree-hash.py)
  repo-file  a file of this repository that pins that tree: the proof that links the record's commit, path and tree
             (web/build/commit-proof.py checks it) and OpenUru's delta, with the records' SHA-256s; beside them
             LICENSE-PSF.txt, which must equal the tree's LICENSE
  derived    Cyan's 2011 release of the tree (the H-uru commit): python2/build.sh fetch rebuilds the pinned tree from
             it and the delta, so the tree above is that source in the form the build uses
  ports      the vcpkg registry commit: for each library port the build links (web/build/notices/texts.json, kind
             port), its download under vcpkg-ports/downloads/ (its SHA-512 is in the port's own files) and the port's
             directory at that commit (portfile and patches) under vcpkg-ports/ports/, or Scripts/Ports for an
             overlay port under vcpkg-ports/overlay/; Emdawnwebgpu's archive (texts.json, kind emport) under
             emscripten-ports/, with the SHA-512 the pinned Emscripten release names
  tool       a build tool used unmodified, which the GPL leaves out of the Corresponding Source (section 1): emsdk,
             Node.js, CMake, Ninja, the vcpkg tool and the GNU autotools that regenerate Speex's build scripts

assemble finds each source in the --find directories (an archive by its file name), checks it against its pin and
copies it (--link: hard-links it when it is on the same volume; the bundle is then never edited in place) into a new
directory, then writes sources.lock and THIRD_PARTY_NOTICES.md (the repository's), MANIFEST.json, README.md and
SHA256SUMS. check reads only the bundle and the repository's sources.lock: the bundle's copy must equal it, every
pinned source must be present with equal digests, the CPython 2.3.3 proof must hold, every port archive's SHA-512 must
be in its bundled port files, every file must be in SHA256SUMS with its digest and no other file may be there. With
--vcpkg-root it also checks each registry port directory against the vcpkg commit, and with --installed that every
package of the target triplet's install tree has its port in the bundle.
"""

import argparse
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import warnings
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import notices  # noqa: E402  (read_lock, digest, find, PinError: the pin checks of the notices generator)

SCHEMA = 1
TRIPLET = "wasm32-emscripten-plasma"
LOCK_CLASSES = {
    "emsdk": {"kind": "tool", "why": "the emsdk installer at a commit; it installs the release below"},
    "emsdk-6.0.12-linux-x86_64": {
        "kind": "download",
        "why": "Emscripten 6.0.12: its system/ and src/ trees are the "
        "source of musl, libc++, libc++abi, libunwind, compiler-rt, dlmalloc and the "
        "JavaScript library code linked into the client; its compilers are build tools",
    },
    "emsdk-node-24.19.0-linux-x86_64": {"kind": "tool", "why": "Node.js, which runs the Emscripten compiler"},
    "cmake-4.4.3-linux-x86_64": {"kind": "tool", "why": "CMake, the CI runner's"},
    "ninja-1.13.2-linux-x86_64": {"kind": "tool", "why": "Ninja, the CI runner's"},
    "vcpkg": {"kind": "ports", "why": "the vcpkg registry: the bundled port directories are at this commit"},
    "vcpkg-tool-macos": {"kind": "tool", "why": "the vcpkg tool"},
    "vcpkg-tool-linux-x86_64": {"kind": "tool", "why": "the vcpkg tool"},
    "vcpkg-tool-linux-arm64": {"kind": "tool", "why": "the vcpkg tool"},
    "m4": {"kind": "tool", "why": "GNU M4, which regenerates Speex's build scripts"},
    "autoconf": {"kind": "tool", "why": "GNU Autoconf, which regenerates Speex's build scripts"},
    "automake": {"kind": "tool", "why": "GNU Automake, which regenerates Speex's build scripts"},
    "libtool": {"kind": "tool", "why": "GNU Libtool, which regenerates Speex's build scripts"},
    "autoconf-archive": {"kind": "tool", "why": "the Autoconf Archive, which regenerates Speex's build scripts"},
    "python-3.12.9": {"kind": "download", "why": "CPython 3.12.9, embedded in plClient"},
    "cypython-2.3.3": {
        "kind": "tree",
        "dir": "cypython-2.3.3",
        "why": "CPython 2.3.3 (OpenUru's Cypython-2.3.3), embedded in plClient-py2",
    },
    "cypython-2.3.3-proof": {
        "kind": "repo-file",
        "dir": "cypython-2.3.3",
        "proves": "cypython-2.3.3",
        "why": "links the tree's commit, path and tree id",
    },
    "cypython-2.3.3-cwe": {
        "kind": "derived",
        "of": "cypython-2.3.3",
        "why": "Cyan's 2011 release of the tree; python2/build.sh fetch rebuilds the pinned tree from it and the delta",
    },
    "cypython-2.3.3-ou-delta": {
        "kind": "repo-file",
        "dir": "cypython-2.3.3",
        "why": "OpenUru's changes to Cyan's tree",
    },
    "libvpx": {"kind": "download", "why": "libvpx, the movie decoder"},
    "libwebm": {"kind": "download", "why": "libwebm, the movie container parser"},
}
# Files of this repository that the bundle carries beside the tree: (bundle path, repository path, must equal).
EXTRA_FILES = [
    ("cypython-2.3.3/LICENSE-PSF.txt", "web/build/python2/LICENSE-PSF.txt", "cypython-2.3.3/Cypython-2.3.3/LICENSE")
]
KINDS = {"download", "tree", "repo-file", "derived", "ports", "tool"}


def _tree_hash():
    spec = importlib.util.spec_from_file_location("tree_hash", ROOT / "web/build/tree-hash.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


TREE_HASH = _tree_hash()


def tree_id(path):
    """(git tree id, manifest SHA-256) of a directory, as web/build/tree-hash.py computes them."""
    with warnings.catch_warnings():  # (tree-hash.py reads each file without closing it explicitly)
        warnings.simplefilter("ignore", ResourceWarning)
        ident, manifest = TREE_HASH.scan(str(path))
    return ident, hashlib.sha256(manifest).hexdigest()


class Pins:
    """What the bundle must hold: the lock's records by class, the ports and Emscripten ports the client links."""

    def __init__(self, root=ROOT, lock=None, classes=None, texts=None, extra=None):
        self.root = Path(root)
        self.lock_path = Path(lock) if lock else self.root / "web/build/sources.lock"
        self.lock = notices.read_lock(self.lock_path)
        self.classes = LOCK_CLASSES if classes is None else classes
        texts = json.loads((HERE / "texts.json").read_text())["texts"] if texts is None else texts
        self.ports = sorted({(e["port"], e["archive"], bool(e.get("overlay"))) for e in texts if e["kind"] == "port"})
        self.emports = sorted({(e["archive"], e["record"], e["portfile"]) for e in texts if e["kind"] == "emport"})
        self.extra = EXTRA_FILES if extra is None else extra

    def class_errors(self):
        errors = [
            f"sources.lock record {n} is in no bundle class (source_bundle.py LOCK_CLASSES)"
            for n in self.lock
            if n not in self.classes
        ]
        errors += [f"bundle class {n}: no such sources.lock record" for n in self.classes if n not in self.lock]
        errors += [
            f"bundle class {n}: unknown kind {c['kind']}" for n, c in self.classes.items() if c["kind"] not in KINDS
        ]
        return errors

    def file_name(self, name):
        rec = self.lock[name]
        return rec.get("file") or rec["url"].rsplit("/", 1)[1]

    def path_of(self, name):
        """The bundle path of a record's source, or None for a class that bundles no file of its own."""
        c, rec = self.classes[name], self.lock[name]
        if c["kind"] == "download":
            return f"lock/{self.file_name(name)}"
        if c["kind"] == "tree":
            return f"{c['dir']}/{rec['path'].rstrip('/').rsplit('/', 1)[-1]}"
        if c["kind"] == "repo-file":
            return f"{c['dir']}/{Path(rec['file']).name}"
        return None

    def vcpkg_commit(self):
        return next(self.lock[n]["commit"] for n, c in self.classes.items() if c["kind"] == "ports")

    def port_paths(self, port, archive, overlay):
        return f"vcpkg-ports/downloads/{archive}", f"vcpkg-ports/{'overlay' if overlay else 'ports'}/{port}"


def _place(src, dst, link):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if link:
        try:
            os.link(src, dst)
            return
        except OSError:
            pass
    shutil.copy2(src, dst)


def _port_files(directory):
    return {p.name: p.read_text(errors="replace") for p in Path(directory).iterdir() if p.is_file()}


def _release_member(tar_path, member, cache={}):
    """One member of a (large) release archive, read once per check."""
    key = (str(tar_path), member)
    if key not in cache:
        with tarfile.open(tar_path, "r:*") as t:
            for m in t:
                if m.name == member and m.isfile():
                    cache[key] = t.extractfile(m).read()
                    break
        if key not in cache:
            raise notices.PinError(f"{tar_path}: no member {member}")
    return cache[key]


def _emport_sha512(release_tar, portfile):
    m = re.search(r"^SHA512 = '([0-9a-f]{128})'", _release_member(release_tar, portfile).decode(), re.M)
    if not m:
        raise notices.PinError(f"{portfile}: no SHA512 line")
    return m.group(1)


def _proof_errors(pins, bundle, name):
    """The CPython 2.3.3 proof: the record's commit, path and tree, linked by the bundled proof file."""
    c = pins.classes[name]
    rec = pins.lock[c["proves"]]
    r = subprocess.run(
        [
            sys.executable,
            "-I",
            str(ROOT / "web/build/commit-proof.py"),
            "check",
            str(bundle / pins.path_of(name)),
            "--commit",
            rec["commit"],
            "--path",
            rec["path"],
            "--tree",
            rec["tree"],
        ],
        capture_output=True,
        text=True,
    )
    return [] if r.returncode == 0 else [f"{pins.path_of(name)}: not the proof of {c['proves']}: {r.stderr.strip()}"]


def assemble(pins, out, find, vcpkg_root, cypython_tree, link=False):
    """Writes the bundle into out (a new directory); raises notices.PinError for a source that does not match."""
    errors = pins.class_errors()
    if errors:
        raise notices.PinError("; ".join(errors))
    out = Path(out)
    if out.exists() and any(out.iterdir()):
        raise notices.PinError(f"{out} is not empty")
    out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": SCHEMA,
        "sourcesLock": {"path": "web/build/sources.lock", "sha256": notices.digest(pins.lock_path, "sha256")},
        "vcpkgCommit": pins.vcpkg_commit(),
        "records": {},
        "ports": [],
        "emports": [],
        "extra": [],
    }
    for name in sorted(pins.lock):
        c, rec = pins.classes[name], pins.lock[name]
        path = pins.path_of(name)
        entry = {"kind": c["kind"], "why": c["why"], "path": path}
        if c["kind"] == "download":
            src = notices.find(pins.file_name(name), find)
            got = notices.digest(src, "sha256")
            if got != rec["sha256"]:
                raise notices.PinError(f"{src}: SHA-256 {got} is not sources.lock {name}'s {rec['sha256']}")
            _place(src, out / path, link)
            entry["sha256"] = got
        elif c["kind"] == "tree":
            if tree_id(cypython_tree) != (rec["tree"], rec["manifest-sha256"]):
                raise notices.PinError(f"{cypython_tree}: not the pinned tree of {name}")
            shutil.copytree(cypython_tree, out / path, symlinks=True)
            entry.update(tree=rec["tree"], manifestSha256=rec["manifest-sha256"])
        elif c["kind"] == "repo-file":
            src = pins.root / rec["file"]
            if notices.digest(src, "sha256") != rec["sha256"]:
                raise notices.PinError(f"{rec['file']} does not match sources.lock {name}")
            _place(src, out / path, False)
            entry["sha256"] = rec["sha256"]
        manifest["records"][name] = entry
    for path, src, equal in pins.extra:
        _place(pins.root / src, out / path, False)
        manifest["extra"].append({"path": path, "from": src, "equals": equal})
    commit = pins.vcpkg_commit()
    if not _has_commit(vcpkg_root, commit):
        raise notices.PinError(f"--vcpkg-root {vcpkg_root} does not hold sources.lock's vcpkg commit {commit}")
    for port, archive, overlay in pins.ports:
        a_path, d_path = pins.port_paths(port, archive, overlay)
        src = notices.find(archive, find)
        sha512 = notices.digest(src, "sha512")
        if overlay:
            shutil.copytree(pins.root / "Scripts/Ports" / port, out / d_path)
            where = "Scripts/Ports"
        else:
            data = subprocess.run(
                ["git", "-C", str(vcpkg_root), "archive", "--format=tar", commit, f"ports/{port}"],
                check=True,
                capture_output=True,
            ).stdout
            with tarfile.open(fileobj=io.BytesIO(data)) as t:
                t.extractall(out / "vcpkg-ports", filter="data")
            where = f"vcpkg {commit}"
        if not any(sha512 in text for text in _port_files(out / d_path).values()):
            raise notices.PinError(f"{src}: SHA-512 {sha512[:16]}... is not in port {port} ({where})")
        _place(src, out / a_path, link)
        manifest["ports"].append(
            {
                "port": port,
                "archive": a_path,
                "sha512": sha512,
                "dir": d_path,
                "tree": tree_id(out / d_path)[0],
                "from": where,
            }
        )
    for archive, record, portfile in pins.emports:
        src = notices.find(archive, find)
        sha512 = notices.digest(src, "sha512")
        if sha512 != _emport_sha512(out / pins.path_of(record), portfile):
            raise notices.PinError(f"{src}: SHA-512 is not the one {portfile} of {record} names")
        path = f"emscripten-ports/{archive}"
        _place(src, out / path, link)
        manifest["emports"].append({"archive": path, "sha512": sha512, "record": record, "portfile": portfile})
    manifest["tools"] = {n: c["why"] for n, c in sorted(pins.classes.items()) if c["kind"] == "tool"}
    shutil.copyfile(pins.lock_path, out / "sources.lock")
    shutil.copyfile(pins.root / "THIRD_PARTY_NOTICES.md", out / "THIRD_PARTY_NOTICES.md")
    (out / "MANIFEST.json").write_text(json.dumps(manifest, indent=1) + "\n")
    (out / "README.md").write_text(README.format(commit=commit))
    write_sums(out)
    return manifest


README = """# Third-party sources of the browser client

The source of every third-party component compiled or linked into the browser client (plClient and plClient-py2),
as `sources.lock` pins it. The client's own source is the repository at the release's tag (its page's "Source code
(GPLv3)" link). `THIRD_PARTY_NOTICES.md` names each component and its licence; `MANIFEST.json` says where each source
is here and how it is pinned; `SHA256SUMS` lists every file.

- `lock/`: the pinned downloads, byte for byte (their SHA-256s are in `sources.lock`).
- `cypython-2.3.3/`: CPython 2.3.3's tree, the proof of its pin and OpenUru's delta, and `LICENSE-PSF.txt`.
- `vcpkg-ports/`: each library port's download and the port's directory (portfile and patches) at vcpkg commit
  {commit} (`ports/`) or from this repository's `Scripts/Ports` (`overlay/`).
- `emscripten-ports/`: Emdawnwebgpu's archive, with the SHA-512 the pinned Emscripten release names.

Build tools used unmodified (emsdk, Node.js, CMake, Ninja, the vcpkg tool, the GNU autotools) are not included;
`MANIFEST.json` lists them under "tools". Check the bundle with
`web/build/notices/source_bundle.py check <this directory>`.
"""


def bundle_files(out):
    out = Path(out)
    return sorted(str(p.relative_to(out)) for p in out.rglob("*") if p.is_file() or p.is_symlink())


def write_sums(out):
    lines = [f"{notices.digest(Path(out) / p, 'sha256')}  {p}" for p in bundle_files(out) if p != "SHA256SUMS"]
    (Path(out) / "SHA256SUMS").write_text("\n".join(lines) + "\n")


def _has_commit(repo, commit):
    """Whether the git checkout at repo holds commit (its HEAD may be another commit: the ports are read at commit)."""
    r = subprocess.run(["git", "-C", str(repo), "cat-file", "-e", f"{commit}^{{commit}}"], capture_output=True)
    return r.returncode == 0


def check(pins, bundle, vcpkg_root=None, installed=None):
    """The bundle against the lock: (errors, summary)."""
    bundle = Path(bundle)
    errors = pins.class_errors()
    summary = {"records": {}, "ports": 0, "emports": 0, "files": 0}
    if not (bundle / "sources.lock").is_file():
        return errors + [f"{bundle}: no sources.lock"], summary
    if (bundle / "sources.lock").read_bytes() != pins.lock_path.read_bytes():
        errors.append(f"sources.lock: not the repository's {pins.lock_path}")
    try:
        manifest = json.loads((bundle / "MANIFEST.json").read_text())
    except (OSError, ValueError) as e:
        return errors + [f"MANIFEST.json: {e}"], summary
    if errors:
        return errors, summary
    for name, rec in sorted(pins.lock.items()):
        c, path = pins.classes[name], pins.path_of(name)
        summary["records"][name] = c["kind"]
        if path is None:
            continue
        target = bundle / path
        if not target.exists():
            errors.append(f"{name}: {path} is missing")
            continue
        if c["kind"] == "tree":
            got = tree_id(target)
            if got != (rec["tree"], rec["manifest-sha256"]):
                errors.append(
                    f"{name}: {path} is tree {got[0][:12]} (manifest {got[1][:12]}), not the pinned "
                    f"{rec['tree'][:12]} (manifest {rec['manifest-sha256'][:12]})"
                )
        else:
            got = notices.digest(target, "sha256")
            if got != rec["sha256"]:
                errors.append(f"{name}: {path} has SHA-256 {got}, not sources.lock's {rec['sha256']}")
        if c.get("proves") and target.exists():
            errors += _proof_errors(pins, bundle, name)
    for path, _, equal in pins.extra:
        if not (bundle / path).is_file() or not (bundle / equal).is_file():
            errors.append(f"{path}: missing, or {equal} is")
        elif (bundle / path).read_bytes() != (bundle / equal).read_bytes():
            errors.append(f"{path} is not {equal}")
    listed = {p["port"]: p for p in manifest.get("ports", [])}
    commit = pins.vcpkg_commit()
    if manifest.get("vcpkgCommit") != commit:
        errors.append(f"MANIFEST.json: vcpkg commit {manifest.get('vcpkgCommit')}, not sources.lock's {commit}")
    if vcpkg_root and not _has_commit(vcpkg_root, commit):
        # Without the pinned commit no port directory can be compared: one error, not one per port
        errors.append(
            f"--vcpkg-root {vcpkg_root} does not hold sources.lock's vcpkg commit {commit}, so the port "
            "directories cannot be checked against it (use a vcpkg checkout that has it)"
        )
        vcpkg_root = None
    for port, archive, overlay in pins.ports:
        a_path, d_path = pins.port_paths(port, archive, overlay)
        if not (bundle / a_path).is_file() or not (bundle / d_path).is_dir():
            errors.append(f"port {port}: {a_path} or {d_path} is missing")
            continue
        sha512 = notices.digest(bundle / a_path, "sha512")
        if not any(sha512 in text for text in _port_files(bundle / d_path).values()):
            errors.append(f"port {port}: {a_path} has SHA-512 {sha512[:16]}..., which its port files do not name")
        ident = tree_id(bundle / d_path)[0]
        if port not in listed or listed[port]["tree"] != ident:
            errors.append(f"port {port}: {d_path} is tree {ident[:12]}, not MANIFEST.json's")
        if overlay:
            r = subprocess.run(
                ["git", "-C", str(pins.root), "rev-parse", f"HEAD:Scripts/Ports/{port}"], capture_output=True, text=True
            )
            if r.returncode or r.stdout.strip() != ident:
                errors.append(f"port {port}: {d_path} is not Scripts/Ports/{port} of this repository's HEAD")
        elif vcpkg_root:
            r = subprocess.run(
                ["git", "-C", str(vcpkg_root), "rev-parse", f"{commit}:ports/{port}"], capture_output=True, text=True
            )
            if r.returncode or r.stdout.strip() != ident:
                errors.append(f"port {port}: {d_path} is not ports/{port} of vcpkg {commit[:12]}")
        summary["ports"] += 1
    for archive, record, portfile in pins.emports:
        path = f"emscripten-ports/{archive}"
        if not (bundle / path).is_file():
            errors.append(f"{path} is missing")
            continue
        try:
            want = _emport_sha512(bundle / pins.path_of(record), portfile)
        except (notices.PinError, OSError, tarfile.TarError) as e:
            errors.append(f"{path}: {e}")
            continue
        if notices.digest(bundle / path, "sha512") != want:
            errors.append(f"{path}: SHA-512 is not the one {portfile} of {record} names")
        summary["emports"] += 1
    if installed:
        info = Path(installed) / "vcpkg/info"
        packages = {f.name.split("_")[0] for f in info.glob(f"*_{TRIPLET}.list")}
        missing = sorted(p for p in packages if p not in listed and not p.startswith("vcpkg-"))
        errors += [f"installed package {p} ({TRIPLET}) has no port in the bundle" for p in missing]
        summary["installed"] = len(packages)
    sums = {}
    if (bundle / "SHA256SUMS").is_file():
        for line in (bundle / "SHA256SUMS").read_text().splitlines():
            digest, _, path = line.partition("  ")
            sums[path] = digest
    else:
        errors.append("SHA256SUMS is missing")
    present = set(bundle_files(bundle)) - {"SHA256SUMS"}
    errors += [f"{p}: not in SHA256SUMS" for p in sorted(present - set(sums))]
    errors += [f"{p}: in SHA256SUMS but missing" for p in sorted(set(sums) - present)]
    errors += [
        f"{p}: SHA-256 is not SHA256SUMS'"
        for p in sorted(present & set(sums))
        if notices.digest(bundle / p, "sha256") != sums[p]
    ]
    summary["files"] = len(present)
    return errors, summary


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("assemble")
    a.add_argument("--out", required=True)
    a.add_argument("--find", action="append", required=True, help="a directory holding pinned archives")
    a.add_argument("--vcpkg-root", required=True, help="a vcpkg git checkout that has the lock's vcpkg commit")
    a.add_argument("--cypython-tree", required=True, help="the pinned Cypython-2.3.3 tree (python2/build.sh fetch)")
    a.add_argument("--link", action="store_true", help="hard-link archives instead of copying them")
    c = sub.add_parser("check")
    c.add_argument("bundle")
    c.add_argument("--vcpkg-root", help="also check the registry port directories against the vcpkg commit")
    c.add_argument("--installed", help="a vcpkg_installed tree: each target package must have its port here")
    c.add_argument("--json", help="also write the result as JSON")
    args = ap.parse_args(argv)
    pins = Pins()
    if args.cmd == "assemble":
        try:
            m = assemble(pins, args.out, args.find, args.vcpkg_root, args.cypython_tree, args.link)
        except (notices.PinError, OSError, subprocess.CalledProcessError, tarfile.TarError) as e:
            print(f"source_bundle: {e}", file=sys.stderr)
            return 1
        print(
            f"assembled {args.out}: {sum(1 for r in m['records'].values() if r['path'])} pinned sources, "
            f"{len(m['ports'])} ports, {len(m['emports'])} Emscripten ports, {len(m['tools'])} tools left out"
        )
        return 0
    errors, summary = check(pins, args.bundle, args.vcpkg_root, args.installed)
    for e in errors:
        print(f"FAIL {e}")
    kinds = {}
    for k in summary["records"].values():
        kinds[k] = kinds.get(k, 0) + 1
    print(
        f"{'PASS' if not errors else 'FAIL'}: {args.bundle}: {len(summary['records'])} sources.lock records "
        f"({', '.join(f'{n} {k}' for k, n in sorted(kinds.items()))}), {summary['ports']} ports, "
        f"{summary['emports']} Emscripten ports, {summary['files']} files"
        + (f", {summary['installed']} installed packages" if "installed" in summary else "")
        + (f"; {len(errors)} error{'' if len(errors) == 1 else 's'}" if errors else "")
    )
    if args.json:
        Path(args.json).write_text(
            json.dumps({"pass": not errors, "errors": errors, "summary": summary}, indent=1) + "\n"
        )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
