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

#ifndef _pfGmHeek_h_
#define _pfGmHeek_h_

// The Heek requests H-uru's pfGameMgr lacks. pfGmHeek is the game client class the py2 flavour's
// GameMgr relay serves Heek with (pyGameMgrWire2.cpp, Relay<pfGmHeek>), and it holds the four
// requests a script sends (pyGameCli2.cpp, ptHeekGame). The relay takes every server message for
// the game and posts it to the script's key, as Cyan's CWE client did, and builds the common-game
// join itself, so this class has no handler and no receive side of its own.
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file):
// FeatureLib/pfGameMgr/Heek/pfGmHeek.{h,cpp}. The wire structs are
// NucleusLib/pnGameMgr/pnGmHeek.h (CWE NucleusLib/pnGameMgr/Heek/pnGmHeek.h, the same layout).
// Built only for the py2 flavour (PLASMA_PYTHON_ABI=2, CMakeLists.txt), whose MOULa scripts play
// Heek through the GameMgr (nb01RPSGame); H-uru's own scripts do not.

#include <string_theory/string>

#include "pfGameCli.h"
#include "pnGameMgr/pnGmHeekConst.h"

class pfGmHeek : public pfGameCli
{
public:
    CLASSNAME_REGISTER(pfGmHeek);
    GETINTERFACE_ANY(pfGmHeek, pfGameCli);

public:
    /**
     * Request to play (instead of observe) at the specified position (CWE pfGmHeek::PlayGame).
     */
    void PlayGame(uint8_t position, uint32_t points, const ST::string& name) const;

    /**
     * Request to stop playing and go back to observing (CWE pfGmHeek::LeaveGame, the Heek
     * message, not the game manager's Game_LeaveGame).
     */
    void LeaveGame() const;

    /**
     * Choose a move (CWE pfGmHeek::Choose).
     */
    void Choose(EHeekChoice choice) const;

    /**
     * Tell the server that a client-side animation sequence finished (CWE pfGmHeek::SequenceFinished).
     */
    void SequenceFinished(EHeekSeqFinished seq) const;
};

#endif
