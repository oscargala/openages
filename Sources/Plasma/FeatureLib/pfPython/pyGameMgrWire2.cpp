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
// The wire side of CWE's GameMgr (minigame) glue in the py2 flavour
// (pyGameMgrWire2.h). The py2 flavour's sources are compiled only when PLASMA_PYTHON_ABI=2
// (CMakeLists.txt); this file includes no Python header.
//
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
//   - FeatureLib/pfGameMgr/pfGameMgr.{h,cpp}: pfGameMgrMsg, pfGameCliMsg, the client registry
//     (IGameMgr::RecvGameInstance, pfGameMgr::GetGameIds/GetGameCli/GetGameNameByTypeId,
//     Add/RemoveReceiver, JoinGame/CreateGame/JoinCommonGame), IGameCli (player count, the four
//     core messages) and pfGameCli::InvitePlayer/UninvitePlayer/LeaveGame;
//   - FeatureLib/pfGameMgr/{Heek,Marker,BlueSpiral,VarSync}/pfGm*.cpp: each client posts every
//     server message to its receiver as a pfGameCliMsg; a marker game deletes itself on a
//     successful GameDeleted (IMarker::RecvGameDeleted in pfGmMarker.cpp).
// The Python classes, functions and enums over it are in pyGameCli2.cpp and pyGameMsg2.cpp.
//
// Transport. H-uru's pfGameMgr owns the GameMgr connection (NetCliGameSetRecvGameMgrMsgHandler)
// and routes each server message by transaction id, then by game id, to a pfGameCli
// (pfGameMgr.cpp RecvGameMgrMsg). The clients here are H-uru's pfGmMarker, pfGmBlueSpiral and
// pfGmVarSync, and pfGmHeek (pfGameMgr/pfGmHeek.h, the Heek requests alone), with CWE's receive
// side: Relay<> takes each message for its game before H-uru's handler dispatch and posts it, as
// CWE does, to the script's key. The requests
// the scripts send use H-uru's methods where their bytes are CWE's (the struct sizes are checked
// below); the joins and the marker create are built here with CWE's layout.

#include "pyGameMgrWire2.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "HeadSpin.h"
#include "pfGameMgr/pfGameCli.h"
#include "pfGameMgr/pfGameMgr.h"
#include "pfGameMgr/pfGameMgrTrans.h"
#include "pfGameMgr/pfGmBlueSpiral.h"
#include "pfGameMgr/pfGmHeek.h"
#include "pfGameMgr/pfGmMarker.h"
#include "pfGameMgr/pfGmVarSync.h"
#include "plNetClientComm/plNetClientComm.h"
#include "plNetGameLib/plNetGameLib.h"
#include "pnGameMgr/pnGameMgr.h"
#include "pnGameMgr/pnGmBlueSpiral.h"
#include "pnGameMgr/pnGmHeek.h"
#include "pnGameMgr/pnGmMarker.h"
#include "pnGameMgr/pnGmVarSync.h"
#include "pnKeyedObject/plKey.h"
#include "pnMessage/plMessage.h"
#include "pnUUID/pnUUID.h"
#include "pnUtils/pnUtStr.h"

// ---------------------------------------------------------------------------------------------
// The wire. CWE's structs (pnGm*.h, #pragma pack(1), 2-byte wchar) against H-uru's: each struct
// a script's messages or requests use has CWE's size. Only the sizes are asserted: these structs
// are not standard-layout, so offsetof does not apply to their fields.

static_assert(sizeof(GameMsgHeader) == 16, "CWE pnGameMgr.h, GameMsgHeader");
static_assert(sizeof(Cli2Srv_GameMgr_CreateGame) == 41,
              "CWE pnGameMgr.h, Cli2Srv_GameMgr_CreateGame (40 + createData[1])");
static_assert(sizeof(Cli2Srv_GameMgr_JoinGame) == 45,
              "CWE pnGameMgr.h, Cli2Srv_GameMgr_JoinGame (44 + createData[1])");
static_assert(sizeof(Srv2Cli_GameMgr_GameInstance) == 44, "CWE pnGameMgr.h, Srv2Cli_GameMgr_GameInstance");
static_assert(sizeof(Srv2Cli_GameMgr_InviteReceived) == 40 && sizeof(Srv2Cli_GameMgr_InviteRevoked) == 40,
              "CWE pnGameMgr.h, Srv2Cli_GameMgr_InviteReceived and InviteRevoked");
static_assert(sizeof(Cli2Srv_Game_LeaveGame) == 16 && sizeof(Cli2Srv_Game_Invite) == 20 &&
                  sizeof(Cli2Srv_Game_Uninvite) == 20,
              "CWE pnGameMgr.h, Cli2Srv_Game_LeaveGame, Invite and Uninvite");
static_assert(sizeof(Srv2Cli_Game_PlayerJoined) == 20 && sizeof(Srv2Cli_Game_PlayerLeft) == 20 &&
                  sizeof(Srv2Cli_Game_InviteFailed) == 28 && sizeof(Srv2Cli_Game_OwnerChange) == 20,
              "CWE pnGameMgr.h, Srv2Cli_Game_PlayerJoined, PlayerLeft, InviteFailed and OwnerChange");
static_assert(sizeof(Marker_CreateParam) == 677, "CWE pnGmMarker.h, Marker_CreateParam");
static_assert(sizeof(Srv2Cli_Marker_MarkerAdded) == 716 && sizeof(Srv2Cli_Marker_GameDeleted) == 17 &&
                  sizeof(Srv2Cli_Marker_MarkerCaptured) == 21 && sizeof(Srv2Cli_Marker_TemplateCreated) == 176,
              "CWE pnGmMarker.h");
static_assert(sizeof(Cli2Srv_Heek_PlayGame) == 533 && sizeof(Cli2Srv_Heek_LeaveGame) == 16 &&
                  sizeof(Cli2Srv_Heek_Choose) == 17 && sizeof(Cli2Srv_Heek_SeqFinished) == 17,
              "CWE pnGmHeek.h");
static_assert(sizeof(Srv2Cli_Heek_PlayGame) == 19 && sizeof(Srv2Cli_Heek_Welcome) == 536 &&
                  sizeof(Srv2Cli_Heek_Setup) == 24 && sizeof(Srv2Cli_Heek_PointUpdate) == 25 &&
                  sizeof(Srv2Cli_Heek_WinLose) == 18,
              "CWE pnGmHeek.h");
static_assert(sizeof(Srv2Cli_BlueSpiral_ClothOrder) == 23 && sizeof(Srv2Cli_BlueSpiral_GameStarted) == 17,
              "CWE pnGmBlueSpiral.h");
static_assert(sizeof(Cli2Srv_VarSync_RequestAllVars) == 16 && sizeof(Srv2Cli_VarSync_NumericVarChanged) == 28 &&
                  sizeof(Srv2Cli_VarSync_NumericVarCreated) == 540 && sizeof(Srv2Cli_VarSync_StringVarCreated) == 1044,
              "CWE pnGmVarSync.h");

// The largest server-to-client struct a script reads (Srv2Cli_VarSync_StringVarCreated).
static constexpr size_t kMaxSrvMsgBytes = sizeof(Srv2Cli_VarSync_StringVarCreated);

namespace plPy2CWE
{
namespace Game
{

std::vector<uint8_t> ICopyMsg(const GameMsgHeader& msg)
{
    std::vector<uint8_t> buf(std::max<size_t>(msg.messageBytes, kMaxSrvMsgBytes), 0);
    memcpy(buf.data(), &msg, std::max<size_t>(msg.messageBytes, sizeof(GameMsgHeader)));
    return buf;
}

// ---------------------------------------------------------------------------------------------
// Game types (pyGameMgrWire2.h)

const plUUID kGameTypeId_TicTacToe("a7236529-11d8-4758-9368-59cb43445a83");    // CWE pnGmTicTacToe.h
const plUUID kGameTypeId_ClimbingWall("6224cdf4-3556-4740-b7cd-d637562d07be"); // CWE pnGmClimbingWall.h

template <class GameT>
Cli* ICreate(const plKey& receiver);

static const GameType kTypes[] = {
    {kGameTypeId_TicTacToe, "Tic-Tac-Toe", kPyGameCliTTTMsg, 0, nullptr},
    {kGameTypeId_Heek, "Heek", kPyGameCliHeekMsg, kSrv2Cli_Heek_PointUpdate + 1, ICreate<pfGmHeek>},
    {kGameTypeId_Marker, "Marker", kPyGameCliMarkerMsg, kSrv2Cli_Marker_MarkerCaptured + 1, ICreate<pfGmMarker>},
    {kGameTypeId_BlueSpiral, "BlueSpiral", kPyGameCliBlueSpiralMsg, kSrv2Cli_BlueSpiral_GameStarted + 1, ICreate<pfGmBlueSpiral>},
    {kGameTypeId_ClimbingWall, "ClimbingWall", kPyGameCliClimbingWallMsg, 0, nullptr},
    {kGameTypeId_VarSync, "VarSync", kPyGameCliVarSyncMsg, kSrv2Cli_VarSync_NumericVarCreated + 1, ICreate<pfGmVarSync>},
};

const GameType* IFindType(const plUUID& typeId)
{
    for (const GameType& t : kTypes) {
        if (t.typeId == typeId)
            return &t;
    }
    return nullptr;
}

template <>
const GameType& ITypeOf<pfGmHeek>()
{
    return kTypes[1];
}
template <>
const GameType& ITypeOf<pfGmMarker>()
{
    return kTypes[2];
}
template <>
const GameType& ITypeOf<pfGmBlueSpiral>()
{
    return kTypes[3];
}
template <>
const GameType& ITypeOf<pfGmVarSync>()
{
    return kTypes[5];
}

// ---------------------------------------------------------------------------------------------
// The live clients (pyGameMgrWire2.h)

std::map<uint32_t, Cli*> s_games;

Cli::~Cli()
{
    for (auto it = s_games.begin(); it != s_games.end(); ++it) {
        if (it->second == this) {
            s_games.erase(it);
            break;
        }
    }
}

Cli* IFindCli(uint32_t gameId)
{
    auto it = s_games.find(gameId);
    return (it != s_games.end() && !it->second->fDeleted) ? it->second : nullptr;
}

} // namespace Game
} // namespace plPy2CWE

pfGameCliMsg::pfGameCliMsg(plPy2CWE::Game::Cli* cli, const GameMsgHeader& msg)
    : fGameCli(cli), fNetMsg(plPy2CWE::Game::ICopyMsg(msg))
{
    if (fGameCli)
        fGameCli->Ref();
}

pfGameCliMsg::~pfGameCliMsg()
{
    if (fGameCli)
        fGameCli->UnRef();
}

namespace plPy2CWE
{
namespace Game
{

// ---------------------------------------------------------------------------------------------
// The relay: an H-uru game client whose server messages go to the script as CWE's do.
//   - The four core messages (IGameCli::Recv in CWE pfGameMgr.cpp): Joined and Left count the
//     players, and each is posted to the receiver (IHeek::OnPlayerJoined and the other games'
//     same functions).
//   - The game's own messages: posted to the receiver (I<Game>::Recv*). An id outside the game's
//     range is CWE's DEFAULT_FATAL (CWE pfGmHeek::Recv and the other games' Recv); it is
//     dropped here.
//   - Marker GameDeleted with `failed` false: CWE deletes the client after posting the message
//     (IMarker::RecvGameDeleted). Here the client leaves H-uru's routing table through
//     pfGameCli's own "I left" path (pfGameCli.cpp Recv(Srv2Cli_Game_PlayerLeft)) and the live
//     list; the Python objects that hold it keep the memory, which CWE freed under them.
// H-uru's own handling (handler calls, its marker LeaveGame on GameDeleted) is not run: these
// clients have no handler.

template <class GameT>
class Relay : public GameT, public Cli
{
public:
    Relay(const plKey& receiver) : Cli(ITypeOf<GameT>(), receiver) {}

    pfGameCli* GameCli() override { return this; }
    void       Route(uint32_t gameId) override { this->SetGameId(gameId); }

protected:
    bool RecvGameMgrMsg(const GameMsgHeader* msg) override
    {
        uint32_t id = msg->messageId;
        bool     core = id < kSrv2Cli_NumGameMsgIds;
        if (!core && id >= fType.srvMsgEnd)
            return false;

        if (id == kSrv2Cli_Game_PlayerJoined)
            ++fPlayerCount;
        else if (id == kSrv2Cli_Game_PlayerLeft)
            --fPlayerCount;

        // The script may drop the last reference while it handles the message.
        this->pfGameCli::Ref();
        pfGameCliMsg* gameCliMsg = new pfGameCliMsg(this, *msg);
        gameCliMsg->Send(fReceiver);

        if (fType.typeId == kGameTypeId_Marker && id == kSrv2Cli_Marker_GameDeleted &&
            !((const Srv2Cli_Marker_GameDeleted*)msg)->failed && !fDeleted)
            IDelete();
        this->pfGameCli::UnRef();
        return true;
    }

private:
    void IDelete()
    {
        fDeleted = true;
        auto it = s_games.find(this->GetGameID());
        if (it != s_games.end() && it->second == this)
            s_games.erase(it);
        if (const NetCommPlayer* player = NetCommGetPlayer()) {
            Srv2Cli_Game_PlayerLeft left{};
            left.messageId = kSrv2Cli_Game_PlayerLeft;
            left.recvGameId = this->GetGameID();
            left.messageBytes = sizeof(left);
            left.playerId = player->playerInt;
            this->pfGameCli::RecvGameMgrMsg(&left);
        }
    }
};

template <class GameT>
Cli* ICreate(const plKey& receiver)
{
    return new Relay<GameT>(receiver);
}

// --------------------------------------------------------------------------------------------- The
// requests CWE's pfGameMgr builds (pfGameMgr::JoinGame, CreateGame and JoinCommonGame in
// pfGameMgr.cpp). transId, recvGameId (0, the GameMgr) and messageBytes are set again when the
// request is sent (pfGameMgrTrans::ISend).

static void IHeader(std::vector<uint8_t>& buf, uint32_t messageId)
{
    GameMsgHeader* hdr = (GameMsgHeader*)buf.data();
    hdr->messageId = messageId;
    hdr->transId = 0;
    hdr->recvGameId = 0;
    hdr->messageBytes = (uint32_t)buf.size();
}

// pfGameMgr::JoinGame: the message without gameTypeId, createDataBytes and createData (24 bytes).
std::vector<uint8_t> IBuildJoinGame(uint32_t gameId)
{
    std::vector<uint8_t> buf(sizeof(Cli2Srv_GameMgr_JoinGame) - sizeof(plUUID) - sizeof(uint32_t) - 1, 0);
    IHeader(buf, kCli2Srv_GameMgr_JoinGame);
    Cli2Srv_GameMgr_JoinGame* msg = (Cli2Srv_GameMgr_JoinGame*)buf.data();
    msg->newGameId = gameId;
    return buf;
}

// pfGameMgr::JoinCommonGame: sizeof - sizeof(createData) + initBytes.
std::vector<uint8_t> IBuildJoinCommonGame(const plUUID& typeId, uint32_t gameNumber,
                                          const void* initData, uint32_t initBytes)
{
    std::vector<uint8_t> buf(sizeof(Cli2Srv_GameMgr_JoinGame) - 1 + initBytes, 0);
    IHeader(buf, kCli2Srv_GameMgr_JoinGame);
    Cli2Srv_GameMgr_JoinGame* msg = (Cli2Srv_GameMgr_JoinGame*)buf.data();
    msg->gameTypeId = typeId;
    msg->newGameId = gameNumber;
    msg->createOptions = kGameJoinCommon;
    msg->createDataBytes = initBytes;
    if (initBytes)
        memcpy(msg->createData, initData, initBytes);
    return buf;
}

// pfGameMgr::CreateGame: sizeof - sizeof(createData) + initBytes.
static std::vector<uint8_t> IBuildCreateGame(const plUUID& typeId, uint32_t createOptions,
                                             const void* initData, uint32_t initBytes)
{
    std::vector<uint8_t> buf(sizeof(Cli2Srv_GameMgr_CreateGame) - 1 + initBytes, 0);
    IHeader(buf, kCli2Srv_GameMgr_CreateGame);
    Cli2Srv_GameMgr_CreateGame* msg = (Cli2Srv_GameMgr_CreateGame*)buf.data();
    msg->gameTypeId = typeId;
    msg->createOptions = createOptions;
    msg->createDataBytes = initBytes;
    if (initBytes)
        memcpy(msg->createData, initData, initBytes);
    return buf;
}

// CWE pyMarkerGame::CreateMarkerGame (pyMarkerGame.cpp): CreateGame with options 0 and a
// Marker_CreateParam.
std::vector<uint8_t> IBuildCreateMarkerGame(uint8_t gameType, const std::u16string& name,
                                            uint32_t timeLimit, const std::u16string& templateId)
{
    Marker_CreateParam init;
    memset(&init, 0, sizeof(init));
    init.gameType = (EMarkerGameType)gameType;
    StrCopy(init.gameName, name.c_str(), std::size(init.gameName));
    init.timeLimit = timeLimit;
    StrCopy(init.templateID, templateId.c_str(), std::size(init.templateID));
    return IBuildCreateGame(kGameTypeId_Marker, 0, &init, sizeof(init));
}

// CWE pyVarSyncGame::JoinCommonVarSyncGame and pyBlueSpiralGame::JoinCommonBlueSpiralGame send
// their empty CreateParam (one byte); Heek sends none (pyHeekGame::JoinCommonHeekGame).
std::vector<uint8_t> IBuildJoinCommonEmptyParam(const plUUID& typeId, uint32_t gameNumber)
{
    VarSync_CreateParam init;
    static_assert(sizeof(VarSync_CreateParam) == 1 && sizeof(BlueSpiral_CreateParam) == 1,
                  "an empty CreateParam is one byte, as MSVC's");
    uint8_t zero = 0;
    (void)init;
    return IBuildJoinCommonGame(typeId, gameNumber, &zero, sizeof(zero));
}

// --------------------------------------------------------------------------------------------- The
// join transaction: CWE's JoinTransState and IGameMgr::RecvGameInstance (pfGameMgr.cpp). On
// success, a client of the reply's game type is created for the receiver, unless one already serves
// that game id, which CWE reuses. H-uru's table holds the client from then on
// (pfGameCli::SetGameId); a new client it does not take (the id is taken) is released here.

class JoinTrans : public pfGameMgrTrans
{
    friend class ::pfGameCli;

    std::vector<uint8_t> fRequest;
    plKey                fReceiver;
    EGameJoinError       fResult = EGameJoinError::kGameJoinPending;
    plUUID               fReplyTypeId;
    uint32_t             fNewGameId = 0;

public:
    JoinTrans(std::vector<uint8_t> request, const plKey& receiver)
        : fRequest(std::move(request)), fReceiver(receiver) {}

    void Send() override
    {
        ISend((GameMsgHeader*)fRequest.data(), fRequest.size());
    }

    void Recv(const GameMsgHeader* msg) override
    {
        if (msg->messageId != kSrv2Cli_GameMgr_GameInstance)
            return;
        std::vector<uint8_t>                copy = ICopyMsg(*msg);
        const Srv2Cli_GameMgr_GameInstance& reply = *(const Srv2Cli_GameMgr_GameInstance*)copy.data();
        fResult = reply.result;
        fReplyTypeId = reply.gameTypeId;
        fNewGameId = reply.newGameId;
    }

    void Post() override
    {
        if (fResult != EGameJoinError::kGameJoinSuccess)
            return;
        const GameType* type = IFindType(fReplyTypeId);
        if (!type || !type->create)
            return;
        Cli*       cli = type->create(fReceiver);
        pfGameCli* gameCli = cli->GameCli();
        cli->Route(fNewGameId);
        // Taken by H-uru's table: this client serves the id from now on (the client CWE would
        // have found under the id, if any, is gone from the table with its connection).
        if (gameCli->RefCnt() > 1)
            s_games[fNewGameId] = cli;
        gameCli->UnRef();
    }
};

void GameMgrPort::Join(std::vector<uint8_t> request, const plKey& receiver)
{
    SendTransaction<JoinTrans>(std::move(request), receiver);
}

} // namespace Game
} // namespace plPy2CWE
