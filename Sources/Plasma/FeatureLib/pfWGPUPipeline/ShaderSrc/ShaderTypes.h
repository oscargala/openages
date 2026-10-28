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

#ifndef _plWGPUShaderTypes_h_
#define _plWGPUShaderTypes_h_

// The uniform blocks shared by the C++ side and the WGSL shaders of pfWGPUPipeline (the role
// of pfMetalPipeline/ShaderSrc/ShaderTypes.h). Each struct matches a WGSL struct byte for byte
// (WGSL uniform layout rules); the static_asserts pin that. They serve the screen-element
// shaders (clear, text), the fixed-function material shader (Metal's VertexUniforms and
// friends), which plates draw with as well, and the shader programs.

#include <cstddef>
#include <cstdint>

// Clear.wgsl ClearUniforms
struct plWGPUClearUniforms
{
    float color[4]; // the D3DCOLOR the clear quantizes to (inlGetD3DColor), as 0..1
    float depth;
    float pad[3];
};
static_assert(sizeof(plWGPUClearUniforms) == 32, "Clear.wgsl ClearUniforms is 32 bytes");
static_assert(offsetof(plWGPUClearUniforms, depth) == 16, "ClearUniforms.depth");

// TextFontShader.wgsl TextUniforms
struct plWGPUTextUniforms
{
    float transform[16]; // column-major (simd matrix_float4x4 layout, plMetalTextFont.cpp)
};
static_assert(sizeof(plWGPUTextUniforms) == 64, "TextFontShader.wgsl TextUniforms is 64 bytes");

// FixedPipelineShaders.wgsl FFUniforms: the D3D9 fixed-function state of one draw, snapshotted
// from plWGPUD3DDevice (plWGPUDevice::DrawFixedFunction). Matrices are D3DMATRIX rows as
// stored (row-vector convention, v' = v * M); WGSL reads them column-major, so `M * v` in the
// shader is D3D9's `v * M`.
struct plWGPUFFLight
{
    float position[4];  // camera space (D3D9 lights in camera space, LOCALVIEWER)
    float direction[4]; // camera space, normalized; w unused
    float diffuse[4];
    float specular[4];
    float ambient[4];
    float spot[4];  // range, falloff, cos(Theta/2), cos(Phi/2)
    float atten[4]; // Attenuation0, 1, 2, D3DLIGHTTYPE
};
static_assert(sizeof(plWGPUFFLight) == 112, "FixedPipelineShaders.wgsl Light is 112 bytes");

struct plWGPUFFStage
{
    uint32_t ops[4];   // COLOROP, COLORARG1, COLORARG2, ALPHAOP
    uint32_t ops2[4];  // ALPHAARG1, ALPHAARG2, TEXCOORDINDEX, TEXTURETRANSFORMFLAGS
    uint32_t flags[4]; // cube texture, texture bound, -, -
    float    bias[4];  // MIPMAPLODBIAS, -, -, -
};
static_assert(sizeof(plWGPUFFStage) == 64, "FixedPipelineShaders.wgsl Stage is 64 bytes");

struct plWGPUFFUniforms
{
    float         worldView0[16]; // D3DTS_WORLD * D3DTS_VIEW
    float         worldView1[16]; // D3DTS_WORLDMATRIX(1) * D3DTS_VIEW (D3DVBF_1WEIGHTS)
    float         normal0[16];    // inverse transpose of worldView0 (normal transform)
    float         normal1[16];    // inverse transpose of worldView1
    float         projection[16]; // D3DTS_PROJECTION
    float         texture[8][16]; // D3DTS_TEXTUREn
    plWGPUFFStage stages[8];
    plWGPUFFLight lights[8];
    float         matDiffuse[4];
    float         matAmbient[4];
    float         matSpecular[4];
    float         matEmissive[4];
    float         globalAmbient[4]; // D3DRS_AMBIENT
    float         fogColor[4];      // D3DRS_FOGCOLOR
    float         fogParams[4];     // FOGSTART, FOGEND, FOGDENSITY, -
    float         textureFactor[4]; // D3DRS_TEXTUREFACTOR
    uint32_t      lighting[4];      // SPECULARENABLE, number of lights, LOCALVIEWER, NORMALIZENORMALS
    uint32_t      sources[4];       // DIFFUSE/SPECULAR/AMBIENT/EMISSIVE MATERIALSOURCE
    uint32_t      fog[4];           // FOGTABLEMODE, FOGVERTEXMODE (as used), ALPHAFUNC (when ALPHATESTENABLE), ALPHAREF
    uint32_t      vertex[4];        // vertex has diffuse, vertex has specular, -, -
    float         misc[4];          // pixel-centre offset (1/W, -1/H), specular power, -
};
static_assert(sizeof(plWGPUFFUniforms) == 2448, "FixedPipelineShaders.wgsl FFUniforms is 2448 bytes");
static_assert(offsetof(plWGPUFFUniforms, texture) == 320, "FFUniforms.texture");
static_assert(offsetof(plWGPUFFUniforms, stages) == 832, "FFUniforms.stages");
static_assert(offsetof(plWGPUFFUniforms, lights) == 1344, "FFUniforms.lights");
static_assert(offsetof(plWGPUFFUniforms, matDiffuse) == 2240, "FFUniforms.matDiffuse");
static_assert(offsetof(plWGPUFFUniforms, lighting) == 2368, "FFUniforms.lighting");
static_assert(offsetof(plWGPUFFUniforms, misc) == 2432, "FFUniforms.misc");

// The vertex inputs of FixedPipelineShaders.wgsl: a missing D3D9 FVF component reads the zero
// buffer (array stride 0).
enum plWGPUFFLocation : uint32_t
{
    kFFLocPosition = 0,
    kFFLocWeight = 1,
    kFFLocNormal = 2,
    kFFLocDiffuse = 3,
    kFFLocSpecular = 4,
    kFFLocUVW0 = 5 // 5..12: UVW channels 0..7
};

// ShaderPrelude.wgsl ShaderUniforms, the uniform block of every shader-program draw: the D3D9
// vs_1_1 and ps_1_1 constant register files and the device state the programs' device side
// reads, nothing else.
struct plWGPUShaderUniforms
{
    float    vs[96][4];    // c0..c95 as set (SetVertexShaderConstantF)
    float    ps[8][4];     // c0..c7 clamped to the ps_1_1 range [-1, 1] ("ps_1_X registers")
    float    fogColor[4];  // D3DRS_FOGCOLOR
    float    misc[4];      // pixel-centre offset in clip units (1/W, -1/H), -, -
    uint32_t state[4];     // ALPHAFUNC (0: no test), ALPHAREF, -, -
    uint32_t stageCube[4]; // stages 0..3: the bound texture is a cube map
    float    stageBias[4]; // stages 0..3: D3DSAMP_MIPMAPLODBIAS
};
static_assert(sizeof(plWGPUShaderUniforms) == 1744, "ShaderPrelude.wgsl ShaderUniforms is 1744 bytes");
static_assert(offsetof(plWGPUShaderUniforms, ps) == 1536, "ShaderUniforms.ps");
static_assert(offsetof(plWGPUShaderUniforms, fogColor) == 1664, "ShaderUniforms.fogColor");
static_assert(offsetof(plWGPUShaderUniforms, stageBias) == 1728, "ShaderUniforms.stageBias");

#endif // _plWGPUShaderTypes_h_
