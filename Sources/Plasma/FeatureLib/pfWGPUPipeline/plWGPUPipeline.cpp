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

#include "plWGPUPipeline.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string_theory/format>
#include <string_theory/string_stream>
#include <tuple>

#include "HeadSpin.h"
#include "ShaderSrc/ShaderTypes.h"
#include "hsResMgr.h"
#include "hsTimer.h"
#include "pfCamera/plVirtualCamNeu.h"
#include "plDrawable/plAuxSpan.h"
#include "plDrawable/plDrawableSpans.h"
#include "plDrawable/plGBufferGroup.h"
#include "plDrawable/plSpanTypes.h"
#include "plDrawable/plWaveSetShaderConsts.h"
#include "plGImage/plCubicEnvironmap.h"
#include "plGImage/plMipmap.h"
#include "plGLight/plLightInfo.h"
#include "plGLight/plShadowCaster.h"
#include "plGLight/plShadowSlave.h"
#include "plMessage/plDeviceRecreateMsg.h"
#include "plPipeResReq.h"
#include "plPipeline/plCaptureRender.h"
#include "plPipeline/plCubicRenderTarget.h"
#include "plPipeline/plDebugText.h"
#include "plPipeline/plDynamicEnvMap.h"
#include "plPipeline/plFogEnvironment.h"
#include "plPipeline/plPlates.h"
#include "plPipeline/plRenderTarget.h"
#include "plPipeline/plSoftwareMeshSkinner.h"
#include "plProfile.h"
#include "plQuality.h"
#include "plResMgr/plRegistryHelpers.h"
#include "plResMgr/plRegistryNode.h"
#include "plResMgr/plResManager.h"
#include "plScene/plPageTreeMgr.h"
#include "plScene/plRenderRequest.h"
#include "plScene/plSceneNode.h"
#include "plScene/plVisMgr.h"
#include "plStatusLog/plStatusLog.h"
#include "plSurface/hsGMaterial.h"
#include "plSurface/plLayerAnimation.h"
#include "plSurface/plLayerInterface.h"
#include "plSurface/plLayerShadowBase.h" // IRenderProjectionEach
#include "plSurface/plShader.h"
#include "plWGPUFragmentShader.h"
#include "plWGPULightRef.h"
#include "plWGPULog.h"
#include "plWGPUPipeline_Private.h"
#include "plWGPUPlateManager.h"
#include "plWGPUTextFont.h"
#include "plWGPUVertexShader.h"
#include "plgDispatch.h"
#include "pnEncryption/plChecksum.h"
#include "pnKeyedObject/plKeyImp.h"
#include "pnMessage/plPipeResMakeMsg.h"
#include "pnNetCommon/plNetApp.h" // for dbg logging
#ifdef PLASMA_TEST_HOOKS
#include "plWGPUPipelineObserver.h"
#endif

using namespace plWGPUD3D9;

// plDXPipeline.cpp's profile counters (plMetalPipeline.cpp declares the same)
plProfile_CreateCounter("Feed Triangles", "Draw", DrawFeedTriangles);
plProfile_CreateCounter("Draw Prim Static", "Draw", DrawPrimStatic);
plProfile_CreateCounter("Layer Change", "Draw", LayChange);

plWGPUEnumerate plWGPUPipeline::enumerator;

// As plDXPipeline.cpp
const int kD3DMaxTotalLights = 8;
const int kMaxProjectors = 100;

plWGPUPipeline::plWGPUPipeline(hsDisplayHndl display, hsWindowHndl window, const hsG3DDeviceModeRecord* devMode)
    : pl3DPipeline(devMode),
      fRenderTargetRefList(),
      fD3DDevice(),
      fCurrD3DLiteState(),
      fLayerTransform(),
      fLayerLODBias(),
      fLayerUVWSrcs(),
      fLayerXformFlags(),
      fLastEndingStage(),
      fTexturing(),
      fIsFullscreen(),
      fUnsupportedProgramPass(),
      fUnsupportedProgramRequests(),
      fTextFontRefList()
{
    // plMetalPipeline::plMetalPipeline
    fTextureRefList = nullptr;
    fVShaderRefList = nullptr; // plDXPipeline::IClearMembers
    fPShaderRefList = nullptr;
    fVtxBuffRefList = nullptr;
    fIdxBuffRefList = nullptr;
    fCurrLayerIdx = 0;
    fDevice.fPipeline = this;
    memset(&fCounters, 0, sizeof(fCounters));
    memset(&fLastCounters, 0, sizeof(fLastCounters));

    // plDXPipeline::IClearMembers, the members the ported span path reads.
    // The material code sets its D3D9 state on the emulated device.
    fD3DDevice = &fDevice.fD3D;
    fTexturing = false;
    fLastEndingStage = uint32_t(-1);
    fSettings.Reset();
    fLights.Reset(this);
    fCurrFog.Reset();
    for (int i = 0; i < 8; i++) {
        fLayerState[i].Reset();
        fOldLayerState[i].Reset();
    }
    fMaxNumLights = kD3DMaxTotalLights;
    fMaxNumProjectors = kMaxProjectors;
    fForceMatHandle = true;

    // plDXPipeline::IClearMembers: the shadow pools and blur resources.
    IResetRenderTargetPools();
    fULutTextureRef = nullptr;
    for (int i = 0; i < kMaxRenderTargetNext; i++)
        fBlurVBuffers[i] = nullptr;
    fBlurQuad = nullptr;
#ifdef PLASMA_TEST_HOOKS
    fRowsArmed = false;
    fRowsArmNext = false;
#endif

    // plDXPipeline's reset state and present parameters (its constructor and ICreateDevice:
    // Windowed TRUE, the back buffer at the view transform's screen size).
    memset(fGammaRamp, 0, sizeof(fGammaRamp));
    fGammaRampSet = false;
    fFrameWaveBiasSeen = false;
    fFrameWaveBiasMixed = false;
    fFrameWaveBiasBits = 0;
    fPrewarmWalk = false;
    fPrewarmSeat = nullptr;
    fPrewarmGeneration = 0;
    fPrewarmColor = WGPUTextureFormat_Undefined;
    fPrewarmDepth = WGPUTextureFormat_Undefined;
    fPrewarmSamples = 0;
    fPrewarmFogType = plFogEnvironment::kNoFog;
    fPrewarmFogDegenerate = false;
    fPrewarmConditionsSet = false;
#ifdef PLASMA_TEST_HOOKS
    fPrewarmCensusDue = false;
#endif
    fDeviceLost = false;
    fDevWasLost = false;
    fForceDeviceReset = false;
    fPresentationLost = false;
    fPresentWindowed = true;
    fPresentWidth = GetViewTransform().GetScreenWidth();
    fPresentHeight = GetViewTransform().GetScreenHeight();
    fNumAASamples = fInitialPipeParams.AntiAliasingAmount;
    memset(&fDisplayStats, 0, sizeof(fDisplayStats));

    // The device was acquired before the client loaded (plWGPUEnumerate::StartAcquire);
    // the record says which device type was selected.
    fDevice.fWGPUDevice = plWGPUEnumerate::Device();
    fDevice.fSurface = plWGPUEnumerate::Surface();
    fDevice.fDeviceType = devMode->GetDevice()->GetG3DDeviceType();
    // D3D9 drops non-multiple-of-4 mip levels of 8/16-bit textures unless the device does
    // small textures (plDXPipeline::IProcessMipmapLevels); the device record's caps say whether it does.
    fDevice.fDoesSmallTextures = devMode->GetDevice()->GetCap(hsG3DDeviceSelector::kCapsDoesSmallTextures);
    fDevice.InitDevice();

    fMaxLayersAtOnce = devMode->GetDevice()->GetLayersAtOnce();
    fIsFullscreen = !fInitialPipeParams.Windowed;
    fDesktopParams = plDisplayHelper::GetInstance()->DesktopDisplayMode();

    // The canvas backing store is D3D9's back buffer: BackBufferWidth/Height = the view
    // transform's screen size (plDXPipeline::ICreateDevice; the pl3DPipeline constructor sets it
    // from the mode, or from the ini size when windowed).
    fDevice.ConfigureSurface(fPresentWidth, fPresentHeight);

    // Alloc half our simultaneous textures to piggybacks.
    // Won't hurt us unless we try to many things at once.
    fMaxPiggyBacks = fMaxLayersAtOnce >> 1;
    // The device record's layers at once (plWGPUEnumerate) decide plDXPipeline::ILayersAtOnce's two-texture
    // branch, and so which layer can be stage 0's MIPMAPLODBIAS writer.
    plWGPULog("device: fMaxLayersAtOnce {}, fMaxPiggyBacks {}", uint32_t(fMaxLayersAtOnce), uint32_t(fMaxPiggyBacks));

    // WebGPU is always PS3 capable (as in plMetalPipeline::plMetalPipeline)
    plQuality::SetCapability(plQuality::kPS_3);

    // plDXPipeline::ISetCaps: every D3D9-class device reports D3DPRASTERCAPS_FOGTABLE, so table
    // (pixel) fog is available. kCapsZBias (D3DPRASTERCAPS_DEPTHBIAS) is cleared by IRestrictCaps
    // unless the device record has kCapsZBias, which the WebGPU record does not set. fMaxNumLights
    // is min(MaxActiveLights, kD3DMaxTotalLights) = 8.
    // Unconditional non-power-of-two textures: a device without D3DPTEXTURECAPS_POW2 has
    // kCapsNpotTextures, and WebGPU textures have no size restriction.
    fSettings.fD3DCaps = kCapsLinearFog | kCapsExpFog | kCapsExp2Fog | kCapsPixelFog | kCapsNpotTextures;

    // plDXPipeline::IRestrictCaps, the tweaks and caps the span path reads.
    const hsG3DDeviceRecord* devRec = devMode->GetDevice();
    if (devRec->GetCap(hsG3DDeviceSelector::kCapsCantProj))
        fSettings.fCantProj = true;
    SetZBiasScale((float)devRec->GetZBiasRating());
    // D3DSAMP_MIPMAPLODBIAS default (IRestrictCaps)
    fTweaks.fDefaultLODBias = (float)-(0.25 + (float)devRec->GetLODBiasRating());
    fTweaks.fDefaultPerspLayerScale = kPerspLayerScale;
    // Less than 4 layers at once means we have to fallback on uv bumpmapping
    if (fMaxLayersAtOnce < 4)
        SetDebugFlag(plPipeDbg::kFlagBumpUV, true);
    if (devRec->GetCap(hsG3DDeviceSelector::kCapsMaxUVWSrc2))
        fSettings.fMaxUVWSrc = 2;
    // The anisotropy the client asked for, up to the 16 WebGPU samplers take (the plDXPipeline
    // constructor and IRestrictCaps).
    fSettings.fMaxAnisotropicSamples = (uint8_t)std::min(fInitialPipeParams.AnisotropicLevel, 16);

    fDevice.SetMaxAnsiotropy(fInitialPipeParams.AnisotropicLevel);
    // The selected mode's FSAA types are what both AA searches read (DX fCurrentMode->fFSAATypes).
    const hsG3DDeviceMode* devModeRec = devMode->GetMode();
    fDevice.fFSAATypes.clear();
    for (size_t i = 0; i < devModeRec->GetNumFSAATypes(); i++)
        fDevice.fFSAATypes.push_back(devModeRec->GetFSAAType(i));
    fDevice.SetMSAASampleCount(fInitialPipeParams.AntiAliasingAmount);

    ICreateDeviceObjects();
    ICreateDynDeviceObjects();

    plWGPULog("plWGPUPipeline active: device type {} ({}), mode {}x{}, MSAA {}, anisotropy {}, BC textures {}, LOD bias {.2f}",
              fDevice.fDeviceType, devMode->GetDevice()->GetG3DDeviceTypeName(),
              devMode->GetMode()->GetWidth(), devMode->GetMode()->GetHeight(), fDevice.fSampleCount,
              fInitialPipeParams.AnisotropicLevel, fDevice.fSupportsDXTTextures ? "yes" : "no (software decompression)",
              fTweaks.fDefaultLODBias);
}

plWGPUPipeline::~plWGPUPipeline()
{
    // A device loss from here on is not one to recover from.
    plWGPUEnumerate::SetShuttingDown();
    // As plDXPipeline::~plDXPipeline: release every device object before the pipeline goes.
    // IReleaseDeviceObjects unlinks each device ref from the pipeline's lists (textures, vertex
    // and index buffers, render targets, text fonts, shaders, lights) and deletes the plate
    // manager (its geometry with it). The plBitmaps, render targets and fonts that own these
    // refs outlive the pipeline (plClient::Shutdown deletes fPipeline before the pages and
    // their objects go); without this, each ref's destructor would unlink itself through a list
    // head inside the freed pipeline. plMetalPipeline::~plMetalPipeline releases only the plate
    // geometry.
    IReleaseDeviceObjects();
    // Then the readbacks the pipeline and its device still own (a capture or harness readback whose map is in
    // flight): their owners let go here, and each map's callback keeps its readback, buffer included, until the map
    // has ended (plWGPUDevice::IRequestReadback). The WebGPU device itself, the adapter, the surface and the
    // instance are plWGPUEnumerate's and are released at exit (plWGPUEnumerate::Shutdown).
    fPendingCaptures.clear();
    fDevice.fReadbacks.clear();
}

void plWGPUPipeline::ICreateDeviceObjects()
{
    fPlateMgr = new plWGPUPlateManager(this);

    // The stored ramp survives the recreate (plMetalDevice keeps fGammaLUTTexture across a resize,
    // and plDXPipeline does not set the ramp again after IResetDevice): the new device gets its
    // table, and the LUT pipeline is made for the canvas format.
    if (fGammaRampSet)
        fDevice.SetGammaTable(fGammaRamp);

    // We've got everything created now, initialize to a known state (plDXPipeline::ICreateDeviceObjects).
    IInitDeviceState();
}

plTextFont* plWGPUPipeline::MakeTextFont(ST::string face, uint16_t size)
{
    // plMetalPipeline::MakeTextFont
    plTextFont* font = new plWGPUTextFont(this, &fDevice);
    font->Create(face, size);
    font->Link(&fTextFontRefList);
    return font;
}

bool plWGPUPipeline::OpenAccess(plAccessSpan& dst, plDrawableSpans* d, const plVertexSpan* span, bool readOnly)
{
    // Never called: plAccessGeometry asks the pipeline only when its useDev is set, and useDev is
    // plConst(false) (plAccessGeometry::IAccessSpanFromVertexSpan). plMetalPipeline::OpenAccess
    // returns false too.
    return false;
}

bool plWGPUPipeline::CloseAccess(plAccessSpan& acc)
{
    // Never called (see OpenAccess).
    return false;
}

void plWGPUPipeline::PushRenderRequest(plRenderRequest* req)
{
    // plDXPipeline::PushRenderRequest (a request with a render target renders into it)

    // Save these, since we want to copy them to our current view
    hsMatrix44 l2w = fView.GetLocalToWorld();
    hsMatrix44 w2l = fView.GetWorldToLocal();

    plFogEnvironment defFog = fView.GetDefaultFog();

    fViewStack.push(fView);

    SetViewTransform(req->GetViewTransform());

    PushRenderTarget(req->GetRenderTarget());
    fView.fRenderState = req->GetRenderState();

    fView.fRenderRequest = req;
    fView.fRenderRequest->Ref();

    SetDrawableTypeMask(req->GetDrawableMask());
    SetSubDrawableTypeMask(req->GetSubDrawableMask());

    float depth = req->GetClearDepth();
    fView.SetClear(&req->GetClearColor(), &depth);

    if (req->GetFogStart() < 0) {
        fView.SetDefaultFog(defFog);
    } else {
        defFog.Set(req->GetYon() * (1.f - req->GetFogStart()), req->GetYon(), 1.f, &req->GetClearColor());
        fView.SetDefaultFog(defFog);
        fCurrFog.fEnvPtr = nullptr;
    }

    if (req->GetOverrideMat())
        PushOverrideMaterial(req->GetOverrideMat());

    // Set from our saved ones...
    fView.SetWorldToLocal(w2l);
    fView.SetLocalToWorld(l2w);

    RefreshMatrices();

    if (req->GetIgnoreOccluders())
        fView.SetMaxCullNodes(0);

    fView.fCullTreeDirty = true;
}

void plWGPUPipeline::PopRenderRequest(plRenderRequest* req)
{
    // plDXPipeline::PopRenderRequest
    if (req->GetOverrideMat())
        PopOverrideMaterial(nullptr);

    hsRefCnt_SafeUnRef(fView.fRenderRequest);
    fView = fViewStack.top();
    fViewStack.pop();

    // Force the next thing drawn to update the fog settings.
    fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
    fCurrFog.fEnvPtr = nullptr;

    PopRenderTarget();
    fView.fXformResetFlags = fView.kResetProjection | fView.kResetCamera;
}

void plWGPUPipeline::ClearRenderTarget(plDrawable* d)
{
    // plDXPipeline::ClearRenderTarget(plDrawable*) (plMetalPipeline's draws without the depth
    // clear and the drawable masks).
    plDrawableSpans* src = plDrawableSpans::ConvertNoRef(d);

    if (!src) {
        ClearRenderTarget();
        return;
    }

    // First clear the depth buffer as normal.
    if (fView.fRenderState & kRenderClearDepth) {
        const float clearColor[4] = {0.f, 0.f, 0.f, 0.f};
        fDevice.Clear(false, clearColor, true, fView.GetClearDepth());
    }

    uint32_t s = fView.fRenderState;
    uint32_t dtm = fView.GetDrawableTypeMask();
    uint32_t sdtm = fView.GetSubDrawableTypeMask();

    fView.SetDrawableTypeMask(plDrawable::kNormal);
    fView.SetSubDrawableTypeMask(uint32_t(-1));

    Draw(d);

    fView.SetSubDrawableTypeMask(sdtm);
    fView.SetDrawableTypeMask(dtm);
    fView.fRenderState = s;
}

// CONVERT_FLOAT_TO_BYTE_COLOR (plDXPipeline.h): 0 at or below 0, 255 at or above 1,
// else the float times 256, truncated.
static float IQuantizeD3DColor(float f)
{
    uint32_t dest;
    if (f <= 0.f)
        dest = 0;
    else if (f >= 1.f)
        dest = 255;
    else
        dest = uint32_t(f * 256.f);
    return float(dest) / 255.f;
}

void plWGPUPipeline::ClearRenderTarget(const hsColorRGBA* col, const float* depth)
{
    // plDXPipeline::ClearRenderTarget(colour, depth): the colour is the D3DCOLOR of
    // inlGetD3DColor, the depth the requested one (plMetalPipeline passes 1.0), over the current
    // viewport (plDXPipeline::IGetClearViewPort, which the device applies).
    if (fView.fRenderState & (kRenderClearColor | kRenderClearDepth)) {
        hsColorRGBA c = col ? *col : GetClearColor();
        float       clearDepth = depth ? *depth : fView.GetClearDepth();
        const float clearColor[4] = {IQuantizeD3DColor(c.r), IQuantizeD3DColor(c.g), IQuantizeD3DColor(c.b), IQuantizeD3DColor(c.a)};

        // The clear values, logged once per distinct value (render requests alternate
        // depth-only and colour clears every frame)
        static std::set<std::array<float, 6>> sLogged;
        const std::array<float, 6>            now = {clearColor[0], clearColor[1], clearColor[2], clearColor[3], clearDepth,
                                                     float(fView.fRenderState & (kRenderClearColor | kRenderClearDepth))};
        if (!fPrewarmWalk && sLogged.insert(now).second) {
            plWGPULog("clear: colour {} {} {} {} (D3DCOLOR {02x}{02x}{02x}{02x} ARGB) depth {}, target{}{}",
                      clearColor[0], clearColor[1], clearColor[2], clearColor[3],
                      uint32_t(clearColor[3] * 255.f + .5f), uint32_t(clearColor[0] * 255.f + .5f),
                      uint32_t(clearColor[1] * 255.f + .5f), uint32_t(clearColor[2] * 255.f + .5f), clearDepth,
                      (fView.fRenderState & kRenderClearColor) ? " TARGET" : "", (fView.fRenderState & kRenderClearDepth) ? " ZBUFFER" : "");
        }

        fDevice.Clear(fView.fRenderState & kRenderClearColor, clearColor, fView.fRenderState & kRenderClearDepth, clearDepth);
    }
}

// The D3DFORMATs of a render target (IPrepRenderTargetInfo), in WebGPU terms. D3D9's
// A8R8G8B8 is bgra8unorm. 16-bit (A4R4G4B4) targets are not supported: WebGPU has no such
// format, and the target is made 32-bit. The depth formats: D24X8 is depth24plus, D24X4S4 and
// D24S8 are depth24plus-stencil8, D16 depth16unorm, D32 depth32float; D15S1 has no WebGPU
// format and fails as on a device without it (CheckDeviceFormat).
static WGPUTextureFormat IRenderTargetColorFormat(int8_t bitDepth)
{
    if (bitDepth == 16 || bitDepth == 32)
        return WGPUTextureFormat_BGRA8Unorm;
    return WGPUTextureFormat_Undefined;
}

bool plWGPUPipeline::IPrepRenderTargetInfo(plRenderTarget* owner, WGPUTextureFormat& surfFormat,
                                           WGPUTextureFormat& depthFormat)
{
    // plDXPipeline::IPrepRenderTargetInfo
    uint16_t flags = owner->GetFlags();
    uint16_t width = owner->GetWidth();
    uint16_t height = owner->GetHeight();
    int8_t   bitDepth = owner->GetPixelSize();
    int8_t   zDepth = owner->GetZDepth();
    int8_t   stencilDepth = owner->GetStencilDepth();

    if (flags != 0) {
        if (flags & plRenderTarget::kIsTexture) {
            /// Do an extra check for width and height here
            if (!(fSettings.fD3DCaps & kCapsNpotTextures)) {
                int i, j;
                for (i = width >> 1, j = 0; i != 0; i >>= 1, j++);
                if (width != (1 << j))
                    return false;

                for (i = height >> 1, j = 0; i != 0; i >>= 1, j++);
                if (height != (1 << j))
                    return false;
            }
        }

        surfFormat = IRenderTargetColorFormat(bitDepth);
        // A 16-bit target is not supported (WebGPU has no A4R4G4B4 format): it is made 32-bit,
        // as plMetalPipeline makes every target. plDXPipeline::IFindRenderTargetInfo falls back
        // from 32 to 16 bits only on a device without an 8-bit RGBA render format, and WebGPU
        // always has one.
        hsAssert(bitDepth != 16, "16-bit (A4R4G4B4) render targets are not supported; made 32-bit");

        /// Get the backbuffer format (if one is requested)
        if (zDepth) {
            if (zDepth == 16 && stencilDepth == 0)
                depthFormat = WGPUTextureFormat_Depth16Unorm;
            else if (zDepth == 24)
                depthFormat = stencilDepth == 0 ? WGPUTextureFormat_Depth24Plus : WGPUTextureFormat_Depth24PlusStencil8;
            else if (zDepth == 32 && stencilDepth == 0)
                depthFormat = WGPUTextureFormat_Depth32Float;

            if (surfFormat == WGPUTextureFormat_Undefined || depthFormat == WGPUTextureFormat_Undefined) {
                plWGPULog("render target {}: no format for {}-bit colour, z{} s{}",
                          owner->GetKey() ? owner->GetKeyName() : ST_LITERAL("(no key)"), bitDepth, zDepth, stencilDepth);
                return false;
            }
        } else {
            depthFormat = WGPUTextureFormat_Undefined;
        }

        // CheckDeviceFormat of the colour format
        if (surfFormat == WGPUTextureFormat_Undefined)
            return false;
    }

    return true;
}

bool plWGPUPipeline::IFindRenderTargetInfo(plRenderTarget* owner, WGPUTextureFormat& surfFormat)
{
    // plDXPipeline::IFindRenderTargetInfo: the colour format only, for a target that shares a depth
    // surface already made.
    if (owner->GetFlags() != 0) {
        surfFormat = IRenderTargetColorFormat(owner->GetPixelSize());
        if (surfFormat == WGPUTextureFormat_Undefined)
            return false;
    }
    return true;
}

WGPUTexture plWGPUPipeline::IMakeRenderTargetTexture(plRenderTarget* owner, WGPUTextureFormat format, uint32_t layers)
{
    // CreateTexture/CreateCubeTexture/CreateRenderTarget with one level and no multisampling.
    // CopySrc for ExtractMipMap's readback and the test harness's readbacks.
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    ST::string            label = owner->GetKey() ? owner->GetKeyName() : ST_LITERAL("plWGPU render target");
    desc.label = WGPUStringView{label.c_str(), label.size()};
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    desc.size = {owner->GetWidth(), layers == 6 ? owner->GetWidth() : owner->GetHeight(), layers};
    desc.format = format;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    WGPUTexture texture = wgpuDeviceCreateTexture(fDevice.fWGPUDevice, &desc);
    if (texture)
        fRenderTargetStats.bytes += uint64_t(desc.size.width) * desc.size.height * layers * 4;
    return texture;
}

void plWGPUPipeline::IRenderTargetSetTexture(plWGPURenderTargetRef* ref, plRenderTarget* owner, WGPUTexture texture,
                                             WGPUTextureFormat format, WGPUTexture depth, WGPUTextureFormat depthFormat)
{
    // plDXRenderTargetRef::Set and SetTexture (plDXDeviceRefs.cpp): the colour texture
    // (a cube's faces get its layers), the depth surface (shared by a cube's faces).
    ref->Release();
    ref->fOwner = owner;
    ref->fFormat = format;
    ref->fLevels = 1;
    ref->fTexture = texture;
    ref->fDepthBuffer = depth;
    ref->fDepthFormat = depth ? depthFormat : WGPUTextureFormat_Undefined;
    if (depth)
        ref->fDepthView = wgpuTextureCreateView(depth, nullptr);

    plCubicRenderTarget* cubic = plCubicRenderTarget::ConvertNoRef(owner);
    if (cubic) {
        WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        vd.dimension = WGPUTextureViewDimension_Cube;
        vd.arrayLayerCount = 6;
        ref->fTextureView = wgpuTextureCreateView(texture, &vd);
        ref->fCubic = true;

        // The D3DCUBEMAP_FACES of plDXRenderTargetRef::SetTexture (left -X, right +X, front +Z,
        // back -Z, top +Y, bottom -Y) as WebGPU's cube layers (+X, -X, +Y, -Y, +Z, -Z).
        static const uint32_t kFaceLayer[] = {1, 0, 4, 5, 2, 3};
        for (int i = 0; i < 6; i++) {
            plWGPURenderTargetRef* fRef = (plWGPURenderTargetRef*)cubic->GetFace(i)->GetDeviceRef();
            fRef->Release();
            fRef->fOwner = cubic->GetFace(i);
            fRef->fFormat = format;
            fRef->fLevels = 1;
            wgpuTextureAddRef(texture);
            fRef->fTexture = texture;
            WGPUTextureViewDescriptor fd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            fd.dimension = WGPUTextureViewDimension_2D;
            fd.baseArrayLayer = kFaceLayer[i];
            fd.arrayLayerCount = 1;
            fd.mipLevelCount = 1;
            fRef->fRenderView = wgpuTextureCreateView(texture, &fd);
            if (depth) {
                wgpuTextureAddRef(depth);
                fRef->fDepthBuffer = depth;
                fRef->fDepthFormat = depthFormat;
                fRef->fDepthView = wgpuTextureCreateView(depth, nullptr);
            }
            fRef->SetDirty(false);
        }
    } else {
        ref->fTextureView = wgpuTextureCreateView(texture, nullptr);
        ref->fRenderView = wgpuTextureCreateView(texture, nullptr);
        ref->fCubic = false;
    }
}

hsGDeviceRef* plWGPUPipeline::MakeRenderTargetRef(plRenderTarget* owner)
{
    // plDXPipeline::MakeRenderTargetRef
    plWGPURenderTargetRef* ref = nullptr;
    WGPUTexture            depthSurface = nullptr;
    WGPUTextureFormat      surfFormat = WGPUTextureFormat_Undefined, depthFormat = WGPUTextureFormat_Undefined;
    plCubicRenderTarget*   cubicRT;

    // If we have Shader Model 3 and support non-POT textures, let's make reflections the pipe
    // size. WebGPU is PS3 (plQuality::kPS_3) and NPOT
    // (kCapsNpotTextures), so the reflection maps follow the pipe as on any current D3D9 card.
    plDynamicCamMap* camMap = plDynamicCamMap::ConvertNoRef(owner);
    if (camMap) {
        bool havePS3 = plQuality::GetCapability() > plQuality::kPS_2;
        bool haveNpot = fSettings.fD3DCaps & kCapsNpotTextures;
        if (camMap->IsReflection() && havePS3 && haveNpot)
            camMap->ResizeViewport(IGetViewTransform());
    }

    /// Check--is this renderTarget really a child of a cubicRenderTarget?
    if (owner->GetParent()) {
        /// This'll create the deviceRefs for all of its children as well
        MakeRenderTargetRef(owner->GetParent());
        return owner->GetDeviceRef();
    }

    // If we already have a rendertargetref, we just need it filled out with WebGPU resources.
    if (owner->GetDeviceRef())
        ref = (plWGPURenderTargetRef*)owner->GetDeviceRef();

    // Look for supported format.
    if (!IPrepRenderTargetInfo(owner, surfFormat, depthFormat)) {
        hsAssert(false, "Error getting renderTarget info");
        return nullptr;
    }

    /// Create the render target now
    // Start with the depth surface. A cubic render target gets a single depth buffer shared by
    // its faces, since one face renders at a time. Every other target has its own
    // (fSettings.fShareDepth comes from kCapsShareDepth, which hsG3DDeviceSelector sets only for
    // ATI R8x00 records; WebGPU could not attach its larger shared surface).
    if (owner->GetZDepth() && (owner->GetFlags() & (plRenderTarget::kIsTexture | plRenderTarget::kIsOffscreen))) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{"plWGPU render target depth", WGPU_STRLEN};
        desc.usage = WGPUTextureUsage_RenderAttachment;
        desc.size = {owner->GetWidth(), owner->GetHeight(), 1};
        desc.format = depthFormat;
        depthSurface = wgpuDeviceCreateTexture(fDevice.fWGPUDevice, &desc);
        if (!depthSurface)
            return nullptr;
        fRenderTargetStats.bytes += uint64_t(owner->GetWidth()) * owner->GetHeight() * 4;
    }

    auto newRef = [this](plRenderTarget* rt) {
        plWGPURenderTargetRef* r = new plWGPURenderTargetRef();
        r->fOwner = rt;
        return r;
    };

    // See if it's a cubic render target.
    // Primary consumer here is the vertex/pixel shader water.
    cubicRT = plCubicRenderTarget::ConvertNoRef(owner);
    if (cubicRT) {
        /// And create the ref (it'll know how to set all the flags)
        if (!ref)
            ref = newRef(owner);

        WGPUTexture texture = IMakeRenderTargetTexture(owner, surfFormat, 6);
        if (texture) {
            /// Create a CUBIC texture
            for (int i = 0; i < 6; i++) {
                plRenderTarget*        face = cubicRT->GetFace(i);
                plWGPURenderTargetRef* fRef;

                if (face->GetDeviceRef() != nullptr) {
                    fRef = (plWGPURenderTargetRef*)face->GetDeviceRef();
                    if (!fRef->IsLinked())
                        fRef->Link(&fRenderTargetRefList);
                } else {
                    face->SetDeviceRef(newRef(face));
                    ((plWGPURenderTargetRef*)face->GetDeviceRef())->Link(&fRenderTargetRefList);
                    // Unref now, since for now ONLY the RT owns the ref, not us (not until we use it, at least)
                    face->GetDeviceRef()->UnRef();
                }
            }

            IRenderTargetSetTexture(ref, owner, texture, surfFormat, depthSurface, depthFormat);
        } else {
            if (depthSurface)
                wgpuTextureRelease(depthSurface);
            ref->UnRef();
            ref = nullptr;
        }
    }
    // Not a cubic, is it a texture render target? These are currently used
    // primarily for shadow map generation.
    else if (owner->GetFlags() & plRenderTarget::kIsTexture) {
        /// Create a normal texture
        if (!ref)
            ref = newRef(owner);

        WGPUTexture texture = IMakeRenderTargetTexture(owner, surfFormat, 1);
        if (texture) {
            IRenderTargetSetTexture(ref, owner, texture, surfFormat, depthSurface, depthFormat);
        } else {
            if (depthSurface)
                wgpuTextureRelease(depthSurface);
            ref->UnRef();
            ref = nullptr;
        }
    }
    // Not a texture either, must be a plain offscreen (read back by ExtractMipMap).
    else if (owner->GetFlags() & plRenderTarget::kIsOffscreen) {
        /// Create a blank surface
        if (!ref)
            ref = newRef(owner);

        WGPUTexture texture = IMakeRenderTargetTexture(owner, surfFormat, 1);
        if (texture) {
            IRenderTargetSetTexture(ref, owner, texture, surfFormat, depthSurface, depthFormat);
        } else {
            if (depthSurface)
                wgpuTextureRelease(depthSurface);
            ref->UnRef();
            ref = nullptr;
        }
    } else if (depthSurface) {
        wgpuTextureRelease(depthSurface);
    }

    // Keep it in a linked list for ready destruction.
    if (owner->GetDeviceRef() != ref) {
        owner->SetDeviceRef(ref);
        // Unref now, since for now ONLY the RT owns the ref, not us (not until we use it, at least)
        hsRefCnt_SafeUnRef(ref);
        if (ref != nullptr && !ref->IsLinked())
            ref->Link(&fRenderTargetRefList);
    } else {
        if (ref != nullptr && !ref->IsLinked())
            ref->Link(&fRenderTargetRefList);
    }

    // Mark as not dirty.
    if (ref != nullptr) {
        ref->SetDirty(false);
        fRenderTargetStats.made++;
    }

    return ref;
}

hsGDeviceRef* plWGPUPipeline::SharedRenderTargetRef(plRenderTarget* share, plRenderTarget* owner)
{
    // plDXPipeline::SharedRenderTargetRef: the shadow map pools of a size share one depth surface.
    plWGPURenderTargetRef* ref = nullptr;
    WGPUTexture            depthSurface = nullptr;
    WGPUTextureFormat      surfFormat = WGPUTextureFormat_Undefined;
    plCubicRenderTarget*   cubicRT;

    // If we don't already have one to share from, start from scratch.
    if (!share)
        return MakeRenderTargetRef(owner);

    /// Check--is this renderTarget really a child of a cubicRenderTarget?
    if (owner->GetParent() != nullptr) {
        /// This'll create the deviceRefs for all of its children as well
        SharedRenderTargetRef(share->GetParent(), owner->GetParent());
        return owner->GetDeviceRef();
    }

    if (owner->GetDeviceRef() != nullptr)
        ref = (plWGPURenderTargetRef*)owner->GetDeviceRef();

    // Look for a good format of matching color and depth size.
    if (!IFindRenderTargetInfo(owner, surfFormat)) {
        hsAssert(false, "Error getting renderTarget info");
        return nullptr;
    }

    /// Create the render target now
    // Start with the depth. We're just going to share the depth surface on the input shareRef.
    plWGPURenderTargetRef* shareRef = (plWGPURenderTargetRef*)share->GetDeviceRef();
    hsAssert(shareRef, "Trying to share from a render target with no ref");
    if (shareRef && shareRef->fDepthBuffer)
        wgpuTextureAddRef(shareRef->fDepthBuffer);
    depthSurface = shareRef ? shareRef->fDepthBuffer : nullptr;
    const WGPUTextureFormat depthFormat = shareRef ? shareRef->fDepthFormat : WGPUTextureFormat_Undefined;

    auto newRef = [](plRenderTarget* rt) {
        plWGPURenderTargetRef* r = new plWGPURenderTargetRef();
        r->fOwner = rt;
        return r;
    };

    // Check for Cubic. This is unlikely, since this function is currently only used for the
    // shadow map pools.
    cubicRT = plCubicRenderTarget::ConvertNoRef(owner);
    if (cubicRT != nullptr) {
        if (!ref)
            ref = newRef(owner);

        WGPUTexture texture = IMakeRenderTargetTexture(owner, surfFormat, 6);
        if (texture) {
            for (int i = 0; i < 6; i++) {
                plRenderTarget*        face = cubicRT->GetFace(i);
                plWGPURenderTargetRef* fRef;

                if (face->GetDeviceRef() != nullptr) {
                    fRef = (plWGPURenderTargetRef*)face->GetDeviceRef();
                    if (!fRef->IsLinked())
                        fRef->Link(&fRenderTargetRefList);
                } else {
                    face->SetDeviceRef(newRef(face));
                    ((plWGPURenderTargetRef*)face->GetDeviceRef())->Link(&fRenderTargetRefList);
                    face->GetDeviceRef()->UnRef();
                }
            }
            IRenderTargetSetTexture(ref, owner, texture, surfFormat, depthSurface, depthFormat);
        } else {
            if (depthSurface)
                wgpuTextureRelease(depthSurface);
            ref->UnRef();
            ref = nullptr;
        }
    }
    // Is it a texture render target? Probably, since shadow maps are all we use this for.
    else if (owner->GetFlags() & (plRenderTarget::kIsTexture | plRenderTarget::kIsOffscreen)) {
        if (!ref)
            ref = newRef(owner);

        WGPUTexture texture = IMakeRenderTargetTexture(owner, surfFormat, 1);
        if (texture) {
            IRenderTargetSetTexture(ref, owner, texture, surfFormat, depthSurface, depthFormat);
        } else {
            if (depthSurface)
                wgpuTextureRelease(depthSurface);
            ref->UnRef();
            ref = nullptr;
        }
    } else if (depthSurface) {
        wgpuTextureRelease(depthSurface);
    }

    if (owner->GetDeviceRef() != ref) {
        owner->SetDeviceRef(ref);
        // Unref now, since for now ONLY the RT owns the ref, not us (not until we use it, at least)
        hsRefCnt_SafeUnRef(ref);
        if (ref != nullptr && !ref->IsLinked())
            ref->Link(&fRenderTargetRefList);
    } else {
        if (ref != nullptr && !ref->IsLinked())
            ref->Link(&fRenderTargetRefList);
    }

    if (ref != nullptr) {
        ref->SetDirty(false);
        fRenderTargetStats.made++;
    }

    return ref;
}

bool plWGPUPipeline::BeginRender()
{
    // plMetalPipeline::BeginRender, with plDXPipeline::BeginRender's device checks.
    // fDeviceLost from the device's lost callback, read here only.
    if (plWGPUEnumerate::DeviceLost() && !fDeviceLost) {
        fDeviceLost = true;
        fDisplayStats.lastRecoveryMs = emscripten_get_now();
        plWGPUEnumerate::StartReacquire();
    }

    // Do we have some restoration work ahead of us?
    // Checks for Device Lost condition
    if (IResetDevice())
        return true;

    // We were lost, but now we're found! Spread the good uint16_t brother!
    if (fDevWasLost) {
        fDevWasLost = false;
    }

    // offset transform
    RefreshScreenMatrices();

    // The frame start's walks, before the canvas texture is acquired: the walked
    // drawables again when the device, the main target or the default fog changed, and the rooms PrewarmRoom could
    // not walk. A scene frame has waited for the builds already (PrewarmBeforeScene); this frame does not wait. Then
    // the builds' pacing: the finished builds are taken and queued builds issued (plWGPUDevice::PumpPipelineBuilds).
    if (fInSceneDepth == 0) {
        IPrewarmFrameStart();
        fDevice.PumpPipelineBuilds();
    }

    // If this is the primary BeginRender, make sure we're really ready.
    if (fInSceneDepth++ == 0) {
        fDevice.BeginRender();

        // Superfluous setting of Z state (as plDXPipeline::BeginRender).
        fD3DDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);

#ifdef PLASMA_TEST_HOOKS
        // The test harness's render dump settles here, on the previous frame's passes, before the counters are
        // cleared; it may arm this frame's rows (fRowsArmNext).
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnBeginRender(*this);
#endif

        // A new frame's counters
        memset(&fCounters, 0, sizeof(fCounters));
        fFrameWaveBiasSeen = false; // the waveset clock
        fFrameWaveBiasMixed = false;

        fVtxRefTime++;
        plWGPUBufferPoolRef::SetFrameTime(fVtxRefTime);

#ifdef PLASMA_TEST_HOOKS
        // The frame the test harness's render dump armed (fRowsArmNext) records its draws.
        fRowsArmed = fRowsArmNext;
        fRowsArmNext = false;
        fDevice.fKeepLastDraw = fRowsArmed;
        fDevice.fRowsArmedMirror = fRowsArmed; // no texture pack upload while rows record
#endif
        fRowCtx = RowContext();

        if (!fDevice.CreateNewCommandBuffer()) {
            // No canvas texture: D3D9's BeginScene failure sets fDeviceLost (plDXPipeline::BeginRender)
            // and the next BeginRender resets (IResetDevice's successful-Reset branch).
            fDeviceLost = true;
            fPresentationLost = true;
            EndRender();
            return true;
        }

        // As plDXPipeline::BeginRender: render our shadow maps, then the avatar
        // textures, before the frame's draws. They need the frame's command buffer (Metal
        // structure: each target records in its own encoder, submitted at the target switch).
        IPreprocessShadows();
        IPreprocessAvatarTextures();
    }

    fRenderCnt++;

    // Would probably rather this be an input.
    fTime = hsTimer::GetSysSeconds();

    return false;
}

bool plWGPUPipeline::EndRender()
{
    // plMetalPipeline::EndRender, with plDXPipeline::EndRender's IBottomLayer and
    // fForceMatHandle
    IBottomLayer();

    bool retVal = false;

    if (--fInSceneDepth == 0) {
        // plDXPipeline::IFlipSurface: the frame is submitted, and it becomes the front
        // buffer; the canvas presents it when the task ends.
        fDevice.SubmitCommandBuffer();
        retVal = fDeviceLost;

        IClearShadowSlaves();

        fLastCounters = fCounters;
#ifdef PLASMA_TEST_HOOKS
        // The test harness's GFX settings line and, on the dump frame, the render dump; first the harness's
        // readbacks that have completed (the shadow maps, clothing composites and shader targets).
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnEndRender(*this);
#endif
    }

    // Do this last, after we've drawn everything
    // Just letting go of things we're done with for the frame.
    fForceMatHandle = true;
    hsRefCnt_SafeUnRef(fCurrMaterial);
    fCurrMaterial = nullptr;

    for (int i = 0; i < 8; i++) {
        if (fLayerRef[i]) {
            fLayerRef[i]->UnRef();
            fLayerRef[i] = nullptr;
        }
    }

    return retVal;
}

void plWGPUPipeline::RenderScreenElements()
{
#ifdef PLASMA_TEST_HOOKS
    // A test harness scene-capture frame (the render dump's frame) is drawn without the screen elements.
    if (const plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get(); observer && observer->SceneCaptureFrame())
        return;
#endif

    // plMetalPipeline::RenderScreenElements
    bool reset = false;

    if (fView.HasCullProxy()) {
        Draw(fView.GetCullProxy());
    }

    hsGMatState tHack = PushMaterialOverride(hsGMatState::kMisc, hsGMatState::kMiscWireFrame, false);
    hsGMatState ambHack = PushMaterialOverride(hsGMatState::kShade, hsGMatState::kShadeWhite, true);

    // Plates
    if (fPlateMgr) {
        fPlateMgr->DrawToDevice(this);
        reset = true;
    }

    PopMaterialOverride(ambHack, true);
    PopMaterialOverride(tHack, false);

    /// Debug text
    if (fDebugTextMgr && plDebugText::Instance().IsEnabled()) {
        fDebugTextMgr->DrawToDevice(this);
        reset = true;
    }

    if (reset) {
        // As plDXPipeline::RenderScreenElements (the vertex and index buffer refs are not tracked here)
        fView.fXformResetFlags = fView.kResetAll; // Text destroys view transforms
        hsRefCnt_SafeUnRef(fLayerRef[0]);
        fLayerRef[0] = nullptr; // Text destroys stage 0 texture
    }
}

bool plWGPUPipeline::IsFullScreen() const { return fIsFullscreen; }

void plWGPUPipeline::Resize(uint32_t width, uint32_t height)
{
    // plDXPipeline::Resize. On Windows only WndProc's WM_SIZING calls it, which the client's
    // window (it has no sizing border) does not receive; ported as it is.
    // TestCooperativeLevel: a lost WebGPU device is D3DERR_DEVICELOST until the new device is
    // there, then D3DERR_DEVICENOTRESET; otherwise D3D_OK.
    const bool webgpuLost = plWGPUEnumerate::DeviceLost();
    if (webgpuLost && !plWGPUEnumerate::Ready()) {
        /// Direct3D is reporting that we lost the device but are unable to reset
        /// it yet, so ignore.
        hsStatusMessage("Received Resize() request at an invalid time. Ignoring...");
        return;
    }
    if (!width && !height) {
        if (!webgpuLost)
            return;
        // Reset on a lost device fails (as in plDXPipeline::Resize): recreate below.
    }

    // Store some states that we *want* to restore back...
    plViewTransform resetTransform = GetViewTransform();

    // Destroy old
    IReleaseDeviceObjects();

    // Reset width and height
    if (width != 0 && height != 0) {
        // Width and height of zero mean just recreate
        fOrigWidth = width;
        fOrigHeight = height;
        IGetViewTransform().SetScreenSize((uint16_t)(fOrigWidth), (uint16_t)(fOrigHeight));
        resetTransform.SetScreenSize((uint16_t)(fOrigWidth), (uint16_t)(fOrigHeight));
        fPresentWidth = width;
        fPresentHeight = height;
    } else {
        // Just for debug
        hsStatusMessage("Recreating the pipeline...");
    }

    // Recreate (hsGDirect3D::GetDirect3D and ICreateDeviceObjects)
    if (webgpuLost) {
        plWGPUEnumerate::FinishReacquire();
        fDeviceLost = false;
    }
    ICreateDevice();
    ICreateDynDeviceObjects();
    ICreateDeviceObjects();

    // Restore states
    SetViewTransform(resetTransform);
    IProjectionMatrixToDevice();

    /// Broadcast a message letting everyone know that we were recreated and that
    /// all device-specific stuff needs to be recreated
    plDeviceRecreateMsg* clean = new plDeviceRecreateMsg(this);
    clean->Send();
}

void plWGPUPipeline::IReleaseVertexIndexTextureRefs()
{
    // plDXPipeline::IReleaseDeviceObjects' vertex buffer, index buffer and texture lists
    plWGPUDeviceRef* ref;
    while (fVtxBuffRefList) {
        ref = fVtxBuffRefList;
        ref->Release();
        ref->Unlink();
    }
    while (fIdxBuffRefList) {
        ref = fIdxBuffRefList;
        ref->Release();
        ref->Unlink();
    }
    while (fTextureRefList) {
        ref = fTextureRefList;
        ref->Release();
        ref->Unlink();
    }
}

void plWGPUPipeline::IReleaseDeviceObjects()
{
    // plDXPipeline::IReleaseDeviceObjects (no MCN_BOUNDS_SPANS, no fLogDrawer: the status-log drawer is
    // not made by this pipeline, no pixel scratch, no MF_ENABLE_HACKOFF)
    for (int i = 0; i < 8; i++) {
        if (fLayerRef[i]) {
            fLayerRef[i]->UnRef();
            fLayerRef[i] = nullptr;
        }
    }

    if (fULutTextureRef)
        fULutTextureRef->UnRef();
    fULutTextureRef = nullptr;

    IReleaseVertexIndexTextureRefs();

    IReleaseShaders();

    fLights.Release();

    IReleaseDynDeviceObjects();

    delete fPlateMgr;
    fPlateMgr = nullptr;

    // The device itself: every WebGPU object made on it. The WebGPU device handle
    // belongs to plWGPUEnumerate.
    fDevice.ReleaseAllDeviceObjects();
}

void plWGPUPipeline::ICreateDevice()
{
    // plDXPipeline::ICreateDevice in WebGPU terms: the device
    // plWGPUEnumerate holds, a fresh D3D9 state (a new IDirect3DDevice9 has default states),
    // the device's own objects, samplers and sample count, and the swap chain at the present
    // parameters.
    fDevice.fWGPUDevice = plWGPUEnumerate::Device();
    fDevice.fD3D.Reset();
    fDevice.InitDevice();
    fDevice.SetMaxAnsiotropy(fSettings.fMaxAnisotropicSamples);
    fDevice.SetMSAASampleCount(uint8_t(std::max(fNumAASamples, 0)));
    fDevice.ConfigureSurface(fPresentWidth, fPresentHeight);
}

bool plWGPUPipeline::IResetDevice()
{
    // plDXPipeline::IResetDevice. TestCooperativeLevel: a lost WebGPU device is
    // D3DERR_DEVICELOST until the new one is there, then D3DERR_DEVICENOTRESET; a lost
    // presentation (no canvas texture) is D3DERR_DEVICENOTRESET; otherwise D3D_OK. Nothing waits
    // (no Sleep(100) on the client thread): BeginRender returns true until then.
    if (fDeviceLost) {
        IClearShadowSlaves();

        const bool webgpuLost = plWGPUEnumerate::DeviceLost();
        if (webgpuLost && !plWGPUEnumerate::Ready()) {
            // Nothing to do yet.
            return true;
        }
        if (webgpuLost || fPresentationLost || fForceDeviceReset) {
            const double t0 = emscripten_get_now();
            plStatusLog::AddLineS("pipeline.log", 0xffff0000, "Resetting Device");
            IReleaseDynDeviceObjects();
            // IFindDepthFormat: the depth formats exist on every WebGPU device.
            if (webgpuLost) {
                // Reset fails on a lost device: release everything and recreate it (the
                // failed-Reset branch).
                ILogPlates("at recovery, before the release");
                IReleaseDeviceObjects();
                plWGPUEnumerate::FinishReacquire();
                ICreateDevice();
                ICreateDynDeviceObjects();
                ICreateDeviceObjects();
                fDisplayStats.lastRecoveryMs = emscripten_get_now() - fDisplayStats.lastRecoveryMs;
            } else {
                // Reset succeeds: the swap chain takes the present parameters, the device
                // state returns to its defaults; textures, buffers, shaders and plates survive.
                fDevice.fD3D.Reset();
                fDevice.SetMSAASampleCount(uint8_t(std::max(fNumAASamples, 0)));
                fDevice.SetMaxAnsiotropy(fSettings.fMaxAnisotropicSamples);
                fDevice.ConfigureSurface(fPresentWidth, fPresentHeight);
            }
            fSettings.fCurrVertexShader = nullptr;
            ICreateDynDeviceObjects();
            IInitDeviceState();

            /// Broadcast a message letting everyone know that we were recreated and that
            /// all device-specific stuff needs to be recreated
            plDeviceRecreateMsg* clean = new plDeviceRecreateMsg(this);
            clean->Send();
            fDisplayStats.lastResetMs = emscripten_get_now() - t0;
            plWGPULog("IResetDevice: {} branch, {}x{} MSAA {}, {.1f} ms{}", webgpuLost ? "failed-Reset (lost device)" : "successful-Reset",
                      fPresentWidth, fPresentHeight, fDevice.fSampleCount, fDisplayStats.lastResetMs,
                      webgpuLost ? ST::format(", {.1f} ms after the loss", fDisplayStats.lastRecoveryMs)
                                 : ST::string());
            if (webgpuLost)
                ILogPlates("after the recreate");
        }
        fDevWasLost = true;
        fDeviceLost = false;
        fPresentationLost = false;

        // We return true here, even though we've successfully recreated, to take
        // another spin around the update loop and give everyone a chance to
        // get back in sync.
        return true;
    }
    return false;
}

void plWGPUPipeline::ILogPlates(const char* when)
{
    if (plWGPUPlateManager* pm = static_cast<plWGPUPlateManager*>(fPlateMgr))
        plWGPULog("plates {}: {}", when, pm->DescribePlates());
    else
        plWGPULog("plates {}: no plate manager", when);
}

void plWGPUPipeline::LoadResources()
{
    // plMetalPipeline::LoadResources
    hsStatusMessageF("Begin Device Reload t={}", hsTimer::GetSeconds());
    plNetClientApp::StaticDebugMsg("Begin Device Reload");

    // Just to be safe (plDXPipeline::LoadResources). The lights register again on the
    // plPipeRTMakeMsg below (plLightInfo::MsgReceive).
    IInitDeviceState();

    if (plWGPUPlateManager* pm = static_cast<plWGPUPlateManager*>(fPlateMgr))
        pm->IReleaseGeometry();

    IReleaseDynamicBuffers();
    IReleaseAvRTPool();

    // Create all RenderTargets
    plPipeRTMakeMsg* rtMake = new plPipeRTMakeMsg(this);
    rtMake->Send();

    // plDXPipeline::ICreateDynamicBuffers: the blur quads and the plates.
    ICreateBlurVBuffers();
    if (plWGPUPlateManager* pm = static_cast<plWGPUPlateManager*>(fPlateMgr))
        pm->ICreateGeometry();

    // Create all POOL_DEFAULT (sorted) index buffers in the scene.
    plPipeGeoMakeMsg* defMake = new plPipeGeoMakeMsg(this, true);
    defMake->Send();

    // This can be a bit of a mem hog and will use more mem if available, so
    // keep it last in the POOL_DEFAULT allocs.
    IFillAvRTPool();

    // Force a create of all our static vertex buffers.
    plPipeGeoMakeMsg* manMake = new plPipeGeoMakeMsg(this, false);
    manMake->Send();

    // Okay, we've done it, clear the request.
    plPipeResReq::Clear();

    hsStatusMessageF("End Device Reload t={}", hsTimer::GetSeconds());
    plNetClientApp::StaticDebugMsg("End Device Reload");
}

// MARK: - Pipelines built ahead of their first draw

// On WebGPU a render pipeline is a shader compile in the browser's GPU process. Created at the draw that first needs
// it (PipelineState), a new Age's pipelines compile one after another in the first frame that draws it; a cold
// browser shader cache turns that into seconds of frozen arrival. D3D9 has no pipeline objects (its shader objects
// are made at first use, plDXPipeline::ISetShaders, plDXVertexShader::GetShader); upstream Metal builds
// asynchronously and waits at the draw (plMetalDevice::PipelineState, plMetalDevice.cpp:1138-1180), and its prewarm
// entry (PrewarmPipelineStateFor, plMetalDevice.cpp:1183) is reached only from
// plMetalPipelineState::PrewarmRenderPipelineState, which nothing calls. These are this port's callers:
// - PrewarmRoom, from plClient::IRoomLoaded: a loaded room's drawables go through the draw's own material code
//   (RenderSpans: ISetupTransforms, ILoopOverLayers, IHandleMaterial, ISetFogParameters, ISetShaders) with a render
//   functor that draws nothing (plWGPUPrewarmTriListFunc), and the device starts their pipelines' builds, which
//   compile in parallel while the load goes on. Everything the walk writes is restored after it.
// - The frame start (IPrewarmFrameStart, at every primary BeginRender and before a scene frame): when the device,
//   the main target or the default fog changed since the drawables were walked (an Age's .fni sets its fog at
//   plAgeLoaded2Msg, after its rooms loaded), the walked drawables are walked again; rooms PrewarmRoom could not walk
//   are walked there.
// - PrewarmBeforeScene, from plClient::IDraw before the scene frame's BeginRender: this frame's render-target passes
//   (the shadow maps with their blur, the clothing composites) and render requests (IPrewarmRequests: each as
//   plRenderRequest::Render draws it, under its own target and view: an environment map's faces, a waveset's bump
//   composite, a GUI dialog), then the wait for every build in flight, before the canvas texture is acquired.
//   Progress frames (plClient::IDrawProgress) draw no scene and do not wait, so the builds compile through the load
//   and the arrival frame waits only for what is left.

bool plWGPUPipeline::PrewarmUoidLess::operator()(const plUoid& a, const plUoid& b) const
{
    const auto key = [](const plUoid& u) {
        return std::make_tuple(u.GetLocation().GetSequenceNumber(), u.GetLocation().GetFlags(), u.GetClassType(),
                               u.GetObjectID(), u.GetCloneID(), u.GetClonePlayerID());
    };
    return key(a) < key(b);
}

const char* plWGPUPipeline::IPrewarmSkipped() const
{
    // Only between frames: inside a frame the walk would write the frame's state, and the wait would suspend it with
    // the canvas texture held.
    if (fInSceneDepth != 0)
        return "inside a frame";
    if (!fDevice.CanPrewarm())
        return "no canvas texture yet";
    if (GetOverrideMaterial())
        return "an override material is pushed";
    return nullptr;
}

hsKeyedObject* plWGPUPipeline::IPrewarmLoadedObject(const plUoid& uoid)
{
    // The object if its page and key are loaded, and nothing is loaded to find out: hsResMgr::FindKey reads and holds
    // a page's keys on a miss (plResManager::FindOriginalKey, plResManagerHelper::LoadAndHoldPageKeys); the page's
    // own key list does not.
    // A clone's key is not in its page's key list (plRegistryKeyList::FindKey looks up the original by object ID): it
    // is found on the original, as plResManager::FindKey does (plResManager.cpp:579-587, plKeyImp::GetClone).
    plRegistryPageNode* page = static_cast<plResManager*>(hsgResMgr::ResMgr())->FindPage(uoid.GetLocation());
    if (!page)
        return nullptr;
    plKeyImp* key = page->FindKey(uoid);
    if (!key)
        return nullptr;
    if (uoid.IsClone()) {
        plKey clone = key->GetClone(uoid.GetClonePlayerID(), uoid.GetCloneID());
        return clone ? clone->ObjectIsLoaded() : nullptr;
    }
    return key->ObjectIsLoaded();
}

void plWGPUPipeline::IPrewarmRoomDrawables(plSceneNode* node, std::vector<plDrawableSpans*>& drawables)
{
    for (plDrawable* drawable : node->GetDrawPool()) {
        if (plDrawableSpans* spans = plDrawableSpans::ConvertNoRef(drawable))
            drawables.push_back(spans);
    }
}

void plWGPUPipeline::IPrewarmFog(uint8_t& type, bool& degenerate) const
{
    // ISetFogParameters' inputs from the default fog, outside any render request (PushRenderRequest sets it for a
    // request and PopRenderRequest restores it): FOGENABLE is FALSE for kNoFog and for a linear fog whose start
    // equals its end; shader layers take the linear path for every type.
    const plFogEnvironment& fog = fView.GetDefaultFog();
    type = fog.GetType();
    degenerate = false;
    if (type != plFogEnvironment::kNoFog) {
        float       start, end;
        hsColorRGBA color;
        fog.GetPipelineParams(&start, &end, &color);
        degenerate = start == end;
    }
}

void plWGPUPipeline::PrewarmRoom(plSceneNode* node, bool afterAgeSettings)
{
    if (!node)
        return;
    // A page of an Age load whose .fni has not run (plClient::IRoomLoaded): the .fni sets the Age's default fog at
    // plAgeLoaded2Msg, after the Age's last page (plAgeLoaderPaging.cpp:124-128, plNetCliAgeJoiner.cpp:413-416,
    // plAgeLoader::ExecPendingAgeFniFiles, plAgeLoader.cpp:422-430), so a walk now would build the room's pipelines
    // for the fog in force before it, which the Age never draws with. The room waits for PrewarmAgeSettings.
    if (afterAgeSettings) {
        fPrewarmDeferredRooms.push_back(node->GetKey()->GetUoid());
        return;
    }
    std::vector<plDrawableSpans*> drawables;
    IPrewarmRoomDrawables(node, drawables);
    if (IPrewarmWalk(drawables, "room", node->GetKey()->GetName().c_str()))
        IPrewarmNoteRoom(node);
    else
        fPrewarmPendingRooms.push_back(node->GetKey()->GetUoid());
}

void plWGPUPipeline::PrewarmAgeSettings()
{
    // The .fni's plConsoleMsg is dispatched in the dispatch loop of the plAgeLoaded2Msg that ran it, so the next frame
    // start, which walks the released rooms, sees the Age's fog. A room unloaded meanwhile (an aborted load) is
    // dropped; its draws, if it comes back, build at the draw or at its next load.
    if (fPrewarmDeferredRooms.empty())
        return;
    size_t gone = 0;
    for (const plUoid& uoid : fPrewarmDeferredRooms) {
        if (plSceneNode::ConvertNoRef(IPrewarmLoadedObject(uoid)))
            fPrewarmReleasedRooms.push_back(uoid);
        else
            gone++;
    }
    plStatusLog::AddLineSF("pipeline.log", 0xff00ffff,
                           "pipeline prewarm: {} rooms of the Age kept for its .fni, walked at the next frame start{}",
                           fPrewarmDeferredRooms.size() - gone,
                           gone ? ST::format(" ({} no longer loaded, dropped)", gone) : ST::string());
    fPrewarmDeferredRooms.clear();
}

bool plWGPUPipeline::PrewarmBeforeScene(const std::vector<plRenderRequest*>& preRequests,
                                        const std::vector<plRenderRequest*>& postRequests, plPageTreeMgr* pageMgr,
                                        bool mayDefer, const char* detail)
{
    if (fInSceneDepth != 0)
        return false;
    IPrewarmFrameStart();
    // This frame's render-target passes and render requests are known now: plRenderMsg has submitted the shadow
    // slaves (pl3DPipeline::SubmitShadowSlave) and the outfits (SubmitClothingOutfit, on plPreResourceMsg) and queued
    // the render requests (plClient::IAddRenderRequest), and BeginRender draws the passes after it acquires the canvas
    // texture (IPreprocessShadows, IPreprocessAvatarTextures; plDXPipeline::BeginRender, plDXPipeline.cpp:2972-2973),
    // then plClient::IDraw the requests (IProcessPreRenderRequests, IProcessPostRenderRequests). The skip test runs
    // once, here: a request's own override material (PushRenderRequest) must not stop its walk.
    if (!IPrewarmSkipped()) {
        IPrewarmTargets();
        IPrewarmRequests(preRequests, postRequests, pageMgr);
    }
    if (mayDefer && fDevice.PipelineBuildsPending()) {
        // Not drawn, as a frame whose device is not ready: plDXPipeline::BeginRender returns true after IResetDevice
        // has cleared the shadow slaves (plDXPipeline.cpp:1909-1918, 2921-2926), and plClient::IDraw flushes the
        // render requests (plClient.cpp:1929-1932). The outfits stay submitted, as there; the next primary
        // BeginRender composites them (plDXPipeline.cpp:2972-2973).
        const uint32_t pending = uint32_t(fDevice.PipelineBuildsPending());
        fDevice.PumpPipelineBuilds();
        IClearShadowSlaves();
#ifdef PLASMA_TEST_HOOKS
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
            plWGPUPipelineObserver::PrewarmView view;
            view.seat = "dropped";
            view.detail = detail;
            view.drained = pending;
            view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
            observer->OnPrewarm(*this, view);
        }
#endif
        plStatusLog::AddLineSF("pipeline.log", 0xff00ffff,
                               "pipeline prewarm: scene frame not drawn, {} pipeline builds pending after the load",
                               pending);
        return true;
    }
    IPrewarmDrain();
    return false;
}

size_t plWGPUPipeline::PrewarmPending()
{
    if (fInSceneDepth != 0)
        return 0;
    IPrewarmFrameStart();
    fDevice.PumpPipelineBuilds();
    return fDevice.PipelineBuildsPending();
}

size_t plWGPUPipeline::PumpPipelineBuilds()
{
    fDevice.PumpPipelineBuilds();
    return fDevice.PipelineBuildsPending();
}

void plWGPUPipeline::NotePrewarmWait(uint32_t before, uint32_t after, uint32_t frames, double ms, const char* detail)
{
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
        plWGPUPipelineObserver::PrewarmView view;
        view.seat = "wait";
        view.detail = detail;
        view.drained = before;
        view.inFlightAfter = after;
        view.frames = frames;
        view.drainMs = ms;
        observer->OnPrewarm(*this, view);
    }
#endif
    if (ms > 100.0)
        plStatusLog::AddLineSF("pipeline.log", 0xff00ffff,
                               "pipeline prewarm: waited {.1f} ms for {} pipeline builds, {} progress frames drawn",
                               ms, before, frames);
}

void plWGPUPipeline::IPrewarmPoolSpans(const std::vector<plDrawable*>& pool, std::vector<size_t>& spans)
{
    spans.clear();
    spans.reserve(pool.size());
    for (plDrawable* drawable : pool) {
        const plDrawableSpans* ds = plDrawableSpans::ConvertNoRef(drawable);
        spans.push_back(ds ? ds->GetSpanArray().size() : 0);
    }
}

void plWGPUPipeline::IPrewarmNoteRoom(plSceneNode* node)
{
    // The walked room, its draw pool and its drawables' span counts, which IPrewarmFrameStart compares at every frame
    // start.
    const plUoid& uoid = node->GetKey()->GetUoid();
    for (PrewarmRoomPool& room : fPrewarmRooms) {
        if (room.node == uoid) {
            room.pool = node->GetDrawPool();
            IPrewarmPoolSpans(room.pool, room.spans);
            return;
        }
    }
    PrewarmRoomPool& room = fPrewarmRooms.emplace_back();
    room.node = uoid;
    room.pool = node->GetDrawPool();
    IPrewarmPoolSpans(room.pool, room.spans);
}

void plWGPUPipeline::IPrewarmFrameStart()
{
    if (fInSceneDepth != 0 || IPrewarmSkipped())
        return;

    // The conditions the walked set was walked under: the device generation, the main target, the default fog.
    uint8_t fogType;
    bool    fogDegenerate;
    IPrewarmFog(fogType, fogDegenerate);
    const char* cause = nullptr;
    if (!fPrewarmConditionsSet)
        cause = "first";
    else if (fPrewarmGeneration != fDevice.Generation())
        cause = "device";
    else if (fPrewarmColor != fDevice.fFramebufferFormat || fPrewarmDepth != fDevice.fDepthFormat ||
             fPrewarmSamples != fDevice.fSampleCount)
        cause = "target";
    else if (fPrewarmFogType != fogType || fPrewarmFogDegenerate != fogDegenerate)
        cause = "fog";
    if (cause) {
        std::vector<plDrawableSpans*> drawables;
        for (const plUoid& uoid : fPrewarmWalked) {
            if (plDrawableSpans* drawable = plDrawableSpans::ConvertNoRef(IPrewarmLoadedObject(uoid)))
                drawables.push_back(drawable);
        }
        fPrewarmWalked.clear();
        fPrewarmRequestsWalked.clear();
        fPrewarmGeneration = fDevice.Generation();
        fPrewarmColor = fDevice.fFramebufferFormat;
        fPrewarmDepth = fDevice.fDepthFormat;
        fPrewarmSamples = fDevice.fSampleCount;
        fPrewarmFogType = fogType;
        fPrewarmFogDegenerate = fogDegenerate;
        fPrewarmConditionsSet = true;
        if (!drawables.empty())
            IPrewarmWalk(drawables, "re-walk", cause);
    }

    // The drawables that joined a walked room since it was walked: a cluster group's drawable, made and added to its
    // room on the group's first plEvalMsg (plClusterGroup::UnPack, plClusterGroup.cpp:268-296), a clone placed in the
    // clone room (an avatar's or an NPC's, plNetClientMgr::ILoadClone, plNetClientMgrLoad.cpp:107-111;
    // plDrawInterface::ISetSceneNode), or a drawable that moved rooms (plDrawableSpans::SetSceneNode). The pool's
    // contents are compared, not its size. A drawable whose span count changed since its walk is walked again: clothing
    // appends an avatar's spans to its drawable after the avatar joined the clone room (plClothingOutfit::IAddItem,
    // plAvatarClothing.cpp:553-589; plInstanceDrawInterface::AddSharedMesh; plDrawableSpans::AppendDISpans,
    // plDrawableSpans.cpp:2100), and removal (RemoveDISpans, IRemoveGarbage) shrinks it.
    {
        std::vector<plDrawableSpans*> drawables;
        std::vector<size_t>           spans;
        ST::string                    rooms;
        for (auto it = fPrewarmRooms.begin(); it != fPrewarmRooms.end();) {
            plSceneNode* node = plSceneNode::ConvertNoRef(IPrewarmLoadedObject(it->node));
            if (!node) {
                it = fPrewarmRooms.erase(it);
                continue;
            }
            const std::vector<plDrawable*>& pool = node->GetDrawPool();
            IPrewarmPoolSpans(pool, spans);
            if (pool != it->pool || spans != it->spans) {
                uint32_t respanned = 0;
                for (size_t i = 0; i < pool.size(); i++) {
                    plDrawableSpans* ds = plDrawableSpans::ConvertNoRef(pool[i]);
                    if (!ds)
                        continue;
                    const auto old = std::find(it->pool.begin(), it->pool.end(), pool[i]);
                    const bool joined = old == it->pool.end();
                    const bool changed = !joined && it->spans[old - it->pool.begin()] != spans[i];
                    if (changed) {
                        fPrewarmWalked.erase(ds->GetKey()->GetUoid());
                        respanned++;
                    }
#ifdef PLASMA_TEST_HOOKS
                    if (joined || changed) {
                        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                            observer->OnPrewarmJoined(*this, node, ds, joined ? -1 : int(it->spans[old - it->pool.begin()]));
                    }
#endif
                }
                rooms += ST::format("{}{}{}", rooms.empty() ? "" : ", ", node->GetKey()->GetName(),
                                    respanned ? ST::format(" ({} drawables with changed spans)", respanned) : ST::string());
                it->pool = pool;
                it->spans = spans;
                IPrewarmRoomDrawables(node, drawables);
            }
            ++it;
        }
        if (!drawables.empty())
            IPrewarmWalk(drawables, "joined", rooms.c_str());
    }

    // The rooms PrewarmRoom could not walk, and an Age's rooms released after its .fni (PrewarmAgeSettings).
    const auto walkRooms = [this](std::vector<plUoid>& list, const char* seat, const char* detail) {
        if (list.empty())
            return;
        std::vector<plUoid> rooms;
        rooms.swap(list);
        std::vector<plSceneNode*>     nodes;
        std::vector<plDrawableSpans*> drawables;
        for (const plUoid& uoid : rooms) {
            if (plSceneNode* node = plSceneNode::ConvertNoRef(IPrewarmLoadedObject(uoid))) {
                nodes.push_back(node);
                IPrewarmRoomDrawables(node, drawables);
            }
        }
        if (IPrewarmWalk(drawables, seat, detail)) {
            for (plSceneNode* node : nodes)
                IPrewarmNoteRoom(node);
        } else {
            list.insert(list.end(), rooms.begin(), rooms.end());
        }
    };
    walkRooms(fPrewarmPendingRooms, "pending rooms", nullptr);
    if (!fPrewarmReleasedRooms.empty()) {
        const ST::string fog = ST::format("after the .fni: fog type {}{}", fPrewarmFogType,
                                          fPrewarmFogDegenerate ? " (degenerate)" : "");
        walkRooms(fPrewarmReleasedRooms, "deferred rooms", fog.c_str());
    }
}

void plWGPUPipeline::IPrewarmDrain()
{
    if (fInSceneDepth != 0 || !fDevice.PipelineBuildsPending())
        return;
    // A scene frame: no draw of it finds a build queued or in flight.
    const uint32_t inFlight = uint32_t(fDevice.PipelineBuildsPending());
    double         drainMs = 0.0;
    fDevice.DrainPipelineBuilds(drainMs);
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
        plWGPUPipelineObserver::PrewarmView view;
        view.seat = "drain";
        view.drained = inFlight;
        view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
        view.drainMs = drainMs;
        observer->OnPrewarm(*this, view);
    }
    if (fPrewarmCensusDue) {
        fPrewarmCensusDue = false;
        IPrewarmCensus();
    }
#endif
    if (drainMs > 100.0)
        plStatusLog::AddLineSF("pipeline.log", 0xff00ffff, "pipeline prewarm: waited {.1f} ms for {} pipeline builds",
                               drainMs, inFlight);
}

// The state a walk writes, saved before it and restored after it (IPrewarmBegin, IPrewarmEnd): the emulated D3D9
// device, the material state, the transforms, the camera (SetViewTransform: the device's camera matrices and fFrame,
// which override layers read), the viewport, the light enables (IInvalidateState and ISetShadowLightState clear them)
// and the base render target. The material code pushes and pops the override and piggy-back stacks in pairs, and the
// render-target passes push and pop the views and targets in pairs: those are checked unchanged.
struct plWGPUPipeline::PrewarmSaved
{
    plWGPUD3DDevice            d3d;
    hsGMatState                layerState[8], oldLayerState[8];
    hsGDeviceRef*              layerRef[8];
    bool                       layerTransform[8];
    float                      layerLODBias[8];
    uint32_t                   layerUVWSrcs[8], layerXformFlags[8];
    uint32_t                   lastEndingStage;
    bool                       texturing;
    bool                       textureInvalid;
    const plWGPUShaderProgram* vertexShader;
    const plWGPUShaderProgram* pixelShader;
    hsGMaterial*               material;
    uint32_t                   layerIdx;
    uint32_t                   numLayers;
    plLayerInterface*          lay;
    bool                       forceMatHandle;
    uint32_t                   renderLayer;
    uint32_t                   lightingMethod;
    bool                       liteState;
    plWGPUFogSettings          fog;
    hsMatrix44                 bumpDu, bumpDv, bumpDw;
    bool                       unsupportedPass;
    RowContext                 rowCtx;
    plWGPUD3D9::D3DCULL        cullMode;
    hsMatrix44                 matrixL2W, matrixW2L, matrixProj, matrixW2C, matrixC2W;
    float                      viewport[4];
    hsMatrix44                 viewL2W, viewW2L;
    bool                       viewVectorsDirty;
    bool                       leftHanded;
    uint8_t                    xformResetFlags;
    uint32_t                   frame;
    hsBitVector                lightsEnabled, lightsHeld;
    plRenderTarget*            baseRenderTarget;
    // Checked unchanged.
    size_t                     overLayers, piggyBacks;
    int32_t                    activePiggyBacks;
    plLayerInterface*          overBase;
    plLayerInterface*          overAll;
    hsGMatState                matOverOn, matOverOff;
    plRenderTarget*            renderTarget;
    hsGDeviceRef*              renderTargetRef;
    size_t                     renderTargets, views, overrideMats;
    double                     t0;
};

void plWGPUPipeline::IPrewarmBegin(PrewarmSaved& s, const char* seat)
{
    s.t0 = emscripten_get_now();
    s.d3d = fDevice.fD3D;
    for (int i = 0; i < 8; i++) {
        s.layerState[i] = fLayerState[i];
        s.oldLayerState[i] = fOldLayerState[i];
        s.layerRef[i] = fLayerRef[i];
        s.layerTransform[i] = fLayerTransform[i];
        s.layerLODBias[i] = fLayerLODBias[i];
        s.layerUVWSrcs[i] = fLayerUVWSrcs[i];
        s.layerXformFlags[i] = fLayerXformFlags[i];
    }
    s.lastEndingStage = fLastEndingStage;
    s.texturing = fTexturing;
    s.textureInvalid = fSettings.fVeryAnnoyingTextureInvalidFlag;
    s.vertexShader = fSettings.fCurrVertexShader;
    s.pixelShader = fSettings.fCurrPixelShader;
    s.material = fCurrMaterial;
    hsRefCnt_SafeRef(s.material);
    s.layerIdx = fCurrLayerIdx;
    s.numLayers = fCurrNumLayers;
    s.lay = fCurrLay;
    s.forceMatHandle = fForceMatHandle;
    s.renderLayer = fCurrRenderLayer;
    s.lightingMethod = fCurrLightingMethod;
    s.liteState = fCurrD3DLiteState;
    s.fog = fCurrFog;
    s.bumpDu = fBumpDuMatrix;
    s.bumpDv = fBumpDvMatrix;
    s.bumpDw = fBumpDwMatrix;
    s.unsupportedPass = fUnsupportedProgramPass;
    s.rowCtx = fRowCtx;
    s.cullMode = fDevice.fCurrCullMode;
    s.matrixL2W = fDevice.fMatrixL2W;
    s.matrixW2L = fDevice.fMatrixW2L;
    s.matrixProj = fDevice.fMatrixProj;
    s.matrixW2C = fDevice.fMatrixW2C;
    s.matrixC2W = fDevice.fMatrixC2W;
    std::copy(std::begin(fDevice.fViewport), std::end(fDevice.fViewport), s.viewport);
    s.viewL2W = fView.GetLocalToWorld();
    s.viewW2L = fView.GetWorldToLocal();
    s.viewVectorsDirty = fView.fViewVectorsDirty;
    s.leftHanded = fView.fLocalToWorldLeftHanded;
    s.xformResetFlags = fView.fXformResetFlags;
    s.frame = fFrame;
    s.lightsEnabled = fLights.fEnabledFlags;
    s.lightsHeld = fLights.fHoldFlags;
    s.baseRenderTarget = fCurrBaseRenderTarget;
    s.overLayers = fOverLayerStack.size();
    s.piggyBacks = fPiggyBackStack.size();
    s.activePiggyBacks = fActivePiggyBacks;
    s.overBase = fOverBaseLayer;
    s.overAll = fOverAllLayer;
    s.matOverOn = fMatOverOn;
    s.matOverOff = fMatOverOff;
    s.renderTarget = fCurrRenderTarget;
    s.renderTargetRef = fCurrRenderTargetRef;
    s.renderTargets = fRenderTargets.size();
    s.views = fViewStack.size();
    s.overrideMats = fOverrideMat.size();

    // fForceMatHandle makes the first IHandleMaterial compute its state (IClearMembers, BeginRender).
    fPrewarmWalk = true;
    fPrewarmSeat = seat;
    fPrewarmTally = PrewarmTally();
    fForceMatHandle = true;
    fDevice.BeginPrewarmWalk();
}

void plWGPUPipeline::IPrewarmEnd(PrewarmSaved& s)
{
    fDevice.EndPrewarmWalk();
    fPrewarmWalk = false;
    fPrewarmSeat = nullptr;

    fDevice.fD3D = s.d3d;
    for (int i = 0; i < 8; i++) {
        fLayerState[i] = s.layerState[i];
        fOldLayerState[i] = s.oldLayerState[i];
        fLayerTransform[i] = s.layerTransform[i];
        fLayerLODBias[i] = s.layerLODBias[i];
        fLayerUVWSrcs[i] = s.layerUVWSrcs[i];
        fLayerXformFlags[i] = s.layerXformFlags[i];
        if (fLayerRef[i] != s.layerRef[i])
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "pipeline prewarm: stage {} texture ref changed by the walk", i);
    }
    fLastEndingStage = s.lastEndingStage;
    fTexturing = s.texturing;
    fSettings.fVeryAnnoyingTextureInvalidFlag = s.textureInvalid;
    fSettings.fCurrVertexShader = s.vertexShader;
    fSettings.fCurrPixelShader = s.pixelShader;
    hsRefCnt_SafeAssign(fCurrMaterial, s.material);
    hsRefCnt_SafeUnRef(s.material);
    fCurrLayerIdx = s.layerIdx;
    fCurrNumLayers = s.numLayers;
    fCurrLay = s.lay;
    fForceMatHandle = s.forceMatHandle;
    fCurrRenderLayer = s.renderLayer;
    fCurrLightingMethod = s.lightingMethod;
    fCurrD3DLiteState = s.liteState;
    fCurrFog = s.fog;
    fBumpDuMatrix = s.bumpDu;
    fBumpDvMatrix = s.bumpDv;
    fBumpDwMatrix = s.bumpDw;
    fUnsupportedProgramPass = s.unsupportedPass;
    fRowCtx = s.rowCtx;
    fDevice.fCurrCullMode = s.cullMode;
    fDevice.fMatrixL2W = s.matrixL2W;
    fDevice.fMatrixW2L = s.matrixW2L;
    fDevice.fMatrixProj = s.matrixProj;
    fDevice.fMatrixW2C = s.matrixW2C;
    fDevice.fMatrixC2W = s.matrixC2W;
    std::copy(std::begin(s.viewport), std::end(s.viewport), fDevice.fViewport);
    fView.SetLocalToWorld(s.viewL2W);
    fView.SetWorldToLocal(s.viewW2L);
    fView.fViewVectorsDirty = s.viewVectorsDirty;
    fView.fLocalToWorldLeftHanded = s.leftHanded;
    fView.fXformResetFlags = s.xformResetFlags;
    fFrame = s.frame;
    fLights.fEnabledFlags = s.lightsEnabled;
    fLights.fHoldFlags = s.lightsHeld;
    fCurrBaseRenderTarget = s.baseRenderTarget;
    if (fOverLayerStack.size() != s.overLayers || fPiggyBackStack.size() != s.piggyBacks ||
        fActivePiggyBacks != s.activePiggyBacks || fOverBaseLayer != s.overBase || fOverAllLayer != s.overAll ||
        s.matOverOn != fMatOverOn || s.matOverOff != fMatOverOff)
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "pipeline prewarm: the material overrides changed in the walk");
    if (fCurrRenderTarget != s.renderTarget || fCurrRenderTargetRef != s.renderTargetRef ||
        fRenderTargets.size() != s.renderTargets || fViewStack.size() != s.views || fOverrideMat.size() != s.overrideMats) {
        plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                               "pipeline prewarm: the render target, view or override material stack changed in the walk");
        fCurrRenderTarget = s.renderTarget;
        hsRefCnt_SafeAssign(fCurrRenderTargetRef, s.renderTargetRef);
    }
}

bool plWGPUPipeline::IPrewarmWalk(const std::vector<plDrawableSpans*>& drawables, const char* seat, const char* detail)
{
#ifdef PLASMA_TEST_HOOKS
    plWGPUPipelineObserver::PrewarmView view;
    view.seat = seat;
    view.detail = detail;
    view.drawables = uint32_t(drawables.size());
    const auto report = [&]() {
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
            observer->OnPrewarm(*this, view);
    };
#endif
    if (const char* skipped = IPrewarmSkipped()) {
#ifdef PLASMA_TEST_HOOKS
        view.skipped = skipped;
        report();
#endif
        return false;
    }

    // Once per drawable under the walked set's conditions (IPrewarmFrameStart).
    std::vector<plDrawableSpans*> toWalk;
    for (plDrawableSpans* drawable : drawables) {
        if (fPrewarmWalked.insert(drawable->GetKey()->GetUoid()).second)
            toWalk.push_back(drawable);
    }
    if (toWalk.empty()) {
#ifdef PLASMA_TEST_HOOKS
        view.walkedBefore = view.drawables;
        view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
        report();
#endif
        return true;
    }

    PrewarmSaved saved;
    IPrewarmBegin(saved, seat);
    for (plDrawableSpans* drawable : toWalk)
        IPrewarmDrawable(drawable, nullptr);
    const PrewarmTally tally = fPrewarmTally;
    IPrewarmEnd(saved);
    const double walkMs = emscripten_get_now() - saved.t0;

#ifdef PLASMA_TEST_HOOKS
    view.walkedBefore = view.drawables - uint32_t(toWalk.size());
    view.spans = tally.spans;
    view.requests = tally.requests;
    view.started = tally.started;
    view.opacityPasses = tally.opacityPasses;
    view.opacityStarted = tally.opacityStarted;
    view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
    view.walkMs = walkMs;
    fPrewarmCensusDue = true;
    report();
#endif
    if (walkMs > 100.0)
        plStatusLog::AddLineSF("pipeline.log", 0xff00ffff, "pipeline prewarm ({}): {} drawables, {} pipelines started; walk {.1f} ms",
                               seat, toWalk.size(), tally.started, walkMs);
    return true;
}

void plWGPUPipeline::IPrewarmTargets()
{
    // This frame's render-target passes, before BeginRender draws them, in its order (plDXPipeline::BeginRender,
    // plDXPipeline.cpp:2972-2973): the shadow maps (IPreprocessShadows' loop, plDXPipeline.cpp:11199-11248), then the
    // clothing composites (IPreprocessAvatarTextures). They run under the walk with the live device state, as the frame
    // will: between here and IPreprocessShadows the frame's BeginRender sets only ZENABLE TRUE, which nothing in the
    // port ever sets FALSE. Every slave and outfit is walked every scene frame, and the device's record lookup
    // (PrewarmPipelineStateFor) is the only deduplication: a cast pass's key takes state the cast does not set
    // (BLENDOP, kMiscTwoSided, the bound pixel shader) and per-span state (format, matrices, handedness), and a
    // caster's spans are collected again each frame.
    if (fShadows.empty() && fClothingOutfits.empty())
        return;
#ifdef PLASMA_TEST_HOOKS
    const uint32_t inFlightBefore = uint32_t(fDevice.PipelineBuildsPending());
#endif
    PrewarmSaved saved;
    IPrewarmBegin(saved, "targets");
    if (!fShadows.empty()) {
        // IPreprocessShadows without its own state changes: the pool cursors and the blur's scratch and destination
        // targets start where IResetRenderTargetPools starts them (plDXPipeline.cpp:11065-11075) and are restored
        // after; no slave leaves fShadows when its cast fails; no counter, row or anisotropy change (ISetAnisotropy
        // sets sampler state only).
        uint32_t        next[kMaxRenderTargetNext];
        plRenderTarget* blurScratch[kMaxRenderTargetNext];
        plRenderTarget* blurDest[kMaxRenderTargetNext];
        std::copy(std::begin(fRenderTargetNext), std::end(fRenderTargetNext), next);
        std::copy(std::begin(fBlurScratchRTs), std::end(fBlurScratchRTs), blurScratch);
        std::copy(std::begin(fBlurDestRTs), std::end(fBlurDestRTs), blurDest);
        std::fill(std::begin(fRenderTargetNext), std::end(fRenderTargetNext), 0);
        std::fill(std::begin(fBlurScratchRTs), std::end(fBlurScratchRTs), nullptr);
        std::fill(std::begin(fBlurDestRTs), std::end(fBlurDestRTs), nullptr);
        for (plShadowSlave* slave : fShadows) {
            // What IFindRenderTarget, SetupViewTransform, IPushShadowCastState (the map, fPipeData) and the blur (the
            // debug blur scale, the swapped map) write, restored: the frame's cast computes them again from the same
            // inputs (plShadowSlave::ISetupOrthoViewTransform and ISetupPerspViewTransform, plShadowSlave.cpp:53-216;
            // plPerspDirSlave::SetupViewTransform, plPerspDirSlave.cpp:120-333; plDXPipeline::IRenderShadowCaster,
            // plDXPipeline.cpp:11172-11182).
            const uint32_t        width = slave->fWidth, height = slave->fHeight, flags = slave->fFlags;
            const plViewTransform view = slave->fView;
            const hsVector3       lightDir = slave->fLightDir;
            const hsPoint3        lightPos = slave->fLightPos;
            const hsMatrix44      worldToTexture = slave->fWorldToTexture;
            void* const           pipeData = slave->fPipeData;
            const float           blurScale = slave->fBlurScale;
            IRenderShadowCaster(slave);
            slave->fWidth = width;
            slave->fHeight = height;
            slave->fFlags = flags;
            slave->fView = view;
            slave->fLightDir = lightDir;
            slave->fLightPos = lightPos;
            slave->fWorldToTexture = worldToTexture;
            slave->fPipeData = pipeData;
            slave->fBlurScale = blurScale;
        }
        std::copy(next, next + kMaxRenderTargetNext, fRenderTargetNext);
        std::copy(blurScratch, blurScratch + kMaxRenderTargetNext, fBlurScratchRTs);
        std::copy(blurDest, blurDest + kMaxRenderTargetNext, fBlurDestRTs);
    }
    if (!fClothingOutfits.empty())
        IPreprocessAvatarTextures();
    IPrewarmEnd(saved);
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
        plWGPUPipelineObserver::PrewarmView view;
        const ST::string                    detail = ST::format("{} slaves, {} outfits", fShadows.size(), fClothingOutfits.size());
        view.seat = "targets";
        view.detail = detail.c_str();
        view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
        view.started = view.inFlightAfter - inFlightBefore;
        view.walkMs = emscripten_get_now() - saved.t0;
        observer->OnPrewarm(*this, view);
    }
#endif
}

void plWGPUPipeline::IPrewarmRequests(const std::vector<plRenderRequest*>& preRequests,
                                      const std::vector<plRenderRequest*>& postRequests, plPageTreeMgr* pageMgr)
{
    // This frame's render requests, as plClient::IDraw draws them: the pre list (IProcessPreRenderRequests,
    // plClient.cpp:1942), then the post list (IProcessPostRenderRequests, :1952), each in its order, each through
    // plRenderRequest::Render with plClient's page tree (IProcessRenderRequests, plClient.cpp:2154-2161), without its
    // ack (plRenderRequest::Render, plRenderRequest.cpp:118-136; a dynamic environment map counts its outstanding faces
    // on the ack, plDynamicEnvMap::MsgReceive, plDynamicEnvMap.cpp:285-289): the request's visibility forcing,
    // PushRenderRequest (its target, view, render state, fog and override material), its clear
    // (ClearRenderTarget(GetClearDrawable()): a clear drawable is drawn, as a waveset's bump composite,
    // plWaveSet7::ICreateRenderRequest, plWaveSet7.cpp:2072-2095), its page tree's Render (its own, as a GUI dialog's,
    // plPostEffectMod::ISetupRenderRequest, plPostEffectMod.cpp:87-109, else plClient's, as an environment map face's,
    // plDynamicEnvMap::ISetupRenderRequests, plDynamicEnvMap.cpp:181-214), whose draws reach PrepForRender and
    // RenderSpans' walk branches, and PopRenderRequest. A request is walked once per render target under the walked
    // set's conditions (cleared by the re-walk, IPrewarmFrameStart). Its drawables are walked under its own target and
    // view, never added to the walked set (a GUI dialog's drawables draw cull-front only through their request).
    // The post requests follow the main Render in the frame: a key input their passes do not set themselves comes
    // from a different predecessor here (a named residual, counted by the trace's draw-time creations).
    std::vector<plRenderRequest*> requests;
    for (const std::vector<plRenderRequest*>* list : {&preRequests, &postRequests}) {
        for (plRenderRequest* req : *list) {
            if (req && fPrewarmRequestsWalked.emplace(req, req->GetRenderTarget()).second)
                requests.push_back(req);
        }
    }
    if (requests.empty())
        return;
#ifdef PLASMA_TEST_HOOKS
    const uint32_t inFlightBefore = uint32_t(fDevice.PipelineBuildsPending());
    PrewarmTally   seatTally;
#endif
    PrewarmSaved saved;
    IPrewarmBegin(saved, "requests");
    for (plRenderRequest* req : requests) {
        plPageTreeMgr* mgr = req->GetPageTreeMgr() ? req->GetPageTreeMgr() : pageMgr;
#ifdef PLASMA_TEST_HOOKS
        plWGPUPipelineObserver::PrewarmRequestView view;
        view.request = req;
        view.ownPageMgr = req->GetPageTreeMgr() != nullptr;
        view.pageMgr = mgr;
        const double   t0 = emscripten_get_now();
        const uint32_t inFlight0 = uint32_t(fDevice.PipelineBuildsPending());
        const auto     report = [&]() {
            if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get())
                observer->OnPrewarmRenderRequest(*this, view);
        };
#endif
        // A forcing no Eval would consume (an empty page tree, IBuildSpaceTree bails at plPageTreeMgr.cpp:142, or no
        // vis manager) would outlive the walk: such a request is not walked. No shipped request is one.
        if (!req->GetVisForce().Empty() && (!mgr || mgr->Empty() || !plPageTreeMgr::VisMgrEnabled() || !mgr->GetVisMgr())) {
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000,
                                   "pipeline prewarm: a render request forces visibility its page tree never evaluates: not walked");
#ifdef PLASMA_TEST_HOOKS
            view.skipped = "its visibility forcing would outlive the walk";
            report();
#endif
            continue;
        }
        fPrewarmTally = PrewarmTally();

        // plRenderRequest::Render
        if (!req->GetVisForce().Empty()) {
            plGlobalVisMgr::Instance()->DisableNormal();
            plGlobalVisMgr::Instance()->ForceVisSets(req->GetVisForce(), false);
        }
        PushRenderRequest(req);
#ifdef PLASMA_TEST_HOOKS
        view.walkTargetValid = fDevice.PrewarmTargetValid();
#endif
        ClearRenderTarget(req->GetClearDrawable());
        if (mgr)
            mgr->Render(this);
        PopRenderRequest(req);

#ifdef PLASMA_TEST_HOOKS
        view.spans = fPrewarmTally.spans;
        view.requests = fPrewarmTally.requests;
        view.started = uint32_t(fDevice.PipelineBuildsPending()) - inFlight0;
        view.opacityPasses = fPrewarmTally.opacityPasses;
        view.opacityStarted = fPrewarmTally.opacityStarted;
        view.walkMs = emscripten_get_now() - t0;
        seatTally.spans += fPrewarmTally.spans;
        seatTally.requests += fPrewarmTally.requests;
        seatTally.opacityPasses += fPrewarmTally.opacityPasses;
        seatTally.opacityStarted += fPrewarmTally.opacityStarted;
        report();
#endif
    }
    IPrewarmEnd(saved);
#ifdef PLASMA_TEST_HOOKS
    if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
        plWGPUPipelineObserver::PrewarmView view;
        const ST::string                    detail = ST::format("{} requests", requests.size());
        view.seat = "requests";
        view.detail = detail.c_str();
        view.spans = seatTally.spans;
        view.requests = seatTally.requests;
        view.opacityPasses = seatTally.opacityPasses;
        view.opacityStarted = seatTally.opacityStarted;
        view.inFlightAfter = uint32_t(fDevice.PipelineBuildsPending());
        view.started = view.inFlightAfter - inFlightBefore;
        view.walkMs = emscripten_get_now() - saved.t0;
        observer->OnPrewarm(*this, view);
    }
#endif
}

bool plWGPUPipeline::IPrewarmSpanFormat(plDrawableSpans* drawable, const plIcicle& span, uint8_t& vertexFormat,
                                        int& numTris)
{
    // The span's buffers, for a walk. A room not drawn yet has no device refs (CheckVertexBufferRef runs in
    // PrepForRender): the draw's vertex format is then the one SetupVertexBufferRef will give it. The refs are read
    // only where they exist (GetVertexRef/GetIndexRef would grow the group's ref vectors). False where the draw would
    // not draw (no buffers, IRenderBufferSpan's nil pair, no triangle). A particle span has its triangles only while
    // its system has particles (plParticleSystem::IHandleRenderMsg: ResetParticleSystem, then
    // AssignEmitterToParticleSystem, plDrawableSpans.cpp:3309-3367, at plRenderMsg): it is walked with one triangle,
    // since the count is no key input (plWGPUDevice::IFixedFunctionKey, IShaderKey).
    if (span.fGroupIdx >= drawable->GetNumBufferGroups())
        return false;
    plGBufferGroup* group = drawable->GetBufferGroup(span.fGroupIdx);
    if (!group || span.fVBufferIdx >= group->GetNumVertexBuffers() || span.fIBufferIdx >= group->GetNumIndexBuffers())
        return false;
    if (span.fILength >= 3)
        numTris = int(span.fILength / 3);
    else if (span.fTypeMask & plSpan::kParticleSpan)
        numTris = 1;
    else
        return false;
    if (group->VertexReady(span.fVBufferIdx)) {
        plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)group->GetVertexBufferRef(span.fVBufferIdx);
        if (!vRef->GetBuffer())
            return false;
        vertexFormat = vRef->fFormat;
    } else
        vertexFormat = plWGPUDevice::VertexBufferFormat(group->GetVertexFormat());
    if (group->IndexReady(span.fIBufferIdx) && !((plWGPUIndexBufferRef*)group->GetIndexBufferRef(span.fIBufferIdx))->GetBuffer())
        return false;
    return true;
}

void plWGPUPipeline::IPrewarmDrawable(plDrawableSpans* drawable, const std::vector<int16_t>* visList)
{
    // RenderSpans (plDXPipeline::RenderSpans) for every span (visList null: a room's walk) or for the visible spans in
    // their order (RenderSpans' walk branch: a render request's draws), each its own icicle: the same material,
    // transforms and buffers as a merged run (CanMergeInto merges only spans that share them, plSpanTypes.cpp:226-285),
    // and the triangle count is no key input. The material is RenderSpans' (an override material pushed by a render
    // request replaces the span's). Not called: IEnableLights (no key carries a light), ICheckDynBuffers and the buffer
    // checks (nothing is uploaded), ICountPass (nothing is drawn). Adds to fPrewarmTally.
    hsMatrix44 lastL2W;
    lastL2W.Reset();
    ISetLocalToWorld(lastL2W, lastL2W);
    const std::vector<plSpan*>& spanArray = drawable->GetSpanArray();
    const size_t                count = visList ? visList->size() : spanArray.size();
    for (size_t n = 0; n < count; n++) {
        const size_t i = visList ? size_t((*visList)[n]) : n;
        plSpan*      span = i < spanArray.size() ? spanArray[i] : nullptr;
        if (!span || !(span->fTypeMask & plSpan::kIcicleSpan))
            continue;
        hsGMaterial* material = GetOverrideMaterial() ? GetOverrideMaterial() : drawable->GetMaterial(span->fMaterialIdx);
        if (!material)
            continue;
        // A copy, so the walk changes no span; with no lights (the frame's lists are stale between frames), as a
        // span the frame has not lit.
        plIcicle ice(*static_cast<plIcicle*>(span));
        ice.ClearLights();

        uint8_t vertexFormat;
        int     numTris;
        if (!IPrewarmSpanFormat(drawable, ice, vertexFormat, numTris))
            continue;

        // The trace's pass, as RenderSpans names the span's draw (restored by IPrewarmEnd).
        fRowCtx = RowContext();
        fRowCtx.drawable = drawable;
        fRowCtx.spanIndex = int(i);
        fRowCtx.material = material;

        ISetupTransforms(drawable, ice, lastL2W);

        fPrewarmTally.spans++;
        plWGPUPrewarmTriListFunc prewarm(this, vertexFormat, numTris);
        ILoopOverLayers(prewarm, material, ice);
        fPrewarmTally.requests += prewarm.fRequests;
        fPrewarmTally.started += prewarm.fStarted;
    }
}

#ifdef PLASMA_TEST_HOOKS
void plWGPUPipeline::IPrewarmCensus()
{
    // Every loaded page's loaded plDrawableSpans against the walked set: the coverage of the room
    // seat. A census only; nothing is walked from it. The iterators collect; the counting runs after the iteration.
    plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get();
    if (!observer)
        return;
    class plPrewarmCensus : public plRegistryPageIterator, public plRegistryKeyIterator
    {
    public:
        std::vector<std::pair<plUoid, ST::string>> fDrawables; // a loaded drawable and its page's name
        ST::string                                 fPage;
        bool                                       EatPage(plRegistryPageNode* page) override
        {
            fPage = ST::format("{}_{}", page->GetPageInfo().GetAge(), page->GetPageInfo().GetPage());
            page->IterateKeys(this, plDrawableSpans::Index());
            return true;
        }
        bool EatKey(const plKey& key) override
        {
            if (plDrawableSpans::ConvertNoRef(key->ObjectIsLoaded()))
                fDrawables.emplace_back(key->GetUoid(), fPage);
            return true;
        }
    } census;
    static_cast<plResManager*>(hsgResMgr::ResMgr())->IteratePages(&census);

    uint32_t                                notWalked = 0;
    std::vector<std::pair<ST::string, int>> byPage;
    std::vector<plDrawableSpans*>           named;
    for (const auto& [uoid, page] : census.fDrawables) {
        if (fPrewarmWalked.count(uoid))
            continue;
        notWalked++;
        if (named.size() < 16) {
            if (plDrawableSpans* drawable = plDrawableSpans::ConvertNoRef(IPrewarmLoadedObject(uoid)))
                named.push_back(drawable);
        }
        auto it = std::find_if(byPage.begin(), byPage.end(), [&page = page](const auto& p) { return p.first == page; });
        if (it == byPage.end())
            byPage.emplace_back(page, 1);
        else
            it->second++;
    }
    ST::string_stream pages;
    for (size_t i = 0; i < byPage.size(); i++)
        pages << (i ? ", " : "") << byPage[i].first << " (" << byPage[i].second << ")";
    observer->OnPrewarmCensus(*this, uint32_t(census.fDrawables.size()), notWalked, pages.to_string().c_str());
    for (plDrawableSpans* drawable : named)
        observer->OnPrewarmCensusDrawable(*this, drawable);
}
#endif

bool plWGPUPipeline::SetGamma(float eR, float eG, float eB)
{
    // plDXPipeline::SetGamma(float, float, float). SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &ramp)
    // becomes the device's table, applied at present after the front-buffer copy.
    if (fSettings.fNoGammaCorrect)
        return false;

    uint16_t (&ramp)[3][256] = fGammaRamp;

    ramp[0][0] = ramp[1][0] = ramp[2][0] = 0L;

    plConst(float) kMinE(0.1f);
    if (eR > kMinE)
        eR = 1.f / eR;
    else
        eR = 1.f / kMinE;
    if (eG > kMinE)
        eG = 1.f / eG;
    else
        eG = 1.f / kMinE;
    if (eB > kMinE)
        eB = 1.f / eB;
    else
        eB = 1.f / kMinE;

    int i;
    for (i = 1; i < 256; i++) {
        float orig = float(i) / 255.f;

        float gamm;
        gamm = pow(orig, eR);
        gamm *= float(uint16_t(-1));
        ramp[0][i] = uint16_t(gamm);

        gamm = pow(orig, eG);
        gamm *= float(uint16_t(-1));
        ramp[1][i] = uint16_t(gamm);

        gamm = pow(orig, eB);
        gamm *= float(uint16_t(-1));
        ramp[2][i] = uint16_t(gamm);
    }

    // fD3DDevice->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &ramp)
    fDevice.SetGammaTable(fGammaRamp);
    fGammaRampSet = true;
    fDisplayStats.gammaSets++;
    {
        plChecksum sha(plChecksum::Type::kSHA256, sizeof(fGammaRamp), reinterpret_cast<const uint8_t*>(fGammaRamp));
        plWGPULog("SetGamma({.4f}, {.4f}, {.4f}): ramp r[64] {} r[128] {} r[255] {} sha256 {} (set {})", 1.f / eR,
                  1.f / eG, 1.f / eB, ramp[0][64], ramp[0][128], ramp[0][255], sha.GetAsHexString(),
                  fDisplayStats.gammaSets);
    }

    return true;
}

bool plWGPUPipeline::SetGamma(const uint16_t* const tabR, const uint16_t* const tabG, const uint16_t* const tabB)
{
    // plDXPipeline::SetGamma(tables)
    if (fSettings.fNoGammaCorrect)
        return false;

    memcpy(fGammaRamp[0], tabR, 256 * sizeof(uint16_t));
    memcpy(fGammaRamp[1], tabG, 256 * sizeof(uint16_t));
    memcpy(fGammaRamp[2], tabB, 256 * sizeof(uint16_t));

    // fD3DDevice->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &ramp)
    fDevice.SetGammaTable(fGammaRamp);
    fGammaRampSet = true;
    fDisplayStats.gammaSets++;
    {
        plChecksum sha(plChecksum::Type::kSHA256, sizeof(fGammaRamp), reinterpret_cast<const uint8_t*>(fGammaRamp));
        plWGPULog("SetGamma(tables): ramp r[64] {} r[128] {} r[255] {} sha256 {} (set {})", fGammaRamp[0][64],
                  fGammaRamp[0][128], fGammaRamp[0][255], sha.GetAsHexString(), fDisplayStats.gammaSets);
    }

    return true;
}

bool plWGPUPipeline::CaptureScreen(plMipmap* dest, bool flipVertical, uint16_t desiredWidth, uint16_t desiredHeight)
{
    // plDXPipeline::CaptureScreen. The front buffer is the last presented frame: its
    // GetFrontBufferData is plWGPUDevice::ReadFrontBuffer, a GPU readback the tick suspends on.
    uint32_t y, *destPtr, *srcPtr, width, height, bigWidth, bigHeight;

    width = GetViewTransform().GetViewPortWidth();
    height = GetViewTransform().GetViewPortHeight();

    int left = 0;
    int right = width;
    int top = 0;
    int bottom = height;

    // Every caller captures between frames (plGrabCubeMap ends its frame first); a capture
    // inside one would suspend with the canvas texture held.
    hsAssert(fInSceneDepth == 0, "CaptureScreen inside BeginRender/EndRender is not supported");

    std::vector<uint32_t> front;
    double                waitMs = 0.0;
    if (!fDevice.ReadFrontBuffer(front, bigWidth, bigHeight, waitMs)) {
        // GetFrontBufferData failed
        return false;
    }

    // The rect to lock in the front buffer.
    int lockLeft, lockTop, lockRight, lockBottom;
    if (fIsFullscreen) {
        // A surface of the viewport size, locked at the viewport rect
        lockLeft = GetViewTransform().GetViewPortLeft();
        lockTop = GetViewTransform().GetViewPortTop();
        lockRight = GetViewTransform().GetViewPortRight();
        lockBottom = GetViewTransform().GetViewPortBottom();
    } else {
        // A screen-sized surface, locked at the client rect in screen coordinates,
        // clipped to the screen. The canvas is the client area and the front buffer all of the
        // screen it shows: the client rect is the whole front buffer, nothing is clipped, and
        // left/top/right/bottom keep their viewport values.
        lockLeft = 0;
        lockTop = 0;
        lockRight = int(bigWidth);
        lockBottom = int(bigHeight);
    }
    // LockRect of a rect outside the surface fails.
    if (lockLeft < 0 || lockTop < 0 || lockRight > int(bigWidth) || lockBottom > int(bigHeight) ||
        lockLeft >= lockRight || lockTop >= lockBottom) {
        return false;
    }
    const int lockWidth = lockRight - lockLeft;
    const int lockHeight = lockBottom - lockTop;
    // The rows the copy reads must lie in the locked rect (D3D9 reads past it into the surface).
    right = std::min(right, left + lockWidth);
    bottom = std::min(bottom, lockHeight);

    if (dest->GetWidth() != width || dest->GetHeight() != height ||
        dest->GetPixelSize() != 32) {
        dest->Reset();
        dest->Create(width, height, plMipmap::kARGB32Config, 1);
    }

    const uint32_t blackOpaque = 0xff000000;
    /// Copy over
    for (y = 0; y < uint32_t(top); y++) {
        if (flipVertical)
            destPtr = dest->GetAddr32(0, height - 1 - y);
        else
            destPtr = dest->GetAddr32(0, y);

        uint32_t x;
        for (x = 0; x < width; x++) {
            *destPtr++ = blackOpaque;
        }
    }
    for (y = top; y < uint32_t(bottom); y++) {
        srcPtr = front.data() + size_t(lockTop + y) * bigWidth + lockLeft;
        if (flipVertical)
            destPtr = dest->GetAddr32(0, height - 1 - y);
        else
            destPtr = dest->GetAddr32(0, y);

        int x;
        for (x = 0; x < left; x++)
            *destPtr++ = blackOpaque;

        memcpy(destPtr, srcPtr, (right - left) * sizeof(uint32_t));
        destPtr += (right - left);

        for (x = right; x < int(width); x++)
            *destPtr++ = blackOpaque;
    }
    for (y = bottom; y < height; y++) {
        if (flipVertical)
            destPtr = dest->GetAddr32(0, height - 1 - y);
        else
            destPtr = dest->GetAddr32(0, y);

        uint32_t x;
        for (x = 0; x < width; x++) {
            *destPtr++ = blackOpaque;
        }
    }

    if (desiredWidth != 0 && desiredHeight != 0) {
        // Rescale to the right size
        dest->ResizeNicely(desiredWidth, desiredHeight, plMipmap::kDefaultFilter);
    }

    return true;
}

plMipmap* plWGPUPipeline::ExtractMipMap(plRenderTarget* targ)
{
    // plDXPipeline::ExtractMipMap. LockRect becomes a copy to a map-read buffer, submitted now
    // (before plCaptureRenderRequest::Render deletes the target); the rows are filled at
    // CompletePendingCaptures, before plCaptureRender::Update sends the mipmap (plClient's
    // update phase): until then it is reachable only through the stashed message.
    if (plCubicRenderTarget::ConvertNoRef(targ))
        return nullptr;

    if (targ->GetPixelSize() != 32) {
        hsAssert(false, "Only RGBA8888 currently implemented");
        return nullptr;
    }

    plWGPURenderTargetRef* ref = (plWGPURenderTargetRef*)targ->GetDeviceRef();
    if (!ref)
        return nullptr;

    WGPUTexture surf = ref->fTexture;
    if (!surf)
        return nullptr;

    const int width = targ->GetWidth();
    const int height = targ->GetHeight();

    WGPUFuture                              future;
    std::shared_ptr<plWGPUDevice::Readback> rb = fDevice.RequestCaptureReadback(surf, width, height, future);
    if (!rb)
        return nullptr; // LockRect failed

    plMipmap* mipMap = new plMipmap(width, height, plMipmap::kARGB32Config, 1);

    PendingCapture pending;
    pending.mipmap = mipMap;
    pending.readback = std::move(rb);
    pending.future = future;
    fPendingCaptures.push_back(std::move(pending));

    return mipMap;
}

void plWGPUPipeline::SetGPUWaitProcs(bool (*begin)(const char* how), void (*end)())
{
    hsAssert((begin == nullptr) == (end == nullptr), "GPU wait procs come in pairs");
    plWGPUDevice::fGPUWaitBegin = begin;
    plWGPUDevice::fGPUWaitEnd = end;
}

void plWGPUPipeline::CompletePendingCaptures()
{
    if (fPendingCaptures.empty())
        return;

    std::vector<PendingCapture> pending;
    pending.swap(fPendingCaptures);

    double     waitMs = 0.0;
    const bool waiting = fDevice.BeginGPUWait("plCaptureRender's ExtractMipMap readbacks");
    for (PendingCapture& p : pending) {
        plWGPUDevice::Readback& rb = *p.readback;
        if (!rb.done && waiting)
            fDevice.WaitFuture(p.future, waitMs, "ExtractMipMap");

        const size_t rowBytes = size_t(p.mipmap->GetWidth()) * 4;
        if (!rb.done || rb.failed || rb.data.size() < rowBytes * p.mipmap->GetHeight()) {
            // This differs from DX on failure only: DX returns before NewKey and the index
            // increment (plCaptureRender::IProcess); here the key exists, so the capture is taken back.
            plWGPULog("ExtractMipMap readback failed ({}); {} dropped, CaptureRender_N advanced",
                      !rb.done ? "not done" : "map failed", p.mipmap->GetKey() ? p.mipmap->GetKeyName() : ST_LITERAL("(no key)"));
            plCaptureRender::DropFailedCapture(p.mipmap);
        } else {
            // As plDXPipeline::ExtractMipMap: alpha forced opaque
            const uint32_t blackOpaque = 0xff000000;
            const uint8_t* ptr = rb.data.data();
            for (uint32_t y = 0; y < p.mipmap->GetHeight(); y++) {
                uint32_t*       destPtr = p.mipmap->GetAddr32(0, y);
                const uint32_t* srcPtr = reinterpret_cast<const uint32_t*>(ptr);
                for (uint32_t x = 0; x < p.mipmap->GetWidth(); x++)
                    destPtr[x] = srcPtr[x] | blackOpaque;
                ptr += rowBytes;
            }
        }
    }
    // The readbacks and their buffers go with the last reference (plWGPUDevice::Readback): here, or at the
    // end of a map still in flight (a wait that was refused).
    if (waiting)
        fDevice.EndGPUWait();
}

void plWGPUPipeline::GetSupportedDisplayModes(std::vector<plDisplayMode>* res, int ColorDepth)
{
    // plMetalPipeline::GetSupportedDisplayModes
    *res = plDisplayHelper::GetInstance()->GetSupportedDisplayModes(plDisplayHelper::GetInstance()->DefaultDisplay());
}

int plWGPUPipeline::GetMaxAnisotropicSamples()
{
    // WebGPU samplers take maxAnisotropy up to 16 (as plMetalPipeline::GetMaxAnisotropicSamples).
    return 16;
}

int plWGPUPipeline::GetMaxAntiAlias(int Width, int Height, int ColorDepth)
{
    // WebGPU render attachments take a sample count of 1 or 4 (plMetalPipeline::GetMaxAntiAlias).
    return 4;
}

void plWGPUPipeline::ResetDisplayDevice(int Width, int Height, int ColorDepth, bool Windowed, int NumAASamples, int MaxAnisotropicSamples, bool VSync)
{
    // plDXPipeline::ResetDisplayDevice
    if (fPresentWidth == uint32_t(Width) &&
        fPresentHeight == uint32_t(Height) &&
        (fPresentWindowed ? 1 : fColorDepth == uint32_t(ColorDepth)) && // if we're windowed dont check color depth we just use the desktop colordepth
        ((fPresentWindowed && Windowed) || (!fPresentWindowed && !Windowed)) &&
        fNumAASamples == NumAASamples &&
        fSettings.fMaxAnisotropicSamples == MaxAnisotropicSamples &&
        fVSync == VSync) {
        return; // nothing has changed
    }
    const double t0 = emscripten_get_now();

    fVSync = VSync;
    size_t                     iMode = 0;
    // fCurrentRenderer->fModes: the display helper's modes (GetSupportedDisplayModes)
    std::vector<plDisplayMode> modes;
    GetSupportedDisplayModes(&modes, ColorDepth);
    // check for supported resolution if we're not going to windowed mode
    if (!Windowed) {
        for (iMode = 0; iMode < modes.size(); iMode++) {
            const plDisplayMode& mode = modes[iMode];
            if (mode.Width == Width &&
                mode.Height == Height &&
                mode.ColorDepth == ColorDepth) {
                break;
            }
        }
    }
    if (iMode != modes.size()) {
        // Set Resolution
        fOrigWidth = Width;
        fOrigHeight = Height;
        IGetViewTransform().SetScreenSize(Width, Height);
        fPresentWidth = Width;
        fPresentHeight = Height;
        fColorDepth = ColorDepth;
    }

    // set windowed/fullscreen mode
    fPresentWindowed = true;
    fIsFullscreen = !Windowed;

    // set Antialiasing
    fNumAASamples = 0;
    // Attempt to find the closest AA setting we can: fFSAATypes are the sample counts a
    // WebGPU render attachment takes above 1 (GetMaxAntiAlias), the mode's list (plWGPUEnumerate)
    for (int i = NumAASamples; i >= 2; i--) {
        if (std::any_of(fDevice.fFSAATypes.cbegin(), fDevice.fFSAATypes.cend(), [i](uint8_t type) { return type == i; })) {
            fNumAASamples = i;
            break;
        }
    }
    if (fNumAASamples > 0)
        fSettings.fD3DCaps |= kCapsFSAntiAlias;
    else
        fSettings.fD3DCaps &= ~kCapsFSAntiAlias;

    // Set Anisotropic filtering
    fSettings.fMaxAnisotropicSamples = MaxAnisotropicSamples;
    ISetAnisotropy(MaxAnisotropicSamples > 0);
    // PresentationInterval: D3DPRESENT_INTERVAL_DEFAULT when windowed, else by fVSync; the
    // canvas presents with the display (WGPUPresentMode_Fifo) either way.

    // Force a device reset
    fDeviceLost = true;
    fForceDeviceReset = true;

    plVirtualCam1::Refresh();
    IResetDevice();
    plWGPULog("ResetDisplayDevice {}x{}x{} windowed {} AA {} (-> {}) aniso {} vsync {}: {}x{}, {.1f} ms",
              Width, Height, ColorDepth, Windowed ? 1 : 0, NumAASamples, fNumAASamples, MaxAnisotropicSamples,
              VSync ? 1 : 0, fPresentWidth, fPresentHeight, emscripten_get_now() - t0);
}

bool plWGPUPipeline::ICheckDynBuffers(plDrawableSpans* drawable, plGBufferGroup* group, const plSpan* spanBase)
{
    // plDXPipeline::ICheckDynBuffers
    if (!(spanBase->fTypeMask & plSpan::kVertexSpan))
        return false;
    // If we arent' an trilist, we're toast.
    if (!(spanBase->fTypeMask & plSpan::kIcicleSpan))
        return false;

    plIcicle* span = (plIcicle*)spanBase;

    plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)group->GetVertexBufferRef(span->fVBufferIdx);
    if (!vRef)
        return true;

    plWGPUIndexBufferRef* iRef = (plWGPUIndexBufferRef*)group->GetIndexBufferRef(span->fIBufferIdx);
    if (!iRef)
        return true;

    // If our vertex buffer ref is volatile and the timestamp is off
    // then it needs to be refilled
    if (vRef->Expired(fVtxRefTime)) {
        IRefreshDynVertices(group, vRef);
    }
    if (vRef->fOffset != iRef->fOffset) {
        iRef->fOffset = vRef->fOffset;

        iRef->SetRebuiltSinceUsed(true);
    }
    if (iRef->IsDirty()) {
        fDevice.FillIndexBufferRef(iRef, group, span->fIBufferIdx);
        iRef->SetRebuiltSinceUsed(true);
    }

    return false; // No error
}

bool plWGPUPipeline::IRefreshDynVertices(plGBufferGroup* group, plWGPUVertexBufferRef* vRef)
{
    // plMetalPipeline::IRefreshDynVertices. The pool gives each pass of a frame its own buffer;
    // queue.writeBuffer takes the data (in whole four-byte words).
    ptrdiff_t size = (group->GetVertBufferEnd(vRef->fIndex) - group->GetVertBufferStart(vRef->fIndex)) * vRef->fVertexSize;
    if (!size)
        return false; // No error, just nothing to do.

    hsAssert(size > 0, "Bad start and end counts in a group");

    uint8_t* vData;
    if (vRef->fData)
        vData = vRef->fData;
    else
        vData = group->GetVertBufferData(vRef->fIndex) + group->GetVertBufferStart(vRef->fIndex) * vRef->fVertexSize;

    vRef->PrepareForWrite();

    const uint64_t writeSize = (uint64_t(size) + 3) & ~uint64_t(3);
    WGPUBuffer     vertexBuffer = vRef->GetBuffer();
    if (!vertexBuffer || vRef->GetBufferSize() < writeSize) {
        // Plasma will present different length buffers at different times
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{"plWGPU dynamic vertices", WGPU_STRLEN};
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        desc.size = writeSize;
        vertexBuffer = wgpuDeviceCreateBuffer(fDevice.fWGPUDevice, &desc);
        vRef->SetBuffer(vertexBuffer, writeSize);
    }
    if (writeSize == uint64_t(size)) {
        wgpuQueueWriteBuffer(fDevice.fQueue, vertexBuffer, 0, vData, size);
    } else {
        std::vector<uint8_t> padded(writeSize, 0);
        memcpy(padded.data(), vData, size);
        wgpuQueueWriteBuffer(fDevice.fQueue, vertexBuffer, 0, padded.data(), writeSize);
    }
#ifdef PLASMA_TEST_HOOKS
    // The test harness's upload record: DX's per-frame dynamic refill (plDXPipeline::IRefreshDynVertices).
    fDevice.NoteUpload(plWGPUDevice::kUploadVolatileVB, writeSize, fDevice.UploadDrawableOwner(group, vRef->fIndex, "vb"),
                       vRef->fData ? "skinned" : "volatile");
#endif

    // The draws' base vertex: the refreshed range starts at offset 0 of its buffer, where
    // D3D9's starts at newStart of the shared dynamic buffer (plDXPipeline::IRefreshDynVertices).
    vRef->fOffset = -int32_t(group->GetVertBufferStart(vRef->fIndex));

    vRef->fRefTime = fVtxRefTime;
    vRef->SetDirty(false);

    return false;
}

void plWGPUPipeline::IDrawPlate(plPlate* plate)
{
    // plDXPipeline::IDrawPlate: every pass of the plate's material through IHandleMaterial,
    // with the plate projection and CW culling per pass (plMetalPipeline::IDrawPlate draws the
    // first layer only).
    int          i;
    hsGMaterial* material = plate->GetMaterial();
    D3DMATRIX    mat;

    /// Set up the D3D transform directly
    IMatrix44ToD3DMatrix(mat, plate->GetTransform());
    fD3DDevice->SetTransform(D3DTS_WORLD, &mat);
    mat = d3dIdentityMatrix;
    mat.m[1][1] = -1.0f;
    mat.m[2][2] = 2.0f;
    mat.m[2][3] = 1.0f;
    mat.m[3][2] = -2.0f;
    mat.m[3][3] = 0.0f;

    IPushPiggyBacks(material);

    plWGPUPlateManager* pm = (plWGPUPlateManager*)fPlateMgr;

    /// Draw the vertex buffer once for each material pass
    for (i = 0; i < material->GetNumLayers();) {
        // As DX: an aborted pass (-1) still draws once with the current state, then -1 compared
        // with the unsigned layer count ends the loop (plDXPipeline::IDrawPlate).
        i = IHandleMaterial(material, i, nullptr);
        ISetShaders(nullptr, nullptr);

        // To override the transform done by the z-bias
        fD3DDevice->SetTransform(D3DTS_PROJECTION, &mat);
        // And this to override cullmode set based on material 2-sidedness.
        fD3DDevice->SetRenderState(D3DRS_CULLMODE, fDevice.fCurrCullMode = D3DCULL_CW);

        ICountPass();

        plWGPUDevice::FixedFunctionDraw draw;
        draw.vertexBuffer = pm->GetVertexBuffer();
        draw.vertexFormat = 0;
        draw.plateVertex = true;
        draw.indexBuffer = nullptr;
        draw.baseVertex = 0;
        draw.start = 0;
        draw.count = 4;
        draw.topology = WGPUPrimitiveTopology_TriangleStrip;
        const RowContext saved = fRowCtx;
        fRowCtx = RowContext();
        fRowCtx.kind = kRowPlate;
        fRowCtx.material = material;
        fRowCtx.layer = fCurrLayerIdx;
        IDrawFixedFunction(draw);
        fRowCtx = saved;
    }

    IPopPiggyBacks();
}

void plWGPUPipeline::IClearShadowSlaves()
{
    // plMetalPipeline::IClearShadowSlaves
    for (size_t i = 0; i < fShadows.size(); i++) {
        const plShadowCaster* caster = fShadows[i]->fCaster;
        caster->GetKey()->UnRefObject();
    }
    fShadows.clear();
}

// Create all our video memory consuming objects.
bool plWGPUPipeline::ICreateDynDeviceObjects()
{
    // plDXPipeline::ICreateDynDeviceObjects (structure: plMetalPipeline::ICreateDynDeviceObjects)
    // RenderTarget pools are shared for our shadow generation algorithm.
    // Different sizes for different resolutions.
    IMakeRenderTargetPools();

    // Create device-specific stuff. DX's failed-Reset path runs this twice (ICreateDeviceObjects
    // and IResetDevice); the first manager is released before the second.
    delete fDebugTextMgr;
    fDebugTextMgr = new plDebugTextManager();
    if (fDebugTextMgr == nullptr)
        return true;

    // Vertex buffers, index buffers, textures, etc.
    LoadResources();

    return false;
}

void plWGPUPipeline::IReleaseDynDeviceObjects()
{
    // plMetalPipeline::IReleaseDynDeviceObjects
    // We should do this earlier, but the textFont objects don't remove
    // themselves from their parent objects yet
    delete fDebugTextMgr;
    fDebugTextMgr = nullptr;

    while (fTextFontRefList)
        delete fTextFontRefList;

    while (fRenderTargetRefList) {
        plWGPURenderTargetRef* rtRef = fRenderTargetRefList;
        rtRef->Release();
        rtRef->Unlink();
    }

    // The shared dynamic vertex buffers used by things like objects skinned on CPU, or
    // particle systems (plDXPipeline::IReleaseDynDeviceObjects).
    IReleaseDynamicBuffers();
    IReleaseAvRTPool();
    IReleaseRenderTargetPools();
}

void plWGPUPipeline::IReleaseDynamicBuffers()
{
    // PlateMgr has a POOL_DEFAULT vertex buffer for drawing quads.
    if (plWGPUPlateManager* pm = static_cast<plWGPUPlateManager*>(fPlateMgr))
        pm->IReleaseGeometry();

    // plDXPipeline::IReleaseDynamicBuffers
    IReleaseBlurVBuffers();
}

//// Ref checks (plMetalPipeline::CheckTextureRef and the buffer checks) ///////////

void plWGPUPipeline::CheckTextureRef(plLayerInterface* layer)
{
    plBitmap* bitmap = layer->GetTexture();

    if (bitmap) {
        CheckTextureRef(bitmap);
    }
}

void plWGPUPipeline::CheckTextureRef(plBitmap* bitmap)
{
    plWGPUTextureRef* tRef = static_cast<plWGPUTextureRef*>(bitmap->GetDeviceRef());

    if (!tRef) {
        tRef = static_cast<plWGPUTextureRef*>(MakeTextureRef(bitmap));
    }

    // If it's dirty, refill it.
    if (tRef->IsDirty()) {
        IReloadTexture(bitmap, tRef);
    }
}

hsGDeviceRef* plWGPUPipeline::MakeTextureRef(plBitmap* bitmap)
{
    plWGPUTextureRef* tRef = static_cast<plWGPUTextureRef*>(bitmap->GetDeviceRef());

    if (!tRef) {
        tRef = new plWGPUTextureRef();

        fDevice.SetupTextureRef(bitmap, tRef);
    }

    if (!tRef->IsLinked()) {
        tRef->Link(&fTextureRefList);
    }

    // Make sure it has all resources created.
    fDevice.CheckTexture(tRef);

    // If it's dirty, refill it.
    if (tRef->IsDirty()) {
        IReloadTexture(bitmap, tRef);
    }
    return tRef;
}

void plWGPUPipeline::IReloadTexture(plBitmap* bitmap, plWGPUTextureRef* ref)
{
#ifdef PLASMA_TEST_HOOKS
    // The test harness's upload record records the reason of this upload: the bitmap's first, a
    // dirty ref (DX re-creates dirty refs only, plDXPipeline::IHandleTextureStage and
    // IDrawClothingQuad), a ref without a texture (DX reloads those too, IUseTextureRef and
    // IDrawClothingQuad), or none of these (a bug in this pipeline).
    const uint32_t uploads = fDevice.fFilledTextures[bitmap]++;
    fDevice.fUploadTextureReason = uploads == 0 ? "first" : ref->IsDirty() ? "dirty"
                                                        : !ref->fTexture   ? "no-texture"
                                                                           : "not-dirty";
#endif
    if (plMipmap* mip = plMipmap::ConvertNoRef(bitmap))
        fDevice.MakeTextureRef(ref, mip);
    else if (plCubicEnvironmap* cubic = plCubicEnvironmap::ConvertNoRef(bitmap))
        fDevice.MakeCubicTextureRef(ref, cubic);
#ifdef PLASMA_TEST_HOOKS
    fDevice.fUploadTextureReason = nullptr;
#endif
}

// CheckVertexBufferRef /////////////////////////////////////////////////////
// Make sure the buffer group has a valid buffer ref and that it is up to date.
void plWGPUPipeline::CheckVertexBufferRef(plGBufferGroup* owner, uint32_t idx)
{
    // First, do we have a device ref at this index?
    plWGPUVertexBufferRef* vRef = static_cast<plWGPUVertexBufferRef*>(owner->GetVertexBufferRef(idx));

    // If not
    if (!vRef) {
        // Make the blank ref
        vRef = new plWGPUVertexBufferRef();

        fDevice.SetupVertexBufferRef(owner, idx, vRef);
    }

    if (!vRef->IsLinked()) {
        vRef->Link(&fVtxBuffRefList);
    }

    // One way or another, we now have a vbufferref[idx] in owner.
    // Now, does it need to be (re)filled?
    // If the owner is volatile, then we hold off. It might not
    // be visible, and we might need to refill it again if we
    // have an overrun of our dynamic buffer.
    if (!vRef->Volatile()) {
        // If it's a static buffer, allocate a vertex buffer for it.
        fDevice.CheckStaticVertexBuffer(vRef, owner, idx);

        // Might want to remove this assert, and replace it with a dirty check
        // if we have static buffers that change very seldom rather than never.
        hsAssert(!vRef->IsDirty(), "Non-volatile vertex buffers should never get dirty");
    } else {
        // Make sure we're going to be ready to fill it.
        if (!vRef->fData && (vRef->fFormat != owner->GetVertexFormat())) {
            vRef->fData = new uint8_t[vRef->fCount * vRef->fVertexSize];
            fDevice.FillVolatileVertexBufferRef(vRef, owner, idx);
        }
    }
}

// CheckIndexBufferRef /////////////////////////////////////////////////////
// Make sure the buffer group has an index buffer ref and that its data is current.
void plWGPUPipeline::CheckIndexBufferRef(plGBufferGroup* owner, uint32_t idx)
{
    plWGPUIndexBufferRef* iRef = static_cast<plWGPUIndexBufferRef*>(owner->GetIndexBufferRef(idx));

    if (!iRef) {
        // Create one from scratch.
        iRef = new plWGPUIndexBufferRef();

        fDevice.SetupIndexBufferRef(owner, idx, iRef);
    }

    if (!iRef->IsLinked()) {
        iRef->Link(&fIdxBuffRefList);
    }

    // Make sure it has all resources created.
    fDevice.CheckIndexBuffer(iRef);

    // If it's dirty, refill it.
    if (iRef->IsDirty()) {
        fDevice.FillIndexBufferRef(iRef, owner, idx);
    }
}

//// Span rendering //////////////////////////////////////////////////////////////
// plDXPipeline's span path (PreRender, PrepForRender, RenderSpans, IRenderBufferSpan,
// ILoopOverLayers, the aux spans) with the WebGPU buffers of Metal's structure: a span draw is
// plWGPURenderTriListFunc (plWGPUPipeline_Private.h), which hands the emulated D3D9 state to
// plWGPUDevice::DrawFixedFunction.

bool plWGPUPipeline::PreRender(plDrawable* drawable, std::vector<int16_t>& visList, plVisMgr* visMgr)
{
    // plDXPipeline::PreRender (the bounds and normals debug spans are not ported)
    plDrawableSpans* ds = plDrawableSpans::ConvertNoRef(drawable);
    if (!ds)
        return false;
    if ((ds->GetType() & fView.GetDrawableTypeMask()) == 0)
        return false;

    fView.GetVisibleSpans(ds, visList, visMgr);

    return !visList.empty();
}

struct plWGPUSortFace
{
    uint16_t fIdx[3];
    float    fDist;
};

struct plWGPUCompSortFace
{
    bool operator()(const plWGPUSortFace& lhs, const plWGPUSortFace& rhs) const
    {
        return lhs.fDist > rhs.fDist;
    }
};

bool plWGPUPipeline::IAvatarSort(plDrawableSpans* d, const std::vector<int16_t>& visList)
{
    // plDXPipeline::IAvatarSort, the TRICENTER sort DX compiles in.
    for (int16_t visIdx : visList) {
        hsAssert(d->GetSpan(visIdx)->fTypeMask & plSpan::kIcicleSpan, "Unknown type for sorting faces");

        plIcicle* span = (plIcicle*)d->GetSpan(visIdx);

        if (span->fProps & plSpan::kPartialSort) {
            hsAssert(d->GetBufferGroup(span->fGroupIdx)->AreIdxVolatile(), "Badly setup buffer group - set PartialSort too late?");

            const hsPoint3 viewPos = GetViewPositionWorld();

            plGBufferGroup* group = d->GetBufferGroup(span->fGroupIdx);

            plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)group->GetVertexBufferRef(span->fVBufferIdx);

            const uint8_t* vdata = vRef->fData;
            const uint32_t stride = vRef->fVertexSize;

            const int numTris = span->fILength / 3;

            static std::vector<plWGPUSortFace> sortScratch;
            sortScratch.resize(numTris);

            uint16_t* indices = group->GetIndexBufferData(span->fIBufferIdx) + span->fIStartIdx;
            int       j;
            for (j = 0; j < numTris; j++) {
                uint16_t idx = *indices++;
                sortScratch[j].fIdx[0] = idx;
                hsPoint3 pos = *(hsPoint3*)(vdata + idx * stride);

                idx = *indices++;
                sortScratch[j].fIdx[1] = idx;
                pos += *(hsPoint3*)(vdata + idx * stride);

                idx = *indices++;
                sortScratch[j].fIdx[2] = idx;
                pos += *(hsPoint3*)(vdata + idx * stride);

                pos *= 0.3333f;

                sortScratch[j].fDist = hsVector3(&pos, &viewPos).MagnitudeSquared();
            }

            std::sort(sortScratch.begin(), sortScratch.end(), plWGPUCompSortFace());

            indices = group->GetIndexBufferData(span->fIBufferIdx) + span->fIStartIdx;
            for (const plWGPUSortFace& iter : sortScratch) {
                *indices++ = iter.fIdx[0];
                *indices++ = iter.fIdx[1];
                *indices++ = iter.fIdx[2];
            }

            group->DirtyIndexBuffer(span->fIBufferIdx);
        }
    }
    return true;
}

bool plWGPUPipeline::PrepForRender(plDrawable* d, std::vector<int16_t>& visList, plVisMgr* visMgr)
{
    // plDXPipeline::PrepForRender
    plDrawableSpans* drawable = plDrawableSpans::ConvertNoRef(d);
    if (!drawable)
        return false;

    // The prewarm's walk of a render request (IPrewarmRequests) changes no span or buffer: every step below writes
    // them (the spans' light lists, the index order, the particle and dynamic buffers, the skinned vertices).
    if (fPrewarmWalk)
        return true;

    // Find our lights
    ICheckLighting(drawable, visList, visMgr);

    // Sort our faces
    if (drawable->GetNativeProperty(plDrawable::kPropSortFaces))
        drawable->SortVisibleSpans(visList, this);

    // Prep for render. This is gives the drawable a chance to
    // do any last minute updates for its buffers, including
    // generating particle tri lists.
    drawable->PrepForRender(this);

    // Any skinning necessary
    if (!ISoftwareVertexBlend(drawable, visList))
        return false;

    // Avatar face sorting happens after the software skin.
    if (drawable->GetNativeProperty(plDrawable::kPropPartialSort))
        IAvatarSort(drawable, visList);

    return true;
}

void plWGPUPipeline::ISetupTransforms(plDrawableSpans* drawable, const plSpan& span, hsMatrix44& lastL2W)
{
    // plDXPipeline::ISetupTransforms
    if (span.fNumMatrices) {
        if (span.fNumMatrices <= 2) {
            ISetLocalToWorld(span.fLocalToWorld, span.fWorldToLocal);
            lastL2W = span.fLocalToWorld;
        } else {
            lastL2W.Reset();
            ISetLocalToWorld(lastL2W, lastL2W);
            fView.fLocalToWorldLeftHanded = span.fLocalToWorld.GetParity();
        }
    } else if (lastL2W != span.fLocalToWorld) {
        ISetLocalToWorld(span.fLocalToWorld, span.fWorldToLocal);
        lastL2W = span.fLocalToWorld;
    } else {
        fView.fLocalToWorldLeftHanded = lastL2W.GetParity();
    }

    if (span.fNumMatrices == 2) {
        D3DMATRIX mat;
        IMatrix44ToD3DMatrix(mat, drawable->GetPaletteMatrix(span.fBaseMatrix + 1));
        fD3DDevice->SetTransform(D3DTS_WORLDMATRIX(1), &mat);
        fD3DDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
    } else {
        fD3DDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    }
}

bool plWGPUPipeline::ICheckAuxBuffers(const plAuxSpan* span)
{
    // plDXPipeline::ICheckAuxBuffers
    plGBufferGroup* group = span->fGroup;

    plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)group->GetVertexBufferRef(span->fVBufferIdx);
    if (!vRef)
        return true;

    plWGPUIndexBufferRef* iRef = (plWGPUIndexBufferRef*)group->GetIndexBufferRef(span->fIBufferIdx);
    if (!iRef)
        return true;

    // If our vertex buffer ref is volatile and the timestamp is off
    // then it needs to be refilled
    if (vRef->Expired(fVtxRefTime)) {
        IRefreshDynVertices(group, vRef);
    }
    if (vRef->fOffset != iRef->fOffset) {
        iRef->fOffset = vRef->fOffset;

        iRef->SetRebuiltSinceUsed(true);
    }

    return false; // No error
}

void plWGPUPipeline::RenderSpans(plDrawableSpans* drawable, const std::vector<int16_t>& visList)
{
    // plDXPipeline::RenderSpans
    // The prewarm's walk of a render request (IPrewarmRequests): the visible spans go through this draw's material
    // code with a render functor that draws nothing, and their pipelines are started (IPrewarmDrawable).
    if (fPrewarmWalk) {
        IPrewarmDrawable(drawable, &visList);
        return;
    }

    hsMatrix44   lastL2W;
    hsGMaterial* material;

    const std::vector<plSpan*>& spans = drawable->GetSpanArray();

    /// Set this (*before* we do our TestVisibleWorld stuff...)
    lastL2W.Reset();
    ISetLocalToWorld(lastL2W, lastL2W); // This is necessary; otherwise, we have to test for
                                        // the first transform set, since this'll be identity
                                        // but the actual device transform won't be (unless
                                        // we do this)

    /// Loop through our spans, combining them when possible
    for (size_t i = 0; i < visList.size();) {
        material = GetOverrideMaterial() ? GetOverrideMaterial() : drawable->GetMaterial(spans[visList[i]]->fMaterialIdx);

        /// It's an icicle--do our icicle merge loop
        plIcicle tempIce(*((plIcicle*)spans[visList[i]]));

        // Start at i + 1, look for as many spans as we can add to tempIce
        size_t j;
        for (j = i + 1; j < visList.size(); j++) {
            if (GetOverrideMaterial())
                tempIce.fMaterialIdx = spans[visList[j]]->fMaterialIdx;

            if (!spans[visList[j]]->CanMergeInto(&tempIce))
                break;

            spans[visList[j]]->MergeInto(&tempIce);
        }

        if (material != nullptr) {
            // What do we change?
            ISetupTransforms(drawable, tempIce, lastL2W);

            // Turn on this spans lights and turn off the rest.
            IEnableLights(&tempIce);

            // Check that the underlying buffers are ready to go.
#ifdef PLASMA_TEST_HOOKS
            fDevice.fUploadDrawable = drawable;
            fDevice.fUploadGroup = tempIce.fGroupIdx;
            fDevice.NoteGroupOwner(drawable->GetBufferGroup(tempIce.fGroupIdx));
#endif
            ICheckDynBuffers(drawable, drawable->GetBufferGroup(tempIce.fGroupIdx), &tempIce);

            CheckVertexBufferRef(drawable->GetBufferGroup(tempIce.fGroupIdx), tempIce.fVBufferIdx);
            CheckIndexBufferRef(drawable->GetBufferGroup(tempIce.fGroupIdx), tempIce.fIBufferIdx);
#ifdef PLASMA_TEST_HOOKS
            fDevice.fUploadDrawable = nullptr; // the upload record's owner context ends here
#endif

            fRowCtx = RowContext();
            fRowCtx.kind = kRowBase;
            fRowCtx.drawable = drawable;
            fRowCtx.span = &tempIce;
            fRowCtx.spanIndex = int(visList[i]);
            fRowCtx.material = material;
            fRowCtx.group = drawable->GetBufferGroup(tempIce.fGroupIdx);
            fRowCtx.vbIdx = tempIce.fVBufferIdx;
            fRowCtx.ibIdx = tempIce.fIBufferIdx;

            // Draw this span now
            IRenderBufferSpan(tempIce,
                              drawable->GetVertexRef(tempIce.fGroupIdx, tempIce.fVBufferIdx),
                              drawable->GetIndexRef(tempIce.fGroupIdx, tempIce.fIBufferIdx),
                              material,
                              tempIce.fVStartIdx, tempIce.fVLength, // These are used as our accumulated range
                              tempIce.fIPackedIdx, tempIce.fILength);
        }

        // Restart our search...
        i = j;
    }
}

void plWGPUPipeline::IRenderBufferSpan(const plIcicle& span, hsGDeviceRef* vb, hsGDeviceRef* ib,
                                       hsGMaterial* material, uint32_t vStart, uint32_t vLength,
                                       uint32_t iStart, uint32_t iLength)
{
    // plDXPipeline::IRenderBufferSpan. WebGPU binds the buffers with each draw, so the stream
    // source and index caching (fSettings.fCurrVertexBuffRef/fCurrIndexBuffRef) is not kept.
    plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)vb;
    plWGPUIndexBufferRef*  iRef = (plWGPUIndexBufferRef*)ib;

    if (vRef->GetBuffer() == nullptr || iRef->GetBuffer() == nullptr) {
        hsAssert(false, "Trying to render a nil buffer pair!");
        return;
    }

    vRef->SetRebuiltSinceUsed(false);
    iRef->SetRebuiltSinceUsed(false);

    plWGPURenderTriListFunc render(this, vRef, iRef, iRef->fOffset, vStart, vLength, iStart, iLength / 3);

    ILoopOverLayers(render, material, span);
}

const plLayerAnimation* plWGPUPipeline::IOpacityAnimation(plLayerInterface* lay, const plLayerAnimation** firstAnim)
{
    // The layer animation in the layer's chain (plLayerInterface::GetAttached) whose opacity controller drives the
    // layer's opacity (plLayerAnimationBase::GetOpacityCtl, evaluated by
    // plLayerAnimationBase::IEvalConvertedTime, plLayerAnimation.cpp:184-188), or null; firstAnim receives the chain's first layer animation.
    const plLayerAnimation* opacityAnim = nullptr;
    if (firstAnim)
        *firstAnim = nullptr;
    for (plLayerInterface* li = lay; li && !opacityAnim; li = li->GetAttached()) {
        const plLayerAnimation* anim = plLayerAnimation::ConvertNoRef(li);
        if (!anim)
            continue;
        if (firstAnim && !*firstAnim)
            *firstAnim = anim;
        if (anim->GetOpacityCtl())
            opacityAnim = anim;
    }
    return opacityAnim;
}

bool plWGPUPipeline::ILoopOverLayers(const plWGPURenderPrimFunc& render, hsGMaterial* material, const plSpan& span)
{
    // plDXPipeline::ILoopOverLayers (the kFlagNoRender and kFlagOverlayWire debug paths are
    // not ported)
    if (GetOverrideMaterial())
        material = GetOverrideMaterial();

    IPushPiggyBacks(material);

    bool normalLightsDisabled = false;

    // Loop across all the layers we need to draw
    int j;
    int passIndex = -1; // the material pass this is (the per-draw rows' `pass`)
    for (j = 0; j < material->GetNumLayers();) {
        int iCurrMat = j;
        j = IHandleMaterial(material, iCurrMat, &span);
        if (j == -1)
            break;
        passIndex++;

        // Set for the prewarm's walk of a pass the opacity test below skips this frame.
        bool opacityAnimated = false;
        if ((fLayerState[0].fBlendFlags & hsGMatState::kBlendAlpha) && (material->GetLayer(iCurrMat)->GetOpacity() <= 0) && (fCurrLightingMethod != plSpan::kLiteVtxPreshaded)) // This opt isn't good for particles, since their
                                                                                                                                                                                // material opacity is undefined/unused... -mcn
        {
            // The prewarm's walk goes on with a pass whose opacity an animation drives (a link fade,
            // plLayerLinkAnimation::Eval, plLayerAnimation.cpp:468-539): the opacity is no key input (the draw uploads
            // it as a value), so the walk's key is the one the draw needs once the animation makes the pass visible.
            if (!fPrewarmWalk || !IOpacityAnimation(material->GetLayer(iCurrMat), nullptr)) {
#ifdef PLASMA_TEST_HOOKS
                if (fRowsArmed && !fPrewarmWalk) {
                    // The dump frame's skipped transparent layers: material, layer, opacity, and
                    // whether an opacity controller animates it (plLayerAnimation::GetOpacityCtl).
                    plLayerInterface*       lay = material->GetLayer(iCurrMat);
                    const plLayerAnimation* firstAnim = nullptr;
                    const plLayerAnimation* opacityAnim = IOpacityAnimation(lay, &firstAnim);
                    const plLayerAnimation* anim = opacityAnim ? opacityAnim : firstAnim;
                    plWGPULog(
                        "transparent layer skipped: material {} layer {} (index {}) opacity {.6f} "
                        "opacity controller {}",
                        material->GetKey() ? material->GetKey()->GetUoid().StringIze() : ST_LITERAL("-"),
                        lay && lay->GetKey() ? lay->GetKey()->GetUoid().StringIze() : ST_LITERAL("-"), iCurrMat,
                        lay ? lay->GetOpacity() : -1.f,
                        anim ? (anim->GetOpacityCtl() ? (anim->GetKey() ? anim->GetKey()->GetUoid().StringIze() : ST_LITERAL("yes")) : ST_LITERAL("none (animation layer without one)")) : ST_LITERAL("none"));
                }
#endif
                continue;
            }
            opacityAnimated = true;
        }

        ISetFogParameters(&span, material->GetLayer(iCurrMat));
        fRowCtx.layer = iCurrMat;
        fRowCtx.pass = passIndex;
        fRowCtx.material = material;

        ISetShaders(material->GetLayer(iCurrMat)->GetVertexShader(), material->GetLayer(iCurrMat)->GetPixelShader());

        if (normalLightsDisabled)
            IRestoreSpanLights();

        // Do the regular draw.
        if (opacityAnimated) {
            const size_t inFlight = fDevice.PipelineBuildsPending();
            render.RenderPrims();
            fPrewarmTally.opacityPasses++;
            fPrewarmTally.opacityStarted += uint32_t(fDevice.PipelineBuildsPending() - inFlight);
        } else {
            render.RenderPrims();
        }

        // Take care of projections that get applied to each pass (not in the prewarm's walk).
        if (!fPrewarmWalk && !fLights.fProjEach.empty() && !(fView.fRenderState & kRenderNoProjection)) {
            // Disable all the normal lights.
            IDisableSpanLights();
            normalLightsDisabled = true;

            IRenderProjectionEach(render, material, iCurrMat, span);
        }
        if (IsDebugFlagSet(plPipeDbg::kFlagNoUpperLayers))
            j = material->GetNumLayers();
    }
    IPopPiggyBacks();

    // If we disabled lighting, re-enable it.
    if (normalLightsDisabled)
        IRestoreSpanLights();

    // Render any aux spans associated (not in the prewarm's walk).
    if (!fPrewarmWalk && span.GetNumAuxSpans())
        IRenderAuxSpans(span);

    // Only render projections and shadows if we successfully rendered the span.
    // j == -1 means we aborted render. The prewarm's walk renders no projection (it has no lights), and the receive
    // pass of a span that could receive a shadow (IRenderShadowsOntoSpan).
    if (j >= 0) {
        // Projections that get applied to the frame buffer (after all passes).
        if (!fPrewarmWalk && !fLights.fProjAll.empty() && !(fView.fRenderState & kRenderNoProjection))
            IRenderProjections(render);

        // Handle render of shadows onto geometry.
        if (!fShadows.empty() || fPrewarmWalk)
            IRenderShadowsOntoSpan(render, &span, material);
    }

    return false;
}

void plWGPUPipeline::IRenderProjectionEach(const plWGPURenderPrimFunc& render, hsGMaterial* material, int iPass, const plSpan& span)
{
    // plDXPipeline::IRenderProjectionEach: render any lights that are to be projected onto
    // each pass of the object.
    // If this is a bump map pass, forget it, we've already "done" per-pixel lighting.
    if (fLayerState[iPass].fMiscFlags & (hsGMatState::kMiscBumpLayer | hsGMatState::kMiscBumpChans))
        return;

    // Push the LayerShadowBase override. This sets the blend
    // to framebuffer as Add/ZNoWrite and AmbientColor = 0.
    static plLayerLightBase layLightBase;

    int iNextPass = iPass + fCurrNumLayers;

    // For each projector:
    for (plLightInfo* li : fLights.fProjEach) {
        // Push it's projected texture as a piggyback.

        // Lower end boards are iffy on when they'll project correctly.
        if (fSettings.fCantProj && !li->GetProperty(plLightInfo::kLPForceProj)) {
            fCounters.projCantProj++;
            continue;
        }

        plLayerInterface* proj = li->GetProjection();
        hsAssert(proj, "A projector with no texture to project?");
        IPushProjPiggyBack(proj);

        // Enable the projecting light only.
        plWGPULightRef* ref = (plWGPULightRef*)li->GetDeviceRef();
        fD3DDevice->LightEnable(ref->fD3DIndex, true);

        AppendLayerInterface(&layLightBase, false);

        // Render until it's done.
        int iRePass = iPass;
        while (iRePass < iNextPass) {
            iRePass = IHandleMaterial(material, iRePass, &span);
            ISetShaders(nullptr, nullptr);

            // Do the render with projection.
            const int kind = fRowCtx.kind;
            fRowCtx.kind = kRowProjEach;
            fRowCtx.light = li;
            fCounters.projEachPasses++;
            render.RenderPrims();
            fRowCtx.kind = kind;
            fRowCtx.light = nullptr;

            // DX loops again on -1 (undefined: IHandleMaterial with layer -1); this pass was
            // just drawn by ILoopOverLayers, so it does not happen. Logged if it ever does.
            if (iRePass < 0) {
                plWGPULog("IRenderProjectionEach: IHandleMaterial aborted a re-pass of {} (DX would loop)",
                          material->GetKey() ? material->GetKey()->GetUoid().StringIze() : ST_LITERAL("(no key)"));
                break;
            }
        }

        RemoveLayerInterface(&layLightBase, false);

        // Disable the projecting light
        fD3DDevice->LightEnable(ref->fD3DIndex, false);

        // Pop it's projected texture off piggyback
        IPopProjPiggyBacks();
    }
}

void plWGPUPipeline::IRenderProjections(const plWGPURenderPrimFunc& render)
{
    // plDXPipeline::IRenderProjections: projected lights rendered once after all the passes.
    IDisableSpanLights();
    for (plLightInfo* li : fLights.fProjAll) {
        if (fSettings.fCantProj && !li->GetProperty(plLightInfo::kLPForceProj)) {
            fCounters.projCantProj++;
            continue;
        }

        IRenderProjection(render, li);
    }
    IRestoreSpanLights();
}

void plWGPUPipeline::IRenderProjection(const plWGPURenderPrimFunc& render, plLightInfo* li)
{
    // plDXPipeline::IRenderProjection: render this light's projection onto the frame buffer.
    plWGPULightRef* ref = (plWGPULightRef*)li->GetDeviceRef();
    fD3DDevice->LightEnable(ref->fD3DIndex, true);

    plLayerInterface* proj = li->GetProjection();

    static D3DMATERIAL9 mat;
    mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.f;

    fD3DDevice->SetMaterial(&mat);
    fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

    fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);

    fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0xffffffff); //@@@

    // Set the FB blend mode, texture, all that.
    ICompositeLayerState(0, proj);
    // We should have put ZNoZWrite on during export, but we didn't.
    fLayerState[0].fZFlags = hsGMatState::kZNoZWrite;
    fCurrNumLayers = 1;
    IHandleFirstTextureStage(proj);

    if (proj->GetBlendFlags() & hsGMatState::kBlendInvertFinalColor) {
        fD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE | D3DTA_COMPLEMENT);
    }

    // Seal it up
    fLastEndingStage = 1;
    fD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    fD3DDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    fLayerState[1].fBlendFlags = uint32_t(-1);

    // Okay, render it already.
    const int kind = fRowCtx.kind;
    fRowCtx.kind = kRowProjAll;
    fRowCtx.light = li;
    fCounters.projAllPasses++;
    render.RenderPrims();
    fRowCtx.kind = kind;
    fRowCtx.light = nullptr;

    fD3DDevice->LightEnable(ref->fD3DIndex, false);
}

void plWGPUPipeline::IRenderShadowsOntoSpan(const plWGPURenderPrimFunc& render, const plSpan* span, hsGMaterial* mat)
{
    // plDXPipeline::IRenderShadowsOntoSpan: after the usual render of a span (all
    // passes), project the shadows that affect it. slaveBits records them (pl3DPipeline
    // IAttachSlaveToReceivers, ISetShadowFromGroup).
    if (fPrewarmWalk) {
        // The prewarm's walk: the receive pass's pipeline for a span that could receive a shadow. A slave is attached
        // to a span per frame (pl3DPipeline::IAttachSlaveToReceivers, ISetShadowFromGroup), always after
        // IReceivesShadows(span, mat) and only while shadows are cast; the pass is what the first attached slave's
        // iteration below draws, without the slave's textures and transforms (ISetupShadowSlaveTextures), its light's
        // power and enable: no key field. No slave is read and nothing is counted.
        if (plShadowCaster::ShadowCastDisabled() || !IReceivesShadows(span, mat))
            return;
        ISetupShadowRcvTextureStages(mat);
        if (!IsDebugFlagSet(plPipeDbg::kFlagNoShadowApply)) {
            const int kind = fRowCtx.kind;
            fRowCtx.kind = kRowShadowReceive;
            render.RenderPrims();
            fRowCtx.kind = kind;
        }
        return;
    }
    const hsBitVector& slaveBits = span->GetShadowSlaves();

    bool first = true;

    for (plShadowSlave* shadow : fShadows) {
        if (slaveBits.IsBitSet(shadow->fIndex)) {
            // This slave affects this span.
            if (first) {
                // On the first, we do all the setup that is independent of
                // the shadow slave, so state that needs to get set once before
                // projecting any number of shadow maps.
                ISetupShadowRcvTextureStages(mat);

                first = false;
                fCounters.shadowReceiveSpans++;
            }

            // Now setup any state specific to this shadow slave.
            ISetupShadowSlaveTextures(shadow);

            int selfShadowNow = span->IsShadowBitSet(shadow->fIndex);

            // We vary the shadow intensity when self shadowing (see below),
            // so we cache whether the shadow light is set for regular or
            // self shadowing intensity. If what we're doing now is different
            // than what we're currently set for, set it again.
            if (selfShadowNow != shadow->fSelfShadowOn) {
                plWGPULightRef* lRef = fLights.fShadowLights[shadow->fLightRefIdx];

                // We lower the power on self shadowing, because the artists like to
                // crank up the shadow strength to huge values to get a darker shadow
                // on the environment, which causes the shadow on the avatar to get
                // way too dark.
                if (selfShadowNow) {
                    plConst(float) kMaxSelfPower = 0.3f;
                    float power = shadow->fPower > kMaxSelfPower ? (float)kMaxSelfPower : shadow->fPower;
                    lRef->fD3DInfo.Diffuse.r = lRef->fD3DInfo.Diffuse.g = lRef->fD3DInfo.Diffuse.b = power;
                } else {
                    lRef->fD3DInfo.Diffuse.r = lRef->fD3DInfo.Diffuse.g = lRef->fD3DInfo.Diffuse.b = shadow->fPower;
                }
                fD3DDevice->SetLight(lRef->fD3DIndex, &lRef->fD3DInfo);

                // record which our intensity is now set for.
                shadow->fSelfShadowOn = selfShadowNow;
            }

            // Enable the light.
            fD3DDevice->LightEnable(shadow->fLightIndex, true);

            if (!IsDebugFlagSet(plPipeDbg::kFlagNoShadowApply)) {
                const int kind = fRowCtx.kind;
                fRowCtx.kind = kRowShadowReceive;
                fRowCtx.slave = shadow;
                fCounters.shadowReceivePasses++;
                render.RenderPrims();
                fRowCtx.kind = kind;
                fRowCtx.slave = nullptr;
            }

            // Disable it again.
            fD3DDevice->LightEnable(shadow->fLightIndex, false);
        }
    }
}

//// Aux spans, shaders and vertex skinning ////////////////////////////////////

void plWGPUPipeline::IRenderAuxSpan(const plSpan& span, const plAuxSpan* aux)
{
    // plDXPipeline::IRenderAuxSpan
    // Make sure the underlying resources are created and filled in with current data.
#ifdef PLASMA_TEST_HOOKS
    fDevice.fUploadDrawable = aux->fDrawable; // the upload record's owner context
    fDevice.fUploadGroup = uint32_t(-1);
#endif
    CheckVertexBufferRef(aux->fGroup, aux->fVBufferIdx);
    CheckIndexBufferRef(aux->fGroup, aux->fIBufferIdx);
    ICheckAuxBuffers(aux);
#ifdef PLASMA_TEST_HOOKS
    fDevice.fUploadDrawable = nullptr; // the upload record's owner context ends here
#endif

    // Set to render from the aux spans buffers.
    plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)aux->fGroup->GetVertexBufferRef(aux->fVBufferIdx);

    if (!vRef)
        return;

    plWGPUIndexBufferRef* iRef = (plWGPUIndexBufferRef*)aux->fGroup->GetIndexBufferRef(aux->fIBufferIdx);

    if (!iRef)
        return;

    plWGPURenderTriListFunc render(this, vRef, iRef, iRef->fOffset, aux->fVStartIdx, aux->fVLength, aux->fIStartIdx, aux->fILength / 3);

    // Now just loop through the aux material, rendering in as many passes as it takes.
    hsGMaterial*     material = aux->fMaterial;
    const RowContext saved = fRowCtx;
    fRowCtx.kind = kRowAux;
    fRowCtx.material = material;
    fRowCtx.group = aux->fGroup;
    fRowCtx.vbIdx = aux->fVBufferIdx;
    fRowCtx.ibIdx = aux->fIBufferIdx;
    int j;
    int passIndex = -1; // the material pass this is (the per-draw rows' `pass`)
    for (j = 0; j < material->GetNumLayers();) {
        int iCurrMat = j;
        j = IHandleMaterial(material, iCurrMat, &span);
        if (j == -1)
            break;
        passIndex++;
        fRowCtx.layer = iCurrMat;
        fRowCtx.pass = passIndex;

        ISetShaders(material->GetLayer(iCurrMat)->GetVertexShader(), material->GetLayer(iCurrMat)->GetPixelShader());

        if (aux->fFlags & plAuxSpan::kOverrideLiteModel) {
            static D3DMATERIAL9 mat;
            fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0xffffffff);

            fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
            fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

            fD3DDevice->SetMaterial(&mat);
        }

        render.RenderPrims();
    }
    fRowCtx = saved;
}

void plWGPUPipeline::IRenderAuxSpans(const plSpan& span)
{
    // plDXPipeline::IRenderAuxSpans (the stream source restore is not needed: WebGPU binds the
    // buffers with each draw)
    if (IsDebugFlagSet(plPipeDbg::kFlagNoAuxSpans))
        return;

    ISetLocalToWorld(hsMatrix44::IdentityMatrix(), hsMatrix44::IdentityMatrix());

    for (size_t i = 0; i < span.GetNumAuxSpans(); i++)
        IRenderAuxSpan(span, span.GetAuxSpan(i));

    ISetLocalToWorld(span.fLocalToWorld, span.fWorldToLocal);
}

void plWGPUPipeline::ISetShaders(plShader* vShader, plShader* pShader)
{
    // plDXPipeline::ISetShaders: the pipe constants, the device refs (created on first use and
    // linked into fVShaderRefList/fPShaderRefList), the constant upload (GetShader), the stage
    // reset the programmable pipeline needs, the programs set on change, the cull mode.
    const plWGPUShaderProgram* vsHandle = nullptr;
    fUnsupportedProgramPass = false;
    if (vShader) {
        hsAssert(vShader->IsVertexShader(), "Wrong type shader as vertex shader");
        ISetPipeConsts(vShader);
        // the waveset clock this frame draws with (plBiasVS::kTexU0 = c0, w)
        // (a draw's: not the prewarm's walk, which runs between frames, before the next frame's render dump settle
        // reads this frame's clock in plWGPUPipelineObserver::OnBeginRender)
        if (!fPrewarmWalk && vShader->GetDecl() && vShader->GetDecl()->GetID() == plShaderID::vs_BiasNormals) {
            const float w = vShader->GetFloat(plBiasVS::kTexU0, 3);
            uint32_t    bits;
            memcpy(&bits, &w, sizeof(bits));
            if (fFrameWaveBiasSeen && bits != fFrameWaveBiasBits)
                fFrameWaveBiasMixed = true;
            if (!fFrameWaveBiasSeen)
                fFrameWaveBiasBits = bits;
            fFrameWaveBiasSeen = true;
        }

        plWGPUVertexShader* vRef = (plWGPUVertexShader*)vShader->GetDeviceRef();
        if (!vRef) {
            vRef = new plWGPUVertexShader(vShader);
            vRef->UnRef();
        }
        if (!vRef->IsLinked())
            vRef->Link(&fVShaderRefList);
        vsHandle = vRef->GetShader(this);
        if (!vsHandle)
            INoteUnsupportedProgram(vShader, vRef->GetErrorString());

        // This is truly obnoxious, but D3D insists that, while using the progammable pipeline,
        // all stages be set up like this, not just the ones we're going to use. We have to
        // do this if we have either a vertex or a pixel shader. See below. Whatever. mf
        int i;
        for (i = 0; i < 8; i++) {
            fD3DDevice->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, fLayerUVWSrcs[i] = i);
            fD3DDevice->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, fLayerXformFlags[i] = 0);
        }
    }

    const plWGPUShaderProgram* psHandle = nullptr;
    if (pShader) {
        hsAssert(pShader->IsPixelShader(), "Wrong type shader as pixel shader");

        ISetPipeConsts(pShader);

        plWGPUFragmentShader* pRef = (plWGPUFragmentShader*)pShader->GetDeviceRef();
        if (!pRef) {
            pRef = new plWGPUFragmentShader(pShader);
            pRef->UnRef();
        }
        if (!pRef->IsLinked())
            pRef->Link(&fPShaderRefList);
        psHandle = pRef->GetShader(this);
        if (!psHandle)
            INoteUnsupportedProgram(pShader, pRef->GetErrorString());

        if (!vShader) {
            int i;
            for (i = 0; i < 8; i++) {
                fD3DDevice->SetTextureStageState(i, D3DTSS_TEXCOORDINDEX, fLayerUVWSrcs[i] = i);
                fD3DDevice->SetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, fLayerXformFlags[i] = 0);
            }
        }
    }

    if (vsHandle != fSettings.fCurrVertexShader)
        fD3DDevice->SetVertexShader(fSettings.fCurrVertexShader = vsHandle);

    if (psHandle != fSettings.fCurrPixelShader)
        fD3DDevice->SetPixelShader(fSettings.fCurrPixelShader = psHandle);

    // Handle cull mode here, because current cullmode is dependent on
    // the handedness of the LocalToCamera AND whether we are twosided.
    ISetCullMode();
}

void plWGPUPipeline::INoteUnsupportedProgram(plShader* shader, const ST::string& error)
{
    // Vertex and pixel programs outside the ported set (plWGPUShaderProgram::Find) are not
    // supported: a request is counted per plShaderID, logged once per program to pipeline.log
    // (the pipeline's warnings go there, as plDXPipeline's do), and its pass is not drawn
    // (RenderPrims). Any other GetShader failure (no bytecode) also skips the pass, where
    // plDXPipeline would draw it with the fixed-function stages.
    fUnsupportedProgramPass = true;
    if (fPrewarmWalk)
        return; // the walk's pass is not drawn either; the draw counts and logs it
    const plShaderDecl* decl = shader->GetDecl();
    const uint32_t      id = decl ? uint32_t(decl->GetID()) : 0;
    if (id < plShaderID::kNumShaders) {
        if (fUnsupportedProgramRequests[id]++ == 0)
            plStatusLog::AddLineSF("pipeline.log", 0xffff0000, "{}", error);
    }
}

// ISetPipeConsts //////////////////////////////////////////////////////////////////
// plDXPipeline::ISetPipeConsts, line for line: the constants a shader asks the pipeline to fill
// in just before the draw (plPipeConst, plShader.h). The lighting pipe constants are not
// implemented in DX either.
void plWGPUPipeline::ISetPipeConsts(plShader* shader)
{
    size_t n = shader->GetNumPipeConsts();
    for (size_t i = 0; i < n; i++) {
        const plPipeConst& pc = shader->GetPipeConst(i);
        switch (pc.fType) {
            case plPipeConst::kFogSet: {
                float set[4];
                IGetVSFogSet(set);
                shader->SetFloat4(pc.fReg, set);
            } break;
            case plPipeConst::kLayAmbient: {
                hsColorRGBA col = fCurrLay->GetAmbientColor();
                shader->SetColor(pc.fReg, col);
            } break;
            case plPipeConst::kLayRuntime: {
                hsColorRGBA col = fCurrLay->GetRuntimeColor();
                col.a = fCurrLay->GetOpacity();
                shader->SetColor(pc.fReg, col);
            } break;
            case plPipeConst::kLaySpecular: {
                hsColorRGBA col = fCurrLay->GetSpecularColor();
                shader->SetColor(pc.fReg, col);
            } break;
            case plPipeConst::kTex3x4_0:
            case plPipeConst::kTex3x4_1:
            case plPipeConst::kTex3x4_2:
            case plPipeConst::kTex3x4_3:
            case plPipeConst::kTex3x4_4:
            case plPipeConst::kTex3x4_5:
            case plPipeConst::kTex3x4_6:
            case plPipeConst::kTex3x4_7: {
                int stage = pc.fType - plPipeConst::kTex3x4_0;

                if (stage > fCurrNumLayers) {
                    // Ooops. This is bad, means the shader is expecting more layers than
                    // we actually have (or is just bogus). Assert and quietly continue.
                    hsAssert(false, "Shader asking for higher stage transform than we have");
                    continue;
                }
                const hsMatrix44& xfm = fCurrMaterial->GetLayer(fCurrLayerIdx + stage)->GetTransform();

                shader->SetMatrix34(pc.fReg, xfm);
            } break;
            case plPipeConst::kTex2x4_0:
            case plPipeConst::kTex2x4_1:
            case plPipeConst::kTex2x4_2:
            case plPipeConst::kTex2x4_3:
            case plPipeConst::kTex2x4_4:
            case plPipeConst::kTex2x4_5:
            case plPipeConst::kTex2x4_6:
            case plPipeConst::kTex2x4_7: {
                int stage = pc.fType - plPipeConst::kTex2x4_0;

                if (stage > fCurrNumLayers) {
                    // Ooops. This is bad, means the shader is expecting more layers than
                    // we actually have (or is just bogus). Assert and quietly continue.
                    hsAssert(false, "Shader asking for higher stage transform than we have");
                    continue;
                }
                const hsMatrix44& xfm = fCurrMaterial->GetLayer(fCurrLayerIdx + stage)->GetTransform();

                shader->SetMatrix24(pc.fReg, xfm);
            } break;
            case plPipeConst::kTex1x4_0:
            case plPipeConst::kTex1x4_1:
            case plPipeConst::kTex1x4_2:
            case plPipeConst::kTex1x4_3:
            case plPipeConst::kTex1x4_4:
            case plPipeConst::kTex1x4_5:
            case plPipeConst::kTex1x4_6:
            case plPipeConst::kTex1x4_7: {
                int stage = pc.fType - plPipeConst::kTex1x4_0;

                if (stage > fCurrNumLayers) {
                    // Ooops. This is bad, means the shader is expecting more layers than
                    // we actually have (or is just bogus). Assert and quietly continue.
                    hsAssert(false, "Shader asking for higher stage transform than we have");
                    continue;
                }
                const hsMatrix44& xfm = fCurrMaterial->GetLayer(fCurrLayerIdx + stage)->GetTransform();

                shader->SetFloat4(pc.fReg, xfm.fMap[0]);
            } break;
            case plPipeConst::kLocalToNDC: {
                hsMatrix44 cam2ndc = IGetCameraToNDC();
                hsMatrix44 world2cam = GetViewTransform().GetWorldToCamera();

                hsMatrix44 local2ndc = cam2ndc * world2cam * GetLocalToWorld();

                shader->SetMatrix44(pc.fReg, local2ndc);
            } break;

            case plPipeConst::kCameraToNDC: {
                hsMatrix44 cam2ndc = IGetCameraToNDC();

                shader->SetMatrix44(pc.fReg, cam2ndc);
            } break;

            case plPipeConst::kWorldToNDC: {
                hsMatrix44 cam2ndc = IGetCameraToNDC();
                hsMatrix44 world2cam = GetViewTransform().GetWorldToCamera();

                hsMatrix44 world2ndc = cam2ndc * world2cam;

                shader->SetMatrix44(pc.fReg, world2ndc);
            } break;

            case plPipeConst::kLocalToWorld:
                shader->SetMatrix34(pc.fReg, GetLocalToWorld());
                break;

            case plPipeConst::kWorldToLocal:
                shader->SetMatrix34(pc.fReg, GetWorldToLocal());
                break;

            case plPipeConst::kWorldToCamera: {
                hsMatrix44 world2cam = GetViewTransform().GetWorldToCamera();

                shader->SetMatrix34(pc.fReg, world2cam);
            } break;

            case plPipeConst::kCameraToWorld: {
                hsMatrix44 cam2world = GetViewTransform().GetCameraToWorld();

                shader->SetMatrix34(pc.fReg, cam2world);
            } break;

            case plPipeConst::kLocalToCamera: {
                hsMatrix44 world2cam = GetViewTransform().GetWorldToCamera();

                hsMatrix44 local2cam = world2cam * GetLocalToWorld();

                shader->SetMatrix34(pc.fReg, local2cam);
            } break;

            case plPipeConst::kCameraToLocal: {
                hsMatrix44 cam2world = GetViewTransform().GetCameraToWorld();

                hsMatrix44 cam2local = GetWorldToLocal() * cam2world;

                shader->SetMatrix34(pc.fReg, cam2local);
            } break;

            case plPipeConst::kCamPosWorld: {
                shader->SetVectorW(pc.fReg, GetViewTransform().GetCameraToWorld().GetTranslate(), 1.f);
            } break;

            case plPipeConst::kCamPosLocal: {
                hsPoint3 localCam = GetWorldToLocal() * GetViewTransform().GetCameraToWorld().GetTranslate();

                shader->SetVectorW(pc.fReg, localCam, 1.f);
            } break;

            case plPipeConst::kObjPosWorld: {
                shader->SetVectorW(pc.fReg, GetLocalToWorld().GetTranslate(), 1.f);
            } break;

            // UNIMPLEMENTED
            case plPipeConst::kDirLight1:
            case plPipeConst::kDirLight2:
            case plPipeConst::kDirLight3:
            case plPipeConst::kDirLight4:
            case plPipeConst::kPointLight1:
            case plPipeConst::kPointLight2:
            case plPipeConst::kPointLight3:
            case plPipeConst::kPointLight4:
                break;

            // DX's switch has no case for these (plDXPipeline::ISetPipeConsts): a no-op there too.
            // plMetalPipeline::ISetPipeConsts fills kColorFilter; this pipeline follows DX.
            case plPipeConst::kColorFilter:
            case plPipeConst::kMaxType:
                break;
        }
    }
}

// IGetVSFogSet ///////////////////////////////////////////////////////////////
// plDXPipeline::IGetVSFogSet: translate the current fog settings into a linear fog that the
// current vertex shaders can use.
void plWGPUPipeline::IGetVSFogSet(float* const set) const
{
    set[2] = 0.f;
    set[3] = 1.f;
    if (fCurrFog.fEnvPtr) {
        hsColorRGBA colorTrash;
        float       start;
        float       end;
        fCurrFog.fEnvPtr->GetPipelineParams(&start, &end, &colorTrash);
        if (end > start) {
            set[0] = -end;
            set[1] = 1.f / (start - end);
        } else {
            set[0] = 1.f;
            set[1] = 0.f;
        }
    } else {
        set[0] = 1.f;
        set[1] = 0.f;
    }
}

// IReleaseShaders ///////////////////////////////////////////////////////////////
// plDXPipeline::IReleaseShaders: delete our vertex and pixel shaders. Releasing the plasma ref
// releases the program.
void plWGPUPipeline::IReleaseShaders()
{
    while (fVShaderRefList) {
        plWGPUVertexShader* ref = fVShaderRefList;
        ref->Release();
        ref->Unlink();
    }

    while (fPShaderRefList) {
        plWGPUFragmentShader* ref = fPShaderRefList;
        ref->Release();
        ref->Unlink();
    }
}

bool plWGPUPipeline::ISoftwareVertexBlend(plDrawableSpans* drawable, const std::vector<int16_t>& visList)
{
    // plDXPipeline::ISoftwareVertexBlend
    if (IsDebugFlagSet(plPipeDbg::kFlagNoSkinning))
        return true;

    if (drawable->GetSkinTime() == fRenderCnt)
        return true;

    const hsBitVector& blendBits = drawable->GetBlendingSpanVector();

    if (drawable->GetBlendingSpanVector().Empty()) {
        // This sucker doesn't have any skinning spans anyway. Just return
        drawable->SetSkinTime(fRenderCnt);
        return true;
    }

    // First, figure out which buffers we need to blend.
    constexpr size_t kMaxBufferGroups = 20;
    constexpr size_t kMaxVertexBuffers = 20;
    static char      blendBuffers[kMaxBufferGroups][kMaxVertexBuffers];
    memset(blendBuffers, 0, kMaxBufferGroups * kMaxVertexBuffers * sizeof(**blendBuffers));

    hsAssert(kMaxBufferGroups >= drawable->GetNumBufferGroups(), "Bigger than we counted on num groups skin.");

    const std::vector<plSpan*>& spans = drawable->GetSpanArray();
    for (int16_t idx : visList) {
        if (blendBits.IsBitSet(idx)) {
            const plVertexSpan& vSpan = *(plVertexSpan*)spans[idx];
            hsAssert(kMaxVertexBuffers > vSpan.fVBufferIdx, "Bigger than we counted on num buffers skin.");

            blendBuffers[vSpan.fGroupIdx][vSpan.fVBufferIdx] = 1;
            drawable->SetBlendingSpanVectorBit(idx, false);
        }
    }

    // Now go through each of the group/buffer (= a real vertex buffer) pairs we found,
    // and blend into it. We'll lock the buffer once, and then for each span that
    // uses it, set the matrix palette and and then do the blend for that span.
    // When we've done all the spans for a group/buffer, we unlock it and move on.
    for (size_t i = 0; i < kMaxBufferGroups; i++) {
        for (size_t j = 0; j < kMaxVertexBuffers; j++) {
            if (blendBuffers[i][j]) {
                // Found one. Do the lock.
                plWGPUVertexBufferRef* vRef = (plWGPUVertexBufferRef*)drawable->GetVertexRef(i, j);

                hsAssert(vRef->fData, "Going into skinning with no place to put results!");

                uint8_t* destPtr = vRef->fData;

                for (int16_t idx : visList) {
                    const plIcicle& span = *(plIcicle*)spans[idx];
                    if ((span.fGroupIdx == i) && (span.fVBufferIdx == j)) {
                        hsMatrix44* matrixPalette = drawable->GetMatrixPalette(span.fBaseMatrix);
                        matrixPalette[0] = span.fLocalToWorld;

                        uint8_t* ptr = vRef->fOwner->GetVertBufferData(vRef->fIndex);
                        ptr += span.fVStartIdx * vRef->fOwner->GetVertexSize();
                        // The skinning plDXPipeline and plMetalPipeline share. On Emscripten its CPU dispatcher
                        // (hsCpuFunctionDispatcher) finds no CPUID and no SSE headers, so it runs the FPU variant,
                        // IBlendVertBuffer<ISkinVertexFPU>.
                        plSoftwareMeshSkinner::BlendVertBuffer(&span,
                                                               matrixPalette, span.fNumMatrices,
                                                               ptr,
                                                               vRef->fOwner->GetVertexFormat(),
                                                               vRef->fOwner->GetVertexSize(),
                                                               destPtr + span.fVStartIdx * vRef->fVertexSize,
                                                               vRef->fVertexSize,
                                                               span.fVLength,
                                                               span.fLocalUVWChans);
                        vRef->SetDirty(true);
                    }
                }
                // Unlock and move on.
            }
        }
    }

    if (drawable->GetBlendingSpanVector().Empty()) {
        // Only do this if we've blended ALL of the spans. Thus, this becomes a trivial
        // rejection for all the skinning flags being cleared
        drawable->SetSkinTime(fRenderCnt);
    }

    return true;
}

//// Fog ///////////////////////////////////////////////////////////////////////

void plWGPUPipeline::ISetFogParameters(const plSpan* span, const plLayerInterface* baseLay)
{
    // plDXPipeline::ISetFogParameters
#ifndef PLASMA_EXTERNAL_RELEASE
    if (IsDebugFlagSet(plPipeDbg::kFlagNoFog)) {
        fCurrFog.fEnvPtr = nullptr;
        fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
        return;
    }
#endif // PLASMA_EXTERNAL_RELEASE

    const plFogEnvironment* fog = (span ? (span->fFogEnvironment ? span->fFogEnvironment : &fView.GetDefaultFog()) : nullptr);

    uint8_t isVertex = 0;
    uint8_t isShader = false;
    if (baseLay) {
        if ((baseLay->GetShadeFlags() & hsGMatState::kShadeReallyNoFog) && !(fMatOverOff.fShadeFlags & hsGMatState::kShadeReallyNoFog))
            fog = nullptr;
        if (baseLay->GetVertexShader())
            isShader = true;
    }
    if (fMatOverOn.fShadeFlags & hsGMatState::kShadeReallyNoFog)
        fog = nullptr;

    bool               forceLoad = false;
    D3DRENDERSTATETYPE d3dFogType = D3DRS_FOGTABLEMODE; // Use VERTEXMODE for vertex fog

    if (!(fSettings.fD3DCaps & kCapsPixelFog) || isShader) {
        d3dFogType = D3DRS_FOGVERTEXMODE;
        isVertex = true;
    }

    // Quick check
    if ((fCurrFog.fEnvPtr == fog) && (fCurrFog.fIsVertex == isVertex) && (fCurrFog.fIsShader == isShader))
        return;

    uint8_t type = (fog == nullptr) ? plFogEnvironment::kNoFog : fog->GetType();

    if (type == plFogEnvironment::kNoFog) {
        /// No fog, just disable
        fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
        fCurrFog.fEnvPtr = nullptr;
        return;
    } else if (fCurrFog.fEnvPtr != fog) {
        fD3DDevice->SetRenderState(D3DRS_FOGENABLE, TRUE);
        forceLoad = true;
        fCurrFog.fEnvPtr = fog;
    }

    if (isShader)
        type = plFogEnvironment::kLinearFog;

    if (fCurrFog.fIsShader != isShader)
        forceLoad = true;

    if (fCurrFog.fIsVertex != isVertex)
        forceLoad = true;

    fCurrFog.fIsShader = isShader;
    fCurrFog.fIsVertex = isVertex;

    float       startOrDensity, end;
    hsColorRGBA color;

    /// Get params
    if (type == plFogEnvironment::kLinearFog) {
        fog->GetPipelineParams(&startOrDensity, &end, &color);

        if (startOrDensity == end) {
            // This should be legal, but some cards don't like it. Just disable. Same thing.
            fD3DDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
            return;
        }
    } else
        fog->GetPipelineParams(&startOrDensity, &color);

    if (isShader) {
        // None of this is technically necessary, but it's to work around
        // a known goofiness in the NVidia drivers. Actually, I don't think
        // having to set the tablemode fog to linear in addition to setting
        // the vertexmode is even a "known" issue. But turns out to be
        // necessary on GeForceFX latest drivers.
        startOrDensity = 1.f;
        end = 0.f;

        fD3DDevice->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    }

    /// Set color
    if (!(fCurrFog.fColor == color) || forceLoad) {
        fCurrFog.fColor = color;
        fCurrFog.fHexColor = inlGetD3DColor(color);
        fD3DDevice->SetRenderState(D3DRS_FOGCOLOR, fCurrFog.fHexColor);
    }

    D3DFOGMODE modes[4] = {D3DFOG_LINEAR, D3DFOG_EXP, D3DFOG_EXP2, D3DFOG_NONE};

    /// Set type
    if (fCurrFog.fMode != modes[type] || forceLoad) {
        fCurrFog.fMode = modes[type];

        if (fCurrFog.fMode == D3DFOG_LINEAR) {
            fCurrFog.fStart = startOrDensity;
            fCurrFog.fEnd = end;

            fD3DDevice->SetRenderState(d3dFogType, fCurrFog.fMode);
            fD3DDevice->SetRenderState(D3DRS_FOGSTART, F2DW(fCurrFog.fStart));
            fD3DDevice->SetRenderState(D3DRS_FOGEND, F2DW(fCurrFog.fEnd));
        } else {
            fCurrFog.fDensity = startOrDensity;

            fD3DDevice->SetRenderState(d3dFogType, fCurrFog.fMode);
            fD3DDevice->SetRenderState(D3DRS_FOGDENSITY, F2DW(fCurrFog.fDensity));
        }
    } else {
        // Type is the same, but are the params?
        if (fCurrFog.fMode == D3DFOG_LINEAR) {
            if (fCurrFog.fStart != startOrDensity) {
                fCurrFog.fStart = startOrDensity;
                fD3DDevice->SetRenderState(D3DRS_FOGSTART, F2DW(fCurrFog.fStart));
            }

            if (fCurrFog.fEnd != end) {
                fCurrFog.fEnd = end;
                fD3DDevice->SetRenderState(D3DRS_FOGEND, F2DW(fCurrFog.fEnd));
            }
        } else {
            if (fCurrFog.fDensity != startOrDensity) {
                fCurrFog.fDensity = startOrDensity;
                fD3DDevice->SetRenderState(D3DRS_FOGDENSITY, F2DW(fCurrFog.fDensity));
            }
        }
    }
}

//// Lights //////////////////////////////////////////////////////////////////////

hsGDeviceRef* plWGPUPipeline::IMakeLightRef(plLightInfo* owner)
{
    // plDXPipeline::IMakeLightRef
    plWGPULightRef* lRef = new plWGPULightRef();

    /// Assign stuff and update
    lRef->fD3DIndex = fLights.ReserveD3DIndex();
    lRef->fOwner = owner;
    owner->SetDeviceRef(lRef);
    // Unref now, since for now ONLY the BG owns the ref, not us (not until we use it, at least)
    lRef->UnRef();

    lRef->Link(&fLights.fRefList);

    lRef->UpdateD3DInfo(fD3DDevice, &fLights);

    // Neutralize it until we need it.
    fD3DDevice->LightEnable(lRef->fD3DIndex, false);

    return lRef;
}

void plWGPUPipeline::RegisterLight(plLightInfo* liInfo)
{
    // plDXPipeline::RegisterLight
    pl3DPipeline::RegisterLight(liInfo);
    liInfo->SetDeviceRef(IMakeLightRef(liInfo));
    fLights.fTime++;
}

void plWGPUPipeline::UnRegisterLight(plLightInfo* liInfo)
{
    // plDXPipeline::UnRegisterLight
    pl3DPipeline::UnRegisterLight(liInfo);

    fLights.fTime++;
}

void plWGPUPipeline::IEnableLights(plSpan* span)
{
    // plDXPipeline::IEnableLights (fMaxNumLights = 8)
    ISelectLights(span, fMaxNumLights, false);
    if (!(fView.fRenderState & kRenderNoProjection)) {
        ISelectLights(span, fMaxNumProjectors, true);
    }
}

void plWGPUPipeline::ISelectLights(plSpan* span, int numLights, bool proj)
{
    // plDXPipeline::ISelectLights
    int                              i, startScale;
    static hsBitVector               newFlags;
    static std::vector<plLightInfo*> onLights;
    plWGPULightRef*                  ref;
    float                            threshhold, overHold = 0.3f, scale;

    /// Build new flags

    /// Step 1: Find the n strongest lights
    newFlags.Clear();
    onLights.clear();

    if (!IsDebugFlagSet(plPipeDbg::kFlagNoRuntimeLights) &&
        !(IsDebugFlagSet(plPipeDbg::kFlagNoApplyProjLights) && proj) &&
        !(IsDebugFlagSet(plPipeDbg::kFlagOnlyApplyProjLights) && !proj)) {
        std::vector<plLightInfo*>& spanLights = span->GetLightList(proj);

        for (i = 0; i < (int)spanLights.size() && i < numLights; i++) {
            ref = (plWGPULightRef*)spanLights[i]->GetDeviceRef();

            if (ref->IsDirty()) {
                if (ref->fD3DIndex == 0)
                    ref->fD3DIndex = fLights.ReserveD3DIndex();
                ref->UpdateD3DInfo(fD3DDevice, &fLights);
                ref->SetDirty(false);
            }

            newFlags.SetBit(ref->fD3DIndex);
            onLights.emplace_back(spanLights[i]);
        }
        startScale = i;

        /// Attempt #2: Take some of the n strongest lights (below a given threshhold) and
        /// fade them out to nothing as they get closer to the bottom. This way, they fade
        /// out of existence instead of pop out.

        if (i < (int)spanLights.size() - 1 && i > 0) {
            threshhold = span->GetLightStrength(i, proj);
            i--;
            overHold = threshhold * 1.5f;
            if (overHold > span->GetLightStrength(0, proj))
                overHold = span->GetLightStrength(0, proj);

            for (; i > 0 && span->GetLightStrength(i, proj) < overHold; i--) {
                scale = (overHold - span->GetLightStrength(i, proj)) / (overHold - threshhold);

                ref = (plWGPULightRef*)spanLights[i]->GetDeviceRef();

                IScaleD3DLight(ref, (1 - scale) * span->GetLightScale(i, proj));
            }
            startScale = i + 1;
        }

        /// Make sure those lights that aren't scaled....aren't
        for (i = 0; i < startScale; i++) {
            ref = (plWGPULightRef*)spanLights[i]->GetDeviceRef();
            IScaleD3DLight(ref, span->GetLightScale(i, proj));
        }
    }

    // If these are non-projected lights, go ahead and enable them.
    // For the projected lights, don't enable, just remember who they are.
    if (!proj) {
        // Solution is to go through the list twice, first disabling, then
        // enabling. mf
        hsBitVector   newOff = fLights.fEnabledFlags - newFlags;
        hsBitIterator iterOff(newOff);
        for (iterOff.Begin(); !iterOff.End(); iterOff.Advance())
            fD3DDevice->LightEnable(iterOff.Current(), false);

        hsBitVector   newOn = newFlags - fLights.fEnabledFlags;
        hsBitIterator iterOn(newOn);
        for (iterOn.Begin(); !iterOn.End(); iterOn.Advance())
            fD3DDevice->LightEnable(iterOn.Current(), true);
        fLights.fEnabledFlags = newFlags;
    } else {
        fLights.fProjAll.clear();
        fLights.fProjEach.clear();
        for (plLightInfo* light : onLights) {
            if (light->OverAll())
                fLights.fProjAll.emplace_back(light);
            else
                fLights.fProjEach.emplace_back(light);
        }
        onLights.clear();
    }
}

void plWGPUPipeline::IDisableSpanLights()
{
    // plDXPipeline::IDisableSpanLights
    int i;
    for (i = 0; i < fLights.fLastIndex + 1; i++) {
        if (fLights.fEnabledFlags.IsBitSet(i)) {
            fD3DDevice->LightEnable(i, false);
            fLights.fHoldFlags.SetBit(i);
        }
    }
    fLights.fEnabledFlags.Clear();
}

void plWGPUPipeline::IRestoreSpanLights()
{
    // plDXPipeline::IRestoreSpanLights
    int i;
    for (i = 0; i < fLights.fLastIndex + 1; i++) {
        if (fLights.fHoldFlags.IsBitSet(i)) {
            fD3DDevice->LightEnable(i, true);
            fLights.fEnabledFlags.SetBit(i);
        }
    }
    fLights.fHoldFlags.Clear();
}

void plWGPUPipeline::IScaleD3DLight(plWGPULightRef* ref, float scale)
{
    // plDXPipeline::IScaleD3DLight
    scale = int(scale * 1.e1f) * 1.e-1f;
    if (ref->fScale != scale) {
        D3DLIGHT9 light = ref->fD3DInfo;

        light.Diffuse.r *= scale;
        light.Diffuse.g *= scale;
        light.Diffuse.b *= scale;

        light.Ambient.r *= scale;
        light.Ambient.g *= scale;
        light.Ambient.b *= scale;

        light.Specular.r *= scale;
        light.Specular.g *= scale;
        light.Specular.b *= scale;

        fD3DDevice->SetLight(ref->fD3DIndex, &light);
        ref->fScale = scale;
    }
}

// inlPlToDWORDColor (plDXPipeline.cpp)
static inline DWORD inlPlToDWORDColor(const hsColorRGBA& c)
{
    return (DWORD(c.a * 255.99f) << 24) | (DWORD(c.r * 255.99f) << 16) | (DWORD(c.g * 255.99f) << 8) | (DWORD(c.b * 255.99f) << 0);
}

// inlPlToD3DColor (plDXPipeline.cpp)
static inline D3DCOLORVALUE inlPlToD3DColor(const hsColorRGBA& c, float a)
{
    D3DCOLORVALUE ret;
    ret.r = c.r;
    ret.g = c.g;
    ret.b = c.b;
    ret.a = a;
    return ret;
}

// CONVERT_FLOAT_TO_BYTE_COLOR (plDXPipeline.h): the float's bits, 0 at or below
// +0, 255 at or above 1, else the float times 256 truncated.
static inline DWORD IConvertFloatToByteColor(float f)
{
    int32_t floatBits;
    memcpy(&floatBits, &f, sizeof(floatBits));
    if (floatBits <= 0)
        return 0;
    if (floatBits >= 0x3f800000)
        return 255;
    int32_t times256 = floatBits + (8 << 23);
    float   g;
    memcpy(&g, &times256, sizeof(g));
    return DWORD(g);
}

inline DWORD plWGPUPipeline::inlGetD3DColor(const hsColorRGBA& col) const
{
    // plDXPipeline::inlGetD3DColor
    DWORD dr = IConvertFloatToByteColor(col.r);
    DWORD dg = IConvertFloatToByteColor(col.g);
    DWORD db = IConvertFloatToByteColor(col.b);
    DWORD da = IConvertFloatToByteColor(col.a);
    return (da << 24) | (dr << 16) | (dg << 8) | db;
}

void plWGPUPipeline::ICalcLighting(const plLayerInterface* currLayer, const plSpan* currSpan)
{
    // plDXPipeline::ICalcLighting
    D3DMATERIAL9 mat;
    static float diffScale = 1.f;
    static float ambScale = 1.f;
    uint32_t     props;

    memset(&mat, 0, sizeof(mat));

    /// Normal rendering--select the right lighting equation
    if (IsDebugFlagSet(plPipeDbg::kFlagAllBright)) {
        inlEnsureLightingOn();
        mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.f;
        mat.Ambient.r = mat.Ambient.g = mat.Ambient.b = mat.Ambient.a = 1.f;
        mat.Emissive.r = mat.Emissive.g = mat.Emissive.b = mat.Emissive.a = 1.f;
        fD3DDevice->SetMaterial(&mat);
        fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
        fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
        fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
        fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
        fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0xffffffff);
        return;
    }

    props = (currSpan != nullptr) ? (currSpan->fProps & plSpan::kLiteMask) : plSpan::kLiteMaterial;

    if (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpChans) {
        props = plSpan::kLiteMaterial;
        fLayerState[0].fShadeFlags |= hsGMatState::kShadeNoShade | hsGMatState::kShadeWhite;
    }
    /// Select one of our three lighting methods
    switch (props) {
        case plSpan::kLiteMaterial: // Material shading

            ///     Material: (the one we all know and love)
            ///             MATd * VTXd + MATa + <sigma of lighting w/ MATd>

            inlEnsureLightingOn();

            // D3D ambient - give it our material static diffuse, since it will be multiplied by the vertex color
            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeWhite) {
                mat.Ambient.r = mat.Ambient.g = mat.Ambient.b = diffScale;
                mat.Ambient.a = 1.f;
            } else if (IsDebugFlagSet(plPipeDbg::kFlagNoPreShade)) {
                mat.Ambient.r = mat.Ambient.g = mat.Ambient.b = 0;
                mat.Ambient.a = 1.f;
            } else
                mat.Ambient = inlPlToD3DColor(currLayer->GetPreshadeColor() * diffScale, 1.f);

            // D3D diffuse - give it our runtime material diffuse
            mat.Diffuse = inlPlToD3DColor(currLayer->GetRuntimeColor() * diffScale, currLayer->GetOpacity());

            // D3D emissive - give it our material ambient
            mat.Emissive = inlPlToD3DColor(currLayer->GetAmbientColor() * ambScale, 1.f);

            // Set specular properties
            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeSpecular) {
                mat.Specular = inlPlToD3DColor(currLayer->GetSpecularColor(), 1.f);
                mat.Power = currLayer->GetSpecularPower();
            }

            fD3DDevice->SetMaterial(&mat);
            fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeWhite)
                fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0xffffffff);
            else {
                hsColorRGBA amb;
                amb.Set(mat.Ambient.r, mat.Ambient.g, mat.Ambient.b, mat.Ambient.a);
                fD3DDevice->SetRenderState(D3DRS_AMBIENT, inlGetD3DColor(amb));
            }

            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeNoShade)
                fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
            else
                fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);

            fCurrLightingMethod = plSpan::kLiteMaterial;
            break;

        case plSpan::kLiteVtxPreshaded: // Vtx preshaded
            //              MATa * VTXd  + 0     + <sigma of lighting w/ VTXd>
            // Mapping to:  GLa  * AMSrc + EMSrc + <.....................DMSrc>

            // (the PARTICLESHADE 0 branch of plDXPipeline::ICalcLighting)
            inlEnsureLightingOn();

            // MATa * white + 0 + <sigma of lighting with VTXd>

            fD3DDevice->SetMaterial(&mat);

            fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
            fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_AMBIENT, 0);

            fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeEmissive)
                fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_COLOR1);
            else
                fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

            fCurrLightingMethod = plSpan::kLiteVtxPreshaded;
            break;

        case plSpan::kLiteVtxNonPreshaded: // Vtx non-preshaded
            //              white * VTXd + MATa  + <sigma of lighting w/ VTXd>
            // Mapping to:  GLa  * AMSrc + EMSrc + <.....................DMSrc>

            inlEnsureLightingOn();

            // D3D emissive - give it our material ambient
            mat.Emissive = inlPlToD3DColor(currLayer->GetAmbientColor() * ambScale, 1.f);

            // Set specular properties
            if (fLayerState[0].fShadeFlags & hsGMatState::kShadeSpecular) {
                mat.Specular = inlPlToD3DColor(currLayer->GetSpecularColor(), 1.f);
                mat.Power = currLayer->GetSpecularPower();
            }
            fD3DDevice->SetMaterial(&mat);

            // Lightmaps want WHITE here, otherwise we want BLACK
            DWORD preShadeStrength;
            preShadeStrength = inlPlToDWORDColor(currLayer->GetPreshadeColor());
            fD3DDevice->SetRenderState(D3DRS_AMBIENT, preShadeStrength);

            fD3DDevice->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
            fD3DDevice->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_COLOR1);
            fD3DDevice->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
            fD3DDevice->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);

            fCurrLightingMethod = plSpan::kLiteVtxNonPreshaded;
            break;

        default:
            hsAssert(false, "Bad lighting type");
            break;
    }
}

//// plWGPULightSettings (plDXLightSettings, plDXPipeline.cpp) ////////////////////

void plWGPULightSettings::Reset(plWGPUPipeline* pipe)
{
    Release();

    fUsedFlags.Clear();
    fEnabledFlags.Clear();
    fHoldFlags.Clear();
    fProjEach.clear();
    fProjAll.clear();
    fShadowLights.clear();
    fNextIndex = 1; /// Light 0 is reserved
    fLastIndex = 1;
    fTime = 0;
    fRefList = nullptr;
    fPipeline = pipe;
}

void plWGPULightSettings::Release()
{
    // plDXLightSettings::Release
    plWGPULightRef* ref;

    fProjEach.clear();
    fProjAll.clear();

    while (fRefList) {
        ref = fRefList;
        ref->Release();
        ref->Unlink();
    }

    for (plWGPULightRef*& shadowLight : fShadowLights.pool()) {
        hsRefCnt_SafeUnRef(shadowLight);
        shadowLight = nullptr;
    }
    fShadowLights.release_and_clear();
}

uint32_t plWGPULightSettings::ReserveD3DIndex()
{
    for (; fNextIndex < (uint32_t)-1; fNextIndex++) {
        if (!fUsedFlags.IsBitSet(fNextIndex))
            break;
    }

    fUsedFlags.SetBit(fNextIndex);
    fEnabledFlags.ClearBit(fNextIndex); // Ensure it's cleared
    fHoldFlags.ClearBit(fNextIndex);
    if (fNextIndex > fLastIndex)
        fLastIndex = fNextIndex;

    return fNextIndex;
}

void plWGPULightSettings::ReleaseD3DIndex(uint32_t idx)
{
    fUsedFlags.SetBit(idx, false);
    if (fNextIndex > idx)
        fNextIndex = idx; // Forces search to start here next time

    // Dec down fLastIndex
    while (fLastIndex > 0 && !fUsedFlags.IsBitSet(fLastIndex))
        fLastIndex--;

    if (fNextIndex > fLastIndex)
        fNextIndex = fLastIndex;
}

//// Frame counters ////////////////////////////////////////////////////////////////

void plWGPUPipeline::ICountPass()
{
    fCounters.passes++;
    if (fLayerState[0].fMiscFlags & hsGMatState::kMiscBumpChans) {
        // Which bump materials drew (once per material key).
        if (fCurrMaterial && fCurrMaterial->GetKey() && fBumpLogged.insert(fCurrMaterial->GetKey()->GetUoid().StringIze()).second) {
            uint32_t ops[4];
            for (int s = 0; s < 4; s++)
                ops[s] = fD3DDevice->fStageState[s][D3DTSS_COLOROP];
            plWGPULog("bump pass: {} layer {} stage colorops {} {} {} {}", fCurrMaterial->GetKey()->GetUoid().StringIze(),
                      fCurrLayerIdx, ops[0], ops[1], ops[2], ops[3]);
        }
    }
    if (fD3DDevice->fRenderState[D3DRS_SPECULARENABLE]) {
        // Which materials draw with SPECULARENABLE (plDXPipeline::IHandleShadeMode), once per
        // material key, as the bump log above. Log only.
        if (fCurrMaterial && fCurrMaterial->GetKey() && fBumpLogged.insert("spec " + fCurrMaterial->GetKey()->GetUoid().StringIze()).second) {
            plWGPULog("specular pass: {} layer {} power {.2f}", fCurrMaterial->GetKey()->GetUoid().StringIze(),
                      fCurrLayerIdx, fCurrLay ? fCurrLay->GetSpecularPower() : 0.f);
        }
    }
}

bool plWGPUPipeline::IDrawFixedFunction(const plWGPUDevice::FixedFunctionDraw& draw)
{
#ifdef PLASMA_TEST_HOOKS
    fDevice.fLastDrawValid = false;
#endif
    const bool ok = fDevice.DrawFixedFunction(draw);
#ifdef PLASMA_TEST_HOOKS
    // the armed frame's per-draw row (the private test harness's)
    if (fRowsArmed && fDevice.fLastDrawValid)
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
            plWGPUDrawView view;
            view.draw = &draw;
            observer->OnDraw(*this, view);
        }
#endif
    return ok;
}

bool plWGPUPipeline::IDrawShader(const plWGPUDevice::FixedFunctionDraw& draw, uint32_t numVerts)
{
#ifdef PLASMA_TEST_HOOKS
    fDevice.fLastShaderDrawValid = false;
#endif
    const bool ok = fDevice.DrawShader(draw);
#ifdef PLASMA_TEST_HOOKS
    // the armed frame's shader row and its fixed-function row (the private test harness's)
    if (fRowsArmed && fDevice.fLastShaderDrawValid)
        if (plWGPUPipelineObserver* observer = plWGPUPipelineObserver::Get()) {
            plWGPUDrawView view;
            view.draw = &draw;
            view.shader = true;
            view.numVerts = numVerts;
            observer->OnDraw(*this, view);
        }
#endif
    return ok;
}
