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

// A translation of pfMetalPipeline/ShaderSrc/TextFontShader.metal, plus the state of the
// D3D9 font (plDXTextFont::IInitStateBlocks): the alpha test (ALPHAREF 0x08, GREATEREQUAL),
// which WGSL has no state for. The vertex is plTextFont's plFontVertex (point,
// D3DCOLOR, UVW), read as vertex attributes instead of Metal's vertex-bytes array.

struct TextUniforms
{
    transform: mat4x4f,
};

@group(0) @binding(0) var<uniform> uniforms: TextUniforms;
@group(1) @binding(0) var colorMap: texture_2d<f32>;
@group(1) @binding(1) var colorSampler: sampler;

struct Vertex
{
    @location(0) position: vec3f,
    @location(1) color: vec4f,      // unorm8x4 of the D3DCOLOR: bytes B, G, R, A
    @location(2) UV: vec3f,
};

struct ColorInOut
{
    @builtin(position) position: vec4f,
    @location(0) texCoord: vec3f,
    @location(1) color: vec4f,
};

@vertex fn textFontVertexShader(vert: Vertex) -> ColorInOut
{
    var out: ColorInOut;

    // The half pixel of TextFontShader.metal's textFontVertexShader: D3D9 pixel centres sit at
    // integer screen coordinates, WebGPU's at +0.5.
    let position = vec4f(vert.position + vec3f(0.5, 0.5, 0.0), 1.0);
    out.position = uniforms.transform * position;
    out.texCoord = vert.UV;
    out.color = vert.color.zyxw;

    return out;
}

@fragment fn textFontFragmentShader(in: ColorInOut) -> @location(0) vec4f
{
    var colorSample = textureSample(colorMap, colorSampler, in.texCoord.xy);
    colorSample *= in.color;

    // D3DRS_ALPHATESTENABLE TRUE, D3DRS_ALPHAREF 0x08, D3DRS_ALPHAFUNC D3DCMP_GREATEREQUAL
    if (!(round(clamp(colorSample.a, 0.0, 1.0) * 255.0) >= 8.0)) {
        discard;
    }

    return colorSample;
}
