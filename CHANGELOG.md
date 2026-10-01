# Changelog

What changed in each published release. The [user guide](https://catastrophiccoder.github.io/ampsim/)
describes how the current version behaves; this records what moved between versions.

## Unreleased

### Fixed

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

[0.3.0]: https://github.com/CatastrophicCoder/ampsim/releases/tag/v0.3.0
