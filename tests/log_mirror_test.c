/* Host test of src/log_mirror.h: every wanted line passes once, the manager's own lines are skipped, a ring that
   turned over between two polls is reported as a gap, a last line without a newline waits, and identical repeated
   lines at the end of the ring do not make a poll skip the lines before them. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/log_mirror.h"

static char out[16384]; static int count;
static void emit(void *ctx, const char *line, size_t n) { (void)ctx; count++; strncat(out, line, n); strcat(out, "|"); }
static int poll(lm_state *st, const char *ring) { out[0] = 0; count = 0; return lm_scan(st, ring, strlen(ring), emit, NULL); }

static char acc[1024];
static void add(void *c, const char *p, size_t n) { (void)c; strncat(acc, p, n); }

#define SPAM "[SceLncUtil] blockAppSuspend: name={SceNpUds}, appId={0x00000006}, refCount={1}\n[SceLncUtil] unblockAppSuspend: name={SceNpUds}, appId={0x00000006}, refCount={0}\n"

int main(void) {
    lm_state st = {0};
    const char *r1 = "noise a\n[NEO CUSA00900] 0>1 a1\n[NeoEnabler] own line\nnoise b\n";
    char ring[8192];
    snprintf(ring, sizeof ring, "%s%s", SPAM SPAM SPAM, r1);
    assert(!poll(&st, ring));
    assert(count == 1 && !strcmp(out, "[NEO CUSA00900] 0>1 a1|"));

    /* same ring again: nothing new */
    assert(!poll(&st, ring) && count == 0);

    /* new lines appended after the remembered end */
    strcat(ring, "# signal: 11 (SIGSEGV)\n# fault address: 0000000000000044\nnoise c\n");
    assert(!poll(&st, ring));
    assert(count == 2 && !strcmp(out, "# signal: 11 (SIGSEGV)|# fault address: 0000000000000044|"));

    /* incomplete last line waits for its newline */
    strcat(ring, "[NEO CUSA00190] 0>1");
    assert(!poll(&st, ring) && count == 0);
    strcat(ring, " a1\nx\n");
    assert(!poll(&st, ring));
    assert(count == 1 && !strcmp(out, "[NEO CUSA00190] 0>1 a1|"));

    /* identical lines repeat before and after a wanted line: the line must not be skipped */
    snprintf(ring, sizeof ring, "%s%s%s", "head\n", SPAM SPAM SPAM SPAM, "tail\n");
    lm_state sp = {0};
    assert(!poll(&sp, ring));
    char ring2[8192];
    snprintf(ring2, sizeof ring2, "%s[NEO CUSA00785] 0>1 a1\n%s%s", ring, SPAM SPAM SPAM SPAM, SPAM);
    assert(!poll(&sp, ring2));
    assert(count == 1 && !strcmp(out, "[NEO CUSA00785] 0>1 a1|"));

    /* ring turned over completely between two polls: gap, new content still read */
    const char *r5 = "completely different content that shares nothing with the earlier text of the ring\n"
                     "[NEO CUSA00785] 0>1 a2\n";
    assert(poll(&sp, r5) == 1);
    assert(count == 1 && !strcmp(out, "[NEO CUSA00785] 0>1 a2|"));

    /* full ring sliding: the oldest bytes drop, new lines come at the end */
    size_t big = 131072; char *a = malloc(big + 1), *b = malloc(big + 1);
    for (size_t i = 0; i < big; i++) a[i] = (i % 80 == 79) ? '\n' : (char)('a' + (i * 7 + i / 80) % 26);
    a[big] = 0;
    lm_state fl = {0};
    assert(!lm_scan(&fl, a, big, emit, NULL));
    for (int round = 0; round < 3; round++) {
        size_t cut = 80 * 100;
        memcpy(b, a + cut, big - cut);
        const char *add = round == 1 ? "[NEO CUSA01493] 0>1 a1\n" : "plain line\n";
        size_t al = strlen(add);
        memcpy(b + big - cut, add, al);
        for (size_t i = big - cut + al; i < big; i++) b[i] = (i % 80 == 79) ? '\n' : (char)('a' + (i * 3) % 26);
        b[big] = 0;
        out[0] = 0; count = 0;
        assert(!lm_scan(&fl, b, big, emit, NULL));
        assert(count == (round == 1 ? 1 : 0));
        char *t = a; a = b; b = t;
    }
    free(a); free(b); free(fl.prev); free(st.prev); free(sp.prev);
    /* squeeze: noise lines and runs of identical lines are shortened, other lines are kept in order */
    {
        const char *k = "a\n" SPAM "b\nb\nb\nc\nd\nd";
        acc[0] = 0;
        size_t hidden = lm_squeeze(k, strlen(k), add, NULL);
        assert(!strcmp(acc, "a\nb\n(line above repeated 2 more times)\nc\nd\n(line above repeated 1 more times)\n"));
        assert(hidden == 2 + 2 + 1);
    }
    puts("log_mirror ok");
    return 0;
}
