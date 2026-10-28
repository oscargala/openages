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
#include <vector>

#include "hsColorRGBA.h"
#include "pfWGPUPipeline/plWGPUTextureFormat.h"
#include "plGImage/plMipmap.h"

// The creatables plMipmap and the libraries it reaches need to link
#include "pfAnimation/pfAnimationCreatable.h"
#include "pfAudio/pfAudioCreatable.h"
#include "pfCamera/pfCameraCreatable.h"
#include "pfConditional/plConditionalObjectCreatable.h"
#include "pfMessage/pfMessageCreatable.h"
#include "plAllCreatables.h"
#include "pnAllCreatables.h"

// The D3D9 side of pfWGPUPipeline's texture upload: the mipmap levels plDXPipeline keeps
// (IProcessMipmapLevels), the D3D9 format of block-compressed data and that of uncompressed data
// (IGetD3DTextureFormat), the latter as the texels the sampler reads. Line numbers are
// plDXPipeline.cpp's (pfDXPipeline).

namespace
{
// A mipmap of the given size and configuration with its full level chain.
plMipmap* Uncompressed(uint32_t w, uint32_t h, unsigned config, uint16_t flags)
{
    plMipmap* mm = new plMipmap(w, h, config);
    mm->SetFlags(flags);
    return mm;
}

plMipmap* Compressed(uint32_t w, uint32_t h, uint8_t dxt)
{
    return new plMipmap(w, h, plMipmap::kARGB32Config, 0, plMipmap::kDirectXCompression, dxt);
}

// The 8-bit value a sampler returns for an n-bit unsigned-normalised channel value v.
uint8_t Unorm8(uint32_t v, uint32_t bits)
{
    return uint8_t(std::lround(255.0 * v / double((1u << bits) - 1)));
}

std::vector<plWGPUBGRA8> Format(const plMipmap* mm, const void* data, uint32_t numPix)
{
    std::vector<plWGPUBGRA8> out(numPix, plWGPUBGRA8{1, 2, 3, 4});
    plWGPUFormatTexels(mm, reinterpret_cast<const uint8_t*>(data), numPix, out.data());
    return out;
}

void ExpectTexel(const plWGPUBGRA8& t, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t at)
{
    EXPECT_EQ(t.r, r) << "pixel " << at;
    EXPECT_EQ(t.g, g) << "pixel " << at;
    EXPECT_EQ(t.b, b) << "pixel " << at;
    EXPECT_EQ(t.a, a) << "pixel " << at;
}

// 32-bit pixels whose channels take distinct values.
std::vector<hsColor32> Colours32()
{
    std::vector<hsColor32> px(256);
    for (uint32_t i = 0; i < px.size(); i++)
        px[i].SetARGB(uint8_t(255 - i), uint8_t(i), uint8_t(i * 7), uint8_t(i * 13));
    return px;
}
} // namespace

//// Levels ///////////////////////////////////////////////////////////////////

TEST(plWGPUTextureFormat, levels_32bit_keeps_every_level)
{
    // 32-bit data is reformatted, never truncated: numLevels = GetNumLevels() (:7615).
    for (bool small : {false, true}) {
        plMipmap* mm = Uncompressed(64, 64, plMipmap::kARGB32Config, plMipmap::kAlphaChannelFlag);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 6); // 64 .. 1
        delete mm;
        mm = Uncompressed(12, 6, plMipmap::kRGB32Config, 0);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 3); // 12x6, 6x3, 3x1, 1x1
        delete mm;
    }
}

TEST(plWGPUTextureFormat, levels_compressed_stop_at_the_last_4x4_level)
{
    // Compressed data drops every level whose width or height is not a multiple of 4, whatever
    // the device's kCapsDoesSmallTextures (IProcessMipmapLevels :7580).
    for (bool small : {false, true}) {
        plMipmap* mm = Compressed(64, 64, plMipmap::DirectXInfo::kDXT1);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 4); // 64, 32, 16, 8, 4; not 2 and 1
        delete mm;
        mm = Compressed(64, 16, plMipmap::DirectXInfo::kDXT5);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 2); // 64x16, 32x8, 16x4; 8x2 is not
        delete mm;
        mm = Compressed(16, 128, plMipmap::DirectXInfo::kDXT5);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 2); // 16x128, 8x64, 4x32; 2x16 is not
        delete mm;
    }
}

TEST(plWGPUTextureFormat, levels_16bit_and_8bit_stop_at_4x4_without_small_textures)
{
    // 16- and 8-bit data are truncated as compressed data are when the device lacks
    // kCapsDoesSmallTextures (:7580), the device record's cap (:955-956).
    plMipmap* mm = Uncompressed(32, 32, plMipmap::kRGB16Config, plMipmap::kAlphaChannelFlag);
    EXPECT_EQ(plWGPUAllowedLevels(mm, false), 3); // 32, 16, 8, 4
    delete mm;
    mm = Uncompressed(32, 8, plMipmap::kGray8Config, plMipmap::kIntensityMap);
    EXPECT_EQ(plWGPUAllowedLevels(mm, false), 1); // 32x8, 16x4; 8x2 is not
    delete mm;
}

TEST(plWGPUTextureFormat, levels_16bit_and_8bit_keep_every_level_with_small_textures)
{
    plMipmap* mm = Uncompressed(32, 32, plMipmap::kRGB16Config, plMipmap::kAlphaChannelFlag);
    EXPECT_EQ(plWGPUAllowedLevels(mm, true), 5); // 32 .. 1
    delete mm;
    mm = Uncompressed(32, 8, plMipmap::kGray8Config, plMipmap::kIntensityMap);
    EXPECT_EQ(plWGPUAllowedLevels(mm, true), 5); // 32x8 .. 1x1
    delete mm;
}

TEST(plWGPUTextureFormat, levels_keep_level_0_whatever_its_size)
{
    // IProcessMipmapLevels' loop stops at level 0 (`while (maxLevel && ...)`, :7583): a top level below 4x4 is kept.
    plMipmap* mm = Uncompressed(2, 2, plMipmap::kRGB16Config, 0);
    EXPECT_EQ(plWGPUAllowedLevels(mm, false), 0);
    delete mm;
    mm = Uncompressed(6, 6, plMipmap::kGray8Config, plMipmap::kIntensityMap);
    EXPECT_EQ(plWGPUAllowedLevels(mm, false), 0); // 6x6 is not a multiple of 4 either
    delete mm;
}

TEST(plWGPUTextureFormat, levels_leave_the_current_level_at_0)
{
    // IProcessMipmapLevels walks the levels with SetCurrLevel and leaves level 0 current (:7590).
    plMipmap* mm = Compressed(64, 64, plMipmap::DirectXInfo::kDXT1);
    mm->SetCurrLevel(3);
    EXPECT_EQ(plWGPUAllowedLevels(mm, false), 4);
    EXPECT_EQ(mm->GetCurrLevel(), uint32_t(0));
    EXPECT_EQ(mm->GetCurrWidth(), uint32_t(64));
    delete mm;
}

TEST(plWGPUTextureFormat, levels_leave_level_0_current_for_compressed_and_sub32bit_data_untruncated_too)
{
    // In IProcessMipmapLevels the truncation's `if` (:7580) comes before SetCurrLevel(0) (:7590): compressed
    // and 8/16-bit data leave level 0 current whether or not the levels were walked, so also with kCapsDoesSmallTextures.
    for (bool small : {false, true}) {
        plMipmap* mm = Compressed(64, 64, plMipmap::DirectXInfo::kDXT5);
        mm->SetCurrLevel(2);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 4);
        EXPECT_EQ(mm->GetCurrLevel(), uint32_t(0)) << "compressed, small " << small;
        delete mm;
        mm = Uncompressed(32, 32, plMipmap::kRGB16Config, plMipmap::kAlphaChannelFlag);
        mm->SetCurrLevel(2);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), small ? 5 : 3);
        EXPECT_EQ(mm->GetCurrLevel(), uint32_t(0)) << "16-bit, small " << small;
        EXPECT_EQ(mm->GetCurrWidth(), uint32_t(32));
        delete mm;
        mm = Uncompressed(32, 8, plMipmap::kGray8Config, plMipmap::kIntensityMap);
        mm->SetCurrLevel(1);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), small ? 5 : 1);
        EXPECT_EQ(mm->GetCurrLevel(), uint32_t(0)) << "8-bit, small " << small;
        delete mm;
    }
}

TEST(plWGPUTextureFormat, levels_leave_the_current_level_of_32bit_data)
{
    // IProcessMipmapLevels' 32-bit branch reads levels with GetLevelPtr (:7623), which sets no current level.
    for (bool small : {false, true}) {
        plMipmap* mm = Uncompressed(64, 64, plMipmap::kARGB32Config, plMipmap::kAlphaChannelFlag);
        mm->SetCurrLevel(2);
        EXPECT_EQ(plWGPUAllowedLevels(mm, small), 6);
        EXPECT_EQ(mm->GetCurrLevel(), uint32_t(2)) << "small " << small;
        EXPECT_EQ(mm->GetCurrWidth(), uint32_t(16));
        delete mm;
    }
}

//// Block-compressed formats /////////////////////////////////////////////////

TEST(plWGPUTextureFormat, block_DXT1_is_BC1_and_DXT5_is_BC3)
{
    // IGetD3DTextureFormat: kDXT1 is D3DFMT_DXT1 (:7691) and kDXT5 is D3DFMT_DXT5 (:7703), whatever
    // the alpha flags.
    for (uint16_t flags : {uint16_t(0), uint16_t(plMipmap::kAlphaChannelFlag), uint16_t(plMipmap::kAlphaBitFlag)}) {
        plMipmap* mm = Compressed(16, 16, plMipmap::DirectXInfo::kDXT1);
        mm->SetFlags(mm->GetFlags() | flags);
        EXPECT_EQ(plWGPUBlockFormatOf(mm), plWGPUBlockFormat::kBC1) << "flags " << flags;
        delete mm;
        mm = Compressed(16, 16, plMipmap::DirectXInfo::kDXT5);
        mm->SetFlags(mm->GetFlags() | flags);
        EXPECT_EQ(plWGPUBlockFormatOf(mm), plWGPUBlockFormat::kBC3) << "flags " << flags;
        delete mm;
    }
    EXPECT_EQ(plWGPUBlockBytes(plWGPUBlockFormat::kBC1), uint32_t(8));
    EXPECT_EQ(plWGPUBlockBytes(plWGPUBlockFormat::kBC3), uint32_t(16));
}

TEST(plWGPUTextureFormat, block_other_DXT_types_and_uncompressed_data_have_none)
{
    // The DXT2-4 cases of IGetD3DTextureFormat are commented out (:7693-7701), as are their
    // enumerators in plBitmap::DirectXInfo: types 2-4 and kError fall to IGetD3DTextureFormat's assert (:7706) and keep
    // D3DFMT_UNKNOWN. Uncompressed data takes the other branch.
    for (uint8_t dxt : {uint8_t(plMipmap::DirectXInfo::kError), uint8_t(2), uint8_t(3), uint8_t(4)}) {
        plMipmap* mm = Compressed(16, 16, dxt);
        EXPECT_EQ(plWGPUBlockFormatOf(mm), plWGPUBlockFormat::kNone) << "DXT type " << int(dxt);
        delete mm;
    }
    plMipmap* mm = Uncompressed(16, 16, plMipmap::kARGB32Config, plMipmap::kAlphaChannelFlag);
    EXPECT_EQ(plWGPUBlockFormatOf(mm), plWGPUBlockFormat::kNone);
    delete mm;
    EXPECT_EQ(plWGPUBlockBytes(plWGPUBlockFormat::kNone), uint32_t(0));
}

TEST(plWGPUTextureFormat, block_rows_cover_each_kept_level_exactly)
{
    // D3D9 copies each kept level of DXT data as it is (IFillD3DTexture), a whole number of 4x4
    // blocks: the level's bytes (plMipmap::IBuildLevelSizes, fBlockSize per block) are rows of
    // ceil(width / 4) blocks of plWGPUBlockBytes, ceil(height / 4) rows of them.
    const uint32_t sizes[][2] = {{256, 256}, {64, 16}, {16, 128}, {4, 4}, {512, 8}};
    for (uint8_t dxt : {plMipmap::DirectXInfo::kDXT1, plMipmap::DirectXInfo::kDXT5}) {
        for (const auto& size : sizes) {
            plMipmap*      mm = Compressed(size[0], size[1], dxt);
            const uint32_t blockBytes = plWGPUBlockBytes(plWGPUBlockFormatOf(mm));
            const int32_t  maxLevel = plWGPUAllowedLevels(mm, false);
            for (int32_t level = 0; level <= maxLevel; level++) {
                uint32_t w = 0, h = 0, rowBytes = 0;
                mm->GetLevelPtr(uint8_t(level), &w, &h, &rowBytes);
                EXPECT_EQ(((w + 3) / 4) * blockBytes * ((h + 3) / 4), mm->GetLevelSize(uint8_t(level)))
                    << "DXT type " << int(dxt) << ", " << size[0] << "x" << size[1] << " level " << level;
            }
            delete mm;
        }
    }
}

//// Cube faces ///////////////////////////////////////////////////////////////

TEST(plWGPUTextureFormat, cube_32bit_faces_all_take_face_5)
{
    // IMakeCubicTextureRef processes 32-bit faces one after another (its face loop, :7488), each reformatted
    // by IProcessMipmapLevels into one shared scratch buffer (IGetPixelScratch, :7637) whose pointer every face keeps:
    // the chain, the sizes and the texels of every face are face 5's, the last processed. With or
    // without alpha, colour or intensity.
    const uint16_t flagSets[] = {0, plMipmap::kAlphaChannelFlag, plMipmap::kIntensityMap,
                                 plMipmap::kIntensityMap | plMipmap::kAlphaChannelFlag};
    for (unsigned config : {plMipmap::kARGB32Config, plMipmap::kRGB32Config}) {
        for (uint16_t flags : flagSets) {
            plMipmap* face0 = Uncompressed(64, 64, config, flags);
            EXPECT_EQ(plWGPUCubeLevelFace(face0), uint32_t(5)) << "config " << config << ", flags " << flags;
            for (uint32_t face = 0; face < 6; face++)
                EXPECT_EQ(plWGPUCubeDataFace(face0, face), uint32_t(5))
                    << "config " << config << ", flags " << flags << ", face " << face;
            delete face0;
        }
    }
}

TEST(plWGPUTextureFormat, cube_compressed_and_sub32bit_faces_take_face_0_levels_and_their_own_texels)
{
    // IMakeCubicTextureRef's branch for block-compressed and 8/16-bit faces (:7476): IProcessMipmapLevels runs
    // once, on face 0, whose chain and sizes every face takes, and each face's data is its own image.
    std::vector<plMipmap*> faces0 = {
        Compressed(64, 64, plMipmap::DirectXInfo::kDXT1),
        Compressed(64, 64, plMipmap::DirectXInfo::kDXT5),
        Uncompressed(64, 64, plMipmap::kRGB16Config, plMipmap::kAlphaChannelFlag),
        Uncompressed(64, 64, plMipmap::kRGB16Config, plMipmap::kIntensityMap),
        Uncompressed(64, 64, plMipmap::kGray8Config, plMipmap::kIntensityMap),
    };
    for (size_t i = 0; i < faces0.size(); i++) {
        EXPECT_EQ(plWGPUCubeLevelFace(faces0[i]), uint32_t(0)) << "case " << i;
        for (uint32_t face = 0; face < 6; face++)
            EXPECT_EQ(plWGPUCubeDataFace(faces0[i], face), face) << "case " << i << ", face " << face;
        delete faces0[i];
    }
}

//// Uncompressed formats /////////////////////////////////////////////////////

TEST(plWGPUTextureFormat, format_32bit_colour_is_A8R8G8B8)
{
    // At 32-bit colour depth, colour data takes D3DFMT_A8R8G8B8 with or without an alpha
    // channel (IGetD3DTextureFormat :7795, :7813), and IFormatTextureData writes every channel, the
    // data's alpha included (:7935).
    const std::vector<hsColor32> px = Colours32();
    for (unsigned config : {plMipmap::kARGB32Config, plMipmap::kRGB32Config}) {
        plMipmap                       mm(4, 4, config, 1);
        const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
        for (uint32_t i = 0; i < px.size(); i++)
            ExpectTexel(out[i], px[i].r, px[i].g, px[i].b, px[i].a, i);
    }
}

TEST(plWGPUTextureFormat, format_32bit_intensity_with_alpha_is_A8L8)
{
    // D3DFMT_A8L8 (IGetD3DTextureFormat :7741): IFormatTextureData keeps red as the luminance and the alpha
    // (:7882); the sampler returns (L, L, L, A).
    const std::vector<hsColor32> px = Colours32();
    plMipmap                     mm(4, 4, plMipmap::kARGB32Config, 1);
    mm.SetFlags(plMipmap::kIntensityMap | plMipmap::kAlphaChannelFlag);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++)
        ExpectTexel(out[i], px[i].r, px[i].r, px[i].r, px[i].a, i);
}

TEST(plWGPUTextureFormat, format_32bit_intensity_is_L8)
{
    // D3DFMT_L8 (IGetD3DTextureFormat :7764): IFormatTextureData keeps red (:7924); the sampler returns
    // (L, L, L, 1).
    const std::vector<hsColor32> px = Colours32();
    plMipmap                     mm(4, 4, plMipmap::kRGB32Config, 1);
    mm.SetFlags(plMipmap::kIntensityMap);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++)
        ExpectTexel(out[i], px[i].r, px[i].r, px[i].r, 255, i);
}

TEST(plWGPUTextureFormat, format_16bit_intensity_with_alpha_is_A8L8)
{
    // 16-bit intensity data with alpha (kAInten88) is D3DFMT_A8L8 (IGetD3DTextureFormat :7723), used as it is
    // (IProcessMipmapLevels :7602): the high byte is alpha, the low byte luminance.
    std::vector<uint16_t> px;
    for (uint32_t a = 0; a < 256; a += 17)
        for (uint32_t l = 0; l < 256; l += 5)
            px.push_back(uint16_t((a << 8) | l));
    plMipmap mm(4, 4, plMipmap::kRGB16Config, 1);
    mm.SetFlags(plMipmap::kIntensityMap | plMipmap::kAlphaChannelFlag);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++) {
        const uint8_t l = uint8_t(px[i] & 0xff), a = uint8_t(px[i] >> 8);
        ExpectTexel(out[i], l, l, l, a, i);
    }
}

TEST(plWGPUTextureFormat, format_16bit_intensity_without_alpha_is_L8_byte_for_byte)
{
    // 16-bit intensity data without alpha is D3DFMT_L8 (IGetD3DTextureFormat :7725), used as it is
    // (IProcessMipmapLevels :7602). IFillD3DTexture copies the level's bytes from the start of its one-byte
    // texels (:7271), so texel k is byte k of the data: (L, L, L, 1). The second half of the 2-byte level is
    // copied past the locked level, and nothing past numPix is written here.
    std::vector<uint8_t> bytes(32);
    for (uint32_t k = 0; k < bytes.size(); k++)
        bytes[k] = uint8_t(k * 29 + 5);
    plMipmap mm(4, 4, plMipmap::kRGB16Config, 1);
    mm.SetFlags(plMipmap::kIntensityMap);
    std::vector<plWGPUBGRA8> out(20, plWGPUBGRA8{1, 2, 3, 4});
    plWGPUFormatTexels(&mm, bytes.data(), 16, out.data());
    for (uint32_t k = 0; k < 16; k++)
        ExpectTexel(out[k], bytes[k], bytes[k], bytes[k], 255, k);
    for (uint32_t k = 16; k < out.size(); k++)
        ExpectTexel(out[k], 3, 2, 1, 4, k);
}

TEST(plWGPUTextureFormat, format_16bit_with_alpha_is_A4R4G4B4)
{
    // 16-bit colour data with alpha is D3DFMT_A4R4G4B4 (IGetD3DTextureFormat :7730): alpha in bits 12-15, red
    // 8-11, green 4-7, blue 0-3 (the layout IFormatTextureData writes, :7894-7897). Every value
    // of every channel.
    std::vector<uint16_t> px;
    for (uint32_t v = 0; v < 16; v++)
        for (uint32_t c = 0; c < 4; c++)
            px.push_back(uint16_t(v << (4 * c)) | uint16_t(((15 - v) & 0xf) << (4 * ((c + 1) & 3))));
    plMipmap mm(4, 4, plMipmap::kRGB16Config, 1);
    mm.SetFlags(plMipmap::kAlphaChannelFlag);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++) {
        const uint16_t v = px[i];
        ExpectTexel(out[i], Unorm8((v >> 8) & 0xf, 4), Unorm8((v >> 4) & 0xf, 4), Unorm8(v & 0xf, 4),
                    Unorm8(v >> 12, 4), i);
    }
}

TEST(plWGPUTextureFormat, format_16bit_without_alpha_is_A1R5G5B5)
{
    // 16-bit colour data without an alpha channel is D3DFMT_A1R5G5B5 (IGetD3DTextureFormat :7732): red in bits
    // 10-14, green 5-9, blue 0-4 and a one-bit alpha in bit 15 (IFormatTextureData :7910-7912).
    // Every value of every channel, with and without the alpha bit.
    std::vector<uint16_t> px;
    for (uint32_t alpha = 0; alpha < 2; alpha++)
        for (uint32_t v = 0; v < 32; v++)
            for (uint32_t c = 0; c < 3; c++)
                px.push_back(uint16_t((alpha << 15) | (v << (5 * c)) | (((31 - v) & 0x1f) << (5 * ((c + 1) % 3)))));
    plMipmap mm(4, 4, plMipmap::kRGB16Config, 1);
    mm.SetFlags(0);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++) {
        const uint16_t v = px[i];
        ExpectTexel(out[i], Unorm8((v >> 10) & 0x1f, 5), Unorm8((v >> 5) & 0x1f, 5), Unorm8(v & 0x1f, 5),
                    Unorm8(v >> 15, 1), i);
    }
}

TEST(plWGPUTextureFormat, format_8bit_intensity_is_L8)
{
    // 8-bit intensity data (kInten8, the software DXT decoder's intensity output) is D3DFMT_L8
    // (IGetD3DTextureFormat :7764), used as it is (IProcessMipmapLevels :7602): (L, L, L, 1).
    std::vector<uint8_t> px(256);
    for (uint32_t i = 0; i < px.size(); i++)
        px[i] = uint8_t(i);
    plMipmap mm(4, 4, plMipmap::kGray8Config, 1);
    mm.SetFlags(plMipmap::kIntensityMap);
    const std::vector<plWGPUBGRA8> out = Format(&mm, px.data(), uint32_t(px.size()));
    for (uint32_t i = 0; i < px.size(); i++)
        ExpectTexel(out[i], px[i], px[i], px[i], 255, i);
}

TEST(plWGPUTextureFormat, format_writes_exactly_the_pixels_asked_for)
{
    // One texel per pixel, none past numPix (the level's w x rows, plWGPUDevice::IUploadLevels).
    const std::vector<hsColor32> px = Colours32();
    for (unsigned config : {plMipmap::kARGB32Config, plMipmap::kRGB16Config, plMipmap::kGray8Config}) {
        plMipmap mm(4, 4, config, 1);
        mm.SetFlags(config == plMipmap::kGray8Config ? plMipmap::kIntensityMap : 0);
        std::vector<plWGPUBGRA8> out(20, plWGPUBGRA8{1, 2, 3, 4});
        plWGPUFormatTexels(&mm, reinterpret_cast<const uint8_t*>(px.data()), 16, out.data());
        for (uint32_t i = 16; i < out.size(); i++)
            ExpectTexel(out[i], 3, 2, 1, 4, i);
    }
}
