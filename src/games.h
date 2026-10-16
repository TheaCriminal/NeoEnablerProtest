/* GPL-3.0-only. Installed PS4 games with NeoEnabler patches, what a launch needs (Pro mode, 8 GiB) from the patches
   enabled in the ps-patch-system settings, and changes to those settings. Needs patch_sync.h (ps_read, ps_write,
   PS_DIR) and log_line(). */
#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include "game_patches.h"

#define APPMETA_DIR "/system_data/priv/appmeta"
#define SETTINGS_XML PS_DIR "/!_settings.xml"
#define GAME_MAX_PATCHES 128

typedef struct {
    char tid[10], name[128], version[16];
    int update;                 /* /user/patch/<title ID> exists */
    int count;
    gp_patch *patches;
} game_t;

/* Title IDs whose patches live in xml_orbis (ps-patch-system folder_for_title_id). */
static int game_tid(const char *s) {
    return strlen(s) == 9 && (!memcmp(s, "CUSA", 4) || !memcmp(s, "CUSB", 4)) &&
           strspn(s + 4, "0123456789") == 5;
}

/* Name and version (the one ps-patch-system matches) of an installed game; 0 when its param.sfo is readable. */
static int game_info(const char *tid, char *name, size_t name_cap, char *ver, size_t ver_cap) {
    char path[96]; size_t n;
    snprintf(path, sizeof path, APPMETA_DIR "/%.9s/param.sfo", tid);
    char *sfo = ps_read(path, 64 << 10, &n);
    if (!sfo) return -1;
    char app[16], version[16];
    gp_sfo((unsigned char *)sfo, n, "TITLE", name, name_cap);
    gp_sfo((unsigned char *)sfo, n, "APP_VER", app, sizeof app);
    gp_sfo((unsigned char *)sfo, n, "VERSION", version, sizeof version);
    free(sfo);
    snprintf(ver, ver_cap, "%s", gp_game_version(app, version));
    return ver[0] ? 0 : -1;
}

/* Entries of the game's patch file that apply to its installed version; malloc'd, count in *n. */
static gp_patch *game_patches(const char *tid, const char *ver, int *n) {
    char path[96], file[16]; size_t len;
    *n = 0;
    snprintf(file, sizeof file, "%.9s.xml", tid);
    snprintf(path, sizeof path, PS_DIR "/%s", file);
    char *xml = ps_read(path, PS_MAX_FILE, &len);
    if (!xml) return NULL;
    gp_patch *p = malloc(sizeof(gp_patch) * GAME_MAX_PATCHES);
    if (p) *n = gp_list(xml, len, file, ver, p, GAME_MAX_PATCHES);
    free(xml);
    return p;
}

static void games_free(game_t *g, int n) {
    for (int i = 0; i < n; i++) free(g[i].patches);
    free(g);
}

static int game_cmp(const void *a, const void *b) {
    const game_t *x = a, *y = b;
    int c = strcasecmp(x->name, y->name);
    return c ? c : strcmp(x->tid, y->tid);
}

/* Installed games that have at least one patch needing Pro mode for their version, sorted by name. */
static game_t *games_scan(int *count) {
    *count = 0;
    DIR *d = opendir(APPMETA_DIR);
    if (!d) return NULL;
    int cap = 32, n = 0; struct dirent *e;
    game_t *g = malloc(sizeof(game_t) * (size_t)cap);
    while (g && (e = readdir(d))) {
        if (!game_tid(e->d_name)) continue;
        game_t x = {0};
        snprintf(x.tid, sizeof x.tid, "%.9s", e->d_name);
        char path[96]; struct stat st;
        snprintf(path, sizeof path, PS_DIR "/%s.xml", x.tid);
        if (stat(path, &st) || game_info(x.tid, x.name, sizeof x.name, x.version, sizeof x.version)) continue;
        x.patches = game_patches(x.tid, x.version, &x.count);
        int pro = 0;
        for (int i = 0; i < x.count; i++) pro |= x.patches[i].needs_pro;
        if (!pro) { free(x.patches); continue; }
        snprintf(path, sizeof path, "/user/patch/%s", x.tid);
        x.update = !stat(path, &st) && S_ISDIR(st.st_mode);
        if (n == cap) {
            game_t *bigger = realloc(g, sizeof(game_t) * (size_t)(cap *= 2));
            if (!bigger) { free(x.patches); break; }
            g = bigger;
        }
        g[n++] = x;
    }
    closedir(d);
    if (g) qsort(g, (size_t)n, sizeof(game_t), game_cmp);
    *count = n;
    return g;
}

static char *settings_read(size_t *len) {
    char *s = ps_read(SETTINGS_XML, PS_MAX_FILE, len);
    if (!s) { s = calloc(1, 1); *len = 0; }
    return s;
}

/* What a launch of this title needs, from its enabled patches: bit 0 Pro mode, bit 1 8 GiB. */
enum { NEEDS_PRO = 1, NEEDS_8GIB = 2 };
static int game_needs(const char *tid) {
    char name[128], ver[16];
    if (!game_tid(tid) || game_info(tid, name, sizeof name, ver, sizeof ver)) return 0;
    int n, needs = 0; size_t sl;
    gp_patch *p = game_patches(tid, ver, &n);
    char *s = settings_read(&sl);
    for (int i = 0; i < n; i++)
        if (gp_setting(s, sl, p[i].hash) == 1) needs |= (p[i].needs_pro ? NEEDS_PRO : 0) | (p[i].needs_8gib ? NEEDS_8GIB | NEEDS_PRO : 0);
    free(p); free(s);
    return needs;
}

/* Enables or disables one patch of a game. Refuses hashes that are not an entry of the game's installed version,
   and 8 GiB patches when the firmware has no 8 GiB support. Returns 0, or -1 with a reason in msg. */
static int game_set_patch(const char *tid, const char *hash, int on, int memory_ok, char *msg, size_t cap) {
    char name[128], ver[16];
    if (!game_tid(tid) || game_info(tid, name, sizeof name, ver, sizeof ver)) { snprintf(msg, cap, "Unknown game"); return -1; }
    int n; gp_patch *p = game_patches(tid, ver, &n), *hit = NULL;
    for (int i = 0; i < n; i++) if (!strcmp(p[i].hash, hash)) hit = &p[i];
    if (!hit) { free(p); snprintf(msg, cap, "This patch is not available for %s %s", tid, ver); return -1; }
    if (on && hit->needs_8gib && !memory_ok) { free(p); snprintf(msg, cap, "Not available on this firmware"); return -1; }
    size_t sl, ol; char *s = settings_read(&sl), *out = NULL;
    int rc = gp_setting_set(s, sl, hash, on, &out, &ol);
    if (rc) snprintf(msg, cap, "The patch system settings file is malformed; nothing changed");
    else if (ps_write(PS_DIR, "!_settings.xml", out, ol)) { snprintf(msg, cap, "Writing the settings failed (errno %d)", errno); rc = -1; }
    else snprintf(msg, cap, "%s: %s %s", tid, hit->name, on ? "enabled" : "disabled");
    log_line("patches: %s", msg);
    free(out); free(s); free(p);
    return rc ? -1 : 0;
}
