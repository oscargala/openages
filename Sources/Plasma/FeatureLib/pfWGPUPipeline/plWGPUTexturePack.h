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
#ifndef _plWGPUTexturePack_inc_
#define _plWGPUTexturePack_inc_

// The optional "verified original high-res textures" pack, browser only and off by default. An Age's pack is read
// into memory when one of its rooms is about to load (plClient::ILoadNextRoom, before PageInRoom: the loading
// screen), never in the upload path. plWGPUDevice::MakeTextureRef then looks keys up in memory only.
// The fetch starts when the client joins the Age (plAgeLoader::UpdateAge, PrefetchAge) and streams on the browser
// main thread into one allocation of the file's size (plWGPUPipeline.js); the client thread verifies it as it
// arrives (Pump) and decides at the Age's first room.
// A pack verified from the network is kept in Cache Storage, keyed by its URL and its data SHA-256; a later
// join streams the entry into the same single allocation once its server confirms it is current (a conditional
// request on its ETag), and the client checks it in full again. A cached pack that fails a check is evicted and
// fetched from the network.

#include <cstdint>
#include <string_theory/string>
#include <vector>

class plLocation;
class plUoid;

namespace plWGPUTexturePack
{
struct Entry
{
    // identity (class plMipmap, location compared as plLocation::operator==, object id)
    uint32_t                    locSequence;
    uint16_t                    locFlags;
    uint16_t                    classType;
    uint32_t                    objectId;
    // the shipped plBitmap/plMipmap header
    uint8_t                     compression;
    uint8_t                     dxtType;
    uint8_t                     pixelSize;
    uint8_t                     rule;
    uint16_t                    flags;
    uint32_t                    width;
    uint32_t                    height;
    uint8_t                     numLevels;
    uint32_t                    totalSize;
    uint8_t                     shaAll[32];    // SHA-256 of all shipped levels (c = 0)
    uint8_t                     shaFrom1[32];  // SHA-256 of shipped levels 1.. (c = 1, the TextureQuality chop)
    uint8_t                     shaLevel0[32]; // SHA-256 of shipped level 0 (the pack's verbatim copy)
    // the pack chain
    uint8_t                     g;    // HD levels above shipped level 0 (log2 of the gain)
    uint8_t                     tail; // 0: shipped levels follow verbatim (rules 1, 2); 1: the pack holds the chain (rule 3)
    uint32_t                    hdWidth;
    uint32_t                    hdHeight;
    uint8_t                     packLevels;
    std::vector<const uint8_t*> levelData; // into the pack's bytes
    std::vector<uint32_t>       levelSize;
};

/// Called when the client joins an Age (plAgeLoader::UpdateAge, before any of its rooms pages in). For an Age the
/// deployment lists, with the setting on, starts that Age's pack fetch unless it is held or on its way. No I/O
/// on this thread: the fetch runs on the browser main thread.
void PrefetchAge(const ST::string& ageName);

/// Once per frame (plClient::IUpdate): verifies the bytes of a running fetch, a few ms at a time.
void Pump();

/// Called before a room pages in (plClient::ILoadNextRoom). For a room of an Age the deployment lists, reads the
/// profile toggle and, when on, decides at the Age's first room: its pack if verified now (after a bounded wait
/// for its bytes, starting the fetch if none runs), else the shipped textures for the visit; when off, drops it.
/// Other Ages: no-op.
void PrepareRoom(const plLocation& loc);

/// The entry for a plMipmap key, or nullptr: memory lookup only.
const Entry* Find(const plUoid& uoid);

/// A key whose loaded data did not match its entry (logged once, never offered again until the pack reloads).
void Refuse(const Entry* entry, const char* why);

/// Whether a pack is active at all (false when forced off).
bool Active();

#ifdef PLASMA_TEST_HOOKS
/// The test hooks' rows read the shipped CPU plMipmap, so a run that records them has no pack for the whole
/// session. The test hooks' client seam calls this from OnClientInit, before any Age joins, when one of its
/// switches is set, and names that switch in the log line. The player build has neither the setter nor a caller.
void ForceOff(const ST::string& switchName);
#endif

/// Counters for the texture pack's log lines.
extern uint32_t fUploaded;
extern uint32_t fRefused;
} // namespace plWGPUTexturePack

#endif // _plWGPUTexturePack_inc_
