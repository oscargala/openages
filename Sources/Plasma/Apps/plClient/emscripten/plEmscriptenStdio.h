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

// plEmscriptenStdio.cpp's test seams: a harness build's only (PLASMA_TEST_HOOKS), for the cache's descriptor-level
// test. The product build declares nothing here.
#ifndef plEmscriptenStdio_inc
#define plEmscriptenStdio_inc

#ifdef PLASMA_TEST_HOOKS
#include <wasi/api.h>

#include <cstddef>
#include <cstdint>

namespace plEmscriptenStdio
{
// The cache's cap, at most the product's 16 MiB; set before any game-data FILE is open.
void SetCacheCapForTest(size_t cap);

// What the cache's preads call instead of __wasi_fd_pread (null: __wasi_fd_pread), with the same arguments.
using PreadForTest = __wasi_errno_t (*)(__wasi_fd_t fd, const __wasi_iovec_t* iovs, size_t iovs_len,
                                        __wasi_filesize_t offset, __wasi_size_t* nread);
void SetPreadForTest(PreadForTest pread);

struct CacheTotals
{
    size_t   resident = 0; // the blocks' bytes and the reservations of the calls in flight
    size_t   peak = 0;
    size_t   cap = 0;
    size_t   pinned = 0; // blocks pinned now
    uint64_t poisonings = 0;
};
CacheTotals CacheTotalsForTest();

// A game-data FILE's descriptor's counts (the "[stdio]" line's); false for any other descriptor.
struct CacheCounts
{
    uint64_t backendReads = 0, served = 0, servedCalls = 0, preads = 0, spanPreads = 0, original = 0;
    uint64_t evictions = 0, reMisses = 0;
    size_t   blocks = 0;
    bool     poisoned = false;
};
bool CacheCountsForTest(int fd, CacheCounts& counts);
} // namespace plEmscriptenStdio
#endif

#endif // plEmscriptenStdio_inc
