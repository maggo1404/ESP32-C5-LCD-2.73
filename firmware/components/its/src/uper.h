/*
 * uper.h - minimal, bounds-checked ASN.1 UPER (ITU-T X.691 unaligned) reader.
 *
 * The reader keeps a sticky error flag: after any out-of-range access or
 * malformed encoding every further read returns 0 and u->err stays true, so
 * decoders can read a whole structure and check the flag once.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *buf;
    uint32_t nbits;   /* total readable bits */
    uint32_t pos;     /* current bit position */
    bool     err;
} uper_t;

void     uper_init(uper_t *u, const uint8_t *buf, size_t len);
uint64_t uper_get(uper_t *u, unsigned nbits);              /* 0..64 bits */
bool     uper_bit(uper_t *u);
/* Constrained whole number lo..hi (no extension marker). Out-of-range -> err. */
int64_t  uper_constrained(uper_t *u, int64_t lo, int64_t hi);
/* Unconstrained length determinant (X.691 11.9, no fragmentation). -1 on error. */
int32_t  uper_length(uper_t *u);
/* Normally small non-negative whole number (X.691 11.6). */
uint32_t uper_normally_small(uper_t *u);
void     uper_skip(uper_t *u, size_t nbits);
/* Skip an open type (length determinant + octets). */
void     uper_skip_open(uper_t *u);
/* After the root components of a SEQUENCE whose extension bit was set: skip all
 * extension additions (presence bitmap + open types). */
void     uper_skip_ext_additions(uper_t *u);
/* ENUMERATED with extension marker and n_root root values. Returns the root
 * index, or n_root + k for an unknown extension value. */
uint32_t uper_enum_ext(uper_t *u, uint32_t n_root);
/* Plain (non-extensible) ENUMERATED with n values. */
uint32_t uper_enum(uper_t *u, uint32_t n);
/* CHOICE header with extension marker and n_root alternatives. Returns the
 * alternative index; sets *is_ext when it is an unknown extension alternative
 * (whose open type has then already been skipped). */
uint32_t uper_choice_ext(uper_t *u, uint32_t n_root, bool *is_ext);
/* IA5String (SIZE(lo..hi)), 7 bit characters. Copies up to dstsz-1 chars. */
void     uper_ia5(uper_t *u, int lo, int hi, char *dst, size_t dstsz);
/* SEQUENCE (SIZE(1..4)) OF RegionalExtension / single RegionalExtension. */
void     uper_skip_regional_list(uper_t *u);
void     uper_skip_regional(uper_t *u);
/* Number of bits needed to encode a range of 'range' values. */
unsigned uper_width(uint64_t range);
