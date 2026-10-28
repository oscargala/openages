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

#ifndef plWGPUTextureFormat_h
#define plWGPUTextureFormat_h

// The D3D9 side of the texture upload (plWGPUDevice::MakeTextureRef, MakeCubicTextureRef): the
// mipmap levels D3D9 keeps, the faces a cube's levels and texels come from, the D3D9 format of
// block-compressed data, and the D3D9 format of uncompressed data, read back as bgra8unorm
// texels. Plain CPU code with no WebGPU type.

#include <cstdint>

class plBitmap;
class plMipmap;

// The block-compressed formats D3D9 uploads as they are (plDXPipeline::IGetD3DTextureFormat), and
// their WebGPU counterparts.
enum class plWGPUBlockFormat
{
    kNone, // not DXT data, or a DXT type D3D9 has no format for (D3DFMT_UNKNOWN)
    kBC1,  // D3DFMT_DXT1: 8 bytes per 4x4 block, its one-bit alpha decoded as D3D9 decodes it
    kBC3,  // D3DFMT_DXT5: 16 bytes per 4x4 block
};

/** The block format of `bitmap`'s DXT data: DXT1 and DXT5 have one; DXT2-4 have none in D3D9. */
plWGPUBlockFormat plWGPUBlockFormatOf(const plBitmap* bitmap);

/** Bytes per 4x4 block of `format`, 0 for kNone. */
uint32_t plWGPUBlockBytes(plWGPUBlockFormat format);

// D3DFMT_A8R8G8B8 is the memory order B, G, R, A of bgra8unorm.
struct plWGPUBGRA8
{
    uint8_t b, g, r, a;
};

/** The highest level of `mipmap` the upload keeps (plDXPipeline::IProcessMipmapLevels).
    doesSmallTextures is the device's kCapsDoesSmallTextures. */
int32_t plWGPUAllowedLevels(plMipmap* mipmap, bool doesSmallTextures);

/** The face whose level chain and level sizes D3D9 gives every face of a cube whose first face,
    as loaded, is `face0` (plDXPipeline::IMakeCubicTextureRef): face 0 for block-compressed and
    8/16-bit data, which IProcessMipmapLevels sizes once, from face 0; face 5 for 32-bit data,
    which it processes face by face, so that the last call's chain and sizes stay. */
uint32_t plWGPUCubeLevelFace(const plMipmap* face0);

/** The face whose texels D3D9 copies into cube face `face` (IMakeCubicTextureRef,
    IFillD3DCubeTexture): the face itself for block-compressed and 8/16-bit data, whose images
    are copied as they are; face 5 for 32-bit data, as IProcessMipmapLevels reformats each face
    into the one shared scratch buffer (IGetPixelScratch), every face's data is that buffer, and
    face 5 is reformatted last. */
uint32_t plWGPUCubeDataFace(const plMipmap* face0, uint32_t face);

/** One level of uncompressed `src` data (numPix pixels at `data`) in the D3D9 format
    plDXPipeline::IGetD3DTextureFormat picks for it, written as IFormatTextureData writes it and
    read back as the sampler reads it: one bgra8unorm texel per pixel into `out`. */
void plWGPUFormatTexels(const plMipmap* src, const uint8_t* data, uint32_t numPix, plWGPUBGRA8* out);

#endif // plWGPUTextureFormat_h
