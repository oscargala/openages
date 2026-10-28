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

# The chainload toolchain of the wasm32-emscripten-plasma triplet (and, through
# cmake/emscripten/Toolchain.cmake, of the client build).
# vcpkg's chainload path includes Emscripten.cmake directly and ignores VCPKG_C_FLAGS,
# so the browser build's global flags are appended here (make-based ports pick them up
# through vcpkg-cmake-get-vars).
include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
set(PLASMA_WASM_FLAGS "-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm")
foreach(_var CMAKE_C_FLAGS_INIT CMAKE_CXX_FLAGS_INIT CMAKE_EXE_LINKER_FLAGS_INIT
             CMAKE_SHARED_LINKER_FLAGS_INIT CMAKE_MODULE_LINKER_FLAGS_INIT)
    string(FIND " ${${_var}} " " ${PLASMA_WASM_FLAGS} " _found)
    if(_found EQUAL -1)
        string(APPEND ${_var} " ${PLASMA_WASM_FLAGS}")
    endif()
endforeach()

# Paths of the deps prefix reach the objects through __FILE__ (the assert and error messages of opus, PhysX and
# string_theory's headers) and through debug information: vcpkg's build trees, the installed headers and CPython's
# sources all lie under the prefix. -ffile-prefix-map (clang: -fmacro-prefix-map and -fdebug-prefix-map together)
# writes the prefix as "wasm-deps" instead, so neither a package nor the client carries the build host's path, and a
# prefix built in one place works unchanged in another.
# It is set here because this file is the one compile configuration both sides share: vcpkg's CMake ports (vcpkg
# ignores VCPKG_C_FLAGS when a chainload toolchain is set), its make-based ports (vcpkg_cmake_get_vars reads their
# flags from this file), and the client, whose own sources include the installed headers
# (cmake/emscripten/Toolchain.cmake chainloads this file). The prefix is PLASMA_WASM_DEPS: the environment in a port
# build (build-deps.sh exports it, and the triplet passes it through), else the client configure's cache variable.
if(DEFINED ENV{PLASMA_WASM_DEPS} AND NOT "$ENV{PLASMA_WASM_DEPS}" STREQUAL "")
    set(_plasma_deps_root "$ENV{PLASMA_WASM_DEPS}")
elseif(PLASMA_WASM_DEPS)
    set(_plasma_deps_root "${PLASMA_WASM_DEPS}")
else()
    set(_plasma_deps_root "")
endif()
if(_plasma_deps_root)
    string(REGEX REPLACE "/+$" "" _plasma_deps_root "${_plasma_deps_root}")
    set(_plasma_deps_map "-ffile-prefix-map=${_plasma_deps_root}=wasm-deps")
    foreach(_var CMAKE_C_FLAGS_INIT CMAKE_CXX_FLAGS_INIT)
        string(FIND " ${${_var}} " " ${_plasma_deps_map} " _found)
        if(_found EQUAL -1)
            string(APPEND ${_var} " ${_plasma_deps_map}")
        endif()
    endforeach()
endif()
unset(_plasma_deps_root)
unset(_plasma_deps_map)
