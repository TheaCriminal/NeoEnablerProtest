/* Host-only: patches that need 8 GiB are turned off while 8 GiB is not available, other patches stay on, and the
   game filter works. Uses the Bloodborne XML of the repository in a fake ps-patch-system folder. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define ROOT "/tmp/neo-games-8gib-test"
#define PS_DIR ROOT "/xml_orbis"
#define PS_BACKUP_ROOT ROOT "/backup"
#define APPMETA_DIR ROOT "/appmeta"
#define PS_NO_HTTP 1

static int logs;
static void log_line(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); logs++;
}
static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}
static char *ps_get(const char *url, size_t *len) { (void)url; (void)len; assert(0); return NULL; }

#include "../src/patch_sync.h"
#include "../src/games.h"

static void put(const char *path, const void *data, size_t n) { assert(!write_file(path, data, n)); }

/* A game in the fake console: its param.sfo and the repository patch file as ps-patch-system would have it. */
static void install_game(const char *tid, const char *title, const char *app_ver, const char *xml_dir, const char *xml_name) {
    char cmd[512], path[160];
    snprintf(cmd, sizeof cmd, "mkdir -p " APPMETA_DIR "/%s && cp %s/%s " PS_DIR "/%s.xml", tid, xml_dir, xml_name, tid);
    assert(!system(cmd));
    unsigned char sfo[256] = {0};
    memcpy(sfo, "\0PSF", 4);
    uint32_t keys = 20 + 3 * 16, data = keys + 32, count = 3;
    memcpy(sfo + 8, &keys, 4); memcpy(sfo + 12, &data, 4); memcpy(sfo + 16, &count, 4);
    const char *k[3] = {"APP_VER", "TITLE", "VERSION"}, *v[3] = {app_ver, title, "01.00"};
    uint16_t ko = 0; uint32_t dof = 0;
    for (int i = 0; i < 3; i++) {
        unsigned char *e = sfo + 20 + i * 16; uint32_t l = (uint32_t)strlen(v[i]) + 1;
        memcpy(e, &ko, 2); memcpy(e + 4, &l, 4); memcpy(e + 12, &dof, 4);
        memcpy(sfo + keys + ko, k[i], strlen(k[i]) + 1); memcpy(sfo + data + dof, v[i], l);
        ko += (uint16_t)(strlen(k[i]) + 1); dof += 32;
    }
    snprintf(path, sizeof path, APPMETA_DIR "/%s/param.sfo", tid);
    put(path, sfo, sizeof sfo);
}

static const gp_patch *pick(gp_patch *p, int n, int needs_8gib) {
    for (int i = 0; i < n; i++) if (!!p[i].needs_8gib == needs_8gib) return &p[i];
    return NULL;
}

int main(int argc, char **argv) {
    (void)game_needs; (void)patch_sync;   /* helpers of the includes that this test does not use */
    assert(argc == 2);
    assert(!system("rm -rf " ROOT " && mkdir -p " PS_DIR));
    install_game("CUSA00900", "Bloodborne", "01.09", argv[1], "Bloodborne_CUSA00900.xml");
    install_game("CUSA00190", "KILLZONE SHADOW FALL", "01.81", argv[1], "KillzoneShadowFall_CUSA00190.xml");

    int nb, nk; char name[128], ver[16];
    assert(!game_info("CUSA00900", name, sizeof name, ver, sizeof ver));
    gp_patch *bb = game_patches("CUSA00900", ver, &nb);
    assert(!game_info("CUSA00190", name, sizeof name, ver, sizeof ver));
    gp_patch *kz = game_patches("CUSA00190", ver, &nk);
    const gp_patch *big = pick(bb, nb, 1), *small = pick(kz, nk, 0);
    assert(big && small);
    char msg[256];
    assert(!game_set_patch("CUSA00900", big->hash, 1, 1, msg, sizeof msg));
    assert(!game_set_patch("CUSA00190", small->hash, 1, 1, msg, sizeof msg));
    assert(game_set_patch("CUSA00900", big->hash, 1, 0, msg, sizeof msg) == -1);   /* refused when 8 GiB is unavailable */

    size_t sl; char *s = settings_read(&sl);
    assert(gp_setting(s, sl, big->hash) == 1 && gp_setting(s, sl, small->hash) == 1); free(s);

    assert(games_drop_8gib("CUSA00190", "test") == 0);                              /* another game: nothing */
    s = settings_read(&sl); assert(gp_setting(s, sl, big->hash) == 1); free(s);

    assert(games_drop_8gib(NULL, "test") == 1);                                     /* every game */
    s = settings_read(&sl);
    assert(gp_setting(s, sl, big->hash) == 0 && gp_setting(s, sl, small->hash) == 1); free(s);
    assert(games_drop_8gib(NULL, "test") == 0);                                     /* nothing left to turn off */

    assert(!game_set_patch("CUSA00900", big->hash, 1, 1, msg, sizeof msg));
    assert(games_drop_8gib("CUSA00900", "test") == 1);                              /* one game */
    s = settings_read(&sl);
    assert(gp_setting(s, sl, big->hash) == 0 && gp_setting(s, sl, small->hash) == 1); free(s);
    free(bb); free(kz);
    puts("PASS 8 GiB patches turned off while 8 GiB is unavailable, other patches kept");
    return 0;
}
