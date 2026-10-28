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

// The CWE surface overlay of the py2 flavour (pyCWEOverlay2.h). This file is compiled only when
// PLASMA_PYTHON_ABI=2 (CMakeLists.txt); the py3 flavour never sees it.
//
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfPython. It reproduces, on top of H-uru's glue, the Python API that Cyan's CWE glue
// gave MOULa's python.pak. Each ported part names the CWE file and function it reproduces; each
// table entry names the CWE glue file that defines the name.

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

#include <string>
#include <string_theory/string>
#include <vector>

#include "HeadSpin.h"
#include "cyAccountManagement.h"
#include "cyMisc.h"
#include "hsResMgr.h"
#include "pfGameGUIMgr/pfGUIMultiLineEditCtrl.h"
#include "pfMessage/pfKIMsg.h"
#include "plFileSystem.h"
#include "plNetClientComm/plNetClientComm.h"
#include "plNetCommon/plNetServerSessionInfo.h"
#include "plResMgr/plKeyFinder.h"
#include "plResMgr/plLocalization.h"
#include "plStatusLog/plStatusLog.h"
#include "plVault/plVault.h"
#include "plgDispatch.h"
#include "pnKeyedObject/plFixedKey.h"
#include "pnKeyedObject/plKey.h"
#include "pnMessage/plClientMsg.h"
#include "pnNetBase/pnNbConst.h"
#include "pnNetCommon/plSynchedObject.h"
#include "pyAgeInfoStruct.h"
#include "pyAgeLinkStruct.h"
#include "pyAudioControl.h"
#include "pyCWEOverlay2.h"
#include "pyGUIControlEditBox.h"
#include "pyGUIControlListBox.h"
#include "pyGUIControlMultiLineEdit.h"
#include "pyGUIControlTextBox.h"
#include "pyGameMgr2.h"
#include "pyGeometry3.h"
#include "pyGlueHelpers.h"
#include "pyKey.h"
#include "pyMarkerMgr.h"
#include "pyObjectRef.h"
#include "pyScoreMgr2.h"
#include "pyVault.h"
#include "pyVaultAgeLinkNode.h"
#include "pyVaultChronicleNode.h"
#include "pyVaultFolderNode.h"
#include "pyVaultImageNode.h"
#include "pyVaultNode.h"
#include "pyVaultPlayerInfoNode.h"
#include "pyVaultPlayerNode.h"
#include "pyVaultTextNoteNode.h"

namespace
{

// ---------------------------------------------------------------------------------------------
// ptCoercedMethod: an H-uru method under its CWE name, called as it is, with its integer result
// turned into the type CWE's glue made (`int` or `long`) where the two glues differ for a method
// the pak uses. The method is a descriptor in the type's dict. Read from the class it is itself,
// as a method descriptor is (2.3 Objects/descrobject.c, method_get); read from an instance it is
// bound to it (PyMethod_New, as funcobject.c func_descr_get does for a function). A call hands
// `self` and the arguments to H-uru's method descriptor, which checks `self`'s type and parses
// the arguments. Scripts cannot create one (no tp_new).

enum class Coerce
{
    kLong,  // `int` -> `long` (CWE PyLong_FromLong)
    kULong, // `int` -> unsigned `long` (CWE PyLong_FromUnsignedLong)
    kInt,   // `long` -> `int` (CWE PyInt_FromLong)
};

PyObject* ICoerce(PyObject* result, Coerce how)
{
    if (!result)
        return nullptr;
    PyObject* out = nullptr;
    switch (how) {
        case Coerce::kLong:
            if (PyInt_CheckExact(result))
                out = PyPlasma_LongObject_FromLong(PyInt_AS_LONG(result));
            break;
        case Coerce::kULong:
            if (PyInt_CheckExact(result))
                out = PyLong_FromUnsignedLong((unsigned long)PyInt_AS_LONG(result));
            break;
        case Coerce::kInt:
            if (PyObject_TypeCheck(result, &PyLong_Type)) {
                long v = PyLong_AsLong(result);
                if (v == -1 && PyErr_Occurred())
                    PyErr_Clear(); // out of int range: keep the long, as CWE's 2.3 PyInt would not
                else
                    out = PyInt_FromLong(v);
            }
            break;
    }
    if (!out)
        return result;
    Py_DECREF(result);
    return out;
}

struct pyCoercedMethod
{
    pyObjectRef fMethod; // H-uru's method descriptor
    const char* fName;   // the CWE name (a string literal)
    Coerce      fHow;
};

PYTHON_CLASS_DEFINITION(ptCoercedMethod, pyCoercedMethod);
PYTHON_DEFAULT_DEALLOC_DEFINITION(ptCoercedMethod)

PyObject* ptCoercedMethod_call(PyObject* self, PyObject* args, PyObject* keywords)
{
    pyCoercedMethod* method = reinterpret_cast<ptCoercedMethod*>(self)->fThis;
    return ICoerce(PyObject_Call(method->fMethod.Get(), args, keywords), method->fHow);
}

PyObject* ptCoercedMethod_descrGet(PyObject* self, PyObject* obj, PyObject* type)
{
    if (!obj) {
        Py_INCREF(self);
        return self;
    }
    return PyMethod_New(self, obj, type);
}

// The name a bound method's repr shows (2.3 Objects/classobject.c, instancemethod_repr).
PyObject* ptCoercedMethod_getName(PyObject* self, void*)
{
    return PyString_FromString(const_cast<char*>(reinterpret_cast<ptCoercedMethod*>(self)->fThis->fName));
}

// clang-format off
PYTHON_START_GETSET_TABLE(ptCoercedMethod)
    {const_cast<char*>("__name__"), ptCoercedMethod_getName, nullptr, nullptr, nullptr},
PYTHON_END_GETSET_TABLE;

PYTHON_TYPE_START(ptCoercedMethod)
    "Plasma.ptCoercedMethod",
    sizeof(ptCoercedMethod),                   /* tp_basicsize */
    0,                                         /* tp_itemsize */
    PYTHON_DEFAULT_DEALLOC(ptCoercedMethod),   /* tp_dealloc */
    0,                                         /* tp_print */
    0,                                         /* tp_getattr */
    0,                                         /* tp_setattr */
    0,                                         /* tp_compare */
    0,                                         /* tp_repr */
    0,                                         /* tp_as_number */
    0,                                         /* tp_as_sequence */
    0,                                         /* tp_as_mapping */
    0,                                         /* tp_hash */
    ptCoercedMethod_call,                      /* tp_call */
    0,                                         /* tp_str */
    0,                                         /* tp_getattro */
    0,                                         /* tp_setattro */
    0,                                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                        /* tp_flags */
    "An H-uru method with CWE's result type",  /* tp_doc */
    0,                                         /* tp_traverse */
    0,                                         /* tp_clear */
    0,                                         /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    0,                                         /* tp_iter */
    0,                                         /* tp_iternext */
    0,                                         /* tp_methods */
    0,                                         /* tp_members */
    PYTHON_DEFAULT_GETSET(ptCoercedMethod),    /* tp_getset */
    0,                                         /* tp_base */
    0,                                         /* tp_dict */
    ptCoercedMethod_descrGet,                  /* tp_descr_get */
PYTHON_TYPE_END;
// clang-format on

// A new ptCoercedMethod that calls `method` (a new reference, or nullptr with the error set).
PyObject* ICoercedMethodNew(const pyObjectRef& method, const char* name, Coerce how)
{
    if (PyType_Ready(&ptCoercedMethod_type) < 0)
        return nullptr;
    ptCoercedMethod* self = (ptCoercedMethod*)ptCoercedMethod_type.tp_alloc(&ptCoercedMethod_type, 0);
    if (self)
        self->fThis = new pyCoercedMethod{method, name, how};
    return (PyObject*)self;
}

// ---------------------------------------------------------------------------------------------
// Installation helpers. A type is found by name in the module that holds it (the overlay runs
// once, after the six builtin modules exist); Python 2.3 has no type attribute cache, so a
// descriptor added to tp_dict is seen by the type and its subclasses at once.

struct Installer
{
    plStatusLog* fLog;
    PyObject*    fPlasma; // borrowed: sys.modules['Plasma']
    int          fAdded = 0;
    int          fFailed = 0;

    void IFail(const ST::string& what)
    {
        fFailed++;
        if (fLog)
            fLog->AddLineF(plStatusLog::kRed, "Python: CWE overlay: {}", what);
        PyErr_Clear();
    }

    PyTypeObject* IType(const char* cls)
    {
        PyObject* t = PyDict_GetItemString(PyModule_GetDict(fPlasma), const_cast<char*>(cls));
        if (!t || !PyType_Check(t)) {
            IFail(ST::format("no class {}", cls));
            return nullptr;
        }
        return reinterpret_cast<PyTypeObject*>(t);
    }

    bool ISet(PyTypeObject* type, const char* name, PyObject* value)
    {
        if (!value || PyDict_SetItemString(type->tp_dict, const_cast<char*>(name), value) != 0) {
            IFail(ST::format("cannot set {}.{}", type->tp_name, name));
            return false;
        }
        fAdded++;
        return true;
    }

    // A second name for an H-uru method, the same descriptor (the CWE name's arguments and
    // result already match).
    void Alias(const char* cls, const char* cweName, const char* huruName)
    {
        PyTypeObject* type = IType(cls);
        if (!type)
            return;
        pyObjectRef d = PyObject_GetAttrString(reinterpret_cast<PyObject*>(type), const_cast<char*>(huruName));
        if (!d) {
            IFail(ST::format("no method {}.{}", cls, huruName));
            return;
        }
        ISet(type, cweName, d.Get());
    }

    // `cweName` on `cls` calls `huruName` (the same name: in place) and coerces its result.
    void Wrap(const char* cls, const char* cweName, const char* huruName, Coerce how)
    {
        PyTypeObject* type = IType(cls);
        if (!type)
            return;
        pyObjectRef orig = PyObject_GetAttrString(reinterpret_cast<PyObject*>(type), const_cast<char*>(huruName));
        if (!orig) {
            IFail(ST::format("no method {}.{}", cls, huruName));
            return;
        }
        pyObjectRef d = ICoercedMethodNew(orig, cweName, how);
        ISet(type, cweName, d.Get());
    }

    void AliasFunction(const char* cweName, const char* huruName)
    {
        PyObject* dict = PyModule_GetDict(fPlasma);
        PyObject* f = PyDict_GetItemString(dict, const_cast<char*>(huruName));
        if (!f || PyDict_SetItemString(dict, const_cast<char*>(cweName), f) != 0)
            IFail(ST::format("no function {}", huruName));
        else
            fAdded++;
    }

    // A second name for an object of another builtin module (e.g. an enum renamed by H-uru), the
    // same object.
    void AliasInModule(const char* module, const char* cweName, const char* huruName)
    {
        PyObject* m = PyDict_GetItemString(PyImport_GetModuleDict(), const_cast<char*>(module));
        PyObject* o = m ? PyDict_GetItemString(PyModule_GetDict(m), const_cast<char*>(huruName)) : nullptr;
        if (!o || PyDict_SetItemString(PyModule_GetDict(m), const_cast<char*>(cweName), o) != 0)
            IFail(ST::format("no {}.{}", module, huruName));
        else
            fAdded++;
    }

    // CWE methods with their own C++ body (a NULL-terminated table).
    void Methods(const char* cls, PyMethodDef* defs)
    {
        PyTypeObject* type = IType(cls);
        if (!type)
            return;
        for (PyMethodDef* d = defs; d->ml_name; d++) {
            pyObjectRef descr = PyDescr_NewMethod(type, d);
            ISet(type, d->ml_name, descr.Get());
        }
    }

    // Plasma module functions (added, or replacing H-uru's of the same name).
    PyObject* Original(const char* name)
    {
        PyObject* f = PyDict_GetItemString(PyModule_GetDict(fPlasma), const_cast<char*>(name));
        if (!f) {
            IFail(ST::format("no function {}", name));
            return nullptr;
        }
        Py_INCREF(f);
        return f;
    }

    void Functions(PyMethodDef* defs)
    {
        int n = 0;
        for (PyMethodDef* d = defs; d->ml_name; d++)
            n++;
        if (PyModule_AddFunctions(fPlasma, defs) != 0)
            IFail("cannot add the module functions");
        else
            fAdded += n;
    }
};

// ---------------------------------------------------------------------------------------------
// CWE's `unicode` getters: the *W twins of H-uru's text getters here and with
// ptGUIControlMultiLineEdit's other methods below, and the module functions CWE made `unicode`
// (Plasma module functions, below). Each gives the engine's text as CWE's PyUnicode_FromWideChar
// did, where H-uru's getter of the same text gives a cp1252 `str` (pyPy2Compat.h).

// ptGUIControlTextBox.getStringW: CWE pyGUIControlTextBoxGlue.cpp (getStringW).
PyObject* ITextBoxGetStringW(PyObject* self, PyObject*)
{
    pyGUIControlTextBox* box = pyGUIControlTextBox::ConvertFrom(self);
    if (!box)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(box->GetText());
}

PyMethodDef s_textBoxMethods[] = {
    {const_cast<char*>("getStringW"), ITextBoxGetStringW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ptGUIControlEditBox.getStringW: CWE pyGUIControlEditBoxGlue.cpp (getStringW).
PyObject* IEditBoxGetStringW(PyObject* self, PyObject*)
{
    pyGUIControlEditBox* box = pyGUIControlEditBox::ConvertFrom(self);
    if (!box)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(box->GetBuffer());
}

PyMethodDef s_editBoxMethods[] = {
    {const_cast<char*>("getStringW"), IEditBoxGetStringW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ptGUIControlListBox.getElementW(index): CWE pyGUIControlListBoxGlue.cpp (getElementW).
PyObject* IListBoxGetElementW(PyObject* self, PyObject* args)
{
    unsigned short index;
    if (!PyArg_ParseTuple(args, const_cast<char*>("h"), &index)) {
        PyErr_SetString(PyExc_TypeError, "getElementW expects an unsigned short");
        PYTHON_RETURN_ERROR;
    }
    pyGUIControlListBox* box = pyGUIControlListBox::ConvertFrom(self);
    if (!box)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(box->GetElement(index));
}

PyMethodDef s_listBoxMethods[] = {
    {const_cast<char*>("getElementW"), IListBoxGetElementW, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ptVaultFolderNode.getFolderNameW: CWE pyVaultFolderNodeGlue.cpp (getFolderNameW).
PyObject* IFolderGetFolderNameW(PyObject* self, PyObject*)
{
    pyVaultFolderNode* folder = pyVaultFolderNode::ConvertFrom(self);
    if (!folder)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(folder->Folder_GetName());
}

PyMethodDef s_folderNodeMethods[] = {
    {const_cast<char*>("getFolderNameW"), IFolderGetFolderNameW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ptVaultImageNode.getTitleW: CWE pyVaultImageNodeGlue.cpp (getTitleW).
PyObject* IImageGetTitleW(PyObject* self, PyObject*)
{
    pyVaultImageNode* image = pyVaultImageNode::ConvertFrom(self);
    if (!image)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(image->Image_GetTitle());
}

PyMethodDef s_imageNodeMethods[] = {
    {const_cast<char*>("getTitleW"), IImageGetTitleW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ptVaultTextNoteNode.getTitleW, getTextW: CWE pyVaultTextNoteNodeGlue.cpp (getTitleW, getTextW).
PyObject* INoteGetTitleW(PyObject* self, PyObject*)
{
    pyVaultTextNoteNode* note = pyVaultTextNoteNode::ConvertFrom(self);
    if (!note)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(note->Note_GetTitle());
}

PyObject* INoteGetTextW(PyObject* self, PyObject*)
{
    pyVaultTextNoteNode* note = pyVaultTextNoteNode::ConvertFrom(self);
    if (!note)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(note->Note_GetText());
}

PyMethodDef s_textNoteNodeMethods[] = {
    {const_cast<char*>("getTitleW"), INoteGetTitleW, METH_NOARGS, nullptr},
    {const_cast<char*>("getTextW"), INoteGetTextW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptGUIControlMultiLineEdit: the text as `unicode`, and the encoded buffer as CWE's bytes (str)
// and UInt16 (unicode) forms. CWE pyGUIControlMultiLineEditGlue.cpp and
// pyGUIControlMultiLineEdit.cpp (getStringW, setEncodedBuffer, setEncodedBufferW, getEncodedBuffer,
// getEncodedBufferW), pfGameGUIMgr/pfGUIMultiLineEditCtrl.cpp (SetBuffer, GetCodedBuffer,
// GetCodedBufferW; 8-bit form: each byte is a character, a character above 0xFF reads back as a
// space). The buffer's '\0' codes travel as 0xFE (0xFFFE).

PyObject* IMleGetStringW(PyObject* self, PyObject*)
{
    pyGUIControlMultiLineEdit* mle = pyGUIControlMultiLineEdit::ConvertFrom(self);
    if (!mle)
        return nullptr;
    return PyPlasma_UnicodeFromSTString(mle->GetText());
}

pfGUIMultiLineEditCtrl* IMultiLineEditCtrl(PyObject* self)
{
    pyGUIControlMultiLineEdit* mle = pyGUIControlMultiLineEdit::ConvertFrom(self);
    if (!mle)
        return nullptr;
    plKey key = mle->getObjKey();
    if (!key)
        return nullptr;
    return pfGUIMultiLineEditCtrl::ConvertNoRef(key->ObjectIsLoaded());
}

PyObject* IMleSetEncodedBuffer(PyObject* self, PyObject* args, bool wide)
{
    PyObject* bufferObj = nullptr;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O"), &bufferObj)) {
        PyErr_SetString(PyExc_TypeError, wide ? "setEncodedBufferW expects a python buffer object"
                                              : "setEncodedBuffer expects a python buffer object");
        PYTHON_RETURN_ERROR;
    }
    if (pfGUIMultiLineEditCtrl* ctrl = IMultiLineEditCtrl(self)) {
        const void* data = nullptr;
        int         length = 0;
        if (PyObject_AsReadBuffer(bufferObj, &data, &length) != 0 || !data) {
            PyErr_Clear(); // CWE skips a non-buffer and returns None
            PYTHON_RETURN_NONE;
        }
        std::vector<wchar_t> text;
        if (wide) {
            // CWE reads UInt16 units (Windows wchar_t, UTF-16); the engine's wchar_t is UTF-32,
            // so a surrogate pair becomes its one character.
            const uint16_t* units = static_cast<const uint16_t*>(data);
            size_t          count = (size_t)length / sizeof(uint16_t);
            text.reserve(count);
            for (size_t i = 0; i < count; i++) {
                uint32_t c = units[i] == 0xFFFE ? 0 : units[i];
                if (c >= 0xD800 && c < 0xDC00 && i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (units[i + 1] - 0xDC00);
                    i++;
                }
                text.push_back((wchar_t)c);
            }
        } else {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            text.reserve(length);
            for (int i = 0; i < length; i++)
                text.push_back(bytes[i] == 254 ? 0 : (wchar_t)bytes[i]);
        }
        ctrl->SetBuffer(text.data(), text.size());
        ctrl->SetCursorToLoc(0);
        ctrl->SetScrollPosition(0);
    }
    PYTHON_RETURN_NONE;
}

PyObject* IMleSetEncodedBufferA(PyObject* self, PyObject* args) { return IMleSetEncodedBuffer(self, args, false); }
PyObject* IMleSetEncodedBufferW(PyObject* self, PyObject* args) { return IMleSetEncodedBuffer(self, args, true); }

PyObject* IMleGetEncodedBuffer(PyObject* self, PyObject*)
{
    std::string out;
    if (pfGUIMultiLineEditCtrl* ctrl = IMultiLineEditCtrl(self)) {
        ST::wchar_buffer buffer = ctrl->GetCodedBuffer();
        out.reserve(buffer.size());
        for (size_t i = 0; i < buffer.size(); i++) {
            wchar_t c = buffer[i];
            if (c == 0)
                out += (char)254;
            else if ((uint32_t)c > 0xFF)
                out += ' ';
            else
                out += (char)(unsigned char)c;
        }
    }
    return PyString_FromStringAndSize(out.data(), (int)out.size());
}

PyObject* IMleGetEncodedBufferW(PyObject* self, PyObject*)
{
    std::u16string out;
    if (pfGUIMultiLineEditCtrl* ctrl = IMultiLineEditCtrl(self)) {
        ST::wchar_buffer buffer = ctrl->GetCodedBuffer();
        out.reserve(buffer.size());
        for (size_t i = 0; i < buffer.size(); i++) {
            uint32_t c = (uint32_t)buffer[i];
            if (c == 0) {
                out += (char16_t)0xFFFE;
            } else if (c >= 0x10000) {
                c -= 0x10000;
                out += (char16_t)(0xD800 + (c >> 10));
                out += (char16_t)(0xDC00 + (c & 0x3FF));
            } else {
                out += (char16_t)c;
            }
        }
    }
    return PyUnicode_FromUnicode(reinterpret_cast<const Py_UNICODE*>(out.data()), (int)out.size());
}

PyMethodDef s_mleMethods[] = {
    {const_cast<char*>("getStringW"), IMleGetStringW, METH_NOARGS, nullptr},
    {const_cast<char*>("setEncodedBuffer"), IMleSetEncodedBufferA, METH_VARARGS, nullptr},
    {const_cast<char*>("setEncodedBufferW"), IMleSetEncodedBufferW, METH_VARARGS, nullptr},
    {const_cast<char*>("getEncodedBuffer"), IMleGetEncodedBuffer, METH_NOARGS, nullptr},
    {const_cast<char*>("getEncodedBufferW"), IMleGetEncodedBufferW, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptAudioControl: the device options of CWE's Options menu, on H-uru's playback device list.
// CWE pyAudioControlGlue.cpp and pyAudioControl.cpp (supportsEAX, getNumAudioDevices,
// getAudioDeviceName, setDeviceName, getDeviceName), plAudio/plAudioSystem.cpp
// (plAudioSystem::GetNumAudioDevices, GetAudioDeviceName, SupportsEAX).

PyObject* IAudioGetNumAudioDevices(PyObject* self, PyObject*)
{
    pyAudioControl* audio = pyAudioControl::ConvertFrom(self);
    if (!audio)
        return nullptr;
    return PyInt_FromLong((long)audio->GetPlaybackDevices().size());
}

PyObject* IAudioGetAudioDeviceName(PyObject* self, PyObject* args)
{
    int index;
    if (!PyArg_ParseTuple(args, const_cast<char*>("i"), &index)) {
        PyErr_SetString(PyExc_TypeError, "getAudioDeviceName expects an int");
        PYTHON_RETURN_ERROR;
    }
    pyAudioControl* audio = pyAudioControl::ConvertFrom(self);
    if (!audio)
        return nullptr;
    std::vector<ST::string> devices = audio->GetPlaybackDevices();
    if (index < 0 || (size_t)index >= devices.size()) {
        // CWE asserts and hands NULL to PyString_FromString (plAudioSystem::GetAudioDeviceName)
        PyErr_SetString(PyExc_IndexError, "Invalid index passed to getAudioDeviceName");
        PYTHON_RETURN_ERROR;
    }
    return PyUnicode_FromSTString(devices[index]);
}

PyObject* IAudioSetDeviceName(PyObject* self, PyObject* args)
{
    ST::string device;
    int        restart;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&i"), PyUnicode_STStringConverter, &device, &restart)) {
        PyErr_SetString(PyExc_TypeError, "setDeviceName expects a string and a bool");
        PYTHON_RETURN_ERROR;
    }
    pyAudioControl* audio = pyAudioControl::ConvertFrom(self);
    if (!audio)
        return nullptr;
    audio->SetPlaybackDevice(device, restart != 0);
    PYTHON_RETURN_NONE;
}

PyObject* IAudioGetDeviceName(PyObject* self, PyObject*)
{
    pyAudioControl* audio = pyAudioControl::ConvertFrom(self);
    if (!audio)
        return nullptr;
    return PyUnicode_FromSTString(audio->GetPlaybackDevice());
}

PyObject* IAudioSupportsEAX(PyObject* self, PyObject* args)
{
    ST::string device;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &device)) {
        PyErr_SetString(PyExc_TypeError, "supportsEAX expects a string");
        PYTHON_RETURN_ERROR;
    }
    pyAudioControl* audio = pyAudioControl::ConvertFrom(self);
    if (!audio)
        return nullptr;
    // CWE: the named device's EAX flag, false for a name not in the list. H-uru keeps one flag
    // for the audio system (plgAudioSys::IsEAXSupported).
    bool known = false;
    for (const ST::string& d : audio->GetPlaybackDevices())
        known = known || d == device;
    PYTHON_RETURN_BOOL(known && audio->IsEAXSupported());
}

PyMethodDef s_audioMethods[] = {
    {const_cast<char*>("getNumAudioDevices"), IAudioGetNumAudioDevices, METH_NOARGS, nullptr},
    {const_cast<char*>("getAudioDeviceName"), IAudioGetAudioDeviceName, METH_VARARGS, nullptr},
    {const_cast<char*>("setDeviceName"), IAudioSetDeviceName, METH_VARARGS, nullptr},
    {const_cast<char*>("getDeviceName"), IAudioGetDeviceName, METH_NOARGS, nullptr},
    {const_cast<char*>("supportsEAX"), IAudioSupportsEAX, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptMarkerMgr.addMarker(x, y, z, id, justCreated): CWE pyMarkerMgrGlue.cpp (addMarker),
// pyMarkerMgr.cpp (pyMarkerMgr::AddMarker). H-uru takes a ptPoint3.

PyObject* IMarkerAddMarker(PyObject* self, PyObject* args)
{
    double        x, y, z;
    long          id;
    unsigned char justCreated;
    if (!PyArg_ParseTuple(args, const_cast<char*>("dddlb"), &x, &y, &z, &id, &justCreated)) {
        PyErr_SetString(PyExc_TypeError, "addMarker expects three doubles, an unsigned long, and a bool");
        PYTHON_RETURN_ERROR;
    }
    pyMarkerMgr* mgr = pyMarkerMgr::ConvertFrom(self);
    if (!mgr)
        return nullptr;
    pyObjectRef pos = pyPoint3::New(hsPoint3((float)x, (float)y, (float)z));
    if (!pos)
        return nullptr;
    mgr->AddMarker(pyPoint3::ConvertFrom(pos.Get()), (uint32_t)id, justCreated != 0);
    PYTHON_RETURN_NONE;
}

PyMethodDef s_markerMethods[] = {
    {const_cast<char*>("addMarker"), IMarkerAddMarker, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptVaultPlayerNode: CWE's player-vault accessors (pyVaultPlayerNodeGlue.cpp and
// pyVaultPlayerNode.cpp, getInbox through isExplorer). The folder getters read the local
// player's vault, not `self`, and look for a Folder node of that type (CWE pyVaultPlayerNode.cpp,
// GetPlayerVaultFolder).

template <unsigned FolderType>
PyObject* IPlayerVaultFolder(PyObject*, PyObject*)
{
    if (hsRef<RelVaultNode> rvnPlr = VaultGetPlayerNode()) {
        if (hsRef<RelVaultNode> rvnFldr = rvnPlr->GetChildFolderNode(FolderType, 1))
            return pyVaultFolderNode::New(rvnFldr);
    }
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerGetPlayerInfo(PyObject*, PyObject*)
{
    if (hsRef<RelVaultNode> rvn = VaultGetPlayerInfoNode())
        return pyVaultPlayerInfoNode::New(rvn);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerGetLinkToMyNeighborhood(PyObject*, PyObject*)
{
    plAgeLinkStruct link;
    if (VaultGetLinkToMyNeighborhood(&link))
        return pyAgeLinkStruct::New(&link);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerGetLinkToCity(PyObject*, PyObject*)
{
    plAgeLinkStruct link;
    if (VaultGetLinkToCity(&link))
        return pyAgeLinkStruct::New(&link);
    PYTHON_RETURN_NONE;
}

pyAgeInfoStruct* IAgeInfoArg(PyObject* args, const char* message)
{
    PyObject* infoObj = nullptr;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O"), &infoObj) || !pyAgeInfoStruct::Check(infoObj)) {
        PyErr_SetString(PyExc_TypeError, message);
        return nullptr;
    }
    return pyAgeInfoStruct::ConvertFrom(infoObj);
}

PyObject* IPlayerGetOwnedAgeLink(PyObject*, PyObject* args)
{
    pyAgeInfoStruct* info = IAgeInfoArg(args, "getOwnedAgeLink expects a ptAgeInfoStruct");
    if (!info)
        PYTHON_RETURN_ERROR;
    plAgeLinkStruct link;
    if (VaultGetOwnedAgeLink(info->GetAgeInfo(), &link))
        return pyAgeLinkStruct::New(&link);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerGetVisitAgeLink(PyObject*, PyObject* args)
{
    pyAgeInfoStruct* info = IAgeInfoArg(args, "getVisitAgeLink expects a ptAgeInfoStruct");
    if (!info)
        PYTHON_RETURN_ERROR;
    if (hsRef<RelVaultNode> rvn = VaultGetVisitAgeLink(info->GetAgeInfo()))
        return pyVaultAgeLinkNode::New(rvn);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerRemoveOwnedAgeLink(PyObject*, PyObject* args)
{
    ST::string ageFilename;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &ageFilename)) {
        PyErr_SetString(PyExc_TypeError, "removeOwnedAgeLink expects a string");
        PYTHON_RETURN_ERROR;
    }
    plAgeInfoStruct info;
    info.SetAgeFilename(ageFilename);
    VaultUnregisterOwnedAge(&info);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerRemoveVisitAgeLink(PyObject*, PyObject* args)
{
    ST::string guid;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &guid)) {
        PyErr_SetString(PyExc_TypeError, "removeVisitAgeLink expects a string");
        PYTHON_RETURN_ERROR;
    }
    plUUID          uuid(guid);
    plAgeInfoStruct info;
    info.SetAgeInstanceGuid(&uuid);
    // CWE unregisters an *owned* age here too (pyVaultPlayerNode::RemoveVisitAgeLink)
    VaultUnregisterOwnedAge(&info);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerFindChronicleEntry(PyObject*, PyObject* args)
{
    ST::string entryName;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &entryName)) {
        PyErr_SetString(PyExc_TypeError, "findChronicleEntry expects a string");
        PYTHON_RETURN_ERROR;
    }
    if (hsRef<RelVaultNode> rvn = VaultFindChronicleEntry(entryName))
        return pyVaultChronicleNode::New(rvn);
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerSetExplorer(PyObject* self, PyObject* args)
{
    unsigned char explorer;
    if (!PyArg_ParseTuple(args, const_cast<char*>("b"), &explorer)) {
        PyErr_SetString(PyExc_TypeError, "setExplorer expects a boolean");
        PYTHON_RETURN_ERROR;
    }
    pyVaultPlayerNode* node = pyVaultPlayerNode::ConvertFrom(self);
    if (!node)
        return nullptr;
    if (hsRef<RelVaultNode> rvn = node->GetNode()) {
        VaultPlayerNode player(rvn);
        player.SetExplorer(explorer != 0);
    }
    PYTHON_RETURN_NONE;
}

PyObject* IPlayerIsExplorer(PyObject* self, PyObject*)
{
    pyVaultPlayerNode* node = pyVaultPlayerNode::ConvertFrom(self);
    if (!node)
        return nullptr;
    hsRef<RelVaultNode> rvn = node->GetNode();
    if (!rvn)
        PYTHON_RETURN_BOOL(false);
    VaultPlayerNode player(rvn);
    PYTHON_RETURN_BOOL(player.GetExplorer() != 0);
}

PyMethodDef s_playerNodeMethods[] = {
    {const_cast<char*>("getInbox"), IPlayerVaultFolder<plVault::kInboxFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getAvatarOutfitFolder"), IPlayerVaultFolder<plVault::kAvatarOutfitFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getAvatarClosetFolder"), IPlayerVaultFolder<plVault::kAvatarClosetFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getChronicleFolder"), IPlayerVaultFolder<plVault::kChronicleFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getAgeJournalsFolder"), IPlayerVaultFolder<plVault::kAgeJournalsFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getIgnoreListFolder"), IPlayerVaultFolder<plVault::kIgnoreListFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getBuddyListFolder"), IPlayerVaultFolder<plVault::kBuddyListFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getPeopleIKnowAboutFolder"), IPlayerVaultFolder<plVault::kPeopleIKnowAboutFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getAgesICanVisitFolder"), IPlayerVaultFolder<plVault::kAgesICanVisitFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getAgesIOwnFolder"), IPlayerVaultFolder<plVault::kAgesIOwnFolder>, METH_NOARGS, nullptr},
    {const_cast<char*>("getPlayerInfo"), IPlayerGetPlayerInfo, METH_NOARGS, nullptr},
    {const_cast<char*>("getLinkToMyNeighborhood"), IPlayerGetLinkToMyNeighborhood, METH_NOARGS, nullptr},
    {const_cast<char*>("getLinkToCity"), IPlayerGetLinkToCity, METH_NOARGS, nullptr},
    {const_cast<char*>("getOwnedAgeLink"), IPlayerGetOwnedAgeLink, METH_VARARGS, nullptr},
    {const_cast<char*>("removeOwnedAgeLink"), IPlayerRemoveOwnedAgeLink, METH_VARARGS, nullptr},
    {const_cast<char*>("getVisitAgeLink"), IPlayerGetVisitAgeLink, METH_VARARGS, nullptr},
    {const_cast<char*>("removeVisitAgeLink"), IPlayerRemoveVisitAgeLink, METH_VARARGS, nullptr},
    {const_cast<char*>("findChronicleEntry"), IPlayerFindChronicleEntry, METH_VARARGS, nullptr},
    {const_cast<char*>("setExplorer"), IPlayerSetExplorer, METH_VARARGS, nullptr},
    {const_cast<char*>("isExplorer"), IPlayerIsExplorer, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptAgeVault.getPublicAgesFolder: CWE pyAgeVault::GetPublicAgesFolder (pyAgeVault.cpp) asks
// VaultGetAgePublicAgesFolderIncRef, which returns nil ("eric, implement me",
// plVault/plVaultClientApi.cpp): always None.

PyObject* IAgeVaultGetPublicAgesFolder(PyObject*, PyObject*)
{
    PYTHON_RETURN_NONE;
}

PyMethodDef s_ageVaultMethods[] = {
    {const_cast<char*>("getPublicAgesFolder"), IAgeVaultGetPublicAgesFolder, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// ptVault.sendToDevice(node, deviceName): CWE pyVaultGlue.cpp (sendToDevice), pyVault.cpp
// (pyVault::SendToDevice), plVault/plVaultClientApi.cpp (VaultPublishNode): into the device's
// inbox; a missing inbox is first set to the default one. CWE waits for that set; H-uru's is
// asynchronous, so the node is added from its callback.

PyObject* IVaultSendToDevice(PyObject*, PyObject* args)
{
    PyObject*  nodeObj = nullptr;
    ST::string deviceName;
    if (!PyArg_ParseTuple(args, const_cast<char*>("OO&"), &nodeObj, PyUnicode_STStringConverter, &deviceName) || !pyVaultNode::Check(nodeObj)) {
        PyErr_SetString(PyExc_TypeError, "sendToDevice expects a ptVaultNode and a string");
        PYTHON_RETURN_ERROR;
    }
    hsRef<RelVaultNode> node = pyVaultNode::ConvertFrom(nodeObj)->GetNode();
    if (!node)
        PYTHON_RETURN_NONE;
    unsigned nodeId = node->GetNodeId();
    if (hsRef<RelVaultNode> inbox = VaultAgeGetDeviceInbox(deviceName)) {
        VaultAddChildNode(inbox->GetNodeId(), nodeId, VaultGetPlayerId(), nullptr);
    } else {
        VaultAgeSetDeviceInbox(deviceName, DEFAULT_DEVICE_INBOX, [nodeId](ENetError result, hsRef<RelVaultNode> newInbox) {
            if (IS_NET_SUCCESS(result) && newInbox)
                VaultAddChildNode(newInbox->GetNodeId(), nodeId, VaultGetPlayerId(), nullptr);
        });
    }
    PYTHON_RETURN_NONE;
}

PyMethodDef s_vaultMethods[] = {
    {const_cast<char*>("sendToDevice"), IVaultSendToDevice, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------
// Plasma module functions.

// PtIsSubscriptionActive: CWE cyAccountManagementGlue.cpp, cyAccountManagement::IsSubscriptionActive.
PyObject* IPtIsSubscriptionActive(PyObject*, PyObject*)
{
    const NetCommAccount* account = NetCommGetAccount();
    PYTHON_RETURN_BOOL((account->billingType & kBillingTypePaidSubscriber) != 0);
}

// PtUpgradeVisitorToExplorer(playerInt): CWE cyAccountManagementGlue.cpp,
// cyAccountManagement::UpgradeVisitorToExplorer.
PyObject* IPtUpgradeVisitorToExplorer(PyObject*, PyObject* args)
{
    unsigned int playerInt = 0;
    if (!PyArg_ParseTuple(args, const_cast<char*>("I"), &playerInt)) {
        PyErr_SetString(PyExc_TypeError, "PtUpgradeVisitorToExplorer expects a unsigned int");
        PYTHON_RETURN_ERROR;
    }
    NetCommUpgradeVisitorToExplorer(playerInt, nullptr);
    PYTHON_RETURN_NONE;
}

// PtUsingUnicode: CWE cyMiscGlue3.cpp, cyMisc::UsingUnicode; plLocalization::fUsesUnicode
// (plLocalization.cpp: true for Japanese only, the one CWE language with it set).
PyObject* IPtUsingUnicode(PyObject*, PyObject*)
{
    PYTHON_RETURN_BOOL(plLocalization::GetLanguage() == plLocalization::kJapanese);
}

// PtGetAgeTime: CWE cyMiscGlue.cpp returns PyLong_FromUnsignedLong(VaultAgeGetAgeTime()),
// which is 0 ("eric, implement me", plVault/plVaultClientApi.cpp).
PyObject* IPtGetAgeTime(PyObject*, PyObject*)
{
    return PyLong_FromUnsignedLong(0);
}

// PtGetLocalizedString(name, arguments=None): CWE cyMiscGlue.cpp (PtGetLocalizedString) returns
// the string as `unicode`. The arguments are read as H-uru's function of the same name reads them
// (cyMiscGlue.cpp): a `str` or `unicode` name, as CWE, which H-uru also refuses when it is empty,
// and a list whose None items are blank and whose non-string items read INVALID ARG, as in both.
PyObject* IPtGetLocalizedString(PyObject*, PyObject* args)
{
    static const char kMessage[] = "PtGetLocalizedString expects a string and a list of strings";
    ST::string        name;
    PyObject*         argObj = nullptr;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&|O"), PyUnicode_STStringConverter, &name, &argObj) ||
        name.empty()) {
        PyErr_SetString(PyExc_TypeError, kMessage);
        PYTHON_RETURN_ERROR;
    }
    std::vector<ST::string> argList;
    if (argObj) {
        if (!PyList_Check(argObj)) {
            PyErr_SetString(PyExc_TypeError, kMessage);
            PYTHON_RETURN_ERROR;
        }
        Py_ssize_t len = PyList_Size(argObj);
        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject* item = PyList_GetItem(argObj, i);
            if (item == Py_None)
                argList.emplace_back();
            else if (PyUnicode_Check(item))
                argList.emplace_back(PyUnicode_AsSTString(item));
            else
                argList.emplace_back(ST_LITERAL("INVALID ARG"));
        }
    }
    return PyPlasma_UnicodeFromSTString(cyMisc::GetLocalizedString(name, argList));
}

// PtGetUserPath, PtGetInitPath: CWE cyMiscGlue3.cpp; PtGetAccountName: CWE
// cyAccountManagementGlue.cpp. CWE returned each as `unicode`.
PyObject* IPtGetUserPath(PyObject*, PyObject*)
{
    return PyPlasma_UnicodeFromSTString(cyMisc::GetUserPath().AsString());
}

PyObject* IPtGetInitPath(PyObject*, PyObject*)
{
    return PyPlasma_UnicodeFromSTString(cyMisc::GetInitPath().AsString());
}

PyObject* IPtGetAccountName(PyObject*, PyObject*)
{
    return PyPlasma_UnicodeFromSTString(cyAccountManagement::GetAccountName());
}

// PtPrintToScreen(message): CWE cyMiscGlue3.cpp (built in every CWE client, `#if 1`),
// cyMisc::PrintToScreen: a 32-line on-screen status log, created and shown at the first call.
PyObject* IPtPrintToScreen(PyObject*, PyObject* args)
{
    ST::string message;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &message)) {
        PyErr_SetString(PyExc_TypeError, "PtPrintToScreen expects a string");
        PYTHON_RETURN_ERROR;
    }
    static plStatusLog* gStatusLog = nullptr;
    if (!gStatusLog) {
        gStatusLog = plStatusLogMgr::GetInstance().CreateStatusLog(32, plFileName(),
                                                                   plStatusLog::kDontWriteFile | plStatusLog::kDeleteForMe | plStatusLog::kFilledBackground);
        plStatusLogMgr::GetInstance().ToggleStatusLog(gStatusLog);
    }
    gStatusLog->AddLine(plStatusLog::kBlue, message);
    PYTHON_RETURN_NONE;
}

// PtPageInNode(nodeName, ageName=None): CWE cyMiscGlue2.cpp, cyMisc::PageInNodes. H-uru
// inserted netForce before the age name and raises on an unknown page; CWE adds whatever
// location the finder returns (plClient skips an invalid one) and does nothing without a ResMgr.
PyObject* IPtPageInNode(PyObject*, PyObject* args)
{
    static const char kMessage[] = "PtPageInNode expects a string or list of strings, and optionally a string";
    PyObject*         nodeNameObj = nullptr;
    ST::string        ageName;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O|O&"), &nodeNameObj, PyUnicode_STStringConverter, &ageName)) {
        PyErr_SetString(PyExc_TypeError, kMessage);
        PYTHON_RETURN_ERROR;
    }
    bool                    haveAge = PyTuple_GET_SIZE(args) > 1;
    std::vector<ST::string> nodeNames;
    if (PyString_Check(nodeNameObj)) {
        nodeNames.emplace_back(PyUnicode_AsSTString(nodeNameObj));
    } else if (PyList_Check(nodeNameObj)) {
        int num = PyList_Size(nodeNameObj);
        for (int i = 0; i < num; i++) {
            PyObject* listItem = PyList_GetItem(nodeNameObj, i);
            if (!PyString_Check(listItem)) {
                PyErr_SetString(PyExc_TypeError, kMessage);
                PYTHON_RETURN_ERROR;
            }
            nodeNames.emplace_back(PyUnicode_AsSTString(listItem));
        }
    } else {
        PyErr_SetString(PyExc_TypeError, kMessage);
        PYTHON_RETURN_ERROR;
    }

    if (hsgResMgr::ResMgr()) {
        plSynchEnabler ps(false); // disable dirty tracking while paging in
        plClientMsg*   msg = new plClientMsg(plClientMsg::kLoadRoom);
        plKey          clientKey = hsgResMgr::ResMgr()->FindKey(kClient_KEY);
        msg->AddReceiver(clientKey);
        const ST::string& age = haveAge ? ageName : NetCommGetAge()->ageDatasetName;
        for (const ST::string& nodeName : nodeNames)
            msg->AddRoomLoc(plKeyFinder::Instance().FindLocation(age, nodeName));
        msg->Send();
    }
    PYTHON_RETURN_NONE;
}

// PtPageOutNode(nodeName): CWE cyMiscGlue2.cpp, cyMisc::PageOutNode. H-uru added netForce
// and raises ValueError on an unknown page (cyMiscGlue2.cpp PtPageOutNode); CWE sends kUnloadRoom
// with whatever location the finder returns, and CWE plClient::IUnloadRooms (plClient.cpp) skips
// an invalid one, so an unknown page is a silent no-op. Nothing without a ResMgr.
PyObject* IPtPageOutNode(PyObject*, PyObject* args)
{
    ST::string nodeName;
    if (!PyArg_ParseTuple(args, const_cast<char*>("O&"), PyUnicode_STStringConverter, &nodeName)) {
        PyErr_SetString(PyExc_TypeError, "PtPageOutNode expects a string");
        PYTHON_RETURN_ERROR;
    }
    if (hsgResMgr::ResMgr()) {
        plSynchEnabler ps(false); // disable dirty tracking while paging out
        plClientMsg*   msg = new plClientMsg(plClientMsg::kUnloadRoom);
        plKey          clientKey = hsgResMgr::ResMgr()->FindKey(kClient_KEY);
        msg->AddReceiver(clientKey);
        msg->AddRoomLoc(plKeyFinder::Instance().FindLocation({}, nodeName));
        msg->Send();
    }
    PYTHON_RETURN_NONE;
}

// PtYesNoDialog(selfkey, dialogMessage): CWE cyMiscGlue2.cpp, cyMisc::YesNoDialog. CWE asks the
// KI: a pfKIMsg kYesNoDialog carrying the text and the sender's key, broadcast to the KI
// message's receivers. MOULa's xKI shows its own KIYesNo dialog for it and answers the sender
// with a "YesNo" notify (xKI.py: OnKIMsg's kYesNoDialog branch, OnGUINotify's KIYesNo answer).
// H-uru's PtYesNoDialog (cyMiscGlue2.cpp) posts a plConfirmationMsg to pfConfirmationMgr instead,
// which installs its own proc on KIYesNo (pfConfirmationMgr.cpp ILoadDialog, MsgReceive). MOULa's
// xKI still shows KIYesNo itself for the quit (xKI.py, OnGUINotify's exit button); that proc's
// OnShow, with nothing pending, hides the dialog inside pfGameGUIMgr::ShowDialog's
// SetEnabled(true), and ShowDialog then links the hidden dialog into the active list: a modal
// dialog that is never drawn.
PyObject* IPtYesNoDialog(PyObject*, PyObject* args)
{
    static const char kMessage[] = "PtYesNoDialog expects a ptKey and a string or unicode string";
    PyObject*         keyObj = nullptr;
    PyObject*         textObj = nullptr;
    if (!PyArg_ParseTuple(args, const_cast<char*>("OO"), &keyObj, &textObj) || !pyKey::Check(keyObj)) {
        PyErr_SetString(PyExc_TypeError, kMessage);
        PYTHON_RETURN_ERROR;
    }
    ST::string text;
    if (!PyUnicode_STStringConverter(textObj, &text)) { // `str` (cp1252) or `unicode`, as CWE
        PyErr_SetString(PyExc_TypeError, kMessage);
        PYTHON_RETURN_ERROR;
    }
    pfKIMsg* msg = new pfKIMsg(pfKIMsg::kYesNoDialog);
    msg->SetSender(pyKey::ConvertFrom(keyObj)->getKey());
    msg->SetString(text);
    plgDispatch::MsgSend(msg);
    PYTHON_RETURN_NONE;
}

// CWE's optional callback argument, ignored by CWE itself: PtGetPublicAgeList(ageName, cbObject=None)
// and PtRemovePublicAge(guid, cbObject=None) take "s|O", PtCreatePublicAge(ageInfo, cbObject=None)
// "O|O" (CWE cyMiscGlue4.cpp; cyMisc.cpp passes cbObject on unused). PtDumpLogs(folder) parses
// "O|O" with one target (CWE cyMiscGlue.cpp): a second argument is accepted and ignored. Each
// calls H-uru's function with the first argument.
PyObject* s_origGetPublicAgeList = nullptr;
PyObject* s_origCreatePublicAge = nullptr;
PyObject* s_origRemovePublicAge = nullptr;
PyObject* s_origDumpLogs = nullptr;

PyObject* ICallFirstArg(PyObject* orig, PyObject* args, const char* message)
{
    Py_ssize_t n = PyTuple_GET_SIZE(args);
    if (n < 1 || n > 2) {
        PyErr_SetString(PyExc_TypeError, message);
        PYTHON_RETURN_ERROR;
    }
    pyObjectRef first = PyTuple_GetSlice(args, 0, 1);
    if (!first)
        return nullptr;
    return PyObject_Call(orig, first.Get(), nullptr);
}

PyObject* IPtGetPublicAgeList(PyObject*, PyObject* args)
{
    return ICallFirstArg(s_origGetPublicAgeList, args,
                         "PtGetPublicAgeList expects a string and an optional object with a gotPublicAgeList() method");
}

PyObject* IPtCreatePublicAge(PyObject*, PyObject* args)
{
    return ICallFirstArg(s_origCreatePublicAge, args,
                         "PtCreatePublicAge expects a ptAgeInfoStruct object and an optional object with a publicAgeCreated() method");
}

PyObject* IPtRemovePublicAge(PyObject*, PyObject* args)
{
    return ICallFirstArg(s_origRemovePublicAge, args,
                         "PtRemovePublicAge expects a string and an optional object with a publicAgeRemoved() method");
}

PyObject* IPtDumpLogs(PyObject*, PyObject* args)
{
    return ICallFirstArg(s_origDumpLogs, args, "PtDumpLogs expects a unicode or standard string");
}

PyMethodDef s_functions[] = {
    {const_cast<char*>("PtIsSubscriptionActive"), IPtIsSubscriptionActive, METH_NOARGS, nullptr},
    {const_cast<char*>("PtUpgradeVisitorToExplorer"), IPtUpgradeVisitorToExplorer, METH_VARARGS, nullptr},
    {const_cast<char*>("PtUsingUnicode"), IPtUsingUnicode, METH_NOARGS, nullptr},
    {const_cast<char*>("PtGetAgeTime"), IPtGetAgeTime, METH_NOARGS, nullptr},
    {const_cast<char*>("PtGetLocalizedString"), IPtGetLocalizedString, METH_VARARGS, nullptr},
    {const_cast<char*>("PtGetUserPath"), IPtGetUserPath, METH_NOARGS, nullptr},
    {const_cast<char*>("PtGetInitPath"), IPtGetInitPath, METH_NOARGS, nullptr},
    {const_cast<char*>("PtGetAccountName"), IPtGetAccountName, METH_NOARGS, nullptr},
    {const_cast<char*>("PtPrintToScreen"), IPtPrintToScreen, METH_VARARGS, nullptr},
    {const_cast<char*>("PtPageInNode"), IPtPageInNode, METH_VARARGS, nullptr},
    {const_cast<char*>("PtPageOutNode"), IPtPageOutNode, METH_VARARGS, nullptr},
    {const_cast<char*>("PtGetPublicAgeList"), IPtGetPublicAgeList, METH_VARARGS, nullptr},
    {const_cast<char*>("PtCreatePublicAge"), IPtCreatePublicAge, METH_VARARGS, nullptr},
    {const_cast<char*>("PtRemovePublicAge"), IPtRemovePublicAge, METH_VARARGS, nullptr},
    {const_cast<char*>("PtDumpLogs"), IPtDumpLogs, METH_VARARGS, nullptr},
    {const_cast<char*>("PtYesNoDialog"), IPtYesNoDialog, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

} // namespace

// ---------------------------------------------------------------------------------------------

void plPy2CWE::InstallOverlay(plStatusLog* dbgLog)
{
    PyObject* plasma = PyDict_GetItemString(PyImport_GetModuleDict(), const_cast<char*>("Plasma"));
    if (!plasma) {
        if (dbgLog)
            dbgLog->AddLine(plStatusLog::kRed, "Python: CWE overlay: no Plasma module");
        return;
    }
    Installer in{dbgLog, plasma};

    // H-uru's own registrations stay in place underneath: initPython has already compiled and
    // registered H-uru's surface, and the overlay overwrites the names whose CWE form differs
    // (ptGameScore, ptGameCli, PtYesNoDialog, PtPageInNode, PtPageOutNode, the public-age functions
    // and PtDumpLogs, which forward to H-uru's originals, PtGetLocalizedString, PtGetUserPath,
    // PtGetInitPath and PtGetAccountName, which return `unicode`, and the same-name wraps below).
    // Those H-uru registrations are replaced on purpose, not dead by mistake. H-uru's names that CWE
    // never had (the ptGm* game classes and the rest) stay visible to the scripts, which were
    // written against CWE and so never name them.

    // Wide-string twins: the same engine call, `unicode` out. The setters and inserters are
    // H-uru's methods, whose converters take a `unicode` too (pyPy2Compat.h); the getters have bodies
    // of their own (ptGUIControlMultiLineEdit's getStringW with its other CWE methods, below). CWE
    // pyGUIControlTextBoxGlue.cpp, pyGUIControlEditBoxGlue.cpp, pyGUIControlMultiLineEditGlue.cpp,
    // pyGUIControlListBoxGlue.cpp, pyGUIPopUpMenuGlue.cpp, pyDynamicTextGlue.cpp,
    // pyVaultFolderNodeGlue.cpp, pyVaultImageNodeGlue.cpp, pyVaultTextNoteNodeGlue.cpp (the *W
    // entries of each method table).
    in.Alias("ptGUIControlTextBox", "setStringW", "setString");             // CWE pyGUIControlTextBoxGlue.cpp
    in.Methods("ptGUIControlTextBox", s_textBoxMethods);                    // getStringW
    in.Alias("ptGUIControlEditBox", "setStringW", "setString");             // CWE pyGUIControlEditBoxGlue.cpp
    in.Methods("ptGUIControlEditBox", s_editBoxMethods);                    // getStringW
    in.Alias("ptGUIControlMultiLineEdit", "setStringW", "setString");       // CWE pyGUIControlMultiLineEditGlue.cpp
    in.Alias("ptGUIControlMultiLineEdit", "insertStringW", "insertString"); // CWE pyGUIControlMultiLineEditGlue.cpp
    in.Alias("ptGUIControlMultiLineEdit", "insertCharW", "insertChar");     // CWE pyGUIControlMultiLineEditGlue.cpp
    in.Alias("ptGUIControlListBox", "addStringW", "addString");             // CWE pyGUIControlListBoxGlue.cpp
    in.Alias("ptGUIControlListBox", "findStringW", "findString");           // CWE pyGUIControlListBoxGlue.cpp
    in.Alias("ptGUIControlListBox", "setElementW", "setElement");           // CWE pyGUIControlListBoxGlue.cpp
    in.Methods("ptGUIControlListBox", s_listBoxMethods);                    // getElementW
    in.Alias("ptGUIControlListBox", "addBranchW", "addBranch");             // CWE pyGUIControlListBoxGlue.cpp
    in.Alias("ptGUIPopUpMenu", "addConsoleCmdItemW", "addConsoleCmdItem");  // CWE pyGUIPopUpMenuGlue.cpp
    in.Alias("ptGUIPopUpMenu", "addNotifyItemW", "addNotifyItem");          // CWE pyGUIPopUpMenuGlue.cpp
    in.Alias("ptGUIPopUpMenu", "addSubMenuItemW", "addSubMenuItem");        // CWE pyGUIPopUpMenuGlue.cpp
    in.Alias("ptDynamicMap", "drawTextW", "drawText");                      // CWE pyDynamicTextGlue.cpp
    in.Alias("ptVaultFolderNode", "setFolderNameW", "setFolderName");       // CWE pyVaultFolderNodeGlue.cpp
    in.Methods("ptVaultFolderNode", s_folderNodeMethods);                   // getFolderNameW
    in.Alias("ptVaultImageNode", "setTitleW", "setTitle");                  // CWE pyVaultImageNodeGlue.cpp
    in.Methods("ptVaultImageNode", s_imageNodeMethods);                     // getTitleW
    in.Alias("ptVaultTextNoteNode", "setTitleW", "setTitle");               // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "setTextW", "setText");                 // CWE pyVaultTextNoteNodeGlue.cpp
    in.Methods("ptVaultTextNoteNode", s_textNoteNodeMethods);               // getTitleW, getTextW
    in.AliasFunction("PtCreatePlayerW", "PtCreatePlayer");                  // CWE cyAccountManagementGlue.cpp

    // H-uru renamed CWE's marker game type enum (same four elements and values: CWE pnGmMarker.h
    // and H-uru pnGmMarkerConst.h, EMarkerGameType). CWE registers it in PlasmaGameConstants (CWE
    // PythonInterface::initPython, pyMarkerGame::AddPlasmaConstantsClasses in
    // Games/Marker/pyMarkerGameGlue.cpp), where H-uru has PtMarkerGameType
    // (pyGmMarker::AddPlasmaGameConstantsClasses). xKI imports it at module level
    // (xMarkerGameKIDisplay: kDefaultGameType = PtMarkerGameTypes.kMarkerGameQuest).
    in.AliasInModule("PlasmaGameConstants", "PtMarkerGameTypes", "PtMarkerGameType");

    // Legacy prefixed names (CWE pyVaultFolderNodeGlue.cpp, pyVaultImageNodeGlue.cpp,
    // pyVaultTextNoteNodeGlue.cpp, pyVaultChronicleNodeGlue.cpp, pyVaultPlayerInfoListNodeGlue.cpp).
    in.Alias("ptVaultFolderNode", "folderGetName", "getFolderName");                 // CWE pyVaultFolderNodeGlue.cpp
    in.Alias("ptVaultFolderNode", "folderSetName", "setFolderName");                 // CWE pyVaultFolderNodeGlue.cpp
    in.Alias("ptVaultFolderNode", "folderGetType", "getFolderType");                 // CWE pyVaultFolderNodeGlue.cpp
    in.Alias("ptVaultFolderNode", "folderSetType", "setFolderType");                 // CWE pyVaultFolderNodeGlue.cpp
    in.Alias("ptVaultImageNode", "imageGetTitle", "getTitle");                       // CWE pyVaultImageNodeGlue.cpp
    in.Alias("ptVaultImageNode", "imageSetTitle", "setTitle");                       // CWE pyVaultImageNodeGlue.cpp
    in.Alias("ptVaultImageNode", "imageGetImage", "getImage");                       // CWE pyVaultImageNodeGlue.cpp
    in.Alias("ptVaultImageNode", "imageSetImage", "setImage");                       // CWE pyVaultImageNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteGetTitle", "getTitle");                     // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteSetTitle", "setTitle");                     // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteGetText", "getText");                       // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteSetText", "setText");                       // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleGetName", "getName");                 // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleSetName", "setName");                 // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleGetValue", "getValue");               // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleSetValue", "setValue");               // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleGetType", "getEntryType");            // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultChronicleNode", "chronicleSetType", "setEntryType");            // CWE pyVaultChronicleNodeGlue.cpp
    in.Alias("ptVaultPlayerInfoListNode", "playerlistAddPlayer", "addPlayer");       // CWE pyVaultPlayerInfoListNodeGlue.cpp
    in.Alias("ptVaultPlayerInfoListNode", "playerlistHasPlayer", "hasPlayer");       // CWE pyVaultPlayerInfoListNodeGlue.cpp
    in.Alias("ptVaultPlayerInfoListNode", "playerlistRemovePlayer", "removePlayer"); // CWE pyVaultPlayerInfoListNodeGlue.cpp
    in.Alias("ptVaultPlayerInfoListNode", "playerlistGetPlayer", "getPlayer");       // CWE pyVaultPlayerInfoListNodeGlue.cpp

    // ptVaultTextNoteNode's note type: CWE getType/setType (and getSubType, setSubType, the note*
    // forms) are the note's type, a `long` (CWE pyVaultTextNoteNodeGlue.cpp). In H-uru getType and
    // setType resolve to ptVaultNode's node type, so the note's own names shadow them, as in CWE.
    in.Wrap("ptVaultTextNoteNode", "getType", "getNoteType", Coerce::kLong); // CWE pyVaultTextNoteNodeGlue.cpp
    in.Wrap("ptVaultTextNoteNode", "noteGetType", "getNoteType", Coerce::kLong);
    in.Wrap("ptVaultTextNoteNode", "getSubType", "getNoteSubType", Coerce::kLong); // CWE pyVaultTextNoteNodeGlue.cpp
    in.Wrap("ptVaultTextNoteNode", "noteGetSubType", "getNoteSubType", Coerce::kLong);
    in.Alias("ptVaultTextNoteNode", "setType", "setNoteType");           // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteSetType", "setNoteType");       // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "setSubType", "setNoteSubType");     // CWE pyVaultTextNoteNodeGlue.cpp
    in.Alias("ptVaultTextNoteNode", "noteSetSubType", "setNoteSubType"); // CWE pyVaultTextNoteNodeGlue.cpp

    // CWE's result types on H-uru's methods of the same name.
    in.Wrap("ptAvatar", "getAvatarClothingGroup", "getAvatarClothingGroup", Coerce::kLong); // CWE cyAvatarGlue.cpp
    in.Wrap("ptGUIControlListBox", "getScrollPos", "getScrollPos", Coerce::kLong);          // CWE pyGUIControlListBoxGlue.cpp
    in.Wrap("ptGUIControlListBox", "getScrollRange", "getScrollRange", Coerce::kLong);
    in.Wrap("ptGUIControlListBox", "getSelection", "getSelection", Coerce::kLong);
    in.Wrap("ptGUIControlMultiLineEdit", "getScrollPosition", "getScrollPosition", Coerce::kLong); // CWE pyGUIControlMultiLineEditGlue.cpp
    in.Wrap("ptGUIControlRadioGroup", "getValue", "getValue", Coerce::kLong);                      // CWE pyGUIControlRadioGroupGlue.cpp
    in.Wrap("ptVaultAgeInfoNode", "getAgeLanguage", "getAgeLanguage", Coerce::kLong);              // CWE pyVaultAgeInfoNodeGlue.cpp
    in.Wrap("ptVaultAgeInfoNode", "getAgeSequenceNumber", "getAgeSequenceNumber", Coerce::kLong);
    in.Wrap("ptMarkerMgr", "getSelectedMarker", "getSelectedMarker", Coerce::kULong);         // CWE pyMarkerMgrGlue.cpp
    in.Wrap("ptGUIControlEditBox", "getLastKeyCaptured", "getLastKeyCaptured", Coerce::kInt); // CWE pyGUIControlEditBoxGlue.cpp
    in.Wrap("ptGUIControlEditBox", "getLastModifiersCaptured", "getLastModifiersCaptured", Coerce::kInt);

    // Methods with their own CWE body.
    in.Methods("ptGUIControlMultiLineEdit", s_mleMethods);
    in.Methods("ptAudioControl", s_audioMethods);
    in.Methods("ptMarkerMgr", s_markerMethods);
    in.Methods("ptVaultPlayerNode", s_playerNodeMethods);
    in.Alias("ptAgeVault", "getAgesIOwnFolder", "getBookshelfFolder"); // CWE pyAgeVaultGlue.cpp
    in.Methods("ptAgeVault", s_ageVaultMethods);
    in.Methods("ptVault", s_vaultMethods);

    // CWE's ptScoreMgr, and CWE's ptGameScore in place of H-uru's (pyScoreMgr2.cpp)
    int scoreNames = plPy2CWE::InstallScoreClasses(plasma);
    if (scoreNames < 0)
        in.IFail("cannot add ptScoreMgr and ptGameScore");
    else
        in.fAdded += scoreNames;

    // CWE's game clients, messages and Pt*Game functions in PlasmaGame (ptGameCli
    // replacing H-uru's) and their enums in PlasmaGameConstants (pyGameCli2.cpp)
    int gameNames = plPy2CWE::InstallGameClasses();
    if (gameNames < 0)
        in.IFail("cannot add the GameMgr classes");
    else
        in.fAdded += gameNames;

    // Module functions, H-uru's originals kept for the ones that forward.
    s_origGetPublicAgeList = in.Original("PtGetPublicAgeList");
    s_origCreatePublicAge = in.Original("PtCreatePublicAge");
    s_origRemovePublicAge = in.Original("PtRemovePublicAge");
    s_origDumpLogs = in.Original("PtDumpLogs");
    if (s_origGetPublicAgeList && s_origCreatePublicAge && s_origRemovePublicAge && s_origDumpLogs)
        in.Functions(s_functions);

    if (dbgLog)
        dbgLog->AddLineF(in.fFailed ? plStatusLog::kRed : plStatusLog::kWhite,
                         "Python: CWE overlay installed: {} names, {} failures", in.fAdded, in.fFailed);
}

#endif // PLASMA_PYTHON_ABI < 3
