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

// Game files from the shard's own file server, as the native clients get them.
//
// A deployment in patcher mode (PLASMA_DATA_FILES=patcher, config.mjs) has no game-data tree: the client's working
// directory is a per-shard install directory on OPFS, and the original patchers fill it:
//   - the launcher's data step (plClientLauncher.cpp InitializeNetCore / IGotFileServIPs / PatchClient /
//     ShutdownNetCore): the client manifest (plManifest::ClientManifest) through its own net session, executables
//     declined (the browser runs no downloaded program);
//   - the client's own patcher, unchanged: plClient::IPatchGlobalAgeFiles after login (the essential manifests and the
//     game code) and plNCAgeJoiner::Start -> plAgeLoader::UpdateAge before each Age (plResPatcher, pfPatcher).
// pfPatcher keeps its own rules: size, then MD5, against each manifest entry; a mismatch downloads the file again
// (pfPatcherWorker::IHashFile, pfPatcher.cpp).
#ifndef plEmscriptenPatcher_inc
#define plEmscriptenPatcher_inc

#include <string>

namespace plEmscriptenPatcher
{
// The OPFS install directory of this deployment: <userData>/PatcherData/<deployment name>, the name restricted to
// [A-Za-z0-9_.-] (else the server display name, else "default").
std::string InstallDirName();

// A deployment's client package (data.root of a patcher deployment: the files a native installer carries that the
// shard's manifests do not, for a shard without a client manifest): which tree entries belong to it.
bool PackageEntry(const std::string& relative, bool directory);

// The launcher's data step. Returns kContinue, kCancelled (the player pressed Cancel) or kFailed (an error box was
// shown, as the launcher's IShowErrorDialog). packageInstalled: a missing client manifest (kNetErrFileNotFound) then
// only logs, because the deployment's client package stands in for it.
enum class LauncherResult
{
    kContinue,
    kCancelled,
    kFailed
};
LauncherResult RunLauncherStep(bool packageInstalled);
} // namespace plEmscriptenPatcher

#endif // plEmscriptenPatcher_inc
