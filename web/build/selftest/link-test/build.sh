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

# Dependency link test of a wasm32-emscripten deps prefix (build-deps.sh's check step): link
# link_test.cpp against every library of $PLASMA_WASM_DEPS with the client's global flags, then run
# it under node. Writes only $PLASMA_WASM_DEPS/build/link-test.
#
#   PLASMA_WASM_DEPS=<prefix> EMSDK=<emsdk checkout> web/build/selftest/link-test/build.sh
[ -z "${BASH_VERSION:-}" ] && exec bash "$0" "$@"
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROG=link-test
. "$HERE/../../common.sh"
need_var PLASMA_WASM_DEPS "the deps prefix to test (build-deps.sh writes it)"
D=$(abs_path "$PLASMA_WASM_DEPS")
use_emsdk
V=$D/vcpkg_installed/wasm32-emscripten-plasma
PY=$D/python
OUT=$D/build/link-test
mkdir -p "$OUT"
FLAGS="-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm"
# shellcheck disable=SC2086 # FLAGS is a list of words
em++ -std=c++20 -O1 $FLAGS -DNDEBUG -DASIO_STANDALONE -I"$V/include" -I"$V/include/physx" -I"$V/include/opus" \
  -I"$PY/include/python3.12" "$HERE/link_test.cpp" -o "$OUT/link_test.js" \
  -sPROXY_TO_PTHREAD -sEXIT_RUNTIME -sNODERAWFS -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=1MB -sENVIRONMENT=node,worker \
  -L"$V/lib" -L"$PY/lib" \
  -lPhysXExtensions_static_32 -lPhysXCharacterKinematic_static_32 -lPhysXCooking_static_32 -lPhysX_static_32 \
  -lPhysXPvdSDK_static_32 -lPhysXCommon_static_32 -lPhysXFoundation_static_32 \
  -lpython3.12 -lmpdec -lHacl_Hash_SHA2 \
  -lssl -lcrypto -lfreetype -lbrotlidec -lbrotlicommon -lpng16 -lbz2 -ljpeg -lvorbisenc -lvorbisfile -lvorbis -logg \
  -lopus -lspeex -lexpat -lz -lm
echo "linked: $OUT/link_test.js"
node "$OUT/link_test.js" "$PY"
