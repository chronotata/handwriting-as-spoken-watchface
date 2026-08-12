# Handwritten As Spoken

## Background

A watchface for the Pebble Time 2, inspired by various "written" watchfaces,
but mainly building off an implemention, **[Handwritten](https://github.com/etiennovski/Handwritten)**
by **[etiennovski](https://github.com/etiennovski)**.

I have always wanted a written watchface that tells the time "traditionally"
as if reading from an analogue clock, as I have always liked thinking of time
as relative to the closest hour mark, rather than reading off the hour and
minute numbers. Others have built some but none in the way that I really like.
Thanks to modern LLM it is a lot easier to implement this with
my low experience in writing these types of apps, so this watchface was
developed extensively with the help of Claude (Anthropic)

I am uploading this to GitHub mainly to help with version control, as I am
planning to make further design changes and features additions

_________________________________

A Pebble watchface for the **Pebble Time 2** that tells the time the way
it's actually said out loud — *"quarter past three"*, *"twenty-nine minutes
to midnight"*, *"half past seven"* — handwritten across the screen, one
word at a time, rather than reading digits off a clock face.

## Features

- **Traditional time-telling** — *past*/*to* the hour, *quarter*,
  *half*, *midnight*/*midday*/*noon* used the way people actually say them
  (not simply mirroring the 12/24-hour setting)
- **A date line** — `21st Aug. 2026`, always centred, ordinal correctly
  superscript.
- **A handwriting-style reveal animation** — words appear to be written on,
  left to right, and only the parts of the display that actually changed
  redraw on each minute tick.
- **Six live tuning sliders** in the watch's settings — nudge any row up or
  down by eye, directly on the watch, no rebuild required.
- Paper and ink colours are configurable from the same settings page.

## Status

**v1.1 — working end to end.** Builds clean, and has been installed and
confirmed correct on both the Pebble emulator and a real Pebble Time 2.
Emery (Pebble Time 2, 200×228) is the only target platform for now; other
Pebble hardware is deferred.

Treat this as the stable point to branch further changes from.

**Next planned step (v2):** replacing the rendered typeface with the
maintainer's own handwriting. The rendering pipeline already exports a full
set of correctly-sized template images for this — the 83 PNGs in
`handwriting-templates/`, each at the exact canvas size its finished
artwork needs to be. Swapping them in is a resources-and-`geometry.h`
change; no layout, anchoring, or animation code should need to move.

## Building it yourself

Full step-by-step instructions, including fixes for every WSL/Ubuntu
gotcha likely to come up along the way, are in **[UPGRADING.md](UPGRADING.md)**.

For how the code is put together and why — the layout engine, the
image-based rendering approach, the tuning constants — see
**[CONTEXT.md](CONTEXT.md)**.

## Origins and credit

This project began as a rewrite of **[Handwritten](https://github.com/etiennovski/Handwritten)**
by **[etiennovski](https://github.com/etiennovski)** — the original concept
of a Pebble watchface that spells out the time in handwriting, word by
word, is entirely theirs, going back to 2014.

Almost nothing of the original implementation remains: the wording engine,
the layout and animation system, and the C source are all new, and the
original's hand-drawn word images have been replaced (for now) by a
rendered typeface. If you're looking for the original hand-drawn artwork
and the classic Pebble-generation build, that's the repository to go to.

The original repository does not carry an explicit license file, so if
you're planning to build on *this* fork for anything beyond personal use,
it's worth reaching out to the original author first — this repo's own
license (below) only covers what's new here.

### Credit for the previous fork guidance

This codebase and its documentation were developed with substantial
assistance from **Claude** (Anthropic) — including the wording engine, the
layout/animation architecture, debugging real hardware issues, and the
documentation in this repo. If you're picking through the commit history
or the `CONTEXT.md` development notes, that's why they read the way they
do.

## License

The code in this repository (everything except the bundled font — see
below) is released under the [MIT License](LICENSE).

**Marck Script**, the typeface currently used to render every word, is
licensed separately under the **SIL Open Font License 1.1** by Denis
Masharov and Marck Fogel. Its full license text is bundled at
[`resources/fonts/OFL.txt`](resources/fonts/OFL.txt), as the license
requires. If you swap in your own handwriting (v2) or a different font,
that file only needs to travel with the font it actually describes.
