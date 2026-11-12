#!/bin/bash
# Host regression: real Swift acquisition flow and native iOS allocator, with
# app switching/debugserver simulated. Physical iOS/TXM execution still needs a device.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="$PWD/build/jit-check"
APP="$OUT/JITTests.app/Contents"
mkdir -p "$APP/MacOS" "$APP/Resources" "$OUT/modules"
export CLANG_MODULE_CACHE_PATH="$OUT/modules"
export SWIFT_MODULECACHE_PATH="$OUT/modules"

xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -Itests/support \
  -DHENGE_JIT_ALLOCATOR_TEST -Dsys_icache_invalidate=henge_test_icache_invalidate \
  -c ios/Henge/Native/henge-ios-jit.c -o "$OUT/allocator.o"
xcrun --sdk macosx clang -std=c11 -Wall -Wextra -Werror \
  -c tests/support/ios_jit_runtime.c -o "$OUT/runtime.o"
xcrun --sdk macosx swiftc -emit-library -emit-module -module-name UIKit \
  tests/support/ios_jit_uikit.swift -o "$OUT/libUIKit.dylib" \
  -emit-module-path "$OUT/UIKit.swiftmodule"
xcrun --sdk macosx swiftc -swift-version 5 -I "$OUT" -L "$OUT" -lUIKit \
  -import-objc-header tests/support/ios_jit_runtime.h \
  ios/Henge/JIT/JITBootstrap.swift ios/Henge/JIT/JITNetworkAdvice.swift ios/Henge/JIT/JITSetup.swift \
  ios/HengeJITHelper/HengeJITMessages.swift tests/support/ios_jit_services.swift \
  tests/test_ios_jit.swift "$OUT/allocator.o" "$OUT/runtime.o" \
  -Xlinker -rpath -Xlinker "$OUT" -o "$APP/MacOS/JITTests"
cat > "$APP/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.henge.jit-tests</string>
<key>CFBundleExecutable</key><string>JITTests</string>
<key>CFBundlePackageType</key><string>APPL</string>
</dict></plist>
PLIST
cp ios/Henge/Resources/henge-jit.js "$APP/Resources/"
"$APP/MacOS/JITTests"

# Also compile the production C and breakpoint assembly against the iOS SDK.
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=17.0 \
  -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -c ios/Henge/Native/henge-ios-jit.c -o "$OUT/allocator-ios.o"
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=17.0 \
  -c ios/Henge/Native/henge-brk.S -o "$OUT/brk-ios.o"
xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=17.0 \
  -std=c11 -Wall -Wextra -Werror -Icore -Ihost -Idevices -Iios/Engine -Iios/Henge/Native \
  -c ios/Henge/Native/henge-vm.c -o "$OUT/vm-ios.o"
