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

/*
 * One online, bounded, content-verified metadata catalog per fetch backend.
 * Unsupported/invalid/absent entries retain the original per-file HEAD path.
 * Payload reads still use the pinned strong tag and exact conditional ranges.
 */
addToLibrary({
  $PlasmaAssetCatalog: {
    create: function() {
      let pending;
      const maxBytes = 8 * 1024 * 1024;
      const origin = typeof self !== 'undefined' && self.location && self.location.origin;
      if (!origin || origin === 'null') return {lookup:async function() { return null; }};
      function eligible(value) {
        try {
          const u = new URL(value, origin);
          if (u.origin !== origin || u.username || u.password || u.search || u.hash) return null;
          const path = decodeURIComponent(u.pathname);
          const parts = path.split('/');
          if (parts[0] !== '' || parts[1] !== 'data' || !['dat','sfx','python','SDL','avi','resource.dat'].includes(parts[2])) return null;
          if (parts.slice(2).some(p => !p || p === '.' || p === '..' || p.includes('\\') || /[\x00-\x1f\x7f]/.test(p))) return null;
          return parts.slice(2).join('/');
        } catch (_) { return null; }
      }
      async function load() {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 15000);
        try {
          const r = await fetch(new URL('/data/.asset-catalog', origin).href,
            {cache:'no-store',redirect:'error',signal:controller.signal});
          const tag = r.headers.get('ETag');
          const length = r.headers.get('Content-Length');
          const encoding = r.headers.get('Content-Encoding');
          if (r.status !== 200 || r.headers.get('X-Plasma-Asset-Catalog') !== '1' ||
              !/^"sha256-[0-9a-f]{64}"$/.test(tag || '') ||
              !/^\d+$/.test(length || '') || +length > maxBytes ||
              (encoding !== null && encoding.trim().toLowerCase() !== 'identity')) return null;
          const reader = r.body.getReader();
          const chunks = []; let count = 0;
          for (;;) {
            const part = await reader.read();
            if (part.done) break;
            count += part.value.length;
            if (count > maxBytes) { await reader.cancel(); return null; }
            chunks.push(part.value);
          }
          if (count !== +length) return null;
          const bytes = new Uint8Array(count); let offset = 0;
          for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
          const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)),
            b => b.toString(16).padStart(2,'0')).join('');
          if (tag !== '"sha256-' + hash + '"') return null;
          const body = JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(bytes));
          if (body.schema !== 1 || body.policy !== 'chunks-v1' || !Array.isArray(body.entries) || body.entries.length > 16384) return null;
          const entries = new Map();
          for (const e of body.entries) {
            if (typeof e.path !== 'string' || !e.path || e.path.startsWith('/') ||
                !Number.isSafeInteger(e.size) || e.size < 0 ||
                !/^"sha256-[0-9a-f]{64}"$/.test(e.etag || '')) return null;
            const url = origin + '/data/' + e.path.split('/').map(encodeURIComponent).join('/');
            const path = eligible(url);
            if (path !== e.path || entries.has(path)) return null;
            entries.set(path, Object.freeze({size:e.size,etag:e.etag,cacheable:true}));
          }
          return entries;
        } catch (_) { return null; }
        finally { clearTimeout(timer); controller.abort(); }
      }
      return {
        lookup: async function(url) {
          const path = eligible(url);
          if (path === null) return null;
          if (!pending) pending = load();
          const entries = await pending;
          return entries ? entries.get(path) || null : null;
        }
      };
    }
  }
});
