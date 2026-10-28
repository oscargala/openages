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
"""Negative controls for check_notices.py: the real THIRD_PARTY_NOTICES.md passes against fixtures shaped like the
browser build (install tree, both link lines, a system-library link, a header scan, a registry), and each mutation
of the notices or of an input fails with the error that names it.

    python3 -I web/build/notices/test_check_notices.py
"""

import hashlib
import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import check_notices  # noqa: E402

T = "wasm32-emscripten-plasma"
H = "arm64-osx"
# The browser build's install tree: package -> (version, triplet, files).
INSTALLED = {
    "asio": ("1.32.0", T, ["include/asio.hpp"]),
    "brotli": ("1.2.0", T, ["lib/libbrotlidec.a", "lib/libbrotlicommon.a"]),
    "bzip2": ("1.0.8", T, ["lib/libbz2.a"]),
    "expat": ("2.8.2", T, ["lib/libexpat.a", "include/expat.h"]),
    "freetype": ("2.14.3", T, ["lib/libfreetype.a", "include/ft2build.h"]),
    "libjpeg-turbo": ("3.2.0", T, ["lib/libjpeg.a"]),
    "libogg": ("1.3.6", T, ["lib/libogg.a"]),
    "libpng": ("1.6.58", T, ["lib/libpng16.a"]),
    "libvorbis": ("1.3.7", T, ["lib/libvorbis.a", "lib/libvorbisfile.a"]),
    "openssl": ("3.6.3", T, ["lib/libcrypto.a"]),
    "opus": ("1.5.2", T, ["lib/libopus.a"]),
    "physx": ("4.1.2", T, ["lib/libPhysX_static_32.a", "lib/libPhysXCommon_static_32.a"]),
    "speex": ("1.2.1", T, ["lib/libspeex.a"]),
    "string-theory": ("3.9", T, ["include/string_theory/string"]),
    "tl-expected": ("1.3.1", T, ["include/tl/expected.hpp"]),
    "zlib": ("1.3.2", T, ["lib/libz.a"]),
    "vcpkg-cmake": ("2024-04-23", H, ["share/vcpkg-cmake/vcpkg-port-config.cmake"]),
    "vcpkg-cmake-config": ("2026-07-21", H, ["share/vcpkg-cmake-config/vcpkg-port-config.cmake"]),
    "vcpkg-cmake-get-vars": ("2025-05-29", H, ["share/vcpkg-cmake-get-vars/vcpkg-port-config.cmake"]),
    "vcpkg-make": ("2026-07-09", H, ["share/vcpkg-make/vcpkg-port-config.cmake"]),
}
VCPKG_LIBS = [
    f"/d/vcpkg_installed/{t}/{f}" for _, t, fs in INSTALLED.values() for f in fs if f.startswith("lib/") and t == T
]
PY3_LIBS = ["/d/python/lib/libpython3.12.a", "/d/python/lib/libmpdec.a", "/d/python/lib/libHacl_Hash_SHA2.a"]
PY2_LIBS = ["/d/py2deps/python2/lib/libpython2.3.a"]
MOVIE_LIBS = ["/d/movies/lib/libvpx.a", "/d/movies/lib/libwebm.a"]
DASH_L = ["-lopenal", "-ldl", "-lm"]
FLAGS = ["-pthread", "--use-port=emdawnwebgpu:cpp_bindings=false", "-sJSPI"]
SYSLIBS = (
    "em++ -v ...\n /e/upstream/bin/wasm-ld -o hello.wasm -L/e/cache/sysroot/lib/wasm32-emscripten "
    "-lGL-mt-getprocaddr -lal -lc++-mt-legacyexcept -lc++abi-debug-mt-legacyexcept -lc-mt "
    "-lclang_rt.builtins-legacysjlj-mt -ldlmalloc-mt -lhtml5 -lsockets-mt -lstubs -lunwind-mt-legacyexcept "
    "-lwasmfs-mt /e/cache/sysroot/lib/wasm32-emscripten/crt1_proxy_main.o "
    "/e/cache/sysroot/lib/wasm32-emscripten/crtbegin-mt.o "
    "/e/cache/sysroot/lib/wasm32-emscripten/libemdawnwebgpu-842f9d2b-O2-shared_memory.a hello.o\n"
)
HEADERS = [
    f"/d/vcpkg_installed/{T}/include/asio.hpp",
    f"/d/vcpkg_installed/{T}/include/string_theory/string",
    f"/d/vcpkg_installed/{T}/include/tl/expected.hpp",
    "/d/python/include/python3.12/Python.h",
    "/d/movies/include/vpx/vp8dx.h",
    "/src/Sources/Plasma/CoreLib/HeadSpin.h",
]
# The registry at the pinned commit: port -> (version, licence), for every non-overlay port a row names.
REGISTRY = {
    "openssl": ("3.6.3", "Apache-2.0"),
    "libjpeg-turbo": ("3.2.0", "BSD-3-Clause"),
    "zlib": ("1.3.2", "Zlib"),
    "libpng": ("1.6.58", "libpng-2.0"),
    "freetype": ("2.14.3", "FTL OR GPL-2.0-or-later"),
    "brotli": ("1.2.0", "MIT"),
    "bzip2": ("1.0.8", "bzip2-1.0.6"),
    "expat": ("2.8.2", "MIT"),
    "libogg": ("1.3.6", "BSD-3-Clause"),
    "libvorbis": ("1.3.7", "BSD-3-Clause"),
    "opus": ("1.5.2", "BSD-3-Clause"),
    "speex": ("1.2.1", "BSD-3-Clause"),
    "libvpx": ("1.16.0", "BSD-3-Clause"),
    "libwebm": ("1.0.0.32", "BSD-3-Clause"),
    "asio": ("1.32.0", "BSL-1.0"),
    "string-theory": ("3.9", "MIT"),
    "tl-expected": ("1.3.1", "CC0-1.0"),
    "curl": ("8.21.0", "curl AND ISC AND BSD-3-Clause"),
    "fontconfig": ("2.17.1", "MIT"),
    "libarchive": ("3.8.7", None),
    "openal-soft": ("1.25.1", "LGPL-2.0-or-later"),
    "fmt": ("12.2.0", "MIT"),
    "libuuid": ("1.0.3", "BSD-3-Clause"),
    "vcpkg-cmake": ("2024-04-23", "MIT"),
    "vcpkg-cmake-config": ("2026-07-21", "MIT"),
    "vcpkg-cmake-get-vars": ("2025-05-29", "MIT"),
    "vcpkg-make": ("2026-07-09", None),
}


def ninja(libs, flags):
    return (
        "rule CXX_EXECUTABLE_LINKER__plClient_Release\n  command = em++ $in -o $out\n\n"
        "build Sources/Plasma/Apps/plClient/plClient.js: CXX_EXECUTABLE_LINKER__plClient_Release a.o\n"
        f"  LINK_FLAGS = {' '.join(flags)}\n"
        f"  LINK_LIBRARIES = Sources/Plasma/CoreLib/libCoreLib.a {' '.join(libs)}\n"
        "  TARGET_FILE = Sources/Plasma/Apps/plClient/plClient.js\n\n"
        "build plClient.js: phony Sources/Plasma/Apps/plClient/plClient.js\n"
    )


class Fixture:
    def __init__(self, tmp):
        self.tmp = Path(tmp)
        self.notices = (ROOT / "THIRD_PARTY_NOTICES.md").read_text()
        self.lock = (ROOT / "web/build/sources.lock").read_text()
        self.installed = {k: (v, t, list(f)) for k, (v, t, f) in INSTALLED.items()}
        self.py3 = VCPKG_LIBS + PY3_LIBS + MOVIE_LIBS + DASH_L
        self.py2 = VCPKG_LIBS + PY2_LIBS + MOVIE_LIBS + DASH_L
        self.flags = list(FLAGS)
        self.syslibs = SYSLIBS
        self.headers = list(HEADERS)
        self.registry = dict(REGISTRY)
        self.texts = False

    def write(self):
        t = self.tmp
        (t / "notices.md").write_text(self.notices)
        inst = t / "vcpkg_installed"
        info = inst / "vcpkg/info"
        info.mkdir(parents=True, exist_ok=True)
        for f in info.glob("*.list"):
            f.unlink()
        for port, (version, triplet, files) in self.installed.items():
            (info / f"{port}_{version}_{triplet}.list").write_text(
                "".join(f"{triplet}/{f}\n" for f in [p.rsplit("/", 1)[0] + "/" for p in files] + files)
            )
        (t / "py3.ninja").write_text(ninja(self.py3, self.flags))
        (t / "py2.ninja").write_text(ninja(self.py2, self.flags))
        (t / "link-v.txt").write_text(self.syslibs)
        (t / "deps.txt").write_text(
            "a.o: #deps 6, deps mtime 1 (VALID)\n" + "".join(f"    {h}\n" for h in self.headers)
        )
        reg = t / "registry"
        if not reg.exists():
            reg.mkdir()
            subprocess.run(["git", "init", "-q", str(reg)], check=True)
        for port, (version, lic) in self.registry.items():
            d = reg / "ports" / port
            d.mkdir(parents=True, exist_ok=True)
            meta = {"name": port, "version": version}
            if lic:
                meta["license"] = lic
            (d / "vcpkg.json").write_text(json.dumps(meta))
        git = ["git", "-C", str(reg), "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
        subprocess.run(git + ["add", "-A"], check=True)
        subprocess.run(git + ["commit", "-q", "--allow-empty", "-m", "ports"], check=True)
        head = subprocess.run(
            ["git", "-C", str(reg), "rev-parse", "HEAD"], check=True, capture_output=True, text=True
        ).stdout.strip()
        (t / "sources.lock").write_text(
            re.sub(r"^(vcpkg url=\S+ commit=)[0-9a-f]+", rf"\g<1>{head}", self.lock, flags=re.M)
        )
        args = [
            "--notices",
            str(t / "notices.md"),
            "--lock",
            str(t / "sources.lock"),
            "--installed",
            str(inst),
            "--vcpkg-root",
            str(reg),
            "--link",
            f"py3={t / 'py3.ninja'}",
            "--link",
            f"py2={t / 'py2.ninja'}",
            "--syslibs",
            str(t / "link-v.txt"),
            "--deps",
            str(t / "deps.txt"),
        ]
        if self.texts:
            # The texts as the notices carry them, so the render must reproduce the file exactly. The ones the
            # notices only name by SHA-256 are not in the file: stand-ins take their place, in a copy of texts.json
            # and in the notices.
            d = t / "texts"
            d.mkdir(exist_ok=True)
            for m in check_notices.BLOCK.finditer((ROOT / "THIRD_PARTY_NOTICES.md").read_text()):
                (d / f"{m.group('id')}.txt").write_text(m.group("body") + "\n")
            spec = json.loads((HERE / "texts.json").read_text())
            text = (t / "notices.md").read_text()
            for e in spec["texts"]:
                if e.get("embed") is False:
                    stand_in = f"stand-in for {e['id']}\n"
                    (d / f"{e['id']}.txt").write_text(stand_in)
                    sha = hashlib.sha256(stand_in.encode()).hexdigest()
                    text = text.replace(e["sha256"], sha)
                    e["sha256"] = sha
            (t / "notices.md").write_text(text)
            (t / "texts.json").write_text(json.dumps(spec))
            args += ["--texts", str(d), "--spec", str(t / "texts.json")]
        return args

    def run(self):
        c = check_notices.Check(check_notices.parse_args(self.write()))
        return c.run()


def row_line(text, name):
    return next(line for line in text.split("\n") if line.startswith(f"| [{name}]("))


class CheckNotices(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.f = Fixture(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def assertFails(self, *needles):
        errors = self.f.run()
        self.assertTrue(errors, "the mutation passed the check")
        for n in needles:
            self.assertTrue(any(n in e for e in errors), f"no error contains {n!r}; errors: {errors}")

    def test_clean_passes(self):
        self.f.texts = True
        self.assertEqual(self.f.run(), [])

    def test_dropped_row(self):
        self.f.notices = self.f.notices.replace(row_line(self.f.notices, "OpenSSL") + "\n", "")
        self.assertFails(
            "vcpkg.json names port openssl, which no row names",
            "installed package openssl:wasm32-emscripten-plasma 3.6.3 is named by no row",
        )

    def test_row_naming_a_missing_record(self):
        self.f.notices = self.f.notices.replace(
            row_line(self.f.notices, "zlib"), row_line(self.f.notices, "zlib").replace("`vcpkg`", "`vcpkg`, `nosuch`")
        )
        self.assertFails("sources.lock has no record nosuch")

    def test_record_named_by_no_row(self):
        self.f.lock += "extra-tool version=1.0 url=https://example.invalid/x.tar.gz file=x.tar.gz sha256=00\n"
        self.assertFails("sources.lock record extra-tool is named by no row")

    def test_lock_version_mismatch(self):
        line = row_line(self.f.notices, "libvpx")
        self.f.notices = self.f.notices.replace(line, line.replace("1.15.2 (browser)", "1.15.1 (browser)"))
        self.assertFails("sources.lock libvpx is version 1.15.2")

    def test_mutated_text(self):
        self.f.notices = self.f.notices.replace(
            "Permission to use, copy, modify, and distribute this software",
            "Permission to use, copy, modify, and distribute this softwarE",
            1,
        )
        self.assertFails("its text has SHA-256")

    def test_removed_text(self):
        m = re.search(r"<!-- text:speex -->.*?<!-- /text:speex -->\n", self.f.notices, re.S)
        self.f.notices = self.f.notices.replace(m.group(0), "")
        self.assertFails("Speex: its section #speex holds no licence text", "text speex: appears 0 times")

    def test_unnamed_non_embedded_text(self):
        sha = next(
            e["sha256"] for e in json.loads((HERE / "texts.json").read_text())["texts"] if e["id"] == "llvm-libunwind"
        )
        self.f.notices = self.f.notices.replace(sha, "0" * 64)
        self.assertFails("text llvm-libunwind: its SHA-256 is not named")

    def test_unknown_library_on_link_line(self):
        self.f.py3.append("/d/other/lib/libfoo.a")
        self.assertFails("py3: library /d/other/lib/libfoo.a on the link line maps to no row")

    def test_unknown_dash_l_and_emscripten_port(self):
        self.f.py2.append("-lfoo")
        self.f.flags += ["-sUSE_SDL=2", "--use-port=sdl3"]
        self.assertFails(
            "py2: -lfoo on the link line maps to no row",
            "-sUSE_SDL=2 in the link flags maps to no row",
            "Emscripten port sdl3 maps to no row",
        )

    def test_vcpkg_library_no_package_installed(self):
        self.f.py3.append(f"/d/vcpkg_installed/{T}/lib/libharfbuzz.a")
        self.assertFails(f"py3: {T}/lib/libharfbuzz.a on the link line is in no installed package's file list")

    def test_extra_installed_package(self):
        self.f.installed["harfbuzz"] = ("12.0.0", T, ["lib/libharfbuzz.a"])
        self.assertFails("installed package harfbuzz:wasm32-emscripten-plasma 12.0.0 is named by no row")

    def test_installed_version_mismatch(self):
        line = row_line(self.f.notices, "OpenSSL")
        self.f.notices = self.f.notices.replace(line, line.replace("| 3.6.3 |", "| 3.6.2 |"))
        self.assertFails("OpenSSL: installed openssl is version 3.6.3")

    def test_flavour(self):
        line = row_line(self.f.notices, "CPython 3.12.9")
        self.f.notices = self.f.notices.replace(line, line.replace("browser (plClient), native", "browser, native"))
        self.assertFails("CPython 3.12.9: only plClient links it")

    def test_flavour_row_that_flavour_does_not_link(self):
        self.f.py2 = [x for x in self.f.py2 if "libpython2.3" not in x]
        self.assertFails(
            "CPython 2.3.3: a browser row, but neither linked",
            "CPython 2.3.3: the row says plClient-py2, which does not link it",
        )

    def test_browser_row_without_evidence(self):
        self.f.headers = [h for h in self.f.headers if "string_theory" not in h]
        self.assertFails("string_theory: a browser row, but neither linked, a system library nor a compiled header")

    def test_unknown_system_library(self):
        self.f.syslibs = self.f.syslibs.replace("-lstubs", "-lstubs -lembind-rtti")
        self.assertFails("system library embind-rtti maps to no row")

    def test_unknown_header(self):
        self.f.headers.append(f"/d/vcpkg_installed/{T}/include/hb.h")
        self.assertFails(f"header {T}/include/hb.h is in no installed package's file list")

    def test_missing_repo_path(self):
        self.f.notices = self.f.notices.replace(
            "## Not included", "See `web/build/python2/nosuch.patch`.\n\n## Not included"
        )
        self.assertFails("the file names web/build/python2/nosuch.patch, which does not exist")

    def test_font_named_by_no_row(self):
        self.f.notices = self.f.notices.replace(row_line(self.f.notices, "DejaVu Sans Bold") + "\n", "")
        self.assertFails("font web/deploy/fonts/DejaVuSans-Bold.ttf is named by no kit row")

    def test_missing_credit(self):
        self.f.notices = self.f.notices.replace("Portions of this software are copyright", "Portions are copyright")
        self.assertFails("the credit the FreeType License asks for is missing")

    def test_overlay_port_named_as_registry_port(self):
        line = row_line(self.f.notices, "NVIDIA PhysX SDK")
        self.f.notices = self.f.notices.replace(line, line.replace("overlay port `physx`", "port `physx`"))
        self.assertFails(
            "NVIDIA PhysX SDK: physx is an overlay port",
            "overlay port Scripts/Ports/physx is named by no row as an overlay port",
        )

    def test_registry_version_and_licence(self):
        self.f.registry["curl"] = ("8.22.0", "curl AND ISC AND BSD-3-Clause")
        self.f.registry["libuuid"] = ("1.0.3", "BSD-2-Clause")
        self.assertFails("curl: port curl is version 8.22.0", "libuuid: port libuuid declares licence 'BSD-2-Clause'")

    def test_registry_override_wins(self):
        line = row_line(self.f.notices, "OpenAL Soft")
        self.f.notices = self.f.notices.replace(line, line.replace("| 1.24.3 |", "| 1.25.1 |"))
        self.assertFails("OpenAL Soft: port openal-soft is version 1.24.3")

    def test_stale_render(self):
        self.f.texts = True
        self.f.notices = self.f.notices.replace("## Native builds", "## Native builds\n\nAn edit made by hand.")
        self.assertFails("is not the render of THIRD_PARTY_NOTICES.in.md")

    def test_unknown_build_and_broken_anchor(self):
        line = row_line(self.f.notices, "Brotli")
        self.f.notices = self.f.notices.replace(
            line, line.replace("(#brotli)", "(#brotly)").replace("browser, native", "browsers, native")
        )
        self.assertFails("Brotli: unknown build 'browsers'", "Brotli: link #brotly names no heading")


if __name__ == "__main__":
    unittest.main(verbosity=2)
