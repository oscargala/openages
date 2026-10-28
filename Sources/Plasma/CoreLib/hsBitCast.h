/*==LICENSE==*

CyanWorlds.com Engine - MMOG client, server and tools
Copyright (C) 2011  Cyan Worlds, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Additional permissions under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or
combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
(or a modified version of those libraries),
containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
licensors of this Program grant you additional
permission to convey the resulting work. Corresponding Source for a
non-source form of such a combination shall include the source code for
the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
work.

You can contact Cyan Worlds, Inc. by email legal@cyan.com
 or by snail mail at:
      Cyan Worlds, Inc.
      14617 N Newport Hwy
      Mead, WA   99021

*==LICENSE==*/

#ifndef hsBitCast_inc
#define hsBitCast_inc

#include <cstring>
#include <type_traits>

// The bits of a value read as another type of the same size, for example a float's IEEE-754 bits as a
// uint32_t and back: std::bit_cast's contract (C++20, [bit.cast]) for this C++17 code base, through
// memcpy, the well-defined way to copy an object representation (a union or a pointer cast is not).
// Once the code base builds as C++20, callers can use std::bit_cast instead.
template <typename To, typename From>
inline To hsBitCast(const From& from) noexcept
{
    static_assert(sizeof(To) == sizeof(From), "hsBitCast: the two types differ in size");
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
                  "hsBitCast: both types must be trivially copyable");
    static_assert(std::is_trivially_default_constructible_v<To>,
                  "hsBitCast: the target type must be trivially default constructible");
    To to;
    std::memcpy(&to, &from, sizeof(to));
    return to;
}

#endif // hsBitCast_inc
