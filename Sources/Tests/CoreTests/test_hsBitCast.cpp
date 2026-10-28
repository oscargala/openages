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

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "HeadSpin.h"
#include "hsBitCast.h"

TEST(hsBitCast, float_bits)
{
    EXPECT_EQ(hsBitCast<uint32_t>(1.f), 0x3f800000u);
    EXPECT_EQ(hsBitCast<uint32_t>(1e-6f), 0x358637bdu);
    EXPECT_EQ(hsBitCast<uint32_t>(-0.f), 0x80000000u);
    EXPECT_EQ(hsBitCast<float>(0x40490fdbu), 3.14159274f);
    EXPECT_EQ(hsBitCast<int32_t>(-2.f), int32_t(0xc0000000u));
}

TEST(hsBitCast, double_bits)
{
    EXPECT_EQ(hsBitCast<uint64_t>(1.0), 0x3ff0000000000000ull);
    EXPECT_EQ(hsBitCast<uint64_t>(-0.0), 0x8000000000000000ull);
    EXPECT_EQ(hsBitCast<double>(0x400921fb54442d18ull), 3.141592653589793);
}

TEST(hsBitCast, round_trip_keeps_every_bit)
{
    // A quiet NaN with a payload, a negative quiet NaN with a payload, a denormal and the largest float: the bits
    // come back as they went in.
    for (uint32_t bits : {0x7fc00001u, 0xfffadbadu, 0x00000001u, 0x7f7fffffu}) {
        EXPECT_EQ(hsBitCast<uint32_t>(hsBitCast<float>(bits)), bits);
    }
    // A signaling NaN keeps its bits too, read from a float object. It is never a returned float value here: on
    // 32-bit x86 a float is returned in ST0, and the x87 load of the returned value quiets a signaling NaN.
    const uint32_t signalingNaN = 0xffbadbadu;
    float          f;
    std::memcpy(&f, &signalingNaN, sizeof(f));
    EXPECT_EQ(hsBitCast<uint32_t>(f), signalingNaN);
    EXPECT_TRUE(std::isnan(hsBitCast<float>(0x7fc00001u)));
    EXPECT_EQ(hsBitCast<float>(0x00000001u), std::numeric_limits<float>::denorm_min());
}
