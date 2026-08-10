#!/usr/bin/env bash
#
# Build and run the layout tests. No Pebble SDK needed - just gcc.
#
#     tools/test/run.sh
#
# Requires tools/test/generated.h, which tools/tune.py writes. If it is
# missing or stale, run tune.py first.
#
# The sanitisers are not optional decoration: the ":20 shows one instead of
# twenty" bug was an out-of-bounds read of kOnes[19], which is undefined
# behaviour rather than a crash. Without -fsanitize=address the sweep would
# have passed while the watch drew the wrong word.

set -euo pipefail

cd "$(dirname "$0")"

if [ ! -f generated.h ]; then
  echo "tools/test/generated.h is missing - run: python3 tools/tune.py" >&2
  exit 1
fi

if [ ../../src/c/geometry.h -nt generated.h ]; then
  echo "warning: geometry.h is newer than generated.h; re-run tools/tune.py" >&2
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

gcc -std=c11 -I. \
    -Wall -Wextra -Werror \
    -Wno-unused-function -Wno-unused-parameter \
    -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    -g -O1 \
    -o "$OUT/harness" harness.c stub.c

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$OUT/harness"

# The settings page is JavaScript running in the phone's browser, so it needs
# a different runner. Node is NOT a build dependency of this project and must
# not become one - if it is absent, say so and carry on rather than failing.
if command -v node >/dev/null 2>&1; then
  node clay-slider.test.js
else
  echo "  clay slider          skipped (no node)"
fi
