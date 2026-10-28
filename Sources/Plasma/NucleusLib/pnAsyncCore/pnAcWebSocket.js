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

// The JS half of pnAsyncCore's Emscripten socket backend
// (pnAcSocketEmscripten.cpp). One browser WebSocket per Plasma TCP connection, opened
// against a WebSocket-to-TCP proxy (a raw bidirectional byte pipe; the URL contract is the
// gateway's, web/deploy/plasma_web/gateway.py).
//
// Every function here runs on the calling thread (no __proxy): the C++ side calls them
// only from its socket thread, a pthread that lives in its own worker event loop, so the
// WebSockets live in that worker and their events call back into wasm on that thread.
addToLibrary({
  $PLWS: { sockets: {}, next: 1 },

  // joinPtr (may be 0) = the page origin's join endpoint. With one, a fresh single-use admission
  // token is fetched (POST, same origin) before the WebSocket opens and offered as the subprotocol
  // "plasma.join.<token>" beside "plasma" (never in the URL). The handle is returned at once, as before; until the
  // socket exists PLWS.sockets[h] holds a placeholder (send -1, buffered 0, close abandons it). A token that cannot
  // be had ends the connect like a refused handshake: plws_on_close with 1006, so the C++ side reports the
  // original connect failure (kNetErrConnectFailed).
  // Why a subprotocol: the browser's WebSocket constructor sets no request header except Sec-WebSocket-Protocol,
  // whose values it takes from the protocols list. The gateway reads the token from that header
  // (web/deploy/plasma_web/join_token.py offered_token; the Worker's gateway, web/deploy/worker, does the same)
  // and answers with "plasma" alone, so the token is never echoed
  // back; a token in the URL would instead be written to access logs and proxies' logs. The token is limited to the
  // characters a subprotocol may hold (an HTTP token), which the reply check below enforces.
  plws_open__deps: ['$PLWS', '$UTF8ToString', '$plwsAttach', 'plws_on_close'],
  plws_open: (urlPtr, joinPtr) => {
    urlPtr >>>= 0;
    joinPtr >>>= 0;
    var url = UTF8ToString(urlPtr);
    var join = joinPtr ? UTF8ToString(joinPtr) : '';
    var h = PLWS.next++;
    if (!join) {
      try {
        plwsAttach(h, new WebSocket(url));
      } catch (e) {
        err('pnAcWebSocket: WebSocket(' + url + ') failed: ' + e);
        return 0;
      }
      return h;
    }
    var pending = { pending: true };
    PLWS.sockets[h] = pending;
    var fail = (why) => {
      if (PLWS.sockets[h] !== pending) return;
      delete PLWS.sockets[h];
      err('pnAcWebSocket: no gateway join token (' + why + ')');
      _plws_on_close(h, 1006);
    };
    fetch(join, { method: 'POST', cache: 'no-store', credentials: 'same-origin', redirect: 'error',
                  headers: { 'Content-Type': 'application/json' }, body: '{}' })
      .then((r) => r.ok ? r.json() : Promise.reject('HTTP ' + r.status))
      .then((j) => {
        if (PLWS.sockets[h] !== pending) return;
        if (!j || typeof j.token !== 'string' || !/^[A-Za-z0-9._-]{1,512}$/.test(j.token)) return fail('bad reply');
        var ws;
        try {
          ws = new WebSocket(url, ['plasma', 'plasma.join.' + j.token]);
        } catch (e) {
          return fail('WebSocket: ' + e);
        }
        plwsAttach(h, ws);
      }, (e) => fail(String(e)));
    return h;
  },

  $plwsAttach__deps: ['$PLWS', 'malloc', 'plws_on_open', 'plws_on_message', 'plws_on_close'],
  $plwsAttach: (h, ws) => {
    ws.binaryType = 'arraybuffer';
    PLWS.sockets[h] = ws;
    ws.onopen = () => _plws_on_open(h);
    ws.onmessage = (e) => {
      if (!(e.data instanceof ArrayBuffer)) {
        err('pnAcWebSocket: ignored a non-binary proxy message');
        return;
      }
      var n = e.data.byteLength;
      if (!n) return;
      var p = _malloc(n) >>> 0;
      // No memory for the copy: the C++ side gets a null pointer and treats it as a failed read.
      if (p) HEAPU8.set(new Uint8Array(e.data), p);
      _plws_on_message(h, p, n);  // the C++ side frees p
    };
    // A failed connect fires error then close; an open socket that fails fires the same
    // pair. close carries the code, so error needs no handler of its own.
    ws.onerror = () => {};
    ws.onclose = (e) => {
      if (PLWS.sockets[h] !== ws) return;
      delete PLWS.sockets[h];
      _plws_on_close(h, e.code);
    };
  },

  // 0 = queued, -1 = no such socket or not open
  plws_send__deps: ['$PLWS'],
  plws_send: (h, ptr, len) => {
    // wasm32 i32 arguments may arrive signed once the heap passes 2 GiB.
    ptr >>>= 0;
    var ws = PLWS.sockets[h];
    if (!ws || ws.readyState !== 1) return -1;
    // slice(): a copy in a plain ArrayBuffer (send() rejects views of shared memory).
    ws.send(HEAPU8.slice(ptr, ptr + len));
    return 0;
  },

  plws_buffered__deps: ['$PLWS'],
  plws_buffered: (h) => {
    var ws = PLWS.sockets[h];
    return ws ? (ws.bufferedAmount | 0) : 0;
  },

  // Closes without any further event: the C++ side has already forgotten the handle.
  plws_close__deps: ['$PLWS'],
  plws_close: (h) => {
    var ws = PLWS.sockets[h];
    if (!ws) return;
    delete PLWS.sockets[h];
    if (ws.pending) return;     // the token fetch, if it completes, finds the handle gone
    ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
    try { ws.close(); } catch (e) {}
  },
});
