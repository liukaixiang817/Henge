#!/bin/bash
# Run the real CPU engine's tests in an explicitly selected iOS Simulator.
# No device frameworks, certificate, iPhone, or Hypervisor.framework is used.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: tools/check_simulator_cpu.sh SIMULATOR_UDID" >&2
    exit 2
fi
HENGE_SIMULATOR="$1"
HENGE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HENGE_ROOT"
HENGE_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"
HENGE_CC="xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -isysroot $HENGE_SDK"
# The native reference harness keeps several HvCpu values on its main stack.
HENGE_FLAGS="-std=c11 -Wall -Wextra -O2 -Icore -Ihost -Idevices -Wl,-stack_size,0x2000000 -Wno-unused-command-line-argument"
HENGE_TESTS=(test_classify test_jit_region test_operands test_jit test_mem test_mmu test_exc test_tso test_rosetta)
HENGE_TARGETS=()
for name in "${HENGE_TESTS[@]}"; do HENGE_TARGETS+=("build/simulator/$name"); done
make B=build/simulator "CC=$HENGE_CC" "CFLAGS=$HENGE_FLAGS" "${HENGE_TARGETS[@]}"

# launchd_sim may wait for a Documents privacy prompt when loading a binary
# directly from a checkout there. Only this new temporary folder is removed.
HENGE_STAGE="$(mktemp -d "${TMPDIR:-/private/tmp}/henge-cpu-tests.XXXXXX")"
trap 'rm -rf "$HENGE_STAGE"' EXIT
mkdir -p build/simulator/results
for name in "${HENGE_TESTS[@]}"; do cp "build/simulator/$name" "$HENGE_STAGE/$name"; done
for mode in 0 1; do
    for name in "${HENGE_TESTS[@]}"; do
        if [ "$name" = test_rosetta ]; then continue; fi
        # test_tso initializes and checks both models itself.
        if [ "$name" = test_tso ] && [ "$mode" = 0 ]; then continue; fi
        SIMCTL_CHILD_HV_ROSETTA=0 SIMCTL_CHILD_HV_SOFTWARE_TSO="$mode" xcrun simctl spawn "$HENGE_SIMULATOR" "$HENGE_STAGE/$name" \
            > "build/simulator/results/$name-$mode.log" 2>&1
        echo "PASS $name (software TSO $mode)"
    done
done
for mode in inline reference; do
    HENGE_CMD=(xcrun simctl spawn "$HENGE_SIMULATOR" "$HENGE_STAGE/test_rosetta")
    if [ "$mode" = reference ]; then HENGE_CMD+=(reference); fi
    rc=0
    "${HENGE_CMD[@]}" \
        > "build/simulator/results/test_rosetta-$mode.log" 2>&1 || rc=$?
    if [ "$rc" -eq 77 ]; then
        echo "SKIP test_rosetta ($mode): host FEAT_AFP unavailable"
    elif [ "$rc" -ne 0 ]; then
        cat "build/simulator/results/test_rosetta-$mode.log" >&2
        exit "$rc"
    else
        echo "PASS test_rosetta ($mode)"
    fi
done
