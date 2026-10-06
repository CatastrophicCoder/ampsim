# Changelog

What changed in each published release. The [user guide](https://catastrophiccoder.github.io/ampsim/)
describes how the current version behaves; this records what moved between versions.

## [0.5.0] — 2026-10-06

**Intel Macs.** macOS now has two builds, one for Apple silicon and one for Intel, and every file
says which it is for.

### Added

- **Intel Macs.** A second macOS build, for Intel Macs on macOS 11 or later with an AVX2
  processor — every Intel Mac that runs macOS 11 except the 2013 Mac Pro. It is a separate
  package rather than a universal binary.

### Changed

- **The macOS files say which Mac they are for**: `AmpSim-<version>-macos-apple-silicon` and
  `AmpSim-<version>-macos-intel`, each as a `.pkg` and a `.dmg`, where there used to be one
  unlabelled pair. The installer refuses the wrong one by name, where before the Apple silicon
  package would install on an Intel Mac and leave plugins that could not load.

## [0.4.0] — 2026-10-04

**Windows.** The VST3 and the standalone now build, test and install on Windows as well as
macOS, published from the same release.

### Added

- **Windows.** The VST3 and the standalone, for x64 Windows 10 (version 2004 or later) and 11,
  with an Inno Setup installer that asks whether to install for you or for everyone. The build
  needs a processor with AVX2, which every Intel Core since 2013 and every AMD Ryzen has; the
  installer checks and refuses rather than install something that would crash its host. The
  standalone supports **ASIO**, which is how to play through it with low latency. Unsigned, so
  SmartScreen asks once.

### Changed

- **New typefaces, on both platforms.** The panel's lettering was Avenir Next and Futura, which
  ship with macOS only and cannot be embedded. It is now Figtree and Jost, both open-licensed and
  built in, scaled to the same cap heights so nothing on the panel moved. JUCE's own popups use
  Figtree too, rather than the system font.
- **The amp model costs a little less.** Neural Amp Modeler's specialised path for the bundled
  capture's architecture had been compiled out by accident; it is now on, and is tested to play
  the capture as the general one does.

### Fixed

- **A host sending a bigger block than it announced no longer crashes the plugin.** Every buffer in
  the chain was sized to the announced block, and a larger one overran them. Hosts are allowed to
  do this; it showed up with the Windows standalone in exclusive mode, as corrupted sound, and
  applies on macOS too.
- **Loading a model before the audio device was open no longer depends on luck.** The resampler
  divided by a sample rate of zero, which happened to be harmless on Apple silicon and threw on
  Windows.

- **The plug-ins no longer offer MIDI learn.** In 0.3.0 a knob's right-click menu offered to learn a
  MIDI controller in every format, and in the Audio Unit or the VST3 it then waited for a controller
  it could never hear, since neither receives MIDI. The menu now appears only in the standalone; in
  the plug-ins a right-click does nothing, and the host's own controller mapping is the way to
  drive a control.

## [0.3.0] — 2026-10-01

**The first published release.** 0.2.0 was published briefly and withdrawn: its Audio Unit was a
MIDI-controlled effect, which Logic feeds from a side chain, so inserted on an audio track it was
silent. Everything below is new relative to having nothing published.

### What is in it

- **Amp** — a Neural Amp Modeler capture, level-matched across captures, behind Gain, Bass, Middle,
  Treble, Presence, Depth and Master. The amp's seven controls are marked 0 to 10 like an
  amplifier's, with the two ends printed on the plate. A power switch with a pilot lamp.
- **Pedals** — six positions in a fixed order, the way a rig is plugged up. In front of the amp: a
  compressor, and a dirt slot holding a distortion, an overdrive, a fuzz or a clean boost. After
  it: a modulation slot holding a chorus, a flanger, a phaser or a tremolo, a delay that can sync
  to the host's tempo, and a reverb. The noise gate listens to the guitar in front of the amp and
  closes after it, so it silences the amp's own hiss.
- **Cabinet** — an impulse response, or a four-corner grid of them blended by mic axis and
  distance, with a low cut and a high cut.
- **Around it** — a tuner that reads the strings ahead of everything; a transpose of up to an
  octave either way; a metronome with twelve time signatures that follows the host's bar lines; a
  preset system; and MIDI controller mapping in the standalone.
- **Formats** — Audio Unit, VST3 and a standalone app, for Macs with Apple silicon on macOS 11 or
  later. Ad-hoc signed, so the first launch needs **System Settings → Privacy & Security → Open
  Anyway**.

### Fixed since the withdrawn 0.2.0

- **The Audio Unit is an ordinary effect.** It inserts on an audio track in Logic like any other amp
  sim, under Catastrophic Audio, instead of as a MIDI-controlled effect that took its audio from a
  side chain.

### Known limitations

- **MIDI learn works in the standalone only.** Declaring a MIDI input is what made the Audio Unit a
  MIDI-controlled effect, and the same setting carries the VST3's MIDI input, so neither plug-in
  receives MIDI. In Logic, Controller Assignments (Cmd-L) map any plug-in's parameters instead. The
  plug-ins' right-click menu still offers to learn a controller, which will wait without ever
  hearing one.
- **Nothing is notarised**, so macOS blocks the first launch until it is allowed through.

[0.5.0]: https://github.com/CatastrophicCoder/ampsim/releases/tag/v0.5.0
[0.4.0]: https://github.com/CatastrophicCoder/ampsim/releases/tag/v0.4.0
[0.3.0]: https://github.com/CatastrophicCoder/ampsim/releases/tag/v0.3.0
