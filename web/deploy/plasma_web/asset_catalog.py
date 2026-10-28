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
"""Validated read-only asset identities, lazy and refreshed by a catalog request at most once per interval.

This is a mutable-directory catalog, not atomic release publication. Conditional
payload requests continue to enforce the file identities captured here.

With an interval (the origin server's CATALOG_RESCAN_SECONDS), a read within that many seconds of the last scan
answers the catalog of that scan: an anonymous request then costs no walk of the data tree. A file changed in the
meantime is still caught by the conditional payload requests. Without an interval, every read scans.
"""

import json
import os
from pathlib import Path
import threading
import time
from plasma_web.representations import RepresentationChanged

TOPS = ("dat", "sfx", "python", "SDL", "avi", "resource.dat")
MAX_ENTRIES = 16384
MAX_BYTES = 8 * 1024 * 1024


class AssetCatalog:
    def __init__(self, root, representations, tops=TOPS, top_roots=None, interval=0, clock=time.monotonic):
        self.root = Path(root)
        self.tops = tuple(tops)
        # {top: other root} serves that top-level folder from another tree (the sealed game-code
        # directory's python/ and SDL/); catalog paths stay <top>/... as served.
        self.top_roots = {top: Path(r) for top, r in (top_roots or {}).items()}
        self.representations = representations
        self.lock = threading.Lock()
        self.state = None
        self.blob = None
        # the seconds a scan answers for (0: every read scans), and when the last one ended
        self.interval, self.clock = interval, clock
        self.scanned = None

    def root_of(self, top):
        return self.top_roots.get(top, self.root)

    def scan(self):
        state = {}
        for top in self.tops:
            root = self.root_of(top)
            base = root / top
            files = (
                [base]
                if base.is_file()
                else (
                    (Path(folder) / name for folder, dirs, names in os.walk(base) for name in sorted(names))
                    if base.is_dir()
                    else []
                )
            )
            for path in files:
                s = path.stat()
                state[path.relative_to(root).as_posix()] = (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
                if len(state) > MAX_ENTRIES:
                    raise RepresentationChanged("catalog entry bound exceeded")
        return state

    def fresh(self):
        """Whether the last scan still answers (within the interval)."""
        return self.blob is not None and self.scanned is not None and self.clock() - self.scanned < self.interval

    def read(self):
        started = time.perf_counter()
        metrics = dict(hashed_bytes=0, hash_ms=0.0)
        try:
            with self.lock:
                if self.fresh():
                    # the last scan's catalog (a read that waited for a scan gets that scan's)
                    metrics.update(
                        catalog_entries=len(self.state), catalog_ms=round((time.perf_counter() - started) * 1000, 3)
                    )
                    return self.blob, metrics
                current = self.scan()
                if self.blob is None or current != self.state:
                    entries = []
                    for relative in sorted(current):
                        path = self.root_of(relative.split("/")[0]) / relative
                        detail = {}
                        with path.open("rb") as stream:
                            identity, etag = self.representations.describe(str(path), stream, detail)
                        if identity != current[relative]:
                            raise RepresentationChanged("catalog changed during capture")
                        metrics["hashed_bytes"] += detail["hashed_bytes"]
                        metrics["hash_ms"] += detail["hash_ms"]
                        entries.append(dict(path=relative, size=identity[2], etag=etag))
                    if self.scan() != current:
                        raise RepresentationChanged("catalog namespace changed during capture")
                    blob = json.dumps(
                        dict(schema=1, policy="chunks-v1", entries=entries), ensure_ascii=True, separators=(",", ":")
                    ).encode()
                    if len(blob) > MAX_BYTES:
                        raise RepresentationChanged("catalog byte bound exceeded")
                    self.blob, self.state = blob, current
                self.scanned = self.clock()
                metrics.update(
                    catalog_entries=len(current), catalog_ms=round((time.perf_counter() - started) * 1000, 3)
                )
                return self.blob, metrics
        except OSError as e:
            raise RepresentationChanged("catalog unavailable or changed during capture") from e
