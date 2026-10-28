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

// Browser platform input; the existing input devices/interfaces implement gameplay.

#include <emscripten.h>
#include <emscripten/threading.h>

#include <array>
#include <cmath>

#include "plClient.h"
#include "plEmscriptenFrameHooks.h"
#include "plEmscriptenKeys.h"
#include "plInputCore/plAvatarInputInterface.h"
#include "plInputCore/plInputDevice.h"
#include "plInputCore/plInputManager.h"
#include "plMessage/plInputEventMsg.h"
#include "plPipeline.h"
#include "pnInputCore/plControlDefinition.h"

extern "C" {
void plInputInit();
void plInputDrain();
void plInputStatus(int ready, int active, int recenter, int buttons, int caps,
                   double x, double y, int width, int height, int clientThread);
}

namespace
{
struct InputState
{
    plClient*             client = nullptr;
    plInputManager*       manager = nullptr;
    std::array<bool, 256> held{};
    bool                  active = false, caps = false, locked = false;
    float                 x = .5f, y = .5f;
} s;
constexpr short kDown[] = {kLeftButtonDown, kMiddleButtonDown, kRightButtonDown};
constexpr short kUp[] = {kLeftButtonUp, kMiddleButtonUp, kRightButtonUp};

void Key(plKeyDef key, bool down, bool repeat = false, wchar_t text = 0)
{
    s.manager->HandleKeyEvent(key, down, repeat, text);
    if (key > 0) s.held[key] = down;
}
void Button(int button, bool down, int clicks = 0)
{
    plIMouseBEventMsg msg;
    msg.fButton = down ? kDown[button] : kUp[button];
    if (!down && clicks == 2) {
        if (button == 0) msg.fButton |= kLeftButtonDblClk;
        if (button == 2) msg.fButton |= kRightButtonDblClk;
    }
    s.manager->MsgReceive(&msg);
}
void Release()
{
    // Native activation releases ordinary keys and modifiers, retaining lock state.
    s.manager->Activate(false);
    s.held.fill(false);
    // The original mouse device does not implement HandleWindowActivate.
    const auto buttons = plMouseDevice::Instance()->GetButtonState();
    for (int i = 0; i < 3; ++i)
        if (buttons & kDown[i]) Button(i, false);
}
void Modifiers(int mods)
{
    const plKeyDef keys[] = {KEY_SHIFT, KEY_CTRL, KEY_ALT};
    for (int i = 0; i < 3; ++i) {
        const bool down = (mods & (1 << i)) != 0;
        if (s.held[keys[i]] != down) Key(keys[i], down);
    }
    // Unix HandleKeyEvent(KEY_CAPSLOCK) takes the lock state, not the physical edge.
    s.caps = (mods & 8) != 0;
    if (plKeyboardDevice::GetInstance()->IsCapsLockKeyOn() != s.caps)
        Key(KEY_CAPSLOCK, s.caps);
}
void Position(float x, float y)
{
    auto*             pipe = s.client->GetPipeline();
    plIMouseXEventMsg mx;
    plIMouseYEventMsg my;
    mx.fX = s.x = x;
    mx.fWx = static_cast<int>(x * pipe->Width());
    my.fY = s.y = y;
    my.fWy = static_cast<int>(y * pipe->Height());
    s.manager->MsgReceive(&mx);
    s.manager->MsgReceive(&my);
}
void Pump(void*)
{
    static bool initialized = false;
    if (!initialized) {
        plInputInit();
        initialized = true;
    }
    auto*      client = plClient::GetInstance();
    auto*      manager = client ? client->GetInputManager() : nullptr;
    const bool ready = client && !client->GetDone() && manager && client->GetPipeline() &&
                       plAvatarInputInterface::GetInstance() && plKeyboardDevice::GetInstance() &&
                       plMouseDevice::Instance();
    if (ready) {
        if (manager != s.manager) {
            s.client = client;
            s.manager = manager;
            s.held.fill(false);
            s.active = false;
            Release();
        }
        // StartClient may change activation between hooks. Browser focus remains authoritative.
        client->WindowActivate(s.active);
        plInputDrain(); // synchronous; never enters the engine from a DOM callback or JSPI suspension
        auto* mouse = plMouseDevice::Instance();
        auto* pipe = client->GetPipeline();
        plInputStatus(1, s.active, plInputManager::RecenterMouse(), mouse->GetButtonState(),
                      plKeyboardDevice::GetInstance()->IsCapsLockKeyOn(), mouse->GetCursorX(),
                      mouse->GetCursorY(), pipe->Width(), pipe->Height(), !emscripten_is_main_browser_thread());
    } else {
        plInputStatus(0, 0, 0, 0, 0, .5, .5, 0, 0, !emscripten_is_main_browser_thread());
    }
}
struct Register
{
    Register() { plEmscriptenFrameHooks::AddMessagePump(Pump); }
} registration;
} // namespace

extern "C" {
EMSCRIPTEN_KEEPALIVE void plInputKey(const char* code, int down, int repeat, int mods, int text)
{
    if (!s.active) return;
    Modifiers(mods);
    auto key = plEmscriptenInput::KeyFromDomCode(code);
    if (!text && (key == KEY_SHIFT || key == KEY_CTRL || key == KEY_ALT || key == KEY_CAPSLOCK)) return;
    if (text && (text < 32 || (text >= 127 && text <= 159) || text > 0x10ffff ||
                 (text >= 0xd800 && text <= 0xdfff))) return;
    if (key == KEY_UNMAPPED && !text) return;
    Key(key, down, repeat, static_cast<wchar_t>(text));
    if (down) s.client->SetQuitIntro(true);
}
EMSCRIPTEN_KEEPALIVE void plInputFocus(int active)
{
    if (!active) Release();
    s.active = active;
    s.client->WindowActivate(s.active);
    if (active) {
        // Activate(true) refreshes CapsLock to false on Unix; restore the last OS snapshot.
        s.manager->Activate(true);
        if (s.caps) Key(KEY_CAPSLOCK, true);
    }
}
EMSCRIPTEN_KEEPALIVE void plInputReset()
{
    Release();
    if (s.active) {
        s.manager->Activate(true);
        if (s.caps) Key(KEY_CAPSLOCK, true);
    }
}
EMSCRIPTEN_KEEPALIVE void plInputPointerLock(int locked)
{
    s.locked = locked;
    if (locked) {
        s.x = plMouseDevice::Instance()->GetCursorX();
        s.y = plMouseDevice::Instance()->GetCursorY();
    }
}
EMSCRIPTEN_KEEPALIVE void plInputMove(double x, double y, double dx, double dy, int relative)
{
    if (!s.active || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy)) return;
    if (relative && s.locked && plInputManager::RecenterMouse()) {
        // A browser cannot warp the OS cursor. Keep the original device centered and
        // deliver OS relative movement as canonical Plasma mouse messages (upstream
        // wheel input likewise sends plMouseEventMsg directly). No camera logic here.
        Position(.5f, .5f);
        for (int axis = 0; axis < 2; ++axis) {
            auto* msg = new plMouseEventMsg;
            msg->SetXPos(.5f);
            msg->SetYPos(.5f);
            msg->SetDX(axis == 0 ? -dx : 0);
            msg->SetDY(axis == 1 ? -dy : 0);
            msg->Send();
        }
        return;
    }
    if (relative && s.locked) {
        x = std::fmax(0., std::fmin(1., s.x + dx));
        y = std::fmax(0., std::fmin(1., s.y + dy));
    }
    // Bound malformed/very large coordinates before conversion to native pixel integers.
    x = std::fmax(-8., std::fmin(8., x));
    y = std::fmax(-8., std::fmin(8., y));
    Position(x, y);
    if (plInputManager::RecenterMouse()) {
        // Match PLSView.mm updateClientMouseLocation and Win32's cursor warp: a recentered control map must receive an
        // edge-axis position of 0.5. Browsers cannot warp an unlocked OS pointer. Keep native virtual coordinates in
        // its control box so an outside release exits that map.
        const double centeredX = (x <= .1 || x >= .9) ? .5 : x;
        const double centeredY = (y <= .1 || y >= .9) ? .5 : y;
        if (centeredX != x || centeredY != y) Position(centeredX, centeredY);
    }
}
EMSCRIPTEN_KEEPALIVE void plInputButton(int button, int down, int clicks)
{
    if (!s.active || button < 0 || button > 2) return;
    Button(button, down, clicks);
    if (down || clicks == 2) s.client->SetQuitIntro(true);
}
EMSCRIPTEN_KEEPALIVE void plInputWheel(double x, double y, double delta)
{
    if (!s.active || !std::isfinite(delta) || !delta || !std::isfinite(x) || !std::isfinite(y)) return;
    auto* msg = new plMouseEventMsg;
    // As in Cocoa, preserve OS magnitude; the engine's wheel controls use sign.
    msg->SetWheelDelta(delta);
    msg->SetButton(delta < 0 ? kWheelNeg : kWheelPos);
    msg->SetXPos(x);
    msg->SetYPos(y);
    msg->Send();
}
}
