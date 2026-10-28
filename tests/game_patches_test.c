/* Host-only: game versions, entry hashes and requirements from the repository XMLs, and settings edits. */
#include <assert.h>
#include <stdio.h>
#include "../src/game_patches.h"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f); buf[n] = 0; *len = (size_t)n;
    return buf;
}

static const gp_patch *find(const gp_patch *p, int n, const char *name) {
    for (int i = 0; i < n; i++) if (!strcmp(p[i].name, name)) return &p[i];
    return NULL;
}

int main(void) {
    (void)xm_merge; (void)xm_title_is;   /* xml_merge.h helpers this test does not use */
    /* param.sfo: header, one index entry per key, key table, data table */
    unsigned char sfo[256] = {0};
    memcpy(sfo, "\0PSF", 4);
    uint32_t keys = 20 + 3 * 16, data = keys + 32, count = 3;
    memcpy(sfo + 8, &keys, 4); memcpy(sfo + 12, &data, 4); memcpy(sfo + 16, &count, 4);
    const char *k[3] = {"APP_VER", "TITLE", "VERSION"}, *v[3] = {"01.02", "The Order: 1886", "01.00"};
    uint16_t ko = 0; uint32_t dof = 0;
    for (int i = 0; i < 3; i++) {
        unsigned char *e = sfo + 20 + i * 16; uint32_t l = (uint32_t)strlen(v[i]) + 1;
        memcpy(e, &ko, 2); memcpy(e + 4, &l, 4); memcpy(e + 12, &dof, 4);
        memcpy(sfo + keys + ko, k[i], strlen(k[i]) + 1); memcpy(sfo + data + dof, v[i], l);
        ko += (uint16_t)(strlen(k[i]) + 1); dof += 32;
    }
    char app[16], ver[16], title[64];
    assert(!gp_sfo(sfo, sizeof sfo, "APP_VER", app, sizeof app) && !strcmp(app, "01.02"));
    assert(!gp_sfo(sfo, sizeof sfo, "VERSION", ver, sizeof ver) && !strcmp(ver, "01.00"));
    assert(!gp_sfo(sfo, sizeof sfo, "TITLE", title, sizeof title) && !strcmp(title, "The Order: 1886"));
    assert(gp_sfo(sfo, sizeof sfo, "APP", title, sizeof title) && !title[0]);   /* whole key only */
    assert(!strcmp(gp_game_version(app, ver), "01.02"));
    assert(!strcmp(gp_game_version("01.00", "01.05"), "01.05"));
    assert(!strcmp(gp_game_version("1.00", "x"), "1.00"));

    char d[64];
    const char *enc = "A &amp; B &lt;&gt; &quot;x&apos; &#233;&#x41; &bogus;";
    gp_decode(enc, strlen(enc), d, sizeof d);
    assert(!strcmp(d, "A & B <> \"x' \xc3\xa9" "A &bogus;"));

    /* Hashes as written by the patch manager for the console file names (6378fc73, d753e8d4, 8cdf7724 and fd9434a8 checked
       against !_settings.xml on a console). */
    size_t len; char *xml = slurp("xml/TheOrder1886_CUSA00785.xml", &len);
    gp_patch p[16]; int n = gp_list(xml, len, "CUSA00785.xml", "01.02", p, 16);
    assert(n == 2);
    const gp_patch *q = find(p, n, "Native 4K (PS4 Pro mode, 60fps)");
    assert(q && !strcmp(q->hash, "d753e8d4") && q->needs_pro && !q->needs_8gib && strstr(q->note, "without tearing"));
    assert((q = find(p, n, "Native 4K (PS4 Pro mode)")) && !strcmp(q->hash, "6378fc73"));
    assert(gp_list(xml, len, "CUSA00785.xml", "01.00", p, 16) == 0);           /* other version: nothing applies */
    free(xml);
    xml = slurp("xml/Driveclub_CUSA00093.xml", &len);
    n = gp_list(xml, len, "CUSA00093.xml", "01.28", p, 16);
    assert((q = find(p, n, "1440p (PS4 Pro mode)")) && !strcmp(q->hash, "8cdf7724") && q->needs_pro && !q->needs_8gib);
    assert((q = find(p, n, "4K, 1800p render (PS4 Pro mode)")) && !strcmp(q->hash, "37ba5d63") && q->needs_pro && !q->needs_8gib && strstr(q->note, "visual artifacts"));
    assert(!find(p, n, "Native 4K (PS4 Pro mode, requires 8 GiB enabled)"));
    free(xml);
    xml = slurp("xml/JustCause3_CUSA01493.xml", &len);
    n = gp_list(xml, len, "CUSA01493.xml", "01.05", p, 16);
    assert((q = find(p, n, "Offline mode (skips online login)")) && !q->needs_pro && !strcmp(q->hash, "fd9434a8"));
    free(xml);

    /* AppVer lists, "mask", and requirements recognised by name when NeoRequires is missing. */
    const char *other = "<Patch>\n<Metadata Title=\"G\" Name=\"A (PS4 Pro mode, requires 8 GiB enabled)\" AppVer=\" 01.00 , 01.01\" AppElf=\"eboot.bin\"><PatchList/></Metadata>\n"
                        "<Metadata Title=\"G\" Name=\"B\" Note=\"x &amp; y\" AppVer=\"mask\"><PatchList/></Metadata>\n"
                        "<Metadata Title=\"G\" Name=\"C\" AppVer=\"01.02\"><PatchList/></Metadata>\n</Patch>\n";
    n = gp_list(other, strlen(other), "CUSA99999.xml", "01.01", p, 16);
    assert(n == 2 && p[0].needs_pro && p[0].needs_8gib && !p[1].needs_pro && !strcmp(p[1].note, "x & y"));
    assert(p[0].resolution && !p[1].resolution);
    char h[9]; snprintf(h, sizeof h, "%08x", gp_sid("xml_orbis") ^ gp_sid("CUSA99999.xml") ^ gp_sid("A (PS4 Pro mode, requires 8 GiB enabled)") ^
                                             gp_sid("G") ^ gp_sid("eboot.bin") ^ gp_sid("01.01"));
    assert(!strcmp(p[0].hash, h));
    snprintf(h, sizeof h, "%08x", gp_sid("xml_orbis") ^ gp_sid("CUSA99999.xml") ^ gp_sid("B") ^ gp_sid("G") ^ gp_sid("") ^ gp_sid("mask"));
    assert(!strcmp(p[1].hash, h));

    /* Settings: read, flip, add, keep everything else, create when missing. */
    const char *s = "<?xml version=\"1.0\"?>\n<Settings>\n  <apply_120hz_videoout>true</apply_120hz_videoout>\n"
                    "  <Patch hash=\"d753e8d4\" enabled=\"true\" />\n  <Patch hash=\"6378fc73\" enabled=\"false\" />\n"
                    "  <Patch hash=\"aaaaaaaa\" />\n</Settings>\n";
    assert(gp_setting(s, strlen(s), "d753e8d4") == 1 && gp_setting(s, strlen(s), "6378fc73") == 0);
    assert(gp_setting(s, strlen(s), "aaaaaaaa") == 0 && gp_setting(s, strlen(s), "bbbbbbbb") == 0);
    char *o; size_t ol;
    assert(!gp_setting_set(s, strlen(s), "d753e8d4", 0, &o, &ol) && strlen(o) == ol);
    assert(gp_setting(o, ol, "d753e8d4") == 0 && strstr(o, "<apply_120hz_videoout>true") && ol == strlen(s) + 1);
    char *o2; size_t ol2;
    assert(!gp_setting_set(o, ol, "6378fc73", 1, &o2, &ol2) && gp_setting(o2, ol2, "6378fc73") == 1 && gp_setting(o2, ol2, "d753e8d4") == 0);
    free(o); o = o2; ol = ol2;
    assert(!gp_setting_set(o, ol, "aaaaaaaa", 1, &o2, &ol2) && gp_setting(o2, ol2, "aaaaaaaa") == 1);
    free(o); o = o2; ol = ol2;
    assert(!gp_setting_set(o, ol, "cccccccc", 1, &o2, &ol2) && gp_setting(o2, ol2, "cccccccc") == 1);
    assert(strstr(o2, "  <Patch hash=\"cccccccc\" enabled=\"true\" />\n</Settings>"));
    free(o); o = o2; ol = ol2;
    assert(!gp_setting_set(o, ol, "cccccccc", 0, &o2, &ol2) && gp_setting(o2, ol2, "cccccccc") == 0);
    free(o);
    assert(!gp_setting_set(o2, ol2, "cccccccc", 0, &o, &ol) && ol == ol2 && !memcmp(o, o2, ol));   /* idempotent */
    free(o); free(o2);
    assert(!gp_setting_set("", 0, "dddddddd", 1, &o, &ol) && gp_setting(o, ol, "dddddddd") == 1 && strstr(o, "<Settings>"));
    free(o);
    assert(gp_setting_set("<Settings>", 10, "dddddddd", 1, &o, &ol) == -1);                     /* truncated file */

    /* 120 Hz: NeoRequires="120hz" (alone or combined), else a "120 FPS" or "120 Hz" in the name. */
    assert(gp_needs_120hz("120hz", "Anything") && gp_needs_120hz("pro,120hz", "Anything") && !gp_needs_120hz("pro", "120 FPS"));
    assert(gp_needs_120hz("", "120 FPS") && gp_needs_120hz("", "Unlock FPS 120fps") && gp_needs_120hz("", "120Hz mode") && gp_needs_120hz("", "60/120 FPS"));
    assert(!gp_needs_120hz("", "60 FPS") && !gp_needs_120hz("", "1200 MB pool") && !gp_needs_120hz("", "Native 4K (PS4 Pro mode)"));
    {
        const char *x = "<Patch>\n<Metadata Title=\"T\" Name=\"120 FPS\" AppVer=\"01.00\" NeoRequires=\"pro,120hz\"><PatchList/></Metadata>\n"
                        "<Metadata Title=\"T\" Name=\"60 FPS\" AppVer=\"01.00\"><PatchList/></Metadata>\n</Patch>\n";
        gp_patch pl[4]; int k = gp_list(x, strlen(x), "CUSA00001.xml", "01.00", pl, 4);
        assert(k == 2 && pl[0].needs_120hz && pl[0].needs_pro && !pl[1].needs_120hz && !pl[1].needs_pro);
    }
    assert(gp_is_fps("60 FPS") && gp_is_fps("120fps") && gp_is_fps("60 FPS Unlock") && gp_is_fps("Unlock FPS 120fps") && gp_is_fps("60/120 FPS"));
    assert(!gp_is_fps("60 FPS Menu toggle") && !gp_is_fps("Skip intro") && !gp_is_fps("1620p") && !gp_is_fps("Native 4K"));
    puts("PASS game patches: versions, entry hashes, requirements, resolution entries, settings read and edit");
    return 0;
}
