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

// pnAsyncCore sockets for Emscripten, over browser WebSockets.
//
// A browser has no TCP sockets, and Emscripten's WasmFS has no socket syscalls at all, so
// asio cannot run here. This file replaces pnAcSocket.cpp on
// Emscripten (CMakeLists.txt) and keeps its public interface (pnAcIo.h) and its callback
// semantics. Each Plasma TCP connection is one WebSocket to a WebSocket-to-TCP proxy, a
// raw byte pipe: ws://{proxy}/connect?host={host}&port={port}, the host percent-encoded as
// a URL query value (the gateway's contract: web/deploy/plasma_web/gateway.py
// upstream_target). The proxy URL is runtime configuration: the environment variable
// PLASMA_WS_PROXY, which the page sets.
//
// Threads. asio delivers every callback on one of its IO pool threads. Here one pthread,
// the socket thread, owns all WebSockets: it lives in its own worker event loop
// (emscripten_exit_with_live_runtime), WebSocket events call into wasm on it, and every
// AsyncNotifySocket* callback runs on it. Other threads hand it work through a private
// proxying queue (FIFO). The JS half is pnAcWebSocket.js.
//
// What matches pnAcSocket.cpp:
// - connect: the initial data is sent first, then AsyncNotifySocketConnectSuccess, then
//   reading starts (SocketInitConnect); a false return closes the socket and notifies
//   AsyncNotifySocketConnectFailed (AsyncSocketConnect's connect handler); a failed or
//   cancelled connect notifies it too (the same handler, after AsyncSocketConnectCancel);
// - reads (SocketStartAsyncRead): at most kAsyncSocketBufferSize bytes are buffered; the
//   callback gets the whole unprocessed buffer and returns how much it used, the rest is moved
//   down; nullopt or no callback stops reading for good; a full buffer with nothing consumed
//   stops reading (asio's zero-byte read, SocketStartAsyncRead :236-237);
// - disconnect: the remote closing (EOF) or a local AsyncSocketDisconnect (cancel ->
//   operation_aborted) notifies AsyncNotifySocketDisconnect once, after which the owner
//   calls AsyncSocketDelete (SocketStartAsyncRead's read handler, AsyncSocketDisconnect);
// - writes: in call order; AsyncSocketSend returns false once the socket is closed; the
//   backlog rule (a write queued for kBacklogFailMs on a connection older than
//   kBacklogInitMs disconnects it, SocketQueueAsyncWrite) uses the WebSocket's bufferedAmount.
// Differences, all forced by the browser API:
// - the local address is unknown (cleared) and the remote address is the one asked for;
// - Nagle cannot be set on a WebSocket. gateway.py turns it off on its TCP side for the game connection only, as
//   natively (NetCliConnectAccept (pnNcCli.cpp:1047), plNglGame.cpp's unbuffered=true); the Worker's connect() cannot;
// - a disconnect requested inside a read callback still notifies (asio's re-armed read
//   on the closed socket fails with bad_descriptor and logs instead);
// - once reading has stopped, bytes that still arrive are discarded and logged: a WebSocket
//   cannot leave them unread, as a TCP socket leaves them in the kernel's buffer;
// - a message that cannot be copied into the module's memory (out of memory) is a failed
//   read: it is logged and reading stops, as asio's read errors do (SocketStartAsyncRead).

#include <emscripten/emscripten.h>
#include <emscripten/proxying.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_theory/format>
#include <string_theory/string_stream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hsLockGuard.h"
#include "hsThread.h"
#include "hsTimer.h"
#include "pnAcCore.h"
#include "pnAcIo.h"
#include "pnAcLog.h"
#include "pnAsyncCore_Private.h"
#include "pnNetCommon/plNetAddress.h"

// pnAcWebSocket.js
extern "C" {
int    plws_open(const char* url, const char* join);
int    plws_send(int handle, const void* data, unsigned bytes);
double plws_buffered(int handle);
void   plws_close(int handle);
}

// wait before checking for backlog problems
static constexpr unsigned kBacklogInitMs = 3 * 60 * 1000;

// destroy a connection if it has a backlog "problem"
static constexpr unsigned kBacklogFailMs = 2 * 60 * 1000;

struct ConnectOperation
{
    plNetAddress                fRemoteAddr;
    AsyncCancelId               fCancelId;
    AsyncNotifySocketCallbacks* fCallbacks;
    std::vector<uint8_t>        fConnectBuffer;
    unsigned int                fConnectionType;
    bool                        fCancelled; // s_connectCrit
    int                         fHandle;    // socket thread

    ConnectOperation() : fCancelId(), fCallbacks(), fConnectionType(), fCancelled(), fHandle() {}
    ConnectOperation(const ConnectOperation&) = delete;
};

struct AsyncSocketStruct
{
    std::recursive_mutex        fCritsect;
    std::atomic<bool>           fOpen;
    int                         fHandle;    // socket thread
    AsyncNotifySocketCallbacks* fCallbacks; // socket thread
    unsigned int                fConnectionType;
    plNetAddress                fRemoteAddr;
    uint8_t                     fBuffer[kAsyncSocketBufferSize];
    size_t                      fBytesLeft;
    bool                        fReading;   // socket thread
    std::vector<uint8_t>        fRecvQueue; // socket thread: received, not yet in fBuffer
    size_t                      fRecvOffset;
    std::vector<uint8_t>        fSendQueue; // fCritsect: written, not yet handed to the WebSocket
    unsigned                    initTimeMs;
    unsigned                    closeTimeMs;
    unsigned                    backlogSinceMs; // socket thread
    uint64_t                    fDroppedBytes;  // socket thread: received after reading stopped

    AsyncSocketStruct(ConnectOperation& op, int handle)
        : fOpen(true), fHandle(handle), fCallbacks(op.fCallbacks), fConnectionType(op.fConnectionType), fRemoteAddr(op.fRemoteAddr), fBuffer(), fBytesLeft(), fReading(), fRecvOffset(), initTimeMs(), closeTimeMs(), backlogSinceMs(), fDroppedBytes()
    {
    }
};

static std::recursive_mutex        s_connectCrit;
static std::list<ConnectOperation> s_connectList;
static uintptr_t                   s_nextConnectCancelId = 1;

static pthread_t          s_sockThread;
static em_proxying_queue* s_queue;
static std::string        s_proxyUrl;
static std::string        s_joinUrl; // the gateway's join endpoint

// socket thread only
static std::unordered_map<int, ConnectOperation*>  s_connecting;
static std::unordered_map<int, AsyncSocketStruct*> s_open;

static void SocketFailCancelledConnects();

static bool OnSocketThread()
{
    return s_queue && pthread_equal(pthread_self(), s_sockThread);
}

static void RunTask(void* arg)
{
    std::unique_ptr<std::function<void()>> task(static_cast<std::function<void()>*>(arg));
    (*task)();
}

// Runs fn on the socket thread, later (never inside the caller), through one FIFO queue
// whichever thread posts it (the socket thread posts to itself the same way).
static void PostToSocketThread(std::function<void()> fn)
{
    auto task = new std::function<void()>(std::move(fn));
    if (!emscripten_proxy_async(s_queue, s_sockThread, RunTask, task)) {
        LogMsg(kLogError, "Failed to post a task to the socket thread");
        delete task;
    }
}

static void* SocketThreadProc(void*)
{
    hsThread::SetThisThreadName(ST_LITERAL("AcWebSocket"));
    // Return to this worker's event loop; the thread keeps running there, serving
    // WebSocket events and the tasks posted to s_queue.
    emscripten_exit_with_live_runtime();
    return nullptr;
}

void SocketInitialize()
{
    if (s_queue)
        return;

    const char* proxy = std::getenv("PLASMA_WS_PROXY");
    s_proxyUrl = proxy ? proxy : "";
    while (!s_proxyUrl.empty() && s_proxyUrl.back() == '/')
        s_proxyUrl.pop_back();
    if (s_proxyUrl.empty())
        LogMsg(kLogError, "PLASMA_WS_PROXY is not set: every socket connect will fail");
    // the page origin's gateway join endpoint (config.mjs PLASMA_WS_JOIN; empty = none)
    const char* join = std::getenv("PLASMA_WS_JOIN");
    s_joinUrl = join ? join : "";

    s_queue = em_proxying_queue_create();
    if (pthread_create(&s_sockThread, nullptr, SocketThreadProc, nullptr) != 0) {
        LogMsg(kLogFatal, "Failed to start the socket thread");
        em_proxying_queue_destroy(s_queue);
        s_queue = nullptr;
    }
}

static void SocketCloseAll()
{
    // socket thread
    for (auto& [handle, op] : s_connecting)
        plws_close(handle);
    for (auto& [handle, sock] : s_open) {
        plws_close(handle);
        sock->fHandle = 0;
        sock->fOpen = false;
    }
    if (!s_connecting.empty() || !s_open.empty())
        LogMsg(kLogDebug, "Socket shutdown closed {} pending connects and {} open sockets",
               s_connecting.size(), s_open.size());
    s_open.clear();
}

void SocketDestroy(unsigned exitThreadWaitMs)
{
    if (!s_queue)
        return;

    // Cancel any pending connect requests (they notify AsyncNotifySocketConnectFailed)
    {
        hsLockGuard(s_connectCrit);
        for (ConnectOperation& op : s_connectList)
            op.fCancelled = true;
        if (!s_connectList.empty())
            PostToSocketThread(SocketFailCancelledConnects);
    }

    // Let the queued work run, then close what is left. asio's pool stops after
    // exitThreadWaitMs and drops its pending handlers; here they are dropped at once.
    // The socket thread itself stays alive in its event loop for the rest of the
    // process (Emscripten terminates it at exit), so a later SocketInitialize reuses it.
    emscripten_proxy_sync(s_queue, s_sockThread, [](void*) { SocketCloseAll(); }, nullptr);
}

static void SocketConnectDone(ConnectOperation* op, bool success)
{
    // socket thread; op is no longer in s_connecting
    if (!success)
        op->fCallbacks->AsyncNotifySocketConnectFailed(op->fRemoteAddr);

    {
        hsLockGuard(s_connectCrit);
        for (auto it = s_connectList.begin(); it != s_connectList.end(); ++it) {
            if (&*it == op) {
                s_connectList.erase(it);
                break;
            }
        }
    }

    PerfSubCounter(kAsyncPerfSocketConnAttemptsOutCurr, 1);
}

static void SocketDeliverReads(AsyncSocket sock);
static bool SocketFlushWrites(AsyncSocket sock);

static bool SocketInitConnect(ConnectOperation& op, int handle)
{
    // socket thread
    auto sock = std::make_unique<AsyncSocketStruct>(op, handle);
    sock->initTimeMs = hsTimer::GetMilliSeconds<unsigned>();
    s_open[handle] = sock.get();

    auto discard = [handle]() {
        s_open.erase(handle);
        plws_close(handle);
        return false;
    };

    // Send initial data
    if (!op.fConnectBuffer.empty()) {
        if (!AsyncSocketSend(sock.get(), op.fConnectBuffer.data(), op.fConnectBuffer.size()))
            return discard();
    }

    // perform callback notification
    plNetAddress localAddr; // not known to a browser
    plNetAddress remoteAddr = op.fRemoteAddr;
    if (!sock->fCallbacks->AsyncNotifySocketConnectSuccess(sock.get(), localAddr, remoteAddr))
        return discard();

    // start reading from the socket
    sock->fReading = true;
    sock.release();
    return true;
}

// The host the proxy connects to. A name resolved by Emscripten's getaddrinfo comes back
// as a placeholder address (172.29.x.x, its in-browser DNS table, src/lib/libcore.js);
// getnameinfo maps it back to the name, which the proxy resolves. Real addresses pass
// through as dotted quads.
static ST::string SocketProxyHostName(const plNetAddress& addr)
{
    sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(addr.GetPort());
    auto bytes = addr.GetHostBytes();
    memcpy(&sin.sin_addr, bytes.data(), 4);
    if (bytes[0] == 172 && bytes[1] == 29) {
        char host[256];
        if (getnameinfo(reinterpret_cast<sockaddr*>(&sin), sizeof(sin), host, sizeof(host),
                        nullptr, 0, NI_NAMEREQD) == 0)
            return ST::string::from_utf8(host);
    }
    return addr.GetHostString();
}

// A query value as encodeURIComponent writes it: every byte of the UTF-8 text except
// A-Z a-z 0-9 - _ . ! ~ * ' ( ) becomes %XX. A dotted quad or a DNS name is unchanged;
// any other text cannot end the host field or add a field to the query.
static ST::string SocketUrlComponent(const ST::string& text)
{
    static const char kHex[] = "0123456789ABCDEF";
    ST::string_stream out;
    for (char c : std::string(text.c_str(), text.size())) {
        const unsigned char b = static_cast<unsigned char>(c);
        if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') ||
            (b != 0 && std::strchr("-_.!~*'()", b) != nullptr)) {
            out.append_char(c);
        } else {
            out.append_char('%');
            out.append_char(kHex[b >> 4]);
            out.append_char(kHex[b & 0xF]);
        }
    }
    return out.to_string();
}

static void SocketStartConnect(ConnectOperation* op)
{
    // socket thread
    bool cancelled;
    {
        hsLockGuard(s_connectCrit);
        cancelled = op->fCancelled;
    }
    if (cancelled) {
        LogMsg(kLogError, "Failed to connect: Operation canceled");
        SocketConnectDone(op, false);
        return;
    }

    int handle = 0;
    if (!s_proxyUrl.empty()) {
        ST::string url = ST::format("{}/connect?host={}&port={}", s_proxyUrl,
                                    SocketUrlComponent(SocketProxyHostName(op->fRemoteAddr)),
                                    op->fRemoteAddr.GetPort());
        handle = plws_open(url.c_str(), s_joinUrl.empty() ? nullptr : s_joinUrl.c_str());
    }
    if (!handle) {
        LogMsg(kLogError, "Failed to connect: no WebSocket for {}", op->fRemoteAddr.AsString());
        SocketConnectDone(op, false);
        return;
    }
    op->fHandle = handle;
    s_connecting[handle] = op;
}

extern "C" EMSCRIPTEN_KEEPALIVE void plws_on_open(int handle)
{
    auto it = s_connecting.find(handle);
    if (it == s_connecting.end()) {
        plws_close(handle);
        return;
    }
    ConnectOperation* op = it->second;
    s_connecting.erase(it);

    bool cancelled;
    {
        hsLockGuard(s_connectCrit);
        cancelled = op->fCancelled;
    }
    bool success;
    if (cancelled) {
        LogMsg(kLogError, "Failed to connect: Operation canceled");
        plws_close(handle);
        success = false;
    } else {
        success = SocketInitConnect(*op, handle);
    }
    SocketConnectDone(op, success);
}

// data is null when pnAcWebSocket.js could not allocate the message's copy.
extern "C" EMSCRIPTEN_KEEPALIVE void plws_on_message(int handle, uint8_t* data, unsigned bytes)
{
    auto it = s_open.find(handle);
    if (it != s_open.end() && it->second->fOpen) {
        AsyncSocket sock = it->second;
        if (!data) {
            // pnAcSocket.cpp, SocketStartAsyncRead: a read that fails with an error other than EOF
            // or a cancel is logged, and no further read is started.
            LogMsg(kLogError, "Failed to read from socket: out of memory for a message of {} bytes", bytes);
            sock->fReading = false;
        } else if (sock->fReading) {
            if (sock->fRecvOffset == sock->fRecvQueue.size()) {
                sock->fRecvQueue.clear();
                sock->fRecvOffset = 0;
            }
            sock->fRecvQueue.insert(sock->fRecvQueue.end(), data, data + bytes);
            SocketDeliverReads(sock);
        } else {
            // Natively a socket nobody reads keeps these bytes in the kernel's buffer until it
            // is closed; a WebSocket delivers them regardless, so they are discarded here.
            sock->fDroppedBytes += bytes;
            LogMsg(kLogError, "Socket {}: reading has stopped; discarded {} received bytes ({} in all)",
                   sock->fRemoteAddr.AsString(), bytes, sock->fDroppedBytes);
        }
    }
    std::free(data);
}

static void SocketNotifyDisconnect(AsyncSocket sock)
{
    // socket thread. We have to be extremely careful from this point because
    // sockets can be deleted during the notification callback.
    // After this call, the application becomes responsible for
    // calling AsyncSocketDelete at some later point in time.
    if (sock->fCallbacks) {
        auto callbacks = sock->fCallbacks;
        sock->fCallbacks = nullptr;
        callbacks->AsyncNotifySocketDisconnect(sock);
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void plws_on_close(int handle, int code)
{
    auto cit = s_connecting.find(handle);
    if (cit != s_connecting.end()) {
        ConnectOperation* op = cit->second;
        s_connecting.erase(cit);
        LogMsg(kLogError, "Failed to connect: WebSocket closed with code {} ({})", code,
               op->fRemoteAddr.AsString());
        SocketConnectDone(op, false);
        return;
    }

    auto it = s_open.find(handle);
    if (it == s_open.end())
        return;
    AsyncSocket sock = it->second;
    s_open.erase(it);
    sock->fHandle = 0;
    // The remote end closed (EOF). If the owner already called AsyncSocketDisconnect,
    // its posted task delivers the notification instead.
    if (sock->fOpen.exchange(false))
        SocketNotifyDisconnect(sock);
}

static void SocketDeliverReads(AsyncSocket sock)
{
    // socket thread
    while (sock->fReading && sock->fOpen && sock->fRecvOffset < sock->fRecvQueue.size()) {
        size_t count = sizeof(sock->fBuffer) - sock->fBytesLeft;
        if (!count) {
            // asio: a zero-byte read completes with no bytes and reading stops
            sock->fReading = false;
            return;
        }
        size_t bytes = std::min(count, sock->fRecvQueue.size() - sock->fRecvOffset);
        memcpy(sock->fBuffer + sock->fBytesLeft, sock->fRecvQueue.data() + sock->fRecvOffset, bytes);
        sock->fRecvOffset += bytes;
        sock->fBytesLeft += bytes;

        size_t                bytesNotified = sock->fBytesLeft;
        std::optional<size_t> res;
        if (sock->fCallbacks)
            res = sock->fCallbacks->AsyncNotifySocketRead(sock, sock->fBuffer, bytesNotified);
        if (!res) {
            // No callback, or the callback told us to stop reading
            sock->fReading = false;
            return;
        }

        // if only some of the bytes were used, then shift
        // remaining bytes down.  Otherwise, clear the buffer.
        size_t bytesProcessed = *res;
        sock->fBytesLeft -= bytesProcessed;
        if (sock->fBytesLeft != 0) {
            if ((sock->fBytesLeft > sizeof(sock->fBuffer)) || ((bytesProcessed + sock->fBytesLeft) > sizeof(sock->fBuffer))) {
                LogMsg(kLogError, "SocketDispatchRead error: {} {} {}",
                       sock->fBytesLeft, bytesNotified, bytesProcessed);
                sock->fReading = false;
                return;
            }

            if (bytesProcessed != 0)
                memmove(sock->fBuffer, sock->fBuffer + bytesProcessed, sock->fBytesLeft);
        }
    }
}

void AsyncSocketConnect(AsyncCancelId* cancelId, const plNetAddress& netAddr,
                        AsyncNotifySocketCallbacks* callbacks,
                        const void* sendData, unsigned sendBytes)
{
    ASSERT(callbacks);
    ASSERT(s_queue);

    PerfAddCounter(kAsyncPerfSocketConnAttemptsOutCurr, 1);
    PerfAddCounter(kAsyncPerfSocketConnAttemptsOutTotal, 1);

    ConnectOperation* op;
    {
        hsLockGuard(s_connectCrit);
        op = &*s_connectList.emplace(s_connectList.end());
        op->fRemoteAddr = netAddr;
        op->fCallbacks = callbacks;
        if (sendBytes) {
            auto sendBuffer = reinterpret_cast<const uint8_t*>(sendData);
            op->fConnectBuffer.assign(sendBuffer, sendBuffer + sendBytes);
            op->fConnectionType = sendBuffer[0];
        } else {
            op->fConnectionType = 0;
        }

        if (cancelId) {
            // get cancel id; we can avoid checking for zero by always using an odd number
            ASSERT(s_nextConnectCancelId & 1);
            s_nextConnectCancelId += 2;

            *cancelId = op->fCancelId = (AsyncCancelId)s_nextConnectCancelId;
        }
    }

    // As with asio's async_connect, the result is always notified later, never inside
    // this call.
    PostToSocketThread([op] { SocketStartConnect(op); });
}

// socket thread: fail the started connects that were cancelled
static void SocketFailCancelledConnects()
{
    std::vector<ConnectOperation*> cancelled;
    {
        hsLockGuard(s_connectCrit);
        for (auto& [handle, op] : s_connecting) {
            if (op->fCancelled)
                cancelled.push_back(op);
        }
    }
    for (ConnectOperation* op : cancelled) {
        s_connecting.erase(op->fHandle);
        plws_close(op->fHandle);
        LogMsg(kLogError, "Failed to connect: Operation canceled");
        SocketConnectDone(op, false);
    }
}

void AsyncSocketConnectCancel(AsyncCancelId cancelId)
{
    bool any = false;
    {
        hsLockGuard(s_connectCrit);
        for (ConnectOperation& op : s_connectList) {
            if (op.fCancelId != cancelId)
                continue;
            op.fCancelled = true;
            any = true;
        }
    }
    // This will wake up the pending connect. A connect that has not started yet fails
    // when it starts (SocketStartConnect); one that already completed is unaffected.
    if (any)
        PostToSocketThread(SocketFailCancelledConnects);
}

void AsyncSocketDelete(AsyncSocket conn)
{
    // After the disconnect notification nothing refers to conn except work still queued
    // for the socket thread (a write posted before the disconnect), so the delete goes
    // through the same FIFO queue, behind that work.
    PostToSocketThread([conn] { delete conn; });
}

void AsyncSocketDisconnect(AsyncSocket conn, bool hardClose)
{
    if (!conn->fOpen.exchange(false))
        return;

    hsAssert(hardClose, "Only hardClose supported on sockets");
    {
        hsLockGuard(conn->fCritsect);
        conn->fSendQueue.clear(); // a hard close (linger 0) drops unsent data
    }
    conn->closeTimeMs |= 1;

    // asio: cancel + close make the pending read complete with operation_aborted, which
    // notifies the disconnect from a pool thread.
    PostToSocketThread([conn] {
        if (conn->fHandle) {
            s_open.erase(conn->fHandle);
            plws_close(conn->fHandle);
            conn->fHandle = 0;
        }
        SocketNotifyDisconnect(conn);
    });
}

static bool SocketFlushWrites(AsyncSocket conn)
{
    // socket thread
    std::vector<uint8_t> data;
    {
        hsLockGuard(conn->fCritsect);
        data.swap(conn->fSendQueue);
    }
    if (!conn->fOpen || !conn->fHandle)
        return false;

    // check for data backlog
    unsigned currTimeMs = hsTimer::GetMilliSeconds<unsigned>();
    if (plws_buffered(conn->fHandle) > 0) {
        if (!conn->backlogSinceMs)
            conn->backlogSinceMs = currTimeMs | 1;
        if (((long)(currTimeMs - conn->backlogSinceMs) >= (long)kBacklogFailMs) && ((long)(currTimeMs - conn->initTimeMs) >= (long)kBacklogInitMs)) {
            PerfAddCounter(kAsyncPerfSocketDisconnectBacklog, 1);

            if (conn->fConnectionType) {
                LogMsg(
                    kLogPerf,
                    "Backlog, c:{} q:{}, i:{}",
                    conn->fConnectionType,
                    currTimeMs - conn->backlogSinceMs,
                    currTimeMs - conn->initTimeMs);
            }
            AsyncSocketDisconnect(conn, true);
            return false;
        }
    } else {
        conn->backlogSinceMs = 0;
    }

    if (data.empty())
        return true;
    if (plws_send(conn->fHandle, data.data(), unsigned(data.size())) != 0) {
        LogMsg(kLogError, "Failed to write data to socket: WebSocket is not open");
        AsyncSocketDisconnect(conn, true);
        return false;
    }
    return true;
}

bool AsyncSocketSend(AsyncSocket conn, const void* data, size_t bytes)
{
    ASSERT(conn);
    ASSERT(data);
    ASSERT(bytes);

    if (!conn->fOpen)
        return false;

    // Writes keep their call order across threads (the stream may be RC4-encrypted in
    // that order, pnNcCli.cpp PutBufferOnWire): append under the lock, then the socket thread
    // hands everything queued so far to the WebSocket.
    bool wasEmpty;
    {
        hsLockGuard(conn->fCritsect);
        wasEmpty = conn->fSendQueue.empty();
        auto bytesIn = reinterpret_cast<const uint8_t*>(data);
        conn->fSendQueue.insert(conn->fSendQueue.end(), bytesIn, bytesIn + bytes);
    }

    if (OnSocketThread())
        return SocketFlushWrites(conn);
    if (wasEmpty)
        PostToSocketThread([conn] { SocketFlushWrites(conn); });
    return true;
}

void AsyncSocketEnableNagling(AsyncSocket conn, bool enable)
{
    // A WebSocket has no Nagle switch; gateway.py sets it on its TCP side from the connection-type byte.
}
