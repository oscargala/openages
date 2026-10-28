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
"""Build pinned original movie codecs from verified cached archives into an explicit prefix.

No downloads, shared-prefix selection, cleanup or dependency-manager install is implicit.
The caller is responsible for authorization to write the chosen work/install directories.

The archives must already be in --archive-root under their sources.lock file names
(build-deps.sh movies downloads them there with web/build/sources.sh). Each one is
checked against the SHA-256 in web/build/sources.lock and the SHA-512 below, which
movie-codecs.json records, before anything is extracted.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

SOURCES = {
    "libvpx": dict(
        version="1.15.2",
        archive="webmproject-libvpx-v1.15.2.tar.gz",
        sha512="824fe8719e4115ec359ae0642f5e1cea051d458f09eb8c24d60858cf082f66e411215e23228173ab154044bafbdfbb2d93b589bb726f55b233939b91f928aae0",
    ),
    "libwebm": dict(
        version="1.0.0.32",
        archive="webmproject-libwebm-libwebm-1.0.0.32.tar.gz",
        sha512="9da60f3e7243fb78e0c02e0b6bf8e628552c5b54631960e34bacdf0349ce690984ff9432b8ffa495051858ecc2f4e4a4c1e0b290666058298abf94c3ad99670f",
    ),
}
ABI = "-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm"
LOCK = Path(__file__).resolve().parent / "sources.lock"
FLAGS = "-O2 " + ABI


def lock_record(name):
    """The fields of the sources.lock record <name>."""
    for line in LOCK.read_text().splitlines():
        fields = line.split()
        if fields and fields[0] == name:
            return dict(field.split("=", 1) for field in fields[1:])
    raise RuntimeError("No sources.lock record for " + name)


def lock_sha256(name, archive):
    """The SHA-256 sources.lock pins for the record <name>, whose file must be <archive>."""
    record = lock_record(name)
    if record.get("file") != archive:
        raise RuntimeError("sources.lock names another file for " + name)
    return record["sha256"]


# The Emscripten release is pinned once, by sources.lock's emsdk record.
EMSCRIPTEN_VERSION = lock_record("emsdk")["version"]


def run(command, cwd, env, log):
    with log.open("a") as output:
        subprocess.run(command, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT, check=True)


def verify_source(archive, source_root):
    count = 0
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            if member.isfile():
                if (source_root / member.name).read_bytes() != tar.extractfile(member).read():
                    raise RuntimeError("Original codec source differs from verified archive")
                count += 1
    return count


def build_codecs(work, prefix, archive_root, target="wasm", jobs=8):
    work = Path(work).expanduser().resolve()
    prefix = Path(prefix).expanduser().resolve()
    archive_root = Path(archive_root).expanduser().resolve()
    # Validate all inputs before creating output directories or invoking a compiler.
    for name, data in SOURCES.items():
        path = archive_root / data["archive"]
        if not path.is_file():
            raise RuntimeError("Missing codec archive: " + str(path))
        content = path.read_bytes()
        if hashlib.sha512(content).hexdigest() != data["sha512"] or hashlib.sha256(content).hexdigest() != lock_sha256(
            name, data["archive"]
        ):
            raise RuntimeError("Cached codec archive digest mismatch: " + data["archive"])
    if target not in ("wasm", "native"):
        raise ValueError("Unknown codec target")
    version = None
    if target == "wasm":
        for command in ("emcc", "em++", "emar", "emranlib", "emcmake"):
            if not shutil.which(command):
                raise RuntimeError("Source the pinned Emscripten SDK first")
        version = subprocess.check_output(["emcc", "-dumpversion"], text=True).strip()
        if version != EMSCRIPTEN_VERSION:
            raise RuntimeError("Expected Emscripten " + EMSCRIPTEN_VERSION)
    if prefix == work or prefix.is_relative_to(work / "src"):
        raise ValueError("Install prefix must be separate from the work/source root")
    work.mkdir(parents=True, exist_ok=True)
    provenance = {}
    for name, data in SOURCES.items():
        archive = archive_root / data["archive"]
        source_root = work / "src" / name
        if not source_root.exists():
            source_root.mkdir(parents=True)
            with tarfile.open(archive) as tar:
                tar.extractall(source_root, filter="data")
        children = list(source_root.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise RuntimeError("Expected one original source root")
        source = children[0]
        verify_source(archive, source_root)
        build = work / "build" / target / name
        build.mkdir(parents=True, exist_ok=True)
        log = work / f"{target}-{name}.log"
        env = os.environ.copy()
        if name == "libvpx":
            env.update(
                CC="emcc" if target == "wasm" else "clang",
                CXX="em++" if target == "wasm" else "clang++",
                AR="emar" if target == "wasm" else "ar",
                RANLIB="emranlib" if target == "wasm" else "ranlib",
                LD="emcc" if target == "wasm" else "clang",
                CFLAGS=FLAGS if target == "wasm" else "-O2",
                CXXFLAGS=FLAGS if target == "wasm" else "-O2",
                LDFLAGS=FLAGS if target == "wasm" else "",
            )
            run(
                [
                    str(source / "configure"),
                    "--target=generic-gnu",
                    "--prefix=" + str(prefix),
                    "--disable-runtime-cpu-detect",
                    "--disable-vp8",
                    "--disable-vp9-encoder",
                    "--disable-examples",
                    "--disable-tools",
                    "--disable-docs",
                    "--disable-unit-tests",
                    "--disable-webm-io",
                    "--disable-libyuv",
                    "--enable-pic",
                    "--enable-static",
                    "--disable-shared",
                ],
                build,
                env,
                log,
            )
            run(["make", "-j" + str(jobs)], build, env, log)
            run(["make", "install"], build, env, log)
        else:
            command = [
                "cmake",
                "-G",
                "Ninja",
                "-S",
                str(source),
                "-B",
                str(build),
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                "-DBUILD_SHARED_LIBS=OFF",
                "-DENABLE_SAMPLE_PROGRAMS=OFF",
                "-DENABLE_WEBMTS=OFF",
                "-DENABLE_WEBMINFO=OFF",
                "-DENABLE_TESTS=OFF",
            ]
            if target == "wasm":
                command = (
                    ["emcmake"]
                    + command
                    + ["-DCMAKE_C_FLAGS=" + FLAGS, "-DCMAKE_CXX_FLAGS=" + FLAGS, "-DCMAKE_EXE_LINKER_FLAGS=" + FLAGS]
                )
            run(command, work, env, log)
            run(["cmake", "--build", str(build), "-j" + str(jobs)], work, env, log)
            run(["cmake", "--install", str(build)], work, env, log)
        provenance[name] = {
            **data,
            "modifiedSource": False,
            "unmodifiedArchiveFiles": verify_source(archive, source_root),
        }
        print(target, name, "built", flush=True)
    # Generated package metadata can move with the prefix; original source is untouched.
    pc = prefix / "lib/pkgconfig/vpx.pc"
    pc.write_text(
        "\n".join(
            "prefix=${pcfiledir}/../.." if line.startswith("prefix=") else line for line in pc.read_text().splitlines()
        )
        + "\n"
    )
    files = {}
    for directory in ("include", "lib"):
        for path in sorted((prefix / directory).rglob("*")):
            if path.is_file():
                files[path.relative_to(prefix).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(
        schema=1,
        target=target,
        abi=ABI if target == "wasm" else None,
        emscriptenVersion=version,
        sources=provenance,
        files=files,
    )
    (prefix / "movie-codecs.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return provenance


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work-dir", type=Path, required=True)
    ap.add_argument("--prefix", type=Path, required=True)
    ap.add_argument("--archive-root", type=Path, required=True, help="folder holding the pinned archives")
    ap.add_argument("--target", choices=("native", "wasm"), default="wasm")
    ap.add_argument("--jobs", type=int, default=8)
    a = ap.parse_args()
    if a.jobs < 1:
        ap.error("--jobs must be positive")
    build_codecs(a.work_dir, a.prefix, a.archive_root, a.target, a.jobs)
    print("Movie codec package:", a.prefix)


if __name__ == "__main__":
    main()
