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

// Native OpenAL/SDL playback only: activation touches browser transport, never AL state. The client's members of
// Module.plasma (pre.js): audioBackend names this backend; audioActivationError holds why the browser refused to
// resume the audio context, or null.
//
// The output's AudioContext is SDL's: SDL2's Emscripten audio driver keeps its state in Module['SDL2'], makes the
// context (Module['SDL2'].audioContext) when the native device opens (plAudioSystem::Init, alcOpenDevice, OpenAL Soft's
// SDL2 backend) and, when the device closes, closes it and sets the member to undefined; a reopened device makes a new
// one (the private test harness's native output fixture measures both). Each context SDL makes joins
// the page's audio activation (plEmscriptenAudioActivation.js) as it is set, so this output resumes on the same trusted
// gestures, in the same states and for the same lifetime as the SDK's OpenAL: listened for from the first live context
// until the last one closes. A native device pause (alcDevicePauseSOFT) stops SDL's device, not the browser's context,
// so a gesture cannot undo it and the output registers no pause rule. The member is watched on Module['SDL2'] whichever
// way SDL comes by that object: made here before the driver runs, or set by the driver.
addToLibrary({
    $plasmaNativeAudio__deps: ['$UTF8ToString', '$plasmaAudioActivation'],
    $plasmaNativeAudio__postset: 'plasmaNativeAudio.install();',
    $plasmaNativeAudio: {
        install: function() {
            if (ENVIRONMENT_IS_PTHREAD || typeof document === 'undefined') return;
            Module['plasma']['audioBackend'] = 'OpenAL Soft / SDL2';
            var output = {
                failed: function(error) {
                    Module['plasma']['audioActivationError'] = String(error);
                }
            };
            var watch = function(sdl) {
                if (!sdl || typeof sdl !== 'object') return sdl;
                var context = sdl['audioContext'];
                Object.defineProperty(sdl, 'audioContext', {
                    configurable: true,
                    enumerable: true,
                    get: function() { return context; },
                    set: function(value) {
                        context = value;
                        if (value) plasmaAudioActivation.add(value, output);
                    }
                });
                if (context) plasmaAudioActivation.add(context, output);
                return sdl;
            };
            var sdl = watch(Module['SDL2']);
            Object.defineProperty(Module, 'SDL2', {
                configurable: true,
                enumerable: true,
                get: function() { return sdl; },
                set: function(value) { sdl = watch(value); }
            });
        }
    }
});
