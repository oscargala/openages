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

// The pipeline test seam: what the private test harness's render instruments see of plWGPUPipeline's frame.
// The product build (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing there, and every call site
// is under #ifdef PLASMA_TEST_HOOKS. In a harness build the pointer is null until plTestHooks::Install sets it, and
// every default does nothing. The overlay's observer (plTestHooksPipelineObserver) is a friend of plWGPUPipeline, so
// it reads the pipeline's state itself; the arguments carry only what the call site holds locally. Every member runs
// on the client thread.
#ifndef _plWGPUPipelineObserver_inc_
#define _plWGPUPipelineObserver_inc_

#ifdef PLASMA_TEST_HOOKS

#include <webgpu/webgpu.h>

#include <cstdint>
#include <string>

#include "plWGPUDevice.h"

class plClothingOutfit;
class plDrawableSpans;
class plPageTreeMgr;
class plRenderRequest;
class plSceneNode;
class plMipmap;
class plRenderTarget;
class plShadowSlave;
class plWGPUPipeline;
class plWGPUPipelineState;
class plWGPURenderTargetRef;

// One draw as the pipeline submitted it: the draw descriptor, and for a shader-program draw the vertex count its
// row reads (plWGPUPipeline::IDrawFixedFunction and IDrawShader).
struct plWGPUDrawView
{
    const plWGPUDevice::FixedFunctionDraw* draw = nullptr;
    bool                                   shader = false; // DrawShader (vertex and pixel programs), not DrawFixedFunction
    uint32_t                               numVerts = 0;   // shader draws only: the span's vertex count (plWGPURenderTriListFunc)
};

// One step of the avatar texture composites (plWGPUPipeline::IPreprocessAvatarTextures and IDrawClothingQuad, as
// plDXPipeline's): the outfit being composed, its target, and for a quad IDrawClothingQuad's arguments.
struct plWGPUClothingView
{
    plClothingOutfit* outfit = nullptr;                                           // kOutfitBegin, kOutfitEnd
    plRenderTarget*   target = nullptr;                                           // kOutfitBegin, kOutfitEnd
    float             x = 0.f, y = 0.f, w = 0.f, h = 0.f, uOff = 0.f, vOff = 0.f; // kQuad
    plMipmap*         texture = nullptr;                                          // kQuad
};

class plWGPUPipelineObserver
{
public:
    // The steps of one shadow slave in plWGPUPipeline::IPreprocessShadows.
    enum ShadowMapStep
    {
        kSlavePrepared, // before any slave is cast (the slave's prepared fields)
        kSlaveCast,     // its shadow map was rendered
        kSlaveFailed,   // IRenderShadowCaster refused it; it leaves this frame's list
    };

    // The steps of plWGPUPipeline::IPreprocessAvatarTextures.
    enum ClothingStep
    {
        kComposeBegin, // there are outfits this frame, before the first one is looked at
        kOutfitBegin,  // one outfit is composed: its target is chosen, before the base quad
        kQuad,         // one IDrawClothingQuad, before its draw
        kOutfitEnd,    // the outfit's quads were drawn and its target popped
    };

    virtual ~plWGPUPipelineObserver() = default;

    // At the primary BeginRender, before the frame's counters are cleared: the render dump's settle rule, which
    // reads the previous frame's passes and arms this frame's rows once the scene has settled.
    virtual void OnBeginRender(plWGPUPipeline& pipeline) {}

    // After a draw the device accepted, on a frame whose rows are armed: record its per-draw row, or for a
    // shader-program draw its shader row, its CPU model and its target pass.
    virtual void OnDraw(plWGPUPipeline& pipeline, const plWGPUDrawView& view) {}

    // When a render target's device ref is released (plWGPURenderTargetRef::Release; the target is deleted or
    // resized, or the device is reset): the shader rows drop a target pass recorded with that ref, so their
    // readbacks never reach it.
    virtual void OnRenderTargetRelease(plWGPURenderTargetRef* ref) {}

    // At each step of a shadow slave, on a frame whose rows are armed: record its slave row and read its cast back
    // (the per-draw rows' shadow-slave fields).
    virtual void OnShadowMap(plWGPUPipeline& pipeline, plShadowSlave* slave, ShadowMapStep step) {}

    // At each step of the avatar texture composites: their bias lines, and the CPU recomposition of the outfits whose
    // targets are read back (the per-draw rows' clothing quads).
    virtual void OnClothing(plWGPUPipeline& pipeline, ClothingStep step, const plWGPUClothingView& view) {}

    // At the primary EndRender, after the frame was submitted: the readbacks that have completed, the GFX settings
    // line when a setting changed, and on the dump frame the render dump with its rows.
    virtual void OnEndRender(plWGPUPipeline& pipeline) {}

    // Once per frame, when the frame's canvas texture is acquired (plWGPUEnumerate::NoteCanvasTexture). newTexture:
    // its serial differs from the last frame's, so the canvas has presented the previous frame (the frame counters).
    virtual void OnPresent(bool newTexture) {}

    // Once a second, from the client loop's report (plWGPUEnumerate::LogStats): the STATS line.
    virtual void OnLoopReport() {}

    // When the device has created a render pipeline (plWGPUDevice::PipelineState, a cache miss), right after the
    // create call: the state, the target's formats and sample count, whether the state's WGSL module was created for
    // it, and the call's own time on the client thread.
    virtual void OnPipelineCreated(plWGPUDevice& device, const plWGPUPipelineState& state, WGPUTextureFormat color,
                                   WGPUTextureFormat depth, uint32_t sampleCount, bool newModule, double clientMs) {}

    // When the device creates a WGSL module (plWGPUDevice::ShaderModule, before the create): a harness run may hand
    // the module a per-run variant of its source in `salted` (PLASMA_TEST_SHADER_SALT), so the browser's GPU process
    // compiles every program anew, as on a first visit, whatever its own and the system's shader caches hold. The
    // variant draws exactly what the source draws. Returns false to keep the source.
    virtual bool SaltShaderSource(const char* source, std::string& salted) { return false; }

    // The steps of a pipeline built ahead of its first draw (plWGPUDevice::PrewarmPipelineStateFor,
    // PumpPipelineBuilds, DrainPipelineBuilds and PipelineState).
    enum PipelineBuildStep
    {
        kBuildStarted,   // a walk asked for it and it was queued (ms: the request's own time on the client)
        kBuildReady,     // the pump or the drain stored its pipeline (ms: from the start)
        kBuildFailed,    // it ended without a pipeline; the draw will create it synchronously (ms: from the start)
        kBuildDuplicate, // a draw needed it while it was in flight and created it synchronously (ms: from the start)
        kBuildDropped,   // its pipeline came after a draw had created one, or from a released device (ms: from the start)
        kBuildUsed,      // the first draw that asked for its pipeline
        kBuildIssued,    // wgpuDeviceCreateRenderPipelineAsync was called (ms: the call's own time on the client)
        kBuildTaken,     // a draw needed it while it was queued and created it synchronously (ms: from the start)
    };

    // At each step of such a build: its number (in start order) and the state.
    virtual void OnPipelineBuild(plWGPUDevice& device, const plWGPUPipelineState& state, PipelineBuildStep step,
                                 uint32_t serial, double ms) {}

    // When the pacing changes its cap (plWGPUDevice::PumpPipelineBuilds): the old and new cap, the frame loop's
    // yields since the builds were issued (their count) and the longest hold in a yield or a frame's submit (ms), the
    // builds that were issued, and the builds queued and in flight now.
    virtual void OnPipelinePace(plWGPUDevice& device, uint32_t oldCap, uint32_t newCap, uint32_t yields,
                                double yieldMaxMs, uint32_t issued, uint32_t queued, uint32_t inFlight) {}

    // One prewarm step of plWGPUPipeline (PrewarmRoom, PrewarmBeforeScene and the frame start, IPrewarmFrameStart):
    // a walk (the drawables walked, the builds started and the walk's time), a drain (the builds waited for and the
    // wait), plClient's wait drawing progress frames (IWaitPipelineBuilds), or a scene frame not drawn because its
    // builds are pending after a load (PrewarmBeforeScene); skipped names why a walk did not run (nullptr when it ran).
    // The builds counted are those queued or in flight.
    struct PrewarmView
    {
        const char* seat = nullptr;   // "room", "pending rooms", "deferred rooms", "re-walk", "joined", "targets",
                                      // "outfits", "requests", "drain", "wait" or "dropped"
        const char* detail = nullptr; // the room's name, the re-walk's cause; for a wait or a drop, the client's state
        const char* skipped = nullptr;
        uint32_t    drawables = 0;      // drawables given to the walk
        uint32_t    walkedBefore = 0;   // of them, walked before under the same conditions
        uint32_t    spans = 0;          // spans walked
        uint32_t    requests = 0;       // draws the walk would make (the dry render functor's calls)
        uint32_t    started = 0;        // builds started
        uint32_t    drained = 0;        // drain, wait, drop: the builds pending before
        uint32_t    inFlightAfter = 0;  // builds still pending after the step
        uint32_t    opacityPasses = 0;  // passes walked though their opacity skips them now (an opacity animation's)
        uint32_t    opacityStarted = 0; // builds started for those
        uint32_t    frames = 0;         // wait: the progress frames it drew
        double      walkMs = 0.0;
        double      drainMs = 0.0; // the wait

        // Of the spans, those on a volatile vertex buffer the draw has not filled yet
        // (plWGPUPipeline::IPrewarmSpanFormat): a skinned buffer's, a particle span's, another volatile buffer's.
        uint32_t unfilledSkinned = 0;
        uint32_t unfilledParticle = 0;
        uint32_t unfilledOther = 0;
    };
    virtual void OnPrewarm(plWGPUPipeline& pipeline, const PrewarmView& view) {}

    // One render request of the requests seat (plWGPUPipeline::IPrewarmRequests), walked as plRenderRequest::Render
    // draws it: its target and whether the walk could build for it (a target without a clean device ref cannot), its
    // page tree (its own or plClient's), what was walked and started; skipped names why it was not walked.
    struct PrewarmRequestView
    {
        const plRenderRequest* request = nullptr;
        const plPageTreeMgr*   pageMgr = nullptr;
        bool                   unqueued = false; // a walked room's waveset's, before the waveset queues it
        bool                   ownPageMgr = false;
        bool                   walkTargetValid = true;
        const char*            skipped = nullptr;
        uint32_t               spans = 0;
        uint32_t               requests = 0;
        uint32_t               started = 0;
        uint32_t               opacityPasses = 0;
        uint32_t               opacityStarted = 0;
        uint32_t               unfilledSkinned = 0; // as PrewarmView's
        uint32_t               unfilledParticle = 0;
        uint32_t               unfilledOther = 0;
        uint32_t               outfits = 0;  // the submitted clothing outfits
        uint32_t               standIns = 0; // of them, those whose target layer held the walk's stand-in
        double                 walkMs = 0.0;
    };
    virtual void OnPrewarmRenderRequest(plWGPUPipeline& pipeline, const PrewarmRequestView& view) {}

    // At a frame start, each drawable of a walked room that joined its draw pool since the room was walked
    // (spansBefore -1), or whose span count changed since (spansBefore its count then): the joined seat walks it.
    virtual void OnPrewarmJoined(plWGPUPipeline& pipeline, plSceneNode* node, plDrawableSpans* drawable, int spansBefore) {}

    // After a scene frame's drain that followed a walk: the loaded drawables (every loaded page's plDrawableSpans) and
    // those not in the walked set, with the pages they are in ("page (n), ..."): what the room seat did not cover.
    virtual void OnPrewarmCensus(plWGPUPipeline& pipeline, uint32_t loaded, uint32_t notWalked, const char* pages) {}

    // After OnPrewarmCensus, each drawable it found not walked (the first 16): the trace names it and where its scene
    // node stands in the room seat.
    virtual void OnPrewarmCensusDrawable(plWGPUPipeline& pipeline, plDrawableSpans* drawable) {}

    // A walk's request for a pipeline (plWGPUDevice::PrewarmPipelineStateFor), whatever became of it: the trace keeps
    // the walk's key for each pass, so a later draw-time creation of the same pass names the key the walk asked for
    // instead, or that no walk reached that pass.
    enum PrewarmRequestResult
    {
        kRequestNoTarget, // the walk's target has no formats: nothing could be built
        kRequestKnown,    // built, queued or in flight already
        kRequestStarted,  // a build was started (queued: the pacing issues it, plWGPUDevice::PumpPipelineBuilds)
        kRequestRefused,  // the async create returned no future: the draw creates it (not reported since the pacing
                          // issues builds outside the walk)
    };
    virtual void OnPrewarmRequest(plWGPUDevice& device, const plWGPUPipelineState& state, PrewarmRequestResult result) {}

    // When the device has the pipeline key and the fragment program of a fixed-function draw
    // (plWGPUDevice::DrawFixedFunction) or of a walk's dry draw (plWGPUDevice::PrewarmFixedFunction), before the
    // pipeline is looked up: the observer reads the rest of the D3D9 state the draw's uniforms carry (plWGPUD3DDevice)
    // itself. walk: the prewarm's dry draw. program: 0 for the uber shader; sig: the draw's program signature
    // (plWGPUFixedFragment::Signature), whatever program it runs.
    virtual void OnFixedFunctionKey(plWGPUDevice& device, const plWGPUFixedFunctionKey& key,
                                    const plWGPUFixedState& fixed, bool walk, uint32_t program,
                                    const plWGPUFixedFragmentSig& sig) {}

    // When the device first sees a fixed-function program signature (plWGPUDevice::IFixedProgram): the id it gave the
    // program and its module text, or 0 and nullptr when the signature is outside the fallback domain (inDomain false)
    // and its draws keep the uber shader.
    virtual void OnFixedProgram(plWGPUDevice& device, const plWGPUFixedFragmentSig& sig, uint32_t program,
                                const char* text, bool inDomain) {}

    // Read by RenderScreenElements: true while the frame is to be drawn without plates and debug text, so a
    // screenshot films the scene alone.
    virtual bool SceneCaptureFrame() const { return false; }

    // Asked by plWGPUEnumerate's adapter callback only for an adapter the product keeps on the uber shader whose
    // specialised fixed-function programs a harness run may measure: WebKit on Metal (vendor and architecture
    // "apple"). True runs that adapter's fixed-function draws on the specialised programs. Asked again on a device
    // re-acquire, so the answer must not change within a run.
    virtual bool AdmitFixedPrograms(const char* vendor, const char* architecture) { return false; }

    static plWGPUPipelineObserver* Get() { return fObserver; }
    static void                    Set(plWGPUPipelineObserver* observer) { fObserver = observer; }

private:
    static inline plWGPUPipelineObserver* fObserver = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _plWGPUPipelineObserver_inc_
