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

// Only this async proxy crosses from the client pthread to the page.
addToLibrary({
  $plFullscreenPage: {api:null, loading:null, latest:null, exited:false, exitHook:false},
  plFullscreenMode__proxy: 'async',
  plFullscreenMode__sig: 'viiid',
  plFullscreenMode__deps: ['$plFullscreenPage'],
  plFullscreenMode: (width,height,windowed,scale) => {
    var page = plFullscreenPage;
    if (page.exited) return;
    page.latest = {width,height,windowed:!!windowed,scale};
    if (page.api) {
      page.api.setMode(width,height,!!windowed,scale);
      return;
    }
    if (!page.exitHook) {
      page.exitHook = true;
      var previous = Module['onExit'];
      Module['onExit'] = code => {
        page.exited = true; page.api?.dispose();
        if (previous) previous(code);
      };
    }
    if (!page.loading) {
      page.loading = import(new URL('../input/fullscreen.mjs',scriptDirectory).href)
        .then(({attachFullscreen}) => {
          if (page.exited) return;
          var canvas = Module['canvas'], stage = document.getElementById('stage');
          // Module.plasma.fullscreen and its last status (the page's boot.mjs declares both).
          page.api = attachFullscreen(canvas,stage,Module['plasma'].setFullscreenStage,
            status => { Module['plasma'].fullscreenStatus = status; });
          Module['plasma'].fullscreen = page.api;
          var latest = page.latest;
          page.api.setMode(latest.width,latest.height,latest.windowed,latest.scale);
        }).catch(e => {
          Module['plasma'].fullscreenStatus = {ready:false,error:{name:String(e?.name || 'Error')}};
          err('[fullscreen] attach failed: ' + e);
        });
    }
  },
});
