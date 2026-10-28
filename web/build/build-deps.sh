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

# Build every wasm32-emscripten dependency of the browser client into one prefix.
#
#   PLASMA_WASM_DEPS=<prefix> EMSDK=<emsdk> web/build/build-deps.sh [--rebuild] [step ...]
#
# Steps, in this order by default:
#   tools    m4, autoconf, automake, libtool, autoconf-archive into <prefix>/tools (speex's port runs
#            autoreconf), plus the pinned vcpkg tool
#   vcpkg    the vcpkg ports, PhysX included, into <prefix>/vcpkg_installed/wasm32-emscripten-plasma
#   python   CPython 3.12.9 into <prefix>/python
#   python2  CPython 2.3.3 for the py2 flavour into <prefix>/python2 (web/build/python2/build.sh)
#   openal   OpenALConfig.cmake for Emscripten's -lopenal into <prefix>/emscripten-openal
#   movies   libvpx and libwebm into <prefix>/movies (web/build/movie-codecs.py)
#   check    flag scan, link test and CMake find probe over the prefix (web/build/selftest)
#
# --rebuild  lets the vcpkg step rebuild ports the prefix already holds. Without it, the step refuses when vcpkg
#            would remove or rebuild an installed port: a tree whose triplet, chainload toolchain or ports differ
#            from the ones the prefix was built with gives every port another ABI hash, and vcpkg then removes them
#            all before it builds them again, so a run stopped half way leaves the prefix without them.
#
# Every step runs with one fixed build time (SOURCE_DATE_EPOCH and TZ=UTC; use_build_date in common.sh), so a
# prefix built on another day or host records the same dates.
#
# Environment:
#   PLASMA_WASM_DEPS  required: the prefix (created if missing). Downloads go to <prefix>/downloads
#                     (CPython's tarball to <prefix>/src), builds to <prefix>/build, logs to <prefix>/logs.
#   EMSDK             required: the emsdk checkout with the Emscripten release web/build/sources.lock pins
#                     (its emsdk record) activated. Any other emcc version is refused.
#   VCPKG_ROOT        the vcpkg registry checkout; default: this repository's vcpkg submodule
#                     ("git submodule update --init vcpkg"). It must be at the commit sources.lock pins,
#                     which is also the root vcpkg.json's builtin-baseline.
#   VCPKG_BINARY_SOURCES  vcpkg's binary cache setting, passed through; without it the cache is off
#                     (--binarysource=clear), so every port is built from source.
#   VCPKG_DEFAULT_HOST_TRIPLET  overrides the detected host triplet (arm64-osx, x64-osx, x64-linux,
#                     arm64-linux).
#   PLASMA_WASM_PYTHON_STAGE  python step only: build a private copy into <dir> instead (below).
#   HOST_PYTHON       Python 3.12 or newer for the helper scripts; default python3.
#
# Every download is pinned in web/build/sources.lock and checked before use (web/build/sources.sh).
# Nothing is installed outside the prefix. Global flags of every object: -pthread
# -fwasm-exceptions -sSUPPORT_LONGJMP=wasm (the check step verifies them).
[ -z "${BASH_VERSION:-}" ] && exec bash "$0" "$@"
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
PROG=build-deps.sh
. "$HERE/common.sh"

need_var PLASMA_WASM_DEPS "the dependency prefix to build into (created if missing)"
mkdir -p "$PLASMA_WASM_DEPS"
D=$(abs_path "$PLASMA_WASM_DEPS")
# The chainload toolchain (cmake/emscripten/ChainloadToolchain.cmake) maps this prefix to a stable name in every object
# with -ffile-prefix-map; the vcpkg ports read it through the triplet's environment passthrough.
export PLASMA_WASM_DEPS=$D
PY_VERSION=$(lock_get python-3.12.9 version)
JOBS=$(host_jobs)
FLAGS="-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm"
REBUILD=0
if [ "${1:-}" = --rebuild ]; then REBUILD=1; shift; fi
if [ $# -gt 0 ]; then STEPS="$*"; else STEPS="tools vcpkg python python2 openal movies check"; fi
for s in $STEPS; do
    case $s in tools | vcpkg | python | python2 | openal | movies | check) ;; *) die "unknown step '$s'" ;; esac
done

use_emsdk
need_python
use_build_date
mkdir -p "$D/src" "$D/build" "$D/logs" "$D/tools"
export PATH="$D/tools/bin:$PATH"

# --- host tools ------------------------------------------------------------------------------
# speex's vcpkg port runs autoreconf. GNU m4 comes first because autoconf needs a newer one than
# macOS ships; libtool's macros and autoconf-archive's are installed next to automake's, where
# aclocal finds them without a dirlist. The stamp names the lock records, so a changed pin rebuilds,
# and the tools' prefix: autoconf, automake and libtool write their install prefix into their
# scripts (aclocal starts with "unshift (@INC, '<prefix>/share/automake-1.17')"), so in a prefix
# that was moved they fail in speex's autoreconf ("Can't locate Automake/Config.pm"), and are
# rebuilt here for the prefix's present path.
TOOLS="m4 autoconf automake libtool autoconf-archive"
build_tools() {
    local want name version file
    want=$(for name in $TOOLS; do grep "^$name " "$SOURCES_LOCK"; done; echo "prefix $D/tools")
    if [ -f "$D/tools/.stamp" ] && [ "$(cat "$D/tools/.stamp")" = "$want" ] && [ -x "$D/tools/bin/autoreconf" ]; then
        stamp "tools: present"
    else
        rm -f "$D/tools/.stamp"
        mkdir -p "$D/build/tools"
        for name in $TOOLS; do
            version=$(lock_get "$name" version)
            file=$(lock_fetch "$name" "$D/downloads")
            stamp "tools: $name $version (logs/tools-$name.log)"
            rm -rf "$D/build/tools/$name-$version"
            tar -xf "$file" -C "$D/build/tools"
            (cd "$D/build/tools/$name-$version" && ./configure --prefix="$D/tools" && make -j"$JOBS" && make install) \
                > "$D/logs/tools-$name.log" 2>&1 || fail_log "$D/logs/tools-$name.log" "tools: $name"
        done
        printf '%s\n' "$want" > "$D/tools/.stamp"
    fi
    vcpkg_tool >/dev/null
}

# vcpkg_tool: install the pinned vcpkg tool for this host into <prefix>/tools and print its path.
vcpkg_tool() {
    local record version file dir
    record=$(vcpkg_tool_record)
    version=$(lock_get "$record" version)
    dir=$D/tools/vcpkg-$version
    if ! lock_verify "$record" "$dir/vcpkg" 2>/dev/null; then
        file=$(lock_fetch "$record" "$D/downloads")
        mkdir -p "$dir"
        cp "$file" "$dir/vcpkg"
        chmod 755 "$dir/vcpkg"
    fi
    echo "$dir/vcpkg"
}

# --- vcpkg ports, PhysX included ----------------------------------------------------------------
# The dependency set is the root vcpkg.json. Its platform expressions leave out on emscripten what
# the browser client does not use or builds another way: curl, fontconfig, libepoxy, openal-soft
# (Emscripten's -lopenal instead), python3 (the python step), libvpx and libwebm (the movies step).
# The triplet is Scripts/Triplets/wasm32-emscripten-plasma.cmake; PhysX is the Scripts/Ports/physx
# overlay, which applies emscripten.patch on Emscripten triplets.
# The manifest pins the registry with builtin-baseline, which vcpkg resolves from the git history
# of VCPKG_ROOT, so the checkout must hold that commit; the submodule does. The host triplet only
# selects vcpkg's own CMake helper ports, which contain no compiled code.
# VCPKG_FORCE_SYSTEM_BINARIES makes vcpkg use the CMake, Ninja and git on PATH instead of
# downloading its own, so the ports build with the same tools on every host.
build_vcpkg() {
    local vr head baseline pinned tool host binsrc
    vr=$(abs_path "${VCPKG_ROOT:-$ROOT/vcpkg}")
    [ -f "$vr/scripts/buildsystems/vcpkg.cmake" ] \
        || die "VCPKG_ROOT=$vr is not a vcpkg checkout (for the submodule: git submodule update --init vcpkg)"
    head=$(git -C "$vr" rev-parse HEAD 2>/dev/null) || die "VCPKG_ROOT=$vr is not a git checkout; vcpkg needs its history"
    pinned=$(lock_get vcpkg commit)
    baseline=$("$HOST_PYTHON" -I -c 'import json, sys; print(json.load(open(sys.argv[1]))["builtin-baseline"])' "$ROOT/vcpkg.json")
    [ "$pinned" = "$baseline" ] || die "sources.lock pins vcpkg $pinned but vcpkg.json's builtin-baseline is $baseline"
    [ "$head" = "$pinned" ] || die "VCPKG_ROOT=$vr is at $head; this build needs vcpkg $pinned (git submodule update --init vcpkg)"
    tool=$(vcpkg_tool)
    host=$(host_triplet)
    binsrc=--binarysource=clear
    [ -z "${VCPKG_BINARY_SOURCES:-}" ] || binsrc=
    # vcpkg_run <log> [option ...]: vcpkg install of the root manifest into the prefix
    vcpkg_run() {
        local log=$1
        shift
        (cd "$ROOT" && VCPKG_DISABLE_METRICS=1 VCPKG_FORCE_SYSTEM_BINARIES=1 "$tool" install \
            --vcpkg-root "$vr" --x-manifest-root "$ROOT" \
            --triplet wasm32-emscripten-plasma --host-triplet "$host" \
            --x-install-root "$D/vcpkg_installed" --x-buildtrees-root "$D/build/vcpkg-buildtrees" \
            --x-packages-root "$D/build/vcpkg-packages" --downloads-root "$D/downloads" $binsrc ${1+"$@"}) \
            > "$log" 2>&1 || fail_log "$log" "vcpkg install ${*:-}"
    }
    # The installed ports are listed in vcpkg's status database. When there are some, the plan of a dry run says
    # whether vcpkg would remove or rebuild any of them (its "will be rebuilt" and "will be removed" lists).
    if [ "$REBUILD" = 0 ] && [ -s "$D/vcpkg_installed/vcpkg/status" ]; then
        stamp "vcpkg: plan against the installed ports (logs/vcpkg-plan.log)"
        vcpkg_run "$D/logs/vcpkg-plan.log" --dry-run
        if grep -q -E '^The following packages will be (rebuilt|removed)' "$D/logs/vcpkg-plan.log"; then
            awk '/^The following packages will be (rebuilt|removed)/ { p = 1; print; next } /^[^ ]/ { p = 0 } p' \
                "$D/logs/vcpkg-plan.log" >&2
            die "vcpkg would remove or rebuild ports that $D already holds (plan: $D/logs/vcpkg-plan.log). This tree's triplet, chainload toolchain or ports differ from the ones the prefix was built with. Build another prefix, or pass --rebuild to rebuild these ports in place"
        fi
    fi
    stamp "vcpkg: install, host triplet $host (logs/vcpkg-install.log)"
    vcpkg_run "$D/logs/vcpkg-install.log"
    stamp "vcpkg: done"
}

# --- CPython 3.12.9 -----------------------------------------------------------------------------
# Cross build per Tools/wasm: native build python first, then the emscripten host with pthreads.
# All extension modules are static builtins (wasm MODULE_BUILDTYPE=static). zlib, bzip2 and expat
# come from the vcpkg tree (one copy of each in the final link).
# No PY_CALL_TRAMPOLINE. With -DPY_CALL_TRAMPOLINE (bpo-47162) CPython calls every C
# method, getter and setter through a JS frame (EM_JS wasmTable.get(f)(...)), so that a function
# whose parameter list does not match its PyCFunction type does not trap. JSPI cannot suspend
# across a JS frame, so a GPU wait or a present inside a Python call trapped with SuspendError.
# The glue declares every slot with the parameter list CPython calls it with (pyGlueHelpers.h), so
# CPython is built as Tools/wasm builds it: direct call_indirect, no trampoline.
# Also as Tools/wasm builds it (wasm_build.py's configure_cmd and wasm_assets.py's --prefix default): configure is
# named by a path relative to the build dir, and the prefix is the default /usr/local. libpython compiles both into
# getpath.c (VPATH, PREFIX and EXEC_PREFIX), so with the build host's absolute paths there the client carried them;
# the client sets its own module search path and getpath finds its prefix from that, so neither value is used at run
# time. make install writes the tree under DESTDIR, which then becomes <prefix>/python. The deps prefix (and a stage
# dir) is mapped to a stable name in the objects' __FILE__ strings, as the chainload toolchain does for the ports.
#
# PLASMA_WASM_PYTHON_STAGE=<dir> builds a private copy instead: it reads the prefix (the source
# tarball, the native build python, the vcpkg zlib/bzip2/expat) and writes the source copy, the
# build, the logs and the result (<dir>/python) under <dir> only. Point a client configure at it with
# PLASMA_WASM_PYTHON=<dir>/python (cmake/emscripten/Toolchain.cmake).
build_python() {
    local V=$D/vcpkg_installed/wasm32-emscripten-plasma
    local SRC=$D/src/Python-$PY_VERSION PYB=$D/build/python-wasm PYP=$D/python PYL=$D/logs
    local tarball S="" BUILD_PY Z REL MAP
    if [ -n "${PLASMA_WASM_PYTHON_STAGE:-}" ]; then
        [ -f "$D/src/Python-$PY_VERSION.tar.xz" ] && [ -d "$D/build/python-native" ] \
            || die "python stage: needs the tarball and the native build python in $D"
        lock_verify python-3.12.9 "$D/src/Python-$PY_VERSION.tar.xz" || die "python stage: unverified tarball"
        mkdir -p "$PLASMA_WASM_PYTHON_STAGE"
        S=$(abs_path "$PLASMA_WASM_PYTHON_STAGE")
        mkdir -p "$S/src" "$S/logs"
        rm -rf "$S/src/Python-$PY_VERSION"
        tar -xf "$D/src/Python-$PY_VERSION.tar.xz" -C "$S/src"
        SRC=$S/src/Python-$PY_VERSION PYB=$S/build/python-wasm PYP=$S/python PYL=$S/logs
        # the prefix's native build python must not write bytecode into the prefix
        export PYTHONDONTWRITEBYTECODE=1
        stamp "python: private stage $S (reads $D)"
    else
        tarball=$(lock_fetch python-3.12.9 "$D/src")
        if [ ! -d "$SRC" ]; then
            stamp "python: unpack $PY_VERSION"
            tar -xf "$tarball" -C "$D/src"
        fi
        mkdir -p "$D/build/python-native"
    fi
    stamp "python: native build python"
    cd "$D/build/python-native"
    # Configured by a relative path, as the emscripten host below: a build-dir python finds its standard library
    # through VPATH (Modules/getpath.py:492 joins the executable's dir and VPATH), so it keeps working when the prefix
    # is moved. One configured by an absolute path that no longer exists does not start (no encodings module), and is
    # rebuilt.
    for BUILD_PY in python.exe python; do
        if [ -x "$BUILD_PY" ] && ! "./$BUILD_PY" -I -B -c 'import encodings' > /dev/null 2>&1; then
            stamp "python: the native build python does not start here (built for another path); rebuilding it"
            find . -mindepth 1 -delete
        fi
    done
    if [ ! -x python.exe ] && [ ! -x python ]; then
        REL=$("$HOST_PYTHON" -I -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$SRC" "$PWD")
        "$REL/configure" --prefix="$D/build/python-native/install" > "$D/logs/python-native-configure.log" 2>&1 \
            || fail_log "$D/logs/python-native-configure.log" "python: native configure"
        make -j"$JOBS" > "$D/logs/python-native-make.log" 2>&1 || fail_log "$D/logs/python-native-make.log" "python: native make"
    fi
    # python.exe on a case-insensitive file system (macOS), python elsewhere
    BUILD_PY=$D/build/python-native/python.exe
    [ -x "$BUILD_PY" ] || BUILD_PY=$D/build/python-native/python

    stamp "python: emscripten host build"
    rm -rf "$PYB" "$PYP"
    mkdir -p "$PYB"
    cd "$PYB"
    REL=$("$HOST_PYTHON" -I -c 'import os, sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$SRC" "$PYB")
    MAP="-ffile-prefix-map=$D=wasm-deps"
    [ -z "$S" ] || MAP="$MAP -ffile-prefix-map=$S=wasm-deps"
    CONFIG_SITE=$SRC/Tools/wasm/config.site-wasm32-emscripten \
    ZLIB_CFLAGS="-I$V/include" ZLIB_LIBS="-L$V/lib -lz" \
    BZIP2_CFLAGS="-I$V/include" BZIP2_LIBS="-L$V/lib -lbz2" \
    LIBEXPAT_CFLAGS="-I$V/include" LIBEXPAT_LDFLAGS="-L$V/lib -lexpat" \
    py_cv_module_xxlimited=n/a py_cv_module_xxlimited_35=n/a \
    CFLAGS="$FLAGS $MAP" CXXFLAGS="$FLAGS $MAP" LDFLAGS="$FLAGS" \
    emconfigure "$REL/configure" --host=wasm32-unknown-emscripten --build="$("$SRC/config.guess")" \
        --with-emscripten-target=browser --enable-wasm-pthreads --disable-shared \
        --without-pymalloc --disable-ipv6 --with-system-expat --with-ensurepip=no \
        --with-build-python="$BUILD_PY" --prefix=/usr/local > "$PYL/python-wasm-configure.log" 2>&1 \
        || fail_log "$PYL/python-wasm-configure.log" "python: configure"
    make -j"$JOBS" > "$PYL/python-wasm-make.log" 2>&1 || fail_log "$PYL/python-wasm-make.log" "python: make"
    make install DESTDIR="$PYB/destdir" > "$PYL/python-wasm-install.log" 2>&1 \
        || fail_log "$PYL/python-wasm-install.log" "python: make install"
    mv "$PYB/destdir/usr/local" "$PYP"
    rm -rf "$PYB/destdir"
    # The trampoline's JS imports must be absent (they exist only with PY_CALL_TRAMPOLINE).
    emnm "$PYP/lib/libpython3.12.a" > "$PYL/python-wasm-nm.txt" 2>/dev/null || true
    if grep -q -e TrampolineCall -e trampoline_call "$PYL/python-wasm-nm.txt"; then
        die "python: libpython3.12.a still has the PY_CALL_TRAMPOLINE JS trampolines"
    fi
    # Helper archives the interpreter links but `make install` does not install.
    cp Modules/_decimal/libmpdec/libmpdec.a "$PYP/lib/libmpdec.a"
    cp Modules/_hacl/libHacl_Hash_SHA2.a "$PYP/lib/libHacl_Hash_SHA2.a"
    # Zipped stdlib (same content as lib/python3.12, minus tests), built by the wasm target.
    Z=$(find "$PYB" -name python312.zip -print -quit)
    if [ -n "$Z" ]; then cp "$Z" "$PYP/lib/python312.zip"; fi
    tail -1 "$PYL/python-wasm-make.log"
    stamp "python: done"
}

install_openal() {
    mkdir -p "$D/emscripten-openal/share/OpenAL"
    cp "$ROOT/cmake/emscripten/OpenALConfig.cmake" "$D/emscripten-openal/share/OpenAL/OpenALConfig.cmake"
    stamp "openal: config installed"
}

# The movie codecs: movie-codecs.py builds from the two pinned archives, which are downloaded and
# verified here first. Configuring a client never runs this.
build_movies() {
    lock_fetch libvpx "$D/downloads" >/dev/null
    lock_fetch libwebm "$D/downloads" >/dev/null
    stamp "movies: libvpx and libwebm (logs in build/movies)"
    "$HOST_PYTHON" -I "$HERE/movie-codecs.py" --work-dir "$D/build/movies" --prefix "$D/movies" \
        --archive-root "$D/downloads" --target wasm --jobs "$JOBS"
}

check() {
    local libs=("$D/vcpkg_installed/wasm32-emscripten-plasma/lib" "$D/python/lib")
    [ ! -d "$D/python2/lib" ] || libs+=("$D/python2/lib")
    [ ! -d "$D/movies/lib" ] || libs+=("$D/movies/lib")
    stamp "check: flag scan (logs/check-deps.log)"
    "$HOST_PYTHON" -I "$ROOT/web/build/check-deps.py" "${libs[@]}" > "$D/logs/check-deps.log" 2>&1 \
        || { cat "$D/logs/check-deps.log"; die "check: flag scan failed"; }
    tail -1 "$D/logs/check-deps.log"
    stamp "check: link test (logs/link-test.log)"
    PLASMA_WASM_DEPS=$D bash "$ROOT/web/build/selftest/link-test/build.sh" > "$D/logs/link-test.log" 2>&1 \
        || fail_log "$D/logs/link-test.log" "check: link test"
    tail -1 "$D/logs/link-test.log"
    stamp "check: CMake find probe with the client's find modules (logs/find-probe-*.log)"
    rm -rf "$D/build/find-probe"
    PLASMA_WASM_DEPS=$D cmake -G Ninja -S "$ROOT/web/build/selftest/find-probe" -B "$D/build/find-probe" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/emscripten/Toolchain.cmake" \
        > "$D/logs/find-probe-configure.log" 2>&1 || fail_log "$D/logs/find-probe-configure.log" "check: find probe configure"
    cmake --build "$D/build/find-probe" -v > "$D/logs/find-probe-build.log" 2>&1 \
        || fail_log "$D/logs/find-probe-build.log" "check: find probe build"
    node "$D/build/find-probe/find_probe.js"
}

for s in $STEPS; do
    case $s in
        tools) build_tools ;;
        vcpkg) build_vcpkg ;;
        python) build_python ;;
        python2) PLASMA_WASM_DEPS=$D bash "$HERE/python2/build.sh" build ;;
        openal) install_openal ;;
        movies) build_movies ;;
        check) check ;;
    esac
done
stamp "build-deps: done ($STEPS)"
