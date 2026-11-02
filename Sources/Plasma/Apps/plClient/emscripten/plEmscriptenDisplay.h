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

// The browser's display, the analogue of the client's native window.
//
// The canvas backing store is the swap chain: it changes size only through plClient::ResetDisplayDevice
// (plWGPUPipeline::ResetDisplayDevice reconfigures the surface, which sets canvas.width/height on the client thread's
// OffscreenCanvas), and then plClient::IResizeNativeDisplayDevice (main.cpp) tells the page and the fullscreen layer.
// Any other page layout change only CSS-scales the canvas, aspect kept (index.html), as the DX client's monitor scaling
// does (plClient::IChangeResolution, win32/plClient_Win.cpp); input maps through the canvas's bounding rect. A
// devicePixelRatio change sends plDisplayScaleChangedMsg(scale) without a suggested location, as the client does at
// start (main.cpp) and the Mac client does (Mac-Cocoa/main.mm startRunLoop and IResizeNativeDisplayDevice):
// plClient::MsgReceive only calls the pipeline's SetBackingScale.
#ifndef plEmscriptenDisplay_inc
#define plEmscriptenDisplay_inc

class plDisplayHelper;

namespace plEmscriptenDisplay
{
// plClient::IResizeNativeDisplayDevice (main.cpp): post the backing size to the page (its
// CSS fit, Module.plasma.canvasBacking) and the mode to the fullscreen layer (plFullscreenMode).
void ResizeNative(int width, int height, bool windowed);

// Starts watching the page's devicePixelRatio (main.cpp, where the first
// plDisplayScaleChangedMsg is sent): the page writes each new ratio to shared memory and a
// frame hook on the client thread sends plDisplayScaleChangedMsg(ratio) for it.
void StartScaleWatch(float initialScale);

// ---- Player mode -----------------------------------------------------------------------
//
// A page opened without canvas=WxH (index.html: a player's page, whose Module.plasma.display is set) is a resizable
// window: the canvas fills the page, the page reports the window (Module.plasma.display: CSS and device-pixel size,
// devicePixelRatio, fullscreen, the screen in device pixels, the render-scale cap; ResizeObserver, 150 ms debounce) and
// the client follows it through the native clients' display path: a frame hook calls plClient::ResetDisplayDevice (the
// pipeline's reset, then plClient::ResizeDisplayDevice, i.e. IResizeWindow's mouse resolution and GUI aspect ratio and
// the native-window hook) with the window's size in device pixels x the render scale, windowed unless the page is
// fullscreen. The render scale is a fraction of the device pixels: by default min(devicePixelRatio, cap) /
// devicePixelRatio, or the page's supersampling (the player's choice on the page, a whole multiple of the device
// pixels). Without supersampling the game's display modes (GetSupportedDisplayModes, the options menu's resolution
// list) are window-relative choices of it; while supersampling is on, the page's choice stands whatever mode the game
// sets (its size still goes to graphics.ini as the scripts write it). With canvas=WxH (the test harness's runs) none of
// this runs: the display is the fixed canvas.
bool PlayerMode();

// The display helper main.cpp installs (WinInit): in player mode the window-relative one
// (desktop = the screen in device pixels; modes = the window at render scales), otherwise
// the stock plEmscriptenDisplayHelper (the canvas, then the screen).
plDisplayHelper* CreateDisplayHelper();

// Player mode: takes the page's window report (synchronously once, then at each change) and
// registers the frame hook that resizes the client. main.cpp, before the frame loop.
void StartWindowWatch();

// The backing scale of the first plDisplayScaleChangedMsg (main.cpp): devicePixelRatio, or in player mode the canvas
// backing's pixels per CSS pixel (the Mac client's backingScaleFactor, drawable pixels per window point:
// Mac-Cocoa/main.mm IResizeNativeDisplayDevice).
float InitialScale();
} // namespace plEmscriptenDisplay

#endif // plEmscriptenDisplay_inc
