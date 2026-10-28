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
// ---- Fullscreen hook contract (the fullscreen layer implements it) --------------------
//
// using FullscreenHook = void (*)(int width, int height, bool windowed,
//                                 float devicePixelRatio, void* user);
// void SetFullscreenHook(FullscreenHook hook, void* user = nullptr);
//
// Who calls it: plClient::IResizeNativeDisplayDevice (main.cpp), the browser's native-window hook, called by
// plClient::ResizeDisplayDevice: once at start, from plClientLoader::StartClient with the pipeline's mode, and at the
// end of every plClient::ResetDisplayDevice: the console's Graphics.Renderer.ResetDevice, the options menu's
// plClientMsg::kResetGraphicsDevice (plClient::MsgReceive), and a plDisplayScaleChangedMsg with a suggested location
// (plClient::MsgReceive; the browser never sends one).
// When: after the pipeline has applied the mode (created the device at start, or reset it), so the canvas backing store
// already is width x height, and after the page has been posted the new backing size (its CSS fit). Also called for a
// mode the pipeline found nothing to change for (its early return, plDXPipeline::ResetDisplayDevice):
// IResizeNativeDisplayDevice is called regardless.
// Arguments: width, height = the display mode the client asked for, in device pixels
// (the swap chain's BackBufferWidth/Height when the mode was applied); windowed = false
// when the client asks for fullscreen (graphics.ini Windowed 0, the options menu);
// devicePixelRatio = the scale of the last plDisplayScaleChangedMsg; user = as registered.
// Thread: the client pthread, inside a tick of the frame loop, never during a suspension.
// The hook returns promptly: it must not block, wait, make a synchronous call to the main thread or suspend. To act on
// the page it posts to the main thread (a library function proxied asynchronously, as plEmscriptenFullscreen.js's
// plFullscreenMode is); a fullscreen request needs transient user activation there, so the page applies it at the next
// user gesture.
// Returns: nothing; the client reads nothing back (the native hook returns nothing). A fullscreen layer that wants
// another backing size asks for it as any mode change is asked for (Graphics.Renderer.ResetDevice through plConsoleMsg,
// or the options menu); the browser's mode list holds the screen size x devicePixelRatio (plEmscriptenDisplayHelper)
// next to the canvas size.
// Registration: at most one hook; SetFullscreenHook(nullptr) removes it. Same thread rules
// as plEmscriptenFrameHooks::Add. No hook registered: nothing is called.
using FullscreenHook = void (*)(int width, int height, bool windowed, float devicePixelRatio, void* user);
void SetFullscreenHook(FullscreenHook hook, void* user = nullptr);

// plClient::IResizeNativeDisplayDevice (main.cpp): post the backing size to the page (its
// CSS fit, Module.plasma.canvasBacking) and call the fullscreen hook.
void ResizeNative(int width, int height, bool windowed);

// Starts watching the page's devicePixelRatio (main.cpp, where the first
// plDisplayScaleChangedMsg is sent): the page writes each new ratio to shared memory and a
// frame hook on the client thread sends plDisplayScaleChangedMsg(ratio) for it.
void StartScaleWatch(float initialScale);

// The scale of the last plDisplayScaleChangedMsg, and how many changes were sent.
float         CurrentScale();
unsigned long ScaleChanges();

// ---- Player mode -----------------------------------------------------------------------
//
// A page opened without canvas=WxH (index.html: a player's page, whose Module.plasma.display is set) is a resizable
// window: the canvas fills the page, the page reports the window (Module.plasma.display: CSS and device-pixel size,
// devicePixelRatio, fullscreen, the screen in device pixels, the render-scale cap; ResizeObserver, 150 ms debounce) and
// the client follows it through the native clients' display path: a frame hook calls plClient::ResetDisplayDevice (the
// pipeline's reset, then plClient::ResizeDisplayDevice, i.e. IResizeWindow's mouse resolution and GUI aspect ratio and
// the native-window hook) with the window's size in device pixels x the render scale, windowed unless the page is
// fullscreen. The render scale is a fraction of the device pixels: by default min(devicePixelRatio, cap) /
// devicePixelRatio; the game's display modes (GetSupportedDisplayModes, the options menu's resolution list) are
// window-relative choices of it. With canvas=WxH (the test harness's runs) none of this runs: the display is the fixed
// canvas.
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
