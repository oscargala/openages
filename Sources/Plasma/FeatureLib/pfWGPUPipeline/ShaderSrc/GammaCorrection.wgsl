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

// The gamma ramp at present. A port of Metal's non-tiler post pass
// (pfMetalPipeline/ShaderSrc/GammaCorrection.metal's gammaCorrectVertex and
// gammaCorrectFragment, drawn by plMetalDevice::PostprocessIntoDrawable): a full-frame quad
// reads the unprocessed frame at the pixel position and writes the ramp's entry per channel,
// alpha 1.
// - The index is the texel's 8-bit channel code: a textureLoad of the 256x3 integer table, not
//   a filtered sample (on 8-bit input this equals Metal's nearest sample (lutSampler) at c).
// - The entry becomes the unorm8 code (entry * 255 + 32767) / 65535 in integer arithmetic,
//   written as code / 255: Metal's entry / USHRT_MAX to unorm, with the rounding made exact.
//   The identity ramp (i * 257) maps every code to itself.

struct GammaVertexOut
{
    @builtin(position) position: vec4f,
};

@vertex fn gammaCorrectVertex(@builtin(vertex_index) vertexID: u32) -> GammaVertexOut
{
    // Metal's fullFrameCoords, drawn as a 4-vertex triangle strip
    // (plMetalDevice::PostprocessIntoDrawable).
    var positions = array<vec2f, 4>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0), vec2f(1.0, 1.0));
    var out: GammaVertexOut;
    out.position = vec4f(positions[vertexID], 0.5, 1.0);
    return out;
}

@group(0) @binding(0) var inputTexture: texture_2d<f32>;
@group(0) @binding(1) var LUT: texture_2d<u32>;

fn gammaChannel(c: f32, channel: u32) -> f32
{
    let code = u32(round(clamp(c, 0.0, 1.0) * 255.0));
    let entry = textureLoad(LUT, vec2u(code, channel), 0).x;
    return f32((entry * 255u + 32767u) / 65535u) / 255.0;
}

@fragment fn gammaCorrectFragment(in: GammaVertexOut) -> @location(0) vec4f
{
    let color = textureLoad(inputTexture, vec2u(in.position.xy), 0);
    return vec4f(gammaChannel(color.r, 0u), gammaChannel(color.g, 1u), gammaChannel(color.b, 2u), 1.0);
}
