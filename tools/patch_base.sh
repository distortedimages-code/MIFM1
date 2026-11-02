#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# MIFM1's changes to fm1-base and its felucca-bsp (patches/*.patch, paths under base/), applied to the submodules'
# checkouts before a build: once (a patch already there is left), or the build stops (a new fm1-base that moved
# their lines: the patch made again on it).
set -e
cd "$(dirname "$0")/.."
for p in patches/*.patch; do
    if patch -p1 -d base -R -f -s --dry-run < "$p" > /dev/null 2>&1; then
        continue                                     # already applied
    fi
    patch -p1 -d base -N -f -s < "$p" || { echo "patch_base: $p does not apply to this fm1-base"; exit 1; }
done
