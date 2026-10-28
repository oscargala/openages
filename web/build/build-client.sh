#!/usr/bin/env bash
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

# Configure and build the C++ client (plClient) for wasm32-emscripten against a deps prefix.
#
#   PLASMA_WASM_BUILD=<dir> PLASMA_WASM_DEPS=<prefix> EMSDK=<emsdk> web/build/build-client.sh [step ...]
#
# Steps (default: clean configure build):
#   clean         remove what CMake and ninja generate in the build dir, and nothing else
#   configure     configure (keeps the build dir)
#   build         build plClient (ninja -k 0: collect every error)
#
# Environment:
#   PLASMA_WASM_BUILD   required: the build dir. Keep it outside the source checkout.
#   PLASMA_WASM_DEPS    required for configure: the prefix build-deps.sh wrote.
#   EMSDK               required: the emsdk checkout with the Emscripten release web/build/sources.lock
#                       pins (its emsdk record) activated. Any other emcc version is refused: the deps
#                       prefix and the client must come from one compiler.
#   VCPKG_ROOT          the vcpkg checkout whose scripts/buildsystems/vcpkg.cmake the toolchain loads;
#                       default: this repository's vcpkg submodule.
#   PLASMA_PYTHON_ABI=2 (configure) the py2 flavour, plClient-py2.{js,wasm}, against CPython 2.3.3 at
#                       $PLASMA_WASM_PYTHON2 (default $PLASMA_WASM_DEPS/python2); use its own build dir.
#   HARNESS=1           the harness variant (PLASMA_TEST_HOOKS=ON, the testhooks/ overlay), in
#                       $PLASMA_WASM_HARNESS_BUILD (default $PLASMA_WASM_BUILD-harness). Without it, the
#                       product build (PLASMA_TEST_HOOKS=OFF). A build dir holds one variant: the script
#                       refuses a dir whose CMakeCache.txt was configured for the other one.
#   PLASMA_TEST_HOOKS_DIR  (configure, HARNESS=1) the overlay to compile, when it is not this checkout's
#                       testhooks/: CMake's PLASMA_TEST_HOOKS_DIR. The product build never reads it.
#   RELEASE=1           a release build: PRODUCT_EMBED_BUILD_INFO=ON, PRODUCT_RELEASE_BUILD=ON
#                       and PRODUCT_EMBED_BUILD_TIME=OFF, so the client reports the commit of this checkout and
#                       its tag (cmake/BuildInfo.cmake), and its bytes depend on that commit alone. Refused with
#                       HARNESS=1, and while the checkout has changes that are not committed (untracked files
#                       too) before configure, before the build and after it. A successful build writes
#                       <client>.release.json beside the client: the commit, the tag, the SHA-256 of
#                       web/build/sources.lock and of the two client files, which emit_static.py and server.py
#                       turn into the kit's source.json. A build dir holds one kind, release or not: the
#                       script refuses a dir whose CMakeCache.txt was configured for the other one. Without
#                       RELEASE=1 nothing changes: no option is added to the ordinary configure.
#   BUILD_TYPE          CMake build type, default Release: the build that ships. Debug and
#                       RelWithDebInfo also keep the function names and the C++ exceptions' stack traces, which
#                       every harness build (HARNESS=1) keeps too.
#   PLASMA_WASM_CMAKE_ARGS  extra configure arguments, split at spaces (e.g.
#                       -DPRODUCT_EMBED_BUILD_TIME=OFF for reproducible bytes).
#
# Output: <build dir>/Sources/Plasma/Apps/plClient/plClient.{js,wasm} (plClient-py2.* for the py2
# flavour). Logs: <build dir>/logs/{configure,build}.log
[ -z "${BASH_VERSION:-}" ] && exec bash "$0" "$@"
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
PROG=build-client.sh
. "$HERE/common.sh"

case ${HARNESS:-0} in
    1)
        TEST_HOOKS=ON
        [ -n "${PLASMA_WASM_HARNESS_BUILD:-}" ] \
            || need_var PLASMA_WASM_BUILD "the build dir (HARNESS=1 builds in \$PLASMA_WASM_BUILD-harness, or set PLASMA_WASM_HARNESS_BUILD)"
        B=${PLASMA_WASM_HARNESS_BUILD:-$PLASMA_WASM_BUILD-harness}
        ;;
    0)
        TEST_HOOKS=OFF
        need_var PLASMA_WASM_BUILD "the build dir (outside the source checkout)"
        B=$PLASMA_WASM_BUILD
        ;;
    *) die "HARNESS must be 0 or 1, not ${HARNESS}" ;;
esac
case $B in *[[:space:]]*) die "white space in the build dir is not supported: '$B'" ;; esac
case ${RELEASE:-0} in
    1)
        [ "$TEST_HOOKS" = OFF ] || die "RELEASE=1 builds the product only: a harness build (HARNESS=1) is never a release"
        RELEASE_BUILD=ON
        ;;
    0) RELEASE_BUILD=OFF ;;
    *) die "RELEASE must be 0 or 1, not ${RELEASE}" ;;
esac
JOBS=$(host_jobs)
BUILD_TYPE=${BUILD_TYPE:-Release}
if [ $# -gt 0 ]; then STEPS="$*"; else STEPS="clean configure build"; fi
for step in $STEPS; do
    case $step in clean | configure | build) ;; *) die "unknown step: $step" ;; esac
done

use_emsdk

# release_tree: RELEASE=1 builds only a checkout whose every change is committed (untracked files count; ignored
# ones and uninitialised submodules do not), the same test cmake/BuildInfo.cmake makes when it reads the commit.
release_tree() {
    local status
    status=$(git -C "$SRC" status --porcelain --untracked-files=normal --ignore-submodules=none) \
        || die "RELEASE=1: $SRC is not a git checkout"
    [ -z "$status" ] || die "RELEASE=1: $SRC has changes that are not committed, so the build would not match the" \
        "commit it reports; commit them first:"$'\n'"$status"
}

# release_record: after a release build, <client>.release.json beside the client (see RELEASE above). The commit
# must be the one the build started from and the one the client embeds (hsBuildInfo.inl GIT_REV).
release_record() {
    local abi name
    abi=$(sed -n 's/^PLASMA_PYTHON_ABI:STRING=//p' "$B/CMakeCache.txt")
    name=plClient
    [ "$abi" != 2 ] || name=plClient-py2
    [ "$(git -C "$SRC" rev-parse --verify HEAD)" = "$RELEASE_HEAD" ] \
        || die "RELEASE=1: HEAD moved during the build (it started at $RELEASE_HEAD)"
    release_tree
    need_python
    "$HOST_PYTHON" -I - "$SRC" "$B" "$name" "${abi:-3}" "$RELEASE_HEAD" <<'PY'
import hashlib, json, os, re, subprocess, sys
src, build, name, abi, head = sys.argv[1:]
def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()
info = open(os.path.join(build, "Sources/Plasma/CoreLib/hsBuildInfo.inl")).read()
rev = re.search(r'GIT_REV = "([^"]*)"', info).group(1)
product_tag = re.search(r'GIT_TAG = "([^"]*)"', info).group(1)
if rev != head:
    sys.exit(f"build-client.sh: the client embeds revision {rev!r}, not the checkout's commit {head}")
tag = subprocess.run(["git", "-C", src, "describe", "--exact-match", "--tags", "HEAD"],
                     capture_output=True, text=True).stdout.strip() or None
if product_tag != (tag or head[:12]):
    sys.exit(f"build-client.sh: the client embeds tag {product_tag!r}, not {tag or head[:12]!r}")
out = os.path.join(build, "Sources/Plasma/Apps/plClient")
record = {
    "schema": 1,
    "commit": head,
    "tag": tag,
    "productTag": product_tag,
    "sourcesLock": {"path": "web/build/sources.lock", "sha256": sha256(os.path.join(src, "web/build/sources.lock"))},
    "client": name,
    "pythonAbi": abi,
    "files": {f: {"bytes": os.path.getsize(os.path.join(out, f)), "sha256": sha256(os.path.join(out, f))}
              for f in (name + ".js", name + ".wasm")},
}
path = os.path.join(out, name + ".release.json")
with open(path + ".tmp", "w") as f:
    f.write(json.dumps(record, indent=1) + "\n")
os.replace(path + ".tmp", path)
print(f"release record {path}: commit {head}, tag {tag or '(none)'}")
PY
}

# One variant per build dir: a cache from before the option existed has no entry and is a product build.
if [ -f "$B/CMakeCache.txt" ]; then
    configured=$(sed -n 's/^PLASMA_TEST_HOOKS:BOOL=//p' "$B/CMakeCache.txt")
    if [ "${configured:-OFF}" != "$TEST_HOOKS" ]; then
        die "$B is configured with PLASMA_TEST_HOOKS=${configured:-OFF}; this run builds PLASMA_TEST_HOOKS=$TEST_HOOKS" \
            "(HARNESS=${HARNESS:-0}): use the variant's own build dir"
    fi
    configured=$(sed -n 's/^PRODUCT_RELEASE_BUILD:BOOL=//p' "$B/CMakeCache.txt")
    if [ "${configured:-OFF}" != "$RELEASE_BUILD" ]; then
        die "$B is configured with PRODUCT_RELEASE_BUILD=${configured:-OFF}; this run builds" \
            "PRODUCT_RELEASE_BUILD=$RELEASE_BUILD (RELEASE=${RELEASE:-0}): use the kind's own build dir"
    fi
fi
if [ "$RELEASE_BUILD" = ON ]; then
    release_tree
    RELEASE_HEAD=$(git -C "$SRC" rev-parse --verify HEAD)
    stamp "release build of commit $RELEASE_HEAD"
fi

for step in $STEPS; do
    case $step in
    clean)
        # Delete only what CMake and ninja generate, so the other folders a build dir
        # also holds (logs/, test output, ...) survive a clean build.
        stamp "clean: removing the CMake/ninja output in $B"
        for entry in CMakeCache.txt CMakeFiles cmake_install.cmake CTestTestfile.cmake \
                     build.ninja .ninja_deps .ninja_log compile_commands.json hsConfig.h \
                     Sources Scripts; do
            rm -rf "${B:?}/$entry"
        done
        ;;
    configure)
        need_var PLASMA_WASM_DEPS "the deps prefix build-deps.sh wrote"
        vr=${VCPKG_ROOT:-$SRC/vcpkg}
        [ -f "$vr/scripts/buildsystems/vcpkg.cmake" ] \
            || die "no vcpkg checkout at $vr: run 'git submodule update --init vcpkg', or set VCPKG_ROOT"
        mkdir -p "$B/logs"
        # RELEASE=1: after PLASMA_WASM_CMAKE_ARGS, so these win
        RELEASE_ARGS=()
        [ "$RELEASE_BUILD" = OFF ] || RELEASE_ARGS=(-DPRODUCT_EMBED_BUILD_INFO=ON -DPRODUCT_RELEASE_BUILD=ON
                                                   -DPRODUCT_EMBED_BUILD_TIME=OFF)
        HOOKS_ARGS=()
        if [ "$TEST_HOOKS" = ON ] && [ -n "${PLASMA_TEST_HOOKS_DIR:-}" ]; then
            [ -f "$PLASMA_TEST_HOOKS_DIR/CMakeLists.txt" ] \
                || die "PLASMA_TEST_HOOKS_DIR=$PLASMA_TEST_HOOKS_DIR holds no test-hook overlay (CMakeLists.txt)"
            HOOKS_ARGS=(-DPLASMA_TEST_HOOKS_DIR="$(cd "$PLASMA_TEST_HOOKS_DIR" && pwd)")
        fi
        stamp "configure ($BUILD_TYPE, PLASMA_TEST_HOOKS=$TEST_HOOKS, PRODUCT_RELEASE_BUILD=$RELEASE_BUILD) -> $B/logs/configure.log"
        [ ${#HOOKS_ARGS[@]} -eq 0 ] || stamp "test-hook overlay: ${HOOKS_ARGS[0]#-DPLASMA_TEST_HOOKS_DIR=}"
        SECONDS=0
        # shellcheck disable=SC2086 # PLASMA_WASM_CMAKE_ARGS is a list of words
        cmake -G Ninja -S "$SRC" -B "$B" \
            -DCMAKE_TOOLCHAIN_FILE="$SRC/cmake/emscripten/Toolchain.cmake" \
            -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
            -DPLASMA_BUILD_TOOLS=OFF \
            -DPLASMA_BUILD_LAUNCHER=OFF \
            -DPLASMA_BUILD_TESTS=OFF \
            -DPLASMA_BUILD_RESOURCE_DAT=OFF \
            -DPLASMA_STACK_WALKER=OFF \
            -DPLASMA_EXTERNAL_RELEASE=ON \
            -DPLASMA_PIPELINE_DX=OFF \
            -DPLASMA_PIPELINE_GL=OFF \
            -DPLASMA_PIPELINE_METAL=OFF \
            -DPLASMA_TEST_HOOKS="$TEST_HOOKS" \
            ${HOOKS_ARGS[@]+"${HOOKS_ARGS[@]}"} \
            ${PLASMA_PYTHON_ABI:+-DPLASMA_PYTHON_ABI=$PLASMA_PYTHON_ABI} \
            ${PLASMA_WASM_CMAKE_ARGS:-} \
            ${RELEASE_ARGS[@]+"${RELEASE_ARGS[@]}"} \
            > "$B/logs/configure.log" 2>&1 || fail_log "$B/logs/configure.log" "configure"
        stamp "configure done in ${SECONDS}s"
        ;;
    build)
        mkdir -p "$B/logs"
        stamp "build (ninja -j$JOBS -k 0) -> $B/logs/build.log"
        SECONDS=0
        # a release record exists only after a successful release build of the current commit
        rm -f "$B"/Sources/Plasma/Apps/plClient/*.release.json
        ninja -C "$B" -j"$JOBS" -k 0 plClient > "$B/logs/build.log" 2>&1 || fail_log "$B/logs/build.log" "build"
        stamp "build done in ${SECONDS}s"
        [ "$RELEASE_BUILD" = OFF ] || release_record
        ;;
    esac
done
