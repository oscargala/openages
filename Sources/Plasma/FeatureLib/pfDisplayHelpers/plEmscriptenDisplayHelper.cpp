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

#include "plEmscriptenDisplayHelper.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

plDisplayMode plEmscriptenDisplayHelper::CanvasMode() const
{
    // The client draws into the page's #canvas, so the canvas is the client's window
    // and its size is the default display mode. The pipeline takes its size from the selected
    // mode (pl3DPipeline.h, the constructor; plMetalPipeline.cpp sizes its drawable from
    // devMode the same way), never from the canvas directly. On the client thread #canvas
    // is the OffscreenCanvas transferred to it (-sOFFSCREENCANVASES_TO_PTHREAD), which
    // emscripten_get_canvas_element_size reads there; elsewhere the call is proxied to the
    // page. Before F2a this was the screen size (emscripten_get_screen_size).
    int width = 0, height = 0;
    if (emscripten_get_canvas_element_size("#canvas", &width, &height) != EMSCRIPTEN_RESULT_SUCCESS)
        width = height = 0;
    return plDisplayMode(width, height, 32);
}

plDisplayMode plEmscriptenDisplayHelper::ScreenMode() const
{
    // The screen in device pixels (CSS screen size x devicePixelRatio): the mode a fullscreen
    // canvas has, and the player page's desktop (plEmscriptenDisplay.cpp, DesktopMode;
    // index.html, screen.width x dpr).
    int cssWidth = 0, cssHeight = 0;
    emscripten_get_screen_size(&cssWidth, &cssHeight);
    const double ratio = emscripten_get_device_pixel_ratio();
    return plDisplayMode(int(cssWidth * ratio + 0.5), int(cssHeight * ratio + 0.5), 32);
}

const plDisplayMode& plEmscriptenDisplayHelper::DesktopDisplayMode() const
{
    // The desktop is the display the client's window sits on, never the window: D3D's is the adapter's
    // display mode (plDXPipeline.cpp, plDXPipeline::ICreateDevice), the Mac's the screen's visibleFrame in
    // pixels, "used to prevent windowed mode from being larger than the desktop" (plMacDisplayHelper.mm,
    // plMacDisplayHelper::SetCurrentScreen). Here the canvas is the window (windowed, it stays in the page;
    // otherwise the fullscreen layer gives it the screen), so the desktop is the screen; the canvas
    // only when the page reports no screen. It feeds PtGetDesktopWidth/Height (plWGPUPipeline.cpp,
    // fDesktopParams; cyMisc::GetDesktopWidth/Height). The canvas as the desktop would leave a windowed client
    // no mode below the desktop (GetVideoResList, xOptionsMenu.py:1549) and Graphic Settings unclosable.
    fDesktopDisplayMode = ScreenMode();
    if (fDesktopDisplayMode.Width <= 0 || fDesktopDisplayMode.Height <= 0)
        fDesktopDisplayMode = CanvasMode();
    return fDesktopDisplayMode;
}

std::vector<plDisplayMode> plEmscriptenDisplayHelper::GetSupportedDisplayModes(
    hsDisplayHndl display, int ColorDepth) const
{
    // The canvas mode first (the device record's default, mode index 0),
    // then the screen in device pixels, the mode a fullscreen canvas has; listed once when
    // the two are equal. The fullscreen layer (plEmscriptenDisplay.h, the hook contract)
    // asks for it as any mode change is asked for.
    std::vector<plDisplayMode> modes = {CanvasMode()};
    const plDisplayMode        screen = ScreenMode();
    if (screen.Width > 0 && screen.Height > 0 && (screen.Width != modes[0].Width || screen.Height != modes[0].Height))
        modes.push_back(screen);
    return modes;
}

hsDisplayHndl plEmscriptenDisplayHelper::DefaultDisplay() const
{
    // There is exactly one display; any non-null handle identifies it.
    static int sBrowserDisplay;
    return reinterpret_cast<hsDisplayHndl>(&sBrowserDisplay);
}
