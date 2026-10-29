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

#ifndef _plWGPUDevice_h_
#define _plWGPUDevice_h_

// The WebGPU device of pfWGPUPipeline, a port of plMetalDevice.h. Same split of work between
// pipeline and device, same members where WebGPU has the concept. What differs:
// - the frame: there is no present call. A frame reaches the canvas when the client thread's
//   task ends; SubmitCommandBuffer only submits.
// - one command encoder per frame for the canvas and one per offscreen target, as Metal's
//   command buffers (plMetalDevice::SetRenderTarget).
// - Metal's setVertexBytes/setFragmentBytes become per-frame arenas (uniforms with dynamic
//   offsets, small vertex streams) written with queue.writeBuffer.
// - depth persists across passes and frames (D3D9 semantics), and clears follow D3D9 Clear
//   (plDXPipeline::ClearRenderTarget).

#include <webgpu/webgpu.h>

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_theory/string>
#include <unordered_map>
#include <vector>

#include "HeadSpin.h"
#include "hsGMatState.h"
#include "hsMatrix44.h"
#include "plWGPUD3DDevice.h"
#include "plWGPUDeviceRef.h"

class plWGPUPipeline;
class plRenderTarget;
class plBitmap;
class plMipmap;
class plCubicEnvironmap;
class plWGPUPipelineState;
enum class plWGPUBindLayout;
struct plWGPUFixedFunctionKey;
struct plWGPUShaderKey;
struct plWGPUShaderUniforms;
struct plWGPUFixedState;

class plWGPUDevice
{
    friend plWGPUPipeline;
    friend class plWGPUPlateManager;
    friend class plWGPUPipelineState;
    friend class plWGPUTextFont;
#ifdef PLASMA_TEST_HOOKS
    // The test hooks' pipeline observer (plWGPUPipelineObserver.h): the dump frame's rows read
    // the draw's target, viewport and uniforms, and its shader rows request the targets'
    // readbacks.
    friend class plTestHooksPipelineObserver;
#endif

public:
    typedef plWGPUVertexBufferRef VertexBufferRef;
    typedef plWGPUIndexBufferRef  IndexBufferRef;
    typedef plWGPUTextureRef      TextureRef;

public:
    plWGPUPipeline* fPipeline;

    ST::string fErrorMsg;

    WGPUDevice  fWGPUDevice;
    WGPUQueue   fQueue;
    WGPUSurface fSurface;
    uint32_t    fDeviceType;

    // As plMetalDevice.h
    hsMatrix44 fMatrixProj;
    hsMatrix44 fMatrixL2W;
    hsMatrix44 fMatrixW2L;
    hsMatrix44 fMatrixW2C;
    hsMatrix44 fMatrixC2W;

    // The D3D9 device state the ported DX material code drives (plWGPUD3DDevice.h), and
    // plDXDevice::fCurrCullMode (plDXDevice.h; D3DCULL_CW at creation, as in the plDXDevice
    // constructor).
    plWGPUD3DDevice     fD3D;
    plWGPUD3D9::D3DCULL fCurrCullMode;

public:
    plWGPUDevice();

    bool InitDevice();

    void Shutdown();

    /**
     * Set rendering to the specified render target.
     *
     * Null rendertarget is the primary (plMetalDevice::SetRenderTarget).
     */
    void SetRenderTarget(plRenderTarget* target);
    /** The colour and depth surfaces of a target alone, without the state invalidation and the
     *  viewport of SetRenderTarget: SetRenderTarget(0, main) and SetDepthStencilSurface(depth)
     *  as plDXPipeline::IBlurSetRenderTarget does them. */
    void BindRenderTargetSurfaces(plRenderTarget* target);

    /** The view transform's viewport (plDXDevice::SetViewport); it persists across passes. */
    void SetViewport();
    /** IDirect3DDevice9::SetViewport with an explicit rect (plDXPipeline::IPreprocessAvatarTextures,
        IBlurSetRenderTarget): the avatar and blur targets set theirs directly. */
    void SetViewport(float x, float y, float w, float h);

    bool BeginRender();

    /* Device Ref Functions **************************************************/
    void           SetupVertexBufferRef(plGBufferGroup* owner, uint32_t idx, VertexBufferRef* vRef);
    /// The format of the vertex buffer SetupVertexBufferRef makes for a buffer group's format (the draw's
    /// FixedFunctionDraw::vertexFormat).
    static uint8_t VertexBufferFormat(uint8_t groupFormat);
    void           CheckStaticVertexBuffer(VertexBufferRef* vRef, plGBufferGroup* owner, uint32_t idx);
    void           FillVertexBufferRef(VertexBufferRef* ref, plGBufferGroup* group, uint32_t idx);
    void           FillVolatileVertexBufferRef(VertexBufferRef* ref, plGBufferGroup* group, uint32_t idx);
    void           SetupIndexBufferRef(plGBufferGroup* owner, uint32_t idx, IndexBufferRef* iRef);
    void           CheckIndexBuffer(IndexBufferRef* iRef);
    void           FillIndexBufferRef(IndexBufferRef* iRef, plGBufferGroup* owner, uint32_t idx);

    void SetupTextureRef(plBitmap* img, TextureRef* tRef);
    void CheckTexture(TextureRef* tRef);
    void MakeTextureRef(TextureRef* tRef, plMipmap* img);
    void MakeCubicTextureRef(TextureRef* tRef, plCubicEnvironmap* img);

    ST::string GetErrorString() const { return fErrorMsg; }

    void SetProjectionMatrix(const hsMatrix44& src);
    void SetWorldToCameraMatrix(const hsMatrix44& src);
    void SetLocalToWorldMatrix(const hsMatrix44& src);

    uint8_t              fSampleCount;
    /// The selected mode's FSAA types (hsG3DDeviceMode::fFSAATypes, plWGPUEnumerate::Enumerate),
    /// the list DX's "closest <=" AA search reads (fCurrentMode->fFSAATypes, plDXPipeline::ICreateDevice).
    std::vector<uint8_t> fFSAATypes;

    /// Start a frame on the canvas: acquire the surface texture and open the frame's command
    /// encoder (plMetalDevice::CreateNewCommandBuffer). False if the surface has no texture
    /// (the caller ends the frame, as plMetalPipeline::BeginRender does).
    bool CreateNewCommandBuffer();
    /// Finish and submit the frame's commands (no GPU wait, no present call).
    void SubmitCommandBuffer();
    /// D3D9 Clear (plDXPipeline::ClearRenderTarget) of the current viewport: TARGET and/or
    /// ZBUFFER with the given values. Outside a frame there is nothing to clear into.
    void Clear(bool shouldClearColor, const float clearColor[4], bool shouldClearDepth, float clearDepth);

    void SetMaxAnsiotropy(uint8_t maxAnsiotropy);
    void SetMSAASampleCount(uint8_t sampleCount);

    WGPUSampler SampleStateForClampFlags(hsGMatState::hsGMatClampFlags sampleState) const;

    uint32_t CurrentTargetSampleCount() const
    {
        if (fCurrentRenderTarget) {
            return 1;
        } else {
            return fSampleCount;
        }
    }

    WGPUTextureFormat GetFramebufferFormat() const { return fFramebufferFormat; }

    /// The render pass of the current target, opened on first use
    /// (plMetalDevice::CurrentRenderCommandEncoder). Null outside a frame.
    WGPURenderPassEncoder CurrentRenderCommandEncoder();

    /// The canvas size the surface is configured at (wgpuSurfaceConfigure also sets the
    /// canvas width/height, library_webgpu.js).
    void ConfigureSurface(uint32_t width, uint32_t height);

    /// Per-frame data for one draw: the uniform block at a dynamic offset (group 0) and
    /// small vertex streams (Metal's setVertexBytes). False outside a frame.
    bool BindUniforms(WGPURenderPassEncoder pass, const void* data, size_t size);
    bool BindVertexBytes(WGPURenderPassEncoder pass, uint32_t slot, const void* data, size_t size);
    /// Group 1 of the texture shaders, released after the frame's submit.
    void BindTexture(WGPURenderPassEncoder pass, WGPUTextureView view, WGPUSampler sampler);

    WGPUSampler     GetPointSampler() const { return fPointSampler; }
    WGPUTextureView GetWhiteTextureView() const { return fWhiteTextureView; }

    void SetPipeline(WGPURenderPassEncoder pass, plWGPUPipelineState& state);

    /// One fixed-function draw with the current D3D9 state (fD3D), the role of
    /// IDirect3DDevice9::DrawIndexedPrimitive/DrawPrimitive for pfDXPipeline's calls.
    struct FixedFunctionDraw
    {
        WGPUBuffer            vertexBuffer = nullptr;
        uint8_t               vertexFormat = 0;    // plGBufferGroup format
        bool                  plateVertex = false; // plPlateVertex instead of a span vertex
        // D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE2(0) (the clothing and blur quads,
        // plDXPipeline::IPreprocessAvatarTextures, ICreateBlurVBuffers), stored as position and
        // (u, v, 1): see Layout().
        bool                  xyzTex1 = false;
        // DrawPrimitiveUP: the vertices themselves, put in the frame's vertex arena.
        const void*           vertexBytes = nullptr;
        uint32_t              vertexBytesSize = 0;
        WGPUBuffer            indexBuffer = nullptr;
        int32_t               baseVertex = 0;
        uint32_t              start = 0; // first index (indexed) or vertex
        uint32_t              count = 0; // indices (indexed) or vertices
        WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
    };
    bool DrawFixedFunction(const FixedFunctionDraw& draw);

    /// One draw with the current D3D9 vertex and pixel programs (fD3D.fVertexShader,
    /// fD3D.fPixelShader), their constant registers and the render state the programmable
    /// pipeline keeps (blend, Z, cull, alpha test, fog). Both programs must be set.
    bool DrawShader(const FixedFunctionDraw& draw);

    /// Pipelines built ahead of their first draw: upstream Metal's build machinery
    /// (plMetalDevice::StartPipelineBuild and PrewarmPipelineStateFor, plMetalDevice.cpp:1083-1201), for the walk
    /// plWGPUPipeline makes over a loaded room's drawables (plWGPUPipeline::PrewarmRoom, IPrewarmWalk).
    /// The main target's formats are known once the canvas has given a texture (CreateNewCommandBuffer).
    bool     CanPrewarm() const { return fCanvasFormatKnown; }
    /// Start building the pipeline that a fixed-function draw, or a shader-program draw, with the current D3D9 state
    /// would use on the main target. Nothing when that pipeline is built or being built. True if a build started.
    bool     PrewarmFixedFunction(const FixedFunctionDraw& draw);
    bool     PrewarmShader(const FixedFunctionDraw& draw);
    /// The bracket of plWGPUPipeline's prewarm walks. Inside it the walk's target is the main target until the walk
    /// pushes a render target: SetRenderTarget then records the target a draw would bind, without binding it
    /// (IPrewarmBindTarget), Clear starts the clear quad's pipeline where the draw would draw that quad
    /// (IPrewarmClear), and the Prewarm entries build for the walk's target.
    void     BeginPrewarmWalk();
    void     EndPrewarmWalk();
    /// In a walk: whether the walk's target can be built for (a render target without a clean device ref cannot).
    bool     PrewarmTargetValid() const { return fWalkTargetValid; }
    bool     InPrewarmWalk() const { return fPrewarmWalkActive; }
    /// Issue the builds still queued, then wait for every build in flight (wgpuInstanceWaitAny, a JSPI suspension
    /// inside a BeginGPUWait/EndGPUWait bracket) and keep the pipelines they made. Called before a scene frame acquires
    /// the canvas (plWGPUPipeline::PrewarmBeforeScene). When the frame loop refuses the wait they stay in flight; a
    /// draw that needs one meanwhile creates it synchronously (PipelineState). waitMs accumulates.
    void     DrainPipelineBuilds(double& waitMs);
    /// The builds' pacing, at every frame start (plWGPUPipeline::BeginRender) and in plClient's wait for them
    /// (IWaitPipelineBuilds); it never suspends. The walks' builds wait in a queue (PrewarmPipelineStateFor), and
    /// the pump keeps at most fPipelineCap of them in flight: it takes the finished builds' pipelines
    /// (wgpuInstanceWaitAny with timeout 0), adapts the cap to how long the client thread was held after builds
    /// were issued (the frame loop's yields, NoteYield, and the frames' submits, NoteHeld), then issues queued builds
    /// up to the cap. A browser that compiles a pipeline on the GPU process's command stream (WebKit) holds the
    /// client thread's next task, a submit or the canvas's present until the builds issued before it are compiled;
    /// the cap keeps that to one build at a time there, and grows to the whole queue where builds compile beside
    /// the stream (Chromium). D3D9 has no pipeline compile (render state is set per
    /// draw) and Metal never paces its builds (plMetalDevice::StartPipelineBuild, plMetalDevice.cpp:1083-1135):
    /// the pacing is the port's own scheduling, and it changes only when a build is issued, never what is built.
    void     PumpPipelineBuilds();
    /// Bumped when the device's objects are released (ReleaseAllDeviceObjects): the pipelines and the builds queued
    /// and in flight belong to one generation.
    uint32_t Generation() const { return fGeneration; }
    /// The builds in flight, and those queued or in flight.
    size_t   PipelineBuildsInFlight() const { return fPipelineBuilds.size(); }
    size_t   PipelineBuildsPending() const { return fPipelineBuilds.size() + fPipelineQueued.size(); }

    /// The frame loop's yields (plEmscriptenFrameLoop's Yield): how long each suspended the client thread.
    static void NoteYield(double waitedMs);
    /// A frame's submit (SubmitCommandBuffer): how long it held the client thread.
    static void NoteHeld(double heldMs);

#ifdef PLASMA_TEST_HOOKS
    /// The last shader draw's uniforms, kept for the rows (fKeepLastDraw).
    bool                 fLastShaderDrawValid;
    std::vector<uint8_t> fLastShaderUniforms; // a plWGPUShaderUniforms
    /// The last fixed-function draw's uniforms, kept while the pipeline records per-draw rows
    /// (fKeepLastDraw); fLastDrawStages is the enabled stage count. Harness build only, with
    /// the pipeline's row arm.
    bool                 fKeepLastDraw;
    bool                 fLastDrawValid;
    uint32_t             fLastDrawStages;
    uint8_t              fLastDrawFog;      // 0 none, 1 table, 2 vertex
    std::vector<uint8_t> fLastDrawUniforms; // a plWGPUFFUniforms
#endif

    /// Read a render target's colour back to the CPU (copyTextureToBuffer +
    /// mapAsync). The bytes arrive in a later frame; TakeReadback hands finished ones over.
    /// A readback owns its map-read buffer. Its map callback holds a reference of its own
    /// (IRequestReadback), so a readback whose owners let go of it lives until its map has ended:
    /// the callback never writes into freed memory.
    struct Readback
    {
        Readback() = default;
        Readback(const Readback&) = delete;
        Readback& operator=(const Readback&) = delete;
        ~Readback()
        {
            if (buffer)
                wgpuBufferRelease(buffer);
        }

        ST::string           name;
        uint32_t             width = 0;
        uint32_t             height = 0;
        WGPUTextureFormat    format = WGPUTextureFormat_Undefined;
        uint32_t             bytesPerRow = 0;
        WGPUBuffer           buffer = nullptr;
        bool                 done = false;
        bool                 failed = false;
        uint32_t             bytesPerPixel = 4;
        std::vector<uint8_t> data; // tightly packed rows (width * bytesPerPixel)
    };
    bool                                   RequestReadback(const ST::string& name, WGPUTexture texture, uint32_t width, uint32_t height,
                                                           WGPUTextureFormat format, uint32_t layer = 0, uint32_t bytesPerPixel = 4);
    /// The same copy and map for ExtractMipMap, kept by the caller (not listed in
    /// fReadbacks): the future to wait on is returned with it.
    std::shared_ptr<Readback>              RequestCaptureReadback(WGPUTexture texture, uint32_t width, uint32_t height,
                                                                  WGPUFuture& future);
    bool                                   TakeReadback(Readback& out);
    std::vector<std::shared_ptr<Readback>> fReadbacks;
    std::shared_ptr<Readback>              IRequestReadback(const ST::string& name, WGPUTexture texture, uint32_t width,
                                                            uint32_t height, WGPUTextureFormat format, uint32_t layer,
                                                            uint32_t bytesPerPixel, WGPUFuture& future);

private:
    struct plWGPUPipelineRecord
    {
        WGPUTextureFormat                    depthFormat;
        WGPUTextureFormat                    colorFormat;
        uint32_t                             sampleCount;
        std::shared_ptr<plWGPUPipelineState> state;

        bool operator==(const plWGPUPipelineRecord& p) const;
    };

    struct plWGPUPipelineRecordHashFunction
    {
        std::size_t operator()(plWGPUPipelineRecord const& s) const noexcept;
    };

    std::unordered_map<plWGPUPipelineRecord, WGPURenderPipeline, plWGPUPipelineRecordHashFunction> fNewPipelineStateMap;
    std::unordered_map<const char*, WGPUShaderModule>                                              fShaderModules; // by WGSL source

    WGPUShaderModule   ShaderModule(const char* source, const char* label);
    WGPUPipelineLayout PipelineLayout(plWGPUBindLayout layout);

    // The render pipeline descriptor of one record, with the storage it points into: the synchronous create
    // (PipelineState) and the asynchronous one (PrewarmPipelineStateFor) build it with the same function.
    struct PipelineDescriptor
    {
        std::vector<WGPUVertexBufferLayout> layouts;
        std::vector<WGPUVertexAttribute>    attributes;
        std::vector<WGPUConstantEntry>      vertexConstants;
        std::vector<WGPUConstantEntry>      fragmentConstants;
        WGPUBlendState                      blend;
        WGPUColorTargetState                target;
        WGPUFragmentState                   fs;
        WGPUDepthStencilState               ds;
        WGPURenderPipelineDescriptor        desc;
    };
    void IPipelineDescriptor(const plWGPUPipelineRecord& record, plWGPUPipelineState* pipelineState, PipelineDescriptor& d);

    // A build in flight (wgpuDeviceCreateRenderPipelineAsync, WaitAnyOnly). Its callback writes only the shared
    // result (as IReadTexture's map); DrainPipelineBuilds moves a delivered pipeline into fNewPipelineStateMap, and a
    // result nobody takes releases its pipeline (~PipelineBuildResult).
    struct PipelineBuildResult
    {
        PipelineBuildResult() = default;
        PipelineBuildResult(const PipelineBuildResult&) = delete;
        PipelineBuildResult& operator=(const PipelineBuildResult&) = delete;
        ~PipelineBuildResult()
        {
            if (pipeline)
                wgpuRenderPipelineRelease(pipeline);
        }

        bool                          done = false;
        WGPUCreatePipelineAsyncStatus status = WGPUCreatePipelineAsyncStatus_Force32;
        WGPURenderPipeline            pipeline = nullptr;
        std::string                   message;
    };
    struct PipelineBuild
    {
        WGPUFuture                           future{};
        std::shared_ptr<PipelineBuildResult> result;
        uint32_t                             generation = 0;     // fGeneration at the start
        double                               startMs = 0.0;      // queued
        bool                                 duplicated = false; // a draw created the pipeline while this was in flight
#ifdef PLASMA_TEST_HOOKS
        uint32_t serial = 0; // the trace's build number
#endif
    };
    std::unordered_map<plWGPUPipelineRecord, PipelineBuild, plWGPUPipelineRecordHashFunction> fPipelineBuilds;
    uint32_t                                                                                  fGeneration;
    bool                                                                                      fCanvasFormatKnown;

    // The builds waiting to be issued (PumpPipelineBuilds), in the order the walks asked for them. A draw that needs
    // one takes it out of fPipelineQueued (PipelineState); its entry in the order is then skipped.
    struct PipelineQueued
    {
        uint32_t generation = 0; // fGeneration when queued
        double   queuedMs = 0.0;
#ifdef PLASMA_TEST_HOOKS
        uint32_t serial = 0;
#endif
    };
    std::deque<plWGPUPipelineRecord>                                                           fPipelineQueue;
    std::unordered_map<plWGPUPipelineRecord, PipelineQueued, plWGPUPipelineRecordHashFunction> fPipelineQueued;

    // The pacing (PumpPipelineBuilds): the builds kept in flight, and whether builds were issued since the cap was
    // last adapted (the yields after an issue decide it).
    uint32_t fPipelineCap = 1;
    uint32_t fPaceIssued = 0;

    // The pump's steps: the finished builds' results, and one queued build's start.
    void ICollectPipelineBuilds();
    bool IIssuePipelineBuild(const plWGPUPipelineRecord& record, const PipelineQueued& queued);
#ifdef PLASMA_TEST_HOOKS
    // The trace's builds: their numbers, and the prewarmed pipelines no draw has asked for yet (by record, the build's
    // number), so the first draw that uses one is seen (plWGPUPipelineObserver::OnPipelineBuild, kBuildUsed).
    uint32_t                                                                             fPipelineBuildSerial = 0;
    std::unordered_map<plWGPUPipelineRecord, uint32_t, plWGPUPipelineRecordHashFunction> fPrewarmUnused;
#endif

    // The key and fixed state of a fixed-function draw (DrawFixedFunction's), and of a shader-program draw on a target
    // whose depth format is depthFormat (DrawShader's; both programs set), from the current D3D9 state. The draws and
    // the prewarm compute their pipelines with these.
    void IFixedFunctionKey(const FixedFunctionDraw& draw, plWGPUFixedFunctionKey& key, plWGPUFixedState& fixed) const;
    void IShaderKey(const FixedFunctionDraw& draw, WGPUTextureFormat depthFormat, plWGPUShaderKey& key,
                    plWGPUFixedState& fixed) const;
    // plMetalDevice::PrewarmPipelineStateFor (plMetalDevice.cpp:1183) with the test it means: start a build when the
    // record is neither built nor in flight (Metal's test, plMetalDevice.cpp:1196, starts one only when a build is
    // already in flight). The record is the walk's target's (formats and sample count): the main target's outside a
    // render target the walk pushed. The build is queued with the record's own state (a clone, as
    // plMetalDevice::StartPipelineBuild builds from record.state, plMetalDevice.cpp:1095); PumpPipelineBuilds issues it.
    bool PrewarmPipelineStateFor(plWGPUPipelineState* pipelineState);

    // The walk's target (BeginPrewarmWalk). fWalkTargetValid is false for a render target whose device ref is missing
    // or dirty: the walk makes no ref (MakeRenderTargetRef creates textures), so that target's draws build at the
    // draw.
    void              IPrewarmBindTarget(plRenderTarget* target);
    void              IPrewarmClear(bool shouldClearColor, bool shouldClearDepth);
    bool              fPrewarmWalkActive = false;
    bool              fWalkTargetValid = true;
    plRenderTarget*   fWalkTarget = nullptr; // nullptr: the main target
    WGPUTextureFormat fWalkColorFormat = WGPUTextureFormat_Undefined;
    WGPUTextureFormat fWalkDepthFormat = WGPUTextureFormat_Undefined;
    uint32_t          fWalkSampleCount = 1;
    uint32_t          fWalkWidth = 0;
    uint32_t          fWalkHeight = 0;

protected:
    WGPURenderPipeline PipelineState(plWGPUPipelineState* pipelineState);

private:
    WGPUTextureFormat fFramebufferFormat;
    WGPUTextureFormat fDepthFormat;

    // these are internal bits for backing the current render pass
    WGPUCommandEncoder    fCurrentCommandBuffer;          // the canvas frame
    WGPUCommandEncoder    fCurrentOffscreenCommandBuffer; // the current offscreen target
    WGPURenderPassEncoder fCurrentRenderTargetCommandEncoder;

    WGPUTexture     fCurrentDrawable; // the canvas texture of this frame
    WGPUTextureView fCurrentDrawableView;
    WGPUTexture     fCurrentDrawableDepthTexture;
    WGPUTextureView fCurrentDrawableDepthView;
    WGPUTexture     fCurrentFragmentMSAAOutputTexture;
    WGPUTextureView fCurrentFragmentMSAAOutputView;
    uint32_t        fDrawableWidth;
    uint32_t        fDrawableHeight;

    // the current target's attachments
    WGPUTextureView   fCurrentFragmentOutputView;
    WGPUTextureView   fCurrentDepthView;
    WGPUTextureFormat fCurrentColorFormat;
    WGPUTextureFormat fCurrentDepthFormat;
    uint32_t          fCurrentTargetWidth;
    uint32_t          fCurrentTargetHeight;
    plRenderTarget*   fCurrentRenderTarget;

    // A clear requested before the pass opened becomes its load operation
    // (plMetalDevice::Clear).
    bool  fShouldClearColor;
    bool  fShouldClearDepth;
    float fClearColor[4];
    float fClearDepth;

    // The viewport (D3D9 keeps it across passes; WebGPU resets it per pass).
    float fViewport[4];

    WGPUSampler fSamplerStates[4];
    WGPUSampler fPointSampler; // plDXTextFont's POINT/POINT/NONE

    WGPUBindGroupLayout fUniformBindGroupLayout;
    WGPUBindGroupLayout fTextureBindGroupLayout;
    WGPUBindGroupLayout fFixedTextureBindGroupLayout;
    WGPUPipelineLayout  fUniformsOnlyLayout;
    WGPUPipelineLayout  fUniformsAndTextureLayout;
    WGPUPipelineLayout  fFixedFunctionLayout;

    // Fixed-function draws: the zero vertex buffer that feeds absent FVF components, a
    // white cube map for stages without a cube texture, samplers by D3D sampler state, bind
    // groups by texture views and samplers, pipelines by packed key and target formats.
    WGPUBuffer                                              fZeroVertexBuffer;
    WGPUTexture                                             fWhiteCubeTexture;
    WGPUTextureView                                         fWhiteCubeTextureView;
    std::map<std::array<plWGPUD3D9::DWORD, 7>, WGPUSampler> fD3DSamplers;
    std::map<std::array<void*, 24>, WGPUBindGroup>          fFixedBindGroups;
    uint32_t                                                fFixedBindGroupSerial;
    std::unordered_map<uint64_t, WGPURenderPipeline>        fFixedPipelines[3]; // [span, plate, XYZ|TEX1]
    uint32_t                                                fFixedTransformSerial;
    float                                                   fFixedWorldView[2][16];
    float                                                   fFixedNormal[2][16];

    WGPUSampler   ID3DSampler(plWGPUD3D9::DWORD stage);
    WGPUBindGroup IFixedBindGroup(const std::array<void*, 24>& key);

    // Shader-program draws: pipelines by packed key and target formats.
    std::unordered_map<uint64_t, WGPURenderPipeline> fShaderPipelines;
    WGPUPipelineLayout                               fShaderLayout;
    WGPURenderPipeline                               IShaderPipeline(const plWGPUShaderKey& key, const plWGPUFixedState& fixed, uint32_t& missing);
    bool                                             IShaderUniformsAndTextures(plWGPUShaderUniforms& u, std::array<void*, 24>& textures);

    WGPUTexture     fWhiteTexture; // bound where a shader samples but the stage has no texture
    WGPUTextureView fWhiteTextureView;

    // Per-frame arenas (see BindUniforms/BindVertexBytes).
    struct Arena
    {
        WGPUBuffer           buffer = nullptr;
        WGPUBindGroup        group = nullptr; // uniform arenas only
        uint64_t             size = 0;
        uint64_t             used = 0;
        // Uniform arenas are staged on the CPU and written with one queue.writeBuffer per
        // arena before each submit (IFlushUniforms), not one per draw.
        std::vector<uint8_t> shadow;
        uint64_t             flushed = 0;
    };
    std::vector<Arena>         fUniformArenas;
    std::vector<Arena>         fVertexArenas;
    std::vector<WGPUBindGroup> fFrameBindGroups;
    bool                       IArenaAlloc(std::vector<Arena>& arenas, bool uniform, size_t size, size_t align,
                                           WGPUBuffer& buffer, uint64_t& offset, WGPUBindGroup* group);
    void                       IResetArenas();
    void                       IFlushUniforms();

    bool fSupportsDXTTextures;
    bool fDoesSmallTextures; // kCapsDoesSmallTextures of the selected device record

    void BeginNewRenderPass();
    void ReleaseSamplerStates();
    void IEndRenderPass();
    void ISubmitOffscreen();
    void IApplyViewport(WGPURenderPassEncoder pass);

    // Textures (D3D9 IGetD3DTextureFormat / IFormatTextureData)
    plMipmap* IUploadSource(TextureRef* tRef, plMipmap* img);
    int32_t   IAllowedLevels(plMipmap* mipmap) const;
    /// The software codec's decode of zero source bytes: a zero DXT block and a zero raw level
    /// pixel (the levels hsDXTSoftwareCodec stores uncompressed), in the decoded mipmap's pixel
    /// format. They fill the rows below a non-square cube face (IUploadLevels).
    struct CubeZeroPixels
    {
        std::vector<uint8_t> block;
        std::vector<uint8_t> raw;
    };
    bool ICubeZeroDecode(plMipmap* compressedFace, CubeZeroPixels& out);
    /// dstLevelOffset: level lvl of src goes to GPU level lvl + dstLevelOffset (the shipped
    /// levels below a texture pack's HD levels); 0 for every other caller.
    void IUploadLevels(TextureRef* tRef, plMipmap* src, uint32_t layer, int32_t levels,
                       plMipmap* sizeSource, std::vector<uint8_t>* level0Bytes = nullptr, uint32_t cubeEdge = 0,
                       const CubeZeroPixels* zeroPixels = nullptr, uint32_t dstLevelOffset = 0);
    /// The optional HD texture pack's upload (plWGPUTexturePack): true when the texture was
    /// created and filled from the pack, false when the shipped path must run.
    bool IUploadPackTexture(TextureRef* tRef, plMipmap* img);
    bool fPackNoBCLogged = false;
#ifdef PLASMA_TEST_HOOKS
public:
    /// The pipeline's fRowsArmed for this frame: the pack never uploads while rows record.
    /// Harness build only, with the row arm.
    bool fRowsArmedMirror = false;

private:
#endif
    /// D3DUSAGE_AUTOGENMIPMAP: the sublevels of a kAutoGenMipmap
    /// texture, generated from its top level after each fill (2x2 box, D3DTEXF_LINEAR).
    void IGenerateAutoMipmaps(TextureRef* tRef, const std::vector<uint8_t>& level0, uint32_t width,
                              uint32_t height, int32_t levels);

public:
    uint32_t fAutoGenMultiLevel; // kAutoGenMipmap on a texture with more than one level
private:
    bool ICreateTexture(TextureRef* tRef, uint32_t width, uint32_t height, uint32_t layers, int32_t levels);

    // The front buffer: the final colour of the last primary frame that presented, after the
    // passes (and their MSAA resolve), before the gamma ramp (D3D9's ramp acts at scan-out,
    // downstream of the front buffer). It is what IDirect3DDevice9::GetFrontBufferData reads
    // (plDXPipeline::CaptureScreen). A postprocessed frame stays in fUnprocessedTexture until the
    // next frame's passes, so that texture is the front buffer. A frame drawn straight to the
    // canvas, until the client sets the first gamma table (pfConsoleCommands.cpp, Graphics.Gamma2),
    // is copied into fFrontBufferCopy at SubmitCommandBuffer: the canvas texture expires when the
    // task ends.
    // fFrontBuffer holds a reference of its own: a texture rebuilt at the start of the next frame
    // still holds the last presented one.
    WGPUTexture fFrontBuffer;
    WGPUTexture fFrontBufferCopy;
    void        ISetFrontBuffer(WGPUTexture frame);
    void        IReleaseFrontBuffer();

    // Gamma at present, Metal's post-processing structure (plMetalDevice::NeedsPostprocessing,
    // CreateNewCommandBuffer, PostprocessIntoDrawable) with the non-tiler gammaCorrectFragment
    // (GammaCorrection.metal). Once a table exists the canvas passes render (and resolve)
    // into fUnprocessedTexture, the front buffer; SubmitCommandBuffer draws the LUT pass from it
    // into the canvas texture. The table texture is 256x3 R16Uint.
    WGPUTexture         fGammaLUTTexture;
    WGPUTextureView     fGammaLUTView;
    WGPUTexture         fUnprocessedTexture;
    WGPUTextureView     fUnprocessedView;
    WGPUBindGroupLayout fGammaBindGroupLayout;
    WGPUPipelineLayout  fGammaPipelineLayout;
    WGPURenderPipeline  fGammaPipeline;
    WGPUTextureFormat   fGammaPipelineFormat; // the pipeline's target format (cache key)
    WGPUBindGroup       fGammaBindGroup;
    WGPUTextureView     fGammaBindGroupInput; // the views fGammaBindGroup was made for
    WGPUTextureView     fGammaBindGroupLUT;
    bool                fFramePostprocessed; // this frame's canvas passes render into fUnprocessedTexture
    void                PostprocessIntoDrawable();
    void                ICreateGammaAdjustState();
    void                IReleaseGammaDeviceObjects();

    // A texture's texels waited on and read back (ReadFrontBuffer).
    bool IReadTexture(WGPUTexture texture, uint32_t width, uint32_t height, WGPUTextureFormat format,
                      std::vector<uint32_t>& texels, double& waitMs, const char* how);

public:
    /// The stored ramp becomes the device's table (plMetalPipeline::SetGamma's
    /// fGammaLUTTexture; 256 entries, as DX). From the first call
    /// on, every presenting frame runs the LUT pass (no identity shortcut).
    void SetGammaTable(const uint16_t (&table)[3][256]);
    bool NeedsPostprocessing() const { return fGammaLUTTexture != nullptr; }

    /// CaptureScreen's read of the front buffer: copy fFrontBuffer to a
    /// map-read buffer (rows padded to 256 bytes), wait for the map (wgpuInstanceWaitAny, a JSPI
    /// suspension bracketed by BeginGPUWait/EndGPUWait), and return the rows top to bottom as
    /// kARGB32Config texels (B, G, R, A in memory; swizzled from rgba8unorm when the canvas format
    /// is not bgra8unorm), alpha opaque. False when no frame has been presented yet, when the
    /// wait is not allowed here, or when the map fails.
    bool ReadFrontBuffer(std::vector<uint32_t>& texels, uint32_t& width, uint32_t& height, double& waitMs);
    /// wgpuInstanceWaitAny on one future, suspending the tick, inside a
    /// BeginGPUWait/EndGPUWait bracket: the frame loop's procs (plWGPUPipeline::SetGPUWaitProcs). BeginGPUWait is
    /// false, and the caller must not wait, when the loop refuses or no procs are registered. waitMs accumulates.
    bool BeginGPUWait(const char* how);
    void EndGPUWait();
    bool WaitFuture(WGPUFuture future, double& waitMs, const char* how);

private:
    static bool (*fGPUWaitBegin)(const char* how);
    static void (*fGPUWaitEnd)();

public:
#ifdef PLASMA_TEST_HOOKS
    /// The upload ledger: the queue writes by class and owner, and the refills DX never makes.
    /// DX's per-frame writers are the skinned and volatile vertex buffers
    /// (plDXPipeline::IRefreshDynVertices, ISoftwareVertexBlend), dirty index buffers
    /// (ICheckDynBuffers, CheckIndexBufferRef) and dirty textures (IHandleTextureStage); static
    /// vertex buffers are never refilled (CheckVertexBufferRef), so a static refill or a not-dirty
    /// texture upload is logged ("re-upload").
    /// Harness build only: the ledger is a test diagnostic with no plDXPipeline counterpart, and
    /// its fill maps are keyed by object pointers, so the product carries none of it (the call
    /// sites compile out with it). Known limit: the fill maps are cleared only at device loss, so
    /// after a link a reused address can read as a refill.
    enum UploadClass
    {
        kUploadStaticVB,       // FillVertexBufferRef: the first fill of a (group, buffer)
        kUploadStaticVBRefill, // a static (group, buffer) filled again: a bug in this pipeline
        kUploadStaticIB,       // the first fill of a (group, index buffer)
        kUploadDirtyIB,        // a later fill of a dirty index buffer (DX refills the dirty range)
        kUploadVolatileVB,     // IRefreshDynVertices (skinned and volatile groups)
        kUploadTexture,        // plBitmap uploads, by key and reason (first / dirty / no-texture / not-dirty)
        kUploadUniform,        // uniform arenas and BindVertexBytes (WebGPU only)
        kUploadOther,          // the device's own one-off objects (white textures, plate quad, blur quad, ULut, fonts)
        kNumUploadClasses
    };
    // The owner context the pipeline sets around a drawable's buffer checks.
    const void*                                          fUploadDrawable; // plDrawableSpans*
    uint32_t                                             fUploadGroup;
    const char*                                          fUploadTextureReason;
    std::map<std::pair<const void*, uint32_t>, uint32_t> fFilledVB; // (group, buffer) -> fills
    std::map<std::pair<const void*, uint32_t>, uint32_t> fFilledIB;
    std::map<const void*, uint32_t>                      fFilledTextures; // plBitmap -> uploads
    mutable std::map<const void*, ST::string>            fGroupOwners;    // group -> the drawable last seen with it
    void                                                 NoteGroupOwner(const void* group);
    void                                                 NoteUpload(UploadClass c, uint64_t bytes, const ST::string& owner, const char* reason);
    ST::string                                           UploadDrawableOwner(const void* group, uint32_t idx, const char* what) const;
    static const char*                                   UploadClassName(UploadClass c);
#endif

    /// Drop every WebGPU object of the old device (lost device): pipelines, shader
    /// modules, samplers, bind groups and layouts, arenas, the frame's attachments, the front
    /// buffer, the readbacks (they fail) and the white textures.
    void ReleaseAllDeviceObjects();
};

#endif
