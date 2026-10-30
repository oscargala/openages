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

// The Emscripten entry point of plClient.
//
// Built with -sPROXY_TO_PTHREAD: main() runs on a pthread (the client thread), and the browser's main thread stays free
// for the work proxied to it (file fetches, OpenAL, screen queries, thread creation).
//
// The startup is win32/winmain.cpp's wWinMain, the most complete native sequence, after the browser's own step, the
// virtual file system (WasmFS), and with the window-system pieces replaced by their browser counterparts:
//   arguments -> general.ini -> DebugInit -> server.ini -> display helper + plClientLoader::Init -> NetCommStartup
//   -> login dialog -> wait for the loader -> plDisplayScaleChangedMsg -> StartClient -> MainLoop.
// From plClientLoader::Init on, the startup and MainLoop run as states of the frame loop (below).
//
// A harness build (PLASMA_TEST_HOOKS) is driven through the client seam, plEmscriptenTestHooks.h. The product build has
// none of it.

#include <dirent.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#include <emscripten/wasmfs.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_theory/format>
#include <unordered_map>
#include <vector>

#include "HeadSpin.h"
#include "hsDebug.h"
#include "plClient.h"
#include "plClientLoader.h"
#include "plClientLogin.h"
#include "plCmdParser.h"
#include "plEmscriptenCwd.h"
#include "plEmscriptenDisplay.h"
#include "plEmscriptenFrameHooks.h"
#include "plEmscriptenFrameLoop.h"
#include "plEmscriptenLogin.h"
#include "plEmscriptenPatcher.h"
#include "plEmscriptenRuntime.h"
#include "plEmscriptenStorage.h"
#include "plFileSystem.h"
#include "plProduct.h"
#ifdef PLASMA_TEST_HOOKS
#include "plEmscriptenTestHooks.h"
#endif
#ifdef PLASMA_PIPELINE_WGPU
#include "pfWGPUPipeline/plWGPUPipeline.h"
#endif

#include "pfConsoleCore/pfConsoleEngine.h"
#include "pfConsoleCore/pfServerIni.h"
#include "pfDisplayHelpers/plEmscriptenDisplayHelper.h"
#include "pfPatcher/pfPatcher.h"
#include "plMessage/plDisplayScaleChangedMsg.h"
#include "plMessageBox/hsMessageBox.h"
#include "plMessageBox/hsMessageBox_Emscripten.h"
#include "plNetClientComm/plNetClientComm.h"
#include "plNetGameLib/plNetGameLib.h"
#include "plPhysX/plPXSimulation.h"
#include "plPipeline/hsG3DDeviceSelector.h"
#include "plProgressMgr/plProgressMgr.h"
#include "plResMgr/plVersion.h"
#include "plStatusLog/plStatusLog.h"
#include "pnMessage/plMessage.h"
#include "pnNetBase/pnNbSrvs.h"

extern bool gDataServerLocal;
extern bool gPythonLocal;
extern bool gSDLLocal;

// Native-window hooks of plClient (win32/plClient_Win.cpp resizes and shows the HWND). The canvas is the client's
// window: a resize goes to the page and the fullscreen layer (plEmscriptenDisplay.h). The page shows the canvas
// itself, so the other hooks do nothing, as in the native clients without a window system.
void plClient::IResizeNativeDisplayDevice(int width, int height, bool windowed)
{
    plEmscriptenDisplay::ResizeNative(width, height, windowed);
}
void plClient::IChangeResolution(int width, int height) {}
void plClient::IUpdateProgressIndicator(plOperationProgress* progress) {}
void plClient::ShowClientWindow() {}
void plClient::FlashWindow() {}

enum
{
    kArgSkipLoginDialog,
    kArgServerIni,
    kArgLocalData,
    kArgLocalPython,
    kArgLocalSDL,
    kArgPlayerId,
    kArgStartUpAgeName,
    kArgPvdFile,
    kArgSkipIntroMovies,
    kArgRenderer
};

static const plCmdArgDef s_cmdLineArgs[] = {
    {kCmdArgFlagged | kCmdTypeBool, "SkipLoginDialog", kArgSkipLoginDialog},
    {kCmdArgFlagged | kCmdTypeString, "ServerIni", kArgServerIni},
    {kCmdArgFlagged | kCmdTypeBool, "LocalData", kArgLocalData},
    {kCmdArgFlagged | kCmdTypeBool, "LocalPython", kArgLocalPython},
    {kCmdArgFlagged | kCmdTypeBool, "LocalSDL", kArgLocalSDL},
    {kCmdArgFlagged | kCmdTypeInt, "PlayerId", kArgPlayerId},
    {kCmdArgFlagged | kCmdTypeString, "Age", kArgStartUpAgeName},
    {kCmdArgFlagged | kCmdTypeString, "PvdFile", kArgPvdFile},
    {kCmdArgFlagged | kCmdTypeBool, "SkipIntroMovies", kArgSkipIntroMovies},
    {kCmdArgFlagged | kCmdTypeString, "Renderer", kArgRenderer},
};

static plClientLoader gClient;
static plStatusLog*   s_DebugLog = nullptr;

PF_CONSOLE_LINK_ALL()

// -----------------------------------------------------------------------------
// Status lines
//
// CLIENT_STATUS reports the client's progress; CLIENT_FAILURE a failure that ends the program or changes what it can
// do. A harness build sends both to the client seam. The product prints only the failures, on stderr, which the page
// mirrors to its console: most happen before the file systems exist, so no log file could hold them.
#ifdef PLASMA_TEST_HOOKS
static void IClientStatus(const ST::string& line)
{
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->Status(line);
}
#define CLIENT_STATUS(...) IClientStatus(ST::format(__VA_ARGS__))
#define CLIENT_FAILURE(...) IClientStatus(ST::format(__VA_ARGS__))
#else
#define CLIENT_STATUS(...) ((void)0)
#define CLIENT_FAILURE(...) hsDebugPrintToTerminal(ST::format(__VA_ARGS__))
#endif

static double s_bootStart = 0.0;

// -----------------------------------------------------------------------------
// The virtual file system
//
// The game data (dat 1.8 GB, sfx 2.1 GB, python, SDL, ...) is served over HTTP and read lazily through WasmFS fetch
// backends: a file's first stat is one HEAD request, and reads fetch only the aligned Range chunks they touch
// (plEmscriptenFetchBackend.js). A fetch directory cannot list the server, so the tree is created from the manifest
// at <base>/.fetch-manifest ("D <path>", "F <mtime> <path>"); the mtimes keep Python's .pyc checks working as on disk.
// The user's files and the private .plasma-cwd directory use OPFS when available. The working directory merges the
// saved files with the immutable assets, whose names win.

// The client's working directory, where the game's files appear to it.
static constexpr char     kGameRoot[] = "/uru";
// The fetch backend's HTTP Range size in bytes: a read fetches the aligned chunks of this size it touches.
static constexpr uint32_t kFetchChunk = 65536;

static std::string EnvOr(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}

static bool MkdirAll(const std::string& path)
{
    std::string partial;
    size_t      pos = 0;
    while (pos != std::string::npos) {
        pos = path.find('/', pos + 1);
        partial = path.substr(0, pos);
        if (partial.empty())
            continue;
        if (mkdir(partial.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

static bool MountFetchTree(const std::string& mountPoint, const std::string& baseUrl, uint32_t chunkSize,
                           size_t& dirCount, size_t& fileCount, bool skipSound = false,
                           bool (*keep)(const std::string& relative, bool directory) = nullptr)
{
    backend_t backend = wasmfs_create_fetch_backend(baseUrl.c_str(), chunkSize);
    if (!backend) {
        CLIENT_FAILURE("fetch backend for {} failed", baseUrl);
        return false;
    }
    // wasmfs_create_directory/wasmfs_create_file return -errno (they do not set errno).
    if (int err = wasmfs_create_directory(const_cast<char*>(mountPoint.c_str()), 0777, backend)) {
        CLIENT_FAILURE("mount {} failed: {}", mountPoint, err);
        return false;
    }

    std::string manifestPath = mountPoint + "/.fetch-manifest";
    int         fd = wasmfs_create_file(manifestPath.c_str(), 0444, backend);
    if (fd < 0) {
        CLIENT_FAILURE("manifest file {} failed: {}", manifestPath, fd);
        return false;
    }
    std::string manifest;
    char        buf[64 * 1024];
    ssize_t     n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        manifest.append(buf, size_t(n));
    close(fd);
    if (manifest.empty()) {
        CLIENT_FAILURE("manifest {} is empty (served at {}/.fetch-manifest?)", manifestPath, baseUrl);
        return false;
    }

    size_t pos = 0;
    while (pos < manifest.size()) {
        size_t end = manifest.find('\n', pos);
        if (end == std::string::npos)
            end = manifest.size();
        std::string line = manifest.substr(pos, end - pos);
        pos = end + 1;
        if (line.size() < 3)
            continue;
        const size_t      pathOffset = line[0] == 'F' ? line.find(' ', 2) + 1 : 2;
        const std::string relative = line.substr(pathOffset);
        if (skipSound && (relative == "sfx" || relative.rfind("sfx/", 0) == 0))
            continue;
        if (keep && !keep(relative, line[0] == 'D')) // the client package's entries only
            continue;
        if (line[0] == 'D') {
            std::string path = mountPoint + "/" + line.substr(2);
            if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
                CLIENT_FAILURE("mkdir {} failed: errno {}", path, errno);
                return false;
            }
            dirCount++;
        } else if (line[0] == 'F') {
            size_t sp = line.find(' ', 2);
            if (sp == std::string::npos)
                continue;
            long long   mtime = std::atoll(line.c_str() + 2);
            std::string path = mountPoint + "/" + line.substr(sp + 1);
            int         ffd = wasmfs_create_file(path.c_str(), 0444, backend);
            if (ffd < 0) {
                CLIENT_FAILURE("create {} failed: {}", path, ffd);
                return false;
            }
            close(ffd);
            struct timespec times[2];
            times[0].tv_sec = times[1].tv_sec = time_t(mtime);
            times[0].tv_nsec = times[1].tv_nsec = 0;
            utimensat(AT_FDCWD, path.c_str(), times, 0);
            fileCount++;
        }
    }
    return true;
}

// Patcher mode (PLASMA_DATA_FILES=patcher, plEmscriptenPatcher.h): the working directory is the
// deployment's OPFS install directory, filled by the original patchers. A deployment that names a data tree there
// names its client package (the files a native installer carries beyond the shard's manifests): missing or
// different-size package files are copied into the install directory before the client starts, as an install would.
static bool s_patcherMode = false;
static bool s_packageInstalled = false;
static bool s_storageFailed = false; // the storage error box was shown; the run ends with 2
static int  s_packageWriteError = 0; // errno of a failed package write

// dry: count the files and bytes that would be copied (missing or different size), copy nothing.
static bool InstallPackageTree(const std::string& from, const std::string& to, size_t& copied, size_t& present,
                               uint64_t& bytes, bool dry = false)
{
    DIR* dir = opendir(from.c_str());
    if (!dir)
        return false;
    bool ok = true;
    while (dirent* de = readdir(dir)) {
        const std::string name = de->d_name;
        if (name == "." || name == ".." || name == ".fetch-manifest")
            continue;
        const std::string src = from + "/" + name, dst = to + "/" + name;
        struct stat       sst, dstst;
        if (stat(src.c_str(), &sst) != 0) {
            ok = false;
            continue;
        }
        if (S_ISDIR(sst.st_mode)) {
            if ((!dry && mkdir(dst.c_str(), 0777) != 0 && errno != EEXIST) ||
                !InstallPackageTree(src, dst, copied, present, bytes, dry))
                ok = false;
            continue;
        }
        if (stat(dst.c_str(), &dstst) == 0 && dstst.st_size == sst.st_size) {
            ++present;
            continue;
        }
        if (dry) {
            ++copied;
            bytes += uint64_t(sst.st_size);
            continue;
        }
        // The errno of the call that failed is taken at once: the close() calls below may change errno.
        int error = 0;
        int in = open(src.c_str(), O_RDONLY);
        if (in < 0)
            error = errno;
        int out = in >= 0 ? open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
        if (in >= 0 && out < 0)
            error = errno;
        std::vector<char> buf(1 << 20);
        bool              fileOk = in >= 0 && out >= 0;
        while (fileOk) {
            const ssize_t n = read(in, buf.data(), buf.size());
            if (n == 0)
                break;
            if (n < 0) {
                error = errno;
                fileOk = false;
                break;
            }
            // A short write is continued, so that a failing write reports its errno (a full quota: EIO, ENOSPC). A
            // write that makes no progress and reports no error ends the copy with errno 0.
            for (ssize_t done = 0; fileOk && done < n;) {
                const ssize_t w = write(out, buf.data() + done, size_t(n - done));
                if (w <= 0) {
                    error = w < 0 ? errno : 0;
                    fileOk = false;
                } else {
                    done += w;
                }
            }
        }
        if (in >= 0) close(in);
        if (out >= 0) close(out);
        if (!fileOk) {
            // never leave a partial copy (its size differs, so the next start copies it again, but a removed file is
            // plainer); a storage error stops the install.
            unlink(dst.c_str());
            s_packageWriteError = error;
            CLIENT_FAILURE("fs: package install of {} failed: errno {} (partial file removed)", dst, error);
            ok = false;
            if (error == EIO || error == ENOSPC || error == EDQUOT)
                break;
            continue;
        }
        ++copied;
        bytes += uint64_t(sst.st_size);
    }
    closedir(dir);
    return ok;
}

static bool MountPatcherInstall(const std::string& userData, uint32_t chunk)
{
    const std::string root = userData + "/PatcherData/" + plEmscriptenPatcher::InstallDirName();
    if (!MkdirAll(root)) {
        CLIENT_FAILURE("fs: patcher install directory {} failed: errno {}", root, errno);
        return false;
    }
    const char* packageUrl = std::getenv("PLASMA_DATA_URL");
    if (packageUrl && *packageUrl) {
        const double      t0 = emscripten_get_now();
        size_t            dirs = 0, files = 0, copied = 0, present = 0;
        uint64_t          bytes = 0;
        const std::string package = "/.plasma-package";
        if (!MountFetchTree(package, packageUrl, chunk, dirs, files, false, plEmscriptenPatcher::PackageEntry))
            return false;
        // Before writing: does the package fit the origin's storage (navigator.storage.estimate)? If not, the
        // launcher's error box (plUruLauncher winmain.cpp: IOnNetError's "Error: {}\r\n{}", shown by
        // IShowErrorDialog), and the run ends as a failed launcher step does (exit 2).
        auto storageError = [&](const ST::string& what) {
            const ST::string error = ST::format("Error: {}\r\n{}",
                                                ST::string::from_wchar(NetErrorAsString(kNetErrInternalError)), what);
            CLIENT_FAILURE("storage: {}", error.replace("\r\n", " / "));
            hsMessageBox(error, ST_LITERAL("Error"), hsMessageBoxNormal, hsMessageBoxIconError);
            s_storageFailed = true;
            return false;
        };
        {
            size_t   need = 0, have = 0;
            uint64_t needBytes = 0;
            InstallPackageTree(package, root, need, have, needBytes, true);
            double         usage = -1, quota = -1;
            const bool     known = plEmscriptenStorage::Estimate(usage, quota);
            const uint64_t available = known ? (quota > usage ? uint64_t(quota - usage) : 0) : 0;
            CLIENT_STATUS("storage: client package needs {} files, {} B; estimate usage {.0f} B, quota {.0f} B{}", need,
                          needBytes, usage, quota, known ? "" : " (unknown: not checked)");
            if (known && needBytes > available)
                return storageError(ST::format(
                    "Not enough browser storage for the client files: {} needed, "
                    "{} available",
                    plFileSystem::ConvertFileSize(needBytes),
                    plFileSystem::ConvertFileSize(available)));
        }
        if (!InstallPackageTree(package, root, copied, present, bytes)) {
            CLIENT_FAILURE("fs: client package install into {} failed", root);
            if (s_packageWriteError == EIO || s_packageWriteError == ENOSPC || s_packageWriteError == EDQUOT)
                return storageError(ST_LITERAL("Not enough browser storage to write the client files"));
            return false;
        }
        s_packageInstalled = true;
        CLIENT_STATUS("fs: client package {} ({} files): {} installed ({} bytes), {} already present, {.3f} s",
                      packageUrl, files, copied, bytes, present, (emscripten_get_now() - t0) / 1000.0);
    } else {
        CLIENT_STATUS("fs: no client package (the launcher step's client manifest provides it)");
    }
    if (chdir(root.c_str()) != 0) {
        CLIENT_FAILURE("chdir {} failed: errno {}", root, errno);
        return false;
    }
    plEmscriptenCwd::SetGameDataRoot(root.c_str());
    s_patcherMode = true;
    CLIENT_STATUS("fs: patcher mode: cwd {} (OPFS install directory of this deployment; the original patchers fill it)",
                  root);
    return true;
}

// The game data and sound trees (http mode), mounted at the temporary assetRoot that
// MountWorkingDirectory composes into the working directory.
static bool MountGameData(const std::string& assetRoot, uint32_t chunk)
{
    const std::string gameRoot = kGameRoot;
    const std::string dataUrl = EnvOr("PLASMA_DATA_URL", "data");
    const std::string soundUrl = EnvOr("PLASMA_SOUND_URL", "");
    size_t            dirs = 0, files = 0;
    if (!MountFetchTree(assetRoot, dataUrl, chunk, dirs, files, !soundUrl.empty()))
        return false;
    if (!soundUrl.empty() && !MountFetchTree(assetRoot + "/sfx", soundUrl, chunk, dirs, files))
        return false;
    CLIENT_STATUS("fs: game data {} -> {} (fetch, chunk {} B): {} dirs, {} files", dataUrl, gameRoot, chunk, dirs,
                  files);
    return true;
}

// plTextFont looks its faces up as /fonts/<face>.ttf on Emscripten.
static bool MountFonts()
{
    const std::string fontsUrl = EnvOr("PLASMA_FONTS_URL", "fonts");
    size_t            fdirs = 0, ffiles = 0;
    if (!MountFetchTree("/fonts", fontsUrl, 0, fdirs, ffiles))
        return false;
    if (int err = plEmscriptenCwd::MakeReadOnly("/fonts")) {
        CLIENT_FAILURE("fs: read-only fonts failed: {}", err);
        return false;
    }
    CLIENT_STATUS("fs: fonts {} -> /fonts (fetch): {} files", fontsUrl, ffiles);
    return true;
}

// Static initializers run before main() and already called plFileSystem::GetLogPath(), which creates <userData>/Log
// in the memory file system. WasmFS cannot mount a backend on an existing path, so those empty directories go first;
// GetLogPath() and GetInitPath() create them again on every call.
static void RemoveStartupUserDirs(const std::string& userData)
{
    size_t                                  removed = 0;
    std::function<bool(const std::string&)> rmEmptyTree = [&](const std::string& d) -> bool {
        DIR* dir = opendir(d.c_str());
        if (!dir)
            return errno == ENOENT;
        bool ok = true;
        while (dirent* de = readdir(dir)) {
            std::string n = de->d_name;
            if (n == "." || n == "..")
                continue;
            std::string full = d + "/" + n;
            struct stat cst;
            if (stat(full.c_str(), &cst) != 0 || !S_ISDIR(cst.st_mode) || !rmEmptyTree(full)) {
                CLIENT_FAILURE("fs: {} existed before main() and is not empty: {}", userData, full);
                ok = false;
            }
        }
        closedir(dir);
        if (ok && rmdir(d.c_str()) == 0)
            removed++;
        return ok;
    };
    rmEmptyTree(userData);
    if (removed)
        CLIENT_STATUS("fs: removed {} empty directories that static initializers created under {}", removed, userData);
}

// plFileSystem::GetUserDataPath() on Emscripten: $HOME/.config/<LongName>, HOME being /home/web_user.
static std::string UserDataPath()
{
    const char* home = std::getenv("HOME");
    return ST::format("{}/.config/{}", home ? home : "/home/web_user", plProduct::LongName()).c_str();
}

// The user folder: plFileSystem::GetUserDataPath(), the profile (Init, logs, the remembered login).
static bool MountUserData(const std::string& userData)
{
    std::string parent = userData.substr(0, userData.rfind('/'));
    if (!MkdirAll(parent)) {
        CLIENT_FAILURE("mkdir {} failed: errno {}", parent, errno);
        return false;
    }
    RemoveStartupUserDirs(userData);
    // The user folder lives in OPFS, or in memory where OPFS cannot be mounted (below). A harness build may ask for
    // memory from the start (the client seam's UserFilesInMemory); the product always asks for OPFS.
    bool userFilesOpfs = true;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        userFilesOpfs = !hooks->UserFilesInMemory();
#endif
    backend_t userBackend = nullptr;
    int       mountErr = 0;
    if (userFilesOpfs) {
        userBackend = wasmfs_create_opfs_backend();
        mountErr = userBackend ? wasmfs_create_directory(const_cast<char*>(userData.c_str()), 0777, userBackend) : -1;
    }
    if (userBackend && mountErr != 0 && wasmfs_get_backend_by_path(userData.c_str()) == userBackend) {
        CLIENT_STATUS("fs: OPFS mount returned {} but {} is on the OPFS backend", mountErr, userData);
        mountErr = 0;
    }
    if (userBackend && mountErr == 0) {
        CLIENT_STATUS("fs: user data {} (OPFS)", userData);
    } else {
        if (userFilesOpfs) {
            CLIENT_FAILURE("fs: OPFS mount failed ({}), using memory", mountErr);
            for (std::string p : {std::string("/home"), std::string("/home/web_user"), parent, userData}) {
                struct stat st;
                int         r = stat(p.c_str(), &st);
                char        mode[16];
                std::snprintf(mode, sizeof(mode), "%o", r == 0 ? unsigned(st.st_mode) : 0u);
                CLIENT_FAILURE("fs:   stat {} -> {} mode {}", p, r == 0 ? 0 : errno, mode);
            }
        }
        if (mkdir(userData.c_str(), 0777) != 0 && errno != EEXIST) {
            CLIENT_FAILURE("mkdir {} failed: errno {}", userData, errno);
            return false;
        }
        CLIENT_STATUS("fs: user data {} (memory)", userData);
    }
    return true;
}

// http mode: the working directory merges the game data at assetRoot with a private writable directory in the
// user folder (plEmscriptenCwd::Mount).
static bool MountWorkingDirectory(const std::string& userData, const std::string& assetRoot)
{
    const std::string gameRoot = kGameRoot;
    const std::string cwdData = userData + "/.plasma-cwd";
    if (!MkdirAll(cwdData)) {
        CLIENT_FAILURE("fs: private cwd directory failed: errno {}", errno);
        return false;
    }
    if (int err = plEmscriptenCwd::Mount(gameRoot.c_str(), cwdData.c_str(), assetRoot.c_str())) {
        CLIENT_FAILURE("fs: private writable cwd mount failed: {}", err);
        return false;
    }
    CLIENT_STATUS("fs: cwd {} -> private .plasma-cwd (immutable asset names take precedence)", gameRoot);
    if (chdir(gameRoot.c_str()) != 0) {
        CLIENT_FAILURE("chdir {} failed: errno {}", gameRoot, errno);
        return false;
    }
    return true;
}

#ifdef PLASMA_TEST_HOOKS
// The client seam's MountFiles (plEmscriptenTestHooks.h): a fetch tree of a harness build's own, read in the game
// data's chunks.
static bool IMountHarnessTree(const char* mountPoint, const char* url, size_t& files)
{
    size_t dirs = 0;
    return MountFetchTree(mountPoint, url, kFetchChunk, dirs, files);
}
#endif

// The file systems, which every later start-up step reads (ClientMain): the game data (http mode), a harness build's
// own trees, the fonts and the user folder, then the working directory (patcher mode: the install directory that
// MountPatcherInstall fills).
static bool MountFileSystems()
{
    const uint32_t    chunk = kFetchChunk;
    const std::string assetRoot = "/.plasma-assets"; // Temporary, removed after private cwd composition.
    const bool        patcher = EnvOr("PLASMA_DATA_FILES", "http") == "patcher";
    if (!patcher && !MountGameData(assetRoot, chunk))
        return false;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get(); hooks && !hooks->MountFiles(IMountHarnessTree))
        return false;
#endif
    if (!MountFonts())
        return false;
    const std::string userData = UserDataPath();
    if (!MountUserData(userData))
        return false;
    if (patcher)
        return MountPatcherInstall(userData, chunk);
    return MountWorkingDirectory(userData, assetRoot);
}

// -----------------------------------------------------------------------------
// Native sequence (winmain.cpp)

static void _StatusMessageProc(const ST::string& msg)
{
    s_DebugLog->AddLine(msg);
}

static void DebugInit()
{
    // winmain.cpp DebugInit. kDebugOutput additionally echoes each line to stderr
    // (plStatusLog::IPrintLineToFile), which the page mirrors to its console.
    plStatusLogMgr& mgr = plStatusLogMgr::GetInstance();
    s_DebugLog = mgr.CreateStatusLog(30, "plasmadbg.log",
                                     plStatusLog::kFilledBackground | plStatusLog::kDeleteForMe |
                                         plStatusLog::kAlignToTop | plStatusLog::kTimestamp |
                                         plStatusLog::kDebugOutput);
    hsSetStatusMessageProc(_StatusMessageProc);
}

// -----------------------------------------------------------------------------
// Login

static void PumpMessageQueueProc();

// The key of the remembered login: the server's display name, as the registry and NSUserDefaults keys are
// (winmain.cpp SaveUserPass, PLSLoginController.mm). One browser origin may serve several deployments, so the
// deployment's name comes first.
static ST::string LoginRecordKey()
{
    ST::string key = EnvOr("PLASMA_DEPLOYMENT_NAME", "").c_str();
    if (key.empty())
        key = GetServerDisplayName();
    return key;
}

// The login dialog: wWinMain without /SkipLoginDialog (LoadUserPass, then IDD_URULOGIN_MAIN until a login succeeds or
// the player quits) with UruLoginDialogProc's Login branch, and the Mac's PLSLoginWindowController.mm. The page shows
// the dialog (plEmscriptenLogin.cpp). The account and password come from it once per Login and are hashed here by
// StoreHash (plClientLogin.h); neither is ever logged. Returns false when the player quits.
static bool RunLoginDialog()
{
    using namespace plEmscriptenLogin;
    const ST::string shard = LoginRecordKey();

    // LoadUserPass (winmain.cpp): the last account, the remember flag, the remembered hash, the focus.
    Record record;
    LoadRecord(shard, record);
    ShaDigest namePassHash;
    memset(namePassHash, 0, sizeof(namePassHash));
    if (record.remember && !record.username.empty() && record.hasHash)
        memcpy(namePassHash, record.namePassHash, sizeof(namePassHash));
    DialogInit init;
    init.username = record.username;
    init.remember = record.remember;
    init.focus = (record.remember && !record.username.empty()) ? Focus::kLogin
                 : record.username.empty()                     ? Focus::kAccount
                                                               : Focus::kPassword;
    // The server.ini's status URL (GetServerStatusUrl). A page cannot read another origin's response without CORS, so
    // a deployment may relay it from its own origin (PLASMA_RUNTIME_STATUS_URL).
    init.statusUrl = EnvOr("PLASMA_RUNTIME_STATUS_URL", "").c_str();
    if (init.statusUrl.empty())
        init.statusUrl = GetServerStatusUrl();
    init.signupUrl = GetServerSignupUrl();
    init.product = plProduct::ProductString();
    Show(init);
    CLIENT_STATUS(
        "LOGIN DIALOG: shown (shard key \"{}\"; remembered account {}, remember password {}, "
        "remembered hash {})",
        shard, record.username.empty() ? "no" : "yes", record.remember ? "on" : "off",
        (record.remember && record.hasHash) ? "yes" : "no");

    unsigned dialogPasses = 0; // NetCommUpdate passes of the dialog loop, for the status lines
    double   dialogSince = emscripten_get_now();
    for (;;) {
        // AUTH_LOGIN_TIMER (UruLoginDialogProc): NetCommUpdate every 10 ms while the dialog is open. The 10 ms is a
        // wait that the player's Login or Quit ends, so the action is taken at once, as the modal loop's GetMessage
        // takes it.
        NetCommUpdate();
        PumpMessageQueueProc();
        ++dialogPasses;
        if (!WaitForPlayer(10.0))
            continue;
        ST::string  username, password;
        bool        remember = false;
        const Event event = Take(username, password, remember);
        if (event == Event::kQuit) {
            // Quit: EndDialog(false) (UruLoginDialogProc), needExit (wWinMain)
            Close();
            CLIENT_STATUS("LOGIN DIALOG: Quit");
            return false;
        }
        if (event != Event::kLogin)
            continue;

        // SaveUserPass (winmain.cpp): the account and the remember flag are saved; a typed password is hashed
        // (StoreHash) and, with "Remember Password", kept as that hash, where pfPasswordStore keeps the password. The
        // fake string stands for the remembered hash, so only a typed password changes it: unticking Remember while
        // the fake string shows keeps the stored hash, which LoadUserPass then ignores.
        const bool typed = password != FAKE_PASS_STRING;
        if (typed)
            StoreHash(username, password, namePassHash);
        password = ST::string();
        record.username = username;
        record.remember = remember;
        if (typed) {
            record.hasHash = remember;
            memcpy(record.namePassHash, namePassHash, sizeof(namePassHash));
            if (!remember)
                memset(record.namePassHash, 0, sizeof(record.namePassHash));
        }
        const bool saved = SaveRecord(shard, record);
        NetCommSetAccountUsernamePassword(username, namePassHash);
        NetCommSetAuthTokenAndOS(nullptr, u"win");
        CLIENT_STATUS(
            "LOGIN DIALOG: Login ({} password, remember password {}; record {}); "
            "NetCommConnect + NetCommAuthenticate",
            typed ? "typed" : "remembered", remember ? "on" : "off", saved ? "saved" : "NOT saved");
        CLIENT_STATUS("LOGIN DIALOG: {} NetCommUpdate passes in {.3f} s before this Login", dialogPasses,
                      (emscripten_get_now() - dialogSince) / 1000.0);

        // AuthenticateNetClientComm (winmain.cpp): IDD_AUTHENTICATING's timer (AuthDialogProc) tests
        // NetCommIsLoginComplete, else runs NetCommUpdate, every 10 ms; the busy box's Cancel ends the wait at once.
        SetBusy(true);
        if (!NetCliAuthQueryConnected())
            NetCommConnect();
        NetCommAuthenticate(nullptr);
        const double tAuth = emscripten_get_now();
        bool         cancelled = false;
        unsigned     authPasses = 0;
        while (!NetCommIsLoginComplete()) {
            NetCommUpdate();
            PumpMessageQueueProc();
            ++authPasses;
            if (WaitForPlayer(10.0) && Take(username, password, remember) == Event::kCancel) {
                cancelled = true;
                break;
            }
        }
        const ENetError auth = cancelled ? kNetSuccess : NetCommGetAuthResult();
        SetBusy(false);
        if (cancelled) {
            // UruLoginDialogProc: a cancelled authentication disconnects and keeps the dialog open
            CLIENT_STATUS(
                "LOGIN DIALOG: Cancel after {.3f} s ({} NetCommUpdate passes); NetCommDisconnect, "
                "back to the dialog",
                (emscripten_get_now() - tAuth) / 1000.0, authPasses);
            NetCommDisconnect();
            dialogPasses = 0;
            dialogSince = emscripten_get_now();
            continue;
        }
        CLIENT_STATUS("LOGIN: auth result {} ({}) after {.3f} s ({} NetCommUpdate passes)", int(auth),
                      ST::string::from_wchar(NetErrorAsString(auth)), (emscripten_get_now() - tAuth) / 1000.0,
                      authPasses);
        if (IS_NET_SUCCESS(auth)) {
            // EndDialog(ok) (UruLoginDialogProc)
            Close();
            memset(namePassHash, 0, sizeof(namePassHash));
            return true;
        }
        // IDD_AUTHFAILED over the dialog (AuthFailedDialogProc; the Mac's "Authentication Failed" alert), then back
        // to the dialog. The box's message loop goes on dispatching the dialog's AUTH_LOGIN_TIMER, so NetCommUpdate
        // runs every 10 ms behind it until OK; hsMessageBoxPumping keeps that loop running.
        CLIENT_STATUS("LOGIN FAILED: {}; back to the login dialog", ST::string::from_wchar(NetErrorToString(auth)));
        [[maybe_unused]] const unsigned passesBefore = dialogPasses;
        [[maybe_unused]] const double   tBox = emscripten_get_now();
        hsMessageBoxPumping(AuthFailedText(auth), ST_LITERAL("Authentication Failed"), hsMessageBoxNormal,
                            hsMessageBoxIconError, [&dialogPasses] {
            NetCommUpdate();
            PumpMessageQueueProc();
            ++dialogPasses;
        }, 10.0);
        CLIENT_STATUS(
            "LOGIN DIALOG: \"Authentication Failed\" answered after {.3f} s "
            "({} NetCommUpdate passes behind it)",
            (emscripten_get_now() - tBox) / 1000.0, dialogPasses - passesBefore);
    }
}

// The thread's message queue: calls other threads proxied to this one (win32 PeekMessage/DispatchMessage, main.mm
// PumpMessageQueueProc).
static void PumpMessageQueueProc()
{
    emscripten_current_thread_process_queued_calls();
    plEmscriptenFrameHooks::RunMessagePumps();
}

// How the client ended, which decides how much of plClientLoader's shutdown ClientFinish runs.
enum class ClientEnd
{
    kLoginEnded, // before its loop, the login ended the program (wWinMain's needExit)
    kNotStarted, // the client was missing or already done when its loop was to start (the end of wWinMain)
    kLoopEnded,  // its loop ended (Mac-Cocoa/main.mm applicationShouldTerminate)
};
static int ClientFinish(ClientEnd end, int rc);

// -----------------------------------------------------------------------------
// The frame loop
//
// The client thread runs one tick per requestAnimationFrame of its worker (plEmscriptenFrameLoop), as the macOS
// client's CVDisplayLink drives its runLoop (Mac-Cocoa/main.mm: MainLoop, then PumpMessageQueueProc). After every tick
// the thread returns to its event loop, which presents the canvas and runs the browser work it waits on (WebGPU
// promises, callbacks). main() runs the startup up to the window (WinInit) and starts the loop; the rest of the
// startup runs as loop states, each in its own tick:
//   kAcquire  the WebGPU adapter, device and surface, whose promises resolve only between ticks
//   kLoad     plClientLoader::Init(), on this thread: WebGPU objects belong to the thread that creates them
//   kStart    NetCommStartup, login, display scale, StartClient (winmain.cpp's order)
//   kRun      plClient::MainLoop once per tick; when it ends, the shutdown and exit, in that tick
// Code that presents frames inside one long call (the progress bar during a load, the intro movie) suspends the tick
// until the next animation frame through JSPI (plEmscriptenPresentYield).

enum class ClientState
{
    kAcquire,
    kLoad,
    kStart,
    kRun
};
static constexpr int kStartLoop = -1; // ClientMain: start the frame loop

static struct LoopState
{
    ClientState state = ClientState::kLoad;
    bool        skipIntroMovies = false;
    // The WebGPU pipeline is the device unless "-Renderer null" asks for the null device.
    bool        useWGPU = false;
} s_loop;

// The patcher launcher's cancel and fail exits, before a client or a net exists. Returns rc, the program's exit code.
static int LauncherExit(int rc)
{
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->OnExit(plEmscriptenTestHooks::ExitPoint::kLauncher, rc);
#endif
    return rc;
}

// The startup up to the window: wWinMain up to WinInit, and WinInit without plClientLoader::Init (the kLoad state).
// Returns kStartLoop, or an exit code.
static int ClientMain(int argc, const char** argv)
{
    CLIENT_STATUS("main() on pthread {}; {} hardware threads", (unsigned long)pthread_self(),
                  emscripten_num_logical_cores());

    // file systems first: every later step reads them.
    if (!MountFileSystems()) {
        CLIENT_FAILURE("FATAL: file system setup failed");
        return s_storageFailed ? 2 : 1;
    }

    // Every patcher of this client, the launcher step's and plResPatcher's, writes into the origin's storage. Before it
    // fetches a manifest's files it checks them against what that storage may still take; a download whose file is
    // not at its manifest size failed for storage only when a write of that file failed (plEmscriptenStdio.cpp).
    pfPatcher::SetStorageBudget(plEmscriptenStorage::Available);
    pfPatcher::SetWriteFailures([](const plFileName& file) {
        return plEmscriptenStorage::WriteFailures(file.AsString().c_str());
    });

    PF_CONSOLE_INIT_ALL()

    std::vector<ST::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; i++)
        args.push_back(ST::string::from_utf8(argv[i]));

    plCmdParser cmdParser(s_cmdLineArgs, std::size(s_cmdLineArgs));
    cmdParser.Parse(args);
#ifdef PLASMA_TEST_HOOKS
    // the client seam, once the command line is parsed and before the client loads
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->OnClientInit(cmdParser);
#endif

    [[maybe_unused]] uint32_t requestedDevType = hsG3DDeviceSelector::kDevTypeUnknown;
    // The local-data flags are parsed in both flavours, as the shipped macOS client parses them (Mac-Cocoa/main.mm
    // applicationDidFinishLaunching; winmain.cpp keeps them internal). In the external flavour they only stop the
    // patcher's data and game-code requests (plResPatcher::CreatePatcher): the external plStreamSource never reads
    // loose files.
    if (cmdParser.IsSpecified(kArgLocalData)) {
        gDataServerLocal = true;
        gPythonLocal = true;
    }
    if (cmdParser.IsSpecified(kArgLocalPython))
        gPythonLocal = true;
    if (cmdParser.IsSpecified(kArgLocalSDL))
        gSDLLocal = true;
#ifndef PLASMA_EXTERNAL_RELEASE
    if (cmdParser.IsSpecified(kArgPlayerId))
        NetCommSetIniPlayerId(cmdParser.GetInt(kArgPlayerId));
    if (cmdParser.IsSpecified(kArgStartUpAgeName))
        NetCommSetIniStartUpAge(cmdParser.GetString(kArgStartUpAgeName));
    if (cmdParser.IsSpecified(kArgPvdFile))
        plPXSimulation::SetDefaultDebuggerEndpoint(cmdParser.GetString(kArgPvdFile));
    // -Renderer: "webgpu" (or "wgpu") selects the WebGPU pipeline, "null" (or "opengl"/"gl", the null device's record
    // type, plEmscriptenNullDevice.cpp) plNullPipeline, and other values the default device.
    if (cmdParser.IsSpecified(kArgRenderer)) {
        const ST::string renderer = cmdParser.GetString(kArgRenderer);
        uint32_t         devType = hsG3DDeviceSelector::kDevTypeUnknown;
#ifdef PLASMA_PIPELINE_WGPU
        if (renderer.compare_i("webgpu") == 0 || renderer.compare_i("wgpu") == 0)
            devType = hsG3DDeviceSelector::kDevTypeWebGPU;
#endif
        if (renderer.compare_i("null") == 0 || renderer.compare_i("opengl") == 0 || renderer.compare_i("gl") == 0)
            devType = hsG3DDeviceSelector::kDevTypeOpenGL;
        gClient.SetRequestedRenderingBackend(devType);
        requestedDevType = devType;
        CLIENT_STATUS("-Renderer {}: requested device type {}", renderer, devType);
    }
#endif
#ifdef PLASMA_PIPELINE_WGPU
    // No WebGPU device is acquired when the null device is requested (its record is then the only one).
    s_loop.useWGPU = requestedDevType != hsG3DDeviceSelector::kDevTypeOpenGL;
#endif
    s_loop.skipIntroMovies = cmdParser.IsSpecified(kArgSkipIntroMovies);
    CLIENT_STATUS("args parsed: {} arguments; LocalData={} LocalPython={} LocalSDL={}; {}",
                  argc - 1, gDataServerLocal, gPythonLocal, gSDLLocal, plProduct::ProductString());
#ifdef PLASMA_TEST_HOOKS
    // the client seam, once the local-data flags are known: a harness build checks its own game code against them
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        if (int rc = hooks->OnArgsParsed(gDataServerLocal, gPythonLocal, gSDLLocal))
            return rc;
#endif

    plFileName serverIni = "server.ini";
    if (cmdParser.IsSpecified(kArgServerIni))
        serverIni = cmdParser.GetString(kArgServerIni);
    if (const char* configured = plEmscriptenRuntime::ServerIni()) {
        if (!*configured) {
            CLIENT_FAILURE("FATAL: runtime server configuration setup failed");
            return 1;
        }
        serverIni = configured;
    }

    // Load an optional general.ini
    plFileName gipath = plFileName::Join(plFileSystem::GetInitPath(), "general.ini");
    FILE*      generalini = plFileSystem::Open(gipath, "rb");
    if (generalini) {
        fclose(generalini);
        pfConsoleEngine tempConsole;
        tempConsole.ExecuteFile(gipath);
    }

    // Redirect hsStatusMessage to plasmadbg.log
    DebugInit();
    hsStatusMessageF("Plasma 2.0.{}.{} - {}", PLASMA2_MAJOR_VERSION, PLASMA2_MINOR_VERSION, plProduct::ProductString());

    FILE* serverIniFile = plFileSystem::Open(serverIni, "rb");
    if (serverIniFile) {
        fclose(serverIniFile);
        try {
            pfServerIni::Load(serverIni);
        } catch (const pfServerIniParseException& exc) {
            hsMessageBox(ST::format("Error in server.ini file. Please check your URU installation.\n{}", exc.what()),
                         ST_LITERAL("Error"), hsMessageBoxNormal);
            return 0;
        }
    } else {
        hsMessageBox(ST_LITERAL("No server.ini file found. Please check your URU installation."), ST_LITERAL("Error"),
                     hsMessageBoxNormal);
        return 0;
    }
    CLIENT_STATUS("server.ini loaded ({})", serverIni);

    // Patcher mode: the launcher's data step before the client exists, as plUruLauncher runs before the client process
    // (plClientLauncher.cpp). /LocalData cannot be combined with it: it stops the file connection and plResPatcher's
    // manifest requests.
    if (s_patcherMode) {
        if (gDataServerLocal) {
            CLIENT_FAILURE("FATAL: patcher mode (PLASMA_DATA_FILES=patcher) with /LocalData");
            return 2;
        }
        switch (plEmscriptenPatcher::RunLauncherStep(s_packageInstalled)) {
            case plEmscriptenPatcher::LauncherResult::kContinue:
                break;
            // No client and no net exist yet, so nothing is shut down.
            case plEmscriptenPatcher::LauncherResult::kCancelled:
                return LauncherExit(0);
            case plEmscriptenPatcher::LauncherResult::kFailed:
                return LauncherExit(2);
        }
    }

    // WinInit (winmain.cpp): the display helper; in player mode the display is the browser window
    // (plEmscriptenDisplay.h).
    plDisplayHelper::SetInstance(plEmscriptenDisplay::CreateDisplayHelper());
    plEmscriptenDisplay::StartWindowWatch();
    gClient.SetClientWindow(nullptr);
    gClient.SetClientDisplay(plDisplayHelper::GetInstance()->DefaultDisplay());
    return kStartLoop;
}

// kLoad: wWinMain's gClient.Init(). On Emscripten plClientLoader::Init runs plClientLoader::Run on the calling thread.
static void ClientLoad()
{
    gClient.Init();
    // plClientLoader::Run names its thread; here that is the client thread
    hsThread::SetThisThreadName(ST_LITERAL("Application main thread"));
    CLIENT_STATUS(
        "plClientLoader finished on the client thread (resource manager, PhysX, pipeline, StartInit): "
        "client {}",
        (gClient && !gClient->GetDone()) ? "ready" : "FAILED");
}

// plClientLoader::ShutdownEnd (plClient::Shutdown, then hsgResMgr::Shutdown), after the platform layers' own teardown
// (plEmscriptenFrameHooks::AddShutdown). Those layers live outside plClient, so they release what they hold here,
// while the client and the resource manager still exist, as plClient::Shutdown releases its keys first. Every exit
// path of the client goes through here.
static void ClientShutdownEnd()
{
    plEmscriptenFrameHooks::RunShutdownHooks();
    gClient.ShutdownEnd();
}

// kStart: wWinMain from NetCommStartup to the main loop. Returns kStartLoop when the client runs, or an exit code.
static int ClientStart()
{
    NetCliAuthAutoReconnectEnable(false);
    NetCommStartup(); // InitNetClientComm (winmain.cpp)
    CLIENT_STATUS("NetCommStartup done");

    // Login (wWinMain): the login dialog, as without /SkipLoginDialog. Only a harness build's client seam may log in
    // another way.
    bool loginDialog = true;
#ifdef PLASMA_TEST_HOOKS
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        loginDialog = hooks->LoginDialog();
#endif
    if (loginDialog) {
        if (!RunLoginDialog()) {
            // needExit (wWinMain): the client shuts down and the program ends normally.
            return ClientFinish(ClientEnd::kLoginEnded, 0);
        }
#ifdef PLASMA_TEST_HOOKS
        for (const NetCommPlayer& p : NetCommGetPlayerList())
            CLIENT_STATUS("LOGIN: player {} \"{}\" explorer={} avatar={}", p.playerInt, p.playerName, p.explorer,
                          p.avatarDatasetName);
        const NetCommPlayer* active = NetCommGetPlayer();
        CLIENT_STATUS("LOGIN: {} players; selected player {}, start-up age {}",
                      NetCommGetPlayerCount(), active ? active->playerInt : 0, NetCommGetStartupAge()->ageDatasetName);
#endif
    }
#ifdef PLASMA_TEST_HOOKS
    else if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get()) {
        if (hooks->Login(PumpMessageQueueProc) == plEmscriptenTestHooks::LoginResult::kFailed)
            return ClientFinish(ClientEnd::kLoginEnded, 2);
    }
#endif

    NetCliAuthAutoReconnectEnable(true);

    // wWinMain waits for the loader here: already inited (kLoad ran the loader); kept for the native order.
    if (!gClient.IsInited())
        gClient.Wait();

    // Tell everybody about the current display scaling
    {
        // devicePixelRatio; in player mode the canvas backing's pixels per CSS pixel
        plDisplayScaleChangedMsg* msg = new plDisplayScaleChangedMsg(plEmscriptenDisplay::InitialScale());
        // and about every later devicePixelRatio change (plEmscriptenDisplay.h)
        plEmscriptenDisplay::StartScaleWatch(msg->GetScale());
        msg->Send();
    }

    // Main loop
    if (gClient && !gClient->GetDone()) {
        // Must be done here due to the plClient* dereference.
        if (s_loop.skipIntroMovies)
            gClient->SetFlag(plClient::kFlagSkipIntroMovies);

        gClient->WindowActivate(true);
        gClient->SetMessagePumpProc(PumpMessageQueueProc);
        gClient.StartClient();
        CLIENT_STATUS("StartClient done (BeginGame); MainLoop runs once per requestAnimationFrame from the next tick");
#ifdef PLASMA_TEST_HOOKS
        if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
            hooks->OnLoopStart();
#endif
        return kStartLoop;
    }
    return ClientFinish(ClientEnd::kNotStarted, 0);
}

// One MainLoop (Mac-Cocoa/main.mm runLoop); false leaves the loop.
static bool LoopStep()
{
#ifdef PLASMA_TEST_HOOKS
    plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get();
    if (hooks)
        hooks->BeforeFrame();
#endif
    gClient->MainLoop();
    if (gClient->GetDone())
        return false;

    PumpMessageQueueProc();
#ifdef PLASMA_TEST_HOOKS
    if (hooks && !hooks->AfterFrame())
        return false;
#endif
    return true;
}

// The client's one shutdown, for every way it ends once the loader has run; returns rc, the program's exit code.
//
// Once the loop has run, every exit is the macOS client's: its runLoop (LoopStep is its translation) answers a done
// client with [NSApp terminate:], whose applicationShouldTerminate runs ShutdownStart, one MainLoop, ShutdownEnd and
// NetCommShutdown, whatever ended the client. ShutdownStart disables the net at once (plNetClientMgr::IDisableNet), so
// that MainLoop posts the cancelled transactions' callbacks while the resource manager exists. win32's tail after the
// loop skips ShutdownStart after App.Quit, and a callback still pending then posts after hsgResMgr::Shutdown into no
// dispatcher, as an offline player's quit shows (the StartUp SetActivePlayer request never completes there).
//
// The steps run in one order, and `end` decides which of the first ones run:
//   kLoginEnded  ShutdownStart, ShutdownEnd, NetCommShutdown: wWinMain's needExit (no MainLoop before the loop)
//   kNotStarted  ShutdownEnd, NetCommShutdown: wWinMain's tail for a client that never looped (its init may have
//                failed)
//   kLoopEnded   ShutdownStart, one MainLoop, ShutdownEnd, NetCommShutdown: applicationShouldTerminate
// ShutdownStart writes through the loader's client, which is null when plClientLoader::Run found no resource.dat, so
// it runs only when a client exists, as on the Mac. The test is safe here because the browser's plClientLoader::Init
// ran Run on this thread and has returned.
static int ClientFinish(ClientEnd end, int rc)
{
#ifdef PLASMA_TEST_HOOKS
    plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get();
    if (hooks && end != ClientEnd::kLoginEnded)
        hooks->BeforeShutdown();
#endif

    if (gClient && end != ClientEnd::kNotStarted) {
        gClient.ShutdownStart();
        if (end == ClientEnd::kLoopEnded)
            gClient->MainLoop();
    }
    ClientShutdownEnd();
    NetCommShutdown(); // DeInitNetClientComm (winmain.cpp)

    // What the C runtime still buffers reaches the console and the files before the static destructors run (Finish).
    std::fflush(nullptr);
#ifdef PLASMA_TEST_HOOKS
    if (hooks)
        hooks->OnExit(end == ClientEnd::kLoginEnded ? plEmscriptenTestHooks::ExitPoint::kLogin
                                                    : plEmscriptenTestHooks::ExitPoint::kClient,
                      rc);
#endif
    return rc;
}

// musl's internal atexit runner (system/lib/libc/musl/src/exit/atexit.c, not a public API: revalidated with the pin in
// web/build/sources.lock). It runs the atexit handlers and static destructors, newest first, and empties the list.
extern "C" void __funcs_on_exit(void);

// Leaves the frame loop and ends the program with rc (inside a tick).
[[noreturn]] static void Finish(int rc)
{
    plEmscriptenFrameLoop::Stop();
    emscripten_runtime_keepalive_pop();
#ifdef PLASMA_PIPELINE_WGPU
    // The WebGPU device, adapter, canvas surface and instance outlive the client and its pipeline, as the D3D9 client's
    // IDirect3D9 does (hsGDirect3D.cpp s_direct3d, released at exit). They go here, before the static destructors.
    plWGPUEnumerate::Shutdown();
#endif
    // Static destructors run here, on the client thread, as natively when WinMain returns: exit() runs them on the
    // page's main thread, where ~plStatusLogMgr's close of an OPFS log file can block for ever (it does in Safari).
    __funcs_on_exit();
    exit(rc);
}

// One tick per animation frame of the client thread.
static void ClientTick()
{
    plEmscriptenFrameHooks::RunAll();

    switch (s_loop.state) {
        case ClientState::kAcquire: // only after main() started the WebGPU pipeline's acquire
#ifdef PLASMA_PIPELINE_WGPU
            if (plWGPUEnumerate::Ready()) {
                CLIENT_STATUS("WebGPU pipeline: device ready on the client thread");
                s_loop.state = ClientState::kLoad;
            } else if (plWGPUEnumerate::Failed()) {
                CLIENT_FAILURE("WebGPU pipeline: device/surface not available; exit 4");
                Finish(4);
            }
#endif
            return;
        case ClientState::kLoad:
            ClientLoad();
            s_loop.state = ClientState::kStart;
            return;
        case ClientState::kStart: {
            const int rc = ClientStart();
            if (rc != kStartLoop)
                Finish(rc);
            s_loop.state = ClientState::kRun;
            return;
        }
        case ClientState::kRun:
            if (!LoopStep())
                Finish(ClientFinish(ClientEnd::kLoopEnded, 0));
            return;
    }
}

int main(int argc, const char** argv)
{
    s_bootStart = emscripten_get_now();
#ifdef PLASMA_TEST_HOOKS
    // the client seam, before the client starts
    if (plEmscriptenTestHooks* hooks = plEmscriptenTestHooks::Get())
        hooks->OnMain(s_bootStart);
#endif
    // The client thread lives until Finish(). The frame loop holds the runtime alive too, but this keepalive also
    // covers main() itself: Emscripten 6.0.12's JSPI handleAsync pops its own keepalive a microtask before the
    // suspended wasm resumes, and a callback completing in that window would otherwise exit the thread under a
    // suspended caller.
    emscripten_runtime_keepalive_push();

    const int rc = ClientMain(argc, argv);
    if (rc != kStartLoop) {
        emscripten_runtime_keepalive_pop();
        return rc;
    }

#ifdef PLASMA_PIPELINE_WGPU
    // The pipeline's device is acquired on this thread before the loader runs: the plClient constructor and
    // InitPipeline enumerate devices.
    if (s_loop.useWGPU) {
        plWGPUEnumerate::StartAcquire(s_bootStart);
        s_loop.state = ClientState::kAcquire;
    }
#endif
    plEmscriptenFrameLoop::Start(ClientTick);
    CLIENT_STATUS("frame loop started: one tick per requestAnimationFrame of the client thread");
    return 0;
}
