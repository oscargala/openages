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
"""Read-only HTTP representation identity, and the bodies of file answers read as they are sent.

The index is process-local and bounded. It caches content hashes by an open file's
nanosecond metadata signature, never by size/mtime seconds alone. No asset is written.

A file answer's body is read from the open file while it is sent (Body), never copied ahead
of the answer, so an answer holds at most one piece of CHUNK bytes in memory and writes no
temporary file.
"""

from collections import OrderedDict
import hashlib
import os
import re
import threading
import time

# The largest piece of a file read at a time, by the hash in describe() and by a body (Body.read). It is the most
# memory one answer's body holds.
CHUNK = 1 << 20


class RepresentationChanged(OSError):
    """The open file changed while its identity was read or its body was sent."""


def signature(stream):
    s = os.fstat(stream.fileno())
    return s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns


def blob_tag(blob):
    return '"sha256-' + hashlib.sha256(blob).hexdigest() + '"'


class Representations:
    def __init__(self, limit=8192):
        self.limit = limit
        self.cache = OrderedDict()
        self.lock = threading.Lock()

    def describe(self, path, stream, metrics=None):
        if metrics is not None:
            metrics.update(hashed_bytes=0, hash_ms=0.0)
        before = signature(stream)
        with self.lock:
            hit = self.cache.get(path)
            if hit and hit[0] == before:
                self.cache.move_to_end(path)
                return before, hit[1]
        started = time.perf_counter()
        digest = hashlib.sha256()
        stream.seek(0)
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            digest.update(chunk)
        if signature(stream) != before:
            raise RepresentationChanged("asset changed during hashing")
        if metrics is not None:
            metrics.update(hashed_bytes=before[2], hash_ms=round((time.perf_counter() - started) * 1000, 3))
        tag = '"sha256-' + digest.hexdigest() + '"'
        with self.lock:
            self.cache[path] = before, tag
            self.cache.move_to_end(path)
            while len(self.cache) > self.limit:
                self.cache.popitem(last=False)
        return before, tag


class Body:
    """The bytes [start, start + length) of an open file, read in pieces while its answer is sent. `expected` is the
    file's identity (signature) that describe() computed the answer's ETag for.

    Nothing is copied ahead of the answer and no temporary file is written. read() returns the next piece, at most CHUNK
    bytes, and only after checking that the file's identity is still `expected`. When the file has changed or become
    shorter it raises RepresentationChanged instead, and the piece it read is never returned. So a client receives the
    last byte of a body only if the identity held through the read of every byte: the guarantee a copy of the whole
    body made before the headers would give, without the copy. check() is the same test before the first read, so a
    file that changed after describe() can still be refused before any header is sent.

    Atomic path replacement keeps the old descriptor. Before the headers, check() treats the metadata change it makes
    as a change (a 503). While the body is sent, read() keeps reading the old file (unchanged()).
    """

    def __init__(self, stream, expected, start, length):
        self.stream, self.expected, self.left = stream, expected, length
        stream.seek(start)

    def check(self):
        if signature(self.stream) != self.expected:
            raise RepresentationChanged("asset changed before its answer")

    def read(self, size=CHUNK):
        """The next piece of the body (at most size and at most CHUNK bytes), or b'' after its last byte."""
        n = min(CHUNK if size is None or size < 0 else size, CHUNK, self.left)
        if n <= 0:
            return b""
        piece = self.stream.read(n)
        if not piece:
            raise RepresentationChanged("asset truncated while its body was sent")
        self.left -= len(piece)
        if not self.unchanged():
            raise RepresentationChanged("asset changed while its body was sent")
        return piece

    def unchanged(self):
        """Whether the bytes still to be read are the ones `expected` names: the identity is unchanged, or the file has
        only been unlinked since.

        Replacing the path by rename(2) (os.replace, the way a deploy swaps a file) or deleting it unlinks the old
        inode, and that alone moves its ctime on APFS and ext4. An unlinked inode is safe to go on reading. With
        st_nlink == 0 no directory entry names it, so nothing can open it again. Its bytes can change only through a
        descriptor opened before the unlink, and a write through one moves its mtime (and its size when it grows or
        shrinks), which are still compared with `expected`. Any other change ends the body."""
        s = os.fstat(self.stream.fileno())
        now = s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns
        return now == self.expected or (s.st_nlink == 0 and now[:4] == self.expected[:4])


# Entity-tag opaque text may contain commas and backslashes; split(',') is wrong.
_TAG = r'(?:W/)?"[\x21\x23-\x7e\x80-\xff]*"'
_LIST = re.compile(r"[ \t]*" + _TAG + r"(?:[ \t]*,[ \t]*" + _TAG + r")*[ \t]*")


def matches(value, tag, *, weak=False):
    if value.strip() == "*":
        return True
    if not _LIST.fullmatch(value):
        return False
    tags = re.findall(_TAG, value)
    return any((v.removeprefix("W/") if weak else v) == tag for v in tags)
