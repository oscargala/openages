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

// The browser client's gateway as one Cloudflare Worker on the page's own hostname. For one deployment it serves what
// the local pair plasma_web.server and plasma_web.gateway (web/deploy/plasma_web: server.py, gateway.py) serve:
//   the page kit (emit_static.py)            with server.py's headers: COOP/COEP/CORP, the deployment's
//                                            Content-Security-Policy, nosniff and the cache policy
//   POST /runtime/join                       server.py do_POST: one single-use join token per call
//   GET /runtime/status                      server.py relay_status and StatusRelay: the login dialog's status text,
//                                            from the deployment's own Server.Status URL (STATUS_URL)
//   GET /connect?host=..&port=.. (upgrade)   gateway.py: admission, then the WebSocket <-> TCP relay (gateway.js)
// Every WebSocket upgrade, on any path, is a gateway handshake, as every request to gateway.py's port is.
import { clientAddress, ipKey } from './address.js';
import { handleConnect, parseConfig } from './gateway.js';
import * as joinToken from './join_token.js';

export { Capacity, IpSlots, JoinNonces } from './objects.js';

const ISOLATION = {
  // crossOriginIsolated, which SharedArrayBuffer and so the client's pthreads need (server.py common_headers)
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin',
};
const NOSNIFF = { 'X-Content-Type-Options': 'nosniff' };
// The types server.py sets itself (server.py MIME); any other file keeps the asset server's type, and a file it gives
// no type (the trees' .fetch-manifest listings) is application/octet-stream, server.py's fallback.
const MIME = {
  '.wasm': 'application/wasm',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.html': 'text/html; charset=utf-8',
  '.json': 'application/json',
  '.txt': 'text/plain; charset=utf-8',
  '.md': 'text/markdown; charset=utf-8',
};
// server.py builds these from the deployment (the page document, the shard's server.ini and the status relay's answer)
// and serves them as application/octet-stream.
const GENERATED = new Set(['/runtime/config.json', '/runtime/server.ini', '/runtime/status']);
const CACHEABLE_DATA = new Set(['dat', 'sfx', 'python', 'SDL', 'avi', 'resource.dat']);
// The page and its module (server.py Handler.page_files; '' is /).
const PAGE_FILES = new Set(['', 'index.html', 'boot.mjs']);
// The provenance files at the page's root, with the page's cache policy (server.py PROVENANCE_FILES).
const PROVENANCE_FILES = new Set(['source.json', 'LICENSE.txt', 'COPYING.txt', 'THIRD_PARTY_NOTICES.md']);
// The trees the client reads uncompressed with strong ETags (server.py FETCH_TREES): the fetch backend's mounts
// (main.cpp MountFileSystems: data, sound, fonts; plEmscriptenFetchBackend.js refuses a file whose HEAD has a
// weak ETag, a content encoding or no Content-Length, and a range answer with another ETag or an encoding) and the HD
// texture packs (plWGPUPipeline.js: identity and a Content-Length). Docs/Web/Deploying.md, "What a host of the trees
// must serve": uncompressed. Cloudflare's edge compresses font/ttf and application/wasm among others for a browser's
// Accept-Encoding and then weakens the ETag; an origin's Cache-Control: no-transform opts out (Cloudflare, "Content
// compression"). Measured on a staging Worker.
const FETCH_TREES = new Set(['fonts', 'data', 'sound', 'hdpack']);
// The trees the client reads by byte range: the fetch backend's mounts (plEmscriptenFetchBackend.js sends Range with
// If-Match; nothing else in the client or the page sends Range, and the HD packs are read whole). The Worker answers
// Range and If-Range there only. Elsewhere it ignores them, as the asset binding does, and streams the whole file
// (200, Accept-Ranges: none), so that no request makes the Worker hold a file in memory.
const RANGE_TREES = new Set(['fonts', 'data', 'sound']);
// The page document. Cloudflare's edge removes a text/html answer's ETag when its HTML features (Email Obfuscation,
// Automatic HTTPS Rewrites) may rewrite it (Cloudflare, "ETag headers"), so the page could not revalidate; they leave
// an answer with Cache-Control: no-transform alone (Cloudflare, "Email Address Obfuscation"). Measured on a staging
// Worker.
const PAGE_DOCUMENTS = new Set(['', 'index.html']);
const MAX_JOIN_BODY = 1024;
// The status relay (server.py relay_status and StatusRelay). The page cannot read the ini's Server.Status URL across
// origins (and its Content-Security-Policy names no such origin), so the page document names /runtime/status on its
// own origin (emit_static.py) and the Worker fetches the deployment's URL, STATUS_URL from emit_worker.py, never one
// from the request. As the native status thread (UruLoginDialogProc's LoginDialogShardStatus thread and its
// CurlCallback, plClient/win32/winmain.cpp): the body whatever the HTTP status, at most 255 bytes, redirects followed,
// a failed request's error text instead. relay_status's timeout is 5 s. The answer is reused for STATUS_SECONDS
// (server.py STATUS_SECONDS), because the route is anonymous.
const STATUS_BYTES = 255;
const STATUS_TIMEOUT_MS = 5000;
const STATUS_SECONDS = 10;
// deployment.py status_url: an http(s) URL, as it keeps one from the ini
const STATUS_URL = /^https?:\/\/[^\s\x00-\x1f\x7f]{1,2040}$/;

// The first segment of a path's decoded segments, when the path is inside a tree (more than one segment), else null.
function treeOf(pathname) {
  let parts;
  try {
    parts = decodeURIComponent(pathname).split('/');
  } catch {
    parts = pathname.split('/');
  }
  return parts.length > 2 && parts[0] === '' ? parts[1] : null;
}

function cachePolicy(pathname, hasEtag) {
  // server.py common_headers: the game trees under /data (and /sound, /hdpack) revalidate by ETag ("private,
  // no-cache", and X-Plasma-Asset-Cache for the page's chunk cache); the page, its module and its code (/build/*,
  // /input/*) and the provenance files revalidate by ETag too ("no-cache", as the kit's _headers has them);
  // everything else is no-store. The fetch trees add no-transform (FETCH_TREES).
  let parts;
  try {
    parts = decodeURIComponent(pathname).split('/');
  } catch {
    parts = pathname.split('/');
  }
  let cacheable = parts.length > 2 && parts[0] === '' && parts[1] === 'data' && CACHEABLE_DATA.has(parts[2]);
  cacheable ||=
    parts.length > 2 && parts[0] === '' && parts[1] === 'sound' && parts[parts.length - 1] !== '.fetch-manifest';
  cacheable ||= parts.length === 3 && parts[0] === '' && parts[1] === 'hdpack';
  const page =
    (parts.length === 2 && parts[0] === '' && (PAGE_FILES.has(parts[1]) || PROVENANCE_FILES.has(parts[1]))) ||
    (parts.length === 3 && parts[0] === '' && (parts[1] === 'build' || parts[1] === 'input'));
  const out = {};
  if (parts.length === 3 && parts[1] === 'data' && parts[2] === '.asset-catalog' && hasEtag) {
    out['X-Plasma-Asset-Catalog'] = '1';
  }
  if (cacheable && hasEtag) {
    out['Cache-Control'] = 'private, no-cache';
    out['X-Plasma-Asset-Cache'] = 'chunks-v1';
  } else if (page && hasEtag) {
    out['Cache-Control'] = 'no-cache';
  } else {
    out['Cache-Control'] = 'no-store';
  }
  if (hasEtag && FETCH_TREES.has(treeOf(pathname))) out['Cache-Control'] += ', no-transform'; // server.py's rule
  if (parts.length === 2 && parts[0] === '' && PAGE_DOCUMENTS.has(parts[1])) out['Cache-Control'] += ', no-transform';
  return out;
}

function plain(status, body, extra = {}) {
  return new Response(body, {
    status,
    headers: { 'Content-Type': 'text/plain', 'Cache-Control': 'no-store', ...ISOLATION, ...NOSNIFF, ...extra },
  });
}

// RFC 9110 entity tags as representations.py matches() compares them: a list of tags, or *; strong comparison unless
// weak.
const TAG = '(?:W/)?"[\\x21\\x23-\\x7e\\x80-\\xff]*"';
const TAG_LIST = new RegExp(`^[ \\t]*${TAG}(?:[ \\t]*,[ \\t]*${TAG})*[ \\t]*$`);
function tagMatches(value, tag, weak) {
  if (value.trim() === '*') return true;
  if (!TAG_LIST.test(value)) return false;
  return value.match(new RegExp(TAG, 'g')).some((v) => (weak && v.startsWith('W/') ? v.slice(2) : v) === tag);
}

// server.py respond(): one range, bytes=first-last, first- or -suffix; anything else is no range (the whole file).
// Returns [first, last] (either may be '') or null.
function rangeSpec(value) {
  const m = /^bytes=(\d*)-(\d*)$/.exec(value.trim());
  return m && (m[1] || m[2]) ? [m[1], m[2]] : null;
}

// The bytes [start, end] a range spec selects in a file of `size` bytes, or null when none (416).
function rangeIn([first, last], size) {
  let start;
  let end = size - 1;
  if (first) {
    start = Number(first);
    if (last) end = Number(last);
  } else {
    start = Math.max(0, size - Number(last));
  }
  end = Math.min(end, size - 1);
  return start >= size || start > end ? null : [start, end];
}

async function byteLength(body) {
  const reader = body.getReader();
  let n = 0;
  for (let r = await reader.read(); !r.done; r = await reader.read()) n += r.value.byteLength;
  return n;
}

async function staticAsset(request, env, source = null) {
  // The deployment's Content-Security-Policy (deployment.py content_security_policy) comes from emit_worker.py; the
  // page is not served without it. source, when given, answers the GET in place of the asset binding (the status
  // relay's file, statusFile), and its answer then takes the same steps as a kit file's.
  const csp = env.CONTENT_SECURITY_POLICY;
  if (typeof csp !== 'string' || !csp) return plain(500, 'Gateway not configured\n');
  const url = new URL(request.url);
  const original = url.pathname;
  if (url.pathname === '/') url.pathname = '/index.html'; // server.py: / is the page
  // The asset binding answers If-None-Match (304) but ignores Range and If-Match; its answers carry no Content-Length
  // header (the runtime adds one from a body's known length, and none to a HEAD answer). Measured on Cloudflare and
  // under wrangler dev. So the Worker asks for the file with GET, without Accept-Encoding (the
  // file's own bytes), and answers If-Match, If-None-Match after it, Range and If-Range itself in server.py respond()'s
  // order (RFC 9110 13.2.2), Range and If-Range in the trees the client range-reads only (RANGE_TREES). A HEAD
  // answer, a range and a 416 need the file's size: the Worker counts the body's bytes for a HEAD (as they stream
  // past) and reads a range's whole file. The runtime keeps a Content-Length header the Worker sets only on an answer
  // without a body, as a HEAD answer is.
  const head = request.method === 'HEAD';
  const ranges = RANGE_TREES.has(treeOf(original));
  const asked = new Headers(request.headers);
  const ifMatch = asked.get('If-Match');
  const ifNoneMatch = asked.get('If-None-Match');
  const spec = head || !ranges || asked.get('Range') === null ? null : rangeSpec(asked.get('Range'));
  const ifRange = asked.get('If-Range');
  for (const name of ['Range', 'If-Range', 'If-Match', 'Accept-Encoding']) asked.delete(name);
  if (ifMatch !== null) asked.delete('If-None-Match');
  const res = await (source === null
    ? env.ASSETS.fetch(new Request(url.toString(), { method: 'GET', headers: asked }))
    : source(asked));
  const headers = new Headers(res.headers);
  headers.delete('Content-Length');
  let body = res.body;
  let status = res.status;
  if (status === 404) {
    if (body !== null) await body.cancel();
    body = head ? null : 'not found\n'; // server.py's 404 body and type
    if (head) headers.set('Content-Length', '10');
    headers.delete('ETag');
    headers.set('Content-Type', 'text/plain');
  } else {
    const dot = url.pathname.lastIndexOf('.');
    const ext = dot > url.pathname.lastIndexOf('/') ? url.pathname.slice(dot).toLowerCase() : '';
    if (GENERATED.has(url.pathname)) headers.set('Content-Type', 'application/octet-stream');
    else if (MIME[ext]) headers.set('Content-Type', MIME[ext]);
    else if (!headers.get('Content-Type')) headers.set('Content-Type', 'application/octet-stream');
  }
  const etag = headers.get('ETag');
  if (status === 200 && etag !== null) {
    const empty = (code) => {
      // no body, Content-Length 0 (server.py's 412 and 416)
      headers.set('Content-Length', '0');
      return [code, null];
    };
    let answer = null;
    if (ifMatch !== null && !tagMatches(ifMatch, etag, false)) {
      answer = empty(412);
    } else if (ifMatch !== null && ifNoneMatch !== null && tagMatches(ifNoneMatch, etag, true)) {
      answer = [304, null];
    } else if (spec !== null && (ifRange === null || ifRange === etag)) {
      const bytes = new Uint8Array(await res.arrayBuffer());
      body = null;
      const size = bytes.byteLength;
      const r = rangeIn(spec, size);
      headers.set('Content-Range', r === null ? `bytes */${size}` : `bytes ${r[0]}-${r[1]}/${size}`);
      if (r === null) {
        headers.set('Content-Type', 'text/plain');
        answer = empty(416);
      } else {
        answer = [206, bytes.subarray(r[0], r[1] + 1)];
      }
    } else if (head) {
      const size = await byteLength(body);
      body = null;
      headers.set('Content-Length', String(size));
      answer = [200, null];
    }
    if (answer !== null) {
      if (body !== null) await body.cancel();
      [status, body] = answer;
    }
  }
  for (const [k, v] of Object.entries({
    ...ISOLATION,
    ...NOSNIFF,
    'Content-Security-Policy': csp,
    'Accept-Ranges': ranges ? 'bytes' : 'none',
  }))
    headers.set(k, v);
  for (const [k, v] of Object.entries(cachePolicy(original, headers.has('ETag')))) headers.set(k, v);
  return new Response(body, { status, headers });
}

// At most `limit` bytes of a body, then the rest is cancelled unread (relay_status response.read(255)).
async function readAtMost(body, limit) {
  const out = new Uint8Array(limit);
  let n = 0;
  if (body === null) return out.slice(0, 0);
  const reader = body.getReader();
  try {
    while (n < limit) {
      const r = await reader.read();
      if (r.done) break;
      const take = Math.min(r.value.byteLength, limit - n);
      out.set(r.value.subarray(0, take), n);
      n += take;
    }
  } finally {
    reader.cancel().catch(() => {});
  }
  return out.slice(0, n);
}

// server.py relay_status: the status URL's body (any HTTP status, redirects followed, at most STATUS_BYTES), or the
// failed request's error text, cut to STATUS_BYTES of UTF-8 as relay_status cuts it.
async function relayStatus(url) {
  try {
    const res = await fetch(url, {
      headers: { 'User-Agent': 'UruClient/1.0' },
      redirect: 'follow',
      signal: AbortSignal.timeout(STATUS_TIMEOUT_MS),
    });
    return await readAtMost(res.body, STATUS_BYTES);
  } catch (e) {
    return new TextEncoder().encode(String((e && e.message) || e)).slice(0, STATUS_BYTES);
  }
}

// server.py StatusRelay, in this isolate: one fetch at a time (a request that arrives during one waits for its
// answer), and its answer, a failed fetch's error text included, reused for STATUS_SECONDS. Only plain bytes are kept
// between requests, never a Response or a stream. The fetch runs under ctx.waitUntil, so it completes for the
// requests waiting on it even when the request that started it has ended.
const statusRelay = { url: null, body: null, at: 0, pending: null };
function statusBody(url, ctx) {
  const s = statusRelay;
  if (s.url === url && s.body !== null && Date.now() - s.at < STATUS_SECONDS * 1000) return Promise.resolve(s.body);
  if (s.url !== url || s.pending === null) {
    s.url = url;
    s.body = null;
    const pending = relayStatus(url).then((body) => {
      if (s.pending === pending) {
        s.body = body;
        s.at = Date.now();
        s.pending = null;
      }
      return body;
    });
    s.pending = pending;
    ctx.waitUntil(pending);
  }
  return s.pending;
}

// representations.py blob_tag: the strong ETag server.py gives a body it serves from memory
async function blobTag(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return '"sha256-' + Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join('') + '"';
}

async function statusEndpoint(request, env, ctx) {
  // GET and HEAD /runtime/status: server.py resolve() serves the relay's answer as a body from memory (respond():
  // application/octet-stream, its blob_tag ETag, the conditional requests, no-store), and 404 when the deployment's
  // ini names no status URL. The answer then takes a kit file's steps (staticAsset): If-Match and If-None-Match, the
  // isolation headers, nosniff, the Content-Security-Policy, the cache policy; a Range is ignored, as outside the
  // trees.
  const url = env.STATUS_URL;
  if (url === undefined || url === null || url === '') return staticAsset(request, env); // no status: 404
  if (typeof url !== 'string' || !STATUS_URL.test(url)) return plain(500, 'Gateway not configured\n');
  return staticAsset(request, env, async (asked) => {
    const body = await statusBody(url, ctx);
    const tag = await blobTag(body);
    const headers = { 'Content-Type': 'application/octet-stream', ETag: tag };
    // the asset binding's step: If-None-Match (asked keeps it only without If-Match) answers 304
    const ifNoneMatch = asked.get('If-None-Match');
    if (ifNoneMatch !== null && tagMatches(ifNoneMatch, tag, true)) return new Response(null, { status: 304, headers });
    return new Response(body.slice(), { status: 200, headers });
  });
}

async function joinEndpoint(request, env) {
  // server.py do_POST: POST /runtime/join answers {"token", "expires"}. Same-origin pages only: Origin is one of the
  // deployment's page origins (never derived from the request's Host, which the client chooses); Sec-Fetch-Site is
  // same-origin when sent; Content-Type is application/json, which a cross-site form cannot send without a
  // preflight; the body is at most 1 KiB. At most 2 x connectsPerMinutePerIp calls per client address and minute
  // (the JOINS rate-limiting binding). The token is never logged.
  //
  // The token blocks pages on other sites and replays. It does not authenticate a scripted client, which can send
  // any Origin; the per-address limits bound what such a client can open.
  const url = new URL(request.url);
  if (url.pathname !== '/runtime/join') return plain(404, 'Not found\n');
  const cfg = parseConfig(env);
  if (cfg === null) return plain(500, 'Gateway not configured\n');
  const ip = clientAddress(request);
  if (ip === null) return plain(400, 'Client address missing from the proxy\n');
  const site = request.headers.get('Sec-Fetch-Site');
  const length = request.headers.get('Content-Length') || '0';
  const ctype = (request.headers.get('Content-Type') || '').split(';')[0].trim().toLowerCase();
  if (
    !cfg.origins.includes(request.headers.get('Origin')) ||
    (site !== null && site !== 'same-origin') ||
    ctype !== 'application/json' ||
    !/^[0-9]+$/.test(length) ||
    Number(length) > MAX_JOIN_BODY
  ) {
    return plain(403, 'Join refused\n');
  }
  if (cfg.limits.connectsPerMinutePerIp <= 0 || !(await env.JOINS.limit({ key: ipKey(ip) })).success) {
    return plain(429, 'Too many join requests\n');
  }
  const [token, expires] = await joinToken.issue(cfg.key, cfg.deployment, cfg.ttl);
  // json.dumps({"token": ..., "expires": ...}), as server.py writes it
  return new Response(`{"token": ${JSON.stringify(token)}, "expires": ${expires}}`, {
    status: 200,
    headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', ...ISOLATION, ...NOSNIFF },
  });
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === '/connect' || (request.headers.get('Upgrade') || '').toLowerCase() === 'websocket') {
      return handleConnect(request, env, ctx);
    }
    if (request.method === 'POST') return joinEndpoint(request, env);
    if (request.method === 'GET' || request.method === 'HEAD') {
      if (url.pathname === '/runtime/status') return statusEndpoint(request, env, ctx);
      return staticAsset(request, env);
    }
    if (request.method === 'OPTIONS') {
      // server.py do_OPTIONS without asset origins: every preflight is refused
      return new Response(null, {
        status: 403,
        headers: { 'Content-Length': '0', 'Cache-Control': 'no-store', ...NOSNIFF },
      });
    }
    return plain(501, 'Unsupported method\n');
  },
};
