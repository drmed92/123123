#!/usr/bin/env python3
"""
ERemote / TimEremote enclosure -- parametric, 3D-printable.

Two parts: base tray + snap-on lid, sized for a NodeMCU V3 (ESP-12E) with the
IR LED and IR receiver sitting on a deck to the right of the board.

    python3 eremote_case.py            # writes STLs + preview PNGs

Every dimension below is a parameter -- tweak and re-run.
"""
import numpy as np, trimesh
from trimesh.creation import box, cylinder

# ----------------------------------------------------------------- parameters
BOARD_L, BOARD_W = 58.0, 34.0      # NodeMCU V3 footprint (measured)
STACK_H          = 13.0            # board + pins + parts, informational
CLR              = 0.6             # fit clearance around the board
RIGHT_ZONE       = 27.0            # deck width to the right of the board
WALL, FLOOR_T, LID_T = 2.0, 1.6, 1.4
INNER_H          = 16.0            # inner floor -> lid underside
RAIL_H           = 9.6             # PCB underside height (pins hang below)
PCB_T            = 1.6

# Component positions: X measured from the board's RIGHT edge,
# Y measured from the board's TOP edge (the end away from the USB).
LED_OFF_X, LED_OFF_Y = 22.0, 30.0
IR_OFF_X,  IR_OFF_Y  = 14.0, 42.0
LED_D      = 5.2                   # 5 mm LED + fit
IR_W, IR_D = 5.6, 5.8              # VS1838B body pocket
DECK_H     = 7.0                   # deck top above inner floor (see README)

USB_W, USB_H = 11.0, 6.5           # micro-USB cutout (generous for cable boot)

VENT_W, VENT_L, VENT_N, VENT_ANG = 2.0, 11.0, 6, 32.0   # side vent slots
IRWIN_SLOTS, IRWIN_W, IRWIN_GAP = 5, 1.7, 1.7           # IR window slot cluster
GLOW_T = 0.8                       # lid thickness over the ESP LED (glow patch)

# --------------------------------------------------------------- derived dims
INNER_W = BOARD_W + 2 * CLR + RIGHT_ZONE
INNER_L = BOARD_L + 2 * CLR
OUT_W, OUT_L = INNER_W + 2 * WALL, INNER_L + 2 * WALL
BASE_H = FLOOR_T + INNER_H

BX, BY = CLR, CLR                                  # board origin (inner coords)
LED_X, LED_Y = BX + BOARD_W + LED_OFF_X, BY + LED_OFF_Y
IR_X,  IR_Y  = BX + BOARD_W + IR_OFF_X,  BY + IR_OFF_Y


def b(sx, sy, sz, cx, cy, cz):
    """Axis-aligned box by size + centre."""
    m = box(extents=(sx, sy, sz))
    m.apply_translation((cx, cy, cz))
    return m


def cyl(d, h, cx, cy, cz, sections=64):
    m = cylinder(radius=d / 2.0, height=h, sections=sections)
    m.apply_translation((cx, cy, cz))
    return m


def side_vents(x_at, thick):
    """Row of angled slots cut through a long (Y-running) wall."""
    cuts = []
    span = INNER_L - 16.0
    for i in range(VENT_N):
        y = 8.0 + span * (i + 0.5) / VENT_N
        s = box(extents=(thick + 2.0, VENT_L, VENT_W))
        s.apply_transform(trimesh.transformations.rotation_matrix(
            np.radians(VENT_ANG), (1, 0, 0)))
        s.apply_translation((x_at, y, FLOOR_T + INNER_H * 0.55))
        cuts.append(s)
    return cuts


def build_base():
    outer = b(OUT_W, OUT_L, BASE_H, OUT_W / 2, OUT_L / 2, BASE_H / 2)
    cavity = b(INNER_W, INNER_L, INNER_H + 1,
               WALL + INNER_W / 2, WALL + INNER_L / 2,
               FLOOR_T + (INNER_H + 1) / 2)
    part = outer.difference(cavity)

    add = []
    # PCB support rails along the board's two long edges
    for x in (BX + 1.0, BX + BOARD_W - 1.0):
        add.append(b(2.4, BOARD_L - 6, RAIL_H,
                     WALL + x, WALL + BY + BOARD_L / 2, FLOOR_T + RAIL_H / 2))
    # component deck (right zone)
    deck_x0 = BX + BOARD_W + 2.0
    deck_w = INNER_W - deck_x0 - 1.0
    add.append(b(deck_w, INNER_L - 8.0, DECK_H,
                 WALL + deck_x0 + deck_w / 2, WALL + INNER_L / 2,
                 FLOOR_T + DECK_H / 2))
    part = trimesh.util.concatenate([part] + add)
    part = trimesh.boolean.union([part])

    cuts = []
    # locating pockets so the parts sit put on the deck
    cuts.append(cyl(LED_D + 0.6, 3.0, WALL + LED_X, WALL + LED_Y,
                    FLOOR_T + DECK_H - 1.0))
    cuts.append(b(IR_W + 0.6, IR_D + 0.6, 3.0, WALL + IR_X, WALL + IR_Y,
                  FLOOR_T + DECK_H - 1.0))
    # micro-USB opening (board's bottom end = Y=0 wall)
    cuts.append(b(USB_W, WALL * 3, USB_H,
                  WALL + BX + BOARD_W / 2, WALL,
                  FLOOR_T + RAIL_H + USB_H / 2 - 1.0))
    # side vents on both long walls
    cuts += side_vents(WALL / 2, WALL)
    cuts += side_vents(OUT_W - WALL / 2, WALL)
    # snap windows in the short walls
    for y in (WALL / 2, OUT_L - WALL / 2):
        cuts.append(b(10.0, WALL * 3, 1.6, OUT_W * 0.36, y,
                      FLOOR_T + INNER_H - 3.2))
    # pry notch
    cuts.append(b(9.0, WALL * 3, 2.2, OUT_W * 0.75, OUT_L - WALL / 2,
                  BASE_H - 0.6))
    return part.difference(trimesh.boolean.union(cuts))


def build_lid():
    plate = b(OUT_W, OUT_L, LID_T, OUT_W / 2, OUT_L / 2, LID_T / 2)

    add = []
    # inner lip (friction fit into the cavity)
    lip_h, lip_t, lip_clr = 3.0, 1.2, 0.25
    for sx, sy, cx, cy in (
        (INNER_W - 2 * lip_clr, lip_t, WALL + INNER_W / 2, WALL + lip_clr + lip_t / 2),
        (INNER_W - 2 * lip_clr, lip_t, WALL + INNER_W / 2, WALL + INNER_L - lip_clr - lip_t / 2),
        (lip_t, INNER_L - 2 * lip_clr, WALL + lip_clr + lip_t / 2, WALL + INNER_L / 2),
        (lip_t, INNER_L - 2 * lip_clr, WALL + INNER_W - lip_clr - lip_t / 2, WALL + INNER_L / 2),
    ):
        add.append(b(sx, sy, lip_h, cx, cy, LID_T + lip_h / 2))
    # snap hooks matching the base windows
    for y in (WALL + lip_clr + lip_t / 2, WALL + INNER_L - lip_clr - lip_t / 2):
        add.append(b(9.0, lip_t + 1.0, 1.4, OUT_W * 0.36, y, LID_T + lip_h - 0.9))
    # board hold-down nubs (press the PCB onto its rails)
    nub_h = INNER_H - (RAIL_H + PCB_T)
    for x in (BX + 3.0, BX + BOARD_W - 3.0):
        for y in (BY + 4.0, BY + BOARD_L - 4.0):
            add.append(b(3.0, 3.0, nub_h, WALL + x, WALL + y, LID_T + nub_h / 2))

    lid = trimesh.boolean.union([trimesh.util.concatenate([plate] + add)])

    cuts = []
    # IR LED: through-hole with a shallow countersink so the dome sits flush
    cuts.append(cyl(LED_D, LID_T * 4, WALL + LED_X, WALL + LED_Y, LID_T / 2))
    cuts.append(cyl(LED_D + 1.6, 0.8, WALL + LED_X, WALL + LED_Y, LID_T - 0.4))
    # IR receiver window: a slot cluster, so it reads as pattern not a hole
    total = IRWIN_SLOTS * IRWIN_W + (IRWIN_SLOTS - 1) * IRWIN_GAP
    for i in range(IRWIN_SLOTS):
        off = -total / 2 + IRWIN_W / 2 + i * (IRWIN_W + IRWIN_GAP)
        cuts.append(b(IRWIN_W, 9.0, LID_T * 4,
                      WALL + IR_X + off, WALL + IR_Y, LID_T / 2))
    # glow patch: thin the lid over the ESP module so the LED shines through
    cuts.append(b(15.0, 11.0, LID_T - GLOW_T,
                  WALL + BX + BOARD_W / 2, WALL + BY + 18.0,
                  (LID_T - GLOW_T) / 2))
    return lid.difference(trimesh.boolean.union(cuts))


def preview(meshes, path, elev=32, azim=-58):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    fig = plt.figure(figsize=(9, 7))
    ax = fig.add_subplot(111, projection="3d")
    light = np.array([0.35, -0.55, 0.76])
    for m, col in meshes:
        base_rgb = np.array(matplotlib.colors.to_rgb(col))
        n = m.face_normals
        shade = 0.45 + 0.55 * np.clip(n @ light, 0, 1)          # lambert + ambient
        cols = np.clip(base_rgb[None, :] * shade[:, None], 0, 1)
        ax.add_collection3d(Poly3DCollection(
            m.vertices[m.faces], facecolors=cols,
            edgecolor="none", linewidths=0, shade=False))
    allv = np.vstack([m.vertices for m, _ in meshes])
    c = allv.mean(0); r = (allv.max(0) - allv.min(0)).max() / 2 * 1.05
    ax.set_xlim(c[0] - r, c[0] + r); ax.set_ylim(c[1] - r, c[1] + r)
    ax.set_zlim(c[2] - r, c[2] + r)
    ax.view_init(elev=elev, azim=azim); ax.set_axis_off()
    try: ax.set_box_aspect((1, 1, 1))
    except Exception: pass
    fig.tight_layout(); fig.savefig(path, dpi=115); plt.close(fig)


if __name__ == "__main__":
    base, lid = build_base(), build_lid()
    base.export("eremote_case_base.stl")
    lid.export("eremote_case_lid.stl")

    print(f"outer  : {OUT_W:.1f} x {OUT_L:.1f} x {BASE_H + LID_T:.1f} mm")
    print(f"base   : watertight={base.is_watertight} vol={base.volume/1000:.1f} cm3")
    print(f"lid    : watertight={lid.is_watertight} vol={lid.volume/1000:.1f} cm3")

    preview([(base, "#e8e8e8")], "preview_base.png")
    lid_open = lid.copy(); lid_open.apply_translation((0, 0, BASE_H + 16))
    preview([(base, "#e8e8e8"), (lid_open, "#cfe6e2")], "preview_exploded.png")
    flip = lid.copy()
    flip.apply_transform(trimesh.transformations.rotation_matrix(np.pi, (1, 0, 0)))
    preview([(flip, "#cfe6e2")], "preview_lid_underside.png", elev=28, azim=-120)
