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

#ifndef plWGPUSampleCountSwitch_h
#define plWGPUSampleCountSwitch_h

// The main target's sample count across an anti-aliasing change (plWGPUPipeline::IResetDevice's successful-Reset
// branch). D3D9 binds its shaders and render state per draw, apart from the multisampled back buffer, so its Reset
// compiles nothing for a new multisample type (plDXPipeline::IResetDevice, plDXPipeline.cpp:1922-1965; shaders are
// released only by IReleaseShaders, :1594, from IReleaseDeviceObjects at :1673 on the failed branch, :1936, and
// from the debug reload, kFlagReload :2939-2943). A WebGPU render pipeline carries its sample count
// (plWGPUDevice::IPipelineDescriptor), so a new count needs every pipeline again. The device keeps drawing at the
// count it draws with while the walks build the new count's pipelines, and takes the new count at a frame start once
// none of them is left to build. Plain CPU code with no WebGPU type: the device owns one and asks it.

#include <cstddef>
#include <cstdint>
#include <vector>

// The class of a pipeline build, queued or in flight (plWGPUDevice's fPipelineQueued, fPipelineBuilds), ordered so
// that a request for a record already there keeps the higher of the two (Merge).
enum class plWGPUBuildClass : uint8_t
{
    kAbandoned,   // in flight for a pending count a later change replaced or cleared: neither drained nor waited for
    kPendingOnly, // asked for only by a pending-count visit of the main target: issued by the pacing, never drained
    kDrain,       // asked for by a visit at a count the device draws with: the scene frame waits for it
};

class plWGPUSampleCountSwitch
{
public:
    // What a successful reset did to the pending count (Request).
    enum class Change
    {
        kUnchanged, // the resolved count is the drawn count, and none was pending
        kSet,       // a new pending count
        kReplaced,  // a pending count replaced by another (Abandoned() names the old one)
        kKept,      // the resolved count is the pending count: it goes on building, its window unchanged
        kCleared,   // back to the drawn count: the pending count is gone (Abandoned() names it)
    };

    /** The closest supported count <= `requested` from the selected mode's FSAA types, 1 when none is
        (plWGPUDevice::SetMSAASampleCount, as plMetalDevice::SetMSAASampleCount and the AA search of
        plDXPipeline::ResetDisplayDevice, plDXPipeline.cpp:2022-2031). */
    static uint8_t Resolve(uint8_t requested, const std::vector<uint8_t>& fsaaTypes);

    /** A successful reset resolved `resolved` while the device draws at `drawn`; nowMs starts a new window. */
    Change Request(uint8_t drawn, uint8_t resolved, double nowMs);
    /** The count is set at once (the device's creation and the failed-Reset branch's recreate): nothing is pending. */
    void   Clear();

    bool    Pending() const { return fPending != 0; }
    uint8_t PendingCount() const { return fPending; }
    /** The count the walks build the main target for: the pending count, or the drawn count when none is pending. */
    uint8_t TargetCount(uint8_t drawn) const { return fPending ? fPending : drawn; }
    /** The pending count the last kReplaced or kCleared gave up. */
    uint8_t Abandoned() const { return fAbandoned; }

    /** The window's figures for the switch line: the pending-only builds queued, the frame starts, its start. */
    void     NoteQueued() { fBuilds++; }
    void     NoteFrameStart() { fFrameStarts++; }
    uint32_t Builds() const { return fBuilds; }
    uint32_t FrameStarts() const { return fFrameStarts; }
    double   StartMs() const { return fStartMs; }

    /** Whether the device takes the pending count now: the frame start's walk conditions were recorded at the pending
        count in this device generation (a frame start the prewarm skipped records none, so it never reads as nothing
        to build), and no pending-only build is queued or in flight. */
    bool Ready(bool conditionsSet, uint32_t walkedSamples, bool walkedThisGeneration, size_t pendingOnly) const;

    /** The class a record keeps when a visit of class `request` asks for it while it has class `existing`. */
    static plWGPUBuildClass Merge(plWGPUBuildClass existing, plWGPUBuildClass request)
    {
        return existing > request ? existing : request;
    }

    /** The builds of `cls` in a map of queued or in-flight builds (values with a `cls` member). */
    template <class Map>
    static size_t Count(const Map& builds, plWGPUBuildClass cls)
    {
        size_t n = 0;
        for (const auto& entry : builds)
            n += entry.second.cls == cls ? 1 : 0;
        return n;
    }

    /** A pending count given up (kReplaced, kCleared, or a Clear while one was pending): its queued pending-only builds
        leave the queue, and those in flight finish unwaited (kAbandoned); upgraded (kDrain) builds stay. */
    template <class QueuedMap, class InFlightMap>
    static void Abandon(QueuedMap& queued, InFlightMap& inFlight, size_t& dequeued, size_t& released)
    {
        dequeued = 0;
        released = 0;
        for (auto it = queued.begin(); it != queued.end();) {
            if (it->second.cls == plWGPUBuildClass::kPendingOnly) {
                it = queued.erase(it);
                dequeued++;
            } else {
                ++it;
            }
        }
        for (auto& entry : inFlight) {
            if (entry.second.cls == plWGPUBuildClass::kPendingOnly) {
                entry.second.cls = plWGPUBuildClass::kAbandoned;
                released++;
            }
        }
    }

private:
    uint8_t  fPending = 0; // 0: none
    uint8_t  fAbandoned = 0;
    uint32_t fBuilds = 0;
    uint32_t fFrameStarts = 0;
    double   fStartMs = 0.0;

    void IStart(uint8_t pending, double nowMs);
};

#endif // plWGPUSampleCountSwitch_h
