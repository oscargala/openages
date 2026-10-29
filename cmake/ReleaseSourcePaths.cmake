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

# A release build of the browser client (PRODUCT_RELEASE_BUILD with Emscripten) gives the same bytes wherever the
# checkout and the build dir lie. Included by the top-level CMakeLists.txt before any target is created.
#
# Where the paths entered. The libraries are ThinLTO bitcode (CompilerChecks.cmake,
# CMAKE_INTERPROCEDURAL_OPTIMIZATION), and clang records each module's input path, exactly as the compiler is given it,
# as the module's source_filename. CMake gives every source by its absolute path: the checkout's as
# <checkout>/Sources/..., the unity sources as <build dir>/Sources/.../Unity/unity_N_cxx.cxx. ThinLTO hashes the module
# (the source_filename included) and names each local function it promotes <name>.llvm.<hash>; a build with
# --emit-symbol-map keeps those names, and the order of promoted functions that share a name follows them. So a release
# built in another folder, or from a checkout in another folder, had other bytes. No clang flag maps the source_filename
# (-ffile-prefix-map does not reach it).
#
# What this does:
#   1. the link <build dir>/CMakeFiles/plasma-source names the checkout;
#   2. every C and C++ compile runs through ReleaseSourcePathsCompile.cmake (the compiler launcher), which gives the
#      compiler its input by a path relative to the build dir, the working directory of the compile: a source of the
#      build dir as Sources/..., a source of the checkout as CMakeFiles/plasma-source/Sources/.... Its source_filename,
#      its depfile and its __FILE__ then name no folder of this machine;
#   3. -ffile-prefix-map writes the same names for the paths the compiler reaches by absolute path (a header found
#      through an absolute -I, a source a unity file includes), so a __FILE__ there names no folder either. The deps
#      prefix already has its own map (cmake/emscripten/ChainloadToolchain.cmake).
# The embedded commit is kept out of the ThinLTO modules separately (Sources/Plasma/CoreLib/CMakeLists.txt).

set(_plasma_source_link "${CMAKE_BINARY_DIR}/CMakeFiles/plasma-source")
file(CREATE_LINK "${CMAKE_SOURCE_DIR}" "${_plasma_source_link}" SYMBOLIC RESULT _plasma_link_result)
if(NOT _plasma_link_result EQUAL 0)
    message(FATAL_ERROR "PRODUCT_RELEASE_BUILD: cannot create the link ${_plasma_source_link} to ${CMAKE_SOURCE_DIR} "
                        "(${_plasma_link_result}). A release build compiles the checkout's sources through it, so "
                        "that its bytes do not depend on where the checkout lies.")
endif()

foreach(_plasma_lang C CXX)
    # A launcher the caller set (ccache, for example) runs after this one, with the rewritten command.
    set(CMAKE_${_plasma_lang}_COMPILER_LAUNCHER
        "${CMAKE_COMMAND}"
        "-DPLASMA_SOURCE_ROOT=${CMAKE_SOURCE_DIR}"
        "-DPLASMA_BINARY_ROOT=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/ReleaseSourcePathsCompile.cmake"
        --
        ${CMAKE_${_plasma_lang}_COMPILER_LAUNCHER})
endforeach()

# The trailing slashes: clang matches a prefix as a string, so without them a sibling folder that begins with the same
# name (<checkout>-other/...) would be mapped too. Clang takes the longest matching prefix, so a build dir inside the
# checkout maps as the build dir.
add_compile_options(
    "-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=CMakeFiles/plasma-source/"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=")

unset(_plasma_source_link)
unset(_plasma_link_result)
unset(_plasma_lang)
