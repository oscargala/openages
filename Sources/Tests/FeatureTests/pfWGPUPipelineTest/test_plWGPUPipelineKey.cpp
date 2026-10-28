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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pfWGPUPipeline/plWGPUD3DDevice.h"
#include "pfWGPUPipeline/plWGPUPipelineKey.h"

// The packed pipeline keys of pfWGPUPipeline. The device caches a WebGPU pipeline by Pack() with
// its own fields above it: the fixed-function key from bit 34 (plWGPUDevice::DrawFixedFunction),
// the shader key from bit 33 (plWGPUDevice::IShaderPipeline). Two keys must pack equal exactly
// when every field is equal, or two passes share one pipeline.
//
// Each layout below is written out from the field widths the values need, and the tests decode a
// packed key with it ("unpack"): every field round-trips at every value its width holds, and the
// fields tile the bits below the device's shift with no gap and no overlap.

namespace
{
struct Field
{
    const char* name;
    unsigned    shift;
    unsigned    width;
};

constexpr uint64_t Mask(unsigned width) { return (uint64_t(1) << width) - 1; }

// plWGPUFixedFunctionKey: blend state as D3DRS_ALPHABLENDENABLE / SRCBLEND / DESTBLEND /
// BLENDOP, D3DRS_COLORWRITEENABLE's four bits, the enabled stage count (0..kNumStages), the
// lighting, fog (0 none, 1 table, 2 vertex) and vertex-blend variants, the plGBufferGroup
// format byte and the vertex kind (0 span, 1 plate, 2 XYZ|TEX1).
const std::vector<Field> kFixedFunctionLayout = {
    {"blendEnabled", 0, 1},
    {"srcBlend", 1, 4},
    {"dstBlend", 5, 4},
    {"blendOp", 9, 3},
    {"writeMask", 12, 4},
    {"stages", 16, 4},
    {"lighting", 20, 1},
    {"fog", 21, 2},
    {"vertexBlend", 23, 1},
    {"vertexFormat", 24, 8},
    {"plateVertex", 32, 2},
};
constexpr unsigned kFixedFunctionBits = 34; // plWGPUDevice::DrawFixedFunction: fixed state << 34

// plWGPUShaderKey: the two program table indices, the blend state, the write mask, the
// plGBufferGroup format byte and D3DRS_FOGENABLE.
const std::vector<Field> kShaderLayout = {
    {"vs", 0, 4},
    {"ps", 4, 4},
    {"blendEnabled", 8, 1},
    {"srcBlend", 9, 4},
    {"dstBlend", 13, 4},
    {"blendOp", 17, 3},
    {"writeMask", 20, 4},
    {"vertexFormat", 24, 8},
    {"fog", 32, 1},
};
constexpr unsigned kShaderBits = 33; // plWGPUDevice::IShaderPipeline: fixed state << 33

uint8_t& FieldOf(plWGPUFixedFunctionKey& k, size_t i)
{
    uint8_t* fields[] = {&k.blendEnabled, &k.srcBlend, &k.dstBlend, &k.blendOp,
                         &k.writeMask, &k.stages, &k.lighting, &k.fog,
                         &k.vertexBlend, &k.vertexFormat, &k.plateVertex};
    return *fields[i];
}

uint8_t& FieldOf(plWGPUShaderKey& k, size_t i)
{
    uint8_t* fields[] = {&k.vs, &k.ps, &k.blendEnabled, &k.srcBlend, &k.dstBlend,
                         &k.blendOp, &k.writeMask, &k.vertexFormat, &k.fog};
    return *fields[i];
}

// The key a packed value holds, by the layout.
template <typename Key>
Key Unpack(uint64_t packed, const std::vector<Field>& layout)
{
    Key k = {};
    for (size_t i = 0; i < layout.size(); i++)
        FieldOf(k, i) = uint8_t((packed >> layout[i].shift) & Mask(layout[i].width));
    return k;
}

// Every field of `k` at `value`, or at its maximum when value is -1.
template <typename Key>
Key AllFields(const std::vector<Field>& layout, int value)
{
    Key k = {};
    for (size_t i = 0; i < layout.size(); i++)
        FieldOf(k, i) = uint8_t(value < 0 ? Mask(layout[i].width) : uint64_t(value));
    return k;
}

template <typename Key>
void ExpectFieldsEqual(Key a, Key b, const std::vector<Field>& layout)
{
    for (size_t i = 0; i < layout.size(); i++)
        EXPECT_EQ(FieldOf(a, i), FieldOf(b, i)) << layout[i].name;
}

// Each field alone at each value its width holds, the other fields at 0 and then at their
// maximum: the packed key decodes to the key, and the field's bits are exactly its own.
template <typename Key>
void ExpectEveryFieldRoundTrips(const std::vector<Field>& layout)
{
    for (int others : {0, -1}) {
        for (size_t i = 0; i < layout.size(); i++) {
            for (uint64_t v = 0; v <= Mask(layout[i].width); v++) {
                Key k = AllFields<Key>(layout, others);
                FieldOf(k, i) = uint8_t(v);
                const uint64_t packed = k.Pack();
                ExpectFieldsEqual(Unpack<Key>(packed, layout), k, layout);
                EXPECT_EQ((packed >> layout[i].shift) & Mask(layout[i].width), v)
                    << layout[i].name << " = " << v;
            }
        }
    }
}

// The fields tile bits 0..bits-1: all at their maximum pack to bits ones, all at 0 to 0,
// and each field at its maximum alone sets only its own bits.
template <typename Key>
void ExpectFieldsTileTheKey(const std::vector<Field>& layout, unsigned bits)
{
    EXPECT_EQ(AllFields<Key>(layout, 0).Pack(), uint64_t(0));
    EXPECT_EQ(AllFields<Key>(layout, -1).Pack(), Mask(bits));
    uint64_t seen = 0;
    for (size_t i = 0; i < layout.size(); i++) {
        Key k = {};
        FieldOf(k, i) = uint8_t(Mask(layout[i].width));
        const uint64_t own = Mask(layout[i].width) << layout[i].shift;
        EXPECT_EQ(k.Pack(), own) << layout[i].name;
        EXPECT_EQ(seen & own, uint64_t(0)) << layout[i].name << " overlaps an earlier field";
        seen |= own;
    }
    EXPECT_EQ(seen, Mask(bits));
}
} // namespace

TEST(plWGPUFixedFunctionKey, every_field_round_trips_at_every_value)
{
    ExpectEveryFieldRoundTrips<plWGPUFixedFunctionKey>(kFixedFunctionLayout);
}

TEST(plWGPUFixedFunctionKey, fields_tile_the_bits_below_the_fixed_state)
{
    ExpectFieldsTileTheKey<plWGPUFixedFunctionKey>(kFixedFunctionLayout, kFixedFunctionBits);
}

TEST(plWGPUFixedFunctionKey, pad_is_not_packed)
{
    plWGPUFixedFunctionKey a = AllFields<plWGPUFixedFunctionKey>(kFixedFunctionLayout, -1);
    plWGPUFixedFunctionKey b = a;
    a.pad = 0;
    b.pad = 0xff;
    EXPECT_EQ(a.Pack(), b.Pack());
}

TEST(plWGPUFixedFunctionKey, every_value_the_device_stores_fits_its_field)
{
    // Pack() does not mask, so a value wider than its field would change the next field.
    // plWGPUDevice::DrawFixedFunction stores the D3DBLEND and D3DBLENDOP values as set and the
    // count of enabled stages (at most kNumStages).
    using namespace plWGPUD3D9;
    EXPECT_LE(uint64_t(D3DBLEND_SRCALPHASAT), Mask(4)); // the largest D3DBLEND of plWGPUD3DDevice.h
    EXPECT_LE(uint64_t(D3DBLENDOP_MAX), Mask(3));       // the largest D3DBLENDOP
    EXPECT_LE(uint64_t(plWGPUD3DDevice::kNumStages), Mask(4));
}

TEST(plWGPUShaderKey, every_field_round_trips_at_every_value)
{
    ExpectEveryFieldRoundTrips<plWGPUShaderKey>(kShaderLayout);
}

TEST(plWGPUShaderKey, fields_tile_the_bits_below_the_fixed_state)
{
    ExpectFieldsTileTheKey<plWGPUShaderKey>(kShaderLayout, kShaderBits);
}

TEST(plWGPUShaderKey, wider_values_are_cut_to_their_field)
{
    // Pack() masks every field but blendEnabled (0 or 1, plWGPUDevice::DrawShader): a value
    // wider than its field keeps only the field's bits and leaves the other fields as they are.
    for (size_t i = 0; i < kShaderLayout.size(); i++) {
        const std::string name = kShaderLayout[i].name;
        if (name == "vertexFormat" || name == "blendEnabled")
            continue; // vertexFormat holds a whole byte; blendEnabled is not masked
        plWGPUShaderKey wide = AllFields<plWGPUShaderKey>(kShaderLayout, 0);
        plWGPUShaderKey cut = wide;
        FieldOf(wide, i) = 0xff;
        FieldOf(cut, i) = uint8_t(Mask(kShaderLayout[i].width));
        EXPECT_EQ(wide.Pack(), cut.Pack()) << kShaderLayout[i].name;
    }
}

TEST(plWGPUShaderKey, every_value_the_device_stores_fits_its_field)
{
    // srcBlend, dstBlend and blendOp hold D3DRS_SRCBLEND, DESTBLEND and BLENDOP as set
    // (plWGPUDevice::DrawShader); a value wider than the field would alias another one.
    using namespace plWGPUD3D9;
    EXPECT_LE(uint64_t(D3DBLEND_SRCALPHASAT), Mask(4));
    EXPECT_LE(uint64_t(D3DBLENDOP_MAX), Mask(3));
}
