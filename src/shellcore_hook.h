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
/* Shim of NeoEnabler 0.1.x-0.2.x (Pro mode for every PS4 game), same cave: replaced on install. */
#define V2_PREV 137
#define V2_MAGIC 158
#define CAVE_MIN_OFFSET 0x10000 /* ps-patch-system shellcode at the start of eh_frame is far smaller */
#define PAGE 0x4000

enum {
    HOOK_OLD = 2,              /* shim of an older NeoEnabler build in the cave; install replaces it */
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

/* Address of the ps-patch-system hook saved in an older shim (v2, or v3 of another build), 0 if none. */
static uintptr_t old_shim_prev(const shell_t *s, uintptr_t at) {
    unsigned char a[CAVE_SIZE]; uint64_t prev = 0;
    size_t n = s->frame_end - at < sizeof(a) ? s->frame_end - at : sizeof(a);
    if (kernel_proc_copyout(s->pid, at, a, n)) return 0;
    if (n >= V2_MAGIC + 8 && !memcmp(a + V2_MAGIC, "NEOENBL2", 8)) memcpy(&prev, a + V2_PREV, 8);
    for (size_t i = SHIM_MAGIC - SHIM_PREV; !prev && i + 8 <= n; i++)       /* v3: data layout fixed from prev to magic */
        if (!memcmp(a + i, "NEOENBL3", 8)) memcpy(&prev, a + i - (SHIM_MAGIC - SHIM_PREV), 8);
    return prev >= s->frame && prev < s->frame_end - CAVE_SIZE ? (uintptr_t)prev : 0;
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
    if (s->target == s->cave && cave_has_shim(s->pid, s->cave)) return HOOK_ON;
    if (s->target == s->cave && old_shim_prev(s, s->target)) return HOOK_OLD;
    if (s->target == s->cave) return HOOK_BAD_CAVE;
    if (s->target >= s->frame && s->target < s->cave) return HOOK_OFF;
    return HOOK_NO_PATCH_SYSTEM;
}

/* One line for HOOK_BAD_CAVE: where the call points and what the cave holds, so a report shows whether the cave
   was unreadable, held another NeoEnabler build or other code. */
static void bad_cave_text(const shell_t *s, char *out, size_t cap) {
    unsigned char a[CAVE_SIZE]; char head[3 * 16 + 1] = "";
    size_t n = s->frame_end - s->cave < sizeof(a) ? s->frame_end - s->cave : sizeof(a);
    int rc = kernel_proc_copyout(s->pid, s->cave, a, n);
    int diff = -1, v2 = 0, v3 = 0;
    if (!rc) {
        for (size_t i = 0; i < 16 && i < n; i++) snprintf(head + 3 * i, 4, "%02x ", a[i]);
        for (size_t i = 0; i < sizeof(shim_code) && i < n; i++) {
            if (i >= SHIM_PREV && i < SHIM_MAGIC) continue;
            if (a[i] != shim_code[i]) { diff = (int)i; break; }
        }
        for (size_t i = 0; i + 8 <= n; i++) {
            if (!memcmp(a + i, "NEOENBL2", 8)) v2 = 1;
            if (!memcmp(a + i, "NEOENBL3", 8)) v3 = 1;
        }
    }
    snprintf(out, cap, "hook area: fw %x.%02x site %lx target %lx cave %lx frame %lx-%lx copyout rc %d "
             "first diff offset %d magic v2 %d v3 %d head %s",
             fw_major() >> 8, fw_major() & 0xff, (unsigned long)s->site, (unsigned long)s->target,
             (unsigned long)s->cave, (unsigned long)s->frame, (unsigned long)s->frame_end, rc, diff, v2, v3, head);
}

static const char *state_text(int st) {
    switch (st) {
    case HOOK_ON: return "enabled";
    case HOOK_OLD: return "older NeoEnabler hook";
    case HOOK_OFF: return "disabled";
    case HOOK_NO_PATCH_SYSTEM: return "ps-patch-system not loaded";
    case HOOK_BAD_CAVE: return "unknown code in the hook area";
    case HOOK_UNSUPPORTED_FW: return "unsupported firmware or ShellCore";
    default: return "getAppInfo call not found";
    }
}

/* Enables (shim in the cave, call redirected to it) or disables (call back to the ps-patch-system hook)
   with ShellCore stopped; every write is read back and the call is restored on failure. An older shim is
   replaced: the call goes back to the ps-patch-system hook it saved, then the new shim is installed. */
static int hook_set(int enable) {
    shell_t s;
    int st = hook_state(&s);
    if (st == enable) return 0;
    if (st != !enable && st != HOOK_OLD) return 3;
    uintptr_t old_prev = st == HOOK_OLD ? old_shim_prev(&s, s.target) : 0;
    if (kill(s.pid, SIGSTOP)) return 5;
    usleep(100000);
    int rc = 0;
    uintptr_t target;
    if (read_call(s.pid, s.site, &target) || target != s.target) rc = 6;
    else if (old_prev && write_call(s.pid, s.site, old_prev)) rc = 7;
    else if (old_prev && !enable) rc = 0;
    else if (enable) {
        if (old_prev) target = old_prev;
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

static int shim_live(shell_t *s) { return hook_state(s) == HOOK_ON; }

/* Launch questions from the installed shim. The shim posts a title ID and waits for answer and ack_seq. */
static int shim_write(const shell_t *s, size_t off, const void *data, size_t n) {
    unsigned char back[CAVE_SIZE];
    if (off + n > CAVE_SIZE || kernel_proc_copyin(s->pid, (void *)data, s->cave + off, n) ||
        kernel_proc_copyout(s->pid, s->cave + off, back, n) || memcmp(back, data, n)) return -1;
    return 0;
}

/* Pro mode for every PS4 game when the manager does not answer, and how long a launch waits for the answer in
   TSC cycles (0: never ask, as after Quit). */
static int shim_configure(const shell_t *s, int force_all, uint64_t wait) {
    unsigned char all = (unsigned char)!!force_all;
    return shim_write(s, SHIM_FORCE_ALL, &all, 1) || shim_write(s, SHIM_WAIT_CYCLES, &wait, 8) ? -1 : 0;
}

/* Latest question: sequence number and title ID. */
static int shim_question(const shell_t *s, uint32_t *seq, char title[10]) {
    unsigned char b[SHIM_ANSWER - SHIM_REQ_SEQ];
    if (kernel_proc_copyout(s->pid, s->cave + SHIM_REQ_SEQ, b, sizeof(b))) return -1;
    memcpy(seq, b, 4);
    memcpy(title, b + (SHIM_REQ_TITLE - SHIM_REQ_SEQ), 9); title[9] = 0;
    return 0;
}

/* Answer (bit 0: launch in Pro mode), then the acknowledgement the shim waits for. */
static int shim_answer(const shell_t *s, uint32_t seq, unsigned char answer) {
    return shim_write(s, SHIM_ANSWER, &answer, 1) || shim_write(s, SHIM_ACK_SEQ, &seq, 4) ? -1 : 0;
}

/* ---- Step hook: BeginAppMount2 GOT redirection for per-launch budget extension ---- */
#include "step_shim_data.h"

typedef struct {
    uint32_t fw;
    uint32_t call;          /* e8 call inside BeginAppMount2 */
    uint32_t import_;       /* PLT import stub */
    uint32_t got;           /* GOT entry for the import */
    uint32_t lib_wrap;      /* libkernel_sys target offset */
    uint32_t pad_start;     /* text padding area start */
    uint32_t pad_end;       /* text padding area end */
    uint32_t pad_cave;      /* first usable slot in padding */
    uint32_t validate_fn;   /* function prologue for validation */
    unsigned char prologue[20]; /* expected prologue bytes */
    unsigned char stub[6];     /* expected PLT stub bytes */
} step_layout;

static const step_layout step_layouts[] = {
    {0x13200000,
     0x6d7c11, 0x195b690, 0x2067458, 0x42850,
     0x19619ec, 0x1964000, 0x1962000, 0x6d6dc0,
     {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,
      0x53,0x48,0x81,0xec,0xe8,0x18,0,0},
     {0xff,0x25,0xc2,0xbd,0x70,0}},
};
#define STEP_LAYOUT_COUNT (sizeof step_layouts/sizeof step_layouts[0])

static const step_layout *step_find_layout(uint32_t fw) {
    for (unsigned i = 0; i < STEP_LAYOUT_COUNT; i++)
        if (step_layouts[i].fw == fw) return &step_layouts[i];
    return NULL;
}

typedef struct {
    uint32_t enabled, active, gate, seq, response_seq, command, phase;
    uint32_t category, result;
    char title[12];
    uint32_t blocked, refused, expired, dirty;
    char magic[8];
} step_mailbox;

typedef struct {
    pid_t pid;
    uintptr_t cave;       /* where the step shim code lives in ShellCore */
    uintptr_t base;       /* ShellCore mapbase */
    uint64_t size;
    uint64_t frame, frame_size;
    uint64_t lib_base;
    unsigned char saved_call[5];
    int attached;
    const step_layout *layout;
} step_hook_t;

static step_hook_t step;

static int step_proc_write(pid_t pid, uintptr_t addr, const void *data, size_t n) {
    unsigned char back[1024];
    if (n > sizeof(back) || kernel_proc_copyin(pid, (void *)data, addr, n) ||
        kernel_proc_copyout(pid, addr, back, n) || memcmp(back, data, n)) return -1;
    return 0;
}

static int step_validate(pid_t pid, uintptr_t base, uint64_t size, uint64_t lib_base, const step_layout *L) {
    unsigned char b[64]; uint64_t got_val = 0;
    if (size < L->got + 8) return -1;
    if (kernel_proc_copyout(pid, base + L->validate_fn, b, sizeof(L->prologue)) ||
        memcmp(b, L->prologue, sizeof(L->prologue))) return -1;
    if (kernel_proc_copyout(pid, base + L->import_, b, sizeof(L->stub)) ||
        memcmp(b, L->stub, sizeof(L->stub))) return -1;
    if (kernel_proc_copyout(pid, base + L->got, &got_val, 8) ||
        got_val != lib_base + L->lib_wrap) return -1;
    return 0;
}

static int step_retained(uintptr_t base, const unsigned char *bytes, size_t sz, const step_layout *L) {
    if (sz < sizeof(step_code) || memcmp(bytes, step_code, MS_ORIGINAL) ||
        memcmp(bytes + MS_MAGIC, step_code + MS_MAGIC, sizeof(step_code) - MS_MAGIC)) return 0;
    uint64_t original, cycles;
    uint32_t enabled, active, gate, dirty;
    memcpy(&original, bytes + MS_ORIGINAL, 8);
    memcpy(&cycles, bytes + MS_WAIT_CYCLES, 8);
    memcpy(&enabled, bytes + MS_ENABLED, 4);
    memcpy(&active, bytes + MS_ACTIVE, 4);
    memcpy(&gate, bytes + MS_GATE, 4);
    memcpy(&dirty, bytes + MS_DIRTY, 4);
    if (original != base + L->import_ || cycles != 100000000 || enabled || active || gate || dirty) return 0;
    for (size_t n = sizeof(step_code); n < sz; n++) if (bytes[n]) return 0;
    return 1;
}

static int step_find_cave(pid_t pid, uintptr_t base, uint64_t size, uintptr_t *out, const step_layout *L) {
    unsigned char bytes[sizeof(step_code)];
    *out = 0;
    if (size < L->pad_end + 16) return -1;
    for (uint64_t off = L->pad_start; off < L->pad_cave;) {
        size_t chunk = L->pad_cave - off;
        if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
        if (kernel_proc_copyout(pid, base + off, bytes, chunk)) return -1;
        for (size_t n = 0; n < chunk; n++) if (bytes[n]) return -1;
        off += chunk;
    }
    for (uint64_t at = base + L->pad_cave; at + sizeof(bytes) <= base + L->pad_end; at += sizeof(bytes)) {
        if (kernel_proc_copyout(pid, at, bytes, sizeof(bytes))) return -1;
        int zero = 1;
        for (size_t n = 0; n < sizeof(bytes); n++) if (bytes[n]) { zero = 0; break; }
        if (zero) { if (!*out) *out = at; }
        else if (!step_retained(base, bytes, sizeof(bytes), L)) return -1;
    }
    return *out ? 0 : -2;
}

static int step_install(uint32_t fw) {
    if (step.attached) return 0;
    const step_layout *L = step_find_layout(fw);
    if (!L) return -100;

    shell_t s; int st = hook_state(&s);
    if (st < HOOK_OFF) return -1;

    dynlib_obj_t main_mod = {0}, lib_mod = {0};
    uint32_t lib_handle = 0;
    if (kernel_dynlib_obj(s.pid, 0, &main_mod) ||
        kernel_dynlib_handle(s.pid, "libkernel_sys.sprx", &lib_handle) ||
        kernel_dynlib_obj(s.pid, lib_handle, &lib_mod)) return -2;

    step.pid = s.pid;
    step.base = main_mod.mapbase;
    step.size = main_mod.mapsize;
    step.frame = main_mod.eh_frame;
    step.frame_size = main_mod.eh_frame_size;
    step.lib_base = lib_mod.mapbase;
    step.layout = L;

    if (step_validate(s.pid, step.base, step.size, step.lib_base, L)) return -3;

    uintptr_t cave = 0;
    int res = step_find_cave(s.pid, step.base, step.size, &cave, L);
    if (res) return res == -2 ? -4 : -5;
    step.cave = cave;

    int64_t disp = (int64_t)cave - (int64_t)(step.base + L->call + 5);
    if (disp != (int32_t)disp) return -6;

    if (kernel_get_vmem_protection(s.pid, (step.base + L->call) & ~(uintptr_t)(PAGE - 1), PAGE) != 7 ||
        kernel_get_vmem_protection(s.pid, cave & ~(uintptr_t)(PAGE - 1), PAGE) != 7) return -7;

    unsigned char code[sizeof(step_code)];
    memcpy(code, step_code, sizeof(code));
    uint64_t original = step.base + L->import_;
    uint32_t enabled = 0;
    memcpy(code + MS_ORIGINAL, &original, 8);
    memcpy(code + MS_ENABLED, &enabled, 4);

    if (kill(s.pid, SIGSTOP)) return -8;
    usleep(100000);

    int rc = 0;
    if (kernel_proc_copyout(s.pid, step.base + L->call, step.saved_call, 5) ||
        step.saved_call[0] != 0xe8) rc = -9;

    if (!rc && step_proc_write(s.pid, cave, code, sizeof(code))) rc = -10;

    if (!rc) {
        unsigned char redirect[5] = {0xe8};
        int32_t rel = (int32_t)disp;
        memcpy(redirect + 1, &rel, 4);
        if (step_proc_write(s.pid, step.base + L->call, redirect, 5)) {
            step_proc_write(s.pid, step.base + L->call, step.saved_call, 5);
            rc = -11;
        } else {
            step.attached = 1;
        }
    }

    kill(s.pid, SIGCONT);
    return rc;
}

static int step_remove(void) {
    if (!step.attached || !step.layout) return 0;
    uint32_t zero = 0;
    step_proc_write(step.pid, step.cave + MS_ENABLED, &zero, 4);

    if (kill(step.pid, SIGSTOP)) return -1;
    usleep(100000);

    unsigned char current[5];
    int rc = 0;
    if (kernel_proc_copyout(step.pid, step.base + step.layout->call, current, 5)) rc = -2;
    else if (!memcmp(current, step.saved_call, 5)) step.attached = 0;
    else if (step_proc_write(step.pid, step.base + step.layout->call, step.saved_call, 5)) rc = -3;
    else step.attached = 0;

    kill(step.pid, SIGCONT);
    return rc;
}

static int step_read_mailbox(step_mailbox *out) {
    if (!step.attached) return -1;
    if (kernel_proc_copyout(step.pid, step.cave + MS_ENABLED, out, sizeof(*out)) ||
        memcmp(out->magic, "NEOMS001", 8)) return -1;
    return 0;
}

static int step_write_field(size_t off, const void *data, size_t n) {
    if (!step.attached) return -1;
    return step_proc_write(step.pid, step.cave + off, data, n);
}

static int step_enable(int on) {
    uint32_t val = !!on;
    return step_write_field(MS_ENABLED, &val, 4);
}

static int step_send_command(uint32_t cmd, uint32_t seq) {
    return step_write_field(MS_COMMAND, &cmd, 4) ||
           step_write_field(MS_RESPONSE_SEQ, &seq, 4) ? -1 : 0;
}

static int step_release_gate(void) {
    uint32_t zero = 0;
    return step_write_field(MS_GATE, &zero, 4) ||
           step_write_field(MS_DIRTY, &zero, 4) ? -1 : 0;
}
