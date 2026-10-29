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

#include <cstring>

#include "HeadSpin.h"
#include "plDrawable/plDrawableSpans.h"
#include "plDrawable/plSpanTypes.h"
#include "plGLight/plShadowCaster.h"
#include "plGLight/plShadowMaster.h"
#include "plGLight/plShadowSlave.h"
#include "plPipeline/plRenderTarget.h"
#include "plSurface/hsGMaterial.h"
#include "plSurface/plLayerInterface.h"
#include "plWGPULightRef.h"
#include "plWGPULog.h"
#include "plWGPUPipeline.h"
#include "plWGPUPipeline_Private.h"
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif

using namespace plWGPUD3D9;

// The shadow blur sample count (pl3DPipeline.cpp; plDXPipeline.cpp declares it the same way).
extern int mfCurrentTest;

//// Shadows ////////////////////////////////////////////////////////////////////
// plDXPipeline's shadow functions, one for one. The shadow maps render into the pool targets
// through the emulated D3D9 device like any other pass; the receive pass projects them onto the
// spans.

void plWGPUPipeline::IBlurShadowMap(plShadowSlave* slave)
{
    // plDXPipeline::IBlurShadowMap
    plRenderTarget* smap = (plRenderTarget*)slave->fPipeData;
    float           scale = slave->fBlurScale;

    // Find a scratch rendertarget which matches the input.
    int which = IGetScratchRenderTarget(smap);
    if (which < 0)
        return;
    plRenderTarget* scratchRT = fBlurScratchRTs[which];
    if (!scratchRT)
        return;
    plRenderTarget* destRT = fBlurDestRTs[which];
    if (!destRT)
        return;

    // Set up to render into it.
    IBlurSetRenderTarget(scratchRT);

    // Clear it appropriately
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    fDevice.Clear(true, black, false, 1.f);

    // Setup our quad for rendering
    ISetBlurQuadToRender(smap);

    // Render the input image into the scratch image, creating the blur.
    IRenderBlurFromShadowMap(scratchRT, smap, scale);

    // Set the rendertarget back to src
    // Setup renderstate to render it back modulating.
    // Render the scratch back into the input.
    IRenderBlurBackToShadowMap(smap, scratchRT, destRT);

    // dst is now now slave's rendertarget and smap is the new scratch dst
    // for this size.
    slave->fPipeData = (void*)destRT;
    fBlurDestRTs[which] = smap;
}

int plWGPUPipeline::IGetScratchRenderTarget(plRenderTarget* smap)
{
    // plDXPipeline::IGetScratchRenderTarget
    int which = -1;
    switch (smap->GetHeight()) {
        case 512:
            which = 9;
            break;
        case 256:
            which = 8;
            break;
        case 128:
            which = 7;
            break;
        case 64:
            which = 6;
            break;
        case 32:
            which = 5;
            break;
        default:
            // DX returns false here, i.e. slot 0, whose targets are never made: no blur.
            return -1;
    }
    if (!fBlurScratchRTs[which]) {
        // This intermediate is one power of 2 smaller than the source.
        uint32_t width = smap->GetWidth();
        uint32_t height = smap->GetHeight();
        if (width > 32) {
            width >>= 1;
            height >>= 1;
        }
        fBlurScratchRTs[which] = IFindRenderTarget(width, height, smap->GetFlags() & plRenderTarget::kIsOrtho);
    }
    if (!fBlurDestRTs[which]) {
        // Destination is same size as source.
        uint32_t width = smap->GetWidth();
        uint32_t height = smap->GetHeight();
        fBlurDestRTs[which] = IFindRenderTarget(width, height, smap->GetFlags() & plRenderTarget::kIsOrtho);
    }
    return which;
}

void plWGPUPipeline::IBlurSetRenderTarget(plRenderTarget* rt)
{
    // plDXPipeline::IBlurSetRenderTarget: the abbreviated PushRenderTarget (the colour and depth
    // surfaces only, no state invalidation), then the target's full viewport.
    fDevice.BindRenderTargetSurfaces(rt);
    fDevice.SetViewport(0.f, 0.f, float(rt->GetWidth()), float(rt->GetHeight()));
}

void plWGPUPipeline::IRenderBlurFromShadowMap(plRenderTarget* scratchRT, plRenderTarget* smap, float scale)
{
    // plDXPipeline::IRenderBlurFromShadowMap
    // Quad is set up in camera space.
    fD3DDevice->SetTransform(D3DTS_VIEW, &d3dIdentityMatrix);
    fD3DDevice->SetTransform(D3DTS_WORLD, &d3dIdentityMatrix);
    fD3DDevice->SetTransform(D3DTS_PROJECTION, &d3dIdentityMatrix);

    // Figure out how many passes we'll need.
    const int kNumSamples = mfCurrentTest > 101 ? 8 : 4;
    int       nPasses = (int)ceil(float(kNumSamples) / fMaxLayersAtOnce);
    int       nSamplesPerPass = kNumSamples / nPasses;

    // Attenuate by number of passes, to average as we sum.
    DWORD atten = 255 / nPasses;
    plConst(float) kAtten(1.f);
    atten = DWORD(atten * kAtten);
    atten = (atten << 24) | (atten << 16) | (atten << 8) | (atten << 0);

    // Disable skinning
    fD3DDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    //  AlphaEnable = true
    //  AlphaTest OFF
    fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);

    //  ZBUFFER disabled
    fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    fLayerState[0].fZFlags &= ~hsGMatState::kZMask;
    fLayerState[0].fZFlags |= hsGMatState::kZNoZWrite | hsGMatState::kZNoZRead;

    //  Cullmode is NONE
    fDevice.fCurrCullMode = D3DCULL_NONE;
    fD3DDevice->SetRenderState(D3DRS_CULLMODE, fDevice.fCurrCullMode);

    plWGPUTextureRef* ref = (plWGPUTextureRef*)smap->GetDeviceRef();
    hsAssert(ref, "Shadow map ref should have been made when it was rendered");
    if (!ref)
        return;

    // TFactor contains the attenuation
    fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, atten);

    // Set the N texture stages all to use the same src rendertarget texture (see the stage
    // table in plDXPipeline::IRenderBlurFromShadowMap).
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    fLayerState[0].fClampFlags = hsGMatState::kClampTexture;

    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    fLayerState[0].fBlendFlags = uint32_t(-1);

    if (!fPrewarmWalk) { // the prewarm's walk binds no texture: no key reads one
        hsRefCnt_SafeAssign(fLayerRef[0], ref);
        fD3DDevice->SetTexture(0, ref);
    }

    if (D3DTTFF_COUNT2 != fLayerXformFlags[0]) {
        fLayerXformFlags[0] = D3DTTFF_COUNT2;
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    }
    fD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    fLayerUVWSrcs[0] = 0;

    int i;
    for (i = 1; i < nSamplesPerPass; i++) {
        fD3DDevice->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        fD3DDevice->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        fLayerState[i].fClampFlags = hsGMatState::kClampTexture;

        fD3DDevice->SetTextureStageState(i, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        fD3DDevice->SetTextureStageState(i, D3DTSS_COLORARG2, D3DTA_CURRENT);
        fD3DDevice->SetTextureStageState(i, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

        fD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        fD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        fD3DDevice->SetTextureStageState(i, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        fLayerState[i].fBlendFlags = uint32_t(-1);

        if (!fPrewarmWalk) {
            hsRefCnt_SafeAssign(fLayerRef[i], ref);
            fD3DDevice->SetTexture(i, ref);
        }

        if (D3DTTFF_COUNT2 != fLayerXformFlags[i]) {
            fLayerXformFlags[i] = D3DTTFF_COUNT2;
            fD3DDevice->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
        }
        fD3DDevice->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, 0);
        fLayerUVWSrcs[i] = 0;
    }
    fD3DDevice->SetTextureStageState(nSamplesPerPass, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(nSamplesPerPass, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // N offsets are { (-1,-1), (1, -1), (1, 1), (-1, 1) } * offsetScale / size
    struct offsetStruct
    {
        float fU;
        float fV;
    };
    offsetStruct        offsetScale = {scale / scratchRT->GetWidth(), scale / scratchRT->GetHeight()};
    static offsetStruct offsets[8] = {
        {-1.f, -1.f},
        {1.f, -1.f},
        {1.f, 1.f},
        {-1.f, 1.f},
        {0.f, -0.5f},
        {0.f, 0.5f},
        {-0.5f, 0.f},
        {0.5f, 0.f}};

    int iSample = 0;
    // For each pass,
    for (i = 0; i < nPasses; i++) {
        // Set the N texture stage uv transforms to the next N offsets.
        int j;
        for (j = 0; j < nSamplesPerPass; j++) {
            D3DMATRIX offXfm = d3dIdentityMatrix;
            offXfm.m[2][0] = offsets[iSample].fU * offsetScale.fU;
            offXfm.m[2][1] = offsets[iSample].fV * offsetScale.fV;
            fD3DDevice->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + j), &offXfm);
            fLayerTransform[j] = true;

            iSample++;
        }

        // Render our quad
        IDrawBlurQuad();
    }
}

void plWGPUPipeline::IRenderBlurBackToShadowMap(plRenderTarget* smap, plRenderTarget* scratch, plRenderTarget* dst)
{
    // plDXPipeline::IRenderBlurBackToShadowMap
    // Set the rendertarget
    IBlurSetRenderTarget(dst);

    // Clear it appropriately. This might not be necessary, since we're just going to overwrite.
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    fDevice.Clear(true, black, false, 1.f);

    // Set Stage0 texture transform
    // Clamp still on (from RBFSM)
    D3DMATRIX offXfm = d3dIdentityMatrix;
    fD3DDevice->SetTransform(D3DTS_TEXTURE0, &offXfm);
    fD3DDevice->SetTransform(D3DTS_TEXTURE1, &offXfm);
    fLayerTransform[0] = false;
    fLayerTransform[1] = false;

    plWGPUTextureRef* ref = (plWGPUTextureRef*)scratch->GetDeviceRef();
    hsAssert(ref, "Blur scratch map ref should have been made when it was rendered");
    if (!ref)
        return;
    if (!fPrewarmWalk) { // the prewarm's walk binds no texture: no key reads one
        hsRefCnt_SafeAssign(fLayerRef[0], ref);
        fD3DDevice->SetTexture(0, ref);
    }

    ref = (plWGPUTextureRef*)smap->GetDeviceRef();
    hsAssert(ref, "Blur src map ref should have been made when it was rendered");
    if (!ref)
        return;
    if (!fPrewarmWalk) {
        hsRefCnt_SafeAssign(fLayerRef[1], ref);
        fD3DDevice->SetTexture(1, ref);
    }

    // Color is ADDSIGNED2X(TFACTOR = black, scratch); alpha SELECTARG1(smap) in stage 1.
    fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, 0L);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED2X);

    // This alpha will be ignored, because in the next stage we select texture alpha.
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

    fLayerState[0].fBlendFlags = uint32_t(-1);

    fD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_SELECTARG2);

    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

    fLayerState[1].fBlendFlags = uint32_t(-1);

    fD3DDevice->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    fLastEndingStage = 2;

    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);

    // Our quad should still be setup to go.
    IDrawBlurQuad();
}

void plWGPUPipeline::IDrawBlurQuad()
{
    // DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2) from the stream ISetBlurQuadToRender set.
    // The prewarm's walk: the draw's pipeline, started; nothing drawn, no buffer, no counter.
    if (fPrewarmWalk) {
        const int kind = fRowCtx.kind;
        fRowCtx.kind = kRowBlur;
        plWGPUDevice::FixedFunctionDraw draw;
        draw.xyzTex1 = true;
        draw.start = 0;
        draw.count = 4;
        draw.topology = WGPUPrimitiveTopology_TriangleStrip;
        fDevice.PrewarmFixedFunction(draw);
        fRowCtx.kind = kind;
        return;
    }
    if (!fBlurQuad)
        return;
    fCounters.shadowBlurPasses++;
    const int kind = fRowCtx.kind;
    fRowCtx.kind = kRowBlur;
    plWGPUDevice::FixedFunctionDraw draw;
    draw.vertexBuffer = fBlurQuad;
    draw.xyzTex1 = true;
    draw.start = 0;
    draw.count = 4;
    draw.topology = WGPUPrimitiveTopology_TriangleStrip;
    IDrawFixedFunction(draw);
    fRowCtx.kind = kind;
}

void plWGPUPipeline::IReleaseBlurVBuffers()
{
    // plDXPipeline::IReleaseBlurVBuffers
    for (int i = 0; i < kMaxRenderTargetNext; i++) {
        if (fBlurVBuffers[i]) {
            wgpuBufferRelease(fBlurVBuffers[i]);
            fBlurVBuffers[i] = nullptr;
        }
    }
    fBlurQuad = nullptr;
}

bool plWGPUPipeline::ICreateBlurVBuffers()
{
    // plDXPipeline::ICreateBlurVBuffers: a quad for each shadow map size, because the UVs depend
    // on the size: positions (-1,-1)..(1,1) at z 0.5, UVs 0.5/size off, drawn as a strip in
    // the order P1, P2, P0, P3.
    for (int i = 0; i < kMaxRenderTargetNext; i++) {
        int width = 0;
        int height = 0;
        int which = -1;
        if (i >= 5 && i <= 9) {
            width = height = 1 << i;
            which = i;
        }
        if (which < 0 || fBlurVBuffers[which])
            continue;

        plWGPUXYZTex1Vert ptr[4];
        plWGPUXYZTex1Vert vert;
        vert.fPos[0] = -1.f;
        vert.fPos[1] = -1.f;
        vert.fPos[2] = 0.5f;

        vert.fUv[0] = 0.5f / width;
        vert.fUv[1] = 1.f + 0.5f / height;
        vert.fUv[2] = 1.f;

        // P0
        ptr[2] = vert;

        // P1
        ptr[0] = vert;
        ptr[0].fPos[0] += 2.f;
        ptr[0].fUv[0] += 1.f;

        // P2
        ptr[1] = vert;
        ptr[1].fPos[0] += 2.f;
        ptr[1].fUv[0] += 1.f;
        ptr[1].fPos[1] += 2.f;
        ptr[1].fUv[1] -= 1.f;

        // P3
        ptr[3] = vert;
        ptr[3].fPos[1] += 2.f;
        ptr[3].fUv[1] -= 1.f;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{"plWGPU blur quad", WGPU_STRLEN};
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        desc.size = sizeof(ptr);
        WGPUBuffer vBuffer = wgpuDeviceCreateBuffer(fDevice.fWGPUDevice, &desc);
        if (!vBuffer) {
            hsAssert(false, "CreateVertexBuffer() call failed!");
            return false;
        }
        wgpuQueueWriteBuffer(fDevice.fQueue, vBuffer, 0, ptr, sizeof(ptr));
#ifdef PLASMA_TEST_HOOKS
        fDevice.NoteUpload(plWGPUDevice::kUploadOther, sizeof(ptr), ST_LITERAL("blur quad"), "create");
#endif

        fBlurVBuffers[which] = vBuffer;
    }
    return true;
}

bool plWGPUPipeline::ISetBlurQuadToRender(plRenderTarget* smap)
{
    // plDXPipeline::ISetBlurQuadToRender: the quad of the source map's size.
    int which = -1;
    switch (smap->GetHeight()) {
        case 512:
            which = 9;
            break;
        case 256:
            which = 8;
            break;
        case 128:
            which = 7;
            break;
        case 64:
            which = 6;
            break;
        case 32:
            which = 5;
            break;
        default:
            return false;
    }

    // The prewarm's walk makes no buffer and leaves the stream source as it is (IDrawBlurQuad's walk draws none).
    if (fPrewarmWalk) {
        fD3DDevice->SetVertexShader(nullptr);
        return true;
    }

    // If we haven't created (or have lost) our resources, make them
    WGPUBuffer vBuffer = fBlurVBuffers[which];
    if (!vBuffer) {
        ICreateBlurVBuffers();
        vBuffer = fBlurVBuffers[which];
        hsAssert(vBuffer, "AllocBlurVBuffers failed");
    }

    // SetVertexShader(nullptr) and the FVF: the draw is fixed
    // function with the XYZ|TEX1 layout. The pixel shader is left as it is, as DX leaves it.
    fD3DDevice->SetVertexShader(nullptr);
    fBlurQuad = vBuffer;

    // No transforms, we're supplying screen ready verts.
    return true;
}

void plWGPUPipeline::IRenderShadowCasterSpan(plShadowSlave* slave, plDrawableSpans* drawable, const plIcicle& span)
{
    // plDXPipeline::IRenderShadowCasterSpan: render the span into the slave's map, generating a depth
    // map from this light to that span.
    // The prewarm's walk (plWGPUPipeline::IPrewarmTargets) checks, uploads and marks no buffer: it takes the span's
    // vertex format as the room walk does (IPrewarmSpanFormat) and starts the draw's pipeline.
    plWGPUVertexBufferRef* vRef = nullptr;
    plWGPUIndexBufferRef*  iRef = nullptr;
    uint8_t                walkVertexFormat = 0;
    int                    walkTris = 0;
    if (fPrewarmWalk) {
        if (!IPrewarmSpanFormat(drawable, span, walkVertexFormat, walkTris))
            return;
    } else {
        // Check that it's ready to render.
#ifdef PLASMA_TEST_HOOKS
        fDevice.fUploadDrawable = drawable;
        fDevice.fUploadGroup = span.fGroupIdx;
#endif
        ICheckDynBuffers(drawable, drawable->GetBufferGroup(span.fGroupIdx), &span);
        // The WebGPU buffers are made or refilled where Metal's are (RenderSpans).
        CheckVertexBufferRef(drawable->GetBufferGroup(span.fGroupIdx), span.fVBufferIdx);
        CheckIndexBufferRef(drawable->GetBufferGroup(span.fGroupIdx), span.fIBufferIdx);
#ifdef PLASMA_TEST_HOOKS
        fDevice.fUploadDrawable = nullptr; // the upload record's owner context ends here
#endif

        vRef = (plWGPUVertexBufferRef*)drawable->GetVertexRef(span.fGroupIdx, span.fVBufferIdx);
        iRef = (plWGPUIndexBufferRef*)drawable->GetIndexRef(span.fGroupIdx, span.fIBufferIdx);

        if (vRef == nullptr || iRef == nullptr || vRef->GetBuffer() == nullptr || iRef->GetBuffer() == nullptr) {
            hsAssert(false, "Trying to render a nil buffer pair!");
            return;
        }

        vRef->SetRebuiltSinceUsed(false);
        iRef->SetRebuiltSinceUsed(false);
    }

    uint32_t vStart = span.fVStartIdx;
    uint32_t vLength = span.fVLength;
    uint32_t iStart = span.fIPackedIdx;
    uint32_t iLength = span.fILength;

    // As DX: the FVF of the buffer and no vertex shader (the pixel shader
    // is left as it is, as DX leaves it). The walk's render functor reads it too.
    fD3DDevice->SetVertexShader(nullptr);

    static hsMatrix44 emptyMatrix;
    hsMatrix44        m = emptyMatrix;

    ISetupTransforms(drawable, span, m);

    bool flip = slave->ReverseCull();
    ISetCullMode(flip);

    const RowContext saved = fRowCtx;
    fRowCtx.kind = kRowShadowCast;
    fRowCtx.drawable = drawable;
    fRowCtx.span = &span;
    fRowCtx.material = nullptr;
    fRowCtx.group = drawable->GetBufferGroup(span.fGroupIdx);
    fRowCtx.vbIdx = span.fVBufferIdx;
    fRowCtx.slave = slave;
    if (fPrewarmWalk) {
        plWGPUPrewarmTriListFunc prewarm(this, walkVertexFormat, walkTris);
        prewarm.RenderPrims();
    } else {
        plWGPURenderTriListFunc render(this, vRef, iRef, iRef->fOffset, vStart, vLength, iStart, iLength / 3);
        fCounters.shadowCastSpans++;
        render.RenderPrims();
    }
    fRowCtx = saved;
}

plWGPUTextureRef* plWGPUPipeline::IGetULutTextureRef()
{
    // plDXPipeline::IGetULutTextureRef: U in [0..1] to colour and alpha of U * 255.9,
    // a 256x1 A8R8G8B8 texture, texel i = (i, i, i, i), one level.
    const int width = 256;
    const int height = 1;
    if (!fULutTextureRef) {
        uint32_t  tData[width * height];
        uint32_t* pData = tData;
        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++) {
                *pData = (i << 24) | (i << 16) | (i << 8) | (i << 0);
                pData++;
            }
        }

        plWGPUTextureRef*     ref = new plWGPUTextureRef();
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{"plWGPU shadow ULut", WGPU_STRLEN};
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.size = {width, height, 1};
        desc.format = WGPUTextureFormat_BGRA8Unorm; // D3DFMT_A8R8G8B8
        desc.mipLevelCount = 1;
        ref->fTexture = wgpuDeviceCreateTexture(fDevice.fWGPUDevice, &desc);
        ref->fTextureView = wgpuTextureCreateView(ref->fTexture, nullptr);
        ref->fFormat = desc.format;
        ref->fLevels = 1;

        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = ref->fTexture;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = width * 4;
        layout.rowsPerImage = height;
        WGPUExtent3D size = {width, height, 1};
        wgpuQueueWriteTexture(fDevice.fQueue, &dst, tData, sizeof(tData), &layout, &size);
#ifdef PLASMA_TEST_HOOKS
        fDevice.NoteUpload(plWGPUDevice::kUploadOther, sizeof(tData), ST_LITERAL("shadow ULut"), "create");
#endif

        ref->SetDirty(false);
        ref->Link(&fTextureRefList);

        fULutTextureRef = ref;
    }
    return fULutTextureRef;
}

plRenderTarget* plWGPUPipeline::IFindRenderTarget(uint32_t& width, uint32_t& height, bool ortho)
{
    // plDXPipeline::IFindRenderTarget: the requested size from the pools, else the next size down
    // (the slave's width and height are rewritten through the references).
    std::vector<plRenderTarget*>* pool = nullptr;
    uint32_t*                     iNext = nullptr;
    // NOT CURRENTLY SUPPORTING NON-SQUARE SHADOWS. IF WE DO, CHANGE THIS.
    switch (height) {
        case 512:
            pool = &fRenderTargetPool512;
            iNext = &fRenderTargetNext[9];
            break;
        case 256:
            pool = &fRenderTargetPool256;
            iNext = &fRenderTargetNext[8];
            break;
        case 128:
            pool = &fRenderTargetPool128;
            iNext = &fRenderTargetNext[7];
            break;
        case 64:
            pool = &fRenderTargetPool64;
            iNext = &fRenderTargetNext[6];
            break;
        case 32:
            pool = &fRenderTargetPool32;
            iNext = &fRenderTargetNext[5];
            break;
        default:
            return nullptr;
    }
    plRenderTarget* rt = (*pool)[*iNext];
    if (!rt) {
        // We didn't find one, try again the next size down.
        if (height > 32)
            return IFindRenderTarget(width >>= 1, height >>= 1, ortho);

        // We must be totally out. Oh well.
        return nullptr;
    }
    (*iNext)++;

    return rt;
}

bool plWGPUPipeline::IPushShadowCastState(plShadowSlave* slave)
{
    // plDXPipeline::IPushShadowCastState
    plRenderTarget* renderTarg = IFindRenderTarget(slave->fWidth, slave->fHeight, slave->fView.GetOrthogonal());
    if (!renderTarg)
        return false;

    // Let the slave setup the transforms, viewport, etc. necessary to render it's shadow
    // map. This just goes into a plViewTransform, we translate that into D3D state ourselves below.
    if (!slave->SetupViewTransform(this))
        return false;

    // Turn off fogging and specular.
    fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    fCurrFog.fEnvPtr = nullptr;
    fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    fLayerState[0].fShadeFlags &= ~hsGMatState::kShadeSpecular;

    // Push the shadow slave's view transform as our current render state.
    fViewStack.push(fView);
    fView.SetMaxCullNodes(0);
    SetViewTransform(slave->fView);
    IProjectionMatrixToDevice();

    // Push the shadow map as the current render target
    PushRenderTarget(renderTarg);

    // We'll be rendering the light space distance to the span fragment into
    // alpha (color is white), so our camera space position, transformed into light space
    // and then converted to [0..255] via our ULut.

    // For stage 0:
    // Set uvw src
    if (fLayerUVWSrcs[0] != D3DTSS_TCI_CAMERASPACEPOSITION) {
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        fLayerUVWSrcs[0] = D3DTSS_TCI_CAMERASPACEPOSITION;
    }
    uint32_t xformFlags = D3DTTFF_COUNT3;

    if (xformFlags != fLayerXformFlags[0]) {
        fLayerXformFlags[0] = xformFlags;
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, xformFlags);
    }

    // Set texture transform to slave's lut transform. See plShadowMaster::IComputeLUT().
    hsMatrix44 castLUT = slave->fCastLUT;
    if (slave->fFlags & plShadowSlave::kCastInCameraSpace) {
        hsMatrix44 c2w = GetCameraToWorld();

        castLUT = castLUT * c2w;
    }

    D3DMATRIX tXfm;
    IMatrix44ToD3DMatrix(tXfm, castLUT);

    fD3DDevice->SetTransform(D3DTS_TEXTURE0, &tXfm);
    fLayerTransform[0] = true;

    // Set texture to clamp
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    fLayerState[0].fClampFlags = hsGMatState::kClampTexture;

    DWORD clearColor = 0xff000000L;
    DWORD intens;
    if (slave->fBlurScale > 0) {
        const int kNumSamples = mfCurrentTest > 101 ? 8 : 4;
        int       nPasses = (int)ceil(float(kNumSamples) / fMaxLayersAtOnce);
        int       nSamplesPerPass = kNumSamples / nPasses;
        DWORD     k = int(128.f / float(nSamplesPerPass));
        intens = (0xff << 24) | ((128 + k) << 16) | ((128 + k) << 8) | ((128 + k) << 0);
        clearColor = (0xff << 24) | ((128 - k) << 16) | ((128 - k) << 8) | ((128 - k) << 0);
    } else {
        intens = 0xffffffff;
    }

    // Set color to white. We could accomplish this easier by making the color
    // in our ULut white.
    fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, intens);

    fSettings.fVeryAnnoyingTextureInvalidFlag = true;
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    fLayerState[0].fBlendFlags = uint32_t(-1);

    // For stage 1 - disable
    fLastEndingStage = 1;
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    fLayerState[1].fBlendFlags = uint32_t(-1);

    // Set texture to U_LUT (the prewarm's walk makes and binds no texture: no key reads one)
    if (!fPrewarmWalk) {
        plWGPUTextureRef* ref = IGetULutTextureRef();

        hsRefCnt_SafeAssign(fLayerRef[0], ref);
        fD3DDevice->SetTexture(0, ref);
    }

    fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);

    fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);

    slave->fPipeData = renderTarg; // restored after the prewarm's walk (IPrewarmTargets)

    // Enable ZBuffering w/ write
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    fLayerState[0].fZFlags &= ~hsGMatState::kZMask;

    // Clear the render target:
    // alpha to white ensures no shadow where there's no caster
    // color to black in case we ever get blurring going
    // Z to 1 (0 with the reverse Z of plPerspDirSlave)
    const float clear[4] = {float((clearColor >> 16) & 0xff) / 255.f, float((clearColor >> 8) & 0xff) / 255.f,
                            float(clearColor & 0xff) / 255.f, float((clearColor >> 24) & 0xff) / 255.f};
    if (slave->ReverseZ()) {
        fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL);
        fDevice.Clear(true, clear, true, 0.0f);
    } else {
        fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
        fDevice.Clear(true, clear, true, 1.0f);
    }

    // Bring the viewport in (AFTER THE CLEAR) to protect the alpha boundary.
    fView.GetViewTransform().SetViewPort(1, 1, (float)(slave->fWidth - 2), (float)(slave->fHeight - 2), false);
    fDevice.SetViewport();

    inlEnsureLightingOff();

    // See ISetupShadowLight below for how the shadow light is used.
    // The shadow light isn't used in generating the shadow map, it's used
    // in projecting the shadow map onto the scene. (Not in the prewarm's walk: it takes a light and a D3D index for
    // the frame, and no key carries a light.)
    if (!fPrewarmWalk)
        ISetupShadowLight(slave);

    return true;
}

void plWGPUPipeline::ISetupShadowLight(plShadowSlave* slave)
{
    // plDXPipeline::ISetupShadowLight: the shadow light modulates the projected shadow by N.L and
    // attenuates it through its diffuse colour (the slave's power).
    plWGPULightRef* lRef = INextShadowLight(slave);

    lRef->fD3DInfo.Diffuse.r = lRef->fD3DInfo.Diffuse.g = lRef->fD3DInfo.Diffuse.b = slave->fPower;

    slave->fSelfShadowOn = false;

    if (slave->Positional()) {
        hsPoint3 position = slave->fLightPos;
        lRef->fD3DInfo.Position.x = position.fX;
        lRef->fD3DInfo.Position.y = position.fY;
        lRef->fD3DInfo.Position.z = position.fZ;

        const float maxRange = 32767.f;
        lRef->fD3DInfo.Range = maxRange;
        lRef->fD3DInfo.Attenuation0 = 1.f;
        lRef->fD3DInfo.Attenuation1 = 0;
        lRef->fD3DInfo.Attenuation2 = 0;

        lRef->fD3DInfo.Type = D3DLIGHT_POINT;
    } else {
        hsVector3 dir = slave->fLightDir;
        lRef->fD3DInfo.Direction.x = dir.fX;
        lRef->fD3DInfo.Direction.y = dir.fY;
        lRef->fD3DInfo.Direction.z = dir.fZ;

        lRef->fD3DInfo.Type = D3DLIGHT_DIRECTIONAL;
    }

    fD3DDevice->SetLight(lRef->fD3DIndex, &lRef->fD3DInfo);

    slave->fLightIndex = lRef->fD3DIndex;
}

plWGPULightRef* plWGPUPipeline::INextShadowLight(plShadowSlave* slave)
{
    // plDXPipeline::INextShadowLight: a scratch light for this frame.
    plWGPULightRef* lightRef = fLights.fShadowLights.next([this] {
        plWGPULightRef* lRef = new plWGPULightRef();

        /// Assign stuff and update
        lRef->fD3DIndex = fLights.ReserveD3DIndex();
        lRef->fOwner = nullptr;
        lRef->fD3DDevice = fD3DDevice;

        lRef->Link(&fLights.fRefList);

        // Neutralize it until we need it.
        fD3DDevice->LightEnable(lRef->fD3DIndex, false);

        // Some things never change.
        memset(&lRef->fD3DInfo, 0, sizeof(lRef->fD3DInfo));
        lRef->fD3DInfo.Ambient.r = lRef->fD3DInfo.Ambient.g = lRef->fD3DInfo.Ambient.b = 0;
        lRef->fD3DInfo.Specular.r = lRef->fD3DInfo.Specular.g = lRef->fD3DInfo.Specular.b = 0;

        return lRef;
    });
    slave->fLightRefIdx = fLights.fShadowLights.size() - 1;

    return lightRef;
}

bool plWGPUPipeline::IPopShadowCastState(plShadowSlave* slave)
{
    // plDXPipeline::IPopShadowCastState
    fView = fViewStack.top();
    fViewStack.pop();

    PopRenderTarget();
    fView.fXformResetFlags = fView.kResetProjection | fView.kResetCamera;

    return true;
}

void plWGPUPipeline::IMakeRenderTargetPools()
{
    // plDXPipeline::IMakeRenderTargetPools: the shadow map pools, each size sharing its first
    // target's depth surface; 32:32, 64:16, 128:8, 256:4, 512:0, with a nullptr sentinel.
    IReleaseRenderTargetPools(); // Just to be sure.

    plConst(size_t) kCount[kMaxRenderTargetNext] = {
        0,  // 1x1
        0,  // 2x2
        0,  // 4x4
        0,  // 8x8
        0,  // 16x16
        32, // 32x32
        16, // 64x64
        8,  // 128x128
        4,  // 256x256
        0   // 512x512
    };
    for (int i = 0; i < kMaxRenderTargetNext; i++) {
        std::vector<plRenderTarget*>* pool = nullptr;
        switch (i) {
            case 5:
                pool = &fRenderTargetPool32;
                break;
            case 6:
                pool = &fRenderTargetPool64;
                break;
            case 7:
                pool = &fRenderTargetPool128;
                break;
            case 8:
                pool = &fRenderTargetPool256;
                break;
            case 9:
                pool = &fRenderTargetPool512;
                break;
            default:
                break;
        }
        if (pool) {
            pool->resize(kCount[i] + 1);
            (*pool)[0] = nullptr;
            (*pool)[kCount[i]] = nullptr;
            for (size_t j = 0; j < kCount[i]; j++) {
                uint16_t flags = plRenderTarget::kIsTexture | plRenderTarget::kIsProjected;
                uint8_t  bitDepth = 32;
                uint8_t  zDepth = 24;
                uint8_t  stencilDepth = 0;

                // If we ever allow non-square shadows, change this.
                int width = 1 << i;
                int height = width;

                plRenderTarget* rt = new plRenderTarget(flags, width, height, bitDepth, zDepth, stencilDepth);

                // If we've failed to create our render target ref, we're probably out of
                // video memory. This guy just doesn't get a shadow.
                if (!SharedRenderTargetRef((*pool)[0], rt)) {
                    delete rt;
                    pool->resize(j + 1);
                    (*pool)[j] = nullptr;
                    break;
                }
                (*pool)[j] = rt;
            }
        }
    }
    plWGPULog("shadow render target pools: 32x32 {} 64x64 {} 128x128 {} 256x256 {} 512x512 {}; render targets {} ({.1f} MiB)",
              fRenderTargetPool32.size() ? fRenderTargetPool32.size() - 1 : 0,
              fRenderTargetPool64.size() ? fRenderTargetPool64.size() - 1 : 0,
              fRenderTargetPool128.size() ? fRenderTargetPool128.size() - 1 : 0,
              fRenderTargetPool256.size() ? fRenderTargetPool256.size() - 1 : 0,
              fRenderTargetPool512.size() ? fRenderTargetPool512.size() - 1 : 0,
              fRenderTargetStats.made, double(fRenderTargetStats.bytes) / (1024. * 1024.));
}

void plWGPUPipeline::IResetRenderTargetPools()
{
    // plDXPipeline::IResetRenderTargetPools
    for (int i = 0; i < kMaxRenderTargetNext; i++) {
        fRenderTargetNext[i] = 0;
        fBlurScratchRTs[i] = nullptr;
        fBlurDestRTs[i] = nullptr;
    }

    fLights.fShadowLights.clear();
}

void plWGPUPipeline::IReleaseRenderTargetPools()
{
    // plDXPipeline::IReleaseRenderTargetPools
    for (std::vector<plRenderTarget*>* pool : {&fRenderTargetPool512, &fRenderTargetPool256, &fRenderTargetPool128,
                                               &fRenderTargetPool64, &fRenderTargetPool32}) {
        for (size_t i = 0; i < pool->size(); i++) {
            delete (*pool)[i];
            (*pool)[i] = nullptr;
        }
        pool->clear();
    }

    for (int i = 0; i < kMaxRenderTargetNext; i++) {
        fRenderTargetNext[i] = 0;
        fBlurScratchRTs[i] = nullptr;
        fBlurDestRTs[i] = nullptr;
    }
}

bool plWGPUPipeline::IPrepShadowCaster(const plShadowCaster* caster)
{
    // plDXPipeline::IPrepShadowCaster: prep each drawable of the caster once, and skin its
    // cast spans.
    static hsBitVector done;
    done.Clear();
    const std::vector<plShadowCastSpan>& castSpans = caster->Spans();

    for (size_t i = 0; i < castSpans.size(); i++) {
        if (!done.IsBitSet(i)) {
            // We haven't already done this castSpan
            plDrawableSpans* drawable = castSpans[i].fDraw;

            // Start a visList with this index.
            static std::vector<int16_t> visList;
            visList.clear();
            visList.emplace_back((int16_t)(castSpans[i].fIndex));

            // We're about to have done this castSpan.
            done.SetBit(i);

            // Look forward through castSpans for any other spans
            // with the same drawable, and add them to visList.
            for (size_t j = i + 1; j < castSpans.size(); j++) {
                if (!done.IsBitSet(j) && (castSpans[j].fDraw == drawable)) {
                    visList.emplace_back((int16_t)(castSpans[j].fIndex));
                    done.SetBit(j);
                }
            }
            // That's all, prep the drawable.
            drawable->PrepForRender(this);

            // Do any software skinning.
            if (!ISoftwareVertexBlend(drawable, visList))
                return false;
        }
    }

    return true;
}

bool plWGPUPipeline::IRenderShadowCaster(plShadowSlave* slave)
{
    // plDXPipeline::IRenderShadowCaster: render the shadow caster into the slave's map.
    const plShadowCaster* caster = slave->fCaster;

    // Setup to render into the slave's render target.
    if (!IPushShadowCastState(slave))
        return false;

    // Get the shadow caster ready to render (not in the prewarm's walk: PrepForRender and the software skinning
    // write the caster's buffers, which the walk does not read).
    if (!fPrewarmWalk && !IPrepShadowCaster(slave->fCaster))
        return false;

    // for each shadowCaster.fSpans
    for (const plShadowCastSpan& castSpan : caster->Spans()) {
        plDrawableSpans* dr = castSpan.fDraw;
        const plSpan*    sp = castSpan.fSpan;

        hsAssert(sp->fTypeMask & plSpan::kIcicleSpan, "Shadow casting from non-trimeshes not currently supported");

        // render shadowcaster.fSpans[i] to rendertarget
        if (!(sp->fProps & plSpan::kPropNoShadowCast))
            IRenderShadowCasterSpan(slave, dr, *(const plIcicle*)sp);

        // Keep track of which shadow slaves this span was rendered into.
        // If self-shadowing is off, we use that to determine not to
        // project the shadow map onto its source geometry.
        if (!fPrewarmWalk)
            sp->SetShadowBit(slave->fIndex); // index set in SubmitShadowSlave
    }

    // The prewarm's walk blurs too (IPrewarmTargets restores the pools, the blur targets and the slave): its passes
    // record the pool targets' formats and start their pipelines.
    // Debug only.
    float blurScale = plShadowMaster::GetGlobalMaxBlur();
    if (blurScale >= 0.f)
        slave->fBlurScale = blurScale;

    // If this shadow requests being blurred, do it.
    if (slave->fBlurScale > 0.f)
        IBlurShadowMap(slave);

    // Finished up, restore previous state.
    IPopShadowCastState(slave);

    return true;
}

void plWGPUPipeline::IPreprocessShadows()
{
    // plDXPipeline::IPreprocessShadows: render the shadow map of every submitted slave.
    // Mark our shared resources as free to be used.
    IResetRenderTargetPools();

    // Some board (possibly the Parhelia) freaked if anistropic filtering
    // was enabled when rendering to a render target.
    ISetAnisotropy(false);

    fCounters.shadowSlaves = uint32_t(fShadows.size());

    // The test harness's shadow rows record each slave's prepared fields here, before any cast
    // and in fShadows order, because the cast changes them: IPushShadowCastState calls
    // plShadowSlave::SetupViewTransform, which sets kReverseCull.
#ifdef PLASMA_TEST_HOOKS
    if (fRowsArmed)
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            for (plShadowSlave* slave : fShadows)
                observer->OnShadowMap(*this, slave, plWGPUPipelineObserver::kSlavePrepared);
#endif

    // Generate a shadow map for each submitted shadow slave.
    for (size_t iSlave = 0; iSlave < fShadows.size(); iSlave++) {
        plShadowSlave* slave = fShadows[iSlave];

        // Any trouble, remove it from the list for this frame.
        if (!IRenderShadowCaster(slave)) {
#ifdef PLASMA_TEST_HOOKS
            if (fRowsArmed)
                if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                    observer->OnShadowMap(*this, slave, plWGPUPipelineObserver::kSlaveFailed);
#endif
            fShadows.erase(fShadows.begin() + iSlave);
            iSlave--;
            continue;
        }
        fCounters.shadowCasters++;
#ifdef PLASMA_TEST_HOOKS
        if (fRowsArmed)
            if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                observer->OnShadowMap(*this, slave, plWGPUPipelineObserver::kSlaveCast);
#endif
    }

    // Restore
    ISetAnisotropy(true);
}

void plWGPUPipeline::ISetupShadowRcvTextureStages(hsGMaterial* mat)
{
    // plDXPipeline::ISetupShadowRcvTextureStages. The material colour sources, the fog
    // enable and mode, the cull mode and the vertex blend stay as the span's last pass left
    // them in the emulated device.
    // Setup for nil shaders to get us back to fixed function pipeline.
    ISetShaders(nullptr, nullptr);

    // We're whacking about with renderstate independent of current material,
    // so make sure the next span processes it's material, even if it's the
    // same one.
    fForceMatHandle = true;

    // Set the D3D lighting/material model
    ISetShadowLightState(mat);

    // Zbuffering on read-only
    fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    fLayerState[0].fZFlags &= ~hsGMatState::kZMask;
    fLayerState[0].fZFlags |= hsGMatState::kZNoZWrite;

    // Stage 0: the slave's map (set per slave), MODULATE(texture, diffuse), alpha texture.
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

    if (fLayerUVWSrcs[0] != D3DTSS_TCI_CAMERASPACEPOSITION) {
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        fLayerUVWSrcs[0] = D3DTSS_TCI_CAMERASPACEPOSITION;
    }

    // Set texture to clamp
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    fLayerState[0].fClampFlags = hsGMatState::kClampTexture;

    // Stage 1: the ULut. ColorArg1 = 1 - ULut, ColorArg2 = Current, ColorOp = Modulate;
    // AlphaArg1 = ULut, AlphaArg2 = Current, AlphaOp = Subtract. (The prewarm's walk makes and binds no texture.)
    if (!fPrewarmWalk) {
        plWGPUTextureRef* ref = IGetULutTextureRef();
        hsRefCnt_SafeAssign(fLayerRef[1], ref);
        fD3DDevice->SetTexture(1, ref);
    }

    fD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_COMPLEMENT);
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);

    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SUBTRACT);
    fLayerState[1].fBlendFlags = uint32_t(-1);

    if (fLayerUVWSrcs[1] != D3DTSS_TCI_CAMERASPACEPOSITION) {
        fD3DDevice->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
        fLayerUVWSrcs[1] = D3DTSS_TCI_CAMERASPACEPOSITION;
    }
    if (D3DTTFF_COUNT3 != fLayerXformFlags[1]) {
        fLayerXformFlags[1] = D3DTTFF_COUNT3;
        fD3DDevice->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT3);
    }

    // Set texture to clamp
    fD3DDevice->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    fD3DDevice->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    fLayerState[1].fClampFlags = hsGMatState::kClampTexture;

    int iNextStage = 2;

    // If mat's base layer is alpha'd, and we have > 3 TMU's factor
    // in the base layer's alpha.
    if ((fMaxLayersAtOnce > 3) && mat->GetLayer(0)->GetTexture() && (mat->GetLayer(0)->GetBlendFlags() & hsGMatState::kBlendAlpha)) {
        plLayerInterface* layer = mat->GetLayer(0);

        // If the following conditions are met, it means that layer 1 is a better choice to
        // get the transparency from (the alpha hack: vertex alpha simulated by an invisible
        // second layer alpha LUT).
        if ((layer->GetMiscFlags() & hsGMatState::kMiscBindNext) && mat->GetLayer(1) && !(mat->GetLayer(1)->GetMiscFlags() & hsGMatState::kMiscNoShadowAlpha) && !(mat->GetLayer(1)->GetBlendFlags() & hsGMatState::kBlendNoTexAlpha) && mat->GetLayer(1)->GetTexture())
            layer = mat->GetLayer(1);

        // Take the texture alpha and modulate the color so far with it. In
        // the final shadow map, black will have no effect, white will be maximal
        // darkening.
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLORARG2, D3DTA_CURRENT);
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLOROP, D3DTOP_MODULATE);

        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);

        // Blend flags to layer blend (alpha +- complement)
        fLayerState[iNextStage].fBlendFlags = uint32_t(-1);

        // Clamp to whatever the texture wants.
        if (fLayerState[iNextStage].fClampFlags ^ layer->GetClampFlags()) {
            fLayerState[iNextStage].fClampFlags = layer->GetClampFlags();
            IHandleStageClamp(iNextStage);
        }

        // Shade to 0
        fLayerState[iNextStage].fShadeFlags = 0;

        // ZFlags to ZNoZWrite
        fLayerState[iNextStage].fZFlags = hsGMatState::kZNoZWrite;

        // MiscFlags to layer's misc flags
        fLayerState[iNextStage].fMiscFlags = layer->GetMiscFlags();

        // Set up whatever UVW transform the layer normally uses.
        IHandleStageTransform(iNextStage, layer);

        // Normal UVW source.
        uint32_t uvwSrc = layer->GetUVWSrc();

        if (fLayerUVWSrcs[iNextStage] != uvwSrc) {
            fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_TEXCOORDINDEX, uvwSrc);
            fLayerUVWSrcs[iNextStage] = uvwSrc;
        }

        uint32_t xformFlags;
        if (layer->GetMiscFlags() & hsGMatState::kMiscPerspProjection)
            xformFlags = D3DTTFF_COUNT3 | D3DTTFF_PROJECTED;
        else if (uvwSrc & (plLayerInterface::kUVWNormal | plLayerInterface::kUVWPosition | plLayerInterface::kUVWReflect))
            xformFlags = D3DTTFF_COUNT3;
        else
            xformFlags = D3DTTFF_COUNT2;

        if (xformFlags != fLayerXformFlags[iNextStage]) {
            fLayerXformFlags[iNextStage] = xformFlags;
            fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_TEXTURETRANSFORMFLAGS, xformFlags);
        }

        // This ref should be pretty safe to use, because we just rendered it. (Not bound by the prewarm's walk.)
        if (!fPrewarmWalk) {
            plWGPUTextureRef* lRef = (plWGPUTextureRef*)layer->GetTexture()->GetDeviceRef();

            hsRefCnt_SafeAssign(fLayerRef[iNextStage], lRef);
            fD3DDevice->SetTexture(iNextStage, lRef);
        }

        iNextStage++;

        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLORARG1, D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE);
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLORARG2, D3DTA_CURRENT);
        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLOROP, D3DTOP_MODULATE);

        fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        fLayerState[iNextStage].fBlendFlags = uint32_t(-1);

        iNextStage++;
    }

    fLayerState[iNextStage].fBlendFlags = uint32_t(-1);

    // And seal it up
    fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(iNextStage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    fLayerState[iNextStage].fBlendFlags = uint32_t(-1);

    fLastEndingStage = 0;

    // Now set the frame buffer blend: Src * ZERO + Dst * InvSrc (white darkens, black has no
    // effect).
    fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);

    fLayerState[0].fBlendFlags = uint32_t(-1);

    // Turn on alpha test. Alpha of zero means the shadow map depth
    // is greater or equal to the surface depth, i.e. the surface
    // is between the shadow caster and the light and doesn't receive
    // shadow.
    fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    fD3DDevice->SetRenderState(D3DRS_ALPHAREF, 0x00000001);
    fLayerState[0].fBlendFlags |= hsGMatState::kBlendTest;

    fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    fLayerState[0].fShadeFlags &= ~hsGMatState::kShadeSpecular;

    // Set fog color to black
    // We should automatically reset it, because our blend mode is -1'd.
    fD3DDevice->SetRenderState(D3DRS_FOGCOLOR, 0);
}

void plWGPUPipeline::ISetupShadowSlaveTextures(plShadowSlave* slave)
{
    // plDXPipeline::ISetupShadowSlaveTextures
    D3DMATRIX tXfm;

    hsMatrix44 c2w = GetCameraToWorld();

    // Stage 0: the slave's map, with the slave's camera to texture transform.
    plRenderTarget* renderTarg = (plRenderTarget*)slave->fPipeData;
    hsAssert(renderTarg, "Processing a slave that hasn't been rendered");
    if (!renderTarg)
        return;
    plWGPUTextureRef* ref = (plWGPUTextureRef*)renderTarg->GetDeviceRef();
    hsAssert(ref, "Shadow map ref should have been made when it was rendered");
    if (!ref)
        return;

    hsRefCnt_SafeAssign(fLayerRef[0], ref);
    fD3DDevice->SetTexture(0, ref);

    hsMatrix44 cameraToTexture = slave->fWorldToTexture * c2w;
    IMatrix44ToD3DMatrix(tXfm, cameraToTexture);

    fD3DDevice->SetTransform(D3DTS_TEXTURE0, &tXfm);
    fLayerTransform[0] = true;

    // Directional lights (ortho projection) just use COUNT2, point lights use COUNT3|PROJECTED.
    uint32_t xformFlags = slave->fView.GetOrthogonal() ? D3DTTFF_COUNT2 : D3DTTFF_COUNT3 | D3DTTFF_PROJECTED;

    if (xformFlags != fLayerXformFlags[0]) {
        fLayerXformFlags[0] = xformFlags;
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, xformFlags);
    }

    // Stage 1: the lut. Set the texture transform to slave's fRcvLUT
    hsMatrix44 cameraToLut = slave->fRcvLUT * c2w;
    IMatrix44ToD3DMatrix(tXfm, cameraToLut);

    fD3DDevice->SetTransform(D3DTS_TEXTURE1, &tXfm);
    fLayerTransform[1] = true;
}

void plWGPUPipeline::ISetShadowLightState(hsGMaterial* mat)
{
    // plDXPipeline::ISetShadowLightState: only the shadow light lights the surface;
    // the material is diffuse = layer 0 opacity, everything else 0; the colour-source states
    // are left as they are.
    IDisableLightsForShadow();
    inlEnsureLightingOn();

    fCurrLightingMethod = plSpan::kLiteShadow;

    static D3DMATERIAL9 d3dMat;
    if (mat && mat->GetNumLayers() && mat->GetLayer(0))
        d3dMat.Diffuse.r = d3dMat.Diffuse.g = d3dMat.Diffuse.b = mat->GetLayer(0)->GetOpacity();
    else
        d3dMat.Diffuse.r = d3dMat.Diffuse.g = d3dMat.Diffuse.b = 1.f;
    d3dMat.Diffuse.a = 1.f;

    fD3DDevice->SetMaterial(&d3dMat);
    fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0);
}

void plWGPUPipeline::IDisableLightsForShadow()
{
    // plDXPipeline::IDisableLightsForShadow
    for (int i = 0; i < fLights.fLastIndex + 1; i++) {
        if (fLights.fEnabledFlags.IsBitSet(i)) {
            fD3DDevice->LightEnable(i, false);
        }
    }
    fLights.fEnabledFlags.Clear();
}
