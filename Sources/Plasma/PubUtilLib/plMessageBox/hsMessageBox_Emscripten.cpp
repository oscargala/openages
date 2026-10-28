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

// hsMessageBox for the browser build. The client runs on a pthread
// (-sPROXY_TO_PTHREAD, plClient/CMakeLists.txt); the page's DOM lives on the browser main
// thread. As the Mac client hands the NSAlert to the main thread and waits for it, in hsMessageBox
// (hsMessageBox_Mac.mm:147: performSelectorOnMainThread ... waitUntilDone:YES), this hands
// a modal page dialog to the main thread with emscripten_proxy_sync_with_ctx and blocks the
// calling thread until the player picks a button. The buttons, their order, the default button
// and the returned codes are MessageBoxW's (the flags and the switch on its answer in hsMessageBox,
// hsMessageBox_Win.cpp): the Win32 box is the one whose results match hsMessageBoxResult (hsMessageBox.h)
// for every kind; the Mac casts NSAlert's 1000-based responses (showAlert:, hsMessageBox_Mac.mm) and drops Abort.
// hsMessageBoxPumping (hsMessageBox_Emscripten.h) shows the same dialog without blocking: the
// page thread is asked asynchronously, and the caller's loop runs until the answer wakes it.

#include "hsMessageBox_Emscripten.h"

#include "hsMessageBox.h"
#ifdef PLASMA_TEST_HOOKS
#include "hsMessageBoxTestResponder.h"
#endif

#include <emscripten.h>
#include <emscripten/proxying.h>
#include <emscripten/threading.h>

#include <atomic>
#include <climits>
#include <cstdio>
#include <string_theory/format>
#include <string_theory/string>

namespace
{
struct hsMessageBoxButtons
{
    int                count;
    const char*        labels[3];
    hsMessageBoxResult results[3];
    int                escape; // the button Escape picks, or -1
};

// MessageBoxW's buttons, the flags hsMessageBox sets (MB_OK, MB_ABORTRETRYIGNORE, ...,
// hsMessageBox_Win.cpp:74-87). Escape answers Cancel when the box has a Cancel button, OK on a
// lone OK box, and nothing otherwise (MessageBoxW's documented keyboard behaviour).
hsMessageBoxButtons IButtonsFor(hsMessageBoxKind kind)
{
    switch (kind) {
        case hsMessageBoxAbortRetyIgnore:
            return {3, {"Abort", "Retry", "Ignore"}, {hsMBoxAbort, hsMBoxRetry, hsMBoxIgnore}, -1};
        case hsMessageBoxOkCancel:
            return {2, {"OK", "Cancel"}, {hsMBoxOk, hsMBoxCancel}, 1};
        case hsMessageBoxRetryCancel:
            return {2, {"Retry", "Cancel"}, {hsMBoxRetry, hsMBoxCancel}, 1};
        case hsMessageBoxYesNo:
            return {2, {"Yes", "No"}, {hsMBoxYes, hsMBoxNo}, -1};
        case hsMessageBoxYesNoCancel:
            return {3, {"Yes", "No", "Cancel"}, {hsMBoxYes, hsMBoxNo, hsMBoxCancel}, 2};
        case hsMessageBoxNormal:
        default:
            return {1, {"OK"}, {hsMBoxOk}, 0};
    }
}

// The icon flags hsMessageBox sets, MB_ICONERROR, ... (hsMessageBox_Win.cpp:89-98); anything else is an error icon.
const char* IIconName(hsMessageBoxIcon icon)
{
    switch (icon) {
        case hsMessageBoxIconQuestion:
            return "question";
        case hsMessageBoxIconExclamation:
            return "exclamation";
        case hsMessageBoxIconAsterisk:
            return "asterisk";
        case hsMessageBoxIconError:
        default:
            return "error";
    }
}

struct hsMessageBoxRequest
{
    const char*           caption;
    const char*           message;
    const char*           icon;
    ST::string            labels; // the button labels, one per line
    int                   escape;
    int                   answer;      // the index of the button picked
    std::atomic<uint32_t> answered{0}; // hsMessageBoxPumping: set (and woken) once `answer` holds the answer
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "emscripten_futex_wait waits on a 32-bit word");

// A one-line, quoted rendering for the console line.
ST::string IQuote(const ST::string& text)
{
    return ST::format("\"{}\"", text.replace("\\", "\\\\").replace("\"", "\\\"").replace("\r", "\\r").replace("\n", "\\n"));
}
} // namespace

// Called on the page thread when the player has picked a button: the blocked caller resumes. A
// null ctx is hsMessageBoxPumping's asynchronous request, whose caller waits on `answered`.
extern "C" EMSCRIPTEN_KEEPALIVE void hsMessageBoxEmscriptenAnswer(void* ctx, void* request, int answer)
{
    auto* r = static_cast<hsMessageBoxRequest*>(request);
    r->answer = answer;
    if (ctx) {
        emscripten_proxy_finish(static_cast<em_proxying_ctx*>(ctx));
    } else {
        r->answered.store(1);
        emscripten_futex_wake(&r->answered, INT_MAX);
    }
}

// The page half, hsMessageBox_Emscripten.js. hsMessageBoxShowDialog lays the modal dialog over the page on the browser
// main thread and answers through hsMessageBoxEmscriptenAnswer; hsMessageBoxShowBlocking is the page thread's own
// synchronous box (window.alert/confirm), which returns the index of the button picked.
extern "C" {
void hsMessageBoxShowDialog(void* ctx, void* request, const char* caption, const char* message, const char* icon,
                            const char* labels, int escape);
int  hsMessageBoxShowBlocking(const char* caption, const char* message, int count);
}

static void IShowOnPageThread(em_proxying_ctx* ctx, void* arg)
{
    auto* request = static_cast<hsMessageBoxRequest*>(arg);
    hsMessageBoxShowDialog(ctx, request, request->caption, request->message,
                           request->icon, request->labels.c_str(), request->escape);
}

static void IShowOnPageThreadAsync(void* arg)
{
    IShowOnPageThread(nullptr, arg);
}

// The page dialog, answered by the player (or the harness's default answer), with `pump` run every
// `intervalMs` while it waits when a pump is given.
static hsMessageBoxResult IShowAndWait(const ST::string& message, const ST::string& caption, hsMessageBoxKind kind,
                                       hsMessageBoxIcon icon, const std::function<void()>* pump, double intervalMs)
{
    if (hsMessageBox_SuppressPrompts)
        return hsMBoxOk;

    const hsMessageBoxButtons buttons = IButtonsFor(kind);
    hsMessageBoxRequest       request{caption.c_str(), message.c_str(), IIconName(icon), {}, buttons.escape, 0};
    for (int i = 0; i < buttons.count; ++i)
        request.labels += i ? ST::format("\n{}", buttons.labels[i]) : ST::string(buttons.labels[i]);

    // A harness build may answer with the default button instead of waiting for a
    // player, so that a test run with no player never hangs on a box (the message-box test seam,
    // hsMessageBoxTestResponder.h). The product build always waits for the player.
    bool autoAnswer = false;
#ifdef PLASMA_TEST_HOOKS
    if (hsMessageBoxTestResponder* responder = hsMessageBoxTestResponder::Get())
        autoAnswer = responder->GetAnswerPolicy() == hsMessageBoxTestResponder::kAnswerAuto;
#endif
    const char* by = autoAnswer ? "harness-default" : "player";

    std::printf("[MSGBOX] show caption=%s icon=%s buttons=%s default=%s answer-by=%s text=%s\n",
                IQuote(caption).c_str(), request.icon, request.labels.replace("\n", "|").c_str(), buttons.labels[0],
                by, IQuote(message).c_str());
    std::fflush(stdout);

    if (autoAnswer) {
        request.answer = 0;
    } else if (emscripten_is_main_browser_thread()) {
        request.answer = hsMessageBoxShowBlocking(request.caption, request.message, buttons.count);
    } else if (pump) {
        // The dialog is shown by a call queued for the page thread; its answer stores `answered` and wakes the
        // futex wait below. Until then the caller's loop runs every intervalMs, as the owner's timer does behind a
        // Win32 modal box. The calls other threads have proxied to this one run around each wait, as they do around
        // the client's other waits.
        if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
                                    IShowOnPageThreadAsync, &request)) {
            std::printf("[MSGBOX] the page thread could not be reached; answered Cancel\n");
            std::fflush(stdout);
            return hsMBoxCancel;
        }
        while (request.answered.load() == 0) {
            (*pump)();
            emscripten_current_thread_process_queued_calls();
            emscripten_futex_wait(&request.answered, 0, intervalMs);
        }
        emscripten_current_thread_process_queued_calls();
    } else if (!emscripten_proxy_sync_with_ctx(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(),
                                               IShowOnPageThread, &request)) {
        std::printf("[MSGBOX] the page thread could not be reached; answered Cancel\n");
        std::fflush(stdout);
        return hsMBoxCancel;
    }

    if (request.answer < 0 || request.answer >= buttons.count)
        request.answer = 0;
    const hsMessageBoxResult result = buttons.results[request.answer];
    std::printf("[MSGBOX] answer %s (hsMessageBoxResult %d) by %s\n", buttons.labels[request.answer], int(result), by);
    std::fflush(stdout);
    return result;
}

hsMessageBoxResult hsMessageBox(const ST::string& message, const ST::string& caption, hsMessageBoxKind kind, hsMessageBoxIcon icon)
{
    return IShowAndWait(message, caption, kind, icon, nullptr, 0);
}

hsMessageBoxResult hsMessageBoxPumping(const ST::string& message, const ST::string& caption, hsMessageBoxKind kind,
                                       hsMessageBoxIcon icon, const std::function<void()>& pump, double intervalMs)
{
    return IShowAndWait(message, caption, kind, icon, &pump, intervalMs);
}
