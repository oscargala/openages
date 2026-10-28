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

#ifndef plWGPUPipelineKey_h
#define plWGPUPipelineKey_h

// The packed keys of the pipeline states (plWGPUPipelineState.h): the D3D9 render state and the
// shader variants a WebGPU pipeline bakes in, as plain data with no WebGPU type. The device caches
// its pipelines by Pack() with the fixed state and the target formats above it
// (plWGPUDevice::DrawFixedFunction, plWGPUDevice::IShaderPipeline).

#include <cstdint>

/*
 The pipeline of a fixed-function pass (FixedPipelineShaders.wgsl). What D3D9 keeps as render
 state and WebGPU bakes into a pipeline: the frame-buffer blend (D3DRS_ALPHABLENDENABLE,
 SRCBLEND, DESTBLEND, BLENDOP), COLORWRITEENABLE, the depth test (ZENABLE, ZFUNC,
 ZWRITEENABLE), the cull mode, the primitive type and the FVF; plus the shader variants
 (kStages, kLighting, kFog, kVertexBlend).
 */
struct plWGPUFixedFunctionKey
{
    uint8_t blendEnabled;
    uint8_t srcBlend;     // D3DBLEND
    uint8_t dstBlend;     // D3DBLEND
    uint8_t blendOp;      // D3DBLENDOP
    uint8_t writeMask;    // D3DRS_COLORWRITEENABLE bits (R=1, G=2, B=4, A=8)
    uint8_t stages;       // kStages
    uint8_t lighting;     // kLighting
    uint8_t fog;          // kFog
    uint8_t vertexBlend;  // kVertexBlend
    uint8_t vertexFormat; // plGBufferGroup format of the vertex buffer
    uint8_t plateVertex;  // vertex kind: 0 span vertex; 1 the plate quad's
                          // D3DFVF_XYZ|DIFFUSE|TEX1|TEXCOORDSIZE3(0); 2 D3DFVF_XYZ|TEX1
                          // (the clothing and blur quads)
    uint8_t pad;

    uint64_t Pack() const
    {
        return uint64_t(blendEnabled) | (uint64_t(srcBlend) << 1) | (uint64_t(dstBlend) << 5) |
               (uint64_t(blendOp) << 9) | (uint64_t(writeMask) << 12) | (uint64_t(stages) << 16) |
               (uint64_t(lighting) << 20) | (uint64_t(fog) << 21) | (uint64_t(vertexBlend) << 23) |
               (uint64_t(vertexFormat) << 24) | (uint64_t(plateVertex) << 32);
    }
};

/*
 The pipeline of a pass with D3D9 shader programs. The vertex and pixel programs
 (plWGPUShaderProgram table index), the buffer's vertex format (the shader's dcl_ usages read
 it) and the D3D9 render state WebGPU bakes into a pipeline, as for the fixed-function key: the
 frame-buffer blend, COLORWRITEENABLE, FOGENABLE (the fog blend after the pixel program) and
 the fixed state. Nothing else: the product and the harness build create the same pipelines.
 */
struct plWGPUShaderKey
{
    uint8_t  vs; // plWGPUShaderProgram::Index()
    uint8_t  ps;
    uint8_t  blendEnabled;
    uint8_t  srcBlend;
    uint8_t  dstBlend;
    uint8_t  blendOp;
    uint8_t  writeMask;
    uint8_t  vertexFormat; // plGBufferGroup format of the vertex buffer
    uint8_t  fog;          // D3DRS_FOGENABLE
    // Bits 0-32 (plWGPUDevice::IShaderPipeline packs the fixed state and the target formats above them).
    uint64_t Pack() const
    {
        return uint64_t(vs & 0xf) | (uint64_t(ps & 0xf) << 4) | (uint64_t(blendEnabled) << 8) |
               (uint64_t(srcBlend & 0xf) << 9) | (uint64_t(dstBlend & 0xf) << 13) | (uint64_t(blendOp & 0x7) << 17) |
               (uint64_t(writeMask & 0xf) << 20) | (uint64_t(vertexFormat) << 24) | (uint64_t(fog & 1) << 32);
    }
};

#endif /* plWGPUPipelineKey_h */
