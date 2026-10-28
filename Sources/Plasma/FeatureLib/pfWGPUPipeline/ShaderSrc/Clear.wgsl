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

// A translation of pfMetalPipeline/ShaderSrc/Clear.metal, the mid-pass clear of
// plWGPUDevice::Clear (plMetalDevice::Clear). Metal's function constants
// ShouldClearDepth/ShouldClearColor become three fragment entry points: WGSL cannot drop a
// fragment output by a constant, and a shader that writes frag_depth needs a depth
// attachment. The values follow the D3D9 clear (plDXPipeline::ClearRenderTarget): the
// colour is the clear colour quantized like inlGetD3DColor and the depth is the requested one
// (plMetalDevice::Clear's encoder passes a shadowed local depth instead).

struct ClearUniforms
{
    color: vec4f,
    depth: f32,
};

@group(0) @binding(0) var<uniform> clearValues: ClearUniforms;

struct ClearVertexOut
{
    @builtin(position) position: vec4f,
};

// Metal reads the four NDC corners from vertex bytes (clearCoords, plMetalDevice::Clear);
// here they come from the vertex index, same order, drawn as a triangle strip.
@vertex fn clearVertex(@builtin(vertex_index) v_id: u32) -> ClearVertexOut
{
    var clearCoords = array<vec2f, 4>(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0), vec2f(1.0, 1.0));
    var out: ClearVertexOut;
    // Just pass the position through. We're clearing in NDC space.
    out.position = vec4f(clearCoords[v_id], 0.5, 1.0);
    return out;
}

struct ClearColorDepthOut
{
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
};

@fragment fn clearFragmentColorDepth() -> ClearColorDepthOut
{
    var out: ClearColorDepthOut;
    out.color = clearValues.color;
    out.depth = clearValues.depth;
    return out;
}

@fragment fn clearFragmentColor() -> @location(0) vec4f
{
    return clearValues.color;
}

@fragment fn clearFragmentDepth() -> @builtin(frag_depth) f32
{
    return clearValues.depth;
}
