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

// The WaveDec1Lay_7 program family (pfMetalPipeline/ShaderSrc/WaveDec1Lay_7.metal names it),
// translated from the D3D9 bytecode by pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py. Do not edit:
// regenerate. Each statement is one instruction token, the instruction in its comment.
// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).

// vs_WaveDec1Lay_7: plSurface/vs_WaveDec1Lay_7.h, 241 tokens
fn vs_WaveDec1Lay_7_run(in: VertexIn) -> VSRegs {
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
    r6 = wmask(r6, vec4f(m4x3(v0, 18u), 0.0), vec4<bool>(true, true, true, false)); // m4x3 r6.xyz, v0, c18
    r6 = wmask(r6, c(13u).zzzz, vec4<bool>(false, false, false, true)); // mov r6.w, c13.z
    r0 = c(7u) * r6.xxxx; // mul r0, c7, r6.x
    r0 = c(8u) * r6.yyyy + r0; // mad r0, c8, r6.y, r0
    r0 = r0 * c(4u); // mul r0, r0, c4
    r0 = r0 + c(5u); // add r0, r0, c5
    r4 = vec4f(rcp((c(12u).wwww).w)); // rcp r4, c12.w
    r0 = r0 + c(12u).zzzz; // add r0, r0, c12.z
    r0 = r0 * r4; // mul r0, r0, r4
    r1 = wmask(r1, expp((r0.xxxx).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.x
    r1 = wmask(r1, r1.yyyy, vec4<bool>(true, false, false, false)); // mov r1.x, r1.y
    r1 = wmask(r1, expp((r0.zzzz).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.z
    r1 = wmask(r1, r1.yyyy, vec4<bool>(false, false, true, false)); // mov r1.z, r1.y
    r1 = wmask(r1, expp((r0.wwww).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.w
    r1 = wmask(r1, r1.yyyy, vec4<bool>(false, false, false, true)); // mov r1.w, r1.y
    r1 = wmask(r1, expp((r0.yyyy).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.y
    r0 = r1 * c(12u).wwww; // mul r0, r1, c12.w
    r0 = r0 + (-c(12u).zzzz); // add r0, r0, -c12.z
    r1 = r0 * r0; // mul r1, r0, r0
    r2 = r1 * r0; // mul r2, r1, r0
    r3 = r1 * r1; // mul r3, r1, r1
    r4 = r1 * r2; // mul r4, r1, r2
    r5 = r2 * r3; // mul r5, r2, r3
    r1 = r1 * c(11u).yyyy; // mul r1, r1, c11.y
    r2 = r2 * c(10u).yyyy + r0; // mad r2, r2, c10.y, r0
    r1 = r1 + c(11u).xxxx; // add r1, r1, c11.x
    r2 = r4 * c(10u).zzzz + r2; // mad r2, r4, c10.z, r2
    r1 = r3 * c(11u).zzzz + r1; // mad r1, r3, c11.z, r1
    r4 = r4 * r0; // mul r4, r4, r0
    r2 = r5 * c(10u).wwww + r2; // mad r2, r5, c10.w, r2
    r1 = r4 * c(11u).wwww + r1; // mad r1, r4, c11.w, r1
    r4 = c(22u) + (-r6.zzzz); // add r4, c22, -r6.z
    r4 = r4 * c(23u); // mul r4, r4, c23
    r4 = r4 + c(24u); // add r4, r4, c24
    r4 = wmask(r4, min(r4, c(13u).zzzz), vec4<bool>(true, true, true, false)); // min r4.xyz, r4, c13.z
    r4 = wmask(r4, max(r4, c(13u).xxxx), vec4<bool>(true, true, true, false)); // max r4.xyz, r4, c13.x
    r11 = v5.wwww * c(21u); // mul r11, v5.w, c21
    r11 = max(r11, c(13u).xxxx); // max r11, r11, c13.x
    r11 = min(r11, c(13u).zzzz); // min r11, r11, c13.z
    r2 = r2 * r11; // mul r2, r2, r11
    r2 = r2 * c(6u); // mul r2, r2, c6
    r8 = wmask(r8, vec4f(dot(r2, c(13u).zzzz)), vec4<bool>(true, false, false, false)); // dp4 r8.x, r2, c13.z
    r8 = wmask(r8, r8.xxxx * r4.zzzz, vec4<bool>(false, true, false, false)); // mul r8.y, r8.x, r4.z
    r8 = wmask(r8, r8.yyyy + c(22u).wwww, vec4<bool>(false, false, true, false)); // add r8.z, r8.y, c22.w
    r6 = wmask(r6, max(r6.zzzz, r8.zzzz), vec4<bool>(false, false, true, false)); // max r6.z, r6.z, r8.z
    r1 = r1 * r11; // mul r1, r1, r11
    r7 = wmask(r7, vec4f(dot(r1, c(30u))), vec4<bool>(true, false, false, false)); // dp4 r7.x, r1, c30
    r7 = wmask(r7, vec4f(dot(r1, c(31u))), vec4<bool>(false, true, false, false)); // dp4 r7.y, r1, c31
    r6 = wmask(r6, r6.xyyy + r7.xyyy, vec4<bool>(true, true, false, false)); // add r6.xy, r6.xyyy, r7.xyyy
    r6 = wmask(r6, r6.zzzz + c(25u).xxxx, vec4<bool>(false, false, true, false)); // add r6.z, r6.z, c25.x
    r9 = m4x4(r6, 0u); // m4x4 r9, r6, c0
    r10 = wmask(r10, r9.wwww + c(29u).xxxx, vec4<bool>(true, false, false, false)); // add r10.x, r9.w, c29.x
    o.oFog = (r10.xxxx * c(29u).yyyy).x; // mul oFog, r10.x, c29.y
    o.oPos = r9; // mov oPos, r9
    o.oD0 = v5.yyyx * c(26u); // mul oD0, v5.yyyx, c26
    r11 = wmask(r11, c(13u).zzzz, vec4<bool>(false, false, true, true)); // mov r11.zw, c13.z
    r11 = wmask(r11, vec4f(dot(v7, c(14u))), vec4<bool>(true, false, false, false)); // dp4 r11.x, v7, c14
    r11 = wmask(r11, vec4f(dot(v7, c(15u))), vec4<bool>(false, true, false, false)); // dp4 r11.y, v7, c15
    o.oT0 = r11; // mov oT0, r11
    return o;
}

@vertex
fn vs_WaveDec1Lay_7(in: VertexIn) -> VSOut {
    return vsFinish(vs_WaveDec1Lay_7_run(in));
}

// ps_CbaseAbase: plSurface/ps_CbaseAbase.h, 8 tokens
fn ps_CbaseAbase_run(in: VSOut) -> vec4f {
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
fn ps_CbaseAbase(in: VSOut) -> @location(0) vec4f {
    return psFinish(ps_CbaseAbase_run(in), in.fog);
}
