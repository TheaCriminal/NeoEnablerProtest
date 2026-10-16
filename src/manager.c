/* GPL-3.0-only. NEO Enabler manager: installs the NEO launch hook in SceShellCore, shows a PS5 notification and
   stays resident with a web page on port 8765 that lists the installed games with NeoEnabler patches and switches
   them in the ps-patch-system settings. At every PS4 launch the hook asks the manager what the game needs: Pro mode
   and the guarded 8 GiB NEO budget follow the enabled patches, and the budget returns to the original after the
   game closes. Also installs the Media tab shortcut (NEOE00001) when missing. Hook logic in shellcore_hook.h. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <netinet/in.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include "shellcore_hook.h"
#include "legacy_v4.h"
#include "icon_data.h"
#include "panel_data.h"
#include "memory_control.h"
#include "gpu_control.h"
#include "settings.h"
#include "http_request.h"

#define PORT 8765
#define VERSION "NeoEnabler 0.3.1"
typedef struct {
    uint32_t app_id; uint64_t unknown1; char title_id[10]; uint32_t apptype; char unknown2[8];
    uint32_t attribute; /* param.sfo ATTRIBUTE as launched, including the hook change */
    char unknown3[0x2a];
} app_info_t;
#define ATTR_PS4_PRO 0x00800000
typedef struct { char useless[45]; char message[3075]; } notify_request_t;
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

/* Every action and its result go to the kernel log (kern.msgbuf), so Save Logs keeps them with the hook's lines. */
static void log_line(const char *fmt, ...) {
    char line[512];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    klog_printf("[NeoEnabler] %s\n", line);
}

static void notify(const char *msg) {
    log_line("%s", msg);
    notify_request_t req; memset(&req, 0, sizeof(req));
    strncpy(req.message, msg, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

#define TITLE_ID "NEOE00001"

int sceKernelLoadStartModule(const char *, size_t, const void *, uint32_t, void *, int *);


/* libSceAppInstUtil entry points. Loaded with the dependency chain used by ps5-app-dumper (each module
   loaded before the next one, then UserService/NetCtl/AppInstUtil initialized): loading AppInstUtil
   alone stops the payload on this loader. */
static struct {
    int (*exists)(const char *, uint8_t *);                  /* sceAppInstUtilAppExists */
    int (*install_dir)(const char *, const char *, void *); /* sceAppInstUtilAppInstallTitleDir (absent on fw 13.x) */
    int (*install_all)(void *);                              /* sceAppInstUtilAppInstallAll */
} appinst;

/* Returns 0 when ready, -1 missing library, -2 missing symbol, or the sceAppInstUtilInitialize error. */
static int appinst_open(void) {
    static int state = 1;
    if (state != 1) return state;
    static const char *libs[] = {"libSceIpmi.sprx", "libSceSsl.sprx", "libSceHttp2.sprx", "libSceSystemService.sprx",
                                 "libSceNetCtl.sprx", "libSceUserService.sprx", "libSceAppInstUtil.sprx"};
    int h[7];
    for (int i = 0; i < 7; i++) {
        char path[96];
        snprintf(path, sizeof(path), "/system/common/lib/%s", libs[i]);
        if ((h[i] = sceKernelLoadStartModule(path, 0, 0, 0, 0, 0)) < 0) return state = -1;
    }
    int (*user_init)(void *) = (void *)kernel_dynlib_resolve(-1, h[5], "j3YMu1MVNNo");  /* sceUserServiceInitialize */
    int (*netctl_init)(void) = (void *)kernel_dynlib_resolve(-1, h[4], "gky0+oaNM4k"); /* sceNetCtlInit */
    int (*init)(void) = (void *)kernel_dynlib_resolve(-1, h[6], "540lotO7oHE");        /* sceAppInstUtilInitialize */
    appinst.exists = (void *)kernel_dynlib_resolve(-1, h[6], "kUT4RpxclMQ");
    appinst.install_dir = (void *)kernel_dynlib_resolve(-1, h[6], "Wudg3Xe3heE");
    appinst.install_all = (void *)kernel_dynlib_resolve(-1, h[6], "+scQA5stvjs");
    if (!user_init || !netctl_init || !init || !appinst.exists || (!appinst.install_dir && !appinst.install_all)) return state = -2;
    uint32_t prio = 0x100;
    user_init(&prio);
    netctl_init();
    return state = init();
}

/* Media tab shortcut, same scheme as the other web panels on the console (category 65536 + deeplinkUri). */
static const char launcher_param[] =
    "{\n"
    "    \"titleId\": \"" TITLE_ID "\",\n"
    "    \"applicationCategoryType\": 65536,\n"
    "    \"deeplinkUri\": \"http://127.0.0.1:8765/\",\n"
    "    \"localizedParameters\": {\n"
    "        \"defaultLanguage\": \"en-US\",\n"
    "        \"en-US\": {\n"
    "            \"titleName\": \"NeoEnabler\"\n"
    "        }\n"
    "    }\n"
    "}\n";


static int same_file(const char *path, const void *data, size_t size) {
    struct stat st;
    if (stat(path, &st) || (size_t)st.st_size != size) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char *buf = malloc(size);
    int same = buf && fread(buf, 1, size, f) == size && !memcmp(buf, data, size);
    free(buf); fclose(f);
    return same;
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, size, f) == size && fflush(f) == 0 && fsync(fileno(f)) == 0;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}

#include "patch_sync.h"
#include "games.h"
static char patches_message[256] = "Not checked yet";

#define SETTINGS_PATH "/data/neo-settings"

/* 8 GiB is offered only on firmwares with a verified budget layout (mb_layouts): 13.20 tested, 13.60 pending testing. */
static uint32_t fw_raw_version(void) {
    uint32_t v = 0; size_t n = sizeof(v);
    sysctlbyname("machdep.upd_version", &v, &n, NULL, 0);
    return v;
}
static int memory_supported(void) { return mb_find_layout(fw_raw_version()) != NULL; }
static int memory_untested(void) { return fw_raw_version() == 0x13600000; }
/* 8 GiB can be used: supported firmware and kernel code access confirmed (hypervisor exploit enabled). */
static int memory_available(void) { mb_snapshot m; return memory_supported() && !mb_check(&m); }
/* GPU clock page and boost: firmware with the MP1 backend (13.20) and the driver answering a clock read. */
static int gpu_available(void) { unsigned mhz; return gpu_backend_supported() && !gpu_backend_read(&mhz); }

/* Advanced overrides: Pro mode for every PS4 game, and 8 GiB kept on. */
static int force_neo, force_8gib;

static void settings_load(void) {
    FILE *f = fopen(SETTINGS_PATH, "rb");
    if (!f) return;
    char buf[1024] = {0};
    size_t used = fread(buf, 1, sizeof(buf) - 1, f);
    if (ferror(f) || !feof(f)) { fclose(f); log_line("settings: unreadable or oversized file ignored"); return; }
    if (used) {
        neo_settings cfg;
        if (neo_settings_parse(buf, &cfg)) log_line("settings: invalid GPU values; GPU boost disabled");
        force_neo = cfg.force_neo;
        force_8gib = cfg.force_8gib && memory_supported();
        gpu_configure(cfg.gpu_enabled, cfg.gpu_mhz);
        if (cfg.gpu_enabled && !gpu_backend_supported()) log_line("settings: saved GPU boost ignored on unsupported firmware");
        if (cfg.force_8gib && !memory_supported())
            log_line("settings: force_8gib=1 ignored, 8 GiB is not supported on firmware %x.%02x",
                     (fw_raw_version() >> 24) & 0xff, (fw_raw_version() >> 16) & 0xff);
        if (cfg.memory && !cfg.force_8gib) log_line("settings: memory_8gib=1 of an older version ignored; 8 GiB now follows the game patches");
    }
    fclose(f);
}

static int settings_save(void) {
    neo_settings cfg = {.neo = 1, .memory = force_8gib, .gpu_enabled = gpu.enabled, .gpu_mhz = gpu.target,
                        .force_neo = force_neo, .force_8gib = force_8gib};
    char buf[128]; int n = neo_settings_format(&cfg, buf, sizeof(buf));
    if (n < 0 || write_file(SETTINGS_PATH ".tmp", buf, (size_t)n)) return -1;
    return rename(SETTINGS_PATH ".tmp", SETTINGS_PATH);
}

/* Returns 0 already installed, 1 installed now, <0 error. "Installed" means registered in the system
   (sceAppInstUtilAppExists) with the current param.json and icon: deleting the shortcut from the home
   screen makes the next start install it again. */
static int install_launcher(void) {
    const char *param_path = "/user/app/" TITLE_ID "/sce_sys/param.json", *icon_path = "/user/app/" TITLE_ID "/sce_sys/icon0.png";
    int err = appinst_open();
    if (err) { log_line("Media tab shortcut: appinst open rc=%x", err); return -3; }
    uint8_t exists = 0;
    if (!appinst.exists(TITLE_ID, &exists) && exists &&
        same_file(param_path, launcher_param, sizeof(launcher_param) - 1) && same_file(icon_path, icon0_png, sizeof(icon0_png))) return 0;
    if ((mkdir("/user/app/" TITLE_ID, 0755) && errno != EEXIST) || (mkdir("/user/app/" TITLE_ID "/sce_sys", 0755) && errno != EEXIST)) return -1;
    if (write_file(param_path, launcher_param, sizeof(launcher_param) - 1) || write_file(icon_path, icon0_png, sizeof(icon0_png))) return -2;
    err = appinst.install_dir ? appinst.install_dir(TITLE_ID, "/user/app/", 0) : appinst.install_all(0);
    log_line("Media tab shortcut: install %s rc=%x", TITLE_ID, err);
    return err ? -3 : 1;
}

/* Game name from the TITLE key of /system_data/priv/appmeta/<title id>/param.sfo; empty when not found. */
static void game_name(const char *title_id, char *out, size_t cap) {
    char path[64]; unsigned char sfo[8192]; size_t n;
    out[0] = 0;
    snprintf(path, sizeof(path), "/system_data/priv/appmeta/%.9s/param.sfo", title_id);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    n = fread(sfo, 1, sizeof(sfo), f); fclose(f);
    if (n < 20 || memcmp(sfo, "\0PSF", 4)) return;
    uint32_t keys, data, count;
    memcpy(&keys, sfo + 8, 4); memcpy(&data, sfo + 12, 4); memcpy(&count, sfo + 16, 4);
    for (uint32_t i = 0; i < count && 20 + (i + 1) * 16 <= n; i++) {
        const unsigned char *e = sfo + 20 + i * 16;
        uint16_t key_off; uint32_t len, data_off;
        memcpy(&key_off, e, 2); memcpy(&len, e + 4, 4); memcpy(&data_off, e + 12, 4);
        if (keys + key_off + 6 > n || memcmp(sfo + keys + key_off, "TITLE", 6)) continue;
        if (data + data_off + len > n) return;
        size_t m = len < cap ? len : cap - 1;
        memcpy(out, sfo + data + data_off, m); out[m] = 0;
        return;
    }
}

/* Copies s into out with the HTML special characters escaped. */
static void html_escape(const char *s, char *out, size_t cap) {
    size_t o = 0;
    for (; *s && o + 7 < cap; s++) {
        const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : *s == '\'' ? "&#39;" : NULL;
        if (r) { size_t l = strlen(r); memcpy(out + o, r, l); o += l; } else out[o++] = *s;
    }
    out[o] = 0;
}

/* Mode of the last open PS4 game listed by games_text: -1 none, 0 base, 1 PS4 Pro. */
static int running_pro = -1;

/* Open PS4 games and whether they run in PS4 Pro mode. */
static int games_text(char *out, size_t cap, int html) {
    running_pro = -1;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0; int len = 0, games = 0;
    out[0] = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n) return 0;
    unsigned char *buf = malloc(n);
    if (!buf || sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return 0; }
    for (size_t off = 0; off + sizeof(struct kinfo_proc) <= n;) {
        struct kinfo_proc *ki = (void *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) break;
        if (!strcmp(ki->ki_comm, "eboot.bin")) {
            app_info_t info = {0}; uint32_t h = 0;
            if (!sceKernelGetAppInfo(ki->ki_pid, &info) && info.title_id[0]) {
                /* The driver name gives the mode when the game loads a mode-specific driver. Some games (The Order: 1886 on 13.20) load
                   libSceGnmDriverCompat1 in both modes, so then the mode comes from the attribute. */
                int neo = !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverForNeoMode.sprx", &h);
                int ps4 = neo || !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriver.sprx", &h);
                if (!ps4 && !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverCompat1.sprx", &h)) {
                    ps4 = 1;
                    neo = (info.attribute & ATTR_PS4_PRO) != 0;
                }
                if (ps4) running_pro = neo;
                if (ps4 && len < (int)cap - 400) {
                    char name[128], esc[384];
                    game_name(info.title_id, name, sizeof(name));
                    if (html) html_escape(name, esc, sizeof(esc)); else snprintf(esc, sizeof(esc), "%s", name);
                    games++;
                    char memory[80]="DMEM query unavailable";int dmib[24];size_t dc=23,bytes=8;uint64_t dmem=0;
                    if(!sysctlnametomib("kern.app.dmem_size_pid",dmib,&dc)&&dc<=23){
                        dmib[dc++]=ki->ki_pid;
                        if(!sysctl(dmib,(unsigned)dc,&dmem,&bytes,NULL,0)&&bytes==8)
                            snprintf(memory,sizeof memory,"DMEM: %llu MiB",(unsigned long long)(dmem>>20));
                    }
                    len += snprintf(out + len, cap - len, html ? "<li><b>%s</b> (%.9s): %s; %s</li>" : "%s (%.9s): %s; %s\n",
                                    esc[0] ? esc : "Unknown game", info.title_id, neo ? "PS4 Pro (NEO)" : "PS4 base",memory);
                }
            }
        }
        off += (size_t)ki->ki_structsize;
    }
    free(buf);
    if (!games) snprintf(out, cap, html ? "<li>No PS4 game running</li>" : "No PS4 game running\n");
    return games;
}

/* A unique PS4 eboot is the only automatic clock target. Native games, mixed
 * game processes, failed scans and launcher processes never trigger boost. */
static int gpu_game_pid(void) {
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0;
    if (sysctl(mib, 4, NULL, &n, NULL, 0) || !n || n > 32 * 1024 * 1024) return -1;
    unsigned char *buf = malloc(n);
    if (!buf) return -1;
    if (sysctl(mib, 4, buf, &n, NULL, 0)) { free(buf); return -1; }
    int candidate = 0, count = 0, valid = 1; size_t off = 0;
    while (off + sizeof(struct kinfo_proc) <= n) {
        struct kinfo_proc *ki = (void *)(buf + off);
        if (ki->ki_structsize < (int)sizeof(*ki) || (size_t)ki->ki_structsize > n - off) { valid = 0; break; }
        if (!strcmp(ki->ki_comm, "eboot.bin")) {
            app_info_t info = {0}; uint32_t h = 0; count++;
            if (sceKernelGetAppInfo(ki->ki_pid, &info) || !info.title_id[0]) valid = 0;
            else if (!kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverForNeoMode.sprx", &h) ||
                     !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriver.sprx", &h) ||
                     !kernel_dynlib_handle(ki->ki_pid, "libSceGnmDriverCompat1.sprx", &h)) candidate = ki->ki_pid;
        }
        off += (size_t)ki->ki_structsize;
    }
    if (off != n || count > 1) valid = 0;
    free(buf); return valid ? candidate : -1;
}

static void send_all(int fd, const char *p, size_t n) {
    while (n) { ssize_t w = write(fd, p, n); if (w <= 0) return; p += w; n -= (size_t)w; }
}

static void reply(int fd, const char *status, const char *type, const char *body) {
    char head[256];
    int n = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, strlen(body));
    send_all(fd, head, (size_t)n); send_all(fd, body, strlen(body));
}

static void reply_bytes(int fd, const char *status, const char *type, const char *cache, const void *body, size_t n) {
    char head[320];
    int h = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: %s\r\nConnection: close\r\n\r\n",
                     status, type, n, cache);
    send_all(fd, head, (size_t)h); send_all(fd, body, n);
}

/* Growing text buffer for pages whose size depends on the games installed. */
typedef struct { char *p; size_t n, cap; } sbuf;
static void sb_printf(sbuf *b, const char *fmt, ...) {
    for (;;) {
        va_list ap; va_start(ap, fmt);
        int w = b->p ? vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap) : -1;
        va_end(ap);
        if (w >= 0 && (size_t)w < b->cap - b->n) { b->n += (size_t)w; return; }
        size_t cap = b->cap ? b->cap * 2 : 16384;
        if (w >= 0 && cap < b->n + (size_t)w + 1) cap = b->n + (size_t)w + 1024;
        char *p = realloc(b->p, cap);
        if (!p) return;
        b->p = p; b->cap = cap;
    }
}
static void sb_esc(sbuf *b, const char *s) {
    char tmp[1536];
    html_escape(s, tmp, sizeof tmp);
    sb_printf(b, "%s", tmp);
}

static const char page_head[] =
    "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>NeoEnabler</title><style>"
    ":root{color-scheme:dark;--bg:#080b12;--surface:#111722;--surface-2:#171f2d;--line:#263145;--text:#f4f7fb;"
    "--muted:#95a2b8;--blue:#55a7ff;--blue-strong:#147ee8;--green:#52d39a;--amber:#f2bd5d;--red:#ff6c79;--radius:14px}"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;background:radial-gradient(circle at 15% -10%,rgba(20,126,232,.17),transparent 34rem),var(--bg);"
    "color:var(--text);font:15px/1.45 system-ui,-apple-system,'Segoe UI',sans-serif}"
    "header{position:sticky;top:0;z-index:2;display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;"
    "padding:14px 22px;background:rgba(8,11,18,.94);border-bottom:1px solid var(--line)}"
    ".brand{display:flex;align-items:center;gap:12px}.mark{display:grid;place-items:center;width:38px;height:38px;border-radius:11px;"
    "background:linear-gradient(145deg,#147ee8,#74baff);font-weight:800}"
    "h1{font-size:19px;margin:0}h1 small{color:var(--muted);font-weight:400;font-size:13px;margin-left:8px}"
    "nav{display:flex;gap:8px}nav a{color:var(--muted);text-decoration:none;padding:8px 14px;border-radius:10px;border:1px solid var(--line)}"
    "nav a[aria-current=page]{color:var(--text);background:var(--surface-2);border-color:var(--blue)}"
    "main{width:min(1400px,100%);margin:0 auto;padding:20px 16px}"
    ".layout{display:grid;grid-template-columns:minmax(0,1fr) 320px;gap:18px;align-items:start}"
    "@media(max-width:900px){.layout{grid-template-columns:1fr}}"
    ".card,.game{background:rgba(17,23,34,.96);border:1px solid var(--line);border-radius:var(--radius);padding:16px}"
    "aside{display:grid;gap:14px}aside h2{font-size:15px;margin:0 0 8px}"
    ".game{display:grid;grid-template-columns:96px minmax(0,1fr);gap:16px;margin-bottom:14px}"
    ".game img{width:96px;height:96px;border-radius:12px;object-fit:cover;background:var(--surface-2)}"
    ".game h2{font-size:18px;margin:0}.game h2 span{color:var(--muted);font-weight:400;font-size:14px}"
    ".ver{color:var(--muted);font-size:13px;margin:2px 0 10px}"
    ".patches{list-style:none;margin:0;padding:0;display:grid;gap:8px}"
    ".patch{display:grid;grid-template-columns:22px minmax(0,1fr);gap:10px;padding:10px 12px;border:1px solid var(--line);"
    "border-radius:10px;background:var(--surface-2);cursor:pointer}"
    ".patch input{width:18px;height:18px;margin:2px 0 0;accent-color:var(--blue)}"
    ".patch:has(input:checked){border-color:var(--blue)}.patch:has(input:disabled){opacity:.5;cursor:default}"
    ".patch i{display:block;color:var(--muted);font-size:13px;margin-top:2px}"
    ".tag{display:inline-block;margin-left:8px;padding:1px 8px;border-radius:999px;font-size:11px;font-weight:600;"
    "color:var(--amber);border:1px solid rgba(242,189,93,.45);vertical-align:1px}"
    ".state{display:inline-block;padding:2px 10px;border-radius:999px;font-size:12px;font-weight:600;background:var(--surface-2)}"
    ".on{background:rgba(82,211,154,.18);color:var(--green)}.off{color:var(--muted)}"
    "button{font:inherit;color:var(--text);background:var(--surface-2);border:1px solid var(--line);border-radius:10px;"
    "padding:7px 13px;margin:8px 8px 0 0;cursor:pointer}button:hover{border-color:var(--blue)}button:disabled{opacity:.45;cursor:default}"
    "button.danger{background:#3a1f1d;border-color:#5c2c28}"
    "details summary{cursor:pointer;color:var(--muted);font-size:13px}details[open] summary{margin-bottom:8px}"
    "input[type=range]{width:100%;margin:20px 0}output{font-size:24px;font-weight:600}"
    ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}@media(max-width:600px){.grid{grid-template-columns:1fr}}"
    ".wide{grid-column:1/-1}.metric{font-size:32px;font-weight:600;margin:10px 0}.notice{margin:12px 0;color:var(--muted)}"
    ".msg{margin:0 0 14px;padding:10px 14px;border:1px solid var(--line);border-radius:10px;background:var(--surface)}"
    ".search{width:100%;min-height:42px;margin:0 0 14px;padding:0 14px;border:1px solid var(--line);border-radius:10px;"
    "background:#0c111a;color:var(--text);font:inherit}.search:focus{outline:2px solid var(--blue);outline-offset:1px}"
    ".game[hidden]{display:none}"
    "ul{margin:6px 0 0;padding-left:18px}small{color:var(--muted)}"
    "</style><script src=/panel.js></script></head><body>";

/* v4 hook still in ShellCore (forces NEO regardless of this manager's hook). */
static int v4_active(const shell_t *s) { return s->pid > 0 && s->frame && v4_state(s->pid, s->frame) == V4_PRESENT; }

static void page_header(sbuf *b, const char *active) {
    uint32_t fw_raw = fw_raw_version();
    sb_printf(b, "%s<header><div class=brand><div class=mark>N</div><h1>NeoEnabler <small>FW %x.%02x</small></h1></div>"
              "<nav aria-label='Pages'><a href=/%s>Games</a>%s%s%s</nav>"
              "<form method=post><button class=danger formaction=/quit>Quit</button></form></header>",
              page_head, (fw_raw >> 24) & 0xff, (fw_raw >> 16) & 0xff,
              !strcmp(active, "games") ? " aria-current=page" : "",
              gpu_available() ? "<a href=/gpu" : "", gpu_available() && !strcmp(active, "gpu") ? " aria-current=page" : "",
              gpu_available() ? ">GPU clock</a>" : "");
}

/* Games with NeoEnabler patches on the left (icon, name, title ID, version, every patch of that version with a
   checkbox); status, patch update, logs and the advanced overrides on the right. */
static void serve_page(int fd, const char *msg) {
    sbuf b = {0};
    page_header(&b, "games");
    sb_printf(&b, "<main>");
    if (msg) { sb_printf(&b, "<p class=msg>"); sb_esc(&b, msg); sb_printf(&b, "</p>"); }
    sb_printf(&b, "<div class=layout><section aria-label='Games'>");
    int n; game_t *g = games_scan(&n);
    size_t sl; char *settings = settings_read(&sl);
    int mem_ok = memory_available();
    if (n) sb_printf(&b, "<input type=search id=game-search class=search placeholder='Search by name or title ID' "
                         "autocomplete=off aria-label='Search games'><p id=no-match class=card hidden>No game matches the search.</p>");
    if (!n) sb_printf(&b, "<div class=card><b>No installed game with NeoEnabler patches</b><p><small>Games appear here when "
                          PS_DIR " has a patch for their installed version that needs PS4 Pro mode. Use Update Patches "
                          "to download the patches.</small></p></div>");
    for (int i = 0; i < n; i++) {
        sb_printf(&b, "<article class=game data-search='");
        sb_esc(&b, g[i].name);
        sb_printf(&b, " %s'><img src='/icon/%s' alt='' loading=lazy><div><h2>", g[i].tid, g[i].tid);
        sb_esc(&b, g[i].name[0] ? g[i].name : g[i].tid);
        sb_printf(&b, " <span>(%s)</span></h2><p class=ver>Version %s%s</p><ul class=patches>", g[i].tid, g[i].version,
                  g[i].update ? " (update installed)" : "");
        for (int k = 0; k < g[i].count; k++) {
            const gp_patch *p = &g[i].patches[k];
            int on = gp_setting(settings, sl, p->hash) == 1, blocked = p->needs_8gib && !mem_ok && !on;
            sb_printf(&b, "<li><label class=patch><input type=checkbox data-tid=%s data-hash=%s%s%s%s><span><b>",
                      g[i].tid, p->hash, p->resolution ? " data-res=1" : "", on ? " checked" : "", blocked ? " disabled" : "");
            sb_esc(&b, p->name);
            sb_printf(&b, "</b>%s", p->needs_8gib ? "<span class=tag>8 GiB</span>" : "");
            if (p->note[0]) { sb_printf(&b, "<i>"); sb_esc(&b, p->note); sb_printf(&b, "</i>"); }
            sb_printf(&b, "</span></label></li>");
        }
        sb_printf(&b, "</ul></div></article>");
    }
    free(settings); games_free(g, n);

    shell_t s; int st = hook_state(&s);
    if (st == HOOK_OFF && v4_active(&s)) st = HOOK_ON;
    mb_snapshot memory; mb_check(&memory);
    char running[1024]; games_text(running, sizeof(running), 1);
    char patches_esc[768]; html_escape(patches_message, patches_esc, sizeof(patches_esc));
    sb_printf(&b, "</section><aside>"
              "<div class=card><h2>Status</h2>"
              "<div>PS4 Pro mode <span class='state %s'>%s</span></div>"
              "<div style='margin-top:6px'>8 GiB memory <span class='state %s'>%s</span>%s</div>"
              "<p><small>Pro mode and 8 GiB follow the enabled patches of each game. 8 GiB returns to the original "
              "memory when the game closes.</small></p><b>Game running</b><ul>%s</ul></div>"
              "<div class=card><h2>Game patches</h2><p>%s</p><small>Downloads the patches from the NeoEnabler repository "
              "into " PS_DIR ". Changed files are backed up to " PS_BACKUP_ROOT " first.</small>"
              "<form method=post><button formaction=/patches/update>Update Patches</button></form></div>"
              "<div class=card><h2>Logs</h2><small>Kernel logs, including every NeoEnabler step.</small>"
              "<form method=post><a href=/logs target=_blank><button type=button>Show Logs</button></a>"
              "<button formaction=/logs/save>Save Logs</button><button formaction=/diagnostic/save>Save Diagnostic</button></form></div>"
              "<details class=card><summary>Advanced</summary>"
              "<form method=post><input type=hidden name=on value=%d><b>Pro mode for every PS4 game</b> "
              "<span class='state %s'>%s</span><br><small>Ignores the patch selection.</small><br>"
              "<button formaction=/advanced/neo>%s</button></form>"
              "<form method=post style='margin-top:12px'><input type=hidden name=on value=%d><b>Keep 8 GiB on</b> "
              "<span class='state %s'>%s</span><br><small>For every Pro mode launch, until turned off.</small><br>"
              "<button formaction=/advanced/memory%s>%s</button></form></details>"
              "<small style='text-align:center'>" VERSION "</small></aside></div></main></body></html>",
              st == HOOK_ON && running_pro == 1 ? "on" : "off",
              st != HOOK_ON ? state_text(st) : running_pro == 1 ? "enabled" : running_pro == 0 ? "disabled" : "ready",
              memory.state == MB_8GIB ? "on" : "off",
              memory.state == MB_8GIB ? "enabled" : memory.state == MB_UNKNOWN ? (mem_ok ? "unavailable" : "not supported") : "disabled",
              memory_untested() ? "<br><small>Firmware 13.60: 8 GiB pending testing, please report results</small>" : "",
              running, patches_esc,
              !force_neo, force_neo ? "on" : "off", force_neo ? "on" : "off", force_neo ? "Turn off" : "Turn on",
              !force_8gib, force_8gib ? "on" : "off", force_8gib ? "on" : "off", mem_ok ? "" : " disabled",
              force_8gib ? "Turn off" : "Turn on");
    if (b.p) reply_bytes(fd, "200 OK", "text/html; charset=utf-8", "no-store", b.p, b.n);
    else reply(fd, "500 Internal Server Error", "text/plain", "out of memory\n");
    free(b.p);
}

static void serve_gpu_status(int fd) {
    gpu_refresh(); char current[24] = "null", previous[24] = "null", detail[384], body[1024];
    size_t at = 0;
    for (const unsigned char *p = (const unsigned char *)gpu.message; *p && at + 3 < sizeof detail; p++) {
        if (*p == '"' || *p == '\\') detail[at++] = '\\';
        detail[at++] = *p < 32 ? ' ' : (char)*p;
    }
    detail[at] = 0;
    if (gpu.reading_valid) snprintf(current, sizeof current, "%u", gpu.actual);
    if (gpu.baseline_valid) snprintf(previous, sizeof previous, "%u", gpu.baseline);
    snprintf(body, sizeof body,
        "{\"supported\":%s,\"enabled\":%s,\"applied\":%s,\"state\":\"%s\","
        "\"current_mhz\":%s,\"target_mhz\":%u,\"previous_mhz\":%s,\"max_mhz\":%u,\"detail\":\"%s\"}",
        gpu_backend_supported() ? "true" : "false", gpu.enabled ? "true" : "false",
        gpu.applied ? "true" : "false", gpu_state_text(), current, gpu.target, previous, GPU_MAX_MHZ, detail);
    reply(fd, "200 OK", "application/json", body);
}

static void serve_gpu_page(int fd, const char *msg) {
    gpu_refresh();
    char current[32] = "Unavailable", previous[32] = "Not captured", fw[16];
    uint32_t raw = fw_raw_version();
    snprintf(fw, sizeof fw, "%x.%02x", (raw >> 24) & 255, (raw >> 16) & 255);
    if (gpu.reading_valid) snprintf(current, sizeof current, "%u MHz", gpu.actual);
    if (gpu.baseline_valid) snprintf(previous, sizeof previous, "%u MHz", gpu.baseline);
    const char *dis = gpu_backend_supported() ? "" : " disabled";
    sbuf hb = {0};
    page_header(&hb, "gpu");
    static char body[16384];
    snprintf(body, sizeof body,
        "%s<main>"
        "%s%s%s<div class=grid>"
        "<div class=card><b>Current GPU clock</b><div class=metric id=gpu-current>%s</div>"
        "<small>Live SMU reading. Updates every two seconds.</small></div>"
        "<div class=card><b>Clock boost</b> <span class='state %s' id=gpu-state>%s</span>"
        "<p id=gpu-detail>%s</p><small>Previous clock: <span id=gpu-previous>%s</span></small></div>"
        "<div class='card wide'><form method=post><label for=gpu-target><b>Target GPU clock</b></label>"
        "<p><output id=gpu-value for=gpu-target>%u</output> MHz</p>"
        "<input type=range id=gpu-target name=mhz min=%u max=%u step=50 value=%u%s>"
        "<div><button formaction=/gpu/enable%s>Apply boost</button>"
        "<button formaction=/gpu/disable%s>Disable boost</button></div></form>"
        "<p class=notice>Maximum allowed target: 2200 MHz. Short gameplay testing has passed. "
        "Actual frequency may be lower. Power and thermal controls remain active.</p>"
        "<small>Applies to PS4 games in base or NEO mode. A saved target is requested on the next PS4 launch. "
        "Disable restores the clock captured for the running game. Use Save Settings to keep your choice.</small>"
        "<form method=post><input type=hidden name=view value=gpu><button formaction=/settings/save>Save Settings</button></form>"
        "%s</div></div><p><small>GPU boost is independent of NEO and 8 GiB. Quitting restores the captured GPU clock.<br>"
        VERSION "</small></p></main>"
        "<script>(function(){const r=document.getElementById('gpu-target'),o=document.getElementById('gpu-value');"
        "r.addEventListener('input',()=>o.textContent=r.value);"
        "async function refresh(){try{const x=await fetch('/gpu/status',{cache:'no-store'});if(!x.ok)return;const s=await x.json();"
        "document.getElementById('gpu-current').textContent=s.current_mhz===null?'Unavailable':s.current_mhz+' MHz';"
        "const b=document.getElementById('gpu-state');b.textContent=s.state;"
        "b.className='state '+(s.applied&&s.state==='enabled'?'on':s.state==='disabled'?'off':'unk');"
        "document.getElementById('gpu-previous').textContent=s.previous_mhz===null?'Not captured':s.previous_mhz+' MHz';"
        "document.getElementById('gpu-detail').textContent=s.detail;"
        "}catch(e){document.getElementById('gpu-current').textContent='Disconnected';}}window.neoGpuTimer=setInterval(refresh,2000);})();</script>"
        "</body></html>",
        hb.p ? hb.p : "", msg ? "<p class=msg>" : "", msg ? msg : "", msg ? "</p>" : "", current,
        gpu.applied && !gpu.blocked ? "on" : gpu.enabled ? "unk" : "off", gpu_state_text(), gpu.message, previous,
        gpu.target, GPU_MIN_MHZ, GPU_MAX_MHZ, gpu.target, dis, dis, dis,
        gpu_backend_supported() ? "" : "<p>GPU clock control is available on firmware 13.20 only.</p>");
    free(hb.p);
    reply(fd, "200 OK", "text/html; charset=utf-8", body);
}

#define DIAG_PATH "/data/neo-diagnostic.jsonl"

static size_t generate_diagnostic(char *buf, size_t cap) {
    size_t len = 0;
    unsigned records = 0;
    uint32_t fw = 0; size_t fw_size = 4; int fw_rc, fw_error;
    errno = 0; fw_rc = sysctlbyname("machdep.upd_version", &fw, &fw_size, NULL, 0); fw_error = errno;
    records++;
    len += snprintf(buf + len, cap - len,
        "{\"format\":\"neo-diagnostic-v1\",\"record\":\"session\","
        "\"firmware_raw\":\"0x%08x\",\"firmware_rc\":%d,\"firmware_errno\":%d,\"firmware_bytes\":%zu,"
        "\"sdk_raw\":\"0x%08x\",\"kernel_data_base\":\"0x%016lx\","
        "\"scope\":\"metadata_only\",\"patches\":false,\"snapshot_atomic\":false}\n",
        fw, fw_rc, fw_error, fw_size, kernel_get_fw_version(), (uint64_t)KERNEL_ADDRESS_DATA_BASE);

    /* Self process */
    records++;
    len += snprintf(buf + len, cap - len,
        "{\"format\":\"neo-diagnostic-v1\",\"record\":\"process\",\"pid\":%d,\"kind\":\"native_payload\","
        "\"title_id\":\"\",\"app_info_rc\":-1,\"app_info_errno\":0,"
        "\"neo_driver_rc\":-1,\"base_driver_rc\":-1,\"compat_driver_rc\":-1,\"mode\":\"native\"}\n",
        (int)getpid());

    /* Enumerate games */
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; size_t n = 0;
    unsigned game_count = 0; int complete = 0, scan_rc = -1, scan_error = 0;
    errno = 0; scan_rc = sysctl(mib, 4, NULL, &n, NULL, 0); scan_error = errno;
    if (!scan_rc && n && n <= 32 * 1024 * 1024 - 0x10000) {
        n += 0x10000; unsigned char *b = malloc(n);
        if (b) {
            errno = 0; scan_rc = sysctl(mib, 4, b, &n, NULL, 0); scan_error = errno;
            if (!scan_rc) {
                size_t off = 0;
                while (off + sizeof(struct kinfo_proc) <= n) {
                    struct kinfo_proc *k = (void *)(b + off);
                    if (k->ki_structsize < (int)sizeof(*k) || (size_t)k->ki_structsize > n - off) break;
                    if (!strcmp(k->ki_comm, "eboot.bin") && game_count < 16 && len + 1024 < cap) {
                        app_info_t app = {0}; errno = 0;
                        int app_rc = sceKernelGetAppInfo(k->ki_pid, &app); int app_error = errno;
                        char title[11] = {0}; if (!app_rc) memcpy(title, app.title_id, 10);
                        uint32_t h = 0;
                        int neo = kernel_dynlib_handle(k->ki_pid, "libSceGnmDriverForNeoMode.sprx", &h);
                        int base = kernel_dynlib_handle(k->ki_pid, "libSceGnmDriver.sprx", &h);
                        int compat = kernel_dynlib_handle(k->ki_pid, "libSceGnmDriverCompat1.sprx", &h);
                        const char *mode = !neo ? "neo" : !base ? "base" : !compat ? "compat_unknown" : "unknown";
                        records++;
                        len += snprintf(buf + len, cap - len,
                            "{\"format\":\"neo-diagnostic-v1\",\"record\":\"process\",\"pid\":%d,\"kind\":\"game\","
                            "\"title_id\":\"%s\",\"app_info_rc\":%d,\"app_info_errno\":%d,"
                            "\"neo_driver_rc\":%d,\"base_driver_rc\":%d,\"compat_driver_rc\":%d,\"mode\":\"%s\"}\n",
                            (int)k->ki_pid, title, app_rc, app_error, neo, base, compat, mode);
                        /* DMEM/FMEM queries */
                        static const char *nodes[] = {"kern.app.dmem_size_pid", "kern.app.fmem_size_pid", "kern.app.fmem_avail_pid"};
                        for (int q = 0; q < 3 && len + 512 < cap; q++) {
                            int qmib[24]; size_t qc = 23, qsz = 8; uint64_t qval = 0;
                            errno = 0; int qrc = sysctlnametomib(nodes[q], qmib, &qc);
                            if (!qrc && qc <= 23) { qmib[qc++] = k->ki_pid; qrc = sysctl(qmib, (unsigned)qc, &qval, &qsz, NULL, 0); }
                            else qrc = -1;
                            int qerr = errno;
                            records++;
                            if (!qrc && qsz == 8)
                                len += snprintf(buf + len, cap - len,
                                    "{\"format\":\"neo-diagnostic-v1\",\"record\":\"query\",\"pid\":%d,\"node\":\"%s\","
                                    "\"rc\":%d,\"errno\":%d,\"returned_bytes\":%zu,\"value_hex\":\"0x%016lx\"}\n",
                                    (int)k->ki_pid, nodes[q], qrc, qerr, qsz, qval);
                            else
                                len += snprintf(buf + len, cap - len,
                                    "{\"format\":\"neo-diagnostic-v1\",\"record\":\"query\",\"pid\":%d,\"node\":\"%s\","
                                    "\"rc\":%d,\"errno\":%d,\"returned_bytes\":%zu,\"value_hex\":null}\n",
                                    (int)k->ki_pid, nodes[q], qrc, qerr, qsz);
                        }
                        game_count++;
                    }
                    off += (size_t)k->ki_structsize;
                }
                complete = off == n;
            }
            free(b);
        } else { scan_rc = -1; scan_error = ENOMEM; }
    } else if (!scan_rc) { scan_rc = -1; scan_error = EOVERFLOW; }

    len += snprintf(buf + len, cap - len,
        "{\"format\":\"neo-diagnostic-v1\",\"record\":\"end\",\"records_before_end\":%u,"
        "\"scan_complete\":%s,\"game_count\":%u,\"scan_rc\":%d,\"scan_errno\":%d}\n",
        records, complete ? "true" : "false", game_count, scan_rc, scan_error);

    return len;
}

static void serve_diagnostic(int fd) {
    static char buf[16384];
    generate_diagnostic(buf, sizeof(buf));
    reply(fd, "200 OK", "application/x-ndjson; charset=utf-8", buf);
}

static void serve_diagnostic_save(int fd) {
    static char buf[16384];
    generate_diagnostic(buf, sizeof(buf));
    int ok = !write_file(DIAG_PATH, buf, strlen(buf));
    char msg[256];
    snprintf(msg, sizeof(msg), ok ? "Diagnostic saved to " DIAG_PATH : "Failed to write " DIAG_PATH);
    notify(msg);
    serve_page(fd, msg);
}

#define LOGS_PATH "/data/neo-logs.txt"
#define LOGS_MAX (1024 * 1024)

static void serve_logs_show(int fd) {
    size_t len = LOGS_MAX;
    char *buf = malloc(len);
    if (!buf) { reply(fd, "500 Internal Server Error", "text/plain", "out of memory\n"); return; }
    int rc = sysctlbyname("kern.msgbuf", buf, &len, NULL, 0);
    if (rc || !len) { free(buf); reply(fd, "500 Internal Server Error", "text/plain", "kern.msgbuf read failed\n"); return; }
    buf[len < LOGS_MAX ? len : LOGS_MAX - 1] = '\0';
    reply(fd, "200 OK", "text/plain; charset=utf-8", buf);
    free(buf);
}

static void serve_logs_save(int fd) {
    size_t len = LOGS_MAX;
    char *buf = malloc(len);
    char msg[256];
    if (!buf) {
        snprintf(msg, sizeof(msg), "Failed to allocate log buffer");
        notify(msg); serve_page(fd, msg); return;
    }
    int rc = sysctlbyname("kern.msgbuf", buf, &len, NULL, 0);
    if (rc || !len) {
        snprintf(msg, sizeof(msg), "Failed to read kern.msgbuf (rc=%d, errno=%d)", rc, errno);
        free(buf); notify(msg); serve_page(fd, msg); return;
    }
    int ok = !write_file(LOGS_PATH, buf, len);
    free(buf);
    snprintf(msg, sizeof(msg), ok ? "Logs saved to " LOGS_PATH " (%zu bytes)" : "Failed to write " LOGS_PATH, len);
    notify(msg); serve_page(fd, msg);
}

static int do_set(int enable, char *msg, size_t cap, int show) {
    int rc = hook_set(enable);
    shell_t s; int st = hook_state(&s);
    if (!enable && !rc && (s.pid <= 0 || !s.frame || v4_remove(s.pid, s.frame))) rc = 9;
    if (!rc) snprintf(msg, cap, "NeoEnabler %s", enable ? "enabled" : "disabled");
    else snprintf(msg, cap, "NeoEnabler: failed to %s (rc=%d, state: %s)", enable ? "enable" : "disable", rc, state_text(st));
    if (show) notify(msg); else log_line("%s", msg);
    return rc;
}

/* State after an action: NEO hook, legacy v4 hook, actual 8 GiB budget and the requested one. */
static void log_state(void) {
    shell_t s; int st = hook_state(&s);
    if (st == HOOK_BAD_CAVE) { char d[400]; bad_cave_text(&s, d, sizeof(d)); log_line("%s", d); }
    mb_snapshot m; mb_check(&m);
    log_line("state: NEO hook %s, v4 hook %s, 8 GiB %s (requested %s): %s", state_text(st),
             v4_active(&s) ? "present" : "absent",
             m.state == MB_8GIB ? "enabled" : m.state == MB_UNKNOWN ? "unavailable" : "disabled",
             memory_target ? "on" : "off", m.detail);
    log_line("state: GPU %s, requested %u MHz, SMU %s%u MHz, owner PID %d: %s",
             gpu_state_text(), gpu.target, gpu.reading_valid ? "" : "unavailable/last=", gpu.actual, gpu.owner_pid, gpu.message);
}

static void patches_update(void) {
    ps_result r;
    patch_sync(&r);
    snprintf(patches_message, sizeof(patches_message), "%s", r.message);
    notify(r.message);
}

static void gpu_tick(void) {
    static unsigned previous_actions;
    static int previous_result = 9999;
    int result = gpu_poll(gpu_game_pid());
    if (result != previous_result || gpu.actions != previous_actions) {
        log_line("GPU: %s (result %d)", gpu.message, result);
        previous_actions = gpu.actions; previous_result = result;
    }
}

/* Launch questions. The hook posts the title ID of every PS4 launch and waits up to LAUNCH_WAIT_S seconds; the
   manager answers Pro mode when an enabled patch of that game needs it, after switching the 8 GiB budget to what
   the launch needs (the kernel reads it when it creates the game process). 8 GiB switched on for a launch returns
   to the original budget once the game has closed. */
#define LAUNCH_WAIT_S 5
static shell_t live;
static int live_ok;
static uint32_t last_seq;
static uint64_t tsc_hz = 1600000000ULL;
static struct { int active, needs; uint32_t seq; char tid[10]; time_t since; } pending;
static struct { int active, seen_game; char tid[10]; time_t since; } auto8;

static uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static uint64_t tsc_rate(void) {
    struct timespec a, c;
    clock_gettime(CLOCK_MONOTONIC, &a); uint64_t t0 = rdtsc();
    usleep(100000);
    clock_gettime(CLOCK_MONOTONIC, &c); uint64_t t1 = rdtsc();
    double sec = (double)(c.tv_sec - a.tv_sec) + (double)(c.tv_nsec - a.tv_nsec) / 1e9;
    return sec > 0.05 && t1 > t0 ? (uint64_t)((double)(t1 - t0) / sec) : 1600000000ULL;
}

/* Called after the hook changes and every two seconds: questions are only read from a live hook. */
static void launch_attach(int ask) {
    int was = live_ok;
    live_ok = shim_live(&live);
    if (!live_ok) return;
    if (!was) {
        char tid[10];
        if (shim_question(&live, &last_seq, tid)) { live_ok = 0; return; }   /* earlier questions are not ours */
    }
    if (!was || ask >= 0) shim_configure(&live, force_neo, ask == 0 ? 0 : tsc_hz * LAUNCH_WAIT_S);
}

static void launch_poll(void) {
    if (!live_ok) return;
    if (!pending.active) {
        uint32_t seq; char tid[10];
        if (shim_question(&live, &seq, tid) || seq == last_seq) return;
        last_seq = seq;
        pending.active = 1; pending.seq = seq; pending.since = time(NULL);
        snprintf(pending.tid, sizeof pending.tid, "%s", tid);
        pending.needs = game_needs(tid);
        if (force_neo) pending.needs |= NEEDS_PRO;
        log_line("launch: %s needs %s%s", tid, pending.needs & NEEDS_PRO ? "Pro mode" : "base mode",
                 pending.needs & NEEDS_8GIB ? " and 8 GiB" : "");
    }
    int pro = (pending.needs & NEEDS_PRO) != 0, ok = 1;
    int mem_ok = memory_available();
    int want8 = force_8gib || ((pending.needs & NEEDS_8GIB) && mem_ok);
    /* the budget only applies to Pro mode processes */
    if (pro && mem_ok) {
        int rc = memory_request(want8);
        ok = rc == 0;
        if (!ok && time(NULL) - pending.since < LAUNCH_WAIT_S - 1) return;   /* e.g. the previous game still closing */
        if (!ok) {
            mb_snapshot m;   /* keep the budget as it is instead of switching it later, after this game */
            if (!mb_check(&m)) memory_target = m.state == MB_8GIB;
            log_line("launch: %s: memory not switched (%s)", pending.tid, memory_message);
        }
        else if (want8 && !force_8gib) {
            auto8.active = 1; auto8.seen_game = 0; auto8.since = time(NULL);
            snprintf(auto8.tid, sizeof auto8.tid, "%s", pending.tid);
        } else if (!want8) auto8.active = 0;
    }
    shim_answer(&live, pending.seq, (unsigned char)pro);
    pending.active = 0;
    log_line("launch: %s starts in %s mode%s", pending.tid, pro ? "Pro" : "base",
             pro && mem_ok && ok ? (want8 ? " with 8 GiB" : " with the original memory") : "");
}

/* 8 GiB switched on for a launch goes back to the original budget when no game runs any more. */
static void launch_tick(void) {
    if (!auto8.active || force_8gib) return;
    int games = mb_games();
    if (games > 0) { auto8.seen_game = 1; return; }
    if (games < 0 || (!auto8.seen_game && time(NULL) - auto8.since < 60)) return;
    auto8.active = 0;
    memory_request(0);
    log_line("8 GiB: %s closed: %s", auto8.tid, memory_message);
}

/* Value of key in an x-www-form-urlencoded body, letters, digits and '.' only. */
static int form_value(const char *body, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, kl) || p[kl] != '=') continue;
        const char *v = p + kl + 1; size_t n = strcspn(v, "&");
        if (!n || n >= cap) return -1;
        for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)v[i]) && v[i] != '.') return -1;
        memcpy(out, v, n); out[n] = 0;
        return 0;
    }
    return -1;
}

static void serve_icon(int fd, const char *tid) {
    char path[96]; size_t n;
    if (!game_tid(tid)) { reply(fd, "404 Not Found", "text/plain", "not found\n"); return; }
    snprintf(path, sizeof path, "/user/appmeta/%s/icon0.png", tid);
    char *png = ps_read(path, 4 << 20, &n);
    if (!png) { reply(fd, "404 Not Found", "text/plain", "not found\n"); return; }
    reply_bytes(fd, "200 OK", "image/png", "max-age=86400", png, n);
    free(png);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    int srv = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(PORT); addr.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    char msg[256];
    if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof(addr)) || listen(srv, 4)) {
        snprintf(msg, sizeof(msg), "Web panel port %d unavailable; nothing changed", PORT);
        notify(msg); puts(msg);
        if(srv>=0)close(srv);
        return 1;
    }
    log_line("%s started", VERSION);
    settings_load();
    log_line("settings: force_neo=%d force_8gib=%d gpu_clock_enabled=%d gpu_clock_mhz=%u",
             force_neo, force_8gib, gpu.enabled, gpu.target);
    do_set(1, msg, sizeof(msg), 0);
    tsc_hz = tsc_rate();
    launch_attach(1);
    log_line("launch: questions %s (TSC %llu Hz, wait %d s)", live_ok ? "enabled" : "unavailable, hook not installed",
             (unsigned long long)tsc_hz, LAUNCH_WAIT_S);
    if (force_8gib) memory_request(1);
    int memory_previous_result=memory_poll();
    log_line("8 GiB: %s", memory_message);
    gpu_tick();
    int li = install_launcher();
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "\nWeb panel: port %d%s", PORT,
             li == 1 ? "\nShortcut installed in the Media tab" : li < 0 ? "\nFailed to install the Media tab shortcut" : "");
    notify(msg); puts(msg);
    log_state();
    patches_update();
    time_t last_memory_poll=time(NULL);
    for (;;) {
        launch_poll();
        time_t now=time(NULL);
        if(now<last_memory_poll||now-last_memory_poll>=2){
            launch_attach(-1);
            launch_tick();
            gpu_tick();
            int memory_result=memory_poll();
            if(memory_result!=memory_previous_result){
                if(memory_result==0||memory_result==5||memory_result==6)notify(memory_message);
                else log_line("8 GiB: %s (result %d)",memory_message,memory_result);
                log_state();
            }
            memory_previous_result=memory_result;last_memory_poll=now;
        }
        /* short wait: launch questions are answered between requests */
        fd_set ready;FD_ZERO(&ready);FD_SET(srv,&ready);struct timeval wait={0,pending.active?2000:20000};
        int selected=select(srv+1,&ready,NULL,NULL,&wait);
        if(selected<=0)continue;
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;
        struct timeval tv = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char req[4096], *form_body = NULL;
        if (neo_http_read(fd, req, sizeof req, &form_body)) {
            reply(fd, "400 Bad Request", "text/plain", "Invalid or incomplete request\n"); close(fd); continue;
        }
        char method[8] = "", path[64] = "";
        sscanf(req, "%7s %63s", method, path);
        int post = !strcmp(method, "POST");
        if (post && !strcmp(path, "/patches/set")) {
            char tid[16], hash[16], on[4], pmsg[256];
            if (form_value(form_body, "tid", tid, sizeof tid) || form_value(form_body, "hash", hash, sizeof hash) ||
                form_value(form_body, "on", on, sizeof on) || (strcmp(on, "0") && strcmp(on, "1"))) {
                reply(fd, "400 Bad Request", "text/plain", "Invalid patch request\n");
            } else if (game_set_patch(tid, hash, on[0] == '1', memory_available(), pmsg, sizeof pmsg)) {
                reply(fd, "409 Conflict", "text/plain; charset=utf-8", pmsg);
            } else reply(fd, "200 OK", "text/plain; charset=utf-8", pmsg);
        } else if (post && (!strcmp(path, "/advanced/neo") || !strcmp(path, "/advanced/memory"))) {
            char on[4]; int value = !form_value(form_body, "on", on, sizeof on) && !strcmp(on, "1");
            const char *amsg;
            if (!strcmp(path, "/advanced/neo")) {
                force_neo = value; launch_attach(1);
                amsg = value ? "Pro mode for every PS4 game: on" : "Pro mode for every PS4 game: off (follows the game patches)";
            } else if (!memory_available()) {
                amsg = "8 GiB needs firmware 13.20 or 13.60 with the hypervisor exploit enabled; nothing changed";
            } else {
                force_8gib = value;
                if (value) memory_request(1); else if (!auto8.active) memory_request(0);
                amsg = value ? "Keep 8 GiB on: on" : "Keep 8 GiB on: off (8 GiB follows the game patches)";
            }
            int saved = !settings_save();
            log_line("panel: %s%s", amsg, saved ? "" : " (settings not saved)");
            notify(amsg); log_state(); serve_page(fd, amsg);
        } else if (post && (!strcmp(path, "/gpu/enable") || !strcmp(path, "/gpu/disable"))) {
            int enable = !strcmp(path, "/gpu/enable"); unsigned target = gpu.target;
            if (enable && !gpu_available()) {     /* disable stays allowed: it restores the captured clock */
                reply(fd, "404 Not Found", "text/plain", "GPU clock control is not available on this console\n");
            } else if (enable && gpu_form_mhz(form_body, &target) != 1) {
                reply(fd, "400 Bad Request", "text/plain", "Choose a GPU target from 1800 to 2200 MHz in 50 MHz steps\n");
            } else {
                int rc = gpu_request(enable, target, gpu_game_pid());
                if (rc == -1 && enable) snprintf(gpu.message, sizeof gpu.message, "GPU clock request unavailable or failed; check firmware and SMU access");
                log_line("panel: GPU enable=%d target=%u rc=%d", enable, target, rc);
                notify(gpu.message); log_state(); serve_gpu_page(fd, gpu.message);
            }
        } else if (post && !strcmp(path, "/patches/update")) {
            log_line("panel: game patches update requested");
            patches_update();
            serve_page(fd, patches_message);
        } else if (post && !strcmp(path, "/settings/save")) {
            int ok = !settings_save();
            const char *smsg = ok ? "Settings saved" : "Failed to save settings";
            log_line("panel: save settings force_neo=%d force_8gib=%d gpu_clock_enabled=%d gpu_clock_mhz=%u",
                     force_neo, force_8gib, gpu.enabled, gpu.target);
            notify(smsg);
            if (!strcmp(form_body, "view=gpu") && gpu_available()) serve_gpu_page(fd, smsg); else serve_page(fd, smsg);
        } else if (post && !strcmp(path, "/quit")) {
            if (gpu_request(0, gpu.target, gpu_game_pid())) {
                notify(gpu.message); serve_gpu_page(fd, "Clock restoration failed. Retry Disable before quitting."); close(fd); continue;
            }
            reply(fd, "200 OK", "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><title>NeoEnabler</title>"
                  "<body style='background:#111418;color:#e8eaed;font-family:system-ui'>Manager stopped. Games no longer ask for Pro mode "
                  "or 8 GiB until the manager runs again. GPU clock restore requested.</body>");
            close(fd);
            launch_attach(0);
            notify("NeoEnabler: panel stopped (per-game Pro mode and 8 GiB stopped, GPU boost stopped)");
            log_state();
            break;
        } else if (!strcmp(path, "/status")) {
            shell_t s; int st = hook_state(&s);
            char games[1024]; games_text(games, sizeof(games), 0);
            char body[3200];mb_snapshot memory;mb_check(&memory);gpu_refresh();
            snprintf(body, sizeof(body), "build: " VERSION "\nmemory_8gib: %s\nmemory_requested: %s\nmemory_detail: %s\nmemory_last_result: %s\nhook: %s\nv4 hook: %s\nfw: %x site: %lx target: %lx cave: %lx eh_frame: %lx-%lx attr: 0x%x four_args: %d\n%s",
                     memory.state==MB_8GIB?"enabled":memory.state==MB_UNKNOWN?"unavailable":"disabled",
                     memory_target?"enabled":"disabled",memory.detail,memory_message,
                     state_text(st), v4_active(&s) ? "present" : "absent", fw_major(), s.site, s.target, s.cave, s.frame, s.frame_end,
                     s.attr_off, s.four_args, games);
            size_t used = strlen(body);
            snprintf(body + used, sizeof body - used,
                     "gpu_clock: %s\ngpu_clock_enabled: %d\ngpu_target_mhz: %u\ngpu_actual_mhz: %s%u\ngpu_detail: %s\n",
                     gpu_state_text(), gpu.enabled, gpu.target, gpu.reading_valid ? "" : "unavailable/last=", gpu.actual, gpu.message);
            used = strlen(body);
            snprintf(body + used, sizeof body - used,
                     "force_neo: %d\nforce_8gib: %d\nlaunch_questions: %s\nlast_question: %u\n8gib_for_launch: %s\n",
                     force_neo, force_8gib, live_ok ? "enabled" : "unavailable", last_seq, auto8.active ? auto8.tid : "none");
            reply(fd, "200 OK", "text/plain; charset=utf-8", body);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/panel.js")) {
            char script[sizeof panel_js + 1];
            memcpy(script, panel_js, sizeof panel_js); script[sizeof panel_js] = 0;
            reply(fd, "200 OK", "application/javascript; charset=utf-8", script);
        } else if (!strcmp(method, "GET") && !strncmp(path, "/icon/", 6)) {
            serve_icon(fd, path + 6);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/gpu")) {
            if (gpu_available()) serve_gpu_page(fd, NULL);
            else serve_page(fd, "GPU clock control is not available on this console");
        } else if (!strcmp(method, "GET") && !strcmp(path, "/gpu/status")) {
            if (gpu_available()) serve_gpu_status(fd);
            else reply(fd, "404 Not Found", "text/plain", "not found\n");
        } else if (!strcmp(method, "GET") && !strcmp(path, "/logs")) {
            serve_logs_show(fd);
        } else if (post && !strcmp(path, "/logs/save")) {
            serve_logs_save(fd);
        } else if (post && !strcmp(path, "/diagnostic/save")) {
            serve_diagnostic_save(fd);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/diagnostic")) {
            serve_diagnostic(fd);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/")) {
            serve_page(fd, NULL);
        } else {
            reply(fd, "404 Not Found", "text/plain", "not found\n");
        }
        close(fd);
    }
    close(srv);
    return 0;
}
