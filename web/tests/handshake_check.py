#!/usr/bin/env python3
# /*==LICENSE==*
#
# CyanWorlds.com Engine - MMOG client, server and tools
# Copyright (C) 2011  Cyan Worlds, Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Additional permissions under GNU GPL version 3 section 7
#
# If you modify this Program, or any covered work, by linking or
# combining it with any of RAD Game Tools Bink SDK, Autodesk 3ds Max SDK,
# NVIDIA PhysX SDK, Microsoft DirectX SDK, OpenSSL library, Independent
# JPEG Group JPEG library, Microsoft Windows Media SDK, or Apple QuickTime SDK
# (or a modified version of those libraries),
# containing parts covered by the terms of the Bink SDK EULA, 3ds Max EULA,
# PhysX SDK EULA, DirectX SDK EULA, OpenSSL and SSLeay licenses, IJG
# JPEG Library README, Windows Media SDK EULA, or QuickTime SDK EULA, the
# licensors of this Program grant you additional
# permission to convey the resulting work. Corresponding Source for a
# non-source form of such a combination shall include the source code for
# the parts of OpenSSL and IJG JPEG Library used as well as that of the covered
# work.
#
# You can contact Cyan Worlds, Inc. by email legal@cyan.com
#  or by snail mail at:
#       Cyan Worlds, Inc.
#       14617 N Newport Hwy
#       Mead, WA   99021
#
# *==LICENSE==*/
"""Scripted Plasma connection handshakes through a gateway, without a login.

  handshake_check.py --backend gateway|worker --server-ini <shard server.ini> --out <dir> [--n 57]
                     [--services auth,gate,file,game]

Starts the local pair (gateway.py + server.py) or the Worker under `wrangler dev`, both with a throwaway non-harness
deployment of the shard's own server.ini, join tokens on, and limits high enough never to refuse
(connectsPerMinutePerIp 100000, connectionsPerIp 64). Each attempt does what the browser does per socket: POST
/runtime/join for a fresh token, then the WebSocket to /connect?host=..&port=.. with the token as a subprotocol; then,
through the relay, the client side of the connection handshake as the native client sends it:
  auth, gate, game  the connect packet and the service's connect data (plNglAuth.cpp, plNglGateKeeper.cpp and
                    plNglGame.cpp Connect), NetCli_Cli2Srv_Connect with the Diffie-Hellman y (pnNcCli.cpp ClientConnect,
                    pnNcEncrypt.cpp NetMsgCryptClientStart), the server's NetCli_Srv2Cli_Encrypt seed (pnNcCli.cpp
                    ClientRecvEncrypt), RC4 both ways from the combined 7-byte seed, then an encrypted PingRequest
                    answered by a PingReply that echoes it (kPingRequestFields and kPingReplyFields in
                    pnNpCli2Auth.cpp, pnNpCli2GateKeeper.cpp and pnNpCli2Game.cpp);
  file              Cli2File_Connect (plNglFile.cpp Connect: the client's build id, server type kSrvTypeNone) and
                    Cli2File_BuildIdRequest answered by File2Cli_BuildIdReply (pnNpCli2File.h), unencrypted.
No account, no password and no player are used. The server.ini keys stay in memory; the evidence holds outcomes only.
"""

import argparse
import asyncio
import base64
import json
import secrets
import struct
import time
import urllib.request
import uuid
from pathlib import Path

from websockets.asyncio.client import connect

from plasma_web import deployment as deployment_file  # the web package (web/deploy: installed, or on PYTHONPATH)
from plasma_web import join_token
import test_contract as contract  # LocalPair, WorkerDev (this folder, the script's own)

HERE = Path(__file__).resolve().parent

BUILD_ID, BUILD_TYPE, BRANCH_ID = 918, 50, 1  # CMakeLists.txt PRODUCT_BUILD_ID/TYPE/BRANCH_ID
PRODUCT = uuid.UUID("ea489821-6c35-4bd0-9dae-bb17c585e680")  # CMakeLists.txt PRODUCT_UUID
CONN_TYPE = {"auth": 10, "game": 11, "file": 16, "gate": 22}
DH_G_DEFAULT = {"auth": 41, "game": 73, "gate": 4}  # pnNbConst.h kNet{Auth,Game,GateKeeper}DefaultDhGValue
SEED_BYTES = 7  # pnNbConst.h kNetMaxSymmetricSeedBytes
DH_BITS = 512  # pnNbConst.h kNetDiffieHellmanKeyBits


class Rc4:
    def __init__(self, key):
        s = list(range(256))
        j = 0
        for i in range(256):
            j = (j + s[i] + key[i % len(key)]) & 255
            s[i], s[j] = s[j], s[i]
        self.s, self.i, self.j = s, 0, 0

    def __call__(self, data):
        s, i, j = self.s, self.i, self.j
        out = bytearray(len(data))
        for n, b in enumerate(data):
            i = (i + 1) & 255
            j = (j + s[i]) & 255
            s[i], s[j] = s[j], s[i]
            out[n] = b ^ s[(s[i] + s[j]) & 255]
        self.i, self.j = i, j
        return bytes(out)


def le_min(n):
    return n.to_bytes(max(1, (n.bit_length() + 7) // 8), "little")  # plBigNum::GetData_LE


def connect_packet(service):
    hdr_build = 0 if service == "file" else BUILD_ID  # plNglFile.cpp Connect: kFileSrvBuildId
    hdr = struct.pack("<BHIII", CONN_TYPE[service], 31, hdr_build, BUILD_TYPE, BRANCH_ID) + PRODUCT.bytes_le
    if service in ("auth", "gate"):
        data = struct.pack("<I", 20) + uuid.UUID(int=0).bytes_le  # dataBytes, token (nil before any login)
    elif service == "game":
        data = struct.pack("<I", 36) + uuid.UUID(int=0).bytes_le * 2  # dataBytes, accountUuid, ageUuid
    else:
        data = struct.pack("<III", 12, BUILD_ID, 0)  # dataBytes, buildId, kSrvTypeNone
    return hdr + data


class Stream:
    def __init__(self, ws):
        self.ws, self.buf = ws, b""

    async def read(self, n, timeout=10):
        while len(self.buf) < n:
            self.buf += await asyncio.wait_for(self.ws.recv(), timeout)
        out, self.buf = self.buf[:n], self.buf[n:]
        return out


async def handshake(ws, service, keys):
    s = Stream(ws)
    await ws.send(connect_packet(service))
    if service == "file":
        trans = secrets.randbelow(1 << 31)
        await ws.send(struct.pack("<III", 12, 10, trans))  # Cli2File_BuildIdRequest
        size, msg_id = struct.unpack("<II", await s.read(8))
        body = await s.read(size - 8)
        r_trans, result, build = struct.unpack("<III", body[:12])
        if msg_id != 10 or r_trans != trans:
            raise RuntimeError(f"file: unexpected reply id {msg_id}")
        return f"BuildIdReply result {result} buildId {build}"
    n, x, g = keys[service]
    b = int.from_bytes(secrets.token_bytes(DH_BITS // 8), "big")
    y = le_min(pow(g, b, n))
    await ws.send(bytes([0, 2 + len(y)]) + y)  # NetCli_Cli2Srv_Connect
    kind, length = await s.read(2)
    rest = await s.read(length - 2)
    if kind == 2:
        raise RuntimeError(f"Srv2CliError {struct.unpack('<I', rest[:4])[0]}")
    if kind != 1 or length != 2 + SEED_BYTES:
        raise RuntimeError(f"unexpected encrypt reply {kind}/{length}")
    client_seed = le_min(pow(x, b, n))[:SEED_BYTES].ljust(SEED_BYTES, b"\0")
    key = bytes(c ^ v for c, v in zip(client_seed, rest))  # CreateSymmetricKey
    out, inp = Rc4(key), Rc4(key)
    ping, trans, payload = int(time.monotonic() * 1000) & 0xFFFFFFFF, secrets.randbelow(1 << 31), secrets.token_bytes(4)
    if service == "auth":
        req = struct.pack("<HIII", 0, ping, trans, len(payload)) + payload
    elif service == "gate":
        req = struct.pack("<HIII", 0, trans, ping, len(payload)) + payload
    else:
        req = struct.pack("<HI", 0, ping)
    await ws.send(out(req))
    extra = ""
    msg_id = inp(await s.read(2))
    if service == "auth" and msg_id == struct.pack("<H", 0x1002):
        # Auth2Cli_ServerCaps (pnNpCli2Auth.h), which the shard sends first after the encrypt reply
        # (plNglAuth.cpp RecvMsg<Auth2Cli_ServerCaps>): capsBytes, capsBuffer
        size = struct.unpack("<I", inp(await s.read(4)))[0]
        inp(await s.read(size))
        extra = f", ServerCaps {size} B first"
        msg_id = inp(await s.read(2))
    reply = msg_id + inp(await s.read(len(req) - 2))
    if reply != req:
        raise RuntimeError(f"{service}: ping reply differs (id {reply[:2].hex()})")
    return "encrypted, PingReply echoed" + extra


def server_keys(server_ini):
    settings = deployment_file.read_server_ini(server_ini.read_text())
    keys = {}
    for service in ("auth", "gate", "game"):
        # pfServerIni.cpp ParseBase64KeyInto, pnNcChannel.cpp NetMsgChannelCreate
        n = int.from_bytes(base64.b64decode(settings[f"server.{service}.n"]), "big")
        x = int.from_bytes(base64.b64decode(settings[f"server.{service}.x"]), "big")
        g = int(settings.get(f"server.{service}.g", DH_G_DEFAULT[service]))
        keys[service] = (n, x, g)
    entry = settings.get("server.auth.host") or settings["server.gate.host"]
    return keys, entry, int(settings.get("server.port", 14617))


async def attempt(inst, host, port, service, keys):
    t = time.monotonic()
    row = {"service": service}
    try:

        def join():
            req = urllib.request.Request(
                inst.page + "/runtime/join",
                data=b"{}",
                method="POST",
                headers={"Origin": inst.origin, "Content-Type": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=5) as r:
                return json.loads(r.read())["token"]

        token = await asyncio.to_thread(join)
        async with connect(
            inst.ws(f"?host={host}&port={port}"),
            origin=inst.origin,
            compression=None,
            open_timeout=10,
            subprotocols=["plasma", join_token.PREFIX + token],
            max_size=None,
        ) as ws:
            row["detail"] = await handshake(ws, service, keys)
        row["ok"] = True
    except Exception as e:  # noqa: BLE001  (every failure is an outcome to count)
        row.update(ok=False, detail=f"{type(e).__name__}: {e}"[:200])
    row["ms"] = round((time.monotonic() - t) * 1000, 1)
    return row


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", choices=sorted(contract.BACKENDS), required=True)
    ap.add_argument("--server-ini", type=Path, required=True, help="the shard's server.ini (its public keys)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--n", type=int, default=57)
    ap.add_argument("--services", default="auth,gate,file,game")
    a = ap.parse_args()
    services = a.services.split(",")
    keys, host, port = server_keys(a.server_ini)
    run = a.out / f"{a.backend}-{int(time.time())}"
    run.mkdir(parents=True)
    secret_file = run / "join.key"
    join_token.init_secret(secret_file)
    try:
        inst = contract.BACKENDS[a.backend](
            run, a.server_ini, secret_file, {"connectsPerMinutePerIp": 100000, "connectionsPerIp": 64}
        )
    except BaseException:
        secret_file.unlink(missing_ok=True)
        raise
    rows = []
    try:
        for i in range(a.n):
            for service in services:
                row = await attempt(inst, host, port, service, keys)
                rows.append({"i": i, **row})
                if not row["ok"]:
                    print(json.dumps(rows[-1]), flush=True)
    finally:
        await asyncio.sleep(1)  # the last relay's log row
        inst.stop()
        secret_file.unlink(missing_ok=True)
        # WorkerDev.stop removed the kit copies (they hold the shard's server.ini); the evidence keeps outcomes only
    summary = {
        s: {
            "ok": sum(r["ok"] for r in rows if r["service"] == s),
            "n": sum(r["service"] == s for r in rows),
            "max_ms": max((r["ms"] for r in rows if r["service"] == s and r["ok"]), default=None),
            "details": sorted({r["detail"] for r in rows if r["service"] == s}),
        }
        for s in services
    }
    relay_rows = [r for r in inst.lines() if "sent" in r]
    refused_rows = [r for r in inst.lines() if "refused" in r]
    summary["_log"] = {
        "relay_rows": len(relay_rows),
        "refused_rows": len(refused_rows),
        "ends": sorted({r["end"] for r in relay_rows}),
        "upstreams": sorted({r["upstream"] for r in relay_rows}),
    }
    (run / "attempts.jsonl").write_text("".join(json.dumps(r) + "\n" for r in rows))
    doc = {"backend": a.backend, "shard": f"{host}:{port}", **summary}
    (run / "summary.json").write_text(json.dumps(doc, indent=1) + "\n")
    print(
        json.dumps(
            {
                "backend": a.backend,
                "run": str(run),
                **{s: f"{summary[s]['ok']}/{summary[s]['n']}" for s in services},
                "_log": summary["_log"],
            }
        )
    )


if __name__ == "__main__":
    asyncio.run(main())
