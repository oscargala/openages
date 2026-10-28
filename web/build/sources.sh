#!/usr/bin/env bash
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

# Read web/build/sources.lock, and download and verify the files it pins.
#
#   web/build/sources.sh get <name> <key>     print one value of a record, e.g. "get python-3.12.9 url"
#   web/build/sources.sh fetch <name> <dir>   download the record's file into <dir> unless a verified
#                                             copy is there; print the file's path
#   web/build/sources.sh verify <name> <file> check a file against the record's SHA-256
#   web/build/sources.sh sha256 <file>        print a file's SHA-256
#
# The build scripts source this file for the same functions (lock_get, lock_fetch, lock_verify,
# sha256_of). A download goes to a temporary name in <dir>, and is renamed to its final name only
# after its SHA-256 matched, so <dir> never holds an unverified file under a pinned name. An existing
# file that does not match is refused rather than replaced: it is not this script's to delete.
# Needs bash 3.2 or newer, curl, and sha256sum, shasum or openssl.

SOURCES_LOCK=${SOURCES_LOCK:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/sources.lock}

# lock_get <name> <key>: print the value, or fail with a message naming what is missing.
lock_get() {
    local value
    value=$(awk -v name="$1" -v key="$2" '
        $1 == name { for (i = 2; i <= NF; i++) if (index($i, key "=") == 1) { print substr($i, length(key) + 2); n++ } }
        END { exit n == 1 ? 0 : 1 }' "$SOURCES_LOCK") \
        || { echo "sources.lock: expected one '$2' for '$1' in $SOURCES_LOCK" >&2; return 1; }
    printf '%s\n' "$value"
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{ print $1 }'
    else
        openssl dgst -sha256 -r "$1" | awk '{ print $1 }'
    fi
}

# lock_verify <name> <file>: succeed only if the file's SHA-256 is the pinned one.
lock_verify() {
    local want got
    want=$(lock_get "$1" sha256) || return 1
    [ -f "$2" ] || { echo "sources: missing $2 ($1)" >&2; return 1; }
    got=$(sha256_of "$2")
    [ "$got" = "$want" ] && return 0
    echo "sources: $2 has SHA-256 $got; sources.lock pins $want for $1" >&2
    return 1
}

# lock_fetch <name> <dir>: print the path of the verified file, downloading it first if needed.
lock_fetch() {
    local name=$1 dir=$2 url file out part
    url=$(lock_get "$name" url) || return 1
    file=$(lock_get "$name" file) || return 1
    out=$dir/$file
    if [ -e "$out" ]; then
        lock_verify "$name" "$out" || { echo "sources: refusing $out; remove it to download it again" >&2; return 1; }
        printf '%s\n' "$out"
        return 0
    fi
    mkdir -p "$dir"
    part=$out.part.$$
    echo "sources: downloading $url" >&2
    if ! curl -fsSL --retry 3 --retry-delay 5 -o "$part" "$url"; then
        rm -f "$part"
        echo "sources: download failed: $url" >&2
        return 1
    fi
    if ! lock_verify "$name" "$part"; then
        rm -f "$part"
        echo "sources: refusing the download of $url" >&2
        return 1
    fi
    mv "$part" "$out"
    printf '%s\n' "$out"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    set -euo pipefail
    usage() { sed -n '/^# Read web/,/^# Needs/p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
    [ $# -ge 2 ] || usage
    case $1 in
        get) [ $# -eq 3 ] || usage; lock_get "$2" "$3" ;;
        fetch) [ $# -eq 3 ] || usage; lock_fetch "$2" "$3" ;;
        verify) [ $# -eq 3 ] || usage; lock_verify "$2" "$3" && echo "OK $3" ;;
        sha256) [ $# -eq 2 ] || usage; sha256_of "$2" ;;
        *) usage ;;
    esac
fi
