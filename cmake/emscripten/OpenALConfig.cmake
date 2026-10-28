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

# Emscripten ships OpenAL as a JS library (-lopenal) with headers in the
# sysroot. The client's cmake/FindOpenAL.cmake tries find_package(OpenAL CONFIG) first,
# so this config provides OpenAL::OpenAL. The client includes <al.h>, hence include/AL.
# Emscripten has no efx.h, so the client's OPENAL_HAS_EFX / USE_EFX turn off.
if(NOT TARGET OpenAL::OpenAL)
    add_library(OpenAL::OpenAL INTERFACE IMPORTED)
    set_target_properties(OpenAL::OpenAL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${EMSCRIPTEN_SYSROOT}/include/AL"
        INTERFACE_LINK_LIBRARIES "-lopenal"
    )
endif()
set(OpenAL_FOUND TRUE)
set(OPENAL_FOUND TRUE)
set(OPENAL_INCLUDE_DIR "${EMSCRIPTEN_SYSROOT}/include/AL")
set(OPENAL_LIBRARY "-lopenal")
