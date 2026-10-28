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
// CWE's synchronous ptScoreMgr and ptGameScore for the py2 flavour (pyScoreMgr2.h).
// This file is compiled only when PLASMA_PYTHON_ABI=2 (CMakeLists.txt); the py3 flavour never
// sees it.
//
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
//   - FeatureLib/pfGameScoreMgr/pfGameScoreMgr.{h,cpp}: the score cache and the blocking network
//     calls, here over H-uru's plNetGameLib score transactions (NetCliAuthScore*, the layer that
//     H-uru's own pfGameScore wraps);
//   - FeatureLib/pfPython/pyScoreMgr.{h,cpp}, pyScoreMgrGlue.cpp: ptScoreMgr;
//   - FeatureLib/pfPython/pyGameScore.{h,cpp}, pyGameScoreGlue.cpp: ptGameScore.
//
// Waiting. CWE's calls return their result to the script: each one pumps the net client on the
// calling thread until the transaction's callback has run (NetClientUpdate(); AsyncSleep(10); CWE
// pfGameScoreMgr::CreateScore and every wait below). The browser client runs the engine and Python
// on its own pthread (plClient's -sPROXY_TO_PTHREAD), and its sockets live on a separate socket
// thread with its own event loop (pnAcSocketEmscripten.cpp), which completes the transaction while
// the client thread sleeps; NetClientUpdate (NetTransUpdate) then sends the queued request and runs
// the callback on the client thread. So CWE's loop ports as is, the same loop H-uru's own *AndWait
// vault calls use on this thread (plVaultClientApi.cpp) and plNetClientComm's gatekeeper waits
// (plNetClientComm.cpp NetCommConnect). Like CWE, the frame does not advance during the wait, and
// the dispatcher is not pumped.

#include "pyPy2Compat.h"
#if PLASMA_PYTHON_ABI < 3

#include <chrono>
#include <cstdint>
#include <string_theory/string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "HeadSpin.h"
#include "hsRefCnt.h"
#include "plNetCommon/plNetCommon.h"
#include "plNetCommon/plNetServerSessionInfo.h"
#include "plNetGameLib/plNetGameLib.h"
#include "plVault/plVault.h"
#include "pnNetBase/pnNbConst.h"
#include "pnNetBase/pnNbError.h"
#include "pnNetProtocol/pnNpCommon.h"
#include "pyGlueHelpers.h"
#include "pyScoreMgr2.h"

namespace plPy2CWE
{
namespace Score
{

// CWE keeps game names in a char[kMaxGameScoreNameLength] (pfGameScore, pfGameScoreMgr.h) and
// rank names in a kMaxPlayerNameLength buffer (pyScoreMgr::GetRankList): at most size - 1
// characters.
ST::string ITruncate(const ST::string& s, size_t bufferSize)
{
    ST::utf32_buffer chars = s.to_utf32();
    if (chars.size() < bufferSize)
        return s;
    return ST::string::from_utf32(chars.data(), bufferSize - 1);
}

// ---------------------------------------------------------------------------------------------
// pfGameScore and the score cache: CWE pfGameScoreMgr.{h,cpp}, pfGameScore and the cache of
// pfGameScoreMgr (AddCachedScore, RemoveCachedScore). A score joins the cache when its id is set
// (Init); if the cache already holds that id, the cached score takes the new values instead. Points
// added, transferred or set through the manager change the cached score's value. Destroying a score
// removes its id from the cache.

struct pfGameScore : public hsRefCnt // CWE AtomicRef: the count starts at 0
{
    unsigned   scoreId = 0;
    unsigned   ownerId = 0;
    uint32_t   createdTime = 0;
    ST::string gameName;
    unsigned   gameType = 0;
    int        value = 0;

    pfGameScore() : hsRefCnt(0) {}
    ~pfGameScore();

    void Init(unsigned sid, unsigned oid, uint32_t createTime, const ST::string& gname, unsigned gType, int val);
    void CopyFrom(const pfGameScore* score);
};

class pfGameScoreMgr
{
    std::unordered_map<unsigned, pfGameScore*> fScores;

    pfGameScoreMgr() = default;

public:
    static pfGameScoreMgr* GetInstance()
    {
        static pfGameScoreMgr s_instance;
        return &s_instance;
    }

    // CWE pfGameScoreMgr::AddCachedScore
    void AddCachedScore(pfGameScore* score)
    {
        auto it = fScores.find(score->scoreId);
        if (it == fScores.end())
            fScores.emplace(score->scoreId, score);
        else
            it->second->CopyFrom(score);
    }

    void RemoveCachedScore(unsigned scoreId) { fScores.erase(scoreId); }

    pfGameScore* ICached(unsigned scoreId)
    {
        auto it = fScores.find(scoreId);
        return it == fScores.end() ? nullptr : it->second;
    }

    ENetError CreateScore(unsigned ownerId, const ST::string& gameName, unsigned gameType, int value, pfGameScore& score);
    ENetError DeleteScore(unsigned scoreId);
    ENetError AddPoints(unsigned scoreId, int numPoints);
    ENetError TransferPoints(unsigned srcScoreId, unsigned destScoreId, int numPoints);
    ENetError SetPoints(unsigned scoreId, int numPoints);
    ENetError GetScoresIncRef(unsigned ownerId, const ST::string& gameName, std::vector<pfGameScore*>& outScoreList);
    ENetError GetRankList(unsigned ownerId, unsigned scoreGroup, unsigned parentFolderId, const ST::string& gameName,
                          unsigned timePeriod, unsigned numResults, unsigned pageNumber, bool sortDesc,
                          std::vector<NetGameRank>& outRankList);
};

pfGameScore::~pfGameScore()
{
    pfGameScoreMgr::GetInstance()->RemoveCachedScore(scoreId);
}

void pfGameScore::Init(unsigned sid, unsigned oid, uint32_t createTime, const ST::string& gname, unsigned gType, int val)
{
    scoreId = sid;
    ownerId = oid;
    createdTime = createTime;
    gameType = gType;
    value = val;
    gameName = ITruncate(gname, kMaxGameScoreNameLength);
    pfGameScoreMgr::GetInstance()->AddCachedScore(this);
}

void pfGameScore::CopyFrom(const pfGameScore* score)
{
    scoreId = score->scoreId;
    ownerId = score->ownerId;
    createdTime = score->createdTime;
    gameType = score->gameType;
    value = score->value;
    gameName = score->gameName;
}

// CWE's NetWaitOp loop (every pfGameScoreMgr call in CWE pfGameScoreMgr.cpp): see the file
// comment.
// Why the loop has no bound and no shutdown check: as in CWE and in upstream's *AndWait vault
// loops (VaultAddChildNodeAndWait and its siblings, plVaultClientApi.cpp), NetTrans ends every
// transaction, and the callback then sets `complete`. A transaction that has been sent ends with
// its reply, its timeout (NetTransUpdate, kNetErrTimeout) or the loss of its connection
// (CheckedReconnect in plNglAuth.cpp, NetTransCancelByConnId). One still waiting for its
// connection (kTransStateWaitServerConnect in NetTransUpdate) has neither a timeout nor a
// connection id (NetTrans's constructor and NetTransUpdate). It ends when the connection comes up
// (then by reply or timeout), or when the connection is given up: after kDisconnectedTimeoutMs
// without a word from the server (plNetGameLib Intern.h), a failed reconnect re-enters
// CheckedReconnect (CliAuConn::AsyncNotifySocketConnectFailed), whose ReportNetError reaches the
// client's error handler (INetErrorCallback in plNetClientComm.cpp, installed before any script
// runs by plNetClientMgr::IInitNetClientComm). That calls NetClientDestroy(false), which cancels
// the transaction (AuthDestroy's NetTransCancelByProtocol with kNetErrRemoteShutdown, and
// NetTransDestroy's NetTransCancelAll), and this loop's own NetClientUpdate then runs its
// callback (NetTransUpdate, kTransStateComplete).
void IWait(const bool& complete)
{
    while (!complete) {
        NetClientUpdate();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// CWE pfGameScoreMgr::CreateScore
ENetError pfGameScoreMgr::CreateScore(unsigned ownerId, const ST::string& gameName, unsigned gameType, int value,
                                      pfGameScore& score)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreCreate(ownerId, gameName, gameType, value,
                          [&result, &complete, &score, ownerId, gameName, gameType, value](ENetError res, unsigned scoreId, uint32_t createdTime) {
        result = res;
        if (IS_NET_SUCCESS(res))
            score.Init(scoreId, ownerId, createdTime, gameName, gameType, value);
        else
            score.scoreId = 0;
        complete = true;
    });
    IWait(complete);
    return result;
}

// CWE pfGameScoreMgr::DeleteScore
ENetError pfGameScoreMgr::DeleteScore(unsigned scoreId)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreDelete(scoreId, [&result, &complete](ENetError res) {
        result = res;
        complete = true;
    });
    IWait(complete);
    return result;
}

// CWE pfGameScoreMgr::GetScoresIncRef. Each score is returned with one reference.
ENetError pfGameScoreMgr::GetScoresIncRef(unsigned ownerId, const ST::string& gameName,
                                          std::vector<pfGameScore*>& outScoreList)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreGetScores(ownerId, gameName,
                             [&result, &complete, &outScoreList](ENetError res, const std::vector<NetGameScore>& scores) {
        result = res;
        if (IS_NET_SUCCESS(res)) {
            outScoreList.reserve(scores.size());
            for (const NetGameScore& ngs : scores) {
                pfGameScore* score = new pfGameScore();
                score->Ref();
                score->Init(ngs.scoreId, ngs.ownerId, ngs.createdTime, ngs.gameName, ngs.gameType, ngs.value);
                outScoreList.push_back(score);
            }
        }
        complete = true;
    });
    IWait(complete);
    return result;
}

// CWE pfGameScoreMgr::AddPoints
ENetError pfGameScoreMgr::AddPoints(unsigned scoreId, int numPoints)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreAddPoints(scoreId, numPoints, [&result, &complete](ENetError res) {
        result = res;
        complete = true;
    });
    IWait(complete);
    if (IS_NET_SUCCESS(result)) {
        if (pfGameScore* score = ICached(scoreId))
            score->value += numPoints;
    }
    return result;
}

// CWE pfGameScoreMgr::TransferPoints
ENetError pfGameScoreMgr::TransferPoints(unsigned srcScoreId, unsigned destScoreId, int numPoints)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreTransferPoints(srcScoreId, destScoreId, numPoints, [&result, &complete](ENetError res) {
        result = res;
        complete = true;
    });
    IWait(complete);
    if (IS_NET_SUCCESS(result)) {
        if (pfGameScore* score = ICached(srcScoreId))
            score->value -= numPoints;
        if (pfGameScore* score = ICached(destScoreId))
            score->value += numPoints;
    }
    return result;
}

// CWE pfGameScoreMgr::SetPoints
ENetError pfGameScoreMgr::SetPoints(unsigned scoreId, int numPoints)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreSetPoints(scoreId, numPoints, [&result, &complete](ENetError res) {
        result = res;
        complete = true;
    });
    IWait(complete);
    if (IS_NET_SUCCESS(result)) {
        if (pfGameScore* score = ICached(scoreId))
            score->value = numPoints;
    }
    return result;
}

// CWE pfGameScoreMgr::GetRankList
ENetError pfGameScoreMgr::GetRankList(unsigned ownerId, unsigned scoreGroup, unsigned parentFolderId,
                                      const ST::string& gameName, unsigned timePeriod, unsigned numResults,
                                      unsigned pageNumber, bool sortDesc, std::vector<NetGameRank>& outRankList)
{
    ENetError result = kNetSuccess;
    bool      complete = false;
    NetCliAuthScoreGetRankList(ownerId, scoreGroup, parentFolderId, gameName, timePeriod, numResults, pageNumber,
                               sortDesc,
                               [&result, &complete, &outRankList](ENetError res, const std::vector<NetGameRank>& ranks) {
        result = res;
        if (IS_NET_SUCCESS(res))
            outRankList = ranks;
        complete = true;
    });
    IWait(complete);
    return result;
}

// ---------------------------------------------------------------------------------------------
// ptGameScore: CWE pyGameScore.{h,cpp} (pyGameScore), pyGameScoreGlue.cpp

class pyGameScore
{
public:
    pfGameScore* fScore = nullptr;

    pyGameScore() = default;
    ~pyGameScore()
    {
        if (fScore)
            fScore->UnRef();
    }

    static PyObject* New(pfGameScore* score);

    int        GetScoreID() const { return fScore ? (int)fScore->scoreId : 0; }
    uint32_t   GetCreatedTime() const { return fScore ? fScore->createdTime : 0; }
    int        GetOwnerID() const { return fScore ? (int)fScore->ownerId : 0; }
    int        GetGameType() const { return fScore ? (int)fScore->gameType : 0; }
    int        GetValue() const { return fScore ? fScore->value : 0; }
    ST::string GetGameName() const { return fScore ? fScore->gameName : ST::string(); }

    // CWE pyGameScore::AddPoints, TransferPoints, SetPoints: a fixed score refuses
    // (kNetErrScoreWrongType) without a request.
    bool AddPoints(int numPoints)
    {
        ENetError result = kNetErrScoreWrongType;
        if (fScore && fScore->gameType != kScoreTypeFixed)
            result = pfGameScoreMgr::GetInstance()->AddPoints(fScore->scoreId, numPoints);
        return IS_NET_SUCCESS(result);
    }

    bool TransferPoints(unsigned destination, int numPoints)
    {
        ENetError result = kNetErrScoreWrongType;
        if (fScore && fScore->gameType != kScoreTypeFixed)
            result = pfGameScoreMgr::GetInstance()->TransferPoints(fScore->scoreId, destination, numPoints);
        return IS_NET_SUCCESS(result);
    }

    bool SetPoints(int numPoints)
    {
        ENetError result = kNetErrScoreWrongType;
        if (fScore && fScore->gameType != kScoreTypeFixed)
            result = pfGameScoreMgr::GetInstance()->SetPoints(fScore->scoreId, numPoints);
        return IS_NET_SUCCESS(result);
    }
};

PYTHON_CLASS_DEFINITION(ptGameScore, pyGameScore);

PYTHON_DEFAULT_NEW_DEFINITION(ptGameScore, pyGameScore)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptGameScore)

PYTHON_NO_INIT_DEFINITION(ptGameScore)

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getScoreID)
{
    return PyInt_FromLong(self->fThis->GetScoreID());
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getCreatedTime)
{
    return PyLong_FromUnsignedLong(self->fThis->GetCreatedTime()); // a `long`, as CWE
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getOwnerID)
{
    return PyInt_FromLong(self->fThis->GetOwnerID());
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getValue)
{
    return PyInt_FromLong(self->fThis->GetValue());
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getGameType)
{
    return PyInt_FromLong(self->fThis->GetGameType());
}

PYTHON_METHOD_DEFINITION_NOARGS(ptGameScore, getGameName)
{
    ST::string name = self->fThis->GetGameName();
    return PyPlasma_StrFromUtf8(name.c_str(), (Py_ssize_t)name.size()); // cp1252 `str`
}

PYTHON_METHOD_DEFINITION(ptGameScore, addPoints, args)
{
    int numPoints = 0;
    if (!PyArg_ParseTuple(args, const_cast<char*>("i"), &numPoints)) {
        PyErr_SetString(PyExc_TypeError, "addPoints expects an int");
        PYTHON_RETURN_ERROR;
    }
    PYTHON_RETURN_BOOL(self->fThis->AddPoints(numPoints));
}

PYTHON_METHOD_DEFINITION(ptGameScore, transferPoints, args)
{
    unsigned int destination = 0;
    int          numPoints = 0;
    if (!PyArg_ParseTuple(args, const_cast<char*>("Ii"), &destination, &numPoints)) {
        PyErr_SetString(PyExc_TypeError, "transferPoints expects an unsigned int and an int");
        PYTHON_RETURN_ERROR;
    }
    PYTHON_RETURN_BOOL(self->fThis->TransferPoints(destination, numPoints));
}

PYTHON_METHOD_DEFINITION(ptGameScore, setPoints, args)
{
    int numPoints = 0;
    if (!PyArg_ParseTuple(args, const_cast<char*>("i"), &numPoints)) {
        PyErr_SetString(PyExc_TypeError, "setPoints expects an int");
        PYTHON_RETURN_ERROR;
    }
    PYTHON_RETURN_BOOL(self->fThis->SetPoints(numPoints));
}

// CWE pyGameScoreGlue.cpp, ptGameScore's method table: the same order and docstrings
PYTHON_START_METHODS_TABLE(ptGameScore)
PYTHON_METHOD_NOARGS(ptGameScore, getScoreID, "Returns the score id."),
    PYTHON_METHOD_NOARGS(ptGameScore, getOwnerID, "Returns a the score owner id."),
    PYTHON_METHOD_NOARGS(ptGameScore, getCreatedTime, "Returns a the score creation time."),
    PYTHON_METHOD_NOARGS(ptGameScore, getValue, "Returns a the score owner value."),
    PYTHON_METHOD_NOARGS(ptGameScore, getGameType, "Returns a the score game type."),
    PYTHON_METHOD_NOARGS(ptGameScore, getGameName, "Returns a the score game name."),
    PYTHON_METHOD(ptGameScore, addPoints, "Params: numPoints\nAdds points to the score"),
    PYTHON_METHOD(ptGameScore, transferPoints, "Params: dest, numPoints\nTransfers points from one score to another"),
    PYTHON_METHOD(ptGameScore, setPoints, "Params: numPoints\nSets the number of points in the score\nDon't use to add/remove points, use only to reset values!"),
    PYTHON_END_METHODS_TABLE;

PLASMA_DEFAULT_TYPE(ptGameScore, "Game score manager");

// CWE pyGameScoreGlue.cpp, pyGameScore::New
PyObject* pyGameScore::New(pfGameScore* score)
{
    ptGameScore* newObj = (ptGameScore*)ptGameScore_type.tp_new(&ptGameScore_type, nullptr, nullptr);
    if (!newObj)
        return nullptr;
    if (newObj->fThis->fScore)
        newObj->fThis->fScore->UnRef();
    newObj->fThis->fScore = score;
    if (newObj->fThis->fScore)
        newObj->fThis->fScore->Ref();
    return (PyObject*)newObj;
}

// ---------------------------------------------------------------------------------------------
// ptScoreMgr: CWE pyScoreMgr.{h,cpp} (pyScoreMgr), pyScoreMgrGlue.cpp

// The list CWE builds from a successful fetch with at least one score, else None
// (pyScoreMgr::GetGlobalScores and its three twins). Drops the fetch's references.
PyObject* IScoreList(ENetError result, std::vector<pfGameScore*>& scoreList)
{
    PyObject* pyScoreList = nullptr;
    if (IS_NET_SUCCESS(result) && !scoreList.empty()) {
        pyScoreList = PyList_New((Py_ssize_t)scoreList.size());
        for (size_t i = 0; i < scoreList.size(); ++i) {
            PyObject* pyScore = pyGameScore::New(scoreList[i]);
            if (pyScoreList)
                PyList_SetItem(pyScoreList, (Py_ssize_t)i, pyScore);
            else
                Py_XDECREF(pyScore);
        }
    }
    for (pfGameScore* score : scoreList)
        score->UnRef();
    scoreList.clear();
    if (pyScoreList)
        return pyScoreList;
    if (PyErr_Occurred())
        return nullptr;
    PYTHON_RETURN_NONE;
}

// The score CWE's Create*Score returns: the new score if the server made it, else None
// (pyScoreMgr::CreateGlobalScore and its three twins). `score` holds the creation's reference.
PyObject* IScoreResult(pfGameScore* score)
{
    if (score) {
        if (score->scoreId > 0) {
            PyObject* pyScore = pyGameScore::New(score);
            score->UnRef();
            return pyScore;
        }
        score->UnRef();
    }
    PYTHON_RETURN_NONE;
}

pfGameScore* ICreate(unsigned ownerId, const ST::string& gameName, unsigned gameType, int value)
{
    pfGameScore* score = new pfGameScore();
    score->Ref();
    pfGameScoreMgr::GetInstance()->CreateScore(ownerId, gameName, gameType, value, *score);
    return score;
}

class pyScoreMgr
{
public:
    // CWE pyScoreMgr::DeleteScore
    bool DeleteScore(unsigned scoreId)
    {
        return IS_NET_SUCCESS(pfGameScoreMgr::GetInstance()->DeleteScore(scoreId));
    }

    // CWE pyScoreMgr::CreateGlobalScore, GetGlobalScores
    PyObject* CreateGlobalScore(const ST::string& gameName, unsigned gameType, int value)
    {
        return IScoreResult(ICreate(0, gameName, gameType, value));
    }

    PyObject* GetGlobalScores(const ST::string& gameName)
    {
        std::vector<pfGameScore*> scoreList;
        ENetError                 result = pfGameScoreMgr::GetInstance()->GetScoresIncRef(0, gameName, scoreList);
        return IScoreList(result, scoreList);
    }

    // CWE pyScoreMgr::CreatePlayerScore, GetPlayerScores (none without a player info node)
    PyObject* CreatePlayerScore(const ST::string& gameName, unsigned gameType, int value)
    {
        hsRef<RelVaultNode> rvn = VaultGetPlayerInfoNode();
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IScoreResult(ICreate(rvn->GetNodeId(), gameName, gameType, value));
    }

    PyObject* GetPlayerScores(const ST::string& gameName)
    {
        hsRef<RelVaultNode> rvn = VaultGetPlayerInfoNode();
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IOwnerScores(rvn->GetNodeId(), gameName);
    }

    // CWE pyScoreMgr::CreateNeighborhoodScore, GetNeighborhoodScores (the owned Neighborhood's age
    // info node)
    PyObject* CreateNeighborhoodScore(const ST::string& gameName, unsigned gameType, int value)
    {
        plAgeInfoStruct info;
        info.SetAgeFilename(kNeighborhoodAgeFilename);
        hsRef<RelVaultNode> rvn = VaultGetOwnedAgeInfo(&info);
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IScoreResult(ICreate(rvn->GetNodeId(), gameName, gameType, value));
    }

    PyObject* GetNeighborhoodScores(const ST::string& gameName)
    {
        plAgeInfoStruct info;
        info.SetAgeFilename(kNeighborhoodAgeFilename);
        hsRef<RelVaultNode> rvn = VaultGetOwnedAgeInfo(&info);
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IOwnerScores(rvn->GetNodeId(), gameName);
    }

    // CWE pyScoreMgr::CreateCurrentAgeScore, GetCurrentAgeScores (the current age's info node)
    PyObject* CreateCurrentAgeScore(const ST::string& gameName, unsigned gameType, int value)
    {
        hsRef<RelVaultNode> rvn = VaultGetAgeInfoNode();
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IScoreResult(ICreate(rvn->GetNodeId(), gameName, gameType, value));
    }

    PyObject* GetCurrentAgeScores(const ST::string& gameName)
    {
        hsRef<RelVaultNode> rvn = VaultGetAgeInfoNode();
        if (!rvn)
            PYTHON_RETURN_NONE;
        return IOwnerScores(rvn->GetNodeId(), gameName);
    }

    // CWE pyScoreMgr::GetRankList: a list of (rank, name, score) tuples, or None
    PyObject* GetRankList(unsigned scoreGroup, unsigned parentFolderId, const ST::string& gameName,
                          unsigned timePeriod, unsigned numResults, unsigned pageNumber, bool sortDesc)
    {
        hsRef<RelVaultNode> rvn = VaultGetPlayerInfoNode();
        if (!rvn)
            PYTHON_RETURN_NONE;
        unsigned ownerId = rvn->GetNodeId();
        rvn = nullptr;

        std::vector<NetGameRank> rankList;
        ENetError                result = pfGameScoreMgr::GetInstance()->GetRankList(ownerId, scoreGroup, parentFolderId, gameName,
                                                                                     timePeriod, numResults, pageNumber, sortDesc,
                                                                                     rankList);
        if (!IS_NET_SUCCESS(result) || rankList.empty())
            PYTHON_RETURN_NONE;

        PyObject* pyRankList = PyList_New((Py_ssize_t)rankList.size());
        if (!pyRankList)
            return nullptr;
        for (size_t i = 0; i < rankList.size(); ++i) {
            ST::string name = ITruncate(rankList[i].name, kMaxPlayerNameLength);
            PyObject*  pyRank = PyTuple_New(3);
            if (!pyRank) {
                Py_DECREF(pyRankList);
                return nullptr;
            }
            PyTuple_SetItem(pyRank, 0, PyInt_FromLong((long)rankList[i].rank));
            PyTuple_SetItem(pyRank, 1, PyPlasma_StrFromUtf8(name.c_str(), (Py_ssize_t)name.size()));
            PyTuple_SetItem(pyRank, 2, PyInt_FromLong((long)rankList[i].score));
            PyList_SetItem(pyRankList, (Py_ssize_t)i, pyRank);
        }
        return pyRankList;
    }

private:
    static PyObject* IOwnerScores(unsigned ownerId, const ST::string& gameName)
    {
        std::vector<pfGameScore*> scoreList;
        ENetError                 result = pfGameScoreMgr::GetInstance()->GetScoresIncRef(ownerId, gameName, scoreList);
        return IScoreList(result, scoreList);
    }
};

PYTHON_CLASS_DEFINITION(ptScoreMgr, pyScoreMgr);

PYTHON_DEFAULT_NEW_DEFINITION(ptScoreMgr, pyScoreMgr)
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptScoreMgr)

PYTHON_INIT_DEFINITION(ptScoreMgr, args, keywords)
{
    PYTHON_RETURN_INIT_OK;
}

PYTHON_METHOD_DEFINITION(ptScoreMgr, deleteScore, args)
{
    int scoreId;
    if (!PyArg_ParseTuple(args, const_cast<char*>("i"), &scoreId)) {
        PyErr_SetString(PyExc_TypeError, "deleteScore expects an int");
        PYTHON_RETURN_ERROR;
    }
    PYTHON_RETURN_BOOL(self->fThis->DeleteScore((unsigned)scoreId));
}

// The game name arguments are CWE's "s" (a char*), converted to the engine's UTF-8 as every
// 8-bit argument of the py2 flavour is (a `str` is read as cp1252, pyPy2Compat.h).
#define PY2_SCORE_CREATE_METHOD(method, call, message)                                                 \
    PYTHON_METHOD_DEFINITION(ptScoreMgr, method, args)                                                 \
    {                                                                                                  \
        ST::string gameName;                                                                           \
        int        gameType;                                                                           \
        int        value;                                                                              \
        if (!PyArg_ParseTuple(args, const_cast<char*>("O&ii"), PyUnicode_STStringConverter, &gameName, \
                              &gameType, &value)) {                                                    \
            PyErr_SetString(PyExc_TypeError, message);                                                 \
            PYTHON_RETURN_ERROR;                                                                       \
        }                                                                                              \
        return self->fThis->call(gameName, (unsigned)gameType, value);                                 \
    }

#define PY2_SCORE_GET_METHOD(method, call, message)                                                     \
    PYTHON_METHOD_DEFINITION(ptScoreMgr, method, args)                                                  \
    {                                                                                                   \
        ST::string gameName;                                                                            \
        if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &gameName)) { \
            PyErr_SetString(PyExc_TypeError, message);                                                  \
            PYTHON_RETURN_ERROR;                                                                        \
        }                                                                                               \
        return self->fThis->call(gameName);                                                             \
    }

// CWE pyScoreMgrGlue.cpp, createGlobalScore through getCurrentAgeScores
PY2_SCORE_CREATE_METHOD(createGlobalScore, CreateGlobalScore, "createGlobalScore expects a string, an int, and an int")
PY2_SCORE_GET_METHOD(getGlobalScores, GetGlobalScores, "getGlobalScores expects a string")
PY2_SCORE_CREATE_METHOD(createPlayerScore, CreatePlayerScore, "createPlayerScore expects a string, an int, and an int")
PY2_SCORE_GET_METHOD(getPlayerScores, GetPlayerScores, "getPlayerScores expects a string")
PY2_SCORE_CREATE_METHOD(createNeighborhoodScore, CreateNeighborhoodScore, "createNeighborhoodScore expects a string, an int, and an int")
PY2_SCORE_GET_METHOD(getNeighborhoodScores, GetNeighborhoodScores, "getNeighborhoodScores expects a string")
PY2_SCORE_CREATE_METHOD(createCurrentAgeScore, CreateCurrentAgeScore, "createCurrentAgeScore expects a string, an int, and an int")
PY2_SCORE_GET_METHOD(getCurrentAgeScores, GetCurrentAgeScores, "getCurrentAgeScores expects a string")

#undef PY2_SCORE_CREATE_METHOD
#undef PY2_SCORE_GET_METHOD

// CWE pyScoreMgrGlue.cpp, getRankList
PYTHON_METHOD_DEFINITION(ptScoreMgr, getRankList, args)
{
    int        scoreGroup;
    int        parentFolderId;
    ST::string gameName;
    int        timePeriod;
    int        numResults;
    int        pageNumber;
    int        sortDesc;
    if (!PyArg_ParseTuple(args, const_cast<char*>("iiO&iiii"), &scoreGroup, &parentFolderId,
                          PyUnicode_STStringConverter, &gameName, &timePeriod, &numResults, &pageNumber, &sortDesc)) {
        PyErr_SetString(PyExc_TypeError, "getRankList expects two ints, a string, and four more ints");
        PYTHON_RETURN_ERROR;
    }
    return self->fThis->GetRankList((unsigned)scoreGroup, (unsigned)parentFolderId, gameName, (unsigned)timePeriod,
                                    (unsigned)numResults, (unsigned)pageNumber, sortDesc != 0);
}

// CWE pyScoreMgrGlue.cpp, ptScoreMgr's method table: the same order and docstrings
PYTHON_START_METHODS_TABLE(ptScoreMgr)
PYTHON_METHOD(ptScoreMgr, deleteScore, "Params: scoreId\nDeletes the specified score"),
    PYTHON_METHOD(ptScoreMgr, createGlobalScore, "Params: gameName, gameType, value\nCreates a score and returns it"),
    PYTHON_METHOD(ptScoreMgr, getGlobalScores, "Params: gameName\nReturns a list of scores associated with the specified game."),
    PYTHON_METHOD(ptScoreMgr, createPlayerScore, "Params: gameName, gameType, value\nCreates a score and returns it"),
    PYTHON_METHOD(ptScoreMgr, getPlayerScores, "Params: gameName\nReturns a list of scores associated with the specified game."),
    PYTHON_METHOD(ptScoreMgr, createNeighborhoodScore, "Params: gameName, gameType, value\nCreates a score and returns it"),
    PYTHON_METHOD(ptScoreMgr, getNeighborhoodScores, "Params: gameName\nReturns a list of scores associated with the specified game."),
    PYTHON_METHOD(ptScoreMgr, createCurrentAgeScore, "Params: gameName, gameType, value\nCreates a score and returns it"),
    PYTHON_METHOD(ptScoreMgr, getCurrentAgeScores, "Params: gameName\nReturns a list of scores associated with the specified game."),
    PYTHON_METHOD(ptScoreMgr, getRankList, "Params: ownerInfoId, scoreGroup, parentFolderId, gameName, timePeriod, numResults, pageNumber, sortDesc\nReturns a list of scores and rank"),
    PYTHON_END_METHODS_TABLE;

PLASMA_DEFAULT_TYPE(ptScoreMgr, "Game score manager");

} // namespace Score
} // namespace plPy2CWE

// CWE pyScoreMgr::AddPlasmaClasses and pyGameScore::AddPlasmaClasses (pyScoreMgrGlue.cpp,
// pyGameScoreGlue.cpp) add both classes to Plasma. Here ptGameScore replaces H-uru's class of that
// name. CWE's three score enums (pyScoreMgr::AddPlasmaConstantsClasses) are H-uru's
// PlasmaConstants ones already, with the same names, elements and values (H-uru
// pyGameScore::AddPlasmaConstantsClasses; pnNbConst.h EGameScoreTypes, EScoreRankGroups,
// EScoreTimePeriods in both trees).
int plPy2CWE::InstallScoreClasses(PyObject* plasmaModule)
{
    using namespace plPy2CWE::Score;
    if (!plasmaModule)
        return -1;
    PyObject* dict = PyModule_GetDict(plasmaModule);
    if (PyType_Ready(&ptScoreMgr_type) < 0 || PyType_Ready(&ptGameScore_type) < 0) {
        PyErr_Clear();
        return -1;
    }
    if (PyDict_SetItemString(dict, const_cast<char*>("ptScoreMgr"), (PyObject*)&ptScoreMgr_type) != 0 ||
        PyDict_SetItemString(dict, const_cast<char*>("ptGameScore"), (PyObject*)&ptGameScore_type) != 0) {
        PyErr_Clear();
        return -1;
    }
    return 2;
}

#endif // PLASMA_PYTHON_ABI < 3
