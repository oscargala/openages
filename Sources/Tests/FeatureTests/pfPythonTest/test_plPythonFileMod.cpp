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

#include <Python.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_theory/string>

#include "hsGeometry3.h"
#include "pfMessage/pfKIMsg.h"
#include "pfPython/cyPythonInterface.h"
#include "pfPython/plPythonFileMod.h"
#include "pfPython/pyKey.h"
#include "pfPython/pyObjectRef.h"
#include "pnKeyedObject/plKey.h"
#include "pnMessage/plNotifyMsg.h"

// What a script's OnNotify and OnKIMsg handlers receive: each notify event type and each shape of
// KI value, sent through plPythonFileMod::MsgReceive to a script whose handlers record their
// arguments. Each value is written as "<type> <repr>" (the Plasma objects by their type name, a
// ptPoint3 by its coordinates), a container as "<type>[<items>]", and an empty tuple slot as NULL.

namespace
{

// The script: handlers that record their arguments, and the description of a value.
const char kScript[] =
    "calls = []\n"
    "def OnNotify(*args):\n"
    "    calls.append(('OnNotify',) + args)\n"
    "def OnKIMsg(*args):\n"
    "    calls.append(('OnKIMsg',) + args)\n"
    "def describe(v):\n"
    "    t = type(v).__name__\n"
    "    if t in ('list', 'tuple'):\n"
    "        return '%s[%s]' % (t, ', '.join([describe(x) for x in v]))\n"
    "    if t == 'ptPoint3':\n"
    "        return 'ptPoint3(%r, %r, %r)' % (v.getX(), v.getY(), v.getZ())\n"
    "    if t in ('ptKey', 'ptSceneobject'):\n"
    "        return t\n"
    "    return '%s %s' % (t, ascii(v))\n";

PyObject* sGlobals = nullptr;

// A python file mod whose script is kScript: its handlers are the script's functions, and its
// own key (the scene objects' sender) is a ptKey of no object.
class plArgsProbeMod : public plPythonFileMod
{
public:
    plArgsProbeMod()
    {
        fSelfKey = pyKey::New(plKey());
        ISetHandler("OnNotify");
        ISetHandler("OnKIMsg");
    }

private:
    void ISetHandler(const char* name)
    {
        for (size_t i = 0; fFunctionNames[i] != nullptr; ++i) {
            if (strcmp(fFunctionNames[i], name) == 0) {
                fPyFunctionInstances[i] = PyDict_GetItemString(sGlobals, name);
                Py_XINCREF(fPyFunctionInstances[i]);
            }
        }
    }
};

class plPythonFileModArgs : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (Py_IsInitialized())
            return;
        // The interpreter on the standard library of the Python the build links (when CMake found
        // it, Python3_STDLIB), as PythonInterface::initPython configures it (isolated, without site).
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.site_import = 0;
        config.write_bytecode = 0;
#ifdef PLASMA_TEST_PYTHON_STDLIB
        PyWideStringList_Append(&config.module_search_paths, L"" PLASMA_TEST_PYTHON_STDLIB);
        config.module_search_paths_set = 1;
#endif
        PyStatus status = Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);
        ASSERT_FALSE(PyStatus_Exception(status)) << (status.err_msg ? status.err_msg : "");

        // The Plasma module's classes (ptKey, ptSceneobject, ptPoint3, ...), as initPython adds them.
        pyObjectRef plasma = PyModule_New("Plasma");
        PythonInterface::AddPlasmaClasses(plasma.Get());

        sGlobals = PyDict_New();
        PyDict_SetItemString(sGlobals, "__builtins__", PyEval_GetBuiltins());
        pyObjectRef result = PyRun_String(kScript, Py_file_input, sGlobals, sGlobals);
        if (!result)
            PyErr_Print();
        ASSERT_TRUE(result);
    }

    // The description of one value, through the script's describe().
    static std::string IDescribe(PyObject* value)
    {
        if (!value)
            return "NULL";
        pyObjectRef text = PyObject_CallFunctionObjArgs(PyDict_GetItemString(sGlobals, "describe"), value, nullptr);
        if (!text) {
            PyErr_Clear();
            return "<describe failed>";
        }
        return PyUnicode_AsUTF8(text.Get());
    }

    // A container at the top level item by item, so that an empty slot reads NULL.
    static std::string IDescribeContainer(PyObject* value)
    {
        if (!value || !(PyTuple_Check(value) || PyList_Check(value)))
            return IDescribe(value);
        const bool       tuple = PyTuple_Check(value);
        std::string      out = tuple ? "tuple[" : "list[";
        const Py_ssize_t size = tuple ? PyTuple_GET_SIZE(value) : PyList_GET_SIZE(value);
        for (Py_ssize_t i = 0; i < size; ++i) {
            if (i)
                out += ", ";
            out += IDescribe(tuple ? PyTuple_GET_ITEM(value, i) : PyList_GET_ITEM(value, i));
        }
        return out + "]";
    }

    // Sends msg to a probe mod and describes the handler's call: "<handler>(<arg>; <arg>; ...)", or
    // "no call".
    static std::string ISend(plMessage* msg)
    {
        PyObject* calls = PyDict_GetItemString(sGlobals, "calls");
        PyList_SetSlice(calls, 0, PyList_GET_SIZE(calls), nullptr);
        {
            plArgsProbeMod mod;
            mod.MsgReceive(msg);
        }
        hsRefCnt_SafeUnRef(msg);
        if (PyErr_Occurred()) {
            PyErr_Print();
            return "<error>";
        }
        if (PyList_GET_SIZE(calls) != 1)
            return PyList_GET_SIZE(calls) ? "several calls" : "no call";
        PyObject*   call = PyList_GET_ITEM(calls, 0);
        std::string out = PyUnicode_AsUTF8(PyTuple_GET_ITEM(call, 0));
        out += "(";
        for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(call); ++i) {
            if (i > 1)
                out += "; ";
            out += IDescribeContainer(PyTuple_GET_ITEM(call, i));
        }
        return out + ")";
    }

    static plNotifyMsg* INotify()
    {
        plNotifyMsg* msg = new plNotifyMsg();
        msg->SetState(1.0f);
        return msg;
    }

    // The OnNotify call for a message with one event; the events argument is described alone.
    static std::string IEvents(plNotifyMsg* msg)
    {
        std::string       call = ISend(msg);
        const std::string head = "OnNotify(float 1.0; int -1; ";
        if (call.compare(0, head.size(), head) != 0 || call.back() != ')')
            return call;
        return call.substr(head.size(), call.size() - head.size() - 1);
    }

    static pfKIMsg* IKIMsg(uint8_t command)
    {
        return new pfKIMsg(command);
    }

    // The OnKIMsg call expected for a command and its value's description.
    static std::string IKICall(uint8_t command, const char* value)
    {
        return "OnKIMsg(int " + std::to_string(command) + "; " + value + ")";
    }
};

// The KI's text in these checks: "Été" (UTF-8 C3 89 74 C3 A9), and a user name "Ré".
const ST::string kKIText = ST_LITERAL("\xC3\x89t\xC3\xA9");
const ST::string kKIUser = ST_LITERAL("R\xC3\xA9");

} // namespace

// OnNotify(state, id, events): state is the message's, id -1 when no parameter sent it, and the
// events a tuple of tuples, one per event record, each starting with its event type.
TEST_F(plPythonFileModArgs, notify_call)
{
    plNotifyMsg* msg = INotify();
    msg->AddActivateEvent(true);
    EXPECT_EQ(ISend(msg), "OnNotify(float 1.0; int -1; tuple[tuple[int 7, int 1, int 1]])");
}

TEST_F(plPythonFileModArgs, notify_collision)
{
    plNotifyMsg* msg = INotify();
    msg->AddCollisionEvent(true, plKey(), plKey());
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 1, int 1, ptSceneobject, ptSceneobject]]");
}

TEST_F(plPythonFileModArgs, notify_picked)
{
    plNotifyMsg* msg = INotify();
    msg->AddPickEvent(plKey(), plKey(), true, hsPoint3(1.5f, -2.f, 0.25f));
    EXPECT_EQ(IEvents(msg),
              "tuple[tuple[int 2, int 1, ptSceneobject, ptSceneobject, "
              "ptPoint3(1.5, -2.0, 0.25), ptPoint3(0.0, 0.0, 0.0)]]");
}

TEST_F(plPythonFileModArgs, notify_control_key)
{
    plNotifyMsg* msg = INotify();
    msg->AddControlKeyEvent(42, true);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 3, int 42, int 1]]");
}

TEST_F(plPythonFileModArgs, notify_variable)
{
    plNotifyMsg* msg = INotify();
    msg->AddVariableEvent(ST_LITERAL("f"), 0.5f);
    msg->AddVariableEvent(ST_LITERAL("V\xC3\xA4r"), -7);
    msg->AddVariableEvent(ST_LITERAL("k"), plKey());
    msg->AddVariableEvent(ST_LITERAL("n"));
    EXPECT_EQ(IEvents(msg),
              "tuple[tuple[int 4, str 'f', int 1, float 0.5], "
              "tuple[int 4, str 'V\\xe4r', int 3, int -7], "
              "tuple[int 4, str 'k', int 2, ptKey], "
              "tuple[int 4, str 'n', int 4, NoneType None]]");
}

TEST_F(plPythonFileModArgs, notify_facing)
{
    plNotifyMsg* msg = INotify();
    msg->AddFacingEvent(plKey(), plKey(), 0.75f, false);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 5, int 0, ptSceneobject, ptSceneobject, float 0.75]]");
}

TEST_F(plPythonFileModArgs, notify_contained)
{
    plNotifyMsg* msg = INotify();
    msg->AddContainerEvent(plKey(), plKey(), false);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 6, int 0, ptSceneobject, ptSceneobject]]");
}

TEST_F(plPythonFileModArgs, notify_activate)
{
    plNotifyMsg* msg = INotify();
    msg->AddActivateEvent(false);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 7, int 1, int 0]]");
}

TEST_F(plPythonFileModArgs, notify_callback)
{
    plNotifyMsg* msg = INotify();
    msg->AddCallbackEvent(3);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 8, int 3]]");
}

TEST_F(plPythonFileModArgs, notify_responder_state)
{
    plNotifyMsg* msg = INotify();
    msg->AddResponderStateEvent(2);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 9, int 2]]");
}

TEST_F(plPythonFileModArgs, notify_multistage)
{
    plNotifyMsg* msg = INotify();
    msg->AddMultiStageEvent(1, 4, plKey());
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 10, int 1, int 4, ptSceneobject]]");
}

TEST_F(plPythonFileModArgs, notify_spawned)
{
    plNotifyMsg* msg = INotify();
    msg->AddSpawnedEvent(plKey(), plKey());
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 11, ptSceneobject, ptSceneobject]]");
}

TEST_F(plPythonFileModArgs, notify_offer_linking_book)
{
    plNotifyMsg* msg = INotify();
    msg->AddOfferBookEvent(plKey(), 5, 9);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 14, ptSceneobject, int 5, int 9]]");
}

TEST_F(plPythonFileModArgs, notify_book)
{
    plNotifyMsg* msg = INotify();
    msg->AddBookEvent(3, 0xFFFFFFFF);
    EXPECT_EQ(IEvents(msg), "tuple[tuple[int 15, int 3, int 4294967295]]");
}

// The three event types the builder has no record for (kClickDrag, kCoop, kClimbingBlockerHit)
// leave their slot of the events tuple empty: the tuple has one slot per event of the message.
TEST_F(plPythonFileModArgs, notify_unbuilt_event_types)
{
    plNotifyMsg* msg = INotify();
    msg->AddCallbackEvent(1);
    msg->AddClickDragEvent(plKey(), plKey(), 0.5f);
    msg->AddActivateEvent(true);
    msg->AddCoopEvent(7, 2);
    msg->AddHitClimbingBlockerEvent(plKey());
    msg->AddResponderStateEvent(0);
    EXPECT_EQ(IEvents(msg),
              "tuple[tuple[int 8, int 1], NULL, tuple[int 7, int 1, int 1], NULL, NULL, "
              "tuple[int 9, int 0]]");
}

// OnKIMsg(command, value), for each shape of value.
TEST_F(plPythonFileModArgs, ki_chat_fade_delay)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kSetChatFadeDelay);
    msg->SetDelay(2.5f);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kSetChatFadeDelay, "float 2.5"));
}

TEST_F(plPythonFileModArgs, ki_admin_mode)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kSetTextChatAdminMode);
    msg->SetFlags(pfKIMsg::kAdminMsg | pfKIMsg::kGlobalMsg);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kSetTextChatAdminMode, "int 1"));
}

TEST_F(plPythonFileModArgs, ki_yes_no_dialog)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kYesNoDialog);
    msg->SetString(kKIText);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kYesNoDialog, "tuple[str '\\xc9t\\xe9', ptKey]"));
}

TEST_F(plPythonFileModArgs, ki_gz_in_range)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kGZInRange);
    msg->SetIntValue(7);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kGZInRange, "tuple[int 7, ptKey]"));
}

TEST_F(plPythonFileModArgs, ki_rate_it)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kRateIt);
    msg->SetUser(kKIUser);
    msg->SetString(kKIText);
    msg->SetIntValue(-3);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kRateIt, "tuple[str 'R\\xe9', str '\\xc9t\\xe9', int -3]"));
}

TEST_F(plPythonFileModArgs, ki_register_imager)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kRegisterImager);
    msg->SetString(kKIText);
    EXPECT_EQ(ISend(msg), IKICall(pfKIMsg::kRegisterImager, "tuple[str '\\xc9t\\xe9', ptKey]"));
}

TEST_F(plPythonFileModArgs, ki_player_device)
{
    for (uint8_t command : {pfKIMsg::kAddPlayerDevice, pfKIMsg::kRemovePlayerDevice}) {
        pfKIMsg* msg = IKIMsg(command);
        msg->SetString(kKIText);
        EXPECT_EQ(ISend(msg), IKICall(command, "str '\\xc9t\\xe9'"));
        EXPECT_EQ(ISend(IKIMsg(command)), IKICall(command, "NoneType None"));
    }
}

TEST_F(plPythonFileModArgs, ki_text)
{
    for (uint8_t command : {pfKIMsg::kKIChatStatusMsg, pfKIMsg::kKILocalChatStatusMsg,
                            pfKIMsg::kKILocalChatErrorMsg, pfKIMsg::kKIOKDialog, pfKIMsg::kKIOKDialogNoQuit,
                            pfKIMsg::kGZFlashUpdate, pfKIMsg::kKICreateMarkerNode}) {
        pfKIMsg* msg = IKIMsg(command);
        msg->SetString(kKIText);
        msg->SetIntValue(5);
        EXPECT_EQ(ISend(msg), IKICall(command, "str '\\xc9t\\xe9'"));
    }
}

// Every other command (the three the builder names among them) gets the message's number.
TEST_F(plPythonFileModArgs, ki_number)
{
    for (uint8_t command : {pfKIMsg::kMGStartCGZGame, pfKIMsg::kMGStopCGZGame, pfKIMsg::kFriendInviteSent,
                            pfKIMsg::kEnterChatMode, pfKIMsg::kQuitDialog, pfKIMsg::kUpgradeKIMarkerLevel}) {
        pfKIMsg* msg = IKIMsg(command);
        msg->SetString(kKIText);
        msg->SetIntValue(-9);
        EXPECT_EQ(ISend(msg), IKICall(command, "int -9"));
    }
}

// A chat line goes to OnRTChat, never to OnKIMsg.
TEST_F(plPythonFileModArgs, ki_chat_line)
{
    pfKIMsg* msg = IKIMsg(pfKIMsg::kHACKChatMsg);
    msg->SetString(kKIText);
    EXPECT_EQ(ISend(msg), "no call");
}
