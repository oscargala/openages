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

// The WaveSet7 program family (pfMetalPipeline/ShaderSrc/WaveSet7.metal names it),
// translated from the D3D9 bytecode by pfWGPUPipeline/ShaderSrc/gen/d3d9_to_wgsl.py. Do not edit:
// regenerate. Each statement is one instruction token, the instruction in its comment.
// ShaderPrelude.wgsl precedes this text in the module (plWGPUShader.cpp).

// vs_WaveFixedFin7: plSurface/vs_WaveFixedFin7.h, 469 tokens
fn vs_WaveFixedFin7_run(in: VertexIn) -> VSRegs {
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
    r6 = wmask(r6, vec4f(m4x3(v0, 21u), 0.0), vec4<bool>(true, true, true, false)); // m4x3 r6.xyz, v0, c21
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
    r4 = c(25u) + (-r6.zzzz); // add r4, c25, -r6.z
    r4 = r4 * c(26u); // mul r4, r4, c26
    r4 = r4 + c(27u); // add r4, r4, c27
    r4 = wmask(r4, min(r4, c(16u).zzzz), vec4<bool>(true, true, true, false)); // min r4.xyz, r4, c16.z
    r4 = wmask(r4, max(r4, c(16u).xxxx), vec4<bool>(true, true, true, false)); // max r4.xyz, r4, c16.x
    r11 = v5.wwww * c(24u); // mul r11, v5.w, c24
    r11 = max(r11, c(16u).xxxx); // max r11, r11, c16.x
    r11 = min(r11, c(16u).zzzz); // min r11, r11, c16.z
    r2 = r2 * r11; // mul r2, r2, r11
    r5 = r2 * c(7u); // mul r5, r2, c7
    r8 = wmask(r8, vec4f(dot(r5, c(16u).zzzz)), vec4<bool>(true, false, false, false)); // dp4 r8.x, r5, c16.z
    r8 = wmask(r8, r8.xxxx * r4.zzzz, vec4<bool>(false, true, false, false)); // mul r8.y, r8.x, r4.z
    r8 = wmask(r8, r8.yyyy + c(25u).wwww, vec4<bool>(false, false, true, false)); // add r8.z, r8.y, c25.w
    r6 = wmask(r6, max(r6.zzzz, r8.zzzz), vec4<bool>(false, false, true, false)); // max r6.z, r6.z, r8.z
    r7 = r1 * c(7u); // mul r7, r1, c7
    r7 = r7 * r11; // mul r7, r7, r11
    r10 = wmask(r10, vec4f(dot(r7, c(29u))), vec4<bool>(true, false, false, false)); // dp4 r10.x, r7, c29
    r6 = wmask(r6, r6.xxxx + r10.xxxx, vec4<bool>(true, false, false, false)); // add r6.x, r6.x, r10.x
    r10 = wmask(r10, vec4f(dot(r7, c(30u))), vec4<bool>(true, false, false, false)); // dp4 r10.x, r7, c30
    r6 = wmask(r6, r6.yyyy + r10.xxxx, vec4<bool>(false, true, false, false)); // add r6.y, r6.y, r10.x
    r1 = wmask(r1, vec4f(dot(r5, (-c(34u)))), vec4<bool>(true, false, false, false)); // dp4 r1.x, r5, -c34
    r2 = wmask(r2, vec4f(dot(r5, (-c(35u)))), vec4<bool>(true, false, false, false)); // dp4 r2.x, r5, -c35
    r3 = wmask(r3, vec4f(dot(r7, c(31u))), vec4<bool>(true, false, false, false)); // dp4 r3.x, r7, c31
    r1 = wmask(r1, r1.xxxx + c(16u).zzzz, vec4<bool>(true, false, false, false)); // add r1.x, r1.x, c16.z
    r1 = wmask(r1, vec4f(dot(r5, (-c(35u)))), vec4<bool>(false, true, false, false)); // dp4 r1.y, r5, -c35
    r2 = wmask(r2, vec4f(dot(r5, (-c(36u)))), vec4<bool>(false, true, false, false)); // dp4 r2.y, r5, -c36
    r3 = wmask(r3, vec4f(dot(r7, c(32u))), vec4<bool>(false, true, false, false)); // dp4 r3.y, r7, c32
    r2 = wmask(r2, r2.yyyy + c(16u).zzzz, vec4<bool>(false, true, false, false)); // add r2.y, r2.y, c16.z
    r1 = wmask(r1, vec4f(dot(r7, (-c(31u)))), vec4<bool>(false, false, true, false)); // dp4 r1.z, r7, -c31
    r2 = wmask(r2, vec4f(dot(r7, (-c(32u)))), vec4<bool>(false, false, true, false)); // dp4 r2.z, r7, -c32
    r3 = wmask(r3, vec4f(dot(r5, (-c(33u)))), vec4<bool>(false, false, true, false)); // dp4 r3.z, r5, -c33
    r3 = wmask(r3, r3.zzzz + c(16u).zzzz, vec4<bool>(false, false, true, false)); // add r3.z, r3.z, c16.z
    r5 = r6 + (-c(17u)); // add r5, r6, -c17
    r10 = wmask(r10, vec4f(dot((r5).xyz, (r5).xyz)), vec4<bool>(true, false, false, false)); // dp3 r10.x, r5, r5
    r10 = wmask(r10, vec4f(rsq((r10.xxxx).w)), vec4<bool>(true, false, false, false)); // rsq r10.x, r10.x
    r5 = r5 * r10.xxxx; // mul r5, r5, r10.x
    r5 = wmask(r5, vec4f(rcp((r10.xxxx).w)), vec4<bool>(false, false, false, true)); // rcp r5.w, r10.x
    r5 = wmask(r5, r5.wwww + c(11u).xxxx, vec4<bool>(false, false, false, true)); // add r5.w, r5.w, c11.x
    r5 = wmask(r5, r5.wwww * c(11u).yyyy, vec4<bool>(false, false, false, true)); // mul r5.w, r5.w, c11.y
    r5 = wmask(r5, min(r5.wwww, c(16u).zzzz), vec4<bool>(false, false, false, true)); // min r5.w, r5.w, c16.z
    r5 = wmask(r5, max(r5.wwww, c(16u).xxxx), vec4<bool>(false, false, false, true)); // max r5.w, r5.w, c16.x
    r5 = wmask(r5, r5.wwww * r5.wwww, vec4<bool>(false, false, false, true)); // mul r5.w, r5.w, r5.w
    r5 = wmask(r5, r5.wwww * c(11u).zzzz, vec4<bool>(false, false, false, true)); // mul r5.w, r5.w, c11.z
    r0 = r5; // mov r0, r5
    r10 = wmask(r10, vec4f(dot((r0).xyz, (c(19u)).xyz)), vec4<bool>(true, false, false, false)); // dp3 r10.x, r0, c19
    r10 = wmask(r10, r10.xxxx * r10.xxxx + (-c(19u).wwww), vec4<bool>(false, true, false, false)); // mad r10.y, r10.x, r10.x, -c19.w
    r9 = wmask(r9, vec4f(rsq((r10.yyyy).w)), vec4<bool>(true, false, false, false)); // rsq r9.x, r10.y
    r10 = wmask(r10, r10.yyyy * r9.xxxx + r10.xxxx, vec4<bool>(false, false, true, false)); // mad r10.z, r10.y, r9.x, r10.x
    r0 = wmask(r0, r0 * r10.zzzz + (-c(19u).xyzz), vec4<bool>(true, true, true, false)); // mad r0.xyz, r0, r10.z, -c19.xyzz
    r10 = wmask(r10, vec4f(dot((r0).xyz, (r0).xyz)), vec4<bool>(true, false, false, false)); // dp3 r10.x, r0, r0
    r9 = wmask(r9, vec4f(rsq((r10.xxxx).w)), vec4<bool>(true, false, false, false)); // rsq r9.x, r10.x
    r0 = wmask(r0, r0.xyzz * r9.xxxx, vec4<bool>(true, true, true, false)); // mul r0.xyz, r0.xyzz, r9.x
    r1 = wmask(r1, (-r0.xxxx), vec4<bool>(false, false, false, true)); // mov r1.w, -r0.x
    r2 = wmask(r2, (-r0.yyyy), vec4<bool>(false, false, false, true)); // mov r2.w, -r0.y
    r3 = wmask(r3, (-r0.zzzz), vec4<bool>(false, false, false, true)); // mov r3.w, -r0.z
    r0 = wmask(r0, c(16u).zzxz, vec4<bool>(false, false, true, true)); // mov r0.zw, c16.zzxz
    r0 = wmask(r0, vec4f(dot((r1).xyz, (r1).xyz)), vec4<bool>(true, false, false, false)); // dp3 r0.x, r1, r1
    r0 = wmask(r0, vec4f(rsq((r0.xxxx).w)), vec4<bool>(true, true, false, false)); // rsq r0.xy, r0.x
    r0 = wmask(r0, r0.xxxx * r5.wwww, vec4<bool>(true, false, false, false)); // mul r0.x, r0.x, r5.w
    o.oT1 = r1 * r0.xxyw; // mul oT1, r1, r0.xxyw
    r11 = wmask(r11, r1.zzzz * r0.yyyy, vec4<bool>(true, false, false, false)); // mul r11.x, r1.z, r0.y
    r0 = wmask(r0, vec4f(dot((r2).xyz, (r2).xyz)), vec4<bool>(true, false, false, false)); // dp3 r0.x, r2, r2
    r0 = wmask(r0, vec4f(rsq((r0.xxxx).w)), vec4<bool>(true, true, false, false)); // rsq r0.xy, r0.x
    r0 = wmask(r0, r0.xxxx * r5.wwww, vec4<bool>(true, false, false, false)); // mul r0.x, r0.x, r5.w
    o.oT3 = r2 * r0.xxyw; // mul oT3, r2, r0.xxyw
    r11 = wmask(r11, r2.zzzz * r0.yyyy, vec4<bool>(false, true, false, false)); // mul r11.y, r2.z, r0.y
    r0 = wmask(r0, vec4f(dot((r3).xyz, (r3).xyz)), vec4<bool>(true, false, false, false)); // dp3 r0.x, r3, r3
    r0 = wmask(r0, vec4f(rsq((r0.xxxx).w)), vec4<bool>(true, true, false, false)); // rsq r0.xy, r0.x
    r0 = wmask(r0, r0.xxxx * r5.wwww, vec4<bool>(true, false, false, false)); // mul r0.x, r0.x, r5.w
    o.oT2 = r3 * r0.xxyw; // mul oT2, r3, r0.xxyw
    r11 = wmask(r11, r3.zzzz * r0.yyyy, vec4<bool>(false, false, true, false)); // mul r11.z, r3.z, r0.y
    r9 = m4x4(r6, 0u); // m4x4 r9, r6, c0
    r10 = wmask(r10, r9.wwww + c(28u).xxxx, vec4<bool>(true, false, false, false)); // add r10.x, r9.w, c28.x
    o.oFog = (r10.xxxx * c(28u).yyyy).x; // mul oFog, r10.x, c28.y
    o.oPos = r9; // mov oPos, r9
    r0 = wmask(r0, v0.xxxx * c(10u).xxxx, vec4<bool>(true, false, false, false)); // mul r0.x, v0.x, c10.x
    r0 = wmask(r0, v0.yyyy * c(10u).xxxx, vec4<bool>(false, true, false, false)); // mul r0.y, v0.y, c10.x
    o.oT0 = r0; // mov oT0, r0
    r1 = wmask(r1, vec4f(dot(((-r5)).xyz, (r11).xyz)), vec4<bool>(true, false, false, false)); // dp3 r1.x, -r5, r11
    r1 = wmask(r1, r1.xxxx * v5.zzzz, vec4<bool>(true, false, false, false)); // mul r1.x, r1.x, v5.z
    r1 = c(16u).zzzz + (-r1.xxxx); // add r1, c16.z, -r1.x
    r1 = wmask(r1, r1.wwww + c(16u).zzzz, vec4<bool>(false, false, false, true)); // add r1.w, r1.w, c16.z
    r1 = wmask(r1, r1.wwww * c(16u).yyyy, vec4<bool>(false, false, false, true)); // mul r1.w, r1.w, c16.y
    r1 = r1 * r4.yyyx; // mul r1, r1, r4.yyyx
    r1 = wmask(r1, r1.wwww * v5.xxxx, vec4<bool>(false, false, false, true)); // mul r1.w, r1.w, v5.x
    r1 = wmask(r1, r1.wwww * c(4u).wwww, vec4<bool>(false, false, false, true)); // mul r1.w, r1.w, c4.w
    o.oD0 = r1 * c(20u); // mul oD0, r1, c20
    o.oD1 = c(4u); // mov oD1, c4
    return o;
}

@vertex
fn vs_WaveFixedFin7(in: VertexIn) -> VSOut {
    return vsFinish(vs_WaveFixedFin7_run(in));
}

// ps_WaveFixed: plSurface/ps_WaveFixed.h, 27 tokens
fn ps_WaveFixed_run(in: VSOut) -> vec4f {
    let v0 = in.d0;
    let v1 = in.d1;
    var r0 = vec4f(0.0);
    var r1 = vec4f(0.0);
    var t0 = vec4f(0.0);
    var t1 = vec4f(0.0);
    var t2 = vec4f(0.0);
    var t3 = vec4f(0.0);
    let def0 = vec4f(1.0, 0.0, 0.0, 1.0); // def c0, 1, 0, 0, 1
    t0 = texStage(0u, in.t0); // tex t0
    // texm3x3pad t1, t0_bx2: row 1 of the texm3x3 product (folded into texm3x3vspec)
    // texm3x3pad t2, t0_bx2: row 2 of the texm3x3 product (folded into texm3x3vspec)
    t3 = texm3x3vspec(3u, in.t1, in.t2, in.t3, bx2(t0)); // texm3x3vspec t3, t0_bx2
    r0 = wmask(r0, pc(t3 * v0 + v1), vec4<bool>(true, true, true, false)); // mad r0.rgb, t3, v0, v1
    r0 = wmask(r0, pc(v0), vec4<bool>(false, false, false, true)); // mov r0.a, v0
    return r0;
}

@fragment
fn ps_WaveFixed(in: VSOut) -> @location(0) vec4f {
    return psFinish(ps_WaveFixed_run(in), in.fog);
}
