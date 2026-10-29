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
"""The gateway Worker's configuration for one deployment. Writes files only; deploys nothing.

  emit_worker.py <deployment.json> <kit dir> <out dir> [--name <worker name>]

<kit dir> is emit_static.py's output for the same deployment (the page, runtime/config.json, runtime/server.ini,
build/...). <out dir> receives:
  wrangler.json   wrangler.toml (this directory) plus the deployment's values: the vars DEPLOYMENT, JOIN_TTL,
                  UPSTREAMS (deployment.py upstreams(): the server.ini hosts and extraUpstreamHosts at the client port,
                  with their resolved IPv4 addresses, and extraUpstreamNetworks), ORIGINS (gateway.origins),
                  PUBLIC_HOSTS (gateway.publicHosts), LIMITS (the gateway.limits the Worker enforces),
                  STATUS_URL (the server.ini's Server.Status URL, deployment.py status_url, which the Worker relays
                  at /runtime/status as server.py does; empty when the ini names none) and
                  CONTENT_SECURITY_POLICY (the kit page document's, deployment.py content_security_policy); the
                  CONNECTS and JOINS rate limits (connectsPerMinutePerIp, and twice it for /runtime/join, as server.py
                  allows); main and assets
  assets/         the kit without the files only a static host reads (_headers, _redirects): the Worker sets
                  server.py's headers itself

The join key is never read or written here: it is the Worker secret JOIN_KEY (`wrangler secret put JOIN_KEY`, or an env
file outside the repository for `wrangler dev`). The Worker always requires join tokens, so a harness deployment
(tokenless, loopback only) and one without origins or public hosts are refused.

The client address comes from Cloudflare (CF-Connecting-IP, which Cloudflare sets itself), so gateway.trustedProxies,
the proxy ranges a self-hosted gateway.py trusts, has no meaning here and is refused. Of gateway.limits the Worker
enforces connectionsPerIp, connectsPerMinutePerIp, bytesPerSecond, idleSeconds and connections, and its own
isolateQueueBytes: the browser frames the relays of one isolate hold together (src/queue_budget.js). memoryBytes
bounds gateway.py's own buffers, by admission; the Worker bounds its queued frames by isolateQueueBytes instead, so
memoryBytes is not passed on.
"""

import argparse
import json
import os
import shutil
import tomllib
from pathlib import Path

from plasma_web import deployment as deployment_file  # the web package (web/deploy: installed, or on PYTHONPATH)

HERE = Path(__file__).resolve().parent

PAGES_ONLY = {"_headers", "_redirects"}
# The gateway.limits keys the Worker enforces (gateway.js parseConfig), and the ones it does not.
ENFORCED_LIMITS = (
    "connectionsPerIp",
    "connectsPerMinutePerIp",
    "bytesPerSecond",
    "idleSeconds",
    "connections",
    "isolateQueueBytes",
)
NOT_ENFORCED_LIMITS = ("memoryBytes",)
# The page document's status relay path (deployment.py public_document status_relay; server.py and the Worker answer it)
STATUS_RELAY = "/runtime/status"


def worker_config(d, page_document, assets_dir, out_dir, name=None):
    """The wrangler.json document for deployment d (deployment.Deployment) and the page document of its kit."""
    if d.harness:
        raise deployment_file.DeploymentError(
            "a harness deployment is tokenless and loopback-only; the Worker always requires join tokens"
        )
    if d.join_secret_file is None:
        raise deployment_file.DeploymentError("the Worker needs gateway.join (its admission tokens)")
    if not d.gateway_origins:
        raise deployment_file.DeploymentError("the Worker needs gateway.origins (the page origin)")
    if not d.public_hosts:
        raise deployment_file.DeploymentError("the Worker needs gateway.publicHosts (the page hostname)")
    if d.trusted_proxies:
        raise deployment_file.DeploymentError(
            "gateway.trustedProxies does not apply to the Worker: Cloudflare "
            "sets the client address (CF-Connecting-IP) itself"
        )
    extra = set(d.limits) - set(ENFORCED_LIMITS) - set(NOT_ENFORCED_LIMITS)
    if extra:
        raise deployment_file.DeploymentError(f"the Worker does not know gateway.limits {sorted(extra)}")
    # The kit's page document names the status relay exactly when the deployment has a status URL (emit_static.py):
    # a kit that does not (another deployment's, or one emitted before the relay) would send the page to the ini's own
    # URL, which its Content-Security-Policy blocks.
    status = page_document.get("shard", {}).get("status")
    if status != (STATUS_RELAY if d.status_url else None):
        raise deployment_file.DeploymentError(
            f"the kit's shard.status is {status!r}, but this deployment's is "
            f"{STATUS_RELAY if d.status_url else None!r} (the Worker's relay of its Server.Status URL): emit the kit "
            "again from this deployment"
        )
    pairs, resolved = d.upstreams()
    base = tomllib.loads((HERE / "wrangler.toml").read_text())
    cfg = dict(base)
    cfg["name"] = name or base["name"]
    cfg["main"] = os.path.relpath(HERE / base["main"], out_dir)
    cfg["assets"] = dict(base["assets"], directory=os.path.relpath(assets_dir, out_dir))
    limits = {k: d.limits[k] for k in ENFORCED_LIMITS}
    cfg["vars"] = {
        "DEPLOYMENT": d.name,
        "JOIN_TTL": d.join_ttl,
        "UPSTREAMS": {
            "pairs": sorted([h, p] for h, p in pairs),
            "networks": {"port": d.client_port, "cidrs": [str(n) for n in d.extra_networks]}
            if d.extra_networks
            else None,
        },
        "ORIGINS": list(d.gateway_origins),
        "PUBLIC_HOSTS": list(d.public_hosts),
        "LIMITS": limits,
        "STATUS_URL": d.status_url,
        "CONTENT_SECURITY_POLICY": deployment_file.content_security_policy(page_document),
    }
    rates = {"CONNECTS": limits["connectsPerMinutePerIp"], "JOINS": 2 * limits["connectsPerMinutePerIp"]}
    # A limit of 0 refuses everything; the Worker does that from LIMITS without calling the binding (whose limit
    # must be positive).
    cfg["ratelimits"] = [dict(r, simple=dict(r["simple"], limit=max(1, rates[r["name"]]))) for r in base["ratelimits"]]
    return cfg, resolved


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deployment")
    ap.add_argument("kit")
    ap.add_argument("out")
    ap.add_argument("--name", help="the Worker name (default: wrangler.toml name)")
    a = ap.parse_args()
    kit, out = Path(a.kit), Path(a.out)
    if not (kit / "runtime/config.json").is_file():
        ap.error(f"{kit} is not an emit_static.py kit (no runtime/config.json)")
    if out.exists() and any(out.iterdir()):
        ap.error(f"{out} is not empty")
    try:
        d = deployment_file.load(a.deployment)
        page_document = json.loads((kit / "runtime/config.json").read_text())
        out.mkdir(parents=True, exist_ok=True)
        cfg, resolved = worker_config(d, page_document, out / "assets", out, a.name)
    except (OSError, ValueError) as e:
        ap.error(f"invalid deployment: {e}")
    shutil.copytree(kit, out / "assets", ignore=lambda _dir, names: [n for n in names if n in PAGES_ONLY])
    (out / "wrangler.json").write_text(json.dumps(cfg, indent=1) + "\n")
    v = cfg["vars"]
    print(
        json.dumps(
            {
                "deployment": d.name,
                "wrangler": str(out / "wrangler.json"),
                "upstreams": v["UPSTREAMS"],
                "resolved": resolved,
                "origins": v["ORIGINS"],
                "public_hosts": v["PUBLIC_HOSTS"],
                "limits": cfg["vars"]["LIMITS"],
                "limits_not_enforced": list(NOT_ENFORCED_LIMITS),
                "status_url": v["STATUS_URL"] or None,
                "content_security_policy": cfg["vars"]["CONTENT_SECURITY_POLICY"],
                "secrets_needed": ["JOIN_KEY"],
            },
            indent=1,
        )
    )


if __name__ == "__main__":
    main()
