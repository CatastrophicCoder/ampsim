# Session notes

One entry per working session, newest last. Records what was done and anything that will not be
obvious from the diff — per the "long gaps between sessions" risk in `ampsim_plan.md`.

## 2026-09-22 — Milestone 0: toolchain

Done:

- `git init` (branch `main`), `.gitignore` covering `build/`, `cmake-build-*/` and per-user CLion state.
- CMake 4.4.3 and Ninja 1.13.2 installed via Homebrew; pluginval 1.0.4 as a cask.
- JUCE added as a submodule at `external/JUCE`, pinned to the 9.0.2 tag
  (`72782788ce18c2d4d760b28e0921d6ffc6431102`).
- `CMakeLists.txt` with `juce_add_plugin`, formats AU / VST3 / Standalone, linking
  `juce_audio_utils` and `juce_dsp`.
- Minimal pass-through `AmpSimAudioProcessor` + placeholder editor, enough to prove the chain builds
  and loads.
- All three formats build; `auval -v aufx Amp1 Amps` and `pluginval --strictness-level 5` both pass.

Worth knowing:

- Full Xcode was **not** needed — the Command Line Tools build and sign all three formats, and
  `auval` ships with macOS. The plan's step 1 (install Xcode from the App Store) can be skipped
  unless something later needs it.
- The VST3 build prints `code has no resources but signature indicates they must be present` and
  re-signs ad-hoc. It is JUCE's normal ad-hoc signing path for an unsigned local build, not an error.
- Plugin codes are manufacturer `Amps`, plugin `Amp1`. Changing them later orphans any DAW sessions
  that already reference the plugin, so leave them alone.

Still open before milestone 1:

- Which DAW is the primary test host — the AU and VST3 have been validated but not yet opened in one.
- The JUCE licence question (see README), which gates making the repo public.

## 2026-09-22 — Milestone 1: gain + bypass

Done:

- `AudioProcessorValueTreeState` with three parameters: `inputGain` and `outputGain`
  (−24…+24 dB, 0.1 dB steps, skewed so 0 dB is mid-travel) and `bypass`.
- `juce::dsp::Gain` for both gain stages, 50 ms ramp; bypass is a 20 ms dry/wet crossfade
  rather than a hard switch.
- `getBypassParameter()` overridden, so the host's own bypass button drives the same parameter.
- State save/reload through `apvts.copyState()` / `replaceState`.
- Editor: two rotary knobs and a bypass toggle, bound via attachments. Plain JUCE look —
  the amp-style `LookAndFeel` is milestone 5.

Verified:

- `auval -v aufx Amp1 Amps` passes; `pluginval --strictness-level 10` passes, including its
  state-restoration, parameter-thread-safety and fuzz tests.
- A throwaway offline harness (built in a scratch directory, not committed) checked: unity gain
  is bit-accurate, +6 dB scales by 10^(6/20), input and output gain compose, bypass returns the
  dry signal, state round-trips, and the bypass toggle ramps instead of stepping.

Worth knowing:

- **A freshly constructed `juce::dsp::Gain` sits at 0, not 1.** Setting the target in
  `prepareToPlay` therefore made the plugin fade in over the 50 ms ramp every time the host
  started playback. Fixed by calling `Gain::reset()` after `setGainDecibels()`, which pulls the
  smoother's current value up to its target. Neither auval nor pluginval catches this — only the
  offline check did. Any DSP block added later that owns a smoother needs the same treatment.
- `setStateInformation` deliberately touches only the APVTS, never the DSP objects: it runs on
  the message thread, and `SmoothedValue::setTargetValue` from there would race with
  `processBlock`. The audio thread picks the new values up on its next call.
- The bypass crossfade uses `applyGainRamp` / `addFromWithRamp` on the whole block rather than a
  per-sample loop, which is only valid because the smoothing is linear.

Still open:

- Still not opened in a DAW or run as a standalone app.

## 2026-09-22 — Test infrastructure

Done:

- Catch2 v3.9.1 added as a pinned submodule at `external/Catch2`; `enable_testing()` and
  `catch_discover_tests` in CMake, so every `TEST_CASE` is its own CTest entry and
  `ctest -R <pattern>` runs one.
- `tests/` with `TestHelpers.h` (prepared processor, DC runner, parameter setters, ramp-length
  helper) and three suites: `GainTests`, `BypassTests`, `StateTests`. 11 tests, all passing.
- The throwaway harness from milestone 1 is now redundant and gone.
- `AMPSIM_BUILD_TESTS` option, defaulting to on only when this is the top-level project.

Worth knowing:

- The tests link the `AmpSim` shared-code target rather than recompiling `src/`, so they exercise
  the same objects the AU/VST3/Standalone builds do. A test file only needs
  `target_include_directories` pointing at `src/`.
- Catch2 needs our own `main()` (`tests/TestMain.cpp`) holding a `ScopedJuceInitialiser_GUI` for the
  whole session — constructing an `AudioProcessor` without it trips the leak detector at shutdown.
- `GENERATE` lives in `<catch2/generators/catch_generators.hpp>`, not in `catch_test_macros.hpp`.
- The suite was checked against the bug it was written for: reverting the `Gain::reset()` fix makes
  two tests fail, so they are not vacuous. Do this for any test worth committing.
- **JUCE 9 has no splash screen at all** — `JUCE_DISPLAY_SPLASH_SCREEN` is obsolete and the build
  warns when it is set. Removed from `CMakeLists.txt`, and the README's licensing note corrected:
  nothing in the build needs changing for either JUCE licence.

## 2026-09-22 — Milestone 2: NAM model playing

Done:

- NeuralAmpModelerCore v0.5.4 as a pinned submodule (it brings Eigen and nlohmann/json as its own
  submodules, so fresh clones need `--recursive`), built as a `nam_core` static library.
- `AmpModel` + `LoadedModel`: model and its resampler prepared together on a loader thread, swapped
  into the audio thread by pointer under a 10 ms mute, handed back for the message thread to delete.
- `ModelResampler`: windowed-sinc conversion host ↔ model rate with FIFOs both sides, bypassed when
  the rates match, exact latency reporting.
- `ModelLoader`: background thread, error text readable from any thread.
- Model path saved on the APVTS tree as a property and reloaded with the session; "Load model..."
  button and a model/error label in the editor.
- 13 new tests (24 total). auval and pluginval --strictness-level 10 still pass.

Worth knowing:

- **NAM registers its architectures (WaveNet, LSTM, ConvNet, Linear) with file-scope static
  objects.** Nothing references them, so a normal static-library link discards the whole translation
  unit and every load fails with "No config parser registered for architecture: WaveNet". Fixed by
  linking `nam_core` with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`, which needed CMake 3.24. JUCE does
  not force-load its own shared-code archive, so this had to be handled explicitly.
- **The resampler's output FIFO used the wrong ratio** (`hostPerModel` where it needed
  `modelPerHost`). At 44.1 kHz the FIFO happened to hold enough anyway and it worked; at 88.2 and
  96 kHz it starved every block, and the measured latency was ~770 samples worse than reported.
  Caught by the impulse-latency test once it was run at more than one rate. Lesson: a rate
  conversion test that only tries one rate proves very little.
- A model and its resampler have to be one object. The first design kept the resampler in AmpModel
  and only swapped the `nam::DSP`, which meant a model loaded into a chain configured for a
  different rate ran with the wrong conversion — latency reported 0 after every swap.
- The editor and tests read load state (`getModelError()`) straight from the loader under a lock
  rather than via `callAsync`, so no test needs a running message loop.
  `MessageManager::runDispatchLoopUntil()` would have needed `JUCE_MODAL_LOOPS_PERMITTED=1`, which
  is not something to turn on in a plugin just to make tests work.

CPU, measured on this machine (Release, 48 kHz, percentage of one core, 10 s of audio):

| Model | 64 | 128 | 512 |
| --- | --- | --- | --- |
| wavenet.nam (tiny example) | 0.35% | 0.25% | 0.30% |
| lstm.nam | 0.34% | 0.35% | 0.35% |
| wavenet_a1_standard.nam | 5.3% | 5.0% | 5.3% |
| A2.nam | 3.1% | 2.7% | 2.5% |

A standard WaveNet at a 64-sample buffer costs about 5% of one core, so model size is not a
constraint for this project; the plan's open CPU question is answered.

Still open:

- Not yet opened in a DAW or run as a standalone app.
- The chain sums to mono before the model, since NAM is mono and the cab comes later.

## 2026-09-22 — Milestone 3: cab IR loader

Done:

- `CabSim` around `juce::dsp::Convolution`: loads a `.wav`/`.aiff` IR, mono, un-normalised, trimmed;
  `cabBypass` parameter with a 20 ms crossfade; IR path saved on the APVTS tree.
- "Load cab IR..." button, IR name/error label and a cab bypass toggle in the editor; the file
  chooser is now one shared helper for both the model and the IR.
- `onModelChanged` renamed `onLoadStateChanged`, since it now covers both.
- 7 new tests (31 total). auval and pluginval level 10 still pass.

Worth knowing:

- **`juce::dsp::Convolution::getCurrentIRSize()` is non-zero straight after `prepare()`.** JUCE
  installs a default engine there, so it cannot answer "has an IR been loaded", and running the
  signal through the convolution before loading one is *not* a no-op — it scaled a 0.5 DC signal to
  0.38. The gain tests from milestone 1 caught this immediately, which is the second time the old
  tests have caught a regression in new code.
- `Convolution` silently ignores a file it cannot read, so the processor validates with an
  `AudioFormatManager` first and reports the failure the way a failed model load is reported.
- `Trim::yes` strips leading and trailing silence from the IR. That is right for a cab (it removes
  pointless pre-delay) but worth remembering if an IR ever seems to have lost its front end.
- The test IR is generated into a temp file rather than committed, so the expected output is
  written down in the test itself instead of hidden in a binary.
- `AudioFormat::createWriterFor` with explicit sample rate/channels/bit depth is deprecated in
  JUCE 9; the replacement takes an `AudioFormatWriterOptions` and a `std::unique_ptr<OutputStream>&`
  by lvalue reference.

Still open:

- Not yet opened in a DAW or run as a standalone app — outstanding since milestone 0.
- The chain is still mono all the way through; the cab is where stereo would start, once there is
  anything stereo to do.

## 2026-09-22 — Milestone 4: amp-style controls

Settled first: **Bass/Mid/Treble are three independent parametric bands**, not a modelled passive
stack. Each control does one thing, centred is flat, and every band's response can be asserted in a
test. What it gives up is the interaction of a real passive network, where the controls load each
other and all-at-noon is mid-scooped. The corresponding open question in the plan is now answered.

Done:

- `ToneStack`: low shelf 100 Hz, peak 800 Hz (Q 0.7), high shelf 3.2 kHz, ±12 dB each, smoothed.
- Chain rebuilt in architecture order and made mono throughout:
  `sum to mono → Gain → model → tone stack → Master → cab → out to all channels`.
- `inputGain`/`outputGain` are now presented as **Gain** and **Master**. The IDs keep their old
  spelling on purpose, since a saved session looks parameters up by ID.
- Editor is a five-knob panel.
- 7 new tests (38 total). auval and pluginval level 10 still pass.

Worth knowing:

- **`juce::dsp::IIR::Coefficients::makeLowShelf()` and friends allocate** — each call returns a new
  reference-counted object, so calling them per block is an allocation on the audio thread.
  `juce::dsp::IIR::ArrayCoefficients` returns a plain `std::array` instead, and assigning it into an
  existing Coefficients only rewrites the five normalised values in already-sized storage.
- Coefficients are recomputed every 32 samples rather than per block, because a 512-sample block at
  48 kHz is 10 ms and a full-range knob sweep in that few steps is audible.
- `snapToTargets()` is the same lesson as `Gain::reset()` in milestone 1, applied before it could
  bite; there is a test that fails if it is removed.
- **Float biquads are not bit-exact at DC.** A 100 Hz shelf at 48 kHz has poles near z = 1, and the
  float recursion accumulates about 0.002 dB at DC even with identity coefficients. The milestone 1
  gain tests measured with DC and were asserting bit-accuracy, so they now use a relative tolerance.
  Checked the coefficients themselves first — they are exactly identity at 0 dB, so the error is in
  the recursion, not the design.
- Measured, rather than assumed, that Gain drives the model: with `wavenet_a1_standard.nam`, a
  +12 dB input boost raises the output by 0.3 dB (ratio 1.036 where linear would be 3.98). The toy
  example models are nearly linear (3.67–3.77), so a saturation test needs a real capture.

Still open:

- **The milestone's stated criterion — a matched-loudness A/B showing each knob does what its label
  says — has not been done.** The measured band responses cover the objective half; the listening
  half needs a guitar and a DAW.
- Not yet opened in a DAW or run as a standalone app, outstanding since milestone 0.

## 2026-09-22 — Milestone 5: amp-style UI

Design direction: **not** a tolex-and-gold-lettering amp pastiche, which is what every amp plugin
looks like. The amp here is a file — a neural capture loaded from disk — so the panel is drawn as a
piece of measuring equipment instead: a pale enamelled plate with a faint grain, engraved lettering,
graphite knobs, one saturated blue arc per control reading its value against a scale, and red
reserved for the single meaning "this is switched out of your signal". Two nameplate rows at the
bottom carry the loaded model and cab, which is the honest thing to put there for a file-driven amp.

Done:

- `AmpLookAndFeel` + `AmpPalette`: rotary control, toggle lamp, button and text-box drawing.
- Editor rebuilt as header rail / plate / two nameplate rows, with `LabelledKnob` and
  `NameplateRow` components.
- Copy: the cab's toggle now says "bypassed" in the same words as the plugin's own bypass, rather
  than "off".
- 38 tests still pass; auval and pluginval level 10 still pass.

Worth knowing:

- **Render the editor to a PNG and look at it.** A ~40-line console app that builds the processor,
  calls `createComponentSnapshot (bounds, false, 2.0f)` and writes a PNG through `PNGImageFormat`
  caught, in one glance, four things that all looked correct in the source: engraved dark-on-dark
  text on the rails was unreadable; the value read-outs still had JUCE's default frame; the plate
  grain was a hard 3-pixel stripe pattern; and the pale Browse buttons were the brightest objects on
  the panel. The harness is in the scratchpad, not committed — worth rebuilding whenever the UI
  changes.
- **Engraving has to know which way the light comes from.** Dark type with a light impression below
  works on the pale plate and turns to mush on a dark rail, which needs the opposite:
  `drawRailText`.
- **A slider's text box takes its colours from the slider, not from the LookAndFeel.** Setting
  `textBoxOutlineColourId` in the LookAndFeel constructor had no effect; it has to be set on the
  `juce::Slider` itself.
- Removed the centre-detent tick after seeing it: at this size it read as a speck of dirt, and the
  value arc already grows out of the rest position, so it was saying the same thing twice.

Still open:

- The listening tests — matched-loudness A/B for milestone 4, and hearing any of this in a DAW —
  remain outstanding.

## 2026-09-22 — Milestone 6: the pedalboard

Done:

- Six pedals, in two groups that are separate `PedalChain` calls rather than one list, so the amp
  cannot end up on the wrong side of one: gate, compressor, drive **into the amp**; chorus, delay,
  reverb **after the amp, before the cab**.
- `BypassCrossfade` shared by all six, so none of them carries its own ramp.
- 20 new parameters, and a `PedalTile` deck in the editor with the two rows labelled by position.
  Pedal lamps are blue ("in your signal"); the amp's bypass lamps stay red ("switched out of it").
- 9 new tests (47 total). auval and pluginval level 10 still pass.

Worth knowing:

- **Chorus, delay and reverb have to keep running while bypassed.** Engaging one whose delay line
  has been sitting empty starts its delayed copy from silence, and that onset is a click no matter
  how long the crossfade is — measured at ten times the baseline discontinuity. They now process a
  scratch copy while off, which also gives the delay proper trails.
- **The drive pedal's oversampler runs whether or not the pedal is engaged**, so its 5 samples of
  latency never change under the host. The cost is that the plugin is no longer bit-transparent
  with everything off: about 3 parts in 100,000, plus a step response into cold filters on the very
  first block. Four milestone-1 tests were asserting bit-exactness and cold-start smoothness; they
  now measure from the second block, with the reason written down.
- **A fixed "worst jump" threshold measures the pedal, not the switch.** At drive 1.0 the pedal's
  output is nearly a square wave whose own slope is 0.24 per sample. Same mistake as milestone 2's
  model swap test, and the same fix: measure the pedal running continuously, then compare a run
  that switches it in halfway.
- The delay pedal's smoothers had to be snapped in `prepareToPlay` — the delay time ramped up from
  zero over 250 ms, so the first repeats landed in the wrong place. Third instance of this bug
  shape; it is now a rule in CLAUDE.md.

CPU on this machine (Release, 48 kHz, percentage of one core), with a real capture loaded:

| | 64 | 128 | 512 |
| --- | --- | --- | --- |
| wavenet_a1_standard, pedals off | 5.5% | 5.3% | 5.3% |
| wavenet_a1_standard, all six on | 5.7% | 5.4% | 5.5% |

The whole pedalboard costs about 0.2% of a core. The model still dominates.

Still open:

- **Nothing here has been heard.** The listening tests from milestones 4 and 6, and opening the
  plugin in a DAW at all, remain outstanding. A compressor's attack, a drive pedal's voicing and a
  reverb's size are judged by ear, and the numbers above only say the blocks do what they claim.

## 2026-09-22 — Milestone 7: tuner, cab grid, MIDI, presets

All four of the deferred items the plan listed, less the notarised installer.

**Tuner.** YIN pitch detection, audio thread into a FIFO, analysis on the message thread at 25 Hz.
Reads all six open strings to within 2 cents and does not answer an octave out on a tone whose
second harmonic is louder than its fundamental. Taps before the pedals so it reads the guitar.

**Multi-mic cabinet.** Four IRs at the corners of a mic-position space, blended by two knobs. The
blend is on the outputs of four convolutions, not by mixing IRs and reloading — convolution is
linear so it is identical, but nothing reloads as the knobs move. Empty corners drop out of the
blend and the rest renormalise. **What no test here can establish is whether it sounds like moving
a microphone**: that depends on the captures, and a grid of IRs of one cab at known mic positions is
material this project does not have.

**MIDI CC mapping.** Right-click any control to learn or forget a controller.

**Presets.** Named files holding the whole state, with four built-in ones.

Worth knowing:

- **Declaring MIDI input changes the AU's type from `aufx` to `aumf`.** `auval -v aufx` then
  reports "didn't find the component", and Logic lists the plugin under MIDI-controlled effects
  rather than with the audio effects. Nothing is released, so no sessions were orphaned, but it is
  a real compatibility boundary to cross knowingly.
- **A preset that leaves the model path empty must keep the loaded model.** The first version
  replaced the whole state, so every factory preset unloaded your amp. `applyState` now carries the
  current paths across whenever the incoming state has none; a session restore still means empty
  where it says empty.
- `juce::AlertWindow` for the preset name is entered with `enterModalState (..., false)`, never a
  modal loop: a plugin that blocks the host's message thread is a plugin that hangs the DAW.
- The `·` character in a source string literal came out as mojibake on the panel. Plain ASCII in
  UI strings until there is a reason not to.
- Four separate edits to `PluginEditor.cpp` this session were applied by line range and one of them
  silently swallowed the pedals' `addAndMakeVisible` loop — the tiles simply stopped being drawn.
  Rendering the panel to a PNG caught it immediately. Prefer unique-anchor replacements.

Still open:

- **Nothing has been heard.** Seven milestones, 72 tests, and not one note played through it.

## 2026-09-22 — Milestone 7: packaging

I had said the installer needed an Apple Developer Program membership. That was wrong, and the
correction is worth writing down: `pkgbuild`, `productbuild`, `hdiutil` and `codesign` all ship
with the Command Line Tools, and the packages they make work. The membership buys a Developer ID
certificate and notarisation — the cost of not having it lands on whoever installs the result, not
on the build.

`packaging/package.sh` builds Release, ad-hoc signs the AU, VST3 and standalone, and produces:

- `AmpSim-0.1.0.pkg`, 8.0 MB — the three formats as separate installer choices, into
  `/Library/Audio/Plug-Ins/` and `/Applications`
- `AmpSim-0.1.0.dmg`, 3.2 MB — the standalone app with a link to Applications

Verified rather than assumed:

- `pkgutil --check-signature` → "no signature"; `spctl -a -t install` → "rejected, source=no usable
  signature". That is exactly the first-launch block, confirmed rather than predicted.
- `codesign -dvv` on the component → `flags=0x2(adhoc)`, `TeamIdentifier=not set`. Ad-hoc signing
  matters because Apple silicon will not execute unsigned native code at all, so a host would fail
  to load the plugin rather than merely warn — but it carries no identity and does not satisfy
  Gatekeeper.
- The expanded payload puts the AU and VST3 under `/Library/Audio/Plug-Ins/`, root:wheel.
- The disk image mounts, and the app on it passes `codesign --verify --strict`.

Worth knowing:

- **`--deep` is deprecated for signing.** Each bundle is signed on its own instead.
- Since macOS 15 Sequoia, control-clicking and choosing Open no longer bypasses the dialog; the
  Privacy & Security route, or `xattr -d com.apple.quarantine`, is what works. Both the installer's
  welcome pane and the disk image's read-me say so.
- The read-me on the disk image is `.txt`, not `.md`: a clean Mac has no default application for a
  markdown file.
