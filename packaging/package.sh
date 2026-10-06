#!/bin/bash
#
# Builds AmpSim in Release and packages it for installation on another Mac.
#
# No Apple Developer Program membership is involved. The binaries are ad-hoc signed, which is
# what Apple silicon requires to run native code at all but says nothing about who made it, so
# whoever installs this has to let it through Gatekeeper once by hand. packaging/README.md and
# the installer's own welcome pane say how.
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# AMPSIM_ARCH picks the Mac it is for: arm64 (Apple silicon) or x86_64 (Intel). It defaults to
# the machine running the script. The two are separate packages rather than one universal binary:
# the Intel build is compiled for AVX2 (CMakeLists.txt), which needs per-architecture flags a
# universal build would have to carry slice by slice, and each half is built and tested on its own
# kind of Mac in CI.
arch="${AMPSIM_ARCH:-$(uname -m)}"
case "$arch" in
    arm64)  platform="macos-apple-silicon"; mac="Apple silicon" ;;
    x86_64) platform="macos-intel";         mac="Intel Macs" ;;
    *) echo "AMPSIM_ARCH must be arm64 or x86_64, not $arch" >&2; exit 1 ;;
esac

# AMPSIM_BUILD_DIR lets a caller that has already built Release — CI, say — point at that tree
# instead of paying for a second full build. Building for the other kind of Mac gets a tree of its
# own, so the two never share a cache that was configured for the wrong one.
if [ "$arch" = "$(uname -m)" ]; then
    default_build="$root/build-release"
else
    default_build="$root/build-release-$arch"
fi
build="${AMPSIM_BUILD_DIR:-$default_build}"
staging="$build/package"
out="$build/artefacts"

version="$(sed -n 's/^project(AmpSim VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
: "${version:?could not read the version out of CMakeLists.txt}"

identifier="com.catastrophicaudio.ampsim"

echo "==> AmpSim $version for $platform ($arch)"

# --- build -------------------------------------------------------------------------------------
# A cross build is not copied into ~/Library/Audio/Plug-Ins: an Intel AU there would replace the
# Apple silicon one this Mac's hosts load, and fail to load in them.
copy_args=()
if [ "$arch" != "$(uname -m)" ]; then
    copy_args=(-DAMPSIM_COPY_PLUGIN=OFF)
fi

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAMPSIM_BUILD_TESTS=OFF \
      -DCMAKE_OSX_ARCHITECTURES="$arch" ${copy_args[@]+"${copy_args[@]}"} > /dev/null
cmake --build "$build" --target AmpSim_AU AmpSim_VST3 AmpSim_Standalone > /dev/null

artefacts="$build/AmpSim_artefacts/Release"

# A build tree reused from elsewhere could have been configured for the other Mac. Refuse to label
# a binary as something it is not.
for binary in "$artefacts/AU/AmpSim.component/Contents/MacOS/AmpSim" \
              "$artefacts/VST3/AmpSim.vst3/Contents/MacOS/AmpSim" \
              "$artefacts/Standalone/AmpSim.app/Contents/MacOS/AmpSim"; do
    built="$(lipo -archs "$binary")"
    if [ "$built" != "$arch" ]; then
        echo "$binary is $built, not $arch" >&2
        exit 1
    fi
done

# --- sign --------------------------------------------------------------------------------------
# Ad-hoc ("-"), and each bundle on its own rather than with --deep, which Apple deprecated for
# signing: it silently re-signs nested code that should be signed on its own terms.
for bundle in "$artefacts/AU/AmpSim.component" \
              "$artefacts/VST3/AmpSim.vst3" \
              "$artefacts/Standalone/AmpSim.app"; do
    echo "==> signing $(basename "$bundle")"
    codesign --force --sign - --timestamp=none "$bundle"
    codesign --verify --strict "$bundle"
done

# --- stage -------------------------------------------------------------------------------------
rm -rf "$staging" "$out"
mkdir -p "$staging/au/Library/Audio/Plug-Ins/Components" \
         "$staging/vst3/Library/Audio/Plug-Ins/VST3" \
         "$staging/app/Applications" \
         "$staging/pkgs" \
         "$out"

cp -R "$artefacts/AU/AmpSim.component"     "$staging/au/Library/Audio/Plug-Ins/Components/"
cp -R "$artefacts/VST3/AmpSim.vst3"        "$staging/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$artefacts/Standalone/AmpSim.app"   "$staging/app/Applications/"

# --- component packages ------------------------------------------------------------------------
# One per format, so the installer can offer them separately: plenty of people want the plugin
# without the standalone, or have no use for VST3.
#
# Each needs relocation turned off. pkgbuild marks bundles relocatable by default, which means the
# installer looks for an existing bundle with the same identifier and installs over that instead of
# where the package says. All three formats share JUCE's single BUNDLE_ID, so with relocation on
# the standalone app was treated as an upgrade of the already-installed AU component and never
# reached /Applications at all — a receipt, and no app.
build_component() {
    local root="$1" id="$2" pkg="$3"
    local plist="$staging/$(basename "$pkg" .pkg)-component.plist"

    pkgbuild --analyze --root "$root" "$plist" > /dev/null

    local entries
    entries="$(/usr/libexec/PlistBuddy -c "Print" "$plist" | grep -c "BundleIsRelocatable" || true)"

    for ((i = 0; i < entries; ++i)); do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist"
    done

    pkgbuild --root "$root" --identifier "$id" --version "$version" \
             --component-plist "$plist" --install-location / "$pkg" > /dev/null
}

build_component "$staging/au"   "$identifier.au"         "$staging/pkgs/au.pkg"
build_component "$staging/vst3" "$identifier.vst3"       "$staging/pkgs/vst3.pkg"
build_component "$staging/app"  "$identifier.standalone" "$staging/pkgs/app.pkg"

# --- installer ---------------------------------------------------------------------------------
sed -e "s/@VERSION@/$version/g" -e "s/@ARCH@/$arch/g" -e "s/@MAC@/$mac/g" \
    "$root/packaging/distribution.xml" > "$staging/distribution.xml"

productbuild --distribution "$staging/distribution.xml" \
             --package-path "$staging/pkgs" \
             --resources "$root/packaging/resources" \
             "$out/AmpSim-$version-$platform.pkg" > /dev/null

# --- disk image for the standalone --------------------------------------------------------------
dmg_staging="$staging/dmg"
mkdir -p "$dmg_staging"
cp -R "$artefacts/Standalone/AmpSim.app" "$dmg_staging/"
cp "$root/packaging/README.md" "$dmg_staging/Read me first.txt"
ln -s /Applications "$dmg_staging/Applications"

hdiutil create -volname "AmpSim $version ($mac)" -srcfolder "$dmg_staging" -ov -quiet -format UDZO \
        "$out/AmpSim-$version-$platform.dmg"

echo
echo "==> built:"
ls -lh "$out" | awk 'NR > 1 { print "    " $9 "  " $5 }'
echo
echo "    Ad-hoc signed, so the first launch on another Mac needs"
echo "    System Settings -> Privacy & Security -> Open Anyway."
echo "    packaging/README.md has the details."
