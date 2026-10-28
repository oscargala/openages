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
#ifndef pyEnum_h
#define pyEnum_h

#include "HeadSpin.h"

#include <type_traits>

#include "plPythonCallable.h"
#include "plPythonConvert.h"
#include "pyObjectRef.h"

#if PLASMA_PYTHON_ABI >= 3
// Helper functions used by the enum glue
class pyEnum
{
public:
    class EnumValue
    {
        pyObjectRef fName;
        pyObjectRef fValue;

    public:
        EnumValue() = delete;
        EnumValue(const EnumValue&) = delete;

        template <size_t _Sz, typename _ValueT>
        EnumValue(const char (&name)[_Sz], _ValueT&& value)
            : fName(PyUnicode_FromStringAndSize(name, _Sz - 1)),
              fValue(plPython::ConvertFrom(static_cast<std::underlying_type_t<_ValueT>>(value)))
        {
            static_assert(std::is_enum_v<_ValueT>, "EnumValue can only be used with enumerations");
            static_assert(std::is_integral_v<std::underlying_type_t<_ValueT>>, "EnumValue can only be used with enumerations with integral underlying types");
        }

        EnumValue(EnumValue&& move) noexcept
            : fName(std::move(move.fName)),
              fValue(std::move(move.fValue))
        {
        }

        PyObject* ReleaseName() { return fName.Release(); }
        PyObject* ReleaseValue() { return fValue.Release(); }
    };

private:
    static inline void InsertEnumValue(PyObject* list, Py_ssize_t& i, EnumValue& arg)
    {
        PyObject* tuple = PyTuple_New(2);
        PyTuple_SET_ITEM(tuple, 0, arg.ReleaseName());
        PyTuple_SET_ITEM(tuple, 1, arg.ReleaseValue());
        PyList_SET_ITEM(list, i, tuple);
        i++;
    }

public:
    template<size_t _Sz, typename... Args>
    static void MakeEnum(PyObject* m, const char (&name)[_Sz], Args&&... args)
    {
        if (m == nullptr)
            return;

        pyObjectRef enumModule = PyImport_ImportModule("enum");
        hsAssert(enumModule, "Failed to import enum module");
        if (!enumModule)
            return;

        pyObjectRef intEnumClass = PyObject_GetAttrString(enumModule.Get(), "IntEnum");
        if (!intEnumClass)
            return;

        pyObjectRef enumList = PyList_New(sizeof...(args));
        Py_ssize_t  i = 0;
        (InsertEnumValue(enumList.Get(), i, args), ...);

        pyObjectRef newEnum = plPython::CallObject(
            intEnumClass,
            PyUnicode_FromStringAndSize(name, _Sz - 1),
            std::move(enumList)
        );

        hsAssert(newEnum, "Failed to create enum");
        if (newEnum) {
            if (PyModule_AddObject(m, name, newEnum.Get()) == 0) {
                // PyModule_AddObject steals a reference only on success.
                // On error, we still own the reference.
                newEnum.Release();
            }
        }
    }
};

#else
#include <map>
#include <string>

// CPython 2.3 has no `enum` module (3.4). The py2 flavour builds Cyan's own enum types instead
// (pyEnum2.cpp, ported from CWE pyEnum.cpp): PtFoo is a PlasmaConstants.Enum whose attributes
// are PlasmaConstants.EnumValue objects named "PtFoo.kBar" that compare, hash and convert as
// their int value. CWE built each enum from a std::map<std::string, int> (PYTHON_ENUM_START and
// PYTHON_ENUM_END in CWE pyGlueHelpers.h), so the values are kept as int, as there.
class pyEnum
{
public:
    class EnumValue
    {
        const char* fName;
        int         fValue;

    public:
        EnumValue() = delete;

        template <size_t _Sz, typename _ValueT>
        EnumValue(const char (&name)[_Sz], _ValueT&& value)
            : fName(name), fValue((int)static_cast<std::underlying_type_t<std::decay_t<_ValueT>>>(value))
        {
            static_assert(std::is_enum_v<std::decay_t<_ValueT>>, "EnumValue can only be used with enumerations");
        }

        const char* Name() const { return fName; }
        int         Value() const { return fValue; }
    };

    template <size_t _Sz, typename... Args>
    static void MakeEnum(PyObject* m, const char (&name)[_Sz], Args&&... args)
    {
        std::map<std::string, int> values;
        ((values[args.Name()] = args.Value()), ...);
        MakeEnumFromMap(m, name, values);
    }

    // CWE's enum construction and type registration (pyEnum2.cpp, from CWE pyEnum.cpp)
    static void MakeEnumFromMap(PyObject* m, const char* name, const std::map<std::string, int>& values);
    static void AddPlasmaConstantsClasses(PyObject* m);
};
#endif // PLASMA_PYTHON_ABI >= 3

/////////////////////////////////////////////////////////////////////
// Enum glue (these should all be inside a function)
/////////////////////////////////////////////////////////////////////

// the start of an enum block
#define PYTHON_ENUM_START(m, enumName) pyEnum::MakeEnum(m, #enumName

// for each element of the enum
#define PYTHON_ENUM_ELEMENT(enumName, elementName, elementValue) , pyEnum::EnumValue(#elementName, elementValue)

// to finish off and define the enum
#define PYTHON_ENUM_END(m, enumName) );

#endif  // pyEnum_h
