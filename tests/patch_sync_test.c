/* Host-only: runs the patch sync against a fake GitHub that serves the repository's xml/ folder.
   Usage: patch_sync_test <xml dir> [GitHub contents listing JSON]. Without a listing, one in the GitHub API format
   is generated from the folder, with entries the sync must ignore. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static char test_dir[64], backup_root[64];
#define PS_DIR test_dir
#define PS_BACKUP_ROOT backup_root
#define PS_NO_HTTP 1

static void log_line(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}

static const char *repo_dir;
static char *listing;
static int fail_listing;
static const char *fail_file;
static int downloads;

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    buf[n] = 0; *len = (size_t)n;
    return buf;
}

/* Fake GitHub: the API listing, and raw files served from repo_dir by name. Any other URL is a test failure. */
static char *ps_get(const char *url, size_t *len);

#include "../src/patch_sync.h"
#include "../src/game_patches.h"

static char *ps_get(const char *url, size_t *len) {
    if (!strcmp(url, PS_LIST_URL)) {
        if (fail_listing) return NULL;
        *len = strlen(listing);
        return strdup(listing);
    }
    assert(!strncmp(url, PS_RAW_PREFIX, strlen(PS_RAW_PREFIX)));
    const char *name = strrchr(url, '/') + 1;
    if (fail_file && !strcmp(name, fail_file)) return NULL;
    char path[512]; snprintf(path, sizeof(path), "%s/%s", repo_dir, name);
    downloads++;
    char *buf = slurp(path, len);
    assert(buf);
    return buf;
}

typedef struct { char name[64]; char *xml; size_t len; char title[96]; char id[16]; } repo_file;
static repo_file repo[64];
static int repo_count;

static void load_repo(void) {
    DIR *d = opendir(repo_dir); struct dirent *e;
    assert(d);
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".xml")) continue;
        repo_file *f = &repo[repo_count++];
        assert(repo_count < 64 && n < sizeof(f->name));
        strcpy(f->name, e->d_name);
        char path[512]; snprintf(path, sizeof(path), "%s/%s", repo_dir, f->name);
        f->xml = slurp(path, &f->len);
        assert(f->xml);
    }
    closedir(d);
    assert(repo_count > 0);
}

/* What the sync relies on in every downloaded file: a title ID in the name, one Title per AppElf (a collection has
   one per game, and the first entry's Title names the file), unique Name + AppVer + AppElf. */
static void check_repo(void) {
    for (int i = 0; i < repo_count; i++) {
        repo_file *f = &repo[i];
        const char *us = strrchr(f->name, '_');
        if (!us || !ps_title_file(us + 1)) { fprintf(stderr, "%s: name must end in _<TITLE ID>.xml\n", f->name); exit(1); }
        snprintf(f->id, sizeof(f->id), "%s", us + 1);
        assert(xm_find(f->xml, f->len, "</Patch>"));
        xm_block a, b; int entries = 0;
        for (size_t p = 0; xm_next(f->xml, f->len, p, &a); p = a.end, entries++) {
            if (!a.title || !a.name || !a.ver) { fprintf(stderr, "%s: entry without Title, Name or AppVer\n", f->name); exit(1); }
            if (!entries) { memcpy(f->title, a.title, a.title_len); f->title[a.title_len] = 0; }
            for (size_t q = a.end; xm_next(f->xml, f->len, q, &b); q = b.end) {
                size_t ael, bel; const char *ae = xm_elf(&a, &ael), *be = xm_elf(&b, &bel);
                if (xm_same(ae, ael, be, bel) && !xm_same(a.title, a.title_len, b.title, b.title_len)) { fprintf(stderr, "%s: mixed Titles\n", f->name); exit(1); }
                if (xm_same(a.name, a.name_len, b.name, b.name_len) && xm_same_target(&a, &b)) {
                    fprintf(stderr, "%s: duplicate entry \"%.*s\" %.*s\n", f->name, (int)a.name_len, a.name, (int)a.ver_len, a.ver);
                    exit(1);
                }
            }
        }
        assert(entries > 0);
        /* Naming standard: "<resolution> (PS4 Pro mode[, ...][, requires 8 GiB enabled])", and NeoRequires agrees
           with the name, so the manager detects the requirements either way. */
        for (size_t p = 0; xm_next(f->xml, f->len, p, &a); p = a.end) {
            char name[160], req[32]; int pro, eight, name_pro, name_eight;
            gp_attr(f->xml, &a, "Name", name, sizeof name);
            gp_attr(f->xml, &a, "NeoRequires", req, sizeof req);
            gp_requirements(req, name, &pro, &eight);
            gp_requirements("", name, &name_pro, &name_eight);
            const char *mode = strstr(name, " (PS4 Pro mode");
            size_t nl = strlen(name);
            int standard = !mode || (mode > name && name[nl - 1] == ')' && !strchr(mode + 14, '(') &&
                                     (mode[14] == ')' || mode[14] == ','));
            if (!*req && name_pro) { fprintf(stderr, "%s: \"%s\" has no NeoRequires\n", f->name, name); exit(1); }
            if (pro != name_pro || eight != name_eight || !standard ||
                (eight && !strstr(name, ", requires 8 GiB enabled)"))) {
                fprintf(stderr, "%s: \"%s\" (NeoRequires \"%s\") does not follow the naming standard\n", f->name, name, req);
                exit(1);
            }
        }
        for (int j = 0; j < i; j++)
            if (!strcasecmp(repo[j].title, f->title)) { fprintf(stderr, "%s and %s share a Title\n", repo[j].name, f->name); exit(1); }
    }
}

/* A listing in the GitHub contents API format, plus a non-XML file, a folder and a foreign URL to ignore. */
static void make_listing(void) {
    size_t cap = 4096 + (size_t)repo_count * 1024, n = 0;
    listing = malloc(cap);
    n += (size_t)snprintf(listing + n, cap - n, "[\n");
    for (int i = 0; i < repo_count; i++)
        n += (size_t)snprintf(listing + n, cap - n,
            "  {\n    \"name\": \"%s\",\n    \"path\": \"xml/%s\",\n    \"type\": \"file\",\n"
            "    \"url\": \"https://api.github.com/repos/gasb07/NeoEnabler/contents/xml/%s?ref=main\",\n"
            "    \"html_url\": \"https://github.com/gasb07/NeoEnabler/blob/main/xml/%s\",\n"
            "    \"download_url\": \"" PS_RAW_PREFIX "main/xml/%s\"\n  },\n",
            repo[i].name, repo[i].name, repo[i].name, repo[i].name, repo[i].name);
    n += (size_t)snprintf(listing + n, cap - n,
        "  {\"name\": \"README.md\", \"download_url\": \"" PS_RAW_PREFIX "main/xml/README.md\"},\n"
        "  {\"name\": \"old\", \"type\": \"dir\", \"download_url\": null},\n"
        "  {\"name\": \"other.xml\", \"download_url\": \"https://example.com/xml/other.xml\"}\n]\n");
    assert(n < cap);
}

static void put(const char *name, const char *text) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", test_dir, name);
    assert(!write_file(path, text, strlen(text)));
}

static char *get(const char *dir, const char *name, size_t *len) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    return slurp(path, len);
}

static int is_dir(const char *path) { struct stat st; return !stat(path, &st) && S_ISDIR(st.st_mode); }

/* Backup folders named like timestamps in backup_root. */
static int backups(void) {
    DIR *d = opendir(backup_root); struct dirent *e; int n = 0;
    assert(d);
    while ((e = readdir(d))) n += ps_stamp_dir(e->d_name);
    closedir(d);
    return n;
}

static int count_of(const char *s, size_t n, const char *pat) {
    int c = 0;
    for (const char *p = s; (p = xm_find(p, (size_t)(s + n - p), pat)); p++) c++;
    return c;
}

int main(int argc, char **argv) {
    (void)gp_sfo; (void)gp_game_version; (void)gp_list;   /* unused here */
    assert(argc >= 2);
    repo_dir = argv[1];
    load_repo();
    check_repo();
    if (argc > 2) { size_t n; listing = slurp(argv[2], &n); assert(listing); }
    else make_listing();

    char root[] = "/tmp/neo-sync-XXXXXX";
    assert(mkdtemp(root));
    snprintf(test_dir, sizeof(test_dir), "%s/xml_orbis", root);
    snprintf(backup_root, sizeof(backup_root), "%s/backup", root);
    assert(!ps_mkdirs(test_dir));

    /* The first game is already on the console under two other title IDs, with a community entry and an old copy of
       our first entry; the second file spells the Title differently. Every other game has no file yet. */
    repo_file *g = &repo[0];
    xm_block first;
    assert(xm_next(g->xml, g->len, 0, &first));
    char old[4096], upper[96];
    for (size_t i = 0; i <= strlen(g->title); i++) upper[i] = (char)toupper((unsigned char)g->title[i]);
    const char *titles[2] = { g->title, upper }, *names[2] = { "CUSA90001.xml", "CUSA90002.xml" };
    for (int i = 0; i < 2; i++) {
        snprintf(old, sizeof(old),
                 "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Patch>\n"
                 "    <Metadata Title=\" %s \" Name=\"Community entry\" Author=\"someone\" AppVer=\"%.*s\">\n"
                 "        <PatchList><Line Type=\"bytes\" Address=\"0x10\" Value=\"0x90\"/></PatchList>\n    </Metadata>\n"
                 "    <Metadata Title=\"%s\" Name=\"%.*s\" AppVer=\"%.*s\">\n"
                 "        <PatchList><Line Type=\"bytes\" Address=\"0x20\" Value=\"0xdeadbeef\"/></PatchList>\n    </Metadata>\n"
                 "</Patch>\n",
                 titles[i], (int)first.ver_len, first.ver, titles[i], (int)first.name_len, first.name, (int)first.ver_len, first.ver);
        put(names[i], old);
    }
    const char *unrelated = "<Patch>\n  <Metadata Title=\"Unrelated Game\" Name=\"x\" AppVer=\"01.00\"><PatchList/></Metadata>\n</Patch>\n";
    put("CUSA90003.xml", unrelated);
    put("notes.xml", "not a title ID file");
    /* Six older backups and a folder that is not a backup: the run keeps the newest four plus its own. */
    char old_dir[160];
    assert(!ps_mkdirs(backup_root));
    for (int i = 1; i <= 6; i++) {
        snprintf(old_dir, sizeof(old_dir), "%s/20200101-00000%d", backup_root, i);
        assert(!ps_mkdirs(old_dir));
        snprintf(old_dir, sizeof(old_dir), "%s/20200101-00000%d/CUSA90001.xml", backup_root, i);
        assert(!write_file(old_dir, "old", 3));
    }
    snprintf(old_dir, sizeof(old_dir), "%s/keep-me", backup_root);
    assert(!ps_mkdirs(old_dir));
    size_t orig_len[2]; char *orig[2];
    for (int i = 0; i < 2; i++) orig[i] = get(test_dir, names[i], &orig_len[i]);

    /* Every title ID listed by every repository file. */
    char ids[64][64][16]; int nids[64], total_ids = 0;
    for (int i = 0; i < repo_count; i++) { nids[i] = ps_listed_ids(repo[i].xml, repo[i].len, repo[i].name, ids[i], 64); assert(nids[i] > 0); total_ids += nids[i]; }

    /* First run: both files of the first game merged and backed up, a file created for every listed title ID. */
    ps_result r;
    patch_sync(&r);
    puts(r.message);
    assert(downloads == repo_count);
    assert(r.games == repo_count && r.errors == 0);
    assert(r.files_updated == 2 && r.files_created == total_ids && r.files_unchanged == 0);
    assert(r.backup_dir[0] && strstr(r.message, r.backup_dir));
    assert(backups() == PS_MAX_BACKUPS);
    for (int i = 1; i <= 6; i++) {
        snprintf(old_dir, sizeof(old_dir), "%s/20200101-00000%d", backup_root, i);
        assert(is_dir(old_dir) == (i > 2));                                      /* the two oldest deleted */
    }
    snprintf(old_dir, sizeof(old_dir), "%s/keep-me", backup_root);
    assert(is_dir(old_dir));
    for (int i = 0; i < 2; i++) {
        size_t n, b; char *now = get(test_dir, names[i], &n), *bak = get(r.backup_dir, names[i], &b);
        assert(bak && b == orig_len[i] && !memcmp(bak, orig[i], b));                 /* backup is the original */
        assert(strstr(now, "Name=\"Community entry\"") && !strstr(now, "0xdeadbeef")); /* others kept, ours replaced */
        xm_block e;
        for (size_t p = 0; xm_next(g->xml, g->len, p, &e); p = e.end) {
            char block[1 << 16]; size_t bl = e.end - e.start;
            assert(bl < sizeof(block));
            memcpy(block, g->xml + e.start, bl); block[bl] = 0;
            assert(count_of(now, n, block) == 1);                                    /* every entry exactly once */
        }
        free(now); free(bak);
    }
    for (int i = 0; i < repo_count; i++)
        for (int k = 0; k < nids[i]; k++) {
            size_t n; char *now = get(test_dir, ids[i][k], &n);
            assert(now && n == repo[i].len && !memcmp(now, repo[i].xml, n));           /* created as downloaded */
            free(now);
        }
    size_t n; char *s = get(test_dir, "CUSA90003.xml", &n);
    assert(!strcmp(s, unrelated)); free(s);
    assert(!get(r.backup_dir, "CUSA90003.xml", &n) && !get(test_dir, "CUSA90001.xml.neo-tmp", &n));

    /* Second run: nothing to change, no new backup. */
    char before[2][1 << 16]; size_t before_len[2];
    for (int i = 0; i < 2; i++) { s = get(test_dir, names[i], &before_len[i]); assert(before_len[i] < sizeof(before[i])); memcpy(before[i], s, before_len[i]); free(s); }
    patch_sync(&r);
    puts(r.message);
    assert(r.files_updated == 0 && r.files_created == 0 && r.files_unchanged == 2 + total_ids && r.errors == 0);
    assert(!r.backup_dir[0] && !strstr(r.message, "Backup"));
    assert(backups() == PS_MAX_BACKUPS);
    for (int i = 0; i < 2; i++) { s = get(test_dir, names[i], &n); assert(n == before_len[i] && !memcmp(s, before[i], n)); free(s); }

    /* Listing download fails: nothing touched. */
    put(names[0], orig[0]);
    fail_listing = 1;
    patch_sync(&r);
    puts(r.message);
    assert(strstr(r.message, "download failed") && !r.games);
    s = get(test_dir, names[0], &n); assert(n == orig_len[0] && !memcmp(s, orig[0], n)); free(s);

    /* One file fails: counted as an error, the rest still merged. */
    fail_listing = 0;
    fail_file = repo[repo_count > 1 ? 1 : 0].name;
    patch_sync(&r);
    puts(r.message);
    assert(r.errors == 1 && strstr(r.message, "errors in log"));
    assert(r.games == repo_count - 1);
    if (repo_count > 1) assert(r.files_updated == 1);
    assert(backups() <= PS_MAX_BACKUPS);
    free(orig[0]); free(orig[1]);

    /* An entry with the same patch lines as our first one under a development name, enabled in the settings:
       renamed to ours, and the setting moves to the new hash (settings backed up). */
    fail_file = NULL;
    size_t bl = first.end - first.start;
    char *dev = malloc(bl + 256);
    const char *nm = xm_find(g->xml + first.start, bl, " Name=\"");
    assert(dev && nm);
    size_t pre = (size_t)(nm - (g->xml + first.start)) + 7;
    const char *nm_end = memchr(nm + 7, '"', bl - pre);
    int dl = snprintf(dev, bl + 256, "<Patch>\n  %.*sdev name%.*s\n</Patch>\n", (int)pre, g->xml + first.start,
                      (int)(g->xml + first.end - nm_end), nm_end);
    put("CUSA90005.xml", dev);
    char ver[32]; snprintf(ver, sizeof ver, "%.*s", (int)first.ver_len, first.ver);
    char *comma = strchr(ver, ','); if (comma) *comma = 0;
    size_t tl = xm_tag_len(dev + 10, (size_t)dl - 10);
    char old_hash[9], new_hash[9], settings[256];
    snprintf(old_hash, sizeof old_hash, "%08x", ps_tag_hash("CUSA90005.xml", dev + 10, tl, ver));
    snprintf(new_hash, sizeof new_hash, "%08x", ps_tag_hash("CUSA90005.xml", g->xml + first.start,
                                                             xm_tag_len(g->xml + first.start, bl), ver));
    snprintf(settings, sizeof settings, "<?xml version=\"1.0\"?>\n<Settings>\n  <Patch hash=\"%s\" enabled=\"true\" />\n</Settings>\n", old_hash);
    put("!_settings.xml", settings);
    patch_sync(&r);
    puts(r.message);
    assert(r.renamed == 1 && r.settings_moved == 1 && strstr(r.message, "renamed"));
    s = get(test_dir, "CUSA90005.xml", &n);
    assert(!strstr(s, "dev name") && count_of(s, n, "<Metadata") == count_of(g->xml, g->len, "<Metadata"));
    free(s);
    s = get(test_dir, "!_settings.xml", &n);
    assert(gp_setting(s, n, new_hash) == 1 && gp_setting(s, n, old_hash) == 1);
    free(s);
    s = get(r.backup_dir, "!_settings.xml", &n);
    assert(s && !strcmp(s, settings));
    free(s); free(dev);

    /* A listed title ID whose file names the game differently (another region's community file): merged anyway. */
    char other[512];
    snprintf(other, sizeof other, "<Patch>\n  <Metadata Title=\"Some Other Name\" Name=\"Regional entry\" AppVer=\"01.00\">\n"
             "    <PatchList><Line Type=\"bytes\" Address=\"0x30\" Value=\"0x90\"/></PatchList>\n  </Metadata>\n</Patch>\n");
    put(ids[0][0], other);
    patch_sync(&r);
    puts(r.message);
    assert(r.errors == 0 && r.files_updated >= 1);
    s = get(test_dir, ids[0][0], &n);
    assert(strstr(s, "Name=\"Regional entry\"") && count_of(s, n, "<Metadata") == 1 + count_of(g->xml, g->len, "<Metadata"));
    free(s);

    char cmd[600]; snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    assert(!system(cmd));
    printf("PASS patch sync: %d repository XMLs, GitHub listing filter, merge by Title and by listed title ID, same patch lines renamed with their settings, backups (at most 5), idempotent, download failures\n",
           repo_count);
    return 0;
}
