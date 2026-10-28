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
// The game clients of CWE's GameMgr glue in the py2 flavour: ptGameCli and the four games'
// client classes, the PlasmaGame functions and the PlasmaGameConstants enums.
// InstallGameClasses (pyGameMgr2.h) adds them to PlasmaGame, with pyGameMsg2.cpp's message
// classes. The requests go out through pyGameMgrWire2.cpp.
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfPython/Games (pyGameCli*.cpp and the Heek, Marker, BlueSpiral and VarSync glue)
// and FeatureLib/pfGameMgr/pfGameMgr.cpp.

#include "pyPy2Compat.h"
#if PLASMA_PYTHON_ABI < 3

#include <cstdint>
#include <map>
#include <string>
#include <string_theory/string>
#include <vector>

#include "pfGameMgr/pfGmBlueSpiral.h"
#include "pfGameMgr/pfGmHeek.h"
#include "pfGameMgr/pfGmMarker.h"
#include "pfGameMgr/pfGmVarSync.h"
#include "pnGameMgr/pnGameMgr.h"
#include "pnGameMgr/pnGmBlueSpiral.h"
#include "pnGameMgr/pnGmHeek.h"
#include "pnGameMgr/pnGmMarker.h"
#include "pnGameMgr/pnGmVarSync.h"
#include "pnKeyedObject/plKey.h"
#include "pnUUID/pnUUID.h"
#include "pyEnum.h"
#include "pyGameGlue2.h"
#include "pyGameMgr2.h"
#include "pyGameMgrWire2.h"
#include "pyGlueHelpers.h"
#include "pyKey.h"
#include "pyObjectRef.h"

namespace plPy2CWE
{
namespace Game
{

// The Python objects of the game clients: ptGameCli and the four games' classes derived from it
// share the one glue class (pyGameGlue2.h).
PYTHON_CLASS_DEFINITION(ptGameCli, pyGameCli2);
PYTHON_CLASS_DEFINITION(ptHeekGame, pyGameCli2);
PYTHON_CLASS_DEFINITION(ptMarkerGame, pyGameCli2);
PYTHON_CLASS_DEFINITION(ptBlueSpiralGame, pyGameCli2);
PYTHON_CLASS_DEFINITION(ptVarSyncGame, pyGameCli2);

pyGameCli2::~pyGameCli2()
{
    if (fCli)
        fCli->UnRef();
}

// A CWE string argument: a unicode (PyUnicode_AsWideChar) or a str (hsStringToWString, the
// ANSI code page: cp1252), as far as its first NUL (CWE copies it into a std::wstring).
static bool IArgString(PyObject* obj, std::u16string& out)
{
    if (PyObject_TypeCheck(obj, &PyUnicode_Type)) {
        out.assign(reinterpret_cast<const char16_t*>(PyUnicode_AS_UNICODE(obj)), (size_t)PyUnicode_GET_SIZE(obj));
    } else if (PyString_Check(obj)) {
        ST::utf16_buffer wide = PyUnicode_AsSTString(obj).to_utf16();
        out.assign(wide.data(), wide.size());
    } else {
        return false;
    }
    size_t nul = out.find(u'\0');
    if (nul != std::u16string::npos)
        out.resize(nul);
    return true;
}

static ST::string IToST(const std::u16string& s)
{
    return ST::string::from_utf16(s.data(), s.size(), ST::substitute_invalid);
}

// CWE Uuid(const wchar*) (GuidFromString): an unparsable string is the nil uuid.
static plUUID IParseUuid(const std::u16string& s)
{
    plUUID uuid;
    uuid.FromString(IToST(s).c_str());
    return uuid;
}

// ---------------------------------------------------------------------------------------------
// ptGameCli and the four game clients (CWE pyGameCliGlue.cpp, Games/*/py*GameGlue.cpp)

PyObject* pyGameCli2::New(PyTypeObject* type, Cli* cli)
{
    ptGameCli* obj = (ptGameCli*)type->tp_new(type, nullptr, nullptr);
    if (obj && cli) {
        obj->fThis->fCli = cli;
        cli->Ref();
    }
    return (PyObject*)obj;
}

static PyTypeObject* ICliType(const GameType& type);

template <class GameT>
static GameT* ICliAs(pyGameCli2* glue)
{
    if (glue->fCli && &glue->fCli->fType == &ITypeOf<GameT>())
        return static_cast<GameT*>(glue->fCli->GameCli());
    return nullptr;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, gameID)
{
    return IInt(self->fThis->fCli ? (long)self->fThis->fCli->GameId() : 0);
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, gameTypeID)
{
    if (!self->fThis->fCli)
        return IEmptyUnicode();
    return PyPlasma_UnicodeFromSTString(self->fThis->fCli->fType.typeId.AsString());
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, name)
{
    if (!self->fThis->fCli)
        return IEmptyUnicode();
    return PyPlasma_UnicodeFromSTString(self->fThis->fCli->fType.name);
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, playerCount)
{
    return IInt(self->fThis->fCli ? self->fThis->fCli->fPlayerCount : 0);
}

PYTHON_METHOD_DEFINITION(ptGameCli, invitePlayer, args)
{
    int playerID = 0;
    if (!PyArg_ParseTuple(args, "i", &playerID)) {
        PyErr_SetString(PyExc_TypeError, "invitePlayer expects an unsigned int");
        PYTHON_RETURN_ERROR;
    }
    if (self->fThis->fCli) {
        Cli2Srv_Game_Invite msg;
        msg.playerId = (uint32_t)playerID;
        ISendGameMsg(self->fThis->fCli, kCli2Srv_Game_Invite, msg);
    }
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptGameCli, uninvitePlayer, args)
{
    int playerID = 0;
    if (!PyArg_ParseTuple(args, "i", &playerID)) {
        PyErr_SetString(PyExc_TypeError, "uninvitePlayer expects an unsigned int");
        PYTHON_RETURN_ERROR;
    }
    if (self->fThis->fCli) {
        Cli2Srv_Game_Uninvite msg;
        msg.playerId = (uint32_t)playerID;
        ISendGameMsg(self->fThis->fCli, kCli2Srv_Game_Uninvite, msg);
    }
    PYTHON_RETURN_NONE;
}

// CWE pfGameCli::LeaveGame (pfGameMgr.cpp): a Game_LeaveGame, whatever the state.
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, leaveGame)
{
    if (self->fThis->fCli) {
        Cli2Srv_Game_LeaveGame msg;
        ISendGameMsg(self->fThis->fCli, kCli2Srv_Game_LeaveGame, msg);
    }
    PYTHON_RETURN_NONE;
}

// CWE pyGameCli::UpcastTo*Game (pyGameCli.cpp): the game's client class, or None.
static PyObject* IUpcastCli(pyGameCli2* glue, const plUUID& typeId)
{
    if (glue->fCli && glue->fCli->fType.typeId == typeId)
        return pyGameCli2::New(ICliType(glue->fCli->fType), glue->fCli);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToTTTGame) { return IUpcastCli(self->fThis, kGameTypeId_TicTacToe); }
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToHeekGame) { return IUpcastCli(self->fThis, kGameTypeId_Heek); }
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToMarkerGame) { return IUpcastCli(self->fThis, kGameTypeId_Marker); }
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToBlueSpiralGame) { return IUpcastCli(self->fThis, kGameTypeId_BlueSpiral); }
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToClimbingWallGame) { return IUpcastCli(self->fThis, kGameTypeId_ClimbingWall); }
PYTHON_METHOD_DEFINITION_NOARGS(ptGameCli, upcastToVarSyncGame) { return IUpcastCli(self->fThis, kGameTypeId_VarSync); }

PYTHON_START_METHODS_TABLE(ptGameCli)
PYTHON_METHOD_NOARGS(ptGameCli, gameID, "Returns the ID number for this game"),
    PYTHON_METHOD_NOARGS(ptGameCli, gameTypeID, "Returns the game type ID for this game (as a guid string)"),
    PYTHON_METHOD_NOARGS(ptGameCli, name, "Returns the name of the game"),
    PYTHON_METHOD_NOARGS(ptGameCli, playerCount, "Returns the current number of players"),
    PYTHON_METHOD(ptGameCli, invitePlayer, "Params: playerID\nInvites the specified player to join the game"),
    PYTHON_METHOD(ptGameCli, uninvitePlayer, "Params: playerID\nRevokes the invitation for the specified player"),
    PYTHON_METHOD_NOARGS(ptGameCli, leaveGame, "Leaves this game"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToTTTGame, "Returns this game client as a ptTTTGame"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToHeekGame, "Returns this game client as a ptHeekGame"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToMarkerGame, "Returns this game client as a ptMarkerGame"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToBlueSpiralGame, "Returns this game client as a ptBlueSpiralGame"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToClimbingWallGame, "Returns this game client as a ptClimbingWallGame"),
    PYTHON_METHOD_NOARGS(ptGameCli, upcastToVarSyncGame, "Returns this game client as a ptVarSyncGame"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptGameCli, pyGameCli2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptGameCli)
PYTHON_NO_INIT_DEFINITION(ptGameCli)

PLASMA_DEFAULT_TYPE(ptGameCli, "Base class for all game client interfaces");
PYTHON_EXPOSE_TYPE_DEFINITION(ptGameCli, pyGameCli2);

// ptHeekGame (CWE Heek/pyHeekGameGlue.cpp, pyHeekGame.cpp)
PYTHON_METHOD_DEFINITION(ptHeekGame, playGame, args)
{
    int            position = 0;
    long           points = 0;
    PyObject*      textObj = nullptr;
    std::u16string name;
    if (!PyArg_ParseTuple(args, "ilO", &position, &points, &textObj) || !IArgString(textObj, name)) {
        PyErr_SetString(PyExc_TypeError, "playGame expects an int, a long, and a unicode string");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmHeek* heek = ICliAs<pfGmHeek>(self->fThis))
        heek->PlayGame((uint8_t)position, (uint32_t)points, IToST(name));
    PYTHON_RETURN_NONE;
}

// CWE pyHeekGame::LeaveGame: Heek_LeaveGame (the player stands up), not Game_LeaveGame.
PYTHON_METHOD_DEFINITION_NOARGS(ptHeekGame, leaveGame)
{
    if (pfGmHeek* heek = ICliAs<pfGmHeek>(self->fThis))
        heek->LeaveGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptHeekGame, choose, args)
{
    int choice = 0;
    if (!PyArg_ParseTuple(args, "i", &choice)) {
        PyErr_SetString(PyExc_TypeError, "choose expects an int");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmHeek* heek = ICliAs<pfGmHeek>(self->fThis))
        heek->Choose((EHeekChoice)(uint8_t)choice);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptHeekGame, sequenceFinished, args)
{
    int seq = 0;
    if (!PyArg_ParseTuple(args, "i", &seq)) {
        PyErr_SetString(PyExc_TypeError, "sequenceFinished expects an int");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmHeek* heek = ICliAs<pfGmHeek>(self->fThis))
        heek->SequenceFinished((EHeekSeqFinished)(uint8_t)seq);
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptHeekGame)
PYTHON_METHOD(ptHeekGame, playGame, "Params: position, points, name\nRequests to start playing the game in the specified position"),
    PYTHON_METHOD_NOARGS(ptHeekGame, leaveGame, "Leaves this game (puts us into \"observer\" mode"),
    PYTHON_METHOD(ptHeekGame, choose, "Params: choice\nMakes the specified move (see PtHeekGameChoice)"),
    PYTHON_METHOD(ptHeekGame, sequenceFinished, "Params: sequence\nTells the server that the specified animation finished (see PtHeekGameSeq)"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptHeekGame, pyGameCli2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptHeekGame)
PYTHON_NO_INIT_DEFINITION(ptHeekGame)

PLASMA_DEFAULT_TYPE_WBASE(ptHeekGame, pyGameCli2, "Game client for the Heek game");

// ptMarkerGame (CWE Marker/pyMarkerGameGlue.cpp, pyMarkerGame.cpp)
PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerGame, startGame)
{
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->StartGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerGame, pauseGame)
{
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->PauseGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerGame, resetGame)
{
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->ResetGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptMarkerGame, changeGameName, args)
{
    PyObject*      textObj = nullptr;
    std::u16string name;
    if (!PyArg_ParseTuple(args, "O", &textObj) || !IArgString(textObj, name)) {
        PyErr_SetString(PyExc_TypeError, "changeGameName expects a unicode string");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->ChangeGameName(IToST(name));
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptMarkerGame, changeTimeLimit, args)
{
    unsigned long timeLimit = 0;
    if (!PyArg_ParseTuple(args, "k", &timeLimit)) {
        PyErr_SetString(PyExc_TypeError, "changeTimeLimit expects an unsigned long");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->ChangeTimeLimit((uint32_t)timeLimit);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_NOARGS(ptMarkerGame, deleteGame)
{
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->DeleteGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION_WKEY(ptMarkerGame, addMarker, args, keywords)
{
    static char*   kwlist[] = {const_cast<char*>("x"), const_cast<char*>("y"), const_cast<char*>("z"),
                               const_cast<char*>("name"), const_cast<char*>("age"), nullptr};
    double         x = 0, y = 0, z = 0;
    PyObject*      nameObj = nullptr;
    PyObject*      ageObj = nullptr;
    std::u16string name, age;
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "ddd|OO", kwlist, &x, &y, &z, &nameObj, &ageObj) ||
        (nameObj && !IArgString(nameObj, name)) || (ageObj && !IArgString(ageObj, age))) {
        PyErr_SetString(PyExc_TypeError, "addMarker expects three doubles, and optionally two strings");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->AddMarker(x, y, z, IToST(name), IToST(age));
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptMarkerGame, deleteMarker, args)
{
    unsigned long markerId = 0;
    if (!PyArg_ParseTuple(args, "k", &markerId)) {
        PyErr_SetString(PyExc_TypeError, "deleteMarker expects an unsigned long");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->DeleteMarker((uint32_t)markerId);
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptMarkerGame, changeMarkerName, args)
{
    unsigned long  markerId = 0;
    PyObject*      nameObj = nullptr;
    std::u16string name;
    if (!PyArg_ParseTuple(args, "kO", &markerId, &nameObj) || !IArgString(nameObj, name)) {
        PyErr_SetString(PyExc_TypeError, "changeMarkerName expects an unsigned long and a string");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->ChangeMarkerName((uint32_t)markerId, IToST(name));
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptMarkerGame, captureMarker, args)
{
    unsigned long markerId = 0;
    if (!PyArg_ParseTuple(args, "k", &markerId)) {
        PyErr_SetString(PyExc_TypeError, "captureMarker expects an unsigned long");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmMarker* marker = ICliAs<pfGmMarker>(self->fThis))
        marker->CaptureMarker((uint32_t)markerId);
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptMarkerGame)
PYTHON_METHOD_NOARGS(ptMarkerGame, startGame, "Starts the game. Won't work on MP games if you're not the owner/creator"),
    PYTHON_METHOD_NOARGS(ptMarkerGame, pauseGame, "Pauses the game. Won't work on MP games if you're not the owner/creator"),
    PYTHON_METHOD_NOARGS(ptMarkerGame, resetGame, "Resets the game. Won't work on MP games if you're not the owner/creator"),
    PYTHON_METHOD(ptMarkerGame, changeGameName, "Params: newName\nChanges the name of the game. Won't work if you're not the game owner/creator"),
    PYTHON_METHOD(ptMarkerGame, changeTimeLimit, "Params: newTimeLimit\nChanges the time limit on the game (in ms). Won't work if you're not the game owner/creator, or if it's a quest game"),
    PYTHON_METHOD_NOARGS(ptMarkerGame, deleteGame, "Tells the server to delete the game. Won't work if you're not the game owner/creator"),
    PYTHON_METHOD_WKEY(ptMarkerGame, addMarker, "Params: x, y, z, name = \"\", age = \"\"\nAdds a marker to the game. Age is ignored in a non-quest game. Won't work if you're not the owner/creator"),
    PYTHON_METHOD(ptMarkerGame, deleteMarker, "Params: markerId\nDeletes the specified marker from the game. Won't work if you're not the game owner/creator"),
    PYTHON_METHOD(ptMarkerGame, changeMarkerName, "Params: markerId, newName\nChanges the name of the specified marker. Won't work if you're not the game owner/creator"),
    PYTHON_METHOD(ptMarkerGame, captureMarker, "Params: markerId\nCaptures the specified marker"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptMarkerGame, pyGameCli2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptMarkerGame)
PYTHON_NO_INIT_DEFINITION(ptMarkerGame)

PLASMA_DEFAULT_TYPE_WBASE(ptMarkerGame, pyGameCli2, "Game client for the Marker game");

// ptBlueSpiralGame (CWE BlueSpiral/pyBlueSpiralGameGlue.cpp, pyBlueSpiralGame.cpp)
PYTHON_METHOD_DEFINITION_NOARGS(ptBlueSpiralGame, startGame)
{
    if (pfGmBlueSpiral* bs = ICliAs<pfGmBlueSpiral>(self->fThis))
        bs->StartGame();
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptBlueSpiralGame, hitCloth, args)
{
    int clothNum = 0;
    if (!PyArg_ParseTuple(args, "i", &clothNum)) {
        PyErr_SetString(PyExc_TypeError, "hitCloth expects one integer");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmBlueSpiral* bs = ICliAs<pfGmBlueSpiral>(self->fThis))
        bs->HitCloth((uint8_t)clothNum);
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptBlueSpiralGame)
PYTHON_METHOD_NOARGS(ptBlueSpiralGame, startGame, "Starts a new game"),
    PYTHON_METHOD(ptBlueSpiralGame, hitCloth, "Params: clothNum\nTells the server you hit the specified cloth"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptBlueSpiralGame, pyGameCli2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptBlueSpiralGame)
PYTHON_NO_INIT_DEFINITION(ptBlueSpiralGame)

PLASMA_DEFAULT_TYPE_WBASE(ptBlueSpiralGame, pyGameCli2, "Game client for the BlueSpiral game");

// ptVarSyncGame (CWE VarSync/pyVarSyncGameGlue.cpp, pyVarSyncGame.cpp)

// CWE: a float, an int or a long (PyLong_AsDouble), as a double.
static bool IArgNumber(PyObject* obj, double& out)
{
    if (PyFloat_Check(obj))
        out = PyFloat_AsDouble(obj);
    else if (PyInt_Check(obj))
        out = (double)PyInt_AsLong(obj);
    else if (PyObject_TypeCheck(obj, &PyLong_Type))
        out = PyLong_AsDouble(obj);
    else
        return false;
    return true;
}

PYTHON_METHOD_DEFINITION(ptVarSyncGame, setStringVar, args)
{
    unsigned long  id = 0;
    PyObject*      valueObj = nullptr;
    std::u16string value;
    if (!PyArg_ParseTuple(args, "kO", &id, &valueObj) || !IArgString(valueObj, value)) {
        PyErr_SetString(PyExc_TypeError, "setStringVar expects an unsigned long and a string");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmVarSync* vs = ICliAs<pfGmVarSync>(self->fThis))
        vs->SetVariable((uint32_t)id, pfGmVarSyncValue(IToST(value)));
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptVarSyncGame, setNumericVar, args)
{
    unsigned long id = 0;
    PyObject*     valueObj = nullptr;
    double        value = 0;
    if (!PyArg_ParseTuple(args, "kO", &id, &valueObj) || !IArgNumber(valueObj, value)) {
        PyErr_SetString(PyExc_TypeError, "setNumericVar expects an unsigned long and a number");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmVarSync* vs = ICliAs<pfGmVarSync>(self->fThis))
        vs->SetVariable((uint32_t)id, pfGmVarSyncValue(value));
    PYTHON_RETURN_NONE;
}

// CWE pfGmVarSync::RequestAllVars: a VarSync_RequestAllVars (H-uru has no method for it).
PYTHON_METHOD_DEFINITION_NOARGS(ptVarSyncGame, requestAllVars)
{
    if (self->fThis->fCli && &self->fThis->fCli->fType == &ITypeOf<pfGmVarSync>()) {
        Cli2Srv_VarSync_RequestAllVars msg;
        ISendGameMsg(self->fThis->fCli, kCli2Srv_VarSync_RequestAllVars, msg);
    }
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptVarSyncGame, createStringVar, args)
{
    PyObject*      nameObj = nullptr;
    PyObject*      valueObj = nullptr;
    std::u16string name, value;
    if (!PyArg_ParseTuple(args, "OO", &nameObj, &valueObj) || !IArgString(nameObj, name) ||
        !IArgString(valueObj, value)) {
        PyErr_SetString(PyExc_TypeError, "createStringVar expects two strings");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmVarSync* vs = ICliAs<pfGmVarSync>(self->fThis))
        vs->CreateVariable(IToST(name), pfGmVarSyncValue(IToST(value)));
    PYTHON_RETURN_NONE;
}

PYTHON_METHOD_DEFINITION(ptVarSyncGame, createNumericVar, args)
{
    PyObject*      nameObj = nullptr;
    PyObject*      valueObj = nullptr;
    std::u16string name;
    double         value = 0;
    if (!PyArg_ParseTuple(args, "OO", &nameObj, &valueObj) || !IArgString(nameObj, name) ||
        !IArgNumber(valueObj, value)) {
        PyErr_SetString(PyExc_TypeError, "createNumericVar expects a string and a number");
        PYTHON_RETURN_ERROR;
    }
    if (pfGmVarSync* vs = ICliAs<pfGmVarSync>(self->fThis))
        vs->CreateVariable(IToST(name), pfGmVarSyncValue(value));
    PYTHON_RETURN_NONE;
}

PYTHON_START_METHODS_TABLE(ptVarSyncGame)
PYTHON_METHOD(ptVarSyncGame, setStringVar, "Params: varID, value\nAttempts to set a string variable to the specified string (clipped to 255 chars)"),
    PYTHON_METHOD(ptVarSyncGame, setNumericVar, "Params: varID, value\nAttempts to set a numeric variable to the specified number (clipped to double)"),
    PYTHON_METHOD_NOARGS(ptVarSyncGame, requestAllVars, "Requests all the vars the server knows about"),
    PYTHON_METHOD(ptVarSyncGame, createStringVar, "Params: varName, value\nAttempts to create a new string variable and set it to the specified string (clipped to 255 chars)"),
    PYTHON_METHOD(ptVarSyncGame, createNumericVar, "Params: varName, value\nAttempts to create a new numeric variable and set it to the specified number (clipped to double)"),
    PYTHON_END_METHODS_TABLE;

PYTHON_DEFAULT_NEW_DEFINITION(ptVarSyncGame, pyGameCli2)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptVarSyncGame)
PYTHON_NO_INIT_DEFINITION(ptVarSyncGame)

PLASMA_DEFAULT_TYPE_WBASE(ptVarSyncGame, pyGameCli2, "Game client for the VarSync game");

static PyTypeObject* ICliType(const GameType& type)
{
    if (&type == &ITypeOf<pfGmHeek>())
        return &ptHeekGame_type;
    if (&type == &ITypeOf<pfGmMarker>())
        return &ptMarkerGame_type;
    if (&type == &ITypeOf<pfGmBlueSpiral>())
        return &ptBlueSpiralGame_type;
    if (&type == &ITypeOf<pfGmVarSync>())
        return &ptVarSyncGame_type;
    return &ptGameCli_type;
}

// ---------------------------------------------------------------------------------------------
// The PlasmaGame functions (CWE pyGameCliGlue.cpp: PtGetGameIDs, PtGetGameCli,
// PtGetGameNameByTypeID, PtJoinGame; and each game's Pt*Game functions)

PYTHON_GLOBAL_METHOD_DEFINITION_NOARGS(PtGetGameIDs, "Returns a list of game IDs that the player is currently joined to")
{
    std::vector<uint32_t> ids;
    for (const auto& [id, cli] : s_games) {
        if (!cli->fDeleted)
            ids.push_back(id);
    }
    PyObject* retVal = PyList_New((int)ids.size());
    for (size_t i = 0; retVal && i < ids.size(); ++i)
        PyList_SetItem(retVal, (int)i, IInt((long)ids[i]));
    return retVal;
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtGetGameCli, args, "Params: gameID\nReturns a ptGameCli associated with the specified id")
{
    int gameID = 0;
    if (!PyArg_ParseTuple(args, "i", &gameID)) {
        PyErr_SetString(PyExc_TypeError, "PtGetGameCli expects an integer");
        PYTHON_RETURN_ERROR;
    }
    if (Cli* cli = IFindCli((uint32_t)gameID))
        return pyGameCli2::New(pyGameCli2::type_ptr, cli);
    PYTHON_RETURN_NONE;
}

// CWE pfGameMgr::GetGameNameByTypeId (pfGameMgr.cpp). CWE built its result from the
// factory's name, a null pointer for an unknown type; that is an empty name here.
PYTHON_GLOBAL_METHOD_DEFINITION(PtGetGameNameByTypeID, args, "Params: guid\nReturns the name of the game represented by guid passed in as a string")
{
    PyObject*      textObj = nullptr;
    std::u16string text;
    if (!PyArg_ParseTuple(args, "O", &textObj) || !IArgString(textObj, text)) {
        PyErr_SetString(PyExc_TypeError, "PtGetGameNameByTypeID expects a unicode string");
        PYTHON_RETURN_ERROR;
    }
    if (const GameType* type = IFindType(IParseUuid(text)))
        return PyPlasma_UnicodeFromSTString(type->name);
    return IEmptyUnicode();
}

static bool IArgKey(PyObject* obj, plKey& key)
{
    if (!obj || !pyKey::Check(obj))
        return false;
    key = pyKey::ConvertFrom(obj)->getKey();
    return true;
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtJoinGame, args, "Params: callbackKey, gameID\nSends a join request to the specified game. Messages are sent to the callback key")
{
    PyObject* callbackObj = nullptr;
    int       gameID = 0;
    plKey     key;
    if (!PyArg_ParseTuple(args, "Oi", &callbackObj, &gameID) || !IArgKey(callbackObj, key)) {
        PyErr_SetString(PyExc_TypeError, "PtJoinGame expects a ptKey and an integer");
        PYTHON_RETURN_ERROR;
    }
    GameMgrPort::Get()->Join(IBuildJoinGame((uint32_t)gameID), key);
    PYTHON_RETURN_NONE;
}

// CWE Pt<Game>IsGame: the string parsed as a uuid equals the game's type id.
static PyObject* IIsGame(PyObject* args, const plUUID& typeId, const char* error)
{
    PyObject*      textObj = nullptr;
    std::u16string text;
    if (!PyArg_ParseTuple(args, "O", &textObj) || !IArgString(textObj, text)) {
        PyErr_SetString(PyExc_TypeError, error);
        PYTHON_RETURN_ERROR;
    }
    return IBool(IParseUuid(text) == typeId);
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtIsHeekGame, args, "Params: typeID\nReturns true if the specifed typeID (guid as a string) is a Heek game")
{
    return IIsGame(args, kGameTypeId_Heek, "PtIsHeekGame expects a unicode string");
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtIsMarkerGame, args, "Params: typeID\nReturns true if the specifed typeID (guid as a string) is a Marker game")
{
    return IIsGame(args, kGameTypeId_Marker, "PtIsMarkerGame expects a unicode string");
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtIsBlueSpiralGame, args, "Params: typeID\nReturns true if the specifed typeID (guid as a string) is a BlueSpiral game")
{
    return IIsGame(args, kGameTypeId_BlueSpiral, "PtIsBlueSpiralGame expects a unicode string");
}

PYTHON_GLOBAL_METHOD_DEFINITION(PtIsVarSyncGame, args, "Params: typeID\nReturns true if the specifed typeID (guid as a string) is a VarSync game")
{
    return IIsGame(args, kGameTypeId_VarSync, "PtIsVarSyncGame expects a unicode string");
}

// CWE pyHeekGame::JoinCommonHeekGame (pyHeekGame.cpp)
PYTHON_GLOBAL_METHOD_DEFINITION(PtJoinCommonHeekGame, args, "Params: callbackKey, gameID\nJoins a common Heek game with the specified ID. If one doesn't exist, it creates it")
{
    PyObject* callbackObj = nullptr;
    int       gameID = 0;
    plKey     key;
    if (!PyArg_ParseTuple(args, "Oi", &callbackObj, &gameID) || !IArgKey(callbackObj, key)) {
        PyErr_SetString(PyExc_TypeError, "PtJoinCommonHeekGame expects a ptKey and an integer");
        PYTHON_RETURN_ERROR;
    }
    GameMgrPort::Get()->Join(IBuildJoinCommonGame(kGameTypeId_Heek, (uint32_t)gameID, nullptr, 0), key);
    PYTHON_RETURN_NONE;
}

// CWE pyBlueSpiralGame::JoinCommonBlueSpiralGame (pyBlueSpiralGame.cpp)
PYTHON_GLOBAL_METHOD_DEFINITION(PtJoinCommonBlueSpiralGame, args, "Params: callbackKey, gameID\nJoins a common BlueSpiral game with the specified ID. If one doesn't exist, it creates it")
{
    PyObject* callbackObj = nullptr;
    int       gameID = 0;
    plKey     key;
    if (!PyArg_ParseTuple(args, "Oi", &callbackObj, &gameID) || !IArgKey(callbackObj, key)) {
        PyErr_SetString(PyExc_TypeError, "PtJoinCommonBlueSpiralGame expects a ptKey and an integer");
        PYTHON_RETURN_ERROR;
    }
    GameMgrPort::Get()->Join(IBuildJoinCommonEmptyParam(kGameTypeId_BlueSpiral, (uint32_t)gameID), key);
    PYTHON_RETURN_NONE;
}

// CWE pyVarSyncGame::JoinCommonVarSyncGame (pyVarSyncGame.cpp): always table 255.
PYTHON_GLOBAL_METHOD_DEFINITION(PtJoinCommonVarSyncGame, args, "Params: callbackKey\nJoins the common VarSync game. If one doesn't exist, it creates it")
{
    PyObject* callbackObj = nullptr;
    plKey     key;
    if (!PyArg_ParseTuple(args, "O", &callbackObj) || !IArgKey(callbackObj, key)) {
        PyErr_SetString(PyExc_TypeError, "PtJoinCommonVarSyncGame expects a ptKey");
        PYTHON_RETURN_ERROR;
    }
    GameMgrPort::Get()->Join(IBuildJoinCommonEmptyParam(kGameTypeId_VarSync, 255), key);
    PYTHON_RETURN_NONE;
}

// CWE pyMarkerGameGlue.cpp, PtCreateMarkerGame
PYTHON_GLOBAL_METHOD_DEFINITION_WKEY(PtCreateMarkerGame, args, keywords,
                                     "Params: callbackKey, gameType, gameName = \"\", timeLimit = 0, templateId = \"\"\n"
                                     "Creates a new Marker game with the specified callback key, game type (from PtMarkerGameTypes), time limit (in ms), and template id (guid string)")
{
    static char*   kwlist[] = {const_cast<char*>("callbackKey"), const_cast<char*>("gameType"),
                               const_cast<char*>("gameName"), const_cast<char*>("timeLimit"),
                               const_cast<char*>("templateId"), nullptr};
    PyObject*      callbackObj = nullptr;
    unsigned int   gameType = 0;
    PyObject*      gameNameObj = nullptr;
    unsigned long  timeLimit = 0;
    PyObject*      templateIdObj = nullptr;
    plKey          key;
    std::u16string name, templateId;
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "OI|OkO", kwlist, &callbackObj, &gameType,
                                     &gameNameObj, &timeLimit, &templateIdObj) ||
        !IArgKey(callbackObj, key) || (gameNameObj && !IArgString(gameNameObj, name)) ||
        (templateIdObj && !IArgString(templateIdObj, templateId))) {
        PyErr_SetString(PyExc_TypeError, "PtCreateMarkerGame expects a ptKey, unsigned int, and optionally a string, an unsigned long, and another string");
        PYTHON_RETURN_ERROR;
    }
    GameMgrPort::Get()->Join(IBuildCreateMarkerGame((uint8_t)gameType, name, (uint32_t)timeLimit, templateId), key);
    PYTHON_RETURN_NONE;
}

static PyMethodDef s_gameFunctions[] = {
    PYTHON_GLOBAL_METHOD(PtGetGameIDs)
        PYTHON_GLOBAL_METHOD(PtGetGameCli)
            PYTHON_GLOBAL_METHOD(PtGetGameNameByTypeID)
                PYTHON_GLOBAL_METHOD(PtJoinGame)
                    PYTHON_GLOBAL_METHOD(PtIsHeekGame)
                        PYTHON_GLOBAL_METHOD(PtJoinCommonHeekGame)
                            PYTHON_GLOBAL_METHOD(PtIsMarkerGame)
                                PYTHON_GLOBAL_METHOD(PtCreateMarkerGame)
                                    PYTHON_GLOBAL_METHOD(PtIsBlueSpiralGame)
                                        PYTHON_GLOBAL_METHOD(PtJoinCommonBlueSpiralGame)
                                            PYTHON_GLOBAL_METHOD(PtIsVarSyncGame)
                                                PYTHON_GLOBAL_METHOD(PtJoinCommonVarSyncGame){nullptr}};

// ---------------------------------------------------------------------------------------------
// The PlasmaGameConstants enums (CWE pyGameCliMsg::AddPlasmaConstantsClasses and
// pyGameCliInviteFailedMsg::AddPlasmaConstantsClasses in pyGameCliMsgGlue.cpp,
// pyGameMgrMsg::AddPlasmaConstantsClasses in pyGameMgrMsgGlue.cpp; the games'
// Add*ConstantsClasses). PtMarkerGameTypes is H-uru's
// PtMarkerGameType under CWE's name (pyCWEOverlay2.cpp).

struct EnumDef
{
    const char*                name;
    std::map<std::string, int> values;
};

static const EnumDef* IEnums(size_t& count)
{
    static const EnumDef kEnums[] = {
        {"PtGameMgrMsgTypes", {{"kGameMgrInviteReceivedMsg", kSrv2Cli_GameMgr_InviteReceived}, {"kGameMgrInviteRevokedMsg", kSrv2Cli_GameMgr_InviteRevoked}}},
        {"PtGameCliMsgTypes", {{"kGameCliPlayerJoinedMsg", kSrv2Cli_Game_PlayerJoined}, {"kGameCliPlayerLeftMsg", kSrv2Cli_Game_PlayerLeft}, {"kGameCliInviteFailedMsg", kSrv2Cli_Game_InviteFailed}, {"kGameCliOwnerChangeMsg", kSrv2Cli_Game_OwnerChange}, {"kGameCliTTTMsg", kPyGameCliTTTMsg}, {"kGameCliHeekMsg", kPyGameCliHeekMsg}, {"kGameCliMarkerMsg", kPyGameCliMarkerMsg}, {"kGameCliBlueSpiralMsg", kPyGameCliBlueSpiralMsg}, {"kGameCliClimbingWallMsg", kPyGameCliClimbingWallMsg}, {"kGameCliVarSyncMsg", kPyGameCliVarSyncMsg}}},
        {"PtGameCliInviteErrors", {{"kGameInviteSuccess", (int)EGameInviteError::kGameInviteSuccess}, {"kGameInviteErrNotOwner", (int)EGameInviteError::kGameInviteErrNotOwner}, {"kGameInviteErrAlreadyInvited", (int)EGameInviteError::kGameInviteErrAlreadyInvited}, {"kGameInviteErrAlreadyJoined", (int)EGameInviteError::kGameInviteErrAlreadyJoined}, {"kGameInviteErrGameStarted", (int)EGameInviteError::kGameInviteErrGameStarted}, {"kGameInviteErrGameOver", (int)EGameInviteError::kGameInviteErrGameOver}, {"kGameInviteErrGameFull", (int)EGameInviteError::kGameInviteErrGameFull}, {"kGameInviteErrNoJoin", (int)EGameInviteError::kGameInviteErrNoJoin}}},
        {"PtHeekGameChoice", {{"kHeekGameChoiceRock", (int)EHeekChoice::kHeekRock}, {"kHeekGameChoicePaper", (int)EHeekChoice::kHeekPaper}, {"kHeekGameChoiceScissors", (int)EHeekChoice::kHeekScissors}}},
        {"PtHeekGameSeq", {{"kHeekGameSeqCountdown", (int)EHeekSeqFinished::kHeekCountdownSeq}, {"kHeekGameSeqChoiceAnim", (int)EHeekSeqFinished::kHeekChoiceAnimSeq}, {"kHeekGameSeqGameWinAnim", (int)EHeekSeqFinished::kHeekGameWinAnimSeq}}},
        {"PtHeekMsgTypes", {{"kHeekPlayGame", kSrv2Cli_Heek_PlayGame}, {"kHeekGoodbye", kSrv2Cli_Heek_Goodbye}, {"kHeekWelcome", kSrv2Cli_Heek_Welcome}, {"kHeekDrop", kSrv2Cli_Heek_Drop}, {"kHeekSetup", kSrv2Cli_Heek_Setup}, {"kHeekLightState", kSrv2Cli_Heek_LightState}, {"kHeekInterfaceState", kSrv2Cli_Heek_InterfaceState}, {"kHeekCountdownState", kSrv2Cli_Heek_CountdownState}, {"kHeekWinLose", kSrv2Cli_Heek_WinLose}, {"kHeekGameWin", kSrv2Cli_Heek_GameWin}, {"kHeekPointUpdate", kSrv2Cli_Heek_PointUpdate}}},
        {"PtHeekLightStates", {{"kHeekLightOn", (int)EHeekLightState::kHeekLightOn}, {"kHeekLightOff", (int)EHeekLightState::kHeekLightOff}, {"kHeekLightFlash", (int)EHeekLightState::kHeekLightFlash}}},
        {"PtHeekCountdownStates", {{"kHeekCountdownStart", (int)EHeekCountdownState::kHeekCountdownStart}, {"kHeekCountdownStop", (int)EHeekCountdownState::kHeekCountdownStop}, {"kHeekCountdownIdle", (int)EHeekCountdownState::kHeekCountdownIdle}}},
        {"PtMarkerMsgTypes", {{"kMarkerTemplateCreated", kSrv2Cli_Marker_TemplateCreated}, {"kMarkerTeamAssigned", kSrv2Cli_Marker_TeamAssigned}, {"kMarkerGameType", kSrv2Cli_Marker_GameType}, {"kMarkerGameStarted", kSrv2Cli_Marker_GameStarted}, {"kMarkerGamePaused", kSrv2Cli_Marker_GamePaused}, {"kMarkerGameReset", kSrv2Cli_Marker_GameReset}, {"kMarkerGameOver", kSrv2Cli_Marker_GameOver}, {"kMarkerGameNameChanged", kSrv2Cli_Marker_GameNameChanged}, {"kMarkerTimeLimitChanged", kSrv2Cli_Marker_TimeLimitChanged}, {"kMarkerGameDeleted", kSrv2Cli_Marker_GameDeleted}, {"kMarkerMarkerAdded", kSrv2Cli_Marker_MarkerAdded}, {"kMarkerMarkerDeleted", kSrv2Cli_Marker_MarkerDeleted}, {"kMarkerMarkerNameChanged", kSrv2Cli_Marker_MarkerNameChanged}, {"kMarkerMarkerCaptured", kSrv2Cli_Marker_MarkerCaptured}}},
        {"PtBlueSpiralMsgTypes", {{"kBlueSpiralClothOrder", kSrv2Cli_BlueSpiral_ClothOrder}, {"kBlueSpiralSuccessfulHit", kSrv2Cli_BlueSpiral_SuccessfulHit}, {"kBlueSpiralGameWon", kSrv2Cli_BlueSpiral_GameWon}, {"kBlueSpiralGameOver", kSrv2Cli_BlueSpiral_GameOver}, {"kBlueSpiralGameStarted", kSrv2Cli_BlueSpiral_GameStarted}}},
        {"PtVarSyncMsgTypes", {{"kVarSyncStringVarChanged", kSrv2Cli_VarSync_StringVarChanged}, {"kVarSyncNumericVarChanged", kSrv2Cli_VarSync_NumericVarChanged}, {"kVarSyncAllVarsSent", kSrv2Cli_VarSync_AllVarsSent}, {"kVarSyncStringVarCreated", kSrv2Cli_VarSync_StringVarCreated}, {"kVarSyncNumericVarCreated", kSrv2Cli_VarSync_NumericVarCreated}}},
    };
    count = std::size(kEnums);
    return kEnums;
}

static const ClassDef kClasses[] = {
    {"ptGameCli", &ptGameCli_type},
    {"ptHeekGame", &ptHeekGame_type},
    {"ptMarkerGame", &ptMarkerGame_type},
    {"ptBlueSpiralGame", &ptBlueSpiralGame_type},
    {"ptVarSyncGame", &ptVarSyncGame_type},
};

} // namespace Game
} // namespace plPy2CWE

// =============================================================================================

int plPy2CWE::InstallGameClasses()
{
    using namespace plPy2CWE::Game;
    PyObject* game = PyDict_GetItemString(PyImport_GetModuleDict(), "PlasmaGame");
    PyObject* constants = PyDict_GetItemString(PyImport_GetModuleDict(), "PlasmaGameConstants");
    if (!game || !constants)
        return -1;

    int       added = 0;
    PyObject* dict = PyModule_GetDict(game);
    auto      addClasses = [&](const ClassDef* classes, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (PyType_Ready(classes[i].type) < 0 ||
                PyDict_SetItemString(dict, const_cast<char*>(classes[i].name), (PyObject*)classes[i].type) != 0) {
                PyErr_Clear();
                return false;
            }
            added++;
        }
        return true;
    };
    size_t          msgCount = 0;
    const ClassDef* msgClasses = IMsgClasses(msgCount);
    if (!addClasses(kClasses, std::size(kClasses)) || !addClasses(msgClasses, msgCount))
        return -1;
    if (PyModule_AddFunctions(game, s_gameFunctions) != 0) {
        PyErr_Clear();
        return -1;
    }
    added += (int)std::size(s_gameFunctions) - 1;

    size_t         enumCount = 0;
    const EnumDef* enums = IEnums(enumCount);
    for (size_t i = 0; i < enumCount; ++i) {
        pyEnum::MakeEnumFromMap(constants, enums[i].name, enums[i].values);
        if (PyErr_Occurred()) {
            PyErr_Clear();
            return -1;
        }
        added++;
    }

    return added;
}

#endif // PLASMA_PYTHON_ABI < 3
