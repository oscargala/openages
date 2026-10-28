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

// Pinned libopenal.js offset-query boundary; original engine buffers/values stay
// authoritative. Native reference: OpenAL Soft 1.24.3 al/source.cpp.
addToLibrary({
  $plasmaOpenALQueryCompat__deps: ['$AL'],
  $plasmaOpenALQueryCompat__postset: 'if (!ENVIRONMENT_IS_PTHREAD) plasmaOpenALQueryCompat();',
  $plasmaOpenALQueryCompat: () => {
    var tell = AL.sourceTell;
    AL.sourceTell = source => {
      var offset = tell(source); // update natural completion before querying state
      return source.state === 0x1012 || source.state === 0x1013 ? offset : 0;
    };
    var get = AL.getSourceParam;
    AL.getSourceParam = (funcname, sourceId, param) => {
      var value = get(funcname, sourceId, param);
      if (value === null || param < 0x1024 || param > 0x1026) return value;
      if (param === 0x1026) { // AL_BYTE_OFFSET: whole interleaved PCM frames
        var buffer = AL.currentCtx.sources[sourceId].bufQueue.find(buffer => buffer.id);
        if (buffer)
          value = Math.floor(value / buffer.bytesPerSample) * buffer.bytesPerSample * buffer.channels;
      }
      // The SDK's scalar float-vector getter reads val[0] from a scalar number.
      // Supply its expected representation for these three offset tokens only.
      return funcname === 'alGetSourcefv' ? [value] : value;
    };
    // A seek written after STOPPED is pending until the next play. The SDK's
    // normal STOPPED->PLAYING restart discards it. Keep the pending request apart
    // from the processed queue; apply through the original seeker at play time.
    var pending = new WeakMap();
    var seek = AL.sourceSeek;
    AL.sourceSeek = (source, offset) => {
      if (source.state === 0x1014) pending.set(source, offset);
      else seek(source, offset);
    };
    var setState = AL.setSourceState;
    AL.setSourceState = (source, state) => {
      if (state === 0x1012 && source.state === 0x1014 && pending.has(source)) {
        var offset = pending.get(source);
        pending.delete(source);
        source.state = 0x1011; // synchronous internal transition, as native restart
        seek(source, offset);
      } else if (state === 0x1012 || state === 0x1014 || state === 0x1011) {
        pending.delete(source);
      }
      return setState(source, state);
    };
  },
});
