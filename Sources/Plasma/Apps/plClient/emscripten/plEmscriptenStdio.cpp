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

// stdio wrappers (-Wl,--wrap=fopen,--wrap=fclose,--wrap=fseeko,--wrap=fseek,--wrap=ungetc,--wrap=ungetwc) for three
// deliberate differences from the native client, kept here so that no engine file changes for them.
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
// (ferror, or fclose's flush: OPFS returns EIO when the origin's quota is exhausted) is removed at fclose, and
// pfPatcher.cpp fails that download (WrittenShort). Natively the partial file stays, and the next run's hash check
// downloads it again (pfPatcher.cpp IHashFile), as it does a missing one; in the browser a full quota is the usual
// cause, and the partial file would only hold quota the next download needs.
//
// A harness build (PLASMA_TEST_HOOKS) also counts each game-data FILE's I/O: its seeks, those served in the buffer,
// its taints and the WasmFS backend reads under it (__wasi_fd_read, linked with -Wl,--wrap only in a harness build),
// printed as one "[stdio]" line when the FILE is closed. The counters never change the I/O; the product build has none
// of them.

#include <emscripten/version.h>
#include <stdio_ext.h>
#include <unistd.h>
#include <wchar.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef PLASMA_TEST_HOOKS
#include <wasi/api.h>

#include <algorithm>
#include <cstdint>
#endif

#include "plEmscriptenCwd.h"
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

// A read-only game-data FILE with the 64 KiB buffer
struct GameDataFile
{
    char* buffer = nullptr;
    bool  mirrored = false; // the fopen check passed: seeks may move the read position within the buffer
    bool  tainted = false;  // a pushback may have put a byte that is not the file's into the buffer
#ifdef PLASMA_TEST_HOOKS
    // the harness build's counts, printed at fclose
    std::string path;
    int         fd = -1;
    uint64_t    seeks = 0;
    uint64_t    inBuffer = 0;
    uint64_t    taints = 0;
    uint64_t    backendReads = 0;
    uint64_t    zeroLength = 0;
    uint64_t    bytes = 0;
#endif
};

// Lock order: a FILE's lock, then sLock, held briefly; sLock is never held while a FILE lock is taken.
std::mutex                              sLock;
std::unordered_map<FILE*, GameDataFile> sBuffers;
std::unordered_map<FILE*, std::string>  sWritable; // install-directory files opened for writing
#ifdef PLASMA_TEST_HOOKS
std::unordered_map<int, GameDataFile*> sCounted; // the descriptors of sBuffers' FILEs
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
} // namespace

extern "C" FILE* __wrap_fopen(const char* path, const char* mode)
{
    FILE* file = __real_fopen(path, mode);
    if (file && mode && !ReadOnly(mode) && plEmscriptenCwd::GameDataPath(path)) {
        std::lock_guard<std::mutex> guard(sLock);
        sWritable[file] = path;
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
    std::lock_guard<std::mutex> guard(sLock);
    GameDataFile&               record = sBuffers[file];
    record.buffer = buffer;
    record.mirrored = mirrored;
#ifdef PLASMA_TEST_HOOKS
    record.path = path;
    record.fd = Musl(file)->fd;
    sCounted[record.fd] = &record;
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
            written = std::move(it->second);
            sWritable.erase(it);
        }
        auto buffers = sBuffers.find(file);
        if (buffers != sBuffers.end()) {
            record = std::move(buffers->second);
            buffered = true;
            sBuffers.erase(buffers);
#ifdef PLASMA_TEST_HOOKS
            sCounted.erase(record.fd);
#endif
        }
    }
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
            "%llu%s\n",
            record.path.c_str(), (unsigned long long)record.seeks, (unsigned long long)record.inBuffer,
            (unsigned long long)record.backendReads, (unsigned long long)record.zeroLength,
            (unsigned long long)record.bytes, (unsigned long long)record.taints,
            record.mirrored ? "" : ", the mirror check failed");
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

#ifdef PLASMA_TEST_HOOKS
extern "C" __wasi_errno_t __real___wasi_fd_read(__wasi_fd_t fd, const __wasi_iovec_t* iovs, size_t iovs_len,
                                                __wasi_size_t* nread);

// The harness build's count of backend reads. WasmFS reads each iovec with its own backend read, up to and including
// the first short one (readAtOffset, system/lib/wasmfs/syscalls.cpp:221-248); a failed call counts as one. It calls
// the original with the same arguments and returns its result.
extern "C" __wasi_errno_t __wrap___wasi_fd_read(__wasi_fd_t fd, const __wasi_iovec_t* iovs, size_t iovs_len,
                                                __wasi_size_t* nread)
{
    const __wasi_errno_t        rc = __real___wasi_fd_read(fd, iovs, iovs_len, nread);
    std::lock_guard<std::mutex> guard(sLock);
    auto                        it = sCounted.find(int(fd));
    if (it == sCounted.end())
        return rc;
    GameDataFile& record = *it->second;
    if (rc != __WASI_ERRNO_SUCCESS || !nread) {
        ++record.backendReads;
        return rc;
    }
    size_t left = *nread;
    for (size_t i = 0; i < iovs_len; ++i) {
        ++record.backendReads;
        if (!iovs[i].buf_len)
            ++record.zeroLength;
        const size_t got = std::min<size_t>(left, iovs[i].buf_len);
        left -= got;
        if (got < iovs[i].buf_len)
            break;
    }
    record.bytes += *nread;
    return rc;
}
#endif
