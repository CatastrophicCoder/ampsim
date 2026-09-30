# Roadmap

Work that is agreed but not done. The original milestone plan was retired once its milestones were
finished; this replaces it and holds only what is still outstanding.

## Rework the panel to look like a current amp plugin

**Decided.** The panel works and is legible, but it does not look like what people expect a guitar
amp plugin to look like in 2026, and it should.

### What is there now, and why

The current panel is a deliberate departure, made in milestone 5: a pale enamelled instrument
plate, engraved lettering, graphite knobs, a single blue arc reading each value, red reserved for
"switched out of your signal". The reasoning was that this amp is a *file* — a neural capture —
rather than a box, so the panel was drawn as measuring equipment instead of as an amplifier.

That reasoning is now superseded. Recording it here so that whoever does the rework knows the
present design was a choice rather than an accident, and does not spend time preserving parts of it
out of caution. The replacement is free to discard all of it.

### What "like everything else" needs to settle

The market has two distinct houses, and they need different work:

- **Photoreal / skeuomorphic** — a rendered amp face: brushed or anodised metal, a tolex or
  vinyl-grain surround, chicken-head or knurled knobs with real highlights, a backlit logo, screws
  and seams. Usually built from bitmap assets, often a knob filmstrip, so it needs artwork as well
  as code and scales badly to arbitrary window sizes.
- **Flat modern dark** — a dark near-black panel, restrained accent colour, vector knobs with a
  value ring, generous spacing, crisp small type. All drawable in code, resolution independent,
  and much closer to how the panel is built today.

Both read as "current"; they are not interchangeable, and the choice decides whether this is mostly
a design-asset job or mostly a drawing-code job.

### Scope

- `AmpLookAndFeel` and `AmpPalette` hold the whole visual identity, so the rework is concentrated
  there plus the four component files in `src/ui/`. No DSP is involved.
- The layout — five amp controls, preset and amp rows, three deck rows — is not in question here;
  only its appearance.
- Whatever replaces it still has to render correctly at the fixed 640-wide editor size, keep the
  red/blue meaning distinct (out of signal / in signal), and show a held tuner reading as dimmed
  rather than hidden.
- Verify by rendering the editor to a PNG and looking at it, as in milestone 5. Reading the paint
  code is not enough; it missed four separate problems last time.

### Open

- Which of the two houses above.
- Whether reference material exists that the result should sit alongside.
- Whether the window should become resizable as part of it, since photoreal assets and resizing
  pull in opposite directions.
