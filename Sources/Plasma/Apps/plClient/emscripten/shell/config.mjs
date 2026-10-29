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

// Deployment metadata only. The original client parses/applies the pinned server.ini.
const LIMIT = 65536;
// Every refusal is the same error message. Its code names the reason (the page's status line and console line quote
// it, boot.mjs CAUSES) without a URL or a response body: CONFIG-* for the deployment document, INI-* for its server.ini.
function fail(code = 'CONFIG-INVALID') {
  const e = new Error('Invalid runtime deployment configuration');
  e.code = code;
  throw e;
}
// The most asset origins a deployment pins (PLASMA_RUNTIME_ASSET_CACHE_ORIGINS): one per root they are derived from.
// The pins come from the verified document's roots only, as unique canonical origins with no arbitrary list: the
// data, fonts and sound roots and textures.hdPack.base, and the one root a test-hook extension adds (the test harness
// page's roots.gameCode, validateConfiguration's extension), which makes five. Every pinned origin shares
// the asset cache's one chunk budget, so the count does not raise the storage bound, and each pin keeps the cache's
// exact-origin rules. Defined here only: the client build reads this line into its asset-origin-pins.js (plClient
// CMakeLists.txt) for plEmscriptenAssetCache.js's pin rule, which the pack fetch's origin check (plWGPUPipeline.js)
// also applies.
export const ASSET_ORIGIN_PINS = 5;
// The deployment's asset-origin pins: the unique origins of its roots, at most ASSET_ORIGIN_PINS.
export function assetOrigins(config) {
  const extension = config.extension && config.extension.roots ? Object.values(config.extension.roots) : [];
  const origins = [
    ...new Set(
      [config.data, config.fonts, config.sound, ...extension, config.hdPack && config.hdPack.base]
        .filter(Boolean)
        .map((value) => new URL(value).origin),
    ),
  ];
  if (origins.length > ASSET_ORIGIN_PINS) fail();
  return origins;
}
function object(value, keys) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || Object.keys(value).some((k) => !keys.includes(k)))
    fail();
}
// A page served from this host itself (development). Only such a page may take its deployment document from another
// origin or carry the harness and diagnostic capabilities. The private test harness page runs only
// on such a page.
export function loopbackPage(pageUrl) {
  return ['localhost', '127.0.0.1', '[::1]'].includes(new URL(pageUrl).hostname);
}
function endpoint(value, base, websocket = false) {
  if (typeof value !== 'string' || !value || value.length > 2048 || /[\x00-\x20\x7f\\]/.test(value)) fail();
  let u;
  try {
    u = new URL(value, base);
  } catch (_) {
    fail();
  }
  const secure = websocket ? 'wss:' : 'https:',
    local = websocket ? 'ws:' : 'http:';
  const loopback = ['localhost', '127.0.0.1', '[::1]'].includes(u.hostname);
  if (
    (u.protocol !== secure && !(u.protocol === local && loopback && new URL(base).protocol === 'http:')) ||
    u.username ||
    u.password ||
    u.search ||
    u.hash ||
    /[%*]/.test(u.hostname)
  )
    fail();
  return u.href.replace(/\/$/, '');
}
// graphics.initialIni, the deployment's first-run graphics.ini (deployment.py INITIAL_GRAPHICS_INI, the
// same table): [key, kind, low, high, required], in the order and format of the client's default writer
// (plClient::IWriteDefaultGraphicsSettings in plClient.cpp, with its WriteBool "true"/"false", WriteInt "%d", CRLF),
// with the options menu's Graphics.Shadow.VisibleDistance last (xIniDisplay.py). The size comes from the page.
const INITIAL_GRAPHICS_INI = [
  ['Graphics.Windowed', 'bool', null, null, true],
  ['Graphics.AntiAliasAmount', 'int', 0, 16, true],
  ['Graphics.AnisotropicLevel', 'int', 0, 16, true],
  ['Graphics.TextureQuality', 'int', 0, 2, true],
  ['Quality.Level', 'int', 0, 3, true],
  ['Graphics.Shadow.Enable', 'int-bool', null, null, true],
  ['Graphics.EnablePlanarReflections', 'int-bool', null, null, true],
  ['Graphics.EnableVSync', 'bool', null, null, true],
  ['Graphics.Shadow.VisibleDistance', 'number', 0, 1, false],
];
function initialIni(value, fraction) {
  object(
    value,
    INITIAL_GRAPHICS_INI.map((row) => row[0]),
  );
  const out = {};
  for (const [key, kind, low, high, required] of INITIAL_GRAPHICS_INI) {
    if (!(key in value)) {
      if (required) fail();
      continue;
    }
    const v = value[key];
    if (
      kind === 'bool' || kind === 'int-bool'
        ? typeof v !== 'boolean'
        : typeof v !== 'number' ||
          !Number.isFinite(v) ||
          (kind === 'int' && (!Number.isInteger(v) || fraction(key))) ||
          v < low ||
          v > high
    )
      fail();
    out[key] = v;
  }
  return Object.freeze(out);
}
// The file the page installs into Init/graphics.ini when the profile has none: the writer's lines for the canvas
// backing (Width/Height/ColorDepth, as the writer writes the default mode's), then the deployment's settings.
export function initialGraphicsIni(values, width, height) {
  if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1) fail();
  const lines = [`Graphics.Width ${width}`, `Graphics.Height ${height}`, 'Graphics.ColorDepth 32'];
  for (const [key, kind] of INITIAL_GRAPHICS_INI) {
    if (!(key in values)) continue;
    const v = values[key];
    lines.push(`${key} ${kind === 'bool' ? (v ? 'true' : 'false') : kind === 'int-bool' ? (v ? 1 : 0) : String(v)}`);
  }
  return lines.map((line) => line + '\r\n').join('');
}
// The document's text (parseDocument): JSON.parse's value, and whether the text wrote the number at a
// path ([key, ...]) with a fraction part or an exponent. An integer the document needs (schema, shard.port, an int
// setting of graphics.initialIni) must be written as one, as the tools read it: JSON 4.0 is a float to Python's json,
// so the tools refuse it where they need an integer (shared_rules.json), and so does this page.
export function parseDocument(text) {
  const value = JSON.parse(text),
    fractions = new Set();
  let i = 0;
  const space = () => {
    while (' \t\n\r'.includes(text[i]) && i < text.length) i++;
  };
  const string = () => {
    const start = i++;
    while (text[i] !== '"') i += text[i] === '\\' ? 2 : 1;
    return JSON.parse(text.slice(start, ++i));
  };
  const NUMBER = /-?(?:0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?/y;
  const walk = (path) => {
    space();
    const c = text[i];
    if (c === '{' || c === '[') {
      i++;
      space();
      if (text[i] === (c === '{' ? '}' : ']')) {
        i++;
        return;
      }
      for (let n = 0; ; n++) {
        let key = n;
        if (c === '{') {
          space();
          key = string();
          space();
          i++;
        }
        walk([...path, key]);
        space();
        if (text[i++] !== ',') return;
      }
    }
    if (c === '"') {
      string();
      return;
    }
    NUMBER.lastIndex = i;
    const m = NUMBER.exec(text);
    if (m && m[0]) {
      i += m[0].length;
      const key = JSON.stringify(path);
      if (m[1] || m[2]) fractions.add(key);
      else fractions.delete(key); // a repeated key: JSON.parse keeps the last
      return;
    }
    i += c === 'f' ? 5 : 4; // true, false, null
  };
  walk([]);
  return { value, fraction: (path) => fractions.has(JSON.stringify(path)) };
}
// extension: null (the player page, the tools), or a test-hook overlay's deployment rules, which only the test harness
// page passes (the private test harness's seam member deployment: boot.mjs): {gameCodes, roots, validate}. gameCodes
// are the game-code modes it adds and roots the names it adds to roots; validate(raw, rules), with rules {caps, base,
// pageUrl, endpoint, fail, loopbackPage}, runs once the capabilities are read, may refuse (rules.fail), and returns
// the configuration's extension member: plain data, or null; its roots, an object of URLs, are pinned as asset origins.
export function validateConfiguration(raw, base, pageUrl = base, fraction = () => false, extension = null) {
  object(raw, [
    'schema',
    'roots',
    'gateway',
    'shard',
    'capabilities',
    'files',
    'join',
    'gameCode',
    'build',
    'graphics',
    'textures',
  ]);
  if (raw.schema !== 1 || fraction(['schema'])) fail();
  // files 'patcher' = the game files come from the shard's file server through the original patchers into a
  // per-deployment OPFS install directory; roots.data is then optional (the client package, when the shard has no
  // client manifest). Absent = the served data tree (dev/harness path).
  if (raw.files !== undefined && raw.files !== 'patcher') fail();
  const patcher = raw.files === 'patcher';
  // A release runs only the shard's own game code: no gameCode, or 'server-py2' = the shard's Python and SDL (no local
  // flags, as the default server mode), run by the py2 build: build 'py2' names plClient-py2.{js,wasm}
  // (PLASMA_PYTHON_ABI=2, CPython 2.3.3), which the page (boot.mjs) then loads instead of plClient.{js,wasm}. The two
  // go together: a server-py2 document without build 'py2' (a py3 build for a Python 2.3 shard) and build 'py2' with
  // any other mode both fail. Patcher only, as is every mode an extension adds.
  if (
    raw.gameCode !== undefined &&
    (!['server-py2', ...(extension ? extension.gameCodes : [])].includes(raw.gameCode) || !patcher)
  )
    fail();
  if (raw.build !== undefined && raw.build !== 'py2') fail();
  const py2 = raw.gameCode === 'server-py2';
  if (py2 !== (raw.build === 'py2')) fail();
  object(raw.roots, ['data', 'fonts', 'sound', ...(extension ? extension.roots : [])]);
  // name = the deployment's name (the remembered login's key), status = the deployment's same-origin relay
  // of the ini's Server.Status URL (the login dialog's status text); both optional.
  object(raw.shard, ['host', 'port', 'serverIni', 'sha256', 'name', 'status']);
  const s = raw.shard;
  // Per-deployment capabilities. harness lets the private test harness page (never
  // part of a kit) run this deployment with its test parameters, on a loopback page only: never a public deployment.
  // The player page (index.html) does not read it.
  const caps = raw.capabilities === undefined ? {} : raw.capabilities;
  object(caps, ['harness', 'gameCodeCapture']);
  if (caps.harness !== undefined && typeof caps.harness !== 'boolean') fail();
  if (caps.harness && !loopbackPage(pageUrl)) fail();
  const extended = extension ? extension.validate(raw, { caps, base, pageUrl, endpoint, fail, loopbackPage }) : null;
  // Diagnostic game-code capture into this origin's OPFS, only while the shard sends game code. A test harness
  // diagnostic (deployment.py), so only with the harness capability, on a loopback page. The test harness page arms it
  // in a harness build (the private test harness); the player page passes nothing for it.
  if (caps.gameCodeCapture !== undefined && typeof caps.gameCodeCapture !== 'boolean') fail();
  if (caps.gameCodeCapture && (caps.harness !== true || !loopbackPage(pageUrl))) fail();
  if (
    typeof s.host !== 'string' ||
    !/^[A-Za-z0-9.-]{1,253}$/.test(s.host) ||
    !Number.isInteger(s.port) ||
    fraction(['shard', 'port']) ||
    s.port < 1 ||
    s.port > 65535 ||
    !/^[a-f0-9]{64}$/.test(s.sha256 || '') ||
    (s.name !== undefined && (typeof s.name !== 'string' || !/^[A-Za-z0-9_.-]{1,64}$/.test(s.name)))
  )
    fail();
  // join = the same-origin endpoint that issues the gateway's single-use admission tokens (POST); the
  // socket backend (pnAcWebSocket.js) fetches one before each connection. It must be this page's own origin.
  const join = raw.join === undefined ? null : endpoint(raw.join, base);
  if (join !== null && new URL(join).origin !== new URL(pageUrl).origin) fail();
  const graphics = raw.graphics === undefined ? {} : raw.graphics;
  object(graphics, ['initialIni']);
  // textures.hdPack = the optional verified original high-res texture packs (deployment.py textures.hdPack):
  // base = the directory holding <Age>.hdpack, ages = the Ages with one.
  // The client fetches an Age's pack only when the player has turned the option on (off by default).
  const textures = raw.textures === undefined ? {} : raw.textures;
  object(textures, ['hdPack']);
  let hdPack = null;
  if (textures.hdPack !== undefined) {
    object(textures.hdPack, ['base', 'ages']);
    const ages = textures.hdPack.ages;
    if (
      !Array.isArray(ages) ||
      !ages.length ||
      ages.length > 64 ||
      ages.some((a) => typeof a !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(a)) ||
      new Set(ages.map((a) => a.toLowerCase())).size !== ages.length
    )
      fail();
    hdPack = Object.freeze({ base: endpoint(textures.hdPack.base, base), ages: Object.freeze([...ages]) });
  }
  const config = Object.freeze({
    hdPack,
    join,
    initialGraphicsIni:
      graphics.initialIni === undefined
        ? null
        : initialIni(graphics.initialIni, (key) => fraction(['graphics', 'initialIni', key])),
    data: patcher && raw.roots.data === undefined ? null : endpoint(raw.roots.data, base),
    fonts: endpoint(raw.roots.fonts, base),
    files: patcher ? 'patcher' : 'http',
    build: py2 ? 'py2' : 'py3',
    client: py2 ? 'plClient-py2' : 'plClient',
    sound: raw.roots.sound === undefined ? null : endpoint(raw.roots.sound, base),
    gateway: endpoint(raw.gateway, base, true),
    shard: Object.freeze({
      host: s.host,
      port: s.port,
      serverIni: endpoint(s.serverIni, base),
      sha256: s.sha256,
      name: s.name === undefined ? null : s.name,
      status: s.status === undefined ? null : endpoint(s.status, base),
    }),
    capabilities: Object.freeze({ harness: caps.harness === true, gameCodeCapture: caps.gameCodeCapture === true }),
    extension: extended,
  });
  assetOrigins(config);
  return config;
}
// The client's configuration (boot.mjs plasma.config): one JSON value, the deployment's roles
// as the client takes them. The client's pre-js (Sources/Plasma/Apps/plClient/emscripten/pre.js, its CONFIG) knows
// these keys and no others; null is a role the deployment does not have. serverIniText is the verified server.ini.
export function clientConfiguration(config, serverIniText) {
  return Object.freeze({
    data: config.data,
    files: config.files,
    fonts: config.fonts,
    sound: config.sound,
    gateway: config.gateway,
    join: config.join,
    serverIniText,
    name: config.shard.name,
    status: config.shard.status,
    hdPack: config.hdPack ? Object.freeze({ base: config.hdPack.base, ages: config.hdPack.ages }) : null,
    assetOrigins: Object.freeze(assetOrigins(config)),
  });
}
// Whether every argument of text that starts with a quote is closed, as pfConsoleTokenizer::NextArgument reads the
// rest of a server.ini line (server_ini.py arguments_close): an argument that starts with " or ' runs to the next of
// the same quote, which must exist; any other runs to the first space, tab, CR, LF, '=' or ',', and a run of that
// character is skipped.
function argumentsClose(text) {
  let i = 0;
  const n = text.length;
  for (;;) {
    while (i < n && ' \t\n\v\f\r'.includes(text[i])) i++;
    if (i === n) return true;
    if (text[i] === '"' || text[i] === "'") {
      const end = text.indexOf(text[i], i + 1);
      if (end < 0) return false;
      i = end + 1;
      continue;
    }
    while (i < n && !' =\r\n\t,'.includes(text[i])) i++;
    if (i < n) {
      const sep = text[i];
      while (i < n && text[i] === sep) i++;
    }
  }
}
export function validateServerIni(text, shard) {
  // No account/settings commands are accepted. Native pfServerIni remains the parser.
  if (typeof text !== 'string' || !text || text.includes('\0')) fail('INI-INVALID');
  // The tools' grammar exactly (web/deploy/plasma_web/server_ini.py, the shared cases of
  // web/tests/shared_rules.json): lines end at LF or CRLF; a line is trimmed of ASCII whitespace only; the name, the
  // value and a trailing comment are separated by spaces and tabs only (pfConsoleParser's separators), so a no-break
  // space, a form feed, a lone CR or a byte order mark there is refused, as the client would misread it.
  // As the client reads the file: a comment line is one whose first character is '#'
  // (hsStream::ReadLn; pfServerIni refuses a line that starts with ';' or "//", or an indented '#', as an unknown
  // setting); a bare value holds no '=' or ',' (pfConsoleTokenizer::NextArgument ends it there: a bare key would lose
  // its "=="), no '#' or ';', and does not start with a quote, and a comment after it starts after a space or tab; an
  // argument of a trailing comment that starts with a quote is closed (argumentsClose).
  const seen = new Map();
  for (const line of text.split(/\r?\n/)) {
    if (line.startsWith('#')) continue;
    const s = line.replace(/^[ \t\n\r\f\v]+|[ \t\n\r\f\v]+$/g, '');
    if (!s) continue;
    const m =
      /^(Server\.(?:Status|Signup|DispName|Port|File\.Host|Auth\.(?:Host|N|X|G)|Game\.(?:N|X|G)|Gate\.(?:Host|N|X|G)))[ \t]+(?:"([^"\r\n]*)"[ \t]*|([^\s"'=,;#][^\s"=,;#]*)(?:[ \t]+|$))((?:#|;|\/\/).*)?$/i.exec(
        s,
      );
    if (!m || !argumentsClose(m[4] || '')) fail('INI-INVALID');
    const key = m[1].toLowerCase(),
      value = m[2] ?? m[3];
    if (seen.has(key)) fail('INI-INVALID');
    seen.set(key, value);
    // Auth/Gate/File hosts may differ (NetCommConnect, plNetClientComm.cpp); the
    // gateway's server-side allowlist comes from this same ini. The entry host (Auth.Host,
    // else Gate.Host) is the document's shard.host.
    if (key.endsWith('.host') && value && !/^[A-Za-z0-9.-]{1,253}$/.test(value)) fail('INI-INVALID');
    if (key === 'server.port' && String(shard.port) !== value) fail('INI-INVALID');
    if (/\.(n|x)$/.test(key) && !/^[A-Za-z0-9+/]{85}[AQgw]==$/.test(value)) {
      // A 64-byte key is 88 base64 characters with two padding characters.
      let bytes;
      try {
        bytes = atob(value);
      } catch (_) {
        fail('INI-INVALID');
      }
      if (bytes.length !== 64 || btoa(bytes) !== value) fail('INI-INVALID');
    }
    if (/\.g$/.test(key) && !/^[1-9][0-9]*$/.test(value)) fail('INI-INVALID');
  }
  for (const key of [
    'server.auth.n',
    'server.auth.x',
    'server.game.n',
    'server.game.x',
    'server.gate.n',
    'server.gate.x',
  ])
    if (!seen.has(key)) fail('INI-INVALID');
  if ((seen.get('server.auth.host') || seen.get('server.gate.host')) !== shard.host) fail('INI-INVALID');
  if (!seen.has('server.port') && shard.port !== 14617) fail('INI-INVALID');
  return text;
}
// absent: a 404 answer is "no document" (null) instead of a failure. kind: the code prefix of a failure (CONFIG, INI):
// -FETCH no answer (the network, a redirect, the 15 s bound), -NONE a 404 for the document, -HTTP any other status
// than 200, -SIZE over 64 KiB; text that is not UTF-8 is CONFIG-JSON or INI-INVALID.
async function boundedText(url, absent = false, kind = 'CONFIG') {
  const control = new AbortController(),
    timer = setTimeout(() => control.abort(), 15000);
  try {
    let response;
    try {
      response = await fetch(url, {
        cache: 'no-store',
        credentials: 'omit',
        redirect: 'error',
        signal: control.signal,
      });
    } catch (_) {
      fail(kind + '-FETCH');
    }
    if (absent && response.status === 404) {
      await response.body?.cancel();
      return null;
    }
    if (response.status !== 200) fail(kind === 'CONFIG' && response.status === 404 ? 'CONFIG-NONE' : kind + '-HTTP');
    const reader = response.body.getReader(),
      chunks = [];
    let size = 0;
    for (;;) {
      let part;
      try {
        part = await reader.read();
      } catch (_) {
        fail(kind + '-FETCH');
      }
      if (part.done) break;
      size += part.value.byteLength;
      if (size > LIMIT) {
        await reader.cancel();
        fail(kind + '-SIZE');
      }
      chunks.push(part.value);
    }
    const bytes = new Uint8Array(size);
    let offset = 0;
    for (const chunk of chunks) {
      bytes.set(chunk, offset);
      offset += chunk.length;
    }
    let text;
    try {
      text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    } catch (_) {
      fail(kind === 'CONFIG' ? 'CONFIG-JSON' : 'INI-INVALID');
    }
    return { bytes, text };
  } finally {
    clearTimeout(timer);
    control.abort();
  }
}
export async function loadRuntimeConfig(value, pageUrl = globalThis.location.href) {
  return load(value, pageUrl, false, null);
}
// The page's own deployment: the document /runtime/config.json on the page's own origin, never a URL named by the
// query. A page without one does not start (CONFIG-NONE, the 404 answer), and every other failure fails closed. absent
// is the private test harness page's case alone: on a loopback page a 404 then means "no deployment"
// (null), the test harness server's own data tree and its WebSocket proxy (that server without --deployment).
// extension: the test harness page's deployment rules (validateConfiguration), null on the player page.
export async function loadPageDeployment(pageUrl = globalThis.location.href, absent = false, extension = null) {
  return load('/runtime/config.json', pageUrl, absent && loopbackPage(pageUrl), extension);
}
// The query parameters the player page reads: renderscale, the visit's choice of the render-scale menu (index.html,
// boot.mjs), which the player can also make in the menu. A new player parameter needs a documented reason and a test.
export const PLAYER_PARAMETERS = Object.freeze(['renderscale']);
// The player parameters of a query (params), and the names of the others (ignored: once each, in query order) for the
// page's one console line. The page reads no other parameter.
export function playerParameters(search) {
  const params = new URLSearchParams(),
    ignored = new Set();
  for (const [name, value] of new URLSearchParams(search)) {
    if (PLAYER_PARAMETERS.includes(name)) params.append(name, value);
    else ignored.add(name);
  }
  return { params, ignored: [...ignored] };
}
const CODE = /^(CONFIG|INI)-[A-Z]+$/;
async function load(value, pageUrl, absent, extension) {
  if (value === null || value === undefined) return null;
  try {
    const configUrl = endpoint(value, pageUrl);
    // The document names the gateway, the shard and the SHA-256 pin of its own server.ini, so it is trusted only from
    // the page's own origin: a document from another site would run this origin's login dialog, and the login it
    // remembers, against that site's gateway and shard. Only a loopback page (development) may name another origin.
    // Refused before any request is made.
    if (new URL(configUrl).origin !== new URL(pageUrl).origin && !loopbackPage(pageUrl)) fail('CONFIG-ORIGIN');
    const document = await boundedText(configUrl, absent);
    if (document === null) return null;
    let parsed;
    try {
      parsed = parseDocument(document.text);
    } catch (_) {
      fail('CONFIG-JSON');
    }
    const config = validateConfiguration(parsed.value, configUrl, pageUrl, parsed.fraction, extension);
    const ini = await boundedText(config.shard.serverIni, false, 'INI');
    const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', ini.bytes)), (b) =>
      b.toString(16).padStart(2, '0'),
    ).join('');
    if (sha !== config.shard.sha256) fail('INI-PIN');
    const text = validateServerIni(ini.text, config.shard);
    return Object.freeze({ config, client: clientConfiguration(config, text) });
  } catch (e) {
    fail(CODE.test(e && e.code) ? e.code : 'CONFIG-INVALID');
  } // Never echo response bodies/URLs or silently select a different shard.
}
