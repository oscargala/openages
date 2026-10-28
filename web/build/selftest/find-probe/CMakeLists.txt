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

# Dependency self-test of a wasm32-emscripten deps prefix (build-deps.sh's check step). It runs the
# client's own find_package() calls (the root CMakeLists.txt, minus CURL, epoxy, libwebm, VPX and
# DirectX, which the browser build does not use) with the client's cmake/ find modules and
# cmake/emscripten/Toolchain.cmake, then links one executable against every imported target the
# client uses. A prefix that passes configures the client the same way.
cmake_minimum_required(VERSION 3.20)
project(plasma_wasm_find_probe C CXX)
set(PLASMA_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../../.." CACHE PATH "")
set(CMAKE_MODULE_PATH "${PLASMA_SOURCE}/cmake")  # as the client sets CMAKE_MODULE_PATH (CMakeLists.txt:7)

find_package(ASIO 1.14 REQUIRED)
find_package(expat REQUIRED)
find_package(freetype REQUIRED)
find_package(JPEG REQUIRED)
find_package(Ogg REQUIRED)
find_package(OpenAL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Opus REQUIRED)
find_package(PhysX REQUIRED)
find_package(PNG REQUIRED)
find_package(Speex REQUIRED)
find_package(string_theory 3.4 REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
find_package(tl-expected REQUIRED)
find_package(Vorbis REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python3 3.8 REQUIRED COMPONENTS Development)

message(STATUS "probe: OPENAL_HAS_EFX=${OPENAL_HAS_EFX}")
message(STATUS "probe: Python3 ${Python3_VERSION} ${Python3_LIBRARIES} ${Python3_INCLUDE_DIRS}")
message(STATUS "probe: OpenSSL ${OPENSSL_VERSION} ${OPENSSL_CRYPTO_LIBRARY}")
foreach(_t ASIO::ASIO expat::expat freetype JPEG::JPEG Ogg::ogg OpenAL::OpenAL OpenSSL::Crypto Opus::opus
           PhysX::PhysX PNG::PNG Speex::speex string_theory Threads::Threads tl::expected Vorbis::vorbis
           Vorbis::vorbisfile ZLIB::ZLIB Python3::Python)
    if(NOT TARGET ${_t})
        message(FATAL_ERROR "probe: target ${_t} missing")
    endif()
endforeach()

add_executable(find_probe probe.cpp)
target_link_libraries(find_probe PRIVATE
    ASIO::ASIO expat::expat freetype JPEG::JPEG Ogg::ogg OpenAL::OpenAL OpenSSL::Crypto Opus::opus
    PhysX::PhysX PNG::PNG Speex::speex string_theory Threads::Threads tl::expected Vorbis::vorbis
    Vorbis::vorbisfile ZLIB::ZLIB Python3::Python ${PLASMA_WASM_PYTHON_LINK_LIBS})
target_link_options(find_probe PRIVATE -sPROXY_TO_PTHREAD -sEXIT_RUNTIME -sNODERAWFS -sALLOW_MEMORY_GROWTH)
