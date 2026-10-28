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
#ifndef _pyGameMgrWire2_h_
#define _pyGameMgrWire2_h_

// The wire side of CWE's GameMgr (minigame) glue in the py2 flavour, defined in
// pyGameMgrWire2.cpp: the two messages the game manager posts to a script, the game types, the
// game clients and the live ones by game id, the requests CWE builds itself, and the GameMgr
// port that sends them. It includes no Python header: the Python classes over it are in
// pyGameCli2.cpp and pyGameMsg2.cpp, and pfPython2Creatable.h registers the two messages.
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfGameMgr (pfGameMgr.{h,cpp}, the games' pfGm*.cpp) and NucleusLib/pnGameMgr.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "pfGameMgr/pfGameCli.h"
#include "pfGameMgr/pfGmBlueSpiral.h"
#include "pfGameMgr/pfGmHeek.h"
#include "pfGameMgr/pfGmMarker.h"
#include "pfGameMgr/pfGmVarSync.h"
#include "plNetGameLib/plNetGameLib.h"
#include "pnGameMgr/pnGameMgr.h"
#include "pnKeyedObject/plKey.h"
#include "pnMessage/plMessage.h"
#include "pnUUID/pnUUID.h"

namespace plPy2CWE
{
namespace Game
{

class Cli;

// The message types ptGameCliMsg.getType returns for a game's own messages (CWE pyGameCliMsg.h,
// pyGameCliMsg::pyGameCliMsgType); the four core messages return their message id.
enum
{
    kPyGameCliMsgTypeStart = kCli2Srv_NumGameMsgIds,
    kPyGameCliTTTMsg,
    kPyGameCliHeekMsg,
    kPyGameCliMarkerMsg,
    kPyGameCliBlueSpiralMsg,
    kPyGameCliClimbingWallMsg,
    kPyGameCliVarSyncMsg,
};

// A copy of a server message, as CWE's pfGameMgrMsg::Set and pfGameCliMsg::Set take it
// (pfGameMgr.cpp), in a zeroed buffer at least as long as the largest message a script reads, so
// that a short message reads its missing fields as zero.
std::vector<uint8_t> ICopyMsg(const GameMsgHeader& msg);

} // namespace Game
} // namespace plPy2CWE

// ---------------------------------------------------------------------------------------------
// CWE pfGameMgr.h, pfGameMgrMsg and pfGameCliMsg. The class indices are H-uru's
// (plCreatableIndex.h). As in CWE, they are never read or written.
//
// No pfGameMgrMsg is posted in this flavour. CWE posted the server's InviteReceived and
// InviteRevoked to the keys of the scripts defining OnGameMgrMsg (IGameMgr::RecvInvite* in CWE
// pfGameMgr.cpp). H-uru's pfGameMgr reads a message with id 1 for the GameMgr as its own disconnect
// broadcast (Srv2Cli_Game_PlayerLeft, pfGameMgr.cpp Recv) and drops the rest, so an InviteReceived
// (also id 1) cannot be told apart there; the server never sends invites, and no module of MOULa's
// python.pak defines OnGameMgrMsg. The classes and the dispatch are CWE's.

class pfGameMgrMsg : public plMessage
{
public:
    std::vector<uint8_t> fNetMsg;

    pfGameMgrMsg(const GameMsgHeader& msg) : fNetMsg(plPy2CWE::Game::ICopyMsg(msg)) {}

    CLASSNAME_REGISTER(pfGameMgrMsg);
    GETINTERFACE_ANY(pfGameMgrMsg, plMessage);

    void Read(hsStream*, hsResMgr*) override { FATAL("not impl"); }
    void Write(hsStream*, hsResMgr*) override { FATAL("not impl"); }

    const GameMsgHeader* NetMsg() const { return (const GameMsgHeader*)fNetMsg.data(); }
};

class pfGameCliMsg : public plMessage
{
public:
    plPy2CWE::Game::Cli* fGameCli; // holds a reference (CWE: a raw pointer)
    std::vector<uint8_t> fNetMsg;

    pfGameCliMsg(plPy2CWE::Game::Cli* cli, const GameMsgHeader& msg);
    ~pfGameCliMsg();

    CLASSNAME_REGISTER(pfGameCliMsg);
    GETINTERFACE_ANY(pfGameCliMsg, plMessage);

    void Read(hsStream*, hsResMgr*) override { FATAL("not impl"); }
    void Write(hsStream*, hsResMgr*) override { FATAL("not impl"); }

    const GameMsgHeader* NetMsg() const { return (const GameMsgHeader*)fNetMsg.data(); }
};

namespace plPy2CWE
{
namespace Game
{

// ---------------------------------------------------------------------------------------------
// Game types: CWE's factories (s_factories and IGameMgr::CreateGameCli in pfGameMgr.cpp; each
// game's GameTypeReg). Tic-Tac-Toe and the climbing wall have a name (PtGetGameNameByTypeID) but no
// client: MOULa's scripts never play them, H-uru has no client for them and the server does not
// host them.

struct GameType
{
    const plUUID& typeId;
    const char*   name;
    int           cliMsgType; // CWE pyGameCliMsg::GetType for the game's own messages
    uint32_t      srvMsgEnd;  // one past the game's last Srv2Cli message id
    Cli* (*create)(const plKey& receiver);
};

// The type ids pnGameMgr does not define (CWE pnGmTicTacToe.h, pnGmClimbingWall.h).
extern const plUUID kGameTypeId_TicTacToe;
extern const plUUID kGameTypeId_ClimbingWall;

// The game type of a type id, or nullptr.
const GameType* IFindType(const plUUID& typeId);

// The game type each H-uru client serves. A client's fType is one of these objects, so a type
// compares by address.
template <class GameT>
const GameType& ITypeOf();
template <>
const GameType& ITypeOf<pfGmHeek>();
template <>
const GameType& ITypeOf<pfGmMarker>();
template <>
const GameType& ITypeOf<pfGmBlueSpiral>();
template <>
const GameType& ITypeOf<pfGmVarSync>();

// ---------------------------------------------------------------------------------------------
// A game client as CWE's scripts see it: pfGameCli plus IGameCli (CWE pfGameMgr.cpp).

class Cli
{
public:
    const GameType& fType;
    plKey           fReceiver;
    int             fPlayerCount = 0;
    bool            fDeleted = false;

    Cli(const GameType& type, const plKey& receiver) : fType(type), fReceiver(receiver) {}
    virtual ~Cli();

    virtual pfGameCli* GameCli() = 0;
    virtual void       Route(uint32_t gameId) = 0;

    uint32_t GameId() { return GameCli()->GetGameID(); }
    void     Ref() { GameCli()->Ref(); }
    void     UnRef() { GameCli()->UnRef(); }
};

// The live clients by game id, CWE's s_games (pfGameMgr.cpp), which PtGetGameIDs and PtGetGameCli
// read. A client leaves it when it is destroyed or its marker game is deleted.
extern std::map<uint32_t, Cli*> s_games;

// The live client of a game id, or nullptr.
Cli* IFindCli(uint32_t gameId);

// --------------------------------------------------------------------------------------------- The
// requests CWE's pfGameMgr builds itself (pfGameMgr::JoinGame, CreateGame and JoinCommonGame in
// pfGameMgr.cpp), sent through GameMgrPort::Join. Each is the whole message; transId, recvGameId
// and messageBytes are set again when it is sent (pfGameMgrTrans::ISend).

// pfGameMgr::JoinGame: a join of the game with this id.
std::vector<uint8_t> IBuildJoinGame(uint32_t gameId);
// pfGameMgr::JoinCommonGame: a join of a type's common game `gameNumber`, created if none exists.
std::vector<uint8_t> IBuildJoinCommonGame(const plUUID& typeId, uint32_t gameNumber,
                                          const void* initData, uint32_t initBytes);
// CWE pyMarkerGame::CreateMarkerGame: a new marker game.
std::vector<uint8_t> IBuildCreateMarkerGame(uint8_t gameType, const std::u16string& name,
                                            uint32_t timeLimit, const std::u16string& templateId);
// JoinCommonGame with the one-byte empty CreateParam of VarSync and BlueSpiral.
std::vector<uint8_t> IBuildJoinCommonEmptyParam(const plUUID& typeId, uint32_t gameNumber);

// The GameMgr itself (game id 0), through which the joins and creates are sent: H-uru's
// transactions are created through a pfGameCli (pfGameCli::SendTransaction).
class GameMgrPort : public pfGameCli
{
public:
    static GameMgrPort* Get()
    {
        static GameMgrPort* s_port = new GameMgrPort;
        return s_port;
    }

    // Sends a join or create request; on success, a client of the reply's game type is created
    // for `receiver` (CWE IGameMgr::RecvGameInstance). The replies come through H-uru's
    // pfGameMgr receive handler, which its static instance installs when it is constructed
    // (pfGameMgr.cpp), as CWE's does (AUTO_INIT_FUNC SetGameMgrMsgHandler in CWE pfGameMgr.cpp);
    // the browser client's launcher step installs it again after its own net session
    // (pfGameMgr::InstallRecvHandler).
    void Join(std::vector<uint8_t> request, const plKey& receiver);

protected:
    bool RecvGameMgrMsg(const GameMsgHeader*) override { return false; }
};

// A request a client sends to its own game (CWE pfGameCli::InvitePlayer, UninvitePlayer and
// LeaveGame in pfGameMgr.cpp; pfGmVarSync::RequestAllVars in pfGmVarSync.cpp).
template <typename MsgT>
void ISendGameMsg(Cli* cli, uint32_t messageId, MsgT& msg)
{
    msg.messageId = messageId;
    msg.transId = 0;
    msg.recvGameId = cli->GameId();
    msg.messageBytes = sizeof(msg);
    NetCliGameSendGameMgrMsg(&msg);
}

} // namespace Game
} // namespace plPy2CWE

#endif // _pyGameMgrWire2_h_
