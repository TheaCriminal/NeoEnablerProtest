/* GPL-3.0-only. Finds, installs and removes the NEO launch hook (shim.S) in SceShellCore.
   The call site is the getAppInfo call that ps-patch-system redirects to its own hook, found with the same
   per-firmware patterns ps-patch-system uses (patch/patch_shellcore.c, patchGetAppInfoSfo). The shim goes to
   a cave at the end of the ShellCore eh_frame (ps-patch-system places its own code at the start of it and
   makes the whole module RWX) and calls the ps-patch-system hook before changing the attribute. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#include "dynlib.h"
#include "shim_data.h"

#define CAVE_SIZE 512
#define CAVE_MIN_OFFSET 0x10000 /* ps-patch-system shellcode at the start of eh_frame is far smaller */
#define PAGE 0x4000

enum {
    HOOK_ON = 1,
    HOOK_OFF = 0,
    HOOK_NO_PATCH_SYSTEM = -1, /* call target outside eh_frame or memory not RWX */
    HOOK_BAD_CAVE = -2,        /* cave holds something other than this shim */
    HOOK_UNSUPPORTED_FW = -3,
    HOOK_NO_SITE = -4,         /* no pattern matched */
};

/* PS5 patterns from ps-patch-system patchGetAppInfoSfo; offset points to the e8 call. Index > 1 passes pInfo as
   the fourth argument (ps-patch-system __export_PS5_GetAppInfoHook2). */
static const struct { const char *pattern; size_t offset; } site_patterns[] = {
    {"e8 ? ? ? ? 48 8b 85 ? ? ? ? 4c 89 ? ? 89 ? 48 8b 70 10 e8 ? ? ? ?", 22},            /* 4.00-6.00 & 8.00 */
    {"49 8b 76 10 4c 89 ef 4c 89 e2 e8 ? ? ? ? 85 c0 0f 89", 10},                         /* 7.00 */
    {"48 8b 85 ? ? ? ? 48 8d 75 ? 4c 89 ef 4c 89 ? 48 8b 50 10 e8 ? ? ? ? 41 89 ?", 21},  /* 9.00 - 10.xx */
    {"49 8b 55 10 48 8b bd ? ? ? ? 48 8d 75 ? 4c 89 e1 e8 ? ? ? ? 41 89 c6", 18},         /* 11.00+ */
    {"49 8b 55 10 48 8b bd ? ? ? ? 48 8b 8d ? ? ? ? 48 8d 75 ? e8 ? ? ? ? 41 89 c6", 22}, /* 12.00+ */
};

typedef struct {
    pid_t pid;
    uintptr_t site, cave, frame, frame_end, target;
    int four_args;
    uint32_t attr_off;
} shell_t;

static unsigned fw_major(void) { return kernel_get_fw_version() >> 16; }

/* ps-patch-system host/shellcode/shellcore_attribute.cpp get_ps5_attr: offset of ATTRIBUTE in the app info. */
static uint32_t attr_offset(unsigned fw) {
    if (fw >= 0x100 && fw <= 0x1060) return 0x26c;
    if (fw >= 0x1100 && fw <= 0x1300) return 0x1f4;
    return 0x214;
}

/* Firmwares covered by the ps-patch-system PS5 patterns (4.00+) and kernel offsets (up to 13.60). */
static int fw_supported(unsigned fw) { return fw >= 0x400 && fw <= 0x1360; }

static pid_t find_proc(const char *comm) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return -1;
    unsigned char *buf = malloc(n);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return -1; }
    pid_t pid = -1;
    for (size_t off = 0; off + sizeof(struct kinfo_proc) <= n;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) break;
        if (!strcmp(ki->ki_comm, comm)) { pid = ki->ki_pid; break; }
        off += (size_t)ki->ki_structsize;
    }
    free(buf); return pid;
}

static size_t parse_pattern(const char *p, unsigned char *bytes, unsigned char *mask, size_t cap) {
    size_t n = 0;
    while (*p && n < cap) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { bytes[n] = 0; mask[n++] = 0; p++; continue; }
        bytes[n] = (unsigned char)strtoul(p, NULL, 16); mask[n++] = 1; p += 2;
    }
    return n;
}

/* First match of the pattern in [start, start+size), plus offset; 0 when not found. */
static uintptr_t scan(pid_t pid, uintptr_t start, size_t size, const char *pattern, size_t offset) {
    unsigned char bytes[64], mask[64];
    size_t plen = parse_pattern(pattern, bytes, mask, sizeof(bytes));
    const size_t chunk = 1 << 20;
    unsigned char *buf = malloc(chunk + sizeof(bytes));
    if (!buf || !plen) { free(buf); return 0; }
    uintptr_t found = 0;
    for (size_t pos = 0; pos < size && !found; pos += chunk) {
        size_t len = size - pos < chunk + plen - 1 ? size - pos : chunk + plen - 1;
        if (len < plen || kernel_proc_copyout(pid, start + pos, buf, len)) continue;
        for (size_t i = 0; i + plen <= len && !found; i++) {
            if (buf[i] != bytes[0]) continue;
            size_t k = 1;
            while (k < plen && (!mask[k] || buf[i + k] == bytes[k])) k++;
            if (k == plen) found = start + pos + i + offset;
        }
    }
    free(buf);
    return found;
}

static int read_call(pid_t pid, uintptr_t site, uintptr_t *target) {
    unsigned char b[5];
    if (kernel_proc_copyout(pid, site, b, 5) || b[0] != 0xe8) return -1;
    int32_t rel; memcpy(&rel, b + 1, 4);
    *target = site + 5 + (intptr_t)rel;
    return 0;
}

static int write_call(pid_t pid, uintptr_t site, uintptr_t target) {
    intptr_t rel = (intptr_t)target - (intptr_t)(site + 5);
    if (rel != (int32_t)rel) return -1;
    unsigned char b[5] = {0xe8}; int32_t r = (int32_t)rel; memcpy(b + 1, &r, 4);
    uintptr_t back;
    return kernel_proc_copyin(pid, b, site, 5) || read_call(pid, site, &back) || back != target ? -1 : 0;
}

/* Code part of the shim in the cave matches this build (data fields excluded). */
static int cave_has_shim(pid_t pid, uintptr_t cave) {
    unsigned char a[sizeof(shim_code)];
    if (kernel_proc_copyout(pid, cave, a, sizeof(a))) return 0;
    return !memcmp(a, shim_code, SHIM_PREV) && !memcmp(a + SHIM_MAGIC, shim_code + SHIM_MAGIC, sizeof(a) - SHIM_MAGIC);
}

/* Fills s and returns the hook state. The call site scan (~1 s) runs once per ShellCore pid. */
static int hook_state(shell_t *s) {
    static pid_t cached_pid = -1; static uintptr_t cached_site; static int cached_four;
    unsigned fw = fw_major();
    memset(s, 0, sizeof(*s));
    if (!fw_supported(fw)) return HOOK_UNSUPPORTED_FW;
    s->attr_off = attr_offset(fw);
    dynlib_obj_t obj = {0};
    s->pid = find_proc("SceShellCore");
    if (s->pid < 0 || kernel_dynlib_obj(s->pid, 0, &obj) || !obj.eh_frame || obj.eh_frame_size < CAVE_MIN_OFFSET + CAVE_SIZE)
        return HOOK_UNSUPPORTED_FW;
    if (s->pid != cached_pid) {
        cached_site = 0;
        for (size_t i = 0; i < sizeof(site_patterns) / sizeof(site_patterns[0]) && !cached_site; i++) {
            cached_site = scan(s->pid, obj.mapbase, obj.mapsize, site_patterns[i].pattern, site_patterns[i].offset);
            cached_four = i > 1;
        }
        cached_pid = s->pid;
    }
    if (!cached_site) return HOOK_NO_SITE;
    s->site = cached_site; s->four_args = cached_four;
    s->frame = obj.eh_frame; s->frame_end = obj.eh_frame + obj.eh_frame_size;
    s->cave = (s->frame_end - CAVE_SIZE) & ~(uintptr_t)15;
    if (read_call(s->pid, s->site, &s->target)) return HOOK_NO_PATCH_SYSTEM;
    if (kernel_get_vmem_protection(s->pid, s->site & ~(uintptr_t)(PAGE - 1), PAGE) != 7 ||
        kernel_get_vmem_protection(s->pid, s->cave & ~(uintptr_t)(PAGE - 1), PAGE) != 7 ||
        kernel_get_vmem_protection(s->pid, (s->cave + sizeof(shim_code)) & ~(uintptr_t)(PAGE - 1), PAGE) != 7)
        return HOOK_NO_PATCH_SYSTEM;
    if (s->target == s->cave) return cave_has_shim(s->pid, s->cave) ? HOOK_ON : HOOK_BAD_CAVE;
    if (s->target >= s->frame && s->target < s->cave) return HOOK_OFF;
    return HOOK_NO_PATCH_SYSTEM;
}

static const char *state_text(int st) {
    switch (st) {
    case HOOK_ON: return "enabled";
    case HOOK_OFF: return "disabled";
    case HOOK_NO_PATCH_SYSTEM: return "ps-patch-system not loaded";
    case HOOK_BAD_CAVE: return "unknown code in the hook area";
    case HOOK_UNSUPPORTED_FW: return "unsupported firmware or ShellCore";
    default: return "getAppInfo call not found";
    }
}

/* Enables (shim in the cave, call redirected to it) or disables (call back to the ps-patch-system hook)
   with ShellCore stopped; every write is read back and the call is restored on failure. */
static int hook_set(int enable) {
    shell_t s;
    int st = hook_state(&s);
    if (st == enable) return 0;
    if (st != !enable) return 3;
    if (kill(s.pid, SIGSTOP)) return 5;
    usleep(100000);
    int rc = 0;
    uintptr_t target;
    if (read_call(s.pid, s.site, &target) || target != s.target) rc = 6;
    else if (enable) {
        unsigned char code[sizeof(shim_code)], back[sizeof(shim_code)];
        memcpy(code, shim_code, sizeof(code));
        uint64_t prev = target, printf_ptr = (uint64_t)kernel_dynlib_dlsym(s.pid, 0x2, "printf");
        if ((int64_t)printf_ptr <= 0) printf_ptr = 0;
        memcpy(code + SHIM_PREV, &prev, 8);
        memcpy(code + SHIM_PRINTF_PTR, &printf_ptr, 8);
        memcpy(code + SHIM_ATTR_OFF, &s.attr_off, 4);
        code[SHIM_FOUR_ARGS] = (unsigned char)s.four_args;
        if (kernel_proc_copyin(s.pid, code, s.cave, sizeof(code)) || kernel_proc_copyout(s.pid, s.cave, back, sizeof(back)) ||
            memcmp(code, back, sizeof(code))) rc = 7;
        else if (write_call(s.pid, s.site, s.cave)) { write_call(s.pid, s.site, target); rc = 7; }
    } else {
        uint64_t prev = 0;
        if (kernel_proc_copyout(s.pid, s.cave + SHIM_PREV, &prev, 8) || prev < s.frame || prev >= s.cave) rc = 6;
        else if (write_call(s.pid, s.site, prev)) { write_call(s.pid, s.site, target); rc = 7; }
    }
    if (kill(s.pid, SIGCONT)) rc = rc ? rc : 8;
    return rc;
}
