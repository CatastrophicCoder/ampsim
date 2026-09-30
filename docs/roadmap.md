# Roadmap

Work that is agreed but not done. The original milestone plan was retired once its milestones were
finished; this replaces it and holds only what is still outstanding.

[`signal-chain-review.md`](signal-chain-review.md) is the other half of this: an assessment of the
blocks in the chain and what could be done to them, with the case against each. Nothing in it is
agreed, which is why it is not here.

## Agreed, not built

Asked for and not yet started. The notes under each are what is known now, not decisions — but a
couple of them are things whoever picks the item up would otherwise find out the hard way.

### A fuzz in the dirt slot

A fourth choice alongside the distortion, the overdrive and the clean boost. The slot mechanism is
already there, so this is a `DirtPedal::Type`, a `StringArray` entry, a shaper and a colour.

Worth knowing before writing the shaper: **a fuzz is not a distortion turned up.** What makes one
recognisable is the behaviour at the edges rather than the amount — a hard, heavily asymmetric
clip with the bias well off centre, so the waveform is lopsided; a thin, splutter-prone response
to a signal that is fading or picked softly, which is the "dying battery" character people buy one
for; and far more gain before it gets there. Turning the existing `shapeHard` up will not produce
any of that.

### Transpose, on the top bar

A switch and a semitone dial, ±12, holding a constant interval — retuning the instrument rather
than harmonising with it. Most useful on a distorted sound, where a drop tuning is the point.

Three things that decide how big this is:

- **It has to be polyphonic.** A guitar plays chords, so a monophonic shifter is no use. That means
  a phase vocoder over an FFT, or something bought in — and JUCE has no pitch shifter, so it is
  code to write rather than a class to configure. This is nearly all of the work.
- **It belongs at the very front**, ahead of the gate, so the amp distorts the shifted note the way
  it would a genuinely detuned string. Putting it after the amp would sound like a pitch shifter on
  a guitar amp, which is a different effect.
- **The tuner should keep reading the strings, not the shifted signal.** It taps before the pedals
  today; it would need to tap before the transpose too, or it would tell you to tune to the
  interval rather than to the guitar.

It will add latency, and the amount depends on the window length the shifter needs, so the figure
has to be reported and the existing latency test extended. Quality falls off towards ±12; the
settings people actually use are −1, −2 and −12.

### A metronome, on the top bar

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

### Room on the top bar for both

The bar is full. At 780 logical points it currently holds the wordmark, a 244-point preset field,
the scale button, the tuner and the bypass, and the tuner already takes the middle over while it is
engaged. A transpose switch with a dial and a metronome with three controls do not fit beside them.

Ways out, none chosen: widen the panel; give the bar a second row; put the metronome behind a
button that opens a small panel; or let each of them take the bar's middle the way the tuner does,
which keeps the layout but means only one can be on screen at a time.

## Still to settle

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

### How it was verified

By rendering the editor to a PNG at each scale and looking at it, not by reading the paint code.
That found four problems reading would not have: a knob that grew to fill a fifth of the window, a
caption a hundred points from the row it named, an em dash in a `const char*` that rendered as
`â€`, and the scale control stuck at 75 % because a preset load wrote a void property over it.
