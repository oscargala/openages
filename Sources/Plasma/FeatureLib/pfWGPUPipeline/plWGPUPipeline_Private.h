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

#ifndef _plWGPUPipeline_Private_inc_
#define _plWGPUPipeline_Private_inc_

// What plWGPUPipeline's source files (plWGPUPipeline.cpp, plWGPUPipelineShadows.cpp,
// plWGPUPipelineAvatar.cpp) share and the library keeps to itself.

#include "HeadSpin.h"
#include "plProfile.h"
#include "plWGPUPipeline.h"

// plDXPipeline.cpp's profile counters (plWGPUPipeline.cpp creates the first two)
plProfile_Extern(DrawFeedTriangles);
plProfile_Extern(DrawPrimStatic);
plProfile_Extern(DrawTriangles);

// plRenderPrimFunc (plDXPipeline.h)
class plWGPURenderPrimFunc
{
public:
    virtual bool RenderPrims() const = 0; // return true on error
};

// plRenderTriListFunc (plDXPipeline.cpp): DrawIndexedPrimitive(TRIANGLELIST,
// BaseVertexIndex = the index ref's fOffset, ..., StartIndex, PrimCount).
class plWGPURenderTriListFunc : public plWGPURenderPrimFunc
{
protected:
    plWGPUPipeline*        fPipe;
    plWGPUVertexBufferRef* fVRef;
    plWGPUIndexBufferRef*  fIRef;
    int                    fBaseVertexIndex;
    int                    fVStart;
    int                    fVLength;
    int                    fIStart;
    int                    fNumTris;

public:
    plWGPURenderTriListFunc(plWGPUPipeline* pipe, plWGPUVertexBufferRef* vRef, plWGPUIndexBufferRef* iRef,
                            int baseVertexIndex, int vStart, int vLength, int iStart, int iNumTris)
        : fPipe(pipe), fVRef(vRef), fIRef(iRef), fBaseVertexIndex(baseVertexIndex), fVStart(vStart), fVLength(vLength), fIStart(iStart), fNumTris(iNumTris)
    {
    }

    bool RenderPrims() const override
    {
        // A pass that asks for an unsupported shader program: logged at ISetShaders and not
        // drawn.
        if (fPipe->fUnsupportedProgramPass)
            return false;
        // The material passes are the counted passes (the render dump's render_ops rows);
        // projections, shadows and the other draws have their own counters.
        if (fPipe->fRowCtx.kind == plWGPUPipeline::kRowBase || fPipe->fRowCtx.kind == plWGPUPipeline::kRowAux)
            fPipe->ICountPass();
        fPipe->fRowCtx.vStart = fVStart;
        fPipe->fRowCtx.vLength = fVLength;
        fPipe->fRowCtx.iStart = fIStart;
        fPipe->fRowCtx.iLength = fNumTris * 3;

        // As plRenderTriListFunc::RenderPrims (Stats.Show "Polys")
        plProfile_IncCount(DrawFeedTriangles, fNumTris);
        plProfile_IncCount(DrawTriangles, fNumTris);
        plProfile_Inc(DrawPrimStatic);

        plWGPUDevice::FixedFunctionDraw draw;
        draw.vertexBuffer = fVRef->GetBuffer();
        draw.vertexFormat = fVRef->fFormat;
        draw.plateVertex = false;
        draw.indexBuffer = fIRef->GetBuffer();
        draw.baseVertex = fBaseVertexIndex;
        draw.start = fIStart;
        draw.count = fNumTris * 3;
        draw.topology = WGPUPrimitiveTopology_TriangleList;

        // DrawIndexedPrimitive runs the programs set on the device (ISetShaders), or the
        // fixed-function stages when none is set.
        const plWGPUD3DDevice* d3d = fPipe->fD3DDevice;
        if (d3d->fVertexShader || d3d->fPixelShader) {
            if (!d3d->fVertexShader || !d3d->fPixelShader) {
                // A vertex program with the fixed-function pixel stages, or the reverse, is
                // not supported (not drawn), as in plMetalPipeline::ISetShaders.
                hsAssert(d3d->fVertexShader, "Can't handle programmable passes without vShader");
                hsAssert(d3d->fPixelShader, "Can't handle programmable passes without pShader");
                return false;
            }
            return !fPipe->IDrawShader(draw, fVLength);
        }
        return !fPipe->IDrawFixedFunction(draw);
    }
};

// The prewarm's render functor (plWGPUPipeline::IPrewarmWalk): the draw
// plWGPURenderTriListFunc::RenderPrims would make, as far as its pipeline, which is started on the device. Its draw
// carries the span's vertex format (the vertex ref's, or for a buffer not prepared yet the one
// plWGPUDevice::SetupVertexBufferRef will give it) and no buffer. It draws nothing and counts nothing (no ICountPass,
// no profile counts, no row context): it returns where RenderPrims and the device's draws refuse (an unsupported
// program, no primitives, half a program pair, two program families).
class plWGPUPrewarmTriListFunc : public plWGPURenderPrimFunc
{
protected:
    plWGPUPipeline* fPipe;
    uint8_t         fVertexFormat;
    int             fNumTris;

public:
    mutable uint32_t fRequests; // the draws the span's passes would make
    mutable uint32_t fStarted;  // of them, the ones whose pipeline build was started (neither built nor in flight)

    plWGPUPrewarmTriListFunc(plWGPUPipeline* pipe, uint8_t vertexFormat, int iNumTris)
        : fPipe(pipe), fVertexFormat(vertexFormat), fNumTris(iNumTris), fRequests(), fStarted()
    {
    }

    bool RenderPrims() const override
    {
        if (fPipe->fUnsupportedProgramPass || !fNumTris)
            return false;

        plWGPUDevice::FixedFunctionDraw draw;
        draw.vertexFormat = fVertexFormat;
        draw.plateVertex = false;
        draw.count = fNumTris * 3;
        draw.topology = WGPUPrimitiveTopology_TriangleList;

        const plWGPUD3DDevice* d3d = fPipe->fD3DDevice;
        bool                   started;
        if (d3d->fVertexShader || d3d->fPixelShader) {
            if (!d3d->fVertexShader || !d3d->fPixelShader)
                return false;
            fRequests++;
            started = fPipe->fDevice.PrewarmShader(draw);
        } else {
            fRequests++;
            started = fPipe->fDevice.PrewarmFixedFunction(draw);
        }
        if (started)
            fStarted++;
        return false;
    }
};

// plShadowVertStruct (plDXPipeline.cpp) with the D3D9 (u, v, 1) of a 2-float
// coordinate (plWGPUFixedFunctionPipelineState::Layout).
struct plWGPUXYZTex1Vert
{
    float fPos[3];
    float fUv[3];
};

inline void plWGPUPipeline::inlEnsureLightingOn()
{
    // plDXPipeline::inlEnsureLightingOn
    if (!fCurrD3DLiteState) {
        fD3DDevice->SetRenderState(plWGPUD3D9::D3DRS_LIGHTING, plWGPUD3D9::TRUE);
        fCurrD3DLiteState = true;
    }
}

inline void plWGPUPipeline::inlEnsureLightingOff()
{
    // plDXPipeline::inlEnsureLightingOff
    if (fCurrD3DLiteState) {
        fD3DDevice->SetRenderState(plWGPUD3D9::D3DRS_LIGHTING, plWGPUD3D9::FALSE);
        fCurrD3DLiteState = false;
    }
}

#endif // _plWGPUPipeline_Private_inc_
