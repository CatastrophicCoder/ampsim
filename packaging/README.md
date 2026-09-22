# Installing AmpSim

AmpSim is built and packaged without an Apple Developer Program membership. Nothing about the
build needs one: `pkgbuild`, `productbuild`, `hdiutil` and `codesign` all ship with the Command
Line Tools, and the packages they produce work. The cost falls at the other end — on the machine
that installs it.

## What you will see the first time

macOS refuses to open an app or installer from an unidentified developer, with

> "AmpSim" cannot be opened because the developer cannot be verified.

and only **Move to Trash** and **Cancel**. There is no "Open anyway" in that dialog, and since
macOS 15 Sequoia, control-clicking the file and choosing Open no longer gets round it.

## Letting it through

**In System Settings.** Open **System Settings → Privacy & Security**, scroll to Security, and
click **Open Anyway** beside the message about AmpSim. Authenticate, then open the file again.

**Or in Terminal**, by removing the quarantine flag the download attached to it:

```sh
xattr -d com.apple.quarantine ~/Downloads/AmpSim-0.1.0.pkg
```

Neither of these verifies anything about who built the software. They record that *you* have
decided to trust it. Do it only for something you got from somewhere you trust.

## What ad-hoc signing does and does not do

`packaging/package.sh` signs every bundle with `codesign --sign -`. An ad-hoc signature carries no
identity — it is a hash of the code and nothing more. It matters because Apple silicon will not
execute unsigned native code at all, so without it the plugin would fail to load in a host rather
than merely warn. It does not satisfy Gatekeeper, and it is not notarisation.

Each bundle is signed on its own rather than with `--deep`, which Apple deprecated for signing
because it re-signs nested code that should be signed on its own terms.

## A note on bundle relocation

All three formats carry the same bundle identifier, because `juce_add_plugin` takes one
`BUNDLE_ID` for the lot. `pkgbuild` marks bundles relocatable by default, which tells the installer
to look for an existing bundle with that identifier and install over *that* instead of where the
package says. With the AU already installed, the standalone app was treated as an upgrade of it and
never reached `/Applications` — the install reported success and left a receipt, with no app.

`package.sh` therefore generates a component plist per package with `BundleIsRelocatable` set to
false. Anything added to the installer later needs the same treatment.

## Building the packages

```sh
./packaging/package.sh
```

It builds Release, ad-hoc signs the three formats, and writes to `build-release/artefacts`:

- `AmpSim-<version>.pkg` — an installer offering the AU, the VST3 and the standalone app
  separately. Installs to `/Library/Audio/Plug-Ins/` and `/Applications`, so it asks for an
  administrator password.
- `AmpSim-<version>.dmg` — the standalone app alone, with a link to Applications to drag it into.

To install without an administrator password, into your own library rather than the system one:

```sh
installer -pkg build-release/artefacts/AmpSim-0.1.0.pkg -target CurrentUserHomeDirectory
```

That puts the plugins in `~/Library/Audio/Plug-Ins/` and the app in `~/Applications`, which every
host looks in.

## If you do get a Developer ID later

The membership buys two things this does not have: a Developer ID certificate to sign with instead
of `-`, and notarisation, which is Apple scanning the upload and issuing a ticket that gets stapled
to it. With both, the first-launch dialog disappears. The changes would be to `package.sh`: the
`codesign --sign -` calls take the certificate name and `--options runtime`, `productbuild` gains
`--sign "Developer ID Installer: ..."`, and `xcrun notarytool submit --wait` plus
`xcrun stapler staple` run at the end.
