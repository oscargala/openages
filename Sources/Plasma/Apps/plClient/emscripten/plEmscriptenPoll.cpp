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

// A poll() that honours its timeout.
//
// Emscripten 6.0.12's WasmFS __syscall_poll never blocks ("TODO: This should block based on the timeout",
// wasmfs/syscalls.cpp), and musl's select() is built on it. asio's select reactor, which runs pnAsyncCore's timers
// (pnAcTimer.cpp), would therefore spin a whole core while it waits for the next timer. This wrapper
// (-Wl,--wrap=__syscall_poll) waits as the native call does. The browser's main thread cannot wait and keeps the
// non-blocking behaviour.
//
// What ends the wait. Of the descriptors WasmFS can poll, only a pipe's read end goes from "not ready" to "ready", and
// only a write to the other end does that. That is how asio wakes its reactor: without eventfd its select_interrupter
// is a pipe, and posting work or stopping writes one byte to it (pipe_select_interrupter::interrupt). So the pipes'
// write ends are recorded when they are made (__syscall_pipe2) and forgotten when closed (__wasi_fd_close), and each
// write to one of them (__wasi_fd_write) bumps s_pipeWrites and wakes every waiting poll(), which probes again: it
// returns when a descriptor is ready, as poll(2) does, or when its timeout expires. A write end duplicated with dup()
// is not recorded; asio never duplicates its interrupter.

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <poll.h>
#include <pthread.h>
#include <wasi/api.h>

#include <atomic>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace
{
std::atomic<uint64_t> s_pipeWriteEnds{0};        // bit n: descriptor n (< 64) is the write end of a pipe
std::atomic<bool>     s_pipeWriteEndHigh{false}; // a write end has a descriptor >= 64: then every write wakes
std::atomic<uint32_t> s_pipeWrites{0};           // the futex word: one more for each write to a pipe
static_assert(sizeof(s_pipeWrites) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free,
              "emscripten_futex_wait waits on a 32-bit word");

bool IIsPipeWriteEnd(__wasi_fd_t fd)
{
    if (fd < 64)
        return (s_pipeWriteEnds.load(std::memory_order_relaxed) >> fd) & 1;
    return s_pipeWriteEndHigh.load(std::memory_order_relaxed);
}

#ifdef PLASMA_TEST_HOOKS
// Harness builds only: what woke each polling thread, one line per thread every 30 s.
struct PollCounts
{
    double   since = 0;
    unsigned polls = 0, waits = 0, woken = 0, timedOut = 0, other = 0;
    unsigned lastFds = 0;
};
thread_local PollCounts t_counts;

void ICount(int waited, nfds_t nfds)
{
    PollCounts&  c = t_counts;
    const double now = emscripten_get_now();
    if (c.since == 0)
        c.since = now;
    c.lastFds = unsigned(nfds);
    if (waited == 0 || waited == -EWOULDBLOCK) // woken, or a write moved s_pipeWrites before the wait began
        ++c.woken;
    else if (waited == -ETIMEDOUT)
        ++c.timedOut;
    else
        ++c.other;
    ++c.waits;
    if (now - c.since >= 30000.0) {
        std::printf(
            "[POLL] thread %p: in %.1f s, %u poll() calls waited (last with %u fds): %u waits, %u ended by a "
            "pipe write, %u at the timeout, %u otherwise\n",
            reinterpret_cast<void*>(pthread_self()),
            (now - c.since) / 1000.0, c.polls, c.lastFds, c.waits, c.woken, c.timedOut, c.other);
        std::fflush(stdout);
        c = PollCounts();
        c.since = now;
    }
}
#endif
} // namespace

extern "C" int            __real___syscall_pipe2(int fd[2], int flags);
extern "C" __wasi_errno_t __real___wasi_fd_close(__wasi_fd_t fd);
extern "C" __wasi_errno_t __real___wasi_fd_write(__wasi_fd_t fd, const __wasi_ciovec_t* iovs, size_t iovs_len,
                                                 __wasi_size_t* nwritten);
extern "C" int            __real___syscall_poll(struct pollfd* fds, nfds_t nfds, int timeout);

extern "C" int __wrap___syscall_pipe2(int fd[2], int flags)
{
    const int rc = __real___syscall_pipe2(fd, flags);
    if (rc == 0) {
        if (fd[1] >= 0 && fd[1] < 64)
            s_pipeWriteEnds.fetch_or(uint64_t(1) << fd[1]);
        else
            s_pipeWriteEndHigh = true;
#ifdef PLASMA_TEST_HOOKS
        std::printf("[POLL] pipe {%d, %d} made on thread %p\n", fd[0], fd[1], reinterpret_cast<void*>(pthread_self()));
        std::fflush(stdout);
#endif
    }
    return rc;
}

extern "C" __wasi_errno_t __wrap___wasi_fd_close(__wasi_fd_t fd)
{
    // Forgotten before the descriptor is freed, so a pipe made after it with the same number keeps its bit.
    if (fd < 64)
        s_pipeWriteEnds.fetch_and(~(uint64_t(1) << fd));
    return __real___wasi_fd_close(fd);
}

extern "C" __wasi_errno_t __wrap___wasi_fd_write(__wasi_fd_t fd, const __wasi_ciovec_t* iovs, size_t iovs_len,
                                                 __wasi_size_t* nwritten)
{
    const __wasi_errno_t rc = __real___wasi_fd_write(fd, iovs, iovs_len, nwritten);
    if (rc == __WASI_ERRNO_SUCCESS && nwritten && *nwritten > 0 && IIsPipeWriteEnd(fd)) {
        s_pipeWrites.fetch_add(1);
        emscripten_futex_wake(&s_pipeWrites, INT_MAX);
    }
    return rc;
}

extern "C" int __wrap___syscall_poll(struct pollfd* fds, nfds_t nfds, int timeout)
{
    if (timeout == 0 || emscripten_is_main_browser_thread())
        return __real___syscall_poll(fds, nfds, 0);

    const double deadline = timeout > 0 ? emscripten_get_now() + timeout : INFINITY;
#ifdef PLASMA_TEST_HOOKS
    ++t_counts.polls;
#endif
    for (;;) {
        // s_pipeWrites is read before the probe: a write that the probe misses moves it, and the wait returns at once.
        const uint32_t seen = s_pipeWrites.load();
        const int      ready = __real___syscall_poll(fds, nfds, 0);
        if (ready != 0)
            return ready;
        const double left = deadline - emscripten_get_now();
        if (left <= 0)
            return 0;
        // The calls other threads have proxied to this one run before and after the wait, as they did around the
        // usleep it replaces (emscripten_thread_sleep, system/lib/pthread/library_pthread.c).
        emscripten_current_thread_process_queued_calls();
        const int waited = emscripten_futex_wait(&s_pipeWrites, seen, left);
#ifdef PLASMA_TEST_HOOKS
        ICount(waited, nfds);
#else
        (void)waited;
#endif
        emscripten_current_thread_process_queued_calls();
    }
}
