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

// The BiasNormals program family (pfMetalPipeline/ShaderSrc/BiasNormals.metal names it),
// translated from the D3D9 bytecode by pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py. Do not edit:
// regenerate. Each statement is one instruction token, the instruction in its comment.
// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).

// vs_BiasNormals: plSurface/vs_BiasNormals.h, 42 tokens
fn vs_BiasNormals_run(in: VertexIn) -> VSRegs {
    var o: VSRegs;
    o.oPos = vec4f(0.0, 0.0, 0.0, 1.0);
    o.oFog = 1.0;   // unwritten output registers: the family's pixel program never reads them
    var r0 = vec4f(0.0);
    var r1 = vec4f(0.0);
    var r2 = vec4f(0.0);
    var r3 = vec4f(0.0);
    var r4 = vec4f(0.0);
    var r5 = vec4f(0.0);
    var r6 = vec4f(0.0);
    var r7 = vec4f(0.0);
    var r8 = vec4f(0.0);
    var r9 = vec4f(0.0);
    var r10 = vec4f(0.0);
    var r11 = vec4f(0.0);
    let v0 = in.position; // dcl_position v0
    let v7 = in.uvw0; // dcl_texcoord v7
    o.oPos = v0; // mov oPos, v0
    r0 = wmask(r0, c(4u).xxxz, vec4<bool>(false, false, true, true)); // mov r0.zw, c4.xxxz
    r0 = wmask(r0, vec4f(dot(v7, c(0u))), vec4<bool>(true, false, false, false)); // dp4 r0.x, v7, c0
    r0 = wmask(r0, vec4f(dot(v7, c(1u))), vec4<bool>(false, true, false, false)); // dp4 r0.y, v7, c1
    o.oT0 = r0; // mov oT0, r0
    r0 = wmask(r0, vec4f(dot(v7, c(2u))), vec4<bool>(true, false, false, false)); // dp4 r0.x, v7, c2
    r0 = wmask(r0, vec4f(dot(v7, c(3u))), vec4<bool>(false, true, false, false)); // dp4 r0.y, v7, c3
    o.oT1 = r0; // mov oT1, r0
    o.oD0 = c(5u).xxzz; // mov oD0, c5.xxzz
    o.oD1 = c(5u).yyzz; // mov oD1, c5.yyzz
    return o;
}

@vertex
fn vs_BiasNormals(in: VertexIn) -> VSOut {
    return vsFinish(vs_BiasNormals_run(in));
}

// ps_BiasNormals: plSurface/ps_BiasNormals.h, 19 tokens
fn ps_BiasNormals_run(in: VSOut) -> vec4f {
    let v0 = in.d0;
    let v1 = in.d1;
    var r0 = vec4f(0.0);
    var r1 = vec4f(0.0);
    var t0 = vec4f(0.0);
    var t1 = vec4f(0.0);
    var t2 = vec4f(0.0);
    var t3 = vec4f(0.0);
    t0 = texStage(0u, in.t0); // tex t0
    t1 = texStage(1u, in.t1); // tex t1
    r0 = wmask(r0, pc(bias(t0) + bias(t1)), vec4<bool>(true, true, true, false)); // add r0.rgb, t0_bias, t1_bias
    r0 = wmask(r0, pc(t0 + t1), vec4<bool>(false, false, false, true)); // +add r0.a, t0, t1
    r0 = wmask(r0, pc(r0 * v0 + v1), vec4<bool>(true, true, true, false)); // mad r0.rgb, r0, v0, v1
    return r0;
}

@fragment
fn ps_BiasNormals(in: VSOut) -> @location(0) vec4f {
    return psFinish(ps_BiasNormals_run(in), in.fog);
}
