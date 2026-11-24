/* its_names.c - German display names. All strings are UTF-8.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "its.h"

const char *its_msg_name(uint8_t id)
{
    switch (id) {
    case ITS_MSG_DENM:    return "DENM";
    case ITS_MSG_CAM:     return "CAM";
    case ITS_MSG_POI:     return "POI";
    case ITS_MSG_SPATEM:  return "SPATEM";
    case ITS_MSG_MAPEM:   return "MAPEM";
    case ITS_MSG_IVIM:    return "IVIM";
    case ITS_MSG_EV_RSR:  return "EV-RSR";
    case ITS_MSG_TISTPGM: return "TISTPGM";
    case ITS_MSG_SREM:    return "SREM";
    case ITS_MSG_SSEM:    return "SSEM";
    case ITS_MSG_EVCSN:   return "EVCSN";
    case ITS_MSG_SAEM:    return "SAEM";
    case ITS_MSG_RTCMEM:  return "RTCMEM";
    case ITS_MSG_CPM:     return "CPM";
    case ITS_MSG_IMZM:    return "IMZM";
    case ITS_MSG_VAM:     return "VAM";
    case ITS_MSG_DSM:     return "DSM";
    case ITS_MSG_PCIM:    return "PCIM";
    case ITS_MSG_PCVM:    return "PCVM";
    case ITS_MSG_MCM:     return "MCM";
    case ITS_MSG_PAM:     return "PAM";
    default:              return "Unbekannt";
    }
}

const char *its_station_type_name(uint8_t type)
{
    switch (type) {
    case 1:  return "Fußgänger";
    case 2:  return "Radfahrer";
    case 3:  return "Moped";
    case 4:  return "Motorrad";
    case 5:  return "Pkw";
    case 6:  return "Bus";
    case 7:  return "Leicht-Lkw";
    case 8:  return "Schwer-Lkw";
    case 9:  return "Anhänger";
    case 10: return "Sonderfahrzeug";
    case 11: return "Straßenbahn";
    case 12: return "Leichtes VRU-Fahrzeug";
    case 13: return "Tier";
    case 14: return "Landmaschine";
    case 15: return "RSU";
    default: return "Unbekannt";
    }
}

/* Cause codes per ETSI TS 102 894-2 (CauseCodeType) */
const char *its_cause_name(uint8_t c)
{
    switch (c) {
    case 1:  return "Verkehrslage/Stau";
    case 2:  return "Unfall";
    case 3:  return "Baustelle";
    case 5:  return "Unpassierbar";
    case 6:  return "Witterung/Haftung";
    case 7:  return "Aquaplaning";
    case 9:  return "Gefahr auf Fahrbahn";
    case 10: return "Hindernis";
    case 11: return "Tier auf Fahrbahn";
    case 12: return "Personen auf Fahrbahn";
    case 14: return "Falschfahrer";
    case 15: return "Rettungsgasse";
    case 17: return "Wetter Extrem";
    case 18: return "Sicht";
    case 19: return "Niederschlag";
    case 26: return "Langsames Fahrzeug";
    case 27: return "Gefährliches Ende Stau";
    case 91: return "Panne";
    case 92: return "Unfallfahrzeug";
    case 93: return "Mensch in Gefahr";
    case 94: return "Stehendes Fahrzeug";
    case 95: return "Einsatzfahrzeug";
    case 96: return "Gefährliche Kurve";
    case 97: return "Kollisionsrisiko";
    case 98: return "Signalverletzung";
    case 99: return "Gefahrensituation";
    case 100: return "Bahnübergang";
    default: return c == 0 ? "Unbekannt" : "Sonstiges";
    }
}

const char *its_phase_name(uint8_t p)
{
    switch (p) {
    case ITS_PHASE_UNAVAILABLE:                  return "Unbekannt";
    case ITS_PHASE_DARK:                         return "Aus";
    case ITS_PHASE_STOP_THEN_PROCEED:            return "Rot (blinkend)";   /* Halt, dann fahren */
    case ITS_PHASE_STOP_AND_REMAIN:              return "Rot";
    case ITS_PHASE_PRE_MOVEMENT:                 return "Rot-Gelb";
    case ITS_PHASE_PERMISSIVE_MOVEMENT_ALLOWED:  return "Grün (bedingt)";
    case ITS_PHASE_PROTECTED_MOVEMENT_ALLOWED:   return "Grün";
    case ITS_PHASE_PERMISSIVE_CLEARANCE:         return "Gelb (bedingt)";
    case ITS_PHASE_PROTECTED_CLEARANCE:          return "Gelb";
    case ITS_PHASE_CAUTION_CONFLICTING_TRAFFIC:  return "Gelb blinkend";
    default:                                     return "Unbekannt";
    }
}

its_light_t its_phase_light(uint8_t p)
{
    switch (p) {
    case ITS_PHASE_STOP_THEN_PROCEED:
    case ITS_PHASE_STOP_AND_REMAIN:              return ITS_LIGHT_RED;
    case ITS_PHASE_PRE_MOVEMENT:                 return ITS_LIGHT_RED_YELLOW;
    case ITS_PHASE_PERMISSIVE_MOVEMENT_ALLOWED:
    case ITS_PHASE_PROTECTED_MOVEMENT_ALLOWED:   return ITS_LIGHT_GREEN;
    case ITS_PHASE_PERMISSIVE_CLEARANCE:
    case ITS_PHASE_PROTECTED_CLEARANCE:          return ITS_LIGHT_YELLOW;
    case ITS_PHASE_CAUTION_CONFLICTING_TRAFFIC:  return ITS_LIGHT_FLASHING_YELLOW;
    default:                                     return ITS_LIGHT_OFF;
    }
}
