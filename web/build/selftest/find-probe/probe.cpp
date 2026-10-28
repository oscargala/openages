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

// Includes headers the way the client does (<al.h>, <opus.h> via Opus::opus, PhysX, Python).
#include <al.h>
#include <alc.h>
#include <asio.hpp>
#include <expat.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <jpeglib.h>
#include <ogg/ogg.h>
#include <opus.h>
#include <openssl/evp.h>
#include <png.h>
#include <speex/speex.h>
#include <string_theory/string>
#include <tl/expected.hpp>
#include <vorbis/vorbisfile.h>
#include <zlib.h>
#include <PxPhysicsAPI.h>
#include <Python.h>
#include <cstdio>
int main() {
    ALCdevice* dev = alcOpenDevice(nullptr);
    std::printf("probe: zlib %s, python %s, openal device %s\n", zlibVersion(), Py_GetVersion(), dev ? "yes" : "no");
    FT_Library ft; FT_Init_FreeType(&ft);
    static physx::PxDefaultAllocator a; static physx::PxDefaultErrorCallback e;
    physx::PxFoundation* f = PxCreateFoundation(PX_PHYSICS_VERSION, a, e);
    std::printf("probe: physx foundation %s, opus %s\n", f ? "ok" : "FAIL", opus_get_version_string());
    return f ? 0 : 1;
}
