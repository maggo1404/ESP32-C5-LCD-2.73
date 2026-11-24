/*
 * its_frame.c - IEEE 802.11 / LLC-SNAP / GeoNetworking / BTP parser.
 *
 * Layers (ETSI EN 302 636-4-1, EN 302 636-5-1, ETSI TS 103 097, IEEE 1609.2):
 *   802.11 data header (24, +2 QoS, +6 4-address, +4 HT control)
 *   LLC/SNAP  AA AA 03 00 00 00 89 47
 *   GN Basic header (4)  NH: 1 = common header, 2 = secured packet
 *   [secured: Ieee1609Dot2Data in COER -> signedData -> tbsData -> payload ->
 *             unsecuredData = GN common header + extended header + payload]
 *   GN Common header (8), extended header by header type
 *   BTP-A / BTP-B (4), ItsPduHeader (6)
 *
 * Header-type handling is informed by the structure of the GeoNetworking
 * parser of cits-to-go (GPL-3.0, https://github.com/OpenTrafficMap/cits-to-go).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>

#include "its.h"

#define WLAN_HDR_MIN      24u
#define LLC_SNAP_LEN       8u
#define GN_BASIC_LEN       4u
#define GN_COMMON_LEN      8u
#define BTP_LEN            4u
#define ITS_PDU_HDR_LEN    6u
#define GN_LPV_LEN        24u   /* long position vector */
#define GN_SPV_LEN        20u   /* short position vector */

/* GN header types */
enum { HT_ANY = 0, HT_BEACON = 1, HT_GUC = 2, HT_GAC = 3, HT_GBC = 4, HT_TSB = 5, HT_LS = 6 };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Length of the GN extended header, or -1 if the combination is unknown. */
static int gn_ext_len(unsigned ht, unsigned hst)
{
    switch (ht) {
    case HT_ANY:    return 0;
    case HT_BEACON: return hst == 0 ? (int)GN_LPV_LEN : -1;
    case HT_GUC:    return hst == 0 ? (int)(4 + GN_LPV_LEN + GN_SPV_LEN) : -1;   /* 48 */
    case HT_GAC:
    case HT_GBC:    return hst <= 2 ? (int)(4 + GN_LPV_LEN + 16) : -1;           /* 44 */
    case HT_TSB:    return hst <= 1 ? (hst == 0 ? (int)(GN_LPV_LEN + 4) : (int)(4 + GN_LPV_LEN)) : -1; /* 28 */
    case HT_LS:     return hst == 0 ? (int)(4 + GN_LPV_LEN + 8)                  /* 36 */
                         : hst == 1 ? (int)(4 + GN_LPV_LEN + GN_SPV_LEN)         /* 48 */
                         : -1;
    default:        return -1;
    }
}

/* Parse GN common header .. ITS PDU header in p[0..n). p is the start of the
 * common header; the packet (including its PL) must lie inside n bytes. */
static its_err_t parse_common(const uint8_t *p, size_t n, its_packet_t *out)
{
    if (n < GN_COMMON_LEN) {
        return ITS_ERR_TOO_SHORT;
    }
    unsigned nh  = p[0] >> 4;
    unsigned ht  = p[1] >> 4;
    unsigned hst = p[1] & 0x0f;
    size_t   pl  = rd16(p + 4);

    out->gn_header_type = (uint8_t)ht;
    out->gn_header_subtype = (uint8_t)hst;

    int ext = gn_ext_len(ht, hst);
    if (ext < 0) {
        return ITS_ERR_GN_UNSUPPORTED;
    }
    if (n < GN_COMMON_LEN + (size_t)ext) {
        return ITS_ERR_TOO_SHORT;
    }
    if (ht == HT_BEACON || ht == HT_LS || ht == HT_ANY || nh == 0) {
        /* Management / payload-less packets: valid GN, no ITS message. */
        out->pdu = NULL;
        out->pdu_len = 0;
        out->message_id = 0;
        out->protocol_version = 0;
        out->station_id = 0;
        out->btp_dst_port = 0;
        return ITS_OK;
    }
    if (nh != 1 && nh != 2) {     /* 1 = BTP-A, 2 = BTP-B, 3 = IPv6 (unsupported) */
        return ITS_ERR_GN_UNSUPPORTED;
    }
    const uint8_t *payload = p + GN_COMMON_LEN + ext;
    size_t avail = n - GN_COMMON_LEN - (size_t)ext;
    if (pl > avail) {
        return ITS_ERR_TOO_SHORT;
    }
    if (pl < BTP_LEN) {
        return ITS_ERR_BTP;
    }
    out->btp_dst_port = rd16(payload);        /* BTP-A and BTP-B both start with dst port */
    if (pl < BTP_LEN + ITS_PDU_HDR_LEN) {
        return ITS_ERR_DECODE;
    }
    const uint8_t *pdu = payload + BTP_LEN;
    out->pdu = pdu;
    out->pdu_len = pl - BTP_LEN;
    out->protocol_version = pdu[0];
    out->message_id = pdu[1];
    out->station_id = rd32(pdu + 2);
    return ITS_OK;
}

/* ------------------------------------------------------------------ */
/* IEEE 1609.2 (COER) unwrapping                                       */
/* ------------------------------------------------------------------ */

/* OER length determinant. Returns false on error. */
static bool oer_length(const uint8_t *p, size_t n, size_t *pos, size_t *len)
{
    if (*pos >= n) {
        return false;
    }
    unsigned b = p[(*pos)++];
    if (b < 0x80) {
        *len = b;
        return true;
    }
    unsigned k = b & 0x7f;
    if (k == 0 || k > 4 || *pos + k > n) {
        return false;
    }
    size_t v = 0;
    while (k--) {
        v = (v << 8) | p[(*pos)++];
    }
    *len = v;
    return true;
}

/* Parse Ieee1609Dot2Data starting at p[*pos]; on success the unsecuredData
 * octets are [*off, *off+*len). 'depth' 0 = outer (may be signedData),
 * 1 = nested inside signedData payload (must be unsecuredData). */
static bool oer_unwrap(const uint8_t *p, size_t n, size_t pos, int depth,
                       size_t *off, size_t *len)
{
    if (pos + 2 > n || p[pos] != 3) {       /* protocolVersion 3 */
        return false;
    }
    unsigned tag = p[pos + 1];
    pos += 2;
    if (tag == 0x80) {                        /* unsecuredData Opaque */
        size_t l;
        if (!oer_length(p, n, &pos, &l) || l > n - pos) {
            return false;
        }
        *off = pos;
        *len = l;
        return true;
    }
    if (tag != 0x81 || depth != 0) {          /* only signedData at the outer level */
        return false;
    }
    /* SignedData ::= SEQUENCE { hashId, tbsData, signer, signature } (no ext marker)
     * hashId: ENUMERATED -> 1 octet.
     * ToBeSignedData ::= SEQUENCE { payload, headerInfo } (no ext marker in
     * TS 103 097 v1.3.1; later editions with a marker would add one preamble
     * octet, tolerated below).
     * SignedDataPayload ::= SEQUENCE { data OPT, extDataHash OPT, ... }:
     * preamble octet: bit7 ext, bit6 data present, bit5 extDataHash present. */
    if (pos >= n || p[pos] >= 0x80) {
        return false;
    }
    pos++;                                    /* hashId */
    for (int extra = 0; extra < 2; extra++) {
        size_t q = pos + (size_t)extra;
        if (q >= n) {
            return false;
        }
        if (extra == 1 && p[pos] != 0x00 && p[pos] != 0x80) {
            return false;                     /* not a plausible ToBeSignedData preamble */
        }
        unsigned pre = p[q];
        if ((pre & 0x40) == 0 || (pre & 0x1f) != 0) {
            continue;
        }
        if (oer_unwrap(p, n, q + 1, 1, off, len)) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */

its_err_t its_parse_frame(const uint8_t *frame, size_t len, its_packet_t *out)
{
    if (out == NULL) {
        return ITS_ERR_DECODE;
    }
    memset(out, 0, sizeof(*out));
    if (frame == NULL || len < WLAN_HDR_MIN) {
        return ITS_ERR_TOO_SHORT;
    }
    unsigned fc0 = frame[0];
    unsigned fc1 = frame[1];
    unsigned type = (fc0 >> 2) & 3u;
    unsigned subtype = (fc0 >> 4) & 0xfu;
    if (type != 2u) {                         /* only data frames carry GN */
        return ITS_ERR_NOT_GN;
    }
    if (subtype & 0x4u) {                     /* Null / CF-Ack-less data without payload */
        return ITS_ERR_NOT_GN;
    }
    size_t hdr = WLAN_HDR_MIN;
    if ((fc1 & 3u) == 3u) {                   /* ToDS && FromDS: fourth address */
        hdr += 6;
    }
    bool qos = (subtype & 0x8u) != 0;
    if (qos) {
        hdr += 2;
        if (fc1 & 0x80u) {                    /* Order bit with QoS: HT control */
            hdr += 4;
        }
    }
    memcpy(out->src_mac, frame + 10, 6);
    if (len < hdr + LLC_SNAP_LEN) {
        return ITS_ERR_TOO_SHORT;
    }
    static const uint8_t snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
    const uint8_t *llc = frame + hdr;
    if (memcmp(llc, snap, 6) != 0 || llc[6] != 0x89 || llc[7] != 0x47) {
        return ITS_ERR_NOT_GN;
    }
    const uint8_t *gn = llc + LLC_SNAP_LEN;
    size_t gn_len = len - hdr - LLC_SNAP_LEN;
    if (gn_len < GN_BASIC_LEN) {
        return ITS_ERR_TOO_SHORT;
    }
    unsigned bver = gn[0] >> 4;
    unsigned bnh = gn[0] & 0x0f;
    if (bver == 0 || bver > 2) {
        return ITS_ERR_GN_UNSUPPORTED;
    }
    const uint8_t *body = gn + GN_BASIC_LEN;
    size_t body_len = gn_len - GN_BASIC_LEN;

    if (bnh == 1) {
        return parse_common(body, body_len, out);
    }
    if (bnh == 2) {
        out->secured = true;
        size_t off = 0, l = 0;
        if (!oer_unwrap(body, body_len, 0, 0, &off, &l)) {
            return ITS_ERR_SECURITY;
        }
        its_err_t e = parse_common(body + off, l, out);
        return e == ITS_ERR_TOO_SHORT ? ITS_ERR_SECURITY : e;
    }
    return ITS_ERR_GN_UNSUPPORTED;
}
