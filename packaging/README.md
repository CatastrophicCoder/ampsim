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
xattr -d com.apple.quarantine ~/Downloads/AmpSim-*.pkg
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
installer -pkg build-release/artefacts/AmpSim-*.pkg -target CurrentUserHomeDirectory
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

# On Windows

`AmpSim-<version>-windows-x64-setup.exe` installs the VST3 plug-in and the standalone application.
Either can be left out on the *Select Components* page.

## What it needs

- **Windows 10 version 2004 (May 2020) or later, or Windows 11, on x64.** On Windows on ARM it
  installs the x64 build, which loads only in an x64 host.
- **A processor with AVX2**: an Intel Core from 2013 (Haswell) on, or any AMD Ryzen. The plugin is
  built for it and would crash its host on a processor without it, so the installer checks first
  and refuses with a message rather than installing something that cannot run.

## For everyone, or just for you

The installer asks first.

| | VST3 goes to | The standalone goes to | Needs |
| --- | --- | --- | --- |
| Install for all users | `C:\Program Files\Common Files\VST3` | `C:\Program Files\AmpSim` | an administrator's approval |
| Install for me only | `%LOCALAPPDATA%\Programs\Common\VST3` | `%LOCALAPPDATA%\Programs\AmpSim` | nothing |

Both are folders the VST3 specification tells hosts to scan. If a host does not find the plug-in
after an install for one user, it is one that only looks in the system folder: add the per-user
folder to its plug-in paths, or reinstall for all users.

Presets, and the built-in amp model and cabinet the plug-in writes out on first run, live in
`%APPDATA%\AmpSim`. Uninstalling leaves them there, as it does on macOS.

## What you will see the first time

The installer is not signed, so Microsoft Defender SmartScreen stops it with *Windows protected
your PC*. Choose **More info**, then **Run anyway**. As with Gatekeeper's *Open Anyway*, that
records that *you* have decided to trust it; it verifies nothing about who built it. Some managed
machines block unsigned installers outright, and then there is no way past it but an administrator.

## No sound in the standalone

Two Windows settings produce silence while the input device looks fine:

- **Settings → Privacy & security → Microphone → Let desktop apps access your microphone** must be
  on. Unlike macOS, Windows never asks: the device appears and nothing arrives.
- The standalone mutes its input by default, the same as on macOS. Untick it in the *Options*
  dialog.

## Playing through it: use ASIO

The *Windows Audio* device types go through Windows' own mixer. On the interface this was
tested with, an Audient iD4 mkII, that fixed the buffer at 441 samples and the rate at 44.1 kHz,
and felt laggy, because it is. **ASIO** talks to the interface's own driver:

1. Install the interface maker's Windows driver. Windows' built-in one runs the interface but
   provides no ASIO.
2. In the standalone's **Options**, set the device type to **ASIO** and choose that driver; in a
   host, set its audio device the same way (REAPER: **Preferences → Audio → Device**).
3. Choose **48 kHz**, which is the amp model's own rate, and the smallest buffer that plays
   cleanly. On the test laptop that was 128 samples, about 10 ms round trip, with AmpSim at about
   4 % of the CPU.

An ASIO driver serves one program at a time: close the standalone before opening a host, or the
host reports the device closed.

## Building the installer

From a Developer PowerShell for Visual Studio, with Inno Setup 6 installed:

```powershell
./packaging/package-windows.ps1
```

It builds Release into `build-release`, checks that neither binary imports the DLL C++ runtime,
stages them, and runs Inno Setup's compiler on `packaging/windows/AmpSim.iss`. The installer lands
in `build-release/artefacts`. CI does the same on every push, then installs it both ways, installs
it again over itself, and uninstalls it.

## If it is signed later

Signing removes the SmartScreen warning once a certificate has built up a reputation, or at once
for an EV certificate or Microsoft's Trusted Signing. The `.vst3` and the `.exe` are signed with
`signtool` in `package-windows.ps1` before staging, and a `SignTool=` line in `AmpSim.iss`
signs the installer and the uninstaller that Inno Setup writes.
