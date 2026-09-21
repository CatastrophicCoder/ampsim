# AmpSim

A minimal guitar amp simulator plugin for macOS (AU / VST3 / Standalone), built with JUCE.
The amp tone comes from a pre-trained [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore)
`.nam` model; the plugin supplies the amp-style controls, the cab IR loader and the pedal chain around it.

See [`ampsim_plan.md`](ampsim_plan.md) for the goal, architecture and milestones.

**Status: milestone 2 (NAM model playing).** Loads a `.nam` model off the audio thread, runs it at
the rate it was trained at, and remembers it with the session. No cab or pedals yet.

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

The model runs at the sample rate it was trained at (usually 48 kHz). At any other session rate the
plugin converts in and out, and reports the resulting latency for the host to compensate — about
220 samples at 44.1 kHz.

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
