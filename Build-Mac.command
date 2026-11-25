#!/bin/bash
# The Sauce Suite - one-click Mac build.
# Double-click in Finder (first time: right-click > Open), or run:  bash Build-Mac.command
# Builds a universal (Apple Silicon + Intel) VST3 + Audio Unit, runs the verification tests on this Mac,
# validates the Audio Unit with Apple's auval, and makes a branded installer package (.pkg).
#   --ci   non-interactive (used by the GitHub workflow): no prompts, no auto-open
set -euo pipefail
cd "$(dirname "$0")"
CI=0; [ "${1:-}" = "--ci" ] && CI=1
VERSION=$(grep -m1 -oE 'project \(TheSauceSuite VERSION [0-9.]+' CMakeLists.txt | awk '{print $4}')
NEON=$'\033[38;5;39m'; BOLD=$'\033[1m'; OFF=$'\033[0m'
step() { echo; echo "${NEON}${BOLD}==> $*${OFF}"; }
pause() { [ "$CI" = 1 ] || read -r -p "Press Return to close this window..." _; }
fail() { echo; echo "The build stopped: $*"; echo "Take a screenshot of this window and send it to Claude."; pause; exit 1; }
trap 'fail "an unexpected error (see the messages above)"' ERR

echo "${NEON}${BOLD}  The Sauce Suite ${VERSION} - Mac build${OFF}"

# ---- 1. Apple command line tools (compiler, codesign, pkgbuild) ----
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Apple's free Command Line Tools are needed. A window will pop up: click Install."
    echo "When it finishes, double-click Build-Mac.command again."
    xcode-select --install || true
    pause; exit 1
fi

# ---- 2. CMake (downloads a private copy if it isn't installed) ----
if command -v cmake >/dev/null 2>&1; then CMAKE=cmake
else
    TOOLS="$PWD/.tools"; CMAKE="$TOOLS/CMake.app/Contents/bin/cmake"
    if [ ! -x "$CMAKE" ]; then
        step "Downloading CMake (one time, ~50 MB)"
        mkdir -p "$TOOLS"
        curl -L --fail --progress-bar -o "$TOOLS/cmake.tar.gz" \
            https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz
        tar -xzf "$TOOLS/cmake.tar.gz" -C "$TOOLS"
        mv "$TOOLS/cmake-3.31.6-macos-universal/CMake.app" "$TOOLS/"
        rm -rf "$TOOLS/cmake.tar.gz" "$TOOLS/cmake-3.31.6-macos-universal"
    fi
fi

# ---- 3. build (downloads JUCE 8.0.15 the first time) ----
step "Configuring (first run downloads JUCE)"
"$CMAKE" -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
step "Compiling for Apple Silicon + Intel (5-15 minutes the first time)"
"$CMAKE" --build build-mac --config Release --parallel "$(sysctl -n hw.ncpu)"
ART="build-mac/TheSauceSuite_artefacts/Release"
VST="$ART/VST3/The Sauce Suite.vst3"; AU="$ART/AU/The Sauce Suite.component"
[ -d "$VST" ] && [ -d "$AU" ] || fail "the plugin bundles weren't produced"

# ---- 4. sign (ad-hoc: required for Apple Silicon; no paid Apple account needed) ----
step "Signing the plugins (ad-hoc)"
codesign --force --deep --sign - "$VST"
codesign --force --deep --sign - "$AU"
lipo -info "$VST/Contents/MacOS/The Sauce Suite" || true

# ---- 5. verification tests on this Mac ----
step "Running the verification tests"
set +e
build-mac/SauceTests_artefacts/Release/SauceTests > build-mac/test-results.txt 2>&1; TESTS=$?
build-mac/SauceHostCheck_artefacts/Release/SauceHostCheck "$PWD/$VST" > build-mac/host-check.txt 2>&1; HOST=$?
set -e
grep -E "checks passed|FAIL" build-mac/test-results.txt || true
tail -1 build-mac/host-check.txt || true

# ---- 6. installer package ----
step "Building the installer package"
PKGWORK=build-mac/pkg; rm -rf "$PKGWORK"; mkdir -p "$PKGWORK/au" "$PKGWORK/vst3" "$PKGWORK/out" dist
cp -R "$AU" "$PKGWORK/au/"; cp -R "$VST" "$PKGWORK/vst3/"
for k in au vst3; do
    pkgbuild --analyze --root "$PKGWORK/$k" "$PKGWORK/$k.plist" >/dev/null
    plutil -replace BundleIsRelocatable -bool NO "$PKGWORK/$k.plist"   # always install to /Library/Audio/Plug-Ins
done
pkgbuild --root "$PKGWORK/au"   --component-plist "$PKGWORK/au.plist"   --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier com.drellmadeit.thesaucesuite.au   --version "$VERSION" "$PKGWORK/out/au.pkg"
pkgbuild --root "$PKGWORK/vst3" --component-plist "$PKGWORK/vst3.plist" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier com.drellmadeit.thesaucesuite.vst3 --version "$VERSION" "$PKGWORK/out/vst3.pkg"
cp -R packaging/mac/resources "$PKGWORK/resources"
sed "s/__VERSION__/$VERSION/g" packaging/mac/resources/distribution.xml > "$PKGWORK/distribution.xml"
sed "s/__VERSION__/$VERSION/g" packaging/mac/resources/welcome.html > "$PKGWORK/resources/welcome.html"
PKG="dist/TheSauceSuite-$VERSION-macOS.pkg"
productbuild --distribution "$PKGWORK/distribution.xml" --resources "$PKGWORK/resources" \
             --package-path "$PKGWORK/out" "$PKG"
echo "Installer: $PWD/$PKG"

# ---- 7. Audio Unit validation (installs a copy for this user so auval can find it) ----
step "Validating the Audio Unit with Apple's auval"
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/The Sauce Suite.component"
cp -R "$AU" "$HOME/Library/Audio/Plug-Ins/Components/"
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
set +e; auval -v aufx Sauc Drel > build-mac/auval.txt 2>&1; AUVAL=$?; set -e
tail -3 build-mac/auval.txt
# remove the temporary user copy so it doesn't shadow the one the installer puts in /Library
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/The Sauce Suite.component"

echo
[ "$TESTS" = 0 ] && echo "Tests: passed" || echo "WARNING: some tests failed (build-mac/test-results.txt)"
[ "$HOST" = 0 ]  && echo "Host check: passed" || echo "WARNING: host check failed (build-mac/host-check.txt)"
[ "$AUVAL" = 0 ] && echo "auval: AU VALIDATION SUCCEEDED" || echo "WARNING: auval reported problems (build-mac/auval.txt)"
trap - ERR
if [ "$CI" = 1 ]; then
    [ "$TESTS" = 0 ] && [ "$HOST" = 0 ] && [ "$AUVAL" = 0 ] || exit 1
    exit 0
fi
step "Done. Opening the installer: click Continue through it, then rescan plugins in your DAW."
open "$PKG"
pause
