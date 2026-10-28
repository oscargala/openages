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

// The client's address and the key the per-address limits count it under. This module has no Workers imports, so the
// contract test can load it in Node and compare it with gateway.py's ip_key.

// A canonical IPv4 literal: four decimal octets, no leading zeros (as Python's ipaddress accepts them).
export const IPV4 = /^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$/;
const HEXTET = /^[0-9A-Fa-f]{1,4}$/;

export function ipv4Number(text) {
  return text.split('.').reduce((acc, part) => acc * 256 + Number(part), 0);
}

function ipv6Groups(text) {
  // The eight 16-bit groups of an IPv6 literal, or null. Accepts the RFC 4291 forms: eight groups, one "::" for a run
  // of zero groups, and a dotted IPv4 tail in place of the last two groups. A zone index ("%eth0") is refused: it
  // names an interface of the sender's host and is never part of the address Cloudflare reports.
  if (!text.includes(':') || !/^[0-9A-Fa-f:.]+$/.test(text)) return null;
  const lastColon = text.lastIndexOf(':');
  let head = text;
  const last = text.slice(lastColon + 1);
  if (last.includes('.')) {
    if (!IPV4.test(last)) return null;
    const n = ipv4Number(last);
    head = text.slice(0, lastColon + 1) + Math.floor(n / 65536).toString(16) + ':' + (n % 65536).toString(16);
  }
  const halves = head.split('::');
  if (halves.length > 2) return null;
  const split = (s) => (s === '' ? [] : s.split(':'));
  const front = split(halves[0]);
  const back = halves.length === 2 ? split(halves[1]) : [];
  if (![...front, ...back].every((g) => HEXTET.test(g))) return null;
  let groups;
  if (halves.length === 1) {
    if (front.length !== 8) return null;
    groups = front;
  } else {
    if (front.length + back.length > 7) return null;
    groups = [...front, ...Array(8 - front.length - back.length).fill('0'), ...back];
  }
  return groups.map((g) => parseInt(g, 16));
}

function formatIpv6(groups) {
  // RFC 5952 text, as Python's ipaddress writes it: lowercase, no leading zeros, the first longest run of two or more
  // zero groups written as "::".
  let best = -1,
    bestLen = 0;
  for (let i = 0; i < 8;) {
    if (groups[i] !== 0) {
      i++;
      continue;
    }
    let j = i;
    while (j < 8 && groups[j] === 0) j++;
    if (j - i > bestLen) {
      best = i;
      bestLen = j - i;
    }
    i = j;
  }
  const hex = groups.map((g) => g.toString(16));
  if (bestLen < 2) return hex.join(':');
  return hex.slice(0, best).join(':') + '::' + hex.slice(best + bestLen).join(':');
}

export function ipKey(address) {
  // The per-address limits' key, or null for a text that is not an IP address: an IPv4 address as is; an IPv6
  // address by its /64, since one host often holds many addresses of one /64; an IPv4-mapped IPv6 address
  // (::ffff:a.b.c.d) as its IPv4 address. The text is gateway.py ip_key's for the same address.
  if (typeof address !== 'string') return null;
  if (IPV4.test(address)) return address;
  const groups = ipv6Groups(address);
  if (groups === null) return null;
  if (groups.slice(0, 5).every((g) => g === 0) && groups[5] === 0xffff) {
    return [groups[6] >> 8, groups[6] & 255, groups[7] >> 8, groups[7] & 255].join('.');
  }
  return formatIpv6([...groups.slice(0, 4), 0, 0, 0, 0]) + '/64';
}

export function clientAddress(request) {
  // The address Cloudflare accepted the request from. Cloudflare sets CF-Connecting-IP on every request that reaches
  // the Worker from the internet and replaces any value the client sent; X-Forwarded-For is never read. A missing or
  // malformed value means the request did not come through Cloudflare's edge, and is refused (null) rather than
  // counted under one shared key.
  const value = (request.headers.get('CF-Connecting-IP') || '').trim();
  return ipKey(value) === null ? null : value;
}
