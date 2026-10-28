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
#ifndef _pyCWEOverlay2_h_
#define _pyCWEOverlay2_h_

// The CWE surface overlay of the py2 flavour (PLASMA_PYTHON_ABI=2 only; see
// Docs/Web/Python 2.3 flavour.md). MOULa's python.pak was written against Cyan's CWE glue
// (OpenUru CWE-ou-minkata 76ef5e01); H-uru's glue renamed, folded or moved part of it. The
// overlay puts CWE's names, argument orders and return types back on top of H-uru's Plasma
// module, after the six builtin modules exist and before any script runs. Each CWE-derived part
// names the CWE file and function it reproduces (GPLv3, as this file).

#include "pyPy2Compat.h"

#if PLASMA_PYTHON_ABI < 3

class plStatusLog;

namespace plPy2CWE
{
// Adds the CWE names to the Plasma modules (sys.modules), logging to dbgLog.
void InstallOverlay(plStatusLog* dbgLog);
} // namespace plPy2CWE

#endif // PLASMA_PYTHON_ABI < 3

#endif // _pyCWEOverlay2_h_
