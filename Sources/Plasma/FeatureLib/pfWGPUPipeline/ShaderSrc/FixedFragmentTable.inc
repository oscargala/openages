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

// The pieces of the specialised fixed-function fragment programs (plWGPUFixedFragment), cut from
// FixedPipelineShaders.wgsl by pfWGPUPipeline/ShaderSrc/gen/fixed_fragment_table.py. Do not edit: regenerate.
// The client uses the table only with the source it was cut from (kSourceFnv, kSourceSize); otherwise every
// draw keeps the uber shader.

constexpr uint64_t kSourceFnv = 0x9bfc81b8dd45f88fULL;
constexpr size_t   kSourceSize = 25845;

// The functions a program replaces (fixedFragment, sampleStage, stageOp, alphaTest):
// [begin, end) in the source, last first.
constexpr size_t kCut[4][2] = {
    { 23624, 25844 },
    { 23140, 23622 },
    { 20731, 23138 },
    { 18498, 19829 },
};

// stageOp's branch per D3DTOP value as a helper function, and its name (0: an unlisted op, Arg1).
const char* const kOpName[25] = {
    "op_default",
    nullptr,
    "op_selectarg1",
    "op_selectarg2",
    "op_modulate",
    "op_modulate2x",
    "op_modulate4x",
    "op_add",
    "op_addsigned",
    "op_addsigned2x",
    "op_subtract",
    "op_addsmooth",
    "op_blenddiffusealpha",
    "op_blendtexturealpha",
    "op_blendfactoralpha",
    "op_blendtexturealphapm",
    "op_blendcurrentalpha",
    nullptr,
    "op_modulatealpha_addcolor",
    "op_modulatecolor_addalpha",
    "op_modulateinvalpha_addcolor",
    "op_modulateinvcolor_addalpha",
    nullptr,
    nullptr,
    "op_dotproduct3",
};
const char* const kOpFn[25] = {
    "fn op_default(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1;\n}\n",
    nullptr,
    "fn op_selectarg1(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1;\n}\n",
    "fn op_selectarg2(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a2;\n}\n",
    "fn op_modulate(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * a2;\n}\n",
    "fn op_modulate2x(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * a2 * 2.0;\n}\n",
    "fn op_modulate4x(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * a2 * 4.0;\n}\n",
    "fn op_add(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 + a2;\n}\n",
    "fn op_addsigned(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return fma(vec4f(1.0), a1 + a2, vec4f(-0.5));\n}\n",
    "fn op_addsigned2x(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return fma(a1 + a2, vec4f(2.0), vec4f(-1.0));\n}\n",
    "fn op_subtract(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 - a2;\n}\n",
    "fn op_addsmooth(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 + a2 - a1 * a2;\n}\n",
    "fn op_blenddiffusealpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return vec4f(fma(a2.rgb, vec3f(1.0 - diffuse.a), a1.rgb * diffuse.a), fma(a1.a - a2.a, diffuse.a, a2.a));\n}\n",
    "fn op_blendtexturealpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * tex.a + a2 * (1.0 - tex.a);\n}\n",
    "fn op_blendfactoralpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * u.textureFactor.a + a2 * (1.0 - u.textureFactor.a);\n}\n",
    "fn op_blendtexturealphapm(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 + a2 * (1.0 - tex.a);\n}\n",
    "fn op_blendcurrentalpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return vec4f(fma(a2.rgb, vec3f(1.0 - current.a), a1.rgb * current.a), fma(a1.a - a2.a, current.a, a2.a));\n}\n",
    nullptr,
    "fn op_modulatealpha_addcolor(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 + a1.a * a2;\n}\n",
    "fn op_modulatecolor_addalpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return a1 * a2 + vec4f(a1.a);\n}\n",
    "fn op_modulateinvalpha_addcolor(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return (1.0 - a1.a) * a2 + a1;\n}\n",
    "fn op_modulateinvcolor_addalpha(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    return (vec4f(1.0) - a1) * a2 + vec4f(a1.a);\n}\n",
    nullptr,
    nullptr,
    "fn op_dotproduct3(a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {\n    let d = (a1.rgb - vec3f(0.5)) * (a2.rgb - vec3f(0.5));\n    return vec4f(4.0 * (d.r + d.g + d.b));\n}\n",
};

// fixedFragment's stageArg calls per stage: COLORARG1, COLORARG2, ALPHAARG1, ALPHAARG2.
const char* const kArgC1[8] = {
    "stageArg(u.stages[0u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[1u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[2u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[3u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[4u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[5u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[6u].ops.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[7u].ops.y, diffuse, current, tex, specular)",
};
const char* const kArgC2[8] = {
    "stageArg(u.stages[0u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[1u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[2u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[3u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[4u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[5u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[6u].ops.z, diffuse, current, tex, specular)",
    "stageArg(u.stages[7u].ops.z, diffuse, current, tex, specular)",
};
const char* const kArgA1[8] = {
    "stageArg(u.stages[0u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[1u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[2u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[3u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[4u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[5u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[6u].ops2.x, diffuse, current, tex, specular)",
    "stageArg(u.stages[7u].ops2.x, diffuse, current, tex, specular)",
};
const char* const kArgA2[8] = {
    "stageArg(u.stages[0u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[1u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[2u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[3u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[4u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[5u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[6u].ops2.y, diffuse, current, tex, specular)",
    "stageArg(u.stages[7u].ops2.y, diffuse, current, tex, specular)",
};

// sampleStage per stage: the coordinate, the bias, the 2D sample and the cube sample.
const char* const kCoord[8] = {
    "sampleCoord(0u, in.t0)",
    "sampleCoord(1u, in.t1)",
    "sampleCoord(2u, in.t2)",
    "sampleCoord(3u, in.t3)",
    "sampleCoord(4u, in.t4)",
    "sampleCoord(5u, in.t5)",
    "sampleCoord(6u, in.t6)",
    "sampleCoord(7u, in.t7)",
};
const char* const kBias[8] = {
    "u.stages[0u].bias.x",
    "u.stages[1u].bias.x",
    "u.stages[2u].bias.x",
    "u.stages[3u].bias.x",
    "u.stages[4u].bias.x",
    "u.stages[5u].bias.x",
    "u.stages[6u].bias.x",
    "u.stages[7u].bias.x",
};
const char* const kFlat[8] = {
    "textureSampleBias(tex0, samp0, c.xy, bias)",
    "textureSampleBias(tex1, samp1, c.xy, bias)",
    "textureSampleBias(tex2, samp2, c.xy, bias)",
    "textureSampleBias(tex3, samp3, c.xy, bias)",
    "textureSampleBias(tex4, samp4, c.xy, bias)",
    "textureSampleBias(tex5, samp5, c.xy, bias)",
    "textureSampleBias(tex6, samp6, c.xy, bias)",
    "textureSampleBias(tex7, samp7, c.xy, bias)",
};
const char* const kCube[8] = {
    "textureSampleBias(cube0, samp0, c.xyz, bias)",
    "textureSampleBias(cube1, samp1, c.xyz, bias)",
    "textureSampleBias(cube2, samp2, c.xyz, bias)",
    "textureSampleBias(cube3, samp3, c.xyz, bias)",
    "textureSampleBias(cube4, samp4, c.xyz, bias)",
    "textureSampleBias(cube5, samp5, c.xyz, bias)",
    "textureSampleBias(cube6, samp6, c.xyz, bias)",
    "textureSampleBias(cube7, samp7, c.xyz, bias)",
};
// A stage that does not read its texture: fixedFragment's sampleStage call per stage, and sampleStage itself.
const char* const kSampleStage[8] = {
    "sampleStage(0u, in.t0)",
    "sampleStage(1u, in.t1)",
    "sampleStage(2u, in.t2)",
    "sampleStage(3u, in.t3)",
    "sampleStage(4u, in.t4)",
    "sampleStage(5u, in.t5)",
    "sampleStage(6u, in.t6)",
    "sampleStage(7u, in.t7)",
};
const char* const kSampleStageFn = "fn sampleStage(s: u32, t: vec4f) -> vec4f {\n    // A stage without a texture reads opaque white (counted on the CPU side).\n    let c = sampleCoord(s, t);\n    let cube = u.stages[s].flags.x != 0u;\n    let bias = u.stages[s].bias.x;\n    if (s == 0u) { if (cube) { return textureSampleBias(cube0, samp0, c.xyz, bias); } return textureSampleBias(tex0, samp0, c.xy, bias); }\n    if (s == 1u) { if (cube) { return textureSampleBias(cube1, samp1, c.xyz, bias); } return textureSampleBias(tex1, samp1, c.xy, bias); }\n    if (s == 2u) { if (cube) { return textureSampleBias(cube2, samp2, c.xyz, bias); } return textureSampleBias(tex2, samp2, c.xy, bias); }\n    if (s == 3u) { if (cube) { return textureSampleBias(cube3, samp3, c.xyz, bias); } return textureSampleBias(tex3, samp3, c.xy, bias); }\n    if (s == 4u) { if (cube) { return textureSampleBias(cube4, samp4, c.xyz, bias); } return textureSampleBias(tex4, samp4, c.xy, bias); }\n    if (s == 5u) { if (cube) { return textureSampleBias(cube5, samp5, c.xyz, bias); } return textureSampleBias(tex5, samp5, c.xy, bias); }\n    if (s == 6u) { if (cube) { return textureSampleBias(cube6, samp6, c.xyz, bias); } return textureSampleBias(tex6, samp6, c.xy, bias); }\n    if (cube) { return textureSampleBias(cube7, samp7, c.xyz, bias); }\n    return textureSampleBias(tex7, samp7, c.xy, bias);\n}\n";

// fogFactor's branch per table fog mode as fog_factor (0: no mode, 1.0).
const char* const kFogFn[4] = {
    "fn fog_factor(d: f32) -> f32 {\n    return 1.0;\n}\n",
    "fn fog_factor(d: f32) -> f32 {\n    return clamp(1.0 / exp(d * u.fogParams.z), 0.0, 1.0);\n}\n",
    "fn fog_factor(d: f32) -> f32 {\n    let dd = d * u.fogParams.z;\n    return clamp(1.0 / exp(dd * dd), 0.0, 1.0);\n}\n",
    "fn fog_factor(d: f32) -> f32 {\n    return clamp((u.fogParams.y - d) / (u.fogParams.y - u.fogParams.x), 0.0, 1.0);\n}\n",
};

// alphaTest: the quantised alpha of current.a, and the comparison per D3DCMP function (1 .. 7).
const char* const kAlpha8 = "u32(round(clamp(current.a, 0.0, 1.0) * 255.0))";
const char* const kTest[8] = {
    nullptr,
    "false",
    "a8 < u.fog.w",
    "a8 == u.fog.w",
    "a8 <= u.fog.w",
    "a8 > u.fog.w",
    "a8 != u.fog.w",
    "a8 >= u.fog.w",
};
// With no alpha function (0): alphaTest itself, and fixedFragment's alpha-test statement.
const char* const kAlphaTestFn = "fn alphaTest(func: u32, a: f32, ref_: u32) -> bool {\n    let a8 = u32(round(clamp(a, 0.0, 1.0) * 255.0));\n    if (func == CMP_NEVER) { return false; }\n    if (func == CMP_LESS) { return a8 < ref_; }\n    if (func == CMP_EQUAL) { return a8 == ref_; }\n    if (func == CMP_LESSEQUAL) { return a8 <= ref_; }\n    if (func == CMP_GREATER) { return a8 > ref_; }\n    if (func == CMP_NOTEQUAL) { return a8 != ref_; }\n    if (func == CMP_GREATEREQUAL) { return a8 >= ref_; }\n    return true;\n}\n";
const char* const kAlphaTestStmt = "    if (u.fog.z != 0u && !alphaTest(u.fog.z, current.a, u.fog.w)) {\n        discard;\n    }";

// fixedFragment's statements, which every program's entry repeats.
const char* const kClampCurrent = "clamp(vec4f(color.rgb, alpha), vec4f(0.0), vec4f(1.0))";
const char* const kFogMix = "fma(vec3f(1.0 - f), u.fogColor.rgb, f * rgb)";
const char* const kVertexFog = "clamp(in.fog.y, 0.0, 1.0)";
const char* const kVertexFogMix = "fma(vec3f(1.0 - f), u.fogColor.rgb, f * rgb)";
const char* const kReturn = "vec4f(rgb, current.a)";
const char* const kSpecStmt = "    if (u.lighting.x != 0u) {\n        rgb = clamp(rgb + specular.rgb, vec3f(0.0), vec3f(1.0));\n    }";
