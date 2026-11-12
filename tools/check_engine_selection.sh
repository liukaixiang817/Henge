#!/bin/bash
# Each invocation uses a dedicated Simulator and fresh process for each engine.
set -euo pipefail
if [ "$#" != 1 ]; then echo "usage: $0 SIMULATOR_UDID" >&2; exit 2; fi
HENGE_SIMULATOR="$1"
HENGE_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HENGE_ROOT"
python3 tools/build_ios_engines.py --sdk iphonesimulator > build/dual-engines-simulator.log 2>&1
HENGE_CC=(xcrun --sdk iphonesimulator clang -target arm64-apple-ios17.0-simulator -std=c11 -O2 -Wall -Wextra
          -Iios/Henge/Native -Iios/Engine)
mkdir -p build/engine-tests/results
"${HENGE_CC[@]}" tests/test_engine_selection.c ios/Henge/Native/henge-vm.c -o build/engine-tests/test_engine_selection
"${HENGE_CC[@]}" -fobjc-arc tools/simulator_engine_lab.m ios/Henge/Native/henge-vm.c -framework Foundation \
    -o build/engine-tests/simulator_engine_lab
HENGE_STAGE="$(mktemp -d "${TMPDIR:-/private/tmp}/henge-engine-tests.XXXXXX")"
trap 'rm -rf "$HENGE_STAGE"' EXIT
ditto --norsrc --noextattr build/engines/iphonesimulator/Frameworks "$HENGE_STAGE/Frameworks"
cp build/engine-tests/test_engine_selection "$HENGE_STAGE/test_engine_selection"
HENGE_REVISION="$(cat ios/engine-mainline.txt)"
for mode in 0 1; do
    for test in normal missing wrong abi; do
        folder="$HENGE_STAGE/Frameworks"
        if [ "$test" = missing ]; then folder="$HENGE_STAGE/missing"; fi
        if [ "$test" = wrong ] || [ "$test" = abi ]; then
            folder="$HENGE_STAGE/$test-$mode"
            name=HengeMainlineEngine
            other=HengeRosettaEngine
            if [ "$mode" = 1 ]; then name=HengeRosettaEngine; other=HengeMainlineEngine; fi
            mkdir -p "$folder/$name.framework"
            if [ "$test" = wrong ]; then
                cp "$HENGE_STAGE/Frameworks/$other.framework/$other" "$folder/$name.framework/$name"
            else
                # A stale or corrupt engine API is an error, never a fallback.
                printf '%s\n' '#include "henge-engine-api.h"' \
                    'const HengeEngineAPI *henge_engine_get_api(void) { static const HengeEngineAPI api = {.abi = 999}; return &api; }' \
                    > "$HENGE_STAGE/bad-api.c"
                "${HENGE_CC[@]}" -dynamiclib "$HENGE_STAGE/bad-api.c" -o "$folder/$name.framework/$name"
                codesign --force --sign - "$folder/$name.framework/$name" 2>/dev/null
            fi
        fi
        xcrun simctl spawn "$HENGE_SIMULATOR" "$HENGE_STAGE/test_engine_selection" "$mode" "$folder" "$HENGE_REVISION" "$test" \
            > "build/engine-tests/results/$mode-$test.log" 2>&1
        echo "PASS engine selection: mode=$mode case=$test"
    done
done
