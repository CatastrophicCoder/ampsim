# Signal chain review

An assessment of every block in the chain except the NAM model and the cabinet IR themselves,
against what comparable products do and what the literature says. Nothing here is agreed work —
[`roadmap.md`](roadmap.md) is for that. This is the material for deciding.

## How each item is judged

Five criteria, applied to every candidate, stated so the reasoning can be checked rather than
taken on trust:

1. **Audible benefit** — would a player hear it, and does it address a limit they would actually
   meet in use?
2. **Correctness** — does it make the plugin behave more like the thing it models, or fix
   something that is currently wrong?
3. **Cost** — lines of new DSP, CPU, added latency, and how much of it is novel code rather than
   a JUCE class configured differently.
4. **Fit with the stated scope** — `CLAUDE.md` says the project is deliberately minimal and that
   proposals to add things should be treated as scope creep and named as such. Several items
   below are good ideas that fail this criterion, and that is recorded rather than glossed over.
5. **Testability** — the repo's standard is an offline measurement that fails when the behaviour
   breaks. Some of these are easy to assert; some are not, and that is a real cost.

---

# Part 1 — What is already there

These are findings about the current code, not proposals. Each is checkable against the source.

## 1.1 The amp model ignores NAM's level calibration

`AmpModel` never calls `HasLoudness()`, `GetLoudness()`, `HasInputLevel()`, `GetInputLevel()`,
`HasOutputLevel()` or `GetOutputLevel()`, all of which `external/NeuralAmpModelerCore/NAM/dsp.h`
exposes. Nothing in `src/` references them.

**Consequence.** Two captures of the same amp at the same settings can differ by 15 dB or more at
the plugin's output, and the only thing the player can do is re-set Master by ear each time they
swap. The NAM file format carries `input_level_dbu` and `output_level_dbu` precisely so a host can
avoid this, and the reference plugin added calibration against them in v0.7.12.

**Inconsistency worth noting.** The cabinet *is* level-matched — `CabSim::measureBandGain()`
normalises the IR grid on exactly the argument that captures are not made to a common level. The
amp, where the same argument applies with more force, is not.

**Cost of fixing.** Small for the loudness-based version: read `GetLoudness()` at load time and
apply the difference from a reference as a gain, the way the cab already does. Larger for true dBu
calibration, which needs the player to enter their interface's maximum input level, and a place to
put that setting.

## 1.2 The gate cannot gate what most players want gated

`PedalChain::processBeforeAmp` runs the gate first, on the raw guitar. The reasoning in the code is
sound for input noise: gate before anything lifts the noise floor.

**But high-gain hiss is made by the amp, not by the guitar.** A gate upstream of the model cannot
remove hiss the model generates. This is the known problem that sidechained gates exist to solve:
a Decimator G String sits late in the chain and is keyed from the clean guitar, so it closes on
amp noise while being triggered by an envelope that still has dynamics. A gate placed after a
high-gain stage without a key input has no usable envelope to trigger on, because the distortion
has flattened it.

**Relevance here.** The plugin already has the clean signal at the top of `processBlock` — the
tuner taps it. A second gate stage after the amp, keyed from that tap, is architecturally cheap.
Whether it belongs is a design question, not a technical one: see 3.1.

## 1.3 The reverb's mix control is not a mix

`ReverbPedal::setParameters` sets `dryLevel = 1.0f - wetLevel * 0.5f`. At mix = 1 the dry signal is
still at 0.5, so the control never reaches fully wet, and the dry level falls as the wet rises
rather than the two trading off to a constant sum. A player who expects "mix" to mean what it means
everywhere else will find the top of the range does not do what it says.

**Also.** `juce::dsp::Reverb` is Freeverb — a Schroeder-Moorer design of parallel combs and series
allpasses, which models a room. Guitar amps have spring tanks, whose character comes from
dispersive propagation producing a sequence of chirps. The current reverb is a reasonable general
reverb; it is not the reverb a guitar amp has.

## 1.4 The drive pedal has no pre-clipping filter

`DrivePedal::process` runs gain → `shape()` → one-pole low pass. The shaper therefore sees the
full-range guitar signal, and the only tone shaping happens *after* clipping.

This is a real difference from the pedal type it stands in for. A Tube Screamer's character comes
substantially from the high pass in front of its clipping stage: bass is rolled off *before*
distortion, which is why it tightens a palm-muted low end instead of turning it to mush, and why it
pushes mids. Clipping a full-range signal and filtering afterwards produces a different result,
which is neither wrong nor the same.

**Cost of changing.** One extra one-pole in the oversampled path and one line in `setParameters`.
Testable as a magnitude measurement below and above the corner with the drive at full.

## 1.5 Smaller observations

- **The mono sum divides by the input channel count.** A guitar recorded to one side of a stereo
  track arrives 6 dB down; a dual-mono signal arrives at unity. Averaging avoids the opposite
  failure (a dual-mono signal arriving 6 dB hot), so this is a trade rather than a bug, but it is
  not documented anywhere a player would look.
- **The compressor's attack and release are fixed** at 8 ms and 180 ms with `juce::dsp::Compressor`'s
  hard knee. Most guitar compressors people reach for are optical or FET designs with
  program-dependent release. The single-knob design is a deliberate and defensible simplification;
  the fixed timings are the part that limits it.
- **The delay has no tempo sync and no modulation on the repeats.** Its repeat filter is a fixed
  2.6 kHz one-pole, which is a reasonable stand-in for tape or bucket-brigade darkening but does
  not change with the number of repeats the way either of those does.
- **The cabinet has no low cut or high cut.** These are near-universal on IR loaders, and are the
  two adjustments people make most often to an IR — removing sub-bass the speaker never produced
  and taming fizz above about 8 kHz.
- **There is no metering anywhere.** With 1.1 unaddressed, a player has no way to see that a new
  capture is 10 dB hotter than the last one; they can only hear it.

---

# Part 2 — Candidates, and the case against each

## 2.1 Level calibration for the model

**What.** Read the capture's loudness or its input/output dBu levels at load time and apply a
compensating gain, so swapping captures does not change the level.

- **For.** Fixes 1.1. Consistent with what the cab already does. The reference implementation
  exists and the metadata is already in the files. Testable directly: load two captures with
  different loudness figures, assert matched output for the same input.
- **Against.** Not every capture carries the metadata, so the feature is silently absent for some
  files, which is its own confusion. True dBu calibration needs a setting for the interface's
  input level, which is a support burden and a piece of UI for a number most players do not know.
  A loudness-only version avoids that but only normalises output, not the drive into the model —
  which is the part that changes the tone.
- **Cost.** Loudness-only: small, and it fits the existing load path. Full calibration: a
  parameter, a UI control, and documentation.

## 2.2 Presence and Depth

**What.** Two more controls on the amp: a high shelf and a low shelf after the model.

- **For.** Almost every amp plugin has them, and players look for them. Cheap: two more
  `IIR::ArrayCoefficients` shelves in `ToneStack`, tested exactly the way the existing three are.
- **Against — and this one is factual rather than a matter of taste.** Presence and depth on a real
  amp are not tone controls. They work by removing negative feedback around the power amp at high
  or low frequencies, which raises gain in that band *and* changes distortion and damping there.
  A NAM capture already contains the power amp with its feedback loop at whatever position it was
  captured. A shelf after the model reproduces the frequency response change and none of the rest,
  so it would be a Presence control in name and an extra EQ band in substance. Whether that is
  acceptable is a product decision; describing it as a presence control would be inaccurate.
- **Cost.** Two parameters, two filters, two UI controls, no new DSP technique.

## 2.3 A modelled passive tone stack

**What.** Replace or supplement the three parametric bands with a discretised passive network —
the Fender/Marshall topologies have closed-form digital forms from the Yeh and Smith work, and the
nodal DK method covers the general case.

- **For.** It is the only way to get the interaction that makes an amp's controls feel like an
  amp's: the controls loading each other, all-at-noon being mid-scooped, treble affecting the mid
  band. `ToneStack.h` already names this as the thing the current design gives up.
- **Against.** The capture already contains the amp's own tone stack, at the position it was
  captured. Putting a second modelled stack after it does not restore the first one; it puts two
  in series. The honest framing is "an additional tone stack of a chosen topology", not "the amp's
  tone stack now works properly". It is also markedly harder to test than three independent bands —
  the current tests assert each band's response in isolation, and a network where every control
  moves every band cannot be tested that way.
- **Cost.** Substantial. New DSP, new test strategy, and a decision about what to do with the
  existing three parameters, which are automatable and saved in people's sessions.

## 2.4 Pre-clipping filter in the drive pedal

**What.** A one-pole high pass before the shaper, fixed or swept, as described in 1.4.

- **For.** Moves the pedal closer to the thing it stands in for, and the behaviour it adds —
  tightening the low end under distortion — is the reason a drive goes in front of an amp at all.
  Cheap, and testable as a magnitude measurement.
- **Against.** It changes the sound of an existing control, so any preset saved with the drive
  engaged will sound different after the change. There is no versioning scheme for that in the
  preset format.
- **Cost.** Small.

## 2.5 Cabinet low cut and high cut

**What.** Two filters after the convolution, with the cab's other controls.

- **For.** The most common adjustment made to an IR, on almost every IR loader. Removes rumble the
  speaker never produced and fizz above the speaker's useful range. Two parameters, two filters,
  trivially testable.
- **Against.** It is EQ, and the host has EQ. The argument for having it here rather than in the
  next plugin along is convenience and that it travels with the preset, not capability.
- **Cost.** Small.

## 2.6 A keyed gate after the amp

**What.** Either move the gate to after the model and key it from the clean tap, or add a second
gate stage there.

- **For.** Addresses 1.2, which is the difference between a gate that works on a high-gain sound
  and one that does not. The clean tap already exists.
- **Against.** It breaks the section's organising idea — that pedals sit where they sit on a real
  board — unless it is presented as what it is, which is a rack gate with a key input rather than
  a stomp box. Two gates is two sets of controls for one job; moving the one gate changes the
  behaviour of every existing preset that uses it.
- **Cost.** Moderate. The gate itself is unchanged; the work is in routing the key signal and in
  deciding how to present it.

## 2.7 Reverb: fix the mix, or replace the algorithm

Two separable items.

**Fixing the mix** (1.3) is a small correction with one consequence: presets saved with a reverb
mix above about 0.5 will sound wetter afterwards.

**Replacing Freeverb with a spring model** is a much larger piece of work — a dispersive allpass
chain in a waveguide is the standard approach, and getting it to sound like a tank rather than a
chirping artefact is the hard part. For: it is the reverb a guitar amp actually has, and the one
most guitar presets want. Against: it is a specialised effect that suits surf and clean tones more
than the high-gain sounds this amp's capture is aimed at, and a room reverb is more generally
useful. Keeping both means two algorithms and a selector.

## 2.8 More pedals

Comparable products (Neural DSP's Archetypes, the Fractal cab and effects blocks) commonly carry a
noise gate, a compressor, an overdrive, an EQ, a phaser, a flanger, a chorus, a delay with tempo
sync, several reverb types, and often a wah, a pitch shifter and a doubler.

Against the list, this plugin is missing: **phaser, flanger, tremolo, wah, an EQ pedal, a boost
distinct from the drive, pitch effects, and tempo sync on the delay.**

- **For adding some.** Tremolo and phaser are small, well-understood, and idiomatic for guitar.
  Tempo sync is the single most-requested delay feature and needs no new DSP, only the host's BPM.
- **Against adding any.** `CLAUDE.md` states the scope is one amp, one cabinet, six pedals, and
  that proposals beyond the four parts in the plan should be named as scope creep. Every added
  pedal is a permanent maintenance and UI cost, and the pedals page is laid out for exactly six —
  a seventh needs a new layout, not just a new class. The existing six were chosen to cover the
  categories (dynamics, dirt, modulation, time, space); phaser and flanger add a second and third
  member of a category already represented by the chorus.
- **Cost.** Per pedal: a DSP class, parameters, a UI object, tests, and a re-think of the board
  layout beyond six.

## 2.9 Metering

**What.** Input and output level meters, probably in the bar.

- **For.** Without 2.1, a meter is the only way to see that a capture is hot. With 2.1, it is how
  a player verifies the calibration is doing anything. Also the standard way to notice the mono-sum
  behaviour in 1.5.
- **Against.** It is the first thing in this plugin that needs a repainting display, which means an
  audio-thread-to-UI level path and a timer redraw — a class of code the panel currently does not
  have anywhere except the tuner. Hosts and interfaces already meter.
- **Cost.** Moderate, mostly in the UI.

---

# Summary

| | Audible benefit | Fixes something wrong | Cost | Fits stated scope |
| --- | --- | --- | --- | --- |
| 2.1 Model level calibration | High when swapping captures | Yes (1.1) | Small to moderate | Yes — it is the amp block |
| 2.2 Presence / Depth | Moderate | No | Small | Adds controls beyond the plan's list |
| 2.3 Modelled tone stack | Disputed — see 2.3 | No | Large | Replaces a settled decision |
| 2.4 Drive pre-filter | Moderate | Arguably (1.4) | Small | Yes — improves an existing pedal |
| 2.5 Cab low/high cut | High in practice | No | Small | Adds controls to an existing block |
| 2.6 Keyed gate | High on high-gain sounds | Yes (1.2) | Moderate | Tension with the board metaphor |
| 2.7a Reverb mix fix | Low | Yes (1.3) | Small | Yes |
| 2.7b Spring reverb | Moderate, style-dependent | No | Large | Replaces an existing block |
| 2.8 More pedals | Varies | No | Large in aggregate | Named as scope creep in CLAUDE.md |
| 2.9 Metering | Low alone, higher with 2.1 | No | Moderate | New UI category |

Three of these — 1.1, 1.2 and 1.3 — are findings about the current code rather than
enhancements, and would be worth resolving one way or the other regardless of which direction the
plugin takes. The rest are choices.

## Sources

- NAM level calibration: [NAM plugin v0.7.12 release note](https://www.neuralampmodeler.com/post/neuralampmodelerplugin-v0-7-12-is-released),
  [calibration tutorial](https://neural-amp-modeler.readthedocs.io/en/latest/tutorials/calibration.html),
  [input calibration discussion](https://github.com/sdatkinson/NeuralAmpModelerPlugin/issues/542)
- Tone stack modelling: [Yeh & Smith, discretisation of the '59 Bassman tone stack](https://ccrma.stanford.edu/~dtyeh/papers/yeh06_dafx.pdf),
  [CCRMA tone stack page](https://ccrma.stanford.edu/~dtyeh/tonestack/),
  [Yeh et al., review of digital techniques for modelling vacuum-tube guitar amplifiers](https://dl.acm.org/doi/abs/10.1162/comj.2009.33.2.85)
- Presence and depth as negative-feedback controls: [Rob Robinette, voicing an amp](https://robrobinette.com/Voicing_an_Amp.htm),
  [Ampbooks, Soldano SLO global feedback controls](https://www.ampbooks.com/mobile/amp-technology/slo-feedback/)
- Keyed gates: [ISP Decimator II G String](https://www.perfectcircuit.com/isp-technologies-decimator-ii-g-string.html)
- Freeverb and spring reverb: [Freeverb, CCRMA](https://ccrma.stanford.edu/~jos/pasp/Freeverb.html),
  [spring reverb modelling survey](https://arxiv.org/pdf/2409.04953)
- Comparable feature sets: [Neural DSP plugins](https://neuraldsp.com/plugins),
  [Fractal Audio cab block](https://wiki.fractalaudio.com/wiki/index.php?title=Cab_block)
