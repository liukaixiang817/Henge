#!/bin/zsh
# Builds Henge for iOS and writes ~/Desktop/Henge.ipa (or ~/Desktop/Henge.tipa with --trollstore).
# Unsigned build, then signed ad hoc inside out with the entitlements file named by the
# HENGE_ENTITLEMENTS environment variable (default Henge/Henge.entitlements), as Husk's
# package_ipa.sh does: the sideloader (SideStore, AltStore, Impactor) re-signs it
# with the user's own certificate and keeps the entitlements it can.
# --trollstore strips the entitlements TrollStore bans under PPL/TXM.
set -e
IOS="$(cd "$(dirname "$0")/.." && pwd)"
TROLLSTORE=false
[ "$1" = "--trollstore" ] && TROLLSTORE=true
OUT="${HENGE_IPA_OUTPUT:-${HENGE_IPA_OUT:-$HOME/Desktop/Henge.ipa}}"
[ "$TROLLSTORE" = true ] && OUT="${HENGE_IPA_OUTPUT:-${HENGE_IPA_OUT:-$HOME/Desktop/Henge.tipa}}"
[ -d "$IOS/deps/StikJIT.xcframework" ] || "$IOS/scripts/fetch_deps.sh"
# The bundled samples are Mac programs and a disk image. Prepare them outside
# Xcode's iOS build-script environment before the app copies them as resources.
env -u SDKROOT -u IPHONEOS_DEPLOYMENT_TARGET make -C "$IOS/.." build/tl-samples.dmg
cd "$IOS"
xcodegen generate >/dev/null
DD="${HENGE_DERIVED_DATA:-$IOS/build/DerivedData}"
mkdir -p "$IOS/build"
xcodebuild -project Henge.xcodeproj -scheme Henge -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
    -derivedDataPath "$DD" CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY="" build > "$DD.buildlog" 2>&1 || {
    # Never package what an older build left behind.
    tail -n 160 "$DD.buildlog"
    echo "==> the build failed; nothing packaged" >&2
    exit 1
}
grep -E "BUILD" "$DD.buildlog" || true
APP="$DD/Build/Products/Release-iphoneos/Henge.app"
rc=0
for f in "$APP/Henge" "$APP/henge-jit.js" "$APP/PlugIns/HengeJITHelper.appex/HengeJITHelper" \
         "$APP/Frameworks/StikJIT.framework/StikJIT" "$APP/Frameworks/StikJIT.framework/Info.plist" \
         "$APP/Frameworks/HengeMainlineEngine.framework/HengeMainlineEngine" "$APP/Frameworks/HengeMainlineEngine.framework/Info.plist" \
         "$APP/Frameworks/HengeRosettaEngine.framework/HengeRosettaEngine" "$APP/Frameworks/HengeRosettaEngine.framework/Info.plist"; do
    if [ -f "$f" ]; then printf "  ok       %s\n" "${f#$APP/}"; else echo "  MISSING  ${f#$APP/}" >&2; rc=1; fi
done
[ $rc -eq 0 ] || { echo "==> bundle is not installable; refusing to package" >&2; exit 1; }

STAGE="$(mktemp -d "${TMPDIR:-/private/tmp}/henge-ipa.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/Payload"
ditto --norsrc --noextattr "$APP" "$STAGE/Payload/Henge.app"
ENT="$STAGE/entitlements.plist"
# HENGE_ENTITLEMENTS picks another file, as for xcodebuild (Henge/Henge.personal.entitlements for a free team).
cp "$IOS/${HENGE_ENTITLEMENTS:-Henge/Henge.entitlements}" "$ENT"
BANNED_KEYS=("com.apple.private.cs.debugger" "dynamic-codesigning" "com.apple.private.skip-library-validation")
if [ "$TROLLSTORE" = true ]; then for k in "${BANNED_KEYS[@]}"; do plutil -remove "$k" "$ENT" 2>/dev/null || true; done; fi
SAPP="$STAGE/Payload/Henge.app"
plutil -insert HengeBuildCommit -string "$(git -C "$IOS/.." rev-parse --short=12 HEAD 2>/dev/null || echo unknown)" "$SAPP/Info.plist"
plutil -insert HengeMainlineEngineCommit -string "$(cat "$IOS/engine-mainline.txt")" "$SAPP/Info.plist"
plutil -insert HengeSelectableRosetta -bool true "$SAPP/Info.plist"
if ! git -C "$IOS/.." diff --quiet HEAD; then
  plutil -insert HengeBuildHasLocalChanges -bool true "$SAPP/Info.plist"
fi
plutil -insert HengeBuildDate -string "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$SAPP/Info.plist"
# Apple's boot ROM is fetched on the device (BootROM.swift) and must never ship. An older build folder can still
# hold a copy from when the app bundled it, so it goes, and nothing is packaged if any is left.
rm -f "$SAPP/AVPBooter.vmapple2.bin"
if [ -n "$(find "$STAGE" -iname 'AVPBooter*' -o -iname 'AVPSEPBooter*')" ]; then
  echo "Apple's boot ROM is in the app: not packaging." >&2
  exit 1
fi
sign() { codesign --force --sign - --timestamp=none "$@"; }
for fw in "$SAPP"/Frameworks/*.framework; do [ -d "$fw" ] && sign "$fw"; done
for ex in "$SAPP"/PlugIns/*.appex; do [ -d "$ex" ] && sign "$ex"; done
sign --entitlements "$ENT" "$SAPP"
codesign --verify --deep --strict "$SAPP"
EMBEDDED=$(codesign -d --entitlements - "$SAPP" 2>&1 || true)
# Every key the entitlements file asks for must be in the signature.
for k in $(plutil -convert json -o - "$ENT" | python3 -I -c 'import json, sys; print(" ".join(json.load(sys.stdin)))'); do
    grep -q "$k" <<< "$EMBEDDED" || { echo "==> $k did not embed; refusing to package" >&2; exit 1; }
done
( cd "$STAGE" && zip -qry "$STAGE/package.tmp" Payload )
unzip -tq "$STAGE/package.tmp"
cp "$STAGE/package.tmp" "$OUT.tmp"
mv -f "$OUT.tmp" "$OUT" # atomic: a half-written IPA never sits where the good one was
echo "==> $OUT ($(du -h "$OUT" | cut -f1))"
