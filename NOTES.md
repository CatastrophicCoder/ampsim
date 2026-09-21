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

Still open before milestone 2:

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
