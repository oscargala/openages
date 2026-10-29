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

// The Direct3D 9 fixed-function pipeline of pfDXPipeline, in WGSL.
//
// Structure from pfMetalPipeline/ShaderSrc/FixedPipelineShaders.metal (one vertex and one
// fragment function for every fixed-function pass, the pass described by uniforms); behaviour
// from Direct3D 9: the texture stages are D3D9's D3DTOP/D3DTA combiner driven by the D3DTSS_*
// state plDXPipeline sets (IHandleTextureMode, IHandleStageBlend, IStageStop), not Metal's
// blendFirst/blend/final.
// The uniforms are a snapshot of plWGPUD3DDevice (ShaderTypes.h plWGPUFFUniforms).
//
// D3D9 runtime behaviour this file implements, with its DirectX 9 SDK documentation cite:
// - Vertex transform: v * World * View * Projection, with D3DVBF_1WEIGHTS blending
//   v*W0*w + v*W1*(1-w) ("Geometry Blending", "Vertex Blending").
// - Normals by the inverse transpose of World*View, then NORMALIZENORMALS
//   ("Camera Space Transformations"; D3DRS_NORMALIZENORMALS).
// - Lighting in camera space ("Mathematics of Lighting", "Camera-Relative Lighting"):
//     Diffuse  = saturate(Ce + Ca*(Ga + sum(Atten*Spot*La)) + sum(Cd*Ld*max(N.L,0)*Atten*Spot)),
//                alpha = Cd.a ("Diffuse Lighting", "Ambient Lighting", "Emissive Lighting"; one
//                saturate of the sum);
//     Specular = saturate(Cs*sum(Ls*(N.H)^P*Atten*Spot)), H = normalize(L + V) with the local
//                viewer V = normalize(-P) ("Specular Lighting", D3DRS_LOCALVIEWER);
//     Atten = 1/(a0 + a1*d + a2*d^2), 0 beyond Range ("Light Attenuation Over Distance");
//     Spot = 1 inside Theta/2, 0 outside Phi/2, else ((rho - cos(Phi/2)) / (cos(Theta/2) -
//     cos(Phi/2)))^Falloff ("Spotlight Model"); a falloff of 0 is a hard cut.
//   Material colour sources D3DMCS_MATERIAL/COLOR1/COLOR2, a missing vertex colour falling
//   back to the material (D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_COLORVERTEX).
//   With LIGHTING FALSE the vertex colours pass through; a vertex without a diffuse colour is
//   white, without a specular colour black ("Vertex Color", FVF defaults).
// - Texture coordinates ("Texture Coordinate Processing", D3DTSS_TEXCOORDINDEX,
//   D3DTEXTURETRANSFORMFLAGS): the D3DTSS_TCI_* generators in camera space; input
//   coordinates are padded to (u,v,w,1) before the D3DTS_TEXTUREn transform;
//   D3DTTFF_COUNTn keeps n outputs and D3DTTFF_PROJECTED divides by the last of them per pixel.
// - Texture stages ("Texture Blending", D3DTEXTUREOP, D3DTA): each stage's result is saturated
//   to [0,1]; D3DTOP_DOTPRODUCT3 = saturate(4*sum((a1-0.5)*(a2-0.5))) replicated to all four
//   channels, alpha included (D3DTOP_DOTPRODUCT3 remarks). Stage 0's CURRENT is
//   the diffuse colour. An ALPHAOP of DISABLE on a stage whose COLOROP is enabled passes the
//   current alpha on (the SDK calls that undefined; plDXPipeline::IStageStop relies on it).
// - Specular is added after the texture stages, before fog, when SPECULARENABLE is set
//   ("Specular Lighting": "added to the result of texture blending").
// - Fog ("Pixel Fog", "Fog Formulas", "Eye-Relative vs. Z-based Depth"): table fog on the
//   planar eye depth; the projection's layer scale (pl3DPipeline::IGetCameraToNDC) keeps it
//   W-friendly to 1e-5. Linear f = (end - d)/(end - start), exp f = 1/e^(d*density),
//   exp2 f = 1/e^((d*density)^2); C = f*C + (1-f)*FOGCOLOR. Vertex fog (the shader programs'
//   layers, ShaderPrelude.wgsl) is computed per vertex and interpolated.
// - Alpha test on the 8-bit alpha ("Alpha Testing State", D3DRS_ALPHAREF 0x00-0xff): the
//   quantized alpha round(a*255) against ALPHAREF with ALPHAFUNC.
// - Pixel centres: D3D9 puts them on integer window coordinates, WebGPU half a pixel further
//   on ("Directly Mapping Texels to Pixels"); the vertex shader shifts by half a pixel.

// Variants (pipeline-overridable constants, part of the pipeline key).
override kStages: u32 = 1u;         // stages before the first COLOROP DISABLE (0..8)
override kLighting: bool = false;   // D3DRS_LIGHTING
override kFog: u32 = 0u;            // 0 none, 1 table (pixel) fog, 2 vertex fog
override kVertexBlend: bool = false;// D3DRS_VERTEXBLEND D3DVBF_1WEIGHTS on a vertex with a weight

struct Light {
    position: vec4f,
    direction: vec4f,
    diffuse: vec4f,
    specular: vec4f,
    ambient: vec4f,
    spot: vec4f,        // range, falloff, cos(Theta/2), cos(Phi/2)
    atten: vec4f,       // a0, a1, a2, type
};

struct Stage {
    ops: vec4u,         // COLOROP, COLORARG1, COLORARG2, ALPHAOP
    ops2: vec4u,        // ALPHAARG1, ALPHAARG2, TEXCOORDINDEX, TEXTURETRANSFORMFLAGS
    flags: vec4u,       // cube texture, texture bound
    bias: vec4f,        // MIPMAPLODBIAS
};

struct FFUniforms {
    worldView0: mat4x4f,
    worldView1: mat4x4f,
    normal0: mat4x4f,
    normal1: mat4x4f,
    projection: mat4x4f,
    texture: array<mat4x4f, 8>,
    stages: array<Stage, 8>,
    lights: array<Light, 8>,
    matDiffuse: vec4f,
    matAmbient: vec4f,
    matSpecular: vec4f,
    matEmissive: vec4f,
    globalAmbient: vec4f,
    fogColor: vec4f,
    fogParams: vec4f,   // start, end, density
    textureFactor: vec4f,
    lighting: vec4u,    // SPECULARENABLE, light count, LOCALVIEWER, NORMALIZENORMALS
    sources: vec4u,     // diffuse, specular, ambient, emissive material source
    fog: vec4u,         // table mode, vertex mode, alpha func (0 = no test), alpha ref
    vertex: vec4u,      // vertex has diffuse, vertex has specular
    misc: vec4f,        // pixel offset x, y, specular power
};

@group(0) @binding(0) var<uniform> u: FFUniforms;

@group(1) @binding(0) var tex0: texture_2d<f32>;
@group(1) @binding(1) var tex1: texture_2d<f32>;
@group(1) @binding(2) var tex2: texture_2d<f32>;
@group(1) @binding(3) var tex3: texture_2d<f32>;
@group(1) @binding(4) var tex4: texture_2d<f32>;
@group(1) @binding(5) var tex5: texture_2d<f32>;
@group(1) @binding(6) var tex6: texture_2d<f32>;
@group(1) @binding(7) var tex7: texture_2d<f32>;
@group(1) @binding(8) var cube0: texture_cube<f32>;
@group(1) @binding(9) var cube1: texture_cube<f32>;
@group(1) @binding(10) var cube2: texture_cube<f32>;
@group(1) @binding(11) var cube3: texture_cube<f32>;
@group(1) @binding(12) var cube4: texture_cube<f32>;
@group(1) @binding(13) var cube5: texture_cube<f32>;
@group(1) @binding(14) var cube6: texture_cube<f32>;
@group(1) @binding(15) var cube7: texture_cube<f32>;
@group(1) @binding(16) var samp0: sampler;
@group(1) @binding(17) var samp1: sampler;
@group(1) @binding(18) var samp2: sampler;
@group(1) @binding(19) var samp3: sampler;
@group(1) @binding(20) var samp4: sampler;
@group(1) @binding(21) var samp5: sampler;
@group(1) @binding(22) var samp6: sampler;
@group(1) @binding(23) var samp7: sampler;

// D3DTEXTUREOP
const TOP_DISABLE: u32 = 1u;
const TOP_SELECTARG1: u32 = 2u;
const TOP_SELECTARG2: u32 = 3u;
const TOP_MODULATE: u32 = 4u;
const TOP_MODULATE2X: u32 = 5u;
const TOP_MODULATE4X: u32 = 6u;
const TOP_ADD: u32 = 7u;
const TOP_ADDSIGNED: u32 = 8u;
const TOP_ADDSIGNED2X: u32 = 9u;
const TOP_SUBTRACT: u32 = 10u;
const TOP_ADDSMOOTH: u32 = 11u;
const TOP_BLENDDIFFUSEALPHA: u32 = 12u;
const TOP_BLENDTEXTUREALPHA: u32 = 13u;
const TOP_BLENDFACTORALPHA: u32 = 14u;
const TOP_BLENDTEXTUREALPHAPM: u32 = 15u;
const TOP_BLENDCURRENTALPHA: u32 = 16u;
const TOP_MODULATEALPHA_ADDCOLOR: u32 = 18u;
const TOP_MODULATECOLOR_ADDALPHA: u32 = 19u;
const TOP_MODULATEINVALPHA_ADDCOLOR: u32 = 20u;
const TOP_MODULATEINVCOLOR_ADDALPHA: u32 = 21u;
const TOP_DOTPRODUCT3: u32 = 24u;

// D3DTA
const TA_SELECTMASK: u32 = 0x0fu;
const TA_DIFFUSE: u32 = 0u;
const TA_CURRENT: u32 = 1u;
const TA_TEXTURE: u32 = 2u;
const TA_TFACTOR: u32 = 3u;
const TA_SPECULAR: u32 = 4u;
const TA_COMPLEMENT: u32 = 0x10u;
const TA_ALPHAREPLICATE: u32 = 0x20u;

// D3DCMPFUNC
const CMP_NEVER: u32 = 1u;
const CMP_LESS: u32 = 2u;
const CMP_EQUAL: u32 = 3u;
const CMP_LESSEQUAL: u32 = 4u;
const CMP_GREATER: u32 = 5u;
const CMP_NOTEQUAL: u32 = 6u;
const CMP_GREATEREQUAL: u32 = 7u;
const CMP_ALWAYS: u32 = 8u;

// D3DFOGMODE
const FOG_EXP: u32 = 1u;
const FOG_EXP2: u32 = 2u;
const FOG_LINEAR: u32 = 3u;

// D3DLIGHTTYPE
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;
const LIGHT_DIRECTIONAL: u32 = 3u;

// D3DMATERIALCOLORSOURCE
const MCS_COLOR1: u32 = 1u;
const MCS_COLOR2: u32 = 2u;

// D3DTSS_TCI_*
const TCI_CAMERASPACENORMAL: u32 = 0x10000u;
const TCI_CAMERASPACEPOSITION: u32 = 0x20000u;
const TCI_CAMERASPACEREFLECTIONVECTOR: u32 = 0x30000u;

// D3DTEXTURETRANSFORMFLAGS
const TTFF_PROJECTED: u32 = 256u;

struct VertexIn {
    @location(0) position: vec3f,
    @location(1) weight: f32,
    @location(2) normal: vec3f,
    @location(3) diffuse: vec4f,    // D3DCOLOR bytes B, G, R, A
    @location(4) specular: vec4f,
    @location(5) uvw0: vec3f,
    @location(6) uvw1: vec3f,
    @location(7) uvw2: vec3f,
    @location(8) uvw3: vec3f,
    @location(9) uvw4: vec3f,
    @location(10) uvw5: vec3f,
    @location(11) uvw6: vec3f,
    @location(12) uvw7: vec3f,
};

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) diffuse: vec4f,
    @location(1) specular: vec4f,
    @location(2) t0: vec4f,
    @location(3) t1: vec4f,
    @location(4) t2: vec4f,
    @location(5) t3: vec4f,
    @location(6) t4: vec4f,
    @location(7) t5: vec4f,
    @location(8) t6: vec4f,
    @location(9) t7: vec4f,
    @location(10) fog: vec2f,       // x: eye depth (table fog), y: vertex fog factor
};

fn materialSource(source: u32, material: vec4f, diffuse: vec4f, specular: vec4f) -> vec4f {
    // D3DRS_*MATERIALSOURCE with COLORVERTEX: a source the vertex lacks falls back to the material.
    if (source == MCS_COLOR1 && u.vertex.x != 0u) {
        return diffuse;
    }
    if (source == MCS_COLOR2 && u.vertex.y != 0u) {
        return specular;
    }
    return material;
}

fn fogFactor(mode: u32, d: f32) -> f32 {
    if (mode == FOG_LINEAR) {
        return clamp((u.fogParams.y - d) / (u.fogParams.y - u.fogParams.x), 0.0, 1.0);
    }
    if (mode == FOG_EXP) {
        return clamp(1.0 / exp(d * u.fogParams.z), 0.0, 1.0);
    }
    if (mode == FOG_EXP2) {
        let dd = d * u.fogParams.z;
        return clamp(1.0 / exp(dd * dd), 0.0, 1.0);
    }
    return 1.0;
}

fn inputUVW(in: VertexIn, channel: u32) -> vec3f {
    switch (channel) {
        case 0u: { return in.uvw0; }
        case 1u: { return in.uvw1; }
        case 2u: { return in.uvw2; }
        case 3u: { return in.uvw3; }
        case 4u: { return in.uvw4; }
        case 5u: { return in.uvw5; }
        case 6u: { return in.uvw6; }
        case 7u: { return in.uvw7; }
        default: { return vec3f(0.0); }
    }
}

fn stageCoord(in: VertexIn, s: u32, camPos: vec3f, camNormal: vec3f) -> vec4f {
    let tci = u.stages[s].ops2.z;
    let ttff = u.stages[s].ops2.w;
    var c: vec4f;
    switch (tci & 0xffff0000u) {
        case TCI_CAMERASPACENORMAL: { c = vec4f(camNormal, 1.0); }
        case TCI_CAMERASPACEPOSITION: { c = vec4f(camPos, 1.0); }
        case TCI_CAMERASPACEREFLECTIONVECTOR: {
            // R = 2(E.N)N - E, E the unit vector from the vertex to the (local) viewer.
            let e = -normalize(camPos);
            c = vec4f(2.0 * dot(e, camNormal) * camNormal - e, 1.0);
        }
        default: { c = vec4f(inputUVW(in, tci & 0xffffu), 1.0); }
    }
    if ((ttff & 0xffu) != 0u) {
        c = u.texture[s] * c;
    }
    return c;
}

@vertex
fn fixedVertex(in: VertexIn) -> VertexOut {
    var out: VertexOut;

    let p = vec4f(in.position, 1.0);
    let n = vec4f(in.normal, 0.0);
    var cam: vec4f;
    var camNormal: vec3f;
    if (kVertexBlend) {
        let w = in.weight;
        cam = (u.worldView0 * p) * w + (u.worldView1 * p) * (1.0 - w);
        camNormal = (u.normal0 * n).xyz * w + (u.normal1 * n).xyz * (1.0 - w);
    } else {
        cam = u.worldView0 * p;
        camNormal = (u.normal0 * n).xyz;
    }
    if (u.lighting.w != 0u) {
        let len = length(camNormal);
        if (len > 0.0) {
            camNormal = camNormal / len;
        }
    }

    var clip = u.projection * cam;
    clip = vec4f(clip.xy + u.misc.xy * clip.w, clip.zw);
    out.position = clip;

    let vtxDiffuse = select(vec4f(1.0), in.diffuse.zyxw, u.vertex.x != 0u);
    let vtxSpecular = select(vec4f(0.0), in.specular.zyxw, u.vertex.y != 0u);

    if (kLighting) {
        let cd = materialSource(u.sources.x, u.matDiffuse, vtxDiffuse, vtxSpecular);
        let cs = materialSource(u.sources.y, u.matSpecular, vtxDiffuse, vtxSpecular);
        let ca = materialSource(u.sources.z, u.matAmbient, vtxDiffuse, vtxSpecular);
        let ce = materialSource(u.sources.w, u.matEmissive, vtxDiffuse, vtxSpecular);

        var view = vec3f(0.0, 0.0, -1.0);
        if (u.lighting.z != 0u) {
            let len = length(cam.xyz);
            if (len > 0.0) {
                view = -cam.xyz / len;
            }
        }

        var ambient = vec3f(0.0);
        var diffuse = vec3f(0.0);
        var specular = vec3f(0.0);
        let numLights = min(u.lighting.y, 8u);
        for (var i = 0u; i < numLights; i++) {
            let light = u.lights[i];
            let type_ = u32(light.atten.w);
            var toLight: vec3f;
            var atten = 1.0;
            var spot = 1.0;
            if (type_ == LIGHT_DIRECTIONAL) {
                toLight = -light.direction.xyz;
            } else {
                let delta = light.position.xyz - cam.xyz;
                let dist = length(delta);
                if (dist > light.spot.x) {
                    continue;
                }
                toLight = select(vec3f(0.0), delta / dist, dist > 0.0);
                let denom = light.atten.x + light.atten.y * dist + light.atten.z * dist * dist;
                atten = select(0.0, 1.0 / denom, denom > 0.0);
                if (type_ == LIGHT_SPOT) {
                    let rho = dot(light.direction.xyz, -toLight);
                    if (rho > light.spot.z) {
                        spot = 1.0;
                    } else if (rho <= light.spot.w) {
                        spot = 0.0;
                    } else {
                        let t = (rho - light.spot.w) / (light.spot.z - light.spot.w);
                        spot = select(1.0, pow(t, light.spot.y), light.spot.y != 0.0);
                    }
                }
            }
            let scale = atten * spot;
            ambient += light.ambient.rgb * scale;
            let nDotL = max(dot(camNormal, toLight), 0.0);
            diffuse += light.diffuse.rgb * (nDotL * scale);
            if (u.lighting.x != 0u) {
                let h = normalize(toLight + view);
                let nDotH = max(dot(camNormal, h), 0.0);
                let power = u.misc.z;
                let term = select(pow(nDotH, power), 1.0, power == 0.0);
                specular += light.specular.rgb * (term * scale);
            }
        }
        let lit = ce.rgb + ca.rgb * (u.globalAmbient.rgb + ambient) + cd.rgb * diffuse;
        out.diffuse = vec4f(clamp(lit, vec3f(0.0), vec3f(1.0)), clamp(cd.a, 0.0, 1.0));
        out.specular = vec4f(clamp(cs.rgb * specular, vec3f(0.0), vec3f(1.0)), clamp(cs.a, 0.0, 1.0));
    } else {
        out.diffuse = vtxDiffuse;
        out.specular = vtxSpecular;
    }

    // Only the stages the pass enables read their coordinates.
    if (kStages > 0u) { out.t0 = stageCoord(in, 0u, cam.xyz, camNormal); }
    if (kStages > 1u) { out.t1 = stageCoord(in, 1u, cam.xyz, camNormal); }
    if (kStages > 2u) { out.t2 = stageCoord(in, 2u, cam.xyz, camNormal); }
    if (kStages > 3u) { out.t3 = stageCoord(in, 3u, cam.xyz, camNormal); }
    if (kStages > 4u) { out.t4 = stageCoord(in, 4u, cam.xyz, camNormal); }
    if (kStages > 5u) { out.t5 = stageCoord(in, 5u, cam.xyz, camNormal); }
    if (kStages > 6u) { out.t6 = stageCoord(in, 6u, cam.xyz, camNormal); }
    if (kStages > 7u) { out.t7 = stageCoord(in, 7u, cam.xyz, camNormal); }

    out.fog = vec2f(cam.z, 1.0);
    if (kFog == 2u) {
        out.fog.y = fogFactor(u.fog.y, cam.z);
    }
    return out;
}

fn sampleCoord(s: u32, t: vec4f) -> vec4f {
    // D3DTTFF_PROJECTED divides by the last of the COUNTn outputs, per pixel.
    let ttff = u.stages[s].ops2.w;
    let count = ttff & 0xffu;
    if ((ttff & TTFF_PROJECTED) != 0u && count >= 2u) {
        let q = t[count - 1u];
        return vec4f(t.xyz / q, 1.0);
    }
    return t;
}

// The fragment-stage helpers below select with if/else chains, not switch: on the Metal backend (Chromium, Apple
// GPU) a fragment shader whose texture sample or combiner sits in a WGSL switch costs about sampleCount times as much
// at 4x MSAA, although it still shades once per pixel; the same code as if/else chains costs what it does at 1x. The
// selections are a switch's, and the results bit-identical.
fn sampleStage(s: u32, t: vec4f) -> vec4f {
    // A stage without a texture reads opaque white (counted on the CPU side).
    let c = sampleCoord(s, t);
    let cube = u.stages[s].flags.x != 0u;
    let bias = u.stages[s].bias.x;
    if (s == 0u) { if (cube) { return textureSampleBias(cube0, samp0, c.xyz, bias); } return textureSampleBias(tex0, samp0, c.xy, bias); }
    if (s == 1u) { if (cube) { return textureSampleBias(cube1, samp1, c.xyz, bias); } return textureSampleBias(tex1, samp1, c.xy, bias); }
    if (s == 2u) { if (cube) { return textureSampleBias(cube2, samp2, c.xyz, bias); } return textureSampleBias(tex2, samp2, c.xy, bias); }
    if (s == 3u) { if (cube) { return textureSampleBias(cube3, samp3, c.xyz, bias); } return textureSampleBias(tex3, samp3, c.xy, bias); }
    if (s == 4u) { if (cube) { return textureSampleBias(cube4, samp4, c.xyz, bias); } return textureSampleBias(tex4, samp4, c.xy, bias); }
    if (s == 5u) { if (cube) { return textureSampleBias(cube5, samp5, c.xyz, bias); } return textureSampleBias(tex5, samp5, c.xy, bias); }
    if (s == 6u) { if (cube) { return textureSampleBias(cube6, samp6, c.xyz, bias); } return textureSampleBias(tex6, samp6, c.xy, bias); }
    if (cube) { return textureSampleBias(cube7, samp7, c.xyz, bias); }
    return textureSampleBias(tex7, samp7, c.xy, bias);
}

fn stageArg(sel: u32, diffuse: vec4f, current: vec4f, tex: vec4f, specular: vec4f) -> vec4f {
    let which = sel & TA_SELECTMASK;
    var a = current;
    if (which == TA_DIFFUSE) {
        a = diffuse;
    } else if (which == TA_TEXTURE) {
        a = tex;
    } else if (which == TA_TFACTOR) {
        a = u.textureFactor;
    } else if (which == TA_SPECULAR) {
        a = specular;
    }
    // COMPLEMENT's 1 - a written as one fma pins its rounding: fma(a, -1, 1) rounds once, as the plain subtraction
    // does, so the D3D9 equation is unchanged, and a compiler has no product or sum left to regroup with the caller's.
    // Rewriting it reopens the bit identity of the specialised programs with this shader.
    if ((sel & TA_COMPLEMENT) != 0u) {
        a = fma(a, vec4f(-1.0), vec4f(1.0));
    }
    if ((sel & TA_ALPHAREPLICATE) != 0u) {
        a = vec4f(a.a);
    }
    return a;
}

fn stageOp(op: u32, a1: vec4f, a2: vec4f, diffuse: vec4f, current: vec4f, tex: vec4f) -> vec4f {
    // BLENDDIFFUSEALPHA and BLENDCURRENTALPHA (Arg1 * alpha + Arg2 * (1 - alpha)) spell out the roundings the
    // switch form compiled to: colour fma(Arg2, 1 - alpha, Arg1 * alpha), alpha fma(Arg1 - Arg2, alpha, Arg2).
    // Written plainly inside the if/else chain, the compiler rounds the alpha lane 1 ulp apart.
    if (op == TOP_SELECTARG1) { return a1; }
    if (op == TOP_SELECTARG2) { return a2; }
    if (op == TOP_MODULATE) { return a1 * a2; }
    if (op == TOP_MODULATE2X) { return a1 * a2 * 2.0; }
    if (op == TOP_MODULATE4X) { return a1 * a2 * 4.0; }
    if (op == TOP_ADD) { return a1 + a2; }
    // ADDSIGNED (a1 + a2 - 0.5) and ADDSIGNED2X ((a1 + a2 - 0.5) * 2) as one fma each pin their roundings:
    // fma(1, s, -0.5) and fma(s, 2, -1) round as the left-to-right sums do (the doubling is exact), so the D3D9
    // equations are unchanged, and the compiler has nothing left to regroup. Rewriting them reopens the bit identity
    // of the specialised programs with this shader.
    if (op == TOP_ADDSIGNED) { return fma(vec4f(1.0), a1 + a2, vec4f(-0.5)); }
    if (op == TOP_ADDSIGNED2X) { return fma(a1 + a2, vec4f(2.0), vec4f(-1.0)); }
    if (op == TOP_SUBTRACT) { return a1 - a2; }
    if (op == TOP_ADDSMOOTH) { return a1 + a2 - a1 * a2; }
    if (op == TOP_BLENDDIFFUSEALPHA) { return vec4f(fma(a2.rgb, vec3f(1.0 - diffuse.a), a1.rgb * diffuse.a), fma(a1.a - a2.a, diffuse.a, a2.a)); }
    if (op == TOP_BLENDTEXTUREALPHA) { return a1 * tex.a + a2 * (1.0 - tex.a); }
    if (op == TOP_BLENDFACTORALPHA) { return a1 * u.textureFactor.a + a2 * (1.0 - u.textureFactor.a); }
    if (op == TOP_BLENDTEXTUREALPHAPM) { return a1 + a2 * (1.0 - tex.a); }
    if (op == TOP_BLENDCURRENTALPHA) { return vec4f(fma(a2.rgb, vec3f(1.0 - current.a), a1.rgb * current.a), fma(a1.a - a2.a, current.a, a2.a)); }
    if (op == TOP_MODULATEALPHA_ADDCOLOR) { return a1 + a1.a * a2; }
    if (op == TOP_MODULATECOLOR_ADDALPHA) { return a1 * a2 + vec4f(a1.a); }
    if (op == TOP_MODULATEINVALPHA_ADDCOLOR) { return (1.0 - a1.a) * a2 + a1; }
    if (op == TOP_MODULATEINVCOLOR_ADDALPHA) { return (vec4f(1.0) - a1) * a2 + vec4f(a1.a); }
    if (op == TOP_DOTPRODUCT3) {
        let d = (a1.rgb - vec3f(0.5)) * (a2.rgb - vec3f(0.5));
        return vec4f(4.0 * (d.r + d.g + d.b));
    }
    return a1;
}

fn alphaTest(func: u32, a: f32, ref_: u32) -> bool {
    let a8 = u32(round(clamp(a, 0.0, 1.0) * 255.0));
    if (func == CMP_NEVER) { return false; }
    if (func == CMP_LESS) { return a8 < ref_; }
    if (func == CMP_EQUAL) { return a8 == ref_; }
    if (func == CMP_LESSEQUAL) { return a8 <= ref_; }
    if (func == CMP_GREATER) { return a8 > ref_; }
    if (func == CMP_NOTEQUAL) { return a8 != ref_; }
    if (func == CMP_GREATEREQUAL) { return a8 >= ref_; }
    return true;
}

@fragment
fn fixedFragment(in: VertexOut) -> @location(0) vec4f {
    var coords = array<vec4f, 8>(in.t0, in.t1, in.t2, in.t3, in.t4, in.t5, in.t6, in.t7);
    let diffuse = in.diffuse;
    let specular = in.specular;

    // Stage 0 with COLOROP DISABLE: texture blending is off and the diffuse colour passes.
    var current = diffuse;
    for (var s = 0u; s < kStages; s++) {
        let stage = u.stages[s];
        let tex = sampleStage(s, coords[s]);
        let c1 = stageArg(stage.ops.y, diffuse, current, tex, specular);
        let c2 = stageArg(stage.ops.z, diffuse, current, tex, specular);
        let color = stageOp(stage.ops.x, c1, c2, diffuse, current, tex);
        var alpha = current.a;
        if (stage.ops.x == TOP_DOTPRODUCT3) {
            alpha = color.a;
        } else if (stage.ops.w != TOP_DISABLE) {
            let a1 = stageArg(stage.ops2.x, diffuse, current, tex, specular);
            let a2 = stageArg(stage.ops2.y, diffuse, current, tex, specular);
            alpha = stageOp(stage.ops.w, a1, a2, diffuse, current, tex).a;
        }
        current = clamp(vec4f(color.rgb, alpha), vec4f(0.0), vec4f(1.0));
    }

    var rgb = current.rgb;
    if (u.lighting.x != 0u) {
        rgb = clamp(rgb + specular.rgb, vec3f(0.0), vec3f(1.0));
    }

    // C = f*C + (1-f)*FOGCOLOR, pinned to one fma: (1-f)*FOGCOLOR is rounded with the add, f*C on its own. That is
    // not strict IEEE evaluation (which rounds both products); it is the contraction Metal's compilers chose for the
    // plain text, measured equal over 537 traced and 320 synthetic variants in Chromium
    // (chromium-20261028-151224-pin1) and WebKit (safari-20261028-154938), so the D3D9 equation and today's output
    // are unchanged. Rewriting it reopens the bit identity of the specialised programs with this shader.
    if (kFog == 1u) {
        let f = fogFactor(u.fog.x, in.fog.x);
        rgb = fma(vec3f(1.0 - f), u.fogColor.rgb, f * rgb);
    } else if (kFog == 2u) {
        let f = clamp(in.fog.y, 0.0, 1.0);
        rgb = fma(vec3f(1.0 - f), u.fogColor.rgb, f * rgb);
    }

    if (u.fog.z != 0u && !alphaTest(u.fog.z, current.a, u.fog.w)) {
        discard;
    }
    return vec4f(rgb, current.a);
}
