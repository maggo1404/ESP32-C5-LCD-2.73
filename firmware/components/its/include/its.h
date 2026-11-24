/*
 * its.h - ITS-G5 (C-ITS) frame parser, message decoders and state store.
 *
 * CONTRACT HEADER: shared between the decoder component (components/its)
 * and the application/UI (main). Pure C99, no ESP-IDF dependencies, so it
 * can be unit-tested on the host. Do not change existing fields without
 * updating both sides.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* ITS PDU message IDs (ETSI TS 102 894-2 ItsPduHeader.messageId)      */
/* ------------------------------------------------------------------ */
typedef enum {
    ITS_MSG_UNKNOWN = 0,
    ITS_MSG_DENM    = 1,
    ITS_MSG_CAM     = 2,
    ITS_MSG_POI     = 3,
    ITS_MSG_SPATEM  = 4,
    ITS_MSG_MAPEM   = 5,
    ITS_MSG_IVIM    = 6,
    ITS_MSG_EV_RSR  = 7,
    ITS_MSG_TISTPGM = 8,
    ITS_MSG_SREM    = 9,
    ITS_MSG_SSEM    = 10,
    ITS_MSG_EVCSN   = 11,
    ITS_MSG_SAEM    = 12,
    ITS_MSG_RTCMEM  = 13,
    ITS_MSG_CPM     = 14,
    ITS_MSG_IMZM    = 15,
    ITS_MSG_VAM     = 16,
    ITS_MSG_DSM     = 17,
    ITS_MSG_PCIM    = 18,
    ITS_MSG_PCVM    = 19,
    ITS_MSG_MCM     = 20,
    ITS_MSG_PAM     = 21,
    ITS_MSG_COUNT   = 32   /* size of per-type counter arrays */
} its_msg_id_t;

/* ------------------------------------------------------------------ */
/* Layer 2..4 parse result                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    ITS_OK = 0,
    ITS_ERR_TOO_SHORT,
    ITS_ERR_NOT_GN,        /* LLC/SNAP ethertype is not 0x8947 */
    ITS_ERR_GN_UNSUPPORTED,
    ITS_ERR_SECURITY,      /* secured packet that could not be unwrapped */
    ITS_ERR_BTP,
    ITS_ERR_DECODE
} its_err_t;

typedef struct {
    uint8_t  src_mac[6];
    uint8_t  gn_header_type;   /* GN common header HT (1=beacon,4=GBC,5=TSB/SHB ...) */
    uint8_t  gn_header_subtype;
    bool     secured;          /* GN basic header NH == 2 (ETSI TS 103 097) */
    uint16_t btp_dst_port;     /* 2001 CAM, 2002 DENM, 2003 MAPEM, 2004 SPATEM ... */
    uint8_t  protocol_version; /* ItsPduHeader.protocolVersion */
    uint8_t  message_id;       /* its_msg_id_t */
    uint32_t station_id;       /* ItsPduHeader.stationId */
    const uint8_t *pdu;        /* points INTO the frame: first byte of ItsPduHeader */
    size_t   pdu_len;
} its_packet_t;

/* Parse an IEEE 802.11 frame (as delivered by the ESP promiscuous callback,
 * FCS already removed): 802.11 header -> LLC/SNAP -> GeoNetworking (basic,
 * optional security envelope, common, extended) -> BTP-B -> ItsPduHeader. */
its_err_t its_parse_frame(const uint8_t *frame, size_t len, its_packet_t *out);

/* ------------------------------------------------------------------ */
/* Decoded messages (only fields we display)                           */
/* ------------------------------------------------------------------ */
#define ITS_LAT_UNAVAILABLE  900000001   /* 1e-7 degree units */
#define ITS_LON_UNAVAILABLE  1800000001
#define ITS_SPEED_UNAVAILABLE 16383      /* 0.01 m/s */
#define ITS_HEADING_UNAVAILABLE 3601     /* 0.1 degree */

typedef struct {
    uint32_t station_id;
    uint8_t  station_type;   /* 0 unknown,1 pedestrian,...,5 passengerCar,6 bus,
                                7 lightTruck,8 heavyTruck,10 specialVehicles,
                                11 tram,15 roadSideUnit */
    int32_t  lat, lon;       /* 1e-7 deg */
    uint16_t speed;          /* 0.01 m/s */
    uint16_t heading;        /* 0.1 deg */
} its_cam_t;

typedef struct {
    uint32_t station_id;     /* originating station of actionID */
    uint16_t sequence_number;
    uint8_t  cause_code;     /* 0 = not present */
    uint8_t  sub_cause_code;
    int32_t  lat, lon;       /* eventPosition, 1e-7 deg */
    bool     termination;
} its_denm_t;

#define ITS_MAX_LANES        48
#define ITS_MAX_LANE_NODES   8
#define ITS_MAX_CONNECTIONS  4
#define ITS_MAX_SIGNAL_STATES 32
#define ITS_NAME_LEN         32

/* lane direction flags (LaneDirection bitstring) */
#define ITS_LANE_INGRESS 0x01
#define ITS_LANE_EGRESS  0x02

/* LaneTypeAttributes CHOICE index */
typedef enum {
    ITS_LANE_VEHICLE = 0, ITS_LANE_CROSSWALK, ITS_LANE_BIKE, ITS_LANE_SIDEWALK,
    ITS_LANE_MEDIAN, ITS_LANE_STRIPING, ITS_LANE_TRACKED, ITS_LANE_PARKING,
    ITS_LANE_TYPE_OTHER
} its_lane_type_t;

/* AllowedManeuvers bits (bit 0 = maneuverStraightAllowed ... ) */
#define ITS_MNV_STRAIGHT   (1u << 0)
#define ITS_MNV_LEFT       (1u << 1)
#define ITS_MNV_RIGHT      (1u << 2)
#define ITS_MNV_UTURN      (1u << 3)
#define ITS_MNV_LEFT_ON_RED  (1u << 4)
#define ITS_MNV_RIGHT_ON_RED (1u << 5)
#define ITS_MNV_LANE_CHANGE  (1u << 6)
#define ITS_MNV_NO_STOP      (1u << 7)
#define ITS_MNV_YIELD        (1u << 8)
#define ITS_MNV_GO_WITH_HALT (1u << 9)
#define ITS_MNV_CAUTION      (1u << 10)

typedef struct {
    uint8_t  connecting_lane;   /* lane id in same intersection */
    uint16_t connecting_maneuver; /* ITS_MNV_* or 0 if absent */
    uint8_t  signal_group;      /* 0 = none */
} its_connection_t;

typedef struct {
    uint8_t  lane_id;
    uint8_t  direction;          /* ITS_LANE_INGRESS / ITS_LANE_EGRESS */
    uint8_t  lane_type;          /* its_lane_type_t */
    uint8_t  ingress_approach;   /* 0 = absent */
    uint8_t  egress_approach;    /* 0 = absent */
    uint16_t maneuvers;          /* ITS_MNV_* , 0 if absent */
    uint8_t  n_nodes;
    int32_t  node_x_cm[ITS_MAX_LANE_NODES]; /* ABSOLUTE offset from ref point (east), cm */
    int32_t  node_y_cm[ITS_MAX_LANE_NODES]; /* ABSOLUTE offset from ref point (north), cm */
    uint8_t  n_connections;
    its_connection_t conn[ITS_MAX_CONNECTIONS];
} its_lane_t;

typedef struct {
    uint16_t region;            /* 0 = absent */
    uint16_t id;
    uint8_t  revision;
    char     name[ITS_NAME_LEN];
    int32_t  ref_lat, ref_lon;  /* 1e-7 deg */
    uint8_t  n_lanes;
    its_lane_t lanes[ITS_MAX_LANES];
} its_map_intersection_t;

/* MovementPhaseState */
typedef enum {
    ITS_PHASE_UNAVAILABLE = 0, ITS_PHASE_DARK = 1, ITS_PHASE_STOP_THEN_PROCEED = 2,
    ITS_PHASE_STOP_AND_REMAIN = 3, ITS_PHASE_PRE_MOVEMENT = 4,
    ITS_PHASE_PERMISSIVE_MOVEMENT_ALLOWED = 5, ITS_PHASE_PROTECTED_MOVEMENT_ALLOWED = 6,
    ITS_PHASE_PERMISSIVE_CLEARANCE = 7, ITS_PHASE_PROTECTED_CLEARANCE = 8,
    ITS_PHASE_CAUTION_CONFLICTING_TRAFFIC = 9
} its_phase_t;

#define ITS_TIMEMARK_UNKNOWN 36001  /* TimeMark: 1/10 s since start of UTC hour */

typedef struct {
    uint8_t  signal_group;
    uint8_t  phase;              /* its_phase_t of the FIRST MovementEvent */
    uint16_t start_time;         /* TimeMark or ITS_TIMEMARK_UNKNOWN */
    uint16_t min_end_time;
    uint16_t max_end_time;
    uint16_t likely_time;
} its_signal_state_t;

typedef struct {
    uint16_t region;
    uint16_t id;
    uint8_t  revision;
    char     name[ITS_NAME_LEN];
    bool     has_moy;
    uint32_t moy;                /* minute of the year */
    bool     has_timestamp;
    uint16_t timestamp_ms;       /* DSecond, ms within minute */
    uint8_t  n_states;
    its_signal_state_t states[ITS_MAX_SIGNAL_STATES];
} its_spat_intersection_t;

bool its_decode_cam(const its_packet_t *pkt, its_cam_t *out);
bool its_decode_denm(const its_packet_t *pkt, its_denm_t *out);
/* return number of intersections written to out (0..max_out), -1 on error */
int  its_decode_mapem(const its_packet_t *pkt, its_map_intersection_t *out, int max_out);
int  its_decode_spatem(const its_packet_t *pkt, its_spat_intersection_t *out, int max_out);

/* ------------------------------------------------------------------ */
/* Human readable names (German UI)                                    */
/* ------------------------------------------------------------------ */
const char *its_msg_name(uint8_t message_id);       /* "CAM", "SPATEM", ... */
const char *its_station_type_name(uint8_t type);    /* "Pkw", "Bus", "Straßenbahn", "RSU" ... */
const char *its_cause_name(uint8_t cause_code);     /* "Stau", "Baustelle", ... */
const char *its_phase_name(uint8_t phase);          /* "Rot", "Grün", "Gelb", ... */

/* Simplified light colour for drawing */
typedef enum { ITS_LIGHT_OFF, ITS_LIGHT_RED, ITS_LIGHT_RED_YELLOW, ITS_LIGHT_YELLOW,
               ITS_LIGHT_GREEN, ITS_LIGHT_FLASHING_YELLOW } its_light_t;
its_light_t its_phase_light(uint8_t phase);

/* ------------------------------------------------------------------ */
/* State store                                                         */
/* ------------------------------------------------------------------ */
#define ITS_STORE_MAX_STATIONS      64
#define ITS_STORE_MAX_INTERSECTIONS 12
#define ITS_STORE_MAX_DENMS         16
#define ITS_STORE_RECENT            16

typedef struct {
    uint32_t station_id;
    uint8_t  station_type;       /* from CAM, 15 for RSUs sending MAP/SPAT */
    uint8_t  mac[6];
    int64_t  last_seen_us;
    int8_t   rssi;
    uint32_t msg_mask;           /* bit (1<<message_id) for each type seen */
    uint32_t packets;
    bool     has_cam;
    its_cam_t cam;
} its_station_t;

typedef struct {
    bool     used;
    uint16_t region, id;
    char     name[ITS_NAME_LEN];
    uint32_t station_id;         /* RSU that sends it */
    int8_t   rssi;
    bool     has_map;
    int64_t  map_rx_us;
    its_map_intersection_t map;
    bool     has_spat;
    int64_t  spat_rx_us;
    its_spat_intersection_t spat;
    /* Time-in-hour (1/10 s) as reported by the last SPATEM that had moy+timestamp,
     * and local time (us) when it was received. Used to turn TimeMarks into
     * countdowns: now_tenths = spat_hour_tenths + (now_us - spat_clock_us)/100000 */
    bool     has_clock;
    uint32_t spat_hour_tenths;
    int64_t  spat_clock_us;
} its_store_intersection_t;

typedef struct {
    bool     used;
    int64_t  rx_us;
    its_denm_t denm;
    int8_t   rssi;
} its_store_denm_t;

typedef struct {
    int64_t  rx_us;
    uint8_t  message_id;
    uint32_t station_id;
    int8_t   rssi;
    uint16_t len;
} its_recent_t;

typedef struct {
    uint32_t total_frames;       /* everything handed to ingest */
    uint32_t its_frames;         /* parsed as GN/BTP successfully */
    uint32_t secured_frames;
    uint32_t decode_errors;
    uint32_t per_msg[ITS_MSG_COUNT];
    float    rate_per_s;         /* exponentially smoothed, updated by its_store_tick */
    int64_t  last_rx_us;
    int8_t   last_rssi;
    its_station_t stations[ITS_STORE_MAX_STATIONS];
    its_store_intersection_t intersections[ITS_STORE_MAX_INTERSECTIONS];
    its_store_denm_t denms[ITS_STORE_MAX_DENMS];
    its_recent_t recent[ITS_STORE_RECENT];  /* ring buffer */
    uint8_t  recent_head;
} its_store_t;

/* Platform hooks; weak no-op defaults in its_store.c, firmware overrides
 * them with a FreeRTOS mutex. */
void its_store_lock(void);
void its_store_unlock(void);

void its_store_init(void);
/* Parse + decode + merge one frame. Call WITHOUT holding the lock (it locks). */
void its_store_ingest(const uint8_t *frame, size_t len, int8_t rssi, int64_t now_us);
/* Update rate, expire entries older than the timeouts. Call ~1x/s. */
void its_store_tick(int64_t now_us);
void its_store_clear(void);
/* Pointer to the store; only read it between its_store_lock()/unlock(). */
const its_store_t *its_store_get(void);

/* Countdown helper: seconds*10 until 'timemark' for intersection 'is' at
 * local time now_us; returns -1 if unknown. Handles hour wrap-around. */
int32_t its_timemark_remaining_tenths(const its_store_intersection_t *is,
                                      uint16_t timemark, int64_t now_us);

#ifdef __cplusplus
}
#endif
