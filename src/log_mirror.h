/* GPL-3.0-only. Picks the lines that matter for a report out of the kernel log (kern.msgbuf), which keeps only the
   last 128 KiB and is overwritten within seconds. The caller polls it and keeps the lines in NeoEnabler's own file.
   The previous snapshot is kept to find how far the ring moved, so every line is passed on once. */
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LM_MIN_OVERLAP 64

/* Launch answers of the hook and the crash report of a game. Lines NeoEnabler logged itself are already in its file. */
static const char *const lm_marks[] = {
    "[NEO ", "# signal:", "# reason:", "# fault address:", "# proc name:", "# thread name:", "notifyAppCrash", NULL
};

typedef struct { char *prev; size_t prev_n, done; } lm_state; /* snapshot of the last poll and how much of it was passed on */

static int lm_has(const char *line, size_t n, const char *needle) {
    size_t k = strlen(needle);
    if (n < k) return 0;
    for (size_t i = 0; i + k <= n; i++) if (!memcmp(line + i, needle, k)) return 1;
    return 0;
}

static int lm_wanted(const char *line, size_t n) {
    if (lm_has(line, n, "[NeoEnabler]")) return 0;
    for (int i = 0; lm_marks[i]; i++) if (lm_has(line, n, lm_marks[i])) return 1;
    return 0;
}

/* Calls emit for each wanted line (without the newline) added since the previous call. The ring drops its oldest
   bytes as it fills, so the previous snapshot is matched against the start of the new one: the smallest shift where
   the whole overlap is equal. A short tail is not enough, the system repeats identical lines for long stretches.
   Returns 1 when no overlap was found, so lines may have been lost between two polls. A last line without a newline
   waits for the next call. Returns -1 when out of memory. */
static int lm_scan(lm_state *st, const char *buf, size_t len, void (*emit)(void *, const char *, size_t), void *ctx) {
    size_t start = 0; int gap = 0;
    if (st->prev_n) {
        size_t k = 0; int found = 0;
        for (; k + LM_MIN_OVERLAP <= st->prev_n; k++) {
            size_t overlap = st->prev_n - k;
            if (overlap > len || st->prev[k] != buf[0]) continue;
            if (!memcmp(st->prev + k, buf, overlap)) { found = 1; break; }
        }
        if (found && st->done >= k) start = st->done - k; else gap = 1;
    }
    size_t end = start;
    for (size_t i = len; i > start; i--) if (buf[i - 1] == '\n') { end = i; break; }
    for (size_t i = start; i < end;) {
        const char *nl = memchr(buf + i, '\n', end - i);
        size_t n = (size_t)(nl - (buf + i));
        if (lm_wanted(buf + i, n)) emit(ctx, buf + i, n);
        i += n + 1;
    }
    char *copy = realloc(st->prev, len ? len : 1);
    if (!copy) return -1;
    memcpy(copy, buf, len);
    st->prev = copy; st->prev_n = len; st->done = end > start ? end : start;
    return gap;
}

/* Lines the system writes by the thousand and that say nothing about NeoEnabler. Only hidden when the kernel log is
   shown in a report: they still fill the ring, which NeoEnabler cannot change. */
static const char *const lm_noise[] = {
    "[SceLncUtil] blockAppSuspend:", "[SceLncUtil] unblockAppSuspend:", NULL
};

static int lm_is_noise(const char *line, size_t n) {
    for (int i = 0; lm_noise[i]; i++) if (lm_has(line, n, lm_noise[i])) return 1;
    return 0;
}

/* Passes the kernel log to emit (each chunk ends with a newline) without the noise lines, and with a run of identical
   lines shortened to one line and a count. Returns the number of lines left out. */
static size_t lm_squeeze(const char *buf, size_t len, void (*emit)(void *, const char *, size_t), void *ctx) {
    size_t hidden = 0, same = 0; const char *last = NULL; size_t last_n = 0;
    for (size_t i = 0; i < len;) {
        const char *nl = memchr(buf + i, '\n', len - i);
        size_t n = nl ? (size_t)(nl - (buf + i)) : len - i;
        const char *line = buf + i;
        i += n + (nl ? 1 : 0);
        if (lm_is_noise(line, n)) { hidden++; continue; }
        if (last && n == last_n && !memcmp(line, last, n)) { same++; hidden++; continue; }
        if (same) {
            char note[64]; int k = snprintf(note, sizeof note, "(line above repeated %zu more times)\n", same);
            emit(ctx, note, (size_t)k); same = 0;
        }
        emit(ctx, line, n); emit(ctx, "\n", 1);
        last = line; last_n = n;
    }
    if (same) {
        char note[64]; int k = snprintf(note, sizeof note, "(line above repeated %zu more times)\n", same);
        emit(ctx, note, (size_t)k);
    }
    return hidden;
}
