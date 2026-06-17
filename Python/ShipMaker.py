# ================================================================
#  Ship Maker  v2.0  — Blender 5.0+ Add-on
#  Procedural sci-fi fighter ships
#  • Each part is a separate named object inside a Collection
#  • All geometry driven by a seed (same seed == same ship)
#  • Geometry Nodes "Surface Detail" modifier on every part
#  • Principled BSDF with Image Texture nodes pre-wired for PBR
#  • Dedicated BAKE and EXPORT buttons that are separate from generate
# ================================================================

bl_info = {
    "name":        "Ship Maker",
    "author":      "CPGE2026 / Daniel J. Hobson",
    "version":     (2, 0, 0),
    "blender":     (5, 0, 0),
    "location":    "View3D > Sidebar > Ship Maker",
    "description": "Procedurally generates sci-fi fighter ships as component collections",
    "category":    "Object",
}

import bpy
import bmesh
import math
import random
import os
from mathutils import Vector

# ================================================================
#  CONSTANTS
# ================================================================

TAG = "ShipMaker"   # prefix used on all generated data-blocks

# Colour palettes: (base_rgb, accent_rgb, glow_rgb)
PALETTES = [
    ((0.04, 0.05, 0.08), (0.76, 0.26, 0.04), (1.00, 0.42, 0.08)),  # gunmetal / orange
    ((0.03, 0.07, 0.13), (0.07, 0.52, 0.87), (0.16, 0.76, 1.00)),  # navy / cyan
    ((0.06, 0.06, 0.06), (0.52, 0.04, 0.76), (0.76, 0.08, 1.00)),  # black / purple
    ((0.09, 0.03, 0.03), (0.87, 0.07, 0.07), (1.00, 0.16, 0.04)),  # dark red / scarlet
]

SHIP_TYPES = ["Interceptor", "HeavyFighter", "AssaultCorvette"]

# ================================================================
#  UTILITIES
# ================================================================

def lerp(a, b, t):
    return a + (b - a) * t

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def make_rng(seed):
    return random.Random(seed)

def activate(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

def shade_smooth(obj):
    for poly in obj.data.polygons:
        poly.use_smooth = True
    obj.data.update()

def clean_geo(obj):
    activate(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.0007)
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    shade_smooth(obj)

def smart_uv(obj):
    activate(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.018)
    bpy.ops.object.mode_set(mode='OBJECT')

def new_mesh_obj(name, collection):
    mesh = bpy.data.meshes.new(name)
    obj  = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    return obj

def write_bm(bm, obj):
    bm.normal_update()
    bm.to_mesh(obj.data)
    obj.data.update()
    bm.free()

def try_face(bm, verts):
    try:
        bm.faces.new(verts)
    except ValueError:
        pass

def hex_ring(hw, hh):
    return [(math.cos(math.radians(a)) * hw,
             math.sin(math.radians(a)) * hh)
            for a in range(0, 360, 60)]

def oct_ring(hw, hh):
    return [(math.cos(math.radians(a)) * hw,
             math.sin(math.radians(a)) * hh)
            for a in range(0, 360, 45)]

def circle_pts(r, n, cx=0, cy=0):
    return [(cx + r * math.cos(2 * math.pi * i / n),
             cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


# ================================================================
#  SHIP PARAMETERS — all geometry driven by seed
# ================================================================

def make_params(seed, ship_type_idx):
    """
    Returns a dict of every geometric parameter for a ship.
    All values drawn from seeded RNG so the same seed always
    produces the same ship.
    """
    r = make_rng(seed)

    p = {
        "seed":      seed,
        "type_idx":  ship_type_idx,
        "type_name": SHIP_TYPES[ship_type_idx % len(SHIP_TYPES)],

        # ── Hull body ──────────────────────────────────────────────────
        "hull_len":         r.uniform(3.20, 5.60),
        "hull_w":           r.uniform(0.34, 0.64),
        "hull_h":           r.uniform(0.24, 0.50),
        "hull_taper_f":     r.uniform(0.05, 0.20),   # front taper ratio
        "hull_taper_r":     r.uniform(0.60, 0.90),   # rear taper ratio
        "hull_profile":     r.choice(["hex", "oct"]),

        # ── Nose cone ─────────────────────────────────────────────────
        "nose_len":         r.uniform(0.45, 1.35),
        "nose_segs":        r.choice([5, 6, 8, 10]),
        "nose_blunt":       r.uniform(0.02, 0.16),

        # ── Cockpit ───────────────────────────────────────────────────
        "ck_y_frac":        r.uniform(-0.14, 0.08),
        "ck_w_frac":        r.uniform(0.30, 0.54),
        "ck_h_frac":        r.uniform(0.30, 0.62),
        "ck_l_frac":        r.uniform(0.14, 0.28),

        # ── Wings ─────────────────────────────────────────────────────
        "wing_span":        r.uniform(1.90, 4.30),
        "wing_sweep_deg":   r.uniform(10.0, 64.0),
        "wing_taper":       r.uniform(0.08, 0.52),
        "wing_chord_root":  r.uniform(0.85, 2.30),
        "wing_dihedral_deg":r.uniform(-6.0, 16.0),
        "wing_thick_frac":  r.uniform(0.050, 0.120),
        "wing_y_frac":      r.uniform(0.04, 0.22),

        # ── Wing tips ─────────────────────────────────────────────────
        "tip_h":            r.uniform(0.10, 0.45),
        "tip_cant_deg":     r.uniform(60.0, 95.0),
        "tip_sweep_deg":    r.uniform(5.0, 32.0),

        # ── Dorsal fin ────────────────────────────────────────────────
        "fin_h":            r.uniform(0.40, 1.15),
        "fin_chord_root":   r.uniform(0.50, 1.40),
        "fin_taper":        r.uniform(0.10, 0.42),
        "fin_sweep_deg":    r.uniform(16.0, 58.0),
        "fin_thick":        r.uniform(0.04, 0.09),

        # ── Thrusters ─────────────────────────────────────────────────
        "thr_radius":       r.uniform(0.10, 0.20),
        "thr_len":          r.uniform(0.60, 1.25),
        "thr_x_frac":       r.uniform(0.38, 0.74),
        "thr_z_frac":       r.uniform(0.18, 0.44),
        "thr_nozzle_flare": r.uniform(1.10, 1.42),
        "thr_count":        r.choice([2, 2, 2, 4]),  # 75% chance of 2

        # ── Heavy Fighter extras ───────────────────────────────────────
        "armor_w_frac":     r.uniform(0.16, 0.30),
        "armor_h_frac":     r.uniform(0.50, 0.80),
        "armor_l_frac":     r.uniform(0.28, 0.48),

        # ── Assault Corvette extras ────────────────────────────────────
        "manta_w":          r.uniform(1.80, 3.20),
        "gun_count":        r.choice([2, 3, 4]),

        # ── Greebles ──────────────────────────────────────────────────
        "greeble_count":    r.randint(8, 22),
        "greeble_inset":    r.uniform(0.015, 0.055),
        "greeble_depth":    r.uniform(0.008, 0.038),

        # ── Geometry Nodes surface detail ─────────────────────────────
        "gn_noise_scale":   r.uniform(4.0, 18.0),
        "gn_displacement":  r.uniform(0.005, 0.022),

        # ── Material ──────────────────────────────────────────────────
        "palette_idx":      r.randint(0, len(PALETTES) - 1),
        "base_hue_shift":   r.uniform(-0.04, 0.04),
    }
    return p


# ================================================================
#  PART BUILDERS
# ================================================================

def _extruded_profile(bm, cross_xz, stations_yzxs):
    """
    Extrude a 2-D cross-section (list of (x,z) points) through a series of
    stations [(y, x_scale, z_scale), ...]. Returns list-of-rings (list of
    bm.Vert). Quads the rings together.
    """
    rings = []
    for (y, xs, zs) in stations_yzxs:
        ring = [bm.verts.new((cx * xs, y, cz * zs)) for (cx, cz) in cross_xz]
        rings.append(ring)

    n = len(cross_xz)
    for ri in range(len(rings) - 1):
        r0, r1 = rings[ri], rings[ri + 1]
        for i in range(n):
            j = (i + 1) % n
            try_face(bm, [r0[i], r0[j], r1[j], r1[i]])
    return rings


# ── Hull Body ──────────────────────────────────────────────────────────────────

def build_hull_body(p, col, mat):
    obj = new_mesh_obj("Hull_Body", col)
    bm  = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    tf, tr = p["hull_taper_f"], p["hull_taper_r"]

    cross = hex_ring(hw, hh) if p["hull_profile"] == "hex" else oct_ring(hw, hh)

    stations = [
        (-hl * 0.50, tf * 0.22, tf * 0.22),
        (-hl * 0.36, tf * 0.78, tf * 0.68),
        (-hl * 0.12, 1.00,      1.00),
        ( hl * 0.10, 1.02,      0.96),
        ( hl * 0.28, tr,        tr * 0.94),
        ( hl * 0.44, tr * 0.87, tr * 0.86),
        ( hl * 0.50, tr * 0.80, tr * 0.78),
    ]

    rings = _extruded_profile(bm, cross, stations)
    try_face(bm, list(reversed(rings[0])))   # nose-end cap

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Nose Cone ──────────────────────────────────────────────────────────────────

def build_nose_cone(p, col, mat):
    obj = new_mesh_obj("Nose_Cone", col)
    bm  = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    tf  = p["hull_taper_f"]
    nl  = p["nose_len"]
    ns  = p["nose_segs"]
    tip = p["nose_blunt"]

    base_r = hw * tf * 0.22
    base_pts = circle_pts(base_r, ns)

    rings = []
    for si in range(6):
        t  = si / 5.0
        y  = -hl * 0.50 - nl * t
        sc = lerp(1.0, tip / max(base_r, 0.001), t)
        ring = [bm.verts.new((bx * sc, y, bz * (hh / hw) * sc))
                for (bx, bz) in base_pts]
        rings.append(ring)

    tip_v = bm.verts.new((0.0, -hl * 0.50 - nl, 0.0))

    for ri in range(len(rings) - 1):
        r0, r1 = rings[ri], rings[ri + 1]
        for i in range(ns):
            j = (i + 1) % ns
            try_face(bm, [r0[i], r0[j], r1[j], r1[i]])

    for i in range(ns):
        j = (i + 1) % ns
        try_face(bm, [rings[-1][i], tip_v, rings[-1][j]])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Cockpit Canopy ─────────────────────────────────────────────────────────────

def build_cockpit(p, col, mat_glass):
    obj = new_mesh_obj("Cockpit", col)
    bm  = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    cy  = hl * p["ck_y_frac"]
    cw  = hw * p["ck_w_frac"]
    ch  = hh * p["ck_h_frac"]
    cl  = hl * p["ck_l_frac"]
    cz0 = hh

    segs   = 10
    vsteps = 7
    prev   = None

    for ri in range(vsteps + 1):
        t   = ri / vsteps
        ang = t * math.pi * 0.48
        rr  = cw * math.cos(ang)
        zz  = cz0 + ch * math.sin(ang)

        if rr < 0.004:
            tip_v = bm.verts.new((0.0, cy, zz))
            if prev:
                for j in range(segs):
                    k = (j + 1) % segs
                    try_face(bm, [prev[j], tip_v, prev[k]])
            break

        y_offset = (0.5 - ri / vsteps) * cl * 0.4
        ring = [bm.verts.new((rr * math.cos(2 * math.pi * j / segs),
                               cy + y_offset,
                               zz + rr * math.sin(2 * math.pi * j / segs) * 0.22))
                for j in range(segs)]

        if prev:
            for j in range(segs):
                k = (j + 1) % segs
                try_face(bm, [prev[j], prev[k], ring[k], ring[j]])
        else:
            try_face(bm, list(reversed(ring)))   # base cap
        prev = ring

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat_glass)
    return obj


# ── Wing (Left / Right) ────────────────────────────────────────────────────────

def build_wing(p, col, mat, side):
    """side = +1 (right) or -1 (left)"""
    name = "Wing_R" if side > 0 else "Wing_L"
    obj  = new_mesh_obj(name, col)
    bm   = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    span   = p["wing_span"]
    sweep  = math.radians(p["wing_sweep_deg"])
    taper  = p["wing_taper"]
    cr     = p["wing_chord_root"]
    ct     = cr * taper
    dihed  = math.radians(p["wing_dihedral_deg"])
    thick  = cr * p["wing_thick_frac"]
    y0     = hl * p["wing_y_frac"]

    rx     = side * hw
    tx     = side * (hw + span)
    ty0    = y0 + span * abs(side) * math.tan(sweep)
    tz_bot = -hh * 0.58
    tz_tip = tz_bot + span * math.tan(dihed)

    # 8 vertices: root (0-3) and tip (4-7)
    # 0: root LE top   1: root TE top   2: root TE bot   3: root LE bot
    # 4: tip  LE top   5: tip  TE top   6: tip  TE bot   7: tip  LE bot
    vv = [
        bm.verts.new((rx, y0,       tz_bot + thick * 0.50)),  # 0
        bm.verts.new((rx, y0 + cr,  tz_bot + thick * 0.14)),  # 1
        bm.verts.new((rx, y0 + cr,  tz_bot - thick * 0.08)),  # 2
        bm.verts.new((rx, y0,       tz_bot - thick * 0.10)),  # 3
        bm.verts.new((tx, ty0,      tz_tip + thick * taper * 0.50)),  # 4
        bm.verts.new((tx, ty0 + ct, tz_tip + thick * taper * 0.14)),  # 5
        bm.verts.new((tx, ty0 + ct, tz_tip - thick * taper * 0.08)),  # 6
        bm.verts.new((tx, ty0,      tz_tip - thick * taper * 0.10)),  # 7
    ]

    if side > 0:
        try_face(bm, [vv[0], vv[4], vv[5], vv[1]])   # top surface
        try_face(bm, [vv[3], vv[2], vv[6], vv[7]])   # bot surface
        try_face(bm, [vv[0], vv[3], vv[7], vv[4]])   # leading edge
        try_face(bm, [vv[1], vv[5], vv[6], vv[2]])   # trailing edge
        try_face(bm, [vv[0], vv[1], vv[2], vv[3]])   # root cap
        try_face(bm, [vv[4], vv[7], vv[6], vv[5]])   # tip cap
    else:
        try_face(bm, [vv[1], vv[5], vv[4], vv[0]])
        try_face(bm, [vv[7], vv[6], vv[2], vv[3]])
        try_face(bm, [vv[4], vv[7], vv[3], vv[0]])
        try_face(bm, [vv[2], vv[6], vv[5], vv[1]])
        try_face(bm, [vv[3], vv[2], vv[1], vv[0]])
        try_face(bm, [vv[5], vv[6], vv[7], vv[4]])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Wing Tip Vane ──────────────────────────────────────────────────────────────

def build_wing_tip(p, col, mat, side):
    name = "WingTip_R" if side > 0 else "WingTip_L"
    obj  = new_mesh_obj(name, col)
    bm   = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    span   = p["wing_span"]
    cr     = p["wing_chord_root"]
    taper  = p["wing_taper"]
    ct     = cr * taper
    thick  = cr * p["wing_thick_frac"] * taper
    sweep  = math.radians(p["wing_sweep_deg"])
    dihed  = math.radians(p["wing_dihedral_deg"])
    cant   = math.radians(p["tip_cant_deg"])
    tip_h  = p["tip_h"]
    ts_deg = math.radians(p["tip_sweep_deg"])
    y0     = hl * p["wing_y_frac"]

    tx  = side * (hw + span)
    ty0 = y0 + span * math.tan(sweep)
    tz  = -hh * 0.58 + span * math.tan(dihed)

    # Winglet root == wing tip: 4 points in XZ plane
    cant_z = tip_h * math.cos(cant)
    cant_x = side * tip_h * math.sin(cant)
    sweep_y = tip_h * math.tan(ts_deg)

    vv = [
        bm.verts.new((tx,           ty0,       tz + thick * 0.50)),  # 0 root LE top
        bm.verts.new((tx,           ty0 + ct,  tz + thick * 0.14)),  # 1 root TE top
        bm.verts.new((tx,           ty0 + ct,  tz - thick * 0.08)),  # 2 root TE bot
        bm.verts.new((tx,           ty0,       tz - thick * 0.10)),  # 3 root LE bot
        bm.verts.new((tx + cant_x,  ty0 + sweep_y,       tz + cant_z + thick * 0.25)),  # 4 tip LE top
        bm.verts.new((tx + cant_x,  ty0 + ct + sweep_y,  tz + cant_z + thick * 0.07)),  # 5 tip TE top
        bm.verts.new((tx + cant_x,  ty0 + ct + sweep_y,  tz + cant_z - thick * 0.04)),  # 6 tip TE bot
        bm.verts.new((tx + cant_x,  ty0 + sweep_y,       tz + cant_z - thick * 0.05)),  # 7 tip LE bot
    ]

    if side > 0:
        try_face(bm, [vv[0], vv[4], vv[5], vv[1]])
        try_face(bm, [vv[3], vv[2], vv[6], vv[7]])
        try_face(bm, [vv[0], vv[3], vv[7], vv[4]])
        try_face(bm, [vv[1], vv[5], vv[6], vv[2]])
        try_face(bm, [vv[4], vv[7], vv[6], vv[5]])
    else:
        try_face(bm, [vv[1], vv[5], vv[4], vv[0]])
        try_face(bm, [vv[7], vv[6], vv[2], vv[3]])
        try_face(bm, [vv[4], vv[7], vv[3], vv[0]])
        try_face(bm, [vv[2], vv[6], vv[5], vv[1]])
        try_face(bm, [vv[5], vv[6], vv[7], vv[4]])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Dorsal Fin ─────────────────────────────────────────────────────────────────

def build_dorsal_fin(p, col, mat):
    obj = new_mesh_obj("Fin_Dorsal", col)
    bm  = bmesh.new()

    hl, hh = p["hull_len"], p["hull_h"]
    fh   = p["fin_h"]
    fcr  = p["fin_chord_root"]
    fct  = fcr * p["fin_taper"]
    fsw  = math.radians(p["fin_sweep_deg"])
    ft   = p["fin_thick"]
    fy   = hl * 0.24   # start from

    sweep_off = fh * math.tan(fsw)
    ht = hh

    vv = [
        bm.verts.new(( ft * 0.5, fy,              ht)),             # 0 root LE right
        bm.verts.new(( ft * 0.5, fy + fcr,        ht)),             # 1 root TE right
        bm.verts.new((-ft * 0.5, fy + fcr,        ht)),             # 2 root TE left
        bm.verts.new((-ft * 0.5, fy,              ht)),             # 3 root LE left
        bm.verts.new(( ft * 0.3, fy + sweep_off,  ht + fh)),        # 4 tip LE right
        bm.verts.new(( ft * 0.3, fy + sweep_off + fct, ht + fh)),   # 5 tip TE right
        bm.verts.new((-ft * 0.3, fy + sweep_off + fct, ht + fh)),   # 6 tip TE left
        bm.verts.new((-ft * 0.3, fy + sweep_off,  ht + fh)),        # 7 tip LE left
    ]

    for fi in [[0,1,5,4],[7,6,2,3],[0,4,7,3],[1,2,6,5],[4,5,6,7],[3,2,1,0]]:
        try_face(bm, [vv[i] for i in fi])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Thruster Nacelle ───────────────────────────────────────────────────────────

def build_thruster(p, col, mat, side, z_side=1):
    """Build one engine nacelle. side=±1 for X offset, z_side=±1 for Z stacking."""
    name = f"Thruster_{'R' if side > 0 else 'L'}{'b' if z_side < 0 else ''}"
    obj  = new_mesh_obj(name, col)
    bm   = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    tr  = p["thr_radius"]
    tl  = p["thr_len"]
    tx  = side * hw * p["thr_x_frac"]
    tz  = -hh * p["thr_z_frac"] * z_side
    ty0 = hl * 0.28

    segs = 10

    # Nacelle body: tapered cylinder (wider at intake, narrower at nozzle-end)
    rings = []
    for si in range(5):
        t  = si / 4.0
        y  = ty0 + tl * t
        sr = tr * lerp(1.1, 0.72, t)   # intake wider, tail narrower
        ring = [bm.verts.new((tx + sr * math.cos(2 * math.pi * j / segs),
                               y,
                               tz + sr * math.sin(2 * math.pi * j / segs)))
                for j in range(segs)]
        rings.append(ring)

    for ri in range(len(rings) - 1):
        r0, r1 = rings[ri], rings[ri + 1]
        for j in range(segs):
            k = (j + 1) % segs
            try_face(bm, [r0[j], r0[k], r1[k], r1[j]])

    # Intake cap with centre vert
    ic = bm.verts.new((tx, ty0, tz))
    for j in range(segs):
        k = (j + 1) % segs
        try_face(bm, [rings[0][j], ic, rings[0][k]])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Thruster Nozzle (glowing ring) ────────────────────────────────────────────

def build_thruster_nozzle(p, col, mat_glow, side, z_side=1):
    name = f"ThrusterNozzle_{'R' if side > 0 else 'L'}{'b' if z_side < 0 else ''}"
    obj  = new_mesh_obj(name, col)
    bm   = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    tr    = p["thr_radius"]
    tl    = p["thr_len"]
    flare = p["thr_nozzle_flare"]
    tx    = side * hw * p["thr_x_frac"]
    tz    = -hh * p["thr_z_frac"] * z_side
    ty_n  = hl * 0.28 + tl        # nozzle position (tail of nacelle)

    segs = 10
    or_  = tr * 0.72 * flare      # outer ring radius
    ir_  = tr * 0.72 * 0.72       # inner ring radius
    depth = 0.08                  # nozzle ring thickness in Y

    outer_f, outer_b = [], []
    inner_f, inner_b = [], []
    for j in range(segs):
        ang = 2 * math.pi * j / segs
        ca, sa = math.cos(ang), math.sin(ang)
        outer_f.append(bm.verts.new((tx + or_ * ca, ty_n,         tz + or_ * sa)))
        outer_b.append(bm.verts.new((tx + or_ * ca, ty_n + depth, tz + or_ * sa)))
        inner_f.append(bm.verts.new((tx + ir_ * ca, ty_n,         tz + ir_ * sa)))
        inner_b.append(bm.verts.new((tx + ir_ * ca, ty_n + depth, tz + ir_ * sa)))

    for j in range(segs):
        k = (j + 1) % segs
        # Outer band
        try_face(bm, [outer_f[j], outer_b[j], outer_b[k], outer_f[k]])
        # Inner band
        try_face(bm, [inner_b[j], inner_f[j], inner_f[k], inner_b[k]])
        # Front annulus
        try_face(bm, [outer_f[j], outer_f[k], inner_f[k], inner_f[j]])
        # Back annulus
        try_face(bm, [outer_b[j], inner_b[j], inner_b[k], outer_b[k]])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat_glow)
    return obj


# ── Type-specific parts ────────────────────────────────────────────────────────

def build_sensor_array(p, col, mat):
    """Flat sensor plate on the nose — Interceptor specific."""
    obj = new_mesh_obj("Sensor_Array", col)
    bm  = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    y_pos = -hl * 0.52
    w, h, d = hw * 0.45, hh * 0.38, 0.06

    for pos in [(0, y_pos - d * 0.5, 0)]:
        x, y, z = pos
        verts = [
            bm.verts.new((x - w, y - d, z - h)), bm.verts.new((x + w, y - d, z - h)),
            bm.verts.new((x + w, y + d, z - h)), bm.verts.new((x - w, y + d, z - h)),
            bm.verts.new((x - w, y - d, z + h)), bm.verts.new((x + w, y - d, z + h)),
            bm.verts.new((x + w, y + d, z + h)), bm.verts.new((x - w, y + d, z + h)),
        ]
        for fi in [[0,3,2,1],[4,5,6,7],[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7]]:
            try_face(bm, [verts[i] for i in fi])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


def build_gun_barrels(p, col, mat):
    """Chin-mounted gun cluster — Assault Corvette specific."""
    obj = new_mesh_obj("Gun_Barrels", col)
    bm  = bmesh.new()

    hl, hh = p["hull_len"], p["hull_h"]
    count  = p["gun_count"]
    segs   = 6
    gr     = 0.028
    gl     = 0.55

    spacing = 0.08
    total_w = (count - 1) * spacing
    for i in range(count):
        gx = -total_w * 0.5 + i * spacing
        gy = -hl * 0.42
        gz = -hh * 0.52

        for si in range(4):
            t   = si / 3.0
            y   = gy + gl * t
            ring = [bm.verts.new((gx + gr * math.cos(2 * math.pi * j / segs),
                                   y,
                                   gz + gr * math.sin(2 * math.pi * j / segs)))
                    for j in range(segs)]
            if si > 0:
                prev = [bm.verts[-segs * (4 - si + 1) + j] for j in range(segs)]
                # just track them properly
            if si == 0:
                rings_i = [ring]
            else:
                rings_i.append(ring)

        for ri in range(len(rings_i) - 1):
            r0, r1 = rings_i[ri], rings_i[ri + 1]
            for j in range(segs):
                k = (j + 1) % segs
                try_face(bm, [r0[j], r0[k], r1[k], r1[j]])

        tip_v = bm.verts.new((gx, gy + gl + 0.03, gz))
        for j in range(segs):
            k = (j + 1) % segs
            try_face(bm, [rings_i[-1][j], tip_v, rings_i[-1][k]])
        try_face(bm, list(reversed(rings_i[0])))

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


def build_armor_cheeks(p, col, mat, side):
    """Side armour panels — Heavy Fighter specific."""
    name = "Armor_Cheek_R" if side > 0 else "Armor_Cheek_L"
    obj  = new_mesh_obj(name, col)
    bm   = bmesh.new()

    hl, hw, hh = p["hull_len"], p["hull_w"], p["hull_h"]
    aw = hw * p["armor_w_frac"]
    ah = hh * p["armor_h_frac"]
    al = hl * p["armor_l_frac"]
    ax = side * (hw + aw * 0.5)

    cross = hex_ring(aw, ah)
    stations = [
        (-al * 0.5 + hl * 0.02, 0.60, 0.70),
        ( al * 0.0,              1.00, 1.00),
        ( al * 0.5 + hl * 0.02, 0.80, 0.85),
    ]
    # Y shifted by hull centre
    stations_shifted = [(y + hl * 0.05, xs, zs) for (y, xs, zs) in stations]
    rings = _extruded_profile(bm, cross, stations_shifted)

    # Offset entire geometry in X
    for v in bm.verts:
        v.co.x += ax

    try_face(bm, list(reversed(rings[0])))
    try_face(bm, rings[-1])

    write_bm(bm, obj)
    clean_geo(obj)
    obj.data.materials.append(mat)
    return obj


# ── Greeble Pass ──────────────────────────────────────────────────────────────

def add_greebles(obj, seed, count, inset, depth):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.faces.ensure_lookup_table()

    large = sorted([f for f in bm.faces if f.calc_area() > 0.035],
                   key=lambda f: -f.calc_area())
    rng = make_rng(seed)

    done = 0
    for face in large:
        if done >= count:
            break
        ret = bmesh.ops.inset_individual(bm, faces=[face],
                                          thickness=inset, depth=0.0,
                                          use_even_offset=True)
        inner = ret.get("faces", [])
        if inner:
            bmesh.ops.translate(bm, verts=list(inner[0].verts),
                                vec=inner[0].normal * rng.uniform(depth * 0.3, depth))
            done += 1
        bm.faces.ensure_lookup_table()
        large = sorted([f for f in bm.faces if f.calc_area() > 0.035],
                       key=lambda f: -f.calc_area())

    bm.normal_update()
    bm.to_mesh(obj.data)
    obj.data.update()
    bm.free()


# ================================================================
#  GEOMETRY NODES — Surface Detail Modifier
# ================================================================

def get_or_create_surface_detail_gn():
    """
    Shared Geometry Nodes group: noise displacement along normals.
    Applied to every ship part so users can tune detail in the
    modifier stack without touching the source mesh.
    Inputs: Geometry, Noise Scale (Float), Displacement (Float)
    """
    GN_NAME = f"{TAG}_SurfaceDetail"
    if GN_NAME in bpy.data.node_groups:
        return bpy.data.node_groups[GN_NAME]

    ng    = bpy.data.node_groups.new(GN_NAME, 'GeometryNodeTree')
    ifc   = ng.interface
    nodes = ng.nodes
    links = ng.links

    # ── Interface sockets ──────────────────────────────────────────
    ifc.new_socket("Geometry",      in_out='INPUT',  socket_type='NodeSocketGeometry')
    s_scale = ifc.new_socket("Noise Scale",  in_out='INPUT',  socket_type='NodeSocketFloat')
    s_scale.default_value = 8.0
    s_scale.min_value = 0.1
    s_scale.max_value = 60.0
    s_disp  = ifc.new_socket("Displacement", in_out='INPUT',  socket_type='NodeSocketFloat')
    s_disp.default_value  = 0.012
    s_disp.min_value  = 0.0
    s_disp.max_value  = 0.5
    ifc.new_socket("Geometry",      in_out='OUTPUT', socket_type='NodeSocketGeometry')

    # ── Nodes ──────────────────────────────────────────────────────
    gi = nodes.new('NodeGroupInput');   gi.location  = (-900, 0)
    go = nodes.new('NodeGroupOutput');  go.location  = ( 700, 0)

    # Subdivide — adds geometry for the displacement to work on
    subdiv = nodes.new('GeometryNodeSubdivideMesh')
    subdiv.location = (-650, 0)
    subdiv.inputs['Level'].default_value = 1

    # Position for noise input
    pos = nodes.new('GeometryNodeInputPosition')
    pos.location = (-650, -260)

    # Noise texture
    noise = nodes.new('GeometryNodeTexNoise')
    noise.location = (-400, -160)
    noise.noise_dimensions = '3D'
    noise.inputs['Detail'].default_value    = 5.0
    noise.inputs['Roughness'].default_value = 0.65

    # Surface normal
    norm = nodes.new('GeometryNodeInputNormal')
    norm.location = (-400, -380)

    # Multiply noise fac by displacement strength
    math_mul = nodes.new('ShaderNodeMath')
    math_mul.operation = 'MULTIPLY'
    math_mul.location  = (-100, -230)

    # Scale normal vector by computed amount
    vec_scale = nodes.new('ShaderNodeVectorMath')
    vec_scale.operation = 'SCALE'
    vec_scale.location  = ( 150, -300)

    # Set Position with offset
    set_pos = nodes.new('GeometryNodeSetPosition')
    set_pos.location = ( 450, 0)

    # ── Links ──────────────────────────────────────────────────────
    links.new(gi.outputs[0],          subdiv.inputs['Mesh'])
    links.new(subdiv.outputs['Mesh'], set_pos.inputs['Geometry'])

    links.new(gi.outputs[1],          noise.inputs['Scale'])   # Noise Scale → noise
    links.new(pos.outputs['Position'], noise.inputs['Vector'])
    links.new(noise.outputs['Fac'],   math_mul.inputs[0])
    links.new(gi.outputs[2],          math_mul.inputs[1])     # Displacement → mult

    links.new(norm.outputs['Normal'],  vec_scale.inputs['Vector'])
    links.new(math_mul.outputs['Value'], vec_scale.inputs['Scale'])
    links.new(vec_scale.outputs['Vector'], set_pos.inputs['Offset'])

    links.new(set_pos.outputs['Geometry'], go.inputs[0])

    return ng


def apply_surface_detail(obj, noise_scale, displacement):
    """Add the shared Surface Detail GN modifier to a part object."""
    ng  = get_or_create_surface_detail_gn()
    mod = obj.modifiers.new(f"{TAG}_Detail", 'NODES')
    mod.node_group = ng

    # Set per-part parameter values via socket identifiers
    for item in ng.interface.items_tree:
        if not hasattr(item, 'identifier'):
            continue
        if item.in_out != 'INPUT':
            continue
        if item.name == 'Noise Scale':
            try: mod[item.identifier] = float(noise_scale)
            except Exception: pass
        elif item.name == 'Displacement':
            try: mod[item.identifier] = float(displacement)
            except Exception: pass


# ================================================================
#  MATERIALS
# ================================================================

def _n(nodes, ntype, loc):
    n = nodes.new(ntype)
    n.location = loc
    return n


def create_hull_material(name, palette, r):
    """
    Principled BSDF with:
      • Procedural noise + wave panel-line base colour
      • Metallic noise ramp, roughness noise ramp
      • Animatable EmissionStrength value node
      • Image Texture nodes pre-wired for baked PBR maps
        (placeholder until baked; disabled via mix until bake runs)
    Returns (material, emit_value_node, tex_node_dict)
    """
    base_rgb, accent_rgb, glow_rgb = palette

    # Apply per-ship hue shift
    sv = r.uniform(-0.04, 0.04)
    base_rgb   = tuple(clamp(c + sv, 0, 1) for c in base_rgb)
    accent_rgb = tuple(clamp(c + sv * 0.5, 0, 1) for c in accent_rgb)

    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    # Output + BSDF
    out  = _n(nodes, "ShaderNodeOutputMaterial", (1100, 0))
    bsdf = _n(nodes, "ShaderNodeBsdfPrincipled",  ( 700, 0))
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])

    # Texture coordinate + mapping
    coord   = _n(nodes, "ShaderNodeTexCoord", (-1000, 0))
    mapping = _n(nodes, "ShaderNodeMapping",  ( -800, 0))
    links.new(coord.outputs["Object"], mapping.inputs["Vector"])

    # ── Procedural base colour (active until textures are baked) ────────────
    noise_pan = _n(nodes, "ShaderNodeTexNoise", (-550, 300))
    noise_pan.inputs["Scale"].default_value      = r.uniform(8.0, 16.0)
    noise_pan.inputs["Detail"].default_value     = 4.0
    noise_pan.inputs["Roughness"].default_value  = 0.65
    links.new(mapping.outputs["Vector"], noise_pan.inputs["Vector"])

    wave = _n(nodes, "ShaderNodeTexWave", (-550, 80))
    wave.wave_type = 'BANDS'
    wave.inputs["Scale"].default_value      = r.uniform(22.0, 46.0)
    wave.inputs["Distortion"].default_value = 1.9
    links.new(mapping.outputs["Vector"], wave.inputs["Vector"])

    mix_pw = _n(nodes, "ShaderNodeMixRGB", (-280, 200))
    mix_pw.blend_type = 'MULTIPLY'
    mix_pw.inputs["Fac"].default_value = 0.26
    links.new(noise_pan.outputs["Color"], mix_pw.inputs["Color1"])
    links.new(wave.outputs["Color"],      mix_pw.inputs["Color2"])

    col_base   = _n(nodes, "ShaderNodeRGB", (-550, 520))
    col_accent = _n(nodes, "ShaderNodeRGB", (-550, 420))
    col_base.outputs[0].default_value   = (*base_rgb, 1.0)
    col_accent.outputs[0].default_value = (*accent_rgb, 1.0)

    mix_col = _n(nodes, "ShaderNodeMixRGB", (-60, 340))
    mix_col.blend_type = 'MIX'
    links.new(mix_pw.outputs["Color"],     mix_col.inputs["Fac"])
    links.new(col_base.outputs["Color"],   mix_col.inputs["Color1"])
    links.new(col_accent.outputs["Color"], mix_col.inputs["Color2"])

    # ── Image Texture nodes (pre-wired, placeholder until bake) ────────────
    # A MixRGB node lets us swap between procedural and baked
    img_col  = _n(nodes, "ShaderNodeTexImage", (-550, -200))
    img_col.name  = "Baked_BaseColor"
    img_col.label = "Baked_BaseColor"

    img_rough = _n(nodes, "ShaderNodeTexImage", (-550, -420))
    img_rough.name  = "Baked_Roughness"
    img_rough.label = "Baked_Roughness"
    img_rough.image_user.use_auto_refresh = True

    img_metal = _n(nodes, "ShaderNodeTexImage", (-550, -640))
    img_metal.name  = "Baked_Metallic"
    img_metal.label = "Baked_Metallic"

    img_norm  = _n(nodes, "ShaderNodeTexImage", (-550, -860))
    img_norm.name  = "Baked_Normal"
    img_norm.label = "Baked_Normal"

    norm_map = _n(nodes, "ShaderNodeNormalMap", (-250, -860))

    # Mix nodes — Fac=0 uses procedural, Fac=1 uses baked
    mix_base  = _n(nodes, "ShaderNodeMixRGB", (200, 300))
    mix_base.name  = "Mix_BaseColor"
    mix_base.label = "Mix_BaseColor"
    mix_base.inputs["Fac"].default_value = 0.0  # 0 = procedural until baked
    links.new(mix_col.outputs["Color"],    mix_base.inputs["Color1"])
    links.new(img_col.outputs["Color"],    mix_base.inputs["Color2"])
    links.new(mix_base.outputs["Color"],   bsdf.inputs["Base Color"])

    # ── Metallic (procedural until baked) ──────────────────────────────────
    noise_metal = _n(nodes, "ShaderNodeTexNoise", (-550, -50))
    noise_metal.inputs["Scale"].default_value  = r.uniform(3.5, 8.0)
    noise_metal.inputs["Detail"].default_value = 2.0
    links.new(mapping.outputs["Vector"], noise_metal.inputs["Vector"])

    ramp_metal = _n(nodes, "ShaderNodeValToRGB", (-280, -50))
    ramp_metal.color_ramp.elements[0].position = 0.28
    ramp_metal.color_ramp.elements[0].color    = (0.62, 0.62, 0.62, 1.0)
    ramp_metal.color_ramp.elements[1].position = 0.80
    ramp_metal.color_ramp.elements[1].color    = (1.00, 1.00, 1.00, 1.0)
    links.new(noise_metal.outputs["Fac"], ramp_metal.inputs["Fac"])

    mix_metal = _n(nodes, "ShaderNodeMixRGB", (200, -50))
    mix_metal.name  = "Mix_Metallic"
    mix_metal.label = "Mix_Metallic"
    mix_metal.inputs["Fac"].default_value = 0.0
    links.new(ramp_metal.outputs["Color"],  mix_metal.inputs["Color1"])
    links.new(img_metal.outputs["Color"],   mix_metal.inputs["Color2"])
    links.new(mix_metal.outputs["Color"],   bsdf.inputs["Metallic"])

    # ── Roughness (procedural until baked) ─────────────────────────────────
    noise_rough = _n(nodes, "ShaderNodeTexNoise", (-550, -260))
    noise_rough.inputs["Scale"].default_value     = r.uniform(15.0, 28.0)
    noise_rough.inputs["Detail"].default_value    = 6.0
    noise_rough.inputs["Roughness"].default_value = 0.74
    links.new(mapping.outputs["Vector"], noise_rough.inputs["Vector"])

    ramp_rough = _n(nodes, "ShaderNodeValToRGB", (-280, -260))
    ramp_rough.color_ramp.elements[0].color = (0.08, 0.08, 0.08, 1.0)
    ramp_rough.color_ramp.elements[1].color = (0.44, 0.44, 0.44, 1.0)
    links.new(noise_rough.outputs["Fac"], ramp_rough.inputs["Fac"])

    mix_rough = _n(nodes, "ShaderNodeMixRGB", (200, -260))
    mix_rough.name  = "Mix_Roughness"
    mix_rough.label = "Mix_Roughness"
    mix_rough.inputs["Fac"].default_value = 0.0
    links.new(ramp_rough.outputs["Color"],  mix_rough.inputs["Color1"])
    links.new(img_rough.outputs["Color"],   mix_rough.inputs["Color2"])
    links.new(mix_rough.outputs["Color"],   bsdf.inputs["Roughness"])

    # ── Normal map (baked only — no procedural fallback needed) ───────────
    links.new(img_norm.outputs["Color"],  norm_map.inputs["Color"])
    links.new(norm_map.outputs["Normal"], bsdf.inputs["Normal"])

    # ── Emission (animatable engine glow) ──────────────────────────────────
    col_glow  = _n(nodes, "ShaderNodeRGB", (400, -160))
    col_glow.outputs[0].default_value = (*glow_rgb, 1.0)
    links.new(col_glow.outputs["Color"], bsdf.inputs["Emission Color"])

    val_emit  = _n(nodes, "ShaderNodeValue", (400, -280))
    val_emit.name  = "EmissionStrength"
    val_emit.label = "EmissionStrength"
    val_emit.outputs[0].default_value = 0.0
    links.new(val_emit.outputs["Value"], bsdf.inputs["Emission Strength"])

    tex_nodes = {
        "BaseColor": img_col,
        "Roughness": img_rough,
        "Metallic":  img_metal,
        "Normal":    img_norm,
    }
    return mat, val_emit, tex_nodes


def create_cockpit_material(name, palette):
    _, _, glow_rgb = palette
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()

    out  = _n(nodes, "ShaderNodeOutputMaterial", (400, 0))
    bsdf = _n(nodes, "ShaderNodeBsdfPrincipled",  (0, 0))
    bsdf.inputs["Base Color"].default_value        = (*glow_rgb, 1.0)
    bsdf.inputs["Metallic"].default_value          = 0.0
    bsdf.inputs["Roughness"].default_value         = 0.03
    bsdf.inputs["Emission Color"].default_value    = (*glow_rgb, 1.0)
    bsdf.inputs["Emission Strength"].default_value = 0.35
    for key in ("Transmission Weight", "Transmission"):
        try:  bsdf.inputs[key].default_value = 0.88; break
        except KeyError: pass
    try:  bsdf.inputs["IOR"].default_value = 1.46
    except KeyError: pass
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    try:    mat.blend_method = 'BLEND'
    except AttributeError: pass
    return mat


def create_glow_material(name, palette):
    _, _, glow_rgb = palette
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()
    out  = _n(nodes, "ShaderNodeOutputMaterial", (400, 0))
    em   = _n(nodes, "ShaderNodeEmission", (0, 0))
    em.inputs["Color"].default_value    = (*glow_rgb, 1.0)
    em.inputs["Strength"].default_value = 4.0
    links.new(em.outputs["Emission"], out.inputs["Surface"])

    # Animatable strength node
    val = _n(nodes, "ShaderNodeValue", (0, -120))
    val.name  = "GlowStrength"
    val.label = "GlowStrength"
    val.outputs[0].default_value = 4.0
    links.new(val.outputs["Value"], em.inputs["Strength"])
    return mat, val


def wire_baked_textures(mat, baked_images):
    """
    After baking, load images into the pre-wired Image Texture nodes
    and flip the Mix node Fac to 1.0 to use the baked maps.
    baked_images = {suffix: bpy.types.Image}
    """
    nodes = mat.node_tree.nodes
    for suffix, img in baked_images.items():
        node_name = f"Baked_{suffix}"
        node = nodes.get(node_name)
        if node and img:
            node.image = img
            if suffix != "BaseColor":
                node.image.colorspace_settings.name = 'Non-Color'

    for mix_name in ("Mix_BaseColor", "Mix_Metallic", "Mix_Roughness"):
        mix = nodes.get(mix_name)
        if mix:
            mix.inputs["Fac"].default_value = 1.0


# ================================================================
#  ANIMATIONS
# ================================================================

def create_animations(obj, hull_mat, glow_mat_val, r):
    scene = bpy.context.scene

    # ── Emission pulse on hull material node tree ────────────────────────────
    hull_mat.node_tree.animation_data_create()
    idle_act = bpy.data.actions.new(f"{obj.name}_Idle")
    hull_mat.node_tree.animation_data.action = idle_act
    dp = 'nodes["EmissionStrength"].outputs[0].default_value'
    fc = idle_act.fcurves.new(data_path=dp)
    for (fr, v) in [(1, 0.0), (10, 2.8), (22, 0.5), (38, 3.4), (52, 0.3), (60, 0.0)]:
        kp = fc.keyframe_points.insert(fr, v)
        kp.interpolation = 'SINE'
    fc.modifiers.new(type='CYCLES')

    # ── Glow nozzle strength pulse ───────────────────────────────────────────
    if glow_mat_val:
        glow_mat = glow_mat_val.id_data
        glow_mat.node_tree.animation_data_create()
        g_act = bpy.data.actions.new(f"{obj.name}_GlowPulse")
        glow_mat.node_tree.animation_data.action = g_act
        gdp = 'nodes["GlowStrength"].outputs[0].default_value'
        gfc = g_act.fcurves.new(data_path=gdp)
        for (fr, v) in [(1, 2.0), (8, 6.0), (20, 2.5), (35, 7.0), (50, 2.0), (60, 2.0)]:
            kp = gfc.keyframe_points.insert(fr, v)
            kp.interpolation = 'SINE'
        gfc.modifiers.new(type='CYCLES')

    # ── Object: Patrol (roll + yaw) ──────────────────────────────────────────
    obj.animation_data_create()
    patrol = bpy.data.actions.new(f"{obj.name}_Patrol")
    roll_a = r.uniform(0.06, 0.14)
    yaw_a  = r.uniform(0.03, 0.08)
    fc_rx  = patrol.fcurves.new("rotation_euler", index=0)
    fc_rz  = patrol.fcurves.new("rotation_euler", index=2)
    for fr in range(0, 121, 10):
        t = fr / 120.0
        fc_rx.keyframe_points.insert(fr, roll_a * math.sin(2*math.pi*t)).interpolation = 'BEZIER'
        fc_rz.keyframe_points.insert(fr, yaw_a  * math.sin(2*math.pi*t*0.5)).interpolation = 'BEZIER'
    for fc in (fc_rx, fc_rz):
        fc.modifiers.new(type='CYCLES')

    # ── Object: Combat (sharp banking roll) ──────────────────────────────────
    combat = bpy.data.actions.new(f"{obj.name}_Combat")
    ba = r.uniform(0.24, 0.42)
    fc2_rx = combat.fcurves.new("rotation_euler", index=0)
    fc2_rz = combat.fcurves.new("rotation_euler", index=2)
    for (fr, rx, rz) in [(1,0,0),(8,ba*0.5,-ba),(22,ba,-ba*0.3),
                          (38,ba*0.3,ba*0.6),(54,-ba*0.4,ba),(70,-ba,0),(80,0,0)]:
        fc2_rx.keyframe_points.insert(fr, rx).interpolation = 'BEZIER'
        fc2_rz.keyframe_points.insert(fr, rz).interpolation = 'BEZIER'
    for fc in (fc2_rx, fc2_rz):
        fc.modifiers.new(type='CYCLES')

    # Push to NLA
    obj.animation_data.action = patrol
    nla_p = obj.animation_data.nla_tracks.new()
    nla_p.name = "Patrol"
    nla_p.strips.new("Patrol", 1, patrol).repeat = 4.0
    nla_c = obj.animation_data.nla_tracks.new()
    nla_c.name = "Combat"
    nla_c.strips.new("Combat", 1, combat).repeat = 4.0

    scene.frame_start = 1
    scene.frame_end   = 120


# ================================================================
#  TEXTURE BAKING
# ================================================================

def bake_ship_textures(parts, hull_mat, tex_size, tex_folder, ship_name):
    """
    Bake BaseColor, Roughness, Metallic, and Normal for the hull material.
    Bakes from the largest hull part (Hull_Body) as the representative mesh.
    Returns dict {suffix: bpy.types.Image}.
    """
    bpy.context.scene.render.engine  = 'CYCLES'
    bpy.context.scene.cycles.samples = 48
    bpy.context.scene.cycles.use_denoising = False

    os.makedirs(tex_folder, exist_ok=True)

    # Find hull body — use it as bake source
    hull_obj = next((o for o in parts if o.name.startswith("Hull_Body")), None)
    if hull_obj is None and parts:
        hull_obj = parts[0]
    if hull_obj is None:
        return {}

    activate(hull_obj)
    nodes = hull_mat.node_tree.nodes
    links = hull_mat.node_tree.links
    bsdf  = next((n for n in nodes if n.type == 'BSDF_PRINCIPLED'), None)

    results = {}

    bake_jobs = [
        ("DIFFUSE",   "BaseColor", (0.5, 0.5, 0.5, 1.0), {'COLOR'}),
        ("ROUGHNESS", "Roughness", (0.5, 0.5, 0.5, 1.0), set()),
        ("NORMAL",    "Normal",    (0.5, 0.5, 1.0, 1.0), set()),
    ]

    for btype, suffix, fill, pf in bake_jobs:
        img = bpy.data.images.new(f"{ship_name}_{suffix}",
                                   width=tex_size, height=tex_size)
        img.generated_color = fill

        img_node = nodes.new("ShaderNodeTexImage")
        img_node.image = img
        nodes.active   = img_node

        try:
            kwargs = {"type": btype, "use_clear": True}
            if pf:
                kwargs["pass_filter"] = pf
            bpy.ops.object.bake(**kwargs)
        except Exception as e:
            print(f"[ShipMaker] Bake warning ({btype}): {e}")

        fp = os.path.join(tex_folder, f"{ship_name}_{suffix}.png")
        img.filepath_raw = fp
        img.file_format  = 'PNG'
        img.save()
        results[suffix] = img
        nodes.remove(img_node)
        print(f"[ShipMaker] Baked {suffix} -> {fp}")

    # ── Metallic: route metallic → emission, bake EMIT ──────────────────────
    if bsdf and bsdf.inputs["Metallic"].links:
        metal_src = bsdf.inputs["Metallic"].links[0].from_socket
        tmp = links.new(metal_src, bsdf.inputs["Emission Strength"])

        img_m = bpy.data.images.new(f"{ship_name}_Metallic",
                                     width=tex_size, height=tex_size)
        img_node_m = nodes.new("ShaderNodeTexImage")
        img_node_m.image = img_m
        nodes.active     = img_node_m
        try:
            bpy.ops.object.bake(type='EMIT', use_clear=True)
        except Exception as e:
            print(f"[ShipMaker] Metallic bake warning: {e}")

        fp_m = os.path.join(tex_folder, f"{ship_name}_Metallic.png")
        img_m.filepath_raw = fp_m
        img_m.file_format  = 'PNG'
        img_m.save()
        results["Metallic"] = img_m
        links.remove(tmp)
        nodes.remove(img_node_m)
        print(f"[ShipMaker] Baked Metallic -> {fp_m}")

    return results


# ================================================================
#  FBX EXPORT
# ================================================================

def export_ship_fbx(collection, assets_folder, ship_name):
    """Select every object in the ship collection and export as FBX 7.4."""
    bpy.ops.object.select_all(action='DESELECT')
    for obj in collection.all_objects:
        if obj.type in {'MESH', 'EMPTY', 'ARMATURE'}:
            obj.select_set(True)

    if not any(o.select_get() for o in bpy.context.scene.objects):
        print("[ShipMaker] Export: nothing selected.")
        return None

    bpy.context.view_layer.objects.active = next(
        o for o in bpy.context.scene.objects if o.select_get()
    )

    fbx_path = os.path.join(assets_folder, f"{ship_name}.fbx")

    bpy.ops.export_scene.fbx(
        filepath                          = fbx_path,
        use_selection                     = True,
        global_scale                      = 1.0,
        apply_unit_scale                  = True,
        apply_scale_options               = 'FBX_SCALE_NONE',
        use_space_transform               = True,
        bake_space_transform              = False,
        object_types                      = {'MESH'},
        use_mesh_modifiers                = True,
        mesh_smooth_type                  = 'FACE',
        use_tspace                        = True,
        use_bake_anim                     = True,
        bake_anim_use_all_actions         = True,
        bake_anim_force_startend_keying   = True,
        bake_anim_step                    = 1.0,
        bake_anim_simplify_factor         = 1.0,
        path_mode                         = 'COPY',
        embed_textures                    = False,
        batch_mode                        = 'OFF',
        axis_forward                      = '-Z',
        axis_up                           = 'Y',
    )
    print(f"[ShipMaker] Exported -> {fbx_path}")
    return fbx_path


# ================================================================
#  SHIP ASSEMBLY
# ================================================================

def assemble_ship(ship_idx, seed, tex_size, assets_folder, do_bake, do_export):
    type_idx  = ship_idx % len(SHIP_TYPES)
    p         = make_params(seed, type_idx)
    ship_name = f"Ship_{p['type_name']}_{seed:05d}"
    palette   = PALETTES[p["palette_idx"]]

    print(f"[ShipMaker] Building {ship_name} (type={p['type_name']}, seed={seed})")

    # ── Collection ────────────────────────────────────────────────────────────
    col = bpy.data.collections.new(ship_name)
    bpy.context.scene.collection.children.link(col)

    # ── Materials ─────────────────────────────────────────────────────────────
    r_mat = make_rng(seed + 9999)
    hull_mat, emit_node, tex_nodes = create_hull_material(
        f"Mat_{ship_name}_Hull", palette, r_mat)
    glass_mat = create_cockpit_material(f"Mat_{ship_name}_Glass", palette)
    glow_mat, glow_val = create_glow_material(f"Mat_{ship_name}_Glow", palette)

    # ── Parts shared by all types ─────────────────────────────────────────────
    hull_body = build_hull_body(p, col, hull_mat)
    nose_cone = build_nose_cone(p, col, hull_mat)
    cockpit   = build_cockpit(p, col, glass_mat)
    wing_r    = build_wing(p, col, hull_mat, +1)
    wing_l    = build_wing(p, col, hull_mat, -1)
    tip_r     = build_wing_tip(p, col, hull_mat, +1)
    tip_l     = build_wing_tip(p, col, hull_mat, -1)
    dorsal    = build_dorsal_fin(p, col, hull_mat)

    thr_count = p["thr_count"]
    thrusters = []
    nozzles   = []
    if thr_count == 2:
        thrusters += [build_thruster(p, col, hull_mat, +1),
                      build_thruster(p, col, hull_mat, -1)]
        nozzles   += [build_thruster_nozzle(p, col, glow_mat, +1),
                      build_thruster_nozzle(p, col, glow_mat, -1)]
    else:  # 4
        for s in (+1, -1):
            thrusters += [build_thruster(p, col, hull_mat, s, +1),
                          build_thruster(p, col, hull_mat, s, -1)]
            nozzles   += [build_thruster_nozzle(p, col, glow_mat, s, +1),
                          build_thruster_nozzle(p, col, glow_mat, s, -1)]

    # ── Type-specific extras ──────────────────────────────────────────────────
    extra_parts = []
    if p["type_name"] == "Interceptor":
        extra_parts.append(build_sensor_array(p, col, hull_mat))
    elif p["type_name"] == "HeavyFighter":
        extra_parts.append(build_armor_cheeks(p, col, hull_mat, +1))
        extra_parts.append(build_armor_cheeks(p, col, hull_mat, -1))
    elif p["type_name"] == "AssaultCorvette":
        extra_parts.append(build_gun_barrels(p, col, hull_mat))

    all_parts = ([hull_body, nose_cone, cockpit, wing_r, wing_l,
                  tip_r, tip_l, dorsal] + thrusters + nozzles + extra_parts)

    # ── Greebles on hull parts only ───────────────────────────────────────────
    hull_parts = [hull_body, nose_cone, wing_r, wing_l,
                  tip_r, tip_l, dorsal] + thrusters + extra_parts
    for i, hobj in enumerate(hull_parts):
        add_greebles(hobj,
                     seed=seed + i * 777,
                     count=max(2, p["greeble_count"] // len(hull_parts)),
                     inset=p["greeble_inset"],
                     depth=p["greeble_depth"])

    # ── Geometry Nodes: Surface Detail modifier on every mesh part ────────────
    for part in all_parts:
        apply_surface_detail(part,
                             p["gn_noise_scale"],
                             p["gn_displacement"])

    # ── UV unwrap all parts ───────────────────────────────────────────────────
    for part in all_parts:
        smart_uv(part)

    # ── Animations (keyed on hull body as root, materials animated) ───────────
    create_animations(hull_body, hull_mat, glow_val,
                      make_rng(seed + 3333))

    # ── Bake textures ─────────────────────────────────────────────────────────
    if do_bake:
        tex_folder   = os.path.join(assets_folder, "textures")
        hull_parts_f = [o for o in all_parts if o.data.materials and
                        o.data.materials[0] == hull_mat]
        baked = bake_ship_textures(hull_parts_f, hull_mat,
                                    tex_size, tex_folder, ship_name)
        wire_baked_textures(hull_mat, baked)

    # ── FBX export ────────────────────────────────────────────────────────────
    if do_export:
        os.makedirs(assets_folder, exist_ok=True)
        export_ship_fbx(col, assets_folder, ship_name)

    print(f"[ShipMaker] Done: {ship_name}  ({len(all_parts)} parts)")
    return col


# ================================================================
#  ADDON PROPERTIES
# ================================================================

class ShipMakerProperties(bpy.types.PropertyGroup):

    assets_folder: bpy.props.StringProperty(
        name        = "Assets Folder",
        description = "Absolute path to output folder (FBX files go here)",
        default     = "//Assets",
        subtype     = 'DIR_PATH',
    )  # type: ignore

    num_ships: bpy.props.IntProperty(
        name="Ship Count", default=3, min=1, max=20,
        description="Number of ships to generate",
    )  # type: ignore

    random_seed: bpy.props.IntProperty(
        name="Base Seed", default=42, min=0, max=999999,
        description="Each ship gets seed + N*1337 so every ship is unique",
    )  # type: ignore

    ship_type: bpy.props.EnumProperty(
        name="Ship Type",
        items=[
            ('RANDOM',      "Random",          "Pick a type per ship"),
            ('INTERCEPTOR', "Interceptor",     "Sleek needle-nose fighter"),
            ('HEAVY',       "Heavy Fighter",   "Wide armoured brawler"),
            ('ASSAULT',     "Assault Corvette","Manta flying-wing"),
        ],
        default='RANDOM',
    )  # type: ignore

    texture_size: bpy.props.EnumProperty(
        name="Texture Size",
        items=[
            ('1024', "1024", "1024 x 1024 px"),
            ('2048', "2048", "2048 x 2048 px (recommended)"),
            ('4096', "4096", "4096 x 4096 px"),
        ],
        default='2048',
    )  # type: ignore

    # Stored ship name for post-generate bake/export
    last_ship_name: bpy.props.StringProperty(default="")  # type: ignore


# ================================================================
#  OPERATORS
# ================================================================

class SHIPMAKER_OT_Generate(bpy.types.Operator):
    bl_idname     = "shipmaker.generate"
    bl_label      = "Generate Ships"
    bl_description = "Build ship geometry, materials, Geometry Nodes, and animations"
    bl_options    = {'REGISTER', 'UNDO'}

    def execute(self, context):
        props  = context.scene.ship_maker_props
        folder = bpy.path.abspath(props.assets_folder)
        if not folder:
            self.report({'ERROR'}, "Set an Assets Folder first.")
            return {'CANCELLED'}

        type_map = {'INTERCEPTOR': 0, 'HEAVY': 1, 'ASSAULT': 2}
        names = []

        for i in range(props.num_ships):
            seed = props.random_seed + i * 1337
            if props.ship_type == 'RANDOM':
                tidx = (i + seed // 137) % len(SHIP_TYPES)
            else:
                tidx = type_map.get(props.ship_type, i % len(SHIP_TYPES))
            try:
                col = assemble_ship(i, seed, int(props.texture_size),
                                    folder, False, False)
                names.append(col.name)
                props.last_ship_name = col.name
            except Exception as exc:
                import traceback; traceback.print_exc()
                self.report({'WARNING'}, f"Ship {i+1} failed: {exc}")

        self.report({'INFO'}, f"Generated: {', '.join(names)}")
        return {'FINISHED'}


class SHIPMAKER_OT_BakeTextures(bpy.types.Operator):
    bl_idname     = "shipmaker.bake_textures"
    bl_label      = "Bake PBR Textures"
    bl_description = (
        "Bake BaseColor / Roughness / Metallic / Normal for all ShipMaker "
        "hull materials in the scene. Requires Cycles. This may take several minutes."
    )
    bl_options = {'REGISTER'}

    def execute(self, context):
        props      = context.scene.ship_maker_props
        folder     = bpy.path.abspath(props.assets_folder)
        tex_folder = os.path.join(folder, "textures")
        tex_size   = int(props.texture_size)

        if not folder:
            self.report({'ERROR'}, "Set an Assets Folder first.")
            return {'CANCELLED'}

        # Find all ShipMaker hull materials in the scene
        baked_count = 0
        for col in bpy.data.collections:
            if not col.name.startswith("Ship_"):
                continue
            hull_mat = next(
                (m for m in bpy.data.materials
                 if m.name == f"Mat_{col.name}_Hull"), None
            )
            if hull_mat is None:
                continue

            hull_objs = [o for o in col.all_objects
                         if o.type == 'MESH' and o.data.materials
                         and o.data.materials[0] == hull_mat]
            if not hull_objs:
                continue

            baked = bake_ship_textures(hull_objs, hull_mat,
                                        tex_size, tex_folder, col.name)
            wire_baked_textures(hull_mat, baked)
            baked_count += 1
            print(f"[ShipMaker] Baked {col.name}")

        if baked_count == 0:
            self.report({'WARNING'},
                        "No ShipMaker ship collections found. Generate ships first.")
        else:
            self.report({'INFO'}, f"Baked textures for {baked_count} ship(s) -> {tex_folder}")
        return {'FINISHED'}


class SHIPMAKER_OT_ExportFBX(bpy.types.Operator):
    bl_idname     = "shipmaker.export_fbx"
    bl_label      = "Export Ships FBX"
    bl_description = (
        "Export all ShipMaker ship collections as FBX 7.4 binary "
        "to the Assets Folder"
    )
    bl_options = {'REGISTER'}

    def execute(self, context):
        props  = context.scene.ship_maker_props
        folder = bpy.path.abspath(props.assets_folder)
        if not folder:
            self.report({'ERROR'}, "Set an Assets Folder first.")
            return {'CANCELLED'}

        os.makedirs(folder, exist_ok=True)
        exported = []

        for col in bpy.data.collections:
            if not col.name.startswith("Ship_"):
                continue
            path = export_ship_fbx(col, folder, col.name)
            if path:
                exported.append(col.name)

        if not exported:
            self.report({'WARNING'},
                        "No ShipMaker collections found. Generate ships first.")
        else:
            self.report({'INFO'},
                        f"Exported {len(exported)} ship(s) to {folder}: {', '.join(exported)}")
        return {'FINISHED'}


# ================================================================
#  UI PANEL
# ================================================================

class SHIPMAKER_PT_Panel(bpy.types.Panel):
    bl_label       = "Ship Maker"
    bl_idname      = "SHIPMAKER_PT_panel"
    bl_space_type  = "VIEW_3D"
    bl_region_type = "UI"
    bl_category    = "Ship Maker"

    def draw(self, context):
        layout = self.layout
        props  = context.scene.ship_maker_props

        # ── Output folder ─────────────────────────────────────────────────────
        box = layout.box()
        box.label(text="Output Folder", icon='FILE_FOLDER')
        box.prop(props, "assets_folder", text="")

        # ── Ship generation ───────────────────────────────────────────────────
        box2 = layout.box()
        box2.label(text="Ship Generation", icon='MESH_ICOSPHERE')
        row = box2.row(align=True)
        row.prop(props, "num_ships",   text="Count")
        row.prop(props, "random_seed", text="Seed")
        box2.prop(props, "ship_type")
        box2.separator()
        big = box2.row()
        big.scale_y = 1.8
        big.operator("shipmaker.generate",
                     text="  Generate Ships", icon='PLAY')

        # ── Texture baking ───────────────────────────────────────────────────
        box3 = layout.box()
        box3.label(text="PBR Texture Baking", icon='TEXTURE')
        box3.label(text="Resolution:", icon='NONE')
        box3.prop(props, "texture_size", text="")
        box3.label(text="Engine: Cycles  |  Samples: 48", icon='INFO')
        bake_row = box3.row()
        bake_row.scale_y = 1.6
        bake_row.operator("shipmaker.bake_textures",
                           text="  Bake PBR Textures", icon='RENDER_STILL')

        # ── FBX Export ────────────────────────────────────────────────────────
        box4 = layout.box()
        box4.label(text="FBX Export", icon='EXPORT')
        box4.label(text="Format: FBX 7.4 binary", icon='INFO')
        exp_row = box4.row()
        exp_row.scale_y = 1.6
        exp_row.operator("shipmaker.export_fbx",
                          text="  Export All Ships FBX", icon='FILE_3D')


# ================================================================
#  REGISTER
# ================================================================

_CLASSES = [
    ShipMakerProperties,
    SHIPMAKER_OT_Generate,
    SHIPMAKER_OT_BakeTextures,
    SHIPMAKER_OT_ExportFBX,
    SHIPMAKER_PT_Panel,
]


def register():
    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Scene.ship_maker_props = bpy.props.PointerProperty(
        type=ShipMakerProperties)


def unregister():
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)
    try:
        del bpy.types.Scene.ship_maker_props
    except AttributeError:
        pass


if __name__ == "__main__":
    register()
