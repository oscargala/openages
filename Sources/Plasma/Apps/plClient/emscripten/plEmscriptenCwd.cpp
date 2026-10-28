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

// The client's working directory on WasmFS. One file owns it: the namespace plEmscriptenCwd (plEmscriptenCwd.h)
// and the getcwd()/chdir() wraps both build on the WasmFS of the pinned Emscripten release.
// - The views: Mount merges the immutable fetch assets of the game data with a private writable directory into
//   the working directory (/uru, main.cpp), MakeReadOnly turns a fetch mount into an immutable view, and
//   ImmutableAsset, SetGameDataRoot and GameDataPath tell plEmscriptenStdio.cpp which opens are game data.
// - getcwd()/chdir(), linked with -Wl,--wrap (the second half of this file).
//
// Platform composition for the pinned Emscripten release's WasmFS, not a replacement filesystem.
// Pinned SDK virtual.h documents why ALL files need the virtual backend identity.

#include "plEmscriptenCwd.h"

#include <emscripten/version.h>
#include <emscripten/wasmfs.h>
#include <fcntl.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "backend.h"
#include "paths.h"
#include "virtual.h"
#include "wasmfs.h"

// The pinned release is web/build/sources.lock's (plClient CMakeLists.txt defines its version here); its emsdk record
// lists what to revalidate, this file and its fixtures among them, before the pin changes.
#if !defined(PLASMA_EMSCRIPTEN_PINNED_MAJOR) || __EMSCRIPTEN_MAJOR__ != PLASMA_EMSCRIPTEN_PINNED_MAJOR || \
    __EMSCRIPTEN_MINOR__ != PLASMA_EMSCRIPTEN_PINNED_MINOR || __EMSCRIPTEN_TINY__ != PLASMA_EMSCRIPTEN_PINNED_TINY
#error "plEmscriptenCwd.cpp uses WasmFS internals: it compiles only with the release web/build/sources.lock pins"
#endif

namespace
{
using namespace wasmfs;

void CopyTimes(const std::shared_ptr<File>& target, const std::shared_ptr<File>& source)
{
    auto from = source->locked();
    auto to = target->locked();
    to.setATime(from.getATime());
    to.setMTime(from.getMTime());
    to.setCTime(from.getCTime());
}

class CwdData final : public VirtualDataFile
{
public:
    const bool immutable;
    CwdData(std::shared_ptr<DataFile> real, wasmfs::backend_t backend, bool immutable)
        : VirtualDataFile(real, backend), immutable(immutable) {}
    std::shared_ptr<DataFile> Underlying() const { return real; }

protected:
    int open(oflags_t flags) override
    {
        if (immutable && ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_TRUNC | O_APPEND))))
            return -EROFS;
        return VirtualDataFile::open(flags);
    }
    ssize_t write(const uint8_t* buf, size_t len, off_t offset) override
    {
        return immutable ? -EROFS : VirtualDataFile::write(buf, len, offset);
    }
    int setSize(off_t size) override
    {
        // Never forward an immutable truncate to ProxiedAsyncJSImplFile's trap.
        return immutable ? -EROFS : VirtualDataFile::setSize(size);
    }
};

class CwdDirectory final : public Directory
{
    std::shared_ptr<Directory>                   writable, assets;
    std::map<std::string, std::shared_ptr<File>> children;

    bool Protected(const std::string& name)
    {
        return !writable || (assets && assets->locked().getChild(name));
    }

    std::shared_ptr<File> Remember(const std::string& name, std::shared_ptr<File> real, bool immutable)
    {
        if (!real) return nullptr;
        if (auto it = children.find(name); it != children.end()) return it->second;
        std::shared_ptr<File> child;
        if (auto data = real->dynCast<DataFile>())
            child = std::make_shared<CwdData>(data, getBackend(), immutable);
        else if (auto dir = real->dynCast<Directory>())
            child = std::make_shared<CwdDirectory>(immutable ? nullptr : dir, immutable ? dir : nullptr, getBackend());
        else
            return nullptr; // OPFS and the manifest do not provide symlinks/devices.
        CopyTimes(child, real);
        child->locked().setParent(cast<Directory>());
        children.emplace(name, child);
        return child;
    }

public:
    CwdDirectory(std::shared_ptr<Directory> writable, std::shared_ptr<Directory> assets, wasmfs::backend_t backend)
        : Directory(writable ? writable->locked().getMode() : 0555, backend),
          writable(writable),
          assets(assets) {}
    bool                       Immutable() const { return !writable; }
    std::shared_ptr<Directory> Underlying() const { return writable; }

protected:
    std::shared_ptr<File> getChild(const std::string& name) override
    {
        if (assets) {
            auto child = assets->locked().getChild(name);
            if (child) return Remember(name, child, true);
        }
        return writable ? Remember(name, writable->locked().getChild(name), false) : nullptr;
    }
    std::shared_ptr<DataFile> insertDataFile(const std::string& name, mode_t mode) override
    {
        if (Protected(name)) return nullptr; // WasmFS open returns -EIO for rejected creates.
        auto file = writable->locked().insertDataFile(name, mode);
        auto child = Remember(name, file, false);
        return child ? child->dynCast<DataFile>() : nullptr;
    }
    std::shared_ptr<Directory> insertDirectory(const std::string& name, mode_t mode) override
    {
        if (Protected(name)) return nullptr;
        auto dir = writable->locked().insertDirectory(name, mode);
        auto child = Remember(name, dir, false);
        return child ? child->dynCast<Directory>() : nullptr;
    }
    std::shared_ptr<Symlink> insertSymlink(const std::string&, const std::string&) override
    {
        return nullptr; // Original OPFS does not implement symlinks; fail, never trap.
    }
    int removeChild(const std::string& name) override
    {
        if (Protected(name)) return -EROFS;
        if (int err = writable->locked().removeChild(name)) return err;
        auto it = children.find(name);
        if (it != children.end()) {
            it->second->locked().setParent(nullptr);
            children.erase(it);
        }
        return 0;
    }
    int insertMove(const std::string& name, std::shared_ptr<File> file) override
    {
        if (Protected(name)) return -EROFS;
        std::shared_ptr<File> real;
        if (auto data = std::dynamic_pointer_cast<CwdData>(file)) {
            if (data->immutable) return -EROFS;
            real = data->Underlying();
        } else if (auto dir = std::dynamic_pointer_cast<CwdDirectory>(file)) {
            if (dir->Immutable()) return -EROFS;
            real = dir->Underlying();
        } else
            return -EXDEV;
        auto oldParent = std::dynamic_pointer_cast<CwdDirectory>(file->locked().getParent());
        if (!oldParent || !oldParent->writable || real->getBackend() != writable->getBackend())
            return -EXDEV;
        if (int err = writable->locked().insertMove(name, real)) return err;
        for (auto it = oldParent->children.begin(); it != oldParent->children.end(); ++it) {
            if (it->second == file) {
                oldParent->children.erase(it);
                break;
            }
        }
        if (auto it = children.find(name); it != children.end())
            if (it->second != file) it->second->locked().setParent(nullptr);
        children[name] = file;
        return 0;
    }
    MaybeEntries getEntries() override
    {
        // One name, one exposed inode: assets always take precedence over a
        // persisted user entry with an immutable name, including after reload.
        std::map<std::string, std::shared_ptr<File>> entries;
        for (const auto& dir : {assets, writable}) {
            if (!dir) continue;
            auto source = dir->locked().getEntries();
            if (source.getError()) return {source.getError()};
            for (const auto& entry : *source) {
                if (entries.count(entry.name)) continue;
                auto child = getChild(entry.name);
                if (child) entries.emplace(entry.name, child);
            }
        }
        std::vector<Entry> result;
        for (const auto& [name, file] : entries)
            result.push_back({name, file->kind, file->getIno()});
        return {result};
    }
    ssize_t getNumEntries() override
    {
        auto entries = getEntries();
        if (auto err = entries.getError()) return err;
        return static_cast<ssize_t>(entries->size());
    }
    bool        maintainsFileIdentity() override { return true; }
    std::string getName(std::shared_ptr<File> file) override
    {
        for (const auto& [name, child] : children)
            if (child == file) return name;
        return "";
    }
};

class CwdBackend final : public wasmfs::Backend
{
    std::shared_ptr<Directory> writable, assets;

public:
    CwdBackend(std::shared_ptr<Directory> writable, std::shared_ptr<Directory> assets)
        : writable(writable), assets(assets) {}
    std::shared_ptr<Directory> createDirectory(mode_t) override
    {
        return std::make_shared<CwdDirectory>(writable, assets, this);
    }
    std::shared_ptr<DataFile> createFile(mode_t) override { return nullptr; }
    std::shared_ptr<Symlink>  createSymlink(std::string) override { return nullptr; }
};

std::shared_ptr<Directory> FindDirectory(const char* path)
{
    auto parsed = wasmfs::path::parseFile(path);
    return parsed.getError() ? nullptr : parsed.getFile()->dynCast<Directory>();
}

int RemoveTemporaryMount(const char* path)
{
    auto parsed = wasmfs::path::parseParent(path);
    if (auto err = parsed.getError()) return err;
    auto& [parent, name] = parsed.getParentChild();
    return parent->locked().removeChild(std::string(name));
}
} // namespace

int plEmscriptenCwd::Mount(const char* destination, const char* writable, const char* assets)
{
    auto data = FindDirectory(writable), immutable = FindDirectory(assets);
    if (!data || !immutable) return -ENOTDIR;
    auto backend = wasmfs::wasmFS.addBackend(std::make_unique<CwdBackend>(data, immutable));
    int  err = wasmfs_create_directory(const_cast<char*>(destination), 0777, reinterpret_cast<::backend_t>(backend));
    if (err) return err;
    return RemoveTemporaryMount(assets);
}

int plEmscriptenCwd::MakeReadOnly(const char* mount)
{
    auto immutable = FindDirectory(mount);
    if (!immutable) return -ENOTDIR;
    auto backend = wasmfs::wasmFS.addBackend(std::make_unique<CwdBackend>(nullptr, immutable));
    if (int err = RemoveTemporaryMount(mount)) return err;
    return wasmfs_create_directory(const_cast<char*>(mount), 0555, reinterpret_cast<::backend_t>(backend));
}

// plEmscriptenStdio.cpp gives these FILEs the larger stdio buffer.
bool plEmscriptenCwd::ImmutableAsset(int fd)
{
    auto open = wasmfs::wasmFS.getFileTable().locked().getEntry(fd);
    if (!open) return false;
    auto data = std::dynamic_pointer_cast<CwdData>(open->locked().getFile());
    return data && data->immutable;
}

// Patcher mode: the client's cwd is the per-shard OPFS install directory.
static std::string sGameDataRoot;

void plEmscriptenCwd::SetGameDataRoot(const char* root)
{
    sGameDataRoot = root ? root : "";
}

bool plEmscriptenCwd::GameDataPath(const char* path)
{
    if (sGameDataRoot.empty() || !path || !*path)
        return false;
    if (path[0] != '/')
        return true; // relative to the cwd, which is the install directory in patcher mode
    return std::string_view(path).substr(0, sGameDataRoot.size()) == sGameDataRoot && path[sGameDataRoot.size()] == '/';
}

// -----------------------------------------------------------------------------
// getcwd() for a working directory on a WasmFS mount.
//
// The client runs with its game folder as the working directory (relative paths "dat", "python", "server.ini"
// everywhere; Python's sys.path is "./python", "./python/system", ... (PythonInterface::initPython,
// cyPythonInterface.cpp)). In the browser that folder is a WasmFS fetch-backend mount (/uru). Emscripten 6.0.12's
// WasmFS getcwd (__syscall_getcwd, system/lib/wasmfs/syscalls.cpp) rebuilds the path by asking each parent for its
// child's name, and a memory directory does not know the names of directories mounted into it
// (Directory::Handle::getName in system/lib/wasmfs/file.cpp only searches its own entries, not the mount cache), so
// getcwd() returns "/" while relative paths still resolve against /uru. Python turns sys.path entries into absolute
// paths with getcwd() and then finds nothing ("No module named 'encodings'").
//
// Compat shim, linked with -Wl,--wrap=getcwd -Wl,--wrap=chdir: chdir() records the absolute, normalized directory it
// changed to and getcwd() returns it. fchdir() is not wrapped (nothing in the client uses it); after an fchdir the real
// getcwd() answers again.

extern "C" {
char* __real_getcwd(char* buf, size_t size);
int   __real_chdir(const char* path);
}

namespace
{
std::mutex& CwdMutex()
{
    static std::mutex m;
    return m;
}

std::string& Cwd()
{
    static std::string cwd; // empty = not changed by chdir yet
    return cwd;
}

std::string Normalize(const std::string& path)
{
    std::vector<std::string> parts;
    size_t                   pos = 0;
    while (pos <= path.size()) {
        size_t end = path.find('/', pos);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(pos, end - pos);
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        pos = end + 1;
    }
    std::string out;
    for (const std::string& p : parts)
        out += "/" + p;
    return out.empty() ? "/" : out;
}
} // namespace

extern "C" int __wrap_chdir(const char* path)
{
    int result = __real_chdir(path);
    if (result == 0 && path) {
        std::lock_guard<std::mutex> lock(CwdMutex());
        std::string                 base = Cwd();
        if (base.empty()) {
            char buf[4096];
            base = __real_getcwd(buf, sizeof(buf)) ? buf : "/";
        }
        Cwd() = Normalize(path[0] == '/' ? std::string(path) : base + "/" + path);
    }
    return result;
}

extern "C" char* __wrap_getcwd(char* buf, size_t size)
{
    std::string cwd;
    {
        std::lock_guard<std::mutex> lock(CwdMutex());
        cwd = Cwd();
    }
    if (cwd.empty())
        return __real_getcwd(buf, size);

    if (!buf) {
        // musl/glibc extension: allocate (at least size bytes when size != 0)
        size_t need = cwd.size() + 1;
        if (size != 0 && size < need) {
            errno = ERANGE;
            return nullptr;
        }
        buf = static_cast<char*>(std::malloc(size ? size : need));
        if (!buf) {
            errno = ENOMEM;
            return nullptr;
        }
    } else if (size == 0) {
        errno = EINVAL;
        return nullptr;
    } else if (size < cwd.size() + 1) {
        errno = ERANGE;
        return nullptr;
    }
    std::memcpy(buf, cwd.c_str(), cwd.size() + 1);
    return buf;
}
