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
