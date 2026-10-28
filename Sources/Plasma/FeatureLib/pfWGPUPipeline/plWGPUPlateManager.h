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

#ifndef plWGPUPlateManager_h
#define plWGPUPlateManager_h

// Plates (load progress, fades, debug graphs) for the WebGPU pipeline, a port of
// plMetalPlateManager with the plate semantics of the D3D9 client. Plates draw through
// plWGPUPipeline::IDrawPlate's IHandleMaterial passes on the emulated D3D9 device, every layer
// (plDXPlateManager::IDrawToDevice, plDXPipeline::IDrawPlate), as the D3D9 client does.

#include <webgpu/webgpu.h>

#include "plPipeline/plPlates.h"

class plWGPUPipeline;
class plWGPUDevice;

class plWGPUPlateManager : public plPlateManager
{
    friend class plWGPUPipeline;

public:
    plWGPUPlateManager(plWGPUPipeline* pipe);
    ~plWGPUPlateManager();

    void IDrawToDevice(plPipeline* pipe) override;

    void       ICreateGeometry();
    void       IReleaseGeometry();
    WGPUBuffer GetVertexBuffer() const { return fVtxBuffer; }
    /// The plates in the list: title, material key, visibility.
    ST::string DescribePlates() const;

private:
    // plPlateVertex of the D3D9 plate (plDXPlateManager::ICreateGeometry; its FVF in the
    // plDXPlateManager constructor)
    struct plateVertex
    {
        float    position[3];
        uint32_t color;
        float    uvw[3];
    };

    WGPUBuffer fVtxBuffer;
};

#endif /* plWGPUPlateManager_h */
