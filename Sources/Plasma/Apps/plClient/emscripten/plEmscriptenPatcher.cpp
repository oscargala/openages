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

// The launcher's data step and the patcher-mode install directory (plEmscriptenPatcher.h).
#include "plEmscriptenPatcher.h"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_theory/format>
#include <type_traits>

#include "HeadSpin.h"
#include "hsTimer.h"
#include "pfConsoleCore/pfServerIni.h"
#include "pfGameMgr/pfGameMgr.h"
#include "pfPatcher/pfPatcher.h"
#include "pfPatcher/plManifests.h"
#include "plFileSystem.h"
#include "plMessageBox/hsMessageBox.h"
#include "plNetGameLib/plNetGameLib.h"
#include "plProduct.h"
#include "pnAsyncCore/pnAsyncCore.h"
#include "pnNetBase/pnNbError.h"
#include "pnNetBase/pnNbSrvs.h"

#ifdef PLASMA_TEST_HOOKS
#include "plEmscriptenTestHooks.h"
#endif

namespace
{
// The launcher's lifecycle (its net session opened and closed, the patch's outcome) and its errors are console lines
// of the product, as the native launcher's are its dialog's text and error box (plUruLauncher winmain.cpp). Its
// progress (the file server it reached, each file it downloads) is a harness build's status line, through the client
// seam's Status; the product prints none.
template <typename... Args>
void LauncherLine(const char* fmt, Args&&... args)
{
    ST::string line = ST::format(fmt, std::forward<Args>(args)...);
    std::printf("launcher: %s\n", line.c_str());
    std::fflush(stdout);
}

#ifdef PLASMA_TEST_HOOKS
template <typename... Args>
void LauncherStatus(const char* fmt, Args&&... args)
{
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->Status(ST::format("launcher: {}", ST::format(fmt, std::forward<Args>(args)...)));
}
#else
template <typename... Args>
void LauncherStatus(const char*, Args&&...)
{
}
#endif

// plClientLauncher.cpp's kNetTransTimeout, kAsyncCoreShutdownTime and kNetCoreUpdateSleepTime
constexpr unsigned kNetTransTimeout = 5 * 60 * 1000;
constexpr unsigned kAsyncCoreShutdownTime = 2 * 1000;
constexpr double   kNetCoreUpdateSleepMs = 10.0;

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The launcher dialog (plUruLauncher.rc IDD_DIALOG: banner, IDC_STATUS_TEXT "Welcome to URU", IDC_PRODUCTSTRING,
// IDC_TEXT, IDC_PROGRESS / IDC_MARQUEE, IDC_DLSIZE, IDC_DLSPEED, Cancel) as a page dialog: plEmscriptenPatcher.js. Each
// function runs on the page thread (IOnPage below). While it is open it is Module.plasma.launcher, the page-module
// object's launcher member (pre.js).
extern "C" {
void plasmaLauncherShow(const char* product);
// text/size/speed: null pointers leave the field; total > 0 sets the bar, total < 0 makes it a marquee again
void plasmaLauncherSet(const char* text, const char* size, const char* speed, double cur, double total);
void plasmaLauncherClose();
}

template <typename F>
static void IOnPage(F&& fn)
{
    using Fn = std::remove_reference_t<F>;
    if (emscripten_is_main_browser_thread()) {
        fn();
        return;
    }
    emscripten_proxy_sync(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
                          [](void* arg) { (*static_cast<Fn*>(arg))(); }, &fn);
}

std::string plEmscriptenPatcher::InstallDirName()
{
    ST::string key;
    if (const char* name = std::getenv("PLASMA_DEPLOYMENT_NAME"))
        key = name;
    if (key.empty())
        key = GetServerDisplayName();
    std::string out;
    for (char c : std::string(key.c_str()))
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
            c == '.')
            out += c;
    if (out.empty() || out == "." || out == "..")
        out = "default";
    return out;
}

static bool EndsWithI(const std::string& s, const char* suffix)
{
    const size_t n = std::strlen(suffix);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suffix) == 0;
}

bool plEmscriptenPatcher::PackageEntry(const std::string& relative, bool directory)
{
    // What a client package carries besides the shard-patched Age data (the test shard's manifests list every .prp,
    // .age, .fni and .ogg; Python and SDL come from the auth server): the root resource.dat (cursors and loading
    // images), dat/*.p2f fonts, dat/*.loc localization, sfx/*.sub subtitles and avi/ movies.
    if (directory)
        return relative == "dat" || relative == "sfx" || relative == "avi";
    if (relative.find('/') == std::string::npos)
        return strcasecmp(relative.c_str(), "resource.dat") == 0;
    const size_t      slash = relative.find('/');
    const std::string dir = relative.substr(0, slash), rest = relative.substr(slash + 1);
    if (rest.find('/') != std::string::npos)
        return false;
    if (dir == "dat")
        return EndsWithI(rest, ".p2f") || EndsWithI(rest, ".loc");
    if (dir == "sfx")
        return EndsWithI(rest, ".sub");
    return dir == "avi";
}

// ---------------------------------------------------------------------------------------------------------------------

namespace
{
struct LauncherState
{
    std::atomic<bool>      done{false};
    std::atomic<bool>      haveAddr{false};
    ENetError              result = kNetSuccess;
    ST::string             message;
    ENetError              addrResult = kNetSuccess;
    ST::string             fileAddr;
    std::atomic<ENetError> netError{kNetSuccess};
    ENetProtocol           netErrorProtocol = kNetProtocolNil;
    uint64_t               downloaded = 0, files = 0, declined = 0;
};
LauncherState* sState = nullptr;

// The launcher's own hsTimer. Natively the launcher is a separate process (plUruLauncher winmain.cpp ICreateProcess
// and ILaunchClientExecutable; plClient win32/winmain.cpp relaunches through it), so the timer its InitializeNetCore
// and PumpNetCore drive (plClientLauncher.cpp) is that process's own staticTimer (hsTimer.cpp), and the client's timer
// is still fresh when plClient's first IUpdate runs: sys is 0, so the first-frame reset runs, and the smoothing buffer
// starts empty (plClient::IUpdate, hsTimer.cpp plTimerShare::IncSysSeconds). RunLauncherStep runs on this one through
// hsTimer::SetTheTimer and hands the client's back when the launcher's net session has closed. It has static storage,
// so a late reader of the old pointer never meets a destroyed object.
plTimerShare sLauncherTimer;

// plClientLauncher::IApproveDownload, plus the browser's rule: no executable is downloaded or run.
bool IApproveDownload(const plFileName& file)
{
    const ST::string name = file.AsString();
    const ST::string ext = file.GetFileExt();
    const bool       executable = ext.compare_i("exe") == 0 || ext.compare_i("dll") == 0 ||
                            ext.compare_i("dylib") == 0 || ext.compare_i("so") == 0 || ext.compare_i("pdb") == 0 ||
                            ext.compare_i("msi") == 0 ||
                            file.GetFileName().compare_i(plManifest::ClientExecutable().AsString()) == 0 ||
                            file.GetFileName().compare_i(plManifest::PatcherExecutable().AsString()) == 0 ||
                            name.find(".app/", ST::case_insensitive) != -1;
    if (executable) {
        ++sState->declined;
        LauncherLine("declined executable '{}'", name);
    }
    return !executable;
}

// What ends the launcher's 10 ms wait early. Natively the launcher's loop (plUruLauncher winmain.cpp PumpMessages)
// pumps the dialog's messages, then the net core, then sleeps 10 ms (plClientLauncher::PumpNetCore,
// kNetCoreUpdateSleepTime); a quit posted by Cancel (PatcherDialogProc, IQuit) or by the patch's end is seen at the
// next pass. Here two events bump sLauncherEvents and wake the wait: the page's Cancel (plEmscriptenLauncherCancel, on
// the page thread) and the patch's end (OnCompletion, on the patcher's worker or the client thread), so the launcher
// quits at once rather than after the sleep (the Mac patcher reacts to both at once too: Mac-Cocoa/PLSPatcher.mm, its
// init and Patcher::IOnPatchComplete). The gatekeeper's address and a net error arrive inside NetClientUpdate on the
// client thread itself, as natively, and are acted on in the same pass: they wake nothing, so the 10 ms between two
// NetClientUpdate calls stays.
std::atomic<uint32_t> sLauncherEvents{0}; // the futex word
std::atomic<bool>     sLauncherCancel{false};
static_assert(sizeof(sLauncherEvents) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free,
              "emscripten_futex_wait waits on a 32-bit word");

void ILauncherWake()
{
    sLauncherEvents.fetch_add(1);
    emscripten_futex_wake(&sLauncherEvents, INT_MAX);
}

// Up to `ms` until sLauncherEvents moves past `seen`. The calls other threads have proxied to this one run before and
// after each wait, as they did around the usleep this replaces (emscripten_thread_sleep, Emscripten's
// system/lib/pthread/library_pthread.c).
void IWaitForLauncherEvent(uint32_t seen, double ms)
{
    emscripten_current_thread_process_queued_calls();
    const double deadline = emscripten_get_now() + ms;
    for (;;) {
        if (sLauncherEvents.load() != seen)
            return;
        const double left = deadline - emscripten_get_now();
        if (left <= 0)
            return;
        emscripten_futex_wait(&sLauncherEvents, seen, left);
        emscripten_current_thread_process_queued_calls();
    }
}
} // namespace

// The launcher dialog's Cancel (plasmaLauncherShow), on the page thread.
extern "C" EMSCRIPTEN_KEEPALIVE void plEmscriptenLauncherCancel()
{
    sLauncherCancel = true;
    ILauncherWake();
}

plEmscriptenPatcher::LauncherResult plEmscriptenPatcher::RunLauncherStep(bool packageInstalled)
{
    // the launcher's process space (sLauncherTimer above); every exit passes the shutdown below, which hands it back
    plTimerShare* const clientTimer = hsTimer::GetTheTimer();
    hsTimer::SetTheTimer(&sLauncherTimer);

    LauncherState state;
    sState = &state;
    const double     t0 = emscripten_get_now();
    const ST::string manifest = plManifest::ClientManifest();
    // Cleared before the overlay exists, so that a Cancel pressed the moment it shows is kept for the
    // loop's first pass, as natively a click queued before the first PumpMessages is dispatched by its first
    // PeekMessage (plUruLauncher winmain.cpp PumpMessages).
    sLauncherCancel = false;
    IOnPage([&] { plasmaLauncherShow(plProduct::ProductString().c_str()); });
    auto setText = [](const ST::string& t) { IOnPage([&] { plasmaLauncherSet(t.c_str(), "", "", 0, -1); }); };
    setText(ST_LITERAL("Connecting...")); // ShowPatcherDialog (plUruLauncher winmain.cpp)

    // plClientLauncher::InitializeNetCore: its shard status timer setup, without the shard status thread (the login
    // dialog shows the status), whose pump below stays (PumpNetCore)
    hsTimer::SetRealTime(true);
    AsyncCoreInitialize();
    NetClientInitialize();
    NetClientSetErrorHandler([](ENetProtocol protocol, ENetError error) {
        // IEapSucksErrorProc (plClientLauncher.cpp) calls IOnNetError (plUruLauncher winmain.cpp), which replaces the
        // error to show (s_error, the last report wins) and quits at the next pass of the message pump.
        if (sState) {
            sState->netErrorProtocol = protocol;
            sState->netError = error;
        }
    });
    NetClientSetTransTimeoutMs(kNetTransTimeout);
    const ST::string* addrs;
    uint32_t          num = GetGateKeeperSrvHostnames(addrs);
    NetCliGateKeeperStartConnect(addrs, num);
    NetCliGateKeeperFileSrvIpAddressRequest(true, [](ENetError result, const ST::string& addr) {
        sState->addrResult = result;
        sState->fileAddr = addr;
        sState->haveAddr = true;
    });
    LauncherLine("net core up; gatekeeper FileSrv address request (isPatcher)");

    bool cancelled = false, patching = false, addrFailed = false;
    for (;;) {
        // The message pump first (plUruLauncher winmain.cpp PumpMessages): a Cancel, a net error or the patch's end
        // quits before the net core is pumped again.
        const uint32_t seen = sLauncherEvents.load();
        if (sLauncherCancel) {
            cancelled = true;
            break;
        }
        if (addrFailed)
            break;
        // IOnNetError quits whatever the launcher is doing, a patch included: plClientLauncher's error proc stays
        // installed until plClientLauncher::ShutdownNetCore.
        // The net errors a patch can meet here come from its file connection: kMaxFailedConnects failed connects or
        // kMaxImmediateDisconnects quick disconnects (plNglFile.cpp CliFileConn::AsyncNotifySocketConnectFailed and
        // AsyncNotifySocketDisconnect). Both report the error without releasing the connection, so the shutdown
        // below waits for it without end (NetClientDestroy -> FileDestroy, its connection-count loop), and no error
        // box follows. Natively it is the same: IOnNetError's quit runs ShutdownNetCore in the dialog's WM_QUIT
        // handler (plUruLauncher winmain.cpp PatcherDialogProc), and the launcher stops responding there.
        if (state.netError != kNetSuccess) {
            state.result = state.netError;
            state.message = ST::format("Protocol: {}", NetProtocolToString(state.netErrorProtocol));
            LauncherLine("net error {} ({}){}; quitting", ST::string::from_wchar(NetErrorAsString(state.result)),
                         state.message, patching ? " during the patch" : "");
            break;
        }
        if (state.done)
            break;

        // plClientLauncher::PumpNetCore
        hsTimer::IncSysSeconds();
        NetClientUpdate();
        if (state.haveAddr && !patching) {
            // plClientLauncher::IGotFileServIPs, which natively runs inside NetClientUpdate's gatekeeper callback
            NetCliGateKeeperDisconnect();
            if (IS_NET_ERROR(state.addrResult)) {
                // its error report (IOnNetError, plUruLauncher winmain.cpp) quits at the next pass, after the wait
                state.result = state.addrResult;
                state.message = ST_LITERAL("Failed to get FileServ addresses");
                addrFailed = true;
                IWaitForLauncherEvent(seen, kNetCoreUpdateSleepMs);
                continue;
            }
            LauncherStatus("FileSrv {}; NetCliFileStartConnect(isPatcher); manifest {}", state.fileAddr, manifest);
            const ST::string fileSrv[] = {state.fileAddr};
            NetCliFileStartConnect(fileSrv, 1, true);
            // plClientLauncher::PatchClient: no self-patch and no redistributables in a browser; the client manifest
            // only.
            setText(ST_LITERAL("Checking for updates...")); // PatchClient's status
            pfPatcher patcher;
            patcher.OnFileDownloadBegin([](const plFileName& file) {
                // IOnDownloadBegin (plUruLauncher winmain.cpp)
                const ST::string msg = ST::format("Downloading... {}", file);
                ++sState->files;
                LauncherStatus("{}", msg);
                IOnPage([&] { plasmaLauncherSet(msg.c_str(), nullptr, nullptr, 0, 0); });
            });
            patcher.OnProgressTick([](uint64_t cur, uint64_t total, const ST::string& speed) {
                // IOnProgressTick (plUruLauncher winmain.cpp)
                sState->downloaded = cur;
                static double last = 0;
                const double  now = emscripten_get_now();
                if (cur < total && now - last < 100)
                    return;
                last = now;
                const ST::string size = ST::format("{} / {}", plFileSystem::ConvertFileSize(cur),
                                                   plFileSystem::ConvertFileSize(total));
                IOnPage([&] { plasmaLauncherSet(nullptr, size.c_str(), speed.c_str(), double(cur), double(total)); });
            });
            patcher.OnFileDownloadDesired(IApproveDownload);
            patcher.OnCompletion([](ENetError result, const ST::string& msg) {
                sState->result = result;
                sState->message = msg;
                sState->done = true;
                ILauncherWake();
            });
            patcher.RequestManifest(manifest);
            patcher.Start();
            patching = true;
        }
        // the sleep of PumpNetCore, which a Cancel or the patch's end cuts short
        IWaitForLauncherEvent(seen, kNetCoreUpdateSleepMs);
    }

    // plClientLauncher::ShutdownNetCore
    NetClientSetErrorHandler(nullptr);
    NetCliGateKeeperDisconnect();
    NetCliFileDisconnect();
    NetClientDestroy();
    AsyncCoreDestroy(kAsyncCoreShutdownTime);
    // The launcher's process ends here natively, and the client runs on a timer the launcher never touched. Every
    // launcher socket is closed by now (SocketDestroy), so nothing reads the launcher's timer any more.
    hsTimer::SetTheTimer(clientTimer);
    // Natively the launcher's process ends here (plUruLauncher/plClientLauncher.cpp ShutdownNetCore),
    // or the macOS patcher runs on the client's own net core and never destroys it (Mac-Cocoa/PLSPatcher.mm). Either
    // way the client keeps the GameMgr receive handler that pfGameMgr's static instance installed when it was
    // constructed. Here the NetClientDestroy above ran GameDestroy in the client's own process, which clears that
    // handler, and nothing else installs it again (GameInitialize does not), so every server GameMgr message would be
    // dropped for the session (RcvdGameMgrMsgTrans::Post). pfGameMgr installs it again, for both Python flavours.
    pfGameMgr::GetInstance()->InstallRecvHandler();
    LauncherLine("net session closed; GameMgr receive handler installed again (pfGameMgr::InstallRecvHandler)");
    IOnPage([] { plasmaLauncherClose(); });
    sState = nullptr;

    const double secs = (emscripten_get_now() - t0) / 1000.0;
    if (cancelled) {
        LauncherLine("cancelled after {.3f} s", secs);
        return LauncherResult::kCancelled;
    }
    if (IS_NET_SUCCESS(state.result)) {
        LauncherLine("manifest {} done in {.3f} s: {} files downloaded, {} bytes, {} executables declined",
                     manifest, secs, state.files, state.downloaded, state.declined);
        return LauncherResult::kContinue;
    }
    if (state.result == kNetErrFileNotFound && packageInstalled) {
        LauncherLine(
            "the shard serves no client manifest {} ({}) after {.3f} s; the deployment's client package "
            "stands in for it",
            manifest, ST::string::from_wchar(NetErrorAsString(state.result)), secs);
        return LauncherResult::kContinue;
    }
    // IOnNetError (plUruLauncher winmain.cpp), shown after the dialog closes (wWinMain, IShowErrorDialog)
    const ST::string error = ST::format("Error: {}\r\n{}", NetErrorAsString(state.result), state.message);
    LauncherLine("FAILED after {.3f} s: {}", secs, error.replace("\r\n", " / "));
    hsMessageBox(error, ST_LITERAL("Error"), hsMessageBoxNormal, hsMessageBoxIconError);
    return LauncherResult::kFailed;
}
