#!/usr/bin/env python3
"""Parametrisches Gehaeuse fuer Waveshare ESP32-C5-LCD-2.73 (CadQuery).

`python enclosure.py` erzeugt alles in enclosure/export/ :
  <teil>.step (Einbaulage) / <teil>.stl (Druckausrichtung), assembly.step,
  print_plate.3mf, preview_*.png, check_report.txt

KOORDINATEN (Baugruppe), alles in mm:
  Ursprung = Mitte der Platine.  +X = rechts, +Y = oben, jeweils bei Blick auf
  die VORDERSEITE (Display).  +Z zeigt zum Betrachter (vorne).  Z=0 = Rueckseite
  des Gehaeusekoerpers.
  Maßzeichnung (docs/reference/board-dimensions.jpg) zeigt die RUECKSEITE:
  dort ist links = +X (gespiegelt!).  Umrechnung Ruecksicht -> Modell:
      X = 34 - bx ,  Y = 34 - by   (bx,by von links-oben der Rueckansicht)
"""
import os, sys, math, zipfile, itertools
import cadquery as cq

# ======================= PARAMETER (alle Masse in mm) =======================
# --- Platine / Display (Zeichnung) ---
BOARD = 68.0            # Platine und LCD-Modul, quadratisch
BOARD_R = 3.0           # Eckenradius Platine (geschaetzt)
HOLE_GRID = 62.0        # Lochraster
HOLE_D = 2.2
ACTIVE = 48.96          # aktive Displayflaeche
# --- Dicken: bitte nachmessen ---
LCD_T = 4.0             # bitte nachmessen: LCD-Modul vor der Platine
PCB_T = 1.6             # bitte nachmessen: Platine
COMP_T = 4.0            # bitte nachmessen: hoechstes Bauteil auf der Rueckseite
USB_H = 3.2             # bitte nachmessen: Hoehe USB-C-Buchse (Rueckseite)
USB_W = 9.0             # Breite USB-C-Buchse
USB_OVERHANG = 0.8      # bitte nachmessen: Buchse steht ueber Platinenkante
# --- Toleranzen ---
WALL = 2.0
CLR = 0.3               # Spiel je Seite fuer Passungen (Platine in Gehaeuse)
DT_CLR = 0.2            # Spiel Schwalbenschwanz / Schiebedeckel
COMP_GAP = 0.4          # Luft zwischen Bauteilen und Boden
LCD_FRONT_GAP = 0.3     # komprimierbare Luft Glas <-> Rahmen-Unterseite (ggf. Schaumstoffband/Klebeband einlegen)
# --- Frontrahmen ---
BEZEL_T = 1.6           # Plattendicke
WIN = 50.5              # Fensterbreite/-hoehe (aktiv 48.96 + Spiel)
WIN_OFF_X = 0.0         # bitte nachmessen: Fenstermitte relativ Platinenmitte (+X = rechts, vorne)
WIN_OFF_Y = 0.0         #                   (+Y = oben)
WIN_CHAMFER = 1.0       # 45 Grad aussen
# --- Befestigung ---
# Platine wird von hinten durch kurze Zapfen (in den Platinenloechern) zentriert, liegt auf 4 Standoffs und wird
# vom Frontrahmen (Rand auf dem Glas, LCD_FRONT_GAP) geklemmt. Das LCD-Glas hat KEINE Loecher.
# Rahmen <-> Koerper: 4x M2 in den Ecken AUSSERHALB der 68x68-Platine.
OUTER = 78.0            # Aussenmass (Koerper und Rahmen)
R_OUT = 4.0             # Aussen-Eckenradius
SCREW_C = 35.8          # Schraubenmitte (+-X, +-Y)
USE_INSERTS = False     # True: M2-Gewindeeinsaetze (Bohrung 3.2 x 5), False: M2 selbstschneidend in 1.8
STANDOFF_OD = 5.0
PIN_D = 2.0             # Zapfen (Loch 2.2)
PIN_H = PCB_T - 0.2     # Zapfen hoechstens Platinendicke -> beruehrt LCD-Rueckseite nie
PILOT_D = 1.8
PILOT_DEPTH = 8.0
INSERT_D = 3.2
INSERT_DEPTH = 5.0
CSK_D = 4.4             # Senkung fuer M2-Senkkopf
SCREW_STD = [5, 6, 8, 10, 12, 14, 16, 20]
# --- Hohlraeume Rueckseite ---
REAR_RIM = 3.0          # umlaufender Rand
REAR_BAND = 9.0         # halbe Breite der vollen Baender um Schwalbenschwanz-Kreuz
REAR_BLOCK = 3.0        # Wand um Akkufach
REAR_RIB_X = 22.0       # Zusatzrippe (rechts, nicht ueber der Antenne)
RIB_T = 2.0
# --- Akku ---
BAT_L, BAT_W, BAT_T = 52.0, 36.0, 6.0   # Fach (Akku 503450 ca. 50x34x5, bitte nachmessen)
DOOR_T = 1.6
DOOR_LIP = 1.0          # Auflage des Deckels unter der Fachkante
BAT_CABLE_X, BAT_CABLE_Y = 34 - 57, 34 - 53   # BAT-JST in Modellkoordinaten (-23,-19)
CABLE_SLOT = (8.0, 9.0) # Breite X, Hoehe Y des Kabeldurchlasses im Boden
FLOOR_T = 1.5           # Trennboden Bauteilraum / Akkufach
# --- Anschluesse (Rueckansicht -> Modell umgerechnet) ---
USB_X = 0.0             # bitte nachmessen (Zeichnung: ca. -1)
USB_PLUG_W, USB_PLUG_H = 12.0, 7.0      # Stecker-Overmold
TF_X = 34 - 15.0        # TF-Slot Mitte Rueckansicht bx~15 -> X=+19
TF_W = 12.0
BTN_Y = [34 - 52.7, 34 - 61.0]  # Taster (beide Seiten) Y = -18.7, -27.0
BTN_TAB_L = 8.0         # Laenge Biegezunge
BTN_TAB_H = 4.4         # Hoehe Zunge (Z)
BTN_TAB_T = 1.2         # Dicke Zunge (X)
BTN_SLIT = 0.6
BTN_NUB = 0.05          # Abstand Stoessel-Spitze zur Platinenkante (Zunge wird beim Druck gebogen)
BTN_Z_OFF = -0.8        # Tastermitte relativ Platinenrueckseite (bitte nachmessen)
# --- Antenne (Ruecksicht bx 38..58, by 0..28 -> X -24..-4, Y 34..6) ---
ANT_RECESS = True       # Rueckseite ueber der Antenne ausgespart (dort nur Boden FLOOR_T)
ANT_X = (-24.0, -9.5)
ANT_Y = (20.5, 34.0)
# --- Klemm-Clip / Schwalbenschwanz ---
CLIP_CX, CLIP_CY = 0.0, 18.0    # Mittelpunkt des Clips auf der Rueckseite (freie Flaeche ueber Akkufach)
DT_NECK = 9.0           # Nutbreite an der Oberflaeche
DT_DEPTH = 2.8          # Nuttiefe (45 Grad Flanke, einseitig = druckbar ohne Stuetzen)
DT_XSHIFT = -1.0
CLIP_RIDGE_L = 26.0
CLIP_PLATE_Y = 28.0
CLIP_PLATE_X = 14.0
CLIP_PLATE_T = 3.0
SLAT_T = 2.0            # Lamellendicke Luftauslass (1.5 .. 3), bitte nachmessen
HOOK_DEPTH = 15.0
PRONG_T = 2.4
PRONG_W = 11.0
PRONG_PRELOAD = 0.4     # Ruhespalt = SLAT_T - PRONG_PRELOAD
# --- Druckbett ---
BED = 220.0

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "export")

# ======================= abgeleitete Masse =======================
INNER = BOARD + 2 * CLR
R_IN = BOARD_R + CLR
CAV_H = DOOR_T + DT_CLR + BAT_T            # Akkufach Hoehe (Z 0..CAV_H)
Z_FLOOR_TOP = CAV_H + FLOOR_T
Z_PCB_BACK = Z_FLOOR_TOP + COMP_T + COMP_GAP
Z_PCB_TOP = Z_PCB_BACK + PCB_T
Z_LCD_TOP = Z_PCB_TOP + LCD_T
Z_BODY_TOP = Z_LCD_TOP + LCD_FRONT_GAP
Z_TOTAL = Z_BODY_TOP + BEZEL_T
BOSS_HOLE_DEPTH = INSERT_DEPTH if USE_INSERTS else PILOT_DEPTH
SCREW_STACK = BEZEL_T
SCREW_LEN_RAW = SCREW_STACK + BOSS_HOLE_DEPTH - 0.5
SCREW_LEN = max(s for s in SCREW_STD if s <= SCREW_LEN_RAW)
POCKET_X0, POCKET_X1 = -BAT_L / 2, BAT_L / 2
POCKET_Y0 = -OUTER / 2 + WALL
POCKET_Y1 = POCKET_Y0 + BAT_W
DOOR_W = BAT_L + 2 * DOOR_LIP - 2 * DT_CLR
RECESS_HALF = DOOR_W / 2 + DT_CLR
RECESS_Y1 = POCKET_Y1 + DOOR_LIP
DOOR_Y0 = -OUTER / 2
DOOR_Y1 = RECESS_Y1 - 0.3
RECESS_H = DOOR_T + DT_CLR
Z_USB = Z_PCB_BACK - USB_H / 2
Z_BTN = Z_PCB_BACK + BTN_Z_OFF

# ======================= Helfer =======================
def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))

def cyl_z(x, y, z0, z1, r):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(r).extrude(z1 - z0)

def rbox(w, h, z0, z1, r, cx=0, cy=0):
    return (cq.Workplane("XY").workplane(offset=z0).center(cx, cy).rect(w, h)
            .extrude(z1 - z0).edges("|Z").fillet(r))

def prism_y(pts, y0, y1):
    """Polygon in (X,Z) entlang +Y extrudiert."""
    return cq.Workplane("XZ", origin=(0, y0, 0)).polyline(pts).close().extrude(-(y1 - y0))

def prism_x(pts, x0, x1):
    """Polygon in (Y,Z) entlang +X extrudiert."""
    return cq.Workplane("YZ", origin=(x0, 0, 0)).polyline(pts).close().extrude(x1 - x0)

def dt_groove_cutter(y_stop):
    """Nut-Schneidwerkzeug fuer Portrait (laeuft entlang Y), offen zur Oberkante.
    Einseitiger Schwalbenschwanz: Flanke -X senkrecht, Flanke +X 45 Grad hinterschnitten."""
    xs, n, d = DT_XSHIFT, DT_NECK / 2, DT_DEPTH
    pts = [(xs - n, -0.5), (xs + n - 0.5, -0.5), (xs + n + d, d), (xs - n, d)]
    return prism_y(pts, y_stop, OUTER / 2 + 1)

def groove_cutters():
    y_stop = CLIP_CY - CLIP_RIDGE_L / 2 - 0.3
    g = dt_groove_cutter(y_stop)
    g2 = g.rotate((CLIP_CX, CLIP_CY, 0), (CLIP_CX, CLIP_CY, 1), 90)   # Querformat
    return g.translate((CLIP_CX, 0, 0)), g2.translate((CLIP_CX, 0, 0))

# ======================= Teile =======================
def make_body():
    b = rbox(OUTER, OUTER, 0, Z_BODY_TOP, R_OUT)
    # Bauteilraum (Platine + LCD)
    b = b.cut(rbox(INNER, INNER, Z_FLOOR_TOP, Z_BODY_TOP + 1, R_IN))
    # Akkufach (von hinten) + Deckelfuehrung
    b = b.cut(box(POCKET_X0, POCKET_X1, POCKET_Y0, POCKET_Y1, -1, CAV_H))
    b = b.cut(box(-RECESS_HALF, RECESS_HALF, -OUTER / 2 - 1, RECESS_Y1, -1, RECESS_H))
    # Rasten-Mulden fuer Deckel (geschlossen)
    yb = DOOR_Y1 - 4.0
    for s in (-1, 1):
        b = b.cut(cyl_z(s * RECESS_HALF, yb, -1, RECESS_H, 0.8))
    # Kabeldurchlass zum BAT-Stecker
    cx, cy = BAT_CABLE_X, BAT_CABLE_Y
    b = b.cut(box(max(cx - CABLE_SLOT[0] / 2, POCKET_X0 + 0.0), cx + CABLE_SLOT[0] / 2,
                  cy - CABLE_SLOT[1] / 2, cy + CABLE_SLOT[1] / 2, CAV_H - 0.1, Z_FLOOR_TOP + 0.1))
    # Antennenaussparung (Rueckseite duenn)
    if ANT_RECESS:
        b = b.cut(box(ANT_X[0], ANT_X[1], ANT_Y[0], ANT_Y[1], -1, CAV_H))
    # Schwalbenschwanz-Nuten (Portrait + Querformat)
    for g in groove_cutters():
        b = b.cut(g)
    # Hohlraeume Rueckseite (offen nach hinten, Boden FLOOR_T bleibt), volle Baender um Nuten + Akkufach
    h = OUTER / 2 - REAR_RIM
    keep = box(-RECESS_HALF - REAR_BLOCK, RECESS_HALF + REAR_BLOCK, -OUTER, RECESS_Y1 + REAR_BLOCK, -2, CAV_H + 1)
    keep = keep.union(box(CLIP_CX - REAR_BAND, CLIP_CX + REAR_BAND, -OUTER, OUTER, -2, CAV_H + 1))
    keep = keep.union(box(-OUTER, OUTER, CLIP_CY - REAR_BAND, CLIP_CY + REAR_BAND, -2, CAV_H + 1))
    keep = keep.union(box(REAR_RIB_X - RIB_T / 2, REAR_RIB_X + RIB_T / 2, -OUTER, OUTER, -2, CAV_H + 1))
    void = box(-h, h, -h, h, -1, CAV_H).cut(keep)
    b = b.cut(void)
    # Antennenzone nochmals ganz frei (nur Boden)
    if ANT_RECESS:
        b = b.cut(box(ANT_X[0], ANT_X[1], ANT_Y[0], ANT_Y[1], -1, CAV_H))
        b = b.cut(box(ANT_X[0], ANT_X[1], ANT_Y[0], ANT_Y[1], -1, CAV_H))
    # Standoffs mit Zentrierzapfen (Platinenloecher), Platine liegt auf Standoff-Oberseite
    g = HOLE_GRID / 2
    for sx, sy in itertools.product((-1, 1), (-1, 1)):
        b = b.union(cyl_z(sx * g, sy * g, Z_FLOOR_TOP - 0.2, Z_PCB_BACK, STANDOFF_OD / 2))
        b = b.union(cyl_z(sx * g, sy * g, Z_PCB_BACK - 0.01, Z_PCB_BACK + PIN_H, PIN_D / 2))
    # Schraublocher Rahmen<->Koerper in den Ecken (ausserhalb der Platine)
    hole_d, hole_dp = (INSERT_D, INSERT_DEPTH) if USE_INSERTS else (PILOT_D, PILOT_DEPTH)
    for sx, sy in itertools.product((-1, 1), (-1, 1)):
        b = b.cut(cyl_z(sx * SCREW_C, sy * SCREW_C, Z_BODY_TOP - hole_dp, Z_BODY_TOP + 1, hole_d / 2))
    # USB-C (Unterkante)
    b = b.cut(box(USB_X - USB_PLUG_W / 2, USB_X + USB_PLUG_W / 2, -OUTER / 2 - 1, -BOARD / 2,
                  Z_USB - USB_PLUG_H / 2, Z_USB + USB_PLUG_H / 2))
    # TF-Karte (Oberkante)
    b = b.cut(box(TF_X - TF_W / 2, TF_X + TF_W / 2, BOARD / 2, OUTER / 2 + 1,
                  Z_PCB_BACK - 3.2, Z_PCB_BACK + 0.2))
    # Seitentaster: Biegezungen (Scharnier am anderen Ende, Zungen laufen auseinander)
    wall_in = INNER / 2
    wall_out = OUTER / 2
    tx0 = wall_out - BTN_TAB_T
    zc = Z_BTN
    h2 = BTN_TAB_H / 2
    tabs = []
    for i, by in enumerate(BTN_Y):
        if i == 0:   # oberer Taster: Scharnier oben, freies Ende unten
            free, hinge = by - 2.0, by - 2.0 + BTN_TAB_L
        else:        # unterer Taster: Scharnier unten, freies Ende oben
            free, hinge = by + 2.0, by + 2.0 - BTN_TAB_L
        tabs.append((by, min(free, hinge), max(free, hinge), free))
    for s in (-1, 1):
        for by, ya, yb_, free in tabs:
            def sbox(xa, xb, y0, y1, z0, z1):
                x0, x1 = sorted((s * xa, s * xb))
                return box(x0, x1, y0, y1, z0, z1)
            # innere Ausnehmung -> Zunge nur BTN_TAB_T dick
            b = b.cut(sbox(wall_in - 0.1, tx0, ya, yb_, zc - h2, zc + h2))
            # Schlitze oben/unten/freies Ende (durch die ganze Wand)
            b = b.cut(sbox(wall_in - 0.1, wall_out + 1, ya, yb_, zc + h2, zc + h2 + BTN_SLIT))
            b = b.cut(sbox(wall_in - 0.1, wall_out + 1, ya, yb_, zc - h2 - BTN_SLIT, zc - h2))
            if free == ya:
                b = b.cut(sbox(wall_in - 0.1, wall_out + 1, ya - BTN_SLIT, ya, zc - h2 - BTN_SLIT, zc + h2 + BTN_SLIT))
            else:
                b = b.cut(sbox(wall_in - 0.1, wall_out + 1, yb_, yb_ + BTN_SLIT, zc - h2 - BTN_SLIT, zc + h2 + BTN_SLIT))
            # Stoessel
            nx = BOARD / 2 + BTN_NUB
            b = b.union(sbox(nx, tx0 + 0.1, by - 1.5, by + 1.5, zc - 1.0, zc + 1.0))
    return b

def make_bezel():
    plate = rbox(OUTER, OUTER, Z_BODY_TOP, Z_TOTAL, R_OUT)
    bz = plate.faces(">Z").chamfer(0.6)
    # Fenster mit 45-Grad-Fase an der Aussenseite
    c = WIN_CHAMFER
    zt = Z_TOTAL
    win = (cq.Workplane("XY").workplane(offset=Z_BODY_TOP - 0.5).center(WIN_OFF_X, WIN_OFF_Y)
           .rect(WIN, WIN).extrude(BEZEL_T - c + 0.5))
    frustum = (cq.Workplane("XY").workplane(offset=zt - c).center(WIN_OFF_X, WIN_OFF_Y)
               .rect(WIN, WIN).workplane(offset=c + 0.01).rect(WIN + 2 * c + 0.02, WIN + 2 * c + 0.02).loft())
    bz = bz.cut(win).cut(frustum)
    # Senkschrauben
    g = SCREW_C
    for sx, sy in itertools.product((-1, 1), (-1, 1)):
        bz = bz.cut(cyl_z(sx * g, sy * g, Z_BODY_TOP - 1, zt + 1, 1.2))
        cone = (cq.Workplane("XY").workplane(offset=zt - 1.1).center(sx * g, sy * g).circle(1.2)
                .workplane(offset=1.2).circle(CSK_D / 2).loft())
        bz = bz.cut(cone)
    return bz

def make_door():
    d = box(-DOOR_W / 2, DOOR_W / 2, DOOR_Y0, DOOR_Y1, 0, DOOR_T)
    yb = DOOR_Y1 - 4.0
    fw, sl, flen = 2.0, 0.8, 16.0
    for s in (-1, 1):
        x_out = s * DOOR_W / 2
        x_in = s * (DOOR_W / 2 - fw)
        x_in2 = s * (DOOR_W / 2 - fw - sl)
        xa, xb = sorted((x_in, x_in2))
        d = d.cut(box(xa, xb, DOOR_Y1 - flen, DOOR_Y1 + 1, -1, DOOR_T + 1))
        d = d.union(cyl_z(x_out, yb, 0, DOOR_T, 0.5))
    # Fingermulde (Griff) aussen
    R = 14.0
    depth = 0.9
    # Rundmulde: Zylinder quer (Achse X), 22 mm breit
    sph = (cq.Workplane("YZ", origin=(-11, 0, 0)).center(DOOR_Y0 + 9.0, -(R - depth)).circle(R).extrude(22))
    d = d.cut(sph)
    return d

def make_clip():
    xs, n, d = DT_XSHIFT, DT_NECK / 2, DT_DEPTH
    c = DT_CLR
    k = c * math.sqrt(2)
    tip = d - 0.3
    z_pl_top = -c
    pts = [(xs - n + c, z_pl_top), (xs + n - 0.5 - k - c, z_pl_top),
           (xs + n - k + tip, tip), (xs - n + c, tip)]
    ridge = prism_y(pts, -CLIP_RIDGE_L / 2, CLIP_RIDGE_L / 2)
    plate = box(-CLIP_PLATE_X / 2, CLIP_PLATE_X / 2, -CLIP_PLATE_Y / 2, CLIP_PLATE_Y / 2,
                z_pl_top - CLIP_PLATE_T, z_pl_top + 0.01)
    z0 = z_pl_top - CLIP_PLATE_T
    g = SLAT_T - PRONG_PRELOAD
    D, t = HOOK_DEPTH, PRONG_T
    def prong(sgn):
        prof = [(g / 2, 0), (g / 2 + t, 0), (g / 2 + t, D), (g / 2 + 0.8, D),
                (g / 2 - 0.5, D - 1.2), (g / 2 - 0.5, D - 2.6), (g / 2, D - 3.6)]
        pts_yz = [(sgn * y, z0 + 0.01 - dd) for (y, dd) in prof]
        return prism_x(pts_yz, -PRONG_W / 2, PRONG_W / 2)
    clip = plate.union(ridge).union(prong(1)).union(prong(-1))
    clip = clip.translate((0, 0, 0))
    return clip

def make_board_dummy():
    z = Z_PCB_BACK
    pcb = rbox(BOARD, BOARD, z, Z_PCB_TOP, BOARD_R)
    lcd = rbox(BOARD, BOARD, Z_PCB_TOP, Z_LCD_TOP, BOARD_R)
    g = HOLE_GRID / 2
    pcb = pcb.cut(cyl_z(0, 0, z - 1, z, 0.01)) if False else pcb
    for sx, sy in itertools.product((-1, 1), (-1, 1)):
        pcb = pcb.cut(cyl_z(sx * g, sy * g, z - 1, Z_PCB_TOP, HOLE_D / 2))
    bd = pcb.union(lcd)
    comps = box(-BOARD / 2, BOARD / 2, -28, 28, z - COMP_T, z).union(box(-28, 28, -BOARD / 2, BOARD / 2, z - COMP_T, z))
    usb = box(USB_X - USB_W / 2, USB_X + USB_W / 2, -BOARD / 2 - USB_OVERHANG, -BOARD / 2 + 8,
              z - USB_H, z)
    return bd.union(comps).union(usb)

# ======================= Druckausrichtung =======================
def to_plate(wp, kind, cx, cy):
    s = wp
    if kind == "flipx":          # Teil umdrehen (Z -> -Z)
        s = s.rotate((0, 0, 0), (1, 0, 0), 180)
    elif kind == "clip":         # auf die Seite: +X zeigt nach oben
        s = s.rotate((0, 0, 0), (0, 1, 0), -90)
    bb = s.val().BoundingBox()
    return s.translate((cx - (bb.xmin + bb.xmax) / 2, cy - (bb.ymin + bb.ymax) / 2, -bb.zmin))

# ======================= Export / Pruefungen =======================
def tess(wp, tol=0.05, ang=0.2):
    v, t = wp.val().tessellate(tol, ang)
    return [(p.x, p.y, p.z) for p in v], t

def write_3mf(path, parts):
    objs, items = [], []
    for i, (name, wp) in enumerate(parts, 1):
        v, t = tess(wp)
        vx = "".join(f'<vertex x="{a:.4f}" y="{b:.4f}" z="{c:.4f}"/>' for a, b, c in v)
        tx = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in t)
        objs.append(f'<object id="{i}" name="{name}" type="model"><mesh><vertices>{vx}</vertices>'
                    f'<triangles>{tx}</triangles></mesh></object>')
        items.append(f'<item objectid="{i}"/>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02"><resources>'
             + "".join(objs) + "</resources><build>" + "".join(items) + "</build></model>")
    ct = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)

HEX = {"gray70": "#b3b3b3", "gray20": "#333333", "orange": "#ff9900", "steelblue": "#4682b4", "forestgreen": "#228b22"}

def render_png(items, path, elev, azim, title="", size=900):
    """Kleiner Software-Rasterizer (orthographisch, Z-Buffer) -> PNG."""
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    tris, cols = [], []
    for wp, color, off in items:
        v, t = tess(wp, 0.1, 0.3)
        V = np.array(v) + np.array(off)
        tri = V[np.array(t)]
        base = np.array(matplotlib.colors.to_rgb(HEX.get(color, color)))
        tris.append(tri); cols.append(np.tile(base, (len(tri), 1)))
    tri = np.vstack(tris); col = np.vstack(cols)
    el, az = math.radians(elev), math.radians(azim)
    # Kamera: Blickrichtung d, Rechts r, Oben u
    d = -np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)])
    r = np.cross(d, [0, 0, 1.0]); r /= np.linalg.norm(r)
    u = np.cross(r, d)
    P = np.stack([tri @ r, tri @ u, tri @ (-d)], axis=-1)    # x,y,Tiefe (gross = naeher)
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    n /= (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12)
    light = np.array([0.5, -0.6, 0.9]); light /= np.linalg.norm(light)
    sh = 0.3 + 0.7 * np.abs(n @ light)
    mn = P[..., :2].reshape(-1, 2).min(0); mx = P[..., :2].reshape(-1, 2).max(0)
    sc = (size - 20) / (mx - mn).max()
    W = H = size
    img = np.ones((H, W, 3)); zb = np.full((H, W), -1e9)
    for T, c, sd in zip(P, col, sh):
        xy = (T[:, :2] - mn) * sc + 10
        x0, x1 = int(max(xy[:, 0].min(), 0)), int(min(xy[:, 0].max() + 1, W - 1))
        y0, y1 = int(max(xy[:, 1].min(), 0)), int(min(xy[:, 1].max() + 1, H - 1))
        if x1 < x0 or y1 < y0:
            continue
        den = (xy[1, 1] - xy[2, 1]) * (xy[0, 0] - xy[2, 0]) + (xy[2, 0] - xy[1, 0]) * (xy[0, 1] - xy[2, 1])
        if abs(den) < 1e-9:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((xy[1, 1] - xy[2, 1]) * (gx - xy[2, 0]) + (xy[2, 0] - xy[1, 0]) * (gy - xy[2, 1])) / den
        l1 = ((xy[2, 1] - xy[0, 1]) * (gx - xy[2, 0]) + (xy[0, 0] - xy[2, 0]) * (gy - xy[2, 1])) / den
        l2 = 1 - l0 - l1
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        z = l0 * T[0, 2] + l1 * T[1, 2] + l2 * T[2, 2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        upd = m & (z > sub)
        sub[upd] = z[upd]
        img[y0:y1 + 1, x0:x1 + 1][upd] = c * sd
    img = img[::-1]
    plt.imsave(path, np.clip(img, 0, 1))

def vol_int(a, b):
    try:
        return a.intersect(b).val().Volume()
    except Exception:
        return 0.0

def main():
    os.makedirs(OUT, exist_ok=True)
    body, bezel, door, clip, dummy = make_body(), make_bezel(), make_door(), make_clip(), make_board_dummy()
    asm_parts = {"body": body, "front_bezel": bezel, "battery_door": door,
                 "vent_clip": clip, "board_dummy": dummy}
    # Klemm-Clip in Einbaulage: Mittelpunkt (CLIP_CX, CLIP_CY)
    clip_asm = clip.translate((CLIP_CX, CLIP_CY, 0))
    asm_parts["vent_clip"] = clip_asm

    # ---- Druckanordnung (220x220 Bett) ----
    plate_parts = [
        ("body", to_plate(body, "none", 45, 45)),
        ("front_bezel", to_plate(bezel, "flipx", 130, 45)),
        ("battery_door", to_plate(door, "flipx", 45, 120)),
        ("vent_clip", to_plate(clip, "clip", 130, 120)),
    ]

    rep = []
    def log(s):
        print(s); rep.append(s)

    log("== Parameter/Stack ==")
    log(f"Aussenmass Koerper/Rahmen: {OUTER:.1f} x {OUTER:.1f} mm, Gesamtdicke ohne Clip: {Z_TOTAL:.2f} mm")
    log(f"Z: Fach 0..{CAV_H:.2f}, Boden bis {Z_FLOOR_TOP:.2f}, Platine {Z_PCB_BACK:.2f}..{Z_PCB_TOP:.2f}, "
        f"LCD bis {Z_LCD_TOP:.2f}, Koerper-Oberkante {Z_BODY_TOP:.2f}, Rahmen bis {Z_TOTAL:.2f}")
    log(f"Schraube: M2 Senkkopf in den Ecken, Rahmen {SCREW_STACK:.1f} + Bohrung {BOSS_HOLE_DEPTH} -> M2x{SCREW_LEN} "
        f"({'Einsatz' if USE_INSERTS else 'selbstschneidend'})")

    log(f"Schrauben-Mitte +-{SCREW_C}, Abstand Bohrungsrand zur Platinentasche: "
        f"{math.hypot(SCREW_C-(INNER/2-R_IN), SCREW_C-(INNER/2-R_IN)) - R_IN - (INSERT_D if USE_INSERTS else PILOT_D)/2:.2f} mm")
    log("== Gueltigkeit ==")
    ok = True
    for n, p in asm_parts.items():
        sh = p.val()
        nsol = p.solids().size()
        valid = sh.isValid()
        bb = sh.BoundingBox()
        log(f"{n:13s} valid={valid} solids={nsol} vol={sh.Volume():9.1f} mm3 "
            f"bbox=({bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f})")
        ok &= bool(valid and nsol == 1)

    log("== Durchdringung (Baugruppe) ==")
    names = list(asm_parts)
    for a, b in itertools.combinations(names, 2):
        v = vol_int(asm_parts[a], asm_parts[b])
        flag = "OK" if v < 0.05 else "FEHLER"
        if v >= 0.05:
            ok = False
        log(f"{a:13s} x {b:13s} {v:8.3f} mm3 {flag}")

    log("== Druckplatte ==")
    bbs = []
    for n, p in plate_parts:
        bb = p.val().BoundingBox()
        bbs.append((n, bb))
        log(f"{n:13s} x {bb.xmin:6.1f}..{bb.xmax:6.1f} y {bb.ymin:6.1f}..{bb.ymax:6.1f} z 0..{bb.zmax:.1f}")
        if bb.xmin < 0 or bb.ymin < 0 or bb.xmax > BED or bb.ymax > BED or abs(bb.zmin) > 1e-6:
            ok = False; log("  -> ausserhalb Bett!")
    for (n1, a), (n2, b) in itertools.combinations(bbs, 2):
        if a.xmin < b.xmax and b.xmin < a.xmax and a.ymin < b.ymax and b.ymin < a.ymax:
            ok = False; log(f"Ueberlappung {n1}/{n2}")

    log("RESULT: " + ("ALLE PRUEFUNGEN OK" if ok else "FEHLER"))

    # ---- Dateien ----
    for n, p in asm_parts.items():
        cq.exporters.export(p, os.path.join(OUT, f"{n}.step"))
    for (n, p) in plate_parts:
        cq.exporters.export(p, os.path.join(OUT, f"{n}.stl"), tolerance=0.02, angularTolerance=0.1)
    cq.exporters.export(dummy, os.path.join(OUT, "board_dummy.stl"), tolerance=0.05)
    asm = cq.Assembly(name="assembly")
    cols = {"body": "gray70", "front_bezel": "gray20", "battery_door": "orange",
            "vent_clip": "steelblue", "board_dummy": "forestgreen"}
    for n, p in asm_parts.items():
        asm.add(p, name=n, color=cq.Color(cols[n]))
    asm.save(os.path.join(OUT, "assembly.step"))
    write_3mf(os.path.join(OUT, "print_plate.3mf"), plate_parts)

    # Previews
    items = [(asm_parts[n], cols[n], (0, 0, 0)) for n in ("body", "board_dummy", "front_bezel", "battery_door", "vent_clip")]
    render_png(items, os.path.join(OUT, "preview_front.png"), 30, -60, "Vorderseite")
    render_png(items, os.path.join(OUT, "preview_back.png"), -35, 140, "Rueckseite")
    ex = [(asm_parts["body"], cols["body"], (0, 0, 0)),
          (asm_parts["board_dummy"], cols["board_dummy"], (0, 0, 22)),
          (asm_parts["front_bezel"], cols["front_bezel"], (0, 0, 45)),
          (asm_parts["battery_door"], cols["battery_door"], (0, 0, -25)),
          (asm_parts["vent_clip"], cols["vent_clip"], (0, 0, -30))]
    render_png(ex, os.path.join(OUT, "preview_exploded.png"), 25, -50, "Explosion")
    render_png([(p, "tan", (0, 0, 0)) for _, p in plate_parts], os.path.join(OUT, "preview_plate.png"), 70, -90, "Druckplatte")
    render_png([(asm_parts["body"], "gray70", (0, 0, 0))], os.path.join(OUT, "preview_body_back.png"), -50, 130, "Koerper hinten")
    render_png([(asm_parts["body"], "gray70", (0, 0, 0))], os.path.join(OUT, "preview_body_front.png"), 40, -60, "Koerper vorne")
    with open(os.path.join(OUT, "check_report.txt"), "w") as f:
        f.write("\n".join(rep) + "\n")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
