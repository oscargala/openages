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

#ifndef plEmscriptenKeys_inc
#define plEmscriptenKeys_inc
#include <string_view>

#include "pnInputCore/plKeyDef.h"

namespace plEmscriptenInput
{
// Use original engine constants, not DOM keyCode or a cast from HID usage.
// In the Unix table KEY_ENTER is 158, not USB Enter's 40 or keypad Enter's 88.
inline plKeyDef KeyFromDomCode(std::string_view code)
{
    constexpr plKeyDef letters[] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
                                    KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                    KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    constexpr plKeyDef digits[] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    constexpr plKeyDef numpad[] = {KEY_NUMPAD0, KEY_NUMPAD1, KEY_NUMPAD2, KEY_NUMPAD3,
                                   KEY_NUMPAD4, KEY_NUMPAD5, KEY_NUMPAD6, KEY_NUMPAD7, KEY_NUMPAD8, KEY_NUMPAD9};
    constexpr plKeyDef functions[] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                                      KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};
    if (code.size() == 4 && code.substr(0, 3) == "Key" && code[3] >= 'A' && code[3] <= 'Z')
        return letters[code[3] - 'A'];
    if (code.size() == 6 && code.substr(0, 5) == "Digit" && code[5] >= '0' && code[5] <= '9')
        return digits[code[5] - '0'];
    if (code.size() == 7 && code.substr(0, 6) == "Numpad" && code[6] >= '0' && code[6] <= '9')
        return numpad[code[6] - '0'];
    if (code.size() == 2 && code[0] == 'F' && code[1] >= '1' && code[1] <= '9')
        return functions[code[1] - '1'];
    if (code.size() == 3 && code[0] == 'F' && code[1] == '1' && code[2] >= '0' && code[2] <= '2')
        return functions[9 + code[2] - '0'];
    struct Mapping
    {
        std::string_view code;
        plKeyDef         key;
    };
    constexpr Mapping special[] = {
        {"Enter", KEY_ENTER},
        {"NumpadEnter", KEY_ENTER},
        {"Escape", KEY_ESCAPE},
        {"Backspace", KEY_BACKSPACE},
        {"Tab", KEY_TAB},
        {"Space", KEY_SPACE},
        {"Minus", KEY_DASH},
        {"Equal", KEY_EQUAL},
        {"BracketLeft", KEY_LBRACKET},
        {"BracketRight", KEY_RBRACKET},
        {"Backslash", KEY_BACKSLASH},
        {"Semicolon", KEY_SEMICOLON},
        {"Quote", KEY_QUOTE},
        {"Backquote", KEY_TILDE},
        {"Comma", KEY_COMMA},
        {"Period", KEY_PERIOD},
        {"Slash", KEY_SLASH},
        {"CapsLock", KEY_CAPSLOCK},
        {"PrintScreen", KEY_PRINTSCREEN},
        {"ScrollLock", KEY_SCROLLLOCK},
        {"Pause", KEY_PAUSE},
        {"Insert", KEY_INSERT},
        {"Home", KEY_HOME},
        {"PageUp", KEY_PAGEUP},
        {"Delete", KEY_DELETE},
        {"End", KEY_END},
        {"PageDown", KEY_PAGEDOWN},
        {"ArrowRight", KEY_RIGHT},
        {"ArrowLeft", KEY_LEFT},
        {"ArrowDown", KEY_DOWN},
        {"ArrowUp", KEY_UP},
        {"NumpadDivide", KEY_NUMPAD_DIVIDE},
        {"NumpadMultiply", KEY_NUMPAD_MULTIPLY},
        {"NumpadSubtract", KEY_NUMPAD_SUBTRACT},
        {"NumpadAdd", KEY_NUMPAD_ADD},
        {"NumpadDecimal", KEY_NUMPAD_PERIOD},
        {"ShiftLeft", KEY_SHIFT},
        {"ShiftRight", KEY_SHIFT},
        {"ControlLeft", KEY_CTRL},
        {"ControlRight", KEY_CTRL},
        {"AltLeft", KEY_ALT},
        {"AltRight", KEY_ALT},
    };
    for (const auto& entry : special)
        if (entry.code == code)
            return entry.key;
    return KEY_UNMAPPED;
}
} // namespace plEmscriptenInput

#endif // plEmscriptenKeys_inc
