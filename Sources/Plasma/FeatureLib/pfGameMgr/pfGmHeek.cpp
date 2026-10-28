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

// The Heek requests (pfGmHeek.h).
// Ported from CWE (OpenUru CWE-ou-minkata 76ef5e01, GPLv3 as this file; "CWE" below):
// FeatureLib/pfGameMgr/Heek/pfGmHeek.cpp, PlayGame, LeaveGame, Choose and SequenceFinished.

#include "pfGmHeek.h"

#include "plNetGameLib/plNetGameLib.h"
#include "pnGameMgr/pnGmHeek.h"
#include "pnUtils/pnUtStr.h"

// ===========================================================================

#define HEEK_MSG(x, name)               \
    Cli2Srv_Heek_##x name;              \
    name.messageId = kCli2Srv_Heek_##x; \
    name.transId = 0;                   \
    name.recvGameId = GetGameID();      \
    name.messageBytes = sizeof(name); //

#define HEEK_SEND(name) \
    NetCliGameSendGameMgrMsg(&name); //

void pfGmHeek::PlayGame(uint8_t position, uint32_t points, const ST::string& name) const
{
    HEEK_MSG(PlayGame, msg)
    msg.position = position;
    msg.points = points;
    StrCopy(msg.name, name.to_utf16().data(), std::size(msg.name));
    HEEK_SEND(msg)
}

void pfGmHeek::LeaveGame() const
{
    HEEK_MSG(LeaveGame, msg)
    HEEK_SEND(msg)
}

void pfGmHeek::Choose(EHeekChoice choice) const
{
    HEEK_MSG(Choose, msg)
    msg.choice = choice;
    HEEK_SEND(msg)
}

void pfGmHeek::SequenceFinished(EHeekSeqFinished seq) const
{
    HEEK_MSG(SeqFinished, msg)
    msg.seqFinished = seq;
    HEEK_SEND(msg)
}

#undef HEEK_SEND
#undef HEEK_MSG
