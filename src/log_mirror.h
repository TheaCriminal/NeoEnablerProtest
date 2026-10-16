/* GPL-3.0-only. Picks the lines that matter for a report out of the kernel log (kern.msgbuf), which keeps only the
   last 128 KiB and is overwritten within seconds. The caller polls it and keeps the lines in NeoEnabler's own file.
   The previous snapshot is kept to find how far the ring moved, so every line is passed on once. */
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
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

/* Calls emit for each line (without the newline) added since the previous call: every line when `all`, else only the
   wanted ones. The ring drops its oldest
   bytes as it fills, so the previous snapshot is matched against the start of the new one: the smallest shift where
   the whole overlap is equal. A short tail is not enough, the system repeats identical lines for long stretches.
   Returns 1 when no overlap was found, so lines may have been lost between two polls. A last line without a newline
   waits for the next call. Returns -1 when out of memory. */
static int lm_scan(lm_state *st, const char *buf, size_t len, int all, void (*emit)(void *, const char *, size_t), void *ctx) {
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
        if (all || lm_wanted(buf + i, n)) emit(ctx, buf + i, n);
        i += n + 1;
    }
    char *copy = realloc(st->prev, len ? len : 1);
    if (!copy) return -1;
    memcpy(copy, buf, len);
    st->prev = copy; st->prev_n = len; st->done = end > start ? end : start;
    return gap;
}

/* Removes what identifies a person or a console from a kernel log line: IPv4 and MAC addresses, e-mail addresses,
   values after account, user, device or serial keys, and the long hex ids of the controller lines. Replacements are
   "<ip>", "<mac>", "<email>" and "<id>". Writes at most cap bytes to out and returns the length. */
static const char *const lm_id_keys[] = {
    "UserId", "userId", "user_id", "AccountId", "accountId", "account_id", "OnlineId", "onlineId", "NpId", "npId",
    "DeviceId", "deviceId", "SerialNumber", "serialNumber", "Serial", "MacAddr", "macAddr", "SpecificUser, param=",
    "/user/home/", NULL
};

static int lm_octets(const char *p, size_t left) {
    size_t i = 0;
    for (int part = 0; part < 4; part++) {
        size_t d = 0, v = 0;
        while (i < left && d < 3 && isdigit((unsigned char)p[i])) { v = v * 10 + (size_t)(p[i] - '0'); i++; d++; }
        if (!d || v > 255) return 0;
        if (part < 3) { if (i >= left || p[i] != '.') return 0; i++; }
    }
    if (i < left && (isdigit((unsigned char)p[i]) || (p[i] == '.' && i + 1 < left && isdigit((unsigned char)p[i + 1])))) return 0;
    return (int)i;
}

static int lm_mac(const char *p, size_t left) {
    if (left < 17) return 0;
    for (int g = 0; g < 6; g++) {
        if (!isxdigit((unsigned char)p[g * 3]) || !isxdigit((unsigned char)p[g * 3 + 1])) return 0;
        if (g < 5 && p[g * 3 + 2] != ':' && p[g * 3 + 2] != '-') return 0;
    }
    return left > 17 && isxdigit((unsigned char)p[17]) ? 0 : 17;
}

static size_t lm_redact(const char *in, size_t n, char *out, size_t cap) {
    size_t o = 0; int pss = lm_has(in, n, "[Pss]");
#define LM_PUT(str) do { const char *t_ = (str); for (size_t q_ = 0; t_[q_] && o < cap; q_++) out[o++] = t_[q_]; } while (0)
    for (size_t i = 0; i < n && o < cap;) {
        int prev_word = i && (isalnum((unsigned char)in[i - 1]) || in[i - 1] == '.' || in[i - 1] == ':');
        int m;
        if (!prev_word && isdigit((unsigned char)in[i]) && (m = lm_octets(in + i, n - i))) { LM_PUT("<ip>"); i += (size_t)m; continue; }
        if (!prev_word && isxdigit((unsigned char)in[i]) && (m = lm_mac(in + i, n - i))) { LM_PUT("<mac>"); i += (size_t)m; continue; }
        if (in[i] == '@' && o && i + 1 < n && isalnum((unsigned char)in[i + 1])) {
            size_t b = o;
            while (b && (isalnum((unsigned char)out[b - 1]) || strchr("._%+-", out[b - 1]))) b--;
            size_t e = i + 1;
            while (e < n && (isalnum((unsigned char)in[e]) || in[e] == '.' || in[e] == '-')) e++;
            if (b < o && memchr(in + i, '.', e - i)) { o = b; LM_PUT("<email>"); i = e; continue; }
        }
        if (pss && in[i] == '0' && i + 1 < n && in[i + 1] == 'x') {
            size_t e = i + 2;
            while (e < n && isxdigit((unsigned char)in[e])) e++;
            if (e - i - 2 >= 8) { LM_PUT("<id>"); i = e; continue; }
        }
        if ((!prev_word && isalpha((unsigned char)in[i])) || in[i] == '/') {
            int hit = 0;
            for (int k = 0; lm_id_keys[k] && !hit; k++) {
                size_t kl = strlen(lm_id_keys[k]);
                if (i + kl >= n || memcmp(in + i, lm_id_keys[k], kl)) continue;
                size_t j = i + kl;
                while (j < n && j < i + kl + 3 && strchr("=:{ \"'", in[j])) j++;
                size_t e = j;
                while (e < n && (isalnum((unsigned char)in[e]) || in[e] == '_' || in[e] == '.' || in[e] == '-')) e++;
                if (e == j || (j == i + kl && !strchr("=:/", in[i + kl - 1]))) continue;
                for (size_t q = i; q < j && o < cap; q++) out[o++] = in[q];
                LM_PUT("<id>"); i = e; hit = 1;
            }
            if (hit) continue;
        }
        out[o++] = in[i++];
    }
#undef LM_PUT
    return o;
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
