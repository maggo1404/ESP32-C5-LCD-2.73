/*
 * its_cam.c - UPER decoder for CAM (ETSI EN 302 637-2 v1.3/v1.4 and
 * TS 103 900 v2). Only the fields shown by the UI are extracted.
 *
 * The layouts of both editions are identical up to and including
 * BasicVehicleContainerHighFrequency.speed:
 *   CAM/CoopAwareness/CamPayload: not extensible
 *   CamParameters: ext, lowFrequency?, specialVehicle?
 *   BasicContainer: ext, stationType(8), ReferencePosition(lat 31, lon 32,
 *                   ellipse 3x12, altitude 20+4), [additions]
 *   HighFrequencyContainer: CHOICE ext, 1 bit
 *   BasicVehicleContainerHighFrequency: 7 optional bits, heading(12+7), speed(14+7)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>

#include "its.h"
#include "uper.h"

bool its_decode_cam(const its_packet_t *pkt, its_cam_t *out)
{
    if (pkt == NULL || out == NULL || pkt->pdu == NULL || pkt->message_id != ITS_MSG_CAM ||
        pkt->pdu_len < 6) {
        return false;
    }
    its_cam_t c;
    memset(&c, 0, sizeof(c));
    c.station_id = pkt->station_id;
    c.lat = ITS_LAT_UNAVAILABLE;
    c.lon = ITS_LON_UNAVAILABLE;
    c.speed = ITS_SPEED_UNAVAILABLE;
    c.heading = ITS_HEADING_UNAVAILABLE;

    uper_t u;
    uper_init(&u, pkt->pdu + 6, pkt->pdu_len - 6);

    uper_skip(&u, 16);                          /* generationDeltaTime */
    (void)uper_bit(&u);                         /* CamParameters extension marker */
    (void)uper_bit(&u);                         /* lowFrequencyContainer present */
    (void)uper_bit(&u);                         /* specialVehicleContainer present */

    bool bc_ext = uper_bit(&u);                 /* BasicContainer */
    c.station_type = (uint8_t)uper_get(&u, 8);
    c.lat = (int32_t)uper_constrained(&u, -900000000, 900000001);
    c.lon = (int32_t)uper_constrained(&u, -1800000000, 1800000001);
    uper_skip(&u, 36);                          /* positionConfidenceEllipse */
    uper_skip(&u, 24);                          /* altitude value 20 + confidence 4 */
    if (bc_ext) {
        uper_skip_ext_additions(&u);
    }

    bool hf_ext = false;
    uint32_t hf = uper_choice_ext(&u, 2, &hf_ext);
    if (u.err) {
        return false;
    }
    if (!hf_ext && hf == 0) {                   /* basicVehicleContainerHighFrequency */
        uper_skip(&u, 7);                       /* optional-component bitmap */
        int64_t heading = uper_constrained(&u, 0, 3601);
        uper_skip(&u, 7);                       /* headingConfidence */
        int64_t speed = uper_constrained(&u, 0, 16383);
        if (!u.err) {
            c.heading = (uint16_t)heading;
            c.speed = (uint16_t)speed;
        }
    }
    /* Truncation inside the (optional) high-frequency details is tolerated as
     * long as the mandatory position was read. */
    *out = c;
    return true;
}
