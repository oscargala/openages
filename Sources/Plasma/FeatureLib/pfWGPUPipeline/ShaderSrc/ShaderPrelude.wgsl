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

// The Direct3D 9 vs_1_1 / ps_1_1 device that pfDXPipeline's shader programs run on, in WGSL.
// Every program family module (WaveSet7.wgsl, CompCosines.wgsl, BiasNormals.wgsl,
// Grass.wgsl, WaveDec1Lay_7.wgsl, WaveDecEnv.wgsl, WaveRip.wgsl, the names of
// pfMetalPipeline/ShaderSrc/*.metal) is this prelude followed by the module's programs
// (plWGPUShader.cpp concatenates them), so any vertex program pairs with any pixel program
// through the one inter-stage interface below, as on a D3D9 device. Each program runs once per
// vertex or pixel, as on that device.
//
// Structure from Metal (one module per family, the constants as one block per program,
// plMetalVertexShader::ISetConstants); behaviour from D3D9 and the program bytecode. D3D9
// runtime behaviour this file implements, with its DirectX 9 SDK documentation cite:
// - Vertex input by declaration usage ("D3DDECLUSAGE", "dcl_usage (vs)"): the stream's FVF
//   (IGetBufferD3DFormat) feeds POSITION to the register declared dcl_position, COLOR0 to
//   dcl_color, TEXCOORDn to dcl_texcoordn. A D3DCOLOR element arrives as (R, G, B, A)
//   ("D3DDECLTYPE": D3DDECLTYPE_D3DCOLOR "expanded to (R, G, B, A)"); FLOAT3 as (x, y, z, 1);
//   an element the stream lacks reads the zero stream as (0, 0, 0, 1) for FLOAT3 (WebGPU's
//   vertex fetch fills a missing w with 1, the same rule).
// - vs_1_1 output registers ("Output Registers (vs_1_1)"): oD0 and oD1 are clamped to [0, 1]
//   before interpolation; oFog is a scalar the device clamps to [0, 1] per vertex
//   and then interpolates ("Fog (Direct3D 9)", "Vertex Fog": with a vertex shader the shader
//   writes the fog factor; the D3DRS_FOGVERTEXMODE formula is not applied); oPos is the clip
//   position.
// - rcp and rsq ("rcp - vs", "rsq - vs"): 1 for an input of exactly 1, +infinity for 0, rsq
//   of |x|; scalar instructions read the replicated component.
// - expp ("expp - vs"): x = 2^floor(s), y = s - floor(s), z = 2^s, w = 1.
// - frc ("frc - vs"): s - floor(s).
// - ps_1_1 ("ps_1_X Registers", "Source Register Modifiers", "Instruction Modifiers"): every
//   arithmetic result clamped to [-MaxPixelShaderValue, MaxPixelShaderValue] = [-1, 1] and the
//   constants clamped to [-1, 1] at upload; _bias = x - 0.5, _bx2 =
//   2 (x - 0.5), 1-x complement; the output r0 saturated to [0, 1]. Precision is float32 (not
//   emulated).
// - tex tN ("tex - ps"): stage N's texture at texture coordinate set N with stage N's sampler
//   state (address, filter, D3DSAMP_MIPMAPLODBIAS); texm3x3pad/texm3x3vspec ("texm3x3vspec - ps"):
//   N = (T1.xyz . s, T2.xyz . s, T3.xyz . s), E = (T1.w, T2.w, T3.w), R = 2 (N.E)/(N.N) N - E,
//   the cube map at stage 3 sampled with R; the cube face from the major axis of R
//   as in "Cubic Environment Mapping", which the WebGPU cube sampling rule shares.
// - Fog with a pixel shader ("Fog (Direct3D 9)"): with D3DRS_FOGENABLE, the fog factor f blends
//   the pixel shader's output colour: f C + (1 - f) FOGCOLOR, alpha untouched.
// - Alpha test on the 8-bit output alpha against D3DRS_ALPHAREF.
// - Pixel centres on integer window coordinates for every draw ("Directly Mapping Texels to
//   Pixels"): the clip position is shifted half a pixel.

// Pipeline-overridable constant (part of the pipeline key).
override kFogEnable: bool = false;  // D3DRS_FOGENABLE at the draw

struct ShaderUniforms {
    vs: array<vec4f, 96>,       // vs_1_1 constant registers c0..c95 (persistent register file)
    ps: array<vec4f, 8>,        // ps_1_1 c0..c7 as the device holds them (clamped to [-1, 1])
    fogColor: vec4f,            // D3DRS_FOGCOLOR
    misc: vec4f,                // pixel-centre offset in clip units (1/W, -1/H), -, -
    state: vec4u,               // ALPHAFUNC (0: no test), ALPHAREF, -, -
    stageCube: vec4u,           // stages 0..3: the bound texture is a cube map
    stageBias: vec4f,           // stages 0..3: D3DSAMP_MIPMAPLODBIAS
};

@group(0) @binding(0) var<uniform> u: ShaderUniforms;

@group(1) @binding(0) var tex0: texture_2d<f32>;
@group(1) @binding(1) var tex1: texture_2d<f32>;
@group(1) @binding(2) var tex2: texture_2d<f32>;
@group(1) @binding(3) var tex3: texture_2d<f32>;
@group(1) @binding(8) var cube0: texture_cube<f32>;
@group(1) @binding(9) var cube1: texture_cube<f32>;
@group(1) @binding(10) var cube2: texture_cube<f32>;
@group(1) @binding(11) var cube3: texture_cube<f32>;
@group(1) @binding(16) var samp0: sampler;
@group(1) @binding(17) var samp1: sampler;
@group(1) @binding(18) var samp2: sampler;
@group(1) @binding(19) var samp3: sampler;

struct VertexIn {
    @location(0) position: vec4f,   // D3DDECLUSAGE_POSITION
    @location(3) diffuse: vec4f,    // D3DDECLUSAGE_COLOR 0: the D3DCOLOR bytes B, G, R, A
    @location(4) specular: vec4f,   // D3DDECLUSAGE_COLOR 1
    @location(5) uvw0: vec4f,       // D3DDECLUSAGE_TEXCOORD 0..3
    @location(6) uvw1: vec4f,
    @location(7) uvw2: vec4f,
    @location(8) uvw3: vec4f,
};

// The vs_1_1 outputs a ps_1_1 program can read (v0, v1, t0..t3) and oFog.
struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) d0: vec4f,
    @location(1) d1: vec4f,
    @location(2) t0: vec4f,
    @location(3) t1: vec4f,
    @location(4) t2: vec4f,
    @location(5) t3: vec4f,
    @location(6) fog: f32,
};

// The D3D9 register file of one program run.
struct VSRegs {
    oPos: vec4f,
    oD0: vec4f,
    oD1: vec4f,
    oT0: vec4f,
    oT1: vec4f,
    oT2: vec4f,
    oT3: vec4f,
    oFog: f32,
};

fn c(i: u32) -> vec4f {
    return u.vs[i];
}

fn d3dColor(bgra: vec4f) -> vec4f {
    return bgra.zyxw;
}

// +infinity, from a uniform so that it is a runtime value: WGSL rejects an infinite constant
// expression ("value inf cannot be represented as 'f32'"); u.state.x & 0u is always 0.
fn plusInf() -> f32 {
    return bitcast<f32>(0x7f800000u | (u.state.x & 0u));
}

// rcp - vs: 1 for 1, +infinity for 0, else 1/x.
fn rcp(x: f32) -> f32 {
    if (x == 1.0) {
        return 1.0;
    }
    if (x == 0.0) {
        return plusInf();
    }
    return 1.0 / x;
}

// rsq - vs: on |x|; 1 for 1, +infinity for 0, else 1/sqrt(|x|).
fn rsq(x: f32) -> f32 {
    let v = abs(x);
    if (v == 1.0) {
        return 1.0;
    }
    if (v == 0.0) {
        return plusInf();
    }
    return inverseSqrt(v);
}

// expp - vs: (2^floor(s), s - floor(s), 2^s, 1).
fn expp(s: f32) -> vec4f {
    let f = floor(s);
    return vec4f(exp2(f), s - f, exp2(s), 1.0);
}

// frc - vs
fn frc(s: vec4f) -> vec4f {
    return s - floor(s);
}

fn m4x4(v: vec4f, r: u32) -> vec4f {
    return vec4f(dot(v, c(r)), dot(v, c(r + 1u)), dot(v, c(r + 2u)), dot(v, c(r + 3u)));
}

fn m4x3(v: vec4f, r: u32) -> vec3f {
    return vec3f(dot(v, c(r)), dot(v, c(r + 1u)), dot(v, c(r + 2u)));
}

// A masked register write (the destination write mask).
fn wmask(old: vec4f, val: vec4f, mask: vec4<bool>) -> vec4f {
    return select(old, val, mask);
}

// The device side of every vertex program: the output clamps and the pixel-centre shift.
fn vsFinish(r: VSRegs) -> VSOut {
    var out: VSOut;
    out.d0 = clamp(r.oD0, vec4f(0.0), vec4f(1.0));
    out.d1 = clamp(r.oD1, vec4f(0.0), vec4f(1.0));
    out.t0 = r.oT0;
    out.t1 = r.oT1;
    out.t2 = r.oT2;
    out.t3 = r.oT3;
    out.fog = clamp(r.oFog, 0.0, 1.0);
    out.pos = vec4f(r.oPos.xy + u.misc.xy * r.oPos.w, r.oPos.zw);
    return out;
}

// ps_1_1 arithmetic: every result in [-MaxPixelShaderValue, MaxPixelShaderValue] = [-1, 1].
fn pc(x: vec4f) -> vec4f {
    return clamp(x, vec4f(-1.0), vec4f(1.0));
}

// ps_1_1 constant register cN, as the device holds it (clamped to [-1, 1] at upload,
// plWGPUDevice::IShaderUniformsAndTextures).
fn pconst(i: u32) -> vec4f {
    return u.ps[i];
}

fn bias(x: vec4f) -> vec4f {
    return x - vec4f(0.5);
}

fn bx2(x: vec4f) -> vec4f {
    return 2.0 * (x - vec4f(0.5));
}

// tex tN: the stage's 2D texture with (u, v), or its cube map with (u, v, w).
fn texStage(s: u32, t: vec4f) -> vec4f {
    let b = u.stageBias[s];
    let cube = u.stageCube[s] != 0u;
    switch (s) {
        case 0u: { if (cube) { return textureSampleBias(cube0, samp0, t.xyz, b); } return textureSampleBias(tex0, samp0, t.xy, b); }
        case 1u: { if (cube) { return textureSampleBias(cube1, samp1, t.xyz, b); } return textureSampleBias(tex1, samp1, t.xy, b); }
        case 2u: { if (cube) { return textureSampleBias(cube2, samp2, t.xyz, b); } return textureSampleBias(tex2, samp2, t.xy, b); }
        default: { if (cube) { return textureSampleBias(cube3, samp3, t.xyz, b); } return textureSampleBias(tex3, samp3, t.xy, b); }
    }
}

// texm3x3vspec: the eye-vector reflection of the 3x3 product, sampled from stage `s`'s cube map.
fn texm3x3vspec(s: u32, t1: vec4f, t2: vec4f, t3: vec4f, src: vec4f) -> vec4f {
    let n = vec3f(dot(t1.xyz, src.xyz), dot(t2.xyz, src.xyz), dot(t3.xyz, src.xyz));
    let e = vec3f(t1.w, t2.w, t3.w);
    let r = 2.0 * dot(n, e) / dot(n, n) * n - e;
    return texStage(s, vec4f(r, 1.0));
}

fn alphaPass(a: f32) -> bool {
    let func = u.state.x;
    let ref_ = u.state.y;
    let a8 = u32(round(clamp(a, 0.0, 1.0) * 255.0));
    switch (func) {
        case 0u: { return true; }
        case 1u: { return false; }
        case 2u: { return a8 < ref_; }
        case 3u: { return a8 == ref_; }
        case 4u: { return a8 <= ref_; }
        case 5u: { return a8 > ref_; }
        case 6u: { return a8 != ref_; }
        case 7u: { return a8 >= ref_; }
        default: { return true; }
    }
}

// The device side of every pixel program, as Direct3D 9 runs a ps_1_1 program: the output r0
// saturated to [0, 1] ("ps_1_X Registers"); with D3DRS_FOGENABLE the fog blend
// f C + (1 - f) FOGCOLOR, alpha untouched ("Fog (Direct3D 9)"); then the alpha test on the 8-bit
// output alpha against D3DRS_ALPHAREF (alphaPass).
fn psFinish(r0: vec4f, fog: f32) -> vec4f {
    var c0 = clamp(r0, vec4f(0.0), vec4f(1.0));
    if (kFogEnable) {
        c0 = vec4f(fog * c0.rgb + (1.0 - fog) * u.fogColor.rgb, c0.a);
    }
    if (!alphaPass(c0.a)) {
        discard;
    }
    return c0;
}
