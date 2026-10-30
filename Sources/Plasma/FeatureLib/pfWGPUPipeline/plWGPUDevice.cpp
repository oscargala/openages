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

#include "plWGPUDevice.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string_theory/format>
#include <string_theory/string_stream>

#include "ShaderSrc/ShaderTypes.h"
#include "hsGMatState.inl"
#include "plDrawable/plDrawableSpans.h"
#include "plDrawable/plGBufferGroup.h"
#include "plFileSystem.h"
#include "plGImage/hsCodecManager.h"
#include "plGImage/plCubicEnvironmap.h"
#include "plGImage/plMipmap.h"
#include "plPipeline/hsG3DDeviceSelector.h"
#include "plPipeline/plCubicRenderTarget.h"
#include "plPipeline/plRenderTarget.h"
#include "plStatusLog/plStatusLog.h"
#include "plWGPULog.h"
#include "plWGPUPipeline.h"
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif
#include "plWGPUPipelineState.h"
#include "plWGPUShader.h"
#include "plWGPUTextureFormat.h"
#include "plWGPUTexturePack.h"
#include "pnEncryption/plChecksum.h"

using namespace plWGPUD3D9;

namespace
{
WGPUStringView Str(const char* text) { return WGPUStringView{text, std::strlen(text)}; }

constexpr uint64_t kUniformAlign = 256;        // minUniformBufferOffsetAlignment (default limit)
constexpr uint64_t kUniformBindingSize = 4096; // every uniform block of ShaderTypes.h fits
constexpr uint64_t kUniformArenaSize = 256 * 1024;
constexpr uint64_t kVertexArenaSize = 256 * 1024;

// The most pipeline creations one shader module object serves (plWGPUDevice::ShaderModule).
constexpr uint32_t kShaderModulePipelines = 64;

// queue.writeBuffer takes sizes and offsets in multiples of four bytes.
constexpr uint64_t Align4(uint64_t v) { return (v + 3) & ~uint64_t(3); }
} // namespace

/// Macros for getting/setting data in a vertex buffer (plMetalDevice.cpp)
template <typename T>
static inline void inlCopy(uint8_t*& src, uint8_t*& dst)
{
    T* src_ptr = reinterpret_cast<T*>(src);
    T* dst_ptr = reinterpret_cast<T*>(dst);
    *dst_ptr = *src_ptr;
    src += sizeof(T);
    dst += sizeof(T);
}

template <typename T, size_t N>
static inline void inlSkip(uint8_t*& src)
{
    src += sizeof(T) * N;
}

plWGPUDevice::plWGPUDevice()
    : fPipeline(),
      fErrorMsg(),
      fWGPUDevice(),
      fQueue(),
      fSurface(),
      fDeviceType(),
      fSampleCount(1),
      fGeneration(),
      fCanvasFormatKnown(),
      fFramebufferFormat(WGPUTextureFormat_BGRA8Unorm),
      fDepthFormat(WGPUTextureFormat_Depth24PlusStencil8),
      fCurrentCommandBuffer(),
      fCurrentOffscreenCommandBuffer(),
      fCurrentRenderTargetCommandEncoder(),
      fCurrentDrawable(),
      fCurrentDrawableView(),
      fCurrentDrawableDepthTexture(),
      fCurrentDrawableDepthView(),
      fCurrentFragmentMSAAOutputTexture(),
      fCurrentFragmentMSAAOutputView(),
      fDrawableWidth(),
      fDrawableHeight(),
      fCurrentFragmentOutputView(),
      fCurrentDepthView(),
      fCurrentColorFormat(WGPUTextureFormat_Undefined),
      fCurrentDepthFormat(WGPUTextureFormat_Undefined),
      fCurrentTargetWidth(),
      fCurrentTargetHeight(),
      fCurrentRenderTarget(),
      fShouldClearColor(),
      fShouldClearDepth(),
      fClearColor{0.f, 0.f, 0.f, 1.f},
      fClearDepth(1.f),
      fViewport{0.f, 0.f, 0.f, 0.f},
      fSamplerStates{},
      fPointSampler(),
      fUniformBindGroupLayout(),
      fTextureBindGroupLayout(),
      fFixedTextureBindGroupLayout(),
      fUniformsOnlyLayout(),
      fUniformsAndTextureLayout(),
      fFixedFunctionLayout(),
      fZeroVertexBuffer(),
      fWhiteCubeTexture(),
      fWhiteCubeTextureView(),
      fFixedBindGroupSerial(uint32_t(-1)),
      fFixedTransformSerial(uint32_t(-1)),
      fWhiteTexture(),
      fWhiteTextureView(),
      fSupportsDXTTextures(),
      fDoesSmallTextures(true)
{
    fCurrCullMode = D3DCULL_CW;
    fShaderLayout = nullptr;
#ifdef PLASMA_TEST_HOOKS
    fKeepLastDraw = false;
    fLastDrawValid = false;
    fLastDrawStages = 0;
    fLastDrawFog = 0;
    fLastShaderDrawValid = false;
#endif
    fFrontBuffer = nullptr;
    fFrontBufferCopy = nullptr;
    fGammaLUTTexture = nullptr;
    fGammaLUTView = nullptr;
    fUnprocessedTexture = nullptr;
    fUnprocessedView = nullptr;
    fGammaBindGroupLayout = nullptr;
    fGammaPipelineLayout = nullptr;
    fGammaPipeline = nullptr;
    fGammaPipelineFormat = WGPUTextureFormat_Undefined;
    fGammaBindGroup = nullptr;
    fGammaBindGroupInput = nullptr;
    fGammaBindGroupLUT = nullptr;
    fFramePostprocessed = false;
#ifdef PLASMA_TEST_HOOKS
    fUploadDrawable = nullptr;
    fUploadGroup = 0;
    fUploadTextureReason = nullptr;
#endif
    fAutoGenMultiLevel = 0;
}

bool plWGPUDevice::InitDevice()
{
    fQueue = wgpuDeviceGetQueue(fWGPUDevice);

    // As plMetalDevice::InitDevice: compressed textures need the optional BC feature; without it
    // they are decompressed in software.
    fSupportsDXTTextures = wgpuDeviceHasFeature(fWGPUDevice, WGPUFeatureName_TextureCompressionBC);
    if (!fSupportsDXTTextures) {
        hsStatusMessage("Render device does not support DXT (BC) textures. Falling back on software decompression.");
    }

    // Bind group layouts: group 0 = one uniform block at a dynamic offset (Metal's
    // setVertexBytes/setFragmentBytes); group 1 = the texture and its sampler.
    {
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = true;
        entry.buffer.minBindingSize = 0;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU uniforms");
        desc.entryCount = 1;
        desc.entries = &entry;
        fUniformBindGroupLayout = wgpuDeviceCreateBindGroupLayout(fWGPUDevice, &desc);
    }
    {
        WGPUBindGroupLayoutEntry entries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU texture");
        desc.entryCount = 2;
        desc.entries = entries;
        fTextureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(fWGPUDevice, &desc);
    }
    {
        WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU uniforms only");
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &fUniformBindGroupLayout;
        fUniformsOnlyLayout = wgpuDeviceCreatePipelineLayout(fWGPUDevice, &desc);

        WGPUBindGroupLayout layouts[2] = {fUniformBindGroupLayout, fTextureBindGroupLayout};
        desc.label = Str("plWGPU uniforms and texture");
        desc.bindGroupLayoutCount = 2;
        desc.bindGroupLayouts = layouts;
        fUniformsAndTextureLayout = wgpuDeviceCreatePipelineLayout(fWGPUDevice, &desc);
    }

    // Fixed function: group 1 holds the eight D3D9 texture stages, each as a 2D and a cube
    // binding (a stage samples the one its texture is) and a sampler (FixedPipelineShaders.wgsl).
    {
        WGPUBindGroupLayoutEntry entries[24];
        for (uint32_t i = 0; i < 24; i++) {
            entries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entries[i].binding = i;
            entries[i].visibility = WGPUShaderStage_Fragment;
            if (i < 16) {
                entries[i].texture.sampleType = WGPUTextureSampleType_Float;
                entries[i].texture.viewDimension = i < 8 ? WGPUTextureViewDimension_2D : WGPUTextureViewDimension_Cube;
            } else {
                entries[i].sampler.type = WGPUSamplerBindingType_Filtering;
            }
        }
        WGPUBindGroupLayoutDescriptor bd = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bd.label = Str("plWGPU fixed-function textures");
        bd.entryCount = 24;
        bd.entries = entries;
        fFixedTextureBindGroupLayout = wgpuDeviceCreateBindGroupLayout(fWGPUDevice, &bd);

        WGPUBindGroupLayout          layouts[2] = {fUniformBindGroupLayout, fFixedTextureBindGroupLayout};
        WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU fixed function");
        desc.bindGroupLayoutCount = 2;
        desc.bindGroupLayouts = layouts;
        fFixedFunctionLayout = wgpuDeviceCreatePipelineLayout(fWGPUDevice, &desc);
    }

    // Shader programs: the fixed-function groups 0 and 1, in a layout object of their own (the
    // teardown releases both).
    {
        WGPUBindGroupLayout          layouts[2] = {fUniformBindGroupLayout, fFixedTextureBindGroupLayout};
        WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU shader programs");
        desc.bindGroupLayoutCount = 2;
        desc.bindGroupLayouts = layouts;
        fShaderLayout = wgpuDeviceCreatePipelineLayout(fWGPUDevice, &desc);
    }
    {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU zero vertex");
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        desc.size = 64;
        fZeroVertexBuffer = wgpuDeviceCreateBuffer(fWGPUDevice, &desc);
        const uint8_t zeros[64] = {};
        wgpuQueueWriteBuffer(fQueue, fZeroVertexBuffer, 0, zeros, sizeof(zeros));
    }

    // As plDXTextFont::IInitStateBlocks: POINT min/mag, no mip filter.
    {
        WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU point");
        desc.addressModeU = WGPUAddressMode_ClampToEdge;
        desc.addressModeV = WGPUAddressMode_ClampToEdge;
        desc.addressModeW = WGPUAddressMode_ClampToEdge;
        desc.magFilter = WGPUFilterMode_Nearest;
        desc.minFilter = WGPUFilterMode_Nearest;
        desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        desc.lodMaxClamp = 0.f;
        fPointSampler = wgpuDeviceCreateSampler(fWGPUDevice, &desc);
    }

    // A 1x1 opaque white texture for shaders that sample while the stage has no texture.
    {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU white");
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size = {1, 1, 1};
        desc.format = WGPUTextureFormat_BGRA8Unorm;
        fWhiteTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
        const uint32_t           white = 0xffffffff;
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = fWhiteTexture;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D size = {1, 1, 1};
        wgpuQueueWriteTexture(fQueue, &dst, &white, sizeof(white), &layout, &size);
        fWhiteTextureView = wgpuTextureCreateView(fWhiteTexture, nullptr);

        desc.label = Str("plWGPU white cube");
        desc.size = {1, 1, 6};
        fWhiteCubeTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
        for (uint32_t face = 0; face < 6; face++) {
            dst.texture = fWhiteCubeTexture;
            dst.origin = {0, 0, face};
            wgpuQueueWriteTexture(fQueue, &dst, &white, sizeof(white), &layout, &size);
        }
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.dimension = WGPUTextureViewDimension_Cube;
        vd.arrayLayerCount = 6;
        fWhiteCubeTextureView = wgpuTextureCreateView(fWhiteCubeTexture, &vd);
    }

    return true;
}

void plWGPUDevice::Shutdown()
{
    // Not implemented for Metal either (plMetalDevice::Shutdown).
    hsAssert(0, "Shutdown not implemented for WebGPU rendering");
}

void plWGPUDevice::SetMaxAnsiotropy(uint8_t maxAnsiotropy)
{
    // As plMetalDevice::SetMaxAnsiotropy: all four clamp variants, picked by the layer's clamp
    // flags. The filters are D3D9's (plDXPipeline::IInitDeviceState): linear, linear mips,
    // anisotropic minification when anisotropy is on.
    if (maxAnsiotropy == 0)
        maxAnsiotropy = 1;

    if (fSamplerStates[0] != nullptr) {
        ReleaseSamplerStates();
    }

    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.maxAnisotropy = std::min<uint16_t>(maxAnsiotropy, 16);
    desc.minFilter = WGPUFilterMode_Linear;
    desc.magFilter = WGPUFilterMode_Linear;
    desc.mipmapFilter = WGPUMipmapFilterMode_Linear;

    const WGPUAddressMode modes[4][2] = {
        {WGPUAddressMode_Repeat, WGPUAddressMode_Repeat},          // 0
        {WGPUAddressMode_ClampToEdge, WGPUAddressMode_Repeat},     // kClampTextureU
        {WGPUAddressMode_Repeat, WGPUAddressMode_ClampToEdge},     // kClampTextureV
        {WGPUAddressMode_ClampToEdge, WGPUAddressMode_ClampToEdge} // kClampTexture
    };
    for (int i = 0; i < 4; i++) {
        desc.addressModeU = modes[i][0];
        desc.addressModeV = modes[i][1];
        desc.addressModeW = WGPUAddressMode_Repeat;
        fSamplerStates[i] = wgpuDeviceCreateSampler(fWGPUDevice, &desc);
    }
}

void plWGPUDevice::SetMSAASampleCount(uint8_t sampleCount)
{
    // As plMetalDevice::SetMSAASampleCount: the closest supported count <= the request, from the
    // mode's FSAA types (plDXPipeline::ICreateDevice).
    while (sampleCount > 1) {
        if (std::find(fFSAATypes.cbegin(), fFSAATypes.cend(), sampleCount) != fFSAATypes.cend()) {
            break;
        }
        sampleCount--;
    }
    if (sampleCount == 0)
        sampleCount = 1;

    fSampleCount = sampleCount;
}

void plWGPUDevice::ReleaseSamplerStates()
{
    for (WGPUSampler& sampler : fSamplerStates) {
        if (sampler)
            wgpuSamplerRelease(sampler);
        sampler = nullptr;
    }
}

WGPUSampler plWGPUDevice::SampleStateForClampFlags(hsGMatState::hsGMatClampFlags sampleState) const
{
    return fSamplerStates[sampleState & hsGMatState::kClampTexture];
}

void plWGPUDevice::ConfigureSurface(uint32_t width, uint32_t height)
{
    // The drawable size from the mode, as the plMetalPipeline constructor and
    // plMetalPipeline::Resize set it. The canvas backing store takes this size
    // (wgpuSurfaceConfigure sets canvas.width/height).
    WGPUSurfaceConfiguration cfg = WGPU_SURFACE_CONFIGURATION_INIT;
    cfg.device = fWGPUDevice;
    cfg.format = fFramebufferFormat;
    // CopySrc: a frame drawn straight to the canvas is copied into the front buffer.
    cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    cfg.width = width;
    cfg.height = height;
    // D3D9's back buffer is X8R8G8B8 (plDXPipeline::IResetToDefaults): the canvas ignores alpha.
    cfg.alphaMode = WGPUCompositeAlphaMode_Opaque;
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(fSurface, &cfg);
    plWGPULog("surface configured {}x{} bgra8unorm opaque (render attachment, copy source)", width, height);
}

void plWGPUDevice::Clear(bool shouldClearColor, const float clearColor[4], bool shouldClearDepth, float clearDepth)
{
    if (fPrewarmWalkActive) {
        IPrewarmClear(shouldClearColor, shouldClearDepth);
        return;
    }

    /*
     D3D9 Clear (plDXPipeline::ClearRenderTarget) clears the current viewport (the rect
     of IGetClearViewPort, or the whole viewport when that is the target) of the colour
     and/or depth buffer. As in Metal (plMetalDevice::Clear) a clear is either the load
     operation of a pass that has not started, or a quad drawn in the pass in flight.
     Outside a frame there is no target to clear.
     */
    if (!fCurrentCommandBuffer && !fCurrentOffscreenCommandBuffer) {
        return;
    }

    // No depth buffer on this target: nothing to clear there.
    shouldClearDepth = shouldClearDepth && fCurrentDepthView != nullptr;
    if (!shouldClearColor && !shouldClearDepth) {
        return;
    }

    const bool fullTarget = fViewport[0] <= 0.f && fViewport[1] <= 0.f &&
                            fViewport[0] + fViewport[2] >= float(fCurrentTargetWidth) &&
                            fViewport[1] + fViewport[3] >= float(fCurrentTargetHeight);

    if (!fCurrentRenderTargetCommandEncoder && fullTarget) {
        // Render has not started yet: clear on load. Clear needs to count as a render
        // operation (plMetalDevice::Clear), so open the pass now.
        if (shouldClearColor) {
            fShouldClearColor = true;
            std::copy(clearColor, clearColor + 4, fClearColor);
        }
        if (shouldClearDepth) {
            fShouldClearDepth = true;
            fClearDepth = clearDepth;
        }
        CurrentRenderCommandEncoder();
        return;
    }

    // Mid-flight (or a partial viewport): paint the clear values over the viewport.
    WGPURenderPassEncoder pass = CurrentRenderCommandEncoder();
    if (!pass)
        return;

    plWGPUClearUniforms uniforms = {};
    std::copy(clearColor, clearColor + 4, uniforms.color);
    uniforms.depth = clearDepth;

    plWGPUClearPipelineState state(this, shouldClearColor, shouldClearDepth);
    WGPURenderPipeline       pipeline = PipelineState(&state);
    if (pipeline)
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (!BindUniforms(pass, &uniforms, sizeof(uniforms)))
        return;
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
}

void plWGPUDevice::IApplyViewport(WGPURenderPassEncoder pass)
{
    // A viewport outside the target fails in D3D9 too (WEAK_ERROR_CHECK keeps the old one);
    // WebGPU would raise a validation error, so clip it to the target.
    float x = std::max(0.f, fViewport[0]);
    float y = std::max(0.f, fViewport[1]);
    float w = std::min(fViewport[0] + fViewport[2], float(fCurrentTargetWidth)) - x;
    float h = std::min(fViewport[1] + fViewport[3], float(fCurrentTargetHeight)) - y;
    if (w <= 0.f || h <= 0.f) {
        x = y = 0.f;
        w = float(fCurrentTargetWidth);
        h = float(fCurrentTargetHeight);
    }
    wgpuRenderPassEncoderSetViewport(pass, x, y, w, h, 0.f, 1.f);
}

void plWGPUDevice::IEndRenderPass()
{
    if (fCurrentRenderTargetCommandEncoder) {
        wgpuRenderPassEncoderEnd(fCurrentRenderTargetCommandEncoder);
        wgpuRenderPassEncoderRelease(fCurrentRenderTargetCommandEncoder);
        fCurrentRenderTargetCommandEncoder = nullptr;
    }
}

void plWGPUDevice::BeginNewRenderPass()
{
    // plMetalDevice::BeginNewRenderPass. Depth loads and stores across passes (D3D9 depth
    // persists over SetRenderTarget and frames); it is cleared only on request.
    IEndRenderPass();

    WGPUCommandEncoder encoder = fCurrentRenderTarget ? fCurrentOffscreenCommandBuffer : fCurrentCommandBuffer;
    if (!encoder || !fCurrentFragmentOutputView) {
        return;
    }

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.loadOp = fShouldClearColor ? WGPULoadOp_Clear : WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {fClearColor[0], fClearColor[1], fClearColor[2], fClearColor[3]};
    if (!fCurrentRenderTarget && fSampleCount != 1) {
        // The MSAA texture keeps its samples between passes (store), and every pass resolves
        // into the output (Metal resolves only, plMetalDevice::BeginNewRenderPass, and never reopens
        // the drawable pass; D3D9 keeps the multisampled back buffer across passes).
        color.view = fCurrentFragmentMSAAOutputView;
        color.resolveTarget = fCurrentFragmentOutputView;
    } else {
        color.view = fCurrentFragmentOutputView;
    }

    WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDescriptor             desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;

    if (fCurrentDepthView) {
        depth.view = fCurrentDepthView;
        depth.depthLoadOp = fShouldClearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depth.depthStoreOp = WGPUStoreOp_Store;
        depth.depthClearValue = fClearDepth;
        // Stencil operations only on a format with a stencil aspect (the render targets'
        // z-only formats have none).
        if (fCurrentDepthFormat == WGPUTextureFormat_Depth24PlusStencil8 ||
            fCurrentDepthFormat == WGPUTextureFormat_Depth32FloatStencil8 ||
            fCurrentDepthFormat == WGPUTextureFormat_Stencil8) {
            depth.stencilLoadOp = WGPULoadOp_Load;
            depth.stencilStoreOp = WGPUStoreOp_Store;
        }
        desc.depthStencilAttachment = &depth;
    }

    fCurrentRenderTargetCommandEncoder = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    IApplyViewport(fCurrentRenderTargetCommandEncoder);

    // The clear was consumed by this pass's load operation.
    fShouldClearColor = false;
    fShouldClearDepth = false;
    fClearColor[0] = fClearColor[1] = fClearColor[2] = 0.f;
    fClearColor[3] = 1.f;
    fClearDepth = 1.f;
}

void plWGPUDevice::ISubmitOffscreen()
{
    if (fCurrentOffscreenCommandBuffer) {
        // As plMetalDevice::SetRenderTarget. Offscreen targets that are read back
        // synchronously (kIsOffscreen) need a GPU wait; that wait is a JSPI suspension, legal
        // only inside a tick.
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(fCurrentOffscreenCommandBuffer, nullptr);
        IFlushUniforms();
        wgpuQueueSubmit(fQueue, 1, &cb);
        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(fCurrentOffscreenCommandBuffer);
        fCurrentOffscreenCommandBuffer = nullptr;
    }
}

void plWGPUDevice::SetRenderTarget(plRenderTarget* target)
{
    // The prewarm walk binds nothing (BindRenderTargetSurfaces records the target for its builds), then invalidates
    // the state cache and sets the viewport as the draw does (IInvalidateState skips its texture half in a walk).
    BindRenderTargetSurfaces(target);

    fPipeline->IInvalidateState();

    SetViewport();
}

void plWGPUDevice::BindRenderTargetSurfaces(plRenderTarget* target)
{
    // plDXDevice::SetRenderTarget: a target without a ref, or with a dirty one, is (re)made; a
    // target without a colour surface renders to the screen; the target's own depth is bound,
    // or none (zDepth 0). SetRenderTarget then invalidates the pipeline's state cache and
    // re-applies the view transform's viewport.
    // Structure from plMetalDevice::SetRenderTarget: each offscreen target records into its own
    // command encoder, submitted when the target changes; the canvas pass is kept open while
    // the canvas stays bound.
    // The prewarm walk binds nothing and makes no ref: it records the target (SetRenderTarget, and the shadow blur's
    // IBlurSetRenderTarget, which calls this directly, plDXPipeline.cpp:10042-10062).
    if (fPrewarmWalkActive) {
        IPrewarmBindTarget(target);
        return;
    }
    plWGPURenderTargetRef* ref = nullptr;
    if (target != nullptr) {
        ref = (plWGPURenderTargetRef*)target->GetDeviceRef();
        if (ref == nullptr || ref->IsDirty())
            ref = (plWGPURenderTargetRef*)fPipeline->MakeRenderTargetRef(target);
    }

    WGPUTextureView   main;
    WGPUTextureView   depth;
    WGPUTextureFormat colorFormat;
    WGPUTextureFormat depthFormat;
    uint32_t          width;
    uint32_t          height;
    if (ref == nullptr || ref->fRenderView == nullptr) {
        /// Set to main screen: the unprocessed texture when the frame is post-processed
        /// (plMetalDevice::BeginNewRenderPass), else the canvas texture.
        target = nullptr;
        main = fFramePostprocessed ? fUnprocessedView : fCurrentDrawableView;
        depth = fCurrentDrawableDepthView;
        colorFormat = fFramebufferFormat;
        depthFormat = fDepthFormat;
        width = fDrawableWidth;
        height = fDrawableHeight;
    } else {
        /// Set to this target
        main = ref->fRenderView;
        depth = ref->fDepthView;
        colorFormat = ref->fFormat;
        depthFormat = depth ? ref->fDepthFormat : WGPUTextureFormat_Undefined;
        width = target->GetWidth();
        height = target->GetHeight();
    }

    if (main != fCurrentFragmentOutputView || depth != fCurrentDepthView || target != fCurrentRenderTarget) {
        IEndRenderPass();
        ISubmitOffscreen();

        fCurrentRenderTarget = target;
        if (target) {
            WGPUCommandEncoderDescriptor ed = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            ed.label = Str("plWGPU offscreen");
            fCurrentOffscreenCommandBuffer = wgpuDeviceCreateCommandEncoder(fWGPUDevice, &ed);
        }
        fCurrentFragmentOutputView = main;
        fCurrentDepthView = depth;
        fCurrentColorFormat = colorFormat;
        fCurrentDepthFormat = depthFormat;
        fCurrentTargetWidth = width;
        fCurrentTargetHeight = height;
    }
}

void plWGPUDevice::SetViewport()
{
    const plViewTransform& vt = fPipeline->GetViewTransform();
    fViewport[0] = float(vt.GetViewPortLeft());
    fViewport[1] = float(vt.GetViewPortTop());
    fViewport[2] = float(vt.GetViewPortWidth());
    fViewport[3] = float(vt.GetViewPortHeight());
    if (fCurrentRenderTargetCommandEncoder && !fPrewarmWalkActive)
        IApplyViewport(fCurrentRenderTargetCommandEncoder);
}

void plWGPUDevice::SetViewport(float x, float y, float w, float h)
{
    fViewport[0] = x;
    fViewport[1] = y;
    fViewport[2] = w;
    fViewport[3] = h;
    if (fCurrentRenderTargetCommandEncoder && !fPrewarmWalkActive)
        IApplyViewport(fCurrentRenderTargetCommandEncoder);
}

bool plWGPUDevice::BeginRender()
{
    // plMetalDevice::BeginRender tracks the rendering thread; every WebGPU object here belongs
    // to the client thread, which is the only one that renders.
    return true;
}

static uint32_t IGetBufferFormatSize(uint8_t format)
{
    // plMetalDevice.cpp, IGetBufferFormatSize
    uint32_t size = sizeof(hsPoint3) * 2 + sizeof(hsColor32) * 2; // Position and normal, and two packed colors

    switch (format & plGBufferGroup::kSkinWeightMask) {
        case plGBufferGroup::kSkinNoWeights:
            break;
        case plGBufferGroup::kSkin1Weight:
            size += sizeof(float);
            break;
        default:
            hsAssert(false, "Invalid skin weight value in IGetBufferFormatSize()");
    }

    size += sizeof(hsPoint3) * plGBufferGroup::CalcNumUVs(format);

    return size;
}

uint8_t plWGPUDevice::VertexBufferFormat(uint8_t groupFormat)
{
    // plMetalDevice::SetupVertexBufferRef's format: skinned buffers lose their weights and indices (the CPU skins
    // them into a volatile buffer).
    uint8_t format = groupFormat;
    if (format & plGBufferGroup::kSkinIndices) {
        format &= ~(plGBufferGroup::kSkinWeightMask | plGBufferGroup::kSkinIndices);
        format |= plGBufferGroup::kSkinNoWeights; // Should do nothing, but just in case...
    }
    return format;
}

void plWGPUDevice::SetupVertexBufferRef(plGBufferGroup* owner, uint32_t idx, plWGPUDevice::VertexBufferRef* vRef)
{
    // plMetalDevice::SetupVertexBufferRef
    const uint8_t format = VertexBufferFormat(owner->GetVertexFormat());

    if (owner->GetVertexFormat() & plGBufferGroup::kSkinIndices) {
        vRef->SetSkinned(true);
        vRef->SetVolatile(true);
    }

    uint32_t vertSize = IGetBufferFormatSize(format); // vertex stride
    uint32_t numVerts = owner->GetVertBufferCount(idx);

    vRef->fOwner = owner;
    vRef->fCount = numVerts;
    vRef->fVertexSize = vertSize;
    vRef->fFormat = format;
    vRef->fRefTime = 0;

    vRef->SetDirty(true);
    vRef->SetRebuiltSinceUsed(true);
    vRef->fData = nullptr;

    vRef->SetVolatile(vRef->Volatile() || owner->AreVertsVolatile());

    vRef->fIndex = idx;

    owner->SetVertexBufferRef(idx, vRef);

    vRef->UnRef();
}

void plWGPUDevice::CheckStaticVertexBuffer(plWGPUDevice::VertexBufferRef* vRef, plGBufferGroup* owner, uint32_t idx)
{
    // plMetalDevice::CheckStaticVertexBuffer
    hsAssert(!vRef->Volatile(), "Creating a managed vertex buffer for a volatile buffer ref");

    if (!vRef->GetBuffer()) {
        FillVertexBufferRef(vRef, owner, idx);

        // This is currently a no op, but this would let the buffer know it can
        // unload the system memory copy, since we have a managed version now.
        owner->PurgeVertBuffer(idx);
    }
}

void plWGPUDevice::FillVertexBufferRef(VertexBufferRef* ref, plGBufferGroup* group, uint32_t idx)
{
    // plMetalDevice::FillVertexBufferRef. Metal fills a shared-memory buffer in place; WebGPU takes the
    // filled bytes through queue.writeBuffer (sizes rounded up to four bytes).
    const uint32_t vertSize = ref->fVertexSize;
    const uint32_t vertStart = group->GetVertBufferStart(idx) * vertSize;
    const uint32_t size = group->GetVertBufferEnd(idx) * vertSize - vertStart;

    if (ref->GetBuffer()) {
        hsAssert(size <= ref->GetBufferSize(), "Allocated buffer does not fit fill data");
    }

    if (!size) {
        return;
    }

    // The buffer starts at the group's first vertex, where D3D9's starts at vertex 0
    // (plDXPipeline::IFillStaticVertexBufferRef locks at vertStart): the draws'
    // base vertex (plDXIndexBufferRef::fOffset, copied from here by ICheckDynBuffers) moves
    // the group's indices back by that start.
    ref->fOffset = -int32_t(group->GetVertBufferStart(idx));

    std::vector<uint8_t> staging(Align4(size));
    uint8_t*             buffer = staging.data();

    if (ref->fData) {
        memcpy(buffer, ref->fData + vertStart, size);
    } else {
        hsAssert(0 == vertStart, "Offsets on non-interleaved data not supported");
        hsAssert(group->GetVertBufferCount(idx) * vertSize == size, "Trailing dead space on non-interleaved data not supported");

        uint8_t* ptr = buffer;

        const uint32_t        vertSmallSize = group->GetVertexLiteStride() - sizeof(hsPoint3) * 2;
        uint8_t*              srcVPtr = group->GetVertBufferData(idx);
        plGBufferColor* const srcCPtr = group->GetColorBufferData(idx);

        const size_t numCells = group->GetNumCells(idx);
        for (size_t i = 0; i < numCells; i++) {
            plGBufferCell* cell = group->GetCell(idx, i);

            if (cell->fColorStart == uint32_t(-1)) {
                /// Interleaved, do straight copy
                memcpy(ptr, srcVPtr + cell->fVtxStart, cell->fLength * vertSize);
                ptr += cell->fLength * vertSize;
            } else {
                /// Separated, gotta interleave
                uint8_t*        tempVPtr = srcVPtr + cell->fVtxStart;
                plGBufferColor* tempCPtr = srcCPtr + cell->fColorStart;
                for (uint32_t j = 0; j < cell->fLength; j++) {
                    memcpy(ptr, tempVPtr, sizeof(hsPoint3) * 2);
                    ptr += sizeof(hsPoint3) * 2;
                    tempVPtr += sizeof(hsPoint3) * 2;

                    memcpy(ptr, &tempCPtr->fDiffuse, sizeof(uint32_t));
                    ptr += sizeof(uint32_t);
                    memcpy(ptr, &tempCPtr->fSpecular, sizeof(uint32_t));
                    ptr += sizeof(uint32_t);

                    memcpy(ptr, tempVPtr, vertSmallSize);
                    ptr += vertSmallSize;
                    tempVPtr += vertSmallSize;
                    tempCPtr++;
                }
            }
        }

        hsAssert((ptr - buffer) == size, "Didn't fill the buffer?");
    }

    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = Str("plWGPU vertices");
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    desc.size = staging.size();
    WGPUBuffer wgpuBuffer = wgpuDeviceCreateBuffer(fWGPUDevice, &desc);
    wgpuQueueWriteBuffer(fQueue, wgpuBuffer, 0, staging.data(), staging.size());
#ifdef PLASMA_TEST_HOOKS
    {
        const bool refill = fFilledVB[{group, idx}]++ != 0;
        NoteUpload(refill ? kUploadStaticVBRefill : kUploadStaticVB, staging.size(), UploadDrawableOwner(group, idx, "vb"),
                   refill ? "refill" : "first");
    }
#endif
    ref->SetBuffer(wgpuBuffer, staging.size());

    /// Unlock and clean up
    ref->SetRebuiltSinceUsed(true);
    ref->SetDirty(false);
}

void plWGPUDevice::FillVolatileVertexBufferRef(plWGPUDevice::VertexBufferRef* ref, plGBufferGroup* group, uint32_t idx)
{
    // plMetalDevice::FillVolatileVertexBufferRef
    uint8_t* dst = ref->fData;
    uint8_t* src = group->GetVertBufferData(idx);

    size_t  uvChanSize = plGBufferGroup::CalcNumUVs(group->GetVertexFormat()) * sizeof(hsPoint3);
    uint8_t numWeights = (group->GetVertexFormat() & plGBufferGroup::kSkinWeightMask) >> 4;

    for (uint32_t i = 0; i < ref->fCount; ++i) {
        inlCopy<hsPoint3>(src, dst); // pre-pos

        src += numWeights * sizeof(float); // weights

        if (group->GetVertexFormat() & plGBufferGroup::kSkinIndices)
            inlSkip<uint32_t, 1>(src); // indices

        inlCopy<hsVector3>(src, dst); // pre-normal
        inlCopy<uint32_t>(src, dst);  // diffuse
        inlCopy<uint32_t>(src, dst);  // specular

        // UVWs
        memcpy(dst, src, uvChanSize);
        src += uvChanSize;
        dst += uvChanSize;
    }
}

void plWGPUDevice::SetupIndexBufferRef(plGBufferGroup* owner, uint32_t idx, plWGPUDevice::IndexBufferRef* iRef)
{
    // plMetalDevice::SetupIndexBufferRef
    uint32_t numIndices = owner->GetIndexBufferCount(idx);
    iRef->fCount = numIndices;
    iRef->fOwner = owner;
    iRef->fIndex = idx;
    iRef->fRefTime = 0;

    iRef->SetDirty(true);
    iRef->SetRebuiltSinceUsed(true);

    owner->SetIndexBufferRef(idx, iRef);
    iRef->UnRef();

    iRef->SetVolatile(owner->AreIdxVolatile());
}

void plWGPUDevice::CheckIndexBuffer(plWGPUDevice::IndexBufferRef* iRef)
{
    // plMetalDevice::CheckIndexBuffer
    if (!iRef->GetBuffer() && iRef->fCount) {
        iRef->SetVolatile(false);

        iRef->SetDirty(true);
        iRef->SetRebuiltSinceUsed(true);
    }
}

void plWGPUDevice::FillIndexBufferRef(plWGPUDevice::IndexBufferRef* iRef, plGBufferGroup* owner, uint32_t idx)
{
    // plMetalDevice::FillIndexBufferRef. D3D9 keeps one index buffer and rewrites the dirty range
    // (plDXPipeline::IFillIndexBufferRef), so every index outside that range is still current. The
    // pool hands out a buffer last written up to three frames ago, so it gets every index of
    // the group, as the D3D9 buffer holds them (Metal copies only the dirty range into a
    // fresh buffer).
    uint32_t startIdx = owner->GetIndexBufferStart(idx);
    uint32_t fullSize = owner->GetIndexBufferCount(idx) * sizeof(uint16_t);
    uint32_t size = (owner->GetIndexBufferEnd(idx) - startIdx) * sizeof(uint16_t);

    if (!size) {
        return;
    }

    const uint8_t* source = reinterpret_cast<const uint8_t*>(owner->GetIndexBufferData(idx));
    const uint64_t bufferSize = Align4(fullSize);

    iRef->PrepareForWrite();
    WGPUBuffer indexBuffer = iRef->GetBuffer();
    if (!indexBuffer || iRef->GetBufferSize() < bufferSize) {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU indices");
        desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        desc.size = bufferSize;
        indexBuffer = wgpuDeviceCreateBuffer(fWGPUDevice, &desc);
        iRef->SetBuffer(indexBuffer, bufferSize);
    }

    if (bufferSize == fullSize) {
        wgpuQueueWriteBuffer(fQueue, indexBuffer, 0, source, fullSize);
    } else {
        std::vector<uint8_t> staging(bufferSize, 0);
        memcpy(staging.data(), source, fullSize);
        wgpuQueueWriteBuffer(fQueue, indexBuffer, 0, staging.data(), staging.size());
    }
#ifdef PLASMA_TEST_HOOKS
    {
        // A later fill is DX's dirty-range refill (plDXPipeline::CheckIndexBufferRef and
        // IFillIndexBufferRef), which copies only
        // [start, end); this buffer takes every index (see above).
        const bool refill = fFilledIB[{owner, idx}]++ != 0;
        NoteUpload(refill ? kUploadDirtyIB : kUploadStaticIB, bufferSize, UploadDrawableOwner(owner, idx, "ib"),
                   refill ? (iRef->Volatile() ? "dirty volatile" : "dirty") : "first");
    }
#endif

    iRef->SetDirty(false);
}

void plWGPUDevice::SetupTextureRef(plBitmap* img, plWGPUDevice::TextureRef* tRef)
{
    // plMetalDevice::SetupTextureRef, with D3D9's formats: see MakeTextureRef.
    tRef->fOwner = img;

    plBitmap* imageToCheck = img;

    // if it's a cubic texture, check the first face. The root img will give a false format that will cause us to decode wrong.
    plCubicEnvironmap* cubicImg = plCubicEnvironmap::ConvertNoRef(img);
    if (cubicImg) {
        imageToCheck = cubicImg->GetFace(0);
    }
    tRef->fCubic = cubicImg != nullptr;

    if (imageToCheck->IsCompressed() && fSupportsDXTTextures) {
        switch (plWGPUBlockFormatOf(imageToCheck)) {
            case plWGPUBlockFormat::kBC1:
                tRef->fFormat = WGPUTextureFormat_BC1RGBAUnorm;
                break;
            case plWGPUBlockFormat::kBC3:
                tRef->fFormat = WGPUTextureFormat_BC3RGBAUnorm;
                break;
            default:
                // D3DFMT_UNKNOWN
                hsAssert(false, "Unknown DirectX compression format");
                tRef->fFormat = WGPUTextureFormat_Undefined;
                break;
        }
    } else {
        // Everything else lands in bgra8unorm: D3DFMT_A8R8G8B8 as is, and the D3D9 formats
        // WebGPU lacks (L8, A8L8, A4R4G4B4, A1R5G5B5) expanded to it (IFormatTextureData).
        tRef->fFormat = WGPUTextureFormat_BGRA8Unorm;
    }

    tRef->SetDirty(true);

    img->SetDeviceRef(tRef);
    tRef->UnRef();
}

void plWGPUDevice::CheckTexture(plWGPUDevice::TextureRef* tRef)
{
    // plMetalDevice::CheckTexture
    if (!tRef->fTexture) {
        tRef->SetDirty(true);
    }
}

int32_t plWGPUDevice::IAllowedLevels(plMipmap* mipmap) const
{
    return plWGPUAllowedLevels(mipmap, fDoesSmallTextures);
}

plMipmap* plWGPUDevice::IUploadSource(plWGPUDevice::TextureRef* tRef, plMipmap* img)
{
    // The mipmap whose data goes to the GPU, with a reference the caller drops.
    if (img->IsCompressed() && tRef->fFormat == WGPUTextureFormat_BGRA8Unorm) {
        // No BC: decompress as Metal does (plMetalDevice::PopulateTexture, kDontCareDepth = 32-bit
        // colour; intensity maps come back as kInten8/kAInten88,
        // hsDXTSoftwareCodec::ICreateUncompressedMipmap).
        tRef->fUploadPath = plWGPUTextureRef::kUploadDecompressed;
        return hsCodecManager::Instance().CreateUncompressedMipmap(img, hsCodecManager::kDontCareDepth);
    }
    tRef->fUploadPath = img->IsCompressed() ? plWGPUTextureRef::kUploadBC : plWGPUTextureRef::kUploadUncompressed;
    img->Ref();
    return img;
}

bool plWGPUDevice::ICreateTexture(plWGPUDevice::TextureRef* tRef, uint32_t width, uint32_t height, uint32_t layers, int32_t levels)
{
    // Reuse the texture when only its contents changed (a dirty ref re-uploads; the loading
    // plate's mipmap is dirtied every 30 ms, plMipmap::CopyFrom).
    if (tRef->fTexture &&
        wgpuTextureGetWidth(tRef->fTexture) == width && wgpuTextureGetHeight(tRef->fTexture) == height &&
        wgpuTextureGetDepthOrArrayLayers(tRef->fTexture) == layers &&
        wgpuTextureGetMipLevelCount(tRef->fTexture) == uint32_t(levels + 1) &&
        wgpuTextureGetFormat(tRef->fTexture) == tRef->fFormat) {
        return true;
    }
    tRef->Release();

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    ST::string            label = tRef->fOwner && tRef->fOwner->GetKey() ? tRef->fOwner->GetKeyName() : ST_LITERAL("texture");
    desc.label = WGPUStringView{label.c_str(), label.size()};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {width, height, layers};
    desc.format = tRef->fFormat;
    desc.mipLevelCount = uint32_t(levels + 1);
    tRef->fTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
    if (!tRef->fTexture)
        return false;

    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.format = tRef->fFormat;
    vd.dimension = tRef->fCubic ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
    vd.mipLevelCount = uint32_t(levels + 1);
    vd.arrayLayerCount = layers;
    tRef->fTextureView = wgpuTextureCreateView(tRef->fTexture, &vd);
    // The level count (levels is the max level index, IAllowedLevels), as D3D9's ref keeps
    // numLevels = maxLevel + 1 (plDXPipeline::IProcessMipmapLevels, MakeTextureRef) and the
    // render target refs keep 1 (plWGPUPipeline.cpp); read only by the test hooks' rows.
    tRef->fLevels = levels + 1;
    tRef->fGpuLevels = levels + 1;
    return true;
}

void plWGPUDevice::IUploadLevels(plWGPUDevice::TextureRef* tRef, plMipmap* src, uint32_t layer, int32_t levels, plMipmap* sizeSource,
                                 std::vector<uint8_t>* level0Bytes, uint32_t cubeEdge, const CubeZeroPixels* zeroPixels,
                                 uint32_t dstLevelOffset)
{
    // Uploads levels 0..levels of src into one layer. Level sizes and data offsets come from
    // sizeSource, as D3D9 fills every cube face with the first face's level sizes
    // (plDXPipeline::IFillD3DCubeTexture). For 2D textures sizeSource is src.
    // cubeEdge, for a cube face of w x h with w > h: D3D9's cube is square, edge = face 0's
    // width (plDXPipeline::IMakeD3DCubeTexture), and the memcpy of face 0's level-i byte count
    // from the start of the locked level (IFillD3DCubeTexture) leaves the face's h_i rows at the
    // top of an edge_i x edge_i level. Rows h_i..edge_i-1, which Plasma never writes, are zero
    // bytes in the D3D9 source format here, converted by the same branch as the face's rows: one
    // edge_i x edge_i write per level. zeroPixels: on the software-decompression path, the
    // codec's decode of a zero source block and of a zero raw pixel (ICubeZeroDecode). A square
    // face (h_i == edge_i) and every 2D texture (cubeEdge 0) pad nothing.
    const uint8_t*       base = reinterpret_cast<const uint8_t*>(src->GetImage());
    const uint32_t       available = src->GetTotalSize();
    uint32_t             offset = 0;
    std::vector<uint8_t> scratch;
    const bool           bc = tRef->fFormat == WGPUTextureFormat_BC1RGBAUnorm || tRef->fFormat == WGPUTextureFormat_BC3RGBAUnorm;
    const uint32_t       blockBytes = plWGPUBlockBytes(plWGPUBlockFormatOf(src));
    // hsDXTSoftwareCodec::CreateUncompressedMipmap: levels decode as DXT blocks up to the first
    // whose width or height is not a multiple of 4; that one and the rest were stored raw.
    bool                 blockLevel = true;

    for (int32_t lvl = 0; lvl <= levels; lvl++) {
        uint32_t w, h;
        sizeSource->GetLevelPtr(uint8_t(lvl), &w, &h);
        const uint32_t levelSize = sizeSource->GetLevelSize(uint8_t(lvl));
        if ((w | h) & 0x03)
            blockLevel = false;

        // The level's rows in the texture: the face's h, or the square cube's edge_i (CreateCubeTexture halves and
        // truncates the edge per level), and the zero-padded source bytes below the face's rows.
        const uint32_t rows = cubeEdge ? std::max(1u, cubeEdge >> lvl) : h;
        const uint32_t padRows = rows > h ? rows - h : 0;
        size_t         padBytes = 0;
        if (padRows) {
            // Kept DXT levels are whole block rows (h_i >= 4, IAllowedLevels) and edge_i - h_i is a multiple of 4.
            hsAssert(!bc || (padRows & 0x03) == 0, "cube padding is not whole DXT block rows");
            padBytes = bc ? size_t(padRows / 4) * ((w + 3) / 4) * blockBytes : size_t(padRows) * w * (src->GetPixelSize() / 8);
        }

        // The level's bytes, bounded by what this face holds.
        scratch.assign(levelSize + padBytes, 0);
        if (offset < available)
            memcpy(scratch.data(), base + offset, std::min(levelSize, available - offset));
        if (padBytes && zeroPixels) {
            // Below the face's rows, the codec's own decode of zero source bytes of this level's kind
            const std::vector<uint8_t>& px = blockLevel ? zeroPixels->block : zeroPixels->raw;
            for (size_t at = levelSize; px.size() && at + px.size() <= scratch.size(); at += px.size())
                memcpy(scratch.data() + at, px.data(), px.size());
        }
        if (offset + levelSize > available) {
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                   "texture {} layer {} level {}: {} bytes past the end of its data (face sizes differ from face 0)",
                                   tRef->fOwner ? tRef->fOwner->GetKeyName() : ST_LITERAL("?"), layer, lvl,
                                   offset + levelSize - available);
        }
        offset += levelSize;

        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tRef->fTexture;
        dst.mipLevel = uint32_t(lvl) + dstLevelOffset;
        dst.origin = {0, 0, layer};
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        WGPUExtent3D              size = {w, rows, 1};

        if (bc) {
            // D3DFMT_DXT1/DXT5 blocks as they are (plDXPipeline::IGetD3DTextureFormat).
            layout.bytesPerRow = ((w + 3) / 4) * blockBytes;
            layout.rowsPerImage = (rows + 3) / 4;
            wgpuQueueWriteTexture(fQueue, &dst, scratch.data(), scratch.size(), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
            NoteUpload(kUploadTexture, scratch.size(), tRef->fOwner && tRef->fOwner->GetKey() ? tRef->fOwner->GetKey()->GetUoid().StringIze() : ST_LITERAL("(unkeyed)"),
                       fUploadTextureReason ? fUploadTextureReason : "device");
#endif
            continue;
        }

        // Uncompressed: D3D9's format and data, expanded to bgra8unorm (plWGPUFormatTexels).
        const uint32_t           numPix = w * rows;
        std::vector<plWGPUBGRA8> out(numPix);
        plWGPUFormatTexels(src, scratch.data(), numPix, out.data());

        layout.bytesPerRow = w * 4;
        layout.rowsPerImage = rows;
        wgpuQueueWriteTexture(fQueue, &dst, out.data(), out.size() * sizeof(plWGPUBGRA8), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
        NoteUpload(kUploadTexture, out.size() * sizeof(plWGPUBGRA8), tRef->fOwner && tRef->fOwner->GetKey() ? tRef->fOwner->GetKey()->GetUoid().StringIze() : ST_LITERAL("(unkeyed)"),
                   fUploadTextureReason ? fUploadTextureReason : "device");
#endif
        if (lvl == 0 && level0Bytes) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(out.data());
            level0Bytes->assign(bytes, bytes + out.size() * sizeof(plWGPUBGRA8));
        }
    }
}

void plWGPUDevice::IGenerateAutoMipmaps(plWGPUDevice::TextureRef* tRef, const std::vector<uint8_t>& level0, uint32_t width,
                                        uint32_t height, int32_t levels)
{
    // "Sublevel generation from this point on is completely transparent to the application.
    // Only the top texture level is accessible to the application" (Automatic Generation of
    // Mipmaps (Direct3D 9)); the default filter is D3DTEXF_LINEAR (SetAutoGenFilterType). Each
    // level is the 2x2 box of the one above, rounded to nearest (the filter is the driver's
    // choice). Level sizes halve and truncate (CreateTexture).
    std::vector<uint8_t> prev = level0;
    uint32_t             pw = width, ph = height;
    for (int32_t lvl = 1; lvl <= levels; lvl++) {
        const uint32_t       w = std::max<uint32_t>(1, pw >> 1);
        const uint32_t       h = std::max<uint32_t>(1, ph >> 1);
        std::vector<uint8_t> cur(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; y++) {
            const uint32_t y0 = std::min(2 * y, ph - 1), y1 = std::min(2 * y + 1, ph - 1);
            for (uint32_t x = 0; x < w; x++) {
                const uint32_t x0 = std::min(2 * x, pw - 1), x1 = std::min(2 * x + 1, pw - 1);
                for (uint32_t c = 0; c < 4; c++) {
                    const uint32_t sum = prev[(size_t(y0) * pw + x0) * 4 + c] + prev[(size_t(y0) * pw + x1) * 4 + c] +
                                         prev[(size_t(y1) * pw + x0) * 4 + c] + prev[(size_t(y1) * pw + x1) * 4 + c];
                    cur[(size_t(y) * w + x) * 4 + c] = uint8_t((sum + 2) / 4);
                }
            }
        }
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tRef->fTexture;
        dst.mipLevel = uint32_t(lvl);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = w * 4;
        layout.rowsPerImage = h;
        WGPUExtent3D size = {w, h, 1};
        wgpuQueueWriteTexture(fQueue, &dst, cur.data(), cur.size(), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
        NoteUpload(kUploadTexture, cur.size(), tRef->fOwner && tRef->fOwner->GetKey() ? tRef->fOwner->GetKey()->GetUoid().StringIze() : ST_LITERAL("(unkeyed)"),
                   fUploadTextureReason ? fUploadTextureReason : "device");
#endif
        prev.swap(cur);
        pw = w;
        ph = h;
    }
}

void plWGPUDevice::MakeTextureRef(plWGPUDevice::TextureRef* tRef, plMipmap* img)
{
    // plMetalDevice::MakeTextureRef with D3D9's formats and levels. An image-less mipmap is not
    // refused as in Metal: it takes the branch below the 4x4 check.
    if (img->GetFlags() & plMipmap::kBumpEnvMap) {
        // D3DFMT_V8U8/L6V5U5 signed bump maps (plDXPipeline::IGetD3DTextureFormat) are not
        // supported: the texture is not uploaded. The only setter of the flag,
        // plDynaDecalMgr::IConvertToEnvMap, runs only from plDynaDecalMgr::ConvertToEnvMap,
        // which nothing calls.
        hsAssert(false, "bump-environment (kBumpEnvMap) textures are not supported");
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                               "texture {}: bump-environment (kBumpEnvMap) textures are not supported; not uploaded",
                               img->GetKeyName());
        tRef->SetDirty(false);
        return;
    }

    // A BC texture needs a top level in whole 4x4 blocks; D3D9 asserts on such data
    // ("How was this ever compressed?"); decompress it instead.
    if (tRef->fFormat != WGPUTextureFormat_BGRA8Unorm && ((img->GetWidth() | img->GetHeight()) & 3)) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "texture {}: compressed {}x{} is not whole 4x4 blocks; decompressed",
                               img->GetKeyName(), img->GetWidth(), img->GetHeight());
        tRef->fFormat = WGPUTextureFormat_BGRA8Unorm;
    }

    // plDXPipeline::MakeTextureRef flags the ref of a kAutoGenMipmap mipmap (set by
    // plDynamicTextMap::Create) and the texture is created with D3DUSAGE_AUTOGENMIPMAP
    // (IGetD3DTextureUsage, IMakeD3DTexture): the driver keeps a full chain below the one level
    // the application fills. Only uncompressed one-level data is generated here; generating the
    // chain of compressed or multi-level data with the flag is not supported (it is uploaded as
    // it is).
    auto autoGenLevels = [this, tRef, img](uint32_t width, uint32_t height, int32_t& levels) {
        bool autoGen = false;
        if (img->GetFlags() & plMipmap::kAutoGenMipmap) {
            if (levels == 0 && tRef->fFormat == WGPUTextureFormat_BGRA8Unorm) {
                uint32_t full = 0;
                for (uint32_t d = std::max(width, height); d > 1; d >>= 1)
                    full++;
                levels = int32_t(full);
                autoGen = levels > 0;
            } else if (fAutoGenMultiLevel++ < 20) {
                plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                       "texture {}: kAutoGenMipmap with {} levels, format {}: generating its chain is not "
                                       "supported; uploaded as it is",
                                       img->GetKeyName(), levels + 1, int(tRef->fFormat));
            }
        }
        return autoGen;
    };

    if (!img->GetImage()) {
        // D3D9 makes the ref of an image-less mipmap like any other (plDXPipeline::MakeTextureRef):
        // IProcessMipmapLevels gives numLevels = GetNumLevels() and totalSize 0 (a plDynamicTextMap
        // before plDynamicTextMap::IIsValid), IGetPixelScratch(0) gives no data, IReloadTexture
        // creates the fMaxWidth x fMaxHeight = GetWidth() x GetHeight() texture and IFillD3DTexture
        // logs and returns without writing it; then SetDirty(false). So: the shape the filled path
        // would take (IAllowedLevels reads only the header and the kAutoGenMipmap block above), no
        // upload, no generated chain; the never-written texture reads WebGPU's zero initialisation.
        // IUploadSource is not called (its decompression path would read the missing image).
        tRef->fUploadPath = img->IsCompressed() ? plWGPUTextureRef::kUploadBC : plWGPUTextureRef::kUploadUncompressed;
        int32_t levels = IAllowedLevels(img);
        autoGenLevels(img->GetWidth(), img->GetHeight(), levels);
        if (ICreateTexture(tRef, img->GetWidth(), img->GetHeight(), 1, levels)) {
            // DX's line, once per ref creation (plDXPipeline::IFillD3DTexture), to the same log.
            const ST::string owner = tRef->fOwner && tRef->fOwner->GetKey() ? tRef->fOwner->GetKey()->GetUoid().GetObjectName()
                                                                            : ST_LITERAL("");
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "Unable to fill texture ref (data is nil) Owner: {}", owner);
        }
        tRef->SetDirty(false);
        return;
    }

    // The optional HD texture pack (plWGPUTexturePack): after the kBumpEnvMap, 4x4 and image-less
    // branches (the 4x4 branch may have switched fFormat to BGRA8), before IUploadSource.
    if (IUploadPackTexture(tRef, img)) {
        tRef->SetDirty(false);
        return;
    }

    plMipmap* src = IUploadSource(tRef, img);
    if (!src) {
        tRef->SetDirty(false);
        return;
    }

    int32_t    levels = IAllowedLevels(src);
    const bool autoGen = autoGenLevels(src->GetWidth(), src->GetHeight(), levels);
    if (ICreateTexture(tRef, src->GetWidth(), src->GetHeight(), 1, levels)) {
        std::vector<uint8_t> level0;
        IUploadLevels(tRef, src, 0, autoGen ? 0 : levels, src, autoGen ? &level0 : nullptr);
        if (autoGen && level0.size() == size_t(src->GetWidth()) * src->GetHeight() * 4)
            IGenerateAutoMipmaps(tRef, level0, src->GetWidth(), src->GetHeight(), levels);
    }
    src->UnRef();

    tRef->SetDirty(false);
}

bool plWGPUDevice::IUploadPackTexture(plWGPUDevice::TextureRef* tRef, plMipmap* img)
{
    // The HD texture pack's upload, from memory only: the pack was read when the Age's room
    // loaded (plWGPUTexturePack::PrepareRoom).
    if (!plWGPUTexturePack::Active())
        return false;
    if (img->ClassIndex() != plMipmap::Index() || !img->GetKey() || (img->GetFlags() & plMipmap::kAutoGenMipmap))
        return false;
    const plWGPUTexturePack::Entry* e = plWGPUTexturePack::Find(img->GetKey()->GetUoid());
    if (!e)
        return false;
    if (tRef->fFormat != WGPUTextureFormat_BC1RGBAUnorm && tRef->fFormat != WGPUTextureFormat_BC3RGBAUnorm) {
        // The no-BC path decompresses the shipped data on the CPU (IUploadSource); the pack holds BC data only.
        if (!fPackNoBCLogged)
            plWGPULog("texture pack: the device has no BC texture support; the shipped textures are used");
        fPackNoBCLogged = true;
        return false;
    }
#ifdef PLASMA_TEST_HOOKS
    if (fRowsArmedMirror)
        return false; // the rows read the shipped CPU plMipmap; never a pack texture while they record
#endif

    // Content check on every upload (a positive result is never cached). c = the TextureQuality
    // chop (plMipmap::Read): at most one level, the loaded header equal to the shipped one shifted
    // by c.
    const uint32_t   c = (img->GetWidth() == e->width && img->GetHeight() == e->height) ? 0 : (img->GetWidth() * 2 == e->width && img->GetHeight() * 2 == e->height) ? 1
                                                                                                                                                                     : 2;
    const ST::string key = img->GetKey()->GetUoid().StringIze();
    if (c > 1 || img->GetNumLevels() + c != e->numLevels || img->GetFlags() != e->flags ||
        !img->IsCompressed() || e->compression != plBitmap::kDirectXCompression || img->fDirectXInfo.fCompressionType != e->dxtType ||
        img->GetPixelSize() != e->pixelSize) {
        plWGPUTexturePack::Refuse(e, "loaded header differs from the shipped one");
        return false;
    }
    plChecksum sha(plChecksum::Type::kSHA256, img->GetTotalSize(), reinterpret_cast<const uint8_t*>(img->GetImage()));
    if (std::memcmp(sha.GetValue(), c == 0 ? e->shaAll : e->shaFrom1, 32) != 0) {
        plWGPUTexturePack::Refuse(e, "loaded data differs from the shipped data");
        return false;
    }

    // GPU levels = g + IAllowedLevels(loaded) + 1, for every rule and both values of c.
    const int32_t  allowed = IAllowedLevels(img);
    const int32_t  gpuLevels = int32_t(e->g) + allowed + 1;
    const uint32_t topW = e->hdWidth >> c, topH = e->hdHeight >> c;
    WGPULimits     limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(fWGPUDevice, &limits) == WGPUStatus_Success &&
        std::max(topW, topH) > limits.maxTextureDimension2D) {
        plWGPUTexturePack::Refuse(e, "HD size above the device's maxTextureDimension2D");
        return false;
    }
    if (e->tail == 1 && int32_t(e->packLevels) < int32_t(c) + gpuLevels) {
        plWGPUTexturePack::Refuse(e, "pack chain shorter than the GPU chain");
        return false;
    }

    tRef->fUploadPath = plWGPUTextureRef::kUploadBC; // IUploadSource, which sets it, is bypassed
    if (!ICreateTexture(tRef, topW, topH, 1, gpuLevels - 1))
        return false;
    tRef->fLevels = allowed + 1; // the shipped D3D9 count (plDXPipeline::MakeTextureRef)
    tRef->fGpuLevels = gpuLevels;

    const uint32_t blockBytes = plWGPUBlockBytes(plWGPUBlockFormatOf(img));
    auto           writePackLevel = [&](uint32_t packLevel, uint32_t dstLevel, uint32_t w, uint32_t h) {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tRef->fTexture;
        dst.mipLevel = dstLevel;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = ((w + 3) / 4) * blockBytes;
        layout.rowsPerImage = (h + 3) / 4;
        WGPUExtent3D size = {w, h, 1};
        wgpuQueueWriteTexture(fQueue, &dst, e->levelData[packLevel], e->levelSize[packLevel], &layout, &size);
#ifdef PLASMA_TEST_HOOKS
        NoteUpload(kUploadTexture, e->levelSize[packLevel], key, "pack");
#endif
    };

    uint32_t dstLevel = 0;
    if (e->tail == 0) {
        // Rules 1 and 2: HD levels c..g-1, then shipped level 0 (the pack's verified copy) when c = 1, then the loaded
        // shipped levels through the shipped path's own upload.
        for (uint32_t k = c; k < e->g; k++, dstLevel++)
            writePackLevel(k, dstLevel, std::max(1u, e->hdWidth >> k), std::max(1u, e->hdHeight >> k));
        if (c == 1)
            writePackLevel(e->g, dstLevel++, e->width, e->height);
        IUploadLevels(tRef, img, 0, allowed, img, nullptr, 0, nullptr, dstLevel);
    } else {
        // Rule 3: the pack chain c..c+gpuLevels-1 (for c = 1 pack level 0 is dropped, nothing else).
        for (int32_t k = 0; k < gpuLevels; k++, dstLevel++) {
            const uint32_t pk = c + uint32_t(k);
            writePackLevel(pk, dstLevel, std::max(1u, e->hdWidth >> pk), std::max(1u, e->hdHeight >> pk));
        }
    }
    plWGPUTexturePack::fUploaded++;
    plWGPULog("texture pack upload {} rule {} {}x{} gpu levels {} (D3D9 levels {}) chop {}", key, e->rule, topW, topH,
              gpuLevels, allowed + 1, c);
    return true;
}

void plWGPUDevice::MakeCubicTextureRef(plWGPUDevice::TextureRef* tRef, plCubicEnvironmap* img)
{
    // plMetalDevice::MakeCubicTextureRef with D3D9's level chain and face data
    // (plDXPipeline::IMakeCubicTextureRef, IFillD3DCubeTexture). D3D9 makes a cube only with
    // kCapsCubicTextures, and gives it levels below the first only with kCapsMipmap and
    // kCapsCubicMipmap: the WebGPU device record has all three (plWGPUEnumerate::Enumerate), so
    // both of those tests pass.
    plMipmap* face0 = img->GetFace(0);
    if (!face0->GetImage()) {
        // D3D9 has no image-less cube path (plDXPipeline::IFillD3DCubeTexture copies without a
        // nil guard).
        return;
    }

    // D3D9's cube is square with edge = face 0's width as loaded: CreateCubeTexture(ref->fMaxWidth,
    // ...) (plDXPipeline::IMakeD3DCubeTexture), fMaxWidth = faces[0]->GetWidth()
    // (plDXPipeline::IMakeCubicTextureRef, plDXTextureRef::Set); fMaxHeight is never passed.
    // Metal takes the same edge (plMetalDevice::MakeCubicTextureRef). WebGPU rejects a cube view
    // of a non-square texture.
    const ST::string key = img->GetKey() ? img->GetKey()->GetUoid().StringIze() : ST_LITERAL("(unkeyed)");
    const uint32_t   edge = face0->GetWidth();
    const uint32_t   faceHeight = face0->GetHeight();
    if (faceHeight > edge) {
        // D3D9 would memcpy w*h bytes into a w*w level (IFillD3DCubeTexture), past the locked
        // level. Not supported: logged once per cube; nothing is uploaded.
        static std::set<ST::string> refused;
        if (refused.insert(key).second)
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                   "cube {}: face 0 is {}x{} (width < height): cube maps taller than wide are not supported; "
                                   "not uploaded",
                                   key, edge, faceHeight);
        tRef->SetDirty(false);
        return;
    }

    plMipmap* faces[6];
    for (size_t i = 0; i < 6; i++)
        faces[i] = IUploadSource(tRef, img->GetFace(i));

    // The level chain and level sizes of every face, and the face whose texels each one takes
    // (plWGPUCubeLevelFace, plWGPUCubeDataFace): for block-compressed and 8/16-bit data, face 0's
    // chain, trimmed on face 0's dimensions (IProcessMipmapLevels), never one recomputed from the
    // square edge, and each face's own texels; for 32-bit data, face 5's chain and texels on
    // every face. Decided on face 0 as loaded: D3D9 decompresses DXT faces to 16 bits for a
    // device without kCapsCompressTextures (k16BitDepth), and those take the first branch.
    plMipmap*      sizeSource = faces[plWGPUCubeLevelFace(face0)];
    const int32_t  levels = IAllowedLevels(sizeSource);
    const uint32_t sourceWidth = sizeSource->GetWidth();
    const uint32_t sourceHeight = sizeSource->GetHeight();
    if (sizeSource != faces[0]) {
        if (sourceWidth > edge || sourceHeight > edge) {
            // D3D9 creates the cube at the edge with face 5's level count (IMakeD3DCubeTexture) and
            // copies face 5's level bytes into each level (IFillD3DCubeTexture): a face 5 larger than
            // the edge has more levels than the edge's chain, which CreateCubeTexture refuses (no
            // texture), or overruns the locked levels. Not supported: logged once per cube; nothing
            // is uploaded.
            static std::set<ST::string> refused;
            if (refused.insert(key).second)
                plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                       "cube {}: face 5 is {}x{}, larger than the edge {} its levels and texels fill: not uploaded",
                                       key, sourceWidth, sourceHeight, edge);
            for (plMipmap* face : faces)
                face->UnRef();
            tRef->SetDirty(false);
            return;
        }
        for (plMipmap* face : faces) {
            if (face->GetWidth() != sourceWidth || face->GetHeight() != sourceHeight) {
                // D3D9 reformats the faces one after another into one scratch buffer that grows
                // when a face needs more room (IGetPixelScratch): the faces before such a face
                // keep a pointer to the freed buffer. Not reproduced: every face takes face 5,
                // and a face 5 narrower than the edge is written at its own size. Logged once
                // per cube.
                static std::set<ST::string> differ;
                if (differ.insert(key).second)
                    plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                           "cube {}: 32-bit faces differ in size from face 5 ({}x{}); every face takes face 5", key,
                                           sourceWidth, sourceHeight);
                break;
            }
        }
    }
    const uint32_t        padEdge = sourceWidth == edge && sourceHeight < edge ? edge : 0;
    CubeZeroPixels        zero;
    const CubeZeroPixels* zeroPixels = nullptr;
    if (padEdge && tRef->fUploadPath == plWGPUTextureRef::kUploadDecompressed) {
        // On the software-decompression path the zero rows are the codec's decode of zero source bytes
        if (ICubeZeroDecode(face0, zero))
            zeroPixels = &zero;
    }
    if (padEdge) {
        // One Uoid-keyed line per non-square cube at upload
        ST::string_stream rows;
        for (int32_t lvl = 0; lvl <= levels; lvl++)
            rows << (lvl ? " " : "") << std::max(1u, sourceHeight >> lvl) << "/" << std::max(1u, edge >> lvl);
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                               "cube {}: face {} {}x{} is not square; edge {}, {} levels, rows written/edge per level {}, path {}",
                               key, plWGPUCubeLevelFace(face0), sourceWidth, sourceHeight, edge, levels + 1, rows.to_string(),
                               tRef->fUploadPath == plWGPUTextureRef::kUploadBC ? "BC" : tRef->fUploadPath == plWGPUTextureRef::kUploadDecompressed ? "decompressed"
                                                                                                                                                    : "uncompressed");
    }
    if (ICreateTexture(tRef, edge, edge, 6, levels)) {
        static constexpr uint32_t kFaceMapping[] = {
            1, // kLeftFace   -X
            0, // kRightFace  +X
            4, // kFrontFace  +Z
            5, // kBackFace   -Z
            2, // kTopFace    +Y
            3  // kBottomFace -Y
        };
        for (uint32_t i = 0; i < 6; i++)
            IUploadLevels(tRef, faces[plWGPUCubeDataFace(face0, i)], kFaceMapping[i], levels, sizeSource, nullptr, padEdge,
                          zeroPixels);
    }
    for (plMipmap* face : faces)
        face->UnRef();

    tRef->SetDirty(false);
}

bool plWGPUDevice::ICubeZeroDecode(plMipmap* compressedFace, plWGPUDevice::CubeZeroPixels& out)
{
    // The codec's own decode of zero source bytes, obtained by running it, never hand-coded: a
    // 4x4 mipmap of the face's DXT type and flags with three levels, all bytes zero
    // (plMipmap::Create clears the image): level 0 is one DXT block (decoded by
    // UncompressMipmap), levels 1-2 (2x2, 1x1) are stored raw and translated (IXlateColorBlock),
    // as the face's own levels below the first non-multiple-of-4 one
    // (hsDXTSoftwareCodec::CreateUncompressedMipmap). Decoded as IUploadSource decodes the face
    // (kDontCareDepth).
    if (!compressedFace->IsCompressed())
        return false;
    plMipmap* zero = new plMipmap(4, 4, plMipmap::kARGB32Config, 3, plMipmap::kDirectXCompression,
                                  compressedFace->fDirectXInfo.fCompressionType);
    zero->SetFlags(compressedFace->GetFlags());
    plMipmap* dec = hsCodecManager::Instance().CreateUncompressedMipmap(zero, hsCodecManager::kDontCareDepth);
    zero->UnRef();
    if (!dec)
        return false;
    const uint32_t pixelBytes = dec->GetPixelSize() / 8;
    const uint8_t* block = dec->GetLevelPtr(0);
    const uint8_t* raw = dec->GetLevelPtr(1);
    out.block.assign(block, block + pixelBytes);
    out.raw.assign(raw, raw + pixelBytes);
    static std::set<std::pair<int, uint16_t>> logged;
    if (logged.insert({int(compressedFace->fDirectXInfo.fCompressionType), compressedFace->GetFlags()}).second) {
        ST::string_stream s;
        for (uint8_t b : out.block)
            s << ST::format("{02x}", unsigned(b));
        s << " raw ";
        for (uint8_t b : out.raw)
            s << ST::format("{02x}", unsigned(b));
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "cube padding: DXT{} flags 0x{x}, {}-bit decoded pixel: block {}",
                               int(compressedFace->fDirectXInfo.fCompressionType), compressedFace->GetFlags(),
                               dec->GetPixelSize(), s.to_string());
    }
    dec->UnRef();
    return true;
}

void plWGPUDevice::SetProjectionMatrix(const hsMatrix44& src)
{
    // plDXDevice::SetProjectionMatrix
    fMatrixProj = src;
    D3DMATRIX mat;
    IMatrix44ToD3DMatrix(mat, src);
    fD3D.SetTransform(D3DTS_PROJECTION, &mat);
}

void plWGPUDevice::SetWorldToCameraMatrix(const hsMatrix44& src)
{
    // plDXDevice::SetWorldToCameraMatrix
    hsMatrix44 inv;
    src.GetInverse(&inv);

    fMatrixW2C = src;
    fMatrixC2W = inv;
    D3DMATRIX mat;
    IMatrix44ToD3DMatrix(mat, src);
    fD3D.SetTransform(D3DTS_VIEW, &mat);
}

void plWGPUDevice::SetLocalToWorldMatrix(const hsMatrix44& src)
{
    // plDXDevice::SetLocalToWorldMatrix
    hsMatrix44 inv;
    src.GetInverse(&inv);

    fMatrixL2W = src;
    fMatrixW2L = inv;
    D3DMATRIX mat;
    IMatrix44ToD3DMatrix(mat, src);
    fD3D.SetTransform(D3DTS_WORLD, &mat);
}

bool plWGPUDevice::CreateNewCommandBuffer()
{
    // plMetalDevice::CreateNewCommandBuffer, with the canvas texture of this frame as the drawable.
    WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(fSurface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        static unsigned long sNoTexture = 0;
        if (sNoTexture++ < 5)
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "no canvas texture this frame (status {})", int(st.status));
        if (st.texture)
            wgpuTextureRelease(st.texture);
        if (st.status == WGPUSurfaceGetCurrentTextureStatus_Outdated || st.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
            ConfigureSurface(fDrawableWidth ? fDrawableWidth : fPipeline->Width(), fDrawableHeight ? fDrawableHeight : fPipeline->Height());
        return false;
    }
    plWGPUEnumerate::NoteCanvasTexture(st.texture);

    fCurrentDrawable = st.texture;
    fCurrentDrawableView = wgpuTextureCreateView(fCurrentDrawable, nullptr);
    fFramebufferFormat = wgpuTextureGetFormat(fCurrentDrawable);
    fCanvasFormatKnown = true;

    const uint32_t width = wgpuTextureGetWidth(fCurrentDrawable);
    const uint32_t height = wgpuTextureGetHeight(fCurrentDrawable);
    const bool     depthNeedsRebuild = !fCurrentDrawableDepthTexture || width != fDrawableWidth || height != fDrawableHeight ||
                                   wgpuTextureGetSampleCount(fCurrentDrawableDepthTexture) != fSampleCount;
    fDrawableWidth = width;
    fDrawableHeight = height;

    // cache the depth buffer; it persists across frames (D3D9 semantics).
    if (depthNeedsRebuild) {
        if (fCurrentDrawableDepthView)
            wgpuTextureViewRelease(fCurrentDrawableDepthView);
        if (fCurrentDrawableDepthTexture)
            wgpuTextureRelease(fCurrentDrawableDepthTexture);
        if (fCurrentFragmentMSAAOutputView)
            wgpuTextureViewRelease(fCurrentFragmentMSAAOutputView);
        if (fCurrentFragmentMSAAOutputTexture)
            wgpuTextureRelease(fCurrentFragmentMSAAOutputTexture);
        fCurrentFragmentMSAAOutputView = nullptr;
        fCurrentFragmentMSAAOutputTexture = nullptr;

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU drawable depth");
        desc.usage = WGPUTextureUsage_RenderAttachment;
        desc.size = {width, height, 1};
        desc.format = fDepthFormat;
        desc.sampleCount = fSampleCount;
        fCurrentDrawableDepthTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
        fCurrentDrawableDepthView = wgpuTextureCreateView(fCurrentDrawableDepthTexture, nullptr);

        if (fSampleCount != 1) {
            // MSAA colour output
            desc.label = Str("plWGPU drawable MSAA colour");
            desc.format = fFramebufferFormat;
            fCurrentFragmentMSAAOutputTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
            fCurrentFragmentMSAAOutputView = wgpuTextureCreateView(fCurrentFragmentMSAAOutputTexture, nullptr);
        }
    }

    // The intermediate texture of the post pass (plMetalDevice::CreateNewCommandBuffer, the
    // non-tiler branch): allocated once a gamma table exists, rebuilt with the depth buffer.
    fFramePostprocessed = NeedsPostprocessing();
    if ((fUnprocessedTexture && depthNeedsRebuild) || (fUnprocessedTexture == nullptr && fFramePostprocessed)) {
        if (fUnprocessedView)
            wgpuTextureViewRelease(fUnprocessedView);
        if (fUnprocessedTexture)
            wgpuTextureRelease(fUnprocessedTexture);
        // The bind group goes with the view it names: a new view may reuse the released
        // handle's address, so the cache must not be matched by pointer alone.
        if (fGammaBindGroup)
            wgpuBindGroupRelease(fGammaBindGroup);
        fGammaBindGroup = nullptr;
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU unprocessed output");
        // CopySrc: it is the front buffer (ReadFrontBuffer).
        desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
        desc.size = {width, height, 1};
        desc.format = fFramebufferFormat;
        fUnprocessedTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
        fUnprocessedView = wgpuTextureCreateView(fUnprocessedTexture, nullptr);
        plWGPULog("gamma: unprocessed output texture {}x{} ({.2f} MiB)", width, height,
                  double(uint64_t(width) * height * 4) / (1024.0 * 1024.0));
    }

    WGPUCommandEncoderDescriptor ed = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    ed.label = Str("plWGPU frame");
    fCurrentCommandBuffer = wgpuDeviceCreateCommandEncoder(fWGPUDevice, &ed);
    IResetArenas();

    // The frame starts on the canvas (plMetalDevice::BeginNewRenderPass's lazy
    // SetRenderTarget(nullptr)).
    fCurrentRenderTarget = nullptr;
    fCurrentFragmentOutputView = fFramePostprocessed ? fUnprocessedView : fCurrentDrawableView;
    fCurrentDepthView = fCurrentDrawableDepthView;
    fCurrentColorFormat = fFramebufferFormat;
    fCurrentDepthFormat = fDepthFormat;
    fCurrentTargetWidth = width;
    fCurrentTargetHeight = height;
    SetViewport();
    return true;
}

void plWGPUDevice::IPipelineDescriptor(const plWGPUPipelineRecord& record, plWGPUPipelineState* pipelineState,
                                       PipelineDescriptor& d)
{
    pipelineState->ConfigureVertexDescriptor(d.layouts, d.attributes);
    pipelineState->ConfigureConstants(WGPUShaderStage_Vertex, d.vertexConstants);
    pipelineState->ConfigureConstants(WGPUShaderStage_Fragment, d.fragmentConstants);

    const char*             source = pipelineState->GetShaderSource();
    WGPUShaderModule        module = ShaderModule(source, pipelineState->GetDescription());
    const plWGPUFixedState& fixed = pipelineState->GetFixedState();

    d.blend = WGPU_BLEND_STATE_INIT;
    const bool blending = pipelineState->ConfigureBlend(d.blend);
    d.target = WGPU_COLOR_TARGET_STATE_INIT;
    d.target.format = record.colorFormat;
    d.target.blend = blending ? &d.blend : nullptr;
    d.target.writeMask = pipelineState->GetWriteMask();

    d.fs = WGPU_FRAGMENT_STATE_INIT;
    d.fs.module = module;
    d.fs.entryPoint = Str(pipelineState->GetFragmentFunction());
    d.fs.constantCount = d.fragmentConstants.size();
    d.fs.constants = d.fragmentConstants.data();
    d.fs.targetCount = 1;
    d.fs.targets = &d.target;

    d.ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    d.ds.format = record.depthFormat;
    d.ds.depthWriteEnabled = fixed.depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    d.ds.depthCompare = fixed.depthCompare;

    d.desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    d.desc.label = Str(pipelineState->GetDescription());
    d.desc.layout = PipelineLayout(pipelineState->GetBindLayout());
    d.desc.vertex.module = module;
    d.desc.vertex.entryPoint = Str(pipelineState->GetVertexFunction());
    d.desc.vertex.constantCount = d.vertexConstants.size();
    d.desc.vertex.constants = d.vertexConstants.data();
    d.desc.vertex.bufferCount = d.layouts.size();
    d.desc.vertex.buffers = d.layouts.data();
    d.desc.primitive.topology = fixed.topology;
    d.desc.primitive.frontFace = fixed.frontFace;
    d.desc.primitive.cullMode = fixed.cullMode;
    d.desc.depthStencil = record.depthFormat != WGPUTextureFormat_Undefined ? &d.ds : nullptr;
    d.desc.multisample.count = record.sampleCount;
    d.desc.fragment = &d.fs;
}

WGPURenderPipeline plWGPUDevice::PipelineState(plWGPUPipelineState* pipelineState)
{
    // plMetalDevice::PipelineState (plMetalDevice.cpp:1138): a built pipeline is returned. A build in flight
    // (PrewarmPipelineStateFor) is not waited for, as plMetalDevice::PipelineState waits (:1157-1168): the port
    // cannot suspend in a frame, so the pipeline is created synchronously now, and the build's late result is
    // released when it is collected. A build still queued (PumpPipelineBuilds) leaves the queue.
    // Neither: built synchronously, as before the prewarm.
    plWGPUPipelineRecord record = {
        fCurrentDepthFormat,
        fCurrentColorFormat,
        CurrentTargetSampleCount()};
    record.state = std::shared_ptr<plWGPUPipelineState>(pipelineState->Clone());

    auto it = fNewPipelineStateMap.find(record);
    if (it != fNewPipelineStateMap.end()) {
#ifdef PLASMA_TEST_HOOKS
        // The first draw that asks for a prewarmed pipeline (the trace's kBuildUsed).
        if (auto unused = fPrewarmUnused.find(record); unused != fPrewarmUnused.end()) {
            if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                observer->OnPipelineBuild(*this, *pipelineState, plWGPUPipelineObserver::kBuildUsed, unused->second, 0.0);
            fPrewarmUnused.erase(unused);
        }
#endif
        return it->second;
    }

    if (auto queued = fPipelineQueued.find(record); queued != fPipelineQueued.end()) {
        // Queued at the draw (PumpPipelineBuilds has not issued it): the draw builds it, as
        // plMetalDevice::PipelineState builds a record neither built nor building (plMetalDevice.cpp:1171-1180), and
        // the build leaves the queue (its entry in fPipelineQueue is skipped).
#ifdef PLASMA_TEST_HOOKS
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnPipelineBuild(*this, *pipelineState, plWGPUPipelineObserver::kBuildTaken, queued->second.serial,
                                      emscripten_get_now() - queued->second.queuedMs);
#endif
        fPipelineQueued.erase(queued);
    } else if (auto build = fPipelineBuilds.find(record); build != fPipelineBuilds.end()) {
        // In flight at the draw: a synchronous duplicate (none is expected once DrainPipelineBuilds has run).
        build->second.duplicated = true;
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                               "pipeline '{}' drawn while its build was in flight: created synchronously",
                               pipelineState->GetDescription());
#ifdef PLASMA_TEST_HOOKS
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnPipelineBuild(*this, *pipelineState, plWGPUPipelineObserver::kBuildDuplicate,
                                      build->second.serial, emscripten_get_now() - build->second.startMs);
#endif
    }

    const double t0 = emscripten_get_now();
#ifdef PLASMA_TEST_HOOKS
    const bool newModule = ShaderModuleCreates(pipelineState->GetShaderSource());
#endif
    PipelineDescriptor d;
    IPipelineDescriptor(record, pipelineState, d);
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(fWGPUDevice, &d.desc);
    fNewPipelineStateMap[record] = pipeline;

    // Synchronous creation cost
    const double ms = emscripten_get_now() - t0;
#ifdef PLASMA_TEST_HOOKS
    // The test hooks' pipeline-creation trace (plWGPUPipelineObserver::OnPipelineCreated).
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnPipelineCreated(*this, *pipelineState, record.colorFormat, record.depthFormat, record.sampleCount,
                                    newModule, ms);
#endif
    if (ms > 100.0)
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "pipeline '{}' took {.1f} ms to create (over 100 ms)",
                               pipelineState->GetDescription(), ms);
    return pipeline;
}

void plWGPUDevice::SetPipeline(WGPURenderPassEncoder pass, plWGPUPipelineState& state)
{
    WGPURenderPipeline pipeline = PipelineState(&state);
    if (pipeline)
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
}

bool plWGPUDevice::PrewarmPipelineStateFor(plWGPUPipelineState* pipelineState)
{
    // plMetalDevice::PrewarmPipelineStateFor and StartPipelineBuild (plMetalDevice.cpp:1183-1201, :1083-1135): the
    // pipeline the draw would build, started asynchronously (newRenderPipelineStateWithDescriptor's completion handler
    // there, wgpuDeviceCreateRenderPipelineAsync here), on the main target. It is queued here and issued by
    // PumpPipelineBuilds or DrainPipelineBuilds.
    if (!fCanvasFormatKnown)
        return false;
    const double t0 = emscripten_get_now();
#ifdef PLASMA_TEST_HOOKS
    // The trace's record of what the walk asked for (plWGPUPipelineObserver::OnPrewarmRequest).
    const auto note = [this, pipelineState](plWGPUPipelineObserver::PrewarmRequestResult result) {
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnPrewarmRequest(*this, *pipelineState, result);
    };
#endif
    plWGPUPipelineRecord record = {fDepthFormat, fFramebufferFormat, fSampleCount};
    if (fPrewarmWalkActive) {
        if (!fWalkTargetValid) {
#ifdef PLASMA_TEST_HOOKS
            note(plWGPUPipelineObserver::kRequestNoTarget);
#endif
            return false;
        }
        record.depthFormat = fWalkDepthFormat;
        record.colorFormat = fWalkColorFormat;
        record.sampleCount = fWalkSampleCount;
    }
    record.state = std::shared_ptr<plWGPUPipelineState>(pipelineState->Clone());
    if (fNewPipelineStateMap.find(record) != fNewPipelineStateMap.end() ||
        fPipelineBuilds.find(record) != fPipelineBuilds.end() || fPipelineQueued.find(record) != fPipelineQueued.end()) {
#ifdef PLASMA_TEST_HOOKS
        note(plWGPUPipelineObserver::kRequestKnown);
#endif
        return false;
    }

    // A new burst of builds starts the pacing again from one build in flight: what one browser compiles cheaply (the
    // GUI's pipelines) says nothing about the next burst (an Age's fixed-function pipelines).
    if (fPipelineBuilds.empty() && fPipelineQueued.empty())
        fPipelineCap = 1;

    PipelineQueued queued;
    queued.generation = fGeneration;
    queued.queuedMs = t0;
#ifdef PLASMA_TEST_HOOKS
    queued.serial = ++fPipelineBuildSerial;
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnPipelineBuild(*this, *pipelineState, plWGPUPipelineObserver::kBuildStarted, queued.serial,
                                  emscripten_get_now() - t0);
    note(plWGPUPipelineObserver::kRequestStarted);
#endif
    fPipelineQueue.push_back(record);
    fPipelineQueued.emplace(std::move(record), queued);
    return true;
}

bool plWGPUDevice::IIssuePipelineBuild(const plWGPUPipelineRecord& record, const PipelineQueued& queued)
{
    // The descriptor comes from the record's own state, the clone the walk queued (plMetalDevice::StartPipelineBuild
    // builds from record.state, plMetalDevice.cpp:1095): the walk's state is gone by now.
    const double       t0 = emscripten_get_now();
    PipelineDescriptor d;
    IPipelineDescriptor(record, record.state.get(), d);

    // The callback writes only the shared result. WaitAnyOnly: it runs inside a wgpuInstanceWaitAny, the pump's
    // (timeout 0, PumpPipelineBuilds) or the drain's (DrainPipelineBuilds), or with CallbackCancelled when the
    // instance goes (IReadTexture's map), possibly after this device is gone.
    auto                                      result = std::make_shared<PipelineBuildResult>();
    WGPUCreateRenderPipelineAsyncCallbackInfo ci = WGPU_CREATE_RENDER_PIPELINE_ASYNC_CALLBACK_INFO_INIT;
    ci.mode = WGPUCallbackMode_WaitAnyOnly;
    ci.callback = [](WGPUCreatePipelineAsyncStatus status, WGPURenderPipeline pipeline, WGPUStringView message,
                     void* userdata1, void*) {
        std::unique_ptr<std::shared_ptr<PipelineBuildResult>> hold(static_cast<std::shared_ptr<PipelineBuildResult>*>(userdata1));
        PipelineBuildResult*                                  r = hold->get();
        r->done = true;
        r->status = status;
        r->pipeline = pipeline;
        if (message.data)
            r->message.assign(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length);
    };
    ci.userdata1 = new std::shared_ptr<PipelineBuildResult>(result);
    const WGPUFuture future = wgpuDeviceCreateRenderPipelineAsync(fWGPUDevice, &d.desc, ci);
    if (!future.id) {
        // The callback is never called (IRequestReadback): no build, the draw creates it synchronously. The walk's
        // request was noted as started when it was queued (PrewarmPipelineStateFor): the issue runs outside the walk,
        // with no pass to note it under.
        delete static_cast<std::shared_ptr<PipelineBuildResult>*>(ci.userdata1);
        return false;
    }

    PipelineBuild build;
    build.future = future;
    build.result = std::move(result);
    build.generation = queued.generation;
    build.startMs = queued.queuedMs;
#ifdef PLASMA_TEST_HOOKS
    build.serial = queued.serial;
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnPipelineBuild(*this, *record.state, plWGPUPipelineObserver::kBuildIssued, build.serial,
                                  emscripten_get_now() - t0);
#endif
    fPipelineBuilds.emplace(record, std::move(build));
    return true;
}

namespace
{
// The pacing's window, from its first issue to its decision (PumpPipelineBuilds): the frame loop's yields in it
// (plWGPUDevice::NoteYield), and the longest time the client thread was held, in a yield or in a frame's submit
// (NoteHeld).
uint32_t s_paceYields = 0;
double   s_paceHoldMaxMs = 0.0;
} // namespace

void plWGPUDevice::NoteYield(double waitedMs)
{
    s_paceYields++;
    s_paceHoldMaxMs = std::max(s_paceHoldMaxMs, waitedMs);
}

void plWGPUDevice::NoteHeld(double heldMs)
{
    s_paceHoldMaxMs = std::max(s_paceHoldMaxMs, heldMs);
}

void plWGPUDevice::PumpPipelineBuilds()
{
    // The finished builds first, and the error scopes of the specialised programs' synchronous creates: the timeout-0
    // wait completes every ready future without suspending (emdawnwebgpu's wgpuInstanceWaitAny, timeoutNS 0), and
    // runs their callbacks.
    if (!fPipelineBuilds.empty() || !fFixedScopeChecks.empty()) {
        std::vector<WGPUFutureWaitInfo> infos;
        infos.reserve(fPipelineBuilds.size() + 2 * fFixedScopeChecks.size());
        for (const auto& [record, build] : fPipelineBuilds) {
            WGPUFutureWaitInfo info = WGPU_FUTURE_WAIT_INFO_INIT;
            info.future = build.future;
            infos.push_back(info);
        }
        for (const FixedScopeCheck& check : fFixedScopeChecks) {
            for (const WGPUFuture& future : check.futures) {
                if (!future.id)
                    continue;
                WGPUFutureWaitInfo info = WGPU_FUTURE_WAIT_INFO_INIT;
                info.future = future;
                infos.push_back(info);
            }
        }
        wgpuInstanceWaitAny(plWGPUEnumerate::Instance(), infos.size(), infos.data(), 0);
        ICollectPipelineBuilds();
        ICollectFixedScopeChecks();
    }

    // The cap, from the window that the first issue after the last decision opened: how long the client thread was
    // held in it, in a yield (the end of its task and its resume) or in a frame's submit. A browser that compiles on
    // the GPU process's command stream holds one of them until the builds issued before it are compiled, often in the
    // second frame after the issue, not the first. So one hold over three frames at 60 Hz halves the cap at once, and
    // the cap doubles only after two yields with no hold over two frames (a frame-yield waits for the next animation
    // frame).
    constexpr uint32_t kMaxCap = 64;
    constexpr double   kFastHoldMs = 34.0;
    constexpr double   kSlowHoldMs = 50.0;
    if (fPaceIssued && s_paceYields) {
        const uint32_t oldCap = fPipelineCap;
        bool           decided = false;
        if (s_paceHoldMaxMs > kSlowHoldMs) {
            fPipelineCap = std::max(1u, fPipelineCap / 2);
            decided = true;
        } else if (s_paceYields >= 2) {
            if (s_paceHoldMaxMs <= kFastHoldMs)
                fPipelineCap = std::min(kMaxCap, fPipelineCap * 2);
            decided = true;
        }
#ifdef PLASMA_TEST_HOOKS
        if (fPipelineCap != oldCap) {
            if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                observer->OnPipelinePace(*this, oldCap, fPipelineCap, s_paceYields, s_paceHoldMaxMs, fPaceIssued,
                                         uint32_t(fPipelineQueued.size()), uint32_t(fPipelineBuilds.size()));
        }
#endif
        if (decided)
            fPaceIssued = 0;
    }

    // Then the queue, up to the cap.
    uint32_t issued = 0;
    while (fPipelineBuilds.size() < fPipelineCap && !fPipelineQueue.empty()) {
        plWGPUPipelineRecord record = std::move(fPipelineQueue.front());
        fPipelineQueue.pop_front();
        auto it = fPipelineQueued.find(record);
        if (it == fPipelineQueued.end())
            continue; // a draw took it (PipelineState)
        const PipelineQueued queued = it->second;
        fPipelineQueued.erase(it);
        if (queued.generation != fGeneration)
            continue;
        if (IIssuePipelineBuild(record, queued))
            issued++;
    }
    if (issued) {
        // The first issue after a decision opens the window: what came before it is spent.
        if (!fPaceIssued) {
            s_paceYields = 0;
            s_paceHoldMaxMs = 0.0;
        }
        fPaceIssued += issued;
    }
}

void plWGPUDevice::DrainPipelineBuilds(double& waitMs)
{
    // Every queued build is issued first: the drain stands for plMetalDevice::PipelineState's wait
    // (plMetalDevice.cpp:1150-1180), which waits for what the draws need, whatever the pacing.
    while (!fPipelineQueue.empty()) {
        plWGPUPipelineRecord record = std::move(fPipelineQueue.front());
        fPipelineQueue.pop_front();
        auto it = fPipelineQueued.find(record);
        if (it == fPipelineQueued.end())
            continue;
        const PipelineQueued queued = it->second;
        fPipelineQueued.erase(it);
        if (queued.generation == fGeneration)
            IIssuePipelineBuild(record, queued);
    }

    // The futures are copied first: no iterator is held across a wait.
    if (fPipelineBuilds.empty())
        return;
    std::vector<WGPUFuture> futures;
    futures.reserve(fPipelineBuilds.size());
    for (const auto& [record, build] : fPipelineBuilds) {
        if (!build.result->done)
            futures.push_back(build.future);
    }
    if (!futures.empty()) {
        if (!BeginGPUWait("the pipeline builds"))
            return; // they stay in flight
        for (const WGPUFuture& future : futures)
            WaitFuture(future, waitMs, "the pipeline builds");
        EndGPUWait();
    }
    ICollectPipelineBuilds();
}

void plWGPUDevice::ICollectPipelineBuilds()
{
    // The builds whose callbacks have run (inside the pump's or the drain's wgpuInstanceWaitAny): a delivered
    // pipeline goes into fNewPipelineStateMap, where the draw finds it (PipelineState).
    for (auto it = fPipelineBuilds.begin(); it != fPipelineBuilds.end();) {
        const plWGPUPipelineRecord& record = it->first;
        PipelineBuild&              build = it->second;
        PipelineBuildResult*        r = build.result.get();
        if (!r->done) {
            ++it; // still in flight, for a later pump or drain
            continue;
        }
#ifdef PLASMA_TEST_HOOKS
        plWGPUPipelineObserver*                   observer = plWGPUPipelineObserver::Get();
        plWGPUPipelineObserver::PipelineBuildStep step;
#endif
        if (r->status != WGPUCreatePipelineAsyncStatus_Success || !r->pipeline) {
            // No pipeline: the draw takes the synchronous path (a failed create stores a null pipeline there). A
            // specialised fixed-function program's failure makes its pipeline key keep the uber shader instead
            // (IFixedProgramFailed), so no draw creates the failed program again.
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "pipeline '{}' build failed (status {}): {}",
                                   record.state->GetDescription(), int(r->status), r->message.c_str());
            if (record.state->GetID() == plWGPUPipelineType::MaterialShader && build.generation == fGeneration) {
                const plWGPUFixedFunctionPipelineState* state =
                    static_cast<const plWGPUFixedFunctionPipelineState*>(record.state.get());
                if (state->GetProgram())
                    IFixedProgramFailed(IFixedPipelineKey(state->GetKey(), state->GetFixedState(), record.colorFormat,
                                                          record.depthFormat, record.sampleCount),
                                        state->GetProgram(), "build", r->message);
            }
#ifdef PLASMA_TEST_HOOKS
            step = plWGPUPipelineObserver::kBuildFailed;
#endif
        } else if (build.generation == fGeneration && fNewPipelineStateMap.find(record) == fNewPipelineStateMap.end()) {
            fNewPipelineStateMap[record] = r->pipeline;
            r->pipeline = nullptr;
#ifdef PLASMA_TEST_HOOKS
            step = plWGPUPipelineObserver::kBuildReady;
            fPrewarmUnused[record] = build.serial;
#endif
        } else {
            // A draw created it meanwhile (PipelineState), or it is from a released generation: its pipeline is
            // released with the result.
#ifdef PLASMA_TEST_HOOKS
            step = plWGPUPipelineObserver::kBuildDropped;
#endif
        }
#ifdef PLASMA_TEST_HOOKS
        if (observer)
            observer->OnPipelineBuild(*this, *record.state, step, build.serial, emscripten_get_now() - build.startMs);
#endif
        it = fPipelineBuilds.erase(it);
    }
}

void plWGPUDevice::ICollectFixedScopeChecks()
{
    // The error scopes whose two pops have answered (inside the pump's wgpuInstanceWaitAny): an error in either makes
    // the pipeline key keep the uber shader (IFixedProgramFailed). A released generation's checks are dropped.
    for (auto it = fFixedScopeChecks.begin(); it != fFixedScopeChecks.end();) {
        if (it->generation != fGeneration) {
            it = fFixedScopeChecks.erase(it);
            continue;
        }
        if (it->result->pending) {
            ++it;
            continue;
        }
        if (it->result->failed)
            IFixedProgramFailed(it->id.key, it->id.program, "create", it->result->message);
        it = fFixedScopeChecks.erase(it);
    }
}

bool plWGPUDevice::ShaderModuleCreates(const char* source) const
{
    auto it = fShaderModules.find(source);
    return it == fShaderModules.end() || it->second.pipelines >= kShaderModulePipelines;
}

WGPUShaderModule plWGPUDevice::ShaderModule(const char* source, const char* label)
{
    // Called once for each pipeline about to be created from the module (IPipelineDescriptor, the gamma pipeline).
    // plMetalDevice takes every pipeline's functions from one library (plMetalDevice.cpp:1100-1101); a WebGPU module
    // stands in for it, but one module object serves at most kShaderModulePipelines creations, and the next creation
    // gets a new module from the same text.
    // Why: Safari 27.0.1 (WebKit on Metal) adds another bounds clamp, `__wgslMin(i, (8u - 1u))`, around each dynamic
    // array index of a module every time it compiles a stage from that module object: two per pipeline created, one
    // per failed attempt. The 128th creation on one object nests 255 of them and fails ("bracket nesting level
    // exceeded maximum of 256" from the Metal compiler); so does every later one, and a draw that creates one
    // synchronously invalidates its pass. The uber fixed-function module reached it at Ercana and GreatZero. A new
    // module object from the same text starts again from none. WGSL and WebGPU set no such limit, and other adapters
    // compile a module from the same text to the same program (G-PIX). The bound holds while base + 2 * k * cap <= 256,
    // where k is the deepest nesting of dynamic indexes in one expression (1 in every source today).
    // Measured in Safari 27.0.1's own console on 2026-10-29: the message and the counts above.
    if (!ShaderModuleCreates(source)) {
        ShaderModuleEntry& entry = fShaderModules[source];
        entry.pipelines++;
        return entry.module;
    }

    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = Str(source);
#ifdef PLASMA_TEST_HOOKS
    // The harness's per-run shader salt (plWGPUPipelineObserver::SaltShaderSource); the module stays keyed by its
    // source.
    std::string salted;
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
        if (observer->SaltShaderSource(source, salted))
            wgsl.code = WGPUStringView{salted.data(), salted.size()};
    }
#endif
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &wgsl.chain;
    desc.label = Str(label);
    WGPUShaderModule   module = wgpuDeviceCreateShaderModule(fWGPUDevice, &desc);
    ShaderModuleEntry& entry = fShaderModules[source];
    if (entry.module) {
        // Pipelines already created, and builds already issued, do not need the old module object.
        wgpuShaderModuleRelease(entry.module);
        entry.renewals++;
        plWGPULog("SHADER MODULE '{}' renewed after {} pipelines (renewal {})", label, entry.pipelines, entry.renewals);
    }
    entry.module = module;
    entry.pipelines = 1;
    return module;
}

WGPUPipelineLayout plWGPUDevice::PipelineLayout(plWGPUBindLayout layout)
{
    switch (layout) {
        case plWGPUBindLayout::kUniformsOnly:
            return fUniformsOnlyLayout;
        case plWGPUBindLayout::kFixedFunction:
            return fFixedFunctionLayout;
        case plWGPUBindLayout::kShader:
            return fShaderLayout;
        default:
            return fUniformsAndTextureLayout;
    }
}

bool plWGPUDevice::plWGPUPipelineRecord::operator==(const plWGPUPipelineRecord& p) const
{
    return depthFormat == p.depthFormat &&
           colorFormat == p.colorFormat &&
           sampleCount == p.sampleCount &&
           state->operator==(*p.state);
}

size_t plWGPUDevice::plWGPUPipelineRecordHashFunction::operator()(plWGPUPipelineRecord const& s) const noexcept
{
    size_t value = std::hash<uint32_t>()(uint32_t(s.depthFormat));
    value ^= std::hash<uint32_t>()(uint32_t(s.colorFormat)) << 1;
    value ^= s.state->GetHash();
    value ^= std::hash<uint32_t>()(s.sampleCount) << 2;
    return value;
}

void plWGPUDevice::SubmitCommandBuffer()
{
    // plMetalDevice::SubmitCommandBuffer: end the pass, submit. No present call: the canvas
    // shows this frame when the client thread's task ends, and no GPU wait.
    // The time it takes goes to the builds' pacing (NoteHeld): a browser that compiles pipelines on the GPU process's
    // command stream can hold the submit until they are compiled.
    const double t0 = emscripten_get_now();
    IEndRenderPass();
    ISubmitOffscreen();

    if (fCurrentCommandBuffer && fCurrentDrawable) {
        if (fFramePostprocessed) {
            // The passes rendered (and resolved) into the unprocessed texture, which nothing
            // writes again until the next frame's passes: it is the front buffer, before the
            // ramp. Then the LUT pass into the canvas texture.
            ISetFrontBuffer(fUnprocessedTexture);
            if (fFrontBufferCopy)
                wgpuTextureRelease(fFrontBufferCopy);
            fFrontBufferCopy = nullptr;
            PostprocessIntoDrawable();
        } else {
            // Drawn straight to the canvas texture, which expires when the task ends: the
            // front buffer is a copy of it.
            const uint32_t          w = wgpuTextureGetWidth(fCurrentDrawable);
            const uint32_t          h = wgpuTextureGetHeight(fCurrentDrawable);
            const WGPUTextureFormat format = wgpuTextureGetFormat(fCurrentDrawable);
            if (!fFrontBufferCopy || w != wgpuTextureGetWidth(fFrontBufferCopy) ||
                h != wgpuTextureGetHeight(fFrontBufferCopy) || format != wgpuTextureGetFormat(fFrontBufferCopy)) {
                if (fFrontBufferCopy)
                    wgpuTextureRelease(fFrontBufferCopy);
                WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                desc.label = Str("plWGPU front buffer");
                desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
                desc.size = {w, h, 1};
                desc.format = format;
                fFrontBufferCopy = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
            }
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = fCurrentDrawable;
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = fFrontBufferCopy;
            WGPUExtent3D size = {w, h, 1};
            wgpuCommandEncoderCopyTextureToTexture(fCurrentCommandBuffer, &src, &dst, &size);
            ISetFrontBuffer(fFrontBufferCopy);
        }
    }
    if (fCurrentCommandBuffer) {
        WGPUCommandBufferDescriptor cd = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer           cb = wgpuCommandEncoderFinish(fCurrentCommandBuffer, &cd);
        IFlushUniforms();
        wgpuQueueSubmit(fQueue, 1, &cb);
        wgpuCommandBufferRelease(cb);
        wgpuCommandEncoderRelease(fCurrentCommandBuffer);
        fCurrentCommandBuffer = nullptr;
    }

    for (WGPUBindGroup group : fFrameBindGroups)
        wgpuBindGroupRelease(group);
    fFrameBindGroups.clear();

    if (fCurrentDrawableView)
        wgpuTextureViewRelease(fCurrentDrawableView);
    fCurrentDrawableView = nullptr;
    if (fCurrentDrawable)
        wgpuTextureRelease(fCurrentDrawable);
    fCurrentDrawable = nullptr;
    fCurrentFragmentOutputView = nullptr;
    fCurrentDepthView = nullptr;
    fCurrentRenderTarget = nullptr;

    // Reset the clear values for the next pass (plMetalDevice::SubmitCommandBuffer).
    fShouldClearColor = false;
    fShouldClearDepth = false;
    fClearColor[0] = fClearColor[1] = fClearColor[2] = 0.f;
    fClearColor[3] = 1.f;
    fClearDepth = 1.f;
    NoteHeld(emscripten_get_now() - t0);
}

WGPURenderPassEncoder plWGPUDevice::CurrentRenderCommandEncoder()
{
    // plMetalDevice::CurrentRenderCommandEncoder: if a pass wasn't opened, open one on the current target.
    if (!fCurrentRenderTargetCommandEncoder) {
        BeginNewRenderPass();
    }
    return fCurrentRenderTargetCommandEncoder;
}

void plWGPUDevice::IResetArenas()
{
    for (Arena& arena : fUniformArenas)
        arena.used = arena.flushed = 0;
    for (Arena& arena : fVertexArenas)
        arena.used = 0;
}

bool plWGPUDevice::IArenaAlloc(std::vector<Arena>& arenas, bool uniform, size_t size, size_t align,
                               WGPUBuffer& buffer, uint64_t& offset, WGPUBindGroup* group)
{
    // Queue writes are ordered before the frame's submit and after every earlier submit, so a
    // region is free again in the next frame; within a frame each draw takes its own region.
    // A uniform binding spans kUniformBindingSize bytes from its offset, so a uniform region
    // must leave that much room before the end of the arena.
    const uint64_t reach = uniform ? std::max<uint64_t>(Align4(size), kUniformBindingSize) : Align4(size);
    for (Arena& arena : arenas) {
        const uint64_t start = (arena.used + align - 1) & ~uint64_t(align - 1);
        if (start + reach <= arena.size) {
            arena.used = start + Align4(size);
            buffer = arena.buffer;
            offset = start;
            if (group)
                *group = arena.group;
            return true;
        }
    }

    Arena arena;
    arena.size = std::max<uint64_t>(uniform ? kUniformArenaSize : kVertexArenaSize, reach);
    if (uniform)
        arena.shadow.resize(arena.size);
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.label = Str(uniform ? "plWGPU uniform arena" : "plWGPU vertex arena");
    desc.usage = (uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Vertex) | WGPUBufferUsage_CopyDst;
    desc.size = arena.size;
    arena.buffer = wgpuDeviceCreateBuffer(fWGPUDevice, &desc);
    if (!arena.buffer)
        return false;
    if (uniform) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = arena.buffer;
        entry.offset = 0;
        entry.size = kUniformBindingSize;
        WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bd.label = Str("plWGPU uniform arena");
        bd.layout = fUniformBindGroupLayout;
        bd.entryCount = 1;
        bd.entries = &entry;
        arena.group = wgpuDeviceCreateBindGroup(fWGPUDevice, &bd);
    }
    arena.used = Align4(size);
    buffer = arena.buffer;
    offset = 0;
    if (group)
        *group = arena.group;
    arenas.push_back(arena);
    return true;
}

bool plWGPUDevice::BindUniforms(WGPURenderPassEncoder pass, const void* data, size_t size)
{
    hsAssert(size <= kUniformBindingSize, "uniform block larger than the arena binding");
    WGPUBuffer    buffer;
    uint64_t      offset;
    WGPUBindGroup group;
    if (!pass || !IArenaAlloc(fUniformArenas, true, size, kUniformAlign, buffer, offset, &group))
        return false;
    for (Arena& arena : fUniformArenas) {
        if (arena.buffer == buffer) {
            memcpy(arena.shadow.data() + offset, data, size);
            break;
        }
    }
    const uint32_t dynamicOffset = uint32_t(offset);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 1, &dynamicOffset);
    return true;
}

void plWGPUDevice::IFlushUniforms()
{
    // One write per arena of everything staged since the last flush; queue writes land before
    // the submit that follows.
    for (Arena& arena : fUniformArenas) {
        if (arena.used > arena.flushed) {
            wgpuQueueWriteBuffer(fQueue, arena.buffer, arena.flushed, arena.shadow.data() + arena.flushed,
                                 Align4(arena.used - arena.flushed));
#ifdef PLASMA_TEST_HOOKS
            NoteUpload(kUploadUniform, Align4(arena.used - arena.flushed), ST::string(), nullptr);
#endif
            arena.flushed = arena.used;
        }
    }
}

bool plWGPUDevice::BindVertexBytes(WGPURenderPassEncoder pass, uint32_t slot, const void* data, size_t size)
{
    WGPUBuffer buffer;
    uint64_t   offset;
    if (!pass || !IArenaAlloc(fVertexArenas, false, size, 4, buffer, offset, nullptr))
        return false;
    if (size % 4) {
        std::vector<uint8_t> padded(Align4(size), 0);
        memcpy(padded.data(), data, size);
        wgpuQueueWriteBuffer(fQueue, buffer, offset, padded.data(), padded.size());
    } else {
        wgpuQueueWriteBuffer(fQueue, buffer, offset, data, size);
    }
#ifdef PLASMA_TEST_HOOKS
    NoteUpload(kUploadUniform, Align4(size), ST::string(), nullptr);
#endif
    wgpuRenderPassEncoderSetVertexBuffer(pass, slot, buffer, offset, size);
    return true;
}

void plWGPUDevice::BindTexture(WGPURenderPassEncoder pass, WGPUTextureView view, WGPUSampler sampler)
{
    WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].sampler = sampler;
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bd.layout = fTextureBindGroupLayout;
    bd.entryCount = 2;
    bd.entries = entries;
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(fWGPUDevice, &bd);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, group, 0, nullptr);
    fFrameBindGroups.push_back(group);
}

//// Fixed function ////////////////////////////////////////////////////////////
// The D3D9 device state that plDXPipeline's ported material code leaves in fD3D becomes a
// WebGPU pipeline (the render states WebGPU bakes), a uniform block (the rest) and a bind
// group of the stage textures and samplers, at each draw.

namespace
{
// D3DMATRIX products and inverses in D3D9's row-vector convention (v' = v * M).
D3DMATRIX MulD3D(const D3DMATRIX& a, const D3DMATRIX& b)
{
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

D3DMATRIX InverseTransposeD3D(const D3DMATRIX& a)
{
    // The D3D9 runtime transforms normals by the inverse transpose of World*View
    // ("Camera Space Transformations").
    hsMatrix44 h;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            h.fMap[i][j] = a.m[i][j];
    h.NotIdentity();
    hsMatrix44 inv;
    h.GetInverse(&inv);
    D3DMATRIX r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = inv.fMap[j][i];
    return r;
}

void D3DColorToFloat(D3DCOLOR c, float out[4])
{
    out[0] = float((c >> 16) & 0xff) / 255.f;
    out[1] = float((c >> 8) & 0xff) / 255.f;
    out[2] = float(c & 0xff) / 255.f;
    out[3] = float((c >> 24) & 0xff) / 255.f;
}

void ColorValue(const D3DCOLORVALUE& c, float out[4])
{
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

WGPUCompareFunction CompareFunction(DWORD d3dCmp)
{
    switch (d3dCmp) {
        case D3DCMP_NEVER:
            return WGPUCompareFunction_Never;
        case D3DCMP_LESS:
            return WGPUCompareFunction_Less;
        case D3DCMP_EQUAL:
            return WGPUCompareFunction_Equal;
        case D3DCMP_LESSEQUAL:
            return WGPUCompareFunction_LessEqual;
        case D3DCMP_GREATER:
            return WGPUCompareFunction_Greater;
        case D3DCMP_NOTEQUAL:
            return WGPUCompareFunction_NotEqual;
        case D3DCMP_GREATEREQUAL:
            return WGPUCompareFunction_GreaterEqual;
        default:
            return WGPUCompareFunction_Always;
    }
}

WGPUAddressMode AddressMode(DWORD d3dAddress, uint32_t& borderCount)
{
    switch (d3dAddress) {
        case D3DTADDRESS_CLAMP:
            return WGPUAddressMode_ClampToEdge;
        case D3DTADDRESS_MIRROR:
            return WGPUAddressMode_MirrorRepeat;
        case D3DTADDRESS_BORDER:
            borderCount++;
            return WGPUAddressMode_ClampToEdge;
        default:
            return WGPUAddressMode_Repeat;
    }
}
} // namespace

namespace
{
// D3DTEXF_ANISOTROPIC minification with linear magnification and mip filters (which
// plDXPipeline::ISetAnisotropy always sets with it) -> maxAnisotropy.
uint16_t D3DSamplerMaxAnisotropy(const std::array<DWORD, 7>& key)
{
    const bool linearMag = key[3] != D3DTEXF_POINT;
    const bool linearMip = key[5] != D3DTEXF_POINT && key[5] != D3DTEXF_NONE;
    if (key[4] == D3DTEXF_ANISOTROPIC && key[6] > 1 && linearMag && linearMip)
        return uint16_t(std::min<DWORD>(key[6], 16));
    return 1;
}

std::array<DWORD, 7> D3DSamplerKey(const DWORD* ss)
{
    return {ss[D3DSAMP_ADDRESSU], ss[D3DSAMP_ADDRESSV], ss[D3DSAMP_ADDRESSW], ss[D3DSAMP_MAGFILTER],
            ss[D3DSAMP_MINFILTER], ss[D3DSAMP_MIPFILTER], ss[D3DSAMP_MAXANISOTROPY]};
}
} // namespace

WGPUSampler plWGPUDevice::ID3DSampler(DWORD stage)
{
    std::array<DWORD, 7> key = D3DSamplerKey(fD3D.fSamplerState[stage]);
    auto                 it = fD3DSamplers.find(key);
    if (it != fD3DSamplers.end())
        return it->second;

    // D3DSAMP_* -> WebGPU (plDXPipeline::IInitDeviceState, ISetAnisotropy): D3DTEXF_ANISOTROPIC
    // minification is linear filtering with maxAnisotropy; WebGPU then requires linear
    // magnification and mip filters, which plDXPipeline always sets with it.
    uint32_t              border = 0;
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.label = Str("plWGPU D3D sampler");
    desc.addressModeU = AddressMode(key[0], border);
    desc.addressModeV = AddressMode(key[1], border);
    desc.addressModeW = AddressMode(key[2], border);
    desc.magFilter = key[3] == D3DTEXF_POINT ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    desc.minFilter = key[4] == D3DTEXF_POINT ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    desc.mipmapFilter = key[5] == D3DTEXF_POINT ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
    if (key[5] == D3DTEXF_NONE) {
        // D3DTEXF_NONE: no mipmapping, the top level only.
        desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        desc.lodMaxClamp = 0.f;
    }
    desc.maxAnisotropy = D3DSamplerMaxAnisotropy(key);
    WGPUSampler sampler = wgpuDeviceCreateSampler(fWGPUDevice, &desc);
    fD3DSamplers[key] = sampler;
    if (border) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                               "D3DTADDRESS_BORDER sampler state on stage {} is not supported: clamped to the edge", stage);
    }
    return sampler;
}

WGPUBindGroup plWGPUDevice::IFixedBindGroup(const std::array<void*, 24>& key)
{
    // Cached by the views and samplers; a texture release may let a new view reuse a released
    // view's address, so any release empties the cache.
    if (fFixedBindGroupSerial != plWGPUTextureRef::fReleaseSerial || fFixedBindGroups.size() > 8192) {
        for (auto& entry : fFixedBindGroups)
            wgpuBindGroupRelease(entry.second);
        fFixedBindGroups.clear();
        fFixedBindGroupSerial = plWGPUTextureRef::fReleaseSerial;
    }
    auto it = fFixedBindGroups.find(key);
    if (it != fFixedBindGroups.end())
        return it->second;

    WGPUBindGroupEntry entries[24];
    for (uint32_t i = 0; i < 24; i++) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = i;
        if (i < 16)
            entries[i].textureView = static_cast<WGPUTextureView>(key[i]);
        else
            entries[i].sampler = static_cast<WGPUSampler>(key[i]);
    }
    WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bd.label = Str("plWGPU fixed-function textures");
    bd.layout = fFixedTextureBindGroupLayout;
    bd.entryCount = 24;
    bd.entries = entries;
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(fWGPUDevice, &bd);
    fFixedBindGroups[key] = group;
    return group;
}

void plWGPUDevice::IFixedFunctionKey(const FixedFunctionDraw& draw, plWGPUFixedFunctionKey& key, plWGPUFixedState& fixed) const
{
    const DWORD*                                         rs = fD3D.fRenderState;
    const uint8_t                                        vertexKind = draw.plateVertex ? 1 : (draw.xyzTex1 ? 2 : 0);
    const plWGPUFixedFunctionPipelineState::VertexLayout layout =
        plWGPUFixedFunctionPipelineState::Layout(draw.vertexFormat, vertexKind);

    // The stages the pass enables: up to the first COLOROP DISABLE ("Texture Blending").
    uint32_t numStages = 0;
    while (numStages < plWGPUD3DDevice::kNumStages && fD3D.fStageState[numStages][D3DTSS_COLOROP] != D3DTOP_DISABLE)
        numStages++;

    key = {};
    key.blendEnabled = rs[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
    key.srcBlend = key.blendEnabled ? uint8_t(rs[D3DRS_SRCBLEND]) : 0;
    key.dstBlend = key.blendEnabled ? uint8_t(rs[D3DRS_DESTBLEND]) : 0;
    key.blendOp = key.blendEnabled ? uint8_t(rs[D3DRS_BLENDOP]) : 0;
    key.writeMask = uint8_t(rs[D3DRS_COLORWRITEENABLE] & 0xf);
    key.stages = uint8_t(numStages);
    key.lighting = rs[D3DRS_LIGHTING] ? 1 : 0;
    // D3D9 fog: table (pixel) fog when FOGTABLEMODE is not NONE, else vertex fog when
    // FOGVERTEXMODE is not NONE ("Fog Parameters").
    if (rs[D3DRS_FOGENABLE]) {
        if (rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE)
            key.fog = 1;
        else if (rs[D3DRS_FOGVERTEXMODE] != D3DFOG_NONE)
            key.fog = 2;
    }
    if (rs[D3DRS_VERTEXBLEND] == D3DVBF_1WEIGHTS && layout.weight >= 0)
        key.vertexBlend = 1;
    key.vertexFormat = vertexKind ? 0 : draw.vertexFormat;
    key.plateVertex = vertexKind;

    fixed = plWGPUFixedState();
    fixed.topology = draw.topology;
    // D3DCULL_CW culls clockwise triangles: front faces are counter-clockwise in WebGPU's
    // terms (same window orientation), so cull the back (the plates confirm it).
    fixed.frontFace = WGPUFrontFace_CCW;
    switch (rs[D3DRS_CULLMODE]) {
        case D3DCULL_CW:
            fixed.cullMode = WGPUCullMode_Back;
            break;
        case D3DCULL_CCW:
            fixed.cullMode = WGPUCullMode_Front;
            break;
        default:
            fixed.cullMode = WGPUCullMode_None;
            break;
    }
    if (rs[D3DRS_ZENABLE]) {
        fixed.depthCompare = CompareFunction(rs[D3DRS_ZFUNC]);
        fixed.depthWrite = rs[D3DRS_ZWRITEENABLE] != 0;
    } else {
        fixed.depthCompare = WGPUCompareFunction_Always;
        fixed.depthWrite = false;
    }
}

bool plWGPUDevice::PrewarmFixedFunction(const FixedFunctionDraw& draw)
{
    // The fixed-function draw's pipeline (DrawFixedFunction) on the main target. Nothing is drawn: the walk's draw
    // carries the buffer's vertex format and no buffer (a room not drawn yet has none). Its fragment program comes from
    // the same uniforms the draw would upload, with the walk's record of the stages' texture kinds (WalkTexture).
    if (!draw.count)
        return false;
    plWGPUFixedFunctionKey key;
    plWGPUFixedState       fixed;
    IFixedFunctionKey(draw, key, fixed);
    uint32_t               program = 0;
    plWGPUFixedFragmentSig sig{};
    if (fCanvasFormatKnown && (!fPrewarmWalkActive || fWalkTargetValid)) {
        const bool       walk = fPrewarmWalkActive;
        plWGPUFFUniforms u{};
        IFixedFragmentUniforms(key, fWalkCube, u);
        program = IFixedProgram(u, key,
                                IFixedPipelineKey(key, fixed, walk ? fWalkColorFormat : fFramebufferFormat,
                                                  walk ? fWalkDepthFormat : fDepthFormat,
                                                  walk ? fWalkSampleCount : fSampleCount),
                                sig);
    }
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnFixedFunctionKey(*this, key, fixed, true, program, sig);
#endif
    plWGPUFixedFunctionPipelineState state(this, key, fixed, program,
                                           program ? fFixedProgramTexts[program - 1].c_str() : nullptr);
    return PrewarmPipelineStateFor(&state);
}

uint64_t plWGPUDevice::IFixedPipelineKey(const plWGPUFixedFunctionKey& key, const plWGPUFixedState& fixed,
                                         WGPUTextureFormat color, WGPUTextureFormat depth, uint32_t samples)
{
    hsAssert(uint32_t(color) < 128 && uint32_t(depth) < 128, "render target format id over 127");
    const uint64_t fixedBits = (uint64_t(fixed.topology) & 0x7) | ((uint64_t(fixed.cullMode) & 0x3) << 3) |
                               ((uint64_t(fixed.depthCompare) & 0xf) << 5) | (uint64_t(fixed.depthWrite) << 9);
    return key.Pack() | (fixedBits << 34) | ((uint64_t(color) & 0x7f) << 44) | ((uint64_t(depth) & 0x7f) << 51) |
           ((uint64_t(samples) & 0x7) << 58);
}

void plWGPUDevice::IFixedFragmentUniforms(const plWGPUFixedFunctionKey& key, const bool* cube, plWGPUFFUniforms& u) const
{
    const DWORD* rs = fD3D.fRenderState;
    for (uint32_t s = 0; s < key.stages && s < plWGPUD3DDevice::kNumStages; s++) {
        const DWORD*   ts = fD3D.fStageState[s];
        plWGPUFFStage& st = u.stages[s];
        st.ops[0] = ts[D3DTSS_COLOROP];
        st.ops[1] = ts[D3DTSS_COLORARG1];
        st.ops[2] = ts[D3DTSS_COLORARG2];
        st.ops[3] = ts[D3DTSS_ALPHAOP];
        st.ops2[0] = ts[D3DTSS_ALPHAARG1];
        st.ops2[1] = ts[D3DTSS_ALPHAARG2];
        st.flags[0] = cube[s] ? 1 : 0;
    }
    u.lighting[0] = rs[D3DRS_SPECULARENABLE] ? 1 : 0;
    u.fog[0] = key.fog == 1 ? rs[D3DRS_FOGTABLEMODE] : 0;
    u.fog[2] = rs[D3DRS_ALPHATESTENABLE] ? rs[D3DRS_ALPHAFUNC] : 0;
}

uint32_t plWGPUDevice::IFixedProgram(const plWGPUFFUniforms& u, const plWGPUFixedFunctionKey& key, uint64_t pipelineKey,
                                     plWGPUFixedFragmentSig& sig)
{
    sig = plWGPUFixedFragment::Signature(u, key.stages, key.fog);
    if (!fFixedFragmentTried) {
        fFixedFragmentTried = true;
        if (!fFixedFragment.Bind(plWGPUFixedFunctionPipelineState::UberSource()))
            plWGPULog("FIXED PROGRAMS: off: {}", fFixedFragment.Error());
    }
    if (!fFixedFragment.Bound() || !plWGPUEnumerate::SpecialisedFixedPrograms())
        return 0;

    uint32_t program;
    if (auto it = fFixedProgramIds.find(sig); it != fFixedProgramIds.end()) {
        program = it->second;
    } else {
        const bool admitted = plWGPUFixedFragment::InDomain(sig);
        program = 0;
        if (admitted) {
            fFixedProgramTexts.push_back(fFixedFragment.Module(sig));
            program = uint32_t(fFixedProgramTexts.size());
        } else {
            // Content uses a combination outside the fallback domain: it keeps the uber shader, said once.
            ST::string_stream stages;
            for (uint32_t s = 0; s < sig.stages && s < 8; s++) {
                const plWGPUFixedFragmentSig::Stage& st = sig.st[s];
                stages << ST::format(" [{} {x} {x} {} {} {x} {x} {}{}]", st.op, st.c1, st.c2, st.alpha, st.aop, st.a1,
                                     st.a2, st.sampled, st.cube);
            }
            plStatusLog::AddLineSF("pipeline.log", 0xffff8000,
                                   "fixed-function program outside the fallback domain, kept on the uber shader: fog {} "
                                   "mode {} spec {} func {} stages{}",
                                   sig.fog, sig.fogMode, sig.spec, sig.func, stages.to_string());
        }
        fFixedProgramIds.emplace(sig, program);
#ifdef PLASMA_TEST_HOOKS
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnFixedProgram(*this, sig, program, program ? fFixedProgramTexts[program - 1].c_str() : nullptr,
                                     admitted);
#endif
    }
    if (program && fFixedFailed.count({pipelineKey, program}))
        return 0;
    return program;
}

void plWGPUDevice::IFixedProgramFailed(uint64_t pipelineKey, uint32_t program, const char* how,
                                       const std::string& message)
{
    if (!fFixedFailed.insert({pipelineKey, program}).second)
        return;
    plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                           "fixed-function program {} failed ({}) for pipeline key 0x{x}: the draws keep the uber "
                           "shader: {}",
                           program, how, pipelineKey, message.c_str());
    plWGPULog("FIXED PROGRAM {} failed ({}) for pipeline key 0x{x}: uber shader from now on: {}", program, how,
              pipelineKey, message);
    // The draw's lookup forgets the failed pipeline, so its next draw picks program 0 (IFixedProgram).
    for (auto& map : fFixedPipelines)
        map.erase({pipelineKey, program});
}

bool plWGPUDevice::DrawFixedFunction(const FixedFunctionDraw& draw)
{
    WGPURenderPassEncoder pass = CurrentRenderCommandEncoder();
    if (!pass || (!draw.vertexBuffer && !draw.vertexBytes) || !draw.count)
        return false;

    const DWORD*                                         rs = fD3D.fRenderState;
    const uint8_t                                        vertexKind = draw.plateVertex ? 1 : (draw.xyzTex1 ? 2 : 0);
    const plWGPUFixedFunctionPipelineState::VertexLayout layout =
        plWGPUFixedFunctionPipelineState::Layout(draw.vertexFormat, vertexKind);
    plWGPUFixedFunctionKey key;
    plWGPUFixedState       fixed;
    IFixedFunctionKey(draw, key, fixed);
    const uint32_t numStages = key.stages;

    // Uniforms first: the pipeline's fragment program is chosen from them (IFixedProgram).
    plWGPUFFUniforms u{}; // zero-initialised, so the bytes no module reads are deterministic
    if (fFixedTransformSerial != fD3D.fTransformSerial) {
        for (int i = 0; i < 2; i++) {
            const D3DMATRIX wv = MulD3D(fD3D.fWorld[i], fD3D.fView);
            const D3DMATRIX n = InverseTransposeD3D(wv);
            memcpy(fFixedWorldView[i], &wv, sizeof(wv));
            memcpy(fFixedNormal[i], &n, sizeof(n));
        }
        fFixedTransformSerial = fD3D.fTransformSerial;
    }
    memcpy(u.worldView0, fFixedWorldView[0], sizeof(u.worldView0));
    memcpy(u.normal0, fFixedNormal[0], sizeof(u.normal0));
    if (key.vertexBlend) {
        memcpy(u.worldView1, fFixedWorldView[1], sizeof(u.worldView1));
        memcpy(u.normal1, fFixedNormal[1], sizeof(u.normal1));
    }
    memcpy(u.projection, &fD3D.fProjection, sizeof(u.projection));

    std::array<void*, 24> textures;
    for (uint32_t s = 0; s < plWGPUD3DDevice::kNumStages; s++) {
        textures[s] = fWhiteTextureView;
        textures[8 + s] = fWhiteCubeTextureView;
        textures[16 + s] = nullptr;
    }
    // Each stage's texture kind once, for its binding slot and its program (flags[0], IFixedFragmentUniforms).
    bool cube[plWGPUD3DDevice::kNumStages] = {};
    for (uint32_t s = 0; s < numStages; s++) {
        const DWORD*   ts = fD3D.fStageState[s];
        plWGPUFFStage& st = u.stages[s];
        st.ops2[2] = ts[D3DTSS_TEXCOORDINDEX];
        st.ops2[3] = ts[D3DTSS_TEXTURETRANSFORMFLAGS];
        st.bias[0] = fD3D.SamplerStateFloat(s, D3DSAMP_MIPMAPLODBIAS);
        st.bias[1] = st.bias[2] = st.bias[3] = 0.f;
        memcpy(u.texture[s], &fD3D.fTexture[s], sizeof(u.texture[s]));

        plWGPUTextureRef* tex = fD3D.fTextures[s];
        const bool        bound = tex && tex->fTextureView;
        cube[s] = bound && tex->fCubic;
        st.flags[1] = bound ? 1 : 0;
        st.flags[2] = st.flags[3] = 0;
        if (bound) {
            if (cube[s])
                textures[8 + s] = tex->fTextureView;
            else
                textures[s] = tex->fTextureView;
        }
        textures[16 + s] = ID3DSampler(s);
    }
    IFixedFragmentUniforms(key, cube, u);
    for (uint32_t s = numStages; s < plWGPUD3DDevice::kNumStages; s++)
        textures[16 + s] = ID3DSampler(s);

    // Lights: the enabled D3DLIGHT9s in camera space (D3D9 lights in camera space).
    uint32_t numLights = 0;
    if (key.lighting) {
        const D3DMATRIX& v = fD3D.fView;
        for (DWORD index : fD3D.fEnabledLights) {
            if (numLights == 8)
                break;
            const D3DLIGHT9& l = fD3D.fLights[index];
            plWGPUFFLight&   o = u.lights[numLights++];
            const D3DVECTOR& p = l.Position;
            for (int j = 0; j < 3; j++)
                o.position[j] = p.x * v.m[0][j] + p.y * v.m[1][j] + p.z * v.m[2][j] + v.m[3][j];
            o.position[3] = 1.f;
            const D3DVECTOR& d = l.Direction;
            float            dir[3];
            for (int j = 0; j < 3; j++)
                dir[j] = d.x * v.m[0][j] + d.y * v.m[1][j] + d.z * v.m[2][j];
            const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            for (int j = 0; j < 3; j++)
                o.direction[j] = len > 0.f ? dir[j] / len : 0.f;
            o.direction[3] = 0.f;
            ColorValue(l.Diffuse, o.diffuse);
            ColorValue(l.Specular, o.specular);
            ColorValue(l.Ambient, o.ambient);
            o.spot[0] = l.Type == D3DLIGHT_DIRECTIONAL ? 3.4e38f : l.Range;
            o.spot[1] = l.Falloff;
            o.spot[2] = std::cos(l.Theta * 0.5f);
            o.spot[3] = std::cos(l.Phi * 0.5f);
            o.atten[0] = l.Attenuation0;
            o.atten[1] = l.Attenuation1;
            o.atten[2] = l.Attenuation2;
            o.atten[3] = float(l.Type);
        }
    }

    ColorValue(fD3D.fMaterial.Diffuse, u.matDiffuse);
    ColorValue(fD3D.fMaterial.Ambient, u.matAmbient);
    ColorValue(fD3D.fMaterial.Specular, u.matSpecular);
    ColorValue(fD3D.fMaterial.Emissive, u.matEmissive);
    D3DColorToFloat(rs[D3DRS_AMBIENT], u.globalAmbient);
    D3DColorToFloat(rs[D3DRS_FOGCOLOR], u.fogColor);
    u.fogParams[0] = fD3D.RenderStateFloat(D3DRS_FOGSTART);
    u.fogParams[1] = fD3D.RenderStateFloat(D3DRS_FOGEND);
    u.fogParams[2] = fD3D.RenderStateFloat(D3DRS_FOGDENSITY);
    u.fogParams[3] = 0.f;
    D3DColorToFloat(rs[D3DRS_TEXTUREFACTOR], u.textureFactor);
    u.lighting[1] = numLights;
    u.lighting[2] = rs[D3DRS_LOCALVIEWER] ? 1 : 0;
    u.lighting[3] = rs[D3DRS_NORMALIZENORMALS] ? 1 : 0;
    u.sources[0] = rs[D3DRS_DIFFUSEMATERIALSOURCE];
    u.sources[1] = rs[D3DRS_SPECULARMATERIALSOURCE];
    u.sources[2] = rs[D3DRS_AMBIENTMATERIALSOURCE];
    u.sources[3] = rs[D3DRS_EMISSIVEMATERIALSOURCE];
    if (!rs[D3DRS_COLORVERTEX])
        u.sources[0] = u.sources[1] = u.sources[2] = u.sources[3] = D3DMCS_MATERIAL;
    u.fog[1] = key.fog == 2 ? rs[D3DRS_FOGVERTEXMODE] : 0;
    u.fog[3] = rs[D3DRS_ALPHAREF] & 0xff;
    u.vertex[0] = layout.diffuse >= 0 ? 1 : 0;
    u.vertex[1] = layout.specular >= 0 ? 1 : 0;
    u.vertex[2] = u.vertex[3] = 0;
    // D3D9 pixel centres: half a pixel of the viewport, in clip units.
    u.misc[0] = fViewport[2] > 0.f ? 1.f / fViewport[2] : 0.f;
    u.misc[1] = fViewport[3] > 0.f ? -1.f / fViewport[3] : 0.f;
    u.misc[2] = fD3D.fMaterial.Power;
    u.misc[3] = 0.f;

    // Pipeline, by packed key and the target's formats (IFixedPipelineKey) and fragment program.
    const uint64_t         pipelineKey = IFixedPipelineKey(key, fixed, fCurrentColorFormat, fCurrentDepthFormat,
                                                           CurrentTargetSampleCount());
    plWGPUFixedFragmentSig sig;
    const uint32_t         program = IFixedProgram(u, key, pipelineKey, sig);
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
        observer->OnFixedFunctionKey(*this, key, fixed, false, program, sig);
#endif
    WGPURenderPipeline pipeline;
    auto               pit = fFixedPipelines[key.plateVertex].find({pipelineKey, program});
    if (pit != fFixedPipelines[key.plateVertex].end()) {
        pipeline = pit->second;
    } else {
        plWGPUFixedFunctionPipelineState state(this, key, fixed, program,
                                               program ? fFixedProgramTexts[program - 1].c_str() : nullptr);
        if (program) {
            // A specialised program's synchronous create, inside error scopes (ICollectFixedScopeChecks): an error
            // makes this pipeline key keep the uber shader from the next draw on.
            wgpuDevicePushErrorScope(fWGPUDevice, WGPUErrorFilter_Validation);
            wgpuDevicePushErrorScope(fWGPUDevice, WGPUErrorFilter_Internal);
        }
        pipeline = PipelineState(&state);
        if (program) {
            FixedScopeCheck check;
            check.result = std::make_shared<FixedScopeResult>();
            check.id = {pipelineKey, program};
            check.plate = key.plateVertex;
            check.generation = fGeneration;
            for (WGPUFuture& future : check.futures) {
                WGPUPopErrorScopeCallbackInfo ci = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
                ci.mode = WGPUCallbackMode_WaitAnyOnly;
                ci.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
                                 void* userdata1, void*) {
                    std::unique_ptr<std::shared_ptr<FixedScopeResult>> hold(
                        static_cast<std::shared_ptr<FixedScopeResult>*>(userdata1));
                    FixedScopeResult* r = hold->get();
                    if (r->pending)
                        r->pending--;
                    if (status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError) {
                        r->failed = true;
                        if (message.data && r->message.empty())
                            r->message.assign(message.data,
                                              message.length == WGPU_STRLEN ? strlen(message.data) : message.length);
                    }
                };
                ci.userdata1 = new std::shared_ptr<FixedScopeResult>(check.result);
                future = wgpuDevicePopErrorScope(fWGPUDevice, ci);
                if (!future.id) {
                    // The callback is never called: nothing to wait for.
                    delete static_cast<std::shared_ptr<FixedScopeResult>*>(ci.userdata1);
                    check.result->pending--;
                }
            }
            if (check.result->pending)
                fFixedScopeChecks.push_back(std::move(check));
        }
        fFixedPipelines[key.plateVertex][{pipelineKey, program}] = pipeline;
    }
    if (!pipeline)
        return false;

#ifdef PLASMA_TEST_HOOKS
    if (fKeepLastDraw) {
        // The per-draw rows read the state this draw used.
        fLastDrawUniforms.resize(sizeof(u));
        memcpy(fLastDrawUniforms.data(), &u, sizeof(u));
        fLastDrawStages = numStages;
        fLastDrawFog = key.fog;
        fLastDrawValid = true;
    }
#endif

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (!BindUniforms(pass, &u, sizeof(u)))
        return false;
    wgpuRenderPassEncoderSetBindGroup(pass, 1, IFixedBindGroup(textures), 0, nullptr);
    if (draw.vertexBytes) {
        if (!BindVertexBytes(pass, 0, draw.vertexBytes, draw.vertexBytesSize))
            return false;
    } else {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, draw.vertexBuffer, 0, WGPU_WHOLE_SIZE);
    }
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, fZeroVertexBuffer, 0, WGPU_WHOLE_SIZE);
    if (draw.indexBuffer) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, draw.indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, draw.count, 1, draw.start, draw.baseVertex, 0);
    } else {
        wgpuRenderPassEncoderDraw(pass, draw.count, 1, draw.start, 0);
    }
    return true;
}

//// Shader-program draws ////////////////////////////////////////////////////////

bool plWGPUDevice::IShaderUniformsAndTextures(plWGPUShaderUniforms& u, std::array<void*, 24>& textures)
{
    const DWORD* rs = fD3D.fRenderState;

    // The register files as the programs read them: vs c0..c95 as set; ps c0..c7 clamped to
    // the ps_1_1 range ("ps_1_X registers": constants in
    // [-MaxPixelShaderValue, MaxPixelShaderValue], 1 on ps_1_1 hardware).
    memcpy(u.vs, fD3D.fVSConsts, sizeof(u.vs));
    for (int i = 0; i < plWGPUD3DDevice::kNumPSConsts; i++) {
        for (int c = 0; c < 4; c++)
            u.ps[i][c] = std::min(1.f, std::max(-1.f, fD3D.fPSConsts[i][c]));
    }
    D3DColorToFloat(rs[D3DRS_FOGCOLOR], u.fogColor);
    // D3D9 pixel centres for every draw: half a pixel of the viewport in clip units.
    u.misc[0] = fViewport[2] > 0.f ? 1.f / fViewport[2] : 0.f;
    u.misc[1] = fViewport[3] > 0.f ? -1.f / fViewport[3] : 0.f;
    u.misc[2] = u.misc[3] = 0.f;
    // The pass's composited alpha test (plDXPipeline::IHandleFirstStageBlend).
    u.state[0] = rs[D3DRS_ALPHATESTENABLE] ? rs[D3DRS_ALPHAFUNC] : 0;
    u.state[1] = rs[D3DRS_ALPHAREF] & 0xff;
    u.state[2] = u.state[3] = 0;

    // Stage N's texture and sampler state for `tex tN` ("tex - ps": texture coordinate set N,
    // the stage's sampler; plDXPipeline::IUseTextureRef set them).
    for (uint32_t s = 0; s < plWGPUD3DDevice::kNumStages; s++) {
        textures[s] = fWhiteTextureView;
        textures[8 + s] = fWhiteCubeTextureView;
        textures[16 + s] = ID3DSampler(s);
    }
    for (uint32_t s = 0; s < 4; s++) {
        plWGPUTextureRef* tex = fD3D.fTextures[s];
        const bool        bound = tex && tex->fTextureView;
        const bool        cube = bound && tex->fCubic;
        u.stageCube[s] = cube ? 1 : 0;
        u.stageBias[s] = fD3D.SamplerStateFloat(s, D3DSAMP_MIPMAPLODBIAS);
        if (bound) {
            if (cube)
                textures[8 + s] = tex->fTextureView;
            else
                textures[s] = tex->fTextureView;
        }
    }
    return true;
}

WGPURenderPipeline plWGPUDevice::IShaderPipeline(const plWGPUShaderKey& key, const plWGPUFixedState& fixed, uint32_t& missing)
{
    // Bits 0-32 the key, 33-42 the fixed state, 43-49 the colour format, 50-56 the depth
    // format, 57-59 the sample count (the fixed-function packing, shifted).
    missing = 0;
    const uint64_t fixedBits = (uint64_t(fixed.topology) & 0x7) | ((uint64_t(fixed.cullMode) & 0x3) << 3) |
                               ((uint64_t(fixed.depthCompare) & 0xf) << 5) | (uint64_t(fixed.depthWrite) << 9);
    const uint64_t pipelineKey = key.Pack() | (fixedBits << 33) | ((uint64_t(fCurrentColorFormat) & 0x7f) << 43) |
                                 ((uint64_t(fCurrentDepthFormat) & 0x7f) << 50) |
                                 ((uint64_t(CurrentTargetSampleCount()) & 0x7) << 57);
    auto it = fShaderPipelines.find(pipelineKey);
    if (it != fShaderPipelines.end())
        return it->second;
    plWGPUShaderPipelineState state(this, key, fixed, fD3D.fVertexShader, fD3D.fPixelShader);
    WGPURenderPipeline        pipeline = PipelineState(&state);
    fShaderPipelines[pipelineKey] = pipeline;
    return pipeline;
}

void plWGPUDevice::IShaderKey(const FixedFunctionDraw& draw, WGPUTextureFormat depthFormat, plWGPUShaderKey& key,
                              plWGPUFixedState& fixed) const
{
    const plWGPUShaderProgram* vs = fD3D.fVertexShader;
    const plWGPUShaderProgram* ps = fD3D.fPixelShader;
    const DWORD*               rs = fD3D.fRenderState;
    key = {};
    key.vs = uint8_t(vs->Index());
    key.ps = uint8_t(ps->Index());
    key.blendEnabled = rs[D3DRS_ALPHABLENDENABLE] ? 1 : 0;
    key.srcBlend = key.blendEnabled ? uint8_t(rs[D3DRS_SRCBLEND]) : 0;
    key.dstBlend = key.blendEnabled ? uint8_t(rs[D3DRS_DESTBLEND]) : 0;
    key.blendOp = key.blendEnabled ? uint8_t(rs[D3DRS_BLENDOP]) : 0;
    key.writeMask = uint8_t(rs[D3DRS_COLORWRITEENABLE] & 0xf);
    key.vertexFormat = draw.vertexFormat;
    // "Fog (Direct3D 9)": with a vertex shader the fog factor is the shader's oFog, used when
    // FOGENABLE is TRUE (plDXPipeline::ISetFogParameters sets FOGTABLEMODE NONE for shader
    // layers).
    key.fog = rs[D3DRS_FOGENABLE] ? 1 : 0;

    fixed = plWGPUFixedState();
    fixed.topology = draw.topology;
    fixed.frontFace = WGPUFrontFace_CCW;
    switch (rs[D3DRS_CULLMODE]) {
        case D3DCULL_CW:
            fixed.cullMode = WGPUCullMode_Back;
            break;
        case D3DCULL_CCW:
            fixed.cullMode = WGPUCullMode_Front;
            break;
        default:
            fixed.cullMode = WGPUCullMode_None;
            break;
    }
    if (rs[D3DRS_ZENABLE]) {
        fixed.depthCompare = CompareFunction(rs[D3DRS_ZFUNC]);
        fixed.depthWrite = rs[D3DRS_ZWRITEENABLE] != 0;
    } else {
        fixed.depthCompare = WGPUCompareFunction_Always;
        fixed.depthWrite = false;
    }
    if (depthFormat == WGPUTextureFormat_Undefined) {
        // A target without depth (the CompRT, zDepth 0, plWaveSet7::ICreateTransferRenderTarget): D3D9 with no depth
        // surface tests and writes nothing.
        fixed.depthCompare = WGPUCompareFunction_Always;
        fixed.depthWrite = false;
    }
}

bool plWGPUDevice::PrewarmShader(const FixedFunctionDraw& draw)
{
    // The shader-program draw's pipeline (DrawShader) on the main target: what DrawShader refuses is not built (the
    // walk's draw has no buffer, as PrewarmFixedFunction's).
    const plWGPUShaderProgram* vs = fD3D.fVertexShader;
    const plWGPUShaderProgram* ps = fD3D.fPixelShader;
    if (!draw.count || !vs || !ps || strcmp(vs->family, ps->family) != 0)
        return false;
    if (fPrewarmWalkActive && !fWalkTargetValid)
        return false;
    plWGPUShaderKey  key;
    plWGPUFixedState fixed;
    IShaderKey(draw, fPrewarmWalkActive ? fWalkDepthFormat : fDepthFormat, key, fixed);
    plWGPUShaderPipelineState state(this, key, fixed, vs, ps);
    return PrewarmPipelineStateFor(&state);
}

void plWGPUDevice::BeginPrewarmWalk()
{
    fPrewarmWalkActive = true;
    IPrewarmBindTarget(nullptr);
    // A walk starts with no texture bound (WalkTexture).
    for (bool& cube : fWalkCube)
        cube = false;
}

void plWGPUDevice::WalkTexture(uint32_t stage, bool cube)
{
    if (fPrewarmWalkActive && stage < plWGPUD3DDevice::kNumStages)
        fWalkCube[stage] = cube;
}

void plWGPUDevice::EndPrewarmWalk()
{
    IPrewarmBindTarget(nullptr);
    fPrewarmWalkActive = false;
}

void plWGPUDevice::IPrewarmBindTarget(plRenderTarget* target)
{
    // BindRenderTargetSurfaces' choice of target, as a record: a target without a colour view renders to the main
    // target; a render target's depth is its own depth view's, and it draws single-sampled (CurrentTargetSampleCount).
    // Unlike the draw, the walk makes no device ref: a pool's targets have theirs from LoadResources
    // (IMakeRenderTargetPools, IFillAvRTPool), and a target without one is skipped (its draws build at the draw).
    plWGPURenderTargetRef* ref = target ? (plWGPURenderTargetRef*)target->GetDeviceRef() : nullptr;
    fWalkTargetValid = true;
    if (target && (!ref || ref->IsDirty())) {
        fWalkTarget = target;
        fWalkTargetValid = false;
        return;
    }
    if (!ref || !ref->fRenderView) {
        fWalkTarget = nullptr;
        fWalkColorFormat = fFramebufferFormat;
        fWalkDepthFormat = fDepthFormat;
        fWalkSampleCount = fSampleCount;
        fWalkWidth = fDrawableWidth;
        fWalkHeight = fDrawableHeight;
    } else {
        fWalkTarget = target;
        fWalkColorFormat = ref->fFormat;
        fWalkDepthFormat = ref->fDepthView ? ref->fDepthFormat : WGPUTextureFormat_Undefined;
        fWalkSampleCount = 1;
        fWalkWidth = target->GetWidth();
        fWalkHeight = target->GetHeight();
    }
}

void plWGPUDevice::IPrewarmClear(bool shouldClearColor, bool shouldClearDepth)
{
    // Clear's choice, for the walk's target: nothing to clear; or the load operation of a pass that has not started
    // over the full target (no pipeline); or the clear quad, whose pipeline is started. A render target the walk
    // pushed has no pass yet (BindRenderTargetSurfaces ends the pass at a target switch). The main target's pass is
    // open in a scene frame before any render request: plClient::IDraw clears it first (ClearRenderTarget, with the
    // view's default kRenderClearColor | kRenderClearDepth), and that clear begins it.
    if (!fWalkTargetValid)
        return;
    shouldClearDepth = shouldClearDepth && fWalkDepthFormat != WGPUTextureFormat_Undefined;
    if (!shouldClearColor && !shouldClearDepth)
        return;
    const bool fullTarget = fViewport[0] <= 0.f && fViewport[1] <= 0.f && fViewport[0] + fViewport[2] >= float(fWalkWidth) &&
                            fViewport[1] + fViewport[3] >= float(fWalkHeight);
    if (fWalkTarget && fullTarget)
        return;
    plWGPUClearPipelineState state(this, shouldClearColor, shouldClearDepth);
    PrewarmPipelineStateFor(&state);
}

bool plWGPUDevice::DrawShader(const FixedFunctionDraw& draw)
{
    WGPURenderPassEncoder      pass = CurrentRenderCommandEncoder();
    const plWGPUShaderProgram* vs = fD3D.fVertexShader;
    const plWGPUShaderProgram* ps = fD3D.fPixelShader;
    if (!pass || !draw.vertexBuffer || !draw.count || !vs || !ps)
        return false;
    if (strcmp(vs->family, ps->family) != 0) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "shader draw pairs {} with {} (two families): not drawn",
                               vs->entry, ps->entry);
        return false;
    }

    plWGPUShaderKey  key;
    plWGPUFixedState fixed;
    IShaderKey(draw, fCurrentDepthFormat, key, fixed);

    uint32_t           missing;
    WGPURenderPipeline pipeline = IShaderPipeline(key, fixed, missing);
    if (!pipeline)
        return false;

    plWGPUShaderUniforms  u;
    std::array<void*, 24> textures;
    IShaderUniformsAndTextures(u, textures);

#ifdef PLASMA_TEST_HOOKS
    if (fKeepLastDraw) {
        // The shader rows read the uniforms this draw used.
        fLastShaderUniforms.resize(sizeof(u));
        memcpy(fLastShaderUniforms.data(), &u, sizeof(u));
        fLastShaderDrawValid = true;
    }
#endif

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (!BindUniforms(pass, &u, sizeof(u)))
        return false;
    wgpuRenderPassEncoderSetBindGroup(pass, 1, IFixedBindGroup(textures), 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, draw.vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, fZeroVertexBuffer, 0, WGPU_WHOLE_SIZE);
    if (draw.indexBuffer) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, draw.indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, draw.count, 1, draw.start, draw.baseVertex, 0);
    } else {
        wgpuRenderPassEncoderDraw(pass, draw.count, 1, draw.start, 0);
    }
    return true;
}

bool plWGPUDevice::RequestReadback(const ST::string& name, WGPUTexture texture, uint32_t width, uint32_t height,
                                   WGPUTextureFormat format, uint32_t layer, uint32_t bytesPerPixel)
{
    WGPUFuture                future;
    std::shared_ptr<Readback> rb = IRequestReadback(name, texture, width, height, format, layer, bytesPerPixel, future);
    if (!rb)
        return false;
    fReadbacks.push_back(rb);
    return true;
}

std::shared_ptr<plWGPUDevice::Readback> plWGPUDevice::RequestCaptureReadback(WGPUTexture texture, uint32_t width,
                                                                             uint32_t height, WGPUFuture& future)
{
    return IRequestReadback(ST_LITERAL("ExtractMipMap"), texture, width, height, WGPUTextureFormat_BGRA8Unorm, 0, 4, future);
}

std::shared_ptr<plWGPUDevice::Readback> plWGPUDevice::IRequestReadback(const ST::string& name, WGPUTexture texture,
                                                                       uint32_t width, uint32_t height, WGPUTextureFormat format,
                                                                       uint32_t layer, uint32_t bytesPerPixel, WGPUFuture& future)
{
    // The render target's commands are submitted when its target is popped (ISubmitOffscreen),
    // so a copy submitted now reads what they rendered. The bytes arrive with mapAsync's
    // callback, in a later frame (or at a wait on the returned future).
    future = WGPUFuture{};
    if (!texture || !width || !height)
        return nullptr;
    auto rb = std::make_shared<Readback>();
    rb->name = name;
    rb->width = width;
    rb->height = height;
    rb->format = format;
    rb->bytesPerPixel = bytesPerPixel;
    rb->bytesPerRow = (width * bytesPerPixel + 255) & ~uint32_t(255);

    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = Str("plWGPU readback");
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = uint64_t(rb->bytesPerRow) * height;
    rb->buffer = wgpuDeviceCreateBuffer(fWGPUDevice, &bd);
    if (!rb->buffer)
        return nullptr;

    WGPUCommandEncoderDescriptor ed = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    ed.label = Str("plWGPU readback");
    WGPUCommandEncoder       enc = wgpuDeviceCreateCommandEncoder(fWGPUDevice, &ed);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = texture;
    src.origin.z = layer; // the cube face of a cube target
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = rb->buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = rb->bytesPerRow;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D size = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &size);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(fQueue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    // The callback's own reference to the readback (shared state): an AllowSpontaneous callback runs
    // exactly once, but whenever the map ends, inside whatever call ends it (an unmap, a destroy or
    // the buffer's last release) or later on this thread's event loop when the browser settles the
    // map, possibly after every owner (fReadbacks, a pending capture, the pipeline itself) is gone.
    // The callback sets the readback's fields and then drops its reference; the last reference
    // releases the buffer (~Readback).
    WGPUBufferMapCallbackInfo ci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    ci.mode = WGPUCallbackMode_AllowSpontaneous;
    ci.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
        std::unique_ptr<std::shared_ptr<Readback>> hold(static_cast<std::shared_ptr<Readback>*>(userdata1));
        Readback*                                  r = hold->get();
        if (status != WGPUMapAsyncStatus_Success) {
            r->failed = true;
            r->done = true;
            return;
        }
        const uint8_t* p = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(r->buffer, 0, size_t(r->bytesPerRow) * r->height));
        if (p) {
            const size_t row = size_t(r->width) * r->bytesPerPixel;
            r->data.resize(row * r->height);
            for (uint32_t y = 0; y < r->height; y++)
                memcpy(r->data.data() + size_t(y) * row, p + size_t(y) * r->bytesPerRow, row);
        } else {
            r->failed = true;
        }
        wgpuBufferUnmap(r->buffer);
        r->done = true;
    };
    ci.userdata1 = new std::shared_ptr<Readback>(rb);
    future = wgpuBufferMapAsync(rb->buffer, WGPUMapMode_Read, 0, size_t(rb->bytesPerRow) * height, ci);
    // A null future is the one case in which the callback is never called (an invalid callback mode)
    if (!future.id)
        delete static_cast<std::shared_ptr<Readback>*>(ci.userdata1);
    return rb;
}

bool plWGPUDevice::TakeReadback(Readback& out)
{
    for (auto it = fReadbacks.begin(); it != fReadbacks.end(); ++it) {
        if ((*it)->done) {
            out.name = (*it)->name;
            out.width = (*it)->width;
            out.height = (*it)->height;
            out.format = (*it)->format;
            out.failed = (*it)->failed;
            out.bytesPerPixel = (*it)->bytesPerPixel;
            out.data.swap((*it)->data);
            fReadbacks.erase(it); // its map has ended, so this was the last reference: the buffer goes with it
            return true;
        }
    }
    return false;
}

// MARK: - The front buffer and the GPU waits

// The frame loop's suspension bracket for a wait from deep inside a tick, registered by the app
// (plWGPUPipeline::SetGPUWaitProcs), as plDispatch::fMsgRecieveCallback is (plDispatch.h).
bool (*plWGPUDevice::fGPUWaitBegin)(const char* how) = nullptr;
void (*plWGPUDevice::fGPUWaitEnd)() = nullptr;

void plWGPUDevice::ISetFrontBuffer(WGPUTexture frame)
{
    // A reference of its own, taken before the old one goes: frame may be the texture it already holds
    if (frame)
        wgpuTextureAddRef(frame);
    if (fFrontBuffer)
        wgpuTextureRelease(fFrontBuffer);
    fFrontBuffer = frame;
}

void plWGPUDevice::IReleaseFrontBuffer()
{
    if (fFrontBuffer)
        wgpuTextureRelease(fFrontBuffer);
    fFrontBuffer = nullptr;
    if (fFrontBufferCopy)
        wgpuTextureRelease(fFrontBufferCopy);
    fFrontBufferCopy = nullptr;
}

bool plWGPUDevice::BeginGPUWait(const char* how)
{
    // No procs: no loop to suspend, so no wait (the caller's refused-wait path)
    return fGPUWaitBegin && fGPUWaitBegin(how);
}

void plWGPUDevice::EndGPUWait()
{
    // Only after a BeginGPUWait that returned true, so the procs are registered
    fGPUWaitEnd();
}

bool plWGPUDevice::WaitFuture(WGPUFuture future, double& waitMs, const char* how)
{
    // wgpuInstanceWaitAny with a timeout suspends the tick (emwgpuWaitAny is an async import
    // under JSPI); the caller has bracketed it with BeginGPUWait/EndGPUWait, so the frame loop
    // counts the suspension like a present-yield.
    const double       t0 = emscripten_get_now();
    WGPUFutureWaitInfo info = WGPU_FUTURE_WAIT_INFO_INIT;
    info.future = future;
    const WGPUWaitStatus status = wgpuInstanceWaitAny(plWGPUEnumerate::Instance(), 1, &info, UINT64_MAX);
    waitMs += emscripten_get_now() - t0;
    if (status != WGPUWaitStatus_Success) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "{}: wgpuInstanceWaitAny status {}", how, int(status));
        return false;
    }
    return true;
}

bool plWGPUDevice::ReadFrontBuffer(std::vector<uint32_t>& texels, uint32_t& width, uint32_t& height, double& waitMs)
{
    // IDirect3DDevice9::GetFrontBufferData + LockRect (plDXPipeline::CaptureScreen): the last
    // presented frame, converted to D3DFMT_A8R8G8B8.
    waitMs = 0.0;
    if (!fFrontBuffer)
        return false;
    width = wgpuTextureGetWidth(fFrontBuffer);
    height = wgpuTextureGetHeight(fFrontBuffer);
    return IReadTexture(fFrontBuffer, width, height, wgpuTextureGetFormat(fFrontBuffer), texels, waitMs,
                        "CaptureScreen's front-buffer read");
}

bool plWGPUDevice::IReadTexture(WGPUTexture texture, uint32_t width, uint32_t height, WGPUTextureFormat format,
                                std::vector<uint32_t>& texels, double& waitMs, const char* how)
{
    // The wait is bracketed before anything is created, so that a refused wait leaves nothing
    // behind (the frame loop refuses only outside a tick or in a callback during a suspension,
    // never inside a tick on the client thread).
    if (!BeginGPUWait(how))
        return false;
    const uint32_t bytesPerRow = (width * 4 + 255) & ~uint32_t(255);

    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = Str(how);
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = uint64_t(bytesPerRow) * height;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(fWGPUDevice, &bd);
    if (!buffer) {
        EndGPUWait();
        return false;
    }

    WGPUCommandEncoderDescriptor ed = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    ed.label = Str(how);
    WGPUCommandEncoder       enc = wgpuDeviceCreateCommandEncoder(fWGPUDevice, &ed);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = texture;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = buffer;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = bytesPerRow;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D size = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &size);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(fQueue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    // Shared state, as IRequestReadback's: a WaitAnyOnly callback runs inside the wait, or, when the wait
    // failed, with CallbackCancelled when the instance goes, after this frame has returned. It holds a
    // reference of its own to the result, which this function reads after the wait through its own.
    struct MapResult
    {
        bool done = false;
        bool ok = false;
    };
    auto                      result = std::make_shared<MapResult>();
    WGPUBufferMapCallbackInfo ci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    ci.mode = WGPUCallbackMode_WaitAnyOnly;
    ci.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
        std::unique_ptr<std::shared_ptr<MapResult>> hold(static_cast<std::shared_ptr<MapResult>*>(userdata1));
        (*hold)->done = true;
        (*hold)->ok = status == WGPUMapAsyncStatus_Success;
    };
    ci.userdata1 = new std::shared_ptr<MapResult>(result);
    WGPUFuture future = wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size_t(bytesPerRow) * height, ci);
    if (!future.id) // the callback is never called (IRequestReadback)
        delete static_cast<std::shared_ptr<MapResult>*>(ci.userdata1);
    const bool waited = WaitFuture(future, waitMs, how);
    EndGPUWait();
    if (!waited || !result->done || !result->ok) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "{}: failed (wait {}, map {})", how, waited ? "ok" : "failed",
                               result->done ? (result->ok ? "ok" : "failed") : "pending");
        wgpuBufferRelease(buffer); // a pending map is aborted with the buffer
        return false;
    }

    const uint8_t* p = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, 0, size_t(bytesPerRow) * height));
    if (!p) {
        wgpuBufferUnmap(buffer);
        wgpuBufferRelease(buffer);
        return false;
    }
    // kARGB32Config is B, G, R, A in memory (0xAARRGGBB little-endian), as bgra8unorm.
    // GetFrontBufferData fills its surface with "a representation of the front buffer,
    // converted to the standard 32 bits per pixel format D3DFMT_A8R8G8B8" (SDK); the front
    // buffer is D3DFMT_X8R8G8B8, "8 bits are reserved for each color" with no alpha
    // (plDXPipeline::IResetToDefaults), and the canvas shows it opaque (alphaMode opaque): its
    // representation is opaque, so alpha is 0xff.
    const uint32_t opaque = 0xff000000;
    const bool     swizzle = format != WGPUTextureFormat_BGRA8Unorm;
    texels.resize(size_t(width) * height);
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = p + size_t(y) * bytesPerRow;
        uint32_t*      out = texels.data() + size_t(y) * width;
        if (!swizzle) {
            const uint32_t* in = reinterpret_cast<const uint32_t*>(row);
            for (uint32_t x = 0; x < width; x++)
                out[x] = in[x] | opaque;
        } else {
            for (uint32_t x = 0; x < width; x++) {
                const uint8_t* t = row + x * 4; // rgba8unorm: R, G, B, A
                out[x] = opaque | (uint32_t(t[0]) << 16) | (uint32_t(t[1]) << 8) | uint32_t(t[2]);
            }
        }
    }
    wgpuBufferUnmap(buffer);
    wgpuBufferRelease(buffer);
    return true;
}

// MARK: - Upload ledger (harness build only)

#ifdef PLASMA_TEST_HOOKS

const char* plWGPUDevice::UploadClassName(UploadClass c)
{
    switch (c) {
        case kUploadStaticVB:
            return "staticVB";
        case kUploadStaticVBRefill:
            return "staticVBRefill";
        case kUploadStaticIB:
            return "staticIB";
        case kUploadDirtyIB:
            return "dirtyIB";
        case kUploadVolatileVB:
            return "volatileVB";
        case kUploadTexture:
            return "texture";
        case kUploadUniform:
            return "uniform";
        case kUploadOther:
            return "other";
        default:
            return "?";
    }
}

void plWGPUDevice::NoteGroupOwner(const void* group)
{
    // The drawable a buffer group is drawn with, for fills outside the span checks
    const plDrawableSpans* drawable = static_cast<const plDrawableSpans*>(fUploadDrawable);
    if (group && drawable && drawable->GetKey() && fGroupOwners.find(group) == fGroupOwners.end())
        fGroupOwners[group] = drawable->GetKey()->GetUoid().StringIze();
}

ST::string plWGPUDevice::UploadDrawableOwner(const void* group, uint32_t idx, const char* what) const
{
    const plDrawableSpans* drawable = static_cast<const plDrawableSpans*>(fUploadDrawable);
    ST::string             key;
    if (drawable && drawable->GetKey()) {
        key = drawable->GetKey()->GetUoid().StringIze();
        fGroupOwners[group] = key;
    } else {
        // a fill outside a span's checks: the drawable this group was last checked with
        auto it = fGroupOwners.find(group);
        key = it != fGroupOwners.end() ? it->second + " (by group)" : ST_LITERAL("(no drawable)");
    }
    return ST::format("{} group {} {} {} (group@0x{x})", key, fUploadGroup, what, idx, uintptr_t(group));
}

void plWGPUDevice::NoteUpload(UploadClass c, uint64_t bytes, const ST::string& owner, const char* reason)
{
    if (c == kUploadStaticVBRefill || (c == kUploadTexture && reason && std::strcmp(reason, "not-dirty") == 0)) {
        static uint32_t sLogged = 0;
        if (sLogged++ < 40)
            plWGPULog("re-upload ({}): {} {} bytes, {}", UploadClassName(c), owner, bytes, reason ? reason : "");
    }
}
#endif

// MARK: - Gamma at present

static const char kGammaCorrectionWGSL[] =
#include "GammaCorrection.wgsl.inc"
    ;

void plWGPUDevice::SetGammaTable(const uint16_t (&table)[3][256])
{
    // plMetalPipeline::SetGamma: a 3-layer R16Uint table; here
    // a 256x3 2D texture read with textureLoad. Re-set on every SetGamma*.
    if (!fWGPUDevice)
        return;
    if (!fGammaLUTTexture) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = Str("plWGPU gamma LUT");
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size = {256, 3, 1};
        desc.format = WGPUTextureFormat_R16Uint;
        fGammaLUTTexture = wgpuDeviceCreateTexture(fWGPUDevice, &desc);
        fGammaLUTView = wgpuTextureCreateView(fGammaLUTTexture, nullptr);
    }
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = fGammaLUTTexture;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = 256 * sizeof(uint16_t);
    layout.rowsPerImage = 3;
    WGPUExtent3D size = {256, 3, 1};
    wgpuQueueWriteTexture(fQueue, &dst, &table[0][0], sizeof(table), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
    NoteUpload(kUploadTexture, sizeof(table), "gamma LUT", "SetGamma");
#endif
}

void plWGPUDevice::ICreateGammaAdjustState()
{
    // plMetalDevice::CreateGammaAdjustState, the non-tiler
    // descriptor: the canvas format, one sample, no depth.
    if (fGammaPipeline)
        wgpuRenderPipelineRelease(fGammaPipeline);
    fGammaPipeline = nullptr;
    if (!fGammaBindGroupLayout) {
        WGPUBindGroupLayoutEntry entries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Uint;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        ld.label = Str("plWGPU gamma");
        ld.entryCount = 2;
        ld.entries = entries;
        fGammaBindGroupLayout = wgpuDeviceCreateBindGroupLayout(fWGPUDevice, &ld);
        WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        pd.label = Str("plWGPU gamma");
        pd.bindGroupLayoutCount = 1;
        pd.bindGroupLayouts = &fGammaBindGroupLayout;
        fGammaPipelineLayout = wgpuDeviceCreatePipelineLayout(fWGPUDevice, &pd);
    }
    WGPUShaderModule module = ShaderModule(kGammaCorrectionWGSL, "plWGPU gamma correction");

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = fFramebufferFormat;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = module;
    fs.entryPoint = Str("gammaCorrectFragment");
    fs.targetCount = 1;
    fs.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = Str("plWGPU gamma correction");
    desc.layout = fGammaPipelineLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = Str("gammaCorrectVertex");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    desc.primitive.cullMode = WGPUCullMode_None;
    desc.multisample.count = 1;
    desc.fragment = &fs;
    fGammaPipeline = wgpuDeviceCreateRenderPipeline(fWGPUDevice, &desc);
    fGammaPipelineFormat = fFramebufferFormat;
    plWGPULog("gamma: LUT pipeline created for format {}", int(fFramebufferFormat));
}

void plWGPUDevice::PostprocessIntoDrawable()
{
    // plMetalDevice::PostprocessIntoDrawable, non-tiler: a new pass
    // on the canvas texture reads the unprocessed frame (Metal's load action DontCare: every
    // pixel is written) and draws the 4-vertex full-frame strip.
    if (!fGammaPipeline || fGammaPipelineFormat != fFramebufferFormat)
        ICreateGammaAdjustState();
    if (!fGammaPipeline || !fUnprocessedView || !fGammaLUTView)
        return;
    if (!fGammaBindGroup || fGammaBindGroupInput != fUnprocessedView || fGammaBindGroupLUT != fGammaLUTView) {
        if (fGammaBindGroup)
            wgpuBindGroupRelease(fGammaBindGroup);
        WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].textureView = fUnprocessedView;
        entries[1].binding = 1;
        entries[1].textureView = fGammaLUTView;
        WGPUBindGroupDescriptor bd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bd.label = Str("plWGPU gamma");
        bd.layout = fGammaBindGroupLayout;
        bd.entryCount = 2;
        bd.entries = entries;
        fGammaBindGroup = wgpuDeviceCreateBindGroup(fWGPUDevice, &bd);
        fGammaBindGroupInput = fUnprocessedView;
        fGammaBindGroupLUT = fGammaLUTView;
    }

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = fCurrentDrawableView;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = {0.0, 0.0, 0.0, 1.0};
    WGPURenderPassDescriptor pd = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pd.label = Str("plWGPU gamma correction");
    pd.colorAttachmentCount = 1;
    pd.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(fCurrentCommandBuffer, &pd);
    wgpuRenderPassEncoderSetPipeline(pass, fGammaPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, fGammaBindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void plWGPUDevice::IReleaseGammaDeviceObjects()
{
    // The device-side objects of the post pass. The table itself is kept by the pipeline
    // (plWGPUPipeline::fGammaRamp) and re-set on the new device (ICreateDeviceObjects).
    if (fGammaBindGroup)
        wgpuBindGroupRelease(fGammaBindGroup);
    fGammaBindGroup = nullptr;
    fGammaBindGroupInput = nullptr;
    fGammaBindGroupLUT = nullptr;
    if (fGammaPipeline)
        wgpuRenderPipelineRelease(fGammaPipeline);
    fGammaPipeline = nullptr;
    fGammaPipelineFormat = WGPUTextureFormat_Undefined;
    if (fGammaPipelineLayout)
        wgpuPipelineLayoutRelease(fGammaPipelineLayout);
    fGammaPipelineLayout = nullptr;
    if (fGammaBindGroupLayout)
        wgpuBindGroupLayoutRelease(fGammaBindGroupLayout);
    fGammaBindGroupLayout = nullptr;
    if (fGammaLUTView)
        wgpuTextureViewRelease(fGammaLUTView);
    fGammaLUTView = nullptr;
    if (fGammaLUTTexture)
        wgpuTextureRelease(fGammaLUTTexture);
    fGammaLUTTexture = nullptr;
    if (fUnprocessedView)
        wgpuTextureViewRelease(fUnprocessedView);
    fUnprocessedView = nullptr;
    // The front buffer goes with the unprocessed texture that holds it
    if (fFrontBuffer && fFrontBuffer == fUnprocessedTexture)
        IReleaseFrontBuffer();
    if (fUnprocessedTexture)
        wgpuTextureRelease(fUnprocessedTexture);
    fUnprocessedTexture = nullptr;
    fFramePostprocessed = false;
}

// MARK: - Lost device

void plWGPUDevice::ReleaseAllDeviceObjects()
{
    // Everything this device created on the lost WebGPU device (plDXPipeline::IReleaseDeviceObjects
    // releases the D3D objects; these are the WebGPU-only ones). The
    // pending readbacks stay listed: their maps fail with the lost device and TakeReadback hands
    // them over as failed.
#ifdef PLASMA_TEST_HOOKS
    // The buffers and textures are filled anew on the new device (first fills)
    fFilledVB.clear();
    fFilledIB.clear();
    fFilledTextures.clear();
    fGroupOwners.clear();
#endif
    IEndRenderPass();
    if (fCurrentOffscreenCommandBuffer)
        wgpuCommandEncoderRelease(fCurrentOffscreenCommandBuffer);
    fCurrentOffscreenCommandBuffer = nullptr;
    if (fCurrentCommandBuffer)
        wgpuCommandEncoderRelease(fCurrentCommandBuffer);
    fCurrentCommandBuffer = nullptr;
    if (fCurrentDrawableView)
        wgpuTextureViewRelease(fCurrentDrawableView);
    fCurrentDrawableView = nullptr;
    if (fCurrentDrawable)
        wgpuTextureRelease(fCurrentDrawable);
    fCurrentDrawable = nullptr;
    fCurrentFragmentOutputView = nullptr;
    fCurrentDepthView = nullptr;
    fCurrentRenderTarget = nullptr;

    // fNewPipelineStateMap owns every render pipeline; fFixedPipelines and fShaderPipelines are
    // lookups of the same handles (PipelineState), so they are only emptied.
    for (auto& [record, pipeline] : fNewPipelineStateMap)
        wgpuRenderPipelineRelease(pipeline);
    fNewPipelineStateMap.clear();
    // The builds queued and in flight belong to the released generation: their results are released with them, or by
    // their callbacks when the instance cancels them (IIssuePipelineBuild).
    fPipelineBuilds.clear();
    fPipelineQueue.clear();
    fPipelineQueued.clear();
    fPaceIssued = 0;
    fGeneration++;
#ifdef PLASMA_TEST_HOOKS
    fPrewarmUnused.clear();
#endif
    for (auto& [source, entry] : fShaderModules)
        wgpuShaderModuleRelease(entry.module);
    fShaderModules.clear();
    for (auto& map : fFixedPipelines)
        map.clear();
    // A program that failed on the released device gets another try on the next one. The program texts and ids stay
    // (they name the modules by address, ShaderModule).
    fFixedFailed.clear();
    fFixedScopeChecks.clear();
    fShaderPipelines.clear();
    for (auto& [key, sampler] : fD3DSamplers)
        wgpuSamplerRelease(sampler);
    fD3DSamplers.clear();
    for (auto& [key, group] : fFixedBindGroups)
        wgpuBindGroupRelease(group);
    fFixedBindGroups.clear();
    fFixedBindGroupSerial = 0;
    for (WGPUBindGroup group : fFrameBindGroups)
        wgpuBindGroupRelease(group);
    fFrameBindGroups.clear();

    for (std::vector<Arena>* arenas : {&fUniformArenas, &fVertexArenas}) {
        for (Arena& arena : *arenas) {
            if (arena.group)
                wgpuBindGroupRelease(arena.group);
            if (arena.buffer)
                wgpuBufferRelease(arena.buffer);
        }
        arenas->clear();
    }

    ReleaseSamplerStates();
    auto releaseSampler = [](WGPUSampler& s) { if (s) wgpuSamplerRelease(s); s = nullptr; };
    auto releaseTexture = [](WGPUTexture& t) { if (t) wgpuTextureRelease(t); t = nullptr; };
    auto releaseView = [](WGPUTextureView& v) { if (v) wgpuTextureViewRelease(v); v = nullptr; };
    auto releaseBuffer = [](WGPUBuffer& b) { if (b) wgpuBufferRelease(b); b = nullptr; };
    auto releaseGroupLayout = [](WGPUBindGroupLayout& l) { if (l) wgpuBindGroupLayoutRelease(l); l = nullptr; };
    auto releaseLayout = [](WGPUPipelineLayout& l) { if (l) wgpuPipelineLayoutRelease(l); l = nullptr; };
    releaseSampler(fPointSampler);

    releaseView(fCurrentDrawableDepthView);
    releaseTexture(fCurrentDrawableDepthTexture);
    releaseView(fCurrentFragmentMSAAOutputView);
    releaseTexture(fCurrentFragmentMSAAOutputTexture);
    fDrawableWidth = 0;
    fDrawableHeight = 0;
    IReleaseFrontBuffer();
    IReleaseGammaDeviceObjects();

    releaseView(fWhiteTextureView);
    releaseTexture(fWhiteTexture);
    releaseView(fWhiteCubeTextureView);
    releaseTexture(fWhiteCubeTexture);
    releaseBuffer(fZeroVertexBuffer);

    releaseLayout(fUniformsOnlyLayout);
    releaseLayout(fUniformsAndTextureLayout);
    releaseLayout(fFixedFunctionLayout);
    releaseLayout(fShaderLayout);
    releaseGroupLayout(fUniformBindGroupLayout);
    releaseGroupLayout(fTextureBindGroupLayout);
    releaseGroupLayout(fFixedTextureBindGroupLayout);

    if (fQueue)
        wgpuQueueRelease(fQueue);
    fQueue = nullptr;
    fWGPUDevice = nullptr;
}
