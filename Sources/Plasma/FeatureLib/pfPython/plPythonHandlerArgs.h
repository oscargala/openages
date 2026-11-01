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

#ifndef _plPythonHandlerArgs_h_
#define _plPythonHandlerArgs_h_

// What plPythonFileMod::MsgReceive (its plNotifyMsg and pfKIMsg branches, upstream's on both flavours) hands a script's
// OnNotify and OnKIMsg handlers. Where CWE's MsgReceive (OpenUru CWE-ou-minkata 76ef5e01, GPLv3) gave a py2 script
// another type than upstream's (H-uru ebe51e651) gives a py3 script, the builder calls one of the three functions
// below, each upstream's own call on py3. Every other value is the same type on both: PyInt_FromLong's numbers are
// PyLong_FromLong's, an `int` on py2 too (pyPy2Compat.h); PyString_FromString's names are PyUnicode_FromSTString's, a
// `str` on py2 too; PyLong_FromUnsignedLong, floats, keys, scene objects, points, None and the KI's tuples are alike.

#include <string_theory/string>

#include "pyGlueHelpers.h"

namespace plPython
{
// A number CWE made with PyLong_FromLong, a 2.3 `long`. In the plNotifyMsg branch: each event record's type (all
// thirteen cases), kControlKey's key, kVariable's type, kCallback's event, kResponderState's state, and
// kMultiStage's stage and event. In the pfKIMsg branch: kSetTextChatAdminMode's admin flag, kGZInRange's GZ number,
// kRateIt's rating, and the number of every other command (the default case).
// Upstream makes each with PyLong_FromLong in the same cases, an `int`.
inline PyObject* LongFrom(long value)
{
#if PLASMA_PYTHON_ABI >= 3
    return PyLong_FromLong(value);
#else
    return PyPlasma_LongObject_FromLong(value);
#endif
}

// The KI's text, which CWE made with PyUnicode_FromWideChar of GetStringU, a `unicode`, in the pfKIMsg branch:
// kYesNoDialog's question, kRateIt's text, kRegisterImager's name, kAddPlayerDevice's and kRemovePlayerDevice's
// device name, and the status, dialog, flash and marker texts (kKIChatStatusMsg to kKICreateMarkerNode).
// Upstream makes each with PyUnicode_FromSTString in the same cases, a `str`.
inline PyObject* UnicodeFrom(const ST::string& value)
{
#if PLASMA_PYTHON_ABI >= 3
    return PyUnicode_FromSTString(value);
#else
    return PyPlasma_UnicodeFromSTString(value);
#endif
}

// The events, from the tuple of event records the builder made (a reference this steals). CWE made a list of
// lists, starting from an empty list and appending each record it had a case for, so an event type without one
// (kClickDrag, kCoop, kClimbingBlockerHit) is left out (CWE's plNotifyMsg branch). Upstream's branch makes a tuple
// of GetEventCount() slots and hands it as built, so an event type without a case leaves an empty (NULL) slot.
inline PyObject* NotifyEvents(PyObject* records)
{
#if PLASMA_PYTHON_ABI >= 3
    return records;
#else
    if (!records)
        return nullptr;
    PyObject* events = PyList_New(0);
    for (int i = 0; events && i < PyTuple_GET_SIZE(records); ++i) {
        PyObject* record = PyTuple_GET_ITEM(records, i);
        if (!record)
            continue;
        PyObject* event = PyList_New(PyTuple_GET_SIZE(record));
        if (!event) {
            Py_CLEAR(events);
            break;
        }
        for (int j = 0; j < PyTuple_GET_SIZE(record); ++j) {
            PyObject* item = PyTuple_GET_ITEM(record, j);
            Py_XINCREF(item);
            PyList_SET_ITEM(event, j, item);
        }
        PyList_Append(events, event);
        Py_DECREF(event);
    }
    Py_DECREF(records);
    return events;
#endif
}
} // namespace plPython

#endif // _plPythonHandlerArgs_h_
