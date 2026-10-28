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

#include "plWGPUPlateManager.h"

#include <cstring>
#include <string_theory/format>
#include <string_theory/string_stream>

#include "hsGMatState.h"
#include "plPipeline/plDebugText.h"
#include "plSurface/hsGMaterial.h"
#include "plWGPUPipeline.h"
#include "pnKeyedObject/plKey.h"

using namespace plWGPUD3D9;

plWGPUPlateManager::plWGPUPlateManager(plWGPUPipeline* pipe)
    : plPlateManager(pipe),
      fVtxBuffer()
{
}

ST::string plWGPUPlateManager::DescribePlates() const
{
    ST::string_stream out;
    int               count = 0;
    for (plPlate* plate = fPlates; plate; plate = plate->GetNext()) {
        hsGMaterial* mat = plate->GetMaterial();
        out << (count++ ? "; " : "") << '"' << plate->GetTitle() << "\" "
            << (mat && mat->GetKey() ? mat->GetKey()->GetUoid().StringIze() : ST_LITERAL("(no material)"))
            << (plate->IsVisible() ? " visible" : " hidden");
    }
    return ST::format("{} plates: {}", count, out.to_string());
}

void plWGPUPlateManager::ICreateGeometry()
{
    // plDXPlateManager::ICreateGeometry: a quad of plPlateVertex
    // (position, white diffuse, UVW) drawn as a triangle strip.
    plWGPUPipeline* pipeline = (plWGPUPipeline*)fOwner;
    if (!fVtxBuffer) {
        const plateVertex vertices[4] = {
            {{-0.5f, -0.5f, 0.0f}, 0xffffffff, {0.0f, 0.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}, 0xffffffff, {0.0f, 1.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, 0xffffffff, {1.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, 0xffffffff, {1.0f, 1.0f, 0.0f}}};
        static_assert(sizeof(plateVertex) == 28, "plPlateVertex is D3DFVF_XYZ|DIFFUSE|TEX1|TEXCOORDSIZE3(0)");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = WGPUStringView{"PlateMgrVtxBuff", WGPU_STRLEN};
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        desc.size = sizeof(vertices);
        fVtxBuffer = wgpuDeviceCreateBuffer(pipeline->fDevice.fWGPUDevice, &desc);
        wgpuQueueWriteBuffer(pipeline->fDevice.fQueue, fVtxBuffer, 0, vertices, sizeof(vertices));
#ifdef PLASMA_TEST_HOOKS
        pipeline->fDevice.NoteUpload(plWGPUDevice::kUploadOther, sizeof(vertices), ST_LITERAL("plate quad"), "create");
#endif
    }
}

void plWGPUPlateManager::IReleaseGeometry()
{
    if (fVtxBuffer) {
        wgpuBufferRelease(fVtxBuffer);
        fVtxBuffer = nullptr;
    }
}

void plWGPUPlateManager::IDrawToDevice(plPipeline* pipe)
{
    // plDXPlateManager::IDrawToDevice: each visible plate, then
    // its title and, for graphs, its key, as debug text.
    plWGPUPipeline*  pipeline = (plWGPUPipeline*)pipe;
    plWGPUD3DDevice* d3dDevice = &pipeline->fDevice.fD3D;
    uint32_t         scrnWidthDiv2 = fOwner->Width() >> 1;
    uint32_t         scrnHeightDiv2 = fOwner->Height() >> 1;
    D3DCULL          oldCullMode;

    if (!fVtxBuffer)
        return;

    // Make sure skinning is disabled.
    d3dDevice->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    d3dDevice->SetVertexShader(nullptr);

    // To get plates properly pixel-aligned, we need to compensate for D3D9's weird half-pixel
    // offset: D3DTS_VIEW is a translation by half a pixel.
    D3DMATRIX viewMat = d3dIdentityMatrix;
    viewMat.m[3][0] = -0.5f / scrnWidthDiv2;
    viewMat.m[3][1] = -0.5f / scrnHeightDiv2;
    d3dDevice->SetTransform(D3DTS_VIEW, &viewMat);
    oldCullMode = pipeline->fDevice.fCurrCullMode;

    for (plPlate* plate = fPlates; plate != nullptr; plate = plate->GetNext()) {
        if (plate->IsVisible()) {
            pipeline->IDrawPlate(plate);

            ST::string title = plate->GetTitle();
            if (plDebugText::Instance().IsEnabled() && !title.empty()) {
                hsPoint3 pt;
                pt.Set(0, -0.5, 0);
                pt = plate->GetTransform() * pt;
                pt.fX = pt.fX * scrnWidthDiv2 + scrnWidthDiv2;
                pt.fY = pt.fY * scrnHeightDiv2 + scrnHeightDiv2;
                pt.fX -= plDebugText::Instance().CalcStringWidth(title) >> 1;
                plDebugText::Instance().DrawString((uint16_t)pt.fX, (uint16_t)pt.fY + 1, title, 255, 255, 255, 255, plDebugText::kStyleBold);
            }

            if (plate->GetFlags() & plPlate::kFlagIsAGraph) {
                plGraphPlate* graph = (plGraphPlate*)plate;
                hsPoint3      pt, pt2;

                uint32_t numLabels = graph->GetNumLabels();
                if (numLabels > 0) {
                    /// Draw key
                    pt.Set(-0.5, -0.5, 0);
                    pt = plate->GetTransform() * pt;
                    pt.fX = pt.fX * scrnWidthDiv2 + scrnWidthDiv2;
                    pt.fY = pt.fY * scrnHeightDiv2 + scrnHeightDiv2;
                    pt.fY += plDebugText::Instance().GetFontHeight();

                    if (numLabels > graph->GetNumColors())
                        numLabels = graph->GetNumColors();

                    for (uint32_t i = 0; i < numLabels; i++) {
                        ST::string str = graph->GetLabelText(i);

                        pt2 = pt;
                        pt2.fX -= plDebugText::Instance().CalcStringWidth(str);
                        plDebugText::Instance().DrawString((uint16_t)pt2.fX, (uint16_t)pt2.fY, str,
                                                           graph->GetDataColor(i), plDebugText::kStyleBold);
                        pt.fY += plDebugText::Instance().GetFontHeight();
                    }
                }
            }
        }
    }

    pipeline->fDevice.fCurrCullMode = (pipeline->fLayerState[0].fMiscFlags & hsGMatState::kMiscTwoSided) ? D3DCULL_NONE : oldCullMode;
    d3dDevice->SetRenderState(D3DRS_CULLMODE, pipeline->fDevice.fCurrCullMode);
}

plWGPUPlateManager::~plWGPUPlateManager()
{
    IReleaseGeometry();
}
