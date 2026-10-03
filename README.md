<div align="center">

# AmpSim

**A guitar amp simulator for macOS and Windows, by Catastrophic Audio**

[![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue.svg)](LICENSE)
![Platforms: macOS | Windows](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows-lightgrey.svg)
![Formats: AU | VST3 | Standalone](https://img.shields.io/badge/formats-AU%20%7C%20VST3%20%7C%20Standalone-orange.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)
![JUCE 9](https://img.shields.io/badge/JUCE-9.0.2-8DC63F.svg)
[![build](https://github.com/CatastrophicCoder/ampsim/actions/workflows/build.yml/badge.svg)](https://github.com/CatastrophicCoder/ampsim/actions/workflows/build.yml)

[Download](https://github.com/CatastrophicCoder/ampsim/releases) &middot;
[User guide](https://catastrophiccoder.github.io/ampsim/) &middot;
[Signal chain](#the-signal-chain) &middot;
[Building](#building) &middot;
[Architecture](CLAUDE.md)

![The AmpSim panel](docs/images/amp.png)

</div>

AmpSim is an open-source guitar amp plugin built with JUCE around a
[Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) capture. The amp itself is
a `.nam` file: a neural network trained on a real amplifier. What this project adds is everything a
capture on its own does not give you — an amp's controls, a cabinet, a pedalboard in the order a
real rig is plugged up, and a tuner.

It is deliberately small. There is one amp model at a time, one cabinet, and six pedal positions in
a fixed order — two of them slots that each hold one of four pedals, so twelve pedals across six
places — with no attempt at a channel switcher, a rack, or a library of tones. On top of that sit the things a practice rig
needs rather than a recording one: a tuner, a transpose and a metronome.

## The signal chain

```
transpose → compressor → drive → Gain → NAM model → Bass/Mid/Treble → Master
          → gate → chorus → delay → reverb → cabinet IR
```

The gate is the odd one out: it measures the guitar at the very front and closes on the other side
of the amp. A gate only in front cannot remove hiss the amp itself makes, and a gate only behind it
has no dynamics left to trigger on — which is why hardware gates for high-gain rigs have a key
input, and why this one works the same way.

**The amp's controls are marked 0 to 10**, the way an amplifier is: 0 and 10 printed at the ends
of each knob's travel, no read-out, and 5 flat for a tone band and unity for Gain and Master. Only the printing changes: underneath they are the same dB
parameters they always were, so presets and saved sessions carry over. The cabinet's controls and
the pedals' keep their own units.

A shelf along the bottom of the panel holds what you play *against* rather than what you play
through: a **transpose** of up to an octave either way, at the very front of the chain with the
tuner tapping in ahead of it so it still reads your strings, and a **metronome** that is added to
the output after everything — the power switch, the tuner's mute and the plugin's own bypass all
leave it running, and it is silent in an offline bounce. Twelve time signatures, from 2/4 through
cut time to 12/8; the tempo is the quarter note, as it is in every host, so the bar decides how
fast the clicks come.

Mono from end to end, because a guitar amp is and a NAM capture is; a stereo input is summed in at
the top. The placement is fixed rather than user-reorderable, because it is the point: a drive
pedal in front of the amp changes what the amp distorts, while modulation and echoes belong after
it so the repeats are of the already-distorted tone.

## Download and install

Ready-made packages are on the [releases page](https://github.com/CatastrophicCoder/ampsim/releases).

**macOS** (Apple silicon, macOS 11 or later): the `.pkg` installs the AU and VST3 into
`/Library/Audio/Plug-Ins/` and the standalone app into `/Applications`; the `.dmg` holds just the
app. In Logic the plugin appears as **Catastrophic Audio: AmpSim**, with the other amps and
distortion plugins.

**Windows** (Windows 10 version 2004 or later, or 11, on x64 with an AVX2 processor): the
`setup.exe` installs the VST3 and the standalone, for you or for everyone — it asks. In the
standalone, choose your interface's **ASIO** driver in *Options*: the *Windows Audio* modes go
through Windows' own mixer and are too slow to play through.

To build your own:

```sh
./packaging/package.sh          # macOS; or: cmake --build build --target package-macos
./packaging/package-windows.ps1 # Windows, from a Developer PowerShell, with Inno Setup 6
```

Builds Release, ad-hoc signs everything and writes an installer and a disk image to
`build-release/artefacts`. No Apple Developer Program membership is needed to build or package.

The cost lands on whoever installs it: nothing is signed with a developer certificate, so macOS
blocks the packages on first launch until the person goes to **System Settings → Privacy &
Security** and clicks **Open Anyway**, and Windows SmartScreen stops the installer until they
choose **More info → Run anyway**. [`packaging/README.md`](packaging/README.md) covers both, what
ad-hoc signing does and does not do, and what signing would change.

### Cutting a release

CI builds, tests, validates and packages every push, on macOS and Windows. A `v*` tag does the same
and then, once both have passed, publishes a GitHub Release with the macOS installer and disk
image and the Windows installer attached:

```sh
# bump project(AmpSim VERSION X.Y.Z) in CMakeLists.txt first, and commit and push it
git tag -a vX.Y.Z -m "AmpSim X.Y.Z"
git push origin vX.Y.Z
```

The tag must match the version in `CMakeLists.txt`; if it does not, the run fails rather than
publishing something misnamed. Push the version commit before the tag, or the tagged run checks out
a tree that still carries the old number and the check rejects it.

## Using it

The **[user guide](https://catastrophiccoder.github.io/ampsim/)** is the place to start: an
annotated tour of the amp, the pedalboard and the cabinet, and walkthroughs for getting a first
sound, dialling in a tone, loading your own capture and impulse responses, using the pedals in the
order they are in, tuning up, and putting a control on a MIDI pedal in the standalone.
[`CHANGELOG.md`](CHANGELOG.md) records what changed between published versions.

Two things worth knowing before you open it:

**Captures are level-matched.** A `.nam` file records how loud it is, and two captures of the same
amp can be 15 dB apart; AmpSim brings each to a common reference on load, so swapping one for
another does not mean re-setting Master by ear. A capture that does not carry a loudness is left
alone.

**An amp and a cab are built in**, so a fresh instance makes a sound rather than passing audio
through untouched: `MARS2204`, a capture of a well-known British 100-watt head, into `V30 SM57`, a
4x12 close-miked on axis. They are written out to
`~/Library/Application Support/AmpSim/Bundled/` on first run and loaded from there. They travel
inside the binary packed rather than as a plain `.nam` and `.wav` — see
[`src/AssetPack.h`](src/AssetPack.h) for the format and what it is and is not for. Load anything
else over them at any time; the public NAM model libraries and any cabinet IR work.

**In the standalone, untick "Mute audio input"** in Options the first time. JUCE mutes a
standalone's input by default, which is right for a synth and wrong for an amp — with it ticked the
meters move and nothing is heard.

![The pedal board](docs/images/pedals.png)

## Building

Full Xcode is not needed — the Command Line Tools build and sign all three formats, and `auval`
ships with macOS.

| | Version used here | |
| --- | --- | --- |
| Xcode Command Line Tools | Apple clang 17 | `xcode-select --install` |
| CMake | 4.4.3 (JUCE needs ≥ 3.24) | `brew install cmake` |
| Ninja | 1.13.2 | `brew install ninja` |
| pluginval | 1.0.4 (optional) | `brew install --cask pluginval` |

JUCE 9.0.2, NeuralAmpModelerCore v0.5.4 (which brings Eigen and nlohmann/json) and Catch2 v3.9.1
are pinned submodules.

```sh
git clone --recursive https://github.com/CatastrophicCoder/ampsim.git
cd ampsim
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Without `--recursive`, or after pulling a change that moves a submodule,
`git submodule update --init --recursive` does the same job — NAM Core has submodules of its own,
so the recursion matters.

A **Release** build installs the AU and VST3 into `~/Library/Audio/Plug-Ins/`; a Debug build does
not, and leaves its plugins in `build/AmpSim_artefacts/Debug/`. That is deliberate: a Debug plugin
is roughly sixty times slower, and when both installed to the same place a Debug build quietly
replaced the one a DAW was loading. Pass `-DAMPSIM_COPY_PLUGIN=ON` to install a Debug build anyway.
Use Release for anything you intend to judge by ear or by CPU load.

### On Windows

The Windows build is the VST3 and the standalone, x64 only. It needs Visual Studio 2022 or later,
or just its Build Tools, with the *Desktop development with C++* workload, which brings MSVC,
CMake and Ninja. Inno Setup 6 builds the installer. Work in a **Developer PowerShell for Visual
Studio**, then:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAMPSIM_COPY_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure
./packaging/package-windows.ps1     # the installer, in build-release/artefacts
```

`AMPSIM_COPY_PLUGIN=OFF` because JUCE's default destination is under Program Files, which needs
an administrator. The build requires AVX2 and links the C++ runtime statically;
[`docs/windows.md`](docs/windows.md) explains both, and everything else about the port.

JUCE is pinned on purpose: Apple toolchain and JUCE updates are a reliable source of "the build
broke and nothing changed". Update it deliberately, in its own commit, and re-validate afterwards.

## Testing

```sh
ctest --test-dir build                        # 115 tests
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "bypass"            # one test, or a pattern
```

The tests link the plugin's own shared-code target, so they exercise the same objects the AU, VST3
and standalone builds ship. They measure DSP behaviour — band responses, resampler latency against
a real impulse, whether switching a pedal introduces a discontinuity the pedal does not already
make — which is the layer plugin validators never look at. `-DAMPSIM_BUILD_TESTS=OFF` skips them.

```sh
auval -v aufx Amp1 Ctcd
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 \
    --validate build/AmpSim_artefacts/Debug/VST3/AmpSim.vst3
```

Both pass. The AU is type `aufx`, an ordinary effect, so it inserts on an audio track like any
other amp sim. It was `aumf` — a music effect — because it declared a MIDI input for controller
mapping, and in Logic that puts a plugin in an instrument track's instrument slot where its audio
comes from a *side chain*: inserted the obvious way it was handed silence. **MIDI learn therefore
works in the standalone only.** In Logic, use its own Controller Assignments (Cmd-L), which map any
plugin's parameters.

CI runs all of this on every push: a Release build, the test suite, both validators, and the
packaging script, with the installer and disk image uploaded as artefacts. See
[`.github/workflows/build.yml`](.github/workflows/build.yml).

## Roadmap

[`docs/roadmap.md`](docs/roadmap.md) holds what is agreed but not built, and records what the panel
rework decided so those choices are not re-argued.

## Reading the code

[`CLAUDE.md`](CLAUDE.md) is the architecture note: what each block is, which thread may touch it,
and the handful of JUCE behaviours that cost time to discover — a default-constructed `dsp::Gain`
sitting at zero, `Convolution` installing an engine before any IR is loaded, `IIR::Coefficients`
factories allocating on every call. It is written for whoever works on this next, including an AI
assistant.

## What it does not do

- **macOS on Apple silicon, and Windows on x64 with AVX2.** No Linux build, no Intel Mac build,
  and no native Windows-on-ARM build: there the x64 build loads only in an x64 host.
- **Mono.** Stereo input is summed at the top of the chain.
- **One model, one cabinet, no channel switching.**
- **The mic-position blend is unproven musically.** The interpolation is exact and tested, but
  whether it sounds like moving a microphone depends entirely on having a grid of IRs of one cab
  captured at known positions. None ship here.
- **MIDI learn works in the standalone only**, and the plug-ins do not offer it. Declaring a MIDI
  input made the Audio Unit a MIDI-controlled effect, which Logic feeds from a side chain; the same
  setting carries the VST3's MIDI input. In Logic, Controller Assignments (Cmd-L) map any plugin's
  parameters instead.
- **Nothing is notarised or code-signed**, so anyone you give a build to has to allow it through
  Gatekeeper or SmartScreen.

## Licence

[GNU AGPL v3](LICENSE). AmpSim links JUCE, which since version 8 is offered under AGPLv3 or a paid
licence; this project takes the open-source option, so the same terms apply to it. In practice that
means anyone distributing a binary built from this source has to make the corresponding source
available. Read JUCE's current terms at [juce.com](https://juce.com) before relying on any of this —
they have changed between major versions.

Third-party code, all as pinned submodules rather than vendored copies:

| | Licence |
| --- | --- |
| [JUCE](https://github.com/juce-framework/JUCE) | AGPLv3 or commercial |
| [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) | MIT |
| [Eigen](https://gitlab.com/libeigen/eigen) | MPL2 |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 |
| VST3 SDK (bundled with JUCE) | GPLv3 or Steinberg's proprietary terms |
| ASIO SDK headers (bundled with JUCE; Windows standalone only) | GPLv3 or Steinberg's proprietary terms |

The panel's two typefaces are embedded in the binary: [Figtree](https://github.com/erikdkennedy/figtree)
and [Jost](https://github.com/indestructible-type/Jost), both under the
[SIL Open Font License 1.1](https://openfontlicense.org). The licence texts are in `resources/fonts`.

Amp captures and impulse responses carry their own licences, and some are captures of trademarked
amplifiers — a personal build can use them; publishing a plugin with an amp's name on it cannot.
