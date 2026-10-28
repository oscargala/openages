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

export const INPUT_CHANNEL = 'plasma-input-v1';
const types = new Set(['key', 'text', 'move', 'button', 'wheel', 'focus', 'reset', 'pointer-lock', 'fullscreen']);

export function createInputQueue(limit = 256) {
  if (!Number.isInteger(limit) || limit < 4) throw new RangeError('Input queue needs at least four slots');
  let events = [],
    overflows = 0,
    focused = null;
  function push(event) {
    if (!event || !types.has(event.type)) return false;
    if (event.type === 'focus') focused = !!event.active;
    const last = events.at(-1);
    if (event.type === 'move' && last?.type === 'move' && event.relative === last.relative) {
      // Never coalesce across a button/key edge. Relative movement accumulates.
      events[events.length - 1] = { ...event, dx: last.dx + event.dx, dy: last.dy + event.dy };
      return true;
    }
    if (events.length >= limit) {
      // Dropping a key-up or button-up silently could leave the avatar moving.
      // Fail closed: clear the backlog and require the engine to release state.
      events = [{ type: 'reset', reason: 'overflow' }];
      if (focused !== null) events.push({ type: 'focus', active: focused });
      overflows++;
    }
    events.push(event);
    return true;
  }
  return {
    push,
    drain() {
      const batch = events;
      events = [];
      return batch;
    },
    get pending() {
      return events.length;
    },
    get overflows() {
      return overflows;
    },
  };
}

// MessagePort keeps these packets away from Emscripten's own worker command
// protocol. The owner transfers a dedicated port to the client pthread bridge.
export function createInputPortSender(port, { schedule, cancel, limit = 256 } = {}) {
  const queue = createInputQueue(limit);
  let handle = null,
    disposed = false;
  function flush() {
    if (handle !== null && cancel) cancel(handle);
    handle = null;
    const events = queue.drain();
    if (events.length && !disposed) port.postMessage({ channel: INPUT_CHANNEL, events });
  }
  function send(event) {
    if (disposed || !queue.push(event)) return;
    if (!schedule || event.type === 'reset' || (event.type === 'focus' && !event.active)) flush();
    else if (handle === null) handle = schedule(flush);
  }
  return {
    send,
    flush,
    dispose() {
      if (disposed) return;
      send({ type: 'reset', reason: 'transport-dispose' });
      disposed = true;
      if (handle !== null && cancel) cancel(handle);
      handle = null;
    },
  };
}

export function createInputPortReceiver(port, { limit = 256 } = {}) {
  const queue = createInputQueue(limit);
  function receive({ data }) {
    if (data?.channel !== INPUT_CHANNEL || !Array.isArray(data.events)) return;
    if (data.events.length > limit) {
      queue.push({ type: 'reset', reason: 'oversized-batch' });
      return;
    }
    for (const event of data.events) queue.push(event);
  }
  port.addEventListener('message', receive);
  port.start();
  return {
    drain: queue.drain,
    get pending() {
      return queue.pending;
    },
    get overflows() {
      return queue.overflows;
    },
    dispose() {
      port.removeEventListener('message', receive);
      queue.drain();
    },
  };
}
