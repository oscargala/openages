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
#ifndef _pyPy2Text_h_
#define _pyPy2Text_h_

// The text codec of the py2 flavour (pyPy2Compat.h): MOULa's Python 2.3 scripts see the engine's
// UTF-8 text as Windows-1252 `str` objects. These helpers hold no Python, so they are built and
// tested on their own (Sources/Tests/FeatureTests/pfPythonTest).

#include <cstddef>
#include <string>

// The code point of a Windows-1252 byte.
char32_t ICp1252Decode(unsigned char c);

// The Windows-1252 byte of a code point, or '?' when the code page has none.
char ICp1252Encode(char32_t cp);

// Appends a code point to `out` in UTF-8.
void IAppendUtf8(std::string& out, char32_t cp);

// Reads the UTF-8 sequence at s[i] of the n bytes at s (i < n) into cp and moves i past it, and
// returns true. Returns false for a byte that does not start a sequence UTF-8 allows (RFC 3629,
// section 4): a continuation byte, a lead 0xC0, 0xC1 or 0xF5-0xFF, a lead whose sequence is cut
// short by n or by a byte that is not a continuation, an overlong form, an encoded surrogate
// (U+D800-U+DFFF) or a code point above U+10FFFF. Then cp is that byte and i moves past it alone,
// so each byte of a rejected sequence is read on its own.
bool INextUtf8(const unsigned char* s, size_t n, size_t& i, char32_t& cp);

// The n bytes of UTF-8 at utf8 (NUL bytes included) as Windows-1252: what a script's `str`
// holds. A code point the code page lacks becomes '?'. A byte INextUtf8 rejects is kept as it
// is: the input can be a C string that is not UTF-8 (PyUnicode_FromString), and Cyan's client
// gave a script such bytes unchanged (CWE plPythonSDLModifier::ISDLVarIdxToPython,
// PyString_FromString).
std::string ICp1252FromUtf8(const char* utf8, size_t n);

// The n bytes of UTF-8 at utf8 as UTF-16, a code point above U+FFFF as a surrogate pair: what a
// script's `unicode` holds (the flavour's CPython is a UCS-2 build). A byte INextUtf8 rejects
// becomes the code point of the same value (U+0080-U+00FF), as Cyan's client widened a C string
// byte by byte (CWE hsUtils.cpp hsStringToWString, btowc in the C locale), so no lone surrogate
// is ever written.
std::u16string IUtf16FromUtf8(const char* utf8, size_t n);

// The n bytes of Windows-1252 at cp1252 (NUL bytes included) as UTF-8. Every byte has a code
// point, so the result is valid UTF-8 and ICp1252FromUtf8 gives the bytes back.
std::string IUtf8FromCp1252(const char* cp1252, size_t n);

#endif // _pyPy2Text_h_
