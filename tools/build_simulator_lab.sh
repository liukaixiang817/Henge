#!/bin/bash
# SIMULATOR_DEPS is a prefix containing simulator builds of libslirp and GLib.
# The arm64 iOS device libraries used by the app are not interchangeable.
set -euo pipefail
HENGE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HENGE_ROOT"
HENGE_DEPS="${SIMULATOR_DEPS:-$HENGE_ROOT/build/simulator-deps/sysroot}"
HENGE_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
HENGE_CC="xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -isysroot $HENGE_SDK"
HENGE_FLAGS="-std=c11 -Wall -Wextra -O2 -Icore -Ihost -Idevices"
HENGE_INC="$(PKG_CONFIG_LIBDIR="$HENGE_DEPS/lib/pkgconfig" PKG_CONFIG_PATH= pkg-config --cflags slirp)"
HENGE_LIBS="$(PKG_CONFIG_LIBDIR="$HENGE_DEPS/lib/pkgconfig" PKG_CONFIG_PATH= pkg-config --libs --static slirp)"
make B=build/simulator "CC=$HENGE_CC" "CFLAGS=$HENGE_FLAGS" "SLIRP_CFLAGS=$HENGE_INC" "SLIRP_LIBS=$HENGE_LIBS" build/simulator/simulator_lab
mkdir -p build/rosetta-probes
for name in rosetta_guest rosetta_guest_extended; do
    xcrun --sdk macosx clang -arch x86_64 -mmacosx-version-min=12.0 -O2 -Wall -Wextra \
        "tests/$name.c" -o "build/rosetta-probes/$name"
done
