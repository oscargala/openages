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

#include <emscripten/emscripten.h>

#include <string_theory/format>

#include "HeadSpin.h"
#include "plAvatar/plArmatureMod.h"
#include "plAvatar/plAvatarClothing.h"
#include "plAvatar/plClothingLayout.h"
#include "plDrawable/plDrawableSpans.h"
#include "plGImage/plMipmap.h"
#include "plPipeline/plRenderTarget.h"
#include "plSurface/hsGMaterial.h"
#include "plSurface/plLayer.h"
#include "plWGPUPipeline.h"
#include "plWGPUPipeline_Private.h"
#include "pnKeyedObject/plKey.h"
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif

using namespace plWGPUD3D9;

//// Avatar texture rendering ////////////////////////////////////////////////////

void plWGPUPipeline::IPreprocessAvatarTextures()
{
    // plDXPipeline::IPreprocessAvatarTextures: compose each outfit's texture into its
    // render target from the base texture and the items' layers.
    // The prewarm's walk (plWGPUPipeline::IPrewarmTargets) runs it too, to start each quad's pipeline: it frees,
    // assigns and clears nothing (the outfits, their targets and dirty items stay for the frame's compose), counts and
    // reports nothing, and makes or binds no texture (IDrawClothingQuad).
    if (!fPrewarmWalk)
        IClearClothingOutfits(&fPrevClothingOutfits); // Frees anyone used last frame that we don't need this frame

    if (fClothingOutfits.empty())
        return;

#ifdef PLASMA_TEST_HOOKS
    // The test harness's clothing checks: the composites' stage-0 bias lines and the CPU recomposition of the
    // composites it reads back (plWGPUPipelineObserver.h).
    plWGPUPipelineObserver* observer = fPrewarmWalk ? nullptr : plWGPUPipelineObserver::Get();
    if (observer)
        observer->OnClothing(*this, plWGPUPipelineObserver::kComposeBegin, plWGPUClothingView());
#endif

    plMipmap* itemBufferTex = nullptr;

    fForceMatHandle = true;
    ISetShaders(nullptr, nullptr); // Has a side effect of futzing with our cull settings...

    // The stream source and FVF (D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0)) are the
    // draw's own (IDrawClothingQuad).
    fD3DDevice->SetTransform(D3DTS_VIEW, &d3dIdentityMatrix);
    fD3DDevice->SetTransform(D3DTS_WORLD, &d3dIdentityMatrix);
    fD3DDevice->SetTransform(D3DTS_PROJECTION, &d3dIdentityMatrix);
    fD3DDevice->SetRenderState(D3DRS_CULLMODE, fDevice.fCurrCullMode = D3DCULL_NONE);
    fD3DDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    fLayerState[0].fZFlags &= ~hsGMatState::kZMask;
    fLayerState[0].fZFlags |= hsGMatState::kZNoZWrite | hsGMatState::kZNoZRead;
    if (fLayerUVWSrcs[0] != 0) {
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        fLayerUVWSrcs[0] = 0;
    }
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    fD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    fLayerState[0].fClampFlags = hsGMatState::kClampTexture;
    if (D3DTTFF_DISABLE != fLayerXformFlags[0]) {
        fLayerXformFlags[0] = D3DTTFF_DISABLE;
        fD3DDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    }
    fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    fLayerState[0].fShadeFlags &= ~hsGMatState::kShadeSpecular;
    fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    fCurrFog.fEnvPtr = nullptr;
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
    fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
    fLayerState[0].fBlendFlags = uint32_t(-1);
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    fLayerState[1].fBlendFlags = uint32_t(-1);
    inlEnsureLightingOff();

    for (plClothingOutfit* co : fClothingOutfits) {
        if (co->fBase == nullptr || co->fBase->fBaseTexture == nullptr)
            continue;

        plRenderTarget* rt = plRenderTarget::ConvertNoRef(co->fTargetLayer->GetTexture());
        if (rt != nullptr && co->fDirtyItems.Empty()) {
            // we've still got our valid RT from last frame and we have nothing to do.
            continue;
        }

        if (rt == nullptr) {
            if (fPrewarmWalk) {
                // The walk takes no target from the pool (IGetNextAvRT does not check the pool's end, and only the
                // frame's IClearClothingOutfits frees last frame's): every pool target has the same formats
                // (pl3DPipeline::IFillAvRTPool), so the first stands for the one the compose will take.
                if (fAvRTPool.empty())
                    continue;
                rt = fAvRTPool.front();
            } else {
                rt = IGetNextAvRT();
                co->fTargetLayer->SetTexture(rt);
            }
        }

        if (!fPrewarmWalk)
            fCounters.clothingOutfits++;
#ifdef PLASMA_TEST_HOOKS
        if (observer) {
            plWGPUClothingView view;
            view.outfit = co;
            view.target = rt;
            observer->OnClothing(*this, plWGPUPipelineObserver::kOutfitBegin, view);
        }
#endif

        PushRenderTarget(rt);
        fDevice.SetViewport(0.f, 0.f, float(rt->GetWidth()), float(rt->GetHeight()));

        float uOff = 0.5f / rt->GetWidth();
        float vOff = 0.5f / rt->GetHeight();

        // Copy over the base
        fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        fD3DDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
        fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, 0xffffffff);
        fLayerState[0].fBlendFlags = uint32_t(-1);
        IDrawClothingQuad(-1.f, -1.f, 2.f, 2.f, uOff, vOff, co->fBase->fBaseTexture);
        plClothingLayout* layout = plClothingMgr::GetClothingMgr()->GetLayout(co->fBase->fLayoutName);

        for (plClothingItem* item : co->fItems) {
            for (size_t j = 0; j < item->fElements.size(); j++) {
                for (int k = 0; k < plClothingElement::kLayerMax; k++) {
                    if (item->fTextures[j][k] == nullptr)
                        continue;

                    itemBufferTex = item->fTextures[j][k];
                    hsColorRGBA tint = co->GetItemTint(item, k);
                    if (k >= plClothingElement::kLayerSkinBlend1 && k <= plClothingElement::kLayerSkinLast)
                        tint.a = co->fSkinBlends[k - plClothingElement::kLayerSkinBlend1];

                    if (k == plClothingElement::kLayerBase) {
                        fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                        fD3DDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
                    } else {
                        fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                        fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                        fD3DDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
                    }
                    fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, tint.ToARGB32());
                    fLayerState[0].fBlendFlags = uint32_t(-1);
                    float screenW = (float)item->fElements[j]->fWidth / layout->fOrigWidth * 2.f;
                    float screenH = (float)item->fElements[j]->fHeight / layout->fOrigWidth * 2.f;
                    float screenX = (float)item->fElements[j]->fXPos / layout->fOrigWidth * 2.f - 1.f;
                    float screenY = (1.f - (float)item->fElements[j]->fYPos / layout->fOrigWidth) * 2.f - 1.f - screenH;
                    IDrawClothingQuad(screenX, screenY, screenW, screenH, uOff, vOff, itemBufferTex);
                }
            }
        }
        PopRenderTarget();
        if (!fPrewarmWalk)
            co->fDirtyItems.Clear();
#ifdef PLASMA_TEST_HOOKS
        if (observer) {
            plWGPUClothingView view;
            view.outfit = co;
            view.target = rt;
            observer->OnClothing(*this, plWGPUPipelineObserver::kOutfitEnd, view);
        }
#endif
    }
    // Nothing else sets this render state, so let's just set it back to the default to be safe
    fD3DDevice->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
    fView.fXformResetFlags = fView.kResetAll;

    if (!fPrewarmWalk)
        fClothingOutfits.swap(fPrevClothingOutfits);
}

void plWGPUPipeline::IPrewarmOutfits()
{
    // The submitted outfits' drawables as this frame's scene and render requests draw them: after BeginRender's
    // compose (IPreprocessAvatarTextures, plDXPipeline.cpp:2973) has given each outfit's target layer its render
    // target (plDXPipeline.cpp:11766-11769). Every clothing span draws with the outfit's material
    // (plClothingOutfit::fMaterial, plAvatarClothing.cpp:1401, plInstanceDrawInterface.cpp:121-123), so before the
    // first compose, or after a link released the outfit (pl3DPipeline::IClearClothingOutfits), the room, joined and
    // re-walk seats walk its base and receive passes untextured (IHandleTextureMode's no-texture branch,
    // plDXPipeline.cpp:6384-6395; ISetupShadowRcvTextureStages' base-alpha stages need layer 0's texture,
    // plDXPipeline.cpp:11459). Each outfit IPreprocessAvatarTextures will assign (a base texture and no target,
    // plDXPipeline.cpp:11756-11769) is walked here with the target layer holding a stand-in, the first pool target (a
    // 2D render target, as each the compose takes: pl3DPipeline::IFillAvRTPool); the avatar's drawable is the
    // outfit's (plClothingOutfit::MsgReceive, plAvatarClothing.cpp:1196). Run by IPrewarmTargets' walk after the
    // shadow casts and the composites, which see the state from before the compose (BeginRender: IPreprocessShadows,
    // then IPreprocessAvatarTextures, plDXPipeline.cpp:2972-2973), under the main target. The stand-ins stay for
    // IPrewarmRequests: the frame's render requests draw after BeginRender too (plClient::IDraw: BeginRender,
    // plClient.cpp:1929, then IProcessPreRenderRequests, :1942, and IProcessPostRenderRequests, :1952), so their draws
    // of the outfit's spans read the target. IPrewarmClearStandIns takes them out again.
    if (fAvRTPool.empty())
        return;
    plRenderTarget* standIn = fAvRTPool.front();
    for (plClothingOutfit* co : fClothingOutfits) {
        if (co->fBase == nullptr || co->fBase->fBaseTexture == nullptr || co->fTargetLayer == nullptr ||
            co->fTargetLayer->GetTexture() != nullptr || co->fAvatar == nullptr)
            continue;
        plDrawableSpans* drawable = plDrawableSpans::ConvertNoRef(co->fAvatar->FindDrawable());
        if (!drawable)
            continue;
#ifdef PLASMA_TEST_HOOKS
        const PrewarmTally before = fPrewarmTally;
        const double       t0 = emscripten_get_now();
#endif
        co->fTargetLayer->SetTexture(standIn);
#ifdef PLASMA_TEST_HOOKS
        // The identity checks: the drawable's spans that draw with the outfit's material, and of them those whose
        // layer 0 reads the stand-in (fTargetLayer itself or a layer over it).
        uint32_t outfitSpans = 0, readStandIn = 0;
        for (plSpan* span : drawable->GetSpanArray()) {
            if (!span || drawable->GetMaterial(span->fMaterialIdx) != co->fMaterial)
                continue;
            outfitSpans++;
            if (co->fMaterial->GetNumLayers() && co->fMaterial->GetLayer(0)->GetTexture() == standIn)
                readStandIn++;
        }
#endif
        fPrewarmStandIns.push_back(co);
        IPrewarmDrawable(drawable, nullptr);
#ifdef PLASMA_TEST_HOOKS
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
            const ST::string detail =
                ST::format("material {} target layer {} drawable {}: {} spans with the outfit's material, {} reading the stand-in at layer 0",
                           co->fMaterial->GetKey() ? co->fMaterial->GetKey()->GetUoid().StringIze() : ST_LITERAL("(no key)"),
                           co->fTargetLayer->GetKey() ? co->fTargetLayer->GetKey()->GetUoid().StringIze() : ST_LITERAL("(no key)"),
                           drawable->GetKey() ? drawable->GetKey()->GetUoid().StringIze() : ST_LITERAL("(no key)"),
                           outfitSpans, readStandIn);
            plWGPUPipelineObserver::PrewarmView view;
            view.seat = "outfits";
            view.detail = detail.c_str();
            view.drawables = 1;
            view.spans = fPrewarmTally.spans - before.spans;
            view.requests = fPrewarmTally.requests - before.requests;
            view.started = fPrewarmTally.started - before.started;
            view.unfilledSkinned = fPrewarmTally.unfilledSkinned - before.unfilledSkinned;
            view.unfilledParticle = fPrewarmTally.unfilledParticle - before.unfilledParticle;
            view.unfilledOther = fPrewarmTally.unfilledOther - before.unfilledOther;
            view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
            view.walkMs = emscripten_get_now() - t0;
            observer->OnPrewarm(*this, view);
        }
#endif
    }
}

void plWGPUPipeline::IPrewarmClearStandIns()
{
    // The target layers IPrewarmOutfits gave a stand-in, cleared again before the frame goes on (its compose, or the
    // dropped frame's return): the walk leaves no layer changed.
    for (plClothingOutfit* co : fPrewarmStandIns)
        co->fTargetLayer->SetTexture(nullptr);
    fPrewarmStandIns.clear();
}

// plAVTexVert (plDXPipeline.cpp) with the D3D9 (u, v, 1) of a 2-float coordinate.

void plWGPUPipeline::IDrawClothingQuad(float x, float y, float w, float h, float uOff, float vOff, plMipmap* tex)
{
    // plDXPipeline::IDrawClothingQuad (the prewarm's walk makes and binds no texture; it records the 2D mipmap
    // plDXPipeline::IDrawClothingQuad binds, plDXPipeline.cpp:11852)
    if (fPrewarmWalk) {
        fDevice.WalkTexture(0, false);
    } else {
        plWGPUTextureRef* ref = (plWGPUTextureRef*)tex->GetDeviceRef();
        if (!ref || ref->IsDirty()) {
            MakeTextureRef(tex);
            ref = (plWGPUTextureRef*)tex->GetDeviceRef();
        }
        if (!ref)
            return;
        if (!ref->fTexture)
            IReloadTexture(tex, ref);
        hsRefCnt_SafeAssign(fLayerRef[0], ref);
        fD3DDevice->SetTexture(0, ref);
    }

    plWGPUXYZTex1Vert ptr[4];
    plWGPUXYZTex1Vert vert;
    vert.fPos[0] = x;
    vert.fPos[1] = y;
    vert.fPos[2] = 0.5f;
    vert.fUv[0] = uOff;
    vert.fUv[1] = 1.f + vOff;
    vert.fUv[2] = 1.f;

    // P0
    ptr[2] = vert;

    // P1
    ptr[0] = vert;
    ptr[0].fPos[0] += w;
    ptr[0].fUv[0] += 1.f;

    // P2
    ptr[1] = vert;
    ptr[1].fPos[0] += w;
    ptr[1].fUv[0] += 1.f;
    ptr[1].fPos[1] += h;
    ptr[1].fUv[1] -= 1.f;

    // P3
    ptr[3] = vert;
    ptr[3].fPos[1] += h;
    ptr[3].fUv[1] -= 1.f;

    // DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, ptr, sizeof(plAVTexVert))
    if (!fPrewarmWalk)
        fCounters.clothingQuads++;
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = fPrewarmWalk ? nullptr : plWGPUPipelineObserver::Get()) {
        plWGPUClothingView view;
        view.x = x;
        view.y = y;
        view.w = w;
        view.h = h;
        view.uOff = uOff;
        view.vOff = vOff;
        view.texture = tex;
        observer->OnClothing(*this, plWGPUPipelineObserver::kQuad, view);
    }
#endif
    const int kind = fRowCtx.kind;
    fRowCtx.kind = kRowClothing;
    plWGPUDevice::FixedFunctionDraw draw;
    draw.vertexBytes = ptr;
    draw.vertexBytesSize = sizeof(ptr);
    draw.xyzTex1 = true;
    draw.start = 0;
    draw.count = 4;
    draw.topology = WGPUPrimitiveTopology_TriangleStrip;
    if (fPrewarmWalk)
        fDevice.PrewarmFixedFunction(draw); // the draw's pipeline, started; nothing drawn
    else
        IDrawFixedFunction(draw);
    fRowCtx.kind = kind;
}
