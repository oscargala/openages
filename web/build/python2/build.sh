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

# CPython 2.3.3 (Cypython-2.3.3 of OpenUru CWE-ou-minkata) as a wasm32-emscripten static library.
#
#   web/build/build-deps.sh python2                 # via the deps script
#   web/build/python2/build.sh [fetch|build|scan]   # directly; default: build
#
# Environment: PLASMA_WASM_DEPS (required) and EMSDK (required, the pinned Emscripten), as build-deps.sh.
# Source:  $PLASMA_WASM_PYTHON2_SRC, default $PLASMA_WASM_DEPS/cypython-2.3.3: the Cypython-2.3.3 tree of
#          CWE-ou-minkata 7978641e0cd7, Build/VS2010/SDKs/XPlatform/Cypython-2.3.3. Every mode first
#          checks the pin in web/build/sources.lock (record cypython-2.3.3): the vendored proof
#          cypython-ou-minkata-proof.txt must show that this commit holds this tree at this path
#          (web/build/commit-proof.py). It then checks the tree against the pin (git tree id and
#          SHA-256 manifest, web/build/tree-hash.py) and refuses any other tree. The tree is only read.
# fetch:   rebuilds that tree at the default path when it is missing, without contacting
#          OpenUru: Cyan's 2011 tree from H-uru/Plasma's first commit (public, read anonymously over
#          HTTPS), every CRLF converted to LF, then the vendored OpenUru delta cypython-ou-minkata.patch.
#          build and scan run it when the default tree is missing.
# Work:    the source is copied to <stage>/build/python2-wasm/src and the patch series
#          web/build/python2/patches/*.patch is applied to the copy.
# Output:  <stage>/python2/{lib/libpython2.3.a, include/python2.3/, lib/python2.3/LICENSE.txt}
#          <stage> = $PLASMA_WASM_PYTHON2_STAGE, default $PLASMA_WASM_DEPS.
# Licence: CPython 2.3.3 is under the Python Software Foundation licence, not the GPL. LICENSE-PSF.txt
#          in this folder is the tree's LICENSE, verbatim (the build refuses a copy that differs). It
#          covers the CPython sources (with the patches in patches/ applied), and config.c and
#          pyconfig.h, which are filled in from CPython's templates. The build installs it with the
#          library, where CPython's own install puts it (Makefile.pre.in of 2.3.3:
#          $(LIBDEST)/LICENSE.txt). The scripts in this folder (build.sh, make_signature_patch.py,
#          scan_signatures.py) are GPL, as their headers say.
#          The licence asks a derived work to summarize its changes to Python 2.3. This build's are:
#          Cyan's 2011 tree, which differs from python.org's 2.3.3 release (Cyan's static-build
#          configuration and project files, and code changes in 49 files); OpenUru's delta
#          (cypython-ou-minkata.patch, listed in its preamble); the four patches in patches/ (each
#          explains itself); and the Emscripten configuration in config.c and pyconfig.h.
# Flags:   the client's global flags (-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm, build-deps.sh) so the
#          archive links into the pthread client, plus -std=gnu89 -fno-strict-aliasing for 2003-era C.
# Date:    Modules/getbuildinfo.c compiles __DATE__ and __TIME__ into sys.version. The units are compiled with
#          the deps build's fixed build time (use_build_date, web/build/common.sh), so every build of the
#          library is the same bytes, whenever and wherever it is built.
# scan:    the signature scan (-Wcast-function-type-strict, syntax only) over every unit, classified by
#          wasm signature by scan_signatures.py: <stage>/build/python2-wasm/scan.{txt,json}.
[ -z "${BASH_VERSION:-}" ] && exec bash "$0" "$@"
set -euo pipefail
# byte order for globs and sorting: the archive's member order must not depend on the locale
export LC_ALL=C

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
PROG=python2
. "$ROOT/web/build/common.sh"

MODE=${1:-build}
case $MODE in fetch | build | scan) ;; *) echo "usage: $0 [fetch|build|scan]" >&2; exit 2 ;; esac
need_var PLASMA_WASM_DEPS "the dependency prefix (build-deps.sh)"
D=$(abs_path "$PLASMA_WASM_DEPS")
SRC0=${PLASMA_WASM_PYTHON2_SRC:-$D/cypython-2.3.3}
JOBS=$(host_jobs)
FLAGS=(-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
CFLAGS=(-O2 -std=gnu89 -fno-strict-aliasing -DNDEBUG)
# os module name (pyconfig.h): nt, as Cyan's client (default), or posix
[ "${PY2_OS_MODULE:-nt}" != posix ] || CFLAGS+=(-DPY2_OS_MODULE_POSIX)
need_python

pystamp() { stamp "python2: $*"; }

# verify_pin: the lock's commit, path and tree of Cypython-2.3.3 belong together, or stop. The proof is
# pinned by SHA-256 like the delta; commit-proof.py recomputes the git id of each object in it and
# follows the path from the commit to the tree, so no other commit, path or tree passes.
verify_pin() {
    local proof
    proof=$ROOT/$(lock_get cypython-2.3.3-proof file)
    lock_verify cypython-2.3.3-proof "$proof" || die "the vendored proof does not match sources.lock"
    "$HOST_PYTHON" -I "$ROOT/web/build/commit-proof.py" check "$proof" --commit "$(lock_get cypython-2.3.3 commit)" \
        --path "$(lock_get cypython-2.3.3 path)" --tree "$(lock_get cypython-2.3.3 tree)" > /dev/null \
        || die "the pin is not proven: web/build/sources.lock cypython-2.3.3 names a commit, path and tree that $proof does not link"
}

# verify_tree <dir>: the pinned Cypython-2.3.3 tree, or stop.
verify_tree() {
    "$HOST_PYTHON" -I "$ROOT/web/build/tree-hash.py" "$1" \
        --expect-tree "$(lock_get cypython-2.3.3 tree)" --expect-manifest "$(lock_get cypython-2.3.3 manifest-sha256)" \
        > /dev/null || die "$1 is not the pinned Cypython-2.3.3 tree (web/build/sources.lock cypython-2.3.3)"
}

# The temporary folder of a fetch in progress. fetch_cleanup removes it however the script ends: a
# die, a failed command, Ctrl-C (INT) or a kill (TERM). Without it an interrupted fetch left a
# .cypython-fetch.* folder beside the tree.
FETCH_TMP=
fetch_cleanup() {
    [ -z "$FETCH_TMP" ] || rm -rf "$FETCH_TMP"
    FETCH_TMP=
}

# fetch: rebuild the pinned tree at $SRC0 (see the header). Works in a temporary folder beside it and
# moves the result into place only after it matched the pin.
fetch() {
    local url commit path tree patch tmp
    [ ! -e "$SRC0" ] || die "$SRC0 already exists; fetch only creates a missing tree"
    url=$(lock_get cypython-2.3.3-cwe url)
    commit=$(lock_get cypython-2.3.3-cwe commit)
    path=$(lock_get cypython-2.3.3-cwe path)
    tree=$(lock_get cypython-2.3.3-cwe tree)
    patch=$ROOT/$(lock_get cypython-2.3.3-ou-delta file)
    lock_verify cypython-2.3.3-ou-delta "$patch" || die "the vendored delta does not match sources.lock"
    mkdir -p "$(dirname "$SRC0")"
    tmp=$(mktemp -d "$(dirname "$SRC0")/.cypython-fetch.XXXXXX")
    FETCH_TMP=$tmp
    # A signal exits with the shell's usual status (128 + its number); the EXIT trap then cleans up.
    trap fetch_cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    pystamp "fetch: $url $commit:$path"
    git init -q "$tmp/repo"
    # no line-ending or other conversion on checkout: the bytes must be the committed ones
    echo '* -text' > "$tmp/repo/.git/info/attributes"
    git -C "$tmp/repo" config core.autocrlf false
    git -C "$tmp/repo" sparse-checkout set --no-cone "/$path/" 2>/dev/null
    git -C "$tmp/repo" fetch -q --depth 1 --filter=blob:none "$url" "$commit"
    [ "$(git -C "$tmp/repo" rev-parse FETCH_HEAD)" = "$commit" ] || die "fetch: got another commit than $commit"
    [ "$(git -C "$tmp/repo" rev-parse "FETCH_HEAD:$path")" = "$tree" ] || die "fetch: $path at $commit is not tree $tree"
    git -C "$tmp/repo" -c advice.detachedHead=false checkout -q FETCH_HEAD
    "$HOST_PYTHON" -I "$ROOT/web/build/tree-hash.py" "$tmp/repo/$path" --expect-tree "$tree" > /dev/null \
        || die "fetch: the checked-out tree differs from $tree"
    # OpenUru's repository stores the tree with LF line endings
    "$HOST_PYTHON" -I - "$tmp/repo/$path" <<'PY'
import os, sys
for top, dirs, files in os.walk(sys.argv[1]):
    for name in files:
        path = os.path.join(top, name)
        with open(path, "rb") as f:
            data = f.read()
        if b"\r\n" in data:
            with open(path, "wb") as f:
                f.write(data.replace(b"\r\n", b"\n"))
PY
    git -C "$tmp/repo" apply --whitespace=nowarn --directory="$path" "$patch" || die "fetch: the OpenUru delta does not apply"
    verify_tree "$tmp/repo/$path"
    mv "$tmp/repo/$path" "$SRC0"
    fetch_cleanup
    trap - EXIT INT TERM
    pystamp "fetch: $SRC0 is the pinned tree $(lock_get cypython-2.3.3 tree)"
}

verify_pin
if [ "$MODE" = fetch ]; then
    fetch
    exit 0
fi
if [ ! -e "$SRC0" ] && [ -z "${PLASMA_WASM_PYTHON2_SRC:-}" ]; then fetch; fi
[ -d "$SRC0" ] || die "no Cypython tree at $SRC0"
verify_tree "$SRC0"
cmp -s "$HERE/LICENSE-PSF.txt" "$SRC0/LICENSE" || die "$HERE/LICENSE-PSF.txt is not the pinned tree's LICENSE"
use_emsdk
use_build_date
mkdir -p "${PLASMA_WASM_PYTHON2_STAGE:-$D}"
S=$(abs_path "${PLASMA_WASM_PYTHON2_STAGE:-$D}")
W=$S/build/python2-wasm
P=$S/python2

prepare() {
    local p
    # logs/ holds only per-unit logs; every aggregate is written outside it, so no glob can read its own output
    rm -rf "$W/src" "$W/obj" "$W/logs"
    mkdir -p "$W/src" "$W/obj" "$W/logs" "$W/pyconfig"
    cp -R "$SRC0/." "$W/src/"
    chmod -R u+w "$W/src"
    for p in "$HERE"/patches/*.patch; do
        [ -e "$p" ] || continue
        patch -s -p1 -d "$W/src" < "$p" || die "patch $(basename "$p") failed"
    done
    cp "$HERE/pyconfig.h" "$W/pyconfig/pyconfig.h"
    cp "$HERE/config.c" "$W/src/Modules/config_wasm.c"
    grep -v '^#' "$HERE/units.txt" | grep -v '^$' > "$W/units.list"
    echo Modules/config_wasm.c >> "$W/units.list"
}

# Per-unit defines (Makefile.pre.in of 2.3.3: getpath.o, getbuildinfo.o), into DEFS.
unit_defs() {
    DEFS=()
    case $1 in
        Modules/getpath.c) DEFS=('-DPYTHONPATH=":plat-emscripten"' '-DPREFIX="/python2"' '-DEXEC_PREFIX="/python2"'
                                 '-DVERSION="2.3"' '-DVPATH=""') ;;
    esac
}

# quoted <word> ...: the words as one shell-quoted command line
quoted() {
    local w out=
    for w in "$@"; do out="$out $(printf '%q' "$w")"; done
    printf '%s' "${out# }"
}

# Every unit is one line of <W>/logs/<mode>.cmds, run JOBS at a time.
compile_all() {
    local mode=$1 u o args
    : > "$W/logs/$mode.cmds"
    while IFS= read -r u; do
        o=${u//\//_}
        o=$W/obj/${o%.c}.o
        args=(-I"$W/pyconfig" -I"$W/src/include" "${FLAGS[@]}" "${CFLAGS[@]}")
        unit_defs "$u"
        if [ "$mode" = scan ]; then
            args+=(-fsyntax-only -Wcast-function-type-strict ${DEFS[@]+"${DEFS[@]}"} "$u")
            echo "cd $(quoted "$W/src") && emcc $(quoted "${args[@]}") > $(quoted "$W/logs/scan-$(basename "$u").log") 2>&1" \
                >> "$W/logs/$mode.cmds"
        else
            args+=(${DEFS[@]+"${DEFS[@]}"} -c "$u" -o "$o")
            echo "cd $(quoted "$W/src") && emcc $(quoted "${args[@]}") > $(quoted "$W/logs/cc-$(basename "$u").log") 2>&1 || echo FAIL $u" \
                >> "$W/logs/$mode.cmds"
        fi
    done < "$W/units.list"
    tr '\n' '\0' < "$W/logs/$mode.cmds" | xargs -0 -P "$JOBS" -n 1 bash -c > "$W/logs/$mode.result" 2>&1 || true
}

case $MODE in
    build)
        pystamp "prepare $W (patches: $(ls "$HERE"/patches/*.patch 2>/dev/null | wc -l | tr -d ' '))"
        prepare
        pystamp "compile $(wc -l < "$W/units.list" | tr -d ' ') units"
        compile_all build
        if grep -q FAIL "$W/logs/build.result"; then
            grep FAIL "$W/logs/build.result" >&2
            for f in $(grep FAIL "$W/logs/build.result" | awk '{print $2}'); do tail -20 "$W/logs/cc-$(basename "$f").log" >&2; done
            exit 1
        fi
        cat "$W"/logs/cc-*.c.log > "$W/warnings.log"
        pystamp "warnings: $(grep -c 'warning:' "$W/warnings.log" || true)"
        rm -rf "$P"
        mkdir -p "$P/lib" "$P/include/python2.3"
        emar rcs "$P/lib/libpython2.3.a" "$W"/obj/*.o
        cp "$W"/src/include/*.h "$W/pyconfig/pyconfig.h" "$P/include/python2.3/"
        mkdir -p "$P/lib/python2.3"
        cp "$SRC0/LICENSE" "$P/lib/python2.3/LICENSE.txt"
        pystamp "done: $P/lib/libpython2.3.a ($(wc -c < "$P/lib/libpython2.3.a" | tr -d ' ') bytes), licence $P/lib/python2.3/LICENSE.txt"
        ;;
    scan)
        pystamp "scan (prepare $W)"
        prepare
        compile_all scan
        cat "$W"/logs/scan-*.c.log > "$W/scan-all.log"
        "$HOST_PYTHON" -I "$HERE/scan_signatures.py" "$W/scan-all.log" "$W/src" "$W/scan.json" > "$W/scan.txt"
        sed -n "1,/^non-matching by file/p" "$W/scan.txt"
        ;;
esac
