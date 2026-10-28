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
# shellcheck shell=bash

# Shared helpers of the browser build scripts, sourced by build-deps.sh, build-client.sh,
# python2/build.sh and web/build/selftest/link-test/build.sh. The caller sets PROG (its name, for messages).
#
# The scripts run on macOS and Linux with nothing but bash 3.2 or newer, a C/C++ compiler, make,
# CMake, Ninja, git, curl and Python 3.12 or newer: no Homebrew, no sysctl, no "stat -f", no
# particular Python distribution. Every input path comes from an argument or the environment,
# and a missing one is an error, never a guess.

. "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sources.sh"

die() { echo "${PROG:-build}: $*" >&2; exit 2; }
stamp() { echo "$(date +%H:%M:%S) $*"; }

# need_var <NAME> <what it is>: fail unless the environment variable is set and not empty.
need_var() {
    eval "[ -n \"\${$1:-}\" ]" || die "$1 is not set: $2"
}

# abs_path <path>: the absolute form of an existing directory, as the shell names it (symbolic links
# kept). Paths with white space are refused: autotools-based builds cannot handle them.
abs_path() {
    case $1 in *[[:space:]]*) die "white space in a path is not supported: '$1'" ;; esac
    [ -d "$1" ] || die "not a directory: $1"
    (cd "$1" && pwd)
}

host_jobs() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2; }

# use_build_date: give the dependency builds one fixed build time, so that a prefix built on any day, on any host,
# records the same date strings. Three inputs record the time of the build:
# - CPython's Modules/getbuildinfo.c (2.3.3 and 3.12.9) compiles __DATE__ and __TIME__ into sys.version;
# - libjpeg-turbo's CMakeLists.txt writes the day into its version string (string(TIMESTAMP DEFAULT_BUILD "%Y%m%d"));
# - CPython 3's py_compile writes timestamp pycs, unless SOURCE_DATE_EPOCH is set: then checked-hash pycs, which
#   carry the source's hash instead of its mtime.
# clang reads SOURCE_DATE_EPOCH for __DATE__ and __TIME__ (as UTC), and CMake's string(TIMESTAMP) reads it too, but
# formats it in the local time zone; TZ=UTC makes the day the same on every host. The value is the commit time of the
# vcpkg pin (web/build/sources.lock), a date the inputs already carry; any fixed value would do, and a different one
# changes only those strings. sys.version keeps its shape: "2.3.3 (#1, Jul 30 2026, 01:36:33)".
# The client build does not use it: plClient's own build time is PRODUCT_EMBED_BUILD_TIME's.
BUILD_DATE_EPOCH=1785375393     # 2026-07-30 01:36:33 UTC
use_build_date() {
    export SOURCE_DATE_EPOCH=$BUILD_DATE_EPOCH TZ=UTC
}

# The vcpkg triplet of the build machine, for vcpkg's host-only ports. VCPKG_DEFAULT_HOST_TRIPLET,
# vcpkg's own variable, overrides it; cmake/emscripten/Toolchain.cmake makes the same choice.
host_triplet() {
    if [ -n "${VCPKG_DEFAULT_HOST_TRIPLET:-}" ]; then
        echo "$VCPKG_DEFAULT_HOST_TRIPLET"
        return
    fi
    case "$(uname -s)/$(uname -m)" in
        Darwin/arm64) echo arm64-osx ;;
        Darwin/x86_64) echo x64-osx ;;
        Linux/x86_64) echo x64-linux ;;
        Linux/aarch64 | Linux/arm64) echo arm64-linux ;;
        *) die "no vcpkg host triplet known for $(uname -s)/$(uname -m): set VCPKG_DEFAULT_HOST_TRIPLET" ;;
    esac
}

# The sources.lock record of the vcpkg tool binary for this machine.
vcpkg_tool_record() {
    case "$(uname -s)/$(uname -m)" in
        Darwin/*) echo vcpkg-tool-macos ;;
        Linux/x86_64) echo vcpkg-tool-linux-x86_64 ;;
        Linux/aarch64 | Linux/arm64) echo vcpkg-tool-linux-arm64 ;;
        *) die "no pinned vcpkg tool for $(uname -s)/$(uname -m)" ;;
    esac
}

# use_emsdk: put the pinned Emscripten on PATH and refuse any other version. EMSDK names an emsdk
# checkout; its emsdk_env.sh is sourced here. Every library of the deps prefix and the client must
# come from one compiler: a different emcc changes the ABI hashes vcpkg records and the code it emits.
use_emsdk() {
    local want have
    need_var EMSDK "the emsdk checkout with Emscripten $(lock_get emsdk version) installed and activated"
    [ -f "$EMSDK/emsdk_env.sh" ] || die "EMSDK=$EMSDK has no emsdk_env.sh"
    # emsdk_env.sh reads variables that may be unset
    set +u
    . "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
    set -u
    want=$(lock_get emsdk version)
    command -v emcc >/dev/null 2>&1 || die "no emcc after sourcing $EMSDK/emsdk_env.sh: run 'emsdk install $want' and 'emsdk activate $want'"
    have=$(emcc -dumpversion 2>/dev/null) || die "emcc does not run"
    [ "$have" = "$want" ] || die "emcc is Emscripten $have; this build is pinned to $want (web/build/sources.lock). Run 'emsdk install $want' and 'emsdk activate $want' in $EMSDK"
}

# need_python: HOST_PYTHON (default python3) must be Python 3.12 or newer (tarfile's extraction filters).
need_python() {
    HOST_PYTHON=${HOST_PYTHON:-python3}
    command -v "$HOST_PYTHON" >/dev/null 2>&1 || die "no $HOST_PYTHON on PATH: set HOST_PYTHON to Python 3.12 or newer"
    "$HOST_PYTHON" -c 'import sys; sys.exit(sys.version_info < (3, 12))' \
        || die "$HOST_PYTHON is $("$HOST_PYTHON" -c 'import sys; print(sys.version.split()[0])'); Python 3.12 or newer is needed (set HOST_PYTHON)"
}

# fail_log <log> <what>: show the end of a failed step's log, then stop.
fail_log() {
    tail -40 "$1" >&2 || true
    die "$2 failed; full log: $1"
}
