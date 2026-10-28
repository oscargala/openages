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

// libopenal.js implements AL_NONE with a large panner reference distance, but
// alSource*(AL_REFERENCE_DISTANCE) overwrites it. Reapply the original backend's
// global mapping after a validated reference- or max-distance change.
//
// AL_MAX_DISTANCE 0 is a valid OpenAL value (range [0, +inf]; plDSoundBuffer::SetMaxDistance passes a
// sound's integer max falloff, 0 for some Cleft sounds), but libopenal.js copies it to PannerNode.maxDistance, which
// throws a RangeError for any value <= 0: at alSource*(AL_MAX_DISTANCE) when the source has a panner, or later in
// updateSourceGlobal when initSourcePanner creates one (alSourceUnqueueBuffers reaches it, for example).
// The exception escapes a call proxied from the client thread and ends the session. The AL value stays 0 (alGetSource
// returns it); the panner gets the smallest positive float instead, which the 'inverse' panner below never reads.
//
// The native client's OpenAL Soft 1.24.3 applies no distance attenuation to an inverse-model source
// whose reference distance is 0, nor, under AL_INVERSE_DISTANCE_CLAMPED (plAudioSystem::Init), to one whose max
// distance is below its reference distance (CalcAttnSourceParams in OpenAL Soft's alc/alu.cpp: the clamped case
// breaks out when MaxDistance < RefDistance, the inverse formula runs only when RefDistance > 0). The Web Audio
// 'inverse' panner libopenal.js maps
// both models to returns 0 for refDistance 0 (silence) and attenuates past refDistance whatever maxDistance is. The
// game's 2D mono sounds carry min/max falloff 0/0 (Cleft, GUI books) or 10/5 (VeeTsah), so those cases decide their
// audibility. They get the setting libopenal.js already uses for AL_NONE: 'inverse' with refDistance FLT_MAX, gain 1.
addToLibrary({
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
});
