# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

**Milestone 1 is done.** `AmpSimAudioProcessor` has an APVTS with `inputGain`, `outputGain` and
`bypass`, two `juce::dsp::Gain` stages, a ramped dry/wet bypass crossfade, working state
save/reload, and a two-knob editor. The signal chain between the gains is still empty — milestone 2
(NAM model playing) is next.

Conventions established in milestone 1, worth following for every block added after it:

- Parameter IDs live in `namespace ParamID` in `PluginProcessor.h`. Never change an existing ID or
  its version hint; a saved session looks parameters up by ID.
- Parameter pointers are cached in the constructor, not looked up by string per block.
- **A freshly constructed `juce::dsp::Gain` sits at 0, not 1.** Call `reset()` after setting a
  smoother's target in `prepareToPlay`, or the block fades in on every playback start. Neither
  auval nor pluginval catches this; `tests/GainTests.cpp` does.
- `setStateInformation` touches only the APVTS — it runs on the message thread, and reaching into
  DSP objects from there races with `processBlock`.

`ampsim_plan.md` is the source of truth for scope, architecture and sequencing; `NOTES.md` is the
running session log, and gets an entry per working session. Read the plan before implementation
work: it records decisions already settled (the amp is NAM Core, not circuit modelling; pedal
placement mirrors hardware; post-amp pedals run before the cab) and open questions that are *not*
settled. Do not re-litigate the settled ones, and do not silently answer the open ones — ask.

## What the project is

A **minimal** macOS guitar amp simulator plugin (JUCE, C++17), built as AU / VST3 / Standalone. Four parts, per the Goal section of `ampsim_plan.md`:

1. **Amp** — a pre-trained `.nam` model run through [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT). No hand-written amp DSP; the model supplies the tone.
2. **Custom amp-like UI** — a front panel with Gain, three-band EQ (Bass, Mid, Treble) and Master Volume. This is the project's own contribution and the reason it is not just a NAM loader.
3. **Cabinet** — a single `.wav` IR loaded into a convolution engine, with bypass. Nothing more.
4. **Pedal section** — standard effect modules with per-pedal bypass, placed like real hardware: dirt and dynamics in front of the amp, modulation and time after it (see below).

ML Sound Lab's Amped Block Letter is the yardstick the plan measures against, not a feature target. Anything outside those four parts (tuner, MIDI mapping, presets, multi-mic cabs, parametric models, Windows builds, installers) is out of scope and belongs to milestone 7 — treat proposals to add them as scope creep and say so.

## Build and validate

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build                            # all three formats
cmake --build build --target AmpSim_Standalone # AmpSim_AU, AmpSim_VST3 likewise

auval -v aufx Amp1 Amps                        # AU; plugin code Amp1, manufacturer Amps
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 \
    --validate build/AmpSim_artefacts/Debug/VST3/AmpSim.vst3
```

```bash
ctest --test-dir build                        # all tests
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "bypass"            # one test, or a pattern
```

All three pass. Run `ctest` after any DSP change and the validators after any change to the
processor shell — they cover different things: the validators catch threading and state bugs a DAW
hides, `ctest` catches wrong DSP, which the validators never look at.

Tests live in `tests/`, link the `AmpSim` shared-code target (so they test the same objects the
plugin builds ship) and use Catch2 v3, pinned as a submodule. `tests/TestHelpers.h` has the shared
fixtures: `makePreparedProcessor()`, `runConstant()` for DC through the chain, `setParam()`, and
`blocksForRamp()` for waiting out a smoother. Add a new block's tests as their own file in
`tests/CMakeLists.txt`.

A test is only worth committing if it fails when the behaviour it describes is broken — check that
by reverting the fix, not by assuming.

`COPY_PLUGIN_AFTER_BUILD` is on, so every build installs into `~/Library/Audio/Plug-Ins/`. Use
`-DCMAKE_BUILD_TYPE=Release` for anything judged by ear or by CPU load.

JUCE is pinned to the 9.0.2 tag as a submodule under `external/JUCE` — update it deliberately, in
its own commit, and re-validate. Full Xcode is not needed; the Command Line Tools build and sign all
three formats. NAM Core, Eigen and nlohmann/json get added as submodules the same way in milestone 2;
`Tr3m/nam-juce`'s `CMakeLists.txt` is the reference for that wiring.

## Architecture

Fixed mono signal chain, each block a self-contained `juce::dsp` processor with `prepare` / `process` / `reset`:

```
Input gain → Noise gate → Front-of-amp pedals (comp, overdrive, distortion)
  → Amp model (NAM) → Tone stack + master → Post-amp pedals (chorus, delay, reverb)
  → Cab sim (IR convolution) → Output gain
```

**The cab IR is last.** The post-amp pedals run *before* it, so delay repeats and reverb tails pass through the speaker response like the dry signal does — the loop-like position, not the studio convention of effects on the miked sound.

Three structural rules that drive most of the code:

- **Pedal placement is fixed, not user-reorderable, and mirrors a physical rig.** Gain-stage pedals go *in front of* the amp model, because an overdrive works by changing what the preamp distorts; modulation and time effects go *after* it, so the repeats are of the already-distorted tone. Keep `preAmpChain` and `postAmpChain` as two distinct chains with the amp between them — do not merge them into one reorderable list. Per-pedal bypass is a parameter; bypassed pedals stay in the chain and pass through, so nothing is rebuilt on the audio thread.
- **All parameters live in one `AudioProcessorValueTreeState`.** The UI only reads and writes that tree, via attachments; it never talks to DSP objects directly.
- **The audio thread never allocates, locks, or touches files.** `.nam` model loading and IR loading happen on a background thread and are swapped in with an atomic pointer (or via `Convolution::loadImpulseResponse`, which already does this internally). Model swaps take tens of milliseconds, so audio crossfades or mutes briefly.

### The amp block is not just the model

A standard `.nam` capture is a snapshot of one amp setting — the knobs are *not* inside the model. Each front-panel control is a plugin-side stage around it:

| Control | Placement | Implementation |
| --- | --- | --- |
| Gain | before the model | input gain in dB (≈ -20 to +20); more level in = more saturation out |
| Bass / Mid / Treble | after the model, before the post-amp pedals | modelled passive tone stack (Yeh & Smith 2006) *or* low shelf + mid peak + high shelf — still an open question |
| Presence *(optional)* | after the model | high shelf ≈ 3–5 kHz; not in the minimal control set — add only if the three-band EQ proves too blunt |
| Master | after the model | output gain; optionally a second waveshaper to imitate power-amp saturation |
| Model selector | replaces the model | `.nam` files from a user folder, loaded off-thread, atomic pointer swap |

This is milestone 4 and is the part that distinguishes the plugin from a plain NAM loader.

### Sample rate and oversampling

A `.nam` model has a native sample rate stored in the file (typically 48 kHz); resample in the plugin when the host runs at another rate. NAM models do *not* need oversampling. The overdrive/distortion pedals and any post-model waveshaper do — 2x–4x via `juce::dsp::Oversampling`; the modulation and time effects do not.

A `.nam` capture is of the whole amp, so there is no insertion point for a true effects loop; the post-amp pedals sit outside the model but before the cab, which is as close as the capture allows.

## Layout

```
CMakeLists.txt
external/JUCE      pinned submodule; NAM Core, Eigen, json join it in milestone 2
external/Catch2    pinned submodule (v3.9.1)
src/
  PluginProcessor.h/.cpp
  PluginEditor.h/.cpp
  dsp/             one file per stage: Amp, Cab, Gate, ...   (empty)
  ui/                                                        (empty)
resources/irs/     bundled IR .wav → BinaryData              (empty)
tests/             Catch2 suites + TestHelpers.h
tests/fixtures/    clean DI guitar recordings                (empty)
```

The empty directories are the planned layout, held by `.gitkeep`.

## Testing approach

Offline C++ tests assert on *measurable* properties, not on sound: the -3 dB point of a filter, the RMS of gated silence, the peak of an impulse through the convolution matching the IR's peak. Null tests against a reference render (invert, sum, measure residual; below -40 dB is close, below -60 dB is hard to hear) are the main check on the amp block. Sanitizer builds (ASan/TSan) and Instruments' Time Profiler at a 64-sample buffer cover real-time safety. Manual listening comes last, at matched loudness, against a fixed DI track.

## Licensing constraints that affect code decisions

- JUCE 8+ is AGPLv3 or a free Personal commercial tier below a revenue limit — which one applies is an open question in the plan, and it determines whether the repo can be public.
- A GPL dependency makes the whole plugin GPL if distributed. Check each third-party DSP library's licence before adding it.
- `.nam` model files and capture datasets carry their own licences, and captures of trademarked amps cannot be republished under the amp's name.
