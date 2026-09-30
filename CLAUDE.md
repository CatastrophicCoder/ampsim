# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

**Milestone 7 is done bar packaging.** On top of the full chain
(`gate → comp → drive → Gain → NAM model → Bass/Mid/Treble → Master → chorus → delay → reverb → cab`)
there is a tuner, a preset system, MIDI controller mapping and a four-corner mic-position cabinet.

The plan's milestone 7 is now complete, installer included: `packaging/package.sh` builds Release,
ad-hoc signs each bundle and produces a `.pkg` and a `.dmg`. No Developer Program membership is
needed to build or package — it buys a Developer ID certificate and notarisation, which is what
removes the first-launch Gatekeeper dialog on someone else's Mac. `packaging/README.md` says
exactly what would change if one is bought later.

Conventions worth following for every block added after this point:

- Parameter IDs live in `namespace ParamID` in `PluginProcessor.h`. Never change an existing ID or
  its version hint; a saved session looks parameters up by ID.
- Parameter pointers are cached in the constructor, not looked up by string per block.
- **A freshly constructed `juce::dsp::Gain` sits at 0, not 1.** Call `reset()` after setting a
  smoother's target in `prepareToPlay`, or the block fades in on every playback start. Neither
  auval nor pluginval catches this; `tests/GainTests.cpp` does.
- `setStateInformation` touches only the APVTS — it runs on the message thread, and reaching into
  DSP objects from there races with `processBlock`. Non-automatable state (file paths) goes on the
  APVTS tree as a property, listed in `namespace StateID`, not as a parameter.
- Anything expensive — parsing, allocating, `Reset()`, prewarming — happens on a loader thread and
  reaches the audio thread as a finished object swapped in by pointer. See `AmpModel`.

## The standalone

Two macOS things that make it look broken rather than misconfigured:

- **`MICROPHONE_PERMISSION_ENABLED TRUE` in `juce_add_plugin` is load-bearing.** Without it the app
  has no `NSMicrophoneUsageDescription` and macOS denies audio input without ever prompting: the
  device appears, and silence arrives. The AU and VST3 are unaffected — a plugin records under its
  host's permission — so nothing but a real standalone catches it.
- **JUCE mutes a standalone's input by default** (`shouldMuteInput` in its settings file). Right for
  a synth, wrong for an amp; it is a checkbox in the Options dialog.

## The model chain

`AmpModel` owns a `LoadedModel` (a `nam::DSP` plus the `ModelResampler` configured for it). They are
one object because the conversion depends on the model's own rate, so swapping a model would
otherwise mean rebuilding the resampler — an allocation — on the audio thread.

Thread rules, which the whole design turns on:

- **Loader thread** (`ModelLoader`) parses the file, calls `AmpModel::prepareForLoading()` (which
  allocates and prewarms) and publishes with `setPendingModel()`. It reads the host's sample rate
  and block size *when it runs*, which at startup can be before `prepareToPlay` has supplied them —
  so the loader waits for `hasHostSettings()` before preparing, a `LoadedModel` records what it was
  sized for, the audio thread refuses one that does not match, and the message thread re-prepares
  it. Getting this wrong crashes on the first block.

  The wait is what keeps the common path working; the other two are the backstop. Without the wait
  the Release build lost the race often enough to fail CI while Debug passed, because a test has no
  timer to do the re-preparing.
- **Audio thread** takes it in `process()`, under a 10 ms mute so the change cannot click, and hands
  the old one back. It refuses to start a swap while a retired model is still uncollected — that is
  what keeps the hand-back slot a single pointer instead of a queue, and why the audio thread never
  deletes anything.
- **Message thread** reaps it in `collectRetiredModel()`, on a 200 ms timer in the processor, which
  also keeps `setLatencySamples()` in step.

`ModelResampler` converts host rate → model rate and back with windowed-sinc interpolation, through
FIFOs on both sides because the two conversions do not line up sample for sample. It is bypassed
entirely when the host already runs at the model's rate. Its reported latency is exact — the tests
measure the real delay with an impulse and compare.

**NAM registers its architectures with file-scope statics**, so `nam_core` must be linked with
`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` (the `NAM_CORE_WHOLE` variable). A normal static link drops those
translation units and every model fails with "No config parser registered for architecture".

## The panel

`AmpLookAndFeel` holds the whole visual identity; `AmpPalette` holds the colours, so a second
window inherits them rather than redefining them.

A cool near-black frame holding drawn objects. Three colours carry meaning and are not
interchangeable with the rest of the palette:

- **amber** (`AmpPalette::value`) — where a control is set;
- **green** (`engaged`) — in your signal;
- **red** (`bypassed`) — switched out of it.

The shape is a persistent bar (preset field, scale, tuner, bypass), a tab bar, and one of three
pages: `AmpPage` (the head), `PedalsPage` (six `PedalObject`s in one left-to-right run with the amp
drawn in the middle of it) and `CabPage` (the cab, with the mic-position grid on its grille).
Nothing uses an image asset; everything is drawn.

`AmpMaterials` holds what the objects are *made of* — tolex, piping, grille cloth, brushed metal,
brass — and draws a covered box, a grille and a control plate. The amp and the cab both use it, so
the two pages read as parts of one rig. Those colours carry no meaning, unlike `AmpPalette`'s
three; a pedal's enclosure colour and the amp's pilot lamp take the same licence.

**Scaling is a transform on one child, not a proportional layout.** Every page lays out in fixed
logical points inside `AmpSimAudioProcessorEditor::Panel`, which carries
`AffineTransform::scale`; the editor itself is sized in physical points. `AudioProcessorEditor::`
`setScaleFactor` is deliberately left alone — that is the host's hook for display DPI, and using it
for the user's own scale means the two fight. The chosen scale lives in `StateID::panelScale`, and
`applyState` will not let a preset change it.

Things that are easy to get wrong here:

- **A child button repainting does not repaint the indicator its parent draws.** Clicking a
  `Button` marks only that button's bounds dirty, so a pedal's LED and the amp head going dark —
  both painted by the enclosure around the switch — stay stale. Hook the switch's `onStateChange`
  (not `onClick`, which misses a preset or the host setting the value) and repaint the parent,
  guarded against the last drawn value so hovering does not repaint the whole object. This has
  been the same bug twice.
- **A slider's text box takes its colours from the slider, not the LookAndFeel.** Clearing
  `textBoxOutlineColourId` and `textBoxBackgroundColourId` on the LookAndFeel does nothing; set them
  on the `juce::Slider`.
- **`Slider::setPopupDisplayEnabled` takes the component the bubble lives in.** Passing the knob
  itself clips the popup to a 40 px control and it disappears. `PedalKnob` sets the top-level
  component as the parent in `parentHierarchyChanged()`, since a knob has no parent when it is
  constructed. `BubbleComponent` also centres itself on the control without pulling itself back
  inside the parent, so a control near the window edge needs a margin.
- **A `const char*` literal must be ASCII.** `juce::String` asserts on anything else and renders
  mojibake in a Release build, so an em dash in a caption comes out as `â€`.
- **Give every parameter a `stringFromValue`.** Without one JUCE prints the raw float, and a mix
  knob reads 0.3499999. A test in `StateTests.cpp` fails on more than one decimal anywhere.

**Look at the panel rather than reasoning about it.** An offline harness that renders the editor
with `createComponentSnapshot` to a PNG takes a couple of minutes to write and catches things no
amount of reading the paint code will: see the milestone 5 entry in `NOTES.md`. It caught four more
in the rework, including a knob that grew to fill a fifth of the window and a caption sitting a
hundred points away from the row it named.

## Bundled assets

The amp model and one cabinet IR ship inside the binary, packed by `AssetPack.h` and written out
to `~/Library/Application Support/AmpSim/Bundled/` by `BundledAssets::install()` on construction.

- **The packing is obfuscation, not encryption, and not a licence.** The unpacking side is in this
  repository, so the method is public; packing a file grants no right to redistribute it. What it
  buys is that the repository holds no playable `.nam` or `.wav` to be dragged out of it, and that
  the files carry neutral names rather than a trademarked amplifier's.
- The packing tool lives outside the repository, in `../ampsim-packer`, as a thin wrapper around
  `AssetPack::pack()` — one implementation of the format, not two that can drift.
- `AmpSimAudioProcessor::loadBundledAssetsOnCreation` is turned off in `tests/TestMain.cpp`, since
  most tests measure what one block does to a signal and an amp model in the way would mean
  measuring the model instead.

## The cab

`CabSim` wraps `juce::dsp::Convolution`, which already loads and resamples the IR on its own thread
and adds no latency in its default uniform-partitioned mode. Bypass is a crossfade, since an IR
changes the tone enough to click on a hard switch.

**Level is normalised across the grid, not per corner.** `measureBandGain()` takes the average
magnitude response over 80 Hz–6 kHz at load time, on the message thread, and the loudest loaded
corner sets one factor for all four. Per-corner normalisation would flatten the level differences
that make a mic position mean anything; total energy as the metric would under-read by half, because
it counts the sub-bass and the air a 4x12 rolls away. JUCE's own `Normalise::yes` is not used — it
normalises each IR independently, which is the thing to avoid here.

## Presets, MIDI and the tuner

- **`PresetManager`** writes the whole state to `~/Library/Application Support/AmpSim/Presets`. Its
  one rule worth knowing: `applyPresetState` carries over the current model and IR paths wherever
  the preset has none, so a preset that only sets the controls does not unload the amp. The
  directory is a constructor argument so tests never write into the user's own.
- **`MidiLearn`** reads its map on the audio thread through an array of atomics, and **never writes
  it there** — learning sets an atomic that the message thread commits to the ValueTree, because a
  ValueTree may only be touched from one thread.
- **`Tuner`** takes samples from the audio thread into a FIFO and does the YIN analysis on the
  message thread, from the editor's 25 Hz timer. It taps the chain before the pedals, so it reads
  the guitar rather than the distorted result.

## The pedals

`PedalChain` owns all six and exposes `processBeforeAmp` and `processAfterAmp` as **two separate
calls, not one list**, so the amp physically cannot end up on the wrong side of a pedal. The
placement is the design; do not add a "reorder" feature without revisiting `ampsim_plan.md`.

`BypassCrossfade` is the shared switch: every pedal uses it rather than carrying its own ramp. It
answers `skip`, `processAll` or `crossfade`, and offers `scratchFor()` for the case below.

Three things that were learned the hard way here:

- **Chorus, delay and reverb must keep running while bypassed.** Their delay lines have to stay
  fed — engaging one that has been sitting empty starts its delayed copy from silence, and that
  onset is a click however long the crossfade is. `BypassCrossfade::scratchFor()` gives them a
  buffer to chew on and throw away.
- **The drive pedal's oversampler runs whether or not the pedal is engaged**, so the 5 samples of
  latency it reports never change under the host. The price is that the plugin is no longer
  bit-transparent with everything off — about 3 parts in 100,000 — which is why the gain tests use
  a relative tolerance and measure from the second block.
- **Every smoother has to be snapped in `prepareToPlay`**, which is now three separate bugs of the
  same shape (`Gain::reset`, `ToneStack::snapToTargets`, `PedalChain::snapToSettings`). The delay
  pedal was the one that showed why it matters: its time smoothed up from zero, so the first
  repeats landed in the wrong place. If you add a block with a `SmoothedValue`, give it a snap.

## The tone stack

`ToneStack` is three `juce::dsp::IIR::Filter<float>` — low shelf, peak, high shelf — with a
smoothed dB value per band. Two things to keep in mind when touching it:

- **Coefficients are rewritten every 32 samples, not once per block.** A block can be 20 ms, and
  stepping a 24 dB swing in that few jumps is audible.
- **Use `juce::dsp::IIR::ArrayCoefficients`, never `Coefficients::makeX`.** The factories allocate a
  new object per call; the array version returns a `std::array` and assigning it only rewrites the
  five normalised values in storage `prepare()` has already sized.
- `snapToTargets()` exists for the same reason `Gain::reset()` is called in `prepareToPlay` — set a
  target without it and the EQ sweeps in from flat on every playback start. There is a test for it.

The bands are float, so they are not bit-exact at DC: a 100 Hz shelf at 48 kHz has poles close
enough to z = 1 that float state accumulates about 0.002 dB of error. Inaudible, but it is why the
gain tests use a relative tolerance rather than an exact one.

**A `Convolution` only installs a loaded IR while it is processing.** A cabinet corner sitting at
zero weight would still be running JUCE's default engine when the mic position first swept onto it,
so `CabSim` keeps feeding a corner until `getCurrentIRSize()` rises above 1 and leaves it alone
after that. The same fact is what makes the cab tests wait on that size rather than on a duration —
a fixed wait passes on an idle machine and fails on a busy one.

**`Convolution::getCurrentIRSize()` is already non-zero after `prepare()`** — JUCE installs a
default engine there — so it cannot be used to ask "has the user loaded an IR". `CabSim` keeps its
own flag; processing through the convolution before loading an IR is *not* a no-op and quietly
changes the level. The processor also checks the file with an `AudioFormatManager` first, because
`Convolution` ignores a file it cannot read, which would otherwise leave the UI claiming an IR that
is not there.

The design decisions that shaped this, and are not up for quiet revision: the amp is a NAM capture
rather than circuit modelling; pedal placement mirrors a hardware rig and is not user-reorderable;
the post-amp pedals run before the cab; Bass/Mid/Treble are independent parametric bands rather than
a modelled passive stack. Each is explained where it is implemented.

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

auval -v aumf Amp1 Ctcd                        # AU; plugin code Amp1, manufacturer Ctcd
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

The whole chain is built. Anything added later goes in at the position the architecture gives it
rather than wherever is convenient.

**The chain is mono throughout**, and a stereo input is summed into it before the Gain stage. A
guitar amp is mono and NAM is mono; the cab is where stereo would begin, once anything in the chain
is stereo.

**The cab IR is last.** The post-amp pedals run *before* it, so delay repeats and reverb tails pass through the speaker response like the dry signal does — the loop-like position, not the studio convention of effects on the miked sound.

Three structural rules that drive most of the code:

- **Pedal placement is fixed, not user-reorderable, and mirrors a physical rig.** Gain-stage pedals go *in front of* the amp model, because an overdrive works by changing what the preamp distorts; modulation and time effects go *after* it, so the repeats are of the already-distorted tone. Keep `preAmpChain` and `postAmpChain` as two distinct chains with the amp between them — do not merge them into one reorderable list. Per-pedal bypass is a parameter; bypassed pedals stay in the chain and pass through, so nothing is rebuilt on the audio thread.
- **All parameters live in one `AudioProcessorValueTreeState`.** The UI only reads and writes that tree, via attachments; it never talks to DSP objects directly.
- **The audio thread never allocates, locks, or touches files.** `.nam` model loading and IR loading happen on a background thread and are swapped in with an atomic pointer (or via `Convolution::loadImpulseResponse`, which already does this internally). Model swaps take tens of milliseconds, so audio crossfades or mutes briefly.

### The amp block is not just the model

A standard `.nam` capture is a snapshot of one amp setting — the knobs are *not* inside the model. Each front-panel control is a plugin-side stage around it:

| Control | Parameter ID | Placement | Implementation |
| --- | --- | --- | --- |
| Gain | `inputGain` | before the model | ±24 dB; more level in = more saturation out, which a test asserts against a real capture |
| Bass / Mid / Treble | `bass` `mid` `treble` | after the model, before Master | ±12 dB parametric bands: low shelf 100 Hz, peak 800 Hz (Q 0.7), high shelf 3.2 kHz. **Settled** against a modelled passive stack — see `ToneStack.h` |
| Master | `outputGain` | after the tone stack, before the cab | ±24 dB |
| Power | `power` | the end of the chain | mutes, on the same ramp as the tuner. Not `bypass`: an amp that is off makes no sound, it does not pass your guitar through. A fully bypassed plugin ignores it, because then the amp is out of the chain |
| Presence *(optional)* | — | after the model | high shelf ≈ 3–5 kHz; not in the minimal control set |
| Model selector | — | replaces the model | `.nam` files loaded off-thread, atomic pointer swap |

The IDs `inputGain` and `outputGain` predate the Gain/Master names and are kept because a saved
session looks parameters up by ID.

This is milestone 4 and is the part that distinguishes the plugin from a plain NAM loader.

### Sample rate and oversampling

A `.nam` model has a native sample rate stored in the file (typically 48 kHz); resample in the plugin when the host runs at another rate. NAM models do *not* need oversampling. The overdrive/distortion pedals and any post-model waveshaper do — 2x–4x via `juce::dsp::Oversampling`; the modulation and time effects do not.

A `.nam` capture is of the whole amp, so there is no insertion point for a true effects loop; the post-amp pedals sit outside the model but before the cab, which is as close as the capture allows.

## Layout

```
CMakeLists.txt
external/JUCE      pinned submodule; NAM Core, Eigen, json join it in milestone 2
external/Catch2    pinned submodule (v3.9.1)
external/NeuralAmpModelerCore  pinned submodule (v0.5.4), with Eigen and nlohmann/json
src/
  PluginProcessor.h/.cpp
  PluginEditor.h/.cpp
  ModelLoader.h/.cpp
  dsp/AmpModel.h/.cpp, ModelResampler.h, CabSim.h/.cpp, ToneStack.h/.cpp
  dsp/PedalChain.h/.cpp, BypassCrossfade.h, pedals/*.h
  ui/AmpLookAndFeel.h/.cpp, AmpKnob.h/.cpp, ParameterSlider.h
  ui/AmpPage, PedalsPage, PedalObject, CabPage, PresetRow  (.h/.cpp each)
resources/         the packed amp model and cab IR → BinaryData
tests/             Catch2 suites + TestHelpers.h
tests/fixtures/    clean DI guitar recordings                (empty)
```

Any empty directory left in that list is the planned layout, held by `.gitkeep`.

## Testing approach

Offline C++ tests assert on *measurable* properties, not on sound: the -3 dB point of a filter, the RMS of gated silence, the peak of an impulse through the convolution matching the IR's peak. Null tests against a reference render (invert, sum, measure residual; below -40 dB is close, below -60 dB is hard to hear) are the main check on the amp block. Sanitizer builds (ASan/TSan) and Instruments' Time Profiler at a 64-sample buffer cover real-time safety. Manual listening comes last, at matched loudness, against a fixed DI track.

## Licensing constraints that affect code decisions

- JUCE 8+ is AGPLv3 or a free Personal commercial tier below a revenue limit — which one applies is an open question in the plan, and it determines whether the repo can be public.
- A GPL dependency makes the whole plugin GPL if distributed. Check each third-party DSP library's licence before adding it.
- `.nam` model files and capture datasets carry their own licences, and captures of trademarked amps cannot be republished under the amp's name.
