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

#include "plEmscriptenDisplay.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string_theory/format>
#include <vector>

#include "HeadSpin.h"
#include "hsBitCast.h"
#include "pfDisplayHelpers/plEmscriptenDisplayHelper.h"
#include "plClient.h"
#include "plEmscriptenFrameHooks.h"
#include "plMessage/plDisplayScaleChangedMsg.h"
#ifdef PLASMA_PIPELINE_WGPU
#include "pfWGPUPipeline/plWGPUPipeline.h"
#endif
#ifdef PLASMA_TEST_HOOKS
#include "plEmscriptenTestHooks.h"
#endif

// The page half of the display, plEmscriptenDisplay.js. Each function runs on the page's main thread: the backing and
// the scale watch without the client waiting for them, the player-mode query and the window watch synchronously.
extern "C" {
void plDisplayCanvasBacking(int width, int height);
void plDisplayWatchScale(int32_t* shared);
int  plDisplayPlayerMode();
void plDisplayWatchWindow(int32_t* seq, double* fields);
}

// The display's progress lines (its modes, scales and resizes) are a harness build's status lines, through the client
// seam's Status, as main.cpp's CLIENT_STATUS lines are. The product prints none: its console carries the client's
// lifecycle and errors only, and none of these lines is either.
#ifdef PLASMA_TEST_HOOKS
#define DISPLAY_STATUS(...) IDisplayStatus(ST::format(__VA_ARGS__))
#else
#define DISPLAY_STATUS(...) ((void)0)
#endif

namespace
{
#ifdef PLASMA_TEST_HOOKS
void IDisplayStatus(const ST::string& line)
{
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->Status(line);
}
#endif

// player mode's IResizeNativeDisplayDevice (below)
void ResizePlayer(int width, int height, bool windowed);

plEmscriptenDisplay::FullscreenHook s_fullscreenHook = nullptr;
void*                               s_fullscreenUser = nullptr;

// [0] = change sequence (the page adds 1 after writing [1]), [1] = the ratio (float bits).
alignas(8) int32_t s_scaleShared[2] = {0, 0};
int32_t       s_scaleSeen = 0;
float         s_scale = 1.f;
unsigned long s_scaleChanges = 0;

void PollScale(void*)
{
    const int32_t seq = __atomic_load_n(&s_scaleShared[0], __ATOMIC_ACQUIRE);
    if (seq == s_scaleSeen)
        return;
    s_scaleSeen = seq;
    const int32_t bits = __atomic_load_n(&s_scaleShared[1], __ATOMIC_RELAXED);
    float         scale = hsBitCast<float>(bits);
    if (scale == s_scale)
        return;
    s_scale = scale;
    s_scaleChanges++;
    DISPLAY_STATUS("DISPLAY SCALE: devicePixelRatio {.3f} -> plDisplayScaleChangedMsg (change {})", scale,
                   s_scaleChanges);
    plDisplayScaleChangedMsg* msg = new plDisplayScaleChangedMsg(scale);
    msg->Send();
}
} // namespace

void plEmscriptenDisplay::SetFullscreenHook(FullscreenHook hook, void* user)
{
    s_fullscreenHook = hook;
    s_fullscreenUser = user;
}

void plEmscriptenDisplay::ResizeNative(int width, int height, bool windowed)
{
    if (PlayerMode()) {
        ResizePlayer(width, height, windowed);
        return;
    }
    DISPLAY_STATUS("DISPLAY: IResizeNativeDisplayDevice {}x{} windowed {} (fullscreen hook {})", width, height,
                   windowed ? 1 : 0, s_fullscreenHook ? "called" : "not registered");
    plDisplayCanvasBacking(width, height);
    if (s_fullscreenHook)
        s_fullscreenHook(width, height, windowed, s_scale, s_fullscreenUser);
}

void plEmscriptenDisplay::StartScaleWatch(float initialScale)
{
    s_scale = initialScale;
    // In player mode the window watch (PollWindow) sends the scale: the backing's pixels per
    // CSS pixel, which a devicePixelRatio change moves together with the window's device size.
    if (PlayerMode())
        return;
    // The page side runs on the main thread: a matchMedia query for the current ratio, re-armed
    // at each change, writes the new ratio and then bumps the sequence.
    plDisplayWatchScale(s_scaleShared);
    plEmscriptenFrameHooks::Add(PollScale);
}

float         plEmscriptenDisplay::CurrentScale() { return s_scale; }
unsigned long plEmscriptenDisplay::ScaleChanges() { return s_scaleChanges; }

// ---- Player mode --------------------------------------------------------------------------------
//
// The page's window report (boot.mjs, Module.plasma.display) arrives in s_window under a sequence lock: the page makes
// s_windowSeq odd, writes the doubles, makes it even. The client thread reads it (ReadWindow), owns the render scale
// and resizes the client (PollWindow, a frame hook). Every size here is in device pixels.

namespace
{
enum WindowField
{
    kDpr,
    kCssW,
    kCssH,
    kDevW,
    kDevH,
    kFullscreen,
    kScreenW,
    kScreenH,
    kCap,
    kRenderScale,
    kNumFields
};
alignas(8) double s_window[kNumFields] = {};
alignas(4) int32_t s_windowSeq = 0;

struct Window
{
    double dpr = 1., cssW = 0., cssH = 0., devW = 0., devH = 0., screenW = 0., screenH = 0., cap = 1.;
    double renderScale = 1.; // the player's supersampling choice: 1 = off, 2
    bool   fullscreen = false;
};
Window  s_win;
int32_t s_winSeq = 0; // the last sequence read

// 0 = the default render scale (DefaultFraction); otherwise a fraction of the device pixels.
double s_fraction = 0.;
double s_renderScaleSeen = 0.; // the page's render scale of the last report PollWindow read
struct
{
    int  width = 0, height = 0;
    bool windowed = true;
} s_applied;                    // the last IResizeNativeDisplayDevice
bool s_started = false;         // plClientLoader::StartClient's ResizeDisplayDevice has run
bool s_inPlatformReset = false; // inside PollWindow's plClient::ResetDisplayDevice
int  s_holdTicks = 0;           // after the game asked for a fullscreen change: wait for the page

// The largest backing: WebGPU's default maxTextureDimension2D, which is the device's limit (the device is
// requested without required limits, plWGPUEnumerate.cpp: the spec's default limits); boot.mjs MAX_BACKING.
constexpr int    kMaxBacking = 8192;
// The scales the game's resolution list offers (fractions of the device pixels).
constexpr double kListedFractions[] = {0.5, 0.75, 1.0};

bool s_player = false;
bool s_playerRead = false;

bool ReadWindow()
{
    const int32_t s1 = __atomic_load_n(&s_windowSeq, __ATOMIC_ACQUIRE);
    if (s1 == 0 || (s1 & 1) || s1 == s_winSeq)
        return false;
    double v[kNumFields];
    for (int i = 0; i < kNumFields; i++)
        __atomic_load(&s_window[i], &v[i], __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&s_windowSeq, __ATOMIC_ACQUIRE) != s1)
        return false; // a write raced the copy: next tick
    s_winSeq = s1;
    s_win.dpr = v[kDpr] > 0. ? v[kDpr] : 1.;
    s_win.cssW = v[kCssW];
    s_win.cssH = v[kCssH];
    s_win.devW = v[kDevW];
    s_win.devH = v[kDevH];
    s_win.fullscreen = v[kFullscreen] != 0.;
    s_win.screenW = v[kScreenW];
    s_win.screenH = v[kScreenH];
    s_win.cap = v[kCap] > 0. ? v[kCap] : 1.;
    s_win.renderScale = v[kRenderScale] > 1. ? v[kRenderScale] : 1.;
    return true;
}

const Window& CurrentWindow()
{
    ReadWindow();
    return s_win;
}

// The render scale without supersampling: min(dpr, cap) / dpr.
double CappedFraction(const Window& w) { return std::min(w.dpr, w.cap) / w.dpr; }
// Supersampling (the page's render scale above 1): the backing is the largest whole multiple of the device pixels
// up to the render scale for which neither side exceeds kMaxBacking, at least the device pixels themselves. The
// browser's compositor scales the canvas down to its CSS box with a bilinear filter at the device pixel centres,
// which is a 2x2 box at exactly 2x and aliases at fractional ratios, hence whole factors only. boot.mjs computes
// the start-up backing the same way (backingFraction, then Math.round).
double DefaultFraction(const Window& w)
{
    const double capped = CappedFraction(w);
    if (w.renderScale <= 1. || w.devW <= 0. || w.devH <= 0.)
        return capped;
    const double fits = std::floor(std::min({w.renderScale, kMaxBacking / w.devW, kMaxBacking / w.devH}));
    return std::max({capped, 1., fits});
}
double Fraction(const Window& w) { return s_fraction > 0. ? s_fraction : DefaultFraction(w); }
int    Scaled(double device, double fraction)
{
    return std::clamp(int(std::floor(device * fraction + 0.5)), 1, kMaxBacking);
}

struct Mode
{
    int    width, height;
    double fraction;
};

// The screen in device pixels; with supersampling, times the render scale, as a supersampled desktop reports
// its framebuffer (macOS's scaled HiDPI modes). The scripts' windowed resolution list keeps only modes smaller
// than the desktop (xOptionsMenu.py GetVideoResList, in this repository's scripts and in MOULa's), so the current
// supersampled size stays on the list and choosing it keeps the render scale (ModeFraction).
plDisplayMode DesktopMode(const Window& w)
{
    const double k = w.renderScale > 1. ? w.renderScale : 1.;
    return plDisplayMode(int(std::floor(w.screenW * k + 0.5)), int(std::floor(w.screenH * k + 0.5)), 32);
}

// The window at the current scale first (the device record's default mode, plWGPUEnumerate::Enumerate),
// then 50/75/100 % of its device pixels and the default (min(dpr, cap)); each size once.
// A windowed client needs a mode below the desktop, as every native display lists: D3D's adapter modes under its
// desktop, the adapter's display mode (hsGDirect3DTnLEnumerate::IEnumAdapterDevices, plDXPipeline::ICreateDevice);
// the Mac's display modes under its visibleFrame (plMacDisplayHelper::SetCurrentScreen). This repository's
// GetVideoResList (xOptionsMenu.py) keeps, when windowed, only modes of at least 800x600 that are not at least the
// desktop in both dimensions (MOULa's keeps modes of at least 800x576), and an empty list leaves Graphic Settings
// unclosable (InitVideoControlsGUI, then WriteVideoControls, before OnGUINotify hides it). So when no mode is both at
// least 800x600 and below the desktop (a window too small for 800x600, or one that fills the screen), the
// smallest scale that reaches 800x600 is listed too. A window filling a screen at most 600 device pixels tall or
// 800 wide has no such scale of its own aspect, and stays without a windowed mode.
std::vector<Mode> WindowModes(const Window& w)
{
    std::vector<Mode> modes;
    auto              add = [&](double f) {
        const int width = Scaled(w.devW, f), height = Scaled(w.devH, f);
        for (const Mode& m : modes)
            if (m.width == width && m.height == height)
                return;
        modes.push_back({width, height, f});
    };
    if (w.devW <= 0. || w.devH <= 0.)
        return modes;
    add(Fraction(w));
    for (double f : kListedFractions)
        add(f);
    add(DefaultFraction(w));
    const plDisplayMode desktop = DesktopMode(w);
    const auto          windowedMode = [&](const Mode& m) {
        return m.width >= 800 && m.height >= 600 && (m.width < desktop.Width || m.height < desktop.Height);
    };
    if (std::none_of(modes.begin(), modes.end(), windowedMode))
        add(std::max(800. / w.devW, 600. / w.devH));
    return modes;
}

// graphics.ini's Graphics.Width/Height (plPipeline::fInitialPipeParams, the console commands Graphics.Width and
// Graphics.Height in pfConsoleCommands.cpp) at start-up, read as a render scale where it names one: a mode of this
// window's list (WindowModes, as ModeFraction matches the game's choice; natively a windowed start-up takes the ini's
// size as the window's, the pl3DPipeline constructor, and xOptionsMenu.py WriteVideoControls saves the chosen mode), or
// the screen mode (the screen in device pixels, the old fullscreen choice) as the full device resolution. Anything else
// (a fixed canvas size of an older session, another window's size) keeps the default.
double IniFraction(const Window& w, int width, int height)
{
    // With supersampling on, the page's render scale decides the start-up backing: a saved size
    // (of a session before the choice, or the game's own resolution choice) does not undo the player's choice.
    if (w.renderScale > 1.)
        return 0.;
    if (width == int(w.screenW) && height == int(w.screenH))
        return 1.0;
    if (width == Scaled(w.devW, DefaultFraction(w)) && height == Scaled(w.devH, DefaultFraction(w)))
        return 0.; // the default (the graphics.ini plClient::IDetectAudioVideoSettings writes from the default mode)
    for (const Mode& m : WindowModes(w))
        if (m.width == width && m.height == height)
            return m.fraction == DefaultFraction(w) ? 0. : m.fraction;
    return 0.;
}

// A mode the game asked for (the options menu, the console): the listed scale it is, else its width over the window's
// (MOULa's GetVideoResList lists a mode of the desktop's height as height - 75, so the width carries the choice).
double ModeFraction(const Window& w, int width, int height)
{
    for (const Mode& m : WindowModes(w))
        if (m.width == width && m.height == height)
            return m.fraction == DefaultFraction(w) ? 0. : m.fraction;
    if (width == int(w.screenW))
        return 1.0;
    if (w.devW <= 0.)
        return 0.;
    return std::clamp(double(width) / w.devW, 0.125, std::max(1.0, std::max(800. / w.devW, 600. / w.devH)));
}

void SendScale(float scale)
{
    if (scale <= 0.f || scale == s_scale)
        return;
    s_scale = scale;
    s_scaleChanges++;
    DISPLAY_STATUS("DISPLAY SCALE: backing {.3f} pixels per CSS pixel -> plDisplayScaleChangedMsg (change {})",
                   scale, s_scaleChanges);
    plDisplayScaleChangedMsg* msg = new plDisplayScaleChangedMsg(scale);
    msg->Send();
}

void PollWindow(void*)
{
    const bool changed = ReadWindow();
    if (!s_started)
        return;
    plClient*   client = plClient::GetInstance();
    plPipeline* pipe = client && !client->GetDone() ? client->GetPipeline() : nullptr;
    if (!pipe)
        return;
    if (s_holdTicks > 0) {
        if (!changed) {
            s_holdTicks--;
            return;
        }
        s_holdTicks = 0;
    }
    const Window& w = s_win;
    if (w.devW <= 0. || w.devH <= 0.)
        return;
    // A change of the page's render scale is the player's newest choice: it replaces a resolution
    // the game chose earlier in the session (the options menu, the console), as a game choice replaces the page's.
    if (w.renderScale != s_renderScaleSeen) {
        if (s_renderScaleSeen != 0. && s_fraction > 0.) {
            DISPLAY_STATUS(
                "DISPLAY PLAYER: page render scale {.2f} -> {.2f}: the game's render scale {.3f} gives way "
                "to the default",
                s_renderScaleSeen, w.renderScale, s_fraction);
            s_fraction = 0.;
        }
        s_renderScaleSeen = w.renderScale;
    }
    // PtGetDesktopWidth/Height read the pipeline's copy (plPipeline::GetDesktopWidth/Height), set once at its creation.
    pipe->fDesktopParams = DesktopMode(w);
    const double f = Fraction(w);
    const int    width = Scaled(w.devW, f), height = Scaled(w.devH, f);
    const bool   windowed = !w.fullscreen;
    if (width != s_applied.width || height != s_applied.height || windowed != s_applied.windowed) {
        int  aa = plPipeline::fInitialPipeParams.AntiAliasingAmount;
        int  aniso = plPipeline::fInitialPipeParams.AnisotropicLevel;
        bool vsync = plPipeline::fInitialPipeParams.VSync;
#ifdef PLASMA_PIPELINE_WGPU
        if (plWGPUPipeline* wgpu = plWGPUPipeline::ConvertNoRef(pipe))
            wgpu->GetPresentSettings(aa, aniso, vsync);
#endif
        DISPLAY_STATUS(
            "DISPLAY PLAYER: window css {.1f}x{.1f} device {}x{} dpr {.3f} fullscreen {} -> "
            "plClient::ResetDisplayDevice {}x{} windowed {} (render scale {.3f} of the device pixels{})",
            w.cssW, w.cssH, int(w.devW), int(w.devH), w.dpr, w.fullscreen ? 1 : 0, width, height,
            windowed ? 1 : 0, f,
            s_fraction > 0.      ? ""
            : w.renderScale > 1. ? ST::format(", the default: supersampling {.2f}", w.renderScale)
                                 : ST::format(", the default: cap {.2f}", w.cap));
        s_inPlatformReset = true;
        client->ResetDisplayDevice(width, height, pipe->ColorDepth(), windowed, aa, aniso, vsync);
        s_inPlatformReset = false;
    }
    if (w.cssW > 0.)
        SendScale(float(double(s_applied.width) / w.cssW));
}

// plClient::IResizeNativeDisplayDevice in player mode. The Mac client's makes its window the mode's
// size and enters or leaves fullscreen as the mode says (Mac-Cocoa/main.mm IResizeNativeDisplayDevice); a page cannot
// size the browser window, so here the mode sets the render scale and the window keeps its size.
void ResizePlayer(int width, int height, bool windowed)
{
    const Window&                w = CurrentWindow();
    [[maybe_unused]] const char* origin = s_inPlatformReset ? "the window"
                                          : !s_started      ? "start-up (plClientLoader::StartClient)"
                                                            : "the game (options menu or console)";
    DISPLAY_STATUS("DISPLAY: IResizeNativeDisplayDevice {}x{} windowed {} (player mode; from {})", width, height,
                   windowed ? 1 : 0, origin);
    plDisplayCanvasBacking(width, height);
    s_applied.width = width;
    s_applied.height = height;
    s_applied.windowed = windowed;
    if (s_inPlatformReset)
        return;
    if (!s_started) {
        // graphics.ini decides the first mode (plClient::InitPipeline); the window decides the size. Its
        // Graphics.Width/Height become a render scale where they name one; its Graphics.Windowed is not
        // applied: a page can enter fullscreen only on a user gesture, so fullscreen stays an explicit act.
        s_started = true;
        s_fraction = IniFraction(w, plPipeline::fInitialPipeParams.Width, plPipeline::fInitialPipeParams.Height);
        DISPLAY_STATUS(
            "DISPLAY PLAYER: start-up mode {}x{} windowed {} (graphics.ini {}x{} windowed {}) -> "
            "render scale {}",
            width, height, windowed ? 1 : 0, plPipeline::fInitialPipeParams.Width,
            plPipeline::fInitialPipeParams.Height, plPipeline::fInitialPipeParams.Windowed ? 1 : 0,
            s_fraction > 0. ? ST::format("{.3f} of the device pixels", s_fraction)
                            : ST::format("the default ({.3f})", DefaultFraction(w)));
        return;
    }
    // A mode the game chose: its size is a render scale of this window; its windowed flag is the game's
    // fullscreen toggle, which the fullscreen layer applies (a request waits for the next user gesture).
    s_fraction = ModeFraction(w, width, height);
    DISPLAY_STATUS("DISPLAY PLAYER: game mode {}x{} windowed {} -> render scale {}; fullscreen layer {}",
                   width, height, windowed ? 1 : 0,
                   s_fraction > 0. ? ST::format("{.3f} of the device pixels", s_fraction)
                                   : ST::format("the default ({.3f})", DefaultFraction(w)),
                   s_fullscreenHook ? (windowed ? "asked to leave fullscreen" : "asked for fullscreen")
                                    : "not registered");
    if (s_fullscreenHook)
        s_fullscreenHook(width, height, windowed, s_scale, s_fullscreenUser);
    if (s_fullscreenHook && windowed == w.fullscreen)
        s_holdTicks = 60; // the page's fullscreenchange report, then the resize (at most about a second)
}

// In player mode the display is the window: the screen is the desktop, the modes are window-relative.
class plPlayerDisplayHelper : public plEmscriptenDisplayHelper
{
public:
    const plDisplayMode& DesktopDisplayMode() const override
    {
        fDesktop = DesktopMode(CurrentWindow());
        return fDesktop;
    }

    std::vector<plDisplayMode> GetSupportedDisplayModes(hsDisplayHndl display, int ColorDepth) const override
    {
        std::vector<plDisplayMode> modes;
        for (const Mode& m : WindowModes(CurrentWindow()))
            modes.emplace_back(m.width, m.height, 32);
        // Before the pipeline exists (plClient::InitPipeline's enumeration), graphics.ini's own mode is
        // a display mode too: a saved fullscreen size is found (plClient::InitPipeline) instead of
        // resetting graphics.ini to its defaults (ISetGraphicsDefaults); the first resize then
        // reads it as a render scale (IniFraction) and takes the window's size.
        plClient* client = plClient::GetInstance();
        const int iniW = plPipeline::fInitialPipeParams.Width, iniH = plPipeline::fInitialPipeParams.Height;
        if (!(client && client->GetPipeline()) && iniW >= 640 && iniH >= 480 &&
            std::none_of(modes.begin(), modes.end(),
                         [&](const plDisplayMode& m) { return m.Width == iniW && m.Height == iniH; }))
            modes.emplace_back(iniW, iniH, 32);
#ifdef PLASMA_TEST_HOOKS
        if (s_started) {
            // the list the game's scripts get (PtGetSupportedDisplayModes: the options menu)
            ST::string list;
            for (const plDisplayMode& m : modes)
                list += ST::format("{}{}x{}", list.empty() ? "" : " ", m.Width, m.Height);
            const plDisplayMode desktop = DesktopMode(CurrentWindow());
            DISPLAY_STATUS("DISPLAY PLAYER: display modes for the game: {} (desktop {}x{})", list, desktop.Width,
                           desktop.Height);
        }
#endif
        return modes;
    }

private:
    mutable plDisplayMode fDesktop;
};
} // namespace

bool plEmscriptenDisplay::PlayerMode()
{
    if (!s_playerRead) {
        // The page has a player display (Module.plasma.display, boot.mjs), not a fixed canvas.
        s_player = plDisplayPlayerMode() != 0;
        s_playerRead = true;
    }
    return s_player;
}

plDisplayHelper* plEmscriptenDisplay::CreateDisplayHelper()
{
    if (PlayerMode())
        return new plPlayerDisplayHelper();
    return new plEmscriptenDisplayHelper();
}

void plEmscriptenDisplay::StartWindowWatch()
{
    if (!PlayerMode())
        return;
    // Synchronous: the page measured the window before it loaded the client (boot.mjs), so the
    // first report is in s_window when this returns, before the device selector enumerates.
    plDisplayWatchWindow(&s_windowSeq, s_window);
    [[maybe_unused]] const Window& w = CurrentWindow();
    DISPLAY_STATUS(
        "DISPLAY PLAYER: player mode (no canvas=WxH): window css {.1f}x{.1f} device {}x{} dpr {.3f}, "
        "screen {}x{}, render-scale cap {.2f}{}; start-up backing {}x{}",
        w.cssW, w.cssH, int(w.devW), int(w.devH), w.dpr, int(w.screenW), int(w.screenH), w.cap,
        w.renderScale > 1. ? ST::format(", supersampling {.2f}", w.renderScale) : ST::string(),
        Scaled(w.devW, Fraction(w)), Scaled(w.devH, Fraction(w)));
    plEmscriptenFrameHooks::Add(PollWindow);
}

float plEmscriptenDisplay::InitialScale()
{
    if (!PlayerMode())
        return float(emscripten_get_device_pixel_ratio());
    const Window& w = CurrentWindow();
    if (w.cssW <= 0.)
        return float(w.dpr);
    return float(double(Scaled(w.devW, Fraction(w))) / w.cssW);
}
