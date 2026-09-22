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
