/* GPL-3.0-only. Patch entries of an installed game and the ps-patch-system settings file, without device access.
   Rules follow ps-patch-system (patch-server/patch_user_xml.cpp, patch_web_xml.cpp): the game version is the higher
   of APP_VER and VERSION, an entry applies when one of its comma separated AppVer values equals that version or is
   "mask", and its hash is the XOR of the FNV-1a hashes of "xml_orbis", the file name, Name, Title, AppElf and the
   matched AppVer value, written as %08x in !_settings.xml (<Settings><Patch hash=".." enabled="true"/>). */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "xml_merge.h"

#define GP_FOLDER "xml_orbis"

typedef struct {
    char name[160];
    char note[640];
    char hash[9];
    char elf[64];               /* AppElf: the executable the entry patches (eboot.bin when missing) */
    char title[160];
    int needs_pro, needs_8gib, needs_120hz;
    int fps;                    /* frame rate entry ("60 FPS", "120 FPS"): at most one per game and AppElf is enabled */
    int resolution;             /* NeoEnabler resolution entry (needs Pro mode): at most one per game and AppElf is enabled */
} gp_patch;

/* Value of a string key of a param.sfo; 0 when found. */
static int gp_sfo(const unsigned char *sfo, size_t n, const char *key, char *out, size_t cap) {
    out[0] = 0;
    if (n < 20 || memcmp(sfo, "\0PSF", 4)) return -1;
    uint32_t keys, data, count;
    memcpy(&keys, sfo + 8, 4); memcpy(&data, sfo + 12, 4); memcpy(&count, sfo + 16, 4);
    size_t klen = strlen(key) + 1;
    for (uint32_t i = 0; i < count && 20 + (size_t)(i + 1) * 16 <= n; i++) {
        const unsigned char *e = sfo + 20 + (size_t)i * 16;
        uint16_t key_off; uint32_t len, data_off;
        memcpy(&key_off, e, 2); memcpy(&len, e + 4, 4); memcpy(&data_off, e + 12, 4);
        if ((size_t)keys + key_off + klen > n || memcmp(sfo + keys + key_off, key, klen)) continue;
        if ((size_t)data + data_off + len > n) return -1;
        size_t m = strnlen((const char *)sfo + data + data_off, len);
        if (m >= cap) m = cap - 1;
        memcpy(out, sfo + data + data_off, m); out[m] = 0;
        return 0;
    }
    return -1;
}

/* ps-patch-system ver_to_num: "01.02" style versions; UINT32_MAX otherwise. */
static uint32_t gp_ver_num(const char *v) {
    int a = 0, b = 0, c = 0, dots = 0;
    for (const char *p = v; *p; p++) dots += *p == '.';
    if (dots == 1 && sscanf(v, "%d.%d", &a, &b) == 2)
        return (uint32_t)(((a / 10) << 12) | ((a % 10) << 8) | ((b / 10) << 4) | (b % 10));
    if (dots == 3 && sscanf(v, "%d.%d.%d", &a, &b, &c) == 3)
        return (uint32_t)(((a / 10) << 20) | ((a % 10) << 16) | ((b / 10) << 12) | ((b % 10) << 8) |
                          ((c / 10 % 10) << 4) | (c % 10));
    return UINT32_MAX;
}

/* The version ps-patch-system matches entries against: the higher of APP_VER and VERSION. */
static const char *gp_game_version(const char *app_ver, const char *version) {
    uint32_t a = gp_ver_num(app_ver), b = gp_ver_num(version);
    return a != UINT32_MAX && b != UINT32_MAX && b > a ? version : app_ver;
}

static uint32_t gp_sid(const char *s) {
    uint32_t h = 0x811c9dc5;
    while (*s) h = 0x01000193 * (h ^ (uint32_t)(signed char)*s++);
    return h;
}

/* Decodes the XML entities of an attribute value (as pugixml does before ps-patch-system hashes it). */
static void gp_decode(const char *s, size_t n, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 4 < cap;) {
        if (s[i] != '&') { out[o++] = s[i++]; continue; }
        const char *semi = memchr(s + i, ';', n - i < 12 ? n - i : 12);
        if (!semi) { out[o++] = s[i++]; continue; }
        size_t l = (size_t)(semi - (s + i)) + 1;
        unsigned long cp = 0; int ok = 1;
        if (l == 5 && !memcmp(s + i, "&amp;", 5)) cp = '&';
        else if (l == 4 && !memcmp(s + i, "&lt;", 4)) cp = '<';
        else if (l == 4 && !memcmp(s + i, "&gt;", 4)) cp = '>';
        else if (l == 6 && !memcmp(s + i, "&quot;", 6)) cp = '"';
        else if (l == 6 && !memcmp(s + i, "&apos;", 6)) cp = '\'';
        else if (l > 3 && s[i + 1] == '#') {
            char num[12] = {0}; memcpy(num, s + i + 2, l - 3);
            char *end; cp = num[0] == 'x' ? strtoul(num + 1, &end, 16) : strtoul(num, &end, 10);
            ok = *end == 0 && cp > 0 && cp < 0x110000;
        } else ok = 0;
        if (!ok) { out[o++] = s[i++]; continue; }
        if (cp < 0x80) out[o++] = (char)cp;                         /* UTF-8, as pugixml stores it */
        else if (cp < 0x800) { out[o++] = (char)(0xc0 | cp >> 6); out[o++] = (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { out[o++] = (char)(0xe0 | cp >> 12); out[o++] = (char)(0x80 | (cp >> 6 & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
        else { out[o++] = (char)(0xf0 | cp >> 18); out[o++] = (char)(0x80 | (cp >> 12 & 63)); out[o++] = (char)(0x80 | (cp >> 6 & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
        i += l;
    }
    out[o] = 0;
}

/* Decoded attribute of the opening tag of a block; empty when absent. */
static void gp_attr(const char *buf, const xm_block *b, const char *attr, char *out, size_t cap) {
    const char *tag = buf + b->start, *end = tag;
    char quote = 0;
    for (; end < buf + b->end; end++) {
        if (quote) { if (*end == quote) quote = 0; }
        else if (*end == '"' || *end == '\'') quote = *end;
        else if (*end == '>') break;
    }
    size_t n; const char *v = xm_attr(tag, end, attr, &n);
    if (v) gp_decode(v, n, out, cap); else out[0] = 0;
}

/* NeoEnabler requirements: NeoRequires="pro" or "pro,8gib"; entries without it are recognised by their name. */
static void gp_requirements(const char *requires, const char *name, int *pro, int *eight) {
    if (*requires) {
        *eight = strstr(requires, "8gib") != NULL;
        *pro = *eight || strstr(requires, "pro") != NULL;
        return;
    }
    *pro = strstr(name, "(PS4 Pro mode") != NULL;
    *eight = *pro && strstr(name, "requires 8 GiB") != NULL;
}

/* NeoRequires="120hz" (alone or with pro, 8gib): the entry runs the game at 120 fps and needs the 120 Hz video output.
   Entries without the attribute are recognised by a "120 FPS" or "120 Hz" in their name. */
static int gp_needs_120hz(const char *requires, const char *name) {
    if (*requires) return strstr(requires, "120hz") != NULL;
    for (const char *s = name; *s; s++)
        if (!strncasecmp(s, "120", 3) && (!strncasecmp(s + 3, " fps", 4) || !strncasecmp(s + 3, "fps", 3) || !strncasecmp(s + 3, " hz", 3) || !strncasecmp(s + 3, "hz", 2)))
            return 1;
    return 0;
}

/* Frame rate entry: a non-resolution entry with "30/60/120 fps" in its name ("60 FPS", "120fps", "60 FPS Unlock").
   Toggles of an in-game menu are not alternatives to each other and stay out. */
static int gp_is_fps(const char *name) {
    if (strcasestr(name, "toggle")) return 0;
    for (const char *s = name; *s; s++) {
        if (strncmp(s, "30", 2) && strncmp(s, "60", 2) && strncmp(s, "120", 3)) continue;
        const char *e = s + (*s == '1' ? 3 : 2);
        if (*e == ' ') e++;
        if (!strncasecmp(e, "fps", 3)) return 1;
    }
    return 0;
}

/* Entries of a title ID file (file = "CUSA00785.xml") that apply to version ver; returns how many were stored. */
static int gp_list(const char *xml, size_t len, const char *file, const char *ver, gp_patch *out, int cap) {
    int n = 0; xm_block b;
    char name[160], title[160], elf[64], vers[160], req[32];
    for (size_t pos = 0; n < cap && xm_next(xml, len, pos, &b); pos = b.end) {
        gp_attr(xml, &b, "AppVer", vers, sizeof vers);
        char matched[32] = ""; int found = 0;
        for (char *tok = vers; !found && tok;) {
            char *comma = strchr(tok, ','), *t = tok, *e;
            if (comma) *comma = 0;
            while (*t == ' ' || *t == '\t') t++;
            e = t + strlen(t);
            while (e > t && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            if (*t && (!strcmp(t, ver) || !strcmp(t, "mask")) && strlen(t) < sizeof matched) { strcpy(matched, t); found = 1; }
            tok = comma ? comma + 1 : NULL;
        }
        if (!found) continue;
        gp_patch *p = &out[n++];
        gp_attr(xml, &b, "Name", name, sizeof name);
        gp_attr(xml, &b, "Title", title, sizeof title);
        gp_attr(xml, &b, "AppElf", elf, sizeof elf);
        gp_attr(xml, &b, "NeoRequires", req, sizeof req);
        gp_attr(xml, &b, "Note", p->note, sizeof p->note);
        snprintf(p->name, sizeof p->name, "%s", name);
        snprintf(p->title, sizeof p->title, "%s", title);
        snprintf(p->elf, sizeof p->elf, "%s", elf[0] ? elf : "eboot.bin");   /* missing: the main executable */
        uint32_t h = gp_sid(GP_FOLDER) ^ gp_sid(file) ^ gp_sid(name) ^ gp_sid(title) ^ gp_sid(elf) ^ gp_sid(matched);
        snprintf(p->hash, sizeof p->hash, "%08x", h);
        gp_requirements(req, name, &p->needs_pro, &p->needs_8gib);
        p->needs_120hz = gp_needs_120hz(req, name);
        p->resolution = p->needs_pro;
        p->fps = !p->resolution && gp_is_fps(name);
    }
    return n;
}

/* 1 for an entry of the main executable, the one a title ID starts with. */
static inline int gp_main_elf(const gp_patch *p) { return !strcasecmp(p->elf, "eboot.bin"); }

/* Resolution part of an entry name: the text before " (PS4 Pro mode", after a "<game> - " prefix when there is one
   ("Uncharted 2: Among Thieves - 1440p (PS4 Pro mode)" -> "1440p"), so entries of two games of a collection pair up. */
static inline int gp_same_resolution(const char *a, const char *b) {
    const char *ea = strstr(a, " (PS4 Pro mode"), *eb = strstr(b, " (PS4 Pro mode");
    if (!ea || !eb) return !strcmp(a, b);
    for (const char *s = a; (s = strstr(s, " - ")) && s < ea; s += 3) a = s + 3;
    for (const char *s = b; (s = strstr(s, " - ")) && s < eb; s += 3) b = s + 3;
    return ea - a == eb - b && !memcmp(a, b, (size_t)(ea - a));
}

/* Collection rule: the title ID starts in eboot.bin, which sets PS4 Pro mode for the executables it starts. A
   resolution entry of another executable needs a resolution entry of eboot.bin when the file has one. Index of the
   first eboot.bin resolution entry, or -1 when the rule does not apply to entry k. */
static inline int gp_main_entry(const gp_patch *p, int n, int k) {
    if (!p[k].resolution || gp_main_elf(&p[k])) return -1;
    for (int i = 0; i < n; i++) if (p[i].resolution && gp_main_elf(&p[i])) return i;
    return -1;
}

/* Next <Patch .../> element of a settings file at or after pos: tag range and its hash and enabled values. */
static int gp_next_setting(const char *s, size_t len, size_t pos, size_t *start, size_t *end,
                           const char **hash, size_t *hash_len, const char **en, size_t *en_len) {
    for (const char *p; (p = xm_find(s + pos, len - pos, "<Patch"));) {
        size_t at = (size_t)(p - s);
        pos = at + 6;
        if (at + 6 >= len || !(p[6] == ' ' || p[6] == '\t' || p[6] == '\n' || p[6] == '\r' || p[6] == '/' || p[6] == '>')) continue;
        const char *t = p + 6; char quote = 0;
        for (; t < s + len; t++) {
            if (quote) { if (*t == quote) quote = 0; }
            else if (*t == '"' || *t == '\'') quote = *t;
            else if (*t == '>') break;
        }
        if (t >= s + len) return 0;
        *start = at; *end = (size_t)(t - s) + 1;
        *hash = xm_attr(p, t, "hash", hash_len);
        *en = xm_attr(p, t, "enabled", en_len);
        return 1;
    }
    return 0;
}

/* 1 when the first <Patch> with this hash is enabled (pugixml as_bool: 1, t, T, y or Y first), 0 otherwise. */
static int gp_setting(const char *s, size_t len, const char *hash) {
    size_t start, end = 0, hl, el; const char *h, *e;
    for (size_t pos = 0; gp_next_setting(s, len, pos, &start, &end, &h, &hl, &e, &el); pos = end)
        if (h && hl == strlen(hash) && !strncasecmp(h, hash, hl))
            return e && el && strchr("1tTyY", e[0]) != NULL;
    return 0;
}

/* New settings text with every <Patch> of this hash set to enabled/disabled, or a new <Patch> before </Settings>.
   A missing or empty file gets a new document. Returns 0 and a malloc'd buffer, -1 on a malformed file. */
static int gp_setting_set(const char *s, size_t len, const char *hash, int enabled, char **out, size_t *out_len) {
    const char *value = enabled ? "true" : "false";
    /* every <Patch> is at least 7 bytes and grows by at most 18 (enabled="false" added) */
    size_t cap = len * 4 + 256, o = 0, start, end = 0, pos = 0, hl, el; const char *h, *e;
    char *buf = malloc(cap + 1);
    if (!buf) return -1;
    int found = 0;
    size_t copied = 0;                                             /* s is copied to buf up to here */
    for (; gp_next_setting(s, len, pos, &start, &end, &h, &hl, &e, &el); pos = end) {
        if (!h || hl != strlen(hash) || strncasecmp(h, hash, hl)) continue;
        found = 1;
        size_t cut = e ? (size_t)(e - s) : (size_t)(h - s) + hl + 1;   /* enabled value, or after the hash quote */
        memcpy(buf + o, s + copied, cut - copied); o += cut - copied;
        if (e) { memcpy(buf + o, value, strlen(value)); o += strlen(value); copied = cut + el; }
        else { o += (size_t)snprintf(buf + o, cap + 1 - o, " enabled=\"%s\"", value); copied = cut; }
    }
    memcpy(buf + o, s + copied, len - copied); o += len - copied;
    buf[o] = 0;
    if (!found) {
        char line[64];
        int ln = snprintf(line, sizeof line, "  <Patch hash=\"%s\" enabled=\"%s\" />\n", hash, value);
        const char *close = xm_find(buf, o, "</Settings>");
        if (!close) {
            if (xm_find(buf, o, "<Settings")) { free(buf); return -1; }
            free(buf);
            buf = malloc(128); if (!buf) return -1;
            o = (size_t)snprintf(buf, 128, "<?xml version=\"1.0\"?>\n<Settings>\n%s</Settings>\n", line);
        } else {
            size_t at = (size_t)(close - buf);
            char *b2 = malloc(o + (size_t)ln + 2);
            if (!b2) { free(buf); return -1; }
            size_t k = 0;
            memcpy(b2, buf, at); k = at;
            if (at && b2[at - 1] != '\n') b2[k++] = '\n';
            memcpy(b2 + k, line, (size_t)ln); k += (size_t)ln;
            memcpy(b2 + k, buf + at, o - at); k += o - at;
            b2[k] = 0; free(buf); buf = b2; o = k;
        }
    }
    *out = buf; *out_len = o;
    return 0;
}
