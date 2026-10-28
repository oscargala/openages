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

#include <cstdint>
#include <cstring>
#include <string>
#include <string_theory/string>

#include "pfPython/pyPy2Text.h"

// The py2 flavour's text codec: the engine's UTF-8 against the Windows-1252 `str` and the UCS-2
// `unicode` of MOULa's Python 2.3 scripts.

// Windows code page 1252 at 0x80-0x9F (the code page's best-fit table, MultiByteToWideChar). The
// five bytes it leaves undefined (0x81 0x8D 0x8F 0x90 0x9D) map to the C1 control of the same
// value, as MultiByteToWideChar maps them.
static const char32_t kWindows1252High[32] = {
    U'\u20AC', // € (0x80)
    0x81,
    U'\u201A', // ‚ (0x82)
    U'\u0192', // ƒ (0x83)
    U'\u201E', // „ (0x84)
    U'\u2026', // … (0x85)
    U'\u2020', // † (0x86)
    U'\u2021', // ‡ (0x87)
    U'\u02C6', // ˆ (0x88)
    U'\u2030', // ‰ (0x89)
    U'\u0160', // Š (0x8A)
    U'\u2039', // ‹ (0x8B)
    U'\u0152', // Œ (0x8C)
    0x8D,
    U'\u017D', // Ž (0x8E)
    0x8F,
    0x90,
    U'\u2018', // ‘ (0x91)
    U'\u2019', // ’ (0x92)
    U'\u201C', // “ (0x93)
    U'\u201D', // ” (0x94)
    U'\u2022', // • (0x95)
    U'\u2013', // – (0x96)
    U'\u2014', // — (0x97)
    U'\u02DC', // ˜ (0x98)
    U'\u2122', // ™ (0x99)
    U'\u0161', // š (0x9A)
    U'\u203A', // › (0x9B)
    U'\u0153', // œ (0x9C)
    0x9D,
    U'\u017E', // ž (0x9E)
    U'\u0178', // Ÿ (0x9F)
};

static std::string AllBytes()
{
    std::string bytes;
    for (int c = 0; c < 256; ++c)
        bytes += (char)c;
    return bytes;
}

TEST(pyPy2Text, cp1252_decode_every_byte)
{
    for (int c = 0; c < 256; ++c) {
        char32_t expected = (c >= 0x80 && c < 0xA0) ? kWindows1252High[c - 0x80] : (char32_t)c;
        EXPECT_EQ(expected, ICp1252Decode((unsigned char)c)) << "byte " << c;
    }
}

TEST(pyPy2Text, cp1252_undefined_bytes)
{
    // The bytes the code page leaves undefined keep their value, so they survive a round trip.
    for (unsigned char c : {0x81, 0x8D, 0x8F, 0x90, 0x9D}) {
        EXPECT_EQ((char32_t)c, ICp1252Decode(c));
        EXPECT_EQ((char)c, ICp1252Encode(c));
    }
}

TEST(pyPy2Text, cp1252_encode_inverts_decode)
{
    for (int c = 0; c < 256; ++c)
        EXPECT_EQ((char)c, ICp1252Encode(ICp1252Decode((unsigned char)c))) << "byte " << c;
}

TEST(pyPy2Text, cp1252_encode_without_a_byte)
{
    // WideCharToMultiByte's default character for what the code page lacks. The C1 controls of
    // the defined bytes 0x80-0x9F have no byte: 0x80 is the euro sign.
    for (char32_t cp : {(char32_t)0x80, (char32_t)0x9F, (char32_t)0x100, (char32_t)0x20AD,
                        (char32_t)0xFFFD, (char32_t)0x1F600, (char32_t)0x10FFFF})
        EXPECT_EQ('?', ICp1252Encode(cp)) << "U+" << std::hex << (uint32_t)cp;
}

TEST(pyPy2Text, utf8_encode_lengths)
{
    struct
    {
        char32_t    cp;
        const char* utf8;
    } cases[] = {
        {0x00, ""}, // NUL: one zero byte (checked below)
        {0x41, "A"},
        {0x7F, "\x7F"},
        {0x80, "\xC2\x80"},
        {0xE9, "\xC3\xA9"},
        {0x7FF, "\xDF\xBF"},
        {0x800, "\xE0\xA0\x80"},
        {0x20AC, "\xE2\x82\xAC"},
        {0xFFFF, "\xEF\xBF\xBF"},
        {0x10000, "\xF0\x90\x80\x80"},
        {0x1F600, "\xF0\x9F\x98\x80"},
        {0x10FFFF, "\xF4\x8F\xBF\xBF"},
    };
    for (const auto& c : cases) {
        std::string out;
        IAppendUtf8(out, c.cp);
        std::string expected = c.cp ? std::string(c.utf8) : std::string(1, '\0');
        EXPECT_EQ(expected, out) << "U+" << std::hex << (uint32_t)c.cp;
    }
}

TEST(pyPy2Text, utf8_decode_and_advance)
{
    const std::string    s = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t               i = 0;
    char32_t             cp = 0;
    EXPECT_TRUE(INextUtf8(p, s.size(), i, cp));
    EXPECT_EQ(U'A', cp);
    EXPECT_EQ(1u, i);
    EXPECT_TRUE(INextUtf8(p, s.size(), i, cp));
    EXPECT_EQ(U'\u00E9', cp);
    EXPECT_EQ(3u, i);
    EXPECT_TRUE(INextUtf8(p, s.size(), i, cp));
    EXPECT_EQ(U'\u20AC', cp);
    EXPECT_EQ(6u, i);
    EXPECT_TRUE(INextUtf8(p, s.size(), i, cp));
    EXPECT_EQ(U'\U0001F600', cp);
    EXPECT_EQ(10u, i);
}

TEST(pyPy2Text, utf8_decode_boundaries)
{
    // The first and last code point of every row of RFC 3629's table (section 4).
    struct
    {
        const char* bytes;
        char32_t    cp;
    } cases[] = {
        {"\xC2\x80", 0x80},
        {"\xDF\xBF", 0x7FF},
        {"\xE0\xA0\x80", 0x800},
        {"\xE0\xBF\xBF", 0xFFF},
        {"\xE1\x80\x80", 0x1000},
        {"\xEC\xBF\xBF", 0xCFFF},
        {"\xED\x80\x80", 0xD000},
        {"\xED\x9F\xBF", 0xD7FF},
        {"\xEE\x80\x80", 0xE000},
        {"\xEF\xBF\xBF", 0xFFFF},
        {"\xF0\x90\x80\x80", 0x10000},
        {"\xF0\xBF\xBF\xBF", 0x3FFFF},
        {"\xF1\x80\x80\x80", 0x40000},
        {"\xF3\xBF\xBF\xBF", 0xFFFFF},
        {"\xF4\x80\x80\x80", 0x100000},
        {"\xF4\x8F\xBF\xBF", 0x10FFFF},
    };
    for (const auto& c : cases) {
        size_t       i = 0;
        char32_t     cp = 0;
        const size_t n = strlen(c.bytes);
        EXPECT_TRUE(INextUtf8(reinterpret_cast<const unsigned char*>(c.bytes), n, i, cp)) << "case " << &c - cases;
        EXPECT_EQ(c.cp, cp) << "case " << &c - cases;
        EXPECT_EQ(n, i) << "case " << &c - cases;
    }
}

TEST(pyPy2Text, utf8_decode_rejected)
{
    // What UTF-8 forbids is rejected one byte at a time: the result is false, cp is the byte and i
    // moves past it alone, so a byte that cannot continue a sequence is left to start the next one.
    // The decoder never reads past n.
    struct
    {
        const char* bytes;
        size_t      n;
    } cases[] = {
        {"\x80", 1}, // a continuation byte without a lead
        {"\xBF", 1},
        {"\xC3", 1},     // a lead at the end
        {"\xE2\x82", 2}, // a sequence cut short
        {"\xF0\x9F\x98", 3},
        {"\xC3"
         "A",
         2}, // a lead before a byte that is not a continuation
        {"\xE2\x82"
         "A",
         3},
        {"\xF0\x9F\x98"
         "A",
         4},
        {"\xC3\xA9", 1}, // n ends inside the sequence
        {"\xC0\x80", 2}, // overlong forms: U+0000 in two, three and four bytes
        {"\xE0\x80\x80", 3},
        {"\xF0\x80\x80\x80", 4},
        {"\xC1\xBF", 2},         // U+007F in two bytes
        {"\xE0\x9F\xBF", 3},     // U+07FF in three
        {"\xF0\x8F\xBF\xBF", 4}, // U+FFFF in four
        {"\xED\xA0\x80", 3},     // encoded surrogates: U+D800, U+DBFF, U+DC00, U+DFFF
        {"\xED\xAF\xBF", 3},
        {"\xED\xB0\x80", 3},
        {"\xED\xBF\xBF", 3},
        {"\xF4\x90\x80\x80", 4}, // above U+10FFFF
        {"\xF5\x80\x80\x80", 4}, // leads that never start a sequence
        {"\xF7\xBF\xBF\xBF", 4},
        {"\xF8\x88\x80\x80\x80", 5},
        {"\xFC\x84\x80\x80\x80\x80", 6},
        {"\xFE", 1},
        {"\xFF", 1},
    };
    for (const auto& c : cases) {
        size_t   i = 0;
        char32_t cp = 0;
        EXPECT_FALSE(INextUtf8(reinterpret_cast<const unsigned char*>(c.bytes), c.n, i, cp)) << "case " << &c - cases;
        EXPECT_EQ((char32_t)(unsigned char)c.bytes[0], cp) << "case " << &c - cases;
        EXPECT_EQ(1u, i) << "case " << &c - cases;
    }

    // Every lead byte, before every second byte: a valid sequence decodes to what ST::string reads, and
    // nothing decodes to a surrogate or above U+10FFFF.
    for (int lead = 0; lead < 256; ++lead) {
        for (int second = 0; second < 256; ++second) {
            const unsigned char bytes[4] = {(unsigned char)lead, (unsigned char)second, 0x80, 0xBF};
            size_t              i = 0;
            char32_t            cp = 0;
            if (!INextUtf8(bytes, sizeof(bytes), i, cp)) {
                EXPECT_EQ((char32_t)lead, cp);
                EXPECT_EQ(1u, i);
                continue;
            }
            EXPECT_FALSE(cp >= 0xD800 && cp <= 0xDFFF) << std::hex << lead << " " << second;
            EXPECT_LE(cp, (char32_t)0x10FFFF) << std::hex << lead << " " << second;
            std::string again;
            IAppendUtf8(again, cp);
            EXPECT_EQ(std::string(reinterpret_cast<const char*>(bytes), i), again) << std::hex << lead << " " << second;
        }
    }
}

TEST(pyPy2Text, empty_strings)
{
    EXPECT_EQ(std::string(), ICp1252FromUtf8("", 0));
    EXPECT_EQ(std::u16string(), IUtf16FromUtf8("", 0));
    EXPECT_EQ(std::string(), IUtf8FromCp1252("", 0));
    // The length decides, not a terminator.
    EXPECT_EQ(std::string(), ICp1252FromUtf8("abc", 0));
    EXPECT_EQ(std::u16string(), IUtf16FromUtf8("abc", 0));
    EXPECT_EQ(std::string(), IUtf8FromCp1252("abc", 0));
}

TEST(pyPy2Text, embedded_nuls)
{
    const std::string utf8("a\0\xC3\xA9\0b", 6);
    EXPECT_EQ(std::string("a\0\xE9\0b", 5), ICp1252FromUtf8(utf8.data(), utf8.size()));
    EXPECT_EQ(std::u16string(u"a\0\u00E9\0b", 5), IUtf16FromUtf8(utf8.data(), utf8.size()));
    const std::string cp1252("a\0\xE9\0b", 5);
    EXPECT_EQ(utf8, IUtf8FromCp1252(cp1252.data(), cp1252.size()));
}

TEST(pyPy2Text, cp1252_round_trip_every_byte)
{
    // cp1252 -> UTF-8 -> cp1252 gives every byte string back (pyPy2Text.h).
    const std::string bytes = AllBytes();
    const std::string utf8 = IUtf8FromCp1252(bytes.data(), bytes.size());
    EXPECT_EQ(bytes, ICp1252FromUtf8(utf8.data(), utf8.size()));

    // ... through valid UTF-8, whose code points are the code page's
    ST::string       checked = ST::string::from_utf8(utf8.data(), utf8.size(), ST::check_validity);
    ST::utf32_buffer cps = checked.to_utf32();
    ASSERT_EQ(256u, cps.size());
    for (size_t c = 0; c < 256; ++c)
        EXPECT_EQ(ICp1252Decode((unsigned char)c), cps.data()[c]) << "byte " << c;
}

TEST(pyPy2Text, utf8_to_cp1252)
{
    const char* utf8 =
        "Shard d\xC3\xA9mo \xE2\x80\x94 \xE2\x80\x9CK\xC3\x8F\xE2\x80\x9D \xE2\x82\xAC"
        "5";
    EXPECT_EQ(std::string("Shard d\xE9mo \x97 \x93K\xCF\x94 \x80"
                          "5"),
              ICp1252FromUtf8(utf8, strlen(utf8)));

    // What the code page lacks becomes '?'; a byte UTF-8 rejects is kept.
    const char* other = "\xE2\x84\xA2\xE2\x84\xA3 \xF0\x9F\x98\x80 \x80 \xC3";
    EXPECT_EQ(std::string("\x99? ? \x80 \xC3"), ICp1252FromUtf8(other, strlen(other)));
}

TEST(pyPy2Text, cp1252_bytes_that_are_not_utf8)
{
    // A C string in cp1252 (CWE wrote such bytes, a STRING32 SDL variable for one) reaches a
    // script's `str` unchanged, and a `unicode` as the byte values, as CWE's clients read them.
    const char* cp1252 = "Caf\xE9 \x80 \x93q\x94 \xFF";
    EXPECT_EQ(std::string(cp1252), ICp1252FromUtf8(cp1252, strlen(cp1252)));
    EXPECT_EQ(std::u16string({'C', 'a', 'f', 0xE9, ' ', 0x80, ' ', 0x93, 'q', 0x94, ' ', 0xFF}),
              IUtf16FromUtf8(cp1252, strlen(cp1252)));

    // Forbidden forms, one byte at a time: an overlong '/', an encoded surrogate, U+110000.
    const char* forbidden = "\xC0\xAF|\xED\xA0\x80|\xF4\x90\x80\x80";
    EXPECT_EQ(std::string(forbidden), ICp1252FromUtf8(forbidden, strlen(forbidden)));
    EXPECT_EQ(std::u16string({0xC0, 0xAF, '|', 0xED, 0xA0, 0x80, '|', 0xF4, 0x90, 0x80, 0x80}),
              IUtf16FromUtf8(forbidden, strlen(forbidden)));
}

TEST(pyPy2Text, utf16_has_no_lone_surrogates)
{
    // Every one- and two-byte input, and every lead before two continuation bytes and a third: the
    // UTF-16 holds surrogates only in pairs.
    auto paired = [](const std::u16string& u) {
        for (size_t k = 0; k < u.size(); ++k) {
            if (u[k] >= 0xD800 && u[k] <= 0xDBFF) {
                if (k + 1 >= u.size() || u[k + 1] < 0xDC00 || u[k + 1] > 0xDFFF)
                    return false;
                ++k;
            } else if (u[k] >= 0xDC00 && u[k] <= 0xDFFF) {
                return false;
            }
        }
        return true;
    };
    for (int a = 0; a < 256; ++a) {
        for (int b = 0; b < 256; ++b) {
            const char two[2] = {(char)a, (char)b};
            ASSERT_TRUE(paired(IUtf16FromUtf8(two, 2))) << std::hex << a << " " << b;
            for (int c : {0x80, 0x8F, 0x90, 0xBF}) {
                const char four[4] = {(char)a, (char)b, (char)c, (char)0xBF};
                ASSERT_TRUE(paired(IUtf16FromUtf8(four, 4))) << std::hex << a << " " << b << " " << c;
            }
        }
    }
}

TEST(pyPy2Text, utf8_to_utf16)
{
    const char* utf8 = "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF\xEF\xBF\xBF";
    EXPECT_EQ(std::u16string(u"\u00E9\u20AC\U0001F600\U0010FFFF\uFFFF"), IUtf16FromUtf8(utf8, strlen(utf8)));
    EXPECT_EQ(std::u16string({0x00E9, 0x20AC, 0xD83D, 0xDE00, 0xDBFF, 0xDFFF, 0xFFFF}),
              IUtf16FromUtf8(utf8, strlen(utf8)));

    // A byte UTF-8 rejects becomes the code point of the same value.
    const char* bad =
        "a\x80"
        "b\xE2\x82";
    EXPECT_EQ(std::u16string({'a', 0x80, 'b', 0xE2, 0x82}), IUtf16FromUtf8(bad, strlen(bad)));
}

TEST(pyPy2Text, utf8_round_trip_through_utf16)
{
    // Every code point of the Basic Multilingual Plane but the surrogates, and a few above it,
    // decode to themselves (the scripts' unicode objects hold what the engine wrote).
    for (char32_t cp = 1; cp < 0x110000; cp = (cp < 0x10000) ? cp + 1 : cp + 0x1003) {
        if (cp >= 0xD800 && cp <= 0xDFFF)
            continue;
        std::string utf8;
        IAppendUtf8(utf8, cp);
        std::u16string utf16 = IUtf16FromUtf8(utf8.data(), utf8.size());
        ST::string     back = ST::string::from_utf16(utf16.data(), utf16.size(), ST::check_validity);
        ASSERT_EQ(1u, back.to_utf32().size()) << "U+" << std::hex << (uint32_t)cp;
        EXPECT_EQ(cp, back.to_utf32().data()[0]) << "U+" << std::hex << (uint32_t)cp;
    }
}
