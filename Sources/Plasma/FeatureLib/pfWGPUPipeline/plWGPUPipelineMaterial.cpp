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

#include <algorithm>

#include "HeadSpin.h"
#include "hsFastMath.h"
#include "hsGMatState.inl"
#include "plDrawable/plSpanTypes.h"
#include "plGImage/plCubicEnvironmap.h"
#include "plGImage/plMipmap.h"
#include "plGLight/plLightInfo.h"
#include "plProfile.h"
#include "plSurface/hsGMaterial.h"
#include "plSurface/plLayer.h"
#include "plSurface/plLayerInterface.h"
#include "plWGPULog.h"
#include "plWGPUPipeline.h"

using namespace plWGPUD3D9;

// plDXPipeline.cpp's profile counters (plWGPUPipeline.cpp creates LayChange)
plProfile_Extern(LayChange);
plProfile_Extern(MatChange);

//// Materials /////////////////////////////////////////////////////////////////////

void plWGPUPipeline::ISetLayer(uint32_t lay)
{
    // plDXPipeline::ISetLayer. kCapsZBias is never set (the device record lacks it): the
    // upper-layer bias is the projection-matrix layer scale of IGetCameraToNDC.
    if (lay) {
        if (fCurrRenderLayer != lay) {
            fCurrRenderLayer = lay;

            plCONST(int) kBiasMult = 8;
            if (!(fSettings.fD3DCaps & kCapsZBias))
                IProjectionMatrixToDevice();
            else
                fD3DDevice->SetRenderState(D3DRS_DEPTHBIAS, kBiasMult * fCurrRenderLayer);
        }
    } else
        IBottomLayer();
}

void plWGPUPipeline::IBottomLayer()
{
    // plDXPipeline::IBottomLayer
    if (fCurrRenderLayer != 0) {
        fCurrRenderLayer = 0;
        if (!(fSettings.fD3DCaps & kCapsZBias))
            IProjectionMatrixToDevice();
        else
            fD3DDevice->SetRenderState(D3DRS_DEPTHBIAS, 0);
    }
}

plLayerInterface* plWGPUPipeline::IPushOverBaseLayer(plLayerInterface* li)
{
    // plDXPipeline::IPushOverBaseLayer
    if (!li)
        return nullptr;

    fOverLayerStack.push_back(li);

    if (!fOverBaseLayer)
        return fOverBaseLayer = li;

    fForceMatHandle = true;
    fOverBaseLayer = fOverBaseLayer->Attach(li);
    fOverBaseLayer->Eval(fTime, fFrame, 0);
    return fOverBaseLayer;
}

plLayerInterface* plWGPUPipeline::IPopOverBaseLayer(plLayerInterface* li)
{
    // plDXPipeline::IPopOverBaseLayer
    if (!li)
        return nullptr;

    fForceMatHandle = true;

    plLayerInterface* pop = fOverLayerStack.back();
    fOverLayerStack.pop_back();
    fOverBaseLayer = fOverBaseLayer->Detach(pop);

    return pop;
}

plLayerInterface* plWGPUPipeline::IPushOverAllLayer(plLayerInterface* li)
{
    // plDXPipeline::IPushOverAllLayer
    if (!li)
        return nullptr;

    fOverLayerStack.push_back(li);

    if (!fOverAllLayer) {
        fOverAllLayer = li;
        fOverAllLayer->Eval(fTime, fFrame, 0);
        return fOverAllLayer;
    }

    fForceMatHandle = true;
    fOverAllLayer = fOverAllLayer->Attach(li);
    fOverAllLayer->Eval(fTime, fFrame, 0);

    return fOverAllLayer;
}

plLayerInterface* plWGPUPipeline::IPopOverAllLayer(plLayerInterface* li)
{
    // plDXPipeline::IPopOverAllLayer
    if (!li)
        return nullptr;

    fForceMatHandle = true;

    plLayerInterface* pop = fOverLayerStack.back();
    fOverLayerStack.pop_back();
    fOverAllLayer = fOverAllLayer->Detach(pop);

    return pop;
}

int plWGPUPipeline::ISetNumActivePiggyBacks()
{
    // plDXPipeline::ISetNumActivePiggyBacks
    return fActivePiggyBacks = std::min(static_cast<size_t>(fMaxPiggyBacks), fPiggyBackStack.size());
}

void plWGPUPipeline::IPushProjPiggyBack(plLayerInterface* li)
{
    // plDXPipeline::IPushProjPiggyBack
    if (fView.fRenderState & plPipeline::kRenderNoPiggyBacks)
        return;

    fPiggyBackStack.push_back(li);
    fActivePiggyBacks = fPiggyBackStack.size() - fMatPiggyBacks;
    fForceMatHandle = true;
}

void plWGPUPipeline::IPopProjPiggyBacks()
{
    // plDXPipeline::IPopProjPiggyBacks
    if (fView.fRenderState & plPipeline::kRenderNoPiggyBacks)
        return;

    fPiggyBackStack.resize(fMatPiggyBacks);
    ISetNumActivePiggyBacks();
    fForceMatHandle = true;
}

void plWGPUPipeline::IPushPiggyBacks(hsGMaterial* mat)
{
    // plDXPipeline::IPushPiggyBacks
    hsAssert(!fMatPiggyBacks, "Push/Pop Piggy mismatch");

    if (fView.fRenderState & plPipeline::kRenderNoPiggyBacks)
        return;

    for (size_t i = 0; i < mat->GetNumPiggyBacks(); i++) {
        if (!mat->GetPiggyBack(i))
            continue;

        if ((mat->GetPiggyBack(i)->GetMiscFlags() & hsGMatState::kMiscLightMap) && IsDebugFlagSet(plPipeDbg::kFlagNoLightmaps))
            continue;

        fPiggyBackStack.push_back(mat->GetPiggyBack(i));
        fMatPiggyBacks++;
    }
    ISetNumActivePiggyBacks();
    fForceMatHandle = true;
}

void plWGPUPipeline::IPopPiggyBacks()
{
    // plDXPipeline::IPopPiggyBacks
    if (fView.fRenderState & plPipeline::kRenderNoPiggyBacks)
        return;

    fPiggyBackStack.resize(fPiggyBackStack.size() - fMatPiggyBacks);
    fMatPiggyBacks = 0;

    ISetNumActivePiggyBacks();
    fForceMatHandle = true;
}

int32_t plWGPUPipeline::IHandleMaterial(hsGMaterial* newMat, uint32_t layer, const plSpan* currSpan)
{
    // plDXPipeline::IHandleMaterial
    // No material means no draw. DX writes `!newMat && newMat->GetLayer(layer)`, which dereferences the null material
    // and is false for any other: only its null case is kept.
    if (!newMat)
        return -1;

    // If this is a bump mapping pass but the object isn't currently runtime lit, just skip.
    // Note that <layer> may change here, if we're skipping past the bump layers but there
    // are more layers (passes) to do after that.
    if (ISkipBumpMap(newMat, layer, currSpan)) {
        return -1;
    }

    // Workaround for the ATI Radeon 7500's inability to use uvw coordinates above 1.
    // If we have a layer trying to use uvw 2 or higher, skip it and any layers bound to
    // it.
    while ((layer < newMat->GetNumLayers()) && newMat->GetLayer(layer) && ((newMat->GetLayer(layer)->GetUVWSrc() & 0xf) > fSettings.fMaxUVWSrc)) {
        if (newMat->GetLayer(layer)->GetMiscFlags() & hsGMatState::kMiscBindNext)
            layer++;
        layer++;
    }
    if (layer >= newMat->GetNumLayers())
        return -1;

    // If nothing has changed, we don't need to recompute and set state.
    if (!fForceMatHandle && (newMat == fCurrMaterial && layer == fCurrLayerIdx)) {
        // Before returning, check if we have to redo our lighting
        uint32_t lightType = (currSpan != nullptr) ? (currSpan->fProps & plSpan::kLiteMask) : plSpan::kLiteMaterial;
        if (lightType != fCurrLightingMethod)
            ICalcLighting(fCurrLay, currSpan);

        if (fLayerState[0].fMiscFlags & (hsGMatState::kMiscBumpDu | hsGMatState::kMiscBumpDw))
            ISetBumpMatrices(fCurrLay, currSpan);

        return layer + fCurrNumLayers;
    }

    fForceMatHandle = false;

    fCurrLayerIdx = layer;

    // As plDXPipeline::IHandleMaterial (Stats.Show "Polys Per Material", plCalculatedProfiles.cpp); a draw's, not
    // the prewarm's walk (plWGPUPipeline::IPrewarmPipelines).
    if (!fPrewarmWalk) {
        if (newMat != fCurrMaterial)
            plProfile_Inc(MatChange);
        plProfile_Inc(LayChange);
    }

    /// Test for fail states
    if (IsDebugFlagSet(plPipeDbg::kFlagNoDecals) && (newMat->GetCompositeFlags() & hsGMaterial::kCompDecal)) {
        return -1;
    }

    /// Workaround for a D3D limitation--you're not allowed to render with a texture that you're
    /// rendering INTO. Hence we can't have self-reflecting cubicRenderTargets (damn)
    if (fCurrBaseRenderTarget != nullptr &&
        newMat->GetLayer(layer)->GetTexture() == plBitmap::ConvertNoRef(fCurrBaseRenderTarget)) {
        return -1;
    }

    /// Figure out our current states
    // Start with the base layer.
    plLayerInterface* currLay = IPushOverBaseLayer(newMat->GetLayer(layer));

    if (IsDebugFlagSet(plPipeDbg::kFlagBumpW) && (currLay->GetMiscFlags() & hsGMatState::kMiscBumpDu))
        currLay = newMat->GetLayer(fCurrLayerIdx = ++layer);

    currLay = IPushOverAllLayer(currLay);

    /// Save stuff for next time around
    ICompositeLayerState(0, currLay);
    hsRefCnt_SafeAssign(fCurrMaterial, newMat);
    fCurrLayerIdx = layer;
    fCurrLay = currLay;

    if (IsDebugFlagSet(plPipeDbg::kFlagDisableSpecular))
        fLayerState[0].fShadeFlags &= ~hsGMatState::kShadeSpecular;

    // ZIncLayer requests Z bias for upper layers.
    if (fLayerState[0].fZFlags & hsGMatState::kZIncLayer)
        ISetLayer(1);
    else
        IBottomLayer();

    /// A few debugging things
    if (IsDebugFlagSet(plPipeDbg::kFlagNoAlphaBlending))
        fLayerState[0].fBlendFlags &= ~hsGMatState::kBlendMask;

    if ((IsDebugFlagSet(plPipeDbg::kFlagBumpUV) || IsDebugFlagSet(plPipeDbg::kFlagBumpW)) && (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpChans)) {
        switch (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpChans) {
            case hsGMatState::kMiscBumpDu:
                break;
            case hsGMatState::kMiscBumpDv:
                if (!(fCurrMaterial->GetLayer(layer - 2)->GetBlendFlags() & hsGMatState::kBlendAdd)) {
                    fLayerState[0].fBlendFlags &= ~hsGMatState::kBlendMask;
                    fLayerState[0].fBlendFlags |= hsGMatState::kBlendMADD;
                }
                break;
            case hsGMatState::kMiscBumpDw:
                if (!(fCurrMaterial->GetLayer(layer - 1)->GetBlendFlags() & hsGMatState::kBlendAdd)) {
                    fLayerState[0].fBlendFlags &= ~hsGMatState::kBlendMask;
                    fLayerState[0].fBlendFlags |= hsGMatState::kBlendMADD;
                }
                break;
            default:
                break;
        }
    }

    /// Get the # of layers we can draw in this pass into fCurrNumLayers
    int oldNumLayers = fCurrNumLayers;
    ILayersAtOnce(newMat, layer);
    if (oldNumLayers != fCurrNumLayers) {
        // This hack is necessary to cover a hack necessary to cover a "limitation" in the GeForce2 drivers.
        // Basically, we have to handle NoTexAlpha/Color differently if it's stage 1 than other stages,
        // so even though the BlendFlags haven't changed, the calls to D3D are different. Another
        // way to handle this would be to have a different handler based on whether we are 2 TMU limited
        // or not, but whatever.
        if (fLayerState[1].fBlendFlags & (hsGMatState::kBlendNoTexAlpha | hsGMatState::kBlendNoTexColor))
            fLayerState[1].fBlendFlags = uint32_t(-1);
    }

    // Placed here, since it's material-dependent (or more accurately, current-layer-dependent)
    ICalcLighting(currLay, currSpan);

    // If we're bump mapping, compute the texture transforms.
    if (fLayerState[0].fMiscFlags & (hsGMatState::kMiscBumpDu | hsGMatState::kMiscBumpDw))
        ISetBumpMatrices(currLay, currSpan);

    /// Transfer states to D3D now
    IHandleFirstTextureStage(currLay);

    currLay = IPopOverAllLayer(currLay);
    currLay = IPopOverBaseLayer(currLay);
    fCurrLay = currLay;

    int nextLayer = fCurrLayerIdx + fCurrNumLayers;
    if (IsDebugFlagSet(plPipeDbg::kFlagBumpW) && (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpDw)) {
        // Bump mapping approximation using only the W (normal direction) component of lighting.
        plLayerInterface* layPtr = IPushOverAllLayer(newMat->GetLayer(fCurrLayerIdx + 2));
        if (!layPtr)
            return -1;
        ICompositeLayerState(1, layPtr);
        IHandleTextureStage(1, layPtr);
        layPtr = IPopOverAllLayer(layPtr);
        nextLayer = fCurrLayerIdx + 3;
    } else if (IsDebugFlagSet(plPipeDbg::kFlagBumpUV) && (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpDu)) {
        // Bump mapping approximation using only the UV (surface tangent directions) component of lighting.
        plLayerInterface* layPtr = IPushOverAllLayer(newMat->GetLayer(fCurrLayerIdx + 3));
        if (!layPtr)
            return -1;
        ICompositeLayerState(1, layPtr);
        IHandleTextureStage(1, layPtr);
        layPtr = IPopOverAllLayer(layPtr);
        nextLayer = fCurrLayerIdx + 2;
    } else {
        // Normal multi texturing.
        /// Loop through all multitexturing layers
        int i;
        if (fView.fRenderState & plPipeline::kRenderBaseLayerOnly)
            nextLayer = newMat->GetNumLayers();

        for (i = 1; i < fCurrNumLayers; i++) {
            plLayerInterface* layPtr = newMat->GetLayer(fCurrLayerIdx + i);
            if (!layPtr)
                return -1;

            // Can't render into a render target using same rendertarget as a texture.
            if (fCurrBaseRenderTarget &&
                layPtr->GetTexture() == (plBitmap*)(fCurrBaseRenderTarget)) {
                // Oops, just bail
                return -1;
            }

            layPtr = IPushOverAllLayer(layPtr);
            ICompositeLayerState(i, layPtr);
            IHandleTextureStage(i, layPtr);
            layPtr = IPopOverAllLayer(layPtr);
        }
    }

    // More cleanup for the DX9.0c 2 texture limitation. See ILayersAtOnce()
    if (fMaxLayersAtOnce == 2) {
        if ((fLayerState[0].fBlendFlags & hsGMatState::kBlendAdd) && (newMat->GetNumLayers() > fCurrLayerIdx + 1) && (newMat->GetLayer(fCurrLayerIdx + 1)->GetUVWSrc() & plLayerInterface::kUVWPosition)) {
            // If we're doing additive blending and the next layer is based on position,
            // it's probably a distance fade. We'd rather have our diffuse color.
            // ILayersAtOnce will already have told us we can't use it this pass.
            // Skip it so it won't draw on its own next pass.
            nextLayer++;
        }
    }

    int numActivePiggyBacks = 0;
    if (!(fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpChans) && !(fLayerState[0].fShadeFlags & hsGMatState::kShadeEmissive)) {
        /// Tack lightmap onto last stage if we have one
        numActivePiggyBacks = fActivePiggyBacks;
        if (numActivePiggyBacks > fMaxLayersAtOnce - fCurrNumLayers)
            numActivePiggyBacks = fMaxLayersAtOnce - fCurrNumLayers;
        if (numActivePiggyBacks) {
            for (int i = 0; i < numActivePiggyBacks; i++) {
                // Note that we take piggybacks off the end of fPiggyBackStack.
                plLayerInterface* layPtr = IPushOverAllLayer(fPiggyBackStack[fPiggyBackStack.size() - 1 - i]);
                if (!layPtr)
                    return -1;
                ICompositeLayerState(fCurrNumLayers + i, layPtr);
                IHandleTextureStage(fCurrNumLayers + i, layPtr);
                layPtr = IPopOverAllLayer(layPtr);
            }

            // If we've got a piggyback, plus two layers that must be drawn together, but
            // only two TMU's to work with, we're screwed. Someone has got to get skipped and
            // hope no one notices. Typically, the first (base) layer has the color info,
            // and the second the opacity. So we'll try using the projection to brighten
            // the color, ignoring the opacity.
            if ((fLayerState[0].fMiscFlags & hsGMatState::kMiscBindNext) && (fCurrNumLayers < 2))
                nextLayer++;
        }
    }
    // Declare we won't be using any more texture stages.
    IStageStop(fCurrNumLayers + numActivePiggyBacks);

    return nextLayer;
}

const hsGMatState& plWGPUPipeline::ICompositeLayerState(int which, plLayerInterface* layer)
{
    // plDXPipeline::ICompositeLayerState
    fOldLayerState[which] = fLayerState[which];
    fLayerState[which].Composite(layer->GetState(), fMatOverOn, fMatOverOff);
    if (fOldLayerState[which].fBlendFlags == uint32_t(-1))
        fOldLayerState[which].fBlendFlags = ~fLayerState[which].fBlendFlags;

    return fLayerState[which];
}

void plWGPUPipeline::IHandleFirstTextureStage(plLayerInterface* layer)
{
    // plDXPipeline::IHandleFirstTextureStage
    IHandleTextureMode(layer);
    IHandleShadeMode();
    if (fLayerState[0].Differs(fLayerState[0].fZFlags, fOldLayerState[0].fZFlags, hsGMatState::kZMask))
        IHandleZMode();
    IHandleMiscMode();

    IHandleTextureStage(0, layer);
}

void plWGPUPipeline::IHandleShadeMode()
{
    // plDXPipeline::IHandleShadeMode
    if (fLayerState[0].Differs(fLayerState[0].fShadeFlags, fOldLayerState[0].fShadeFlags, hsGMatState::kShadeSpecular)) {
        if (fLayerState[0].fShadeFlags & hsGMatState::kShadeSpecular)
            fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
        else
            fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    }
}

void plWGPUPipeline::IHandleZMode()
{
    // plDXPipeline::IHandleZMode (kZClearZ is ZFUNC ALWAYS with Z writes, no clear)
    switch (fLayerState[0].fZFlags & hsGMatState::kZMask) {
        case hsGMatState::kZClearZ:
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            break;
        case hsGMatState::kZNoZRead:
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            break;
        case hsGMatState::kZNoZWrite:
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            break;
        case hsGMatState::kZNoZRead | hsGMatState::kZClearZ:
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            break;
        case hsGMatState::kZNoZRead | hsGMatState::kZNoZWrite:
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
            break;
        case 0:
            fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
            fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            break;

        // illegal combinations
        case hsGMatState::kZClearZ | hsGMatState::kZNoZWrite:
        case hsGMatState::kZClearZ | hsGMatState::kZNoZWrite | hsGMatState::kZNoZRead:
            hsAssert(false, "Illegal combination of Z Buffer modes (Clear but don't write)");
            break;
    }
}

void plWGPUPipeline::IHandleMiscMode()
{
    // plDXPipeline::IHandleMiscMode
    if (fLayerState[0].Differs(fLayerState[0].fMiscFlags, fOldLayerState[0].fMiscFlags, hsGMatState::kMiscWireFrame)) {
        if (fLayerState[0].fMiscFlags & hsGMatState::kMiscWireFrame)
            fD3DDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
        else
            fD3DDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    }
}

void plWGPUPipeline::IHandleTextureStage(uint32_t stage, plLayerInterface* layer)
{
    // plDXPipeline::IHandleTextureStage
    hsGDeviceRef* ref = nullptr;
    plBitmap*     texture;

    // Blend mode
    if (fLayerState[stage].fBlendFlags ^ fOldLayerState[stage].fBlendFlags)
        IHandleStageBlend(stage);

    // Texture wrap/clamp mode
    if (fLayerState[stage].fClampFlags ^ fOldLayerState[stage].fClampFlags)
        IHandleStageClamp(stage);

    // UVW transform
    IHandleStageTransform(stage, layer);

    // Create the texture (if necessary) and set it to the device. The prewarm's walk (plWGPUPipeline::
    // IPrewarmPipelines) neither creates nor binds textures: no pipeline depends on them.
    if (fPrewarmWalk)
        return;
    if (texture = layer->GetTexture(); texture != nullptr) {
        ref = texture->GetDeviceRef();
        if (ref == nullptr || ref->IsDirty()) {
            // Normal textures and cubic environment maps
            if (plMipmap::ConvertNoRef(texture) != nullptr || plCubicEnvironmap::ConvertNoRef(texture) != nullptr)
                ref = MakeTextureRef(texture);
        }
    }

    if (ref != nullptr)
        IUseTextureRef(stage, ref, layer);
    else {
        fD3DDevice->SetTexture(stage, nullptr);
        hsRefCnt_SafeUnRef(fLayerRef[stage]);
        fLayerRef[stage] = nullptr;
    }
}

void plWGPUPipeline::IHandleBumpEnv(int stage, uint32_t blendFlags)
{
    // plDXPipeline::IHandleBumpEnv. D3DTOP_BUMPENVMAPLUMINANCE, the dyna-decal environment
    // bump that DX notes is unused in production, is not supported: the WGSL combiner passes
    // the current colour through for it, and DrawFixedFunction counts it.
    DWORD    current = stage ? D3DTA_CURRENT : D3DTA_DIFFUSE;
    uint32_t colorSrc = blendFlags & hsGMatState::kBlendInvertColor ? D3DTA_TEXTURE | D3DTA_COMPLEMENT : D3DTA_TEXTURE;

    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_BUMPENVMAPLUMINANCE);
    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, current);

    fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);

    const hsMatrix44& envXfm = fCurrLay->GetBumpEnvMatrix();
    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVMAT00, F2DW(envXfm.fMap[0][0]));
    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVMAT01, F2DW(envXfm.fMap[1][0]));
    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVMAT10, F2DW(envXfm.fMap[0][1]));
    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVMAT11, F2DW(envXfm.fMap[1][1]));

    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVLSCALE, F2DW(envXfm.fMap[2][2]));
    fD3DDevice->SetTextureStageState(stage, D3DTSS_BUMPENVLOFFSET, F2DW(envXfm.fMap[2][3]));
}

void plWGPUPipeline::IHandleStageBlend(int stage)
{
    // plDXPipeline::IHandleStageBlend
    const uint32_t blendFlags = fLayerState[stage].fBlendFlags;
    // If it's the base layer, handle that differently, because it's not really
    // texture stage settings, but frame buffer blend settings.
    if (stage == 0) {
        IHandleFirstStageBlend();
        return;
    }

    uint32_t colorSrc = D3DTA_TEXTURE;
    if (blendFlags & hsGMatState::kBlendInvertColor)
        colorSrc |= D3DTA_COMPLEMENT;
    // kBlendEnvBumpNext not really used.
    if (blendFlags & hsGMatState::kBlendEnvBumpNext) {
        IHandleBumpEnv(stage, blendFlags);
    } else
        switch (blendFlags & hsGMatState::kBlendMask) {
            // Alpha blending. Complicated by the ability to ignore either
            // color or alpha for any given texture. The lower end GeForces
            // don't orthogonally support settings, especially when the final
            // (3rd) stage is the diffuse color/alpha modulate and the board
            // really only wants to support 2 stages.
            case hsGMatState::kBlendAlpha:
                // If the current number of layers is 2, then we've already handled the
                // base layer, so this must be layer 1 and the final layer.
                // If the base layer has NoTexColor or this layer has NoTexColor, we need
                // to do some rearranging.
                if ((fCurrNumLayers == 2) && ((blendFlags | fLayerState[0].fBlendFlags) & hsGMatState::kBlendNoTexColor)) {
                    // If this layer AND base layer are NoTexColor, then we just want the diffuse color.
                    if ((blendFlags & hsGMatState::kBlendNoTexColor) && (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexColor)) {
                        // select diffuse color
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                    }
                    // If the base layer has NoTexColor but this layer doesn't, then we
                    // want the output to be this texture color times diffuse (ignoring base texture color).
                    else if (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexColor) {
                        // diffuse is arg2, modulate
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    }
                    // If base layer doesn't have NoTexColor, but this layer does, then
                    // we want the output to be diffuse times base texture, which is in current.
                    else if (blendFlags & hsGMatState::kBlendNoTexColor) {
                        // diffuse is arg1, modulate
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_MODULATE);
                    }
                }
                // If we get here and this layer has NoTexColor, then we MUST be on a layer
                // above 1, which means we're on an advanced enough board to handle this orthogonally,
                // i.e. one with more than 2 texture stages.
                else if (blendFlags & hsGMatState::kBlendNoTexColor) {
                    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                }
                // Finally, no NoTexColor in sight, just set it.
                else {
                    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                    fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP,
                                                     blendFlags & hsGMatState::kBlendInvertAlpha
                                                         ? D3DTOP_MODULATEINVALPHA_ADDCOLOR
                                                         : D3DTOP_BLENDTEXTUREALPHA);
                }
                // The same ordeal for alpha, and the ability to ignore the alpha on any texture.
                // Note the additional logic for how to combine the alphas of multiple textures
                // into a final FB alpha.
                // This is orthogonal to using the alpha to combine colors of two different textures.
                // The default behavior is to use the upper texture alpha to blend the upper layer color
                // with the lower texture color, but retain the lower texture alpha (modulated by diffuse)
                // for the frame buffer alpha.
                switch (blendFlags & (hsGMatState::kBlendAlphaAdd | hsGMatState::kBlendAlphaMult)) {
                    default:
                    case 0:
                        // Using alpha to blend textures, but this layer's alpha doesn't affect final FB
                        // alpha.
                        // Two layer setup with one or the other (or both) ignoring alpha.
                        if ((fCurrNumLayers == 2) && ((blendFlags | fLayerState[0].fBlendFlags) & hsGMatState::kBlendNoTexAlpha)) {
                            // Both ignoring alpha, use diffuse.
                            if ((blendFlags & hsGMatState::kBlendNoTexAlpha) && (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexAlpha)) {
                                // select diffuse alpha
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                            }
                            // Base ignoring alpha, use diffuse times this texure alpha.
                            else if (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexAlpha) {
                                // diffuse is arg2, modulate
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG1,
                                                                 blendFlags & hsGMatState::kBlendInvertAlpha
                                                                     ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                                     : D3DTA_TEXTURE);
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
                            }
                            // This ignoring alpha, use diffuse times base alpha (in current).
                            else if (blendFlags & hsGMatState::kBlendNoTexAlpha) {
                                // diffuse is arg1, modulate
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
                                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                            }
                        }
                        // Ignoring alpha or not, with more than 2 texture stages,
                        // Either way, we'll ignore this texture's alpha, because it's an upper layer
                        // and has already been used (if it's going to get used) to blend this texture's
                        // color with the lower layers.
                        else {
                            fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                            fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                        }
                        break;
                        // Alpha coming out of this stage is lower stage alpha plus this texture alpha.
                    case hsGMatState::kBlendAlphaAdd:
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_ADD);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG1,
                                                         blendFlags & hsGMatState::kBlendInvertAlpha
                                                             ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                             : D3DTA_TEXTURE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                        break;
                        // Alpha coming out of this stage is lower stage alpha times this texture alpha.
                    case hsGMatState::kBlendAlphaMult:
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG1,
                                                         blendFlags & hsGMatState::kBlendInvertAlpha
                                                             ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                             : D3DTA_TEXTURE);
                        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                        break;
                }
                break;

                // Add texture colors, pass through current alpha.
            case hsGMatState::kBlendAdd:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_ADD);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                break;

                // Multiply texture colors, pass through current alpha
            case hsGMatState::kBlendMult:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_MODULATE);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);

                if (fMaxLayersAtOnce == 2 && stage == 1) {
                    // On these boards, the only way we can do 2 textures plus diffuse is to
                    // multiply it in during stage 0, but that only gives the same result
                    // when doing a mult blend, which we won't know when setting up stage 0.
                    // Now that we know, adjust stage 0 settings.
                    fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
                    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                }
                break;

                // Dot3 texture colors, pass through current alpha.
            case hsGMatState::kBlendDot3:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                break;

                // Add signed texture colors, pass through current alpha.
            case hsGMatState::kBlendAddSigned:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                break;

                // Add signed * 2 texture colors, pass through current alpha.
            case hsGMatState::kBlendAddSigned2X:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_ADDSIGNED2X);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                break;

                // kBlendAddColorTimesAlpha is only supported for the base layer.
            case hsGMatState::kBlendAddColorTimesAlpha:
                hsAssert(false, "Blend mode unsupported on upper layers");
                break;

                // No blend, select this texture color and pass through current alpha
            case 0:
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, colorSrc);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
                fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
                break;
        }
}

void plWGPUPipeline::IHandleFirstStageBlend()
{
    // plDXPipeline::IHandleFirstStageBlend (the alpha test and the fog colour included)
    // No color, just writing out Z values.
    if (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoColor) {
        fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
        fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        fLayerState[0].fBlendFlags |= 0x80000000;
    } else {
        switch (fLayerState[0].fBlendFlags & hsGMatState::kBlendMask) {
            // Detail is just a special case of alpha, handled in construction of the texture
            // mip chain by making higher levels of the chain more transparent.
            case hsGMatState::kBlendDetail:
            case hsGMatState::kBlendAlpha:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                if (fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertFinalAlpha) {
                    if (fLayerState[0].fBlendFlags & hsGMatState::kBlendAlphaPremultiplied) {
                        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                    } else {
                        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_INVSRCALPHA);
                    }
                    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCALPHA);
                } else {
                    if (fLayerState[0].fBlendFlags & hsGMatState::kBlendAlphaPremultiplied) {
                        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                    } else {
                        fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                    }
                    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
                }
                break;
            // Multiply the final color onto the frame buffer.
            case hsGMatState::kBlendMult:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                if (fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertFinalColor) {
                    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
                    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
                } else {
                    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
                    fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
                }
                break;

            // Add final color to FB.
            case hsGMatState::kBlendAdd:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

                break;

            // Multiply final color by FB color and add it into the FB.
            case hsGMatState::kBlendMADD:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR);
                fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

                break;

            // Final color times final alpha, added into the FB.
            case hsGMatState::kBlendAddColorTimesAlpha:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                if (fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertFinalAlpha)
                    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_INVSRCALPHA);
                else
                    fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);

                break;

            // Overwrite final color onto FB
            case 0:
                fD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                fD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
                fD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);

                break;

            default: {
                hsAssert(false, "Too many blend modes specified in material");
                plLayer* lay = plLayer::ConvertNoRef(fCurrMaterial->GetLayer(fCurrLayerIdx)->BottomOfStack());
                if (lay) {
                    if (lay->GetBlendFlags() & hsGMatState::kBlendAlpha) {
                        lay->SetBlendFlags((lay->GetBlendFlags() & ~hsGMatState::kBlendMask) | hsGMatState::kBlendAlpha);
                    } else {
                        lay->SetBlendFlags((lay->GetBlendFlags() & ~hsGMatState::kBlendMask) | hsGMatState::kBlendAdd);
                    }
                }
            } break;
        }
    }
    // Blend ops, not currently used in production.
    if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, (hsGMatState::kBlendSubtract | hsGMatState::kBlendRevSubtract))) {
        if (fLayerState[0].fBlendFlags & hsGMatState::kBlendSubtract)
            fD3DDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_SUBTRACT);
        else if (fLayerState[0].fBlendFlags & hsGMatState::kBlendRevSubtract)
            fD3DDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_REVSUBTRACT);
        else
            fD3DDevice->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    }

    // AlphaTestHigh is used for reducing sort artifacts on textures that are mostly opaque or transparent, but
    // have regions of translucency in transition. Like a texture for a bush billboard. It lets there be some
    // transparency falloff, but quit drawing before it gets so transparent that draw order problems (halos)
    // become apparent.
    if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, hsGMatState::kBlendAlphaTestHigh)) {
        plConst(uint32_t) kHighAlphaTest(0x40);
        if (fLayerState[0].fBlendFlags & hsGMatState::kBlendAlphaTestHigh)
            fD3DDevice->SetRenderState(D3DRS_ALPHAREF, kHighAlphaTest);
        else
            fD3DDevice->SetRenderState(D3DRS_ALPHAREF, 0x00000001);
    }
    // Set the alpha test function, turn on for alpha blending, else off.
    if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, hsGMatState::kBlendAlpha | hsGMatState::kBlendTest | hsGMatState::kBlendAlphaAlways | hsGMatState::kBlendAddColorTimesAlpha)) {
        if ((fLayerState[0].fBlendFlags & (hsGMatState::kBlendAlpha | hsGMatState::kBlendTest | hsGMatState::kBlendAddColorTimesAlpha)) && !(fLayerState[0].fBlendFlags & hsGMatState::kBlendAlphaAlways))
            fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
        else
            fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
    }
    // Adjust the fog color based on the blend mode. Setting fog color to black for additive modes is
    // an exact solution, setting it to white for multipication is as close of an approximation to correct
    // as you're going to get with DX.
    if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, hsGMatState::kBlendAdd | hsGMatState::kBlendMult | hsGMatState::kBlendMADD | hsGMatState::kBlendAddColorTimesAlpha)) {
        if (fLayerState[0].fBlendFlags & (hsGMatState::kBlendAdd | hsGMatState::kBlendMADD | hsGMatState::kBlendAddColorTimesAlpha))
            fD3DDevice->SetRenderState(D3DRS_FOGCOLOR, 0);
        else if (fLayerState[0].fBlendFlags & hsGMatState::kBlendMult)
            fD3DDevice->SetRenderState(D3DRS_FOGCOLOR, 0xffffffff);
        else
            fD3DDevice->SetRenderState(D3DRS_FOGCOLOR, fCurrFog.fHexColor);
    }
}

void plWGPUPipeline::IHandleTextureMode(plLayerInterface* layer)
{
    // plDXPipeline::IHandleTextureMode
    plBitmap* bitmap = layer->GetTexture();
    if (bitmap) {
        // EnvBumpNext not used in production.
        if (fLayerState[0].fBlendFlags & hsGMatState::kBlendEnvBumpNext) {
            IHandleBumpEnv(0, fLayerState[0].fBlendFlags);
        }
        // If the texture stage settings have changed. Note that this
        // is a bad test, we should just be doing something like keeping
        // an array of D3D TextureStageStates as we set them and checking against
        // that directly rather than trying to infer from higher level state
        // whether we need to make the D3D call.
        else if (fSettings.fVeryAnnoyingTextureInvalidFlag || !fTexturing || (fLayerState[0].fBlendFlags ^ fOldLayerState[0].fBlendFlags) || (fCurrNumLayers + fActivePiggyBacks != fLastEndingStage)) {
            // If we're only doing one layer, just modulate texture color by diffuse and we're done.
            if ((fCurrNumLayers + fActivePiggyBacks) <= 1) {
                // See IHandleStageBlend for notes on NoTexColor.
                if (fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexColor)
                    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                else
                    fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1,
                                                 fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertColor
                                                     ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                     : D3DTA_TEXTURE);
                fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            } else {
                // See the check in IHandleStageBlend for fSettings.fMaxLayersAtOnce == 2.
                // It depends on these settings and adjusts what it needs.

                // Multitexturing, select texture color to make its way upstream on stages.
                fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
                fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1,
                                                 fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertColor
                                                     ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                     : D3DTA_TEXTURE);

                // If our NoTexColor setting has changed, for a refresh of blend state on the next stage
                // since it's affected by our NoTexColor state.
                if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, hsGMatState::kBlendNoTexColor))
                    fLayerState[1].fBlendFlags = uint32_t(-1);
            }

            // Alpha Arg1 is texture alpha (possibly complemented), and Arg2 is diffuse (possibly complemented).
            // If we want to ignore vertex alpha, select arg1
            // If we want to ignore texture alpha, select arg2
            // Otherwise (and normally) multiply the two.
            fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP,
                                             fLayerState[0].fBlendFlags & hsGMatState::kBlendNoVtxAlpha
                                                 ? D3DTOP_SELECTARG1
                                             : fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexAlpha
                                                 ? D3DTOP_SELECTARG2
                                                 : D3DTOP_MODULATE);
            fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1,
                                             fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertAlpha
                                                 ? D3DTA_TEXTURE | D3DTA_COMPLEMENT
                                                 : D3DTA_TEXTURE);
            fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE | (fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertVtxAlpha ? D3DTA_COMPLEMENT : 0));

            fTexturing = true;
        }
    }
    // Here we've no texture for the base layer, but we have more than layer.
    // Select diffuse color and alpha, and pretend we have a texture but we're ignoring its
    // color and alpha.
    else if (fCurrNumLayers + fActivePiggyBacks > 1) {
        fLayerState[0].fBlendFlags |= hsGMatState::kBlendNoTexColor | hsGMatState::kBlendNoTexAlpha;
        fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
        fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        if (fLayerState[0].Differs(fLayerState[0].fBlendFlags, fOldLayerState[0].fBlendFlags, (hsGMatState::kBlendNoTexColor | hsGMatState::kBlendNoTexAlpha)))
            fLayerState[1].fBlendFlags = uint32_t(-1);
        fTexturing = false;
    }
    // Finally, a color only (non-textured) pass. Just select diffuse.
    else {
        if (fTexturing || fSettings.fVeryAnnoyingTextureInvalidFlag) {
            fD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
            fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            fD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE | (fLayerState[0].fBlendFlags & hsGMatState::kBlendInvertVtxAlpha ? D3DTA_COMPLEMENT : 0));

            fTexturing = false;
        }
    }

    fSettings.fVeryAnnoyingTextureInvalidFlag = false;
}

void plWGPUPipeline::IHandleStageClamp(int stage)
{
    // plDXPipeline::IHandleStageClamp
    const uint32_t flags = fLayerState[stage].fClampFlags;
    switch (flags) {
        case 0:
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            break;
        case hsGMatState::kClampTextureU:
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            break;
        case hsGMatState::kClampTextureV:
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            break;
        case hsGMatState::kClampTexture:
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            fD3DDevice->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            break;
    }
}

void plWGPUPipeline::ISetBumpMatrices(const plLayerInterface* layer, const plSpan* span)
{
    // plDXPipeline::ISetBumpMatrices without the BUMP_COMPARE_MATH debug block
    hsPoint3                         spanPos = span->fWorldBounds.GetCenter();
    hsVector3                        liDir(0, 0, 0);
    const std::vector<plLightInfo*>& spanLights = span->GetLightList(false);
    float                            maxStrength = 0;
    for (size_t i = 0; i < spanLights.size(); i++) {
        float liWgt = span->GetLightStrength(i, false);
        // A light strength of 2.f means it's from a light group, and we haven't actually calculated
        // the strength. So calculate it now.
        if (liWgt == 2.f) {
            float scale;
            spanLights[i]->GetStrengthAndScale(span->fWorldBounds, liWgt, scale);
        }
        if (liWgt > maxStrength)
            maxStrength = liWgt;
        liDir += spanLights[i]->GetNegativeWorldDirection(spanPos) * liWgt;
    }
    hsFastMath::NormalizeAppr(liDir);

    static float kUVWScale = 1.f;
    float        uvwScale = kUVWScale;
    if (fLayerState[0].fBlendFlags & hsGMatState::kBlendAdd) {
        hsPoint3  viewPos = GetViewPositionWorld();
        hsVector3 cam2span(&viewPos, &spanPos);
        hsFastMath::NormalizeAppr(cam2span);
        liDir += cam2span;
        hsFastMath::NormalizeAppr(liDir);
        static float kSpecularMax = 0.1f;
        static float kSpecularMaxUV = 0.5f;
        if (IsDebugFlagSet(plPipeDbg::kFlagBumpUV))
            uvwScale *= kSpecularMaxUV;
        else
            uvwScale *= kSpecularMax;
    }

    switch (fCurrMaterial->GetLayer(fCurrLayerIdx)->GetMiscFlags() & hsGMatState::kMiscBumpChans) {
        case hsGMatState::kMiscBumpDu:
            uvwScale *= fCurrMaterial->GetLayer(fCurrLayerIdx + 3)->GetRuntimeColor().r;
            break;
        case hsGMatState::kMiscBumpDv: // This currently should never happen
            uvwScale *= fCurrMaterial->GetLayer(fCurrLayerIdx + 1)->GetRuntimeColor().r;
            break;
        case hsGMatState::kMiscBumpDw:
            uvwScale *= fCurrMaterial->GetLayer(fCurrLayerIdx + 2)->GetRuntimeColor().r;
            break;
    }
    maxStrength *= 20.f;
    if (maxStrength > 1.f)
        maxStrength = 1.f;
    liDir *= uvwScale * maxStrength;

    const float kUVWOffset = 0.5f;

    float kOffsetToRed;
    float kOffsetToGreen;
    float kOffsetToBlue;

    if (IsDebugFlagSet(plPipeDbg::kFlagBumpUV) || IsDebugFlagSet(plPipeDbg::kFlagBumpW)) {
        kOffsetToRed = 0.2f;
        kOffsetToGreen = 0.6f;
        kOffsetToBlue = 1.f;
    } else {
        kOffsetToRed = 0.f;
        kOffsetToGreen = 0.4f;
        kOffsetToBlue = 0.8f;
    }

    const hsMatrix44& l2w = span->fLocalToWorld;

    fBumpDvMatrix.fMap[0][0] = fBumpDuMatrix.fMap[0][0] = (liDir.fX * l2w.fMap[0][0] + liDir.fY * l2w.fMap[1][0] + liDir.fZ * l2w.fMap[2][0]);
    fBumpDvMatrix.fMap[0][1] = fBumpDuMatrix.fMap[0][1] = (liDir.fX * l2w.fMap[0][1] + liDir.fY * l2w.fMap[1][1] + liDir.fZ * l2w.fMap[2][1]);
    fBumpDvMatrix.fMap[0][2] = fBumpDuMatrix.fMap[0][2] = (liDir.fX * l2w.fMap[0][2] + liDir.fY * l2w.fMap[1][2] + liDir.fZ * l2w.fMap[2][2]);

    fBumpDvMatrix.fMap[0][3] = fBumpDuMatrix.fMap[0][3] = kUVWOffset;

    fBumpDuMatrix.fMap[1][3] = kOffsetToRed;
    fBumpDvMatrix.fMap[1][3] = kOffsetToGreen;

    hsMatrix44 c2w = fView.GetCameraToWorld();

    // The bump textures created so far have very strong blue components, which make anything
    // bump mapped glow. The ideal fix would be to have the artists adjust the blue component
    // to a better (lower) value, so there would be a little extra illumination where the bump
    // is straight out into the normal direction, to complement the lateral illumination.
    // Attempts so far have been unsuccessful in getting them to get a better understanding
    // of bump maps, so I've just zeroed out the contribution in the normal direction.
    plConst(int) kBumpUVOnly(true);
    if (!kBumpUVOnly) {
        fBumpDwMatrix.fMap[0][0] = (liDir.fX * c2w.fMap[0][0] + liDir.fY * c2w.fMap[1][0] + liDir.fZ * c2w.fMap[2][0]);
        fBumpDwMatrix.fMap[0][1] = (liDir.fX * c2w.fMap[0][1] + liDir.fY * c2w.fMap[1][1] + liDir.fZ * c2w.fMap[2][1]);
        fBumpDwMatrix.fMap[0][2] = (liDir.fX * c2w.fMap[0][2] + liDir.fY * c2w.fMap[1][2] + liDir.fZ * c2w.fMap[2][2]);
    } else {
        fBumpDwMatrix.fMap[0][0] = 0;
        fBumpDwMatrix.fMap[0][1] = 0;
        fBumpDwMatrix.fMap[0][2] = 0;
    }

    fBumpDwMatrix.fMap[0][3] = kUVWOffset;
    fBumpDwMatrix.fMap[1][3] = kOffsetToBlue;
}

const hsMatrix44& plWGPUPipeline::IGetBumpMatrix(uint32_t miscFlags) const
{
    // plDXPipeline::IGetBumpMatrix
    switch (miscFlags & hsGMatState::kMiscBumpChans) {
        case hsGMatState::kMiscBumpDu:
            return fBumpDuMatrix;
        case hsGMatState::kMiscBumpDv:
            return fBumpDvMatrix;
        case hsGMatState::kMiscBumpDw:
        default:
            return fBumpDwMatrix;
    }
}

bool plWGPUPipeline::ISkipBumpMap(hsGMaterial* newMat, uint32_t& layer, const plSpan* currSpan) const
{
    // plDXPipeline::ISkipBumpMap
    if (newMat && currSpan) {
        if (newMat->GetLayer(layer) && (newMat->GetLayer(layer)->GetMiscFlags() & hsGMatState::kMiscBumpChans) && (!currSpan->GetNumLights(false) || IsDebugFlagSet(plPipeDbg::kFlagNoBump))) {
            plWGPUPipeline* self = const_cast<plWGPUPipeline*>(this);
            if (!fPrewarmWalk && newMat->GetKey() && self->fBumpLogged.insert("skip " + newMat->GetKey()->GetUoid().StringIze()).second)
                plWGPULog("bump skipped (span has no lights): {} layer {}", newMat->GetKey()->GetUoid().StringIze(), layer);
            layer += 4;
            if (layer >= newMat->GetNumLayers())
                return true;
        }
    }
    return false;
}

void plWGPUPipeline::IHandleStageTransform(int stage, plLayerInterface* layer)
{
    // plDXPipeline::IHandleStageTransform (the `if( 1 || ...)` branch DX always takes)
    D3DMATRIX tXfm;

    if (fLayerState[stage].fMiscFlags & (hsGMatState::kMiscUseReflectionXform | hsGMatState::kMiscUseRefractionXform)) {
        // Reflection - this is just the camera to world, with translation removed,
        // and rotated to match cube map conventions.
        hsMatrix44 c2env = fView.GetCameraToWorld();
        c2env = fView.GetCameraToWorld();

        c2env.fMap[0][3] = c2env.fMap[1][3] = c2env.fMap[2][3] = 0.f;

        if (fLayerState[stage].fMiscFlags & hsGMatState::kMiscUseReflectionXform) {
            // This is just a rotation about X of Pi/2 (y = z, z = -y),
            // followed by flipping Z to reflect back towards us (z = -z).
            float t = c2env.fMap[1][0];
            c2env.fMap[1][0] = c2env.fMap[2][0];
            c2env.fMap[2][0] = t;

            t = c2env.fMap[1][1];
            c2env.fMap[1][1] = c2env.fMap[2][1];
            c2env.fMap[2][1] = t;

            t = c2env.fMap[1][2];
            c2env.fMap[1][2] = c2env.fMap[2][2];
            c2env.fMap[2][2] = t;
        } else { // must be kMiscUseRefractionXform
            // This is just a rotation about X of Pi/2 (y = z, z = -y),
            // followed by NOT flipping Z to reflect back towards us (z = -z).
            // In other words, same as reflection, but then c2env = c2env * scaleMatNegateZ.
            float t = c2env.fMap[1][0];
            c2env.fMap[1][0] = c2env.fMap[2][0];
            c2env.fMap[2][0] = t;

            t = c2env.fMap[1][1];
            c2env.fMap[1][1] = c2env.fMap[2][1];
            c2env.fMap[2][1] = t;

            t = c2env.fMap[1][2];
            c2env.fMap[1][2] = c2env.fMap[2][2];
            c2env.fMap[2][2] = t;

            c2env.fMap[0][2] = -c2env.fMap[0][2];
            c2env.fMap[1][2] = -c2env.fMap[1][2];
            c2env.fMap[2][2] = -c2env.fMap[2][2];
        }

        IMatrix44ToD3DMatrix(tXfm, c2env);
    }
    // cam2Screen will also have the kMiscPerspProjection flag set, so this needs
    // to go before the regular kMiscProjection check.
    else if (fLayerState[stage].fMiscFlags & hsGMatState::kMiscCam2Screen) {
        // Still needs a bit of cleaning...
        static hsVector3 camScale(0.5f, -0.5f, 1.f);
        static hsVector3 camTrans(0.5f, 0.5f, 0.f);
        hsMatrix44       p2s;
        p2s.MakeScaleMat(&camScale);
        p2s.fMap[0][3] += camTrans.fX;
        p2s.fMap[1][3] += camTrans.fY;

        // The scale and trans move us from NDC to Screen space. We need to swap
        // the Z and W coordinates so that the texture projection will divide by W
        // and give us projected 2D coordinates.
        float temp = p2s.fMap[2][2];
        p2s.fMap[2][2] = p2s.fMap[3][2];
        p2s.fMap[3][2] = temp;

        temp = p2s.fMap[2][3];
        p2s.fMap[2][3] = p2s.fMap[3][3];
        p2s.fMap[3][3] = temp;

        IMatrix44ToD3DMatrix(tXfm, p2s * IGetCameraToNDC());
    } else if (fLayerState[stage].fMiscFlags & hsGMatState::kMiscProjection) {
        // For projection, the worldToLight transform is in the layer transform,
        // so we append the cameraToWorld, getting cameraToLight
        hsMatrix44 c2w = fView.GetCameraToWorld();
        if (!(layer->GetUVWSrc() & plLayerInterface::kUVWPosition)) {
            c2w.fMap[0][3] = 0;
            c2w.fMap[1][3] = 0;
            c2w.fMap[2][3] = 0;
        }

        // We've already stuffed the worldToLight transform into the layer.
        hsMatrix44 c2l = layer->GetTransform() * c2w;

        IMatrix44ToD3DMatrix(tXfm, c2l);
    } else if (fLayerState[stage].fMiscFlags & hsGMatState::kMiscBumpChans) {
        // Bump matrices are already set, just get the right one and stuff it in.
        hsMatrix44 m = IGetBumpMatrix(fLayerState[stage].fMiscFlags);

        IMatrix44ToD3DMatrix(tXfm, m);
    } else {
        // Just put take the layer transform and stuff it in.
        IMatrix44ToD3DMatrix(tXfm, layer->GetTransform());
    }

    fD3DDevice->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + stage), &tXfm);
    fLayerTransform[stage] = true;

    // If there's an lod bias associated with the layer, set it here.
    // There usually isn't.
    float newBias = fLayerState[stage].fZFlags & hsGMatState::kZLODBias ? layer->GetLODBias() : fTweaks.fDefaultLODBias;
    if (newBias != fLayerLODBias[stage]) {
        fLayerLODBias[stage] = newBias;
        fD3DDevice->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, F2DW(fLayerLODBias[stage]));
    }
}

void plWGPUPipeline::IUseTextureRef(int stage, hsGDeviceRef* dRef, plLayerInterface* layer)
{
    // plDXPipeline::IUseTextureRef
    plWGPUTextureRef* ref = (plWGPUTextureRef*)dRef;
    uint32_t          xformFlags;

    uint32_t uvwSrc = layer->GetUVWSrc();

    // DX pixel shaders require the TEXCOORDINDEX to be equal to the stage,
    // even though its ignored.
    if (layer->GetPixelShader() && (stage != uvwSrc))
        uvwSrc = stage;

    // Update our UVW source
    if (fLayerUVWSrcs[stage] != uvwSrc) {
        fD3DDevice->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, uvwSrc);
        fLayerUVWSrcs[stage] = uvwSrc;
    }

    if (!layer->GetVertexShader() && !layer->GetPixelShader()) {
        /// Set the transform flags
        /// Note: the perspective projection flag must be taken from the layer, since it's layer-specific.
        /// Storing it on the texture ref is bad, because the texture ref can be shared among layers whose
        /// projection flags might not match. This should probably be cleaned up, but for now this fixes the
        /// problem.
        if (ref->fCubic)
            xformFlags = D3DTTFF_COUNT3;
        else if (layer->GetMiscFlags() & hsGMatState::kMiscPerspProjection)
            xformFlags = D3DTTFF_COUNT3 | D3DTTFF_PROJECTED;
        else
            xformFlags = D3DTTFF_COUNT2;

        if (xformFlags != fLayerXformFlags[stage]) {
            fLayerXformFlags[stage] = xformFlags;
            fD3DDevice->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, xformFlags);
        }
    }

    // Update our current ref
    if (!ref->fTexture) {
        if (ref->fOwner)
            IReloadTexture(ref->fOwner, ref);
    } else if (dRef == fLayerRef[stage]) {
        return;
    }
    hsRefCnt_SafeAssign(fLayerRef[stage], dRef);

    /// Actually make it active!
    fD3DDevice->SetTexture(stage, ref);
}

void plWGPUPipeline::IStageStop(uint32_t stage)
{
    // plDXPipeline::IStageStop
    int disableStage = stage;

    // Note: even if we don't have a texture, we handle it similar to if we had one,
    // so the only special case we need here is if we only had one stage to set up -mcn
    if ((stage <= 1)) {
        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_DISABLE);
        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        fLayerState[stage].fBlendFlags = uint32_t(-1);
        disableStage = stage;
    } else if (stage == 2) {
        // The fMaxLayersAtOnce == 2 check is for the DX9.0c 2 texture limitation.
        // See ILayersAtOnce()
        if ((fLayerState[0].fBlendFlags & hsGMatState::kBlendNoTexColor) || (fLayerState[1].fBlendFlags & hsGMatState::kBlendNoTexColor) || fMaxLayersAtOnce == 2) {
            fD3DDevice->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
            disableStage = 2;
        } else {
            fD3DDevice->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_MODULATE);
            fD3DDevice->SetTextureStageState(2, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            fD3DDevice->SetTextureStageState(2, D3DTSS_COLORARG2, D3DTA_CURRENT);

            fD3DDevice->SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
            disableStage = 3;
        }

        fD3DDevice->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        fLayerState[2].fBlendFlags = uint32_t(-1);
        fLayerState[3].fBlendFlags = uint32_t(-1);
    } else {
        // This is directly contrary to the DX documentation, but in line with
        // the code generated by MFCTex (which works). The docs say:
        //  "Alpha operations cannot be disabled when color operations are enabled.
        //      Setting the alpha operation to D3DTOP_DISABLE when color blending
        //      is enabled causes undefined behavior."
        // But not disabling the earliest possible alpha stage causes the driver
        // to choke.

        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLOROP, D3DTOP_MODULATE);
        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        fD3DDevice->SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);

        fD3DDevice->SetTextureStageState(stage, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        fLayerState[stage].fBlendFlags = uint32_t(-1);

        fD3DDevice->SetTextureStageState(stage + 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        fLayerState[stage + 1].fBlendFlags = uint32_t(-1);

        disableStage = stage + 1;
    }

    fLastEndingStage = stage;

    if (fSettings.fIsIntel) {
        int maxUVW = 0;
        int k;
        for (k = 0; k < fCurrNumLayers; k++) {
            if ((fCurrMaterial->GetLayer(k + fCurrLayerIdx)->GetUVWSrc() & 0xf) > maxUVW)
                maxUVW = fCurrMaterial->GetLayer(k + fCurrLayerIdx)->GetUVWSrc() & 0xf;
        }
        for (k = disableStage; k <= maxUVW; k++) {
            fD3DDevice->SetTextureStageState(k, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
            fD3DDevice->SetTextureStageState(k, D3DTSS_COLORARG2, D3DTA_CURRENT);
        }
        fD3DDevice->SetTextureStageState(k, D3DTSS_COLOROP, D3DTOP_DISABLE);
    }
}

void plWGPUPipeline::IInvalidateState()
{
    // plDXPipeline::IInvalidateState. A render-target switch of the prewarm's walk (plWGPUDevice::SetRenderTarget)
    // keeps the texture refs, which the walk neither binds nor reads; the render state is reset as the draw's is.
    fLastEndingStage = 0;
    fTexturing = false;
    int i;
    if (!fPrewarmWalk) {
        for (i = 0; i < 8; i++) {
            hsRefCnt_SafeUnRef(fLayerRef[i]);
            fLayerRef[i] = nullptr;
            fD3DDevice->SetTexture(i, nullptr);
        }
    }

    fLayerState[0].fZFlags = 0;
    fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);

    // Workaround for an ATI driver light cache (plDXPipeline::IInvalidateState): disable the
    // enabled lights at a render target switch.
    hsBitIterator iterOff(fLights.fEnabledFlags);
    for (iterOff.Begin(); !iterOff.End(); iterOff.Advance())
        fD3DDevice->LightEnable(iterOff.Current(), false);
    fLights.fEnabledFlags.Clear();
    fLights.fHoldFlags.Clear();

    // This is very annoying. Set fTexturing to false doesn't work if the next layer
    // we draw doesn't have a texture. So we have to set this flag instead to force
    // a state update. I have an idea about how to do all of this a lot better, but
    // it's not time to do it...not yet at least.... --mcn
    fSettings.fVeryAnnoyingTextureInvalidFlag = true;
}

uint32_t plWGPUPipeline::ILayersAtOnce(hsGMaterial* mat, uint32_t which)
{
    // plDXPipeline::ILayersAtOnce
    fCurrNumLayers = 1;

    if (fView.fRenderState & plPipeline::kRenderBaseLayerOnly)
        return fCurrNumLayers;

    plLayerInterface* lay = mat->GetLayer(which);

    if (IsDebugFlagSet(plPipeDbg::kFlagNoMultitexture))
        return fCurrNumLayers;

    if ((IsDebugFlagSet(plPipeDbg::kFlagBumpUV) || IsDebugFlagSet(plPipeDbg::kFlagBumpW)) && (lay->GetMiscFlags() & hsGMatState::kMiscBumpChans))
        return fCurrNumLayers = 2;

    if ((lay->GetBlendFlags() & hsGMatState::kBlendNoColor) || (lay->GetMiscFlags() & hsGMatState::kMiscTroubledLoner))
        return fCurrNumLayers;

    // New DX9.0c limitation for cards that can only do 2 textures per pass.
    // We used to be able to set stage 0 and 1 to textures, and set stage 2 to the
    // diffuse color. With DX9.0c we just get two texture stages. Period.
    // Either we give up a texture or the diffuse color.
    if (fMaxLayersAtOnce == 2) {
        if ((mat->GetNumLayers() > which + 1) && !(mat->GetLayer(which + 1)->GetBlendFlags() & hsGMatState::kBlendNoTexColor)) {
            // If we're just using the texture for alpha, we can multiply
            // the diffuse color in stage 1. Otherwise, save it for the next pass.
            return fCurrNumLayers;
        }
    }

    int i;
    int maxLayersAtOnce = fMaxLayersAtOnce;

    // Now Reserve space for piggy backs, and see if there are
    // are any more layers we can pick up.
    //
    maxLayersAtOnce = fMaxLayersAtOnce - fActivePiggyBacks;
    if (which + maxLayersAtOnce > mat->GetNumLayers())
        maxLayersAtOnce = mat->GetNumLayers() - which;

    for (i = fCurrNumLayers; i < maxLayersAtOnce; i++) {
        plLayerInterface* lay = mat->GetLayer(which + i);
        if ((lay->GetUVWSrc() & 0xf) > fSettings.fMaxUVWSrc)
            break;
        if ((lay->GetMiscFlags() & hsGMatState::kMiscBindNext) && (i + 1 >= maxLayersAtOnce))
            break;
        if (lay->GetMiscFlags() & hsGMatState::kMiscRestartPassHere)
            break;
        if (!(mat->GetLayer(which + i - 1)->GetMiscFlags() & hsGMatState::kMiscBindNext) && !ICanEatLayer(lay))
            break;
        fCurrNumLayers++;
    }
    return fCurrNumLayers;
}

bool plWGPUPipeline::ICanEatLayer(plLayerInterface* lay)
{
    // plDXPipeline::ICanEatLayer
    if (!lay->GetTexture())
        return false;

    if ((lay->GetBlendFlags() & hsGMatState::kBlendNoColor) || (lay->GetBlendFlags() & hsGMatState::kBlendAddColorTimesAlpha) // has to be base layer
        || (lay->GetMiscFlags() & hsGMatState::kMiscTroubledLoner))
        return false;

    if ((lay->GetBlendFlags() & hsGMatState::kBlendAlpha) && (lay->GetAmbientColor().a < 1.f))
        return false;

    if (!(lay->GetZFlags() & hsGMatState::kZNoZWrite))
        return false;

    return true;
}

bool plWGPUPipeline::IIsViewLeftHanded()
{
    // plDXPipeline::IIsViewLeftHanded
    return fView.GetViewTransform().GetOrthogonal() ^ (fView.fLocalToWorldLeftHanded ^ fView.fWorldToCamLeftHanded) ? true : false;
}

void plWGPUPipeline::ISetCullMode(bool flip)
{
    // plDXPipeline::ISetCullMode (D3DCULL to WebGPU: plWGPUDevice::DrawFixedFunction)
    D3DCULL newCull = D3DCULL_NONE;

    if (!(fLayerState[0].fMiscFlags & hsGMatState::kMiscTwoSided))
        newCull = !IIsViewLeftHanded() ^ !flip ? D3DCULL_CW : D3DCULL_CCW;

    if (newCull != fDevice.fCurrCullMode) {
        fDevice.fCurrCullMode = newCull;
        fD3DDevice->SetRenderState(D3DRS_CULLMODE, fDevice.fCurrCullMode);
    }
}

void plWGPUPipeline::ISetAnisotropy(bool on)
{
    // plDXPipeline::ISetAnisotropy
    if ((fSettings.fMaxAnisotropicSamples <= 0) || IsDebugFlagSet(plPipeDbg::kFlagNoAnisotropy))
        on = false;

    if (on == fSettings.fCurrAnisotropy)
        return;

    if (on) {
        int i;
        for (i = 0; i < 8; i++) {
            // GeForce cards have decided that they no longer handle anisotropic as a mag filter.
            // We could detect caps... but I don't think we'd notice if we just made the mag
            // filter always be linear.
            fD3DDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, (DWORD)fSettings.fMaxAnisotropicSamples);
        }
        fSettings.fCurrAnisotropy = true;
    } else {
        int i;
        for (i = 0; i < 8; i++) {
            fD3DDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        }
        fSettings.fCurrAnisotropy = false;
    }
}

void plWGPUPipeline::IInitDeviceState()
{
    // plDXPipeline::IInitDeviceState (the D3D states WebGPU has no counterpart for, CLIPPING,
    // MULTISAMPLEANTIALIAS, ANTIALIASEDLINEENABLE, DITHERENABLE and STENCILENABLE, are not set)
    fLayerState[0].Reset();
    fDevice.fCurrCullMode = D3DCULL_CW;

    /// Set D3D states
    fCurrFog.Reset();
    ISetFogParameters(nullptr, nullptr);

    fD3DDevice->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    fD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    fD3DDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    fD3DDevice->SetRenderState(D3DRS_CULLMODE, fDevice.fCurrCullMode);
    ISetCullMode();

    fD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    fD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
    fD3DDevice->SetRenderState(D3DRS_ALPHAREF, 0x00000001);

    fD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    fD3DDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    fCurrD3DLiteState = false;
    fD3DDevice->SetRenderState(D3DRS_TEXTUREFACTOR, 0x0);
    fD3DDevice->SetTransform(D3DTS_TEXTURE0, &d3dIdentityMatrix);
    fD3DDevice->SetTransform(D3DTS_WORLD, &d3dIdentityMatrix);

    /// NEW: to compensate for scaling transformations that might screw up our nicely
    /// normalized normals. Note: nVidia says this is as fast or faster than with
    /// this disabled, but who knows what it'll do on other cards...
    fD3DDevice->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    fD3DDevice->SetRenderState(D3DRS_LOCALVIEWER, TRUE);

    // Initialization for all 8 stages (even though we only use a few of them).
    int i;
    for (i = 0; i < 8; i++) {
        fLayerLODBias[i] = fTweaks.fDefaultLODBias;
        fLayerTransform[i] = false;
        fLayerRef[i] = nullptr;
        fLayerUVWSrcs[i] = i;
        fLayerState[i].Reset();

        fD3DDevice->SetTexture(i, nullptr);
        fD3DDevice->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, i);
        fD3DDevice->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        fD3DDevice->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        fD3DDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, F2DW(fLayerLODBias[i]));

        if (fSettings.fMaxAnisotropicSamples > 0 && !IsDebugFlagSet(plPipeDbg::kFlagNoAnisotropy)) {
            fD3DDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, (DWORD)fSettings.fMaxAnisotropicSamples);
            fSettings.fCurrAnisotropy = true;
        } else {
            fD3DDevice->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            fD3DDevice->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            fSettings.fCurrAnisotropy = false;
        }
        fD3DDevice->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

        fD3DDevice->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + i), &d3dIdentityMatrix);
        fLayerXformFlags[i] = D3DTTFF_COUNT2;
        fD3DDevice->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
    }

    // Initialize our bump mapping matrices.
    for (i = 0; i < 4; i++) {
        int j;
        for (j = 0; j < 4; j++) {
            fBumpDuMatrix.fMap[i][j] = 0;
            fBumpDvMatrix.fMap[i][j] = 0;
            fBumpDwMatrix.fMap[i][j] = 0;
        }
    }
    fBumpDuMatrix.NotIdentity();
    fBumpDvMatrix.NotIdentity();
    fBumpDwMatrix.NotIdentity();

    // As plDXPipeline::IInitDeviceState: specular highlights off by override
    PushMaterialOverride(hsGMatState::kShade, hsGMatState::kShadeSpecularHighlight, false);

    fLights.Reset(this);

    // Tell the light infos to unlink themselves
    while (fActiveLights)
        UnRegisterLight(fActiveLights);
}
