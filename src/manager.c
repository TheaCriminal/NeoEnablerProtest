/* GPL-3.0-only. NEO Enabler manager: installs the generic NEO launch hook in SceShellCore,
   shows a PS5 notification and stays resident with a web page on port 8765 to enable/disable the hook
   and show the mode of open PS4 games. Also installs the Media tab shortcut (NEOE00001) when missing. Hook logic in shellcore_hook.h. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <netinet/in.h>
#include <ps5/kernel.h>
#include "shellcore_hook.h"
#include "legacy_v4.h"
#include "icon_data.h"

#define PORT 8765
#define VERSION "NEO Enabler manager 0.1.1"

typedef struct {
    uint32_t app_id; uint64_t unknown1; char title_id[10]; uint32_t apptype; char unknown2[8];
    uint32_t attribute; /* param.sfo ATTRIBUTE as launched, including the hook change */
    char unknown3[0x2a];
} app_info_t;
#define ATTR_PS4_PRO 0x00800000
typedef struct { char useless[45]; char message[3075]; } notify_request_t;
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *msg) {
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
    "            \"titleName\": \"NEO Enabler\"\n"
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

/* Returns 0 already installed, 1 installed now, <0 error. "Installed" means registered in the system
   (sceAppInstUtilAppExists) with the current param.json and icon: deleting the shortcut from the home
   screen makes the next start install it again. */
static int install_launcher(void) {
    const char *param_path = "/user/app/" TITLE_ID "/sce_sys/param.json", *icon_path = "/user/app/" TITLE_ID "/sce_sys/icon0.png";
    int err = appinst_open();
    if (err) { printf("appinst open rc=%x\n", err); return -3; }
    uint8_t exists = 0;
    if (!appinst.exists(TITLE_ID, &exists) && exists &&
        same_file(param_path, launcher_param, sizeof(launcher_param) - 1) && same_file(icon_path, icon0_png, sizeof(icon0_png))) return 0;
    if ((mkdir("/user/app/" TITLE_ID, 0755) && errno != EEXIST) || (mkdir("/user/app/" TITLE_ID "/sce_sys", 0755) && errno != EEXIST)) return -1;
    if (write_file(param_path, launcher_param, sizeof(launcher_param) - 1) || write_file(icon_path, icon0_png, sizeof(icon0_png))) return -2;
    err = appinst.install_dir ? appinst.install_dir(TITLE_ID, "/user/app/", 0) : appinst.install_all(0);
    printf("install %s rc=%x\n", TITLE_ID, err);
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
                    len += snprintf(out + len, cap - len, html ? "<li><b>%s</b> (%.9s): %s</li>" : "%s (%.9s): %s\n",
                                    esc[0] ? esc : "Unknown game", info.title_id, neo ? "PS4 Pro (NEO)" : "PS4 base");
                }
            }
        }
        off += (size_t)ki->ki_structsize;
    }
    free(buf);
    if (!games) snprintf(out, cap, html ? "<li>No PS4 game running</li>" : "No PS4 game running\n");
    return games;
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
    "<title>NEO Enabler</title><style>"
    ":root{--bg:#111418;--fg:#e8eaed;--muted:#9aa0a6;--card:#1b1f24;--on:#2e7d32;--off:#8e2c2c;--btn:#2b3138}"
    "body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif}"
    "main{max-width:560px;margin:0 auto;padding:24px 16px}h1{font-size:22px;margin:0 0 16px}"
    ".card{background:var(--card);border-radius:10px;padding:16px;margin-bottom:16px}"
    ".state{display:inline-block;padding:4px 10px;border-radius:6px;font-weight:600}"
    ".on{background:var(--on)}.off{background:var(--off)}.unk{background:var(--btn)}"
    "button{font:inherit;color:var(--fg);background:var(--btn);border:0;border-radius:8px;padding:10px 16px;margin:8px 8px 0 0;cursor:pointer}"
    "button:hover{filter:brightness(1.25)}ul{margin:8px 0 0;padding-left:20px}small{color:var(--muted)}"
    "</style></head><body><main><h1>NEO Enabler</h1>";

/* v4 hook still in ShellCore (forces NEO regardless of this manager's hook). */
static int v4_active(const shell_t *s) { return s->pid > 0 && s->frame && v4_state(s->pid, s->frame) == V4_PRESENT; }

static void serve_page(int fd, const char *msg) {
    shell_t s; int st = hook_state(&s);
    if (st == HOOK_OFF && v4_active(&s)) st = HOOK_ON;
    char games[1024]; games_text(games, sizeof(games), 1);
    static char body[8192];
    snprintf(body, sizeof(body),
             "%s<div class=card>PS4 Pro mode: <span class='state %s'>%s</span>"
             "<form method=post><button formaction=/enable>Enable</button><button formaction=/disable>Disable</button></form>"
             "%s%s%s</div>"
             "<div class=card>Running games<ul>%s</ul></div>"
             "<div class=card><small>Applies to games launched after the change. With the hook enabled, every PS4 game without "
             "native Pro support launches in Pro mode. Rebooting the console disables it.<br>" VERSION "</small>"
             "<form method=post><button formaction=/quit>Quit the manager</button></form></div></main></body></html>",
             page_head, st == 1 ? "on" : st == 0 ? "off" : "unk", state_text(st),
             msg ? "<p>" : "", msg ? msg : "", msg ? "</p>" : "", games);
    reply(fd, "200 OK", "text/html; charset=utf-8", body);
}

static int do_set(int enable, char *msg, size_t cap, int show) {
    int rc = hook_set(enable);
    shell_t s; int st = hook_state(&s);
    if (!enable && !rc && (s.pid <= 0 || !s.frame || v4_remove(s.pid, s.frame))) rc = 9;
    if (!rc) snprintf(msg, cap, "NEO Enabler %s", enable ? "enabled" : "disabled");
    else snprintf(msg, cap, "NEO Enabler: failed to %s (rc=%d, state: %s)", enable ? "enable" : "disable", rc, state_text(st));
    if (show) notify(msg);
    return rc;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);
    int srv = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET; addr.sin_port = htons(PORT); addr.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    char msg[256];
    int rc = do_set(1, msg, sizeof(msg), 0);
    if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof(addr)) || listen(srv, 4)) {
        /* Another manager already owns the port: the hook was still installed above. */
        snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "\nWeb panel already running on port %d", PORT);
        notify(msg); puts(msg);
        return rc;
    }
    int li = install_launcher();
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "\nWeb panel: port %d%s", PORT,
             li == 1 ? "\nShortcut installed in the Media tab" : li < 0 ? "\nFailed to install the Media tab shortcut" : "");
    notify(msg); puts(msg);
    for (;;) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) continue;
        struct timeval tv = {5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        char req[2048]; ssize_t n = read(fd, req, sizeof(req) - 1);
        if (n <= 0) { close(fd); continue; }
        req[n] = 0;
        char method[8] = "", path[64] = "";
        sscanf(req, "%7s %63s", method, path);
        int post = !strcmp(method, "POST");
        if (post && (!strcmp(path, "/enable") || !strcmp(path, "/disable"))) {
            do_set(!strcmp(path, "/enable"), msg, sizeof(msg), 1);
            serve_page(fd, msg);
        } else if (post && !strcmp(path, "/quit")) {
            reply(fd, "200 OK", "text/html; charset=utf-8", "<!doctype html><meta charset=utf-8><title>NEO Enabler</title>"
                  "<body style='background:#111418;color:#e8eaed;font-family:system-ui'>Manager stopped. The hook state was kept.</body>");
            close(fd);
            notify("NEO Enabler: panel stopped (hook kept)");
            break;
        } else if (!strcmp(path, "/status")) {
            shell_t s; int st = hook_state(&s);
            char games[1024]; games_text(games, sizeof(games), 0);
            char body[1600];
            snprintf(body, sizeof(body), "hook: %s\nv4 hook: %s\nfw: %x site: %lx target: %lx cave: %lx eh_frame: %lx-%lx attr: 0x%x four_args: %d\n%s",
                     state_text(st), v4_active(&s) ? "present" : "absent", fw_major(), s.site, s.target, s.cave, s.frame, s.frame_end,
                     s.attr_off, s.four_args, games);
            reply(fd, "200 OK", "text/plain; charset=utf-8", body);
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
