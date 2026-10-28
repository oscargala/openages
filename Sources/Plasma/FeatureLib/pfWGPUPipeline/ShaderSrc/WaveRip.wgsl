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

// The WaveRip program family (pfMetalPipeline/ShaderSrc/WaveRip.metal names it),
// translated from the D3D9 bytecode by pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py. Do not edit:
// regenerate. Each statement is one instruction token, the instruction in its comment.
// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).

// vs_WaveRip7: plSurface/vs_WaveRip7.h, 302 tokens
fn vs_WaveRip7_run(in: VertexIn) -> VSRegs {
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
    r6 = wmask(r6, vec4f(m4x3(v0, 25u), 0.0), vec4<bool>(true, true, true, false)); // m4x3 r6.xyz, v0, c25
    r6 = wmask(r6, c(16u).zzzz, vec4<bool>(false, false, false, true)); // mov r6.w, c16.z
    r0 = c(8u) * r6.xxxx; // mul r0, c8, r6.x
    r0 = c(9u) * r6.yyyy + r0; // mad r0, c9, r6.y, r0
    r0 = r0 * c(5u); // mul r0, r0, c5
    r0 = r0 + c(6u); // add r0, r0, c6
    r4 = vec4f(rcp((c(15u).wwww).w)); // rcp r4, c15.w
    r0 = r0 + c(15u).zzzz; // add r0, r0, c15.z
    r0 = r0 * r4; // mul r0, r0, r4
    r1 = wmask(r1, expp((r0.xxxx).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.x
    r1 = wmask(r1, r1.yyyy, vec4<bool>(true, false, false, false)); // mov r1.x, r1.y
    r1 = wmask(r1, expp((r0.zzzz).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.z
    r1 = wmask(r1, r1.yyyy, vec4<bool>(false, false, true, false)); // mov r1.z, r1.y
    r1 = wmask(r1, expp((r0.wwww).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.w
    r1 = wmask(r1, r1.yyyy, vec4<bool>(false, false, false, true)); // mov r1.w, r1.y
    r1 = wmask(r1, expp((r0.yyyy).w), vec4<bool>(false, true, false, false)); // expp r1.y, r0.y
    r0 = r1 * c(15u).wwww; // mul r0, r1, c15.w
    r0 = r0 + (-c(15u).zzzz); // add r0, r0, -c15.z
    r1 = r0 * r0; // mul r1, r0, r0
    r2 = r1 * r0; // mul r2, r1, r0
    r3 = r1 * r1; // mul r3, r1, r1
    r4 = r1 * r2; // mul r4, r1, r2
    r5 = r2 * r3; // mul r5, r2, r3
    r1 = r1 * c(14u).yyyy; // mul r1, r1, c14.y
    r2 = r2 * c(13u).yyyy + r0; // mad r2, r2, c13.y, r0
    r1 = r1 + c(14u).xxxx; // add r1, r1, c14.x
    r2 = r4 * c(13u).zzzz + r2; // mad r2, r4, c13.z, r2
    r1 = r3 * c(14u).zzzz + r1; // mad r1, r3, c14.z, r1
    r4 = r4 * r0; // mul r4, r4, r0
    r2 = r5 * c(13u).wwww + r2; // mad r2, r5, c13.w, r2
    r1 = r4 * c(14u).wwww + r1; // mad r1, r4, c14.w, r1
    r4 = c(30u) + (-r6.zzzz); // add r4, c30, -r6.z
    r4 = r4 * c(31u); // mul r4, r4, c31
    r4 = r4 + c(32u); // add r4, r4, c32
    r4 = wmask(r4, min(r4, c(16u).zzzz), vec4<bool>(true, true, true, false)); // min r4.xyz, r4, c16.z
    r4 = wmask(r4, max(r4, c(16u).xxxx), vec4<bool>(true, true, true, false)); // max r4.xyz, r4, c16.x
    r11 = v5.wwww * c(29u); // mul r11, v5.w, c29
    r11 = max(r11, c(16u).xxxx); // max r11, r11, c16.x
    r11 = min(r11, c(16u).zzzz); // min r11, r11, c16.z
    r2 = r2 * r11; // mul r2, r2, r11
    r2 = r2 * c(7u); // mul r2, r2, c7
    r8 = wmask(r8, vec4f(dot(r2, c(16u).zzzz)), vec4<bool>(true, false, false, false)); // dp4 r8.x, r2, c16.z
    r8 = wmask(r8, r8.xxxx * r4.zzzz, vec4<bool>(false, true, false, false)); // mul r8.y, r8.x, r4.z
    r8 = wmask(r8, r8.yyyy + c(30u).wwww, vec4<bool>(false, false, true, false)); // add r8.z, r8.y, c30.w
    r6 = wmask(r6, max(r6.zzzz, r8.zzzz), vec4<bool>(false, false, true, false)); // max r6.z, r6.z, r8.z
    r1 = r1 * r11; // mul r1, r1, r11
    r7 = wmask(r7, vec4f(dot(r1, c(10u))), vec4<bool>(true, false, false, false)); // dp4 r7.x, r1, c10
    r7 = wmask(r7, vec4f(dot(r1, c(11u))), vec4<bool>(false, true, false, false)); // dp4 r7.y, r1, c11
    r6 = wmask(r6, r6.xyyy + r7.xyyy, vec4<bool>(true, true, false, false)); // add r6.xy, r6.xyyy, r7.xyyy
    r6 = wmask(r6, r6.zzzz + c(35u).zzzz, vec4<bool>(false, false, true, false)); // add r6.z, r6.z, c35.z
    r9 = m4x4(r6, 0u); // m4x4 r9, r6, c0
    r10 = wmask(r10, r9.wwww + c(4u).xxxx, vec4<bool>(true, false, false, false)); // add r10.x, r9.w, c4.x
    o.oFog = (r10.xxxx * c(4u).yyyy).x; // mul oFog, r10.x, c4.y
    o.oPos = r9; // mov oPos, r9
    r1 = c(16u).xxxz; // mov r1, c16.xxxz
    r1 = wmask(r1, c(34u).yyyy + (-v7.zzzz), vec4<bool>(true, false, false, false)); // add r1.x, c34.y, -v7.z
    r1 = wmask(r1, r1.xxxx * c(35u).yyyy, vec4<bool>(false, true, false, false)); // mul r1.y, r1.x, c35.y
    r1 = wmask(r1, min(r1.yyyy, c(16u).zzzz), vec4<bool>(false, true, false, false)); // min r1.y, r1.y, c16.z
    r1 = wmask(r1, c(34u).zzzz + (-r1.xxxx), vec4<bool>(false, false, true, false)); // add r1.z, c34.z, -r1.x
    r1 = wmask(r1, r1.zzzz * c(34u).wwww, vec4<bool>(false, false, true, false)); // mul r1.z, r1.z, c34.w
    r1 = wmask(r1, min(r1.zzzz, c(16u).zzzz), vec4<bool>(false, false, true, false)); // min r1.z, r1.z, c16.z
    r1 = wmask(r1, max(r1.zzzz, c(16u).xxxx), vec4<bool>(false, false, true, false)); // max r1.z, r1.z, c16.x
    r1 = wmask(r1, r1.yyyy * r1.zzzz, vec4<bool>(false, true, false, false)); // mul r1.y, r1.y, r1.z
    r0 = wmask(r0, r4.yyyy * c(34u).xxxx, vec4<bool>(false, true, false, false)); // mul r0.y, r4.y, c34.x
    o.oD0 = r0.yyyy * r1.yyyw; // mul oD0, r0.y, r1.yyyw
    r2 = c(16u).xxxz; // mov r2, c16.xxxz
    r2 = wmask(r2, r1.xxxx * c(33u).ywww, vec4<bool>(true, true, false, false)); // mul r2.xy, r1.x, c33.ywww
    r2 = wmask(r2, r2.xyyy + c(16u).zzzz, vec4<bool>(true, true, false, false)); // add r2.xy, r2.xyyy, c16.z
    r2 = wmask(r2, vec4f(rcp((r2.xxxx).w)), vec4<bool>(true, false, false, false)); // rcp r2.x, r2.x
    r2 = wmask(r2, vec4f(rcp((r2.yyyy).w)), vec4<bool>(false, true, false, false)); // rcp r2.y, r2.y
    r2 = wmask(r2, r2.xyyy * c(33u).xzzz, vec4<bool>(true, true, false, false)); // mul r2.xy, r2.xyyy, c33.xzzz
    r1 = wmask(r1, v7.xyyy + (-c(16u).yyyy), vec4<bool>(true, true, false, false)); // add r1.xy, v7.xyyy, -c16.y
    r1 = wmask(r1, r1.xyyy * r2.xyyy, vec4<bool>(true, true, false, false)); // mul r1.xy, r1.xyyy, r2.xyyy
    r1 = wmask(r1, r1.xyyy + c(16u).yyyy, vec4<bool>(true, true, false, false)); // add r1.xy, r1.xyyy, c16.y
    o.oT0 = r1; // mov oT0, r1
    return o;
}

@vertex
fn vs_WaveRip7(in: VertexIn) -> VSOut {
    return vsFinish(vs_WaveRip7_run(in));
}

// ps_WaveRip: plSurface/ps_WaveRip.h, 8 tokens
fn ps_WaveRip_run(in: VSOut) -> vec4f {
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
fn ps_WaveRip(in: VSOut) -> @location(0) vec4f {
    return psFinish(ps_WaveRip_run(in), in.fog);
}
