/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "uper.h"

void uper_init(uper_t *u, const uint8_t *buf, size_t len)
{
    u->buf = buf;
    u->err = false;
    u->pos = 0;
    if (buf == NULL || len > (UINT32_MAX / 8)) {
        u->nbits = 0;
        u->err = (buf == NULL && len != 0) || len > (UINT32_MAX / 8);
        return;
    }
    u->nbits = (uint32_t)(len * 8);
}

uint64_t uper_get(uper_t *u, unsigned nbits)
{
    if (u->err || nbits > 64 || (uint64_t)u->pos + nbits > u->nbits) {
        u->err = true;
        return 0;
    }
    uint64_t v = 0;
    unsigned left = nbits;
    while (left) {
        unsigned off = u->pos & 7u;
        unsigned avail = 8u - off;
        unsigned take = left < avail ? left : avail;
        unsigned b = u->buf[u->pos >> 3];
        b = (b >> (avail - take)) & ((1u << take) - 1u);
        v = (v << take) | b;
        u->pos += take;
        left -= take;
    }
    return v;
}

bool uper_bit(uper_t *u)
{
    return uper_get(u, 1) != 0;
}

unsigned uper_width(uint64_t range)
{
    unsigned w = 0;
    uint64_t r = range > 0 ? range - 1 : 0;
    while (r) {
        w++;
        r >>= 1;
    }
    return w;
}

int64_t uper_constrained(uper_t *u, int64_t lo, int64_t hi)
{
    if (hi < lo) {
        u->err = true;
        return lo;
    }
    uint64_t range = (uint64_t)(hi - lo) + 1u;
    unsigned w = uper_width(range);
    int64_t v = lo + (int64_t)uper_get(u, w);
    if (v > hi) {
        u->err = true;
        return lo;
    }
    return u->err ? lo : v;
}

int32_t uper_length(uper_t *u)
{
    if (!uper_bit(u)) {
        return (int32_t)uper_get(u, 7);
    }
    if (!uper_bit(u)) {
        return (int32_t)uper_get(u, 14);
    }
    u->err = true; /* fragmented lengths are not supported */
    return -1;
}

uint32_t uper_normally_small(uper_t *u)
{
    if (!uper_bit(u)) {
        return (uint32_t)uper_get(u, 6);
    }
    int32_t n = uper_length(u);
    if (n < 1 || n > 4) {
        u->err = true;
        return 0;
    }
    return (uint32_t)uper_get(u, (unsigned)n * 8u);
}

void uper_skip(uper_t *u, size_t nbits)
{
    if (u->err || (uint64_t)u->pos + nbits > u->nbits) {
        u->err = true;
        return;
    }
    u->pos += (uint32_t)nbits;
}

void uper_skip_open(uper_t *u)
{
    int32_t n = uper_length(u);
    if (n < 0) {
        return;
    }
    uper_skip(u, (size_t)n * 8u);
}

void uper_skip_ext_additions(uper_t *u)
{
    uint32_t n = uper_normally_small(u);
    if (u->err) {
        return;
    }
    n += 1;
    if (n > 4096) {
        u->err = true;
        return;
    }
    uint32_t present = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (uper_bit(u)) {
            present++;
        }
    }
    for (uint32_t i = 0; i < present && !u->err; i++) {
        uper_skip_open(u);
    }
}

uint32_t uper_enum(uper_t *u, uint32_t n)
{
    int64_t v = uper_constrained(u, 0, (int64_t)n - 1);
    return (uint32_t)v;
}

uint32_t uper_enum_ext(uper_t *u, uint32_t n_root)
{
    if (uper_bit(u)) {
        return n_root + uper_normally_small(u);
    }
    return uper_enum(u, n_root);
}

uint32_t uper_choice_ext(uper_t *u, uint32_t n_root, bool *is_ext)
{
    *is_ext = false;
    if (uper_bit(u)) {
        uint32_t idx = uper_normally_small(u);
        uper_skip_open(u);
        *is_ext = true;
        return n_root + idx;
    }
    return uper_enum(u, n_root);
}

void uper_ia5(uper_t *u, int lo, int hi, char *dst, size_t dstsz)
{
    int len = (int)uper_constrained(u, lo, hi);
    size_t w = 0;
    for (int i = 0; i < len && !u->err; i++) {
        char c = (char)uper_get(u, 7);
        if (c < 0x20 || c == 0x7f) {
            c = '?';
        }
        if (dst != NULL && w + 1 < dstsz) {
            dst[w++] = c;
        }
    }
    if (dst != NULL && dstsz > 0) {
        dst[w] = '\0';
    }
}

void uper_skip_regional(uper_t *u)
{
    (void)uper_get(u, 8);   /* regionId */
    uper_skip_open(u);
}

void uper_skip_regional_list(uper_t *u)
{
    int n = (int)uper_constrained(u, 1, 4);
    for (int i = 0; i < n && !u->err; i++) {
        uper_skip_regional(u);
    }
}
