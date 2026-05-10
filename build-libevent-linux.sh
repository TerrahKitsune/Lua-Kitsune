#!/usr/bin/env bash
# Builds libevent 2.2 from source and installs static libs into libevent/linux/lib.
# Run from the repo root inside WSL (or any Linux shell) before cmake.
set -e

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$REPO_ROOT/libevent/linux"

if [ -f "$OUT_DIR/lib/libevent.a" ]; then
    echo "libevent/linux/lib/libevent.a already exists — skipping build."
    exit 0
fi

echo "Building libevent 2.2 from source..."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

git clone --depth 1 https://github.com/libevent/libevent.git "$TMP/src"
mkdir -p "$TMP/build"
cd "$TMP/build"
cmake "$TMP/src" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEVENT__DISABLE_OPENSSL=ON \
    -DEVENT__DISABLE_SAMPLES=ON \
    -DEVENT__DISABLE_TESTS=ON \
    -DEVENT__LIBRARY_TYPE=STATIC \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build . --parallel

mkdir -p "$OUT_DIR/lib"
find . -name '*.a' -exec cp {} "$OUT_DIR/lib/" \;

# Copy the Linux-generated event-config.h so CMake uses it instead of the
# Windows version bundled in libevent/include/event2/event-config.h.
mkdir -p "$OUT_DIR/include/event2"
cp include/event2/event-config.h "$OUT_DIR/include/event2/event-config.h"

echo "Done. Static libs in $OUT_DIR/lib:"
ls "$OUT_DIR/lib/"
