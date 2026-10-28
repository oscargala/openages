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
"""Tests of source_bundle.py on a synthetic lock: a download, a tree pinned by a proof, its repository
files, a registry port, an overlay port, an Emscripten port and a tool. Each check failure has its own case: a missing
source, a changed digest, a changed tree, a port archive its port does not name, an unlisted file, a record in no
class, and LICENSE-PSF.txt differing from the tree's LICENSE. Local files and git only.

    python3 web/build/notices/test_source_bundle.py
"""

import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import source_bundle as sb  # noqa: E402
import notices  # noqa: E402


def git(repo, *args):
    return subprocess.run(
        [
            "git",
            "-C",
            str(repo),
            "-c",
            "user.name=Bundle Test",
            "-c",
            "user.email=b@example.test",
            "-c",
            "commit.gpgsign=false",
            *args,
        ],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def sha(data, algo="sha256"):
    return hashlib.new(algo, data).hexdigest()


class Fixture:
    """A repository with a lock, the pinned sources in a downloads folder, a vcpkg registry and an upstream tree."""

    def __init__(self, tmp):
        t = Path(tmp)
        self.downloads = t / "downloads"
        self.downloads.mkdir()
        lib, ovl, reg, emport = b"lib-1.0 source\n", b"overlay source\n", b"registry source\n", b"PK emport\n"
        for name, data in (("lib-1.0.tar.gz", lib), ("ovl-1.0.tar.gz", ovl), ("reg-1.0.tar.gz", reg)):
            (self.downloads / name).write_bytes(data)
        z = io.BytesIO()
        with zipfile.ZipFile(z, "w") as f:
            f.writestr("emport/LICENSE", "emport licence\n")
        (self.downloads / "emport.zip").write_bytes(z.getvalue())
        portfile = f"SHA512 = '{sha(z.getvalue(), 'sha512')}'\n".encode()
        with tarfile.open(self.downloads / "release.tar.xz", "w:xz") as tar:
            for name, data in (("release/system/lib/libc.c", b"int x;\n"), ("tools/ports/emport.py", portfile)):
                info = tarfile.TarInfo(name)
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
        # the vcpkg registry: one port whose portfile names reg's SHA-512
        self.vcpkg = t / "vcpkg"
        (self.vcpkg / "ports/reg").mkdir(parents=True)
        (self.vcpkg / "ports/reg/portfile.cmake").write_text(f"vcpkg_download(SHA512 {sha(reg, 'sha512')})\n")
        (self.vcpkg / "ports/reg/fix.patch").write_text("--- a\n+++ b\n")
        (self.vcpkg / "ports/other/").mkdir(parents=True)
        (self.vcpkg / "ports/other/portfile.cmake").write_text("unrelated\n")
        git(t, "init", "-q", str(self.vcpkg))
        git(self.vcpkg, "add", "-A")
        git(self.vcpkg, "commit", "-q", "-m", "registry")
        vcpkg_commit = git(self.vcpkg, "rev-parse", "HEAD")
        # the upstream tree (pinned by commit, path, tree id and manifest) and its proof
        up = t / "upstream"
        (up / "SDK/Py").mkdir(parents=True)
        (up / "SDK/Py/LICENSE").write_text("PSF licence\n")
        (up / "SDK/Py/a.c").write_text("int a;\n")
        git(t, "init", "-q", str(up))
        git(up, "add", "-A")
        git(up, "commit", "-q", "-m", "tree")
        tree_commit = git(up, "rev-parse", "HEAD")
        tree, manifest = sb.tree_id(up / "SDK/Py")
        self.tree = up / "SDK/Py"
        proof = subprocess.run(
            [sys.executable, "-I", str(ROOT / "web/build/commit-proof.py"), "make", str(up), tree_commit, "SDK/Py"],
            check=True,
            capture_output=True,
        ).stdout
        # the repository: the lock, the tree's proof and delta, its LICENSE copy, an overlay port, the notices
        self.root = t / "repo"
        (self.root / "web/build").mkdir(parents=True)
        (self.root / "py2").mkdir()
        (self.root / "py2/proof.txt").write_bytes(proof)
        (self.root / "py2/delta.patch").write_text("delta\n")
        (self.root / "py2/LICENSE-PSF.txt").write_text("PSF licence\n")
        (self.root / "Scripts/Ports/ovl").mkdir(parents=True)
        (self.root / "Scripts/Ports/ovl/portfile.cmake").write_text(f"SHA512 {sha(ovl, 'sha512')}\n")
        (self.root / "THIRD_PARTY_NOTICES.md").write_text("# Third-party notices\n")
        self.lock_text = (
            "\n".join(
                [
                    "# synthetic",
                    f"vcpkg url=https://example.test/vcpkg.git commit={vcpkg_commit}",
                    f"tool-x version=1 url=https://example.test/tool.tar.gz file=tool.tar.gz sha256={'0' * 64}",
                    f"lib version=1.0 url=https://example.test/lib-1.0.tar.gz file=lib-1.0.tar.gz sha256={sha(lib)}",
                    f"release version=1 url=https://example.test/release.tar.xz file=release.tar.xz "
                    f"sha256={sha((self.downloads / 'release.tar.xz').read_bytes())}",
                    f"py url=https://example.test/up.git commit={tree_commit} path=SDK/Py tree={tree} manifest-sha256={manifest}",
                    f"py-proof file=py2/proof.txt sha256={sha(proof)}",
                    f"py-cwe url=https://example.test/cwe.git commit={'1' * 40} path=Old/Py tree={'2' * 40}",
                    f"py-delta file=py2/delta.patch sha256={sha(b'delta' + bytes([10]))}",
                ]
            )
            + "\n"
        )
        (self.root / "web/build/sources.lock").write_text(self.lock_text)
        git(t, "init", "-q", str(self.root))
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "repo")
        self.classes = {
            "vcpkg": {"kind": "ports", "why": "registry"},
            "tool-x": {"kind": "tool", "why": "a tool"},
            "lib": {"kind": "download", "why": "a library"},
            "release": {"kind": "download", "why": "the compiler release"},
            "py": {"kind": "tree", "dir": "py", "why": "the tree"},
            "py-proof": {"kind": "repo-file", "dir": "py", "proves": "py", "why": "the proof"},
            "py-cwe": {"kind": "derived", "of": "py", "why": "the tree's origin"},
            "py-delta": {"kind": "repo-file", "dir": "py", "why": "the delta"},
        }
        self.texts = [
            {"kind": "port", "port": "reg", "archive": "reg-1.0.tar.gz"},
            {"kind": "port", "port": "ovl", "archive": "ovl-1.0.tar.gz", "overlay": True},
            {"kind": "emport", "record": "release", "archive": "emport.zip", "portfile": "tools/ports/emport.py"},
        ]
        self.extra = [("py/LICENSE-PSF.txt", "py2/LICENSE-PSF.txt", "py/Py/LICENSE")]

    def pins(self, classes=None):
        return sb.Pins(self.root, classes=classes or self.classes, texts=self.texts, extra=self.extra)


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.f = Fixture(self.tmp.name)
        self.out = Path(self.tmp.name) / "bundle"
        self.manifest = sb.assemble(self.f.pins(), self.out, [self.f.downloads], self.f.vcpkg, self.f.tree)

    def errors(self, **kw):
        return sb.check(self.f.pins(), self.out, vcpkg_root=self.f.vcpkg, **kw)[0]

    def test_assembled_bundle_passes(self):
        self.assertEqual(self.errors(), [])
        files = set(sb.bundle_files(self.out))
        for want in (
            "lock/lib-1.0.tar.gz",
            "lock/release.tar.xz",
            "py/Py/a.c",
            "py/Py/LICENSE",
            "py/proof.txt",
            "py/delta.patch",
            "py/LICENSE-PSF.txt",
            "vcpkg-ports/downloads/reg-1.0.tar.gz",
            "vcpkg-ports/ports/reg/portfile.cmake",
            "vcpkg-ports/ports/reg/fix.patch",
            "vcpkg-ports/downloads/ovl-1.0.tar.gz",
            "vcpkg-ports/overlay/ovl/portfile.cmake",
            "emscripten-ports/emport.zip",
            "sources.lock",
            "THIRD_PARTY_NOTICES.md",
            "MANIFEST.json",
            "README.md",
            "SHA256SUMS",
        ):
            self.assertIn(want, files)
        self.assertNotIn("vcpkg-ports/ports/other/portfile.cmake", files)  # only the ports the client links
        self.assertFalse(any("tool.tar.gz" in p for p in files))  # tools are left out
        self.assertEqual(self.manifest["tools"], {"tool-x": "a tool"})
        self.assertEqual(self.manifest["records"]["py-cwe"]["path"], None)

    def test_missing_source(self):
        (self.out / "lock/lib-1.0.tar.gz").rename(self.out / "lib.away")
        errors = self.errors()
        self.assertIn("lib: lock/lib-1.0.tar.gz is missing", errors)
        self.assertIn("lock/lib-1.0.tar.gz: in SHA256SUMS but missing", errors)
        self.assertIn("lib.away: not in SHA256SUMS", errors)

    def test_changed_digest(self):
        (self.out / "lock/lib-1.0.tar.gz").write_bytes(b"lib-1.0 source, changed\n")
        sb.write_sums(self.out)  # even with matching sums, the lock's digest decides
        errors = self.errors()
        self.assertEqual(len(errors), 1, errors)
        self.assertRegex(errors[0], r"^lib: lock/lib-1\.0\.tar\.gz has SHA-256 [0-9a-f]{64}, not sources\.lock's")

    def test_changed_tree_and_licence_copy(self):
        (self.out / "py/Py/a.c").write_text("int b;\n")
        (self.out / "py/LICENSE-PSF.txt").write_text("another licence\n")
        sb.write_sums(self.out)
        errors = self.errors()
        self.assertTrue(any(e.startswith("py: py/Py is tree") for e in errors), errors)
        self.assertIn("py/LICENSE-PSF.txt is not py/Py/LICENSE", errors)

    def test_proof_must_link_the_pin(self):
        (self.out / "py/proof.txt").write_text("commit 0\n")
        sb.write_sums(self.out)
        errors = self.errors()
        self.assertTrue(any(e.startswith("py-proof: py/proof.txt has SHA-256") for e in errors), errors)
        self.assertTrue(any("not the proof of py" in e for e in errors), errors)

    def test_port_archive_and_directory(self):
        (self.out / "vcpkg-ports/downloads/reg-1.0.tar.gz").write_bytes(b"another archive\n")
        (self.out / "vcpkg-ports/overlay/ovl/extra.patch").write_text("+x\n")
        sb.write_sums(self.out)
        errors = self.errors()
        self.assertTrue(any(e.startswith("port reg: vcpkg-ports/downloads/reg-1.0.tar.gz has SHA-512") for e in errors))
        self.assertIn("port ovl: vcpkg-ports/overlay/ovl is not Scripts/Ports/ovl of this repository's HEAD", errors)

    def test_vcpkg_checkout_without_the_commit(self):
        # A vcpkg checkout that lacks the pinned commit is named once, not reported as 2 differing ports
        other = Path(self.tmp.name) / "other-vcpkg"
        git(Path(self.tmp.name), "init", "-q", str(other))
        errors = sb.check(self.f.pins(), self.out, vcpkg_root=other)[0]
        self.assertEqual(
            [e for e in errors if "vcpkg" in e],
            [
                f"--vcpkg-root {other} does not hold sources.lock's vcpkg commit {self.f.pins().vcpkg_commit()}, "
                "so the port directories cannot be checked against it (use a vcpkg checkout that has it)"
            ],
        )
        with self.assertRaises(sb.notices.PinError):
            sb.assemble(self.f.pins(), Path(self.tmp.name) / "out2", [self.f.downloads], other, self.f.tree)

    def test_emscripten_port(self):
        (self.out / "emscripten-ports/emport.zip").write_bytes(b"PK another\n")
        sb.write_sums(self.out)
        self.assertEqual(
            self.errors(),
            ["emscripten-ports/emport.zip: SHA-512 is not the one tools/ports/emport.py of release names"],
        )

    def test_unlisted_and_unclassified(self):
        (self.out / "lock/extra.tar.gz").write_bytes(b"x")
        self.assertEqual(self.errors(), ["lock/extra.tar.gz: not in SHA256SUMS"])
        (self.out / "lock/extra.tar.gz").unlink()
        classes = dict(self.f.classes)
        del classes["tool-x"]
        errors = sb.check(self.f.pins(classes), self.out)[0]
        self.assertEqual(errors, ["sources.lock record tool-x is in no bundle class (source_bundle.py LOCK_CLASSES)"])

    def test_lock_copy_must_be_the_repository_lock(self):
        (self.f.root / "web/build/sources.lock").write_text(self.f.lock_text + "# a later edit\n")
        self.assertEqual(
            self.errors(), [f"sources.lock: not the repository's {self.f.root / 'web/build/sources.lock'}"]
        )

    def test_installed_packages_need_their_port(self):
        info = Path(self.tmp.name) / "installed/vcpkg/info"
        info.mkdir(parents=True)
        for name in (
            "reg_1.0_wasm32-emscripten-plasma.list",
            "ovl_1.0_wasm32-emscripten-plasma.list",
            "vcpkg-cmake_2024_wasm32-emscripten-plasma.list",
            "zz_1.0_arm64-osx.list",
        ):
            (info / name).write_text("")
        self.assertEqual(self.errors(installed=info.parents[1]), [])
        (info / "new_2.0_wasm32-emscripten-plasma.list").write_text("")
        self.assertEqual(
            self.errors(installed=info.parents[1]),
            ["installed package new (wasm32-emscripten-plasma) has no port in the bundle"],
        )

    def test_assemble_refuses_a_source_that_does_not_match(self):
        (self.f.downloads / "lib-1.0.tar.gz").write_bytes(b"tampered\n")
        with self.assertRaisesRegex(notices.PinError, "SHA-256 .* is not sources.lock lib's"):
            sb.assemble(self.f.pins(), Path(self.tmp.name) / "b2", [self.f.downloads], self.f.vcpkg, self.f.tree)

    def test_real_lock_is_fully_classified(self):
        pins = sb.Pins()
        self.assertEqual(pins.class_errors(), [])
        kinds = {c["kind"] for c in pins.classes.values()}
        self.assertEqual(kinds, sb.KINDS)
        # every port and Emscripten port the notices name is bundled; OpenSSL and libjpeg-turbo among them (COPYING.txt)
        ports = {p for p, _, _ in pins.ports}
        self.assertTrue({"openssl", "libjpeg-turbo", "physx", "zlib"} <= ports, ports)
        self.assertEqual([a for a, _, _ in pins.emports], ["emdawnwebgpu_pkg-v20260423.175430.zip"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
