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

# The compiler launcher of a release build of the browser client (cmake/ReleaseSourcePaths.cmake sets it):
#
#   cmake -DPLASMA_SOURCE_ROOT=<checkout> -DPLASMA_BINARY_ROOT=<build dir> -P ReleaseSourcePathsCompile.cmake -- \
#         <compiler> <arguments> -c <source>
#
# It runs the compiler command with one change: the source after the last -c, an absolute path, becomes a path
# relative to the build dir, the working directory Ninja runs the compile in. A source in the build dir (a unity
# source) becomes Sources/...; a source in the checkout becomes CMakeFiles/plasma-source/Sources/..., through the link
# to the checkout. The compiler records that path as the module's source_filename, which ThinLTO hashes into the names
# it gives promoted local functions, so the names, and the release's bytes, do not depend on where the checkout and
# the build dir lie. The output, the depfile and every other argument are unchanged, and the depfile's main source
# names a file that exists. The exit code is the compiler's.
#
# It stops, and the compile fails, when the working directory is not the build dir, when the command has no -c
# followed by a source, when the source is outside both the checkout and the build dir, and when an argument holds a
# character that would change how a CMake list splits it (";", "[" or "]").

if(NOT PLASMA_SOURCE_ROOT OR NOT PLASMA_BINARY_ROOT)
    message(FATAL_ERROR "ReleaseSourcePathsCompile.cmake: PLASMA_SOURCE_ROOT and PLASMA_BINARY_ROOT must be set")
endif()
# In script mode the current binary dir is the working directory, as the system names it: compared without links
# (macOS's /tmp is a link to /private/tmp).
file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}" _working_dir)
file(REAL_PATH "${PLASMA_BINARY_ROOT}" _binary_dir)
if(NOT _working_dir STREQUAL _binary_dir)
    message(FATAL_ERROR "ReleaseSourcePathsCompile.cmake: the compile runs in ${CMAKE_CURRENT_BINARY_DIR}, "
                        "not in the build dir ${PLASMA_BINARY_ROOT}")
endif()

set(_command "")
set(_after_separator FALSE)
set(_source_index -1)
set(_index 0)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_argn RANGE 0 ${_last})
    set(_arg "${CMAKE_ARGV${_argn}}")
    if(NOT _after_separator)
        if(_arg STREQUAL "--")
            set(_after_separator TRUE)
        endif()
        continue()
    endif()
    if(_arg MATCHES "[];[]")
        message(FATAL_ERROR "ReleaseSourcePathsCompile.cmake: an argument holds \";\", \"[\" or \"]\": ${_arg}")
    endif()
    list(APPEND _command "${_arg}")
    if(_arg STREQUAL "-c")
        math(EXPR _source_index "${_index} + 1")
    endif()
    math(EXPR _index "${_index} + 1")
endforeach()

list(LENGTH _command _count)
if(_source_index LESS 1 OR NOT _source_index LESS _count)
    message(FATAL_ERROR "ReleaseSourcePathsCompile.cmake: no source after -c in the compile command")
endif()
list(GET _command ${_source_index} _source)

# A path is inside a folder when it is the folder or starts with the folder and a slash. The build dir is tested
# first: it may lie inside the checkout.
string(LENGTH "${PLASMA_BINARY_ROOT}/" _binary_length)
string(LENGTH "${PLASMA_SOURCE_ROOT}/" _source_length)
string(SUBSTRING "${_source}" 0 ${_binary_length} _binary_head)
string(SUBSTRING "${_source}" 0 ${_source_length} _source_head)
if(_binary_head STREQUAL "${PLASMA_BINARY_ROOT}/")
    string(SUBSTRING "${_source}" ${_binary_length} -1 _relative)
elseif(_source_head STREQUAL "${PLASMA_SOURCE_ROOT}/")
    string(SUBSTRING "${_source}" ${_source_length} -1 _relative)
    set(_relative "CMakeFiles/plasma-source/${_relative}")
else()
    message(FATAL_ERROR "ReleaseSourcePathsCompile.cmake: ${_source} is in neither the checkout ${PLASMA_SOURCE_ROOT} "
                        "nor the build dir ${PLASMA_BINARY_ROOT}: a release build compiles only those")
endif()
list(REMOVE_AT _command ${_source_index})
list(INSERT _command ${_source_index} "${_relative}")

execute_process(COMMAND ${_command} RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    if(NOT _result MATCHES "^[0-9]+$")
        set(_result 1)
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.29)
        message(FATAL_ERROR "")
    endif()
    cmake_language(EXIT ${_result})
endif()
