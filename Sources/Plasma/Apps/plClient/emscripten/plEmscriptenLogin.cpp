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

// The page login dialog and the remembered login record (plEmscriptenLogin.h).

#include "plEmscriptenLogin.h"

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_theory/format>
#include <string_theory/string_stream>
#include <type_traits>
#include <vector>

#include "hsStream.h"
#include "plClientLogin.h"
#include "plFileSystem.h"

#ifdef PLASMA_TEST_HOOKS
#include "plEmscriptenTestHooks.h"
#endif

// -----------------------------------------------------------------------------
// The record

static const char kRecordMagic[4] = {'P', 'W', 'L', '1'};

static plFileName IRecordPath(const ST::string& shard)
{
    // One file per shard key (the deployment name, else the server's display name: the registry key of
    // winmain.cpp SaveUserPass and the NSUserDefaults key of PLSLoginController.mm); any other character becomes '_'.
    ST::string_stream name;
    for (const char* c = shard.c_str(); *c; ++c)
        name << ((std::isalnum(static_cast<unsigned char>(*c)) || *c == '-' || *c == '_') ? *c : '_');
    ST::string file = name.to_string();
    if (file.empty())
        file = ST_LITERAL("default");
    return plFileName::Join(plFileSystem::GetUserDataPath(), "WebLogin", ST::format("{}.dat", file));
}

bool plEmscriptenLogin::LoadRecord(const ST::string& shard, Record& record)
{
    record = Record();
    hsUNIXStream stream;
    if (!stream.Open(IRecordPath(shard), "rb"))
        return false;
    char magic[4];
    if (stream.Read(sizeof(magic), magic) != sizeof(magic) || memcmp(magic, kRecordMagic, sizeof(magic)) != 0)
        return false;
    record.username = stream.ReadSafeString();
    record.remember = stream.ReadBool();
    record.hasHash = stream.ReadBool();
    if (record.hasHash &&
        stream.Read(sizeof(record.namePassHash), record.namePassHash) != sizeof(record.namePassHash)) {
        record = Record();
        return false;
    }
    return true;
}

bool plEmscriptenLogin::SaveRecord(const ST::string& shard, const Record& record)
{
    const plFileName path = IRecordPath(shard);
    plFileSystem::CreateDir(path.StripFileName(), true);
    hsUNIXStream stream;
    if (!stream.Open(path, "wb"))
        return false;
    stream.Write(sizeof(kRecordMagic), kRecordMagic);
    stream.WriteSafeString(record.username);
    stream.WriteBool(record.remember);
    stream.WriteBool(record.hasHash);
    if (record.hasHash)
        stream.Write(sizeof(record.namePassHash), record.namePassHash);
    return true;
}

// -----------------------------------------------------------------------------
// The auth-failed text (client thread)

ST::string plEmscriptenLogin::AuthFailedText(ENetError authError)
{
    const char*    str1;
    const char*    str2;
    const wchar_t* wstr;
    AuthFailedStrings(authError, &str1, &str2, &wstr);
    return ST::format("{}\n\n{}", str1, str2 ? ST::string(str2) : ST::string::from_wchar(wstr));
}

// -----------------------------------------------------------------------------
// The dialog (page thread)

// The page half of the dialog, plEmscriptenLogin.js: IDD_URULOGIN_MAIN (win32/res/plClient.rc) and the Mac's
// PLSLoginWindowController.xib as a modal page dialog, with the busy box IDD_AUTHENTICATING. Each function runs on the
// page thread (IOnPage below).
extern "C" {
void plasmaLoginShow(const char* username, int remember, int focus, const char* statusUrl, const char* signupUrl,
                     const char* product, const char* fakePass);
void plasmaLoginArt(const char* name, const uint8_t* data, int size);
int  plasmaLoginTake(char* userBuf, char* passBuf, int size);
void plasmaLoginBusy(int busy);
void plasmaLoginClose();
}

// The client runs on a pthread (-sPROXY_TO_PTHREAD); every DOM call is handed to the page thread and waited for,
// as hsMessageBox_Emscripten.cpp hands its dialog over (the Mac's performSelectorOnMainThread ... waitUntilDone:YES).
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

// The login banner: Mac-Cocoa/banner@2x.png of this repository (the Mac login window's art; the Win32
// dialog draws the same picture from res/banner.bmp), compiled in by CMakeLists.txt.
static const uint8_t kBanner[] = {
#include "plEmscriptenLoginBanner.inc"
};

// The game's own art for the dialog, from the shard's client data in the working directory (the HTTP
// data tree of a served deployment, the per-shard install directory once patched): resource.dat, which the client
// loads at start (plClientLoader::Run) through plClientResMgr::ILoadResources: header,
// version 1, the count, then per resource [u32 name length][name][u32 size][bytes] (a JPEG's size is read by plJPEG
// itself, the same field). Only the PNG bytes of the cursors and the linking-book frames are handed over; a missing or
// unreadable file sends nothing, and the dialog keeps its CSS theme.
static bool IWantedArt(const ST::string& name)
{
    return name == "cursor_up.png" || name == "cursor_poised.png" || name == "cursor_clicked.png" ||
           name == "cursor_disabled.png" ||
           (name.starts_with("xLoading_Linking.") && name.ends_with(".png") && name != "xLoading_Linking_Text.png");
}

static unsigned ISendClientResources()
{
    hsUNIXStream in;
    if (!in.Open("resource.dat", "rb"))
        return 0;
    (void)in.ReadLE32();
    if (in.ReadLE32() != 1)
        return 0;
    const uint32_t       count = in.ReadLE32();
    std::vector<uint8_t> bytes;
    unsigned             sent = 0;
    for (uint32_t i = 0; i < count && !in.AtEnd(); ++i) {
        const uint32_t nameChars = in.ReadLE32();
        if (nameChars == 0 || nameChars > 1024)
            break;
        ST::char_buffer nameBuf;
        nameBuf.allocate(nameChars);
        if (in.Read(nameChars, nameBuf.data()) != nameChars)
            break;
        const ST::string name(nameBuf);
        const uint32_t   size = in.ReadLE32();
        if (size > in.GetSizeLeft())
            break;
        if (!IWantedArt(name)) {
            in.Skip(size);
            continue;
        }
        bytes.resize(size);
        if (in.Read(size, bytes.data()) != size)
            break;
        IOnPage([&] { plasmaLoginArt(name.c_str(), bytes.data(), int(size)); });
        ++sent;
    }
    return sent;
}

void plEmscriptenLogin::Show(const DialogInit& init)
{
    IOnPage([&] {
        plasmaLoginShow(init.username.c_str(), init.remember ? 1 : 0, int(init.focus), init.statusUrl.c_str(),
                        init.signupUrl.c_str(), init.product.c_str(), FAKE_PASS_STRING);
        plasmaLoginArt("banner", kBanner, int(sizeof(kBanner)));
    });
    // After the dialog is up: it never waits for this art.
    const unsigned art = ISendClientResources();
#ifdef PLASMA_TEST_HOOKS
    // What the dialog shows is a harness build's status line, through the client seam's Status; the product prints
    // none.
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->Status(ST::format("login dialog art: banner ({} bytes, compiled in), {} images from resource.dat",
                                 sizeof(kBanner), art));
#else
    (void)art;
#endif
}

// The player's actions wake the client thread. Natively the login dialog and the "Logging in" box are modal dialogs:
// their message loop sleeps until a message arrives, and the player's click (WM_COMMAND: Login or Quit,
// UruLoginDialogProc; Cancel, AuthDialogProc) is handled as soon as it is posted, between the ticks of AUTH_LOGIN_TIMER
// that pump NetCommUpdate every 10 ms (the WM_TIMER cases of both procs). Here the dialog is the page's: each of its
// handlers records the action (L.pending) and then calls plEmscriptenLoginActed on the page thread, which bumps s_acted
// and wakes the client thread waiting in WaitForPlayer. Take notes the count it has answered for in s_taken, before it
// asks the page, so an action recorded while Take runs is either taken now or wakes the next wait (never lost).
static std::atomic<uint32_t> s_acted{0};  // the futex word: one more for each action the page records
static uint32_t              s_taken = 0; // s_acted when Take last asked the page (client thread only)
static_assert(sizeof(s_acted) == sizeof(uint32_t) && std::atomic<uint32_t>::is_always_lock_free,
              "emscripten_futex_wait waits on a 32-bit word");

extern "C" EMSCRIPTEN_KEEPALIVE void plEmscriptenLoginActed()
{
    s_acted.fetch_add(1);
    emscripten_futex_wake(&s_acted, INT_MAX);
}

bool plEmscriptenLogin::WaitForPlayer(double timeoutMs)
{
    // The calls other threads have proxied to this one run before and after each wait, as they did around the usleep
    // this wait replaces (emscripten_thread_sleep, Emscripten's system/lib/pthread/library_pthread.c).
    emscripten_current_thread_process_queued_calls();
    const double deadline = emscripten_get_now() + timeoutMs;
    for (;;) {
        if (s_acted.load() != s_taken)
            return true;
        const double left = deadline - emscripten_get_now();
        if (left <= 0)
            return false;
        // Returns on a wake, at the timeout, or at once when s_acted has already moved; the loop tells them apart.
        emscripten_futex_wait(&s_acted, s_taken, left);
        emscripten_current_thread_process_queued_calls();
    }
}

plEmscriptenLogin::Event plEmscriptenLogin::Take(ST::string& username, ST::string& password, bool& remember)
{
    char userBuf[256];
    char passBuf[256];
    int  got = 0;
    s_taken = s_acted.load();
    IOnPage([&] { got = plasmaLoginTake(userBuf, passBuf, int(sizeof(userBuf))); });
    Event event = Event::kNone;
    switch (got & 0xff) {
        case 1:
            username = ST::string::from_utf8(userBuf);
            password = ST::string::from_utf8(passBuf);
            remember = (got & 256) != 0;
            event = Event::kLogin;
            break;
        case 2:
            event = Event::kQuit;
            break;
        case 3:
            event = Event::kCancel;
            break;
    }
    memset(userBuf, 0, sizeof(userBuf));
    memset(passBuf, 0, sizeof(passBuf));
    return event;
}

void plEmscriptenLogin::SetBusy(bool busy)
{
    IOnPage([&] { plasmaLoginBusy(busy ? 1 : 0); });
}

void plEmscriptenLogin::Close()
{
    IOnPage([&] { plasmaLoginClose(); });
}
