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

// The page's audio activation, one rule for both audio outputs: the SDK's OpenAL (plEmscriptenOpenALLifecycle.js) and
// the opt-in native OpenAL Soft / SDL2 output (plEmscriptenNativeAudio.js) register each AudioContext they create here.
// A context created before the page's first user gesture starts suspended, and only resume() inside a trusted
// gesture's handler starts it everywhere (Chromium ignores sticky activation from a gesture elsewhere; Safari takes the
// call only inside the gesture). The client creates its context while it loads (plClient::StartInit,
// plgAudioSys::Activate, plAudioSystem::Init, alcCreateContext) and plays the intro once the login holds
// (plClient::BeginGame, plClient::IPlayIntroMovie); the player's first gesture is in the login dialog
// (plEmscriptenLogin.js) or a launcher dialog before it, page elements, not the canvas. So the listener is the
// document's, in the capture phase, as the SDK's own helper's (Emscripten 6.0.12 src/lib/libcore.js,
// $autoResumeAudioContext): any trusted gesture resumes the context in its own handler, before the intro, and no page
// handler can stop it. The events: the SDK's three, and the touch ones HTML counts as activation-triggering (touchend;
// pointerup for touch and pen). The listeners live from the first live context's registration to the last one's close.
// Each output registers with what only it knows: whether the context is held paused on purpose (paused) and what a
// refused resume() does (failed).
addToLibrary({
  $plasmaAudioActivation: {
    contexts: new Map(),
    listening: false,
    events: ['keydown', 'mousedown', 'touchstart', 'touchend', 'pointerup'],
    gesture: (event) => {
      if (!event.isTrusted) return;
      for (var [ac, output] of plasmaAudioActivation.contexts) {
        // Native opens and runs the device unconditionally (plAudioSystem::Init, alcOpenDevice and
        // alcCreateContext); the page's equivalent is resume() on a trusted gesture. A context that may not
        // start yet is 'suspended' in Chromium and Firefox but 'interrupted' in Safari, which then runs on
        // that resume().
        if ((ac.state === 'suspended' || ac.state === 'interrupted') && !output.paused?.())
          ac.resume().catch(output.failed);
      }
    },
    add: (ac, output) => {
      var activation = plasmaAudioActivation;
      if (activation.contexts.has(ac)) return;
      if (!activation.listening) {
        activation.listening = true;
        for (var type of activation.events) document.addEventListener(type, activation.gesture, true);
      }
      activation.contexts.set(ac, output);
      var changed = () => {
        if (ac.state !== 'closed') return;
        ac.removeEventListener('statechange', changed);
        activation.contexts.delete(ac);
        if (!activation.contexts.size) {
          for (var type of activation.events) document.removeEventListener(type, activation.gesture, true);
          activation.listening = false;
        }
      };
      ac.addEventListener('statechange', changed);
    },
  },
});
