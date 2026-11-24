/*
 * test_main.c - host tests for the ITS decoder component.
 * Build with ASan/UBSan (see CMakeLists.txt). SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "its.h"
#include "uper.h"
#include "vectors.h"

static int g_fail;
static int g_checks;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_fail++;                                                                     \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        long long va_ = (long long)(a), vb_ = (long long)(b);                             \
        g_checks++;                                                                       \
        if (va_ != vb_) {                                                                 \
            g_fail++;                                                                     \
            printf("  FAIL %s:%d: %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                 \
    } while (0)

/* ---- store lock instrumentation (overrides the weak defaults) ------------ */
static int g_lock_depth, g_lock_max, g_lock_calls;
void its_store_lock(void)
{
    g_lock_depth++;
    g_lock_calls++;
    if (g_lock_depth > g_lock_max) {
        g_lock_max = g_lock_depth;
    }
}
void its_store_unlock(void) { g_lock_depth--; }

/* exact-size heap copy so that ASan flags any out-of-bounds read */
static uint8_t *dup_exact(const uint8_t *p, size_t n)
{
    uint8_t *q = malloc(n ? n : 1);
    if (q == NULL) {
        abort();
    }
    if (n) {
        memcpy(q, p, n);
    }
    return q;
}

/* ====================================================================== */
static void test_uper(void)
{
    printf("uper\n");
    const uint8_t b1[] = { 0xA5, 0x0F, 0xF0 };
    uper_t u;
    uper_init(&u, b1, sizeof(b1));
    CHECK_EQ(uper_get(&u, 4), 0xA);
    CHECK_EQ(uper_get(&u, 8), 0x50);
    CHECK_EQ(uper_get(&u, 12), 0xFF0);
    CHECK(!u.err);
    CHECK_EQ(uper_get(&u, 1), 0);
    CHECK(u.err);

    /* constrained: negative lower bound, range check */
    const uint8_t b2[] = { 0x00, 0x01, 0xFF };
    uper_init(&u, b2, sizeof(b2));
    CHECK_EQ(uper_constrained(&u, -512, 511), -512);     /* 10 bits 0 */
    CHECK_EQ(uper_constrained(&u, 0, 16383), 0x1FF);        /* remaining 14 bits */
    CHECK(!u.err);
    uper_init(&u, b2, sizeof(b2));
    (void)uper_get(&u, 16);
    CHECK_EQ(uper_constrained(&u, 0, 200), 0);           /* 0xFF > 200 -> error */
    CHECK(u.err);
    uper_init(&u, b2, sizeof(b2));
    CHECK_EQ(uper_constrained(&u, 5, 5), 5);             /* range 1: zero bits */
    CHECK_EQ(u.pos, 0);

    /* 64 bit read */
    const uint8_t b3[8] = { 0x80, 0, 0, 0, 0, 0, 0, 1 };
    uper_init(&u, b3, 8);
    CHECK(uper_get(&u, 64) == 0x8000000000000001ull);

    /* length determinants */
    const uint8_t l1[] = { 0x05 };                        /* 0 + 0000101 */
    uper_init(&u, l1, 1);
    CHECK_EQ(uper_length(&u), 5);
    const uint8_t l2[] = { 0x80 | 0x01, 0x00 };           /* 10 + 14 bits = 0x0100 */
    uper_init(&u, l2, 2);
    CHECK_EQ(uper_length(&u), 0x100);
    const uint8_t l3[] = { 0xC1, 0x00 };                  /* 11 -> fragmentation unsupported */
    uper_init(&u, l3, 2);
    CHECK_EQ(uper_length(&u), -1);
    CHECK(u.err);

    /* normally small number: 0 + 000101 = 5 */
    const uint8_t n1[] = { 0x05 << 1 };
    uper_init(&u, n1, 1);
    CHECK_EQ(uper_normally_small(&u), 5);

    /* skip: open type of length 2 followed by marker byte 0xAB */
    const uint8_t o1[] = { 0x01, 0x23, 0x45, 0xAB };      /* length 0x01 -> bit 0 then 7 bits: 0000001=1? */
    uper_init(&u, o1, sizeof(o1));
    uper_skip_open(&u);                                   /* 0x01 = 0|0000001 -> len 1; skips 8 bits */
    CHECK(!u.err);
    CHECK_EQ(u.pos, 16);
    uper_skip(&u, 1000);
    CHECK(u.err);

    /* oversized open-type length must error, not overread */
    const uint8_t o2[] = { 0x7F, 0x00 };
    uper_init(&u, o2, sizeof(o2));
    uper_skip_open(&u);
    CHECK(u.err);

    /* NULL / zero length */
    uper_init(&u, NULL, 0);
    CHECK_EQ(uper_get(&u, 1), 0);
    CHECK(u.err);

    /* extension additions: count-1 = 1 (2 additions: bitmap 10 -> one present), open type len 1 */
    /* bits: 0 000001 | 1 0 | 0 0000001 | 0xFF | trailing 0xAA */
    /* 0000 0011 0000 0000 1000 0000 1111 1111 -> assemble manually */
    uint8_t e1[8];
    memset(e1, 0, sizeof(e1));
    {
        unsigned pos = 0;
        const char *pat = "0" "000001" "10" "0" "0000001" "11111111" "10101010";
        for (const char *c = pat; *c; c++, pos++) {
            if (*c == '1') {
                e1[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7));
            }
        }
    }
    uper_init(&u, e1, 5);
    uper_skip_ext_additions(&u);
    CHECK(!u.err);
    CHECK_EQ(uper_get(&u, 8), 0xAA);

    /* ia5 */
    {
        uint8_t s[8];
        memset(s, 0, sizeof(s));
        /* SIZE(1..63): len-1 = 2 in 6 bits -> "ab\x7f"?  use "Hi!" */
        unsigned pos = 0;
        const unsigned vals[] = { 2, 'H', 'i', '!' };
        const unsigned w[] = { 6, 7, 7, 7 };
        for (int i = 0; i < 4; i++) {
            for (int b = (int)w[i] - 1; b >= 0; b--, pos++) {
                if ((vals[i] >> b) & 1u) {
                    s[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7));
                }
            }
        }
        char dst[8];
        uper_init(&u, s, sizeof(s));
        uper_ia5(&u, 1, 63, dst, sizeof(dst));
        CHECK(!u.err);
        CHECK(strcmp(dst, "Hi!") == 0);
        /* truncated destination */
        char d2[3];
        uper_init(&u, s, sizeof(s));
        uper_ia5(&u, 1, 63, d2, sizeof(d2));
        CHECK(strcmp(d2, "Hi") == 0);
    }
    /* enum / choice ext */
    const uint8_t c1[] = { 0x80, 0x00, 0x00 };            /* ext bit set, normally small 0, then open type */
    uper_init(&u, c1, sizeof(c1));
    bool is_ext = false;
    (void)uper_choice_ext(&u, 4, &is_ext);
    CHECK(is_ext);
}

/* ====================================================================== */
static void print_hex_diff(const char *what, int idx, long long a, long long b)
{
    printf("    %s[%d]: got %lld expected %lld\n", what, idx, a, b);
}

#define CMPX(G, E, field)                                                                 \
    do {                                                                                  \
        g_checks++;                                                                       \
        if ((long long)(G)->field != (long long)(E)->field) {                             \
            g_fail++;                                                                     \
            printf("  FAIL %s: %s (%s) got %lld expected %lld\n", vname, #field, ctx,     \
                   (long long)(G)->field, (long long)(E)->field);                         \
        }                                                                                 \
    } while (0)
#define CMP(field) CMPX(got, exp, field)

static void cmp_map(const char *vname, const its_map_intersection_t *got, const its_map_intersection_t *exp, int idx)
{
    char ctx[64];
    snprintf(ctx, sizeof(ctx), "intersection %d", idx);
    CMP(region); CMP(id); CMP(revision); CMP(ref_lat); CMP(ref_lon); CMP(n_lanes);
    g_checks++;
    if (strcmp(got->name, exp->name) != 0) {
        g_fail++;
        printf("  FAIL %s: name '%s' vs '%s'\n", vname, got->name, exp->name);
    }
    for (int l = 0; l < exp->n_lanes && l < got->n_lanes; l++) {
        const its_lane_t *gl = &got->lanes[l], *el = &exp->lanes[l];
        snprintf(ctx, sizeof(ctx), "i%d lane %d(id %d)", idx, l, el->lane_id);
        CMPX(gl, el, lane_id); CMPX(gl, el, direction); CMPX(gl, el, lane_type);
        CMPX(gl, el, ingress_approach); CMPX(gl, el, egress_approach);
        CMPX(gl, el, maneuvers); CMPX(gl, el, n_nodes); CMPX(gl, el, n_connections);
        for (int n = 0; n < el->n_nodes && n < gl->n_nodes; n++) {
            CHECK_EQ(gl->node_x_cm[n], el->node_x_cm[n]);
            CHECK_EQ(gl->node_y_cm[n], el->node_y_cm[n]);
            if (gl->node_x_cm[n] != el->node_x_cm[n] || gl->node_y_cm[n] != el->node_y_cm[n]) {
                print_hex_diff("node", n, gl->node_x_cm[n], el->node_x_cm[n]);
            }
        }
        for (int c = 0; c < el->n_connections && c < gl->n_connections; c++) {
            CHECK_EQ(gl->conn[c].connecting_lane, el->conn[c].connecting_lane);
            CHECK_EQ(gl->conn[c].connecting_maneuver, el->conn[c].connecting_maneuver);
            CHECK_EQ(gl->conn[c].signal_group, el->conn[c].signal_group);
        }
    }
}

static void cmp_spat(const char *vname, const its_spat_intersection_t *got, const its_spat_intersection_t *exp, int idx)
{
    char ctx[64];
    snprintf(ctx, sizeof(ctx), "spat %d", idx);
    CMP(region); CMP(id); CMP(revision); CMP(has_moy); CMP(moy); CMP(has_timestamp); CMP(timestamp_ms);
    CMP(n_states);
    g_checks++;
    if (strcmp(got->name, exp->name) != 0) {
        g_fail++;
        printf("  FAIL %s: spat name '%s' vs '%s'\n", vname, got->name, exp->name);
    }
    for (int k = 0; k < exp->n_states && k < got->n_states; k++) {
        const its_signal_state_t *gs = &got->states[k], *es = &exp->states[k];
        snprintf(ctx, sizeof(ctx), "i%d state %d", idx, k);
        CMPX(gs, es, signal_group); CMPX(gs, es, phase); CMPX(gs, es, start_time);
        CMPX(gs, es, min_end_time); CMPX(gs, es, max_end_time); CMPX(gs, es, likely_time);
    }
}

static void test_vectors(void)
{
    printf("vectors (%u)\n", (unsigned)N_VECTORS);
    for (size_t i = 0; i < N_VECTORS; i++) {
        const test_vector_t *v = &g_vectors[i];
        uint8_t *f = dup_exact(v->frame, v->len);
        its_packet_t p;
        its_err_t e = its_parse_frame(f, v->len, &p);
        if ((int)e != (int)v->err) {
            printf("  FAIL %s: parse err %d expected %d\n", v->name, e, v->err);
            g_fail++;
            free(f);
            continue;
        }
        g_checks++;
        if (e == ITS_OK) {
            CHECK_EQ(p.secured, v->secured);
            CHECK_EQ(p.gn_header_type, v->ht);
            CHECK_EQ(p.gn_header_subtype, v->hst);
            CHECK(memcmp(p.src_mac, v->src_mac, 6) == 0);
            if (v->pdu_len == 0) {
                CHECK(p.pdu == NULL);
                CHECK_EQ(p.pdu_len, 0);
            } else {
                CHECK_EQ(p.btp_dst_port, v->port);
                CHECK_EQ(p.protocol_version, v->proto);
                CHECK_EQ(p.message_id, v->msg_id);
                CHECK_EQ(p.station_id, v->station_id);
                CHECK_EQ(p.pdu_len, v->pdu_len);
                CHECK(p.pdu == f + v->pdu_off);
            }
        }
        switch (v->kind) {
        case VK_CAM: {
            its_cam_t c;
            CHECK(its_decode_cam(&p, &c));
            const its_cam_t *got = &c, *exp = v->cam;
            const char *vname = v->name, *ctx = "cam";
            CMP(station_id); CMP(station_type); CMP(lat); CMP(lon); CMP(speed); CMP(heading);
            its_denm_t d;
            CHECK(!its_decode_denm(&p, &d));            /* wrong message id */
            break;
        }
        case VK_DENM: {
            its_denm_t d;
            CHECK(its_decode_denm(&p, &d));
            const its_denm_t *got = &d, *exp = v->denm;
            const char *vname = v->name, *ctx = "denm";
            CMP(station_id); CMP(sequence_number); CMP(cause_code); CMP(sub_cause_code); CMP(lat); CMP(lon);
            CMP(termination);
            its_cam_t c;
            CHECK(!its_decode_cam(&p, &c));
            break;
        }
        case VK_MAP: {
            its_map_intersection_t *out = calloc(6, sizeof(*out));
            int n = its_decode_mapem(&p, out, 6);
            CHECK_EQ(n, v->n_map);
            for (int k = 0; k < n && k < v->n_map; k++) {
                cmp_map(v->name, &out[k], &v->map[k], k);
            }
            if (strcmp(v->name, "mapem_many_lanes") == 0) {
                CHECK_EQ(out[0].n_lanes, ITS_MAX_LANES);
                CHECK_EQ(out[0].lanes_dropped, 70 - ITS_MAX_LANES);
            } else {
                for (int k = 0; k < n; k++) { CHECK_EQ(out[k].lanes_dropped, 0); }
            }
            /* output limit */
            int n1 = its_decode_mapem(&p, out, 1);
            CHECK_EQ(n1, 1);
            if (n1 == 1) {
                cmp_map(v->name, &out[0], &v->map[0], 0);
            }
            its_spat_intersection_t sp;
            CHECK_EQ(its_decode_spatem(&p, &sp, 1), -1);
            free(out);
            break;
        }
        case VK_SPAT: {
            its_spat_intersection_t *out = calloc(6, sizeof(*out));
            int n = its_decode_spatem(&p, out, 6);
            CHECK_EQ(n, v->n_spat);
            for (int k = 0; k < n && k < v->n_spat; k++) {
                cmp_spat(v->name, &out[k], &v->spat[k], k);
            }
            if (strcmp(v->name, "spatem_many_states") == 0) {
                CHECK_EQ(out[0].n_states, ITS_MAX_SIGNAL_STATES);
                CHECK_EQ(out[0].states_dropped, 60 - ITS_MAX_SIGNAL_STATES);
            }
            if (strcmp(v->name, "spatem_invalid_moy") == 0) {
                CHECK(!out[0].has_moy);
            }
            int n1 = its_decode_spatem(&p, out, 1);
            CHECK_EQ(n1, 1);
            its_map_intersection_t *m = calloc(1, sizeof(*m));
            CHECK_EQ(its_decode_mapem(&p, m, 1), -1);
            free(m);
            free(out);
            break;
        }
        default:
            break;
        }
        free(f);
    }
}

/* every truncation of every frame: no crash, pdu inside buffer; decoders on truncated pdu */
static void check_pdu_decoders(const its_packet_t *p)
{
    its_cam_t c;
    its_denm_t d;
    its_map_intersection_t *m = malloc(sizeof(*m) * 2);
    its_spat_intersection_t sp[2];
    switch (p->message_id) {
    case ITS_MSG_CAM:    (void)its_decode_cam(p, &c); break;
    case ITS_MSG_DENM:   (void)its_decode_denm(p, &d); break;
    case ITS_MSG_MAPEM: {
        int n = its_decode_mapem(p, m, 2);
        CHECK(n >= -1 && n <= 2);
        for (int i = 0; i < n; i++) {
            CHECK(m[i].n_lanes <= ITS_MAX_LANES);
            CHECK(m[i].name[ITS_NAME_LEN - 1] == '\0');
            for (int l = 0; l < m[i].n_lanes; l++) {
                CHECK(m[i].lanes[l].n_nodes <= ITS_MAX_LANE_NODES);
                CHECK(m[i].lanes[l].n_connections <= ITS_MAX_CONNECTIONS);
                CHECK(m[i].lanes[l].lane_type <= ITS_LANE_TYPE_OTHER);
            }
        }
        break;
    }
    case ITS_MSG_SPATEM: {
        int n = its_decode_spatem(p, sp, 2);
        CHECK(n >= -1 && n <= 2);
        for (int i = 0; i < n; i++) {
            CHECK(sp[i].n_states <= ITS_MAX_SIGNAL_STATES);
            for (int s = 0; s < sp[i].n_states; s++) {
                CHECK(sp[i].states[s].phase <= 9);
                CHECK(sp[i].states[s].min_end_time <= ITS_TIMEMARK_UNKNOWN);
            }
        }
        break;
    }
    default: break;
    }
    free(m);
}

static void test_truncation(void)
{
    printf("truncation\n");
    int ok_frames = 0;
    for (size_t i = 0; i < N_VECTORS; i++) {
        const test_vector_t *v = &g_vectors[i];
        for (size_t n = 0; n < v->len; n++) {
            uint8_t *f = dup_exact(v->frame, n);
            its_packet_t p;
            its_err_t e = its_parse_frame(f, n, &p);
            if (e == ITS_OK) {
                ok_frames++;
                if (p.pdu != NULL) {
                    CHECK(p.pdu >= f && p.pdu + p.pdu_len <= f + n);
                    check_pdu_decoders(&p);
                }
            }
            free(f);
        }
        /* truncated PDU, exact-size buffer, valid headers */
        if (v->err == ITS_OK && v->pdu_len > 6) {
            for (size_t n = 0; n < v->pdu_len; n++) {
                uint8_t *pdu = dup_exact(v->frame + v->pdu_off, n);
                its_packet_t p;
                memset(&p, 0, sizeof(p));
                p.pdu = pdu;
                p.pdu_len = n;
                p.message_id = v->msg_id;
                p.station_id = v->station_id;
                check_pdu_decoders(&p);
                free(pdu);
            }
        }
    }
    printf("  (%d truncated frames still parsed)\n", ok_frames);
}

/* ====================================================================== */
static const test_vector_t *find_vec(const char *name)
{
    for (size_t i = 0; i < N_VECTORS; i++) {
        if (strcmp(g_vectors[i].name, name) == 0) {
            return &g_vectors[i];
        }
    }
    printf("  missing vector %s\n", name);
    g_fail++;
    return &g_vectors[0];
}

static void ingest_vec(const char *name, int8_t rssi, int64_t t)
{
    const test_vector_t *v = find_vec(name);
    its_store_ingest(v->frame, v->len, rssi, t);
}

static const its_store_intersection_t *find_is(uint16_t region, uint16_t id)
{
    const its_store_t *s = its_store_get();
    for (int i = 0; i < ITS_STORE_MAX_INTERSECTIONS; i++) {
        if (s->intersections[i].used && s->intersections[i].id == id && s->intersections[i].region == region) {
            return &s->intersections[i];
        }
    }
    return NULL;
}

static int count_stations(void)
{
    int n = 0;
    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        n += its_store_get()->stations[i].packets != 0;
    }
    return n;
}
static int count_denms(void)
{
    int n = 0;
    for (int i = 0; i < ITS_STORE_MAX_DENMS; i++) {
        n += its_store_get()->denms[i].used;
    }
    return n;
}
static int count_is(void)
{
    int n = 0;
    for (int i = 0; i < ITS_STORE_MAX_INTERSECTIONS; i++) {
        n += its_store_get()->intersections[i].used;
    }
    return n;
}

static void test_store(void)
{
    printf("store\n");
    its_store_init();
    const its_store_t *s = its_store_get();
    const int64_t T0 = 1000000;
    g_lock_depth = g_lock_max = g_lock_calls = 0;

    ingest_vec("cam_v1_car", -60, T0);
    ingest_vec("cam_v2_bus", -61, T0 + 1000);
    ingest_vec("cam_v1_secured", -62, T0 + 2000);
    ingest_vec("denm_v1_accident", -63, T0 + 3000);
    ingest_vec("mapem_two_intersections", -64, T0 + 4000);
    ingest_vec("spatem_two_intersections", -65, T0 + 5000);
    ingest_vec("gn_beacon", -66, T0 + 6000);
    ingest_vec("neg_ethertype_ipv6", -67, T0 + 7000);
    ingest_vec("neg_pl_too_large", -68, T0 + 8000);

    CHECK_EQ(g_lock_depth, 0);
    CHECK_EQ(g_lock_max, 1);
    CHECK(g_lock_calls >= 9);
    CHECK_EQ(s->total_frames, 9);
    CHECK_EQ(s->its_frames, 7);
    CHECK_EQ(s->secured_frames, 1);
    CHECK_EQ(s->decode_errors, 1);              /* PL too large; IPv6 is not counted */
    CHECK_EQ(s->per_msg[ITS_MSG_CAM], 3);
    CHECK_EQ(s->per_msg[ITS_MSG_DENM], 1);
    CHECK_EQ(s->per_msg[ITS_MSG_MAPEM], 1);
    CHECK_EQ(s->per_msg[ITS_MSG_SPATEM], 1);
    CHECK_EQ(s->per_msg[0], 1);                 /* beacon */
    CHECK_EQ(s->last_rssi, -68);
    CHECK_EQ(s->last_rx_us, T0 + 8000);
    CHECK_EQ(count_stations(), 6);

    /* recent ring: newest is at head-1 */
    const its_recent_t *r = &s->recent[(s->recent_head + ITS_STORE_RECENT - 1) % ITS_STORE_RECENT];
    CHECK_EQ(r->message_id, 0);                 /* beacon was the last parsed frame */
    CHECK_EQ(s->recent_head, 7);

    /* stations */
    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        const its_station_t *st = &s->stations[i];
        if (st->packets == 0) { continue; }
        if (st->station_id == 0x11111111) {
            CHECK(st->has_cam);
            CHECK_EQ(st->station_type, 5);
            CHECK_EQ(st->cam.speed, 1389);
            CHECK_EQ(st->rssi, -60);
            CHECK(st->msg_mask == (1u << ITS_MSG_CAM));
            CHECK(memcmp(st->mac, g_vectors[0].src_mac, 6) == 0);
        }
        if (st->station_id == 0x55555551 || st->station_id == 0x66666661) {
            CHECK_EQ(st->station_type, 15);
            CHECK(!st->has_cam);
        }
        if (st->station_id == 0x22222221) {
            CHECK_EQ(st->station_type, 6);
        }
    }

    /* intersections merged by (region, id) */
    CHECK_EQ(count_is(), 2);                    /* (7,12) (7,13) from MAP + SPAT */
    const its_store_intersection_t *a = find_is(7, 12);
    const its_store_intersection_t *b = find_is(7, 13);
    CHECK(a && b);
    if (a) {
        CHECK(a->has_map && a->has_spat);
        CHECK(strcmp(a->name, "Kreuzung Mitte") == 0);
        CHECK_EQ(a->station_id, 0x55555551);    /* MAP sender */
        CHECK_EQ(a->map_rx_us, T0 + 4000);
        CHECK_EQ(a->spat_rx_us, T0 + 5000);
        CHECK_EQ(a->map.n_lanes, 11);
        CHECK_EQ(a->spat.n_states, 9);
        CHECK(a->has_clock);
        CHECK_EQ(a->spat_hour_tenths, 35999);   /* moy 527039 % 60 = 59, 59999 ms */
        CHECK_EQ(a->spat_clock_us, T0 + 5000);
        /* timemarks: now == receive time */
        int64_t now = T0 + 5000;
        CHECK_EQ(its_timemark_remaining_tenths(a, 35999, now), 0);
        CHECK_EQ(its_timemark_remaining_tenths(a, 20, now), 21);          /* wraps over the hour */
        CHECK_EQ(its_timemark_remaining_tenths(a, 36000, now), -1);
        CHECK_EQ(its_timemark_remaining_tenths(a, ITS_TIMEMARK_UNKNOWN, now), -1);
        CHECK_EQ(its_timemark_remaining_tenths(a, 20, now + 1000000), 11);  /* 1 s later */
        CHECK_EQ(its_timemark_remaining_tenths(a, 20, now + 2100000), 0);   /* 2.1s: tolerated late */
        CHECK_EQ(its_timemark_remaining_tenths(a, 20, now + 30000000), -1); /* long gone */
        CHECK_EQ(its_timemark_remaining_tenths(a, 36000 - 100, now), -1);   /* 10 s in the past */
        CHECK_EQ(its_timemark_remaining_tenths(NULL, 5, now), -1);
    }
    if (b) {
        CHECK(b->has_map && b->has_spat);
        CHECK_EQ(b->spat.n_states, 1);
        CHECK(b->has_clock);
        CHECK_EQ(b->spat_hour_tenths, (100 % 60) * 600);
    }
    const its_store_intersection_t *c = find_is(8, 99);
    CHECK(c == NULL);

    /* invalid moy must not create a clock */
    ingest_vec("spatem_invalid_moy", -50, T0 + 8500);
    {
        const its_store_intersection_t *bm = find_is(13, 1);
        CHECK(bm && bm->has_spat && !bm->has_clock);
        CHECK_EQ(its_timemark_remaining_tenths(bm, 50, T0 + 8500), -1);
    }
    /* MAP without name must not erase an existing name */
    CHECK(strcmp(find_is(7, 12)->name, "Kreuzung Mitte") == 0);

    /* DENM table and termination */
    CHECK_EQ(count_denms(), 1);
    ingest_vec("denm_v1_accident", -50, T0 + 9000);
    CHECK_EQ(count_denms(), 1);                 /* same actionID: updated in place */
    ingest_vec("denm_v2_roadworks", -50, T0 + 9100);
    CHECK_EQ(count_denms(), 2);
    ingest_vec("denm_v1_termination", -50, T0 + 9200);   /* unknown actionID: ignored */
    CHECK_EQ(count_denms(), 2);
    ingest_vec("denm_v1_accident_cancel", -50, T0 + 9300);
    CHECK_EQ(count_denms(), 2);
    int cancelled = 0;
    for (int i = 0; i < ITS_STORE_MAX_DENMS; i++) {
        if (s->denms[i].used && s->denms[i].denm.termination) {
            cancelled++;
            CHECK_EQ(s->denms[i].denm.station_id, 0x33333331);
            CHECK_EQ(s->denms[i].denm.cause_code, 2);   /* cause kept from the original */
        }
    }
    CHECK_EQ(cancelled, 1);
    its_store_tick(T0 + 10000);
    CHECK_EQ(count_denms(), 1);

    /* expiry: stations 60 s, intersections 120 s, DENM 10 min */
    its_store_tick(T0 + 61LL * 1000000LL + 9300);
    CHECK_EQ(count_stations(), 0);
    CHECK_EQ(count_is(), 3);
    CHECK(!find_is(7, 12)->has_spat && find_is(7, 12)->has_map);   /* SPAT stale after 30 s */
    its_store_tick(T0 + 5000 + 121LL * 1000000LL);
    CHECK_EQ(count_is(), 0);
    CHECK_EQ(count_denms(), 1);
    its_store_tick(T0 + 9100 + 601LL * 1000000LL);
    CHECK_EQ(count_denms(), 0);
    CHECK_EQ(g_lock_depth, 0);

    /* clear */
    ingest_vec("cam_v1_car", -60, T0);
    its_store_clear();
    CHECK_EQ(s->total_frames, 0);
    CHECK_EQ(count_stations(), 0);
    CHECK_EQ(g_lock_depth, 0);
}

static void test_store_lru_and_rate(void)
{
    printf("store lru / rate\n");
    its_store_init();
    const its_store_t *s = its_store_get();
    const test_vector_t *v = find_vec("cam_v1_car");
    uint8_t *f = dup_exact(v->frame, v->len);
    for (uint32_t i = 0; i < 70; i++) {
        uint32_t id = 1000 + i;
        f[v->pdu_off + 2] = (uint8_t)(id >> 24);
        f[v->pdu_off + 3] = (uint8_t)(id >> 16);
        f[v->pdu_off + 4] = (uint8_t)(id >> 8);
        f[v->pdu_off + 5] = (uint8_t)id;
        its_store_ingest(f, v->len, -50, 1000000 + (int64_t)i * 1000);
    }
    CHECK_EQ(count_stations(), ITS_STORE_MAX_STATIONS);
    int present[70] = { 0 };
    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        uint32_t id = s->stations[i].station_id;
        if (s->stations[i].packets && id >= 1000 && id < 1070) {
            present[id - 1000] = 1;
        }
    }
    for (int i = 0; i < 6; i++) { CHECK_EQ(present[i], 0); }       /* oldest evicted */
    for (int i = 6; i < 70; i++) { CHECK_EQ(present[i], 1); }
    /* touching an old one keeps it */
    f[v->pdu_off + 5] = (uint8_t)(1006 & 0xff);
    f[v->pdu_off + 4] = (uint8_t)(1006 >> 8);
    its_store_ingest(f, v->len, -50, 5000000);
    for (int i = 0; i < 6; i++) {
        uint32_t id = 2000 + (uint32_t)i;
        f[v->pdu_off + 4] = (uint8_t)(id >> 8);
        f[v->pdu_off + 5] = (uint8_t)id;
        its_store_ingest(f, v->len, -50, 5001000 + i);
    }
    int found1006 = 0;
    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        found1006 |= s->stations[i].packets && s->stations[i].station_id == 1006;
    }
    CHECK_EQ(found1006, 1);
    free(f);

    /* smoothed rate converges to the real rate */
    its_store_init();
    int64_t t = 0;
    its_store_tick(t);
    for (int sec = 0; sec < 40; sec++) {
        for (int k = 0; k < 50; k++) {
            its_store_ingest(v->frame, v->len, -50, t + k);
        }
        t += 1000000;
        its_store_tick(t);
        if (sec == 0) {
            CHECK(s->rate_per_s > 14.9f && s->rate_per_s < 15.1f);
        }
    }
    CHECK(s->rate_per_s > 49.0f && s->rate_per_s <= 50.01f);
    for (int sec = 0; sec < 40; sec++) {
        t += 1000000;
        its_store_tick(t);
    }
    CHECK(s->rate_per_s < 1.0f);
}

static void test_store_intersection_replacement(void)
{
    printf("store intersection replacement\n");
    its_store_init();
    const test_vector_t *v = find_vec("mapem_four_intersections");
    for (int rep = 0; rep < 5; rep++) {   /* 4 intersections per frame; table holds 12 */
        uint8_t *f = dup_exact(v->frame, v->len);
        /* different station id has no influence on keys; change region via a fresh vector instead */
        its_store_ingest(f, v->len, -50, 1000 + rep);
        free(f);
    }
    CHECK_EQ(count_is(), 4);              /* same keys: merged */
    ingest_vec("mapem_two_intersections", -50, 5000);
    ingest_vec("mapem_secured_noregion", -50, 6000);
    ingest_vec("mapem_with_trailing_containers", -50, 7000);
    ingest_vec("mapem_future_extensions", -50, 8000);
    ingest_vec("mapem_many_lanes", -50, 9000);
    CHECK_EQ(count_is(), 4 + 2 + 1 + 0 + 2 + 1);   /* trailing_containers has the key of secured_noregion */
    ingest_vec("spatem_many_states", -50, 10000);     /* new SPAT-only intersection (12,1) */
    ingest_vec("spatem_future_extensions", -50, 11000);
    /* 12 slots are full now (4+2+1+0+2+1=10, +(12,1) = 11, then (11,1),(11,2) replace the oldest) */
    CHECK_EQ(count_is(), ITS_STORE_MAX_INTERSECTIONS);
    CHECK(find_is(11, 1) != NULL && find_is(11, 2) != NULL);
    CHECK(find_is(12, 1) != NULL);
    CHECK(find_is(10, 1) == NULL);                    /* oldest ones evicted */
    const its_store_intersection_t *m = find_is(9, 1000);
    CHECK(m && m->map.n_lanes == ITS_MAX_LANES && m->map.lanes_dropped == 6);
    its_store_clear();
}

static void test_names(void)
{
    printf("names\n");
    CHECK(strcmp(its_msg_name(ITS_MSG_CAM), "CAM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_SPATEM), "SPATEM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_MAPEM), "MAPEM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_DENM), "DENM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_PCIM), "MIM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_PCVM), "MVM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_PAM), "PIM") == 0);
    CHECK(strcmp(its_msg_name(ITS_MSG_EV_RSR), "rfu") == 0);
    CHECK(strcmp(its_msg_name(200), "Unbekannt") == 0);
    CHECK(strcmp(its_station_type_name(0), "Unbekannt") == 0);
    CHECK(strcmp(its_station_type_name(1), "Fußgänger") == 0);
    CHECK(strcmp(its_station_type_name(5), "Pkw") == 0);
    CHECK(strcmp(its_station_type_name(7), "Leicht-Lkw") == 0);
    CHECK(strcmp(its_station_type_name(11), "Straßenbahn") == 0);
    CHECK(strcmp(its_station_type_name(15), "RSU") == 0);
    CHECK(strcmp(its_station_type_name(99), "Unbekannt") == 0);
    CHECK(strcmp(its_cause_name(1), "Verkehrslage/Stau") == 0);
    CHECK(strcmp(its_cause_name(3), "Baustelle") == 0);
    CHECK(strcmp(its_cause_name(27), "Gefährliches Ende Stau") == 0);
    CHECK(strcmp(its_cause_name(99), "Gefahrensituation") == 0);
    CHECK(strcmp(its_cause_name(0), "Unbekannt") == 0);
    CHECK(strcmp(its_cause_name(250), "Sonstiges") == 0);
    CHECK(strcmp(its_phase_name(0), "Unbekannt") == 0);
    CHECK(strcmp(its_phase_name(3), "Rot") == 0);
    CHECK(strcmp(its_phase_name(4), "Rot-Gelb") == 0);
    CHECK(strcmp(its_phase_name(5), "Grün (bedingt)") == 0);
    CHECK(strcmp(its_phase_name(6), "Grün") == 0);
    CHECK(strcmp(its_phase_name(8), "Gelb") == 0);
    CHECK(strcmp(its_phase_name(9), "Gelb blinkend") == 0);
    CHECK(strcmp(its_phase_name(77), "Unbekannt") == 0);
    CHECK_EQ(its_phase_light(0), ITS_LIGHT_OFF);
    CHECK_EQ(its_phase_light(1), ITS_LIGHT_OFF);
    CHECK_EQ(its_phase_light(2), ITS_LIGHT_RED);
    CHECK_EQ(its_phase_light(3), ITS_LIGHT_RED);
    CHECK_EQ(its_phase_light(4), ITS_LIGHT_RED_YELLOW);
    CHECK_EQ(its_phase_light(5), ITS_LIGHT_GREEN);
    CHECK_EQ(its_phase_light(6), ITS_LIGHT_GREEN);
    CHECK_EQ(its_phase_light(7), ITS_LIGHT_YELLOW);
    CHECK_EQ(its_phase_light(8), ITS_LIGHT_YELLOW);
    CHECK_EQ(its_phase_light(9), ITS_LIGHT_FLASHING_YELLOW);
    CHECK_EQ(its_phase_light(10), ITS_LIGHT_OFF);
}

static void test_timemark_direct(void)
{
    printf("timemark\n");
    its_store_intersection_t is;
    memset(&is, 0, sizeof(is));
    CHECK_EQ(its_timemark_remaining_tenths(&is, 100, 0), -1);       /* no clock */
    is.has_clock = true;
    is.spat_hour_tenths = 100;
    is.spat_clock_us = 5000000;
    CHECK_EQ(its_timemark_remaining_tenths(&is, 150, 5000000), 50);
    CHECK_EQ(its_timemark_remaining_tenths(&is, 150, 5000000 + 300000), 47);
    CHECK_EQ(its_timemark_remaining_tenths(&is, 150, 4000000), 50);  /* time going backwards: clamp */
    is.spat_hour_tenths = 35990;                                      /* 0.9 s before the full hour */
    CHECK_EQ(its_timemark_remaining_tenths(&is, 35999, 5000000), 9);
    CHECK_EQ(its_timemark_remaining_tenths(&is, 10, 5000000), 20);
    CHECK_EQ(its_timemark_remaining_tenths(&is, 10, 5000000 + 1500000), 5);   /* now past the wrap */
    CHECK_EQ(its_timemark_remaining_tenths(&is, 35995, 5000000 + 1500000), 0);/* 0.5 s late */
}

/* ====================================================================== */
static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

static size_t mutate(uint8_t *buf, size_t len, size_t cap)
{
    int n = 1 + (int)(rnd() % 4);
    for (int k = 0; k < n && len > 0; k++) {
        switch (rnd() % 7) {
        case 0: buf[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: buf[rnd() % len] = (uint8_t)rnd(); break;
        case 2: buf[rnd() % len] = (rnd() & 1) ? 0x00 : 0xff; break;
        case 3: len = rnd() % (len + 1); break;                       /* truncate */
        case 4: {                                                      /* delete a chunk */
            size_t a = rnd() % len, c = 1 + rnd() % 8;
            if (a + c > len) { c = len - a; }
            memmove(buf + a, buf + a + c, len - a - c);
            len -= c;
            break;
        }
        case 5: {                                                      /* insert random bytes */
            size_t a = rnd() % (len + 1), c = 1 + rnd() % 4;
            if (len + c <= cap) {
                memmove(buf + a + c, buf + a, len - a);
                for (size_t i = 0; i < c; i++) { buf[a + i] = (uint8_t)rnd(); }
                len += c;
            }
            break;
        }
        default: {                                                     /* arithmetic */
            size_t a = rnd() % len;
            buf[a] = (uint8_t)(buf[a] + (int)(rnd() % 7) - 3);
            break;
        }
        }
    }
    return len;
}

static void test_fuzz(void)
{
    const char *e = getenv("ITS_FUZZ_ITERS");
    long iters = e ? atol(e) : 200000;
    printf("fuzz (%ld frame mutations + %ld pdu mutations)\n", iters, iters);
    its_store_init();
    uint8_t *work = malloc(8192);
    long parsed_ok = 0, decoded_ok = 0;

    /* 1) mutate whole frames; exact-size heap copy per run */
    for (long it = 0; it < iters; it++) {
        const test_vector_t *v = &g_vectors[rnd() % N_VECTORS];
        size_t len = v->len;
        memcpy(work, v->frame, len);
        len = mutate(work, len, 4096);
        uint8_t *f = dup_exact(work, len);
        its_packet_t p;
        its_err_t er = its_parse_frame(f, len, &p);
        if (er == ITS_OK) {
            parsed_ok++;
            if (p.pdu != NULL) {
                CHECK(p.pdu >= f && p.pdu + p.pdu_len <= f + len);
                check_pdu_decoders(&p);
            }
        }
        if ((it & 7) == 0) {
            its_store_ingest(f, len, (int8_t)rnd(), (int64_t)it * 1000);
        }
        if ((it & 0xffff) == 0xffff) {
            its_store_tick((int64_t)it * 1000);
        }
        free(f);
        if (g_fail) { break; }
    }

    /* 2) mutate only the UPER PDU (deeper decoder coverage) */
    for (long it = 0; it < iters; it++) {
        const test_vector_t *v = &g_vectors[rnd() % N_VECTORS];
        if (v->err != ITS_OK || v->pdu_len < 8) { continue; }
        size_t len = v->pdu_len;
        memcpy(work, v->frame + v->pdu_off, len);
        if (rnd() % 8) {
            len = 6 + mutate(work + 6, len - 6, 4096 - 6);
        }
        uint8_t *pdu = dup_exact(work, len);
        its_packet_t p;
        memset(&p, 0, sizeof(p));
        p.pdu = pdu;
        p.pdu_len = len;
        p.message_id = v->msg_id;
        p.station_id = v->station_id;
        check_pdu_decoders(&p);
        {
            its_map_intersection_t *m = malloc(sizeof(*m));
            its_spat_intersection_t sp;
            its_cam_t c;
            its_denm_t d;
            /* ignore message id: run every decoder on every payload */
            p.message_id = ITS_MSG_MAPEM;
            decoded_ok += its_decode_mapem(&p, m, 1) >= 0;
            p.message_id = ITS_MSG_SPATEM;
            decoded_ok += its_decode_spatem(&p, &sp, 1) >= 0;
            p.message_id = ITS_MSG_CAM;
            decoded_ok += its_decode_cam(&p, &c);
            p.message_id = ITS_MSG_DENM;
            decoded_ok += its_decode_denm(&p, &d);
            free(m);
        }
        free(pdu);
        if (g_fail) { break; }
    }
    free(work);
    printf("  (%ld mutated frames parsed OK, %ld decoder successes)\n", parsed_ok, decoded_ok);
    its_store_clear();
}

static void test_misc(void)
{
    printf("misc\n");
    its_packet_t p;
    CHECK_EQ(its_parse_frame(NULL, 0, &p), ITS_ERR_TOO_SHORT);
    CHECK_EQ(its_parse_frame((const uint8_t *)"", 0, &p), ITS_ERR_TOO_SHORT);
    uint8_t zero[64] = { 0 };
    CHECK_EQ(its_parse_frame(zero, sizeof(zero), &p), ITS_ERR_NOT_GN);   /* management frame type 0 */
    its_cam_t c;
    its_denm_t d;
    CHECK(!its_decode_cam(NULL, &c));
    CHECK(!its_decode_denm(NULL, &d));
    CHECK(!its_decode_denm(&p, NULL));
    its_map_intersection_t *m = malloc(sizeof(*m));
    its_spat_intersection_t sp;
    CHECK_EQ(its_decode_mapem(NULL, m, 1), -1);
    CHECK_EQ(its_decode_spatem(&p, NULL, 1), -1);
    CHECK_EQ(its_decode_spatem(&p, &sp, 0), -1);
    free(m);
    /* struct sizes: the store must live in static memory, MAP intersections are big */
    printf("  sizeof(its_map_intersection_t)=%zu sizeof(its_store_t)=%zu\n",
           sizeof(its_map_intersection_t), sizeof(its_store_t));
}

int main(void)
{
    test_uper();
    test_names();
    test_timemark_direct();
    test_vectors();
    test_truncation();
    test_store();
    test_store_lru_and_rate();
    test_store_intersection_replacement();
    test_misc();
    test_fuzz();
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    printf(g_fail ? "FAILED\n" : "ALL TESTS PASSED\n");
    return g_fail ? 1 : 0;
}
