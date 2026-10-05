# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current state

**The milestone plan is finished and retired.** [`docs/roadmap.md`](docs/roadmap.md) replaced it and
holds what is still open; everything that was agreed has been built. The chain is

```
transpose → comp → dirt → Gain → NAM model → Bass/Mid/Treble/Presence/Depth → Master
          → gate → modulation → delay → reverb → cab (+ low/high cut) → power
```

with the gate keyed from the raw guitar in front of the amp, the tuner tapping in ahead of
everything, and the metronome added to the output after all of it. Two of the six pedal positions
are slots rather than fixed pedals. Around that there is a preset system, MIDI controller mapping,
a four-corner mic-position cabinet, a tuner, a transpose and a metronome.

**0.3.0 was the first published release and 0.4.0 added Windows** — 0.2.0 was withdrawn because its Audio Unit was
silent in Logic. [`CHANGELOG.md`](CHANGELOG.md) records what each published version changed; add
to it before tagging.

Packaging is complete too: `packaging/package.sh` builds Release,
ad-hoc signs each bundle and produces a `.pkg` and a `.dmg`. No Developer Program membership is
needed to build or package — it buys a Developer ID certificate and notarisation, which is what
removes the first-launch Gatekeeper dialog on someone else's Mac. `packaging/README.md` says
exactly what would change if one is bought later.

**Windows builds too**: the VST3 and the standalone, x64, packaged by
`packaging/package-windows.ps1` into an Inno Setup installer (`packaging/windows/AmpSim.iss`)
that asks whether to install for everyone or for one user. [`docs/windows.md`](docs/windows.md)
is the record of how the port was done and why each choice was made — AVX2 only, the static C++
runtime, ASIO in the standalone, the embedded fonts — with the measurements behind them. Read it
before changing any of those.

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

And on Windows:

- **Every *Windows Audio* mode goes through Windows' own audio engine**, which on a typical
  interface fixes the period at 10 ms and the rate at whatever Windows' Sound settings say. That
  is too slow to play through, and it looked like a slow plugin when it was not. **ASIO is the
  path** (`JUCE_ASIO=1`, Windows only, using the SDK headers bundled with JUCE). The standalone
  still opens on Windows Audio the first time; ASIO is chosen once in Options and remembered.
- **Windows has its own microphone privacy switch** (*Let desktop apps access your microphone*),
  and when it is off the effect is macOS's: the device appears and silence arrives. Nothing in
  the binary controls it.
- **A host may send a bigger block than `prepareToPlay` announced**, and JUCE's documentation
  says to expect it. `processBlock` splits such a block into pieces that fit; before that it
  overran every buffer in the chain and crashed. Windows' exclusive mode is where it showed, and
  `tests/ModelTests.cpp` holds the case.

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

**Captures are brought to a common loudness.** A `.nam` file records how loud it is, and two
captures of the same amp can be 15 dB apart. `prepareForLoading()` reads `HasLoudness()` /
`GetLoudness()` and stores a gain on the `LoadedModel` that brings it to
`AmpModel::referenceLoudnessDb` (−18 dB, the figure NAM's own plugin normalises to); `process()`
applies it. A file that does not carry a loudness is left alone rather than guessed at. This is the
same argument `CabSim` normalises its IRs on, and the gain is constant for the life of the model so
a swap needs no extra smoothing — the swap fade already covers it.

**The transpose runs at the very front, after the tuner's tap.** `Transpose` is a granular
shifter — two read pointers running through a delay line faster or slower than it is written, each
faded in and out so one takes over as the other runs out of room. It is polyphonic without being
told, which a guitar needs, and it does not smear a pick attack the way an FFT-based shifter must.

- **Where the read pointer jumps to is the whole design.** It cannot run away for ever, so every
  so often it jumps back about a window and carries on. Jump a *fixed* distance and the join lands
  at an arbitrary point in the waveform: the two sides partly cancel through the crossfade, which
  is a tremolo at the rate the joins happen — eight decibels deep at some intervals — and the phase
  lost at each one accumulates into a pitch error, two hundred cents sharp at an octave down.
  `bestJumpFrom()` instead looks back over the recent signal and picks the distance, within about
  twelve milliseconds either side, at which it most nearly repeats. That is overlap-add
  synchronisation; it needs no pitch detection and a chord does not confuse it.
- **Matching the join is what keeps the window short, and the window is the delay.** An earlier
  version hid unmatched joins by making the grains long enough to swamp them, which cost up to
  100 ms at an octave. With the joins aligned, 30 ms does for everything from an octave down to a
  fifth up; only larger upward shifts need more, because shifting up moves the pointer fastest and
  so joins most often.
- **The search reads tens of thousands of samples inside the one sample a join falls on**, so what
  it reads from matters more than how much. Against a `juce::dsp::DelayLine` that was about two
  milliseconds in a single sample — under one per cent of a core *on average*, and a dropout every
  time, because an audio block has about ten milliseconds to do everything in and the model has
  already spent most of it. A plain ring buffer with whole-sample indexing brought it to twenty
  microseconds and 0.05 % of a core. **Average cost is the wrong measure for anything that happens
  in bursts.**
- **The jump's direction is not symmetric.** Shifting down, the pointer drifts away from the write
  head and jumps back toward it; shifting up, it catches up and jumps further away. The search has
  to look the same way the jump goes, and the pointer is kept a search-width clear of the write
  head so that looking never reads past it.
- **It is bypassed outright at zero semitones**, rather than run at a ratio of one — the pointers
  would still sit a window behind and delay the signal for nothing. That interacts with the fade
  that covers an interval change: while the shifter is stepped aside the fade still has to be
  advanced, or an interval set while it is off leaves it waiting on a fade that never finishes and
  it never shifts again. Only a change between two *non-zero* intervals is faded at all; starting
  and stopping a shift is a change between dry and shifted, which the bypass crossfade already
  covers.
- **Its delay is not reported to the host**, because it varies with where in the sweep the pointer
  is. It is under 25 ms at any interval. Anything recorded through an engaged transpose will be
  late by about that.
- **Test it through the processor, not only on its own.** The shifter was correct in isolation and
  unusable in the plugin, and the only processor-level test looked at the tuner — which the bug did
  not touch.
- **The tuner taps ahead of it**, so it goes on reading the strings. Tuning to a transposed reading
  would put the guitar out, and `tests/TransposeTests.cpp` keeps the two in that order.

**The metronome is added after everything, including the bypass.** `Metronome` is not in the
signal chain at all — `AmpSimAudioProcessor::addMetronome` writes it into the output buffer once
the chain has finished, and again on the early return the fully-bypassed path takes.

- **Everything that silences the amp leaves the click going**: the power switch, the tuner's mute
  and the plugin's own bypass. It is a practice tool rather than part of the rig, and a click that
  stopped when you muted to tune would be the wrong tool. `tests/MetronomeTests.cpp` asserts this
  against all three.
- **It is silent in an offline render**, which `isNonRealtime()` is what distinguishes. A bounce
  with a click track printed into it is nobody's intention, and this is the only place in the
  plugin where the two cases differ. That is the only protection there is, and it is enough for
  the cases that matter: recording a guitar onto an armed track captures the track's *input*,
  ahead of the inserts, so the click is not on it either. What does capture it is printing this
  plugin's output in real time — a real-time bounce, a print track, a host set to record output —
  and nothing in a plugin can prevent that, because its output is the only place it has to put a
  sound. The guide says so rather than the code pretending otherwise.
- **It follows the host's grid when the host is playing**, from `AudioPlayHead::PositionInfo`'s
  `ppqPosition`, so the clicks land on the host's bar lines rather than drifting against them. The
  tempo parameter is used only when there is no transport to follow, which in practice means the
  standalone.
- **The tempo is the quarter note, and the bar decides what a beat is worth.** Both paths count
  quarter notes — the host reports them, and the free-running counter accumulates them — and
  `Metronome::TimeSignature::quarterNotesPerBeat` is what turns that into beats. So 6/8 clicks
  eighths and comes twice as often as 6/4 at the same setting, and 2/2 clicks halves and comes
  half as often. Counting in beats instead would have made a bar change move the position.
- **`Metronome`'s own table is the one place the list of bars lives**, and the parameter's choices
  are built from it with `timeSignatureNames()`. This is the dirt slot's lesson applied in
  advance: a hand-written choice list beside a hand-written table is two orders that can drift,
  and the symptom is a name attached to the wrong bar rather than anything that looks like a bug.
  **Append to the table, never reorder it** — a saved session stores the choice as an index, and
  `tests/MetronomeTests.cpp` pins the six that shipped first to theirs.
- The accent is the same sound a fifth up rather than a louder one, so the bar reads as a bar in
  a mix. It is also somewhat louder, which is what a test can measure — but measure it against a
  threshold *between* the two levels, not just under the loudest: where a sine's own peak lands
  inside a 14 ms decay varies enough that the accented clicks are not all the same height.

**NAM registers its architectures with file-scope statics**, so `nam_core` must be linked with
`$<LINK_LIBRARY:WHOLE_ARCHIVE,...>` (the `NAM_CORE_WHOLE` variable). A normal static link drops those
translation units and every model fails with "No config parser registered for architecture".

**NAM is built by this project's CMake, not its own, so NAM's defaults do not apply.** Its
`NAM_ENABLE_A2_FAST` was compiled out that way until it was noticed. It is a specialised WaveNet
for the A2 shape, which the bundled capture has (a `SlimmableContainer` of a 3- and an 8-channel
A2 WaveNet), and NAM takes it silently. `ampsim_bench` prints whether it was taken. Because it is
a second implementation of the same model, `tests/ModelTests.cpp` null-tests it against the
generic WaveNet on the bundled capture. Check NAM's `CMakeLists.txt` for anything else it
defines when the submodule is updated.

## The panel

`AmpLookAndFeel` holds the whole visual identity; `AmpPalette` holds the colours, so a second
window inherits them rather than redefining them.

A cool near-black frame holding drawn objects. Three colours carry meaning and are not
interchangeable with the rest of the palette:

- **amber** (`AmpPalette::value`) — where a control is set;
- **green** (`engaged`) — in your signal;
- **red** (`bypassed`) — switched out of it.

The shape is a persistent bar (preset field, tuner, bypass), a tab bar, one of three
pages, and a shelf along the bottom: `AmpPage` (the head), `PedalsPage` (six `PedalObject`s in one left-to-right run with the amp
drawn in the middle of it) and `CabPage` (the cab, with the mic-position grid on its grille).
Nothing uses an image asset; everything is drawn.

**There are two bars, and they are deliberately not alike.** The top one is what you are playing
*through* — which preset, whether the amp is in circuit, whether you are tuning. The bottom shelf
is what you are playing *against*: the transpose, the metronome, and the panel's size, which is
housekeeping. It keeps the background's colour with a hairline over it rather than taking the top
bar's, because two bars of the same colour would frame the pages and make the window look like a
picture rather than a piece of gear. The pages between them are laid out in exactly the space they
always were — the panel grew downwards, so nothing on a page moved.

**The shelf is taller than the top bar, which it was not meant to be.** It was 46 points of
steppers; it is 88 because its three value controls are `AmpKnob`s, the same object the amp and the
cab pages carry, and a knob needs its name over it and its value under it. Three controls on a
shelf are not worth a second kind of knob, so the shelf gave way rather than the knob. What keeps
the two bars from reading as a frame is now colour and the hairline alone, not the difference in
height — if anything else is added down here, that is the property to protect.

- **A shelf knob keeps the default body colour.** `AmpPalette::raised` is the one for a knob
  sitting on a page; the amp and the cab both call `setBodyColour (AmpMaterials::knobCap)` and
  `setEngravedOnMetal (true)` because theirs are cut into a brushed plate. The shelf is not a
  plate, so it does neither.
- **The groups are separated by a hairline in the gap, not by spacing alone.** `transposeGroupWidth`
  and `metronomeGroupWidth` sit with the other layout constants because the paint and the layout
  both need them.
- Captions returned with the knobs, so the unit in a reading is no longer load-bearing — but it
  is still there (`+2 st`, `120 BPM`) and still worth keeping, because a knob's name says what it
  is and only the reading says what it is set to.

`AmpSimAudioProcessorEditor::ChoiceButton` is how a choice parameter gets onto the panel: the
scale button's shape pointed at a parameter, with the choices in a `PopupMenu`. A
`juce::ComboBox` would be the one object on the window that came out of the box.

**Growing the panel moves every hotspot in the user guide.** Their positions are percentages of
the panel's height, so a taller panel needs all of them scaled by the old height over the new one.
`docs/guide/index.html` and `docs/images/` both have to be redone.

**The guide's screenshots come from the snapshot tool**: `ampsim_snapshot docs/images 2`, from a
Release build with `-DAMPSIM_BUILD_TOOLS=ON`, writes all six — the three pages, the amp switched
off, the slots holding other pedals, and the tuner reading an A2 nine cents sharp. Regenerate them
rather than editing them after any change that shows on the panel.

`AmpMaterials` holds what the objects are *made of* — tolex, piping, grille cloth, brushed metal,
brass — and draws a covered box, a grille and a control plate. The amp and the cab both use it, so
the two pages read as parts of one rig. Those colours carry no meaning, unlike `AmpPalette`'s
three; a pedal's enclosure colour and the amp's pilot lamp take the same licence. **The pilot lamp
is red and does not mean what red means elsewhere** — it is a jewel on the front of an amplifier,
a thing rather than a reading. It is deeper and more saturated than `AmpPalette::bypassed` so that
the two do not read as the same mark, and dark is the whole of its off state.

**A glow has to fall off, and needs somewhere to fall off into.** The lamp's halo was a flat disc
of one alpha, which has an edge; the edge ran into the switch's own top edge and into the rocker
below, so it looked cut rather than lit. It is now a radial `ColourGradient` reaching zero alpha at
its rim, and `PowerSwitch`'s bounds are expanded upward by `lampHeadroom` into the control plate's
inset, which is empty. `paintButton` removes that headroom before laying anything out, so the lamp,
the rocker and the word stay level with the knobs beside them.

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
- **The amp's seven controls are marked 0 to 10, and only the printing changes.** `dialAttributes`
  in `createParameterLayout` gives Gain, the three tone bands, Presence, Depth and Master a
  `stringFromValue` that maps their dB range linearly onto 0–10, so 5 is flat for a band and unity
  for Gain and Master. The parameter is still the dB figure the stage works in — the range, the
  DSP, every preset and every saved session are untouched, and the host sees the same dial the
  panel does. **Give the inverse too**: an `AmpKnob`'s read-out can be typed into, and without a
  `valueFromString` a host takes "7" as seven decibels and prints 6.5 back. A whole number drops
  its decimal, because an amp goes to 10 rather than to 10.0.

  The cab's knobs and the pedals' keep their units. A cut at 100 Hz and a delay of 320 ms are
  measurements; a dial position is not, and the amp's controls are the only ones a player reads
  as an amp's.
- **The amp's knobs print the two ends of the travel rather than the value**, through
  `AmpKnob::setEndMarks`, which is what a fascia does and the reason they are numbered at all.
  The marks go at the ends of the knob's own arc — `rotaryStart` and `rotaryEnd`, which run from
  the lower left round to the lower right — so they sit where the pointer does at either stop.
  A mark's centre is therefore only 0.707 of the way out diagonally, which is why six points of
  margin each side is enough and fifteen made the knobs visibly smaller for nothing.

  It also takes the read-out away, and with it typing a value into the panel. The host's own
  editor still accepts one, which is why `valueFromString` is still worth having.
- **A value ring reads outward from wherever the control is doing nothing** — the middle for a
  band that cuts and boosts, the bottom for an amount, the top for a high cut that is switched out
  of the way when it is turned up. `ParameterSlider::ringOrigin` overrides the default guess; the
  cab's high cut is the one control that needs it, and without it wore a full ring while doing
  nothing.
- **Give every parameter a `stringFromValue`.** Without one JUCE prints the raw float, and a mix
  knob reads 0.3499999. A test in `StateTests.cpp` fails on more than one decimal anywhere.

**Look at the panel rather than reasoning about it.** `tools/PanelSnapshot.cpp` (`ampsim_snapshot`,
built with `-DAMPSIM_BUILD_TOOLS=ON`) renders each page of the real editor to a PNG with
`createComponentSnapshot`, and catches things no amount of reading the paint code will. The
Windows CI job runs it and uploads the images. A harness like it caught four problems in the
panel rework, including a knob that grew to fill a fifth of the window and a caption sitting a
hundred points away from the row it named, and the font change's "120 BP…".

**The typefaces are embedded, and scaled.** Figtree (interface) and Jost (printed names) come from
`resources/fonts` through `EmbeddedTypefaces`, so the panel looks the same on every platform.
Every height on the panel was chosen for Avenir Next and Futura, which they replaced, so
`AmpLookAndFeel::font` and `stencil` scale each face to put its capitals where the old ones were.
Pass heights as before and leave the scale alone. A new face needs its own scale measured from
its file, cap height over ascent plus descent, not guessed.

## The user guide

`docs/guide/index.html` is one self-contained page, published by GitHub Pages from `main` and
`/docs`. Its interface tour and walkthroughs are **data inside a `<script>` block**, so an edit to
a tour entry is an edit to a JavaScript string literal.

**Check the script parses after editing it.** A stray newline inside one of those strings is a
syntax error that kills the whole block — the tour and the walkthroughs then render as empty boxes
while every static section still looks fine, so the page does not obviously appear broken. It
shipped that way once.

```sh
node --check <(python3 -c "s=open('docs/guide/index.html').read(); print(s[s.index('<script>')+8:s.index('</script>')])")
```

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

**The two cuts are skipped at their end stops.** `setCutoffs()` takes a low cut and a high cut,
second order, applied inside the cab so that bypassing the cabinet takes them with it. No IIR
filter is transparent, so a 20 Hz high pass left permanently in circuit would colour every cabinet
slightly whether or not anyone had touched the control — each is therefore skipped entirely while
its smoothed value sits at `lowCutOffHz` / `highCutOffHz`, which keeps the default path the one
that was there before they existed. The high cut is also held below 0.45 × the sample rate, or a
session at 44.1 kHz gets a resonance where the control says it is doing nothing.

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
- **`MidiLearn`** only ever sees MIDI in the standalone now. `NEEDS_MIDI_INPUT` is `FALSE`, which
  is what makes the AU an `aufx` effect instead of an `aumf` music effect — an `aumf` in Logic goes
  in an instrument track's instrument slot and takes its audio from a *side chain*, so inserted the
  way anyone inserts an amp sim it is handed silence. That flag also guards the VST3's event input
  bus, so the VST3 no longer receives MIDI either. The standalone is unaffected:
  `StandalonePluginHolder` adds the MIDI callback unconditionally and `AudioProcessorPlayer` never
  consults `acceptsMidi()`, which is why that still returns `true`. **In Logic, use its own
  Controller Assignments (Cmd-L) instead** — they map any plugin's parameters without the plugin
  declaring MIDI at all.

  So the panel offers MIDI learn only where it can work. `AmpSimAudioProcessor::midiLearnAvailable()`
  is `wrapperType == wrapperType_Standalone`, and `buildParameterMenu` returns an empty menu
  otherwise, which `showParameterMenu` then declines to show. `wrapperType` is `const` and fixed at
  construction, so a test impersonates a wrapper with `AudioProcessor::setTypeOfNextNewPlugin()`
  before making the processor — see `tests/EditorTests.cpp`.
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

**Two of the six are slots rather than pedals.** `DirtPedal` holds a distortion, an overdrive, a
fuzz or a clean boost; `ModulationPedal` holds a chorus, a flanger, a phaser or a tremolo. Substitution is
not reordering — what each position does to the signal, and which side of the amp it is on, is
unchanged — so it does not touch the rule above.

- **Every type keeps its own parameters.** The processor picks the right ones in
  `currentPedalSettings()` and hands the slot a fixed set of numbers. Anonymous per-slot knobs
  would be fewer parameters and worse: a host would show "Slot 2 Knob 1", a MIDI mapping would
  follow the slot instead of the pedal it was made for, and switching type would silently move the
  settings of the one you switched away from.
- **The IDs keep the names they had.** `driveOn`, `driveAmount` and the chorus's are what the
  slots were called when each held one pedal, and a saved session looks parameters up by ID. The
  first choice in each slot is therefore the pedal that used to be there.
- **The `Type` enum, the parameter's choice list and the drawn pedal's variants are one list in
  three places.** The processor turns the parameter's index straight into a `Type`, so an order
  that differs anywhere makes every name select its neighbour's circuit — which sounds like a
  different pedal rather than like a bug, and shipped that way once. `tests/PedalTests.cpp` asserts
  the names and the enum line up.
- **The dirt slot blocks DC on its way out.** The fuzz's clipper is offset on purpose and the
  overdrive's shaper is asymmetric, so both produce a standing offset; a one-pole high pass at
  15 Hz in the oversampled path takes it before the tone control sees it. Without that the tilt
  would be working on a signal sitting on a shelf of its own making, and the amp model would be
  given a biased input.
- **A type change fades.** Two pedals in a slot sound nothing alike, so `DirtPedal` and
  `ModulationPedal` each dip to silence and back around the swap. The fade is about one block
  long, which is why the test measures a 64-sample window rather than a block peak — a whole block
  still contains a loud half.

`BypassCrossfade` is the shared switch: every pedal uses it rather than carrying its own ramp. It
answers `skip`, `processAll` or `crossfade`, and offers `scratchFor()` for the case below.

**The gate is keyed, not in-line.** `measureKey()` runs in `processBeforeAmp` on the raw guitar and
`apply()` runs at the top of `processAfterAmp`, before the time effects. A gate only in front of the
amp cannot remove hiss the *amp* makes, which on a high-gain capture is nearly all of it; a gate
only after the amp has no usable envelope to trigger on, because the distortion has flattened the
dynamics. Hardware solves this with a key input and so does this. The threshold therefore still
means a level of the raw guitar, so presets kept their meaning across the change. The detector runs
even when the pedal is bypassed, for the same reason the time effects do.

The key leads the audio it gates by the amp's latency — the resampler's ~220 samples plus the
drive's 5. Leading is the safe direction: the gate opens a fraction early rather than clipping an
attack.

Four things that were learned the hard way here:

- **`juce::Reverb` scales what you hand it**: `dryLevel` by 2 and `wetLevel` by 3. Writing a 0–1
  mix straight into `Parameters` gives +6 dB of dry at mix 0. `ReverbPedal` divides both out so the
  control means what it says, and a test measures the surviving dry gain by projecting the output
  onto a noise input.
- **The delay's time comes from the processor, not from the pedal.** `currentDelaySeconds()` reads
  the host's tempo through `getPlayHead()` and cuts it to whatever note length `delayDivision` is
  set to; `DelayPedal` still just takes seconds and knows nothing about tempo. The division's first
  choice is **Free**, so a session saved before this existed lands on the knob and behaves exactly
  as it did. A host that reports no tempo — a standalone — gets 120 BPM.

  `DelayPedal::maxDelaySeconds` (2 s, what the line holds) and `maxKnobSeconds` (1.2 s, what the
  time control offers) are deliberately different. A synced half note runs to two seconds at
  60 BPM, and the knob's range cannot be widened to suit it: a stored parameter is a proportion of
  its range, so every saved session's delay time would move.
- **Chorus, delay and reverb must keep running while bypassed.** Their delay lines have to stay
  fed — engaging one that has been sitting empty starts its delayed copy from silence, and that
  onset is a click however long the crossfade is. `BypassCrossfade::scratchFor()` gives them a
  buffer to chew on and throw away.
- **The drive boosts into the clipper above a corner rather than across the band.** `boostFilter`
  is a first-order high pass at `boostCornerHz` (700 Hz), and what the drive knob scales is that
  filtered copy added to the signal — so the low end reaches the shaper at the level it arrived
  while the rest is driven up to 36 dB harder. Clipping everything equally is what turns a
  palm-muted low string to mush, and is the one thing a pedal in front of an amp is there not to
  do. The boost is the gain *minus one*, so the knob at zero adds nothing and the pedal starts
  exactly where it did before this existed. The gap between 80 Hz and 1.5 kHz at full drive
  measures about 18 dB, which is the first-order slope's and the real pedal's.
- **The drive pedal's oversampler runs whether or not the pedal is engaged**, so the 5 samples of
  latency it reports never change under the host. The price is that the plugin is no longer
  bit-transparent with everything off — about 3 parts in 100,000 — which is why the gain tests use
  a relative tolerance and measure from the second block.
- **Every smoother has to be snapped in `prepareToPlay`**, which is now three separate bugs of the
  same shape (`Gain::reset`, `ToneStack::snapToTargets`, `PedalChain::snapToSettings`). The delay
  pedal was the one that showed why it matters: its time smoothed up from zero, so the first
  repeats landed in the wrong place. If you add a block with a `SmoothedValue`, give it a snap.

## The tone stack

`ToneStack` is five `juce::dsp::IIR::Filter<float>` — the three tone bands, plus a presence shelf
and a depth peak — with a smoothed dB value per band.

**Presence and depth are honestly named but not honestly modelled, and the header says so.** On a
real amplifier they take negative feedback off the power amp at one end of the range, which raises
the gain there *and* changes the distortion and damping with it. A capture already contains the
power amp with its loop at whatever position it was captured, so two filters after the model can
offer the frequency response and nothing else. They are placed away from the bands they would
otherwise duplicate: presence is a shelf at 5.5 kHz, above the treble shelf's 3.2 kHz corner, and
depth is a *resonant peak* at 85 Hz rather than a second low shelf beside the bass control's.

Things to keep in mind when touching it:

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

A **minimal** guitar amp simulator plugin (JUCE, C++17), built as AU / VST3 / Standalone on macOS (Apple silicon and Intel, separately) and as VST3 / Standalone on Windows. Four parts, per the Goal section of `ampsim_plan.md`:

1. **Amp** — a pre-trained `.nam` model run through [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT). No hand-written amp DSP; the model supplies the tone.
2. **Custom amp-like UI** — a front panel with Gain, three-band EQ (Bass, Mid, Treble) and Master Volume. This is the project's own contribution and the reason it is not just a NAM loader.
3. **Cabinet** — a single `.wav` IR loaded into a convolution engine, with bypass. Nothing more.
4. **Pedal section** — standard effect modules with per-pedal bypass, placed like real hardware: dirt and dynamics in front of the amp, modulation and time after it (see below).

ML Sound Lab's Amped Block Letter is the yardstick the plan measures against, not a feature target.

**That list is what the project was, not what it is.** The four parts are still the core, but the
tuner, MIDI mapping, presets, the multi-mic cab, the installer, the transpose and the metronome
were all asked for and built on top of them — and an earlier version of this file told a reader to
treat each of those as scope creep. Do not reject a request on the strength of that list. What is
still deliberately out: Linux builds, a native Windows-on-ARM build, parametric or multi-model switching, a
reorderable pedal chain, and anything stereo before the cab. Each has a reason recorded where it
would be implemented, and `docs/roadmap.md` holds the rest of what is open.

## Build and validate

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build                            # all three formats
cmake --build build --target AmpSim_Standalone # AmpSim_AU, AmpSim_VST3 likewise

# auval finds the AU by registration, so it tests whatever is installed — which is only ever a
# Release build. Build build-release first, or it validates a stale one.
auval -v aufx Amp1 Ctcd                        # AU; plugin code Amp1, manufacturer Ctcd
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

**On Windows**, from a *Developer PowerShell for Visual Studio* (it puts MSVC, Ninja and
`dumpbin` on the PATH), the same commands build the VST3 and the standalone, and
`./packaging/package-windows.ps1` builds the installer with Inno Setup 6. Nobody working on this
has a Windows machine to build on, so in practice the Windows job in CI is the Windows build:
push a branch and run the workflow by hand from the Actions tab, since only `main`, tags and pull
requests trigger it on their own.

A test is only worth committing if it fails when the behaviour it describes is broken — check that
by reverting the fix, not by assuming.

**Only a Release build installs itself into `~/Library/Audio/Plug-Ins/`.** `AMPSIM_COPY_PLUGIN`
defaults on for Release and off otherwise, and feeds `COPY_PLUGIN_AFTER_BUILD`. It used to be on in
both trees, both install to the same place, and whichever built last won — so running the Debug
tests after a Release build left **a Debug AU where Logic loads it**. That looked like a heavy
plugin rather than a misconfigured one: unoptimised Eigen runs the amp model about sixty times
slower, the plugin needed 195 % of a core at 48 kHz, Logic reported "the audio engine was unable to
process all required data in time", and nothing came out. The standalone was fine throughout,
because it runs from `build-release/` directly, which is what made it hard to see.

Two ways to tell a Debug plugin is installed, should it happen again: the bundle is about 45 MB
against Release's 7.6, and a profile shows Eigen's `EIGEN_STRONG_INLINE` helpers — `madd`,
`ploadu`, `loadLhs` — as separate stack frames, which an optimised build never has. The Debug
plugin is still built, in `build/AmpSim_artefacts/Debug/`, for pluginval and for loading by hand.

Use `-DCMAKE_BUILD_TYPE=Release` for anything judged by ear or by CPU load.

**CI builds, tests, validates and packages every push and PR, on macOS and on Windows; a `v*` tag
does all of that and then a third job publishes a GitHub Release with the `.pkg`, the `.dmg` and
the Windows `setup.exe`.** That job waits for both platforms, so a tag cannot publish one while
the other failed. The tag has to match the version in `CMakeLists.txt` or the run fails before it
publishes — the workflow reads it with the same `sed` expression `packaging/package.sh` uses, and
`package-windows.ps1` reads it the same way, so the release and the files in it cannot disagree
about what version they are.

The Windows job also renders the panel (`AmpSim-Windows-panel`), runs the benchmark, and installs,
upgrades and uninstalls the installer both ways. **Run anything that launches a GUI-subsystem
`.exe` under bash, or with `Start-Process -Wait`**: PowerShell does not wait for those, and a
pluginval step once passed in five seconds without having finished. To cut a release: bump `project(AmpSim VERSION ...)`, add the version's entry to `CHANGELOG.md`,
commit and push, then

```bash
git tag -a vX.Y.Z -m "AmpSim X.Y.Z" && git push origin vX.Y.Z   # after the version commit is pushed
```

**There are two Mac builds, not one universal binary**: `AmpSim-<version>-macos-apple-silicon`
and `-macos-intel`, each a `.pkg` and a `.dmg`, both against a deployment target of macOS 11. The
macOS job is a matrix, and each leg builds, tests and validates on its own kind of Mac:
`macos-latest` for arm64 and `macos-15-intel` for x86_64. That Intel label is the last GitHub
offers and is retired in autumn 2027; after that the Intel build has to be cross-built on Apple
silicon and tested under Rosetta, which runs AVX2 (all 126 tests passed that way) though
`sysctl hw.optional.avx2_0` says 0 under it. `package.sh` takes `AMPSIM_ARCH`, refuses a binary
`lipo` says is the other architecture, and turns off `AMPSIM_COPY_PLUGIN` for a cross build so an
Intel AU never replaces the one the hosts on an Apple silicon Mac load.

**The Intel Mac build is AVX2 and FMA**, `-mavx2 -mfma` in `CMakeLists.txt`, for the reason the
Windows build is; clang does not imply FMA from AVX2 the way MSVC's `/arch:AVX2` does. The
installer's `installation-check` in `packaging/distribution.xml` refuses an Intel package on
Apple silicon, an Apple silicon one on Intel, and an Intel Mac without AVX2 (only the 2013 Mac Pro
among those macOS 11 supports). The Windows build is x64 and needs AVX2 and Windows 10 2004 or
later, which its installer checks.

## What it costs

`tools/Benchmark.cpp` runs the real processor through two seconds of audio per pass and reports the
time as a percentage of one core, one row per thing switched on. It is off by default and only a
Release build of it means anything — a Debug figure is several times the real one.

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DAMPSIM_BUILD_TOOLS=ON
cmake --build build-release --target ampsim_bench && ./build-release/ampsim_bench
```

On an M-series laptop at 48 kHz, the shape of the answer is: **the amp model is the whole cost and
everything else is rounding.** 2.8 % of a core at a 512-sample block and 3.5 % at 64; the cab adds
about 0.1, each pedal between 0.01 and 0.07, and all six plus the transpose and the metronome
together take it to 4.4 % at 64 samples. A fully bypassed plugin is 0.03 %.

Two consequences worth keeping in mind before optimising anything in the chain: **a block that is
not the model is not worth hand-tuning for speed**, and **the small block sizes are where the cost
is**, because the model's own overhead per call does not shrink with the block.

The Intel Mac leg in CI runs it too. Its first run, on an i7-8700B (the 2018 Mac mini's part),
was 7.9 % for amp and cab at 64 samples and 8.0 % with everything on at 48 kHz — about twice the
M-series figure — and 11.2 % at 44.1 kHz, where the resampler joins in. The same caveat about a
shared runner applies.

On Windows the same benchmark runs in CI on a shared runner, whose hardware changes between runs,
so only figures from one job can be compared with each other. On a real laptop (an i5-8350U)
through ASIO in REAPER, AmpSim took about 3.7 % — the Mac's range.

The figure to watch is not the average. See the transpose's note above: a cost that arrives in
bursts drops audio while averaging under one per cent. For that, Instruments' Time Profiler at a
64-sample buffer is the tool, not this one.

JUCE is pinned to the 9.0.2 tag as a submodule under `external/JUCE` — update it deliberately, in
its own commit, and re-validate. Full Xcode is not needed; the Command Line Tools build and sign all
three formats. NAM Core, Eigen and nlohmann/json get added as submodules the same way in milestone 2;
`Tr3m/nam-juce`'s `CMakeLists.txt` is the reference for that wiring.

## Architecture

Fixed mono signal chain, each block a self-contained `juce::dsp` processor with `prepare` / `process` / `reset`:

```
            ┌──────── the tuner's tap, ahead of everything
            │   ┌──── the gate's key, taken from the raw guitar
            │   │
Input → Transpose → Front-of-amp pedals (comp, dirt slot) → Input gain
  → Amp model (NAM) → Tone stack (Bass/Mid/Treble + Presence/Depth) + Master → Gate ←┘
  → Post-amp pedals (modulation slot, delay, reverb) → Cab sim (IR convolution + low/high cut)
  → Power → Metronome, added to the output after the plugin's own bypass
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
| Gain | `inputGain` | before the model | ±24 dB; more level in = more saturation out, which a test asserts against a real capture. Printed as 0–10 — see the panel section |
| Bass / Mid / Treble | `bass` `mid` `treble` | after the model, before Master | ±12 dB parametric bands: low shelf 100 Hz, peak 800 Hz (Q 0.7), high shelf 3.2 kHz. **Settled** against a modelled passive stack — see `ToneStack.h` |
| Master | `outputGain` | after the tone stack, before the cab | ±24 dB, printed as 0–10 like the rest of the amp's seven |
| Cab Low/High Cut | `cabLowCut` `cabHighCut` | inside the cab, after the convolution | second order, 20 Hz–1 kHz and 1–20 kHz, skipped at their end stops |
| Power | `power` | the end of the chain | mutes, on the same ramp as the tuner. Not `bypass`: an amp that is off makes no sound, it does not pass your guitar through. A fully bypassed plugin ignores it, because then the amp is out of the chain |
| Presence / Depth | `presence` `depth` | with the tone bands | ±12 dB: a high shelf at 5.5 kHz and a resonant peak at 85 Hz (Q 1.1). Named after the power-amp controls they sit where, **not** a model of the mechanism — see `ToneStack.h` |
| Model selector | — | replaces the model | `.nam` files loaded off-thread, atomic pointer swap |
| Transpose | `transposeOn` `transposeSemitones` | the very front, after the tuner's tap | ±12 semitones, granular. On the bottom shelf, not the amp: it is not one of the amp's controls |
| Metronome | `metronomeOn` `metronomeTempo` `metronomeBeats` `metronomeSound` `metronomeLevel` | not in the chain at all | added to the output after the bypass. On the bottom shelf |

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
  PluginProcessor.h/.cpp, PluginEditor.h/.cpp, ModelLoader.h/.cpp
  AssetPack.h, BundledAssets.h/.cpp, MidiLearn.h/.cpp, PresetManager.h/.cpp
  dsp/AmpModel.h/.cpp, ModelResampler.h, CabSim.h/.cpp, ToneStack.h/.cpp
  dsp/PedalChain.h/.cpp, BypassCrossfade.h, pedals/*.h and *.cpp
  dsp/Transpose.h/.cpp, Metronome.h/.cpp, Tuner.h/.cpp
  ui/AmpLookAndFeel.h/.cpp, AmpKnob.h/.cpp, ParameterSlider.h
  ui/AmpPage, PedalsPage, PedalObject, CabPage, PresetRow  (.h/.cpp each)
resources/         the packed amp model and cab IR → BinaryData
resources/fonts/   Figtree and Jost (static instances) and their OFL texts → BinaryData
tests/             Catch2 suites + TestHelpers.h
tests/fixtures/    clean DI guitar recordings                (empty)
tools/             Benchmark.cpp, PanelSnapshot.cpp, HostCheck.cpp (macOS), behind -DAMPSIM_BUILD_TOOLS=ON
docs/              roadmap.md, signal-chain-review.md, windows.md, guide/index.html, images/
packaging/         package.sh (macOS), package-windows.ps1 and windows/AmpSim.iss (Windows), README
```

Any empty directory left in that list is the planned layout, held by `.gitkeep`.

## Testing approach

Offline C++ tests assert on *measurable* properties, not on sound: the -3 dB point of a filter, the RMS of gated silence, the peak of an impulse through the convolution matching the IR's peak. Null tests against a reference render (invert, sum, measure residual; below -40 dB is close, below -60 dB is hard to hear) are the main check on the amp block. Sanitizer builds (ASan/TSan) and Instruments' Time Profiler at a 64-sample buffer cover real-time safety. Manual listening comes last, at matched loudness, against a fixed DI track.

## Licensing constraints that affect code decisions

- JUCE 8+ is AGPLv3 or a free Personal commercial tier below a revenue limit — which one applies is an open question in the plan, and it determines whether the repo can be public.
- A GPL dependency makes the whole plugin GPL if distributed. Check each third-party DSP library's licence before adding it.
- The Windows standalone's ASIO support uses Steinberg's SDK headers under their GPLv3 option, which is compatible with the AGPLv3; the installer ships that licence. The embedded fonts are OFL, and their licence texts sit in `resources/fonts` and go into the Windows installer.
- `.nam` model files and capture datasets carry their own licences, and captures of trademarked amps cannot be republished under the amp's name.
