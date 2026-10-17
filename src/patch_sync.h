/* GPL-3.0-only. Game patch sync: downloads the XMLs in xml/ of the NeoEnabler repository and merges their entries
   into every ps-patch-system file (/data/user/patches/xml_orbis/<TITLE ID>.xml) of the same game: the files whose
   entry Title matches, and the file of every title ID listed in the repository file's <TitleID> (regions and
   editions), which is created when missing. Files are backed up before the first change of each run. Needs log_line() and write_file() from the manager. With PS_NO_HTTP
   (host tests) the includer provides ps_get(). */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "xml_merge.h"
#include "game_patches.h"

#ifndef PS_DIR
#define PS_DIR "/data/user/patches/xml_orbis"
#endif
#ifndef PS_BACKUP_ROOT
#define PS_BACKUP_ROOT "/data/neo-enabler/xml-backup"
#endif
#define PS_LIST_URL "https://api.github.com/repos/gasb07/NeoEnabler/contents/xml?ref=main"
#define PS_RAW_PREFIX "https://raw.githubusercontent.com/gasb07/NeoEnabler/"
#define PS_MAX_FILE (16u << 20)
#define PS_MAX_FILES 1024
#define PS_MAX_BACKUPS 5

typedef struct {
    int games, files_updated, files_created, files_unchanged, renamed, settings_moved, errors;
    char backup_dir[192];
    char message[256];
} ps_result;

/* AAAA00000.xml */
static int ps_title_file(const char *s) {
    if (strlen(s) != 13 || strcmp(s + 9, ".xml")) return 0;
    for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

/* Reads at most `cap` bytes of a file; returns a malloc'd, NUL-terminated buffer. */
static char *ps_read(const char *path, size_t cap, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(cap + 1);
    size_t n = buf ? fread(buf, 1, cap, f) : 0;
    int bad = !buf || ferror(f);
    fclose(f);
    if (bad) { free(buf); return NULL; }
    buf[n] = 0; *len = n;
    return buf;
}

static int ps_mkdirs(const char *path) {
    char tmp[192]; snprintf(tmp, sizeof(tmp), "%.191s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; if (mkdir(tmp, 0777) && errno != EEXIST) return -1; *p = '/'; }
    return mkdir(tmp, 0777) && errno != EEXIST ? -1 : 0;
}

static int ps_write(const char *dir, const char *name, const char *data, size_t len) {
    char path[192], tmp[208];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(tmp, sizeof(tmp), "%s.neo-tmp", path);
    if (write_file(tmp, data, len)) { remove(tmp); return -1; }
    return rename(tmp, path);
}

/* Backup folder names are timestamps (YYYYMMDD-HHMMSS), so their names sort by age. */
static int ps_stamp_dir(const char *s) {
    if (strlen(s) != 15 || s[8] != '-') return 0;
    for (int i = 0; i < 15; i++) if (i != 8 && (s[i] < '0' || s[i] > '9')) return 0;
    return 1;
}

static int ps_cmp_name(const void *a, const void *b) { return strcmp(a, b); }

/* Deletes the oldest backup folders in root until at most `keep` remain. Other folders are left alone. */
static void ps_prune_backups(const char *root, int keep) {
    static char names[256][16];
    int n = 0; struct dirent *e;
    DIR *d = opendir(root);
    if (!d) return;
    while ((e = readdir(d)) && n < 256)
        if (ps_stamp_dir(e->d_name)) snprintf(names[n++], sizeof(names[0]), "%.15s", e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof(names[0]), ps_cmp_name);
    for (int i = 0; i < n - keep; i++) {
        char dir[192], path[224];
        snprintf(dir, sizeof(dir), "%.160s/%.15s", root, names[i]);
        if ((d = opendir(dir))) {
            while ((e = readdir(d)))
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) {
                    snprintf(path, sizeof(path), "%s/%.30s", dir, e->d_name);
                    remove(path);
                }
            closedir(d);
        }
        if (rmdir(dir)) log_line("patches: cannot delete old backup %s (errno %d)", dir, errno);
        else log_line("patches: old backup %s deleted", dir);
    }
}

/* Copies the current file to this run's backup folder (created on first use, keeping at most PS_MAX_BACKUPS). */
static int ps_backup(ps_result *r, const char *dir, const char *name, const char *data, size_t len) {
    if (!r->backup_dir[0]) {
        time_t now = time(NULL); struct tm tm; char stamp[32];
        localtime_r(&now, &tm);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        snprintf(r->backup_dir, sizeof(r->backup_dir), "%.160s/%.20s", PS_BACKUP_ROOT, stamp);
        ps_prune_backups(PS_BACKUP_ROOT, PS_MAX_BACKUPS - 1);
        if (ps_mkdirs(r->backup_dir)) { log_line("patches: cannot create %s (errno %d)", r->backup_dir, errno); r->backup_dir[0] = 0; return -1; }
    }
    char path[224]; snprintf(path, sizeof(path), "%s/%.15s", r->backup_dir, name);
    (void)dir;
    if (write_file(path, data, len)) return -1;
    log_line("patches: %s backed up to %s", name, r->backup_dir);
    return 0;
}

typedef struct { char name[16]; char title[96]; } ps_file;

/* Title ID files in dir and the Title of their first entry (read from the first 64 KiB). */
static int ps_scan(const char *dir, ps_file *files, int cap) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0; struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (!ps_title_file(e->d_name)) continue;
        char path[160]; size_t len; xm_block b;
        snprintf(path, sizeof(path), "%s/%.13s", dir, e->d_name);
        char *buf = ps_read(path, 64 << 10, &len);
        if (!buf) continue;
        if (xm_next(buf, len, 0, &b) && b.title && b.title_len < sizeof(files[n].title)) {
            snprintf(files[n].name, sizeof(files[n].name), "%.13s", e->d_name);
            memcpy(files[n].title, b.title, b.title_len); files[n].title[b.title_len] = 0;
            n++;
        }
        free(buf);
    }
    closedir(d);
    return n;
}

/* Merges one repository XML (repo_name like "Bloodborne_CUSA00900.xml") into every file of the same game. */
/* Entries renamed by a merge: their hash changes, so an enabled setting moves to the new hash. */
#define PS_MAX_RENAMES 512
typedef struct { char old_hash[9], new_hash[9], file[16]; } ps_rename;
static ps_rename ps_renames[PS_MAX_RENAMES];
static int ps_rename_count;

static void ps_tag_attr(const char *tag, size_t len, const char *attr, char *out, size_t cap) {
    size_t n; const char *v = xm_attr(tag, tag + len, attr, &n);
    if (v) gp_decode(v, n, out, cap); else out[0] = 0;
}

static uint32_t ps_tag_hash(const char *file, const char *tag, size_t len, const char *ver) {
    char name[160], title[160], elf[64];
    ps_tag_attr(tag, len, "Name", name, sizeof name);
    ps_tag_attr(tag, len, "Title", title, sizeof title);
    ps_tag_attr(tag, len, "AppElf", elf, sizeof elf);
    return gp_sid(GP_FOLDER) ^ gp_sid(file) ^ gp_sid(name) ^ gp_sid(title) ^ gp_sid(elf) ^ gp_sid(ver);
}

/* xm_rename_fn: logs the rename and records the old and new hash of every AppVer value. */
static void ps_on_rename(void *ctx, const char *old_tag, size_t old_len, const char *new_tag, size_t new_len) {
    const char *file = ctx;
    char old_name[160], new_name[160], vers[160];
    ps_tag_attr(old_tag, old_len, "Name", old_name, sizeof old_name);
    ps_tag_attr(new_tag, new_len, "Name", new_name, sizeof new_name);
    ps_tag_attr(new_tag, new_len, "AppVer", vers, sizeof vers);
    log_line("patches: %s: \"%.80s\" has the same patch lines as \"%.80s\", renamed", file, old_name, new_name);
    for (char *tok = vers, *next; tok; tok = next) {
        next = strchr(tok, ','); if (next) *next++ = 0;
        while (*tok == ' ' || *tok == '\t') tok++;
        char *e = tok + strlen(tok);
        while (e > tok && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (!*tok || ps_rename_count >= PS_MAX_RENAMES) continue;
        ps_rename *m = &ps_renames[ps_rename_count++];
        snprintf(m->old_hash, sizeof m->old_hash, "%08x", ps_tag_hash(file, old_tag, old_len, tok));
        snprintf(m->new_hash, sizeof m->new_hash, "%08x", ps_tag_hash(file, new_tag, new_len, tok));
        snprintf(m->file, sizeof m->file, "%.15s", file);
    }
}

/* Turns on the new hash of every renamed entry that was enabled; the settings file is backed up first. */
static void ps_move_settings(ps_result *r, const char *dir) {
    if (!ps_rename_count) return;
    char path[192]; size_t len;
    snprintf(path, sizeof path, "%s/!_settings.xml", dir);
    char *cur = ps_read(path, PS_MAX_FILE, &len), *now = NULL;
    if (!cur) { log_line("patches: no settings file to update for renamed entries"); return; }
    size_t now_len = len;
    now = malloc(len + 1); if (!now) { free(cur); r->errors++; return; }
    memcpy(now, cur, len + 1);
    for (int i = 0; i < ps_rename_count; i++) {
        if (gp_setting(now, now_len, ps_renames[i].old_hash) != 1 || gp_setting(now, now_len, ps_renames[i].new_hash) == 1) continue;
        char *next; size_t next_len;
        if (gp_setting_set(now, now_len, ps_renames[i].new_hash, 1, &next, &next_len)) { log_line("patches: settings file malformed, not changed"); r->errors++; break; }
        free(now); now = next; now_len = next_len;
        log_line("patches: %s: enabled %s moved to %s", ps_renames[i].file, ps_renames[i].old_hash, ps_renames[i].new_hash);
        r->settings_moved++;
    }
    if (r->settings_moved) {
        if (ps_backup(r, dir, "!_settings.xml", cur, len)) { log_line("patches: backup of !_settings.xml failed, not changed"); r->errors++; r->settings_moved = 0; }
        else if (ps_write(dir, "!_settings.xml", now, now_len)) { log_line("patches: writing !_settings.xml failed (errno %d)", errno); r->errors++; r->settings_moved = 0; }
    }
    free(now); free(cur);
}

/* Title IDs in the <TitleID> block of a repository file; without one, the title ID in the file name. */
static int ps_listed_ids(const char *xml, size_t len, const char *repo_name, char ids[][16], int cap) {
    int n = 0;
    const char *a = xm_find(xml, len, "<TitleID>"), *m = xm_find(xml, len, "<Metadata");
    const char *b = a ? xm_find(a, len - (size_t)(a - xml), "</TitleID>") : NULL;
    if (a && b && (!m || b < m)) {
        for (const char *p = a; n < cap && (p = xm_find(p, (size_t)(b - p), "<ID>")); p += 4) {
            const char *v = p + 4, *e = xm_find(v, (size_t)(b - v), "</ID>");
            if (!e) break;
            while (v < e && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
            snprintf(ids[n], 16, "%.*s.xml", (int)(e - v), v);
            if ((size_t)(e - v) == 9 && ps_title_file(ids[n])) {
                int dup = 0;
                for (int k = 0; k < n; k++) dup |= !strcmp(ids[k], ids[n]);
                if (!dup) n++;
            }
        }
    }
    const char *us = strrchr(repo_name, '_');
    if (!n && us && ps_title_file(us + 1)) snprintf(ids[n++], 16, "%s", us + 1);
    return n;
}

/* Merges a repository file into one title ID file, with a backup before the first change. */
static void ps_merge_into(ps_result *r, const char *dir, const char *name, const char *repo_name,
                          const char *xml, size_t xml_len) {
    char path[160]; size_t len, out_len; char *out = NULL; int added, replaced, renamed;
    snprintf(path, sizeof(path), "%s/%.15s", dir, name);
    char *cur = ps_read(path, PS_MAX_FILE, &len);
    if (!cur || xm_merge_ex(cur, len, xml, xml_len, &out, &out_len, &added, &replaced, &renamed, ps_on_rename, (void *)name)) {
        log_line("patches: %s unreadable or malformed, skipped", name); r->errors++; free(cur); return;
    }
    r->renamed += renamed;
    if (!added && !replaced && !renamed) { log_line("patches: %s already up to date with %s", name, repo_name); r->files_unchanged++; }
    else if (ps_backup(r, dir, name, cur, len)) {
        log_line("patches: backup of %s failed, file not changed", name); r->errors++;
    } else if (ps_write(dir, name, out, out_len)) {
        log_line("patches: writing %s failed (errno %d)", name, errno); r->errors++;
    } else {
        log_line("patches: %s <- %s: %d added, %d replaced, %d renamed", name, repo_name, added, replaced, renamed);
        r->files_updated++;
    }
    free(out); free(cur);
}

static void ps_apply(ps_result *r, const char *dir, const ps_file *files, int count,
                     const char *repo_name, const char *xml, size_t xml_len) {
    xm_block first;
    if (!xm_next(xml, xml_len, 0, &first) || !first.title || !xm_find(xml, xml_len, "</Patch>")) {
        log_line("patches: %s has no entries, skipped", repo_name); r->errors++; return;
    }
    r->games++;
    char ids[64][16];
    int nids = ps_listed_ids(xml, xml_len, repo_name, ids, 64), done[64] = {0};
    /* Files of the same game by Title, under any title ID. */
    for (int i = 0; i < count; i++) {
        if (!xm_title_is(xml, xml_len, files[i].title, strlen(files[i].title))) continue;
        for (int k = 0; k < nids; k++) done[k] |= !strcmp(ids[k], files[i].name);
        ps_merge_into(r, dir, files[i].name, repo_name, xml, xml_len);
    }
    /* Every listed title ID: merged when its file exists with another Title, created when missing. */
    for (int k = 0; k < nids; k++) {
        if (done[k]) continue;
        char path[160]; struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, ids[k]);
        if (!stat(path, &st)) { ps_merge_into(r, dir, ids[k], repo_name, xml, xml_len); continue; }
        if (ps_write(dir, ids[k], xml, xml_len)) { log_line("patches: creating %s failed (errno %d)", ids[k], errno); r->errors++; continue; }
        log_line("patches: %s created from %s", ids[k], repo_name);
        r->files_created++;
    }
    if (!nids) { log_line("patches: %s has no title ID, skipped", repo_name); r->errors++; }
}

#ifndef PS_NO_HTTP
int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceSslInit(size_t);
int sceHttpInit(int, int, size_t);
int sceHttpCreateTemplate(int, const char *, int, int);
int sceHttpsSetSslCallback(int, void *, void *);
int sceHttpSetResponseHeaderMaxSize(int, size_t);
int sceHttpSetConnectTimeOut(int, uint32_t);
int sceHttpSetRecvTimeOut(int, uint32_t);
int sceHttpCreateConnectionWithURL(int, const char *, int);
int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int, int, const char *, uint64_t);
int sceHttpDeleteRequest(int);
int sceHttpSendRequest(int, const void *, size_t);
int sceHttpGetStatusCode(int, int *);
int sceHttpReadData(int, void *, size_t);

/* The payload process has no CA store: certificate verification fails (0x8095f00c), so the callback accepts the
   server certificate, as the ELF loader does for https payload URLs. */
static int ps_ssl_cb(void) { return 0; }

/* GET over HTTPS; returns a malloc'd NUL-terminated body, NULL on failure (reason in log). */
static char *ps_get(const char *url, size_t *len) {
    static int tmpl = -1;
    if (tmpl < 0) {
        int pool, ssl, ctx;
        sceNetInit();
        if ((pool = sceNetPoolCreate("neo-patches", 64 * 1024, 0)) < 0 || (ssl = sceSslInit(256 * 1024)) < 0 ||
            (ctx = sceHttpInit(pool, ssl, 256 * 1024)) < 0 || (tmpl = sceHttpCreateTemplate(ctx, VERSION, 2, 1)) < 0 ||
            sceHttpSetResponseHeaderMaxSize(tmpl, 0x4000) < 0 || sceHttpsSetSslCallback(tmpl, ps_ssl_cb, NULL)) {
            log_line("patches: HTTP init failed"); tmpl = -1; return NULL;
        }
        sceHttpSetConnectTimeOut(tmpl, 10 * 1000 * 1000);
        sceHttpSetRecvTimeOut(tmpl, 20 * 1000 * 1000);
    }
    int conn = sceHttpCreateConnectionWithURL(tmpl, url, 1), req = -1, status = 0, rc = -1;
    if (conn >= 0) req = sceHttpCreateRequestWithURL(conn, 0, url, 0);
    if (req >= 0) rc = sceHttpSendRequest(req, NULL, 0);
    if (rc || sceHttpGetStatusCode(req, &status) || status != 200) {
        log_line("patches: GET %s failed (conn %x, req %x, rc %x, HTTP %d)", url, conn, req, rc, status);
        if (req >= 0) sceHttpDeleteRequest(req);
        if (conn >= 0) sceHttpDeleteConnection(conn);
        return NULL;
    }
    size_t cap = 64 * 1024, n = 0; char *buf = malloc(cap + 1);
    for (;;) {
        if (!buf) break;
        if (n == cap) {
            if (cap >= PS_MAX_FILE) { free(buf); buf = NULL; break; }
            char *bigger = realloc(buf, cap * 2 + 1);
            if (!bigger) { free(buf); buf = NULL; break; }
            buf = bigger; cap *= 2;
        }
        int got = sceHttpReadData(req, buf + n, cap - n);
        if (got < 0) { free(buf); buf = NULL; break; }
        if (!got) break;
        n += (size_t)got;
    }
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    if (!buf) { log_line("patches: reading %s failed", url); return NULL; }
    log_line("patches: GET %s: %zu bytes", url, n);
    buf[n] = 0; *len = n;
    return buf;
}
#endif

/* Downloads the repository's xml/ folder and merges it into the patch system files. Each step is logged; the caller
   logs the summary in r->message. */
static void patch_sync(ps_result *r) {
    memset(r, 0, sizeof(*r));
    ps_rename_count = 0;
    log_line("patches: sync started, listing %s", PS_LIST_URL);
    size_t list_len;
    char *list = ps_get(PS_LIST_URL, &list_len);
    if (!list) { snprintf(r->message, sizeof(r->message), "Game patches: download failed, files not changed"); return; }
    static ps_file files[PS_MAX_FILES];
    int count = ps_scan(PS_DIR, files, PS_MAX_FILES);
    if (count < 0) {
        if (ps_mkdirs(PS_DIR)) { free(list); snprintf(r->message, sizeof(r->message), "Game patches: %s unavailable", PS_DIR); return; }
        log_line("patches: %s did not exist, created", PS_DIR);
        count = 0;
    }
    log_line("patches: %d title ID files in %s", count, PS_DIR);
    int listed = 0, skipped = 0;
    for (const char *p = list; (p = strstr(p, "\"download_url\""));) {
        p += 14;
        while (*p == ' ' || *p == ':') p++;
        if (*p != '"') { skipped++; continue; }   /* folders have "download_url": null */
        const char *url = ++p, *end = strchr(url, '"');
        if (!end) break;
        p = end + 1;
        size_t ul = (size_t)(end - url);
        char u[256];
        if (ul >= sizeof(u) || strncmp(url, PS_RAW_PREFIX, strlen(PS_RAW_PREFIX)) || ul < 4 || memcmp(end - 4, ".xml", 4)) {
            log_line("patches: listing entry %.*s skipped (not a repository XML)", (int)(ul < 200 ? ul : 200), url); skipped++; continue;
        }
        memcpy(u, url, ul); u[ul] = 0;
        listed++;
        size_t len; char *xml = ps_get(u, &len);
        if (!xml) { r->errors++; continue; }
        ps_apply(r, PS_DIR, files, count, strrchr(u, '/') + 1, xml, len);
        free(xml);
    }
    free(list);
    ps_move_settings(r, PS_DIR);
    log_line("patches: listing had %d repository XMLs, %d other entries skipped", listed, skipped);
    int changed = r->files_updated + r->files_created;
    if (!r->games) snprintf(r->message, sizeof(r->message), "Game patches: no patch files found in the repository");
    else snprintf(r->message, sizeof(r->message), "Game patches: %d game%s, %d file%s updated, %d created, %d unchanged%s%s%s%.120s",
                  r->games, r->games == 1 ? "" : "s", r->files_updated, r->files_updated == 1 ? "" : "s", r->files_created,
                  r->files_unchanged, r->renamed ? ", same patches renamed" : "", r->errors ? ", errors in log" : "", changed && r->backup_dir[0] ? "\nBackup: " : "",
                  changed && r->backup_dir[0] ? r->backup_dir : "");
}
