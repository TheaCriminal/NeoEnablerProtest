/* GPL-3.0-only. NEO Enabler manager: installs the generic NEO launch hook in SceShellCore,
   shows a PS5 notification and stays resident with a web page on port 8765 to enable/disable the hook
   and show the mode of open PS4 games. Independently manages a guarded 8GiB NEO budget when kernel access allows.
   Also installs the Media tab shortcut (NEOE00001) when missing. Hook logic in shellcore_hook.h. */
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
#define VERSION "NeoEnabler 0.3.0"
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

#define SETTINGS_PATH "/data/neo-settings"

/* 8 GiB is offered only on firmwares with a verified budget layout (mb_layouts): 13.20 tested, 13.60 pending testing. */
static uint32_t fw_raw_version(void) {
    uint32_t v = 0; size_t n = sizeof(v);
    sysctlbyname("machdep.upd_version", &v, &n, NULL, 0);
    return v;
}
static int memory_supported(void) { return mb_find_layout(fw_raw_version()) != NULL; }
static int memory_untested(void) { return fw_raw_version() == 0x13600000; }

static int settings_neo = 1;

static void settings_load(void) {
    FILE *f = fopen(SETTINGS_PATH, "rb");
    if (!f) return;
    char buf[1024] = {0};
    size_t used = fread(buf, 1, sizeof(buf) - 1, f);
    if (ferror(f) || !feof(f)) { fclose(f); log_line("settings: unreadable or oversized file ignored"); return; }
    if (used) {
        neo_settings cfg;
        if (neo_settings_parse(buf, &cfg)) log_line("settings: invalid GPU values; GPU boost disabled");
        settings_neo = cfg.neo;
        gpu_configure(cfg.gpu_enabled, cfg.gpu_mhz);
        if (cfg.gpu_enabled && !gpu_backend_supported()) log_line("settings: saved GPU boost ignored on unsupported firmware");
        if (cfg.memory) {
            if (memory_supported()) memory_request(1);
            else log_line("settings: memory_8gib=1 ignored, 8 GiB is not supported on firmware %x.%02x",
                          (fw_raw_version() >> 24) & 0xff, (fw_raw_version() >> 16) & 0xff);
        }
    }
    fclose(f);
}

static int settings_save(int neo_on) {
    neo_settings cfg = {neo_on, memory_target, gpu.enabled, gpu.target};
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
        const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : NULL;
        if (r) { size_t l = strlen(r); memcpy(out + o, r, l); o += l; } else out[o++] = *s;
    }
    out[o] = 0;
}

/* Open PS4 games and whether they run in PS4 Pro mode. */
static int games_text(char *out, size_t cap, int html) {
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

static const char page_head[] =
    "<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>NeoEnabler</title><style>"
    ":root{--bg:#0e1116;--fg:#e8eaed;--muted:#97a0ab;--card:#171b22;--line:#262c36;--on:#2f9e57;--off:#c2453b;--btn:#262d38}"
    "*{box-sizing:border-box}"
    "body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}"
    "header{position:sticky;top:0;z-index:2;display:flex;flex-wrap:wrap;gap:12px;align-items:center;"
    "justify-content:space-between;padding:14px 20px;background:rgba(14,17,22,.95);border-bottom:1px solid var(--line)}"
    "h1{font-size:20px;margin:0}h1 small{color:var(--muted);font-weight:400;font-size:13px;margin-left:8px}"
    "main{max-width:900px;margin:0 auto;padding:20px 16px}"
    ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}"
    "@media(max-width:600px){.grid{grid-template-columns:1fr}}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px}"
    ".wide{grid-column:1/-1}"
    ".state{display:inline-block;padding:3px 10px;border-radius:999px;font-size:13px;font-weight:600}"
    ".on{background:var(--on)}.off{background:var(--off)}.unk{background:var(--btn)}"
    "button{font:inherit;color:var(--fg);background:var(--btn);border:1px solid var(--line);border-radius:8px;"
    "padding:8px 14px;margin:8px 8px 0 0;cursor:pointer}"
    "button:hover{border-color:#3d7bfd}button:disabled{opacity:.45;cursor:default}"
    "button.danger{background:#3a1f1d;border-color:#5c2c28}"
    "nav{display:flex;gap:8px;margin:0 0 18px}nav a{color:var(--muted);text-decoration:none;"
    "padding:9px 16px;border-radius:8px;border:1px solid var(--line)}"
    "nav a[aria-current=page]{color:var(--fg);background:var(--btn);border-color:#3d7bfd}"
    "input[type=range]{width:100%;margin:20px 0}output{font-size:24px;font-weight:600}"
    ".metric{font-size:32px;font-weight:600;margin:10px 0}.notice{margin:12px 0;color:var(--muted)}"
    "ul{margin:8px 0 0;padding-left:20px}small{color:var(--muted)}"
    "</style><script src=/panel.js></script></head><body>";

/* v4 hook still in ShellCore (forces NEO regardless of this manager's hook). */
static int v4_active(const shell_t *s) { return s->pid > 0 && s->frame && v4_state(s->pid, s->frame) == V4_PRESENT; }

static void serve_page(int fd, const char *msg) {
    shell_t s; int st = hook_state(&s);
    if (st == HOOK_OFF && v4_active(&s)) st = HOOK_ON;
    char games[1024]; games_text(games, sizeof(games), 1);
    mb_snapshot memory;mb_check(&memory);
    const char*memory_state=memory.state==MB_8GIB?"enabled":memory.state==MB_UNKNOWN?"unavailable":"disabled";
    uint32_t fw_raw = fw_raw_version();
    int mem_supported = memory_supported();
    const char *dis = mem_supported ? "" : " disabled";
    char fw_str[16];
    snprintf(fw_str, sizeof(fw_str), "%x.%02x", (fw_raw >> 24) & 0xff, (fw_raw >> 16) & 0xff);
    static char body[10240];
    snprintf(body, sizeof(body),
             "%s"
             "<header><h1>NeoEnabler <small>FW %s</small></h1>"
             "<div><form method=post style='display:inline'>"
             "<button formaction=/settings/save>Save Settings</button>"
             "<button class=danger formaction=/quit>Quit</button>"
             "</form></div></header>"
             "<main><nav aria-label='Settings tabs'><a href=/ aria-current=page>Overview</a><a href=/gpu>GPU clock</a></nav>"
             "%s%s%s<div class=grid>"
             "<div class=card><b>PS4 Pro (NEO) mode</b> "
             "<span class='state %s'>%s</span>"
             "<form method=post><button formaction=/enable>Enable</button>"
             "<button formaction=/disable>Disable</button></form></div>"
             "<div class=card><b>8 GiB memory</b> "
             "<span class='state %s'>%s</span>%s"
             "<form method=post><button formaction=/memory/enable%s>Enable</button>"
             "<button formaction=/memory/disable%s>Disable</button></form>"
             "<br><small>Close every game first. Applies to new launches.</small></div>"
             "<div class='card wide'><b>Games running</b><ul>%s</ul></div>"
             "<div class=card><b>Firmware diagnostic</b><br>"
             "<small>Saves metadata to /data/neo-diagnostic.jsonl</small>"
             "<form method=post><button formaction=/diagnostic/save>Save Diagnostic</button></form></div>"
             "<div class=card><b>System logs</b><br>"
             "<small>Saves kernel logs to /data/neo-logs.txt</small>"
             "<form method=post><a href=/logs target=_blank><button type=button>Show Logs</button></a>"
             "<button formaction=/logs/save>Save Logs</button></form></div>"
             "</div><small style='display:block;margin-top:16px;text-align:center'>With the hook enabled, every PS4 game without "
             "native Pro support launches in Pro mode. Rebooting disables it.<br>" VERSION "</small>"
             "</main></body></html>",
             page_head, fw_str,
             msg ? "<p>" : "", msg ? msg : "", msg ? "</p>" : "",
             st == 1 ? "on" : st == 0 ? "off" : "unk", state_text(st),
             memory.state==MB_8GIB?"on":memory.state==MB_UNKNOWN?"unk":"off",memory_state,
             !mem_supported ? "<br><small>Not available on this firmware (supported: 13.20; 13.60 pending testing)</small>"
             : memory_untested() ? "<br><small>Firmware 13.60: pending testing, please report results</small>" : "",
             dis, dis, games);
    reply(fd, "200 OK", "text/html; charset=utf-8", body);
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
    static char body[12288];
    snprintf(body, sizeof body,
        "%s<header><h1>NeoEnabler <small>FW %s</small></h1>"
        "<div><form method=post style='display:inline'><input type=hidden name=view value=gpu>"
        "<button formaction=/settings/save>Save Settings</button><button class=danger formaction=/quit>Quit</button>"
        "</form></div></header><main>"
        "<nav aria-label='Settings tabs'><a href=/>Overview</a><a href=/gpu aria-current=page>GPU clock</a></nav>"
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
        page_head, fw, msg ? "<p>" : "", msg ? msg : "", msg ? "</p>" : "", current,
        gpu.applied && !gpu.blocked ? "on" : gpu.enabled ? "unk" : "off", gpu_state_text(), gpu.message, previous,
        gpu.target, GPU_MIN_MHZ, GPU_MAX_MHZ, gpu.target, dis, dis, dis,
        gpu_backend_supported() ? "" : "<p>GPU clock control is available on firmware 13.20 only.</p>");
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
    mb_snapshot m; mb_check(&m);
    log_line("state: NEO hook %s, v4 hook %s, 8 GiB %s (requested %s): %s", state_text(st),
             v4_active(&s) ? "present" : "absent",
             m.state == MB_8GIB ? "enabled" : m.state == MB_UNKNOWN ? "unavailable" : "disabled",
             memory_target ? "on" : "off", m.detail);
    log_line("state: GPU %s, requested %u MHz, SMU %s%u MHz, owner PID %d: %s",
             gpu_state_text(), gpu.target, gpu.reading_valid ? "" : "unavailable/last=", gpu.actual, gpu.owner_pid, gpu.message);
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
    log_line("settings: neo=%d memory_8gib=%d gpu_clock_enabled=%d gpu_clock_mhz=%u",
             settings_neo, memory_target, gpu.enabled, gpu.target);
    do_set(settings_neo, msg, sizeof(msg), 0);
    int memory_previous_result=memory_poll();
    log_line("8 GiB: %s", memory_message);
    gpu_tick();
    int li = install_launcher();
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "\nWeb panel: port %d%s", PORT,
             li == 1 ? "\nShortcut installed in the Media tab" : li < 0 ? "\nFailed to install the Media tab shortcut" : "");
    notify(msg); puts(msg);
    log_state();
    time_t last_memory_poll=time(NULL);
    for (;;) {
        time_t now=time(NULL);
        if(now<last_memory_poll||now-last_memory_poll>=2){
            gpu_tick();
            int memory_result=memory_poll();
            if(memory_result!=memory_previous_result){
                if(memory_result==0||memory_result==5||memory_result==6)notify(memory_message);
                else log_line("8 GiB: %s (result %d)",memory_message,memory_result);
                log_state();
            }
            memory_previous_result=memory_result;last_memory_poll=now;
        }
        fd_set ready;FD_ZERO(&ready);FD_SET(srv,&ready);struct timeval wait={1,0};
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
        if (post && (!strcmp(path, "/enable") || !strcmp(path, "/disable"))) {
            log_line("panel: NEO %s requested", !strcmp(path, "/enable") ? "enable" : "disable");
            do_set(!strcmp(path, "/enable"), msg, sizeof(msg), 1);
            log_state();
            serve_page(fd, msg);
        } else if(post&&(!strcmp(path,"/memory/enable")||!strcmp(path,"/memory/disable"))){
            log_line("panel: 8 GiB %s requested",!strcmp(path,"/memory/enable")?"enable":"disable");
            if(!memory_supported()){
                const char*umsg="8 GiB is not supported on this firmware (supported: 13.20; 13.60 pending testing); nothing changed";
                notify(umsg);serve_page(fd,umsg);
            }else{
                memory_request(!strcmp(path,"/memory/enable"));
                notify(memory_message);log_state();serve_page(fd,memory_message);
            }
        } else if (post && (!strcmp(path, "/gpu/enable") || !strcmp(path, "/gpu/disable"))) {
            int enable = !strcmp(path, "/gpu/enable"); unsigned target = gpu.target;
            if (enable && gpu_form_mhz(form_body, &target) != 1) {
                reply(fd, "400 Bad Request", "text/plain", "Choose a GPU target from 1800 to 2200 MHz in 50 MHz steps\n");
            } else {
                int rc = gpu_request(enable, target, gpu_game_pid());
                if (rc == -1 && enable) snprintf(gpu.message, sizeof gpu.message, "GPU clock request unavailable or failed; check firmware and SMU access");
                log_line("panel: GPU enable=%d target=%u rc=%d", enable, target, rc);
                notify(gpu.message); log_state(); serve_gpu_page(fd, gpu.message);
            }
        } else if (post && !strcmp(path, "/settings/save")) {
            shell_t ss; int sst = hook_state(&ss);
            if (sst == HOOK_OFF && v4_active(&ss)) sst = HOOK_ON;
            int ok = !settings_save(sst == HOOK_ON);
            const char *smsg = ok ? "Settings saved" : "Failed to save settings";
            log_line("panel: save settings neo=%d memory_8gib=%d gpu_clock_enabled=%d gpu_clock_mhz=%u",
                     sst == HOOK_ON, memory_target, gpu.enabled, gpu.target);
            notify(smsg);
            if (!strcmp(form_body, "view=gpu")) serve_gpu_page(fd, smsg); else serve_page(fd, smsg);
        } else if (post && !strcmp(path, "/quit")) {
            if (gpu_request(0, gpu.target, gpu_game_pid())) {
                notify(gpu.message); serve_gpu_page(fd, "Clock restoration failed. Retry Disable before quitting."); close(fd); continue;
            }
            reply(fd, "200 OK", "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><title>NeoEnabler</title>"
                  "<body style='background:#111418;color:#e8eaed;font-family:system-ui'>Manager stopped. NEO and memory settings were kept. GPU clock restore requested.</body>");
            close(fd);
            notify("NeoEnabler: panel stopped (NEO and memory kept, GPU boost stopped)");
            log_state();
            break;
        } else if (!strcmp(path, "/status")) {
            shell_t s; int st = hook_state(&s);
            char games[1024]; games_text(games, sizeof(games), 0);
            char body[3200];mb_snapshot memory;mb_check(&memory);gpu_refresh();
            snprintf(body, sizeof(body), "build: " VERSION "\nmemory_8gib: %s\nmemory_auto: %s\nmemory_detail: %s\nmemory_last_result: %s\nhook: %s\nv4 hook: %s\nfw: %x site: %lx target: %lx cave: %lx eh_frame: %lx-%lx attr: 0x%x four_args: %d\n%s",
                     memory.state==MB_8GIB?"enabled":memory.state==MB_UNKNOWN?"unavailable":"disabled",
                     memory_target?"enabled":"disabled",memory.detail,memory_message,
                     state_text(st), v4_active(&s) ? "present" : "absent", fw_major(), s.site, s.target, s.cave, s.frame, s.frame_end,
                     s.attr_off, s.four_args, games);
            size_t used = strlen(body);
            snprintf(body + used, sizeof body - used,
                     "gpu_clock: %s\ngpu_clock_enabled: %d\ngpu_target_mhz: %u\ngpu_actual_mhz: %s%u\ngpu_detail: %s\n",
                     gpu_state_text(), gpu.enabled, gpu.target, gpu.reading_valid ? "" : "unavailable/last=", gpu.actual, gpu.message);
            reply(fd, "200 OK", "text/plain; charset=utf-8", body);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/panel.js")) {
            char script[sizeof panel_js + 1];
            memcpy(script, panel_js, sizeof panel_js); script[sizeof panel_js] = 0;
            reply(fd, "200 OK", "application/javascript; charset=utf-8", script);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/gpu")) {
            serve_gpu_page(fd, NULL);
        } else if (!strcmp(method, "GET") && !strcmp(path, "/gpu/status")) {
            serve_gpu_status(fd);
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
