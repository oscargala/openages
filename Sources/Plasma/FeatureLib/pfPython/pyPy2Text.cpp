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

// The py2 flavour's text codec (pyPy2Text.h): Windows-1252 and UTF-8, with no Python.

#include "pyPy2Text.h"

// ---------------------------------------------------------------------------------------------
// Windows-1252. 0x80-0x9F as Windows' code page 1252 (MultiByteToWideChar): the five bytes the
// code page leaves undefined (0x81 0x8D 0x8F 0x90 0x9D) map to the C1 code points of the same
// value, so every byte string round-trips. Every other byte is its Latin-1 code point.
static const char16_t s_cp1252High[32] = {
    0x20AC,
    0x0081,
    0x201A,
    0x0192,
    0x201E,
    0x2026,
    0x2020,
    0x2021,
    0x02C6,
    0x2030,
    0x0160,
    0x2039,
    0x0152,
    0x008D,
    0x017D,
    0x008F,
    0x0090,
    0x2018,
    0x2019,
    0x201C,
    0x201D,
    0x2022,
    0x2013,
    0x2014,
    0x02DC,
    0x2122,
    0x0161,
    0x203A,
    0x0153,
    0x009D,
    0x017E,
    0x0178,
};

char32_t ICp1252Decode(unsigned char c)
{
    return (c >= 0x80 && c < 0xA0) ? s_cp1252High[c - 0x80] : c;
}

// A code point to its cp1252 byte; '?' when the code page has none (WideCharToMultiByte's
// default character).
char ICp1252Encode(char32_t cp)
{
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF))
        return (char)cp;
    for (int i = 0; i < 32; ++i) {
        if ((char32_t)s_cp1252High[i] == cp)
            return (char)(0x80 + i);
    }
    return '?';
}

void IAppendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// One UTF-8 sequence at s[i] (pyPy2Text.h). The second byte's range for each lead is RFC 3629's
// table (section 4): E0 and F0 exclude the overlong forms, ED the surrogates, F4 everything above
// U+10FFFF; C0, C1 and F5-FF never start a sequence.
bool INextUtf8(const unsigned char* s, size_t n, size_t& i, char32_t& cp)
{
    const unsigned char c = s[i];
    cp = c;
    if (c < 0x80) {
        ++i;
        return true;
    }
    int           extra;
    unsigned char lo = 0x80, hi = 0xBF; // the second byte's range
    if (c >= 0xC2 && c <= 0xDF) {
        extra = 1;
    } else if (c >= 0xE0 && c <= 0xEF) {
        extra = 2;
        if (c == 0xE0)
            lo = 0xA0;
        else if (c == 0xED)
            hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        extra = 3;
        if (c == 0xF0)
            lo = 0x90;
        else if (c == 0xF4)
            hi = 0x8F;
    } else {
        ++i;
        return false;
    }
    if (i + extra >= n || s[i + 1] < lo || s[i + 1] > hi) {
        ++i;
        return false;
    }
    for (int k = 2; k <= extra; ++k) {
        if ((s[i + k] & 0xC0) != 0x80) {
            ++i;
            return false;
        }
    }
    char32_t v = c & (0x3F >> extra);
    for (int k = 1; k <= extra; ++k)
        v = (v << 6) | (s[i + k] & 0x3F);
    i += extra + 1;
    cp = v;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Strings

std::string ICp1252FromUtf8(const char* utf8, size_t n)
{
    std::string out;
    out.reserve(n);
    const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8);
    for (size_t i = 0; i < n;) {
        char32_t cp;
        out += INextUtf8(s, n, i, cp) ? ICp1252Encode(cp) : (char)cp;
    }
    return out;
}

std::u16string IUtf16FromUtf8(const char* utf8, size_t n)
{
    std::u16string out;
    out.reserve(n);
    const unsigned char* s = reinterpret_cast<const unsigned char*>(utf8);
    for (size_t i = 0; i < n;) {
        char32_t cp;
        if (INextUtf8(s, n, i, cp) && cp >= 0x10000) {
            // UCS-2 build (Py_UNICODE_SIZE 2): a surrogate pair, as Windows' UTF-16 wchar_t was
            cp -= 0x10000;
            out += (char16_t)(0xD800 + (cp >> 10));
            out += (char16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out += (char16_t)cp;
        }
    }
    return out;
}

std::string IUtf8FromCp1252(const char* cp1252, size_t n)
{
    const unsigned char* s = reinterpret_cast<const unsigned char*>(cp1252);
    std::string          out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
        IAppendUtf8(out, ICp1252Decode(s[i]));
    return out;
}
