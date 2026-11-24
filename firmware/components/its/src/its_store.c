/*
 * its_store.c - state store: counters, stations, intersections (MAP + SPAT
 * merged), DENMs, recent-frame ring.
 *
 * Conventions (see its.h for the types):
 *  - a station slot is in use iff packets > 0
 *  - recent[]: ring buffer, recent_head is the index of the NEXT slot to
 *    write; the newest entry is recent[(recent_head + ITS_STORE_RECENT - 1) %
 *    ITS_STORE_RECENT]; empty slots have rx_us == 0 && message_id == 0 && len == 0
 *  - intersection / DENM slots are in use iff 'used'
 *
 * Decoded MAP intersections are ~5 KB, so decoding goes through static
 * scratch buffers (guarded by the store lock) instead of the stack.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>

#include "its.h"

#define SCRATCH_MAP   4
#define SCRATCH_SPAT  4

#define STATION_TIMEOUT_US       (60LL * 1000000LL)
#define INTERSECTION_TIMEOUT_US  (120LL * 1000000LL)
#define SPAT_TIMEOUT_US          (30LL * 1000000LL)
#define DENM_TIMEOUT_US          (600LL * 1000000LL)

static its_store_t s_store;
static its_map_intersection_t  s_map_scratch[SCRATCH_MAP];
static its_spat_intersection_t s_spat_scratch[SCRATCH_SPAT];

static int64_t  s_tick_us;
static uint32_t s_tick_total;
static bool     s_tick_valid;

__attribute__((weak)) void its_store_lock(void) {}
__attribute__((weak)) void its_store_unlock(void) {}

/* Call once at start-up, before other tasks use the store (does not lock, so it
 * may run before the firmware's mutex exists). */
void its_store_init(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_tick_valid = false;
    s_tick_us = 0;
    s_tick_total = 0;
}

void its_store_clear(void)
{
    its_store_lock();
    its_store_init();
    its_store_unlock();
}

const its_store_t *its_store_get(void)
{
    return &s_store;
}

/* ------------------------------------------------------------------ */

static its_station_t *station_slot(uint32_t station_id)
{
    its_station_t *free_slot = NULL;
    its_station_t *oldest = &s_store.stations[0];
    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        its_station_t *s = &s_store.stations[i];
        if (s->packets == 0) {
            if (free_slot == NULL) {
                free_slot = s;
            }
            continue;
        }
        if (s->station_id == station_id) {
            return s;
        }
        if (s->last_seen_us < oldest->last_seen_us || oldest->packets == 0) {
            oldest = s;
        }
    }
    its_station_t *s = free_slot != NULL ? free_slot : oldest;
    memset(s, 0, sizeof(*s));
    s->station_id = station_id;
    return s;
}

static its_station_t *touch_station(const its_packet_t *pkt, int8_t rssi, int64_t now_us)
{
    its_station_t *s = station_slot(pkt->station_id);
    memcpy(s->mac, pkt->src_mac, 6);
    s->last_seen_us = now_us;
    s->rssi = rssi;
    s->msg_mask |= 1u << (pkt->message_id < ITS_MSG_COUNT ? pkt->message_id : 0);
    if (s->packets != UINT32_MAX) {
        s->packets++;
    }
    return s;
}

static int64_t intersection_last_rx(const its_store_intersection_t *is)
{
    int64_t t = 0;
    if (is->has_map && is->map_rx_us > t) { t = is->map_rx_us; }
    if (is->has_spat && is->spat_rx_us > t) { t = is->spat_rx_us; }
    return t;
}

static its_store_intersection_t *intersection_slot(uint16_t region, uint16_t id)
{
    its_store_intersection_t *free_slot = NULL;
    its_store_intersection_t *oldest = NULL;
    its_store_intersection_t *loose = NULL;
    for (int i = 0; i < ITS_STORE_MAX_INTERSECTIONS; i++) {
        its_store_intersection_t *is = &s_store.intersections[i];
        if (!is->used) {
            if (free_slot == NULL) {
                free_slot = is;
            }
            continue;
        }
        if (is->id == id && is->region == region) {
            return is;
        }
        /* region may be absent (0) on one side only */
        if (is->id == id && (is->region == 0 || region == 0) && loose == NULL) {
            loose = is;
        }
        if (oldest == NULL || intersection_last_rx(is) < intersection_last_rx(oldest)) {
            oldest = is;
        }
    }
    if (loose != NULL) {
        if (loose->region == 0) {
            loose->region = region;
        }
        return loose;
    }
    its_store_intersection_t *is = free_slot != NULL ? free_slot : oldest;
    memset(is, 0, sizeof(*is));
    is->used = true;
    is->region = region;
    is->id = id;
    return is;
}

static void ingest_map(const its_packet_t *pkt, int8_t rssi, int64_t now_us)
{
    int n = its_decode_mapem(pkt, s_map_scratch, SCRATCH_MAP);
    if (n < 0) {
        s_store.decode_errors++;
        return;
    }
    for (int i = 0; i < n; i++) {
        const its_map_intersection_t *m = &s_map_scratch[i];
        its_store_intersection_t *is = intersection_slot(m->region, m->id);
        is->has_map = true;
        is->map_rx_us = now_us;
        memcpy(&is->map, m, sizeof(*m));
        if (m->name[0] != '\0') {
            memcpy(is->name, m->name, sizeof(is->name));
        }
        is->station_id = pkt->station_id;
        is->rssi = rssi;
    }
}

static void ingest_spat(const its_packet_t *pkt, int8_t rssi, int64_t now_us)
{
    int n = its_decode_spatem(pkt, s_spat_scratch, SCRATCH_SPAT);
    if (n < 0) {
        s_store.decode_errors++;
        return;
    }
    for (int i = 0; i < n; i++) {
        const its_spat_intersection_t *sp = &s_spat_scratch[i];
        its_store_intersection_t *is = intersection_slot(sp->region, sp->id);
        is->has_spat = true;
        is->spat_rx_us = now_us;
        memcpy(&is->spat, sp, sizeof(*sp));
        if (!is->has_map && sp->name[0] != '\0') {
            memcpy(is->name, sp->name, sizeof(is->name));
        }
        if (!is->has_map) {
            is->station_id = pkt->station_id;
        }
        is->rssi = rssi;
        if (sp->has_moy && sp->moy < 527040u && sp->has_timestamp && sp->timestamp_ms < 61000u) {
            is->has_clock = true;
            is->spat_hour_tenths = (sp->moy % 60u) * 600u + sp->timestamp_ms / 100u;
            is->spat_clock_us = now_us;
        }
    }
}

static void ingest_denm(const its_packet_t *pkt, int8_t rssi, int64_t now_us)
{
    its_denm_t d;
    if (!its_decode_denm(pkt, &d)) {
        s_store.decode_errors++;
        return;
    }
    its_store_denm_t *slot = NULL, *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < ITS_STORE_MAX_DENMS; i++) {
        its_store_denm_t *e = &s_store.denms[i];
        if (!e->used) {
            if (free_slot == NULL) { free_slot = e; }
            continue;
        }
        if (e->denm.station_id == d.station_id && e->denm.sequence_number == d.sequence_number) {
            slot = e;
            break;
        }
        if (oldest == NULL || e->rx_us < oldest->rx_us) {
            oldest = e;
        }
    }
    if (slot == NULL) {
        if (d.termination) {
            return;                 /* cancellation of an unknown event: nothing to show */
        }
        slot = free_slot != NULL ? free_slot : oldest;
    }
    slot->used = true;
    slot->rx_us = now_us;
    slot->rssi = rssi;
    if (d.termination && slot->denm.cause_code != 0 && d.cause_code == 0) {
        /* keep the cause of the event that is being cancelled */
        d.cause_code = slot->denm.cause_code;
        d.sub_cause_code = slot->denm.sub_cause_code;
    }
    slot->denm = d;
}

void its_store_ingest(const uint8_t *frame, size_t len, int8_t rssi, int64_t now_us)
{
    its_store_lock();
    s_store.total_frames++;
    s_store.last_rx_us = now_us;
    s_store.last_rssi = rssi;

    its_packet_t pkt;
    its_err_t err = its_parse_frame(frame, len, &pkt);
    if (err != ITS_OK) {
        if (err != ITS_ERR_NOT_GN) {
            if (err == ITS_ERR_SECURITY) {
                s_store.secured_frames++;
            }
            s_store.decode_errors++;
        }
        its_store_unlock();
        return;
    }
    s_store.its_frames++;
    if (pkt.secured) {
        s_store.secured_frames++;
    }
    uint8_t mid = pkt.message_id < ITS_MSG_COUNT ? pkt.message_id : 0;
    s_store.per_msg[mid]++;

    its_recent_t *r = &s_store.recent[s_store.recent_head];
    r->rx_us = now_us;
    r->message_id = pkt.message_id;
    r->station_id = pkt.station_id;
    r->rssi = rssi;
    r->len = (uint16_t)(len > UINT16_MAX ? UINT16_MAX : len);
    s_store.recent_head = (uint8_t)((s_store.recent_head + 1) % ITS_STORE_RECENT);

    if (pkt.pdu == NULL) {          /* beacon / location service: no ITS message */
        its_store_unlock();
        return;
    }

    its_station_t *st = touch_station(&pkt, rssi, now_us);
    switch (pkt.message_id) {
    case ITS_MSG_CAM: {
        its_cam_t cam;
        if (its_decode_cam(&pkt, &cam)) {
            st->has_cam = true;
            st->cam = cam;
            st->station_type = cam.station_type;
        } else {
            s_store.decode_errors++;
        }
        break;
    }
    case ITS_MSG_DENM:
        ingest_denm(&pkt, rssi, now_us);
        break;
    case ITS_MSG_MAPEM:
        if (!st->has_cam) { st->station_type = 15; }
        ingest_map(&pkt, rssi, now_us);
        break;
    case ITS_MSG_SPATEM:
        if (!st->has_cam) { st->station_type = 15; }
        ingest_spat(&pkt, rssi, now_us);
        break;
    default:
        break;
    }
    its_store_unlock();
}

void its_store_tick(int64_t now_us)
{
    its_store_lock();
    if (s_tick_valid && now_us > s_tick_us) {
        float dt = (float)(now_us - s_tick_us) / 1e6f;
        float inst = (float)(s_store.total_frames - s_tick_total) / dt;
        s_store.rate_per_s = 0.7f * s_store.rate_per_s + 0.3f * inst;
    }
    s_tick_valid = true;
    s_tick_us = now_us;
    s_tick_total = s_store.total_frames;

    for (int i = 0; i < ITS_STORE_MAX_STATIONS; i++) {
        its_station_t *s = &s_store.stations[i];
        if (s->packets != 0 && now_us - s->last_seen_us > STATION_TIMEOUT_US) {
            memset(s, 0, sizeof(*s));
        }
    }
    for (int i = 0; i < ITS_STORE_MAX_INTERSECTIONS; i++) {
        its_store_intersection_t *is = &s_store.intersections[i];
        if (is->used && is->has_spat && now_us - is->spat_rx_us > SPAT_TIMEOUT_US) {
            /* stale signal states must not be shown; MAP geometry stays */
            is->has_spat = false;
            is->has_clock = false;
            memset(&is->spat, 0, sizeof(is->spat));
        }
        if (is->used && now_us - intersection_last_rx(is) > INTERSECTION_TIMEOUT_US) {
            memset(is, 0, sizeof(*is));
        }
    }
    for (int i = 0; i < ITS_STORE_MAX_DENMS; i++) {
        its_store_denm_t *d = &s_store.denms[i];
        if (d->used && (d->denm.termination || now_us - d->rx_us > DENM_TIMEOUT_US)) {
            memset(d, 0, sizeof(*d));
        }
    }
    its_store_unlock();
}

int32_t its_timemark_remaining_tenths(const its_store_intersection_t *is,
                                      uint16_t timemark, int64_t now_us)
{
    if (is == NULL || !is->has_clock || timemark >= 36000u) {
        return -1;                  /* 36000 / 36001: undefined / unknown */
    }
    int64_t elapsed = now_us - is->spat_clock_us;
    if (elapsed < 0) {
        elapsed = 0;
    }
    int64_t now_t = ((int64_t)is->spat_hour_tenths + elapsed / 100000) % 36000;
    int64_t d = (int64_t)timemark - now_t;
    /* normalise to [-18000, 18000) to cope with the wrap at the full hour */
    if (d >= 18000) {
        d -= 36000;
    } else if (d < -18000) {
        d += 36000;
    }
    if (d < 0) {
        return d >= -50 ? 0 : -1;   /* up to 5 s late: show 0, older: unknown */
    }
    return (int32_t)d;
}
