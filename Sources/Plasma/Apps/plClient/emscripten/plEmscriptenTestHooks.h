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

// The client test seam: where the private test harness's instruments meet the browser client's frame loop, its
// login, its exits and the three points of plClient's frame the test harness's pins need. The product build
// (PLASMA_TEST_HOOKS OFF) has none of it: this header declares nothing there, and every call site is under
// #ifdef PLASMA_TEST_HOOKS. In a harness build the pointer is null until plTestHooks::Install sets it, and every
// default does nothing, so a call site reads
//     if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
//         hooks->BeforeFrame();
// Every member runs on the client thread.
#ifndef plEmscriptenTestHooks_inc
#define plEmscriptenTestHooks_inc

#ifdef PLASMA_TEST_HOOKS

#include <cstddef>
#include <string_theory/string>

class plCmdParser;
class plPipeline;

class plEmscriptenTestHooks
{
public:
    // How a harness login ended (Login).
    enum class LoginResult
    {
        kNoLogin,  // the harness asked for none: the client starts in StartUp without an account
        kLoggedIn, // the account the harness gave is logged in
        kFailed,   // no account, no answer or an auth error: the client shuts down and the program ends with 2
    };

    // Where the program ends (OnExit).
    enum class ExitPoint
    {
        kLauncher, // the patcher launcher's step was cancelled or failed: no client and no net exist yet
        kLogin,    // the login ended the program, before the client's loop (main.cpp ClientFinish)
        kClient,   // the client's loop ended, or the client could not start it (main.cpp ClientFinish)
    };

    virtual ~plEmscriptenTestHooks() = default;

    // First thing in main(), with its start time (emscripten_get_now(), ms): the time base of the
    // status lines, which the frame loop and the WebGPU pipeline log against too. The harness
    // installs its SDL time codec here (plSDLTimeCodec.h), before the client starts.
    virtual void OnMain(double bootMs) {}

    // A line about the client's progress: its boot milestones, its file systems, its login and the
    // device. The product build has none of these lines; it reports only its failures, on stderr.
    virtual void Status(const ST::string& line) {}

    // In main.cpp MountFileSystems, after the game data and before the fonts and the user folder: the harness build's
    // own trees (the private test harness's client game code). mountTree mounts one fetch tree as
    // main.cpp mounts the game data's (MountFetchTree) and sets files to its file count. false ends the start-up as a
    // failed file system does.
    virtual bool MountFiles(bool (*mountTree)(const char* mountPoint, const char* url, size_t& files)) { return true; }

    // Before the user folder is mounted (main.cpp MountUserData): whether it lives in memory
    // instead of OPFS. The product always asks for OPFS, and falls back to memory where OPFS cannot be mounted.
    virtual bool UserFilesInMemory() { return false; }

    // Once, before the client loads: harness flags, setters and boot lines, and the hooks of
    // the age-instance pin and the fixed time at an Age's load in the engine libraries.
    virtual void OnClientInit(const plCmdParser& args) {}

    // Right after the command line's "args parsed" line, with its local-data flags (/LocalData, /LocalPython,
    // /LocalSDL): the harness build's checks of its own game code against them. A result other than 0 ends the
    // program with that exit code.
    virtual int OnArgsParsed(bool localData, bool localPython, bool localSdl) { return 0; }

    // After NetCommStartup: whether the client shows the player's login dialog (the product always
    // does), or calls Login instead.
    virtual bool LoginDialog() { return true; }

    // After NetCommStartup, unless the client shows the player's login dialog: the harness's own login
    // with the account the page hands over, in place of the dialog. pumpMessageQueue is the client thread's
    // message pump (main.cpp PumpMessageQueueProc), which the wait for the auth result runs as the dialog's does.
    virtual LoginResult Login(void (*pumpMessageQueue)()) { return LoginResult::kNoLogin; }

    // Once the client has started (plClientLoader::StartClient), before its first MainLoop: the
    // timed and tick-scheduled console lines and the loop's clock.
    virtual void OnLoopStart() {}

    // Before each plClient::MainLoop: counts the frames.
    virtual void BeforeFrame() {}

    // After each MainLoop of a client that is not done, once the message queue has run (false ends the loop): the
    // console lines, the time limits, the status lines and the 1 Hz report.
    virtual bool AfterFrame() { return true; }

    // Before the shutdown of a client that got past its login: the loop's end line.
    virtual void BeforeShutdown() {}

    // At the end of the program, with its exit code, once the C runtime's buffers are flushed: export the client's
    // files to the page and print the exit lines.
    virtual void OnExit(ExitPoint point, int exitCode) {}

    // Top of plClient::IDraw, after the cameras' update and before the progress check, so also on
    // the frames that draw only the progress bar: the virtual camera's pin.
    virtual void BeforeDraw(plPipeline* pipeline) {}

    // plClient::IDraw after the progress check, before the visibility evaluation: the frames that
    // draw the scene. The fixed time and time of day, then the avatar, field-of-view and camera pins.
    virtual void BeforeDrawScene(plPipeline* pipeline) {}

    // plClient::IUpdate, after hsTimer::IncSysSeconds, before plNetClientMgr::Update: the physics accumulator pin.
    virtual void AfterIncSysSeconds() {}

    static plEmscriptenTestHooks* Get() { return fHooks; }
    static void                   Set(plEmscriptenTestHooks* hooks) { fHooks = hooks; }

private:
    static inline plEmscriptenTestHooks* fHooks = nullptr;
};

namespace plTestHooks
{
// Installs the overlay's implementations of every test seam: this one, plWGPUPipelineObserver,
// hsMessageBoxTestResponder, plEmscriptenFrameLoopObserver, plResPatcherObserver and plPythonInitHooks. Defined in
// the private test harness (library plTestHooks); plEmscriptenRuntime.cpp calls it once.
void Install();
} // namespace plTestHooks

#endif // PLASMA_TEST_HOOKS

#endif // plEmscriptenTestHooks_inc
