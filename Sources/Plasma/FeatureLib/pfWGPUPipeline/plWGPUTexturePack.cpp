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

#include "plWGPUTexturePack.h"

#include <emscripten/atomic.h>
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_theory/format>
#include <tuple>

#include "plFileSystem.h"
#include "plResMgr/plKeyFinder.h"
#include "plResMgr/plPageInfo.h"
#include "plWGPULog.h"
#include "pnEncryption/plChecksum.h"
#include "pnKeyedObject/plUoid.h"

// plWGPUPipeline.js: starts one pack fetch on the browser main thread (0), or refuses it (< 0).
// Mode 0: the pack's Cache Storage entry when its server confirms it is current, else the network; mode 1:
// the entry is evicted first and the pack comes from the network.
extern "C" int plWGPUTexturePack_fetch(const char* url, void* block, uint32_t maxBytes, int mode);
// plWGPUPipeline.js: starts keeping a verified pack in Cache Storage (0), or declines it
// (-1 Cache Storage unavailable, -2 no strong ETag from the server to revalidate the entry with, -3 the pack
// is larger than the kept packs' byte cap)
extern "C" int plWGPUTexturePack_store(const char* url, void* block);

namespace plWGPUTexturePack
{
uint32_t fUploaded = 0;
uint32_t fRefused = 0;
} // namespace plWGPUTexturePack

namespace
{
using Key = std::tuple<uint32_t, uint16_t, uint32_t>; // sequence, flags without kItinerant, object id

// The state block of one fetch, shared with plWGPUPipeline.js plWGPUTexturePack_fetch, which
// writes it from the browser main thread. This side owns the block and the buffer unless it abandons the fetch first.
struct FetchBlock
{
    int32_t  state;    // 0 in flight, 1 complete, 2 abandoned by this side, < 0 failed (the codes of IFetchWhy)
    uint32_t received; // bytes written at data (also the address both sides wait and notify on)
    uint32_t data;     // the buffer, a malloc of size bytes, 0 until the response's length is known
    uint32_t size;     // the response's Content-Length
    // Written before data: where the bytes come from (0 the network, 1 the pack's Cache Storage entry) and,
    // for an entry, the SHA-256 of its key, which must be the pack's own data SHA-256
    uint32_t source;
    uint8_t  sha[32];
};

// The state block of one Cache Storage write (plWGPUPipeline.js plWGPUTexturePack_store),
// which reads the held pack's buffer from the browser main thread. This side owns the block, and the pack owns the
// buffer, unless this side abandons the write first (compare-exchange 0 to 2); the JS side then frees both.
struct StoreBlock
{
    int32_t  state; // 0 writing, 1 kept, 2 abandoned by this side, < 0 not kept (-2: kept, then
                    // evicted by the cap as the least recently used pack)
    uint32_t data;  // the verified pack
    uint32_t size;
    uint8_t  sha[32]; // the pack's data SHA-256: with the URL, the entry's key
    // The kept packs' byte cap (plWGPUPipeline.js $plWGPUTexturePackCache.capBytes): the
    // other packs this write evicted, least recently used first, their bytes, and the cap (written at the call)
    uint32_t evicted;
    uint32_t evictedBytes;
    uint32_t cap;
};
static_assert(offsetof(StoreBlock, evicted) == 44 && offsetof(StoreBlock, cap) == 52, "plWGPUPipeline.js block [11]-[13]");

constexpr uint32_t kMaxPackBytes = 1u << 30; // a sanity cap on the response length (wasm32: 4 GiB in all)
constexpr double   kWaitMs = 2000.0;         // the bounded wait at an Age's first room
constexpr double   kPumpMs = 4.0;            // the client thread's verification time per frame while a fetch runs
constexpr size_t   kHashStep = 256u << 10;   // bytes per SHA-256 update between budget checks

struct IFree
{
    void operator()(uint8_t* p) const { std::free(p); }
};

struct Pack
{
    ST::string                              age;
    std::unique_ptr<uint8_t, IFree>         data; // the whole file (plWGPUPipeline.js malloc); entries point into it
    size_t                                  size = 0;
    std::map<Key, plWGPUTexturePack::Entry> entries;
};

// One Age's pack state: from the fetch's start (plAgeLoader::UpdateAge's prefetch or the Age's first room) until
// another Age's pack starts, or the setting turns off. The pack is verified (IAdvance's checks, in the pack's
// order) as its bytes arrive, a few ms per frame (Pump), and taken into use only at the Age's first room, before any
// of its surfaces is drawn: held = HD for the visit, shipped = the shipped textures for the visit (a failed or late
// fetch, or the setting off at a room). A shipped visit never turns HD; the next join of the Age tries again.
// The bytes come from the pack's Cache Storage entry when its server confirms the entry is current (the game
// files' persisted chunks are likewise revalidated online every session), else from the network; either way every
// byte is checked here. A cached pack that fails a check is evicted and fetched
// again from the network, once. A pack verified from the network is then kept in Cache Storage for later sessions.
enum class State
{
    kNone,
    kFetching,
    kHeld,
    kShipped
};

struct Session
{
    ST::string                            age;
    ST::string                            url; // <base>/<Age>.hdpack
    State                                 state = State::kNone;
    FetchBlock*                           fetch = nullptr;     // while kFetching
    bool                                  sourceKnown = false; // the fetch block's source has been read (once its buffer is known)
    bool                                  fromCache = false;   // the bytes come from the pack's Cache Storage entry
    StoreBlock*                           store = nullptr;     // while kHeld, until the Cache Storage write ends
    double                                storeT0 = 0;
    double                                t0 = 0;         // the fetch's start (emscripten_get_now)
    double                                fetchedMs = -1; // when this side first saw the fetch complete, from t0
    double                                cpuMs = 0;      // client-thread time spent verifying
    bool                                  used = false;   // a room of the Age has been prepared with this pack held
    int                                   phase = 0;      // 0 header, 1 entries, 2 data hash, 3 level-0 copies, 4 verified
    size_t                                entriesAt = 0, blobAt = 0;
    uint32_t                              count = 0;
    uint8_t                               blobSha[32] = {};
    std::unique_ptr<plChecksum>           sha;
    size_t                                hashed = 0;       // absolute offset the data hash has reached
    uint32_t                              seen = 0;         // the bytes written when IAdvance last looked (what the wait waits past)
    std::vector<plWGPUTexturePack::Entry> list;             // the entries in pack order
    uint32_t                              bad = UINT32_MAX; // the first malformed entry, reported in entry order (after the data hash)
    ST::string                            badWhy;
    uint32_t                              next = 0; // the next entry whose level-0 copy is checked
    Pack                                  pack;
};

Session              s_session;
std::set<Key>        s_refused;            // refusals are cached until the pack reloads; matches never are
bool                 s_forcedOff = false;  // off for the session (ForceOff; test hooks runs that record rows only)
bool                 s_configRead = false; // the deployment's two variables below have been read
std::set<ST::string> s_ages;               // the deployment's Ages with a pack (PLASMA_TEXTURES_HDPACK_AGES)
std::string          s_base;               // PLASMA_TEXTURES_HDPACK_URL
ST::string           s_offAge;             // the Age last prepared with the setting off (its log line once)

Key IKey(uint32_t seq, uint16_t flags, uint32_t id)
{
    return Key(seq, uint16_t(flags & ~plLocation::kItinerant), id);
}

// The deployment's pack location and Ages, read once, when an Age first joins or a room first loads.
void IReadConfig()
{
    if (s_configRead)
        return;
    s_configRead = true;
    const char* url = std::getenv("PLASMA_TEXTURES_HDPACK_URL");
    s_base = url ? url : "";
    if (const char* ages = std::getenv("PLASMA_TEXTURES_HDPACK_AGES")) {
        std::string list = ages;
        size_t      pos = 0;
        while (pos <= list.size()) {
            size_t end = list.find(',', pos);
            if (end == std::string::npos)
                end = list.size();
            if (end > pos)
                s_ages.insert(ST::string(list.substr(pos, end - pos).c_str()));
            pos = end + 1;
        }
    }
}

// The profile toggle: Init/webtextures.txt, "HDTextures 1" (written by the page; absent means off). Not an .ini: the
// client runs every Init/*.ini as a console file (the plClient constructor), which has no such command.
bool IToggleOn()
{
    plFileName path = plFileName::Join(plFileSystem::GetInitPath(), "webtextures.txt");
    FILE*      f = std::fopen(path.AsString().c_str(), "rb");
    if (!f)
        return false;
    char   buf[256] = {};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = 0;
    for (char* line = std::strtok(buf, "\r\n"); line; line = std::strtok(nullptr, "\r\n")) {
        int v = 0;
        if (std::sscanf(line, "HDTextures %d", &v) == 1)
            return v != 0;
    }
    return false;
}

ST::string IListed(const ST::string& age)
{
    // Age names compare case-insensitively (config.mjs refuses a list with case-only duplicates)
    for (const ST::string& a : s_ages)
        if (a.compare_i(age) == 0)
            return a;
    return {};
}

template <typename T>
bool IRead(const uint8_t* b, size_t n, size_t& at, T& v)
{
    if (at + sizeof(T) > n)
        return false;
    std::memcpy(&v, b + at, sizeof(T));
    at += sizeof(T);
    return true;
}

uint32_t IBCLevelBytes(uint32_t w, uint32_t h, uint8_t dxt)
{
    return std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * (dxt == 1 ? 8u : 16u);
}

const char* IFetchWhy(int32_t code)
{
    switch (code) {
        case -3:
            return "refused (not the page's origin or a pinned asset origin)";
        case -4:
            return "larger than the client accepts";
        case -5:
            return "too large for the client's memory";
        default:
            return "missing or unreadable";
    }
}

// Ends the fetch: abandons it while in flight (the JS side then frees its memory), else frees the block and, unless
// the pack took it, the buffer.
void IEndFetch(Session& s)
{
    if (!s.fetch)
        return;
    int32_t expected = 0;
    if (!__atomic_compare_exchange_n(&s.fetch->state, &expected, 2, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (!s.pack.data)
            std::free(reinterpret_cast<void*>(uintptr_t(__atomic_load_n(&s.fetch->data, __ATOMIC_ACQUIRE))));
        std::free(s.fetch);
    }
    s.fetch = nullptr;
}

// Ends a Cache Storage write still running. Returns true when the JS side took the pack's buffer (it frees
// the buffer and the block when its write ends); else the block is freed here and the buffer stays the pack's.
bool IEndStore(Session& s)
{
    if (!s.store)
        return false;
    int32_t    expected = 0;
    const bool handed = __atomic_compare_exchange_n(&s.store->state, &expected, 2, false, __ATOMIC_ACQ_REL,
                                                    __ATOMIC_ACQUIRE);
    if (!handed)
        std::free(s.store);
    s.store = nullptr;
    return handed;
}

void IRelease()
{
    IEndFetch(s_session);
    if (IEndStore(s_session))
        (void)s_session.pack.data.release(); // the JS side frees it when its Cache Storage write ends
    s_session = Session();
    s_refused.clear();
}

void IShipped(Session& s, const ST::string& why)
{
    plWGPULog("texture pack {}: pack {} ({}); the shipped textures are used", s.age, why, s_base);
    IEndFetch(s);
    const ST::string age = s.age;
    s = Session();
    s.age = age;
    s.state = State::kShipped;
    s_refused.clear();
}

// Starts the session's fetch (mode: plWGPUTexturePack_fetch's); on a refusal the visit uses the shipped textures.
void IFetch(Session& s, int mode)
{
    auto* f = static_cast<FetchBlock*>(std::calloc(1, sizeof(FetchBlock)));
    if (!f) {
        IShipped(s, IFetchWhy(-5));
        return;
    }
    const int rc = plWGPUTexturePack_fetch(s.url.c_str(), f, kMaxPackBytes, mode);
    if (rc != 0) {
        std::free(f);
        IShipped(s, IFetchWhy(rc));
        return;
    }
    s.fetch = f;
}

void IStart(const ST::string& age, bool prefetched)
{
    IRelease();
    Session& s = s_session;
    s.age = age;
    s.url = ST::format("{}/{}.hdpack", s_base, age);
    s.state = State::kFetching;
    s.t0 = emscripten_get_now();
    IFetch(s, 0);
    if (s.state == State::kFetching)
        plWGPULog("texture pack {}: fetch started ({})", age,
                  prefetched ? "prefetch at plAgeLoader::UpdateAge" : "at the Age's first room");
}

// A pack read from its Cache Storage entry failed a check, or the entry could not be read to the end: the
// entry is evicted and the pack fetched again from the network, once (mode 1), logged once. The visit's timing
// (t0, the client-thread time) carries over. Returns true when the session left kFetching (the refetch was refused).
bool IRefetch(Session& s, const ST::string& why)
{
    plWGPULog(
        "texture pack {}: cached pack defective ({}; {}); its Cache Storage entry is evicted and the pack fetched "
        "from the network",
        s.age, why, s.url);
    IEndFetch(s);
    const ST::string age = s.age, url = s.url;
    const double     t0 = s.t0, cpuMs = s.cpuMs;
    s = Session();
    s.age = age;
    s.url = url;
    s.state = State::kFetching;
    s.t0 = t0;
    s.cpuMs = cpuMs;
    IFetch(s, 1);
    return s.state != State::kFetching;
}

// Keeps a pack verified from the network in Cache Storage. The JS side reads the held buffer in slices
// (no copy of the pack); Pump reports the result.
void IKeep(Session& s)
{
    auto* sb = static_cast<StoreBlock*>(std::calloc(1, sizeof(StoreBlock)));
    if (!sb)
        return;
    sb->data = uint32_t(uintptr_t(s.pack.data.get()));
    sb->size = uint32_t(s.pack.size);
    std::memcpy(sb->sha, s.blobSha, 32);
    s.storeT0 = emscripten_get_now();
    const int rc = plWGPUTexturePack_store(s.url.c_str(), sb);
    if (rc != 0) {
        const uint32_t cap = sb->cap;
        std::free(sb);
        if (rc == -3) // one line per visit, like every other outcome of keeping
            plWGPULog(
                "texture pack {}: not kept between sessions ({} bytes, more than the kept packs' cap of {} "
                "bytes)",
                s.age, s.pack.size, cap);
        else
            plWGPULog("texture pack {}: not kept between sessions ({})", s.age,
                      rc == -2 ? "its server sent no strong ETag to revalidate an entry with" : "Cache Storage is unavailable");
        return;
    }
    s.store = sb;
}

void IPollStore(Session& s)
{
    const int32_t st = __atomic_load_n(&s.store->state, __ATOMIC_ACQUIRE);
    if (st == 0)
        return;
    // The kept packs' cap: the packs this write evicted, least recently used first
    const ST::string evicted = s.store->evicted
                                   ? ST::format("; {} kept pack(s), {} bytes, evicted as the least recently used for the kept packs' cap of {} bytes",
                                                s.store->evicted, s.store->evictedBytes, s.store->cap)
                                   : ST::string();
    if (st == 1)
        plWGPULog("texture pack {}: kept in Cache Storage for later sessions ({} bytes, {.1f} ms{})", s.age,
                  s.pack.size, emscripten_get_now() - s.storeT0, evicted);
    else if (st == -2)
        plWGPULog(
            "texture pack {}: not kept between sessions (written, then evicted as the least recently used pack for "
            "the kept packs' cap of {} bytes{})",
            s.age, s.store->cap, evicted);
    else
        plWGPULog("texture pack {}: not kept between sessions (the Cache Storage write failed)", s.age);
    std::free(s.store);
    s.store = nullptr;
}

// The pack layout (Docs/Web/HD texture pack format.md), checked in its order as the bytes
// arrive: magic, header, entry table, data SHA-256, then per entry its fields and its
// shipped level-0 copy. Returns false while it needs more bytes or time, or once the session left kFetching (held, or
// shipped on any defect). budgetMs < 0: no time budget.
bool IAdvance(Session& s, double budgetMs)
{
    const double   t0 = emscripten_get_now();
    FetchBlock*    f = s.fetch;
    const int32_t  st = __atomic_load_n(&f->state, __ATOMIC_ACQUIRE);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(uintptr_t(__atomic_load_n(&f->data, __ATOMIC_ACQUIRE)));
    if (b && !s.sourceKnown) {
        // The JS side writes the source (and an entry's key SHA-256) before it publishes the buffer
        s.fromCache = __atomic_load_n(&f->source, __ATOMIC_ACQUIRE) == 1;
        s.sourceKnown = true;
    }
    if (st < 0) {
        if (s.fromCache)
            return IRefetch(s, "its entry could not be read to the end");
        IShipped(s, IFetchWhy(st));
        return true;
    }
    if (st == 1 && s.fetchedMs < 0) {
        s.fetchedMs = t0 - s.t0;
        plWGPULog("texture pack {}: fetched {} bytes in {.1f} ms ({})", s.age,
                  __atomic_load_n(&f->size, __ATOMIC_ACQUIRE), s.fetchedMs,
                  s.fromCache ? "from its Cache Storage entry, revalidated" : "from the network");
    }
    const size_t have = __atomic_load_n(&f->received, __ATOMIC_ACQUIRE); // the bytes below it are written
    const size_t n = __atomic_load_n(&f->size, __ATOMIC_ACQUIRE);
    const bool   all = st == 1; // then have == n
    s.seen = uint32_t(have);
    if (!b) {
        s.cpuMs += emscripten_get_now() - t0;
        return false; // the response's headers have not arrived
    }
    auto over = [&] { return budgetMs >= 0 && emscripten_get_now() - t0 >= budgetMs; };
    auto fail = [&](const ST::string& why) {
        s.cpuMs += emscripten_get_now() - t0;
        if (s.fromCache)
            return IRefetch(s, why);
        IShipped(s, why);
        return true;
    };
    if (s.phase == 0) {
        if (!all && have < std::min<size_t>(n, 24))
            return false;
        if (n < 8 || std::memcmp(b, "PLHDPK01", 8) != 0)
            return fail("bad magic");
        size_t   at = 8;
        uint32_t version = 0, textLen = 0, entrySize = 0;
        if (!IRead(b, n, at, version) || !IRead(b, n, at, s.count) || !IRead(b, n, at, textLen) ||
            !IRead(b, n, at, entrySize) || version != 1 || entrySize != 160 || at + textLen + 32 > n)
            return fail("bad header");
        s.entriesAt = at + textLen + 32;
        s.blobAt = s.entriesAt + size_t(s.count) * entrySize;
        if (s.blobAt > n)
            return fail("entries past the end");
        s.phase = 1;
    }
    if (s.phase == 1) {
        if (!all && have < s.blobAt)
            return false;
        std::memcpy(s.blobSha, b + s.entriesAt - 32, 32);
        if (s.fromCache && std::memcmp(s.blobSha, f->sha, 32) != 0)
            return fail("its data SHA-256 is not the one in its Cache Storage key");
        for (uint32_t i = 0; i < s.count; i++) {
            size_t                   at = s.entriesAt + size_t(i) * 160;
            plWGPUTexturePack::Entry e = {};
            uint8_t                  pad8;
            uint16_t                 pad16;
            uint64_t                 offset = 0;
            uint32_t                 size = 0;
            bool                     ok = IRead(b, n, at, e.locSequence) && IRead(b, n, at, e.locFlags) && IRead(b, n, at, e.classType) &&
                      IRead(b, n, at, e.objectId) && IRead(b, n, at, e.compression) && IRead(b, n, at, e.dxtType) &&
                      IRead(b, n, at, e.pixelSize) && IRead(b, n, at, e.rule) && IRead(b, n, at, e.flags) &&
                      IRead(b, n, at, pad16) && IRead(b, n, at, e.width) && IRead(b, n, at, e.height) &&
                      IRead(b, n, at, e.numLevels) && IRead(b, n, at, pad8) && IRead(b, n, at, pad8) &&
                      IRead(b, n, at, pad8) && IRead(b, n, at, e.totalSize);
            ok = ok && at + 96 <= n;
            if (ok) {
                std::memcpy(e.shaAll, b + at, 32);
                std::memcpy(e.shaFrom1, b + at + 32, 32);
                std::memcpy(e.shaLevel0, b + at + 64, 32);
                at += 96;
            }
            ok = ok && IRead(b, n, at, e.g) && IRead(b, n, at, e.tail) && IRead(b, n, at, pad16) &&
                 IRead(b, n, at, e.hdWidth) && IRead(b, n, at, e.hdHeight) && IRead(b, n, at, e.packLevels) &&
                 IRead(b, n, at, pad8) && IRead(b, n, at, pad8) && IRead(b, n, at, pad8) && IRead(b, n, at, offset) &&
                 IRead(b, n, at, size);
            if (!ok || (e.dxtType != 1 && e.dxtType != 5) || e.compression != 1 || s.blobAt + offset + size > n ||
                e.g > 12 || (e.tail == 0 && e.packLevels != e.g + 1)) {
                s.bad = i;
                s.badWhy = ST::format("entry {} malformed", i);
                break;
            }
            size_t pos = s.blobAt + offset;
            for (uint32_t k = 0; k < e.packLevels && s.bad == UINT32_MAX; k++) {
                const bool     shippedCopy = e.tail == 0 && k == e.g;
                const uint32_t w = shippedCopy ? e.width : std::max(1u, e.hdWidth >> k);
                const uint32_t h = shippedCopy ? e.height : std::max(1u, e.hdHeight >> k);
                const uint32_t lb = IBCLevelBytes(w, h, e.dxtType);
                if (pos + lb > s.blobAt + offset + size) {
                    s.bad = i;
                    s.badWhy = ST::format("entry {} level {} past its data", i, k);
                    break;
                }
                e.levelData.push_back(b + pos);
                e.levelSize.push_back(lb);
                pos += lb;
            }
            if (s.bad != UINT32_MAX)
                break;
            s.list.push_back(std::move(e));
        }
        s.sha = std::make_unique<plChecksum>(plChecksum::Type::kSHA256);
        s.sha->Start();
        s.hashed = s.blobAt;
        s.phase = 2;
    }
    if (s.phase == 2) {
        while (s.hashed < have && !over()) {
            const size_t step = std::min(kHashStep, have - s.hashed);
            s.sha->AddTo(step, b + s.hashed);
            s.hashed += step;
        }
        if (s.hashed < n) {
            s.cpuMs += emscripten_get_now() - t0;
            return false;
        }
        s.sha->Finish();
        if (std::memcmp(s.sha->GetValue(), s.blobSha, 32) != 0)
            return fail("data SHA-256 mismatch");
        s.sha.reset();
        s.phase = 3;
    }
    if (s.phase == 3) {
        for (; s.next < s.list.size(); s.next++) {
            if (over()) {
                s.cpuMs += emscripten_get_now() - t0;
                return false;
            }
            const plWGPUTexturePack::Entry& e = s.list[s.next];
            if (e.tail == 0) {
                // The verbatim copy of shipped level 0, checked against the PRP's level-0 hash from build time
                plChecksum l0(plChecksum::Type::kSHA256, e.levelSize.back(), e.levelData.back());
                if (std::memcmp(l0.GetValue(), e.shaLevel0, 32) != 0)
                    return fail(ST::format("entry {} shipped level-0 copy does not match its hash", s.next));
            }
        }
        if (s.bad != UINT32_MAX)
            return fail(s.badWhy);
        s.phase = 4;
    }
    s.cpuMs += emscripten_get_now() - t0;
    if (!all)
        return false; // verified with every byte present, but the JS side still holds the block until it ends
    s.pack.age = s.age;
    s.pack.data.reset(const_cast<uint8_t*>(b));
    s.pack.size = n;
    for (plWGPUTexturePack::Entry& e : s.list)
        s.pack.entries.emplace(IKey(e.locSequence, e.locFlags, e.objectId), std::move(e));
    s.list.clear();
    IEndFetch(s);
    s.state = State::kHeld;
    s_refused.clear();
    plWGPULog(
        "texture pack {}: {} keys, {} bytes, loaded in {.1f} ms, {.1f} ms of it verifying on the client thread "
        "(licence: CC BY-NC-SA 4.0, Cyan Worlds)",
        s.age, s.pack.entries.size(), s.pack.size,
        emscripten_get_now() - s.t0, s.cpuMs);
    if (!s.fromCache)
        IKeep(s); // an entry read from Cache Storage is kept as it is
    return true;
}
} // namespace

void plWGPUTexturePack::PrefetchAge(const ST::string& ageName)
{
    if (s_forcedOff)
        return;
    IReadConfig();
    if (s_base.empty())
        return;
    const ST::string age = IListed(ageName);
    if (age.empty() || !IToggleOn())
        return;
    if (s_session.age == age && (s_session.state == State::kHeld || s_session.state == State::kFetching))
        return; // held from an earlier visit, or already on its way
    IStart(age, true);
}

void plWGPUTexturePack::Pump()
{
    if (s_session.state == State::kFetching)
        IAdvance(s_session, kPumpMs);
    if (s_session.store)
        IPollStore(s_session); // the Cache Storage write's result, once
}

void plWGPUTexturePack::PrepareRoom(const plLocation& loc)
{
    if (s_forcedOff)
        return;
    IReadConfig();
    const plPageInfo* info = plKeyFinder::Instance().GetLocationInfo(loc);
    if (!info || s_base.empty())
        return;
    const ST::string age = IListed(info->GetAge());
    if (age.empty())
        return;
    if (!IToggleOn()) {
        if (s_session.state == State::kHeld)
            plWGPULog("texture pack off (profile setting): {} pack released", s_session.age);
        if (s_offAge != age)
            plWGPULog("texture pack {}: off (profile setting); the shipped textures are used", age);
        s_offAge = age;
        IRelease();
        s_session.age = age;
        s_session.state = State::kShipped; // the setting turning on later in this visit applies at the next one
        return;
    }
    s_offAge.clear();
    Session& s = s_session;
    if (s.age == age && s.state == State::kHeld) {
        if (!s.used)
            plWGPULog(
                "texture pack {}: in use from the first room {} (verified before it; fetch started {.1f} ms "
                "before)",
                age, info->GetPage(), emscripten_get_now() - s.t0);
        s.used = true;
        return;
    }
    if (s.age == age && s.state == State::kShipped)
        return; // decided at this Age's first room (or its fetch failed): no switch within the visit
    if (s.age != age || s.state != State::kFetching)
        IStart(age, false);
    if (s.state != State::kFetching)
        return;
    // The Age's first room: the pack is used only if it is verified now, after a bounded wait; else the shipped
    // textures for the whole visit. Nothing of this Age has been drawn yet (this precedes its first PageInRoom).
    // The bound applies to the time spent waiting for bytes; verifying bytes that have arrived is not cut short.
    const double w0 = emscripten_get_now();
    double       waited = 0;
    while (!IAdvance(s, -1)) {
        if (__atomic_load_n(&s.fetch->state, __ATOMIC_ACQUIRE) != 0)
            continue; // it ended since IAdvance looked
        if (waited >= kWaitMs) {
            IShipped(s, ST::format("not here after a {.0f} ms wait at the Age's first room ({.0f} ms after its fetch "
                                   "started)",
                                   kWaitMs, emscripten_get_now() - s.t0));
            return;
        }
        const double t = emscripten_get_now();
        emscripten_atomic_wait_u32(&s.fetch->received, s.seen, int64_t(std::min(kWaitMs - waited, 50.0) * 1e6));
        waited += emscripten_get_now() - t;
    }
    if (s.state == State::kHeld) {
        s.used = true;
        plWGPULog(
            "texture pack {}: in use from the first room {} ({.1f} ms there, {.1f} ms of it waiting for bytes; fetch "
            "started {.1f} ms before)",
            age, info->GetPage(), emscripten_get_now() - w0, waited, w0 - s.t0);
    }
}

const plWGPUTexturePack::Entry* plWGPUTexturePack::Find(const plUoid& uoid)
{
    const Pack& pack = s_session.pack;
    if (s_session.state != State::kHeld || pack.entries.empty())
        return nullptr;
    const plLocation& loc = uoid.GetLocation();
    const Key         k = IKey(loc.GetSequenceNumber(), loc.GetFlags(), uoid.GetObjectID());
    if (s_refused.count(k))
        return nullptr;
    auto it = pack.entries.find(k);
    if (it == pack.entries.end() || it->second.classType != uoid.GetClassType())
        return nullptr;
    return &it->second;
}

void plWGPUTexturePack::Refuse(const Entry* e, const char* why)
{
    fRefused++;
    s_refused.insert(IKey(e->locSequence, e->locFlags, e->objectId));
    plWGPULog("texture pack refused {}:{} id {}: {}; the shipped texture is used", e->locSequence, e->locFlags,
              e->objectId, why);
}

bool plWGPUTexturePack::Active()
{
    return !s_forcedOff && s_session.state == State::kHeld && !s_session.pack.entries.empty();
}

#ifdef PLASMA_TEST_HOOKS
void plWGPUTexturePack::ForceOff(const ST::string& switchName)
{
    // Before any Age joins: nothing is held or on its way yet, so nothing is released here.
    if (s_forcedOff)
        return;
    s_forcedOff = true;
    plWGPULog("texture pack forced off for this session: test switch {} is set", switchName);
}
#endif
