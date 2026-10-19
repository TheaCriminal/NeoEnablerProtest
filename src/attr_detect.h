/* GPL-3.0-only. Finds the ATTRIBUTE offset in the ShellCore app info at launch time. The per-firmware table
   (shellcore_hook.h attr_offset) was measured on 4.50, 8.00, 11.00 and 13.20 only, so on other firmwares the shim
   can read the wrong field and the game starts in base mode. The app info holds ATTRIBUTE and ATTRIBUTE2 of the
   game's param.sfo next to each other: the offsets where both match are candidates. The firmware's table offset
   wins when it is one of them, then the other known offsets, then a single candidate. No PS5-only code: the
   tests build it on the host. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AD_INFO_SIZE 0x400     /* app info bytes read from ShellCore */
#define AD_MIN_OFFSET 0x100
#define AD_MAX_CANDIDATES 16

/* Integer value (format 0x0404) of a param.sfo key; 0 when found. */
static int ad_sfo_int(const unsigned char *sfo, size_t n, const char *key, uint32_t *out) {
    if (n < 20 || memcmp(sfo, "\0PSF", 4)) return -1;
    uint32_t keys, data, count;
    memcpy(&keys, sfo + 8, 4); memcpy(&data, sfo + 12, 4); memcpy(&count, sfo + 16, 4);
    size_t klen = strlen(key) + 1;
    for (uint32_t i = 0; i < count && 20 + (size_t)(i + 1) * 16 <= n; i++) {
        const unsigned char *e = sfo + 20 + (size_t)i * 16;
        uint16_t key_off, fmt; uint32_t len, data_off;
        memcpy(&key_off, e, 2); memcpy(&fmt, e + 2, 2); memcpy(&len, e + 4, 4); memcpy(&data_off, e + 12, 4);
        if ((size_t)keys + key_off + klen > n || memcmp(sfo + keys + key_off, key, klen)) continue;
        if (fmt != 0x0404 || len != 4 || (size_t)data + data_off + 4 > n) return -1;
        memcpy(out, sfo + data + data_off, 4);
        return 0;
    }
    return -1;
}

typedef struct {
    uint32_t offset;                        /* chosen offset, 0 when none */
    uint32_t candidates[AD_MAX_CANDIDATES]; /* offsets where the param.sfo values match */
    int count;                              /* every match, also those past AD_MAX_CANDIDATES */
    const char *why;
} ad_result;

/* ATTRIBUTE (and ATTRIBUTE2 at + 4 when has_attr2) found at off of the app info. */
static int ad_match(const unsigned char *info, size_t n, size_t off, uint32_t attr, int has_attr2, uint32_t attr2) {
    uint32_t a, b;
    if (off < AD_MIN_OFFSET || off % 4 || off + 8 > n) return 0;
    memcpy(&a, info + off, 4); memcpy(&b, info + off + 4, 4);
    return a == attr && (!has_attr2 || b == attr2);
}

/* info: n bytes of the app info. */
static void ad_pick(const unsigned char *info, size_t n, uint32_t attr, int has_attr2, uint32_t attr2,
                    uint32_t table_offset, ad_result *r) {
    static const uint32_t known[] = {0x214, 0x1f4, 0x26c};
    memset(r, 0, sizeof(*r));
    for (size_t off = AD_MIN_OFFSET; off + 8 <= n; off += 4) {
        if (!ad_match(info, n, off, attr, has_attr2, attr2)) continue;
        if (r->count < AD_MAX_CANDIDATES) r->candidates[r->count] = (uint32_t)off;
        r->count++;
    }
    if (ad_match(info, n, table_offset, attr, has_attr2, attr2)) { r->offset = table_offset; r->why = "firmware table"; return; }
    for (size_t k = 0; k < sizeof(known) / sizeof(known[0]); k++)
        if (ad_match(info, n, known[k], attr, has_attr2, attr2)) { r->offset = known[k]; r->why = "offset of another firmware"; return; }
    if (r->count == 1) { r->offset = r->candidates[0]; r->why = "single match"; return; }
    r->why = r->count ? "several matches, none known" : "no match";
}

/* Hex dwords of info[from, to) for the log, "off:v v v ..." in rows of 8 dwords. */
static void ad_dump(const unsigned char *info, size_t n, size_t from, size_t to, char *out, size_t cap) {
    size_t used = 0; out[0] = 0;
    for (size_t off = from; off + 4 <= to && off + 4 <= n && used + 16 < cap; off += 4) {
        uint32_t v; memcpy(&v, info + off, 4);
        int w = (off - from) % 32 ? snprintf(out + used, cap - used, " %x", v)
                                  : snprintf(out + used, cap - used, "%s%x:%x", off == from ? "" : " | ", (unsigned)off, v);
        if (w < 0 || (size_t)w >= cap - used) { out[used] = 0; break; }
        used += (size_t)w;
    }
}
