/*
 * its_mapspat.c - UPER decoders for MAPEM and SPATEM (ETSI TS 103 301,
 * ISO/TS 19091 "DSRC" module, SAE J2735).
 *
 * The decoding logic (field order, preamble bits, constraints) is derived
 * from the Kotlin implementation of cits-to-go
 * (https://github.com/OpenTrafficMap/cits-to-go, GPL-3.0), and was verified
 * against the ASN.1 module. In contrast to the reference, extension markers
 * are not rejected: SEQUENCE extension additions, CHOICE extension
 * alternatives and extensible ENUMERATED values are skipped via their
 * open-type length. Regional extensions are skipped as open types.
 *
 * Every read is bounds-checked (uper.c). On any error the decoders return
 * -1 and the caller's output is unspecified; nothing outside 'out' is touched.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <math.h>
#include <string.h>

#include "its.h"
#include "uper.h"

#define ITS_PDU_HDR 6u

/* ------------------------------------------------------------------ */
/* Shared sub-structures                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t region;     /* 0 = absent */
    uint16_t id;
} ref_id_t;

static ref_id_t read_ref_id(uper_t *u)
{
    ref_id_t r = { 0, 0 };
    bool has_region = uper_bit(u);
    if (has_region) {
        r.region = (uint16_t)uper_get(u, 16);
    }
    r.id = (uint16_t)uper_get(u, 16);
    return r;
}

static void skip_speed_limits(uper_t *u)   /* SpeedLimitList */
{
    int n = (int)uper_constrained(u, 1, 9);
    for (int i = 0; i < n && !u->err; i++) {
        (void)uper_enum_ext(u, 13);          /* SpeedLimitType */
        uper_skip(u, 13);                    /* Velocity 0..8191 */
    }
}

static void skip_position3d(uper_t *u, int32_t *lat, int32_t *lon)
{
    bool ext = uper_bit(u);
    bool has_elev = uper_bit(u);
    bool has_reg = uper_bit(u);
    *lat = (int32_t)uper_constrained(u, -900000000, 900000001);
    *lon = (int32_t)uper_constrained(u, -1800000000, 1800000001);
    if (has_elev) {
        uper_skip(u, 16);
    }
    if (has_reg) {
        uper_skip_regional_list(u);
    }
    if (ext) {
        uper_skip_ext_additions(u);
    }
}

/* ------------------------------------------------------------------ */
/* MAPEM                                                               */
/* ------------------------------------------------------------------ */

static int32_t clamp_i32(int64_t v)
{
    if (v > INT32_MAX) { return INT32_MAX; }
    if (v < INT32_MIN) { return INT32_MIN; }
    return (int32_t)v;
}

/* AllowedManeuvers: 12-bit string, named bit 0 is transmitted first (MSB). */
static uint16_t read_maneuvers(uper_t *u)
{
    uint32_t v = (uint32_t)uper_get(u, 12);
    uint16_t m = 0;
    for (unsigned i = 0; i < 11; i++) {
        if ((v >> (11 - i)) & 1u) {
            m |= (uint16_t)(1u << i);
        }
    }
    return m;
}

/* LaneTypeAttributes (inside LaneAttributes). Returns its_lane_type_t. */
static uint8_t read_lane_type(uper_t *u)
{
    bool is_ext = false;
    uint32_t idx = uper_choice_ext(u, 8, &is_ext);
    if (is_ext) {
        return ITS_LANE_TYPE_OTHER;
    }
    if (idx == 0) {                          /* LaneAttributes-Vehicle BIT STRING (SIZE(8,...)) */
        if (uper_bit(u)) {
            int32_t n = uper_length(u);
            if (n >= 0) {
                uper_skip(u, (size_t)n);
            }
        } else {
            uper_skip(u, 8);
        }
    } else {
        uper_skip(u, 16);                    /* crosswalk, bike, sidewalk, barrier, striping, tracked, parking */
    }
    return (uint8_t)idx;
}

typedef struct {
    int32_t ref_lat, ref_lon;
} map_ctx_t;

/* NodeOffsetPointXY -> (dx, dy) in cm, or absolute position for node-LatLon. */
static void read_node_offset(uper_t *u, const map_ctx_t *ctx,
                             int32_t *dx, int32_t *dy, bool *absolute)
{
    static const uint8_t bits[6] = { 10, 11, 12, 13, 14, 16 };
    *absolute = false;
    *dx = *dy = 0;
    uint32_t idx = (uint32_t)uper_get(u, 3);
    if (idx < 6) {
        unsigned w = bits[idx];
        int64_t lim = (int64_t)1 << (w - 1);
        *dx = (int32_t)uper_constrained(u, -lim, lim - 1);
        *dy = (int32_t)uper_constrained(u, -lim, lim - 1);
    } else if (idx == 6) {                   /* node-LatLon: Node-LLmD-64b { lon, lat } absolute */
        int64_t lon = uper_constrained(u, -1800000000, 1800000001);
        int64_t lat = uper_constrained(u, -900000000, 900000001);
        double cl = cos((double)ctx->ref_lat * 1e-7 * (3.14159265358979323846 / 180.0));
        /* 1e-7 degree = 1.11195 cm (mean earth radius) */
        *dx = clamp_i32((int64_t)llround((double)(lon - ctx->ref_lon) * cl * 1.11195));
        *dy = clamp_i32((int64_t)llround((double)(lat - ctx->ref_lat) * 1.11195));
        *absolute = true;
    } else {                                 /* regional */
        uper_skip_regional(u);
    }
}

static void skip_node_attributes(uper_t *u)  /* NodeAttributeSetXY */
{
    bool ext = uper_bit(u);
    bool has_local = uper_bit(u);
    bool has_disabled = uper_bit(u);
    bool has_enabled = uper_bit(u);
    bool has_data = uper_bit(u);
    bool has_width = uper_bit(u);
    bool has_elev = uper_bit(u);
    bool has_reg = uper_bit(u);
    if (has_local) {
        int n = (int)uper_constrained(u, 1, 8);
        for (int i = 0; i < n && !u->err; i++) {
            (void)uper_enum_ext(u, 12);      /* NodeAttributeXY */
        }
    }
    if (has_disabled) {
        int n = (int)uper_constrained(u, 1, 8);
        for (int i = 0; i < n && !u->err; i++) {
            (void)uper_enum_ext(u, 38);      /* SegmentAttributeXY */
        }
    }
    if (has_enabled) {
        int n = (int)uper_constrained(u, 1, 8);
        for (int i = 0; i < n && !u->err; i++) {
            (void)uper_enum_ext(u, 38);
        }
    }
    if (has_data) {
        int n = (int)uper_constrained(u, 1, 8);
        for (int i = 0; i < n && !u->err; i++) {   /* LaneDataAttribute CHOICE */
            bool is_ext = false;
            uint32_t idx = uper_choice_ext(u, 7, &is_ext);
            if (is_ext) {
                continue;
            }
            switch (idx) {
            case 0: uper_skip(u, 9); break;        /* DeltaAngle -150..150 */
            case 1: case 2: case 3: uper_skip(u, 8); break;   /* RoadwayCrownAngle */
            case 4: uper_skip(u, 9); break;        /* MergeDivergeNodeAngle -180..180 */
            case 5: skip_speed_limits(u); break;
            case 6: uper_skip_regional_list(u); break;
            default: u->err = true; break;
            }
        }
    }
    if (has_width) { uper_skip(u, 10); }
    if (has_elev) { uper_skip(u, 10); }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
}

static void skip_computed_lane(uper_t *u)
{
    bool ext = uper_bit(u);
    bool has_rot = uper_bit(u);
    bool has_sx = uper_bit(u);
    bool has_sy = uper_bit(u);
    bool has_reg = uper_bit(u);
    uper_skip(u, 8);                         /* referenceLaneId */
    for (int axis = 0; axis < 2; axis++) {   /* offsetXaxis / offsetYaxis CHOICE { small, large } */
        uper_skip(u, uper_bit(u) ? 16 : 12);
    }
    if (has_rot) { uper_skip(u, 15); }       /* Angle 0..28800 */
    if (has_sx) { uper_skip(u, 12); }
    if (has_sy) { uper_skip(u, 12); }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
}

static void read_lane(uper_t *u, const map_ctx_t *ctx, its_lane_t *L)
{
    memset(L, 0, sizeof(*L));
    bool ext = uper_bit(u);
    bool has_name = uper_bit(u);
    bool has_in = uper_bit(u);
    bool has_eg = uper_bit(u);
    bool has_mnv = uper_bit(u);
    bool has_conn = uper_bit(u);
    bool has_ovl = uper_bit(u);
    bool has_reg = uper_bit(u);

    L->lane_id = (uint8_t)uper_get(u, 8);
    if (has_name) {
        uper_ia5(u, 1, 63, NULL, 0);
    }
    if (has_in) { L->ingress_approach = (uint8_t)uper_get(u, 4); }
    if (has_eg) { L->egress_approach = (uint8_t)uper_get(u, 4); }

    /* LaneAttributes (not extensible): regional?, directionalUse(2), sharedWith(10), laneType */
    bool attr_reg = uper_bit(u);
    uint32_t dir = (uint32_t)uper_get(u, 2);
    uper_skip(u, 10);
    L->lane_type = read_lane_type(u);
    if (attr_reg) {
        uper_skip_regional(u);
    }
    L->direction = (uint8_t)(((dir & 2u) ? ITS_LANE_INGRESS : 0) | ((dir & 1u) ? ITS_LANE_EGRESS : 0));

    if (has_mnv) {
        L->maneuvers = read_maneuvers(u);
    }

    /* NodeListXY CHOICE { nodes, computed, ... } */
    bool is_ext = false;
    uint32_t nl = uper_choice_ext(u, 2, &is_ext);
    if (!is_ext && nl == 0) {
        int n = (int)uper_constrained(u, 2, 63);
        int32_t x = 0, y = 0;
        int next_k = 0;
        for (int i = 0; i < n && !u->err; i++) {
            bool nx_ext = uper_bit(u);
            bool nx_attr = uper_bit(u);
            int32_t dx, dy;
            bool absolute;
            read_node_offset(u, ctx, &dx, &dy, &absolute);
            if (absolute) {
                x = dx;
                y = dy;
            } else {
                x = clamp_i32((int64_t)x + dx);
                y = clamp_i32((int64_t)y + dy);
            }
            if (nx_attr) {
                skip_node_attributes(u);
            }
            if (nx_ext) {
                uper_skip_ext_additions(u);
            }
            /* keep up to ITS_MAX_LANE_NODES, evenly sub-sampled, first and last included */
            bool keep;
            if (n <= ITS_MAX_LANE_NODES) {
                keep = true;
            } else {
                keep = next_k < ITS_MAX_LANE_NODES &&
                       i == (next_k * (n - 1) + (ITS_MAX_LANE_NODES - 1) / 2) / (ITS_MAX_LANE_NODES - 1);
            }
            if (keep && L->n_nodes < ITS_MAX_LANE_NODES) {
                L->node_x_cm[L->n_nodes] = x;
                L->node_y_cm[L->n_nodes] = y;
                L->n_nodes++;
                next_k++;
            }
        }
    } else if (!is_ext && nl == 1) {
        skip_computed_lane(u);
    }

    if (has_conn) {
        int n = (int)uper_constrained(u, 1, 16);
        for (int i = 0; i < n && !u->err; i++) {
            bool has_remote = uper_bit(u);
            bool has_sg = uper_bit(u);
            bool has_uc = uper_bit(u);
            bool has_cid = uper_bit(u);
            bool has_cm = uper_bit(u);                 /* ConnectingLane.maneuver */
            its_connection_t c;
            memset(&c, 0, sizeof(c));
            c.connecting_lane = (uint8_t)uper_get(u, 8);
            if (has_cm) { c.connecting_maneuver = read_maneuvers(u); }
            if (has_remote) { (void)read_ref_id(u); }
            if (has_sg) { c.signal_group = (uint8_t)uper_get(u, 8); }
            if (has_uc) { uper_skip(u, 8); }
            if (has_cid) { uper_skip(u, 8); }
            if (!u->err && L->n_connections < ITS_MAX_CONNECTIONS) {
                L->conn[L->n_connections++] = c;
            }
        }
    }
    if (has_ovl) {
        int n = (int)uper_constrained(u, 1, 5);
        uper_skip(u, (size_t)n * 8u);
    }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
}

static void read_geometry(uper_t *u, its_map_intersection_t *m)
{
    memset(m, 0, sizeof(*m));
    bool ext = uper_bit(u);
    bool has_name = uper_bit(u);
    bool has_lw = uper_bit(u);
    bool has_sl = uper_bit(u);
    bool has_pp = uper_bit(u);
    bool has_reg = uper_bit(u);
    if (has_name) {
        uper_ia5(u, 1, 63, m->name, sizeof(m->name));
    }
    ref_id_t id = read_ref_id(u);
    m->region = id.region;
    m->id = id.id;
    m->revision = (uint8_t)uper_get(u, 7);
    map_ctx_t ctx;
    skip_position3d(u, &ctx.ref_lat, &ctx.ref_lon);
    m->ref_lat = ctx.ref_lat;
    m->ref_lon = ctx.ref_lon;
    if (has_lw) { uper_skip(u, 15); }
    if (has_sl) { skip_speed_limits(u); }

    int n_lanes = (int)uper_constrained(u, 1, 255);
    its_lane_t tmp;
    for (int i = 0; i < n_lanes && !u->err; i++) {
        bool store = m->n_lanes < ITS_MAX_LANES;
        read_lane(u, &ctx, store ? &m->lanes[m->n_lanes] : &tmp);
        if (store && !u->err) {
            m->n_lanes++;
        }
    }
    if (has_pp) {
        int n = (int)uper_constrained(u, 1, 32);
        for (int i = 0; i < n && !u->err; i++) {   /* SignalControlZone { zone RegionalExtension, ... } */
            bool zext = uper_bit(u);
            uper_skip_regional(u);
            if (zext) { uper_skip_ext_additions(u); }
        }
    }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
}

int its_decode_mapem(const its_packet_t *pkt, its_map_intersection_t *out, int max_out)
{
    if (pkt == NULL || out == NULL || max_out < 1 || pkt->pdu == NULL ||
        pkt->message_id != ITS_MSG_MAPEM || pkt->pdu_len < ITS_PDU_HDR) {
        return -1;
    }
    uper_t u;
    uper_init(&u, pkt->pdu + ITS_PDU_HDR, pkt->pdu_len - ITS_PDU_HDR);

    (void)uper_bit(&u);                         /* MapData extension marker (additions follow the root) */
    bool has_ts = uper_bit(&u);
    bool has_lt = uper_bit(&u);
    bool has_lid = uper_bit(&u);
    bool has_isec = uper_bit(&u);
    /* roadSegments, dataParameters, restrictionList, regional follow the intersections
     * and are not needed: decoding stops after the intersection list. */
    uper_skip(&u, 4);
    if (has_ts) { uper_skip(&u, 20); }
    uper_skip(&u, 7);                           /* msgIssueRevision */
    if (has_lt) { (void)uper_enum_ext(&u, 8); }
    if (has_lid) { (void)uper_constrained(&u, 0, 100); }
    if (u.err) {
        return -1;
    }
    if (!has_isec) {
        return 0;
    }
    int count = (int)uper_constrained(&u, 1, 32);
    int n = 0;
    while (n < count && n < max_out && !u.err) {
        read_geometry(&u, &out[n]);
        if (u.err) {
            return -1;
        }
        n++;
    }
    return u.err ? -1 : n;
}

/* ------------------------------------------------------------------ */
/* SPATEM                                                              */
/* ------------------------------------------------------------------ */

static void skip_maneuver_assist_list(uper_t *u)
{
    int n = (int)uper_constrained(u, 1, 16);
    for (int i = 0; i < n && !u->err; i++) {   /* ConnectionManeuverAssist */
        bool ext = uper_bit(u);
        bool has_q = uper_bit(u);
        bool has_a = uper_bit(u);
        bool has_w = uper_bit(u);
        bool has_p = uper_bit(u);
        bool has_r = uper_bit(u);
        uper_skip(u, 8);                       /* connectionID */
        if (has_q) { uper_skip(u, 14); }       /* ZoneLength 0..10000 */
        if (has_a) { uper_skip(u, 14); }
        if (has_w) { uper_skip(u, 1); }
        if (has_p) { uper_skip(u, 1); }
        if (has_r) { uper_skip_regional_list(u); }
        if (ext) { uper_skip_ext_additions(u); }
    }
}

static void read_movement_event(uper_t *u, its_signal_state_t *st /* NULL: skip */)
{
    bool ext = uper_bit(u);
    bool has_timing = uper_bit(u);
    bool has_speeds = uper_bit(u);
    bool has_reg = uper_bit(u);
    uint32_t phase = (uint32_t)uper_constrained(u, 0, 9);
    uint16_t start = ITS_TIMEMARK_UNKNOWN, minend = ITS_TIMEMARK_UNKNOWN;
    uint16_t maxend = ITS_TIMEMARK_UNKNOWN, likely = ITS_TIMEMARK_UNKNOWN;
    if (has_timing) {                          /* TimeChangeDetails (not extensible) */
        bool h_start = uper_bit(u);
        bool h_max = uper_bit(u);
        bool h_likely = uper_bit(u);
        bool h_conf = uper_bit(u);
        bool h_next = uper_bit(u);
        if (h_start) { start = (uint16_t)uper_constrained(u, 0, 36001); }
        minend = (uint16_t)uper_constrained(u, 0, 36001);
        if (h_max) { maxend = (uint16_t)uper_constrained(u, 0, 36001); }
        if (h_likely) { likely = (uint16_t)uper_constrained(u, 0, 36001); }
        if (h_conf) { uper_skip(u, 4); }
        if (h_next) { uper_skip(u, 16); }
    }
    if (has_speeds) {
        int n = (int)uper_constrained(u, 1, 16);
        for (int i = 0; i < n && !u->err; i++) {   /* AdvisorySpeed */
            bool aext = uper_bit(u);
            bool h_speed = uper_bit(u);
            bool h_conf = uper_bit(u);
            bool h_dist = uper_bit(u);
            bool h_class = uper_bit(u);
            bool h_reg = uper_bit(u);
            (void)uper_enum_ext(u, 4);
            if (h_speed) { uper_skip(u, 9); }
            if (h_conf) { uper_skip(u, 3); }
            if (h_dist) { uper_skip(u, 14); }
            if (h_class) { uper_skip(u, 8); }
            if (h_reg) { uper_skip_regional_list(u); }
            if (aext) { uper_skip_ext_additions(u); }
        }
    }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
    if (st != NULL && !u->err) {
        st->phase = (uint8_t)phase;
        st->start_time = start;
        st->min_end_time = minend;
        st->max_end_time = maxend;
        st->likely_time = likely;
    }
}

static void read_intersection_state(uper_t *u, its_spat_intersection_t *s)
{
    memset(s, 0, sizeof(*s));
    bool ext = uper_bit(u);
    bool has_name = uper_bit(u);
    bool has_moy = uper_bit(u);
    bool has_ts = uper_bit(u);
    bool has_el = uper_bit(u);
    bool has_ma = uper_bit(u);
    bool has_reg = uper_bit(u);
    if (has_name) {
        uper_ia5(u, 1, 63, s->name, sizeof(s->name));
    }
    ref_id_t id = read_ref_id(u);
    s->region = id.region;
    s->id = id.id;
    s->revision = (uint8_t)uper_get(u, 7);
    uper_skip(u, 16);                          /* status */
    if (has_moy) {
        s->moy = (uint32_t)uper_constrained(u, 0, 527040);
        s->has_moy = !u->err;
    }
    if (has_ts) {
        s->timestamp_ms = (uint16_t)uper_get(u, 16);
        s->has_timestamp = !u->err;
    }
    if (has_el) {
        int n = (int)uper_constrained(u, 1, 16);
        uper_skip(u, (size_t)n * 8u);
    }
    int n_states = (int)uper_constrained(u, 1, 255);
    for (int i = 0; i < n_states && !u->err; i++) {   /* MovementState */
        bool mext = uper_bit(u);
        bool h_name = uper_bit(u);
        bool h_ma = uper_bit(u);
        bool h_reg = uper_bit(u);
        if (h_name) { uper_ia5(u, 1, 63, NULL, 0); }
        uint8_t sg = (uint8_t)uper_get(u, 8);
        int n_ev = (int)uper_constrained(u, 1, 16);
        its_signal_state_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.signal_group = sg;
        for (int e = 0; e < n_ev && !u->err; e++) {
            read_movement_event(u, e == 0 ? &tmp : NULL);
        }
        if (h_ma) { skip_maneuver_assist_list(u); }
        if (h_reg) { uper_skip_regional_list(u); }
        if (mext) { uper_skip_ext_additions(u); }
        if (!u->err && s->n_states < ITS_MAX_SIGNAL_STATES) {
            s->states[s->n_states++] = tmp;
        }
    }
    if (has_ma) { skip_maneuver_assist_list(u); }
    if (has_reg) { uper_skip_regional_list(u); }
    if (ext) { uper_skip_ext_additions(u); }
}

int its_decode_spatem(const its_packet_t *pkt, its_spat_intersection_t *out, int max_out)
{
    if (pkt == NULL || out == NULL || max_out < 1 || pkt->pdu == NULL ||
        pkt->message_id != ITS_MSG_SPATEM || pkt->pdu_len < ITS_PDU_HDR) {
        return -1;
    }
    uper_t u;
    uper_init(&u, pkt->pdu + ITS_PDU_HDR, pkt->pdu_len - ITS_PDU_HDR);

    (void)uper_bit(&u);                         /* SPAT extension marker */
    bool has_ts = uper_bit(&u);
    bool has_name = uper_bit(&u);
    (void)uper_bit(&u);                         /* regional (after intersections: not needed) */
    if (has_ts) { uper_skip(&u, 20); }
    if (has_name) { uper_ia5(&u, 1, 63, NULL, 0); }
    int count = (int)uper_constrained(&u, 1, 32);
    int n = 0;
    while (n < count && n < max_out && !u.err) {
        read_intersection_state(&u, &out[n]);
        if (u.err) {
            return -1;
        }
        n++;
    }
    return u.err ? -1 : n;
}
