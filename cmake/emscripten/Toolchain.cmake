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

# CMake toolchain for configuring the client for wasm32-emscripten against a deps prefix.
# web/build/build-client.sh uses it; directly:
#
#   source <emsdk>/emsdk_env.sh
#   cmake -G Ninja -S <source> -B <build dir> \
#         -DCMAKE_TOOLCHAIN_FILE=<source>/cmake/emscripten/Toolchain.cmake \
#         -DCMAKE_BUILD_TYPE=Release -DPLASMA_WASM_DEPS=<prefix>
#
# Inputs, each a cache variable (-D) or the environment variable of the same name; the environment
# wins when set, so a reconfigure follows it, and the cache keeps the value for regeneration:
#   PLASMA_WASM_DEPS  required: the prefix web/build/build-deps.sh wrote.
#   VCPKG_ROOT        the vcpkg checkout that provides scripts/buildsystems/vcpkg.cmake; default:
#                     this repository's vcpkg submodule.
#   VCPKG_DEFAULT_HOST_TRIPLET  (environment) overrides the host triplet detected below.
# The compiler must be the Emscripten release web/build/sources.lock pins (PLASMA_EMSCRIPTEN_PINNED_VERSION below).
#
# Layers:
#   1. vcpkg's buildsystems/vcpkg.cmake in classic mode, pointed at the deps prefix's installed tree
#      (VCPKG_INSTALLED_DIR, triplet wasm32-emscripten-plasma). Needed, not optional: the
#      vcpkg package configs (e.g. freetype's, which links BZip2::BZip2) rely on vcpkg's
#      find_package wrappers and VCPKG_IMPORT_PREFIX. Manifest mode is forced OFF so the
#      client's vcpkg.json does not trigger an install.
#   2. its chainload: cmake/emscripten/ChainloadToolchain.cmake = Emscripten.cmake
#      plus the global flags (-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm) on every
#      C/C++ compile and link.
#   3. CPython and Emscripten-OpenAL prefixes on CMAKE_PREFIX_PATH and CMAKE_FIND_ROOT_PATH
#      (Emscripten.cmake sets the find-root modes to ONLY; a prefix outside
#      CMAKE_FIND_ROOT_PATH is invisible), plus Python3 hints.
get_filename_component(_plasma_source_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(DEFINED ENV{PLASMA_WASM_DEPS} AND NOT "$ENV{PLASMA_WASM_DEPS}" STREQUAL "")
    set(PLASMA_WASM_DEPS "$ENV{PLASMA_WASM_DEPS}" CACHE PATH "wasm32-emscripten deps prefix (build-deps.sh)" FORCE)
elseif(NOT PLASMA_WASM_DEPS)
    message(FATAL_ERROR "PLASMA_WASM_DEPS is not set: pass -DPLASMA_WASM_DEPS=<prefix> or set it in the environment, "
                        "naming the prefix build-deps.sh wrote")
endif()
if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "vcpkg checkout providing scripts/buildsystems/vcpkg.cmake" FORCE)
elseif(NOT VCPKG_ROOT)
    set(VCPKG_ROOT "${_plasma_source_root}/vcpkg" CACHE PATH "vcpkg checkout providing scripts/buildsystems/vcpkg.cmake")
endif()
if(NOT EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "No vcpkg checkout at '${VCPKG_ROOT}': run 'git submodule update --init vcpkg', "
                        "or set VCPKG_ROOT")
endif()
# try_compile projects read this file again; give them the same inputs.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PLASMA_WASM_DEPS VCPKG_ROOT)
set(PLASMA_WASM_VCPKG "${PLASMA_WASM_DEPS}/vcpkg_installed/wasm32-emscripten-plasma")
# PLASMA_WASM_PYTHON (environment) selects a CPython prefix other than the shared one, e.g.
# a private stage of build-deps.sh python (PLASMA_WASM_PYTHON_STAGE=<dir> -> <dir>/python).
if(DEFINED ENV{PLASMA_WASM_PYTHON} AND NOT "$ENV{PLASMA_WASM_PYTHON}" STREQUAL "")
    set(PLASMA_WASM_PYTHON "$ENV{PLASMA_WASM_PYTHON}")
else()
    set(PLASMA_WASM_PYTHON "${PLASMA_WASM_DEPS}/python")
endif()

# PLASMA_WASM_PYTHON2 (environment) selects the CPython 2.3.3 prefix the py2
# flavour (-DPLASMA_PYTHON_ABI=2) links: build-deps.sh python2 writes <stage>/python2
# (PLASMA_WASM_PYTHON2_STAGE=<dir>, default $PLASMA_WASM_DEPS). Unused by the default ABI 3.
# Cached, so a regeneration during the build (no environment) keeps the configured prefix.
if(DEFINED ENV{PLASMA_WASM_PYTHON2} AND NOT "$ENV{PLASMA_WASM_PYTHON2}" STREQUAL "")
    set(PLASMA_WASM_PYTHON2 "$ENV{PLASMA_WASM_PYTHON2}" CACHE PATH "CPython 2.3.3 prefix of the py2 flavour" FORCE)
else()
    set(PLASMA_WASM_PYTHON2 "${PLASMA_WASM_DEPS}/python2" CACHE PATH "CPython 2.3.3 prefix of the py2 flavour")
endif()

set(VCPKG_TARGET_TRIPLET "wasm32-emscripten-plasma" CACHE STRING "")
# The host triplet names the vcpkg_installed/<triplet> folder of vcpkg's host-only helper ports
# (no compiled code). build-deps.sh chooses it the same way (web/build/common.sh).
if(NOT VCPKG_HOST_TRIPLET)
    if(DEFINED ENV{VCPKG_DEFAULT_HOST_TRIPLET} AND NOT "$ENV{VCPKG_DEFAULT_HOST_TRIPLET}" STREQUAL "")
        set(_plasma_host_triplet "$ENV{VCPKG_DEFAULT_HOST_TRIPLET}")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_plasma_host_triplet "arm64-osx")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(_plasma_host_triplet "x64-osx")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(_plasma_host_triplet "x64-linux")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_plasma_host_triplet "arm64-linux")
    else()
        message(FATAL_ERROR "No vcpkg host triplet known for ${CMAKE_HOST_SYSTEM_NAME}/${CMAKE_HOST_SYSTEM_PROCESSOR}: "
                            "set VCPKG_DEFAULT_HOST_TRIPLET")
    endif()
    set(VCPKG_HOST_TRIPLET "${_plasma_host_triplet}" CACHE STRING "")
endif()
set(VCPKG_INSTALLED_DIR "${PLASMA_WASM_DEPS}/vcpkg_installed" CACHE PATH "")
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "")
set(VCPKG_MANIFEST_INSTALL OFF CACHE BOOL "")
set(VCPKG_OVERLAY_TRIPLETS "${_plasma_source_root}/Scripts/Triplets" CACHE STRING "")
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/ChainloadToolchain.cmake" CACHE FILEPATH "")
include("${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")

# The Emscripten release is pinned in one place, the version of web/build/sources.lock's emsdk record,
# whose comment lists the code that depends on that release. The build scripts refuse another emcc
# (web/build/common.sh, use_emsdk), and so does a configure here: the deps prefix and the client must come from one
# compiler. PLASMA_EMSCRIPTEN_PINNED_VERSION is that version, for the checks below and in plClient's CMakeLists.txt.
# (Emscripten.cmake, included by the chainload above, sets EMSCRIPTEN_VERSION from the compiler.)
file(STRINGS "${_plasma_source_root}/web/build/sources.lock" _plasma_emsdk_record REGEX "^emsdk ")
list(LENGTH _plasma_emsdk_record _plasma_emsdk_records)
if(NOT _plasma_emsdk_records EQUAL 1 OR
   NOT _plasma_emsdk_record MATCHES " version=([0-9]+\\.[0-9]+\\.[0-9]+)( |$)")
    message(FATAL_ERROR "${_plasma_source_root}/web/build/sources.lock: no single emsdk record with a version")
endif()
set(PLASMA_EMSCRIPTEN_PINNED_VERSION "${CMAKE_MATCH_1}")
if(NOT EMSCRIPTEN_VERSION STREQUAL PLASMA_EMSCRIPTEN_PINNED_VERSION)
    message(FATAL_ERROR "The compiler is Emscripten ${EMSCRIPTEN_VERSION}; this build is pinned to "
                        "${PLASMA_EMSCRIPTEN_PINNED_VERSION} (web/build/sources.lock). Install and activate it with "
                        "emsdk ('emsdk install ${PLASMA_EMSCRIPTEN_PINNED_VERSION}', "
                        "'emsdk activate ${PLASMA_EMSCRIPTEN_PINNED_VERSION}'), then configure a new build dir.")
endif()
unset(_plasma_emsdk_record)
unset(_plasma_emsdk_records)

set(_plasma_prefixes "${PLASMA_WASM_PYTHON}" "${PLASMA_WASM_DEPS}/emscripten-openal")
# Optional common movie package. Discovery only: configuring never builds or
# installs dependencies. Explicit prefixes fail if incomplete instead of silently
# disabling movies. Native builds do not use this Emscripten toolchain.
if(DEFINED ENV{PLASMA_WASM_MOVIE_PREFIX} AND NOT "$ENV{PLASMA_WASM_MOVIE_PREFIX}" STREQUAL "")
    set(_plasma_movies "$ENV{PLASMA_WASM_MOVIE_PREFIX}")
    set(_plasma_movies_explicit TRUE)
else()
    set(_plasma_movies "${PLASMA_WASM_DEPS}/movies")
    set(_plasma_movies_explicit FALSE)
endif()
if(_plasma_movies_explicit OR EXISTS "${_plasma_movies}")
    foreach(_file movie-codecs.json lib/libvpx.a lib/libwebm.a include/vpx/vp8.h include/webm/mkvparser/mkvparser.h)
        if(NOT EXISTS "${_plasma_movies}/${_file}")
            message(FATAL_ERROR "Incomplete movie codec package: ${_plasma_movies}/${_file}")
        endif()
    endforeach()
    file(READ "${_plasma_movies}/movie-codecs.json" _plasma_movies_json)
    string(JSON _plasma_movies_target GET "${_plasma_movies_json}" target)
    string(JSON _plasma_movies_abi GET "${_plasma_movies_json}" abi)
    string(JSON _plasma_movies_sdk GET "${_plasma_movies_json}" emscriptenVersion)
    if(NOT _plasma_movies_target STREQUAL "wasm" OR
       NOT _plasma_movies_abi STREQUAL "-pthread -fwasm-exceptions -sSUPPORT_LONGJMP=wasm" OR
       NOT _plasma_movies_sdk STREQUAL PLASMA_EMSCRIPTEN_PINNED_VERSION)
        message(FATAL_ERROR "Movie codec package must use the pinned Emscripten/ABI recipe")
    endif()
    list(APPEND _plasma_prefixes "${_plasma_movies}")
endif()
list(APPEND CMAKE_PREFIX_PATH ${_plasma_prefixes})
list(APPEND CMAKE_FIND_ROOT_PATH ${_plasma_prefixes})

# CPython (cross build: no runnable interpreter; the client's CMakeLists asks only for
# Development when CMake >= 4.0 and CMAKE_CROSSCOMPILING).
set(Python3_INCLUDE_DIR "${PLASMA_WASM_PYTHON}/include/python3.12" CACHE PATH "")
set(Python3_LIBRARY "${PLASMA_WASM_PYTHON}/lib/libpython3.12.a" CACHE FILEPATH "")
# Archives a libpython3.12.a consumer must also link (builtin _decimal and _sha2 helpers;
# zlib, bzip2 and expat are the vcpkg copies the client links anyway).
set(PLASMA_WASM_PYTHON_LINK_LIBS
    "${PLASMA_WASM_PYTHON}/lib/libmpdec.a"
    "${PLASMA_WASM_PYTHON}/lib/libHacl_Hash_SHA2.a"
    "${PLASMA_WASM_VCPKG}/lib/libbz2.a"
    "${PLASMA_WASM_VCPKG}/lib/libexpat.a"
    "${PLASMA_WASM_VCPKG}/lib/libz.a"
    CACHE STRING "Extra archives a libpython3.12.a consumer links")
