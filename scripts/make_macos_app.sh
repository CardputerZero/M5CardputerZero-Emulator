#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# make_macos_app.sh — assemble a self-contained M5CardputerZero Emulator.app
# from a finished `build/` directory, then produce a DMG.
#
# Why a .app bundle:
#  - One double-clickable icon instead of a folder of loose files.
#  - All resources (apps/*.dylib, assets, APPLaunch/share) live inside
#    Contents/Resources, and the executable cd's there at launch.
#  - The whole bundle is signed with ONE identity. When the binary dlopen's
#    apps/libAPPLaunch.dylib, library validation sees a matching signature and
#    allows the load — fixing "library load disallowed by system policy" that
#    ad-hoc signing loose files ran into.
#
# Signing identity:
#   SIGN_ID env var. Default "-" (ad-hoc). For a notarizable build pass a
#   "Developer ID Application: … (TEAMID)" identity and set NOTARIZE_PROFILE.
#
# Usage:
#   scripts/make_macos_app.sh <build_dir> <out_dir>
# Produces:
#   <out_dir>/M5CardputerZero Emulator.app
#   <out_dir>/M5CardputerZero-Emulator-macOS-arm64.dmg
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

BUILD_DIR="${1:-build}"
OUT_DIR="${2:-dist}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

APP_NAME="M5CardputerZero Emulator"
APP="${OUT_DIR}/${APP_NAME}.app"
SIGN_ID="${SIGN_ID:--}"          # "-" = ad-hoc
ICNS="${REPO_ROOT}/assets/macos/CardputerZero.icns"
BUNDLE_ID="com.cardputerzero.emulator"

echo "==> assembling ${APP}"
rm -rf "${APP}"
mkdir -p "${APP}/Contents/MacOS" "${APP}/Contents/Resources"

# Executable + the dlopen'd app dylibs are CODE → Contents/MacOS.
# (codesign signs everything under MacOS/ as nested code, which is what we want
# for the dylibs so library validation matches the bundle signature.)
cp "${BUILD_DIR}/cardputer-emu" "${APP}/Contents/MacOS/cardputer-emu"
cp -R "${BUILD_DIR}/apps"       "${APP}/Contents/MacOS/apps"

# Data resources (PNG/TTF/WAV) are NOT code → Contents/Resources, else codesign
# rejects them as "unsigned code objects". The binary finds them via ../Resources.
cp -R "${BUILD_DIR}/assets"     "${APP}/Contents/Resources/assets"
cp -R "${BUILD_DIR}/APPLaunch"  "${APP}/Contents/Resources/APPLaunch"

# Icon
if [ -f "${ICNS}" ]; then
    cp "${ICNS}" "${APP}/Contents/Resources/CardputerZero.icns"
fi

# Info.plist
cat > "${APP}/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>         <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>          <string>${BUNDLE_ID}</string>
    <key>CFBundleVersion</key>             <string>1.0</string>
    <key>CFBundleShortVersionString</key>  <string>1.0</string>
    <key>CFBundlePackageType</key>         <string>APPL</string>
    <key>CFBundleExecutable</key>          <string>cardputer-emu</string>
    <key>CFBundleIconFile</key>            <string>CardputerZero.icns</string>
    <key>LSMinimumSystemVersion</key>      <string>11.0</string>
    <key>NSHighResolutionCapable</key>     <true/>
</dict>
</plist>
PLIST

# ── sign inside-out: dylibs first, then the bundle ───────────────────────────
echo "==> codesign (identity: ${SIGN_ID})"
SIGN_ARGS=(--force --timestamp --options runtime --sign "${SIGN_ID}")
# Ad-hoc ("-") can't use --timestamp/--options runtime meaningfully; strip them.
if [ "${SIGN_ID}" = "-" ]; then
    SIGN_ARGS=(--force --sign -)
fi

for dylib in "${APP}/Contents/MacOS/apps"/*.dylib; do
    [ -e "${dylib}" ] || continue
    codesign "${SIGN_ARGS[@]}" "${dylib}"
done
codesign "${SIGN_ARGS[@]}" "${APP}/Contents/MacOS/cardputer-emu"
# Sign the bundle last (seals Resources + Info.plist).
codesign "${SIGN_ARGS[@]}" "${APP}"
codesign --verify --deep --verbose=2 "${APP}"

# Strip quarantine on what we just built (harmless locally; CI artifacts are clean).
xattr -cr "${APP}" || true

# ── DMG ──────────────────────────────────────────────────────────────────────
DMG="${OUT_DIR}/M5CardputerZero-Emulator-macOS-arm64.dmg"
echo "==> building ${DMG}"
STAGE="$(mktemp -d)"
cp -R "${APP}" "${STAGE}/"
ln -s /Applications "${STAGE}/Applications"   # drag-to-install affordance
rm -f "${DMG}"
hdiutil create -volname "${APP_NAME}" -srcfolder "${STAGE}" \
    -ov -format UDZO "${DMG}"
rm -rf "${STAGE}"

echo "==> done:"
echo "    ${APP}"
echo "    ${DMG}"
