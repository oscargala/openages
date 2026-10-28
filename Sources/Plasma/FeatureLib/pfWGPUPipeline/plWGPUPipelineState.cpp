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

#include "plWGPUPipelineState.h"

#include "ShaderSrc/ShaderTypes.h"
#include "plDrawable/plGBufferGroup.h"
#include "plWGPUD3DDevice.h"
#include "plWGPUDevice.h"
#include "plWGPUShader.h"

using namespace plWGPUD3D9;

// The WGSL sources, embedded at build time as raw string literals (CMakeLists.txt,
// EmbedWGSL.cmake): the role of Metal's compiled shader library.
static const char kClearWGSL[] =
#include "Clear.wgsl.inc"
    ;
static const char kFixedPipelineWGSL[] =
#include "FixedPipelineShaders.wgsl.inc"
    ;

plWGPUPipelineState::plWGPUPipelineState(plWGPUDevice* device)
    : fDevice(device), fFixed()
{
}

WGPURenderPipeline plWGPUPipelineState::GetRenderPipelineState()
{
    return fDevice->PipelineState(this);
}

size_t plWGPUPipelineState::GetHash() const
{
    return std::hash<uint16_t>()(GetID()) ^ (fFixed.Hash() << 3);
}

// MARK: Clear

plWGPUClearPipelineState::plWGPUClearPipelineState(plWGPUDevice* device, bool shouldClearColor, bool shouldClearDepth)
    : plWGPUPipelineState(device),
      fShouldClearDepth(shouldClearDepth),
      fShouldClearColor(shouldClearColor)
{
    // As plMetalDevice::Clear, the quad covers the viewport at any depth; D3D9 Clear writes
    // the depth value everywhere in the rect (no depth test), and never culls.
    fFixed.topology = WGPUPrimitiveTopology_TriangleStrip;
    fFixed.cullMode = WGPUCullMode_None;
    fFixed.depthCompare = WGPUCompareFunction_Always;
    fFixed.depthWrite = shouldClearDepth;
}

size_t plWGPUClearPipelineState::GetHash() const
{
    size_t value = plWGPUPipelineState::GetHash();
    value ^= std::hash<bool>()(fShouldClearDepth) << 1;
    value ^= std::hash<bool>()(fShouldClearColor) << 2;
    return value;
}

const char* plWGPUClearPipelineState::GetShaderSource() const
{
    return kClearWGSL;
}

const char* plWGPUClearPipelineState::GetFragmentFunction() const
{
    // A depth-only clear still has the pass's colour target (writeMask None, GetWriteMask).
    // WebKit's WebGPU rejects a pipeline whose fragment entry writes only @builtin(frag_depth)
    // while a colour target is present, even with writeMask 0, and accepts the same pipeline
    // when the entry also writes @location(0); Chromium accepts both. The colour value goes to
    // a writeMask-0 target and is never written, so the depth-only clear uses the
    // colour-and-depth entry.
    if (fShouldClearDepth)
        return "clearFragmentColorDepth";
    return "clearFragmentColor";
}

// MARK: Fixed function

plWGPUFixedFunctionPipelineState::plWGPUFixedFunctionPipelineState(plWGPUDevice* device, const plWGPUFixedFunctionKey& key,
                                                                   const plWGPUFixedState& fixed)
    : plWGPUPipelineState(device), fKey(key)
{
    fFixed = fixed;
}

size_t plWGPUFixedFunctionPipelineState::GetHash() const
{
    return plWGPUPipelineState::GetHash() ^ std::hash<uint64_t>()(fKey.Pack());
}

const char* plWGPUFixedFunctionPipelineState::GetShaderSource() const
{
    return kFixedPipelineWGSL;
}

static WGPUBlendFactor IBlendFactor(uint8_t d3dBlend)
{
    // D3DBLEND -> WebGPU. With D3DRS_SEPARATEALPHABLENDENABLE off (never set by plDXPipeline)
    // the alpha channel uses the same factors, whose colour forms read alpha there, as
    // WebGPU's do.
    switch (d3dBlend) {
        case D3DBLEND_ZERO:
            return WGPUBlendFactor_Zero;
        case D3DBLEND_ONE:
            return WGPUBlendFactor_One;
        case D3DBLEND_SRCCOLOR:
            return WGPUBlendFactor_Src;
        case D3DBLEND_INVSRCCOLOR:
            return WGPUBlendFactor_OneMinusSrc;
        case D3DBLEND_SRCALPHA:
            return WGPUBlendFactor_SrcAlpha;
        case D3DBLEND_INVSRCALPHA:
            return WGPUBlendFactor_OneMinusSrcAlpha;
        case D3DBLEND_DESTALPHA:
            return WGPUBlendFactor_DstAlpha;
        case D3DBLEND_INVDESTALPHA:
            return WGPUBlendFactor_OneMinusDstAlpha;
        case D3DBLEND_DESTCOLOR:
            return WGPUBlendFactor_Dst;
        case D3DBLEND_INVDESTCOLOR:
            return WGPUBlendFactor_OneMinusDst;
        case D3DBLEND_SRCALPHASAT:
            return WGPUBlendFactor_SrcAlphaSaturated;
        default:
            return WGPUBlendFactor_One;
    }
}

// The frame-buffer blend of D3DRS_ALPHABLENDENABLE/SRCBLEND/DESTBLEND/BLENDOP (both the
// fixed-function and the shader-program pipelines).
static bool IConfigureD3DBlend(WGPUBlendState& blend, uint8_t enabled, uint8_t srcBlend, uint8_t dstBlend, uint8_t blendOp);

bool plWGPUFixedFunctionPipelineState::ConfigureBlend(WGPUBlendState& blend) const
{
    return IConfigureD3DBlend(blend, fKey.blendEnabled, fKey.srcBlend, fKey.dstBlend, fKey.blendOp);
}

static bool IConfigureD3DBlend(WGPUBlendState& blend, uint8_t enabled, uint8_t srcBlend, uint8_t dstBlend, uint8_t blendOp)
{
    if (!enabled)
        return false;

    WGPUBlendOperation op;
    switch (blendOp) {
        case D3DBLENDOP_SUBTRACT:
            op = WGPUBlendOperation_Subtract;
            break;
        case D3DBLENDOP_REVSUBTRACT:
            op = WGPUBlendOperation_ReverseSubtract;
            break;
        case D3DBLENDOP_MIN:
            op = WGPUBlendOperation_Min;
            break;
        case D3DBLENDOP_MAX:
            op = WGPUBlendOperation_Max;
            break;
        default:
            op = WGPUBlendOperation_Add;
            break;
    }
    blend.color.operation = op;
    blend.color.srcFactor = IBlendFactor(srcBlend);
    blend.color.dstFactor = IBlendFactor(dstBlend);
    blend.alpha = blend.color;
    if (op == WGPUBlendOperation_Min || op == WGPUBlendOperation_Max) {
        // D3D9 ignores the factors for MIN/MAX; WebGPU requires them to be One.
        blend.color.srcFactor = blend.color.dstFactor = WGPUBlendFactor_One;
        blend.alpha = blend.color;
    }
    return true;
}

WGPUColorWriteMask plWGPUFixedFunctionPipelineState::GetWriteMask() const
{
    // D3DCOLORWRITEENABLE_RED/GREEN/BLUE/ALPHA = 1/2/4/8, the WebGPU bits.
    return WGPUColorWriteMask(fKey.writeMask & 0xf);
}

plWGPUFixedFunctionPipelineState::VertexLayout plWGPUFixedFunctionPipelineState::Layout(uint8_t vertexFormat, uint8_t vertexKind)
{
    VertexLayout l;
    if (vertexKind == 2) {
        // D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0): plAVTexVert and
        // plShadowVertStruct (both in plDXPipeline.cpp), drawn without lighting. D3D9 extends a
        // 2-float coordinate with 1 before the texture transform,
        // which is why DX puts the blur offsets in the matrix's third row
        // (plDXPipeline::IRenderBlurFromShadowMap);
        // the vertices are stored with that 1 as (u, v, 1).
        l.weight = -1;
        l.normal = -1;
        l.diffuse = -1;
        l.specular = -1;
        l.uvw = 12;
        l.numUVWs = 1;
        l.stride = 24;
        return l;
    }
    if (vertexKind == 1) {
        // plPlateVertex: D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)
        // (the plDXPlateManager constructor)
        l.weight = -1;
        l.normal = -1;
        l.diffuse = 12;
        l.specular = -1;
        l.uvw = 16;
        l.numUVWs = 1;
        l.stride = 28;
        return l;
    }
    // plDXPipeline::IGetBufferD3DFormat/IGetBufferFormatSize: position, the
    // blend weight (D3DFVF_XYZB1), normal, diffuse, specular, then 3-float UVW channels.
    uint32_t offset = 12;
    l.weight = -1;
    if ((vertexFormat & plGBufferGroup::kSkinWeightMask) == plGBufferGroup::kSkin1Weight) {
        l.weight = offset;
        offset += 4;
    }
    l.normal = offset;
    offset += 12;
    l.diffuse = offset;
    offset += 4;
    l.specular = offset;
    offset += 4;
    l.uvw = offset;
    l.numUVWs = plGBufferGroup::CalcNumUVs(vertexFormat);
    offset += 12 * l.numUVWs;
    l.stride = offset;
    return l;
}

void plWGPUFixedFunctionPipelineState::ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                                 std::vector<WGPUVertexAttribute>&    attributes) const
{
    // Buffer 0 is the vertex data; buffer 1 is the device's zero buffer (stride 0) that feeds
    // every FVF component the vertex lacks. The attribute vector is sized before the layouts
    // point into it.
    const VertexLayout l = Layout(fKey.vertexFormat, fKey.plateVertex);

    struct Input
    {
        uint32_t         location;
        WGPUVertexFormat format;
        int32_t          offset;
    };
    std::vector<Input> inputs = {
        {kFFLocPosition, WGPUVertexFormat_Float32x3, 0},
        {kFFLocWeight, WGPUVertexFormat_Float32, l.weight},
        {kFFLocNormal, WGPUVertexFormat_Float32x3, l.normal},
        {kFFLocDiffuse, WGPUVertexFormat_Unorm8x4, l.diffuse},
        {kFFLocSpecular, WGPUVertexFormat_Unorm8x4, l.specular},
    };
    for (uint32_t i = 0; i < 8; i++)
        inputs.push_back({kFFLocUVW0 + i, WGPUVertexFormat_Float32x3, i < l.numUVWs ? int32_t(l.uvw + 12 * i) : -1});

    attributes.resize(inputs.size());
    size_t present = 0;
    for (const Input& in : inputs) {
        if (in.offset >= 0)
            present++;
    }
    size_t front = 0, back = present;
    for (const Input& in : inputs) {
        WGPUVertexAttribute a = WGPU_VERTEX_ATTRIBUTE_INIT;
        a.format = in.format;
        a.shaderLocation = in.location;
        if (in.offset >= 0) {
            a.offset = uint64_t(in.offset);
            attributes[front++] = a;
        } else {
            a.offset = 0;
            attributes[back++] = a;
        }
    }

    WGPUVertexBufferLayout layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    layout.stepMode = WGPUVertexStepMode_Vertex;
    layout.arrayStride = l.stride;
    layout.attributeCount = present;
    layout.attributes = attributes.data();
    layouts.push_back(layout);

    WGPUVertexBufferLayout zero = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    zero.stepMode = WGPUVertexStepMode_Vertex;
    zero.arrayStride = 0;
    zero.attributeCount = attributes.size() - present;
    zero.attributes = attributes.data() + present;
    layouts.push_back(zero);
}

void plWGPUFixedFunctionPipelineState::ConfigureConstants(WGPUShaderStage stage, std::vector<WGPUConstantEntry>& constants) const
{
    auto add = [&constants](const char* key, double value) {
        WGPUConstantEntry e = WGPU_CONSTANT_ENTRY_INIT;
        e.key = WGPUStringView{key, WGPU_STRLEN};
        e.value = value;
        constants.push_back(e);
    };
    add("kStages", fKey.stages);
    add("kFog", fKey.fog);
    if (stage == WGPUShaderStage_Vertex) {
        add("kLighting", fKey.lighting);
        add("kVertexBlend", fKey.vertexBlend);
    }
}

// MARK: Shader programs

plWGPUShaderPipelineState::plWGPUShaderPipelineState(plWGPUDevice* device, const plWGPUShaderKey& key,
                                                     const plWGPUFixedState& fixed, const plWGPUShaderProgram* vs,
                                                     const plWGPUShaderProgram* ps)
    : plWGPUPipelineState(device), fKey(key), fVS(vs), fPS(ps)
{
    fFixed = fixed;
}

size_t plWGPUShaderPipelineState::GetHash() const
{
    return plWGPUPipelineState::GetHash() ^ std::hash<uint64_t>()(fKey.Pack());
}

const char* plWGPUShaderPipelineState::GetShaderSource() const
{
    // The pair always comes from one family (plWaveSet7, plGrassShaderMod and the decal and
    // ripple setups pair a vertex program with its family's pixel program); the device draws
    // nothing for a pair from two families (DrawShader).
    return plWGPUShaderProgram::ModuleSource(fVS->family);
}

const char* plWGPUShaderPipelineState::GetVertexFunction() const
{
    return fVS->entry;
}

const char* plWGPUShaderPipelineState::GetFragmentFunction() const
{
    return fPS->entry;
}

bool plWGPUShaderPipelineState::ConfigureBlend(WGPUBlendState& blend) const
{
    return IConfigureD3DBlend(blend, fKey.blendEnabled, fKey.srcBlend, fKey.dstBlend, fKey.blendOp);
}

WGPUColorWriteMask plWGPUShaderPipelineState::GetWriteMask() const
{
    return WGPUColorWriteMask(fKey.writeMask & 0xf);
}

void plWGPUShaderPipelineState::ConfigureVertexDescriptor(std::vector<WGPUVertexBufferLayout>& layouts,
                                                          std::vector<WGPUVertexAttribute>&    attributes) const
{
    // The buffer's FVF (IGetBufferD3DFormat) at the fixed-function locations; the programs'
    // VertexIn reads position, the two colours and texture coordinates 0-3 by usage
    // (ShaderPrelude.wgsl), the absent ones from the zero buffer.
    plWGPUFixedFunctionKey ffKey = {};
    ffKey.vertexFormat = fKey.vertexFormat;
    plWGPUFixedFunctionPipelineState ff(fDevice, ffKey, fFixed);
    ff.ConfigureVertexDescriptor(layouts, attributes);
}

void plWGPUShaderPipelineState::ConfigureConstants(WGPUShaderStage stage, std::vector<WGPUConstantEntry>& constants) const
{
    auto add = [&constants](const char* key, double value) {
        WGPUConstantEntry e = WGPU_CONSTANT_ENTRY_INIT;
        e.key = WGPUStringView{key, WGPU_STRLEN};
        e.value = value;
        constants.push_back(e);
    };
    // The vertex programs read no overridable constant; the pixel programs' psFinish reads kFogEnable.
    if (stage == WGPUShaderStage_Fragment)
        add("kFogEnable", fKey.fog);
}
