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
// The messages of CWE's GameMgr glue in the py2 flavour: the ptGameMgrMsg and ptGameCliMsg
// classes a script's OnGameMgrMsg and OnGameCliMsg receive, and each game's message classes.
// plPythonFileMod hands a GameMgr message to the script through GameMsgHandlerName and GameMsgNew
// (pyGameMgr2.h); InstallGameClasses adds the classes to PlasmaGame (IMsgClasses).
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfPython/Games (pyGameMgrMsg*.cpp, pyGameCliMsg*.cpp and each game's message glue).

#include "pyPy2Compat.h"
#if PLASMA_PYTHON_ABI < 3

#include <cstdint>

#include "pfGameMgr/pfGmBlueSpiral.h"
#include "pfGameMgr/pfGmHeek.h"
#include "pfGameMgr/pfGmMarker.h"
#include "pfGameMgr/pfGmVarSync.h"
#include "pnGameMgr/pnGameMgr.h"
#include "pnGameMgr/pnGmBlueSpiral.h"
#include "pnGameMgr/pnGmHeek.h"
#include "pnGameMgr/pnGmMarker.h"
#include "pnGameMgr/pnGmVarSync.h"
#include "pnMessage/plMessage.h"
#include "pyGameGlue2.h"
#include "pyGameMgr2.h"
#include "pyGameMgrWire2.h"
#include "pyGlueHelpers.h"

namespace plPy2CWE
{
namespace Game
{

pyGameMsg2::~pyGameMsg2()
{
    if (fMsg)
        fMsg->UnRef();
}

template <size_t N>
static PyObject* IWireString(const char16_t (&s)[N])
{
    static_assert(sizeof(Py_UNICODE) == sizeof(char16_t), "the py2 flavour is a UCS-2 build");
    size_t n = 0;
    while (n < N && s[n])
        ++n;
    return PyUnicode_FromUnicode(reinterpret_cast<const Py_UNICODE*>(s), (int)n);
}

// ---------------------------------------------------------------------------------------------
// The messages (CWE pyGameMgrMsg*.cpp, pyGameCliMsg*.cpp, Games/*/py*Msg*.cpp). A message
// object of a final class reads its struct; CWE's final and per-game constructors keep the
// message only when its id or game type matches (pyGameCliPlayerJoinedMsg's constructor in
// pyGameCliMsg.cpp, and the others),
// which the upcasts below always give.

// The object of any message class: each class's (PYTHON_CLASS_DEFINITION) holds its family's glue
// class, all of them a pyGameMsg2.
struct IGameMsgObject
{
    PyObject_HEAD pyGameMsg2* fThis;
};

static PyObject* IWrapMsg(PyTypeObject* type, plMessage* msg)
{
    IGameMsgObject* obj = (IGameMsgObject*)type->tp_new(type, nullptr, nullptr);
    if (obj && msg) {
        obj->fThis->fMsg = msg;
        msg->Ref();
    }
    return (PyObject*)obj;
}

static pfGameCliMsg* ICliMsg(const pyGameMsg2* glue)
{
    return glue->fMsg ? pfGameCliMsg::ConvertNoRef(glue->fMsg) : nullptr;
}

static pfGameMgrMsg* IMgrMsg(const pyGameMsg2* glue)
{
    return glue->fMsg ? pfGameMgrMsg::ConvertNoRef(glue->fMsg) : nullptr;
}

template <typename T>
static const T* ICliMsgAs(const pyGameMsg2* glue)
{
    pfGameCliMsg* msg = ICliMsg(glue);
    return msg ? (const T*)msg->NetMsg() : nullptr;
}

template <typename T>
static const T* IMgrMsgAs(const pyGameMsg2* glue)
{
    pfGameMgrMsg* msg = IMgrMsg(glue);
    return msg ? (const T*)msg->NetMsg() : nullptr;
}

// A getter on a final message: `expr` reads `m`, or `none` without a message.
#define PY2_CLI_MSG_GETTER(cls, method, MsgT, expr, none) \
    PYTHON_METHOD_DEFINITION_NOARGS(cls, method)          \
    {                                                     \
        const MsgT* m = ICliMsgAs<MsgT>(self->fThis);     \
        if (!m)                                           \
            return none;                                  \
        return expr;                                      \
    }

// A final message class, derived from its family's base class; the methods end with a comma.
#define PY2_GAME_MSG_FINAL(cls, glueClass, doc, ...) \
    PYTHON_START_METHODS_TABLE(cls)                  \
    __VA_ARGS__                                      \
    PYTHON_END_METHODS_TABLE;                        \
    PYTHON_DEFAULT_NEW_DEFINITION(cls, glueClass)    \
    PYTHON_DEFAULT_DEALLOC_DEFINITION(cls)           \
    PYTHON_NO_INIT_DEFINITION(cls)                   \
    PLASMA_DEFAULT_TYPE_WBASE(cls, glueClass, doc);

// In each family the final classes come before the base class, whose upcasts return their types;
// ptGameCliMsg, whose upcasts return the core finals and each game's base class, comes last.

// ptGameMgrMsg (CWE pyGameMgrMsg*.cpp)
PYTHON_CLASS_DEFINITION(ptGameMgrMsg, pyGameMgrMsg2);
PYTHON_CLASS_DEFINITION(ptGameMgrInviteReceivedMsg, pyGameMgrMsg2);
PYTHON_CLASS_DEFINITION(ptGameMgrInviteRevokedMsg, pyGameMgrMsg2);

PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteReceivedMsg, inviterID)
{
    const Srv2Cli_GameMgr_InviteReceived* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteReceived>(self->fThis);
    return IULong(m ? m->inviterId : 0);
}
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteReceivedMsg, gameTypeID)
{
    const Srv2Cli_GameMgr_InviteReceived* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteReceived>(self->fThis);
    return m ? PyPlasma_UnicodeFromSTString(m->gameTypeId.AsString()) : IEmptyUnicode();
}
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteReceivedMsg, newGameID)
{
    const Srv2Cli_GameMgr_InviteReceived* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteReceived>(self->fThis);
    return IULong(m ? m->newGameId : 0);
}
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteRevokedMsg, inviterID)
{
    const Srv2Cli_GameMgr_InviteRevoked* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteRevoked>(self->fThis);
    return IULong(m ? m->inviterId : 0);
}
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteRevokedMsg, gameTypeID)
{
    const Srv2Cli_GameMgr_InviteRevoked* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteRevoked>(self->fThis);
    return m ? PyPlasma_UnicodeFromSTString(m->gameTypeId.AsString()) : IEmptyUnicode();
}
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrInviteRevokedMsg, newGameID)
{
    const Srv2Cli_GameMgr_InviteRevoked* m = IMgrMsgAs<Srv2Cli_GameMgr_InviteRevoked>(self->fThis);
    return IULong(m ? m->newGameId : 0);
}

PY2_GAME_MSG_FINAL(ptGameMgrInviteReceivedMsg, pyGameMgrMsg2, "Game manager message when an invite is received",
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteReceivedMsg, inviterID, "Returns the inviter's ID number"),
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteReceivedMsg, gameTypeID, "Returns the game type ID (as a guid string)"),
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteReceivedMsg, newGameID, "Returns the new game's ID number"), )
PY2_GAME_MSG_FINAL(ptGameMgrInviteRevokedMsg, pyGameMgrMsg2, "Game manager message when an invite is received",
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteRevokedMsg, inviterID, "Returns the inviter's ID number"),
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteRevokedMsg, gameTypeID, "Returns the game type ID (as a guid string)"),
                   PYTHON_METHOD_NOARGS(ptGameMgrInviteRevokedMsg, newGameID, "Returns the new game's ID number"), )

// CWE pyGameMgrMsg::GetType (pyGameMgrMsg.cpp): the message id.
PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrMsg, getType)
{
    pfGameMgrMsg* msg = IMgrMsg(self->fThis);
    return IInt(msg ? (long)msg->NetMsg()->messageId : -1);
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrMsg, upcastToInviteReceivedMsg)
{
    pfGameMgrMsg* msg = IMgrMsg(self->fThis);
    if (msg && msg->NetMsg()->messageId == kSrv2Cli_GameMgr_InviteReceived)
        return IWrapMsg(&ptGameMgrInviteReceivedMsg_type, msg);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameMgrMsg, upcastToInviteRevokedMsg)
{
    pfGameMgrMsg* msg = IMgrMsg(self->fThis);
    if (msg && msg->NetMsg()->messageId == kSrv2Cli_GameMgr_InviteRevoked)
        return IWrapMsg(&ptGameMgrInviteRevokedMsg_type, msg);
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptGameMgrMsg)
PYTHON_METHOD_NOARGS(ptGameMgrMsg, getType, "Returns the type of the message (see PtGameMgrMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptGameMgrMsg, upcastToInviteReceivedMsg, "Returns this message as a ptGameMgrInviteReceivedMsg"),
    PYTHON_METHOD_NOARGS(ptGameMgrMsg, upcastToInviteRevokedMsg, "Returns this message as a ptGameMgrInviteRevokedMsg"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptGameMgrMsg, pyGameMgrMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptGameMgrMsg)
PYTHON_NO_INIT_DEFINITION(ptGameMgrMsg)

PLASMA_DEFAULT_TYPE(ptGameMgrMsg, "Message from the game manager");
PYTHON_EXPOSE_TYPE_DEFINITION(ptGameMgrMsg, pyGameMgrMsg2);

// ptGameCliMsg and its core finals (CWE pyGameCliMsg*.cpp); ptGameCliMsg itself is at the end
PYTHON_CLASS_DEFINITION(ptGameCliMsg, pyGameCliMsg2);
PYTHON_CLASS_DEFINITION(ptGameCliPlayerJoinedMsg, pyGameCliMsg2);
PYTHON_CLASS_DEFINITION(ptGameCliPlayerLeftMsg, pyGameCliMsg2);
PYTHON_CLASS_DEFINITION(ptGameCliInviteFailedMsg, pyGameCliMsg2);
PYTHON_CLASS_DEFINITION(ptGameCliOwnerChangeMsg, pyGameCliMsg2);

PY2_CLI_MSG_GETTER(ptGameCliPlayerJoinedMsg, playerID, Srv2Cli_Game_PlayerJoined, IULong(m->playerId), IULong(0))
PY2_CLI_MSG_GETTER(ptGameCliPlayerLeftMsg, playerID, Srv2Cli_Game_PlayerLeft, IULong(m->playerId), IULong(0))
PY2_CLI_MSG_GETTER(ptGameCliInviteFailedMsg, inviteeID, Srv2Cli_Game_InviteFailed, IULong(m->inviteeId), IULong(0))
PY2_CLI_MSG_GETTER(ptGameCliInviteFailedMsg, operationID, Srv2Cli_Game_InviteFailed, IULong(m->operationId), IULong(0))
// CWE pyGameCliMsgGlue.cpp, ptGameCliInviteFailedMsg.error: PyLong_FromLong, a `long`
PY2_CLI_MSG_GETTER(ptGameCliInviteFailedMsg, error, Srv2Cli_Game_InviteFailed,
                   PyPlasma_LongObject_FromLong((long)m->error), PyPlasma_LongObject_FromLong(0))
PY2_CLI_MSG_GETTER(ptGameCliOwnerChangeMsg, ownerID, Srv2Cli_Game_OwnerChange, IULong(m->ownerId), IULong(0))

PY2_GAME_MSG_FINAL(ptGameCliPlayerJoinedMsg, pyGameCliMsg2, "Game client message when a player joined message is received",
                   PYTHON_METHOD_NOARGS(ptGameCliPlayerJoinedMsg, playerID, "Returns the player's ID number"), )
PY2_GAME_MSG_FINAL(ptGameCliPlayerLeftMsg, pyGameCliMsg2, "Game client message when a player left message is received",
                   PYTHON_METHOD_NOARGS(ptGameCliPlayerLeftMsg, playerID, "Returns the player's ID number"), )
PY2_GAME_MSG_FINAL(ptGameCliInviteFailedMsg, pyGameCliMsg2, "Game client message when an invite failed message is received",
                   PYTHON_METHOD_NOARGS(ptGameCliInviteFailedMsg, inviteeID, "Returns the invitee's ID number"),
                   PYTHON_METHOD_NOARGS(ptGameCliInviteFailedMsg, operationID, "Returns the operation's ID number"),
                   PYTHON_METHOD_NOARGS(ptGameCliInviteFailedMsg, error, "Returns the error value (See PtGameCliInviteErrors)"), )
PY2_GAME_MSG_FINAL(ptGameCliOwnerChangeMsg, pyGameCliMsg2, "Game client message when a owner change message is received",
                   PYTHON_METHOD_NOARGS(ptGameCliOwnerChangeMsg, ownerID, "Returns the owner's ID number"), )

// A game's message base class: CWE py<Game>Msg::Get<Game>MsgType (the message id, or -1) and
// UpcastToFinal<Game>Msg (the final class of the message id, or None).
static PyObject* IGameMsgType(const pyGameMsg2* glue)
{
    pfGameCliMsg* msg = ICliMsg(glue);
    return IInt(msg ? (long)msg->NetMsg()->messageId : -1);
}

struct FinalOfId
{
    uint32_t      id;
    PyTypeObject* type;
};

static PyObject* IUpcastFinal(const pyGameMsg2* glue, const FinalOfId* table, size_t count)
{
    pfGameCliMsg* msg = ICliMsg(glue);
    if (msg) {
        for (size_t i = 0; i < count; ++i) {
            if (table[i].id == msg->NetMsg()->messageId)
                return IWrapMsg(table[i].type, msg);
        }
    }
    PYTHON_RETURN_NONE;
}

// Heek (CWE Heek/pyHeekMsg*.cpp)
PYTHON_CLASS_DEFINITION(ptHeekMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekPlayGameMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekGoodbyeMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekWelcomeMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekDropMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekSetupMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekLightStateMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekInterfaceStateMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekCountdownStateMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekWinLoseMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekGameWinMsg, pyHeekMsg2);
PYTHON_CLASS_DEFINITION(ptHeekPointUpdateMsg, pyHeekMsg2);

PY2_CLI_MSG_GETTER(ptHeekPlayGameMsg, isPlaying, Srv2Cli_Heek_PlayGame, IBool(m->isPlaying), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekPlayGameMsg, isSinglePlayer, Srv2Cli_Heek_PlayGame, IBool(m->isSinglePlayer), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekPlayGameMsg, enableButtons, Srv2Cli_Heek_PlayGame, IBool(m->enableButtons), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekWelcomeMsg, points, Srv2Cli_Heek_Welcome, IULong(m->points), IULong(0))
PY2_CLI_MSG_GETTER(ptHeekWelcomeMsg, rank, Srv2Cli_Heek_Welcome, IULong(m->rank), IULong(0))
PY2_CLI_MSG_GETTER(ptHeekWelcomeMsg, name, Srv2Cli_Heek_Welcome, IWireString(m->name), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptHeekDropMsg, position, Srv2Cli_Heek_Drop, IULong(m->position), IULong(0))
PY2_CLI_MSG_GETTER(ptHeekSetupMsg, position, Srv2Cli_Heek_Setup, IInt(m->position), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekSetupMsg, buttonState, Srv2Cli_Heek_Setup, IBool(m->buttonState), IBool(false))
// CWE pyHeekMsgGlue.cpp, ptHeekSetupMsg.lightOn: a list of six ints 0/1 (empty without a message)
PYTHON_METHOD_DEFINITION_NOARGS(ptHeekSetupMsg, lightOn)
{
    const Srv2Cli_Heek_Setup* m = ICliMsgAs<Srv2Cli_Heek_Setup>(self->fThis);
    size_t                    count = m ? std::size(m->lightOn) : 0;
    PyObject*                 retVal = PyList_New((int)count);
    for (size_t i = 0; retVal && i < count; ++i)
        PyList_SetItem(retVal, (int)i, IInt(m->lightOn[i] ? 1 : 0));
    return retVal;
}
PY2_CLI_MSG_GETTER(ptHeekLightStateMsg, lightNum, Srv2Cli_Heek_LightState, IInt(m->lightNum), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekLightStateMsg, state, Srv2Cli_Heek_LightState, IInt(m->state), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekInterfaceStateMsg, buttonsEnabled, Srv2Cli_Heek_InterfaceState, IBool(m->buttonsEnabled), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekCountdownStateMsg, state, Srv2Cli_Heek_CountdownState, IInt((long)m->state), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekWinLoseMsg, win, Srv2Cli_Heek_WinLose, IBool(m->win), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekWinLoseMsg, choice, Srv2Cli_Heek_WinLose, IInt((long)m->choice), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekGameWinMsg, choice, Srv2Cli_Heek_GameWin, IInt((long)m->choice), IInt(0))
PY2_CLI_MSG_GETTER(ptHeekPointUpdateMsg, displayUpdate, Srv2Cli_Heek_PointUpdate, IBool(m->displayUpdate), IBool(false))
PY2_CLI_MSG_GETTER(ptHeekPointUpdateMsg, points, Srv2Cli_Heek_PointUpdate, IULong(m->points), IULong(0))
PY2_CLI_MSG_GETTER(ptHeekPointUpdateMsg, rank, Srv2Cli_Heek_PointUpdate, IULong(m->rank), IULong(0))

PY2_GAME_MSG_FINAL(ptHeekPlayGameMsg, pyHeekMsg2, "Heek message received when the server processes your play game request",
                   PYTHON_METHOD_NOARGS(ptHeekPlayGameMsg, isPlaying, "Returns true if the server accepted the play game request"),
                   PYTHON_METHOD_NOARGS(ptHeekPlayGameMsg, isSinglePlayer, "Returns true if you are the only player at the table"),
                   PYTHON_METHOD_NOARGS(ptHeekPlayGameMsg, enableButtons, "Returns true if we should enable the buttons at the place we sat down"), )
PY2_GAME_MSG_FINAL(ptHeekGoodbyeMsg, pyHeekMsg2, "Heek message received when the server processes leave request", )
PY2_GAME_MSG_FINAL(ptHeekWelcomeMsg, pyHeekMsg2, "Heek message received when a new player sits down",
                   PYTHON_METHOD_NOARGS(ptHeekWelcomeMsg, points, "Returns the new player's points"),
                   PYTHON_METHOD_NOARGS(ptHeekWelcomeMsg, rank, "Returns the new player's rank"),
                   PYTHON_METHOD_NOARGS(ptHeekWelcomeMsg, name, "Returns the new player's name"), )
PY2_GAME_MSG_FINAL(ptHeekDropMsg, pyHeekMsg2, "Heek message received when another player's position needs to be reset/modified",
                   PYTHON_METHOD_NOARGS(ptHeekDropMsg, position, "Returns player position to reset/drop"), )
PY2_GAME_MSG_FINAL(ptHeekSetupMsg, pyHeekMsg2, "Heek message for setting up each position's state",
                   PYTHON_METHOD_NOARGS(ptHeekSetupMsg, position, "Returns the position this message is for"),
                   PYTHON_METHOD_NOARGS(ptHeekSetupMsg, buttonState, "Returns whether the buttons are enabled or not"),
                   PYTHON_METHOD_NOARGS(ptHeekSetupMsg, lightOn, "Returns a list of bools representing lights on or off"), )
PY2_GAME_MSG_FINAL(ptHeekLightStateMsg, pyHeekMsg2, "Heek message received when one of your local lights needs to change state",
                   PYTHON_METHOD_NOARGS(ptHeekLightStateMsg, lightNum, "Returns the index of the light this refers to"),
                   PYTHON_METHOD_NOARGS(ptHeekLightStateMsg, state, "Returns state the light should be switched to (see PtHeekLightStates)"), )
PY2_GAME_MSG_FINAL(ptHeekInterfaceStateMsg, pyHeekMsg2, "Heek message received when your interface buttons need to enable or disable",
                   PYTHON_METHOD_NOARGS(ptHeekInterfaceStateMsg, buttonsEnabled, "Returns whether your buttons should be enabled"), )
PY2_GAME_MSG_FINAL(ptHeekCountdownStateMsg, pyHeekMsg2, "Heek message received by game admin when the countdown state needs to change",
                   PYTHON_METHOD_NOARGS(ptHeekCountdownStateMsg, state, "Returns state the countdown should be switched to (see PtHeekCountdownStates)"), )
PY2_GAME_MSG_FINAL(ptHeekWinLoseMsg, pyHeekMsg2, "Heek message received when the round is over and you won or lost",
                   PYTHON_METHOD_NOARGS(ptHeekWinLoseMsg, win, "Returns true if you won"),
                   PYTHON_METHOD_NOARGS(ptHeekWinLoseMsg, choice, "Returns the choice that won or lost (see PtHeekGameChoice)"), )
PY2_GAME_MSG_FINAL(ptHeekGameWinMsg, pyHeekMsg2, "Heek message received by game admin when a game is won",
                   PYTHON_METHOD_NOARGS(ptHeekGameWinMsg, choice, "Returns the choice that won (see PtHeekGameChoice)"), )
PY2_GAME_MSG_FINAL(ptHeekPointUpdateMsg, pyHeekMsg2, "Heek message received when the number of points you have needs to be changed",
                   PYTHON_METHOD_NOARGS(ptHeekPointUpdateMsg, displayUpdate, "Returns whether you should display a message to the user"),
                   PYTHON_METHOD_NOARGS(ptHeekPointUpdateMsg, points, "Returns your new amount of points"),
                   PYTHON_METHOD_NOARGS(ptHeekPointUpdateMsg, rank, "Returns your new rank"), )

static const FinalOfId kHeekFinals[] = {
    {kSrv2Cli_Heek_PlayGame, &ptHeekPlayGameMsg_type},
    {kSrv2Cli_Heek_Goodbye, &ptHeekGoodbyeMsg_type},
    {kSrv2Cli_Heek_Welcome, &ptHeekWelcomeMsg_type},
    {kSrv2Cli_Heek_Drop, &ptHeekDropMsg_type},
    {kSrv2Cli_Heek_Setup, &ptHeekSetupMsg_type},
    {kSrv2Cli_Heek_LightState, &ptHeekLightStateMsg_type},
    {kSrv2Cli_Heek_InterfaceState, &ptHeekInterfaceStateMsg_type},
    {kSrv2Cli_Heek_CountdownState, &ptHeekCountdownStateMsg_type},
    {kSrv2Cli_Heek_WinLose, &ptHeekWinLoseMsg_type},
    {kSrv2Cli_Heek_GameWin, &ptHeekGameWinMsg_type},
    {kSrv2Cli_Heek_PointUpdate, &ptHeekPointUpdateMsg_type},
};

PYTHON_METHOD_DEFINITION_NOARGS(ptHeekMsg, getHeekMsgType) { return IGameMsgType(self->fThis); }
PYTHON_METHOD_DEFINITION_NOARGS(ptHeekMsg, upcastToFinalHeekMsg) { return IUpcastFinal(self->fThis, kHeekFinals, std::size(kHeekFinals)); }

PYTHON_START_METHODS_TABLE(ptHeekMsg)
PYTHON_METHOD_NOARGS(ptHeekMsg, getHeekMsgType, "Returns the type of the Heek message (see PtHeekMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptHeekMsg, upcastToFinalHeekMsg, "Returns this message as the Heek message it really is"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptHeekMsg, pyHeekMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptHeekMsg)
PYTHON_NO_INIT_DEFINITION(ptHeekMsg)

PLASMA_DEFAULT_TYPE_WBASE(ptHeekMsg, pyGameCliMsg2, "Base class for Heek game messages");
PYTHON_EXPOSE_TYPE_DEFINITION(ptHeekMsg, pyHeekMsg2);

// Marker (CWE Marker/pyMarkerMsg*.cpp)
PYTHON_CLASS_DEFINITION(ptMarkerMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerTemplateCreatedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerTeamAssignedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameTypeMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameStartedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGamePausedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameResetMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameOverMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameNameChangedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerTimeLimitChangedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerGameDeletedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerMarkerAddedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerMarkerDeletedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerMarkerNameChangedMsg, pyMarkerMsg2);
PYTHON_CLASS_DEFINITION(ptMarkerMarkerCapturedMsg, pyMarkerMsg2);

PY2_CLI_MSG_GETTER(ptMarkerTemplateCreatedMsg, templateID, Srv2Cli_Marker_TemplateCreated, IWireString(m->templateID), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptMarkerTeamAssignedMsg, teamNumber, Srv2Cli_Marker_TeamAssigned, IInt(m->teamNumber), IInt(0))
PY2_CLI_MSG_GETTER(ptMarkerGameTypeMsg, gameType, Srv2Cli_Marker_GameType, IInt((long)m->gameType), IInt(0))
PY2_CLI_MSG_GETTER(ptMarkerGamePausedMsg, timeLeft, Srv2Cli_Marker_GamePaused, IULong(m->timeLeft), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerGameNameChangedMsg, name, Srv2Cli_Marker_GameNameChanged, IWireString(m->newName), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptMarkerTimeLimitChangedMsg, timeLimit, Srv2Cli_Marker_TimeLimitChanged, IULong(m->newTimeLimit), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerGameDeletedMsg, failed, Srv2Cli_Marker_GameDeleted, IBool(m->failed), IBool(false))
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, x, Srv2Cli_Marker_MarkerAdded, PyFloat_FromDouble(m->x), PyFloat_FromDouble(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, y, Srv2Cli_Marker_MarkerAdded, PyFloat_FromDouble(m->y), PyFloat_FromDouble(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, z, Srv2Cli_Marker_MarkerAdded, PyFloat_FromDouble(m->z), PyFloat_FromDouble(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, markerId, Srv2Cli_Marker_MarkerAdded, IULong(m->markerID), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, name, Srv2Cli_Marker_MarkerAdded, IWireString(m->name), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptMarkerMarkerAddedMsg, age, Srv2Cli_Marker_MarkerAdded, IWireString(m->age), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptMarkerMarkerDeletedMsg, markerId, Srv2Cli_Marker_MarkerDeleted, IULong(m->markerID), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerNameChangedMsg, markerId, Srv2Cli_Marker_MarkerNameChanged, IULong(m->markerID), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerNameChangedMsg, name, Srv2Cli_Marker_MarkerNameChanged, IWireString(m->newName), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptMarkerMarkerCapturedMsg, markerId, Srv2Cli_Marker_MarkerCaptured, IULong(m->markerID), IULong(0))
PY2_CLI_MSG_GETTER(ptMarkerMarkerCapturedMsg, team, Srv2Cli_Marker_MarkerCaptured, IInt((long)m->team), IInt(0))

PY2_GAME_MSG_FINAL(ptMarkerTemplateCreatedMsg, pyMarkerMsg2, "Marker message received when a quest game template is created",
                   PYTHON_METHOD_NOARGS(ptMarkerTemplateCreatedMsg, templateID, "Returns the ID number of the template that was created"), )
PY2_GAME_MSG_FINAL(ptMarkerTeamAssignedMsg, pyMarkerMsg2, "Marker message received when you are assigned a team number",
                   PYTHON_METHOD_NOARGS(ptMarkerTeamAssignedMsg, teamNumber, "Returns the number of the team you were assigned to"), )
PY2_GAME_MSG_FINAL(ptMarkerGameTypeMsg, pyMarkerMsg2, "Marker message received when you are assigned a team number",
                   PYTHON_METHOD_NOARGS(ptMarkerGameTypeMsg, gameType, "Returns the type of the game you just joined"), )
PY2_GAME_MSG_FINAL(ptMarkerGameStartedMsg, pyMarkerMsg2, "Marker message received when the game is started by the owner", )
PY2_GAME_MSG_FINAL(ptMarkerGamePausedMsg, pyMarkerMsg2, "Marker message received when the game is paused by the owner",
                   PYTHON_METHOD_NOARGS(ptMarkerGamePausedMsg, timeLeft, "Returns the amount of time left on the server clock"), )
PY2_GAME_MSG_FINAL(ptMarkerGameResetMsg, pyMarkerMsg2, "Marker message received when the game is reset by the owner", )
PY2_GAME_MSG_FINAL(ptMarkerGameOverMsg, pyMarkerMsg2, "Marker message received when the server determines the game is over (usually via timeout)", )
PY2_GAME_MSG_FINAL(ptMarkerGameNameChangedMsg, pyMarkerMsg2, "Marker message received when the game name is changed",
                   PYTHON_METHOD_NOARGS(ptMarkerGameNameChangedMsg, name, "Returns the new game name"), )
PY2_GAME_MSG_FINAL(ptMarkerTimeLimitChangedMsg, pyMarkerMsg2, "Marker message received when the game name is changed",
                   PYTHON_METHOD_NOARGS(ptMarkerTimeLimitChangedMsg, timeLimit, "Returns the new time limit (in ms)"), )
PY2_GAME_MSG_FINAL(ptMarkerGameDeletedMsg, pyMarkerMsg2, "Marker message received when the game is deleted",
                   PYTHON_METHOD_NOARGS(ptMarkerGameDeletedMsg, failed, "Returns whether the delete succeeded or not"), )
PY2_GAME_MSG_FINAL(ptMarkerMarkerAddedMsg, pyMarkerMsg2, "Marker message received when a marker is added to the game",
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, x, "Returns x coord of the marker"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, y, "Returns y coord of the marker"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, z, "Returns z coord of the marker"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, markerId, "Returns the id number of the marker"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, name, "Returns the name of the marker"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerAddedMsg, age, "Returns the age the marker was created in"), )
PY2_GAME_MSG_FINAL(ptMarkerMarkerDeletedMsg, pyMarkerMsg2, "Marker message received when a marker is deleted",
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerDeletedMsg, markerId, "Returns id of the marker that was deleted"), )
PY2_GAME_MSG_FINAL(ptMarkerMarkerNameChangedMsg, pyMarkerMsg2, "Marker message received when the name of a marker is changed",
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerNameChangedMsg, markerId, "Returns id of the marker who's name was changed"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerNameChangedMsg, name, "Returns the new name"), )
PY2_GAME_MSG_FINAL(ptMarkerMarkerCapturedMsg, pyMarkerMsg2, "Marker message received when a marker is captured",
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerCapturedMsg, markerId, "Returns id of the marker which was captured"),
                   PYTHON_METHOD_NOARGS(ptMarkerMarkerCapturedMsg, team, "Returns the team number of the team that captured it (0 for no team, or a quest game)"), )

static const FinalOfId kMarkerFinals[] = {
    {kSrv2Cli_Marker_TemplateCreated, &ptMarkerTemplateCreatedMsg_type},
    {kSrv2Cli_Marker_TeamAssigned, &ptMarkerTeamAssignedMsg_type},
    {kSrv2Cli_Marker_GameType, &ptMarkerGameTypeMsg_type},
    {kSrv2Cli_Marker_GameStarted, &ptMarkerGameStartedMsg_type},
    {kSrv2Cli_Marker_GamePaused, &ptMarkerGamePausedMsg_type},
    {kSrv2Cli_Marker_GameReset, &ptMarkerGameResetMsg_type},
    {kSrv2Cli_Marker_GameOver, &ptMarkerGameOverMsg_type},
    {kSrv2Cli_Marker_GameNameChanged, &ptMarkerGameNameChangedMsg_type},
    {kSrv2Cli_Marker_TimeLimitChanged, &ptMarkerTimeLimitChangedMsg_type},
    {kSrv2Cli_Marker_GameDeleted, &ptMarkerGameDeletedMsg_type},
    {kSrv2Cli_Marker_MarkerAdded, &ptMarkerMarkerAddedMsg_type},
    {kSrv2Cli_Marker_MarkerDeleted, &ptMarkerMarkerDeletedMsg_type},
    {kSrv2Cli_Marker_MarkerNameChanged, &ptMarkerMarkerNameChangedMsg_type},
    {kSrv2Cli_Marker_MarkerCaptured, &ptMarkerMarkerCapturedMsg_type},
};

PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerMsg, getMarkerMsgType) { return IGameMsgType(self->fThis); }
PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerMsg, upcastToFinalMarkerMsg) { return IUpcastFinal(self->fThis, kMarkerFinals, std::size(kMarkerFinals)); }

PYTHON_START_METHODS_TABLE(ptMarkerMsg)
PYTHON_METHOD_NOARGS(ptMarkerMsg, getMarkerMsgType, "Returns the type of the Marker message (see PtMarkerMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptMarkerMsg, upcastToFinalMarkerMsg, "Returns this message as the Marker msg it is"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptMarkerMsg, pyMarkerMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptMarkerMsg)
PYTHON_NO_INIT_DEFINITION(ptMarkerMsg)

PLASMA_DEFAULT_TYPE_WBASE(ptMarkerMsg, pyGameCliMsg2, "Base class for Marker game messages");
PYTHON_EXPOSE_TYPE_DEFINITION(ptMarkerMsg, pyMarkerMsg2);

// BlueSpiral (CWE BlueSpiral/pyBlueSpiralMsg*.cpp)
PYTHON_CLASS_DEFINITION(ptBlueSpiralMsg, pyBlueSpiralMsg2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralClothOrderMsg, pyBlueSpiralMsg2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralSuccessfulHitMsg, pyBlueSpiralMsg2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralGameWonMsg, pyBlueSpiralMsg2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralGameOverMsg, pyBlueSpiralMsg2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralGameStartedMsg, pyBlueSpiralMsg2);

// CWE pyBlueSpiralMsgGlue.cpp, ptBlueSpiralClothOrderMsg.order: a list of seven ints (empty without
// a message)
PYTHON_METHOD_DEFINITION_NOARGS(ptBlueSpiralClothOrderMsg, order)
{
    const Srv2Cli_BlueSpiral_ClothOrder* m = ICliMsgAs<Srv2Cli_BlueSpiral_ClothOrder>(self->fThis);
    size_t                               count = m ? std::size(m->order) : 0;
    PyObject*                            retVal = PyList_New((int)count);
    for (size_t i = 0; retVal && i < count; ++i)
        PyList_SetItem(retVal, (int)i, IInt(m->order[i]));
    return retVal;
}
PY2_CLI_MSG_GETTER(ptBlueSpiralGameStartedMsg, startSpin, Srv2Cli_BlueSpiral_GameStarted, IBool(m->startSpin), IBool(false))

PY2_GAME_MSG_FINAL(ptBlueSpiralClothOrderMsg, pyBlueSpiralMsg2, "BlueSpiral message received when the game is started and the cloth order is set",
                   PYTHON_METHOD_NOARGS(ptBlueSpiralClothOrderMsg, order, "Returns a list of numbers indicating the correct order to hit the clothes in"), )
PY2_GAME_MSG_FINAL(ptBlueSpiralSuccessfulHitMsg, pyBlueSpiralMsg2, "BlueSpiral message received when a cloth is hit in the correct order", )
PY2_GAME_MSG_FINAL(ptBlueSpiralGameWonMsg, pyBlueSpiralMsg2, "BlueSpiral message received when the last cloth is successfully hit", )
PY2_GAME_MSG_FINAL(ptBlueSpiralGameOverMsg, pyBlueSpiralMsg2, "BlueSpiral message received when the timer runs out, someone hits the wrong cloth, or the game is restarted (before a game start msg in that last case)", )
PY2_GAME_MSG_FINAL(ptBlueSpiralGameStartedMsg, pyBlueSpiralMsg2, "BlueSpiral message received when someone starts the game (or when you join a game that is running)",
                   PYTHON_METHOD_NOARGS(ptBlueSpiralGameStartedMsg, startSpin, "Returns true if you are supposed to start spinning the door thingy"), )

static const FinalOfId kBlueSpiralFinals[] = {
    {kSrv2Cli_BlueSpiral_ClothOrder, &ptBlueSpiralClothOrderMsg_type},
    {kSrv2Cli_BlueSpiral_SuccessfulHit, &ptBlueSpiralSuccessfulHitMsg_type},
    {kSrv2Cli_BlueSpiral_GameWon, &ptBlueSpiralGameWonMsg_type},
    {kSrv2Cli_BlueSpiral_GameOver, &ptBlueSpiralGameOverMsg_type},
    {kSrv2Cli_BlueSpiral_GameStarted, &ptBlueSpiralGameStartedMsg_type},
};

PYTHON_METHOD_DEFINITION_NOARGS(ptBlueSpiralMsg, getBlueSpiralMsgType) { return IGameMsgType(self->fThis); }
PYTHON_METHOD_DEFINITION_NOARGS(ptBlueSpiralMsg, upcastToFinalBlueSpiralMsg) { return IUpcastFinal(self->fThis, kBlueSpiralFinals, std::size(kBlueSpiralFinals)); }

PYTHON_START_METHODS_TABLE(ptBlueSpiralMsg)
PYTHON_METHOD_NOARGS(ptBlueSpiralMsg, getBlueSpiralMsgType, "Returns the type of the BlueSpiral message (see PtBlueSpiralMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptBlueSpiralMsg, upcastToFinalBlueSpiralMsg, "Returns this message as the BlueSpiral msg it is"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptBlueSpiralMsg, pyBlueSpiralMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptBlueSpiralMsg)
PYTHON_NO_INIT_DEFINITION(ptBlueSpiralMsg)

PLASMA_DEFAULT_TYPE_WBASE(ptBlueSpiralMsg, pyGameCliMsg2, "Base class for BlueSpiral game messages");
PYTHON_EXPOSE_TYPE_DEFINITION(ptBlueSpiralMsg, pyBlueSpiralMsg2);

// VarSync (CWE VarSync/pyVarSyncMsg*.cpp). The numbers are PyLong_FromDouble: a `long`.
PYTHON_CLASS_DEFINITION(ptVarSyncMsg, pyVarSyncMsg2);
PYTHON_CLASS_DEFINITION(ptVarSyncStringVarChangedMsg, pyVarSyncMsg2);
PYTHON_CLASS_DEFINITION(ptVarSyncNumericVarChangedMsg, pyVarSyncMsg2);
PYTHON_CLASS_DEFINITION(ptVarSyncAllVarsSentMsg, pyVarSyncMsg2);
PYTHON_CLASS_DEFINITION(ptVarSyncStringVarCreatedMsg, pyVarSyncMsg2);
PYTHON_CLASS_DEFINITION(ptVarSyncNumericVarCreatedMsg, pyVarSyncMsg2);

PY2_CLI_MSG_GETTER(ptVarSyncStringVarChangedMsg, id, Srv2Cli_VarSync_StringVarChanged, IULong(m->varID), IULong(0))
PY2_CLI_MSG_GETTER(ptVarSyncStringVarChangedMsg, value, Srv2Cli_VarSync_StringVarChanged, IWireString(m->varValue), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptVarSyncNumericVarChangedMsg, id, Srv2Cli_VarSync_NumericVarChanged, IULong(m->varID), IULong(0))
PY2_CLI_MSG_GETTER(ptVarSyncNumericVarChangedMsg, value, Srv2Cli_VarSync_NumericVarChanged, PyLong_FromDouble(m->varValue), PyLong_FromDouble(0))
PY2_CLI_MSG_GETTER(ptVarSyncStringVarCreatedMsg, name, Srv2Cli_VarSync_StringVarCreated, IWireString(m->varName), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptVarSyncStringVarCreatedMsg, id, Srv2Cli_VarSync_StringVarCreated, IULong(m->varID), IULong(0))
PY2_CLI_MSG_GETTER(ptVarSyncStringVarCreatedMsg, value, Srv2Cli_VarSync_StringVarCreated, IWireString(m->varValue), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptVarSyncNumericVarCreatedMsg, name, Srv2Cli_VarSync_NumericVarCreated, IWireString(m->varName), IEmptyUnicode())
PY2_CLI_MSG_GETTER(ptVarSyncNumericVarCreatedMsg, id, Srv2Cli_VarSync_NumericVarCreated, IULong(m->varID), IULong(0))
PY2_CLI_MSG_GETTER(ptVarSyncNumericVarCreatedMsg, value, Srv2Cli_VarSync_NumericVarCreated, PyLong_FromDouble(m->varValue), PyLong_FromDouble(0))

PY2_GAME_MSG_FINAL(ptVarSyncStringVarChangedMsg, pyVarSyncMsg2, "VarSync message received when a string variable's value changes",
                   PYTHON_METHOD_NOARGS(ptVarSyncStringVarChangedMsg, id, "Returns the id of the var that changed"),
                   PYTHON_METHOD_NOARGS(ptVarSyncStringVarChangedMsg, value, "Returns the variable's new value"), )
PY2_GAME_MSG_FINAL(ptVarSyncNumericVarChangedMsg, pyVarSyncMsg2, "VarSync message received when a numeric variable's value changes",
                   PYTHON_METHOD_NOARGS(ptVarSyncNumericVarChangedMsg, id, "Returns the id of the var that changed"),
                   PYTHON_METHOD_NOARGS(ptVarSyncNumericVarChangedMsg, value, "Returns the variable's new value"), )
PY2_GAME_MSG_FINAL(ptVarSyncAllVarsSentMsg, pyVarSyncMsg2, "VarSync message received after the last var is sent to you when you join the game, or request a list of vars", )
PY2_GAME_MSG_FINAL(ptVarSyncStringVarCreatedMsg, pyVarSyncMsg2, "VarSync message received when a string variable is created and assigned an id",
                   PYTHON_METHOD_NOARGS(ptVarSyncStringVarCreatedMsg, name, "Returns the name of the var that was created"),
                   PYTHON_METHOD_NOARGS(ptVarSyncStringVarCreatedMsg, id, "Returns the id that was assigned to this variable"),
                   PYTHON_METHOD_NOARGS(ptVarSyncStringVarCreatedMsg, value, "Returns the variable's new value"), )
PY2_GAME_MSG_FINAL(ptVarSyncNumericVarCreatedMsg, pyVarSyncMsg2, "VarSync message received when a numeric variable is created and assigned an id",
                   PYTHON_METHOD_NOARGS(ptVarSyncNumericVarCreatedMsg, name, "Returns the name of the var that was created"),
                   PYTHON_METHOD_NOARGS(ptVarSyncNumericVarCreatedMsg, id, "Returns the id that was assigned to this variable"),
                   PYTHON_METHOD_NOARGS(ptVarSyncNumericVarCreatedMsg, value, "Returns the variable's new value"), )

static const FinalOfId kVarSyncFinals[] = {
    {kSrv2Cli_VarSync_StringVarChanged, &ptVarSyncStringVarChangedMsg_type},
    {kSrv2Cli_VarSync_NumericVarChanged, &ptVarSyncNumericVarChangedMsg_type},
    {kSrv2Cli_VarSync_AllVarsSent, &ptVarSyncAllVarsSentMsg_type},
    {kSrv2Cli_VarSync_StringVarCreated, &ptVarSyncStringVarCreatedMsg_type},
    {kSrv2Cli_VarSync_NumericVarCreated, &ptVarSyncNumericVarCreatedMsg_type},
};

PYTHON_METHOD_DEFINITION_NOARGS(ptVarSyncMsg, getVarSyncMsgType) { return IGameMsgType(self->fThis); }
PYTHON_METHOD_DEFINITION_NOARGS(ptVarSyncMsg, upcastToFinalVarSyncMsg) { return IUpcastFinal(self->fThis, kVarSyncFinals, std::size(kVarSyncFinals)); }

PYTHON_START_METHODS_TABLE(ptVarSyncMsg)
PYTHON_METHOD_NOARGS(ptVarSyncMsg, getVarSyncMsgType, "Returns the type of the VarSync message (see PtVarSyncMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptVarSyncMsg, upcastToFinalVarSyncMsg, "Returns this message as the VarSync msg it is"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptVarSyncMsg, pyVarSyncMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptVarSyncMsg)
PYTHON_NO_INIT_DEFINITION(ptVarSyncMsg)

PLASMA_DEFAULT_TYPE_WBASE(ptVarSyncMsg, pyGameCliMsg2, "Base class for VarSync game messages");
PYTHON_EXPOSE_TYPE_DEFINITION(ptVarSyncMsg, pyVarSyncMsg2);

// ptGameCliMsg (CWE pyGameCliMsg*.cpp)
// CWE pyGameCliMsg::GetType (pyGameCliMsg.cpp)
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCliMsg, getType)
{
    pfGameCliMsg* msg = ICliMsg(self->fThis);
    if (!msg)
        return IInt(-1);
    uint32_t id = msg->NetMsg()->messageId;
    if (id < kSrv2Cli_NumGameMsgIds)
        return IInt((long)id);
    return IInt(msg->fGameCli ? msg->fGameCli->fType.cliMsgType : -1);
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCliMsg, getGameCli)
{
    pfGameCliMsg* msg = ICliMsg(self->fThis);
    if (msg && msg->fGameCli)
        return pyGameCli2::New(pyGameCli2::type_ptr, msg->fGameCli);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCliMsg, upcastToFinalGameCliMsg)
{
    pfGameCliMsg* msg = ICliMsg(self->fThis);
    if (!msg)
        PYTHON_RETURN_NONE;
    switch (msg->NetMsg()->messageId) {
        case kSrv2Cli_Game_PlayerJoined:
            return IWrapMsg(&ptGameCliPlayerJoinedMsg_type, msg);
        case kSrv2Cli_Game_PlayerLeft:
            return IWrapMsg(&ptGameCliPlayerLeftMsg_type, msg);
        case kSrv2Cli_Game_InviteFailed:
            return IWrapMsg(&ptGameCliInviteFailedMsg_type, msg);
        case kSrv2Cli_Game_OwnerChange:
            return IWrapMsg(&ptGameCliOwnerChangeMsg_type, msg);
        default:
            PYTHON_RETURN_NONE;
    }
}

// CWE pyGameCliMsg::UpcastToGameMsg (pyGameCliMsg.cpp): the game's message base class.
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCliMsg, upcastToGameMsg)
{
    pfGameCliMsg* msg = ICliMsg(self->fThis);
    if (!msg || !msg->fGameCli)
        PYTHON_RETURN_NONE;
    const GameType& type = msg->fGameCli->fType;
    if (&type == &ITypeOf<pfGmHeek>())
        return IWrapMsg(&ptHeekMsg_type, msg);
    if (&type == &ITypeOf<pfGmMarker>())
        return IWrapMsg(&ptMarkerMsg_type, msg);
    if (&type == &ITypeOf<pfGmBlueSpiral>())
        return IWrapMsg(&ptBlueSpiralMsg_type, msg);
    if (&type == &ITypeOf<pfGmVarSync>())
        return IWrapMsg(&ptVarSyncMsg_type, msg);
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptGameCliMsg)
PYTHON_METHOD_NOARGS(ptGameCliMsg, getType, "Returns the type of the message (see PtGameCliMsgTypes)"),
    PYTHON_METHOD_NOARGS(ptGameCliMsg, getGameCli, "Returns the game client associated with this message"),
    PYTHON_METHOD_NOARGS(ptGameCliMsg, upcastToFinalGameCliMsg, "Returns this message as the game client message it is (player joined, player left, invite failed, or owner change)"),
    PYTHON_METHOD_NOARGS(ptGameCliMsg, upcastToGameMsg, "Returns this message as the base class of message for the game it is associated with (ttt, heek, marker, etc)"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptGameCliMsg, pyGameCliMsg2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptGameCliMsg)
PYTHON_NO_INIT_DEFINITION(ptGameCliMsg)

PLASMA_DEFAULT_TYPE(ptGameCliMsg, "Message from the game server from a game");
PYTHON_EXPOSE_TYPE_DEFINITION(ptGameCliMsg, pyGameCliMsg2);

// ---------------------------------------------------------------------------------------------

const ClassDef* IMsgClasses(size_t& count)
{
    static const ClassDef kClasses[] = {
        {"ptGameMgrMsg", &ptGameMgrMsg_type},
        {"ptGameMgrInviteReceivedMsg", &ptGameMgrInviteReceivedMsg_type},
        {"ptGameMgrInviteRevokedMsg", &ptGameMgrInviteRevokedMsg_type},
        {"ptGameCliMsg", &ptGameCliMsg_type},
        {"ptGameCliPlayerJoinedMsg", &ptGameCliPlayerJoinedMsg_type},
        {"ptGameCliPlayerLeftMsg", &ptGameCliPlayerLeftMsg_type},
        {"ptGameCliInviteFailedMsg", &ptGameCliInviteFailedMsg_type},
        {"ptGameCliOwnerChangeMsg", &ptGameCliOwnerChangeMsg_type},
        {"ptHeekMsg", &ptHeekMsg_type},
        {"ptHeekPlayGameMsg", &ptHeekPlayGameMsg_type},
        {"ptHeekGoodbyeMsg", &ptHeekGoodbyeMsg_type},
        {"ptHeekWelcomeMsg", &ptHeekWelcomeMsg_type},
        {"ptHeekDropMsg", &ptHeekDropMsg_type},
        {"ptHeekSetupMsg", &ptHeekSetupMsg_type},
        {"ptHeekLightStateMsg", &ptHeekLightStateMsg_type},
        {"ptHeekInterfaceStateMsg", &ptHeekInterfaceStateMsg_type},
        {"ptHeekCountdownStateMsg", &ptHeekCountdownStateMsg_type},
        {"ptHeekWinLoseMsg", &ptHeekWinLoseMsg_type},
        {"ptHeekGameWinMsg", &ptHeekGameWinMsg_type},
        {"ptHeekPointUpdateMsg", &ptHeekPointUpdateMsg_type},
        {"ptMarkerMsg", &ptMarkerMsg_type},
        {"ptMarkerTemplateCreatedMsg", &ptMarkerTemplateCreatedMsg_type},
        {"ptMarkerTeamAssignedMsg", &ptMarkerTeamAssignedMsg_type},
        {"ptMarkerGameTypeMsg", &ptMarkerGameTypeMsg_type},
        {"ptMarkerGameStartedMsg", &ptMarkerGameStartedMsg_type},
        {"ptMarkerGamePausedMsg", &ptMarkerGamePausedMsg_type},
        {"ptMarkerGameResetMsg", &ptMarkerGameResetMsg_type},
        {"ptMarkerGameOverMsg", &ptMarkerGameOverMsg_type},
        {"ptMarkerGameNameChangedMsg", &ptMarkerGameNameChangedMsg_type},
        {"ptMarkerTimeLimitChangedMsg", &ptMarkerTimeLimitChangedMsg_type},
        {"ptMarkerGameDeletedMsg", &ptMarkerGameDeletedMsg_type},
        {"ptMarkerMarkerAddedMsg", &ptMarkerMarkerAddedMsg_type},
        {"ptMarkerMarkerDeletedMsg", &ptMarkerMarkerDeletedMsg_type},
        {"ptMarkerMarkerNameChangedMsg", &ptMarkerMarkerNameChangedMsg_type},
        {"ptMarkerMarkerCapturedMsg", &ptMarkerMarkerCapturedMsg_type},
        {"ptBlueSpiralMsg", &ptBlueSpiralMsg_type},
        {"ptBlueSpiralClothOrderMsg", &ptBlueSpiralClothOrderMsg_type},
        {"ptBlueSpiralSuccessfulHitMsg", &ptBlueSpiralSuccessfulHitMsg_type},
        {"ptBlueSpiralGameWonMsg", &ptBlueSpiralGameWonMsg_type},
        {"ptBlueSpiralGameOverMsg", &ptBlueSpiralGameOverMsg_type},
        {"ptBlueSpiralGameStartedMsg", &ptBlueSpiralGameStartedMsg_type},
        {"ptVarSyncMsg", &ptVarSyncMsg_type},
        {"ptVarSyncStringVarChangedMsg", &ptVarSyncStringVarChangedMsg_type},
        {"ptVarSyncNumericVarChangedMsg", &ptVarSyncNumericVarChangedMsg_type},
        {"ptVarSyncAllVarsSentMsg", &ptVarSyncAllVarsSentMsg_type},
        {"ptVarSyncStringVarCreatedMsg", &ptVarSyncStringVarCreatedMsg_type},
        {"ptVarSyncNumericVarCreatedMsg", &ptVarSyncNumericVarCreatedMsg_type},
    };
    count = std::size(kClasses);
    return kClasses;
}

#undef PY2_CLI_MSG_GETTER
#undef PY2_GAME_MSG_FINAL

} // namespace Game
} // namespace plPy2CWE

const char* plPy2CWE::GameMsgHandlerName(plMessage* msg)
{
    if (pfGameMgrMsg::ConvertNoRef(msg))
        return "OnGameMgrMsg";
    if (pfGameCliMsg::ConvertNoRef(msg))
        return "OnGameCliMsg";
    return nullptr;
}

PyObject* plPy2CWE::GameMsgNew(plMessage* msg)
{
    using namespace plPy2CWE::Game;
    if (pfGameMgrMsg* mgrMsg = pfGameMgrMsg::ConvertNoRef(msg))
        return IWrapMsg(&ptGameMgrMsg_type, mgrMsg);
    if (pfGameCliMsg* cliMsg = pfGameCliMsg::ConvertNoRef(msg))
        return IWrapMsg(&ptGameCliMsg_type, cliMsg);
    PyErr_SetString(PyExc_TypeError, "not a GameMgr message");
    return nullptr;
}

#endif // PLASMA_PYTHON_ABI < 3
