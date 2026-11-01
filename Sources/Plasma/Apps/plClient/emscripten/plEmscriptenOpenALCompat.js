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

// Corrections of the pinned Emscripten 6.0.12 libopenal.js, each a library function whose postset installs it once,
// on the main thread, over the SDK's own AL object.
addToLibrary({
  // Pinned Emscripten 6.0.12 libopenal.js maps 360 degrees to 0 with `% 360`.
  // Both OpenAL and Web Audio define 360 as omnidirectional. Correct only that
  // boundary in the platform backend, after the SDK's original validation/setter.
  $plasmaOpenALConeCompat__deps: ['$AL'],
  $plasmaOpenALConeCompat__postset: 'if (!ENVIRONMENT_IS_PTHREAD) plasmaOpenALConeCompat();',
  $plasmaOpenALConeCompat: () => {
    var original = AL.setSourceParam;
    AL.setSourceParam = (funcname, sourceId, param, value) => {
      original(funcname, sourceId, param, value);
      if (value !== 360 || (param !== 0x1001 && param !== 0x1002)) return;
      var source = AL.currentCtx?.sources[sourceId];
      if (!source?.panner) return;
      if (param === 0x1001 && source.coneInnerAngle === 360)
        source.panner.coneInnerAngle = 360;
      if (param === 0x1002 && source.coneOuterAngle === 360)
        source.panner.coneOuterAngle = 360;
    };
  },

  // libopenal.js implements AL_NONE with a large panner reference distance, which alSource*(AL_REFERENCE_DISTANCE)
  // overwrites: the original backend's global mapping is reapplied after a validated reference- or max-distance change.
  //
  // AL_MAX_DISTANCE 0 is valid OpenAL (plDSoundBuffer::SetMaxDistance passes a sound's max falloff, 0 for some Cleft
  // sounds), but PannerNode.maxDistance throws a RangeError for it, at alSource*(AL_MAX_DISTANCE) or later in
  // updateSourceGlobal (initSourcePanner, from alSourceUnqueueBuffers for one), and the exception ends the session. The
  // AL value stays 0; the panner gets FLT_MIN, which the 'inverse' panner below never reads.
  //
  // OpenAL Soft 1.24.3, the native client's, does not attenuate an inverse-model source whose reference distance is 0,
  // nor, under AL_INVERSE_DISTANCE_CLAMPED (plAudioSystem::Init), one whose max distance is below its reference
  // distance (CalcAttnSourceParams, OpenAL Soft's alc/alu.cpp); the Web Audio 'inverse' panner silences the first and
  // attenuates the second. Those are the game's 2D mono sounds (min/max falloff 0/0: Cleft, GUI books; 10/5: VeeTsah),
  // so they get libopenal.js's AL_NONE setting: 'inverse' with refDistance FLT_MAX, gain 1.
  $plasmaOpenALDistanceCompat__deps: ['$AL'],
  $plasmaOpenALDistanceCompat__postset: 'if (!ENVIRONMENT_IS_PTHREAD) plasmaOpenALDistanceCompat();',
  $plasmaOpenALDistanceCompat: () => {
    var kMinPannerMaxDistance = 1.1754943508222875e-38; // FLT_MIN: positive, and exact as a float
    var kUnattenuatedRefDistance = 3.40282e38; // libopenal.js's AL_NONE panner reference distance (FLT_MAX)
    var unattenuated = (src) => {
      var model = src.context.sourceDistanceModel ? src.distanceModel : src.context.distanceModel;
      if (model === 0xd002 /* AL_INVERSE_DISTANCE_CLAMPED */ && src.maxDistance < src.refDistance) return true;
      return (model === 0xd001 /* AL_INVERSE_DISTANCE */ || model === 0xd002) && !(src.refDistance > 0);
    };
    var originalGlobal = AL.updateSourceGlobal;
    AL.updateSourceGlobal = (src) => {
      if (!src.panner) return originalGlobal(src);
      var maxDistance = src.maxDistance;
      if (!(maxDistance > 0)) src.maxDistance = kMinPannerMaxDistance;
      try {
        originalGlobal(src);
      } finally {
        src.maxDistance = maxDistance;
      }
      if (unattenuated(src)) src.panner.refDistance = kUnattenuatedRefDistance; // distanceModel is 'inverse'
    };
    var original = AL.setSourceParam;
    AL.setSourceParam = (funcname, sourceId, param, value) => {
      if (param !== 0x1020 && param !== 0x1023) return original(funcname, sourceId, param, value);
      var source = AL.currentCtx?.sources[sourceId];
      var panner = param === 0x1023 && value === 0 ? source?.panner : null; // AL_MAX_DISTANCE 0
      if (panner) source.panner = null;
      try {
        original(funcname, sourceId, param, value);
      } finally {
        if (panner) source.panner = panner;
      }
      if (!source?.panner || !Number.isFinite(value) || value < 0) return;
      if ((param === 0x1020 ? source.refDistance : source.maxDistance) !== value) return; // rejected by the setter
      AL.updateSourceGlobal(source); // AL_REFERENCE_DISTANCE / AL_MAX_DISTANCE; the AL values remain unchanged
    };
  },

  // For non-looping-static/streamed sources, libopenal.js derives bufOffset from
  // elapsed node time but omits that node's initial buffer offset after resume/seek.
  // Retain it so AL_SEC_OFFSET and subsequent pause/resume use the original position.
  $plasmaOpenALOffsetCompat__deps: ['$AL'],
  $plasmaOpenALOffsetCompat__postset: 'if (!ENVIRONMENT_IS_PTHREAD) plasmaOpenALOffsetCompat();',
  $plasmaOpenALOffsetCompat: () => {
    var original = AL.updateSourceTime;
    AL.updateSourceTime = source => {
      var time = original(source);
      var node = source.audioQueue[0];
      if (source.state === 0x1012 && !(source.type === 0x1028 && source.looping) &&
          node && node._startOffset > 0 && time >= node._startTime)
        source.bufOffset = node._startOffset + (time - node._startTime) * source.playbackRate;
      return time;
    };
  },

  // plListener::IEval sets the listener's orientation, position and velocity on every frame (plListener.cpp:185-187),
  // each one alListener* call (plAudioSystem::SetListenerOrientation, SetListenerPos, SetListenerVelocity:
  // plAudioSystem.cpp:389-403; the velocity is always 0, 0, 0). OpenAL Soft stores the value for its mixer. The pinned
  // libopenal.js stores it too, then updateListenerSpace writes all nine listener AudioParams from the stored listener
  // and walks every source of the context through updateSourceSpace, for each of the three calls. That walk maps a
  // listener-relative source into world space and recomputes each source's Doppler shift, both from the stored listener
  // and the source's own values; every source setter that changes one of those values (AL_POSITION, AL_DIRECTION,
  // AL_VELOCITY, AL_SOURCE_RELATIVE) and every new panner (initSourcePanner) already runs updateSourceSpace for its
  // source. A set whose value equals the stored listener's therefore leaves every AudioParam, panner and Doppler shift
  // where they are, and is skipped. A changed value, an invalid one (the original setter's error) and every other
  // parameter go to the original setter. Values compare as Object.is, so a sign change of zero still applies.
  $plasmaOpenALListenerCompat__deps: ['$AL'],
  $plasmaOpenALListenerCompat__postset: 'if (!ENVIRONMENT_IS_PTHREAD) plasmaOpenALListenerCompat();',
  $plasmaOpenALListenerCompat: () => {
    var same = (stored, value, from) =>
      Object.is(stored[0], value[from]) && Object.is(stored[1], value[from + 1]) && Object.is(stored[2], value[from + 2]);
    var original = AL.setListenerParam;
    AL.setListenerParam = (funcname, param, value) => {
      var listener = AL.currentCtx?.listener;
      if (listener && value !== null) {
        if (param === 0x1004 /* AL_POSITION */ && same(listener.position, value, 0)) return;
        if (param === 0x1006 /* AL_VELOCITY */ && same(listener.velocity, value, 0)) return;
        if (param === 0x100f /* AL_ORIENTATION */ && same(listener.direction, value, 0) && same(listener.up, value, 3))
          return;
      }
      return original(funcname, param, value);
    };
  },

  // Pinned libopenal.js offset-query boundary; original engine buffers/values stay
  // authoritative. Native reference: OpenAL Soft 1.24.3 al/source.cpp.
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
