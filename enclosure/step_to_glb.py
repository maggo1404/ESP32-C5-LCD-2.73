#!/usr/bin/env python3
"""STEP -> GLB fuer den 3D-Viewer der Web-Seite (web/cad-lightbox.js).

    pip install cascadio==0.0.17 trimesh numpy
    python step_to_glb.py [lipo|lifepo4|usb|all]   # export/<variante>/*.step -> export/<variante>/glb/*.glb

Erzeugt assembly, body, front_bezel, battery_door (nicht bei usb), vent_clip (LiFePO4 zusaetzlich charger_lid) sowie
assembly_exploded (Teile entlang Z auseinandergezogen).  Einheit im GLB: Meter
(wie von cascadio geliefert), Achsen wie im Modell: +Z zeigt zum Betrachter
(Vorderseite), +Y oben.  Farben werden hier pro Teil gesetzt (die Einzel-STEPs
tragen keine Farben), damit Baugruppe und Einzelteile einheitlich aussehen.
"""
import os
import sys

import numpy as np
import trimesh
import cascadio

HERE = os.path.dirname(os.path.abspath(__file__))
VARIANTS = ["lipo", "lifepo4", "usb"]

TOL_LINEAR = 0.05   # mm
TOL_ANGULAR = 0.3   # rad

PARTS = ["assembly", "body", "front_bezel", "battery_door", "charger_lid", "vent_clip"]   # charger_lid nur LiFePO4

# sRGB 0-255; Metallic 0, Rauheit je Teil
COLORS = {
    "body":         ((158, 164, 172), 0.55),   # hellgrau
    "front_bezel":  (( 44,  48,  56), 0.45),   # anthrazit
    "battery_door": ((255, 122,  26), 0.50),   # orange
    "vent_clip":    (( 40, 120, 220), 0.50),   # blau
    "board_dummy":  (( 24, 100,  64), 0.35),   # dunkelgruen (Platine/Display)
    "charger_lid":  ((138,  79, 208), 0.50),   # violett (Modul-/Kabeldeckel, LiFePO4)
    "cell_dummy":   ((127, 184, 232), 0.30),   # hellblau (14500-Zelle, nur Baugruppe)
    "charger_dummy": ((200,  40,  60), 0.40),  # rot (Lademodul, nur Baugruppe)
}

# Explosionsansicht: Versatz in mm entlang Z (wie preview_exploded.png)
EXPLODE = {"body": 0, "board_dummy": 22, "front_bezel": 45, "battery_door": -25, "vent_clip": -30,
           "charger_lid": -25, "cell_dummy": -22, "charger_dummy": -25}


def srgb_to_linear(c):
    c /= 255.0
    return int(round(255 * (c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)))


def set_material(scene, gname, key):
    # glTF-baseColorFactor ist linear, COLORS sind sRGB
    (r, g, b), rough = (tuple(srgb_to_linear(v) for v in COLORS[key][0]), COLORS[key][1])
    scene.geometry[gname].visual = trimesh.visual.TextureVisuals(
        material=trimesh.visual.material.PBRMaterial(
            name=key, baseColorFactor=[r, g, b, 255],
            metallicFactor=0.0, roughnessFactor=rough))


def colorize(scene):
    """Baugruppe: Knotenname = Teilname (aus dem STEP-Assembly)."""
    for node in scene.graph.nodes_geometry:
        _, gname = scene.graph[node]
        if node in COLORS:
            set_material(scene, gname, node)


def convert(export, name):
    src = os.path.join(export, f"{name}.step")
    dst = os.path.join(export, "glb", f"{name}.glb")
    cascadio.step_to_glb(src, dst, TOL_LINEAR, TOL_ANGULAR)
    scene = trimesh.load(dst, force="scene")
    if name == "assembly":
        colorize(scene)
    else:   # Einzelteil: eine Geometrie, Farbe nach Dateiname
        for gname in scene.geometry:
            set_material(scene, gname, name)
    scene.export(dst)
    return scene


def exploded(scene):
    ex = scene.copy()
    for node in ex.graph.nodes_geometry:
        dz = EXPLODE.get(node)
        if dz is None:
            continue
        T, g = ex.graph[node]
        T = T.copy()
        T[2, 3] += dz * 0.001
        ex.graph.update(frame_to=node, matrix=T, geometry=g)
    return ex


def main(export=None):
    """export: Ordner einer Variante (z. B. export/lifepo4); ohne Angabe alle Varianten."""
    if export is None:
        arg = sys.argv[1] if len(sys.argv) > 1 else "all"
        names = VARIANTS if arg == "all" else [arg]
        for v in names:
            main(os.path.join(HERE, "export", v))
        return 0
    glb = os.path.join(export, "glb")
    os.makedirs(glb, exist_ok=True)
    asm = None
    for n in PARTS:
        if not os.path.exists(os.path.join(export, f"{n}.step")):
            continue
        sc = convert(export, n)
        if n == "assembly":
            asm = sc
    exploded(asm).export(os.path.join(glb, "assembly_exploded.glb"))
    print(export)
    for f in sorted(os.listdir(glb)):
        if f.endswith(".glb"):
            print(f"{f:28s} {os.path.getsize(os.path.join(glb, f)) / 1024:8.1f} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
