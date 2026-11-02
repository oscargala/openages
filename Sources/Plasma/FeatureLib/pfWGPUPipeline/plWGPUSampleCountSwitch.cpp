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

// The main target's sample count across an anti-aliasing change (see plWGPUSampleCountSwitch.h).

#include "plWGPUSampleCountSwitch.h"

#include <algorithm>

uint8_t plWGPUSampleCountSwitch::Resolve(uint8_t requested, const std::vector<uint8_t>& fsaaTypes)
{
    uint8_t sampleCount = requested;
    while (sampleCount > 1) {
        if (std::find(fsaaTypes.cbegin(), fsaaTypes.cend(), sampleCount) != fsaaTypes.cend()) {
            break;
        }
        sampleCount--;
    }
    if (sampleCount == 0)
        sampleCount = 1;
    return sampleCount;
}

void plWGPUSampleCountSwitch::IStart(uint8_t pending, double nowMs)
{
    fPending = pending;
    fBuilds = 0;
    fFrameStarts = 0;
    fStartMs = nowMs;
}

plWGPUSampleCountSwitch::Change plWGPUSampleCountSwitch::Request(uint8_t drawn, uint8_t resolved, double nowMs)
{
    if (fPending) {
        // A browser resize re-issues ResetDisplayDevice with the same settings (plEmscriptenDisplay): the pending
        // count goes on building.
        if (resolved == fPending)
            return Change::kKept;
        fAbandoned = fPending;
        if (resolved == drawn) {
            fPending = 0;
            return Change::kCleared;
        }
        IStart(resolved, nowMs);
        return Change::kReplaced;
    }
    if (resolved == drawn)
        return Change::kUnchanged;
    IStart(resolved, nowMs);
    return Change::kSet;
}

void plWGPUSampleCountSwitch::Clear()
{
    fPending = 0;
}

bool plWGPUSampleCountSwitch::Ready(bool conditionsSet, uint32_t walkedSamples, bool walkedThisGeneration,
                                    size_t pendingOnly) const
{
    return fPending != 0 && conditionsSet && walkedSamples == fPending && walkedThisGeneration && pendingOnly == 0;
}
