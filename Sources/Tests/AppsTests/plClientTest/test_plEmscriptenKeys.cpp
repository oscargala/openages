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

#include <gtest/gtest.h>

#include <set>
#include <string_view>

#include "emscripten/plEmscriptenKeys.h"

// The browser client's key map (emscripten/plEmscriptenKeys.h): a DOM KeyboardEvent.code, which names a
// key by its place on the keyboard, to the engine's key. The expected key is the one the native
// Windows client gets for the same physical key: the virtual key of WM_KEYDOWN's wParam, read as a
// plKeyDef (winmain.cpp, plInputManager::HandleWin32ControlEvent; the Win32 table of
// plKeyDef.h), whose name is given beside each row. Names, not values, are compared, so the test
// holds for each platform's table of plKeyDef.h.

using plEmscriptenInput::KeyFromDomCode;

namespace
{
struct Row
{
    std::string_view code;
    plKeyDef         key;
};
} // namespace

static const Row kRows[] = {
    {"KeyA", KEY_A},
    {"KeyB", KEY_B},
    {"KeyC", KEY_C},
    {"KeyD", KEY_D},
    {"KeyE", KEY_E},
    {"KeyF", KEY_F},
    {"KeyG", KEY_G},
    {"KeyH", KEY_H},
    {"KeyI", KEY_I},
    {"KeyJ", KEY_J},
    {"KeyK", KEY_K},
    {"KeyL", KEY_L},
    {"KeyM", KEY_M},
    {"KeyN", KEY_N},
    {"KeyO", KEY_O},
    {"KeyP", KEY_P},
    {"KeyQ", KEY_Q},
    {"KeyR", KEY_R},
    {"KeyS", KEY_S},
    {"KeyT", KEY_T},
    {"KeyU", KEY_U},
    {"KeyV", KEY_V},
    {"KeyW", KEY_W},
    {"KeyX", KEY_X},
    {"KeyY", KEY_Y},
    {"KeyZ", KEY_Z}, // 'A'..'Z'
    {"Digit0", KEY_0},
    {"Digit1", KEY_1},
    {"Digit2", KEY_2},
    {"Digit3", KEY_3},
    {"Digit4", KEY_4},
    {"Digit5", KEY_5},
    {"Digit6", KEY_6},
    {"Digit7", KEY_7},
    {"Digit8", KEY_8},
    {"Digit9", KEY_9}, // '0'..'9'
    {"F1", KEY_F1},
    {"F2", KEY_F2},
    {"F3", KEY_F3},
    {"F4", KEY_F4},
    {"F5", KEY_F5},
    {"F6", KEY_F6},
    {"F7", KEY_F7},
    {"F8", KEY_F8},
    {"F9", KEY_F9},
    {"F10", KEY_F10},
    {"F11", KEY_F11},
    {"F12", KEY_F12}, // VK_F1..VK_F12
    {"Numpad0", KEY_NUMPAD0},
    {"Numpad1", KEY_NUMPAD1},
    {"Numpad2", KEY_NUMPAD2},
    {"Numpad3", KEY_NUMPAD3},
    {"Numpad4", KEY_NUMPAD4},
    {"Numpad5", KEY_NUMPAD5},
    {"Numpad6", KEY_NUMPAD6},
    {"Numpad7", KEY_NUMPAD7},
    {"Numpad8", KEY_NUMPAD8},
    {"Numpad9", KEY_NUMPAD9},                // VK_NUMPAD0..9
    {"NumpadDivide", KEY_NUMPAD_DIVIDE},     // VK_DIVIDE
    {"NumpadMultiply", KEY_NUMPAD_MULTIPLY}, // VK_MULTIPLY
    {"NumpadSubtract", KEY_NUMPAD_SUBTRACT}, // VK_SUBTRACT
    {"NumpadAdd", KEY_NUMPAD_ADD},           // VK_ADD
    {"NumpadDecimal", KEY_NUMPAD_PERIOD},    // VK_DECIMAL
    {"Enter", KEY_ENTER},                    // VK_RETURN
    {"NumpadEnter", KEY_ENTER},              // VK_RETURN (extended)
    {"Escape", KEY_ESCAPE},                  // VK_ESCAPE
    {"Backspace", KEY_BACKSPACE},            // VK_BACK
    {"Tab", KEY_TAB},                        // VK_TAB
    {"Space", KEY_SPACE},                    // VK_SPACE
    {"Minus", KEY_DASH},                     // VK_OEM_MINUS
    {"Equal", KEY_EQUAL},                    // VK_OEM_PLUS
    {"BracketLeft", KEY_LBRACKET},           // VK_OEM_4
    {"BracketRight", KEY_RBRACKET},          // VK_OEM_6
    {"Backslash", KEY_BACKSLASH},            // VK_OEM_5
    {"Semicolon", KEY_SEMICOLON},            // VK_OEM_1
    {"Quote", KEY_QUOTE},                    // VK_OEM_7
    {"Backquote", KEY_TILDE},                // VK_OEM_3
    {"Comma", KEY_COMMA},                    // VK_OEM_COMMA
    {"Period", KEY_PERIOD},                  // VK_OEM_PERIOD
    {"Slash", KEY_SLASH},                    // VK_OEM_2
    {"CapsLock", KEY_CAPSLOCK},              // VK_CAPITAL
    {"PrintScreen", KEY_PRINTSCREEN},        // VK_SNAPSHOT
    {"ScrollLock", KEY_SCROLLLOCK},          // VK_SCROLL
    {"Pause", KEY_PAUSE},                    // VK_PAUSE
    {"Insert", KEY_INSERT},                  // VK_INSERT
    {"Delete", KEY_DELETE},                  // VK_DELETE
    {"Home", KEY_HOME},                      // VK_HOME
    {"End", KEY_END},                        // VK_END
    {"PageUp", KEY_PAGEUP},                  // VK_PRIOR
    {"PageDown", KEY_PAGEDOWN},              // VK_NEXT
    {"ArrowUp", KEY_UP},                     // VK_UP
    {"ArrowDown", KEY_DOWN},                 // VK_DOWN
    {"ArrowLeft", KEY_LEFT},                 // VK_LEFT
    {"ArrowRight", KEY_RIGHT},               // VK_RIGHT
    {"ShiftLeft", KEY_SHIFT},
    {"ShiftRight", KEY_SHIFT}, // VK_SHIFT
    {"ControlLeft", KEY_CTRL},
    {"ControlRight", KEY_CTRL}, // VK_CONTROL
    {"AltLeft", KEY_ALT},
    {"AltRight", KEY_ALT}, // VK_MENU
};

// Every key plKeyDef names (its Win32 table, in order), KEY_UNMAPPED aside.
static const plKeyDef kNamedKeys[] = {
    KEY_A,
    KEY_B,
    KEY_C,
    KEY_D,
    KEY_E,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_I,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_M,
    KEY_N,
    KEY_O,
    KEY_P,
    KEY_Q,
    KEY_R,
    KEY_S,
    KEY_T,
    KEY_U,
    KEY_V,
    KEY_W,
    KEY_X,
    KEY_Y,
    KEY_Z,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_ESCAPE,
    KEY_TAB,
    KEY_SHIFT,
    KEY_CTRL,
    KEY_ALT,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_BACKSPACE,
    KEY_ENTER,
    KEY_PAUSE,
    KEY_CAPSLOCK,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_END,
    KEY_HOME,
    KEY_PRINTSCREEN,
    KEY_SCROLLLOCK,
    KEY_INSERT,
    KEY_DELETE,
    KEY_NUMPAD0,
    KEY_NUMPAD1,
    KEY_NUMPAD2,
    KEY_NUMPAD3,
    KEY_NUMPAD4,
    KEY_NUMPAD5,
    KEY_NUMPAD6,
    KEY_NUMPAD7,
    KEY_NUMPAD8,
    KEY_NUMPAD9,
    KEY_NUMPAD_MULTIPLY,
    KEY_NUMPAD_ADD,
    KEY_NUMPAD_SUBTRACT,
    KEY_NUMPAD_PERIOD,
    KEY_NUMPAD_DIVIDE,
    KEY_SPACE,
    KEY_COMMA,
    KEY_PERIOD,
    KEY_DASH,
    KEY_EQUAL,
    KEY_SEMICOLON,
    KEY_SLASH,
    KEY_TILDE,
    KEY_LBRACKET,
    KEY_BACKSLASH,
    KEY_RBRACKET,
    KEY_QUOTE,
};

TEST(plEmscriptenKeys, named_keys_are_distinct)
{
    // The comparisons below mean something only if the platform's table gives each name its own
    // value, none of them KEY_UNMAPPED.
    std::set<unsigned> values;
    for (plKeyDef key : kNamedKeys) {
        EXPECT_NE(KEY_UNMAPPED, key);
        values.insert((unsigned)key);
    }
#if HS_BUILD_FOR_APPLE
    // The macOS table gives KEY_DELETE the code of the key Macs label "delete", which is
    // backspace (kVK_Delete for both); forward delete is not named there.
    EXPECT_EQ(KEY_BACKSPACE, KEY_DELETE);
    EXPECT_EQ(std::size(kNamedKeys) - 1, values.size());
#else
    EXPECT_EQ(std::size(kNamedKeys), values.size());
#endif
}

TEST(plEmscriptenKeys, dom_codes)
{
    for (const Row& row : kRows)
        EXPECT_EQ(row.key, KeyFromDomCode(row.code)) << row.code;
}

TEST(plEmscriptenKeys, every_named_key_is_reachable)
{
    std::set<unsigned> reached;
    for (const Row& row : kRows)
        reached.insert((unsigned)KeyFromDomCode(row.code));
    for (plKeyDef key : kNamedKeys)
        EXPECT_EQ(1u, reached.count((unsigned)key)) << "plKeyDef " << (unsigned)key;
    EXPECT_EQ(0u, reached.count((unsigned)KEY_UNMAPPED));
}

TEST(plEmscriptenKeys, keys_without_a_name)
{
    // Keys for which plKeyDef has no name: the client gets KEY_UNMAPPED, and the bridge passes
    // such a key on only for the text it types (plEmscriptenInput.cpp plInputKey).
    for (std::string_view code : {"MetaLeft", "MetaRight", "ContextMenu", "NumLock", "IntlBackslash",
                                  "IntlRo", "IntlYen", "NumpadEqual", "NumpadComma", "F13", "F24",
                                  "AudioVolumeUp", "Lang1", "Fn", "Unidentified", ""})
        EXPECT_EQ(KEY_UNMAPPED, KeyFromDomCode(code)) << code;
}

TEST(plEmscriptenKeys, malformed_codes)
{
    // KeyboardEvent.code values are case-sensitive and exact.
    for (std::string_view code : {"Key", "KeyAA", "Keya", "keyA", "KEYA", "Key@", "Key[",
                                  "Digit", "Digit10", "DigitA", "Digit/", "Digit:",
                                  "Numpad", "NumpadA", "Numpad10", "numpad1",
                                  "F", "F0", "F13", "F19", "F1 ", " F1", "f1", "F01",
                                  "enter", "ENTER", "Enter ", "Arrow", "Shift", "Control", "Alt"})
        EXPECT_EQ(KEY_UNMAPPED, KeyFromDomCode(code)) << "'" << code << "'";
    // A code is the whole view: an embedded NUL does not end it.
    EXPECT_EQ(KEY_UNMAPPED, KeyFromDomCode(std::string_view("KeyA\0", 5)));
    EXPECT_EQ(KEY_UNMAPPED, KeyFromDomCode(std::string_view("Enter\0", 6)));
    EXPECT_EQ(KEY_A, KeyFromDomCode(std::string_view("KeyAB", 4)));
}
