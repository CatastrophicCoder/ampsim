# AmpSim

A minimal guitar amp simulator plugin for macOS (AU / VST3 / Standalone), built with JUCE.
The amp tone comes from a pre-trained [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore)
`.nam` model; the plugin supplies the amp-style controls, the cab IR loader and the pedal chain around it.

See [`ampsim_plan.md`](ampsim_plan.md) for the goal, architecture and milestones.

**Status: milestone 6 (pedalboard).** The whole chain from the Goal is built: pedals into the amp,
a NAM model, tone stack, master, pedals after the amp, cabinet IR.

## Requirements

| Tool | Version used | Install |
| --- | --- | --- |
| Xcode Command Line Tools | Apple clang 17 | `xcode-select --install` |
| CMake | 4.4.3 (JUCE needs ≥ 3.22) | `brew install cmake` |
| Ninja | 1.13.2 | `brew install ninja` |
| JUCE | 9.0.2, pinned submodule | `git submodule update --init --recursive` |
| NeuralAmpModelerCore | v0.5.4, pinned submodule (brings Eigen and nlohmann/json) | as above |
| Catch2 | v3.9.1, pinned submodule | as above |
| pluginval | 1.0.4 | `brew install --cask pluginval` |

Full Xcode is *not* required: the Command Line Tools are enough to build and validate all three
formats. `auval` ships with macOS.

## Build

```sh
git submodule update --init --recursive           # first checkout only; NAM has its own submodules
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build                               # all three formats
cmake --build build --target AmpSim_Standalone    # or one at a time
```

`COPY_PLUGIN_AFTER_BUILD` is on, so a build installs into `~/Library/Audio/Plug-Ins/Components`
(AU) and `~/Library/Audio/Plug-Ins/VST3`. The standalone app is at
`build/AmpSim_artefacts/Debug/Standalone/AmpSim.app`.

Use `-DCMAKE_BUILD_TYPE=Release` for anything you intend to listen to critically — the Debug build
is much slower and will not represent real CPU load once NAM is in the chain.

## Using it

Click **Load model...** and pick a `.nam` file. NAM Core's own example models are in
`external/NeuralAmpModelerCore/example_models/`, and the public model libraries linked from the
[Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) project work too.

The panel is a pale enamelled plate with graphite knobs: the blue arc around each control is its
value, read against the scale behind it, and red means something is switched out of the signal.
Below the plate, two nameplates show the loaded model and cab, and turn red with the reason if a
file cannot be read.

The front panel is **Gain — Bass — Mid — Treble — Master**. Gain sits before the model, so turning
it up drives the network harder and it saturates, the way a preamp gain control does; Bass, Mid and
Treble are three independent parametric bands (low shelf at 100 Hz, peak at 800 Hz, high shelf at
3.2 kHz, ±12 dB each) between the model and the cab; Master is the level out of the amp.

Because the bands are parametric rather than a modelled passive network, all three centred is
genuinely flat, and each control moves only its own band. A real amp's tone stack interacts with
itself and is mid-scooped at noon — that difference is deliberate, and recorded in the plan.

Below the amp are six pedals in two rows, and the rows are the point: **into the amp** (gate,
compressor, drive) and **after the amp, before the cab** (chorus, delay, reverb). The placement is
fixed, because it is what a working pedalboard does — a drive pedal in front changes what the amp
distorts, while modulation and echoes belong after it so the repeats are of the distorted tone. A
blue lamp means the pedal is in your signal; the red lamps on the amp mean something is switched
out of it.

The chorus, delay and reverb keep running while switched off, so engaging one picks up repeats
already in flight rather than starting from an empty line.

Click **Load cab IR...** and pick a `.wav` or `.aiff` impulse response for the cabinet. It is
convolved at the end of the chain, with no added latency, and **Cab bypass** switches it out with a
crossfade. A stereo IR is folded to mono, and the IR's own level is kept rather than normalised, so
swapping IRs changes tone rather than volume.

The model runs at the sample rate it was trained at (usually 48 kHz). At any other session rate the
plugin converts in and out, and reports the resulting latency for the host to compensate — about
220 samples at 44.1 kHz. The drive pedal's 4x oversampler adds a further 5 samples, and runs whether
or not the pedal is engaged so that this number never changes under the host.

## Test

```sh
cmake --build build --target AmpSimTests
ctest --test-dir build                        # all tests
ctest --test-dir build --output-on-failure    # with output from failures
ctest --test-dir build -R "bypass"            # one test, or a pattern
ctest --test-dir build -N                     # list without running
```

The tests link the plugin's shared-code target, so they exercise the same objects the AU, VST3 and
Standalone builds do. They check DSP behaviour offline — gain values, ramp continuity, state
round-trips — which is the layer `auval` and `pluginval` do not look at. Pass `-DAMPSIM_BUILD_TESTS=OFF`
to skip building them.

## Validate

```sh
auval -v aufx Amp1 Amps                                                   # AU
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 \
    --validate build/AmpSim_artefacts/Debug/VST3/AmpSim.vst3              # VST3
```

Both pass. Run them on every build; they catch threading and state bugs that a DAW hides — but not
wrong DSP, which is what `ctest` is for.

## CLion

Open the project directory — CLion picks up `CMakeLists.txt` directly. In
*Settings → Build, Execution, Deployment → CMake*, set the generator to Ninja and the toolchain to
the system clang. The `AmpSim_Standalone` target is the convenient one to run from the IDE.

## JUCE version policy

JUCE is pinned to the 9.0.2 tag as a submodule under `external/JUCE`, deliberately: Apple toolchain
and JUCE updates are a known source of "the build broke and nothing changed". Update it on purpose,
in its own commit, and re-run `auval` and `pluginval` afterwards.

## Licensing

JUCE 8 and later are dual-licensed: AGPLv3, or a free Personal tier below a revenue limit. Which one
this project uses is **not yet decided** (it is an open question in the plan), and it determines
whether this repository can be made public.

JUCE 9 has no splash screen, so `JUCE_DISPLAY_SPLASH_SCREEN` is obsolete and the build warns if it
is set. Nothing in the build needs to be changed to satisfy either licence; the choice is about
what you may do with a distributed binary.
