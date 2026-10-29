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

// The upstream half of a relay writes each browser frame to the shard's socket in pieces, as the downstream half sends
// the shard's bytes to the browser in pieces of at most gateway.js MAX_FRAME_OUT. TCP carries a byte stream with no
// message boundaries, so the shard reads the same bytes in the same order however they are cut, and gateway.py hands
// each frame to its asyncio transport in one write (make_relay upstream), which sends it as the socket takes it.
//
// This module has no Workers imports, so the contract test can run it in Node.

export async function writePieces(bytes, max, write) {
  // Calls write(piece) for consecutive views of bytes (a Uint8Array) of at most max bytes, in order, and awaits each
  // call before the next, so at most one piece is in flight. The views share the frame's memory: no piece is copied,
  // and the frame stays held until its last piece is written. write may return false to stop: the rest is not
  // written, and writePieces returns false. A rejected write rejects writePieces with its error, and the rest is not
  // written. An empty frame writes nothing. Returns true when every piece was written.
  for (let o = 0; o < bytes.byteLength; o += max) {
    if ((await write(bytes.subarray(o, o + max))) === false) return false;
  }
  return true;
}
