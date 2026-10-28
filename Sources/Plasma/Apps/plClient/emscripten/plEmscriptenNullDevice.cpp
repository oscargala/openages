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

// The device record the null pipeline is chosen from.
//
// plClient::InitPipeline fails ("No suitable rendering devices found") when no hsG3DDeviceSelector enumerator
// registers a device. Each pipeline registers its enumerator from a static object, as this file does; the WebGPU one
// registers a device only when the browser gave the page one (plWGPUEnumerate::Enumerate), and the client falls back
// to this one without it. There is no "null" device type, so the record says kDevTypeOpenGL, for which
// plClient::ICreatePipeline returns plNullPipeline because PLASMA_PIPELINE_GL is off. The record is filled as
// plGLEnumerate.cpp's fillDeviceRecord fills its own, with the display helper's modes.

#include "HeadSpin.h"
#include "plPipeline/hsG3DDeviceSelector.h"
#include "plPipeline/pl3DPipeline.h"

namespace
{
void EnumerateEmscriptenNullDevice(std::vector<hsG3DDeviceRecord>& records, hsDisplayHndl display)
{
    hsG3DDeviceRecord devRec;
    devRec.SetG3DDeviceType(hsG3DDeviceSelector::kDevTypeOpenGL);
    devRec.SetG3DSubDeviceType(hsG3DDeviceSelector::kGLProviderUnknown);
    devRec.SetDriverName(ST_LITERAL("plNullPipeline"));
    devRec.SetDeviceDesc(ST_LITERAL("Emscripten null device (no rendering)"));
    devRec.SetDriverDesc(ST_LITERAL("Emscripten"));
    devRec.SetDriverVersion(ST_LITERAL("0"));

    devRec.SetCap(hsG3DDeviceSelector::kCapsMipmap);
    devRec.SetCap(hsG3DDeviceSelector::kCapsPerspective);
    devRec.SetCap(hsG3DDeviceSelector::kCapsCompressTextures);
    devRec.SetCap(hsG3DDeviceSelector::kCapsDoesSmallTextures);
    devRec.SetLayersAtOnce(8);

    plDisplayHelper* displayHelper = plDisplayHelper::GetInstance();
    if (displayHelper) {
        for (const auto& mode : displayHelper->GetSupportedDisplayModes(display))
            devRec.GetModes().emplace_back(mode.Width, mode.Height, mode.ColorDepth);
        devRec.SetDefaultModeIndex(0);
    } else {
        devRec.GetModes().emplace_back(
            hsG3DDeviceSelector::kDefaultWidth,
            hsG3DDeviceSelector::kDefaultHeight,
            hsG3DDeviceSelector::kDefaultDepth);
    }

    devRec.SetFudgeFactors(hsG3DDeviceRecord::kDefaultChipset);
    records.emplace_back(devRec);
}

struct plEmscriptenNullEnumerate
{
    plEmscriptenNullEnumerate()
    {
        hsG3DDeviceSelector::AddDeviceEnumerator(&EnumerateEmscriptenNullDevice);
    }
};

plEmscriptenNullEnumerate sNullEnumerator;
} // namespace
