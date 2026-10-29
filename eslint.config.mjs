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

// eslint for the port's own JavaScript: the page's modules (shell/, input/) and the gateway Worker, the files prettier
// formats (.prettierignore), and the JavaScript emcc links into the client (LIBRARIES below), which keeps its own style
// and stays out of prettier; everything else is ignored below. The pinned eslint is web/build/formatters' (install.sh),
// and Docs/Web/Building.md, "Formatting", says how to run it.
//
// The rules are eslint's own correctness rules: the ones its recommended set marks as problems (code that is wrong or
// will fail), not style. no-control-regex is left out: config.mjs's endpoint check matches control characters on
// purpose. A catch clause may leave its error unused.
import { createRequire } from 'node:module';

// The globals package is installed beside eslint, outside the checkout: NODE_PATH names that node_modules folder.
const globals = createRequire(import.meta.url)('globals');

// The JavaScript emcc links into plClient.js: the --js-library files and the client's own --pre-js. Each is a plain
// script that Emscripten's preprocessor copies into the generated script as it is, so eslint reads it as written. One
// library is left out: plEmscriptenFetchBackend.js uses the preprocessor's own syntax ({{{ }}} macros, #if lines), which
// is not JavaScript until emcc has expanded it.
const LIBRARIES = [
  'Sources/Plasma/Apps/plClient/emscripten/pre.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenAssetCache.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenAssetCatalog.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenAudioActivation.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenDisplay.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenFrameLoop.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenFullscreen.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenInput.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenLogin.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenNativeAudio.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALCones.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALDistance.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALLifecycle.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALOffset.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenOpenALQueries.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenPatcher.js',
  'Sources/Plasma/Apps/plClient/emscripten/plEmscriptenStorage.js',
  'Sources/Plasma/FeatureLib/pfWGPUPipeline/plWGPUPipeline.js',
  'Sources/Plasma/NucleusLib/pnAsyncCore/pnAcWebSocket.js',
  'Sources/Plasma/PubUtilLib/plMessageBox/hsMessageBox_Emscripten.js',
];

// The names those files use that the generated script defines: all of its code shares one scope, inside the factory
// createPlasmaClient (-sMODULARIZE). A library member '$name' is the script's name, a native function name is _name.
const GENERATED = Object.fromEntries(
  [
    // Emscripten's runtime and the library functions the files depend on (their __deps)
    'Module',
    'ENV',
    'ENVIRONMENT_IS_PTHREAD',
    'scriptDirectory',
    'err',
    'addToLibrary',
    'HEAPU8',
    'HEAP32',
    'HEAPU32',
    'HEAPF64',
    'UTF8ToString',
    'stringToUTF8',
    'stringToNewUTF8',
    'lengthBytesUTF8',
    'getExceptionMessage',
    'getWasmTableEntry',
    'setMainLoop',
    'handleException',
    'addOnExit',
    'AL',
    'PThread',
    'WebGPU',
    '_malloc',
    '_free',
    '_getenv',
    '_pthread_self',
    '_alSourcei',
    '_emscripten_cancel_main_loop',
    '_emscripten_proxy_finish',
    // the libraries' own members, which one library may use from another
    'PlasmaAssetCache',
    'PLWS',
    'plwsAttach',
    'plInputWorker',
    'plFrameLoopChannel',
    'plFrameLoopSkipped',
    'plFullscreenPage',
    'plasmaAudioActivation',
    'plWGPUTexturePackCache',
    'plWGPUTextureSerials',
    // the asset origins' pin count, the generated pre-js asset-origin-pins.js (plClient's CMakeLists.txt)
    'PlasmaAssetOriginPins',
    // the client's native functions the libraries call back
    '_plEmscriptenLoginActed',
    '_plEmscriptenLauncherCancel',
    '_hsMessageBoxEmscriptenAnswer',
    '_plInputAttachMain',
    '_plInputKey',
    '_plInputMove',
    '_plInputButton',
    '_plInputWheel',
    '_plInputFocus',
    '_plInputReset',
    '_plInputPointerLock',
    '_plws_on_open',
    '_plws_on_message',
    '_plws_on_close',
  ].map((name) => [name, 'readonly']),
);

const CORRECTNESS = [
  'constructor-super',
  'for-direction',
  'getter-return',
  'no-async-promise-executor',
  'no-class-assign',
  'no-compare-neg-zero',
  'no-cond-assign',
  'no-const-assign',
  'no-constant-binary-expression',
  'no-constant-condition',
  'no-debugger',
  'no-dupe-args',
  'no-dupe-class-members',
  'no-dupe-else-if',
  'no-dupe-keys',
  'no-duplicate-case',
  'no-empty-character-class',
  'no-empty-pattern',
  'no-ex-assign',
  'no-fallthrough',
  'no-func-assign',
  'no-import-assign',
  'no-invalid-regexp',
  'no-irregular-whitespace',
  'no-loss-of-precision',
  'no-misleading-character-class',
  'no-new-native-nonconstructor',
  'no-obj-calls',
  'no-prototype-builtins',
  'no-self-assign',
  'no-setter-return',
  'no-sparse-arrays',
  'no-this-before-super',
  'no-unassigned-vars',
  'no-undef',
  'no-unexpected-multiline',
  'no-unreachable',
  'no-unsafe-finally',
  'no-unsafe-negation',
  'no-unsafe-optional-chaining',
  'no-unused-private-class-members',
  'no-unused-vars',
  'no-useless-assignment',
  'no-useless-backreference',
  'use-isnan',
  'valid-typeof',
];

export default [
  {
    // Every file is ignored but these (directories stay visible, so the files inside them can be listed).
    ignores: [
      '**/*',
      '!**/*/',
      '!eslint.config.mjs',
      '!Sources/Plasma/Apps/plClient/emscripten/shell/*.mjs',
      '!Sources/Plasma/Apps/plClient/emscripten/input/*.mjs',
      '!web/deploy/worker/src/*.js',
      ...LIBRARIES.map((file) => '!' + file),
    ],
  },
  {
    files: ['**/*.js', '**/*.mjs'],
    languageOptions: { ecmaVersion: 'latest', sourceType: 'module' },
    rules: {
      ...Object.fromEntries(CORRECTNESS.map((rule) => [rule, 'error'])),
      'no-unused-vars': ['error', { caughtErrors: 'none' }],
    },
  },
  {
    files: [
      'Sources/Plasma/Apps/plClient/emscripten/shell/*.mjs',
      'Sources/Plasma/Apps/plClient/emscripten/input/*.mjs',
    ],
    languageOptions: { globals: globals.browser },
  },
  {
    // A library runs on the page's main thread and on the client's pthread workers.
    files: LIBRARIES,
    languageOptions: {
      sourceType: 'script',
      globals: { ...globals.browser, ...globals.worker, ...GENERATED },
    },
  },
  {
    // The Cloudflare Workers runtime: a service worker's globals, and WebSocketPair.
    files: ['web/deploy/worker/src/*.js'],
    languageOptions: { globals: { ...globals.serviceworker, WebSocketPair: 'readonly' } },
  },
  {
    files: ['eslint.config.mjs'],
    languageOptions: { globals: globals.node },
  },
];
