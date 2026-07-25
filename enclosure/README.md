# ERemote / TimEremote enclosure

Parametric 3D-printable case for a **NodeMCU V3 (ESP-12E)** with the IR LED and
IR receiver on a deck to the right of the board.

- `eremote_case.py` — the parametric model (edit + re-run to regenerate)
- `eremote_case_base.stl`, `eremote_case_lid.stl` — ready to slice
- `preview_*.png` — renders

Regenerate after any edit:

```bash
pip install trimesh manifold3d matplotlib
python3 eremote_case.py
```

## Size

Outer **66.2 × 63.2 × 19.0 mm**. Two parts: base tray + snap-on lid
(friction lip + a snap hook on each short wall, pry notch to open).

## What's in it

- **PCB rails** along the board's long edges, top at 9.6 mm, so the soldered
  pin tails hang free above the floor.
- **Component deck** in the right zone with a round pocket for the 5 mm LED
  and a rectangular pocket for the VS1838B, so they can't wander.
- **LED hole** Ø5.2 mm in the lid with a countersink so the dome sits flush —
  deliberately an open hole, not a window (see below).
- **IR receiver window**: five 1.7 mm slots over the receiver instead of one
  round hole, so it reads as part of the pattern rather than a staring eye.
- **Vents**: angled slots on both long walls, same visual language as the
  IR window.
- **Glow patch**: the lid is thinned to 0.8 mm over the ESP module so the
  GPIO2 heartbeat LED shines through white plastic.
- **Micro-USB cutout** 11 × 6.5 mm, sized to clear a cable boot.

## Before you print — two things to check

1. **`DECK_H` (default 7.0 mm)** sets how high the LED and receiver sit. It is
   a guess: it assumes the LED stands upright with its dome reaching the lid.
   Sit your parts in place, measure, and adjust. Too low = the dome doesn't
   reach the hole; too high = the lid won't close.
2. **`LED_OFF_X` / `IR_OFF_X`** are measured from the board's **right edge**
   (22 mm and 14 mm). Your first note measured from the pins instead
   (1.5 cm right of D7, 1 cm right of G/3V3), which lands ~10 mm further left.
   Confirm which is right and edit the two constants.

`LED_OFF_Y` / `IR_OFF_Y` (30 mm / 42 mm from the top edge) were estimated from
the pin rows in the photo — verify those too.

## Printing

- **ASA** if you have it, **ABS** as specified, **PETG** if you want an easy
  life. All three glow nicely at the 0.8 mm patch.
- White, 0.2 mm layers, 3 perimeters. Both parts print flat, no supports.
- ABS on a part this size wants an enclosure or draft shield — the fine vent
  slots are where warping shows first.
- Print the **lid first** as a test: it is 10 minutes and tells you whether the
  LED hole, slot spacing and lip fit are right before committing to the base.

## IR through plastic — why the LED hole is open

White ABS is pigmented with titanium dioxide, which scatters near-IR badly. A
thin window over the *emitter* would cost most of your range, so the LED fires
through open air. The receiver is far more forgiving and sits behind the slot
cluster, which is also open air — no transmission gamble either way.

## Sending sideways instead of up

The current model fires **upward** through the lid, matching how you specified
the holes (X/Y offsets on the top face). If you would rather aim horizontally
at the AC, that is a geometry change in `build_lid()` / `build_base()` — say
the word and I'll move the LED hole and IR window to the right wall.
