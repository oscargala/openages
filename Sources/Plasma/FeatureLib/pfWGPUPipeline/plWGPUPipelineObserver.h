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

// The pipeline test seam: what a harness build's render instruments (testhooks/wgpu/) see of plWGPUPipeline's frame.
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

#include "plWGPUDevice.h"

class plClothingOutfit;
class plMipmap;
class plRenderTarget;
class plShadowSlave;
class plWGPUPipeline;
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

    // Read by RenderScreenElements: true while the frame is to be drawn without plates and debug text, so a
    // screenshot films the scene alone.
    virtual bool SceneCaptureFrame() const { return false; }

    static plWGPUPipelineObserver* Get() { return fObserver; }
    static void                    Set(plWGPUPipelineObserver* observer) { fObserver = observer; }

private:
    static inline plWGPUPipelineObserver* fObserver = nullptr;
};

#endif // PLASMA_TEST_HOOKS

#endif // _plWGPUPipelineObserver_inc_
