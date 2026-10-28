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

// The D3D9 texture levels, cube faces and formats of pfWGPUPipeline (see plWGPUTextureFormat.h).

#include "plWGPUTextureFormat.h"

#include <cstring>

#include "HeadSpin.h"
#include "plGImage/plMipmap.h"

plWGPUBlockFormat plWGPUBlockFormatOf(const plBitmap* bitmap)
{
    // plDXPipeline::IGetD3DTextureFormat: kDXT1 is D3DFMT_DXT1 and kDXT5 D3DFMT_DXT5; its DXT2-4
    // cases are commented out, so any other type asserts there and stays D3DFMT_UNKNOWN.
    if (!bitmap->IsCompressed())
        return plWGPUBlockFormat::kNone;
    switch (bitmap->fDirectXInfo.fCompressionType) {
        case plBitmap::DirectXInfo::kDXT1:
            return plWGPUBlockFormat::kBC1;
        case plBitmap::DirectXInfo::kDXT5:
            return plWGPUBlockFormat::kBC3;
        default:
            return plWGPUBlockFormat::kNone;
    }
}

uint32_t plWGPUBlockBytes(plWGPUBlockFormat format)
{
    switch (format) {
        case plWGPUBlockFormat::kBC1:
            return 8;
        case plWGPUBlockFormat::kBC3:
            return 16;
        default:
            return 0;
    }
}

int32_t plWGPUAllowedLevels(plMipmap* mipmap, bool doesSmallTextures)
{
    // plDXPipeline::IProcessMipmapLevels: compressed and 8/16-bit data drop the levels whose
    // width or height is not a multiple of four (always for compressed data, and for the rest
    // when the device lacks kCapsDoesSmallTextures, which SetFudgeFactors(kDefaultChipset)
    // clears). 32-bit data keeps every level.
    // Metal truncates only compressed data (plMetalDevice::ConfigureAllowedLevels).
    // Compressed and 8/16-bit data leave level 0 current, truncated or not (IProcessMipmapLevels'
    // SetCurrLevel(0), plDXPipeline.cpp:7590); 32-bit data keeps the level it came with.
    int32_t maxLevel = mipmap->GetNumLevels() - 1;
    if (mipmap->IsCompressed() || mipmap->GetPixelSize() < 32) {
        if (mipmap->IsCompressed() || !doesSmallTextures) {
            mipmap->SetCurrLevel(maxLevel);
            while (maxLevel && (mipmap->GetCurrWidth() | mipmap->GetCurrHeight()) & 0x03) {
                maxLevel--;
                mipmap->SetCurrLevel(maxLevel);
            }
        }
        mipmap->SetCurrLevel(0);
    }
    return maxLevel;
}

// plDXPipeline::IMakeCubicTextureRef's branch on faces[0]: compressed and 8/16-bit data are used
// as they are, 32-bit data is reformatted face by face.
static bool ICubeFacesReformatted(const plMipmap* face0)
{
    return !(face0->IsCompressed() || face0->GetPixelSize() < 32);
}

uint32_t plWGPUCubeLevelFace(const plMipmap* face0)
{
    return ICubeFacesReformatted(face0) ? 5 : 0;
}

uint32_t plWGPUCubeDataFace(const plMipmap* face0, uint32_t face)
{
    return ICubeFacesReformatted(face0) ? 5 : face;
}

void plWGPUFormatTexels(const plMipmap* src, const uint8_t* data, uint32_t numPix, plWGPUBGRA8* out)
{
    // Uncompressed: D3D9's format choice (plDXPipeline::IGetD3DTextureFormat) and data
    // (plDXPipeline::IFormatTextureData), expanded to bgra8unorm.
    const uint32_t flags = src->GetFlags();
    if (src->GetPixelSize() == 32) {
        const plWGPUBGRA8* in = reinterpret_cast<const plWGPUBGRA8*>(data);
        if (flags & plMipmap::kIntensityMap) {
            // D3DFMT_A8L8 (alpha) or D3DFMT_L8: L = red (IFormatTextureData)
            const bool alpha = (flags & plMipmap::kAlphaChannelFlag) != 0;
            for (uint32_t i = 0; i < numPix; i++)
                out[i] = {in[i].r, in[i].r, in[i].r, alpha ? in[i].a : uint8_t(255)};
        } else {
            // D3DFMT_A8R8G8B8: the data's own alpha (IFormatTextureData)
            memcpy(out, in, numPix * sizeof(plWGPUBGRA8));
        }
    } else if (src->GetPixelSize() == 16) {
        const uint16_t* in = reinterpret_cast<const uint16_t*>(data);
        if (flags & plMipmap::kIntensityMap) {
            if (flags & plMipmap::kAlphaChannelFlag) {
                // D3DFMT_A8L8: high byte alpha, low byte luminance
                for (uint32_t i = 0; i < numPix; i++) {
                    const uint8_t l = uint8_t(in[i] & 0xff);
                    out[i] = {l, l, l, uint8_t(in[i] >> 8)};
                }
            } else {
                // D3DFMT_L8, filled with the 16-bit data as it is: IFillD3DTexture copies the
                // level's bytes into its one-byte texels from the start, so texel k is byte k (the
                // low byte of pixel k/2 for even k, its high byte for odd k). The level's second
                // half lands past the locked level and is not reproduced.
                for (uint32_t i = 0; i < numPix; i++)
                    out[i] = {data[i], data[i], data[i], 255};
            }
        } else if (flags & plMipmap::kAlphaChannelFlag) {
            // D3DFMT_A4R4G4B4: each nibble n samples as n/15 = n*17/255
            for (uint32_t i = 0; i < numPix; i++) {
                const uint16_t v = in[i];
                out[i] = {uint8_t((v & 0xf) * 17), uint8_t(((v >> 4) & 0xf) * 17),
                          uint8_t(((v >> 8) & 0xf) * 17), uint8_t(((v >> 12) & 0xf) * 17)};
            }
        } else {
            // D3DFMT_A1R5G5B5: each 5-bit field f samples as f/31
            auto expand5 = [](uint16_t f) { return uint8_t((f * 255 + 15) / 31); };
            for (uint32_t i = 0; i < numPix; i++) {
                const uint16_t v = in[i];
                out[i] = {expand5(v & 0x1f), expand5((v >> 5) & 0x1f), expand5((v >> 10) & 0x1f),
                          uint8_t((v & 0x8000) ? 255 : 0)};
            }
        }
    } else if (src->GetPixelSize() == 8) {
        // kInten8 -> D3DFMT_L8
        const uint8_t* in = data;
        for (uint32_t i = 0; i < numPix; i++)
            out[i] = {in[i], in[i], in[i], 255};
    } else {
        hsAssert(false, "Unknown texture pixel size");
    }
}
