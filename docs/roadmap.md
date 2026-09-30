# Roadmap

Work that is agreed but not done. The original milestone plan was retired once its milestones were
finished; this replaces it and holds only what is still outstanding.

## Rework the panel

**Decided.** The panel works and is legible, but it does not look like what people expect a guitar
amp plugin to look like, and it should. Three decisions are settled:

- **A flat modern dark frame** — dark panel, restrained accent, crisp small type — **holding
  drawn objects**: the pedals, and the amp, as recognisable pieces of gear rather than panel
  sections.
- **Tabbed sections** rather than one tall page.
- **Resizable**, by a scale factor.
- **No image assets.** Everything drawn in code, as now.

They fit together: vector drawing is what makes scaling free, and tabs give each section room to be
designed rather than compressed into a strip.

### The reference, and what is taken from it

[Nunchuck](https://www.polychromedsp.com/nunchuck/) — a JCM800-inspired plugin, so the same amp as
the capture that ships here. Its interface is photoreal: 3D-rendered enclosures in three-quarter
perspective, brushed anodised metal with specular highlights, knurled knobs with tick rings, chrome
footswitches, screen-printed names, a tolex-and-grille amp head.

What is taken from it: **the anatomy**. Effects are objects with a footswitch, an LED and a name,
laid out as a board rather than as rows of controls.

What is not, and why: the perspective and the highlights are what a render buys. Reproducing them
means producing artwork — a 3D artist, a photo shoot, or bought packs, plus one set per scale
factor. Drawn straight-on in code it will read as a well-drawn pedal rather than a photographed
one. That trade was made deliberately: no assets, and scaling stays free.

### What is there now, and why

The current panel is a deliberate departure, made in milestone 5: a pale enamelled instrument
plate, engraved lettering, graphite knobs, a single blue arc reading each value, red reserved for
"switched out of your signal". The reasoning was that this amp is a *file* — a neural capture —
rather than a box, so the panel was drawn as measuring equipment instead of as an amplifier.

That reasoning is superseded. It is recorded so that whoever does the rework knows the present
design was a choice rather than an accident, and does not spend time preserving parts of it out of
caution. The replacement is free to discard all of it.

### Shape

A persistent bar, and three pages under it:

```
┌───────────────────────────────────────────────────────────┐
│ ampsim   [ Default        ‹ › ▾ ]        tuner   bypassed │   always visible
├──────────┬──────────┬──────────┬──────────────────────────┤
│   AMP    │  PEDALS  │   CAB    │                          │   tab bar
├──────────┴──────────┴──────────┴──────────────────────────┤
│                                                           │
│   the selected page                                       │
│                                                           │
└───────────────────────────────────────────────────────────┘
```

- **AMP** — the model in use, and Gain, Bass, Mid, Treble, Master.
- **PEDALS** — the six pedals as pedals, still in the two groups, still labelled by which side of
  the amp they are on. That placement is the design and survives the rework.

  Each one is an enclosure: a coloured body with a vertical brushed gradient, its controls as
  knurled knobs with a pointer and a ring of ticks, an LED, its name screen-printed across the
  bottom, and **a footswitch, which is how it is switched on and off**. The small lamp toggle it
  has now goes. The LED keeps its present meaning — lit is in your signal — so the colour language
  the rest of the panel uses survives.
- **CAB** — the four mic-position corners and the axis and distance controls.

The preset controls, the tuner and the bypass are global, so they stay out of the pages. The tuner
display continues to take over the bar's middle while it is engaged.

Roughly 760 × 460 logical points, wide rather than tall, against 640 × 632 now.

### Scaling

Lay out in fixed logical points and apply `AffineTransform::scale` to the whole editor, rather than
making every layout number proportional. The existing layout code then keeps working unchanged, a
scale of 75 / 100 / 125 / 150 % is a single number, and a vector panel scales with no second set of
assets. The chosen scale belongs in the plugin state so it survives reopening.

### What this touches

- `AmpLookAndFeel` and `AmpPalette` — rewritten. This is the bulk of the work.
- `src/ui/` components — their structure survives; their painting does not.
- `PluginEditor` — gains a tab bar and a page container; loses the deck layout.
- No DSP, no processor changes, no parameters added or removed.

### Still to settle

- **The accent colour.** Blue currently means "in your signal" and red means "switched out of it";
  that pairing is worth keeping, but the blue itself can change. A warm amber for values against a
  cool dark panel would read as an amp without being skeuomorphic, and would avoid the single-cold-
  accent-on-near-black look that is what a flat dark plugin looks like when nobody designed it.
- **The typeface.** Currently Helvetica Neue. The system font reads native and modern; something
  with more character would carry more of the identity.
- **Whether the pedals get names.** The reference gives each effect an identity — Green Mamba,
  Metal Charlie — rather than calling them what they are. It is the difference between a product
  and a utility, and it is a branding decision rather than a drawing one.
- **Whether the amp becomes an object too**, as a head with a control panel, or stays a panel of
  controls while only the pedals are objects.
- **Whether the file pickers become lists.** Most amp plugins show a dropdown of the models in a
  folder rather than opening a file chooser every time. That is a product change rather than a
  visual one, but the rework is when it would be cheapest to do.

### How to verify

Render the editor to a PNG and look at it, at each scale, as in milestone 5. Reading the paint code
is not enough — it missed four separate problems last time, including text that was invisible
against its own background.
