/*
 * its_denm.c - UPER decoder for DENM (ETSI EN 302 637-3 v1.3 and TS 103 831 v2).
 *
 *   DENM payload: 3 presence bits (situation, location, alacarte), not extensible
 *   ManagementContainer: ext, 5 optional/default bits (termination, relevanceDistance,
 *       relevanceTrafficDirection, validityDuration, transmissionInterval),
 *       actionID (32 + 16), detectionTime 42, referenceTime 42, [termination 1],
 *       eventPosition (31 + 32 + 36 + 24)
 *   SituationContainer: ext, 2 bits (linkedCause, eventHistory | eventZone),
 *       informationQuality 3, eventType: CauseCode (ext, 8, 8) in v1,
 *       CauseCodeV2 (ext, choice index 8, subcause 8) in v2 - identical width.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>

#include "its.h"
#include "uper.h"

bool its_decode_denm(const its_packet_t *pkt, its_denm_t *out)
{
    if (pkt == NULL || out == NULL || pkt->pdu == NULL || pkt->message_id != ITS_MSG_DENM ||
        pkt->pdu_len < 6) {
        return false;
    }
    its_denm_t d;
    memset(&d, 0, sizeof(d));
    d.lat = ITS_LAT_UNAVAILABLE;
    d.lon = ITS_LON_UNAVAILABLE;

    uper_t u;
    uper_init(&u, pkt->pdu + 6, pkt->pdu_len - 6);

    bool has_situation = uper_bit(&u);
    (void)uper_bit(&u);                         /* location */
    (void)uper_bit(&u);                         /* alacarte */

    bool mc_ext = uper_bit(&u);                 /* ManagementContainer extension marker */
    bool has_term = uper_bit(&u);
    bool has_rd = uper_bit(&u);                 /* relevanceDistance / awarenessDistance */
    bool has_td = uper_bit(&u);                 /* relevanceTrafficDirection / trafficDirection */
    bool has_vd = uper_bit(&u);                 /* validityDuration (DEFAULT, present if != default) */
    bool has_ti = uper_bit(&u);                 /* transmissionInterval */

    d.station_id = (uint32_t)uper_get(&u, 32);
    d.sequence_number = (uint16_t)uper_get(&u, 16);
    uper_skip(&u, 84);                          /* detectionTime + referenceTime (2 x 42) */
    if (has_term) {
        uper_skip(&u, 1);
        d.termination = true;
    }
    d.lat = (int32_t)uper_constrained(&u, -900000000, 900000001);
    d.lon = (int32_t)uper_constrained(&u, -1800000000, 1800000001);
    uper_skip(&u, 60);                          /* positionConfidenceEllipse 36 + altitude 24 */
    if (u.err) {
        return false;
    }
    if (has_rd) { uper_skip(&u, 3); }
    if (has_td) { uper_skip(&u, 2); }
    if (has_vd) { uper_skip(&u, 17); }          /* 0..86400 */
    if (has_ti) { uper_skip(&u, 14); }          /* 1..10000 */
    uper_skip(&u, 8);                           /* stationType */
    if (mc_ext) {
        uper_skip_ext_additions(&u);
    }
    if (u.err) {
        return false;
    }
    if (has_situation) {
        (void)uper_bit(&u);                     /* SituationContainer extension marker */
        (void)uper_bit(&u);                     /* linkedCause */
        (void)uper_bit(&u);                     /* eventHistory / eventZone */
        uper_skip(&u, 3);                       /* informationQuality */
        (void)uper_bit(&u);                     /* CauseCode / CauseCodeV2 extension marker */
        uint8_t cc = (uint8_t)uper_get(&u, 8);
        uint8_t scc = (uint8_t)uper_get(&u, 8);
        if (u.err) {
            return false;
        }
        d.cause_code = cc;
        d.sub_cause_code = scc;
    }
    *out = d;
    return true;
}
