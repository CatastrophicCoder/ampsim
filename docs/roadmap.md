# Roadmap

Work that is agreed but not done. The original milestone plan was retired once its milestones were
finished; this replaces it and holds only what is still outstanding.

[`signal-chain-review.md`](signal-chain-review.md) is the other half of this: an assessment of the
blocks in the chain and what could be done to them, with the case against each. Nothing in it is
agreed, which is why it is not here.

## Agreed, not built

Asked for and not yet started. The notes under each are what is known now, not decisions — but a
couple of them are things whoever picks the item up would otherwise find out the hard way.

### A metronome, in the bottom bar

Its own volume, a time signature, and possibly a choice of sound. Tempo comes from the host, which
`currentDelaySeconds()` already reads for the delay's sync.

**The thing to settle first is where its sound goes.** A metronome inside a plugin on a track is in
that track's signal path, so it is recorded along with the guitar and it is heard by anything
downstream. That is almost never what anyone wants. The options are all product decisions rather
than technical ones:

- only in the standalone, where there is no track to bleed into;
- in the plugin too, and documented as something to switch off before recording;
- or not audible through the plugin's output at all, which for an audio plugin means it has nowhere
  to go.

A standalone also has no transport to follow, so it needs its own tempo and its own start and stop,
which the plugin version would not.

### A bottom bar, for the metronome

**Decided, and built** — the transpose is in it, and it is where the metronome goes too. The top bar is full — at 780 logical points it holds the wordmark, a 244-point preset
field, the scale button, the tuner and the bypass, and the tuner already takes the middle over
while it is engaged. A transpose switch with a dial and a metronome with three controls do not fit
beside them. So the panel grows a second bar along the bottom and they go there.

It also reads correctly: the top bar is what you are playing through — which preset, whether the
amp is in circuit, whether you are tuning. The bottom bar would be what you are playing *against*:
a click, an interval, things that are about the practice session rather than about the tone. The
pages between them are unchanged.

What it costs and what to watch:

- **The panel gets taller.** 460 logical points now; a bar of 44 to 52 makes it about 505. At
  150 % that is 1170 × 758 physical, which still fits a laptop screen with room over. Nothing in
  the layout has to change to allow it — every page is laid out inside
  `AmpSimAudioProcessorEditor::Panel` and the scale is a transform on that one component.
- **It must not turn the panel into a sandwich.** Two bars of the same height and colour top and
  bottom would frame the pages symmetrically and make the window look like a picture rather than a
  piece of gear. Make it shorter and quieter than the top one — smaller controls, dimmer labels,
  a hairline above rather than a slab of its own colour.
- **The scale button could move down into it.** It is housekeeping rather than playing, it is the
  one thing in the top bar nobody touches twice a session, and moving it buys back 58 points plus
  a gap up top — which is where a transpose read-out would sit comfortably if the bottom bar ever
  fills.
- **Every screenshot and hotspot in the user guide shifts.** The tour's marker positions are
  percentages of the panel's height, worked out by hand from the layout numbers, so growing the
  panel moves all of them. Re-render `docs/images/` and recompute, or the markers drift off their
  controls. That is a chore, not a risk, but it is the part that gets forgotten.

The bar arrives with its first occupant rather than on its own — an empty strip is not worth
shipping, and its height depends on what goes in it.

## Still to settle

### Whether to tame a downshifted tone in code

A heavily downshifted distorted tone goes muddy and loses its articulation. Measured, by putting a
harmonically rich low E through the shifter and comparing band energies against the dry signal:

| shift | below 70 Hz | 70–250 Hz | 250 Hz–2 kHz | 2–8 kHz |
| --- | --- | --- | --- | --- |
| −2 | +5.5 dB | −0.2 | +0.1 | −1.5 |
| −6 | **+19.2 dB** | −6.9 | −1.6 | −5.7 |
| −12 | +21.5 dB | −4.9 | −3.1 | −28.1 |

Two separate things, which want different answers:

- **The mud is the energy below 70 Hz.** A low E at 82 Hz becomes 58 Hz at −6, and everything under
  it comes along — content a guitar never produces. Fed to a distortion it intermodulates into
  low-mid porridge, which is the flub and the lost tightness.
- **The lost articulation is the loss above 2 kHz.** A constant-ratio shift moves everything down,
  including the region that makes a pick attack read. It is also why a shifter never quite sounds
  like a genuinely detuned guitar: detune a real one and the string, pickup and body resonances
  stay where they are, and only the pitch moves.

**In practice the plugin already handles it**, which is why nothing has been built. The overdrive
high-passes at 700 Hz before its clipper, so the low end is never driven hard — that is the
difference between it and the clean boost for this job, and the distortion is the wrong choice
because its corner is at 90 Hz by design. With the cab's low cut brought up to 80–100 Hz as well,
the result was reported as "way better" and the question stopped being pressing.

If it ever stops being enough, two things could be built:

- **A high-pass inside the transpose**, before anything distorts what it produces. This is the
  targeted fix: it is the only point at which removing the sub-bass *prevents* the mud rather than
  cleaning it up afterwards, where the cab's low cut sits after the amp and can only do the latter.
  Fixed at about 70 Hz and automatic, or a control in the bottom bar. Against: a fixed corner will
  not suit every guitar and tuning, and it changes how the existing feature sounds for everyone.
- **Brightness compensation** — a high shelf scaled with the interval, putting back what the shift
  darkened. Against: it is a cosmetic correction to a real physical consequence rather than a fix
  for a fault, and at large intervals it is likely to sound applied rather than natural.

### Whether the pedals get names of their own

The reference the board's anatomy came from gives each effect an identity — Green Mamba, Metal
Charlie — rather than calling it what it is. They are currently called `gate`, `comp`, `drive`,
`chorus`, `delay` and `reverb`, which is what they are and what a person looking for one would
search for. It is the difference between a product and a utility, and it is a branding decision
rather than a drawing one, so it is left to whoever wants to make it.

### Whether the model and IR pickers become folder lists

Most amp plugins show a dropdown of the models in a folder rather than opening a file chooser every
time. That is a product change rather than a visual one: it needs a folder to watch, a way to set
it, and a decision about what happens to a preset whose model is not in that folder. The panel
rework has left the pickers as chooser buttons.

## Done

### The panel rework

Delivered. `docs/panel.png` and `docs/pedals.png` show the result, and the **The panel** section of
[`CLAUDE.md`](../CLAUDE.md) records the parts worth knowing before changing it.

What was decided along the way, so it is not re-litigated:

- **Warm amber for values, against a cool near-black frame.** Blue used to mean "in your signal";
  that job moved to green, red still means "switched out of it", and amber now means "where this
  control is set". A single cold accent on near-black is what a dark plugin looks like when nobody
  designed it.
- **Avenir Next for the interface, Futura for names printed on an object** — the wordmark, the
  model on its plate, a pedal's name. Both ship with macOS.
- **The amp became an object too**, not only the pedals: a chassis with the capture stamped on a
  plate and the five controls on a strip below it.
- **The pedals are one left-to-right run with the amp drawn in the middle**, rather than two rows.
  Which side of the amp a pedal is on is the design of that section, and a single run says it as
  signal flow instead of as a caption.
- **No image assets**, which is what keeps scaling free.

### A fuzz in the dirt slot

Built. A fourth choice, and not a distortion turned up: its clipper is offset so that its operating
point sits below what it will pass, which is where the lopsided waveform, the second harmonic and
the cut-off as a note dies all come from. Measured rather than asserted by ear — the second
harmonic sits more than 20 dB above the distortion's, and a signal below its threshold comes out
54 dB quieter than the same signal through the distortion.

### Transpose

Built, in the bottom bar. A granular shifter at the very front of the chain, with the tuner tapping
ahead of it so it goes on reading the strings.

What turned out to matter was where the read pointer jumps to when it runs out of room. Jumping a
fixed distance lands the join at an arbitrary point in the waveform: the two sides partly cancel,
which is a tremolo a few decibels deep at the rate the joins happen, and the phase lost at each one
accumulates into a pitch error — two hundred cents sharp at an octave down. Matching the join
against the recent signal fixes both, and it also removes the reason the grains had to be long, so
the delay is now under 25 ms at any interval rather than up to 100. Every whole step from −12 to
+12 lands within a third of a semitone, and the envelope holds to within a decibel.

### How it was verified

By rendering the editor to a PNG at each scale and looking at it, not by reading the paint code.
That found four problems reading would not have: a knob that grew to fill a fifth of the window, a
caption a hundred points from the row it named, an em dash in a `const char*` that rendered as
`â€`, and the scale control stuck at 75 % because a preset load wrote a void property over it.
