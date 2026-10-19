/* Host test of src/attr_detect.h: param.sfo integers, the ATTRIBUTE offset choice and the log dump. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/attr_detect.h"

static void put(unsigned char *info, size_t off, uint32_t v) { memcpy(info + off, &v, 4); }

/* param.sfo with TITLE (string), ATTRIBUTE and, when attr2 >= 0, ATTRIBUTE2. */
static size_t make_sfo(unsigned char *s, uint32_t attr, long long attr2) {
    const char *keys[] = {"ATTRIBUTE", "ATTRIBUTE2", "TITLE"};
    int count = attr2 >= 0 ? 3 : 2;
    if (attr2 < 0) keys[1] = "TITLE";
    size_t key_table = 20 + 16 * (size_t)count, kl = 0, dl = 0;
    unsigned char kt[64] = {0}, dt[64] = {0};
    memset(s, 0, 256);
    memcpy(s, "\0PSF", 4);
    for (int i = 0; i < count; i++) {
        unsigned char *e = s + 20 + 16 * i;
        uint16_t ko = (uint16_t)kl, fmt = strcmp(keys[i], "TITLE") ? 0x0404 : 0x0204;
        uint32_t len = fmt == 0x0404 ? 4 : 5, doff = (uint32_t)dl;
        memcpy(e, &ko, 2); memcpy(e + 2, &fmt, 2); memcpy(e + 4, &len, 4); memcpy(e + 8, &len, 4); memcpy(e + 12, &doff, 4);
        memcpy(kt + kl, keys[i], strlen(keys[i]) + 1); kl += strlen(keys[i]) + 1;
        if (fmt == 0x0204) memcpy(dt + dl, "Game", 5);
        else { uint32_t v = i == 0 ? attr : (uint32_t)attr2; memcpy(dt + dl, &v, 4); }
        dl += len;
    }
    uint32_t kto = (uint32_t)key_table, dto = (uint32_t)(key_table + 64), c = (uint32_t)count;
    memcpy(s + 8, &kto, 4); memcpy(s + 12, &dto, 4); memcpy(s + 16, &c, 4);
    memcpy(s + kto, kt, kl); memcpy(s + dto, dt, dl);
    return dto + dl;
}

int main(void) {
    unsigned char sfo[256]; uint32_t v;
    size_t n = make_sfo(sfo, 2, 0);
    assert(!ad_sfo_int(sfo, n, "ATTRIBUTE", &v) && v == 2);
    assert(!ad_sfo_int(sfo, n, "ATTRIBUTE2", &v) && v == 0);
    assert(ad_sfo_int(sfo, n, "TITLE", &v) == -1);          /* string, not an integer */
    assert(ad_sfo_int(sfo, n, "MISSING", &v) == -1);
    assert(ad_sfo_int(sfo, 10, "ATTRIBUTE", &v) == -1);
    n = make_sfo(sfo, 0x802, -1);
    assert(!ad_sfo_int(sfo, n, "ATTRIBUTE", &v) && v == 0x802);
    assert(ad_sfo_int(sfo, n, "ATTRIBUTE2", &v) == -1);

    unsigned char info[AD_INFO_SIZE]; ad_result r;

    /* 13.20 layout: the table offset matches */
    memset(info, 0xee, sizeof info);
    put(info, 0x214, 2); put(info, 0x218, 0);
    ad_pick(info, sizeof info, 2, 1, 0, 0x214, &r);
    assert(r.offset == 0x214 && r.count == 1 && !strcmp(r.why, "firmware table"));

    /* the 13.00 case: table says 0x1f4, the values are at 0x214 */
    put(info, 0x1f4, 0x12345); put(info, 0x1f8, 7);
    ad_pick(info, sizeof info, 2, 1, 0, 0x1f4, &r);
    assert(r.offset == 0x214 && !strcmp(r.why, "offset of another firmware"));

    /* the table offset wins over other matches */
    put(info, 0x1f4, 2); put(info, 0x1f8, 0);
    ad_pick(info, sizeof info, 2, 1, 0, 0x1f4, &r);
    assert(r.offset == 0x1f4 && r.count == 2);

    /* unknown layout, one match */
    memset(info, 0xee, sizeof info);
    put(info, 0x230, 2); put(info, 0x234, 0);
    ad_pick(info, sizeof info, 2, 1, 0, 0x1f4, &r);
    assert(r.offset == 0x230 && !strcmp(r.why, "single match"));

    /* unknown layout, two matches: no choice, the shim keeps its table offset */
    put(info, 0x300, 2); put(info, 0x304, 0);
    ad_pick(info, sizeof info, 2, 1, 0, 0x1f4, &r);
    assert(r.offset == 0 && r.count == 2 && !strcmp(r.why, "several matches, none known"));

    /* without ATTRIBUTE2 only ATTRIBUTE is compared; offsets below AD_MIN_OFFSET are ignored */
    memset(info, 0xee, sizeof info);
    put(info, 0x40, 2); put(info, 0x26c, 2);
    ad_pick(info, sizeof info, 2, 0, 0, 0x26c, &r);
    assert(r.offset == 0x26c && r.count == 1);

    /* no match */
    memset(info, 0, sizeof info);
    ad_pick(info, sizeof info, 2, 1, 0, 0x214, &r);
    assert(r.offset == 0 && r.count == 0 && !strcmp(r.why, "no match"));

    /* many matches: counted past the list */
    for (size_t off = AD_MIN_OFFSET; off + 4 <= sizeof info; off += 4) put(info, off, 2);
    ad_pick(info, sizeof info, 2, 0, 0, 0x214, &r);
    assert(r.offset == 0x214 && r.count > AD_MAX_CANDIDATES);

    /* dump: rows of 8 dwords, cut cleanly when the buffer is short */
    memset(info, 0, sizeof info);
    put(info, 0x1c0, 0xabc); put(info, 0x1e0, 2);
    char out[400];
    ad_dump(info, sizeof info, 0x1c0, 0x1e8, out, sizeof out);
    assert(!strcmp(out, "1c0:abc 0 0 0 0 0 0 0 | 1e0:2 0"));
    char tiny[12];
    ad_dump(info, sizeof info, 0x1c0, 0x2a0, tiny, sizeof tiny);
    assert(strlen(tiny) < sizeof tiny);
    puts("PASS attr detect: param.sfo integers, table offset first, other firmwares' offsets, single match, ambiguous, dump");
    return 0;
}
