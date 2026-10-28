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

#include <cstdio>
#include <cstring>
#include <string>
#include <string_theory/codecs>
#include <string_theory/format>
#include <string_theory/string>

#include "pfConsoleCore/pfServerIni.h"
#include "plFileSystem.h"

// The server.ini files a deployment of the browser client may serve, through the client's own
// parser (pfServerIni, as the native clients read it). The deployment tools and the page check
// these files with a stricter grammar of their own (web/deploy/plasma_web/server_ini.py,
// web/tests/shared_rules.json); the cases below are that grammar's, read here the way the client
// reads them. The key is the shared rules' fixture: the 64 bytes 0, 1, ..., 63.

static ST::string FixtureKeyBase64()
{
    uint8_t key[sizeof(NetDhKey)];
    for (size_t i = 0; i < sizeof(key); ++i)
        key[i] = (uint8_t)i;
    return ST::base64_encode(key, sizeof(key));
}

// The six key lines of the shared rules ($KEYS).
static std::string KeyLines()
{
    const std::string key = FixtureKeyBase64().c_str();
    std::string       lines;
    for (const char* server : {"Auth", "Game", "Gate"}) {
        for (const char* part : {"N", "X"})
            lines += std::string("Server.") + server + "." + part + " \"" + key + "\"\n";
    }
    return lines;
}

static bool IsFixtureKey(const NetDhKey& key)
{
    for (size_t i = 0; i < sizeof(NetDhKey); ++i) {
        if (key[i] != (uint8_t)i)
            return false;
    }
    return true;
}

// Parses `text` as a server.ini file.
static pfServerIni Parse(const std::string& text)
{
    static int s_serial = 0;
    plFileName path = plFileName::Join(testing::TempDir().c_str(),
                                       ST::format("pfServerIniTest-{}-{}.ini",
                                                  testing::UnitTest::GetInstance()->current_test_info()->name(),
                                                  ++s_serial));
    FILE*      f = plFileSystem::Open(path, "wb");
    EXPECT_NE(nullptr, f);
    if (f) {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
    pfServerIni ini;
    try {
        ini.Parse(path);
    } catch (...) {
        plFileSystem::Unlink(path);
        throw;
    }
    plFileSystem::Unlink(path);
    return ini;
}

static void ExpectFixtureKeys(const pfServerIni& ini)
{
    EXPECT_TRUE(IsFixtureKey(ini.fAuthDhConstants.n));
    EXPECT_TRUE(IsFixtureKey(ini.fAuthDhConstants.x));
    EXPECT_TRUE(IsFixtureKey(ini.fGameDhConstants.n));
    EXPECT_TRUE(IsFixtureKey(ini.fGameDhConstants.x));
    EXPECT_TRUE(IsFixtureKey(ini.fGateKeeperDhConstants.n));
    EXPECT_TRUE(IsFixtureKey(ini.fGateKeeperDhConstants.x));
}

TEST(pfServerIni, shard_layout)
{
    pfServerIni ini = Parse("# a shard\n" + KeyLines() +
                            "Server.Auth.Host \"192.0.2.174\"\nServer.Gate.Host \"192.0.2.174\"\n");
    ExpectFixtureKeys(ini);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fAuthHostname);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fGateKeeperHostname);
    EXPECT_EQ(ST::string(), ini.fFileHostname);
    // What the file leaves out keeps its default.
    EXPECT_EQ(kDefaultClientPort, ini.fPort);
    EXPECT_EQ(kNetAuthDefaultDhGValue, ini.fAuthDhConstants.g);
    EXPECT_EQ(kNetGameDefaultDhGValue, ini.fGameDhConstants.g);
    EXPECT_EQ(kNetGateKeeperDefaultDhGValue, ini.fGateKeeperDhConstants.g);
    EXPECT_EQ(ST::string(), ini.fStatusUrl);
    EXPECT_EQ(ST::string(), ini.fDisplayName);
}

TEST(pfServerIni, ports)
{
    EXPECT_EQ(14618u, Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\nServer.Port 14618\n").fPort);
    EXPECT_EQ(65535u, Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\nServer.Port \"65535\"\n").fPort);
}

TEST(pfServerIni, hosts)
{
    pfServerIni gate = Parse(KeyLines() + "Server.Gate.Host \"gate.example.test\"\n");
    EXPECT_EQ(ST_LITERAL("gate.example.test"), gate.fGateKeeperHostname);
    EXPECT_EQ(ST::string(), gate.fAuthHostname);

    pfServerIni file = Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\nServer.File.Host \"files.example.test\"\n");
    EXPECT_EQ(ST_LITERAL("files.example.test"), file.fFileHostname);

    pfServerIni empty = Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\nServer.File.Host \"\"\n");
    EXPECT_EQ(ST::string(), empty.fFileHostname);

    const std::string longHost(253, 'a');
    pfServerIni       longer = Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\nServer.File.Host \"" + longHost + "\"\n");
    EXPECT_EQ(ST::string(longHost.c_str()), longer.fFileHostname);
}

TEST(pfServerIni, line_ends)
{
    std::string crlf = "# a shard\r\n" + KeyLines() + "Server.Auth.Host \"192.0.2.174\"\r\nServer.Gate.Host \"192.0.2.174\"\r\n";
    pfServerIni a = Parse(crlf);
    ExpectFixtureKeys(a);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), a.fAuthHostname);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), a.fGateKeeperHostname);

    // No final line end, a trailing CR
    pfServerIni b = Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\"\r");
    ExpectFixtureKeys(b);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), b.fAuthHostname);
}

TEST(pfServerIni, comments_and_blank_lines)
{
    // A '#' line, a blank line, a line of spaces, an indented setting and comments after a value.
    pfServerIni ini = Parse("# one\n\n   \n" + KeyLines() +
                            "\tServer.Auth.Host \"192.0.2.174\"   # the auth server\n"
                            "Server.Gate.Host 192.0.2.174 ; bare\n"
                            "Server.Status \"https://status.example.test/s\" // trailing\n");
    ExpectFixtureKeys(ini);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fAuthHostname);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fGateKeeperHostname);
    EXPECT_EQ(ST_LITERAL("https://status.example.test/s"), ini.fStatusUrl);
}

TEST(pfServerIni, semicolon_and_slash_comment_lines)
{
    // The client skips only '#' lines (hsStream::ReadLn): a line that starts with ';' or "//" is
    // read as a setting named ";" or "//", which pfServerIni does not know.
    EXPECT_THROW(Parse(KeyLines() + "; two\nServer.Auth.Host \"192.0.2.174\"\n"), pfServerIniParseException);
    EXPECT_THROW(Parse(KeyLines() + "// three\nServer.Auth.Host \"192.0.2.174\"\n"), pfServerIniParseException);
}

TEST(pfServerIni, names_in_any_case)
{
    const std::string key = "\"" + std::string(FixtureKeyBase64().c_str()) + "\"";
    pfServerIni       ini = Parse("server.auth.n " + key + "\nSERVER.AUTH.X " + key + "\nServer.Game.n " + key +
                                  "\nServer.Game.x " + key + "\nserver.gate.N " + key + "\nserver.GATE.x " + key +
                                  "\nserver.auth.host 192.0.2.174\n");
    ExpectFixtureKeys(ini);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fAuthHostname);
}

TEST(pfServerIni, bare_values)
{
    // pfConsoleParser ends a bare value at '=' or ',' as at a space: a bare key loses its base-64
    // padding, and a bare URL its query.
    const std::string base = KeyLines() + "Server.Auth.Host 192.0.2.174\n";
    pfServerIni       ini = Parse(base + "Server.DispName shard\n");
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), ini.fAuthHostname);
    EXPECT_EQ(ST_LITERAL("shard"), ini.fDisplayName);
    EXPECT_THROW(Parse(base + "Server.Game.X " + FixtureKeyBase64().c_str() + "\n"), pfServerIniParseException);
    EXPECT_EQ(ST_LITERAL("http://status.example.test/s?x"),
              Parse(base + "Server.Status http://status.example.test/s?x=1\n").fStatusUrl);
    EXPECT_EQ(ST_LITERAL("a"), Parse(base + "Server.DispName a,b\n").fDisplayName);
}

TEST(pfServerIni, quoted_text)
{
    // Spaces and non-ASCII text in quotes, read as UTF-8.
    pfServerIni ini = Parse(KeyLines() +
                            "Server.Auth.Host \"192.0.2.174\"\n"
                            "Server.Status \"http://status.example.test/serverstatus?x=1\"\n"
                            "Server.Signup \"https://example.test/sign up\"\n"
                            "Server.DispName \"Shard d\xC3\xA9mo \xE2\x80\x94 test\"\n");
    EXPECT_EQ(ST_LITERAL("http://status.example.test/serverstatus?x=1"), ini.fStatusUrl);
    EXPECT_EQ(ST_LITERAL("https://example.test/sign up"), ini.fSignupUrl);
    EXPECT_EQ(ST::string::from_utf8("Shard d\xC3\xA9mo \xE2\x80\x94 test"), ini.fDisplayName);
}

TEST(pfServerIni, generators)
{
    pfServerIni ini = Parse(KeyLines() +
                            "Server.Auth.G 41\nServer.Game.G \"73\"\nServer.Gate.G 4\n"
                            "Server.Auth.Host \"192.0.2.174\"\n");
    EXPECT_EQ(41u, ini.fAuthDhConstants.g);
    EXPECT_EQ(73u, ini.fGameDhConstants.g);
    EXPECT_EQ(4u, ini.fGateKeeperDhConstants.g);
}

TEST(pfServerIni, accepted_by_the_client_only)
{
    // Files the deployment tools refuse (they check a stricter grammar, web/tests/shared_rules.json)
    // that the client reads without an error, and what it reads.
    const std::string key = FixtureKeyBase64().c_str();
    const std::string base = KeyLines() + "Server.Auth.Host \"192.0.2.174\"\n";

    EXPECT_EQ(ST_LITERAL("192.0.2.174"), Parse(KeyLines() + "Server.Auth.Host \"192.0.2.174\" \"192.0.2.175\"\n").fAuthHostname);
    EXPECT_EQ(ST_LITERAL("192.0.2.175"), Parse(base + "server.auth.host \"192.0.2.175\"\n").fAuthHostname);
    EXPECT_EQ(ST_LITERAL("192.0.2.174"), Parse(KeyLines() + "Server.Auth.Host=192.0.2.174\n").fAuthHostname);
    EXPECT_EQ(ST::string(), Parse(base + "Server.Status\n").fStatusUrl);
    EXPECT_EQ(0u, Parse(base + "Server.Port 0\n").fPort);
    EXPECT_EQ(65536u, Parse(base + "Server.Port 65536\n").fPort);
    EXPECT_EQ(014617u, Parse(base + "Server.Port 014617\n").fPort); // a leading zero reads as octal
    EXPECT_EQ(0u, Parse(base + "Server.Auth.G 0\n").fAuthDhConstants.g);
    EXPECT_EQ(4u, Parse(base + "Server.Gate.G 04\n").fGateKeeperDhConstants.g);
    EXPECT_EQ((unsigned)-73, Parse(base + "Server.Game.G -73\n").fGameDhConstants.g);
    EXPECT_EQ(ST_LITERAL("files.example!"), Parse(base + "Server.File.Host \"files.example!\"\n").fFileHostname);
    EXPECT_EQ(ST::string(std::string(254, 'a').c_str()),
              Parse(base + "Server.File.Host \"" + std::string(254, 'a') + "\"\n").fFileHostname);
    EXPECT_EQ(ST::string::from_utf8("demo\xC2\xA0shard"), Parse(base + "Server.DispName demo\xC2\xA0shard\n").fDisplayName);

    // No entry host, a missing key (it stays zero), comments only, an empty file.
    pfServerIni noHost = Parse(KeyLines() + "Server.File.Host \"files.example.test\"\n");
    EXPECT_EQ(ST::string(), noHost.fAuthHostname);
    EXPECT_EQ(ST::string(), noHost.fGateKeeperHostname);
    pfServerIni noKey = Parse("Server.Auth.N \"" + key + "\"\nServer.Auth.X \"" + key + "\"\nServer.Game.N \"" + key +
                              "\"\nServer.Gate.N \"" + key + "\"\nServer.Gate.X \"" + key +
                              "\"\nServer.Auth.Host \"192.0.2.174\"\n");
    for (uint8_t b : noKey.fGameDhConstants.x)
        EXPECT_EQ(0, b);
    EXPECT_EQ(ST::string(), Parse("# nothing\n").fAuthHostname);
    EXPECT_EQ(ST::string(), Parse("").fAuthHostname);
}

TEST(pfServerIni, refused)
{
    // What the client refuses: an unknown setting, a value it cannot read, an unterminated quote.
    const std::string base = KeyLines() + "Server.Auth.Host \"192.0.2.174\"\n";
    const std::string refused[] = {
        base + "Account.Username fixture\n",
        base + "Server.File.Port 1\n",
        base + "Server.Auth.Host \"192.0.2.174\n",
        base + "Server.Port auto\n",
        base + "Server.Game.X \"AAAA\"\n",
    };
    for (const std::string& text : refused)
        EXPECT_THROW(Parse(text), pfServerIniParseException) << text.substr(base.size());
}

TEST(pfServerIni, refused_separators)
{
    // pfConsoleParser separates a name from its value with spaces and tabs only, so these lines
    // are one unknown setting name: a no-break space, a form feed, a byte order mark.
    const std::string key = KeyLines();
    EXPECT_THROW(Parse(key + "Server.Auth.Host\xC2\xA0\"192.0.2.174\"\n"), pfServerIniParseException);
    EXPECT_THROW(Parse(key + "Server.Auth.Host\f\"192.0.2.174\"\n"), pfServerIniParseException);
    EXPECT_THROW(Parse("\xEF\xBB\xBFServer.Auth.Host \"192.0.2.174\"\n" + key), pfServerIniParseException);
    // A lone CR ends the line (hsStream::ReadLn): the setting has no value, and the value is a line
    // of its own, which is no setting.
    EXPECT_THROW(Parse(key + "Server.Auth.Host\r\"192.0.2.174\"\n"), pfServerIniParseException);
}

TEST(pfServerIni, missing_file)
{
    pfServerIni ini;
    EXPECT_THROW(ini.Parse(plFileName::Join(testing::TempDir().c_str(), "pfServerIniTest-no-such-file.ini")),
                 pfServerIniParseException);
}
