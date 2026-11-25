#!/usr/bin/env python3
"""Parametrisches Gehaeuse fuer Waveshare ESP32-C5-LCD-2.73 (CadQuery).

    python enclosure.py                         # LiPo 503450 -> export/lipo/
    python enclosure.py --battery lifepo4_14500 # LiFePO4 14500 + Lademodul -> export/lifepo4/
    python enclosure.py --all                   # beide Varianten nacheinander
  (alternativ Umgebungsvariable ENC_BATTERY=lipo_503450|lifepo4_14500)

Je Variante entsteht in export/<lipo|lifepo4>/ :
  <teil>.step (Einbaulage) / <teil>.stl (Druckausrichtung), assembly.step,
  print_plate.3mf, preview_*.png, check_report.txt, glb/ (3D-Viewer)

KOORDINATEN (Baugruppe), alles in mm:
  Ursprung = Mitte der Platine.  +X = rechts, +Y = oben, jeweils bei Blick auf
  die VORDERSEITE (Display).  +Z zeigt zum Betrachter (vorne).  Z=0 = Rueckseite
  des Gehaeusekoerpers.
  Maßzeichnung (docs/reference/board-dimensions.jpg) zeigt die RUECKSEITE:
  dort ist links = +X (gespiegelt!).  Umrechnung Ruecksicht -> Modell:
      X = 34 - bx ,  Y = 34 - by   (bx,by von links-oben der Rueckansicht)
"""
import os, sys, math, zipfile, itertools, subprocess
import cadquery as cq

# ======================= VARIANTE (Akkutyp) =======================
# lipo_503450    : LiPo 503450 flach (52 x 36 x 6), Akku wird ueber den Onboard-Lader (ETA6098) geladen.
# lifepo4_14500  : LiFePO4 14500 (AA-Format) laengs im Fach + internes Lademodul (3,6 V), Onboard-Lader per
#                  Entfernen von L1 DEAKTIVIEREN (siehe enclosure/README.md, Abschnitt LiFePO4-Variante)!
BATTERY_ALIASES = {"lipo": "lipo_503450", "lifepo4": "lifepo4_14500"}

def _select_battery(argv):
    sel = os.environ.get("ENC_BATTERY", "lipo_503450")
    for i, a in enumerate(argv):
        if a == "--battery" and i + 1 < len(argv):
            sel = argv[i + 1]
        elif a.startswith("--battery="):
            sel = a.split("=", 1)[1]
    return BATTERY_ALIASES.get(sel, sel)

BATTERY_TYPE = _select_battery(sys.argv[1:])

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
COMP_GAP = 0.3          # Luft zwischen Bauteilen und Boden
LCD_FRONT_GAP = 0.5     # Luft Glas <-> Rahmen-Unterseite: fuer 0.8-1.0 mm selbstklebendes Schaumstoff-Dichtband (auf 0.5 komprimiert)
# --- Frontrahmen ---
BEZEL_T = 1.8           # Plattendicke (Senkung 1.0 tief laesst 0.8 unter dem Kopf)
WIN = 50.5              # Fensterbreite/-hoehe (aktiv 48.96 + Spiel)
WIN_OFF_X = 0.0         # bitte nachmessen: Fenstermitte relativ Platinenmitte (+X = rechts, vorne)
WIN_OFF_Y = 0.0         #                   (+Y = oben)
WIN_CHAMFER = 1.0       # 45 Grad aussen
# --- Befestigung ---
# Platine wird von hinten durch kurze Zapfen (in den Platinenloechern) zentriert, liegt auf 4 Standoffs und wird
# vom Frontrahmen (Rand auf dem Glas) geklemmt. Das LCD-Glas hat KEINE Loecher.
# Rahmen <-> Koerper: 4x M2 in den Ecken AUSSERHALB der 68x68-Platine.
OUTER = 78.0            # Aussenmass (Koerper und Rahmen)
R_OUT = 4.0             # Aussen-Eckenradius
SCREW_C = 35.8          # Schraubenmitte (+-X, +-Y)
USE_INSERTS = False     # True: M2-Gewindeeinsaetze (Bohrung 3.2 x 5), False: M2 selbstschneidend in 1.8
STANDOFF_OD = 5.0
PIN_D = 2.0             # Zapfen (Loch 2.2), 0.3 Einfuehrfase
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
REAR_RIB_PITCH = 15.0   # Rippenraster in den Hohlraeumen (keine Spannweite > ~15-20 mm)
RIB_T = 2.0
# --- Akku ---
# Pro Akkutyp: Fachmasse (BAT_L = lichte Laenge zwischen den Kontaktflaechen, BAT_W = Breite in Y, BAT_T = Fachhoehe
# ueber dem Deckel), Opfer-Stege, Lademodul, Clip-Verstaerkung.  lipo_503450 = unveraenderter Altbestand.
BATTERIES = {
    "lipo_503450": dict(dir="lipo", label="LiPo 503450", BAT_L=52.0, BAT_W=36.0, BAT_T=6.0,   # Akku ca. 50x34x5, bitte nachmessen
                        cyl=False, struts=True, charger=False, clip_plate_t=3.0, prong_t=2.4),
    # 14500: O14,5 x 53,5 (max., mit Schutzschaltung) + Federkontakt; BAT_T = O14,5 + 0,9 (Schaumstoff/Spiel)
    "lifepo4_14500": dict(dir="lifepo4", label="LiFePO4 14500", BAT_L=54.0, BAT_W=16.4, BAT_T=15.4,
                          cyl=True, struts=False, charger=True, clip_plate_t=3.5, prong_t=2.8),
}
if BATTERY_TYPE not in BATTERIES:
    sys.exit(f"Unbekannter Akkutyp {BATTERY_TYPE!r}; erlaubt: {', '.join(BATTERIES)}")
_B = BATTERIES[BATTERY_TYPE]
BAT_L, BAT_W, BAT_T = _B["BAT_L"], _B["BAT_W"], _B["BAT_T"]
CYL = _B["cyl"]                 # Rundzelle laengs in X (statt flachem Pouch)
CHARGER = _B["charger"]         # internes Lademodul + Deckel
# --- Rundzelle 14500 (nur CYL) ---
CELL_D_NOM, CELL_D_MAX = 14.2, 14.5     # Durchmesser nominal / maximal (Schrumpfschlauch, Toleranz)
CELL_L_MIN, CELL_L_NOM, CELL_L_MAX = 49.7, 50.5, 53.5   # blank 49.7-50.5, mit Schutzschaltung/Knopfpol bis ca. 53
CELL_TOP_GAP = 0.5      # Luft Zelle <-> Fachdecke (Rest ist Schaumstoff auf dem Deckel)
CRADLE_C = 5.2          # 45-Grad-Fasen der oberen Fachkanten = Zellenwiege (zentriert die Zelle, druckbar)
PLATE_W, PLATE_T, PLATE_CLR = 10.0, 0.5, 0.2   # AA-Haltekontakt: Blech 10 x 10 x 0,5 (Aussparung +0,2)
SPRING_FREE, SPRING_SOLID = 6.0, 2.0    # Feder (Minuspol): freie Laenge / Blocklaenge
SPRING_REC_D, SPRING_REC_DEPTH = 9.2, 1.5   # Federtasche O9,2 x 1,5 (Feder sitzt auf dem Blech dahinter)
WIRE_Z = (6.0, 9.0)     # Kabelrinne neben dem Fach (Hoehe Z)
WIRE_D = 2.0            # Tiefe der Kabelrinne in der Fachwand
# --- Lademodul (nur CHARGER): TP5000 (auf 3,6 V / LiFePO4 gestellt) oder CN3058E, typ. Platine ---
CHARGER_L, CHARGER_W, CHARGER_H = 26.0, 17.0, 4.0   # ANNAHME, bitte nachmessen: Laenge x Breite x Hoehe (Bauteile)
CHARGER_Y_C = -4.6      # Mitte des Modul-Schachts in Y (stehend, Platine in der XZ-Ebene)
CH_CLR_L, CH_CLR_T, CH_CLR_W = 1.4, 0.6, 0.3   # Spiel Laenge gesamt / Dicke gesamt / Breite gesamt
LID_T = 1.8             # Modul-/Kabeldeckel (buendig in Falz, 2x M2 Senkkopf)
LID_GROOVE = 0.8        # Rille im Deckel fuer die Modulunterkante
RIB_U_T, RIB_U_L = 1.4, 6.0    # Fuehrungsrippen (am Deckel) Dicke / Laenge
RIB_U_H = 14.0
CH_SLIT_UP = 0.3        # Schlitz fuer die Modul-Oberkante in der Fachdecke (Rest Boden >= 0,8)
LED_X = 0.0             # ANNAHME: Lage der Lade-LED relativ zur Modulmitte (X); Lichtfenster im Deckel
LED_W = 8.0
RAIL_T = 1.0            # hintere Lippe (haelt den Deckel nach aussen, Oeffnung = Akkufach)
DOOR_T = 1.4
DOOR_LIP = 2.0          # Ueberlappung Deckel <-> hintere Lippe je Seite
BAT_CABLE_X, BAT_CABLE_Y = -22.6, -17.0   # BAT-JST (gemessen X -22.6, Y -21.8; Stecker zieht nach +Y)
CABLE_SLOT = (8.0, 19.0) # Breite X, Hoehe Y des Kabeldurchlasses im Boden (Y -26.5..-7.5)
BAT_STRUTS = _B["struts"]   # Opfer-Stege im Akkufach (nur im Druckteil) tragen die Fachdecke
FLOOR_T = 1.5           # Trennboden Bauteilraum / Akkufach
# --- Anschluesse (Rueckansicht -> Modell umgerechnet) ---
USB_X = -0.7            # bitte nachmessen
USB_PLUG_W, USB_PLUG_H = 14.0, 8.0      # Stecker-Overmold
TF_X = 15.3             # TF-Kaefig Mitte (Rueckansicht bx 18.7 -> X = 34 - 18.7)
TF_W = 14.0
BTN_Y = [34 - 52.7, 34 - 59.5]  # Taster (beide Seiten) Y = -18.7, -25.5
BTN_TAB_L = 10.0         # Laenge Biegezunge
BTN_TAB_H = 4.4         # Hoehe Zunge (Z)
BTN_TAB_T = 1.2         # Dicke Zunge (X)
BTN_SLIT = 0.6
BTN_NUB = 0.25          # Abstand Stoessel-Spitze zur Platinenkante (Zunge wird beim Druck gebogen)
BTN_Z_OFF = -0.8        # Tastermitte relativ Platinenrueckseite (bitte nachmessen)
# --- Antenne (Ruecksicht bx 38..58, by 0..28 -> X -24..-4, Y 34..6) ---
ANT_RECESS = True       # Rueckseite ueber der Antenne ausgespart (dort nur Boden FLOOR_T)
ANT_X = (-24.0, -4.0)
ANT_Y = (25.5, 34.0)
# --- Klemm-Clip / Schwalbenschwanz ---
CLIP_CX, CLIP_CY = 6.0, 16.0    # Mittelpunkt des Clips auf der Rueckseite (freie Flaeche ueber Akkufach)
DT_NECK = 9.0           # Nutbreite an der Oberflaeche
DT_DEPTH = 2.8          # Nuttiefe (45 Grad Flanke, einseitig = druckbar ohne Stuetzen)
DT_XSHIFT = -1.7   # Nut-Hinterkante = Clip-Plattenkante (Druckflaeche)
CLIP_RIDGE_L = 26.0
CLIP_PLATE_Y = 28.0
CLIP_PLATE_X = 12.0
CLIP_PLATE_T = _B["clip_plate_t"]   # LiFePO4: 3,5 (schwereres Geraet)
SLAT_T = 2.0            # Lamellendicke Luftauslass (1.5 .. 3), bitte nachmessen
HOOK_DEPTH = 15.0
PRONG_T = _B["prong_t"]             # LiFePO4: 2,8 (steifere Zinken, ~1,6x Klemmkraft)
PRONG_W = CLIP_PLATE_X   # gleiche Breite -> gemeinsame Druckflaeche
PRONG_PRELOAD = 0.4     # Ruhespalt = SLAT_T - PRONG_PRELOAD
# --- Druckbett ---
BED = 220.0

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "export", _B["dir"])

# ======================= abgeleitete Masse =======================
INNER = BOARD + 2 * CLR
R_IN = BOARD_R + CLR
DOOR_GROOVE_H = DOOR_T + DT_CLR
CAV_H = RAIL_T + DOOR_GROOVE_H + BAT_T      # Akkufach Hoehe (Z 0..CAV_H)
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
DOOR_Z0 = RAIL_T                            # Deckel liegt hinter der Lippe
DETENT_Y = -CLIP_RIDGE_L / 2 + 4.0
DT_TIP = DT_DEPTH - 0.3
Z_USB = Z_PCB_BACK - USB_H / 2
Z_BTN = Z_PCB_BACK + BTN_Z_OFF
# --- Rundzelle: Lage der Zellenachse, Kontakte ---
CELL_YC = (POCKET_Y0 + POCKET_Y1) / 2
CELL_ZC = CAV_H - CELL_TOP_GAP - CELL_D_MAX / 2
PLATE_SLOT = PLATE_W + 0.4                      # Blech-Tasche (Y x Z)
PLATE_DEPTH = PLATE_T + PLATE_CLR
SPRING_X_BASE = POCKET_X0 - SPRING_REC_DEPTH    # Federfuss (Blechoberflaeche) am Minuspol
# --- Lademodul-Schacht (stehend: Platine in der XZ-Ebene, Laenge in X, Breite in Z) ---
CH_SLIT_L = CHARGER_L + CH_CLR_L
CH_SLIT_T = CHARGER_H + CH_CLR_T
CH_SLIT_X0 = -33.0
CH_SLIT_X1 = CH_SLIT_X0 + CH_SLIT_L
CH_SLIT_Y0, CH_SLIT_Y1 = CHARGER_Y_C - CH_SLIT_T / 2, CHARGER_Y_C + CH_SLIT_T / 2
CH_SLIT_Z1 = LID_T - LID_GROOVE + CHARGER_W + CH_CLR_W   # Oberkante Schlitz in der Fachdecke (Z)
BAY_X0, BAY_X1 = CH_SLIT_X0 - RIB_U_T - 0.2, CH_SLIT_X1 + RIB_U_T + 0.2   # Kabel-/Modulraum (Deckel-Oeffnung)
BAY_Y0 = RECESS_Y1 + REAR_BLOCK                  # grenzt an die Wand um das Akkufach
BAY_Y1 = CH_SLIT_Y1 + RIB_U_T + 0.2
FRAME_X0, FRAME_X1, FRAME_Y1 = -OUTER, -3.0, 7.0   # massiver Rahmen um den Raum (grenzt an Band/Rippen)
LID_X0, LID_X1 = -35.8, -2.8                     # Deckel (Falz-Aussenmass = Deckel + 0,2)
LID_Y0, LID_Y1 = BAY_Y0 + 0.2, 5.8
LID_SCREWS = [(-30.0, 2.6), (-8.0, 2.6)]         # M2 Senkkopf, selbstschneidend in Pilotloch
NOTCH_X = (-26.6, -18.6)                         # Kabeldurchlass Fach -> Raum (breit wie der Boden-Schlitz)
NOTCH_Z0 = 4.0                                   # darunter bleibt eine Schwelle (Rueckseite geschlossen)

# ======================= Helfer =======================
def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))

def cyl_z(x, y, z0, z1, r):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(r).extrude(z1 - z0)

def cyl_x(y, z, x0, x1, r):
    """Zylinder entlang X (Achse bei y, z)."""
    return cq.Workplane("YZ", origin=(x0, 0, 0)).center(y, z).circle(r).extrude(x1 - x0)

def rbox(w, h, z0, z1, r, cx=0, cy=0):
    return (cq.Workplane("XY").workplane(offset=z0).center(cx, cy).rect(w, h)
            .extrude(z1 - z0).edges("|Z").fillet(r))

def prism_y(pts, y0, y1):
    """Polygon in (X,Z) entlang +Y extrudiert."""
    return cq.Workplane("XZ", origin=(0, y0, 0)).polyline(pts).close().extrude(-(y1 - y0))

def prism_x(pts, x0, x1):
    """Polygon in (Y,Z) entlang +X extrudiert."""
    return cq.Workplane("YZ", origin=(x0, 0, 0)).polyline(pts).close().extrude(x1 - x0)

def dt_groove_cutter(y_stop, y_end):
    """Nut-Schneidwerkzeug fuer Portrait (laeuft entlang Y), offen zur Oberkante.
    Einseitiger Schwalbenschwanz: Flanke -X senkrecht, Flanke +X 45 Grad hinterschnitten."""
    xs, n, d = DT_XSHIFT, DT_NECK / 2, DT_DEPTH
    pts = [(xs - n, -0.5), (xs + n - 0.5, -0.5), (xs + n + d, d), (xs - n, d)]
    return prism_y(pts, y_stop, y_end)

def groove_cutters():
    """Nut Portrait (offen nach oben) + Nut Querformat (um 90 Grad gedreht, offen nach -X), je mit Rastmulde."""
    y_stop = CLIP_CY - CLIP_RIDGE_L / 2 - 0.3
    out = []
    g = dt_groove_cutter(y_stop, CLIP_CY + OUTER).translate((CLIP_CX, 0, 0))
    dimple = cq.Workplane("XY").sphere(1.0).translate((CLIP_CX + DT_XSHIFT, CLIP_CY + DETENT_Y, DT_TIP))
    for sh in (g, dimple):
        out.append(sh)
        out.append(sh.rotate((CLIP_CX, CLIP_CY, 0), (CLIP_CX, CLIP_CY, 1), 90))
    return out

# ======================= Rundzelle / Lademodul =======================
def cell_bay_features(b):
    """Zellenwiege (45-Grad-Fasen oben), Kontakttaschen an beiden Stirnseiten, Kabelrinne + Durchlass, Polaritaetsmarken."""
    yc, zc = CELL_YC, CELL_ZC
    C = CRADLE_C
    # obere Fachkanten anfasen (Material zurueck): Zelle liegt zentriert zwischen den Fasen, druckbar ohne Stuetzen
    for sg, yw in ((1, POCKET_Y0), (-1, POCKET_Y1)):
        pts = [(yw - sg * 0.3, CAV_H - C - 0.3), (yw + sg * (C + 0.3), CAV_H + 0.3), (yw - sg * 0.3, CAV_H + 0.3)]
        b = b.union(prism_x(pts, POCKET_X0, POCKET_X1))
    ps = PLATE_SLOT / 2
    # Pluspol (+X): flaches Kontaktblech buendig in der Stirnwand
    b = b.cut(box(POCKET_X1 - 0.01, POCKET_X1 + PLATE_DEPTH, yc - ps, yc + ps, zc - ps, zc + ps))
    # Minuspol (-X): Federtasche, dahinter die Blech-Tasche (Feder sitzt auf dem Blech)
    b = b.cut(cyl_x(yc, zc, SPRING_X_BASE, POCKET_X0 + 0.01, SPRING_REC_D / 2))
    b = b.cut(box(SPRING_X_BASE - PLATE_DEPTH, SPRING_X_BASE + 0.01, yc - ps, yc + ps, zc - ps, zc + ps))
    # Kabelrinne in der +Y-Wand (neben der Zelle) ueber die ganze Laenge + Verbindung zu beiden Blech-Taschen
    gz0, gz1 = WIRE_Z
    xa, xb = SPRING_X_BASE - PLATE_DEPTH, POCKET_X1 + PLATE_DEPTH
    b = b.cut(box(xa, xb, POCKET_Y1 - 0.01, POCKET_Y1 + WIRE_D, gz0, gz1))
    b = b.cut(box(POCKET_X1 - 0.01, POCKET_X1 + PLATE_DEPTH, yc + ps - 0.2, POCKET_Y1 + 0.01, gz0, gz1))
    b = b.cut(box(xa, SPRING_X_BASE + 0.01, yc + ps - 0.2, POCKET_Y1 + 0.01, gz0, gz1))
    # Durchlass Fach -> Kabel-/Modulraum (oberhalb einer 4 mm Schwelle; Rueckseite bleibt zu)
    b = b.cut(box(NOTCH_X[0], NOTCH_X[1], POCKET_Y1 - 0.01, BAY_Y0 + 0.01, NOTCH_Z0, CAV_H))
    # Polaritaetsmarken in die Fachdecke eingepraegt (vertieft, 0,4 mm; Plus rechts, Minus links)
    mk = 4.4
    for xm, plus in ((POCKET_X1 - 7.0, True), (POCKET_X0 + 7.0, False)):
        b = b.cut(box(xm - mk / 2, xm + mk / 2, yc - 0.5, yc + 0.5, CAV_H - 0.01, CAV_H + 0.4))
        if plus:
            b = b.cut(box(xm - 0.5, xm + 0.5, yc - mk / 2, yc + mk / 2, CAV_H - 0.01, CAV_H + 0.4))
    return b

def charger_bay(b):
    """Kabel-/Modulraum (von hinten offen, mit Deckel geschlossen): Oeffnung, Deckelfalz, Modul-Schlitz, Schraub-Pilotloecher."""
    b = b.cut(box(BAY_X0, BAY_X1, BAY_Y0 - 0.01, BAY_Y1, -1, CAV_H))
    b = b.cut(box(CH_SLIT_X0, CH_SLIT_X1, CH_SLIT_Y0, CH_SLIT_Y1, CAV_H - 0.1, CH_SLIT_Z1))   # Modul-Oberkante
    b = b.cut(box(LID_X0 - 0.2, LID_X1 + 0.2, LID_Y0 - 0.2, LID_Y1 + 0.2, -1, LID_T))           # Deckelfalz
    for sx, sy in LID_SCREWS:
        b = b.cut(cyl_z(sx, sy, LID_T - 0.01, LID_T + PILOT_DEPTH - 2.0, PILOT_D / 2))
    return b

# ======================= Teile =======================
def make_body():
    b = rbox(OUTER, OUTER, 0, Z_BODY_TOP, R_OUT)
    # Bauteilraum (Platine + LCD)
    b = b.cut(rbox(INNER, INNER, Z_FLOOR_TOP, Z_BODY_TOP + 1, R_IN))
    # Akkufach (von hinten) + Deckelfuehrung
    b = b.cut(box(POCKET_X0, POCKET_X1, POCKET_Y0, POCKET_Y1, -1, CAV_H))
    b = b.cut(box(-RECESS_HALF, RECESS_HALF, -OUTER / 2 - 1, RECESS_Y1, RAIL_T, RAIL_T + DOOR_GROOVE_H))
    # Rasten-Mulden fuer Deckel (geschlossen)
    yb = DOOR_Y1 - 4.0
    for s in (-1, 1):
        b = b.cut(cyl_z(s * RECESS_HALF, yb, RAIL_T, RAIL_T + DOOR_GROOVE_H, 0.8))
    # Kabeldurchlass zum BAT-Stecker
    cx, cy = BAT_CABLE_X, BAT_CABLE_Y
    b = b.cut(box(max(cx - CABLE_SLOT[0] / 2, POCKET_X0 + 0.0), cx + CABLE_SLOT[0] / 2,
                  cy - CABLE_SLOT[1] / 2, cy + CABLE_SLOT[1] / 2, CAV_H - 0.1, Z_FLOOR_TOP + 0.1))
    if CYL:
        b = cell_bay_features(b)
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
    k = 1
    while -h + k * REAR_RIB_PITCH < h - 1:
        t0 = -h + k * REAR_RIB_PITCH
        keep = keep.union(box(t0 - RIB_T / 2, t0 + RIB_T / 2, -OUTER, OUTER, -2, CAV_H + 1))
        keep = keep.union(box(-OUTER, OUTER, t0 - RIB_T / 2, t0 + RIB_T / 2, -2, CAV_H + 1))
        k += 1
    if ANT_RECESS:   # Rippe direkt neben der Antennenzone (nicht darin)
        keep = keep.union(box(ANT_X[0] - RIB_T, ANT_X[0], -OUTER, OUTER, -2, CAV_H + 1))
    if CHARGER:      # massiver Rahmen um den Kabel-/Modulraum (ersetzt dort das Rippenraster)
        keep = keep.union(box(FRAME_X0, FRAME_X1, BAY_Y0 - 0.4, FRAME_Y1, -2, CAV_H + 1))
    void = box(-h, h, -h, h, -1, CAV_H).cut(keep)
    b = b.cut(void)
    if CHARGER:
        b = charger_bay(b)
    # Antennenzone nochmals ganz frei (nur Boden)
    if ANT_RECESS:
        b = b.cut(box(ANT_X[0], ANT_X[1], ANT_Y[0], ANT_Y[1], -1, CAV_H))
    # Standoffs mit Zentrierzapfen (Platinenloecher), Platine liegt auf Standoff-Oberseite
    g = HOLE_GRID / 2
    for sx, sy in itertools.product((-1, 1), (-1, 1)):
        b = b.union(cyl_z(sx * g, sy * g, Z_FLOOR_TOP - 0.2, Z_PCB_BACK, STANDOFF_OD / 2))
        pin = cyl_z(0, 0, 0, PIN_H, PIN_D / 2).faces(">Z").chamfer(0.3).translate((sx * g, sy * g, Z_PCB_BACK - 0.01))
        b = b.union(pin)
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
    # Fingermulde aussen, damit eine ausgeworfene Karte greifbar ist (Wand ist ~5 mm dick)
    b = b.cut(box(TF_X - 9.0, TF_X + 9.0, OUTER / 2 - 2.0, OUTER / 2 + 1, Z_PCB_BACK - 4.5, Z_PCB_BACK + 1.5))
    # Seitentaster: Biegezungen (Scharnier am anderen Ende, Zungen laufen auseinander)
    wall_in = INNER / 2
    wall_out = OUTER / 2
    tx0 = wall_out - BTN_TAB_T
    zc = Z_BTN
    h2 = BTN_TAB_H / 2
    tabs = []
    for i, by in enumerate(BTN_Y):
        if i == 0:   # oberer Taster: Scharnier oben, freies Ende unten
            free, hinge = by - 2.0, by - 2.0 + BTN_TAB_L   # Stoessel 2 mm vom freien Ende
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
        cone = (cq.Workplane("XY").workplane(offset=zt - 1.0).center(sx * g, sy * g).circle(1.2)
                .workplane(offset=1.01).circle(CSK_D / 2 + 0.01).loft())
        bz = bz.cut(cone)
    return bz

def make_door():
    d = box(-DOOR_W / 2, DOOR_W / 2, DOOR_Y0, DOOR_Y1, 0, DOOR_T)   # lokal Z 0..DOOR_T, am Ende um RAIL_T versetzt
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
    return d.translate((0, 0, DOOR_Z0))

def make_clip():
    xs, n, d = DT_XSHIFT, DT_NECK / 2, DT_DEPTH
    c = DT_CLR
    k = c * math.sqrt(2)
    tip = d - 0.3
    z_pl_top = -c
    pts = [(xs - n + c, z_pl_top), (xs + n - 0.5 - k - c, z_pl_top),
           (xs + n - k + tip, tip), (xs - n + c, tip)]
    ridge = prism_y(pts, -CLIP_RIDGE_L / 2, CLIP_RIDGE_L / 2)
    ridge = ridge.faces(">Z").edges("|Y").fillet(0.6)          # Spitze verrunden
    ridge = ridge.union(cq.Workplane("XY").sphere(0.7).translate((xs, DETENT_Y, tip)))   # Rastnase
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

def make_charger_lid():
    """Deckel fuer Kabel-/Modulraum: Platte (buendig in Falz) + U-Fuehrungen fuer das stehende Modul + Lichtfenster."""
    lid = box(LID_X0, LID_X1, LID_Y0, LID_Y1, 0, LID_T)
    # Rille fuer die Modulunterkante
    lid = lid.cut(box(CH_SLIT_X0, CH_SLIT_X1, CH_SLIT_Y0, CH_SLIT_Y1, LID_T - LID_GROOVE, LID_T + 1))
    # Lichtfenster unter dem Modul (ANNAHME: LED nahe der Unterkante bei LED_X)
    xc = (CH_SLIT_X0 + CH_SLIT_X1) / 2 + LED_X
    lid = lid.cut(box(xc - LED_W / 2, xc + LED_W / 2, CHARGER_Y_C - 1.0, CHARGER_Y_C + 1.0, -1, LID_T))
    # U-Fuehrungen an beiden Modul-Enden (Endwand + zwei Seitenrippen)
    z0, z1 = LID_T - 0.01, LID_T + RIB_U_H
    for end in (-1, 1):
        if end < 0:
            xe0, xe1 = CH_SLIT_X0 - RIB_U_T, CH_SLIT_X0
            xs0, xs1 = CH_SLIT_X0 - RIB_U_T, CH_SLIT_X0 + RIB_U_L
        else:
            xe0, xe1 = CH_SLIT_X1, CH_SLIT_X1 + RIB_U_T
            xs0, xs1 = CH_SLIT_X1 - RIB_U_L, CH_SLIT_X1 + RIB_U_T
        lid = lid.union(box(xe0, xe1, CH_SLIT_Y0 - RIB_U_T, CH_SLIT_Y1 + RIB_U_T, z0, z1))
        lid = lid.union(box(xs0, xs1, CH_SLIT_Y0 - RIB_U_T, CH_SLIT_Y0, z0, z1))
        lid = lid.union(box(xs0, xs1, CH_SLIT_Y1, CH_SLIT_Y1 + RIB_U_T, z0, z1))
    # Senkschrauben M2
    for sx, sy in LID_SCREWS:
        lid = lid.cut(cyl_z(sx, sy, -1, LID_T + 1, 1.2))
        cone = (cq.Workplane("XY").workplane(offset=-0.01).center(sx, sy).circle(CSK_D / 2 + 0.01)
                .workplane(offset=1.02).circle(1.2).loft())
        lid = lid.cut(cone)
    return lid

def make_cell_dummy():
    """14500 (nominal O14,2 x 50,5), am Pluspol anliegend (Feder gedrueckt) - nur Referenzkoerper."""
    return cyl_x(CELL_YC, CELL_ZC, POCKET_X1 - CELL_L_NOM, POCKET_X1, CELL_D_NOM / 2)

def make_charger_dummy():
    """Lademodul stehend im Schacht (Platine XZ, ANNAHME 26 x 17 x 4)."""
    xc = (CH_SLIT_X0 + CH_SLIT_X1) / 2
    zb = LID_T - LID_GROOVE + 0.1
    return box(xc - CHARGER_L / 2, xc + CHARGER_L / 2, CHARGER_Y_C - CHARGER_H / 2, CHARGER_Y_C + CHARGER_H / 2,
               zb, zb + CHARGER_W)

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
    """Tesselliert und verschweisst Eckpunkte (Rundung auf 1e-4) -> geschlossene Netze."""
    v, t = wp.val().tessellate(tol, ang)
    idx, verts, remap = {}, [], []
    for p in v:
        key = (round(p.x, 4), round(p.y, 4), round(p.z, 4))
        if key not in idx:
            idx[key] = len(verts); verts.append(key)
        remap.append(idx[key])
    tris = []
    for a, b, c in t:
        a, b, c = remap[a], remap[b], remap[c]
        if a != b and b != c and a != c:
            tris.append((a, b, c))
    return verts, tris

def write_stl(path, wp, tol=0.02, ang=0.1):
    import struct
    v, t = tess(wp, tol, ang)
    with open(path, "wb") as f:
        f.write(b"enclosure.py".ljust(80, b" "))
        f.write(struct.pack("<I", len(t)))
        for a, b, c in t:
            A, B, C = v[a], v[b], v[c]
            u = [B[i] - A[i] for i in range(3)]; w = [C[i] - A[i] for i in range(3)]
            n = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]]
            l = math.sqrt(sum(x * x for x in n)) or 1.0
            f.write(struct.pack("<12fH", *[x / l for x in n], *A, *B, *C, 0))

def mesh_is_closed(verts, tris):
    from collections import Counter
    e = Counter()
    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            e[(u, v)] += 1
    return all(e[(v, u)] == n for (u, v), n in e.items())

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

HEX = {"purple": "#8a4fd0", "lightblue": "#7fb8e8", "red3": "#c8283c", "gray70": "#b3b3b3", "gray20": "#333333", "orange": "#ff9900", "steelblue": "#4682b4", "forestgreen": "#228b22"}

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

def clip_hold_estimate():
    """Grobe Abschaetzung (ANNAHMEN: PETG E=1200 MPa quer zu den Schichten, mu=0.4, Geraetemasse siehe Dict)."""
    E, mu, g = 1200.0, 0.4, 9.81
    mass = {"lipo_503450": 0.075, "lifepo4_14500": 0.105}[BATTERY_TYPE]       # kg, geschaetzt (Teile + Board + Zelle)
    delta = (SLAT_T - (SLAT_T - PRONG_PRELOAD)) / 2                          # Auslenkung je Zinke
    Lp = HOOK_DEPTH - 2.0                                                    # wirksame Biegelaenge (bis zum Haken)
    I = PRONG_W * PRONG_T ** 3 / 12
    F = 3 * E * I * delta / Lp ** 3                                          # N je Zinke
    arm = Z_TOTAL * 0.45 + CLIP_PLATE_T + HOOK_DEPTH * 0.5                   # COG-Hebel zur Klemmstelle (mm)
    m_tilt, cap_tilt = mass * g * arm, F * (HOOK_DEPTH - 3.0)                # N*mm
    f_slip = mass * g
    sigma = 3 * E * PRONG_T * delta / (2 * Lp ** 2)
    return dict(F=F, sf_slip=2 * mu * F / f_slip, sf_tilt=cap_tilt / m_tilt, sigma=sigma, mass=mass, arm=arm)

def run_cyl_probes(body, door, lid, clip_asm, asm_parts, body_print, inside, probe, log):
    """Funktions-Probes der LiFePO4-Variante (Zelle, Kontakte, Kabelwege, Modul, Deckel)."""
    log("== LiFePO4: Zelle / Kontakte ==")
    yc, zc = CELL_YC, CELL_ZC
    # Zelle max (O14,5 x 53,5) / nominal / kurz, am Pluspol anliegend: keine Durchdringung mit Koerper und Deckel
    for lab, d, L in (("max O14,5 x 53,5 (mit Schutzschaltung)", CELL_D_MAX, CELL_L_MAX),
                      ("nominal O14,2 x 50,5", CELL_D_NOM, CELL_L_NOM), ("kurz O14,0 x 49,7", 14.0, CELL_L_MIN)):
        cyl = cyl_x(yc, zc, POCKET_X1 - L, POCKET_X1, d / 2)
        probe(f"Zelle {lab}: frei von Koerper", vol_int(body, cyl) < 0.05)
        probe(f"Zelle {lab}: frei von Deckel", vol_int(door, cyl) < 0.05)
    cyl = cyl_x(yc, zc, POCKET_X0 + 0.0, POCKET_X0 + CELL_L_MAX, CELL_D_MAX / 2)
    probe("Zelle max am Minuspol anliegend (Feder auf Block): frei von Koerper", vol_int(body, cyl) < 0.05)
    # Wiege: Spiel zu den Fasen und Schaumstofffuge
    dist = (POCKET_Y1 - yc + (CAV_H - zc) - CRADLE_C) / math.sqrt(2)
    gap = dist - CELL_D_MAX / 2
    probe(f"Wiege: Spiel Zelle max <-> 45-Grad-Fase {gap:.2f} mm (0 .. 0.8)", 0.0 <= gap <= 0.8)
    foam = (zc - CELL_D_MAX / 2) - (RAIL_T + DOOR_T)
    probe(f"Fuge Zelle max <-> Deckel (Schaumstoff 1 mm, komprimiert) {foam:.2f} mm >= 0.5", foam >= 0.5)
    # Federweg
    sp = lambda L: (POCKET_X1 - L) - SPRING_X_BASE
    log(f"Feder (frei {SPRING_FREE}, Block {SPRING_SOLID}): Laenge eingebaut {sp(CELL_L_MAX):.1f} (Zelle {CELL_L_MAX}) .. "
        f"{sp(CELL_L_NOM):.1f} (nominal) .. {sp(CELL_L_MIN):.1f} (Zelle {CELL_L_MIN})")
    probe("Feder: kuerzeste Zelle (49,7) wird noch beruehrt (Laenge <= frei)", sp(CELL_L_MIN) <= SPRING_FREE + 1e-6)
    probe("Feder: laengste Zelle (53,5) drueckt nicht auf Block (Laenge >= Block)", sp(CELL_L_MAX) >= SPRING_SOLID - 1e-6)
    # Kontakttaschen
    ps = PLATE_SLOT / 2
    probe("Pluspol: Blechtasche 10,4 x 10,4 vorhanden (Ecken frei)",
          all(not inside(body, POCKET_X1 + PLATE_DEPTH / 2, yc + a * (ps - 0.1), zc + c * (ps - 0.1)) for a in (-1, 1) for c in (-1, 1)))
    probe("Pluspol: Blechtasche begrenzt (Wand dahinter / daneben)",
          inside(body, POCKET_X1 + PLATE_DEPTH + 0.3, yc, zc) and inside(body, POCKET_X1 + PLATE_DEPTH / 2, yc + ps + 0.3, zc))
    probe("Minuspol: Federtasche O9,2 frei", all(not inside(body, SPRING_X_BASE + 0.5, yc + 4.0 * math.cos(t), zc + 4.0 * math.sin(t))
                                              for t in (0, 1.5, 3.1, 4.7)))
    probe("Minuspol: Federtasche begrenzt (Tasche hat Boden bei Blech)", inside(body, SPRING_X_BASE - PLATE_DEPTH - 0.3, yc, zc)
          and inside(body, SPRING_X_BASE + 0.5, yc + SPRING_REC_D / 2 + 0.3, zc))
    probe("Minuspol: Blechtasche hinter der Feder frei", not inside(body, SPRING_X_BASE - PLATE_DEPTH / 2, yc + 4.8, zc + 4.8))
    probe("Polaritaetsmarke Plus (vertieft in der Fachdecke)", not inside(body, POCKET_X1 - 7.0, yc, CAV_H + 0.2)
          and not inside(body, POCKET_X1 - 7.0, yc + 1.5, CAV_H + 0.2) and inside(body, POCKET_X1 - 7.0 + 1.5, yc + 1.5, CAV_H + 0.2))
    probe("Polaritaetsmarke Minus (vertieft in der Fachdecke)", not inside(body, POCKET_X0 + 7.0, yc, CAV_H + 0.2)
          and inside(body, POCKET_X0 + 7.0, yc + 1.5, CAV_H + 0.2))
    # Deckel haengt weiter an Rail/Nut
    probe("Akkudeckel: Oberseite unter der Zelle (Z)", RAIL_T + DOOR_T < zc - CELL_D_MAX / 2)

    log("== LiFePO4: Kabelwege (Strecken im Koerper frei) ==")
    def path_free(name, wps):
        bad = None
        for a, b in zip(wps, wps[1:]):
            n = max(2, int(math.dist(a, b) / 0.5))
            for i in range(n + 1):
                q = tuple(a[k] + (b[k] - a[k]) * i / n for k in range(3))
                if inside(body, *q):
                    bad = q; break
            if bad:
                break
        probe(f"Kabelweg {name}", bad is None)
    gzm = sum(WIRE_Z) / 2
    gy = POCKET_Y1 + WIRE_D / 2
    xj = (NOTCH_X[0] + NOTCH_X[1]) / 2
    path_free("Pluspol -> Rinne -> Durchlass -> Kabelraum -> Bodenschlitz (BAT-Stecker)",
              [(POCKET_X1 + 0.3, yc + 3.0, gzm), (POCKET_X1 + 0.3, gy, gzm), (xj, gy, gzm), (xj, BAY_Y0 + 1.0, gzm),
               (xj, -12.0, 17.0), (xj, -12.0, Z_FLOOR_TOP + 0.5)])
    path_free("Minuspol -> Rinne -> Durchlass -> Kabelraum -> Bodenschlitz",
              [(SPRING_X_BASE - 0.3, yc + 3.0, gzm), (SPRING_X_BASE - 0.3, gy, gzm), (xj, gy, gzm), (xj, BAY_Y0 + 1.0, gzm),
               (xj, -12.0, 17.0), (xj, -12.0, Z_FLOOR_TOP + 0.5)])
    mxc = (CH_SLIT_X0 + CH_SLIT_X1) / 2
    path_free("Lademodul (beide Enden) -> Kabelraum -> Bodenschlitz (5 V / Akku)",
              [(CH_SLIT_X0 + 1.0, CHARGER_Y_C, 8.0), (CH_SLIT_X0 + 1.0, -10.0, 8.0), (xj, -12.0, 8.0), (xj, -12.0, Z_FLOOR_TOP + 0.5),
               (xj + 3, -12.0, 8.0), (CH_SLIT_X1 - 1.0, -10.0, 8.0), (CH_SLIT_X1 - 1.0, CHARGER_Y_C, 8.0)])
    probe("Kabelraum: nur Schwelle zur Rueckseite (Durchlass beginnt erst ab z=4)", inside(body, xj, gy, NOTCH_Z0 - 0.5))

    log("== LiFePO4: Lademodul / Deckel ==")
    chg = asm_parts["charger_dummy"]
    probe("Lademodul 26 x 17 x 4 steht im Schacht: frei von Koerper", vol_int(body, chg) < 0.05)
    probe("Lademodul: frei von Deckel (Rille/Fuehrungen)", vol_int(lid, chg) < 0.05)
    fat = box(CH_SLIT_X0 + 0.05, CH_SLIT_X1 - 0.05, CH_SLIT_Y0 + 0.05, CH_SLIT_Y1 - 0.05, LID_T, CH_SLIT_Z1 - 0.05)
    probe(f"Lademodul bis {CH_SLIT_L - CH_CLR_L / 2:.1f} x {CHARGER_W + CH_CLR_W - 0.1:.1f} x {CH_SLIT_T - 0.1:.1f} (Schacht ganz frei): frei von Koerper",
          vol_int(body, fat) < 0.05)
    probe(f"Boden ueber dem Modul-Schlitz >= 0.8 mm ({Z_FLOOR_TOP - CH_SLIT_Z1:.2f})", Z_FLOOR_TOP - CH_SLIT_Z1 >= 0.8 - 1e-9
          and inside(body, mxc, CHARGER_Y_C, CH_SLIT_Z1 + 0.4))
    probe("Lademodul weit von der Antennenzone (>= 20 mm in Y)", ANT_Y[0] - (CHARGER_Y_C + CHARGER_H / 2) >= 20.0)
    probe("Lademodul ausserhalb Clip-Band/Nuten (X < -3)", CH_SLIT_X1 + RIB_U_T < FRAME_X1)
    bl = lid.val().BoundingBox()
    probe("Deckel buendig mit der Rueckseite (z 0 .. LID_T)", abs(bl.zmin) < 1e-6 and bl.zmax >= LID_T)
    probe("Deckel liegt im Falz auf (Falzboden ueber dem Deckel)", inside(body, LID_X0 + 1.0, BAY_Y1 + 1.0, LID_T + 0.2)
          and not inside(body, LID_X0 + 1.0, BAY_Y1 + 1.0, LID_T - 0.2))
    probe("Deckel frei von Koerper / Akkudeckel / Clip", vol_int(lid, body) < 0.05 and vol_int(lid, door) < 0.05
          and vol_int(lid, clip_asm) < 0.05)
    miss = [(x, y) for x in range(int(BAY_X0) + 1, int(BAY_X1)) for y in range(int(BAY_Y0) + 1, int(BAY_Y1))
            if not inside(lid, x, y, 0.5) and not (abs(x - (mxc + LED_X)) <= LED_W / 2 and abs(y - CHARGER_Y_C) <= 1.0)]
    probe("Rueckseite ueber dem Kabelraum durch den Deckel geschlossen (ausser Lichtfenster)", not miss)
    probe("Lichtfenster im Deckel offen", not inside(lid, mxc + LED_X, CHARGER_Y_C, 0.5))
    for sx, sy in LID_SCREWS:
        probe(f"Schraube M2 bei ({sx:+.0f},{sy:+.0f}): Deckelloch frei, Pilotloch frei, Material daneben massiv",
              not inside(lid, sx, sy, 0.5) and not inside(body, sx, sy, LID_T + 2.0) and inside(body, sx + 1.5, sy, LID_T + 2.0))
    log(f"Schraube Deckel: M2x6 Senkkopf (Deckel {LID_T} + Pilotloch {PILOT_DEPTH - 2.0:.0f} - 0.5 = {LID_T + PILOT_DEPTH - 2.0 - 0.5:.1f}), 2 Stueck")

    log("== LiFePO4: Vent-Clip ==")
    h = clip_hold_estimate()
    probe(f"Clip-Haltesicherheit gegen Rutschen {h['sf_slip']:.1f} >= 3", h["sf_slip"] >= 3.0)
    probe(f"Clip-Haltesicherheit gegen Kippen {h['sf_tilt']:.1f} >= 2", h["sf_tilt"] >= 2.0)

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
    if CHARGER:
        lid = make_charger_lid()
        asm_parts["charger_lid"] = lid
    if CYL:
        asm_parts["cell_dummy"] = make_cell_dummy()
        asm_parts["charger_dummy"] = make_charger_dummy()
    # Klemm-Clip in Einbaulage: Mittelpunkt (CLIP_CX, CLIP_CY)
    clip_asm = clip.translate((CLIP_CX, CLIP_CY, 0))
    asm_parts["vent_clip"] = clip_asm

    # ---- Druckanordnung (220x220 Bett) ----
    body_print = body
    if BAT_STRUTS:   # Opfer-Stege (0.8 mm) tragen die Akkufach-Decke, vor dem Einlegen des Akkus ausbrechen
        for sx in (-1, 1):
            body_print = body_print.union(box(sx * BAT_L / 6 - 0.4, sx * BAT_L / 6 + 0.4,
                                              POCKET_Y0, POCKET_Y1, 0, CAV_H + 0.01))
    plate_parts = [
        ("body", to_plate(body_print, "none", 45, 45)),
        ("front_bezel", to_plate(bezel, "flipx", 130, 45)),
        ("battery_door", to_plate(door, "flipx", 45, 120)),
        ("vent_clip", to_plate(clip, "clip", 130, 120)),
    ]
    if CHARGER:
        plate_parts.append(("charger_lid", to_plate(lid, "none", 130, 175)))

    rep = []
    def log(s):
        print(s); rep.append(s)

    log(f"== Variante: {_B['label']} ({BATTERY_TYPE}) -> export/{_B['dir']}/ ==")
    log("== Parameter/Stack ==")
    log(f"Aussenmass Koerper/Rahmen: {OUTER:.1f} x {OUTER:.1f} mm, Gesamtdicke ohne Clip: {Z_TOTAL:.2f} mm "
        f"(mit Clip +{CLIP_PLATE_T + 0.2 + HOOK_DEPTH:.1f}, gesamt {Z_TOTAL + CLIP_PLATE_T + 0.2 + HOOK_DEPTH:.1f} mm)")
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

    log("== Funktions-Probes ==")
    from OCP.gp import gp_Pnt  # noqa
    def inside(wp, x, y, z):
        return bool(wp.val().isInside(cq.Vector(x, y, z), 1e-4))
    def probe(name, cond):
        nonlocal ok
        log(f"{name:70s} {'OK' if cond else 'FEHLER'}")
        ok &= bool(cond)
    def void_run(wp, pts):
        best = cur = 0.0
        for i, pt in enumerate(pts):
            if inside(wp, *pt):
                cur = 0.0
            else:
                cur += 1.0 if i else 0.0
                best = max(best, cur)
        return best
    xs_ = DT_XSHIFT
    PY = CELL_YC if CYL else -10.0     # Y-Lage der Pruefpunkte im Akkufach
    # 1) Nuten laufen bis zum Rand
    pts = [(x, CLIP_CY + xs_, 1.0) for x in range(int(-OUTER / 2) + 1, int(CLIP_CX + CLIP_RIDGE_L / 2))]
    probe("Nut Querformat frei von linker Aussenkante bis Anschlag", not any(inside(body, *q) for q in pts))
    probe("Nut Querformat endet am Anschlag (Wand dahinter)", inside(body, CLIP_CX + CLIP_RIDGE_L / 2 + 1.0, CLIP_CY + xs_, 1.0))
    pts = [(CLIP_CX + xs_, y, 1.0) for y in range(int(CLIP_CY - CLIP_RIDGE_L / 2), int(OUTER / 2))]
    probe("Nut Hochformat frei von oberer Aussenkante bis Anschlag", not any(inside(body, *q) for q in pts))
    # 2) Akkudeckel wird nach aussen (-Z) von der Lippe gehalten
    bd = door.val().BoundingBox()
    probe("Deckel liegt hinter der Lippe (zmin >= RAIL_T)", bd.zmin >= RAIL_T - 1e-6)
    probe("Deckel ueberlappt Lippe >= 1.5 mm je Seite", bd.xmax - BAT_L / 2 >= 1.5 and -bd.xmin - BAT_L / 2 >= 1.5)
    for sgn in (-1, 1):
        x = sgn * (BAT_L / 2 + 1.0)
        probe(f"Lippe (Koerper) unter dem Deckel bei x={x:+.0f}, z={RAIL_T/2:.1f}", inside(body, x, PY, RAIL_T / 2))
        probe(f"Deckel sitzt in Nut bei x={x:+.0f}", inside(door, x, PY, RAIL_T + DOOR_T / 2) and not inside(body, x, PY, RAIL_T + DOOR_T / 2))
    probe("Akkufach nach hinten offen (Oeffnung frei)", not inside(body, 0, PY, 0.2) and not inside(body, 0, PY, CAV_H - 0.5))
    # 3) TF-Fenster
    for dx in (-TF_W / 2 + 0.5, 0, TF_W / 2 - 0.5):
        probe(f"TF-Fenster frei bei X={TF_X + dx:.1f}", not inside(body, TF_X + dx, 36.5, Z_PCB_BACK - 1.5))
    probe("TF-Fingermulde aussen frei", not inside(body, TF_X, OUTER / 2 - 1.0, Z_PCB_BACK - 1.0))
    probe("TF-Fenster ca. 15.3 mm, Wand daneben geschlossen", inside(body, TF_X + TF_W / 2 + 1.5, 36.5, Z_PCB_BACK - 1.5))
    # 4) Rastnase Clip
    probe("Rastmulde im Koerper vorhanden", not inside(body, CLIP_CX + xs_, CLIP_CY + DETENT_Y, DT_TIP + 0.8))
    probe("Rastnase am Clip vorhanden", inside(clip_asm, CLIP_CX + xs_, CLIP_CY + DETENT_Y, DT_TIP + 0.3))
    # 5) Bruecken-Spannweiten (Druckteil, Schnitt z=0.5)
    ZB = LID_T + 0.5 if CHARGER else 0.5   # Pruefhoehe (mit Charger-Deckel liegt der Falz unterhalb)
    lines = {"Akkufach": ([(0.0, y * 1.0, 0.5) for y in range(-38, -17)] if CYL else [(x, -20.0, ZB) for x in range(-26, 27)]),
             "Hohlraum y=30": [(x * 1.0, 30.0, ZB) for x in range(-35, 36)],
             "Hohlraum y=-8 (rechts)": [(x * 1.0, -8.0, ZB) for x in range(32, 36)],
             "Hohlraum x=-33": [(-33.0, y * 1.0, ZB) for y in range(-35, 36)]}
    for nm, pts in lines.items():
        r = void_run(body_print, pts)
        probe(f"Maximale freie Spannweite {nm}: {r:.0f} mm <= 20", r <= 20)
    # 6) Antenne frei
    ax = [(x * 1.0, y * 1.0, 1.0) for x in range(int(ANT_X[0]) + 1, int(ANT_X[1])) for y in range(int(ANT_Y[0]) + 1, int(ANT_Y[1]))]
    probe("Antennenzone rueckseitig frei (nur Boden)", not any(inside(body, *q) for q in ax))
    probe("Clip-Mitte ausserhalb der Antennenzone", CLIP_CX - CLIP_PLATE_X / 2 > ANT_X[1] or CLIP_CY - CLIP_PLATE_Y / 2 > ANT_Y[1])
    # 7) Bruecken (2D): jeder freie Punkt im Druckteil (z=0.5) hat in X oder Y eine freie Strecke <= 20 mm
    ZC = DT_DEPTH + 0.4    # 2D-Pruefhoehe knapp ueber den (flachen) Schwalbenschwanz-Nuten
    def free_run(wp, x, y, dx, dy, z=None, limit=22):
        z = ZC if z is None else z
        """freie Strecke (mm) durch (x,y) in Richtung (dx,dy) und zurueck, bis Material oder Rand; bricht bei limit ab."""
        n = 0.0
        for sgn in (-1, 1):
            k = 1
            while k < limit and abs(x + sgn * dx * k) < OUTER / 2 - 1 and abs(y + sgn * dy * k) < OUTER / 2 - 1 \
                    and not inside(wp, x + sgn * dx * k, y + sgn * dy * k, z):
                k += 1
            n += (k - 1) * math.hypot(dx, dy)
        return n + 1
    worst = (0, None)
    for gx in range(-36, 37, 3):
        for gy in range(-36, 37, 3):
            if inside(body_print, gx, gy, ZC):
                continue
            r = min(free_run(body_print, gx, gy, 1, 0), free_run(body_print, gx, gy, 0, 1))
            if r > 20:   # Kreuzungen (z. B. Schwalbenschwanz-Nuten): auch Diagonalen pruefen
                r = min(r, free_run(body_print, gx, gy, 1, 1), free_run(body_print, gx, gy, 1, -1))
            if r > worst[0]:
                worst = (r, (gx, gy))
    probe(f"Bruecken 2D: groesste Freispannweite (kuerzeste Richtung) {worst[0]:.0f} mm bei {worst[1]} <= 20", worst[0] <= 20)
    h = clip_hold_estimate()
    log(f"Clip-Schaetzung (ANNAHMEN: PETG E=1200 MPa, mu=0.4, Masse {h['mass']*1000:.0f} g, Hebel {h['arm']:.0f} mm): "
        f"Klemmkraft je Zinke {h['F']:.1f} N, Biegespannung {h['sigma']:.1f} MPa, Sicherheit Rutschen {h['sf_slip']:.1f} / Kippen {h['sf_tilt']:.1f}")
    if CYL:
        run_cyl_probes(body, door, lid, clip_asm, asm_parts, body_print, inside, probe, log)
    log("RESULT: " + ("ALLE PRUEFUNGEN OK" if ok else "FEHLER"))

    # ---- Dateien ----
    for n, p in asm_parts.items():
        cq.exporters.export(p, os.path.join(OUT, f"{n}.step"))
    for (n, p) in plate_parts:
        write_stl(os.path.join(OUT, f"{n}.stl"), p)
    write_stl(os.path.join(OUT, "board_dummy.stl"), dummy, 0.05, 0.2)
    asm = cq.Assembly(name="assembly")
    cols = {"body": "gray70", "front_bezel": "gray20", "battery_door": "orange",
            "vent_clip": "steelblue", "board_dummy": "forestgreen", "charger_lid": "purple",
            "cell_dummy": "lightblue", "charger_dummy": "red3"}
    for n, p in asm_parts.items():
        asm.add(p, name=n, color=cq.Color(cols[n]))
    asm.save(os.path.join(OUT, "assembly.step"))
    write_3mf(os.path.join(OUT, "print_plate.3mf"), plate_parts)
    import xml.etree.ElementTree as ET
    with zipfile.ZipFile(os.path.join(OUT, "print_plate.3mf")) as z:
        root = ET.fromstring(z.read("3D/3dmodel.model"))
    ns = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}
    closed_all = True
    for o in root.iterfind(".//m:object", ns):
        vs = [tuple(map(float, (v.get("x"), v.get("y"), v.get("z")))) for v in o.iterfind(".//m:vertex", ns)]
        ts = [(int(t.get("v1")), int(t.get("v2")), int(t.get("v3"))) for t in o.iterfind(".//m:triangle", ns)]
        c = mesh_is_closed(vs, ts)
        closed_all &= c
        print(f"3MF {o.get('name')}: {len(vs)} Ecken, {len(ts)} Dreiecke, geschlossen={c}")
        rep.append(f"3MF {o.get('name')}: geschlossen={c}")
    if not closed_all:
        rep.append("RESULT 3MF: FEHLER"); ok = False

    # Previews
    items = [(asm_parts[n], cols[n], (0, 0, 0)) for n in cols if n in asm_parts and n not in ("cell_dummy", "charger_dummy")]
    if CYL:   # Zelle/Modul im Gehaeuse sind von hinten durch den Koerper verdeckt, daher nur in der Explosion
        pass
    render_png(items, os.path.join(OUT, "preview_front.png"), 30, -60, "Vorderseite")
    render_png(items, os.path.join(OUT, "preview_back.png"), -35, 140, "Rueckseite")
    ex = [(asm_parts["body"], cols["body"], (0, 0, 0)),
          (asm_parts["board_dummy"], cols["board_dummy"], (0, 0, 22)),
          (asm_parts["front_bezel"], cols["front_bezel"], (0, 0, 45)),
          (asm_parts["battery_door"], cols["battery_door"], (0, 0, -25)),
          (asm_parts["vent_clip"], cols["vent_clip"], (0, 0, -30))]
    for n, dz in (("charger_lid", -25), ("cell_dummy", -22), ("charger_dummy", -25)):
        if n in asm_parts:
            ex.append((asm_parts[n], cols[n], (0, 0, dz)))
    render_png(ex, os.path.join(OUT, "preview_exploded.png"), 25, -50, "Explosion")
    render_png([(p, "tan", (0, 0, 0)) for _, p in plate_parts], os.path.join(OUT, "preview_plate.png"), 70, -90, "Druckplatte")
    render_png([(asm_parts["body"], "gray70", (0, 0, 0))], os.path.join(OUT, "preview_body_back.png"), -50, 130, "Koerper hinten")
    render_png([(asm_parts["body"], "gray70", (0, 0, 0))], os.path.join(OUT, "preview_body_front.png"), 40, -60, "Koerper vorne")
    with open(os.path.join(OUT, "check_report.txt"), "w") as f:
        f.write("\n".join(rep) + "\n")
    try:   # optional: GLB fuer den Web-3D-Viewer (braucht cascadio, trimesh)
        import cascadio, trimesh  # noqa: F401
    except ImportError:
        print("cascadio/trimesh fehlt -> GLB uebersprungen (siehe step_to_glb.py)")
    else:
        try:
            import step_to_glb
            step_to_glb.main(OUT)
        except Exception as exc:   # GLB-Export darf Pruefergebnis/Exit-Code nicht beeinflussen
            print(f"WARNUNG: GLB-Export fehlgeschlagen: {exc}")
    return 0 if ok else 1

if __name__ == "__main__":
    if "--all" in sys.argv[1:]:   # beide Varianten nacheinander (je ein eigener Prozess, da die Parameter beim Import festgelegt werden)
        rc = 0
        for t in BATTERIES:
            rc |= subprocess.call([sys.executable, os.path.abspath(__file__), "--battery", t])
        sys.exit(rc)
    sys.exit(main())
