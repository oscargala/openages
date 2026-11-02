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

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <vector>

#include "pfWGPUPipeline/plWGPUSampleCountSwitch.h"

// The main target's sample count across an anti-aliasing change: the count drawn with stays until the pending count's
// pipelines are built (plWGPUDevice::RequestMSAASampleCount, SwitchSampleCount). The builds are modelled as the device
// keeps them: records queued and in flight, each with its class (plWGPUDevice's fPipelineQueued, fPipelineBuilds).

namespace
{
struct Build
{
    plWGPUBuildClass cls;
};
using Builds = std::map<int, Build>; // record -> its build

// A visit asks for a record (plWGPUDevice::IPrewarmRecord): a record already queued or in flight keeps the higher
// class; a new one is queued with the visit's class. True if a build was queued.
bool Visit(Builds& queued, Builds& inFlight, int record, plWGPUBuildClass cls)
{
    if (auto it = queued.find(record); it != queued.end()) {
        it->second.cls = plWGPUSampleCountSwitch::Merge(it->second.cls, cls);
        return false;
    }
    if (auto it = inFlight.find(record); it != inFlight.end()) {
        it->second.cls = plWGPUSampleCountSwitch::Merge(it->second.cls, cls);
        return false;
    }
    queued[record] = Build{cls};
    return true;
}

// The same, after the built pipelines (fNewPipelineStateMap): a built record is known, and nothing is queued.
bool Visit(const Builds& built, Builds& queued, Builds& inFlight, int record, plWGPUBuildClass cls)
{
    return !built.count(record) && Visit(queued, inFlight, record, cls);
}

size_t PendingOnly(const Builds& queued, const Builds& inFlight)
{
    return plWGPUSampleCountSwitch::Count(queued, plWGPUBuildClass::kPendingOnly) +
           plWGPUSampleCountSwitch::Count(inFlight, plWGPUBuildClass::kPendingOnly);
}

const std::vector<uint8_t> kChromiumTypes = {4}; // Chromium on the Mac mini: the canvas takes 1 or 4 samples
} // namespace

// SetMSAASampleCount's search, which the pending count also takes: the closest supported count <= the request, 1
// when none is.
TEST(plWGPUSampleCountSwitch, resolve_is_the_closest_supported_count_at_most_the_request)
{
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(4, kChromiumTypes), 4);
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(6, kChromiumTypes), 4);
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(2, kChromiumTypes), 1);
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(0, kChromiumTypes), 1);
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(3, {2, 4}), 2);
    // WebKit on Metal: kCapsNoAA empties the list, so every request gives 1 and nothing is ever pending.
    EXPECT_EQ(plWGPUSampleCountSwitch::Resolve(4, {}), 1);
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(1, plWGPUSampleCountSwitch::Resolve(4, {}), 0.0), plWGPUSampleCountSwitch::Change::kUnchanged);
    EXPECT_FALSE(sw.Pending());
}

// Case 1: 4 -> 1. The pending count's main-target records are single-sampled, as every render target's: a record the
// pending visit queued and a render-target visit then asks for is upgraded, and the scene frame drains it; the
// pending-only ones are not drained, and the switch waits for them.
TEST(plWGPUSampleCountSwitch, a_pending_only_record_a_render_target_visit_asks_for_is_drained)
{
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(4, 1, 10.0), plWGPUSampleCountSwitch::Change::kSet);
    EXPECT_TRUE(sw.Pending());
    EXPECT_EQ(sw.PendingCount(), 1);
    EXPECT_EQ(sw.TargetCount(4), 1);

    Builds queued, inFlight;
    EXPECT_TRUE(Visit(queued, inFlight, 100, plWGPUBuildClass::kPendingOnly)); // the main target at 1
    EXPECT_TRUE(Visit(queued, inFlight, 101, plWGPUBuildClass::kPendingOnly));
    EXPECT_FALSE(Visit(queued, inFlight, 100, plWGPUBuildClass::kDrain)); // a render target's visit, the same record
    EXPECT_EQ(queued[100].cls, plWGPUBuildClass::kDrain);
    EXPECT_EQ(plWGPUSampleCountSwitch::Count(queued, plWGPUBuildClass::kDrain), 1u);
    EXPECT_EQ(PendingOnly(queued, inFlight), 1u);

    // A later pending visit of an upgraded record leaves it drained.
    EXPECT_FALSE(Visit(queued, inFlight, 100, plWGPUBuildClass::kPendingOnly));
    EXPECT_EQ(queued[100].cls, plWGPUBuildClass::kDrain);

    // Walked at the pending count, but 101 is still queued: no switch. Once it is built, the switch.
    EXPECT_FALSE(sw.Ready(true, 1, true, PendingOnly(queued, inFlight)));
    queued.erase(101);
    EXPECT_TRUE(sw.Ready(true, 1, true, PendingOnly(queued, inFlight)));
}

// Case 2: a second change before the switch. The pending count's queued pending-only builds leave the queue, those in
// flight finish unwaited, and upgraded builds stay; the newest choice is the pending count.
TEST(plWGPUSampleCountSwitch, a_second_change_abandons_pending_only_builds_and_keeps_upgraded_ones)
{
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(4, 1, 0.0), plWGPUSampleCountSwitch::Change::kSet);
    Builds queued, inFlight;
    queued[1] = Build{plWGPUBuildClass::kPendingOnly};
    queued[2] = Build{plWGPUBuildClass::kDrain}; // upgraded by a render target's visit
    inFlight[3] = Build{plWGPUBuildClass::kPendingOnly};
    inFlight[4] = Build{plWGPUBuildClass::kDrain};
    sw.NoteQueued();
    sw.NoteQueued();
    sw.NoteFrameStart();

    EXPECT_EQ(sw.Request(4, 2, 50.0), plWGPUSampleCountSwitch::Change::kReplaced);
    EXPECT_EQ(sw.Abandoned(), 1);
    EXPECT_EQ(sw.PendingCount(), 2);
    EXPECT_EQ(sw.TargetCount(4), 2);
    // The window starts again for the newest choice.
    EXPECT_EQ(sw.Builds(), 0u);
    EXPECT_EQ(sw.FrameStarts(), 0u);
    EXPECT_EQ(sw.StartMs(), 50.0);

    size_t dequeued = 0, released = 0;
    plWGPUSampleCountSwitch::Abandon(queued, inFlight, dequeued, released);
    EXPECT_EQ(dequeued, 1u);
    EXPECT_EQ(released, 1u);
    EXPECT_EQ(queued.count(1), 0u);
    EXPECT_EQ(queued[2].cls, plWGPUBuildClass::kDrain);
    EXPECT_EQ(inFlight[3].cls, plWGPUBuildClass::kAbandoned);
    EXPECT_EQ(inFlight[4].cls, plWGPUBuildClass::kDrain);
    // An abandoned build is neither drained nor waited for by the switch.
    EXPECT_EQ(PendingOnly(queued, inFlight), 0u);
    EXPECT_EQ(plWGPUSampleCountSwitch::Count(inFlight, plWGPUBuildClass::kDrain), 1u);

    // A pending visit that asks for an abandoned build in flight takes it back; a drawn-count visit drains it.
    EXPECT_FALSE(Visit(queued, inFlight, 3, plWGPUBuildClass::kPendingOnly));
    EXPECT_EQ(inFlight[3].cls, plWGPUBuildClass::kPendingOnly);
    EXPECT_FALSE(Visit(queued, inFlight, 3, plWGPUBuildClass::kDrain));
    EXPECT_EQ(inFlight[3].cls, plWGPUBuildClass::kDrain);

    // The same choice again (a browser resize re-issues ResetDisplayDevice) keeps the pending count and its window.
    sw.NoteQueued();
    EXPECT_EQ(sw.Request(4, 2, 80.0), plWGPUSampleCountSwitch::Change::kKept);
    EXPECT_EQ(sw.PendingCount(), 2);
    EXPECT_EQ(sw.Builds(), 1u);
    EXPECT_EQ(sw.StartMs(), 50.0);
}

// Case 3: a change back to the drawn count clears the pending count; the re-walk that follows is at the drawn count,
// whose pipelines were never released, so it builds nothing.
TEST(plWGPUSampleCountSwitch, a_change_back_to_the_drawn_count_clears_it_with_nothing_to_build)
{
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(4, 1, 0.0), plWGPUSampleCountSwitch::Change::kSet);
    Builds queued, inFlight;
    // Built at 4; queued and in flight at 1.
    Builds built = {{10, Build{plWGPUBuildClass::kDrain}}, {11, Build{plWGPUBuildClass::kDrain}}};
    queued[20] = Build{plWGPUBuildClass::kPendingOnly};
    inFlight[21] = Build{plWGPUBuildClass::kPendingOnly};

    EXPECT_EQ(sw.Request(4, 4, 5.0), plWGPUSampleCountSwitch::Change::kCleared);
    EXPECT_EQ(sw.Abandoned(), 1);
    EXPECT_FALSE(sw.Pending());
    EXPECT_EQ(sw.TargetCount(4), 4);
    size_t dequeued = 0, released = 0;
    plWGPUSampleCountSwitch::Abandon(queued, inFlight, dequeued, released);
    EXPECT_EQ(dequeued, 1u);
    EXPECT_EQ(released, 1u);

    // The re-walk at the drawn count finds every record built: no build is queued.
    size_t started = 0;
    for (int record : {10, 11})
        started += Visit(built, queued, inFlight, record, plWGPUBuildClass::kDrain) ? 1 : 0;
    EXPECT_EQ(started, 0u);
    EXPECT_TRUE(queued.empty());
    EXPECT_EQ(PendingOnly(queued, inFlight), 0u);
    // Nothing pending: no switch, at any condition.
    EXPECT_FALSE(sw.Ready(true, 4, true, 0));
    EXPECT_FALSE(sw.Ready(true, 1, true, 0));

    // With nothing pending, the drawn count again is no change.
    EXPECT_EQ(sw.Request(4, 4, 9.0), plWGPUSampleCountSwitch::Change::kUnchanged);
}

// Case 4: the switch waits for a frame start that walked at the pending count in this device generation, even with
// no build queued or in flight (a frame start the prewarm skipped records no conditions).
TEST(plWGPUSampleCountSwitch, the_switch_waits_for_a_walk_at_the_pending_count)
{
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(1, 4, 0.0), plWGPUSampleCountSwitch::Change::kSet);
    EXPECT_FALSE(sw.Ready(false, 0, true, 0)); // no conditions recorded yet
    EXPECT_FALSE(sw.Ready(true, 1, true, 0));  // walked at the drawn count only
    EXPECT_FALSE(sw.Ready(true, 4, false, 0)); // walked at 4 in a released generation
    EXPECT_FALSE(sw.Ready(true, 4, true, 3));  // walked at 4, three builds left
    EXPECT_TRUE(sw.Ready(true, 4, true, 0));
}

// Case 5: a recreate (the failed-Reset branch) sets the count at once: whatever was pending is gone.
TEST(plWGPUSampleCountSwitch, a_recreate_clears_the_pending_count_and_sets_the_count_at_once)
{
    plWGPUSampleCountSwitch sw;
    EXPECT_EQ(sw.Request(4, 1, 0.0), plWGPUSampleCountSwitch::Change::kSet);
    // ICreateDevice -> SetMSAASampleCount: fSampleCount = Resolve(fNumAASamples), then Clear.
    const uint8_t drawn = plWGPUSampleCountSwitch::Resolve(0, kChromiumTypes);
    sw.Clear();
    EXPECT_EQ(drawn, 1);
    EXPECT_FALSE(sw.Pending());
    EXPECT_EQ(sw.TargetCount(drawn), 1);
    EXPECT_FALSE(sw.Ready(true, 1, true, 0));
    // The next reset at the same count changes nothing.
    EXPECT_EQ(sw.Request(drawn, 1, 1.0), plWGPUSampleCountSwitch::Change::kUnchanged);
}
