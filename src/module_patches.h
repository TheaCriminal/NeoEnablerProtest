/* GPL-3.0-only. Patch entries of game modules. ps-patch-system patches only the executable a process starts with,
   so an entry whose AppElf is a module the game loads itself (AppElf ending in .prx or .sprx, for example
   PreyDll.prx) never applies. The manager applies those: when a new PS4 game process appears, it reads the enabled
   module entries of its title ID and waits for each module to be loaded, then writes the lines at the module base
   plus Address (Address is an offset in the module) and reads them back. A Line with a Module attribute is written
   into that module instead of the AppElf one, so one entry can patch several modules (it waits for all of them).
   Line types: bytes, byte, bytes16, bytes32, bytes64, float32, float64, utf8. An entry with another type (mask
   searches) is skipped. Needs games.h and log_line(); the parsing part has no device access. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "game_patches.h"

#define MP_MAX_LINE 256        /* bytes of one line */

typedef struct { uint64_t offset; size_t len; char module[64]; unsigned char data[MP_MAX_LINE]; } mp_line;   /* module: Module attribute, "" = the AppElf of the entry */

/* 1 for an AppElf the game loads as a module. */
static int mp_is_module(const char *elf) {
    size_t n = strlen(elf);
    return (n > 4 && !strcasecmp(elf + n - 4, ".prx")) || (n > 5 && !strcasecmp(elf + n - 5, ".sprx"));
}

static int mp_hex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* Bytes of one Line value; 0 when the type is supported and the value valid. */
static int mp_value(const char *type, const char *v, mp_line *l) {
    l->len = 0;
    if (!strcmp(type, "bytes")) {
        if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
        int hi = -1;
        for (; *v; v++) {
            if (*v == ' ') continue;
            int d = mp_hex(*v);
            if (d < 0) return -1;
            if (hi < 0) { hi = d; continue; }
            if (l->len >= MP_MAX_LINE) return -1;
            l->data[l->len++] = (unsigned char)(hi << 4 | d); hi = -1;
        }
        return l->len && hi < 0 ? 0 : -1;
    }
    if (!strcmp(type, "utf8")) {
        size_t n = strlen(v);
        if (!n || n > MP_MAX_LINE) return -1;
        memcpy(l->data, v, n); l->len = n;
        return 0;
    }
    size_t w = !strcmp(type, "byte") ? 1 : !strcmp(type, "bytes16") ? 2 : !strcmp(type, "bytes32") ? 4 :
               !strcmp(type, "bytes64") ? 8 : !strcmp(type, "float32") ? 4 : !strcmp(type, "float64") ? 8 : 0;
    if (!w || !*v) return -1;
    char *end; uint64_t x;
    if (type[0] == 'f') {
        double d = strtod(v, &end);
        if (*end) return -1;
        if (w == 4) { float f = (float)d; memcpy(&x, &f, 4); } else memcpy(&x, &d, 8);
    } else {
        x = strtoull(v, &end, 0);
        if (*end) return -1;
    }
    memcpy(l->data, &x, w); l->len = w;                       /* little endian, as ps-patch-system writes them */
    return 0;
}

/* Lines of the Metadata block [s, e); malloc'd, count in *n. NULL when a line has an unsupported type or value. */
static mp_line *mp_lines(const char *s, const char *e, int *n) {
    int cap = 0; *n = 0;
    for (const char *p = s; (p = xm_find(p, (size_t)(e - p), "<Line"));) cap++, p += 5;
    mp_line *out = calloc((size_t)(cap ? cap : 1), sizeof *out);
    if (!out) return NULL;
    for (const char *p = s; (p = xm_find(p, (size_t)(e - p), "<Line"));) {
        const char *t = memchr(p, '>', (size_t)(e - p));
        if (!t) break;
        char type[16] = "", addr[32] = "", val[2 * MP_MAX_LINE + 8] = "";
        xm_block b = {.start = (size_t)(p - s), .end = (size_t)(t - s) + 1};
        gp_attr(s, &b, "Type", type, sizeof type);
        gp_attr(s, &b, "Address", addr, sizeof addr);
        gp_attr(s, &b, "Value", val, sizeof val);
        char *end; mp_line *l = &out[*n];
        gp_attr(s, &b, "Module", l->module, sizeof l->module);
        l->offset = strtoull(addr, &end, 16);
        if (!addr[0] || *end || mp_value(type, val, l)) { free(out); *n = 0; return NULL; }
        ++*n; p = t;
    }
    return out;
}

#if defined(__PROSPERO__)
#include "dynlib.h"

#define MP_MAX 16              /* module entries handled for one launch */
#define MP_WAIT_S 120          /* how long after the process appears a module may still load */

static struct {
    pid_t pid;
    time_t since;
    int count;
    struct { char elf[64], name[160]; int n; mp_line *lines; } e[MP_MAX];
} mp;

static void mp_clear(void) {
    for (int i = 0; i < mp.count; i++) free(mp.e[i].lines);
    mp.count = 0;
}

/* Enabled module entries of the game of this process. */
static void mp_load(pid_t pid, const char *tid) {
    char name[128], ver[16], path[96], file[16]; size_t len, sl;
    mp_clear();
    if (game_info(tid, name, sizeof name, ver, sizeof ver)) return;
    snprintf(file, sizeof file, "%.9s.xml", tid);
    snprintf(path, sizeof path, PS_DIR "/%s", file);
    char *xml = ps_read(path, PS_MAX_FILE, &len), *st = settings_read(&sl);
    xm_block b; gp_patch p;
    for (size_t pos = 0; xml && mp.count < MP_MAX && xm_next(xml, len, pos, &b); pos = b.end) {
        if (gp_list(xml + b.start, b.end - b.start, file, ver, &p, 1) != 1 || !mp_is_module(p.elf) ||
            gp_setting(st, sl, p.hash) != 1) continue;
        int n; mp_line *l = mp_lines(xml + b.start, xml + b.end, &n);
        if (!l) { log_line("modules: %s: %s: unsupported line, entry skipped", tid, p.name); continue; }
        snprintf(mp.e[mp.count].elf, sizeof mp.e[0].elf, "%s", p.elf);
        snprintf(mp.e[mp.count].name, sizeof mp.e[0].name, "%s", p.name);
        mp.e[mp.count].n = n; mp.e[mp.count].lines = l; mp.count++;
    }
    free(xml); free(st);
    if (mp.count) log_line("modules: %s pid %d: %d enabled module entries, waiting for the modules", tid, (int)pid, mp.count);
}

/* Writes the entries of every module that is loaded now; an entry is dropped once written (or failed). */
static int mp_module(pid_t pid, const char *name, dynlib_obj_t *o) {
    uint32_t h; memset(o, 0, sizeof *o);
    return kernel_dynlib_handle(pid, name, &h) || kernel_dynlib_obj(pid, h, o) || !o->mapbase;
}

static void mp_apply(void) {
    for (int i = 0; i < mp.count;) {
        int ready = 1;
        for (int k = 0; k < mp.e[i].n && ready; k++) {
            dynlib_obj_t o;
            ready = !mp_module(mp.pid, mp.e[i].lines[k].module[0] ? mp.e[i].lines[k].module : mp.e[i].elf, &o);
        }
        if (!ready) { i++; continue; }
        int bad = 0; unsigned long base0 = 0;
        for (int k = 0; k < mp.e[i].n && !bad; k++) {
            mp_line *l = &mp.e[i].lines[k]; unsigned char back[MP_MAX_LINE]; dynlib_obj_t o;
            if (mp_module(mp.pid, l->module[0] ? l->module : mp.e[i].elf, &o)) { bad = k + 1; break; }
            if (!k) base0 = o.mapbase;
            intptr_t at = (intptr_t)(o.mapbase + l->offset);
            bad = l->offset + l->len > o.mapsize || kernel_proc_copyin(mp.pid, l->data, at, l->len) ||
                  kernel_proc_copyout(mp.pid, at, back, l->len) || memcmp(back, l->data, l->len) ? k + 1 : 0;
        }
        if (bad) log_line("modules: %s: %s: line %d failed (base 0x%lx)", mp.e[i].elf, mp.e[i].name, bad, base0);
        else log_line("modules: %s: %s: %d lines written (base 0x%lx)", mp.e[i].elf, mp.e[i].name, mp.e[i].n, base0);
        free(mp.e[i].lines);
        mp.e[i] = mp.e[--mp.count];
    }
}

/* The single PS4 game process (eboot.bin) and its title ID; 0 when there is none. */
static pid_t mp_game(char tid[10]) {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return 0;
    unsigned char *buf = malloc(n);
    pid_t pid = 0;
    if (buf && !sysctl(mib, 4, buf, &n, NULL, 0))
        for (size_t off = 0; off + sizeof(struct kinfo_proc) <= n;) {
            struct kinfo_proc *ki = (void *)(buf + off);
            if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) break;
            app_info_t info = {0};
            if (!strcmp(ki->ki_comm, "eboot.bin") && !sceKernelGetAppInfo(ki->ki_pid, &info) && game_tid(info.title_id)) {
                memcpy(tid, info.title_id, 9); tid[9] = 0; pid = ki->ki_pid; break;
            }
            off += (size_t)ki->ki_structsize;
        }
    free(buf);
    return pid;
}

/* Main loop step: the process list every second, the modules every 250 ms while entries wait for them. */
static void mp_tick(void) {
    static struct timespec last, scan;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000;
    if (ms < (mp.count ? 250 : 1000)) return;
    last = now;
    if (now.tv_sec - scan.tv_sec >= 1) {
        scan = now;
        char tid[10]; pid_t pid = mp_game(tid);
        if (pid != mp.pid) { mp.pid = pid; mp.since = now.tv_sec; if (pid) mp_load(pid, tid); else mp_clear(); }
    }
    if (!mp.count) return;
    if (now.tv_sec - mp.since > MP_WAIT_S) {
        for (int i = 0; i < mp.count; i++) log_line("modules: %s not loaded after %d s: %s not applied", mp.e[i].elf, MP_WAIT_S, mp.e[i].name);
        mp_clear();
        return;
    }
    mp_apply();
}
#endif
