# Roadmap

Work that is agreed but not done. The original milestone plan was retired once its milestones were
finished; this replaces it and holds only what is still outstanding.

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
