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
static int poll(lm_state *st, const char *ring) { out[0] = 0; count = 0; return lm_scan(st, ring, strlen(ring), 0, emit, NULL); }

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
    assert(!lm_scan(&fl, a, big, 0, emit, NULL));
    for (int round = 0; round < 3; round++) {
        size_t cut = 80 * 100;
        memcpy(b, a + cut, big - cut);
        const char *add = round == 1 ? "[NEO CUSA01493] 0>1 a1\n" : "plain line\n";
        size_t al = strlen(add);
        memcpy(b + big - cut, add, al);
        for (size_t i = big - cut + al; i < big; i++) b[i] = (i % 80 == 79) ? '\n' : (char)('a' + (i * 3) % 26);
        b[big] = 0;
        out[0] = 0; count = 0;
        assert(!lm_scan(&fl, b, big, 0, emit, NULL));
        assert(count == (round == 1 ? 1 : 0));
        char *t = a; a = b; b = t;
    }
    free(a); free(b); free(fl.prev); free(st.prev); free(sp.prev);
    /* all mode: every new line passes, the noise filter is the caller's */
    {
        lm_state al = {0};
        const char *r = "a\n[NeoEnabler] x\nb\n";
        out[0] = 0; count = 0;
        assert(!lm_scan(&al, r, strlen(r), 1, emit, NULL));
        assert(count == 3 && !strcmp(out, "a|[NeoEnabler] x|b|"));
        assert(lm_is_noise(SPAM, 80) && !lm_is_noise("plain line", 10));
        free(al.prev);
    }
    /* redact: personal and console identifiers go, version numbers and counters stay */
    {
        struct { const char *in, *out; } c[] = {
            {"connect 192.168.0.4 port 8765", "connect <ip> port 8765"},
            {"firmware 13.20 refCount={0} pid:0xa3 v1.2.3", "firmware 13.20 refCount={0} pid:0xa3 v1.2.3"},
            {"addr 300.1.1.1 and 1.2.3.4.5", "addr 300.1.1.1 and 1.2.3.4.5"},
            {"mac 00:1a:2b:3c:4d:5e up", "mac <mac> up"},
            {"mail jane.doe+x@example.com sent", "mail <email> sent"},
            {"UserId=1ea2f4da name", "UserId=<id> name"},
            {"accountId: 123456789 ok", "accountId: <id> ok"},
            {"[SceCdlgApp][Pss]   [o][0x0000000000070300][0x1ea2f4da][DUALSENSE ]", "[SceCdlgApp][Pss]   [o][<id>][<id>][DUALSENSE ]"},
            {"[Pss] Open Pad [0x0000000000070300, 0, 0]: ret=0x03090700", "[Pss] Open Pad [<id>, 0, 0]: ret=<id>"},
            {"SetDeviceIndexBehavior: mode=SpecificUser, param=0x1ea2f4da", "SetDeviceIndexBehavior: mode=SpecificUser, param=<id>"},
            {"SetDeviceIndexBehavior: mode=MergeAllConnectedDevice, param=null", "SetDeviceIndexBehavior: mode=MergeAllConnectedDevice, param=null"},
            {"src \"/user/home/1ea2f4da/webkit/shell\" -> dst \"/sce_sbx/006/user/home/1ea2f4da/x\"", "src \"/user/home/<id>/webkit/shell\" -> dst \"/sce_sbx/006/user/home/<id>/x\""},
            {"deviceId : 0xffffffffffffffff / userId : 0xffffffff /", "deviceId : <id> / userId : <id> /"},
            {"plain 0x0000000000070300 outside", "plain 0x0000000000070300 outside"},
        };
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
            char r[256]; size_t rn = lm_redact(c[i].in, strlen(c[i].in), r, sizeof r - 1); r[rn] = 0;
            if (strcmp(r, c[i].out)) { printf("redact %zu: got [%s] want [%s]\n", i, r, c[i].out); return 1; }
        }
    }
    puts("log_mirror ok");
    return 0;
}
