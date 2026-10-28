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

// The Grass program family (pfMetalPipeline/ShaderSrc/Grass.metal names it),
// translated from the D3D9 bytecode by pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py. Do not edit:
// regenerate. Each statement is one instruction token, the instruction in its comment.
// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).

// vs_GrassShader: plSurface/vs_GrassShader.h, 125 tokens
fn vs_GrassShader_run(in: VertexIn) -> VSRegs {
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
    let v5 = d3dColor(in.diffuse); // dcl_color v5
    let v7 = in.uvw0; // dcl_texcoord v7
    r0 = c(11u) * v0.xxxx; // mul r0, c11, v0.x
    r0 = c(12u) * v0.yyyy + r0; // mad r0, c12, v0.y, r0
    r1 = c(5u).xxxx; // mov r1, c5.x
    r0 = r1 * c(13u) + r0; // mad r0, r1, c13, r0
    r0 = wmask(r0, frc(r0), vec4<bool>(true, true, false, false)); // frc r0.xy, r0
    r1 = wmask(r1, frc(r0.zwzw), vec4<bool>(true, true, false, false)); // frc r1.xy, r0.zwzw
    r0 = wmask(r0, r1.xyxy, vec4<bool>(false, false, true, true)); // mov r0.zw, r1.xyxy
    r0 = r0 + (-c(4u).yyyy); // add r0, r0, -c4.y
    r1 = r0 * c(6u).wwww; // mul r1, r0, c6.w
    r2 = r1 * r1; // mul r2, r1, r1
    r3 = r2 * r1; // mul r3, r2, r1
    r5 = r3 * r2; // mul r5, r3, r2
    r7 = r5 * r2; // mul r7, r5, r2
    r9 = r7 * r2; // mul r9, r7, r2
    r0 = r3 * c(7u).xxxx + r1; // mad r0, r3, c7.x, r1
    r0 = r5 * c(7u).yyyy + r0; // mad r0, r5, c7.y, r0
    r0 = r7 * c(7u).zzzz + r0; // mad r0, r7, c7.z, r0
    r0 = r9 * c(7u).wwww + r0; // mad r0, r9, c7.w, r0
    r3 = wmask(r3, vec4f(dot(r0, c(8u))), vec4<bool>(true, false, false, false)); // dp4 r3.x, r0, c8
    r3 = wmask(r3, vec4f(dot(r0, c(9u))), vec4<bool>(false, true, false, false)); // dp4 r3.y, r0, c9
    r3 = wmask(r3, vec4f(dot(r0, c(10u))), vec4<bool>(false, false, true, true)); // dp4 r3.zw, r0, c10
    r4 = c(4u).zzzz + (-v7.yyyy); // add r4, c4.z, -v7.y
    r3 = r3 * r4; // mul r3, r3, r4
    r2 = wmask(r2, v0, vec4<bool>(false, false, false, true)); // mov r2.w, v0
    r2 = wmask(r2, r3 + v0, vec4<bool>(true, true, true, false)); // add r2.xyz, r3, v0
    o.oPos = m4x4(r2, 0u); // m4x4 oPos, r2, c0
    o.oFog = (c(4u).zzzz).x; // mov oFog, c4.z
    o.oD0 = v5; // mov oD0, v5
    o.oT0 = v7; // mov oT0, v7
    return o;
}

@vertex
fn vs_GrassShader(in: VertexIn) -> VSOut {
    return vsFinish(vs_GrassShader_run(in));
}

// ps_GrassShader: plSurface/ps_GrassShader.h, 8 tokens
fn ps_GrassShader_run(in: VSOut) -> vec4f {
    let v0 = in.d0;
    let v1 = in.d1;
    var r0 = vec4f(0.0);
    var r1 = vec4f(0.0);
    var t0 = vec4f(0.0);
    var t1 = vec4f(0.0);
    var t2 = vec4f(0.0);
    var t3 = vec4f(0.0);
    t0 = texStage(0u, in.t0); // tex t0
    r0 = pc(t0 * v0); // mul r0, t0, v0
    return r0;
}

@fragment
fn ps_GrassShader(in: VSOut) -> @location(0) vec4f {
    return psFinish(ps_GrassShader_run(in), in.fog);
}
