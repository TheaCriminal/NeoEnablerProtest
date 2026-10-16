/* GPL-3.0-only. Merges NeoEnabler patch entries into a ps-patch-system XML without parsing the whole file:
   each <Metadata ...>...</Metadata> block is one entry, keyed by its Name and AppVer attributes. */
#pragma once
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    size_t start, end;          /* byte range of the whole block, '<' to after "</Metadata>" */
    const char *title, *name, *ver;
    size_t title_len, name_len, ver_len;
} xm_block;

static const char *xm_find(const char *s, size_t n, const char *pat) {
    size_t m = strlen(pat);
    for (size_t i = 0; m <= n && i <= n - m; i++)
        if (!memcmp(s + i, pat, m)) return s + i;
    return NULL;
}

/* Value of attribute `attr` inside the tag [tag, tag_end); NULL when absent. */
static const char *xm_attr(const char *tag, const char *tag_end, const char *attr, size_t *len) {
    size_t m = strlen(attr);
    for (const char *p = tag; p + m < tag_end; p++) {
        if (!isspace((unsigned char)p[0]) || memcmp(p + 1, attr, m)) continue;
        const char *q = p + 1 + m;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        if (q >= tag_end || *q != '=') continue;
        q++;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        if (q >= tag_end || (*q != '"' && *q != '\'')) continue;
        char quote = *q++;
        const char *e = memchr(q, quote, (size_t)(tag_end - q));
        if (!e) return NULL;
        *len = (size_t)(e - q);
        return q;
    }
    return NULL;
}

/* Finds the next Metadata block at or after `pos`. Returns 1 when found. */
static int xm_next(const char *buf, size_t len, size_t pos, xm_block *b) {
    while (pos < len) {
        const char *p = xm_find(buf + pos, len - pos, "<Metadata");
        if (!p) return 0;
        size_t at = (size_t)(p - buf);
        if (at + 9 >= len || !(isspace((unsigned char)p[9]) || p[9] == '>' || p[9] == '/')) { pos = at + 9; continue; }
        const char *t = p + 9; char quote = 0;
        for (; t < buf + len; t++) {
            if (quote) { if (*t == quote) quote = 0; }
            else if (*t == '"' || *t == '\'') quote = *t;
            else if (*t == '>') break;
        }
        if (t >= buf + len) return 0;
        memset(b, 0, sizeof(*b));
        b->start = at;
        if (t[-1] == '/') b->end = (size_t)(t - buf) + 1;
        else {
            const char *e = xm_find(t, (size_t)(buf + len - t), "</Metadata>");
            if (!e) return 0;
            b->end = (size_t)(e - buf) + 11;
        }
        b->title = xm_attr(p, t, "Title", &b->title_len);
        b->name = xm_attr(p, t, "Name", &b->name_len);
        b->ver = xm_attr(p, t, "AppVer", &b->ver_len);
        return 1;
    }
    return 0;
}

static int xm_same(const char *a, size_t al, const char *b, size_t bl) { return al == bl && (!al || !memcmp(a, b, al)); }

/* Title of the first entry, compared case-insensitively and without surrounding spaces. */
static int xm_title_is(const char *buf, size_t len, const char *title, size_t title_len) {
    xm_block b;
    if (!xm_next(buf, len, 0, &b) || !b.title) return 0;
    const char *t = b.title; size_t n = b.title_len;
    while (n && isspace((unsigned char)*t)) { t++; n--; }
    while (n && isspace((unsigned char)t[n - 1])) n--;
    while (title_len && isspace((unsigned char)*title)) { title++; title_len--; }
    while (title_len && isspace((unsigned char)title[title_len - 1])) title_len--;
    if (n != title_len || !n) return 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)t[i]) != tolower((unsigned char)title[i])) return 0;
    return 1;
}

/* Replaces [at, at + cut) of *buf with ins. */
static int xm_splice(char **buf, size_t *len, size_t at, size_t cut, const char *ins, size_t ins_len) {
    size_t n = *len - cut + ins_len;
    char *out = malloc(n + 1);
    if (!out) return -1;
    memcpy(out, *buf, at);
    memcpy(out + at, ins, ins_len);
    memcpy(out + at + ins_len, *buf + at + cut, *len - at - cut);
    out[n] = 0;
    free(*buf); *buf = out; *len = n;
    return 0;
}

/* Same patch lines: the <PatchList> contents compared without whitespace outside attribute values. */
static int xm_same_lines(const char *a, size_t al, const char *b, size_t bl) {
    const char *pa = xm_find(a, al, "<PatchList"), *pb = xm_find(b, bl, "<PatchList");
    const char *ea = pa ? xm_find(pa, (size_t)(a + al - pa), "</PatchList>") : NULL;
    const char *eb = pb ? xm_find(pb, (size_t)(b + bl - pb), "</PatchList>") : NULL;
    if (!ea || !eb) return 0;
    char q = 0;                     /* both sides advance together, so they share the quote state */
    for (;;) {
        while (pa < ea && !q && isspace((unsigned char)*pa)) pa++;
        while (pb < eb && !q && isspace((unsigned char)*pb)) pb++;
        if (pa == ea || pb == eb) return pa == ea && pb == eb;
        if (*pa != *pb) return 0;
        if (q) { if (*pa == q) q = 0; } else if (*pa == '"' || *pa == '\'') q = *pa;
        pa++; pb++;
    }
}

/* Called for an entry of the target renamed to one of ours: its old and new opening tags. */
typedef void (*xm_rename_fn)(void *ctx, const char *old_tag, size_t old_len, const char *new_tag, size_t new_len);

static size_t xm_tag_len(const char *b, size_t n) {
    char q = 0;
    for (size_t i = 0; i < n; i++) {
        if (q) { if (b[i] == q) q = 0; } else if (b[i] == '"' || b[i] == '\'') q = b[i]; else if (b[i] == '>') return i + 1;
    }
    return n;
}

/* Merges every entry of `ours` into a copy of `target`: an entry with the same Name and AppVer is replaced; an
   entry with the same AppVer and patch lines but another Name (or Note) is replaced too, so the repository names
   and descriptions win and nothing is duplicated (on_rename reports it); any other entry is added before the
   last "</Patch>". Returns 0 and the new file in *out (malloc'd), -1 on a malformed file or allocation failure. */
static int xm_merge_ex(const char *target, size_t target_len, const char *ours, size_t ours_len,
                       char **out, size_t *out_len, int *added, int *replaced, int *renamed,
                       xm_rename_fn on_rename, void *ctx) {
    *added = *replaced = *renamed = 0;
    char *buf = malloc(target_len + 1);
    if (!buf) return -1;
    memcpy(buf, target, target_len); buf[target_len] = 0;
    size_t len = target_len;
    xm_block ob;
    for (size_t pos = 0; xm_next(ours, ours_len, pos, &ob); pos = ob.end) {
        if (!ob.name) continue;
        const char *text = ours + ob.start; size_t text_len = ob.end - ob.start;
        xm_block tb; int found = 0;
        for (size_t tp = 0; xm_next(buf, len, tp, &tb); tp = tb.end) {
            if (!tb.name || !xm_same(tb.name, tb.name_len, ob.name, ob.name_len) ||
                !xm_same(tb.ver ? tb.ver : "", tb.ver_len, ob.ver ? ob.ver : "", ob.ver_len)) continue;
            found = 1;
            if (!xm_same(buf + tb.start, tb.end - tb.start, text, text_len)) {
                if (xm_splice(&buf, &len, tb.start, tb.end - tb.start, text, text_len)) { free(buf); return -1; }
                (*replaced)++;
            }
            break;
        }
        if (found) continue;
        for (size_t tp = 0; xm_next(buf, len, tp, &tb); tp = tb.end) {
            if (!tb.name || !xm_same(tb.ver ? tb.ver : "", tb.ver_len, ob.ver ? ob.ver : "", ob.ver_len) ||
                !xm_same_lines(buf + tb.start, tb.end - tb.start, text, text_len)) continue;
            /* an entry that already carries another of our names keeps it */
            int ours_name = 0; xm_block x;
            for (size_t xp = 0; !ours_name && xm_next(ours, ours_len, xp, &x); xp = x.end)
                ours_name = x.name && xm_same(x.name, x.name_len, tb.name, tb.name_len);
            if (ours_name) continue;
            if (on_rename) on_rename(ctx, buf + tb.start, xm_tag_len(buf + tb.start, tb.end - tb.start),
                                     text, xm_tag_len(text, text_len));
            if (xm_splice(&buf, &len, tb.start, tb.end - tb.start, text, text_len)) { free(buf); return -1; }
            (*renamed)++; found = 1;
            break;
        }
        if (found) continue;
        const char *close = NULL;
        for (const char *p = buf; (p = xm_find(p, (size_t)(buf + len - p), "</Patch>")); p += 8) close = p;
        if (!close) { free(buf); return -1; }
        size_t at = (size_t)(close - buf), ins_len = text_len + 6;
        char *ins = malloc(ins_len);
        if (!ins) { free(buf); return -1; }
        size_t k = 0;
        if (at && buf[at - 1] != '\n') ins[k++] = '\n';
        memcpy(ins + k, "  ", 2); k += 2;
        memcpy(ins + k, text, text_len); k += text_len;
        ins[k++] = '\n';
        int rc = xm_splice(&buf, &len, at, 0, ins, k);
        free(ins);
        if (rc) { free(buf); return -1; }
        (*added)++;
    }
    *out = buf; *out_len = len;
    return 0;
}

static inline int xm_merge(const char *target, size_t target_len, const char *ours, size_t ours_len,
                    char **out, size_t *out_len, int *added, int *replaced) {
    int renamed, rc = xm_merge_ex(target, target_len, ours, ours_len, out, out_len, added, replaced, &renamed, NULL, NULL);
    *replaced += renamed;
    return rc;
}
