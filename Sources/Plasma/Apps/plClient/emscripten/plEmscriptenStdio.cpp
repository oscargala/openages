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

// stdio wrappers (-Wl,--wrap=fopen,--wrap=fclose,--wrap=fseeko,--wrap=fseek,--wrap=ungetc,--wrap=ungetwc) and a
// descriptor read wrapper (-Wl,--wrap=__wasi_fd_read) for four deliberate differences from the native client, kept here
// so that no engine file changes for them.
//
// A larger buffer for the game data. Each refill of a FILE's buffer costs proxied WasmFS backend reads: musl reads the
// caller's part and the buffer in one two-element readv (musl __stdio_read.c), and WasmFS reads each iovec as its own
// backend read, a hop to the fetch backend's proxy worker. With musl's 1 KiB BUFSIZ an Age load makes one backend read
// for about every twenty freads. A FILE opened read-only on an immutable fetch asset (plEmscriptenCwd.cpp) gets a fully
// buffered 64 KiB buffer before its first I/O (setvbuf). Natively hsUNIXStream::Open and plFileSystem::Open only call
// fopen; setting it there would change every platform for a cost only the browser has. The bytes each read returns are
// the same; other files keep musl's buffer.
//
// Seeks within that buffer. musl's fseek drops the buffer on every call (__fseeko_unlocked, src/stdio/fseek.c:28), so
// each seek and read refills 64 KiB. libwebm's MkvReader, which plMoviePlayer reads its movie through (IOpenMovie, ILoadAudio), seeks
// before each of its reads: 165,549 of them for avi/NewPlayerIntro.webm, so 331,098 backend reads and 10 GiB, one hop
// to the OPFS worker each in patcher mode. The native C libraries keep the buffer when the target lies inside it (macOS
// libc, FreeBSD's _fseeko, glibc's _IO_file_seekoff), and so do these wrappers for a game-data FILE: a SEEK_SET or
// SEEK_CUR to a byte the buffer holds moves musl's read position there, and every other seek is musl's. musl has no
// call that moves the read position back, so the wrappers read musl's FILE through a mirror of the pinned release's
// layout (src/internal/stdio_impl.h:27-44), which this file compiles only against (PLASMA_EMSCRIPTEN_PINNED_*), and
// check the mirror against each FILE at fopen; a FILE that fails the check keeps musl's seek. musl's ungetc and
// ungetwc write the pushed-back bytes into the buffer itself (glibc and BSD keep them apart), so those are wrapped too:
// a FILE that may hold a byte that is not the file's takes musl's seek until musl has dropped its buffer. Every byte,
// result, ftell, feof and ferror is musl's own. Like any stdio buffer, this assumes that nothing moves a game-data
// FILE's descriptor outside stdio: Sources/Plasma uses fileno only for flock, for ftruncate on files opened for
// writing, and for the immutable-asset check below.
//
// No half-written download. A FILE opened for writing in the patcher-mode install directory whose writes failed
// (ferror, or fclose's flush: OPFS returns EIO when the origin's quota is exhausted) is removed at fclose and noted
// for its path (plEmscriptenStorage::NoteWriteFailure), and pfPatcher.cpp fails that download for storage
// (pfPatcherWorker::IFileThingDownloadCB). Natively the partial file stays, and the next patch of its manifest hashes it
// and downloads it again (pfPatcher.cpp IHashFile), as it does a missing one; in the browser a full quota is the usual
// cause, and the partial file would only hold quota the next download needs.
//
// A block cache under the game-data FILEs. A page-in reads a page's objects through one FILE
// (plResManager::PageInRoom, plResManager.cpp:1096-1220), seeking to each (ReadObject, :313-353), and musl's fseek
// drops the buffer, so the same bytes are read again and again: a Neighborhood load makes about 19,400 backend reads
// for 650 MiB. Natively the OS page cache serves those reads; here each is a hop to the backend's proxy worker. The
// wrapped __wasi_fd_read, musl's one read call (__stdio_read.c:14; read(2) and readv(2) make it too), serves the
// descriptor of a game-data FILE from 64 KiB aligned blocks of its file, kept for the FILE's lifetime (its fopen to its
// fclose) in one process-wide LRU of 16 MiB; any other descriptor's call is the original's. Each result is the one the
// original computes (readAtOffset, system/lib/wasmfs/syscalls.cpp:184-256): from the descriptor's offset (SEEK_CUR, no
// backend call: __wasi_fd_seek, syscalls.cpp:688-730), the iovecs filled in order, a zero-length iovec not short
// (readAtOffset, syscalls.cpp:246), a stop at the first short one, nread the total, and the offset moved to offset +
// nread (SEEK_SET: readAtOffset, syscalls.cpp:252-254). So every stdio result is musl's own from the same values
// (__stdio_read, __stdio_read.c:22-31), and so is the window the seeks above rely on.
//   - Misses. Each maximal run of missing blocks in the call's span is one single-iovec __wasi_fd_pread
//     (syscalls.cpp:283-289). When the runs outnumber the backend reads the original would make (one per iovec up to
//     the first short one, readAtOffset's loop, syscalls.cpp:221-251, over the file's size: its EOF if known, else the
//     bytes this lifetime's reads have shown, so never above the real count), one pread of the whole aligned span
//     instead: no call makes more backend reads than the original. A pread that comes back short fixes the file's EOF,
//     and a call at or past it makes no backend read.
//   - Room before the reads. Least recently used blocks outside the call's span are evicted until the cache's bytes,
//     the preads' buffers counted, fit in the cap (the buffers: whole blocks, clipped at a known EOF, and one block
//     more when a run of two blocks or more is split into its blocks, a copy each but the first: dlmalloc's in-place
//     shrink, try_realloc_chunk, dlmalloc.c:4915-4923). So the cache's bytes never exceed the cap, and since dlmalloc
//     never returns memory (HAVE_MMAP, dlmalloc.c:8; MORECORE_CANNOT_TRIM, dlmalloc.c:12) the cap is also the cache's
//     whole rise of the heap.
//   - The original call, with the original arguments, its result returned as it is, serving, inserting and evicting
//     nothing: for a call whose iovecs ask more than a quarter of the cap (4 MiB); for one the cap has no room for
//     because other calls have pinned the blocks they copy out of; for a null buffer with a length (the original's
//     EINVAL: readAtOffset, syscalls.cpp:225-226); for every call of a poisoned descriptor (below); and after a failed
//     pread, which inserts nothing and is redone as the original call from the original offset, so no bytes are served
//     ahead of an error (a short read would change what fread returns: __stdio_read, __stdio_read.c:26). A call whose
//     iovecs are all zero-length is nread 0 and success, as the original's zero-length backend reads give.
//   - Locks. A call holds its descriptor's record mutex from its offset to its new offset, preads included, as
//     readAtOffset holds the open file's lock and the file's (lockedOpenFile, syscalls.cpp:195; lockedFile,
//     syscalls.cpp:218); the LRU's lock is held for bookkeeping only (look-up, pin, eviction, reservation, insertion),
//     never across a backend read or a copy; blocks a call copies out of are pinned, and eviction skips them. No FILE
//     lock is taken: the call comes both under musl's (FLOCK, fread.c:12) and without it.
//   - Coherence. The cache is as fresh as the backend, which serves a read-only open from a snapshot until a
//     write-capable open of the same file (`OpenState::open`, opfs_backend.cpp:71-94). So a write-capable fopen in the
//     install directory (below) poisons each open cached reader of the same WasmFS file (plEmscriptenCwd::FileIdentity:
//     the file table's File, one per name, so not the path): its blocks are freed and its calls are the original's
//     until its fclose; a reader opened while such a FILE is open opens poisoned. Writers outside fopen are assumed not
//     to write a file a game-data FILE has open: main.cpp's package install (open(2): InstallPackageTree,
//     main.cpp:363-366) runs before the client; Python 3's io (open(2)) writes only debug commands' files
//     (SaveAvatarToDisk, xAvatarCustomization.py:384; SaveColumns, jlakField.py:765; DumpMarkers, xCheat.py:575), which
//     those scripts read back through io; Python 2's file and ptStream open through fopen. One divergence is named:
//     under the page's own OPFS writes (boot.mjs) a snapshot read fails with EIO (_wasmfs_opfs_read_blob,
//     libwasmfs_opfs.js:371-378), where a call the cache serves succeeds with the snapshot's bytes; no game-data FILE
//     is open on those files.
//
// A harness build (PLASMA_TEST_HOOKS) also counts each game-data FILE's I/O: its seeks, those served in the buffer,
// its taints, the WasmFS backend reads made under it (the preads and the original calls) and the cache's counts,
// printed as one "[stdio]" line when the FILE is closed: the bytes served from the cache and in how many calls, the
// preads (whole-span ones too), the calls passed to the original, the evictions of its blocks, the blocks read again
// after their eviction, whether it was poisoned, and the cache's peak bytes so far against the cap. The counters never
// change the I/O; the product build has none of them. Its test seams (plEmscriptenStdio.h) lower the cap and stand in
// for the pread.
//
// With PLASMA_TEST_STDIO_TRACE=1 a harness build also writes the trace of that I/O to stdio-trace.txt in the client's
// log folder (plEmscriptenStorage::LogFolder), which the harness's exit export hands to the runner (client-files/Log).
// One line per event, times in ms since the trace opened (its header gives that instant as an epoch time,
// emscripten_get_now):
//   O <lifetime> <t> <fd> <mirrored 0|1> <path>       a game-data FILE's fopen (the lifetime ends at its C line)
//   C <lifetime> <t> <seeks> <in the buffer> <backend reads> <bytes>   its fclose, with the [stdio] line's counts
//   R <lifetime> <t> <us> <offset> <iovec lengths, comma-separated> <nread or -1> <rc> <backend reads>   a
//                                           __wasi_fd_read under it: the descriptor's offset before the call (SEEK_CUR,
//                                           no backend call: __wasi_fd_seek, syscalls.cpp:705), the call's wall time in
//                                           microseconds, and the backend reads made under it
//   S <lifetime> <t> <whence> <offset> <target or -1> <rc>   a seek musl handled (not one within the buffer): the
//                                           descriptor's offset after it, which musl's seek leaves at the target
//                                           (__fseeko_unlocked, fseek.c:13, :25-28)
//   W <t> <path> / X <t> <path>             the fopen and fclose of a FILE opened for writing in the install directory
// The trace FILE is opened with __real_fopen at the first game-data fopen, where the thread holds no FILE lock (musl's
// fflush of NULL holds the open-file list's lock while it takes each FILE's lock: fflush(), fflush.c:15-20; so opening
// it under a game-data FILE's lock could deadlock), and it is never closed: main.cpp's fflush(nullptr) before the exit
// export writes its last buffer. The trace adds no I/O to the game-data FILEs and changes none of their results.

#include <emscripten/version.h>
#include <stdio_ext.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef PLASMA_TEST_HOOKS
#include <emscripten/emscripten.h>

#include <cstdarg>
#include <unordered_set>
#endif

#include "plEmscriptenCwd.h"
#include "plEmscriptenStdio.h"
#include "plEmscriptenStorage.h"

// The pinned release is web/build/sources.lock's (plClient CMakeLists.txt defines its version here); its emsdk record
// lists what to revalidate, this file's musl FILE mirror among them, before the pin changes.
#if !defined(PLASMA_EMSCRIPTEN_PINNED_MAJOR) || __EMSCRIPTEN_MAJOR__ != PLASMA_EMSCRIPTEN_PINNED_MAJOR || \
    __EMSCRIPTEN_MINOR__ != PLASMA_EMSCRIPTEN_PINNED_MINOR || __EMSCRIPTEN_TINY__ != PLASMA_EMSCRIPTEN_PINNED_TINY
#error "plEmscriptenStdio.cpp mirrors musl's FILE: it compiles only with the release web/build/sources.lock pins"
#endif

extern "C" FILE*  __real_fopen(const char* path, const char* mode);
extern "C" int    __real_fclose(FILE* file);
extern "C" int    __real_fseeko(FILE* file, off_t offset, int whence);
extern "C" int    __real_fseek(FILE* file, long offset, int whence);
extern "C" int    __real_ungetc(int c, FILE* file);
extern "C" wint_t __real_ungetwc(wint_t c, FILE* file);
// musl's FILE lock, which its FLOCK and FUNLOCK take (src/internal/stdio_impl.h:15-16, src/stdio/__lockfile.c)
extern "C" int    __lockfile(FILE* file);
extern "C" void   __unlockfile(FILE* file);

// the original descriptor read (header)
extern "C" __wasi_errno_t __real___wasi_fd_read(__wasi_fd_t fd, const __wasi_iovec_t* iovs, size_t iovs_len,
                                                __wasi_size_t* nread);

namespace
{
constexpr size_t   kGameDataBuffer = 64 * 1024;
// musl's pushback room in front of a setvbuf buffer (stdio_impl.h UNGET; setvbuf.c:16-18)
constexpr size_t   kUnget = 8;
// musl's F_NOWR (stdio_impl.h:21) and F_SVB (:24): __fdopen sets F_NOWR for a read-only mode (__fdopen.c:27), and
// setvbuf sets F_SVB for the caller's buffer (setvbuf.c:26)
constexpr unsigned kReadOnlyUserBuffer = 8 | 64;

// musl's FILE from flags through lock, as the pinned release declares it (src/internal/stdio_impl.h:27-44)
struct MuslFile
{
    unsigned       flags;
    unsigned char *rpos, *rend;
    int (*close)(FILE*);
    unsigned char *wend, *wpos;
    unsigned char* mustbezero_1;
    unsigned char* wbase;
    size_t (*read)(FILE*, unsigned char*, size_t);
    size_t (*write)(FILE*, const unsigned char*, size_t);
    off_t (*seek)(FILE*, off_t, int);
    unsigned char* buf;
    size_t         buf_size;
    FILE *         prev, *next;
    int            fd;
    int            pipe_pid;
    long           lockcount;
    int            mode;
    volatile int   lock;
};

MuslFile* Musl(FILE* file)
{
    return reinterpret_cast<MuslFile*>(file);
}

// The FILE's lock, held as musl's own functions hold it: taken unless the FILE has none or this thread holds it
// already, whether through musl's FLOCK or through flockfile (__lockfile returns 0 to the owner: __lockfile.c:7-9),
// and released only if taken here.
class FileLock
{
public:
    explicit FileLock(FILE* file) : fFile(file), fTaken(Musl(file)->lock >= 0 ? __lockfile(file) : 0) {}
    ~FileLock()
    {
        if (fTaken)
            __unlockfile(fFile);
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

private:
    FILE* fFile;
    int   fTaken;
};

constexpr size_t kBlock = 64 * 1024;           // the cache's aligned blocks (header)
constexpr size_t kCacheCap = 16 * 1024 * 1024; // the product's cap, a constant
// the most blocks a cached call's span covers: its iovecs ask at most a quarter of the cap, so its aligned span covers
// at most that over kBlock, plus 2 (the test seam only lowers the cap)
constexpr size_t kSpanMax = kCacheCap / 4 / kBlock + 2;

struct CacheFile;

// One aligned block of a game-data file (shorter only at the file's end), in the process-wide LRU
struct Block
{
    CacheFile*     owner = nullptr;
    uint64_t       index = 0;
    unsigned char* data = nullptr;
    size_t         bytes = 0;
    unsigned       pins = 0; // the calls copying out of it: eviction skips it
    Block*         older = nullptr;
    Block*         newer = nullptr;
};

// The cache of a game-data FILE's descriptor, from its fopen to its fclose. Shared by the descriptor's entry and each
// call in flight, so an fclose never frees it under a call.
struct CacheFile
{
    std::mutex                           lock; // held by a call from its offset to its new offset (header)
    int                                  fd = -1;
    const void*                          identity = nullptr; // the WasmFS file (plEmscriptenCwd::FileIdentity)
    std::atomic<bool>                    poisoned{false};    // its calls are the original's until its fclose (header)
    // under lock
    bool                                 closed = false; // its FILE's fclose has begun: any later call is the original's
    off_t                                eof = -1;       // the file's size, once a pread came back short
    off_t                                proven = 0;     // the bytes this lifetime's reads have shown to exist
    // under sCacheLock
    std::unordered_map<uint64_t, Block*> blocks;
#ifdef PLASMA_TEST_HOOKS
    // the harness build's counts: under lock, but evictions and evicted under sCacheLock
    uint64_t                     backendReads = 0, zeroLength = 0, bytes = 0;
    uint64_t                     served = 0, servedCalls = 0, preads = 0, spanPreads = 0, passThrough = 0;
    uint64_t                     evictions = 0, reMisses = 0;
    std::unordered_set<uint64_t> evicted;      // blocks evicted and not read again
    uint64_t                     lifetime = 0; // the trace's id of its FILE, 0 with the trace off
#endif
};

// A read-only game-data FILE with the 64 KiB buffer
struct GameDataFile
{
    char*                      buffer = nullptr;
    bool                       mirrored = false; // the fopen check passed: seeks may move the read position within the buffer
    bool                       tainted = false;  // a pushback may have put a byte that is not the file's into the buffer
    std::shared_ptr<CacheFile> cache;
#ifdef PLASMA_TEST_HOOKS
    // the harness build's counts, printed at fclose
    std::string path;
    uint64_t    seeks = 0;
    uint64_t    inBuffer = 0;
    uint64_t    taints = 0;
    uint64_t    lifetime = 0; // the trace's id of this FILE (PLASMA_TEST_STDIO_TRACE), 0 with the trace off
#endif
};

// An install-directory file opened for writing
struct Writer
{
    std::string path;
    const void* identity = nullptr; // its WasmFS file, which it poisons the cached readers of (header)
};

// Lock order: a FILE's lock, then sLock, held briefly; sLock is never held while a FILE lock is taken, nor together
// with a CacheFile's lock or sCacheLock. The cache's own order: a CacheFile's lock, then sCacheLock.
std::mutex                                          sLock;
std::unordered_map<FILE*, GameDataFile>             sBuffers;
std::unordered_map<FILE*, Writer>                   sWritable;
std::unordered_map<int, std::shared_ptr<CacheFile>> sCached; // the descriptors of sBuffers' FILEs

// The LRU of all CacheFiles' blocks (header), under sCacheLock
std::mutex sCacheLock;
Block*     sOldest = nullptr;
Block*     sNewest = nullptr;
size_t     sResident = 0;         // the blocks' bytes and the reservations of the calls reading into the cache
size_t     sCacheCap = kCacheCap; // lowered only by the harness build's test seam
#ifdef PLASMA_TEST_HOOKS
size_t   sPeak = 0;
uint64_t sPoisonings = 0; // under sLock
__wasi_errno_t (*sPreadForTest)(__wasi_fd_t, const __wasi_iovec_t*, size_t, __wasi_filesize_t,
                                __wasi_size_t*) = nullptr;

// The I/O trace (PLASMA_TEST_STDIO_TRACE=1, header). Lock order: a game-data FILE's lock, then sLock, then sTraceLock,
// then the trace FILE's own lock (fwrite); nothing is taken under sTraceLock but the trace FILE's lock, and the trace
// FILE is opened (ITraceOpen) with no other lock held.
std::mutex sTraceLock;
FILE*      sTrace = nullptr;
bool       sTraceFailed = false;
uint64_t   sLifetimes = 0; // under sLock

// Whether the trace is on (read once), and the epoch time (ms) its times count from.
double ITraceOrigin()
{
    static const double origin = [] {
        const char* v = std::getenv("PLASMA_TEST_STDIO_TRACE");
        return v && !std::strcmp(v, "1") ? emscripten_get_now() : -1.0;
    }();
    return origin;
}

bool ITraceOn()
{
    return ITraceOrigin() >= 0.0;
}

double ITraceNow()
{
    return emscripten_get_now() - ITraceOrigin();
}

// At a game-data fopen, with no FILE lock held (header): opens the trace FILE once.
void ITraceOpen()
{
    std::lock_guard<std::mutex> guard(sTraceLock);
    if (sTrace || sTraceFailed)
        return;
    const std::string path = plEmscriptenStorage::LogFolder() + "/stdio-trace.txt";
    sTrace = __real_fopen(path.c_str(), "w");
    if (!sTrace) {
        sTraceFailed = true;
        std::printf("[stdio] trace: %s cannot be opened (errno %d): no trace\n", path.c_str(), errno);
        return;
    }
    // a large buffer, so that the trace writes to the log folder about once per MiB (never freed: the FILE stays open)
    constexpr size_t kTraceBuffer = 1024 * 1024;
    if (char* buffer = static_cast<char*>(std::malloc(kTraceBuffer)))
        setvbuf(sTrace, buffer, _IOFBF, kTraceBuffer);
    std::fprintf(sTrace, "# plEmscriptenStdio trace v2: times in ms since epoch_ms %.3f\n",
                 ITraceOrigin());
    std::printf("[stdio] trace on: %s, epoch_ms %.3f\n", path.c_str(), ITraceOrigin());
}

// One line of the trace (without its newline), if the trace FILE is open.
void ITrace(const char* fmt, ...)
{
    char    line[1536];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n < 0)
        return;
    size_t len = std::min<size_t>(size_t(n), sizeof(line) - 2);
    line[len++] = '\n';
    line[len] = '\0';
    std::lock_guard<std::mutex> guard(sTraceLock);
    if (sTrace)
        std::fwrite(line, 1, len, sTrace);
}

// The descriptor's offset, which lseek reads without a backend call (__wasi_fd_seek, syscalls.cpp:705), errno kept; -1
// if it fails.
long long IOffset(int fd)
{
    const int   error = errno;
    const off_t at = lseek(fd, 0, SEEK_CUR);
    errno = error;
    return (long long)at;
}
#endif

bool ReadOnly(const char* mode)
{
    return mode && mode[0] == 'r' && !std::strchr(mode, '+');
}

// The FILE's record, or null for a FILE without the game-data buffer. The record lives until the FILE's fclose, and
// only a thread holding the FILE's lock changes it.
GameDataFile* IRecord(FILE* file)
{
    std::lock_guard<std::mutex> guard(sLock);
    auto                        it = sBuffers.find(file);
    return it == sBuffers.end() ? nullptr : &it->second;
}

// Whether musl's FILE, just set up by fopen and setvbuf, reads as the pinned layout: the buffer that setvbuf() placed
// after its pushback room (setvbuf.c:16-18; ext.c's __fbufsize), and a read-only FILE with the caller's buffer and no
// I/O yet, as __fdopen() leaves it (__fdopen.c:24-57).
bool IMirrorMatches(FILE* file, const char* buffer)
{
    const MuslFile* f = Musl(file);
    return f->buf == reinterpret_cast<const unsigned char*>(buffer) + kUnget && f->buf_size == kGameDataBuffer - kUnget &&
           __fbufsize(file) == f->buf_size && f->flags == kReadOnlyUserBuffer && !f->rpos && !f->rend && !f->wpos &&
           !f->wbase && !f->wend && f->fd == fileno(file) && f->pipe_pid == 0 && f->lockcount == 0 && f->mode == 0 &&
           (f->lock == 0 || f->lock == -1);
}

void IMirrorFailed(const char* path)
{
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!reported.test_and_set())
        std::fprintf(stderr,
                     "plEmscriptenStdio: musl's FILE does not read as the pinned layout (%s): game-data files "
                     "seek through musl\n",
                     path ? path : "a seek");
}

// Whether the buffer holds file bytes, under the FILE's lock. While buf <= rpos < rend, [buf, rend) are the bytes
// that end at the descriptor's offset: musl reads the descriptor only after __toread's reset (rpos == rend ==
// buf + buf_size; fread.c:27, __uflow.c:9, __toread.c:12), a short or failed read leaves that reset (__stdio_read.c:
// 22-26), a full one sets rpos = buf + 1 and rend = buf + count (__stdio_read.c:28-30), and fseek and fflush drop the
// buffer (rpos = rend = 0). rpos == rend is never a window: a consumed buffer and the reset look the same.
bool IValidWindow(FILE* file, const GameDataFile& record)
{
    const MuslFile* f = Musl(file);
    return record.mirrored && !record.tainted && f->rend && f->buf <= f->rpos && f->rpos < f->rend &&
           f->rend <= f->buf + f->buf_size && f->wpos == f->wbase && !feof(file) && !__fwritable(file);
}

// A seek within the buffer, under the FILE's lock: true when the target is a byte the buffer holds and the read
// position is now there; false leaves the call to musl. F_EOF cannot be set while the window is valid (only
// __stdio_read.c:23 sets it, leaving rpos == rend), so only rpos is written.
bool ISeekInBuffer(FILE* file, GameDataFile& record, off_t offset, int whence)
{
    if ((whence != SEEK_SET && whence != SEEK_CUR) || !IValidWindow(file, record))
        return false;
    MuslFile*      f = Musl(file);
    unsigned char* target;
    if (whence == SEEK_CUR) {
        // relative to the read position, as __ftello_unlocked counts it (ftell.c:13-14)
        if (offset < -off_t(f->rpos - f->buf) || offset >= off_t(f->rend - f->rpos))
            return false;
        target = f->rpos + offset;
    } else {
        // the buffer ends at the descriptor's offset, which lseek reads without a backend call (__wasi_fd_seek,
        // syscalls.cpp:705)
        const int   error = errno;
        const off_t end = lseek(f->fd, 0, SEEK_CUR);
        if (end < 0) {
            errno = error;
            return false;
        }
        const off_t start = end - off_t(f->rend - f->buf);
        if (offset < start || offset >= end)
            return false;
        target = f->buf + (offset - start);
    }
    // musl's own accessor must agree with the mirror (__freadptr, ext2.c:9-13)
    size_t      left = 0;
    const char* at = __freadptr(file, &left);
    if (reinterpret_cast<const unsigned char*>(at) != f->rpos || left != size_t(f->rend - f->rpos)) {
        record.mirrored = false;
        IMirrorFailed(nullptr);
        return false;
    }
    f->rpos = target;
    return true;
}

template <typename Offset>
int ISeek(FILE* file, Offset offset, int whence, int (*real)(FILE*, Offset, int))
{
    FileLock      hold(file);
    GameDataFile* record = IRecord(file);
    if (!record)
        return real(file, offset, whence);
#ifdef PLASMA_TEST_HOOKS
    ++record->seeks;
#endif
    if (ISeekInBuffer(file, *record, off_t(offset), whence)) {
#ifdef PLASMA_TEST_HOOKS
        ++record->inBuffer;
#endif
        return 0;
    }
    const int result = real(file, offset, whence);
    // musl dropped the buffer, and any byte pushed back into it, only if its seek succeeded (__fseeko_unlocked,
    // fseek.c:25, :28)
    if (result == 0)
        record->tainted = false;
#ifdef PLASMA_TEST_HOOKS
    if (record->lifetime)
        ITrace("S %llu %.3f %d %lld %lld %d", (unsigned long long)record->lifetime, ITraceNow(), whence,
               (long long)offset, result == 0 ? IOffset(Musl(file)->fd) : -1LL, result);
#endif
    return result;
}

void ITaint(GameDataFile& record)
{
#ifdef PLASMA_TEST_HOOKS
    if (!record.tainted)
        ++record.taints;
#endif
    record.tainted = true;
}

// The cache (header). Under sCacheLock: a block out of the LRU, or into it as the most recently used.
void IUnlink(Block* block)
{
    (block->older ? block->older->newer : sOldest) = block->newer;
    (block->newer ? block->newer->older : sNewest) = block->older;
    block->older = block->newer = nullptr;
}

void IAppend(Block* block)
{
    block->older = sNewest;
    block->newer = nullptr;
    (sNewest ? sNewest->newer : sOldest) = block;
    sNewest = block;
}

// Under sCacheLock: a block out of its file, the LRU and the count, freed.
void IFree(Block* block)
{
    IUnlink(block);
    block->owner->blocks.erase(block->index);
    sResident -= block->bytes;
    std::free(block->data);
    delete block;
}

// Under sCacheLock: evicts the least recently used unpinned blocks until need more bytes fit in the cap, or evicts
// nothing and returns false when they cannot (the pinned blocks are the calls' in flight, the call's own span among
// them).
bool IMakeRoom(size_t need)
{
    size_t evictable = 0;
    Block* last = nullptr; // the last block to evict
    for (Block* block = sOldest; block && sResident - evictable + need > sCacheCap; block = block->newer)
        if (!block->pins) {
            evictable += block->bytes;
            last = block;
        }
    if (sResident - evictable + need > sCacheCap)
        return false;
    for (Block* block = sOldest; last;) {
        Block* next = block->newer;
        if (!block->pins) {
#ifdef PLASMA_TEST_HOOKS
            ++block->owner->evictions;
            block->owner->evicted.insert(block->index);
#endif
            if (block == last)
                last = nullptr;
            IFree(block);
        }
        block = next;
    }
    return true;
}

// Under sCacheLock: the call's pins taken off.
void IUnpin(Block* const* blocks, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (blocks[i])
            --blocks[i]->pins;
}

// Under the CacheFile's lock (no call of it in flight, so none of its blocks pinned): all its blocks freed.
void IDropBlocks(CacheFile& cache)
{
    std::lock_guard<std::mutex> guard(sCacheLock);
    while (!cache.blocks.empty())
        IFree(cache.blocks.begin()->second);
}

// Under sLock: poisons the open cached readers of the WasmFS file a write-capable fopen has open (header); the caller
// frees their blocks after sLock (IDropPoisoned).
void IPoison(const void* identity, std::vector<std::shared_ptr<CacheFile>>& poisoned)
{
    for (auto& [fd, cache] : sCached)
        if (cache->identity == identity && !cache->poisoned.exchange(true)) {
            poisoned.push_back(cache);
#ifdef PLASMA_TEST_HOOKS
            ++sPoisonings;
#endif
        }
}

void IDropPoisoned(const std::vector<std::shared_ptr<CacheFile>>& poisoned)
{
    for (const auto& cache : poisoned) {
        std::lock_guard<std::mutex> guard(cache->lock);
        IDropBlocks(*cache);
    }
}

// The backend reads the original makes for these iovecs at offset in a file of size bytes: one per iovec, up to and
// including the first short one; a zero-length iovec is not short (readAtOffset, syscalls.cpp:221-251).
size_t IOriginalReads(const __wasi_iovec_t* iovs, size_t iovs_len, off_t offset, off_t size)
{
    size_t reads = 0;
    off_t  at = offset;
    for (size_t i = 0; i < iovs_len; ++i) {
        ++reads;
        const off_t got = std::min<off_t>(off_t(iovs[i].buf_len), std::max<off_t>(size - at, 0));
        at += got;
        if (got < off_t(iovs[i].buf_len))
            break;
    }
    return reads;
}

// One single-iovec pread of the cache (header), or the harness build's test seam in its place.
__wasi_errno_t IPread(int fd, unsigned char* buffer, size_t length, off_t offset, __wasi_size_t* got)
{
    const __wasi_iovec_t iov = {buffer, __wasi_size_t(length)};
#ifdef PLASMA_TEST_HOOKS
    if (sPreadForTest)
        return sPreadForTest(__wasi_fd_t(fd), &iov, 1, __wasi_filesize_t(offset), got);
#endif
    return __wasi_fd_pread(__wasi_fd_t(fd), &iov, 1, __wasi_filesize_t(offset), got);
}

// The original call with the original arguments, its result returned as it is (header). offset is the descriptor's
// before it, or -1 if unknown: a successful call shows the file holds offset + nread bytes.
__wasi_errno_t IOriginal(CacheFile& cache, off_t offset, const __wasi_iovec_t* iovs, size_t iovs_len,
                         __wasi_size_t* nread, size_t& reads)
{
    const __wasi_errno_t rc = __real___wasi_fd_read(__wasi_fd_t(cache.fd), iovs, iovs_len, nread);
#ifdef PLASMA_TEST_HOOKS
    ++cache.passThrough;
#endif
    if (rc != __WASI_ERRNO_SUCCESS) {
        ++reads; // counted as one, as the harness counted it before the cache
        return rc;
    }
    // its backend reads: one per iovec, up to and including the first short one (readAtOffset, syscalls.cpp:221-251)
    size_t left = *nread;
    for (size_t i = 0; i < iovs_len; ++i) {
        ++reads;
#ifdef PLASMA_TEST_HOOKS
        if (!iovs[i].buf_len)
            ++cache.zeroLength;
#endif
        const size_t got = std::min<size_t>(left, iovs[i].buf_len);
        left -= got;
        if (got < iovs[i].buf_len)
            break;
    }
#ifdef PLASMA_TEST_HOOKS
    cache.bytes += *nread;
#endif
    if (offset >= 0)
        cache.proven = std::max<off_t>(cache.proven, offset + off_t(*nread));
    return rc;
}

// A run of missing blocks [first, last) and its pread's buffer
struct Run
{
    uint64_t       first = 0, last = 0;
    unsigned char* data = nullptr;
    size_t         length = 0; // the pread's length
    size_t         got = 0;    // its result
};

// A call of a game-data FILE's descriptor, under its CacheFile's lock (header). offset is set to the descriptor's offset
// before the call when it is known; reads counts the backend reads made under the call.
__wasi_errno_t ICachedRead(CacheFile& cache, const __wasi_iovec_t* iovs, size_t iovs_len, __wasi_size_t* nread,
                           off_t& offset, size_t& reads)
{
    __wasi_filesize_t position = 0;
    if (!nread || __wasi_fd_seek(__wasi_fd_t(cache.fd), 0, __WASI_WHENCE_CUR, &position) != __WASI_ERRNO_SUCCESS)
        return IOriginal(cache, -1, iovs, iovs_len, nread, reads);
    offset = off_t(position);
    size_t want = 0;
    bool   plain = true;
    for (size_t i = 0; i < iovs_len; ++i) {
        if (!iovs[i].buf && iovs[i].buf_len)
            plain = false;
        want = std::min<size_t>(want + iovs[i].buf_len, SIZE_MAX / 2);
    }
    if (!plain || cache.poisoned || cache.closed || want > sCacheCap / 4)
        return IOriginal(cache, offset, iovs, iovs_len, nread, reads);
    if (!want || (cache.eof >= 0 && offset >= cache.eof)) {
        // the original's result without its backend reads: nread 0, and the offset where it is
        *nread = 0;
        return __WASI_ERRNO_SUCCESS;
    }

    const off_t    end = cache.eof >= 0 ? std::min<off_t>(offset + off_t(want), cache.eof) : offset + off_t(want);
    const uint64_t b0 = uint64_t(offset) / kBlock;
    const size_t   count = size_t((uint64_t(end) + kBlock - 1) / kBlock - b0);
    Block*         span[kSpanMax] = {}; // the span's blocks, pinned while the call copies out of them
    Run            runs[kSpanMax];
    size_t         nruns = 0;
    size_t         need = 0; // the preads' bytes, reserved in the cache's count until their blocks are in
#ifdef PLASMA_TEST_HOOKS
    bool whole = false; // the harness build's count of whole-span preads
#endif
    {
        std::lock_guard<std::mutex> guard(sCacheLock);
        for (size_t i = 0; i < count; ++i) {
            auto it = cache.blocks.find(b0 + i);
            if (it != cache.blocks.end()) {
                span[i] = it->second;
                ++span[i]->pins;
                IUnlink(span[i]);
                IAppend(span[i]);
            } else if (nruns && runs[nruns - 1].last == b0 + i) {
                ++runs[nruns - 1].last;
            } else {
                runs[nruns].first = b0 + i;
                runs[nruns++].last = b0 + i + 1;
            }
        }
        if (nruns) {
            if (nruns > IOriginalReads(iovs, iovs_len, offset, cache.eof >= 0 ? cache.eof : cache.proven)) {
                runs[0].first = b0;
                runs[0].last = b0 + count;
                nruns = 1;
#ifdef PLASMA_TEST_HOOKS
                whole = true;
#endif
            }
            bool split = false;
            for (size_t r = 0; r < nruns; ++r) {
                const off_t stop = off_t(runs[r].last * kBlock);
                runs[r].length = size_t((cache.eof >= 0 ? std::min(stop, cache.eof) : stop) - off_t(runs[r].first * kBlock));
                need += runs[r].length;
                split |= runs[r].last - runs[r].first >= 2;
            }
            if (split)
                need += kBlock; // the copy of the block being split off a run's buffer (header)
            if (!IMakeRoom(need)) {
                IUnpin(span, count);
                need = 0;
                nruns = 0;
                plain = false; // the cap has no room: the original call (header)
            } else {
                sResident += need;
#ifdef PLASMA_TEST_HOOKS
                sPeak = std::max(sPeak, sResident);
#endif
            }
        }
    }
    if (!plain)
        return IOriginal(cache, offset, iovs, iovs_len, nread, reads);

    // The preads, with no lock but the CacheFile's, then each run's buffer split into its blocks.
    const off_t eof = cache.eof, proven = cache.proven;
    bool        failed = false;
    Block*      fresh[kSpanMax] = {}; // the blocks read now, pinned as the span's
    for (size_t r = 0; r < nruns && !failed; ++r) {
        Run&        run = runs[r];
        const off_t start = off_t(run.first * kBlock);
        run.data = static_cast<unsigned char*>(std::malloc(run.length));
        if (!run.data) {
            failed = true;
            break;
        }
        __wasi_size_t        got = 0;
        const __wasi_errno_t rc = IPread(cache.fd, run.data, run.length, start, &got);
        ++reads;
#ifdef PLASMA_TEST_HOOKS
        ++cache.preads;
        if (whole)
            ++cache.spanPreads;
#endif
        if (rc != __WASI_ERRNO_SUCCESS) {
            failed = true;
            break;
        }
        run.got = got;
#ifdef PLASMA_TEST_HOOKS
        cache.bytes += got;
#endif
        cache.proven = std::max<off_t>(cache.proven, start + off_t(got));
        if (got < run.length) {
            cache.eof = start + off_t(got); // and the later runs lie past it
            break;
        }
    }
    for (size_t r = 0; r < nruns && !failed; ++r) {
        Run& run = runs[r];
        if (!run.data)
            break;
        // from the last block to the first: a copy of the block, then the buffer shrunk in place to the blocks before it
        // (try_realloc_chunk, dlmalloc.c:4915-4923); the first block keeps the buffer
        for (size_t k = (run.got + kBlock - 1) / kBlock; k-- > 0 && !failed;) {
            const uint64_t index = run.first + k;
            const size_t   bytes = std::min<size_t>(kBlock, run.got - k * kBlock);
            unsigned char* data = nullptr;
            if (!span[index - b0]) { // not a block the cache holds already (a whole-span pread reads those again)
                if (k) {
                    data = static_cast<unsigned char*>(std::malloc(bytes));
                    if (data)
                        std::memcpy(data, run.data + k * kBlock, bytes);
                } else {
                    data = static_cast<unsigned char*>(std::realloc(run.data, bytes));
                    if (!data)
                        data = run.data;
                    run.data = nullptr;
                }
                if (!data) {
                    failed = true;
                    break;
                }
                Block* block = new Block;
                block->owner = &cache;
                block->index = index;
                block->data = data;
                block->bytes = bytes;
                block->pins = 1;
                fresh[index - b0] = block;
            }
            if (k) {
                if (unsigned char* shrunk = static_cast<unsigned char*>(std::realloc(run.data, k * kBlock)))
                    run.data = shrunk;
            } else if (run.data) {
                std::free(run.data); // the first block was the cache's already
                run.data = nullptr;
            }
        }
        if (!run.got) {
            std::free(run.data);
            run.data = nullptr;
        }
    }
    if (failed) {
        // nothing inserted: the original call, from the original offset (header)
        for (size_t r = 0; r < nruns; ++r)
            std::free(runs[r].data);
        for (size_t i = 0; i < count; ++i)
            if (fresh[i]) {
                std::free(fresh[i]->data);
                delete fresh[i];
            }
        cache.eof = eof;
        cache.proven = proven;
        {
            std::lock_guard<std::mutex> guard(sCacheLock);
            sResident -= need;
            IUnpin(span, count);
        }
        return IOriginal(cache, offset, iovs, iovs_len, nread, reads);
    }
    if (nruns) {
        std::lock_guard<std::mutex> guard(sCacheLock);
        sResident -= need;
        for (size_t i = 0; i < count; ++i)
            if (Block* block = fresh[i]) {
                cache.blocks[block->index] = block;
                IAppend(block);
                sResident += block->bytes;
#ifdef PLASMA_TEST_HOOKS
                if (cache.evicted.erase(block->index))
                    ++cache.reMisses;
#endif
                span[i] = block;
            }
    }

    // The original's result from the blocks: the iovecs filled in order, a stop at the first short one (header)
    size_t total = 0;
    off_t  at = offset;
    bool   missing = false;
    for (size_t i = 0; i < iovs_len && !missing; ++i) {
        const size_t length = iovs[i].buf_len;
        size_t       got = 0;
        while (got < length && (cache.eof < 0 || at < cache.eof)) {
            const Block* block = span[size_t(uint64_t(at) / kBlock - b0)];
            if (!block) {
                missing = true; // never: before the file's end the span's blocks are all in (the original call then)
                break;
            }
            const size_t inside = size_t(uint64_t(at) % kBlock);
            if (inside >= block->bytes)
                break; // the file's end (a short block is the last)
            const size_t n = std::min(length - got, block->bytes - inside);
            std::memcpy(iovs[i].buf + got, block->data + inside, n);
            got += n;
            at += off_t(n);
        }
        total += got;
        if (got < length)
            break;
    }
    if (missing) {
        {
            std::lock_guard<std::mutex> guard(sCacheLock);
            IUnpin(span, count);
        }
        return IOriginal(cache, offset, iovs, iovs_len, nread, reads);
    }
    *nread = __wasi_size_t(total);
    const __wasi_errno_t rc = __wasi_fd_seek(__wasi_fd_t(cache.fd), offset + off_t(total), __WASI_WHENCE_SET, &position);
#ifdef PLASMA_TEST_HOOKS
    cache.served += total;
    ++cache.servedCalls;
#endif
    {
        std::lock_guard<std::mutex> guard(sCacheLock);
        IUnpin(span, count);
    }
    return rc;
}
} // namespace

extern "C" FILE* __wrap_fopen(const char* path, const char* mode)
{
    FILE* file = __real_fopen(path, mode);
    if (file && mode && !ReadOnly(mode) && plEmscriptenCwd::GameDataPath(path)) {
#ifdef PLASMA_TEST_HOOKS
        if (ITraceOn()) {
            ITraceOpen();
            ITrace("W %.3f %s", ITraceNow(), path);
        }
#endif
        // its WasmFS file's cached readers are poisoned before fopen returns, so before its first write (header)
        const void*                             identity = plEmscriptenCwd::FileIdentity(fileno(file));
        std::vector<std::shared_ptr<CacheFile>> poisoned;
        {
            std::lock_guard<std::mutex> guard(sLock);
            sWritable[file] = {path, identity};
            if (identity)
                IPoison(identity, poisoned);
        }
        IDropPoisoned(poisoned);
        return file;
    }
    // or a read-only open in the patcher-mode install directory (plEmscriptenCwd.h GameDataPath)
    if (!file || !ReadOnly(mode) ||
        !(plEmscriptenCwd::ImmutableAsset(fileno(file)) || plEmscriptenCwd::GameDataPath(path)))
        return file;
    // No other thread has the FILE before fopen returns: setvbuf and the check take no lock.
    char* buffer = static_cast<char*>(std::malloc(kGameDataBuffer));
    if (!buffer || setvbuf(file, buffer, _IOFBF, kGameDataBuffer) != 0) {
        std::free(buffer);
        return file;
    }
    const bool mirrored = IMirrorMatches(file, buffer);
    if (!mirrored)
        IMirrorFailed(path);
    auto cache = std::make_shared<CacheFile>();
    cache->fd = fileno(file);
    cache->identity = plEmscriptenCwd::FileIdentity(cache->fd);
#ifdef PLASMA_TEST_HOOKS
    if (ITraceOn())
        ITraceOpen(); // before sLock (header)
#endif
    std::lock_guard<std::mutex> guard(sLock);
    // a reader of a file a write-capable FILE has open, or of one without an identity, opens poisoned (header)
    bool                        poisoned = !cache->identity;
    for (const auto& [writing, writer] : sWritable)
        poisoned |= writer.identity == cache->identity;
    if (poisoned) {
        cache->poisoned = true;
#ifdef PLASMA_TEST_HOOKS
        ++sPoisonings;
#endif
    }
    sCached[cache->fd] = cache;
    GameDataFile& record = sBuffers[file];
    record.buffer = buffer;
    record.mirrored = mirrored;
    record.cache = cache;
#ifdef PLASMA_TEST_HOOKS
    record.path = path;
    if (ITraceOn()) {
        // before fopen returns, so before any read of the FILE
        record.lifetime = cache->lifetime = ++sLifetimes;
        ITrace("O %llu %.3f %d %d %s", (unsigned long long)record.lifetime, ITraceNow(), cache->fd, mirrored ? 1 : 0,
               path);
    }
#endif
    return file;
}

extern "C" int __wrap_fclose(FILE* file)
{
    std::string  written;
    GameDataFile record;
    bool         buffered = false;
    {
        // Both records leave before the FILE closes: its address and its descriptor are free for the next fopen.
        std::lock_guard<std::mutex> guard(sLock);
        auto                        it = sWritable.find(file);
        if (it != sWritable.end()) {
            written = std::move(it->second.path);
            sWritable.erase(it);
        }
        auto buffers = sBuffers.find(file);
        if (buffers != sBuffers.end()) {
            record = std::move(buffers->second);
            buffered = true;
            sBuffers.erase(buffers);
            sCached.erase(record.cache->fd);
        }
    }
#ifdef PLASMA_TEST_HOOKS
    char     counts[320] = "";
    uint64_t backendReads = 0, bytes = 0, zeroLength = 0;
#endif
    if (buffered) {
        // its blocks are freed once no call of it is in flight
        std::lock_guard<std::mutex> hold(record.cache->lock);
        record.cache->closed = true;
        IDropBlocks(*record.cache);
#ifdef PLASMA_TEST_HOOKS
        const CacheFile& c = *record.cache;
        backendReads = c.backendReads;
        bytes = c.bytes;
        zeroLength = c.zeroLength;
        size_t peak;
        {
            std::lock_guard<std::mutex> guard(sCacheLock);
            peak = sPeak;
        }
        std::snprintf(counts, sizeof(counts),
                      "; cache: served %llu B in %llu calls, preads %llu (whole span %llu), original calls %llu, "
                      "evictions %llu, re-misses %llu, poisoned %d, peak %zu B of %zu B",
                      (unsigned long long)c.served, (unsigned long long)c.servedCalls, (unsigned long long)c.preads,
                      (unsigned long long)c.spanPreads, (unsigned long long)c.passThrough,
                      (unsigned long long)c.evictions, (unsigned long long)c.reMisses, c.poisoned ? 1 : 0, peak,
                      sCacheCap);
#endif
    }
#ifdef PLASMA_TEST_HOOKS
    if (buffered && record.lifetime)
        ITrace("C %llu %.3f %llu %llu %llu %llu", (unsigned long long)record.lifetime, ITraceNow(),
               (unsigned long long)record.seeks, (unsigned long long)record.inBuffer, (unsigned long long)backendReads,
               (unsigned long long)bytes);
    else if (!written.empty() && ITraceOn())
        ITrace("X %.3f %s", ITraceNow(), written.c_str());
#endif
    const bool failed = !written.empty() && ferror(file);
    int        result = __real_fclose(file);
    if (!written.empty() && (failed || result != 0)) {
        // fclose's own errno when its flush or close failed. When only an earlier write failed (ferror), that write's
        // errno has been overwritten since, so none is reported (0).
        const int error = result != 0 ? errno : 0;
        unlink(written.c_str());
        plEmscriptenStorage::NoteWriteFailure(written.c_str(), error);
    }
    if (buffered) {
#ifdef PLASMA_TEST_HOOKS
        std::printf(
            "[stdio] %s: seeks %llu (in the buffer %llu), backend reads %llu (zero-length %llu), %llu B, taints "
            "%llu%s%s\n",
            record.path.c_str(), (unsigned long long)record.seeks, (unsigned long long)record.inBuffer,
            (unsigned long long)backendReads, (unsigned long long)zeroLength, (unsigned long long)bytes,
            (unsigned long long)record.taints, record.mirrored ? "" : ", the mirror check failed", counts);
#endif
        std::free(record.buffer); // musl's fclose has stopped using it
    }
    return result;
}

extern "C" int __wrap_fseeko(FILE* file, off_t offset, int whence)
{
    return ISeek<off_t>(file, offset, whence, __real_fseeko);
}

extern "C" int __wrap_fseek(FILE* file, long offset, int whence)
{
    return ISeek<long>(file, offset, whence, __real_fseek);
}

// ungetc() stores (unsigned char)c in the buffer in front of rpos (ungetc.c:15). That is the file's byte only when it
// puts back the byte just read from a valid window; anything else taints the FILE. ungetc(EOF) changes nothing
// (ungetc.c:5), and musl's own calls, in fgetln() (fgetln.c:10) and __fgetwc_unlocked_internal() (fgetwc.c:38), come
// with its FILE lock held, which FileLock leaves as it is.
extern "C" int __wrap_ungetc(int c, FILE* file)
{
    if (c == EOF)
        return __real_ungetc(c, file);
    FileLock hold(file);
    if (GameDataFile* record = IRecord(file)) {
        const MuslFile* f = Musl(file);
        if (!(IValidWindow(file, *record) && f->rpos > f->buf && f->rpos[-1] == static_cast<unsigned char>(c)))
            ITaint(*record);
    }
    return __real_ungetc(c, file);
}

// ungetwc stores one to MB_LEN_MAX bytes in front of rpos, after __toread's reset too (ungetwc.c:19-28): it always
// taints the FILE.
extern "C" wint_t __wrap_ungetwc(wint_t c, FILE* file)
{
    FileLock hold(file);
    if (GameDataFile* record = IRecord(file))
        ITaint(*record);
    return __real_ungetwc(c, file);
}

// The game-data FILEs' descriptor reads, from the cache (header); any other descriptor's call is the original.
extern "C" __wasi_errno_t __wrap___wasi_fd_read(__wasi_fd_t fd, const __wasi_iovec_t* iovs, size_t iovs_len,
                                                __wasi_size_t* nread)
{
    std::shared_ptr<CacheFile> cache;
    {
        std::lock_guard<std::mutex> guard(sLock);
        auto                        it = sCached.find(int(fd));
        if (it != sCached.end())
            cache = it->second;
    }
    if (!cache)
        return __real___wasi_fd_read(fd, iovs, iovs_len, nread);
    std::lock_guard<std::mutex> hold(cache->lock);
#ifdef PLASMA_TEST_HOOKS
    const double start = cache->lifetime ? ITraceNow() : 0.0;
#endif
    off_t                offset = -1;
    size_t               reads = 0;
    const __wasi_errno_t rc = ICachedRead(*cache, iovs, iovs_len, nread, offset, reads);
#ifdef PLASMA_TEST_HOOKS
    cache->backendReads += reads;
    if (cache->lifetime) {
        // the trace's R line (header), written under the descriptor's lock, so in its calls' order
        const double end = ITraceNow();
        char         lengths[512];
        size_t       used = 0;
        lengths[0] = '\0';
        for (size_t i = 0; i < iovs_len && used + 24 < sizeof(lengths); ++i)
            used += size_t(std::snprintf(lengths + used, sizeof(lengths) - used, "%s%zu", i ? "," : "",
                                         size_t(iovs[i].buf_len)));
        ITrace("R %llu %.3f %.0f %lld %s %lld %d %zu", (unsigned long long)cache->lifetime, start,
               (end - start) * 1000.0, (long long)offset, iovs_len ? lengths : "-",
               nread && rc == __WASI_ERRNO_SUCCESS ? (long long)*nread : -1LL, int(rc), reads);
    }
#endif
    return rc;
}

#ifdef PLASMA_TEST_HOOKS
// The test seams (plEmscriptenStdio.h)
void plEmscriptenStdio::SetCacheCapForTest(size_t cap)
{
    std::lock_guard<std::mutex> guard(sCacheLock);
    sCacheCap = std::min(cap, kCacheCap);
}

void plEmscriptenStdio::SetPreadForTest(PreadForTest pread)
{
    sPreadForTest = pread;
}

plEmscriptenStdio::CacheTotals plEmscriptenStdio::CacheTotalsForTest()
{
    CacheTotals totals;
    {
        std::lock_guard<std::mutex> guard(sLock);
        totals.poisonings = sPoisonings;
    }
    std::lock_guard<std::mutex> guard(sCacheLock);
    totals.resident = sResident;
    totals.peak = sPeak;
    totals.cap = sCacheCap;
    for (Block* block = sOldest; block; block = block->newer)
        totals.pinned += block->pins ? 1 : 0;
    return totals;
}

bool plEmscriptenStdio::CacheCountsForTest(int fd, CacheCounts& counts)
{
    std::shared_ptr<CacheFile> cache;
    {
        std::lock_guard<std::mutex> guard(sLock);
        auto                        it = sCached.find(fd);
        if (it == sCached.end())
            return false;
        cache = it->second;
    }
    std::lock_guard<std::mutex> hold(cache->lock);
    counts.backendReads = cache->backendReads;
    counts.served = cache->served;
    counts.servedCalls = cache->servedCalls;
    counts.preads = cache->preads;
    counts.spanPreads = cache->spanPreads;
    counts.original = cache->passThrough;
    counts.poisoned = cache->poisoned;
    std::lock_guard<std::mutex> guard(sCacheLock);
    counts.evictions = cache->evictions;
    counts.reMisses = cache->reMisses;
    counts.blocks = cache->blocks.size();
    return true;
}
#endif
