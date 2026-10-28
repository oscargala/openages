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

#ifndef plEmscriptenCwd_inc
#define plEmscriptenCwd_inc

namespace plEmscriptenCwd
{
// Merge private writable children with immutable fetch children. All parameters are existing absolute directories
// except destination, which must not exist. The temporary asset mount is removed only after the virtual mount succeeds.
int  Mount(const char* destination, const char* writable, const char* assets);
// Replace an existing temporary fetch mount with an immutable virtual view.
int  MakeReadOnly(const char* mount);
// Whether an open descriptor is an immutable fetch asset of a virtual view.
bool ImmutableAsset(int fd);
// The per-shard install directory of patcher mode (plEmscriptenPatcher.h): read-only opens of
// files under it (or relative to it, the cwd) are game data too, for the same larger stdio buffer.
void SetGameDataRoot(const char* root);
bool GameDataPath(const char* path);
} // namespace plEmscriptenCwd

#endif // plEmscriptenCwd_inc
