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

// fopen and fclose wrappers (-Wl,--wrap=fopen,--wrap=fclose) for two deliberate differences from the native client,
// kept here so that no engine file changes for them.
//
// A larger buffer for the game data. Each refill of a FILE's buffer costs proxied WasmFS backend reads: musl reads the
// caller's part and the buffer in one two-element readv (musl __stdio_read.c), and WasmFS reads each iovec as its own
// backend read, a hop to the fetch backend's proxy worker. With musl's 1 KiB BUFSIZ an Age load makes one backend read
// for about every twenty freads. A FILE opened read-only on an immutable fetch asset (plEmscriptenCwd.cpp) gets a fully
// buffered 64 KiB buffer before its first I/O (setvbuf). Natively hsUNIXStream::Open and plFileSystem::Open only call
// fopen; setting it there would change every platform for a cost only the browser has. The bytes each read returns are
// the same; other files keep musl's buffer.
//
// No half-written download. A FILE opened for writing in the patcher-mode install directory whose writes failed
// (ferror, or fclose's flush: OPFS returns EIO when the origin's quota is exhausted) is removed at fclose, and
// pfPatcher.cpp fails that download (WrittenShort). Natively the partial file stays, and the next run's hash check
// downloads it again (pfPatcher.cpp IHashFile), as it does a missing one; in the browser a full quota is the usual
// cause, and the partial file would only hold quota the next download needs.

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "plEmscriptenCwd.h"
#include "plEmscriptenStorage.h"

extern "C" FILE* __real_fopen(const char* path, const char* mode);
extern "C" int   __real_fclose(FILE* file);

namespace
{
constexpr size_t kGameDataBuffer = 64 * 1024;

std::mutex                             sLock;
std::unordered_map<FILE*, char*>       sBuffers;
std::unordered_map<FILE*, std::string> sWritable; // install-directory files opened for writing

bool ReadOnly(const char* mode)
{
    return mode && mode[0] == 'r' && !std::strchr(mode, '+');
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
    std::lock_guard<std::mutex> guard(sLock);
    char*                       buffer = static_cast<char*>(std::malloc(kGameDataBuffer));
    if (buffer && setvbuf(file, buffer, _IOFBF, kGameDataBuffer) == 0)
        sBuffers[file] = buffer;
    else
        std::free(buffer);
    return file;
}

extern "C" int __wrap_fclose(FILE* file)
{
    std::string written;
    {
        std::lock_guard<std::mutex> guard(sLock);
        auto                        it = sWritable.find(file);
        if (it != sWritable.end()) {
            written = std::move(it->second);
            sWritable.erase(it);
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
    std::lock_guard<std::mutex> guard(sLock);
    auto                        it = sBuffers.find(file);
    if (it != sBuffers.end()) {
        std::free(it->second);
        sBuffers.erase(it);
    }
    return result;
}
