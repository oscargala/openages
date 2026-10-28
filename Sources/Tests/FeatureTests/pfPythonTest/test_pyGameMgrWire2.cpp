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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pfPython/pyGameMgrWire2.h"
#include "pnGameMgr/pnGameMgr.h"
#include "pnGameMgr/pnGmBlueSpiral.h"
#include "pnGameMgr/pnGmHeek.h"
#include "pnGameMgr/pnGmMarker.h"
#include "pnGameMgr/pnGmVarSync.h"

// The creatables the GameMgr clients and the net client they reach need to link
#include "pfAnimation/pfAnimationCreatable.h"
#include "pfAudio/pfAudioCreatable.h"
#include "pfCamera/pfCameraCreatable.h"
#include "pfConditional/plConditionalObjectCreatable.h"
#include "pfGameMgr/pfGameMgr2Creatable.h"
#include "pfGameMgr/pfGameMgrCreatable.h"
#include "pfMessage/pfMessageCreatable.h"
#include "pfPython/pfPython2Creatable.h"
#include "plAllCreatables.h"
#include "pnAllCreatables.h"

using namespace plPy2CWE::Game;

// The GameMgr requests the py2 flavour builds itself (pyGameMgrWire2.h), byte for byte against
// the layout Cyan's client sends: CWE pfGameMgr::JoinGame, CreateGame and JoinCommonGame
// (pfGameMgr.cpp), pyMarkerGame::CreateMarkerGame, pyHeekGame::JoinCommonHeekGame,
// pyVarSyncGame::JoinCommonVarSyncGame and pyBlueSpiralGame::JoinCommonBlueSpiralGame; the structs
// GameMsgHeader, Cli2Srv_GameMgr_CreateGame and Cli2Srv_GameMgr_JoinGame (pnGameMgr.h) and
// Marker_CreateParam (pnGmMarker.h) (#pragma pack(1), a 2-byte wchar). Every field is little-endian; a GUID is in its Windows memory
// layout (Data1, Data2 and Data3 little-endian, Data4 as stored).

static void Put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back((uint8_t)(v >> (8 * i)));
}

static void PutHex(std::vector<uint8_t>& out, const char* hex)
{
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        char byte[3] = {p[0], p[1], 0};
        out.push_back((uint8_t)std::strtoul(byte, nullptr, 16));
    }
}

// The 16-byte message header (GameMsgHeader): messageId, transId, recvGameId, messageBytes.
static void PutHeader(std::vector<uint8_t>& out, uint32_t messageId, uint32_t messageBytes)
{
    Put32(out, messageId);
    Put32(out, 0);
    Put32(out, 0);
    Put32(out, messageBytes);
}

TEST(pyGameMgrWire2, join_common_heek)
{
    // PtJoinCommonHeekGame(key, 1): JoinCommonGame without create data, 44 bytes.
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_JoinGame, 44);
    Put32(e, 1);                                   // newGameId: the table number
    Put32(e, kGameJoinCommon);                     // createOptions
    PutHex(e, "e2c2839d357877449aaa22254c59a753"); // gameTypeId 9d83c2e2-7835-4477-9aaa-22254c59a753
    Put32(e, 0);                                   // createDataBytes
    EXPECT_EQ(e, IBuildJoinCommonGame(kGameTypeId_Heek, 1, nullptr, 0));
}

TEST(pyGameMgrWire2, join_common_varsync)
{
    // PtJoinCommonVarSyncGame(key): table 255 and a one-byte VarSync_CreateParam, 45 bytes.
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_JoinGame, 45);
    Put32(e, 255);
    Put32(e, kGameJoinCommon);
    PutHex(e, "9b2e5c4745a20641a0479b25d41ff333"); // 475c2e9b-a245-4106-a047-9b25d41ff333
    Put32(e, 1);
    e.push_back(0);
    EXPECT_EQ(e, IBuildJoinCommonEmptyParam(kGameTypeId_VarSync, 255));
}

TEST(pyGameMgrWire2, join_common_blue_spiral)
{
    // PtJoinCommonBlueSpiralGame(key, 1): a one-byte BlueSpiral_CreateParam, 45 bytes.
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_JoinGame, 45);
    Put32(e, 1);
    Put32(e, kGameJoinCommon);
    PutHex(e, "6581f95f3e91d14fa2c29c7f31be2cc8"); // 5ff98165-913e-4fd1-a2c2-9c7f31be2cc8
    Put32(e, 1);
    e.push_back(0);
    EXPECT_EQ(e, IBuildJoinCommonEmptyParam(kGameTypeId_BlueSpiral, 1));
}

TEST(pyGameMgrWire2, join_common_with_create_data)
{
    // JoinCommonGame copies the create data after createDataBytes: sizeof - 1 + initBytes.
    const uint8_t        init[] = {0x11, 0x22, 0x33};
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_JoinGame, 47);
    Put32(e, 7);
    Put32(e, kGameJoinCommon);
    PutHex(e, "e2c2839d357877449aaa22254c59a753");
    Put32(e, 3);
    e.insert(e.end(), std::begin(init), std::end(init));
    EXPECT_EQ(e, IBuildJoinCommonGame(kGameTypeId_Heek, 7, init, sizeof(init)));
}

TEST(pyGameMgrWire2, join_game)
{
    // PtJoinGame(key, 0x12345678): JoinGame without gameTypeId, createDataBytes and createData,
    // 24 bytes.
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_JoinGame, 24);
    Put32(e, 0x12345678);
    Put32(e, 0);
    EXPECT_EQ(e, IBuildJoinGame(0x12345678));
}

// CreateGame for a marker game: the header, gameTypeId, createOptions 0, createDataBytes 677 and
// a Marker_CreateParam: gameType (1 byte), gameName[256] and templateID[80] (UTF-16LE, NUL
// terminated, zero filled) around timeLimit (4 bytes), 717 bytes in all.
static std::vector<uint8_t> MarkerCreate(uint8_t gameType, const std::u16string& name,
                                         uint32_t timeLimit, const std::u16string& templateId)
{
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_CreateGame, 717);
    PutHex(e, "392c0b001903e14bb06c7a105b160fcf"); // 000b2c39-0319-4be1-b06c-7a105b160fcf
    Put32(e, 0);
    Put32(e, 677);
    std::vector<uint8_t> param(677, 0);
    param[0] = gameType;
    for (size_t i = 0; i < name.size() && i < 255; ++i) {
        param[1 + 2 * i] = (uint8_t)name[i];
        param[2 + 2 * i] = (uint8_t)(name[i] >> 8);
    }
    for (int i = 0; i < 4; ++i)
        param[513 + i] = (uint8_t)(timeLimit >> (8 * i));
    for (size_t i = 0; i < templateId.size() && i < 79; ++i) {
        param[517 + 2 * i] = (uint8_t)templateId[i];
        param[518 + 2 * i] = (uint8_t)(templateId[i] >> 8);
    }
    e.insert(e.end(), param.begin(), param.end());
    return e;
}

TEST(pyGameMgrWire2, create_marker_quest)
{
    // PtCreateMarkerGame(key, kMarkerGameQuest, u"Q\xe9", 60000, u"T")
    std::vector<uint8_t> e;
    PutHeader(e, kCli2Srv_GameMgr_CreateGame, 717);
    PutHex(e, "392c0b001903e14bb06c7a105b160fcf");
    Put32(e, 0);
    Put32(e, 677);
    std::vector<uint8_t> param(677, 0);
    param[0] = 0; // gameType
    param[1] = 'Q';
    param[3] = 0xe9; // gameName[256], UTF-16LE
    param[513] = 0x60;
    param[514] = 0xea; // timeLimit 60000
    param[517] = 'T';  // templateID[80]
    e.insert(e.end(), param.begin(), param.end());
    EXPECT_EQ(e, IBuildCreateMarkerGame((uint8_t)EMarkerGameType::kMarkerGameQuest, u"Q\u00E9", 60000, u"T"));
    EXPECT_EQ(e, MarkerCreate((uint8_t)EMarkerGameType::kMarkerGameQuest, u"Q\u00E9", 60000, u"T"));
}

TEST(pyGameMgrWire2, create_marker_games)
{
    // The other game types, a name outside Latin-1 and a made-up template id in the form the KI writes.
    const std::u16string tmpl = u"12345678-9abc-4def-8123-456789abcdef";
    EXPECT_EQ(MarkerCreate((uint8_t)EMarkerGameType::kMarkerGameCGZ, u"Bahro \u2014 caves", 0, tmpl),
              IBuildCreateMarkerGame((uint8_t)EMarkerGameType::kMarkerGameCGZ, u"Bahro \u2014 caves", 0, tmpl));
    EXPECT_EQ(MarkerCreate((uint8_t)EMarkerGameType::kMarkerGameCapture, u"\u0416", 0xFFFFFFFF, u""),
              IBuildCreateMarkerGame((uint8_t)EMarkerGameType::kMarkerGameCapture, u"\u0416", 0xFFFFFFFF, u""));
}

TEST(pyGameMgrWire2, create_marker_truncates)
{
    // StrCopy keeps room for the terminator: 255 name characters, 79 of the template id.
    const std::u16string name(300, u'n');
    const std::u16string tmpl(100, u't');
    std::vector<uint8_t> built = IBuildCreateMarkerGame((uint8_t)EMarkerGameType::kMarkerGameQuest, name, 1, tmpl);
    EXPECT_EQ(MarkerCreate((uint8_t)EMarkerGameType::kMarkerGameQuest, name, 1, tmpl), built);
    ASSERT_EQ(717u, built.size());
    EXPECT_EQ('n', built[40 + 1 + 2 * 254]);
    EXPECT_EQ(0, built[40 + 1 + 2 * 255]);
    EXPECT_EQ('t', built[40 + 517 + 2 * 78]);
    EXPECT_EQ(0, built[40 + 517 + 2 * 79]);
}

TEST(pyGameMgrWire2, game_types)
{
    // CWE's game types (the factories of IGameMgr::CreateGameCli in pfGameMgr.cpp) and their names
    // (pfGameMgr::GetGameNameByTypeId, PtGetGameNameByTypeID).
    struct
    {
        const plUUID& id;
        const char*   name;
    } types[] = {
        {kGameTypeId_TicTacToe, "Tic-Tac-Toe"},
        {kGameTypeId_Heek, "Heek"},
        {kGameTypeId_Marker, "Marker"},
        {kGameTypeId_BlueSpiral, "BlueSpiral"},
        {kGameTypeId_ClimbingWall, "ClimbingWall"},
        {kGameTypeId_VarSync, "VarSync"},
    };
    for (const auto& t : types) {
        const GameType* type = IFindType(t.id);
        ASSERT_NE(nullptr, type) << t.name;
        EXPECT_STREQ(t.name, type->name);
    }
    EXPECT_EQ(nullptr, IFindType(plUUID("00000000-0000-0000-0000-000000000001")));
    EXPECT_EQ(nullptr, IFindType(kNilUuid));

    // The games MOULa plays have a client; Tic-Tac-Toe and the climbing wall have none.
    EXPECT_EQ(&ITypeOf<pfGmHeek>(), IFindType(kGameTypeId_Heek));
    EXPECT_EQ(&ITypeOf<pfGmMarker>(), IFindType(kGameTypeId_Marker));
    EXPECT_EQ(&ITypeOf<pfGmBlueSpiral>(), IFindType(kGameTypeId_BlueSpiral));
    EXPECT_EQ(&ITypeOf<pfGmVarSync>(), IFindType(kGameTypeId_VarSync));
    EXPECT_EQ(nullptr, IFindType(kGameTypeId_TicTacToe)->create);
    EXPECT_EQ(nullptr, IFindType(kGameTypeId_ClimbingWall)->create);
}

TEST(pyGameMgrWire2, copy_short_message)
{
    // A server message is copied into a zeroed buffer at least as long as the largest message a
    // script reads, so the fields a short message lacks read as zero.
    Srv2Cli_Game_PlayerJoined joined{};
    joined.messageId = kSrv2Cli_Game_PlayerJoined;
    joined.transId = 3;
    joined.recvGameId = 9;
    joined.messageBytes = sizeof(joined);
    joined.playerId = 4321;
    std::vector<uint8_t> copy = ICopyMsg(joined);
    ASSERT_EQ(sizeof(Srv2Cli_VarSync_StringVarCreated), copy.size());
    EXPECT_EQ(0, memcmp(copy.data(), &joined, sizeof(joined)));
    for (size_t i = sizeof(joined); i < copy.size(); ++i)
        ASSERT_EQ(0, copy[i]) << "byte " << i;

    // A header that claims fewer bytes than the header itself still gives the header.
    GameMsgHeader bare{};
    bare.messageId = 2;
    bare.messageBytes = 4;
    copy = ICopyMsg(bare);
    EXPECT_EQ(0, memcmp(copy.data(), &bare, sizeof(bare)));
}
