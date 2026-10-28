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

#ifndef _plWGPUPipeline_inc_
#define _plWGPUPipeline_inc_

// The WebGPU pipeline of the browser (Emscripten) client.
//
// Its structure is pfMetalPipeline's (plMetalPipeline.h): the same base class, members and
// method split. Its behaviour is the D3D9 pipeline's (plDXPipeline): spans, materials, lights,
// fog, bump mapping, projected lights, shadows, avatar texture composition, vertex and pixel
// shader programs, render requests, gamma and screen capture run plDXPipeline's code against an
// emulated D3D9 device (plWGPUD3DDevice). At each draw, plWGPUDevice turns that device's state
// into WebGPU pipelines, uniforms and bind groups (plWGPUDevice::DrawFixedFunction and
// DrawShader).

#include <webgpu/webgpu.h>

#include <memory>
#include <set>
#include <string_theory/string>
#include <string_theory/string_stream>
#include <vector>

#include "hsGMatState.h"
#include "plPipeline/hsG3DDeviceSelector.h"
#include "plPipeline/pl3DPipeline.h"
#include "plWGPUDevice.h"
#include "plWGPUSettings.h"
#include "plWGPUShader.h"

class plAuxSpan;
class plClothingOutfit;
class plGBufferGroup;
class plIcicle;
class plMipmap;
class plRenderRequest;
class plShadowCaster;
class plShadowSlave;
class plPlate;
class plShader;
class plSpan;
class plWGPULightRef;
class plWGPURenderPrimFunc;
class plWGPURenderTargetRef;
class plWGPUVertexShader;
class plWGPUFragmentShader;

// The device enumerator and the client thread's WebGPU instance, adapter, device and
// surface. WebGPU hands them out asynchronously; StartAcquire issues the requests from the
// client loop's kAcquire state (emscripten/main.cpp) and the loop waits for Ready() or
// Failed() before it loads the client, so the plClient constructor and InitPipeline find
// the device ready. The record is only emitted once a device exists;
// the null device's record (plEmscriptenNullDevice.cpp) is always there.
class plWGPUEnumerate
{
public:
    plWGPUEnumerate()
    {
        hsG3DDeviceSelector::AddDeviceEnumerator(&plWGPUEnumerate::Enumerate);
    }

    static void StartAcquire(double t0Ms);
    static bool Ready();
    static bool Failed();

    static WGPUDevice   Device();
    static WGPUSurface  Surface();
    static WGPUInstance Instance();

    /// The current device was lost (its lost callback ran, not at shutdown).
    static bool          DeviceLost();
    static unsigned long DeviceLosses();
    /// Re-request the adapter and device after a loss; Ready() turns true again when they
    /// arrive. FinishReacquire(), called once the pipeline has released every object of the
    /// lost device, releases its handle and clears the loss (true once, when the new Device()
    /// is there).
    static void          StartReacquire();
    static bool          FinishReacquire();
    /// The pipeline is going away: a device loss from now on is not one to recover from.
    static void          SetShuttingDown();
    /// Releases the device, the adapter, the canvas surface and the instance, once, on the client thread; run at exit
    /// (the app's Finish() calls it).
    static void          Shutdown();

    /// A canvas texture acquired for a frame: a new one means the previous frame was presented, and the second
    /// one of the run tells the page that the client's first frame is on the canvas.
    static void          NoteCanvasTexture(WGPUTexture texture);
    /// Uncaptured WebGPU errors so far (validation, out of memory, internal).
    static unsigned long ErrorCount();
    /// The client loop's once-a-second report: the test harness build's STATS line (its pipeline observer);
    /// nothing in the product build.
    static void          LogStats();

private:
    static void Enumerate(std::vector<hsG3DDeviceRecord>& records, hsDisplayHndl display);
};

class plWGPUPipeline : public pl3DPipeline<plWGPUDevice>
{
protected:
    friend class plWGPUDevice;
    friend class plWGPUPlateManager;
    friend class plWGPUTextFont;
    friend class plWGPURenderTriListFunc;
    friend class plWGPUVertexShader;
    friend class plWGPUFragmentShader;
#ifdef PLASMA_TEST_HOOKS
    // The test harness's pipeline observer (plWGPUPipelineObserver.h, testhooks/wgpu/plTestHooksPipelineObserver.h):
    // its render dump reads and arms the frame's state, and the dump frame's per-draw and shader rows read each
    // draw's state.
    friend class plTestHooksPipelineObserver;
#endif

    // plDXPipeline's caps (kCapsZBias and kCapsPixelFog are the ones the ported code reads)
    enum
    {
        kCapsNone = 0x00000000,
        kCapsZBias = 0x00000010,
        kCapsLinearFog = 0x00000020,
        kCapsExpFog = 0x00000040,
        kCapsExp2Fog = 0x00000080,
        kCapsFSAntiAlias = 0x00000800,
        kCapsPixelFog = 0x00008000,
        kCapsNpotTextures = 0x00020000
    };

    plWGPURenderTargetRef* fRenderTargetRefList;
    plWGPUVertexShader*    fVShaderRefList; // as plDXPipeline's
    plWGPUFragmentShader*  fPShaderRefList;

    // The emulated D3D9 device the ported material code sets state on (plDXPipeline's
    // fD3DDevice).
    plWGPUD3DDevice* fD3DDevice;

    // States (plDXPipeline.h)
    plWGPUGeneralSettings fSettings;
    bool                  fCurrD3DLiteState;
    hsGMatState           fLayerState[8]; // base stage (0) state is held in base class
    hsGMatState           fOldLayerState[8];
    bool                  fLayerTransform[8];
    float                 fLayerLODBias[8];
    uint32_t              fLayerUVWSrcs[8];
    uint32_t              fLayerXformFlags[8];
    uint32_t              fLastEndingStage;
    bool                  fTexturing;

    // Fog
    plWGPUFogSettings fCurrFog;

    // Light
    plWGPULightSettings fLights;

    // Shadows (plDXPipeline.h)
    std::vector<plRenderTarget*> fRenderTargetPool512;
    std::vector<plRenderTarget*> fRenderTargetPool256;
    std::vector<plRenderTarget*> fRenderTargetPool128;
    std::vector<plRenderTarget*> fRenderTargetPool64;
    std::vector<plRenderTarget*> fRenderTargetPool32;
    enum
    {
        kMaxRenderTargetNext = 10
    };
    uint32_t          fRenderTargetNext[kMaxRenderTargetNext];
    plWGPUTextureRef* fULutTextureRef;
    plRenderTarget*   fBlurScratchRTs[kMaxRenderTargetNext];
    plRenderTarget*   fBlurDestRTs[kMaxRenderTargetNext];
    WGPUBuffer        fBlurVBuffers[kMaxRenderTargetNext];

public:
    plWGPUPipeline(hsDisplayHndl display, hsWindowHndl window, const hsG3DDeviceModeRecord* devMode);
    ~plWGPUPipeline();

    CLASSNAME_REGISTER(plWGPUPipeline);
    GETINTERFACE_ANY(plWGPUPipeline, plPipeline);

    /*** VIRTUAL METHODS ***/
    bool          PreRender(plDrawable* drawable, std::vector<int16_t>& visList, plVisMgr* visMgr = nullptr) override;
    bool          PrepForRender(plDrawable* drawable, std::vector<int16_t>& visList, plVisMgr* visMgr = nullptr) override;
    plTextFont*   MakeTextFont(ST::string face, uint16_t size) override;
    bool          OpenAccess(plAccessSpan& dst, plDrawableSpans* d, const plVertexSpan* span, bool readOnly) override;
    bool          CloseAccess(plAccessSpan& acc) override;
    void          PushRenderRequest(plRenderRequest* req) override;
    void          PopRenderRequest(plRenderRequest* req) override;
    void          ClearRenderTarget(plDrawable* d) override;
    void          ClearRenderTarget(const hsColorRGBA* col = nullptr, const float* depth = nullptr) override;
    hsGDeviceRef* MakeRenderTargetRef(plRenderTarget* owner) override;
    hsGDeviceRef* SharedRenderTargetRef(plRenderTarget* sharer, plRenderTarget* owner);
    bool          BeginRender() override;
    bool          EndRender() override;
    void          RenderScreenElements() override;
    bool          IsFullScreen() const override;
    void          Resize(uint32_t width, uint32_t height) override;
    // The present settings of the last ResetDisplayDevice (or of creation), so that a resize of the browser
    // window can reset the display without changing them. plClient::MsgReceive's resize has no way to read
    // them ("FIXME... There doesn't seem to be a way to get the current AA value?").
    void          GetPresentSettings(int& numAASamples, int& maxAnisotropicSamples, bool& vSync) const
    {
        numAASamples = fNumAASamples;
        maxAnisotropicSamples = fSettings.fMaxAnisotropicSamples;
        vSync = fVSync;
    }
    void      LoadResources() override;
    bool      SetGamma(float eR, float eG, float eB) override;
    bool      SetGamma(const uint16_t* const tabR, const uint16_t* const tabG, const uint16_t* const tabB) override;
    bool      CaptureScreen(plMipmap* dest, bool flipVertical = false, uint16_t desiredWidth = 0, uint16_t desiredHeight = 0) override;
    plMipmap* ExtractMipMap(plRenderTarget* targ) override;
    void      GetSupportedDisplayModes(std::vector<plDisplayMode>* res, int ColorDepth = 32) override;
    int       GetMaxAnisotropicSamples() override;
    int       GetMaxAntiAlias(int Width, int Height, int ColorDepth) override;
    void      ResetDisplayDevice(int Width, int Height, int ColorDepth, bool Windowed, int NumAASamples, int MaxAnisotropicSamples, bool vSync = false) override;
    void      RenderSpans(plDrawableSpans* ice, const std::vector<int16_t>& visList) override;

    // Create and/or Refresh geometry buffers
    void          CheckVertexBufferRef(plGBufferGroup* owner, uint32_t idx) override;
    void          CheckIndexBufferRef(plGBufferGroup* owner, uint32_t idx) override;
    void          CheckTextureRef(plLayerInterface* lay) override;
    void          CheckTextureRef(plBitmap* bitmap);
    hsGDeviceRef* MakeTextureRef(plBitmap* bitmap);
    void          IReloadTexture(plBitmap* bitmap, plWGPUTextureRef* ref);

    WGPUTextureFormat GetFramebufferFormat() { return fDevice.GetFramebufferFormat(); }

    /// Fill the plMipmaps ExtractMipMap returned this frame: wait for their GPU readbacks (a
    /// suspension of the tick) and copy the rows with alpha forced opaque, as
    /// plDXPipeline::ExtractMipMap does. plClient calls it immediately before
    /// plCaptureRender::Update (the update phase), where no canvas texture is held. A readback
    /// that fails takes the capture back out of plCaptureRender (DropFailedCapture).
    void CompletePendingCaptures();

    /// The frame loop's bracket around a GPU readback wait (CompletePendingCaptures, CaptureScreen's front-buffer
    /// read): begin(how) before the wait, end() after it. begin returns false where the loop cannot suspend, and then
    /// nothing waits. The app registers them before the pipeline exists, as plClient registers plDispatch's and
    /// plProgressMgr's callbacks; until then every GPU wait is refused.
    static void SetGPUWaitProcs(bool (*begin)(const char* how), void (*end)());

    // Default fog settings (plDXPipeline.h)
    void SetDefaultFogEnviron(plFogEnvironment* fog) override
    {
        fView.SetDefaultFog(*fog);
        fCurrFog.fEnvPtr = nullptr;
    }

    // Overriden (Un)Register Light methods (plDXPipeline.h)
    void RegisterLight(plLightInfo* light) override;
    void UnRegisterLight(plLightInfo* light) override;

    /// What the span path drew in a frame. The test harness's render dump reads these counts (its rows, its
    /// settle rule and the row ids).
    struct FrameCounters
    {
        uint32_t passes;              // draws issued by ILoopOverLayers
        uint32_t projEachPasses;      // IRenderProjectionEach draws
        uint32_t projAllPasses;       // IRenderProjection draws
        uint32_t projCantProj;        // projectors skipped for fCantProj && !kLPForceProj
        uint32_t shadowSlaves;        // fShadows at IPreprocessShadows
        uint32_t shadowCasters;       // slaves rendered (IRenderShadowCaster true)
        uint32_t shadowCastSpans;     // IRenderShadowCasterSpan draws
        uint32_t shadowBlurPasses;    // IRenderBlurFromShadowMap + IRenderBlurBackToShadowMap draws
        uint32_t shadowReceiveSpans;  // spans with a slave in IRenderShadowsOntoSpan
        uint32_t shadowReceivePasses; // receive draws (one per slave per span)
        uint32_t clothingOutfits;     // outfits recomposed (IPreprocessAvatarTextures)
        uint32_t clothingQuads;       // IDrawClothingQuad draws
    };
    FrameCounters fCounters;
    FrameCounters fLastCounters;

private:
    bool fIsFullscreen; // plDXGeneralSettings::fFullscreen

    // The gamma ramp as plDXPipeline::SetGamma builds it. As D3D9's SetGammaRamp hands it to the
    // hardware, it applies to the display: the device's LUT pass at present, downstream of the
    // front buffer. The table is kept here across ResetDisplayDevice and the recreate after a
    // device loss, and ICreateDeviceObjects sets it on the new device.
    uint16_t fGammaRamp[3][256];
    bool     fGammaRampSet;
    // The waveset clock of this frame: vs_BiasNormals' c0.w (kTexU0.w, the bias offset that
    // plWaveSet7::IUpdateBiasVShader integrates). The test harness's render dump can wait for a
    // given value of it (PLASMA_RENDER_DUMP_WAVESET_BIAS).
    bool     fFrameWaveBiasSeen;
    bool     fFrameWaveBiasMixed; // two BiasNormals draws of one frame disagreed
    uint32_t fFrameWaveBiasBits;

    // plDXPipeline's device-reset state (fDeviceLost, fDevWasLost, fForceDeviceReset) and present
    // parameters (fSettings.fPresentParams, fSettings.fNumAASamples).
    bool     fDeviceLost;
    bool     fDevWasLost;
    bool     fForceDeviceReset;
    bool     fPresentationLost; // the canvas gave no texture (BeginScene/Present failure)
    bool     fPresentWindowed;  // D3DPRESENT_PARAMETERS::Windowed: always TRUE (ICreateDevice, ResetDisplayDevice)
    uint32_t fPresentWidth;     // BackBufferWidth/Height
    uint32_t fPresentHeight;
    int      fNumAASamples; // plDXGeneralSettings::fNumAASamples

    // The number of SetGamma calls (for its log line and the test harness's render dump) and the times of the last
    // reset and recovery (for the IResetDevice log line).
    struct DisplayStats
    {
        uint32_t gammaSets;
        double   lastResetMs;
        double   lastRecoveryMs; // from the loss to the recreated device
    };
    DisplayStats fDisplayStats;
    struct PendingCapture
    {
        plMipmap*                               mipmap = nullptr;
        std::shared_ptr<plWGPUDevice::Readback> readback;
        WGPUFuture                              future{};
    };
    std::vector<PendingCapture> fPendingCaptures;
    bool                        IResetDevice();
    void                        ICreateDevice();
    void                        ILogPlates(const char* when);
    bool                        fUnsupportedProgramPass; // the current pass asks for an unsupported shader program: not drawn
    // requests for unsupported programs over the run, by plShaderID (each program is logged once)
    uint32_t                    fUnsupportedProgramRequests[plShaderID::kNumShaders];

    std::set<ST::string> fBumpLogged;

    bool ICheckDynBuffers(plDrawableSpans* drawable, plGBufferGroup* group, const plSpan* spanBase);
    bool ICheckAuxBuffers(const plAuxSpan* span);
    bool IRefreshDynVertices(plGBufferGroup* group, plWGPUVertexBufferRef* vRef);

    void IDrawPlate(plPlate* plate);

    /// Every fixed-function draw of the pipeline goes through here, so that the test harness's
    /// per-draw rows see each one.
    bool IDrawFixedFunction(const plWGPUDevice::FixedFunctionDraw& draw);

    // Render targets (plDXPipeline.h)
    bool            IPrepRenderTargetInfo(plRenderTarget* owner, WGPUTextureFormat& surfFormat, WGPUTextureFormat& depthFormat);
    bool            IFindRenderTargetInfo(plRenderTarget* owner, WGPUTextureFormat& surfFormat);
    WGPUTexture     IMakeRenderTargetTexture(plRenderTarget* owner, WGPUTextureFormat format, uint32_t layers);
    void            IRenderTargetSetTexture(plWGPURenderTargetRef* ref, plRenderTarget* owner, WGPUTexture texture,
                                            WGPUTextureFormat format, WGPUTexture depth, WGPUTextureFormat depthFormat);
    void            IMakeRenderTargetPools();
    void            IResetRenderTargetPools();
    plRenderTarget* IFindRenderTarget(uint32_t& w, uint32_t& h, bool ortho);
    void            IReleaseRenderTargetPools();

    // Shadow generation (plDXPipeline.h)
    void              IClearShadowSlaves();
    void              IPreprocessShadows();
    bool              IPrepShadowCaster(const plShadowCaster* caster);
    void              IRenderShadowCasterSpan(plShadowSlave* slave, plDrawableSpans* drawable, const plIcicle& span);
    bool              IRenderShadowCaster(plShadowSlave* slave);
    void              ISetupShadowLight(plShadowSlave* slave);
    plWGPULightRef*   INextShadowLight(plShadowSlave* slave);
    bool              IPushShadowCastState(plShadowSlave* slave);
    bool              IPopShadowCastState(plShadowSlave* slave);
    plWGPUTextureRef* IGetULutTextureRef();
    bool              ICreateBlurVBuffers();
    void              IReleaseBlurVBuffers();

    // Shadow application (plDXPipeline.h)
    void ISetupShadowRcvTextureStages(hsGMaterial* mat);
    void ISetShadowLightState(hsGMaterial* mat);
    void IDisableLightsForShadow();
    void ISetupShadowSlaveTextures(plShadowSlave* slave);

    // Shadow blur (plDXPipeline.h)
    bool       ISetBlurQuadToRender(plRenderTarget* smap);
    void       IRenderBlurBackToShadowMap(plRenderTarget* smap, plRenderTarget* scratch, plRenderTarget* dst);
    void       IRenderBlurFromShadowMap(plRenderTarget* scratchRT, plRenderTarget* smap, float scale);
    void       IBlurSetRenderTarget(plRenderTarget* rt);
    int        IGetScratchRenderTarget(plRenderTarget* smap);
    void       IBlurShadowMap(plShadowSlave* slave);
    WGPUBuffer fBlurQuad;       // ISetBlurQuadToRender's stream source
    void       IDrawBlurQuad(); // DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2) of fBlurQuad

    // Avatar texture rendering (plDXPipeline.h)
    void IPreprocessAvatarTextures();
    void IDrawClothingQuad(float x, float y, float w, float h, float uOff, float vOff, plMipmap* tex);

    // Rendering (plDXPipeline.h)
    bool ILoopOverLayers(const plWGPURenderPrimFunc& render, hsGMaterial* material, const plSpan& span);
    void IRenderBufferSpan(const plIcicle& span, hsGDeviceRef* vb, hsGDeviceRef* ib, hsGMaterial* material,
                           uint32_t vStart, uint32_t vLength, uint32_t iStart, uint32_t iLength);
    void IRenderAuxSpan(const plSpan& span, const plAuxSpan* aux);
    void IRenderAuxSpans(const plSpan& span);
    void ISetupTransforms(plDrawableSpans* drawable, const plSpan& span, hsMatrix44& lastL2W);
    bool IAvatarSort(plDrawableSpans* d, const std::vector<int16_t>& visList);
    bool ISoftwareVertexBlend(plDrawableSpans* drawable, const std::vector<int16_t>& visList);

    // Fog
    void ISetFogParameters(const plSpan* span, const plLayerInterface* baseLay);

    // Lighting
    hsGDeviceRef* IMakeLightRef(plLightInfo* owner);
    void          IScaleD3DLight(plWGPULightRef* ref, float scale);
    void          ICalcLighting(const plLayerInterface* currLayer, const plSpan* currSpan);
    void          IDisableSpanLights();
    void          IRestoreSpanLights();
    void          ISelectLights(plSpan* span, int numLights, bool proj);
    void          IEnableLights(plSpan* span);
    inline void   inlEnsureLightingOff();
    inline void   inlEnsureLightingOn();
    void          IRenderProjection(const plWGPURenderPrimFunc& render, plLightInfo* li);
    void          IRenderProjections(const plWGPURenderPrimFunc& render);
    void          IRenderProjectionEach(const plWGPURenderPrimFunc& render, hsGMaterial* material, int iPass, const plSpan& span);
    void          IRenderShadowsOntoSpan(const plWGPURenderPrimFunc& render, const plSpan* span, hsGMaterial* mat);

    bool              ISkipBumpMap(hsGMaterial* newMat, uint32_t& layer, const plSpan* currSpan) const;
    void              ISetBumpMatrices(const plLayerInterface* layer, const plSpan* span);
    const hsMatrix44& IGetBumpMatrix(uint32_t miscFlags) const;

    // Materials (plDXPipeline.h)
    const hsGMatState& ICompositeLayerState(int which, plLayerInterface* layer);
    int32_t            IHandleMaterial(hsGMaterial* newMat, uint32_t which, const plSpan* currSpan);
    void               IHandleFirstTextureStage(plLayerInterface* layer);
    void               IHandleShadeMode();
    void               IHandleZMode();
    void               IHandleMiscMode();
    void               IHandleTextureStage(uint32_t stage, plLayerInterface* layer);
    void               IHandleFirstStageBlend();
    void               IHandleBumpEnv(int stage, uint32_t blendFlags);
    void               IHandleStageBlend(int stage);
    void               IHandleStageClamp(int stage);
    void               IHandleStageTransform(int stage, plLayerInterface* layer);
    void               IHandleTextureMode(plLayerInterface* layer);
    void               IUseTextureRef(int stage, hsGDeviceRef* dRef, plLayerInterface* layer);
    void               IStageStop(uint32_t stage);
    uint32_t           ILayersAtOnce(hsGMaterial* mat, uint32_t which);
    bool               ICanEatLayer(plLayerInterface* lay);
    void               ISetLayer(uint32_t lay);
    void               IBottomLayer();

    // Push special effects (plDXPipeline.h)
    plLayerInterface* IPushOverBaseLayer(plLayerInterface* li);
    plLayerInterface* IPopOverBaseLayer(plLayerInterface* li);
    plLayerInterface* IPushOverAllLayer(plLayerInterface* li);
    plLayerInterface* IPopOverAllLayer(plLayerInterface* li);
    int               ISetNumActivePiggyBacks();
    void              IPushPiggyBacks(hsGMaterial* mat);
    void              IPopPiggyBacks();
    void              IPushProjPiggyBack(plLayerInterface* li);
    void              IPopProjPiggyBacks();
    void              ISetShaders(plShader* vShader, plShader* pShader);
    void              ISetPipeConsts(plShader* shader);
    void              IGetVSFogSet(float* const set) const;
    void              IReleaseShaders();

    // Device initialization (plDXPipeline.h)
    void IInvalidateState();
    void IInitDeviceState();

    // Transforms (plDXPipeline.h)
    void ISetCullMode(bool flip = false);
    bool IIsViewLeftHanded();

    // View and clipping (plDXPipeline.h)
    void ISetAnisotropy(bool on);

    // Properties (plDXPipeline.h)
    inline plWGPUD3D9::DWORD inlGetD3DColor(const hsColorRGBA& c) const;

    /// Counts a pass about to be drawn (fCounters), from the emulated device state.
    void ICountPass();

    /// The context of the current draw. The test harness's pipeline observer records it in a dump frame's
    /// per-draw rows (testhooks/wgpu/plTestHooksDrawRows.cpp): the render_ops fields, plus the emulated D3D9
    /// state each draw used and its lit colour and stage outputs at the draw's first vertex.
    enum RowKind
    {
        kRowBase,          // a material pass of ILoopOverLayers (counted in fCounters.passes)
        kRowAux,           // IRenderAuxSpan
        kRowPlate,         // IDrawPlate
        kRowProjEach,      // IRenderProjectionEach
        kRowProjAll,       // IRenderProjection
        kRowShadowCast,    // IRenderShadowCasterSpan
        kRowShadowReceive, // IRenderShadowsOntoSpan
        kRowBlur,          // IRenderBlurFromShadowMap, IRenderBlurBackToShadowMap
        kRowClothing       // IDrawClothingQuad
    };
    struct RowContext
    {
        int              kind = kRowBase;
        plDrawableSpans* drawable = nullptr;
        const plSpan*    span = nullptr;
        int              spanIndex = -1; // visList[i] of a base span (the rows' "<drawable>#<index>")
        hsGMaterial*     material = nullptr;
        int              layer = -1;
        int              pass = 0;
        plGBufferGroup*  group = nullptr;
        uint32_t         vbIdx = 0;
        uint32_t         ibIdx = 0;
        uint32_t         vStart = 0;
        uint32_t         vLength = 0;
        uint32_t         iStart = 0;
        uint32_t         iLength = 0;
        plShadowSlave*   slave = nullptr;
        plLightInfo*     light = nullptr;
        plRenderTarget*  target = nullptr;
    };
    // In both builds: its kind also selects the passes ICountPass counts (RenderPrims).
    RowContext fRowCtx;
#ifdef PLASMA_TEST_HOOKS
    // Harness build only: the render dump arms the next frame (fRowsArmNext), whose draws its rows then record
    // (fRowsArmed).
    bool fRowsArmed; // this frame's draws are recorded
    bool fRowsArmNext;
#endif

    /// A pass drawn with the device's vertex and pixel programs (plWGPURenderTriListFunc), and a request for a
    /// program this pipeline does not support. The test harness's pipeline observer records a dump frame's shader
    /// rows (testhooks/wgpu/plTestHooksShaderRows.cpp).
    bool IDrawShader(const plWGPUDevice::FixedFunctionDraw& draw, uint32_t numVerts);
    void INoteUnsupportedProgram(plShader* shader, const ST::string& error);

    // Render targets made and their bytes, for the shadow pool log line and the test harness's per-draw rows.
    struct RenderTargetStats
    {
        uint32_t made;
        uint64_t bytes;
    };
    RenderTargetStats fRenderTargetStats{};

    void ICreateDeviceObjects();
    void IReleaseDynDeviceObjects();
    void IReleaseVertexIndexTextureRefs();
    bool ICreateDynDeviceObjects();
    void IReleaseDynamicBuffers();
    void IReleaseDeviceObjects();

    static plWGPUEnumerate enumerator;

    plTextFont* fTextFontRefList;
};

#endif // _plWGPUPipeline_inc_
