#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "plugin.h"

Plugin plugs[PL_MAX];
int nplugs;
void (*plug_note)(const Plugin *p, const char *msg);
int (*plug_grab)(double x, double y, double w, double h, const char *path, int *pw, int *ph);

static int loud;

typedef struct {
    char key[72];
    int v;
} Saved;
static Saved saved[400]; // plugin.<name> and plugin.<name>.<key> from the settings file, kept even for plugins that arent there
static int nsaved;

static Saved *saved_get(const char *key, int make)
{
    for (int i = 0; i < nsaved; i++)
        if (!strcmp(saved[i].key, key)) return &saved[i];
    if (!make || nsaved == (int)(sizeof(saved) / sizeof(saved[0])) || strlen(key) >= sizeof(saved[0].key)) return NULL;
    snprintf(saved[nsaved].key, sizeof(saved[0].key), "%s", key);
    saved[nsaved].v = 0;
    return &saved[nsaved++];
}

static const char *num(const char *s, double *out) // "-12.5", no strtod
{
    while (*s == ' ') s++;
    int neg = *s == '-';
    if (neg || *s == '+') s++;
    if (!((*s >= '0' && *s <= '9') || (*s == '.' && s[1] >= '0' && s[1] <= '9'))) return NULL;
    double v = 0, sc = 0.1;
    for (; *s >= '0' && *s <= '9'; s++)
        if (v < 1e9) v = v * 10 + *s - '0';
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++, sc /= 10) v += (*s - '0') * sc;
    *out = neg ? -v : v;
    return s;
}

static const char *inum(const char *s, int *out)
{
    double d;
    s = num(s, &d);
    if (s) *out = (int)(d < 0 ? d - 0.5 : d + 0.5);
    return s;
}

void plug_cfg_line(const char *key, const char *val)
{
    int v;
    if (!inum(val, &v)) return;
    Saved *s = saved_get(key, 1);
    if (s) s->v = v;
}

void plug_cfg_save(FILE *f)
{
    for (int i = 0; i < nsaved; i++) fprintf(f, "%s %d\n", saved[i].key, saved[i].v);
}

int plug_square(const char *s)
{
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return -1;
    if (s[2] && s[2] != ' ' && s[2] != '\r' && s[2] != '\n') return -1;
    return (s[1] - '1') * 8 + s[0] - 'a';
}

static void tidy(char *s) // no control chars on screen
{
    for (; *s; s++)
        if ((unsigned char)*s < 32 || *s == 127) *s = ' ';
}

static int ok_name(const char *n)
{
    if (!*n || strlen(n) > 31 || *n == '.') return 0;
    for (; *n; n++)
        if (!((*n >= 'a' && *n <= 'z') || (*n >= 'A' && *n <= 'Z') || (*n >= '0' && *n <= '9') || *n == '-' || *n == '_' || *n == '.')) return 0;
    return 1;
}

static void read_manifest(Plugin *p, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char ln[600];
    int os_run = 0, os_build = 0; // run.linux and friends win over plain run, whichever comes first
    while (fgets(ln, sizeof(ln), f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        char *eq = strchr(ln, '=');
        if (*ln == '#' || !eq) continue;
        char *k = ln, *v = eq + 1, *e = eq;
        *eq = 0;
        while (*k == ' ') k++;
        while (e > k && e[-1] == ' ') *--e = 0;
        while (*v == ' ') v++;
        e = v + strlen(v);
        while (e > v && e[-1] == ' ') *--e = 0;
        tidy(v);
        char *dot = strchr(k, '.');
        if (dot && (!strncmp(k, "run.", 4) || !strncmp(k, "build.", 6))) {
            if (strcmp(dot + 1, PL_OS)) continue;
            *dot = 0;
            if (*k == 'r') snprintf(p->run, sizeof(p->run), "%s", v), os_run = 1;
            else snprintf(p->build, sizeof(p->build), "%s", v), os_build = 1;
        } else if (!strcmp(k, "run")) {
            if (!os_run) snprintf(p->run, sizeof(p->run), "%s", v);
        } else if (!strcmp(k, "build")) {
            if (!os_build) snprintf(p->build, sizeof(p->build), "%s", v);
        } else if (!strcmp(k, "name")) snprintf(p->title, sizeof(p->title), "%s", v);
        else if (!strcmp(k, "description")) snprintf(p->desc, sizeof(p->desc), "%s", v);
        else if (!strcmp(k, "version")) snprintf(p->ver, sizeof(p->ver), "%s", v);
        else if (!strcmp(k, "author")) snprintf(p->author, sizeof(p->author), "%s", v);
        else if (!strcmp(k, "url")) snprintf(p->url, sizeof(p->url), "%s", v);
        else if (!strcmp(k, "lang")) snprintf(p->lang, sizeof(p->lang), "%s", v);
        else if (!strcmp(k, "needs")) snprintf(p->needs, sizeof(p->needs), "%s", v);
        else if (!strcmp(k, "platforms")) snprintf(p->platforms, sizeof(p->platforms), "%s", v);
        else if (!strcmp(k, "update")) snprintf(p->update, sizeof(p->update), "%s", v);
        else if (!strcmp(k, "internet")) snprintf(p->net_why, sizeof(p->net_why), "%s", v);
    }
    fclose(f);
}

const char *plug_flag_name[PF_N] = {"internet", "runs programs", "build runs code", "has programs", "looks shady", "reads secrets", "starts with pc", "reads keys", "deletes stuff"};
const char *plug_flag_says[PF_N] = {"uses the internet", "runs other programs", "runs its own code while building", "has program files it cant read", "looks shady",
                                    "goes after passwords or secret files", "sets itself up to start with your pc", "reads your keyboard",
                                    "can delete your stuff"};

typedef struct {
    char cat;
    const char *s, *also, *also2, *what; // also and also2 are a|b|c lists, one of each has to be on the same line
} Rule;

static const Rule rules[] = {
    {PF_NET, "urllib", 0, 0, "urllib"},
    {PF_NET, "urlopen", 0, 0, "urlopen"},
    {PF_NET, "import requests", 0, 0, "requests"},
    {PF_NET, "from requests", 0, 0, "requests"},
    {PF_NET, "requests.", "get(|post(|put(|session(", 0, "requests"},
    {PF_NET, "http.client", 0, 0, "http.client"},
    {PF_NET, "httpx", 0, 0, "httpx"},
    {PF_NET, "aiohttp", 0, 0, "aiohttp"},
    {PF_NET, "import socket", 0, 0, "socket"},
    {PF_NET, "from socket", 0, 0, "socket"},
    {PF_NET, "socket.socket", 0, 0, "socket"},
    {PF_NET, "websocket", 0, 0, "websocket"},
    {PF_NET, "ftplib", 0, 0, "ftplib"},
    {PF_NET, "smtplib", 0, 0, "smtplib"},
    {PF_NET, "paramiko", 0, 0, "paramiko"},
    {PF_NET, "curl ", 0, 0, "curl"},
    {PF_NET, "wget ", 0, 0, "wget"},
    {PF_NET, "invoke-webrequest", 0, 0, "Invoke-WebRequest"},
    {PF_NET, "invoke-restmethod", 0, 0, "Invoke-RestMethod"},
    {PF_NET, "iwr ", 0, 0, "iwr"},
    {PF_NET, "irm ", 0, 0, "irm"},
    {PF_NET, "net.webclient", 0, 0, "Net.WebClient"},
    {PF_NET, "system.net", 0, 0, "System.Net"},
    {PF_NET, "fetch(", 0, 0, "fetch"},
    {PF_NET, "xmlhttprequest", 0, 0, "XMLHttpRequest"},
    {PF_NET, "axios", 0, 0, "axios"},
    {PF_NET, "net/http", 0, 0, "net/http"},
    {PF_NET, "libcurl", 0, 0, "libcurl"},
    {PF_NET, "curl_easy", 0, 0, "libcurl"},
    {PF_NET, "sys/socket.h", 0, 0, "sockets"},
    {PF_NET, "winsock", 0, 0, "winsock"},
    {PF_NET, "ws2_32", 0, 0, "winsock"},
    {PF_NET, "winhttp", 0, 0, "winhttp"},
    {PF_NET, "wininet", 0, 0, "wininet"},
    {PF_NET, "/dev/tcp/", 0, 0, "/dev/tcp"},
    {PF_NET, "netcat", 0, 0, "netcat"},
    {PF_NET, "getaddrinfo", 0, 0, "getaddrinfo"},
    {PF_NET, "gethostbyname", 0, 0, "gethostbyname"},
    {PF_NET, "require(", "'http|\"http|'net'|\"net\"|'dgram|\"dgram", 0, "node http"},

    {PF_RUN, "subprocess", 0, 0, "subprocess"},
    {PF_RUN, "os.system", 0, 0, "os.system"},
    {PF_RUN, "popen(", 0, 0, "popen"},
    {PF_RUN, "os.exec", 0, 0, "os.exec"},
    {PF_RUN, "os.spawn", 0, 0, "os.spawn"},
    {PF_RUN, "os.startfile", 0, 0, "os.startfile"},
    {PF_RUN, "pty.spawn", 0, 0, "pty.spawn"},
    {PF_RUN, "child_process", 0, 0, "child_process"},
    {PF_RUN, "start-process", 0, 0, "Start-Process"},
    {PF_RUN, "shellexecute", 0, 0, "ShellExecute"},
    {PF_RUN, "createprocess", 0, 0, "CreateProcess"},
    {PF_RUN, "winexec", 0, 0, "WinExec"},
    {PF_RUN, "system(", 0, 0, "system()"},
    {PF_RUN, "execv", 0, 0, "exec"},
    {PF_RUN, "execl", 0, 0, "exec"},
    {PF_RUN, "posix_spawn", 0, 0, "posix_spawn"},
    {PF_RUN, "os/exec", 0, 0, "os/exec"},
    {PF_RUN, "exec.command", 0, 0, "exec.Command"},
    {PF_RUN, "runtime.getruntime", 0, 0, "Runtime.exec"},
    {PF_RUN, "process.start(", 0, 0, "Process.Start"},
    {PF_RUN, "cmd /c", 0, 0, "cmd /c"},
    {PF_RUN, "cmd.exe", 0, 0, "cmd.exe"},
    {PF_RUN, "powershell", 0, 0, "powershell"},

    {PF_SHADY, "exec(compile(", 0, 0, "exec of compiled code"},
    {PF_SHADY, "marshal.loads", 0, 0, "marshal.loads"},
    {PF_SHADY, "b64decode", "exec(|eval(|exec (|eval (", 0, "runs base64 code"},
    {PF_SHADY, "decompress(", "exec(|eval(", 0, "runs packed code"},
    {PF_SHADY, "fromhex(", "exec(|eval(", 0, "runs hex code"},
    {PF_SHADY, "codecs.decode(", "exec(|eval(", 0, "runs scrambled code"},
    {PF_SHADY, "__import__(", "exec(|eval(|base64|zlib|marshal", 0, "hidden import"},
    {PF_SHADY, "eval(atob(", 0, 0, "runs base64 code"},
    {PF_SHADY, "eval(unescape(", 0, 0, "runs scrambled code"},
    {PF_SHADY, "eval(function(p,a,c,k,e", 0, 0, "packed js"},
    {PF_SHADY, "fromcharcode", "eval(", 0, "runs scrambled code"},
    {PF_SHADY, "frombase64string", "iex|invoke-expression|.invoke(|load(", 0, "runs base64 code"},
    {PF_SHADY, "invoke-expression", 0, 0, "Invoke-Expression"},
    {PF_SHADY, "iex(", 0, 0, "iex"},
    {PF_SHADY, "iex (", 0, 0, "iex"},
    {PF_SHADY, "| iex", 0, 0, "iex"},
    {PF_SHADY, "|iex", 0, 0, "iex"},
    {PF_SHADY, "-encodedcommand", 0, 0, "powershell -enc"},
    {PF_SHADY, "-enc ", "powershell|pwsh", 0, "powershell -enc"},
    {PF_SHADY, "|", "curl|wget|iwr|invoke-webrequest", "sh|bash|python|zsh|perl", "downloads code and runs it"},
    {PF_SHADY, "exec(", "requests.|urlopen|urllib|.read()|r.text|r.content|resp", 0, "runs downloaded code"},
    {PF_SHADY, "eval(", "requests.|urlopen|urllib|r.text|r.content|resp", 0, "runs downloaded code"},
    {PF_SHADY, "chmod +x", "curl|wget|urlretrieve|download", 0, "downloads a program and runs it"},
    {PF_SHADY, "discord.com/api/webhooks", 0, 0, "discord webhook"},
    {PF_SHADY, "discordapp.com/api/webhooks", 0, 0, "discord webhook"},
    {PF_SHADY, "api.telegram.org/bot", 0, 0, "telegram bot"},
    {PF_SHADY, "pastebin.com/raw", 0, 0, "pastebin"},
    {PF_SHADY, "ngrok", 0, 0, "ngrok"},
    {PF_SHADY, "certutil", "-decode|-urlcache", 0, "certutil"},
    {PF_SHADY, "bitsadmin", 0, 0, "bitsadmin"},
    {PF_SHADY, "mshta", 0, 0, "mshta"},
    {PF_SHADY, "rundll32", 0, 0, "rundll32"},
    {PF_SHADY, "regsvr32", 0, 0, "regsvr32"},
    {PF_SHADY, "virtualalloc", 0, 0, "VirtualAlloc"},
    {PF_SHADY, "writeprocessmemory", 0, 0, "WriteProcessMemory"},
    {PF_SHADY, "createremotethread", 0, 0, "CreateRemoteThread"},

    {PF_SECRET, ".ssh", "/|\\\\", 0, "~/.ssh"},
    {PF_SECRET, "id_rsa", 0, 0, "ssh keys"},
    {PF_SECRET, "id_ed25519", 0, 0, "ssh keys"},
    {PF_SECRET, "login data", 0, 0, "browser passwords"},
    {PF_SECRET, "logins.json", 0, 0, "browser passwords"},
    {PF_SECRET, "key4.db", 0, 0, "browser passwords"},
    {PF_SECRET, "cookies.sqlite", 0, 0, "browser cookies"},
    {PF_SECRET, "cookies", "\\\\|/|network", 0, "browser cookies"},
    {PF_SECRET, "browser_cookie", 0, 0, "browser cookies"},
    {PF_SECRET, "user data", "chrome|edge|brave|chromium|opera", 0, "browser profile"},
    {PF_SECRET, "google-chrome", 0, 0, "browser profile"},
    {PF_SECRET, ".mozilla", 0, 0, "browser profile"},
    {PF_SECRET, "mozilla", "firefox\\\\profiles|firefox/profiles", 0, "browser profile"},
    {PF_SECRET, "brave-browser", 0, 0, "browser profile"},
    {PF_SECRET, "leveldb", 0, 0, "leveldb (discord tokens)"},
    {PF_SECRET, "discord", "token|local storage|leveldb", 0, "discord token"},
    {PF_SECRET, "keychain", 0, 0, "keychain"},
    {PF_SECRET, "find-generic-password", 0, 0, "keychain"},
    {PF_SECRET, "find-internet-password", 0, 0, "keychain"},
    {PF_SECRET, ".aws", "/|\\\\|credentials", 0, "aws keys"},
    {PF_SECRET, ".gnupg", 0, 0, "gpg keys"},
    {PF_SECRET, "wallet.dat", 0, 0, "crypto wallet"},
    {PF_SECRET, "wallets", 0, 0, "crypto wallets"},
    {PF_SECRET, "exodus", 0, 0, "crypto wallet"},
    {PF_SECRET, "metamask", 0, 0, "crypto wallet"},
    {PF_SECRET, "electrum", 0, 0, "crypto wallet"},
    {PF_SECRET, "keepass", 0, 0, "password manager"},
    {PF_SECRET, ".kdbx", 0, 0, "password manager"},
    {PF_SECRET, "password-store", 0, 0, "password manager"},
    {PF_SECRET, "/etc/shadow", 0, 0, "/etc/shadow"},
    {PF_SECRET, "lsass", 0, 0, "lsass"},
    {PF_SECRET, "mimikatz", 0, 0, "mimikatz"},
    {PF_SECRET, "pyperclip", 0, 0, "clipboard"},
    {PF_SECRET, "getclipboarddata", 0, 0, "clipboard"},
    {PF_SECRET, "get-clipboard", 0, 0, "clipboard"},
    {PF_SECRET, "clipboard_get", 0, 0, "clipboard"},
    {PF_SECRET, "xclip", "-o|-out", 0, "clipboard"},
    {PF_SECRET, "xsel", 0, 0, "clipboard"},
    {PF_SECRET, "wl-paste", 0, 0, "clipboard"},
    {PF_SECRET, "pbpaste", 0, 0, "clipboard"},

    {PF_START, "crontab", 0, 0, "crontab"},
    {PF_START, "/etc/cron", 0, 0, "cron"},
    {PF_START, "systemctl", "enable", 0, "systemd service"},
    {PF_START, "/etc/systemd", 0, 0, "systemd service"},
    {PF_START, "systemd/user", 0, 0, "systemd service"},
    {PF_START, "autostart", 0, 0, "autostart"},
    {PF_START, ".bashrc", 0, 0, ".bashrc"},
    {PF_START, ".bash_profile", 0, 0, ".bash_profile"},
    {PF_START, ".zshrc", 0, 0, ".zshrc"},
    {PF_START, ".zprofile", 0, 0, ".zprofile"},
    {PF_START, "/.profile", 0, 0, ".profile"},
    {PF_START, "currentversion\\run", 0, 0, "Run registry key"},
    {PF_START, "currentversion\\\\run", 0, 0, "Run registry key"},
    {PF_START, "\\startup", 0, 0, "Startup folder"},
    {PF_START, "shell:startup", 0, 0, "Startup folder"},
    {PF_START, "launchagents", 0, 0, "LaunchAgents"},
    {PF_START, "launchdaemons", 0, 0, "LaunchDaemons"},
    {PF_START, "launchctl", "load|bootstrap", 0, "launchctl"},
    {PF_START, "loginitems", 0, 0, "login items"},
    {PF_START, "schtasks", 0, 0, "schtasks"},
    {PF_START, "scheduledtask", 0, 0, "scheduled task"},
    {PF_START, "/etc/rc.local", 0, 0, "rc.local"},
    {PF_START, "/etc/init.d", 0, 0, "init.d"},
    {PF_START, "winlogon", 0, 0, "winlogon"},

    {PF_KEYS, "pynput", 0, 0, "pynput"},
    {PF_KEYS, "import keyboard", 0, 0, "keyboard module"},
    {PF_KEYS, "from keyboard", 0, 0, "keyboard module"},
    {PF_KEYS, "keyboard.", "on_press|hook|read_key|record|add_hotkey", 0, "keyboard module"},
    {PF_KEYS, "getasynckeystate", 0, 0, "GetAsyncKeyState"},
    {PF_KEYS, "setwindowshookex", 0, 0, "SetWindowsHookEx"},
    {PF_KEYS, "wh_keyboard", 0, 0, "keyboard hook"},
    {PF_KEYS, "/dev/input", 0, 0, "/dev/input"},
    {PF_KEYS, "evdev", 0, 0, "evdev"},
    {PF_KEYS, "uinput", 0, 0, "uinput"},
    {PF_KEYS, "xinput", "test", 0, "xinput test"},
    {PF_KEYS, "cgeventtapcreate", 0, 0, "CGEventTap"},
    {PF_KEYS, "addglobalmonitorforevents", 0, 0, "global key monitor"},
    {PF_KEYS, "pyhook", 0, 0, "pyHook"},
    {PF_KEYS, "keylog", 0, 0, "keylogger"},

    {PF_BUILD, "<exec ", 0, 0, "msbuild exec"},
    {PF_BUILD, "<usingtask", 0, 0, "msbuild task"},
    {PF_BUILD, "<target ", 0, 0, "msbuild target"},
    {PF_BUILD, "<packagereference", 0, 0, "nuget packages"},
    {PF_BUILD, "\"preinstall\"", 0, 0, "npm install script"},
    {PF_BUILD, "\"install\":", 0, 0, "npm install script"},
    {PF_BUILD, "\"postinstall\"", 0, 0, "npm install script"},
    {PF_BUILD, "\"prepare\":", 0, 0, "npm install script"},

    {PF_WIPE, "del ", "/s", "/q", "del /s /q"},
    {PF_WIPE, "rd ", "/s", 0, "rd /s"},
    {PF_WIPE, "rmdir ", "/s", 0, "rmdir /s"},
    {PF_WIPE, "remove-item", "-recurse", "$home|$env:userprofile|c:\\\\|~", "Remove-Item on home"},
    {PF_WIPE, "rmtree(", "expanduser|home|environ|'/'|\"/\"|'~|\"~|c:\\\\|c:/", 0, "rmtree outside its folder"},
    {PF_WIPE, "rmsync(", "homedir|'/'|\"/\"", 0, "rmSync outside its folder"},
    {PF_WIPE, "rimraf", "homedir|'/'|\"/\"", 0, "rimraf outside its folder"},
    {PF_WIPE, "remove(", "expanduser", 0, "deletes in your home"},
    {PF_WIPE, "format c:", 0, 0, "format c:"},
    {PF_WIPE, "mkfs", 0, 0, "mkfs"},
    {PF_WIPE, "dd ", "of=/dev/", 0, "dd to a disk"},
    {PF_WIPE, "cipher /w", 0, 0, "cipher /w"},
    {PF_WIPE, "vssadmin", "delete", 0, "deletes backups"},
    {PF_WIPE, "shred ", 0, 0, "shred"},
};

static uint64_t fnv(uint64_t h, const void *b, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char *)b)[i]) * 0x100000001B3ull;
    return h;
}

static int alnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

static const char *has(const char *low, const char *s) // a word that starts with a letter has to start a word
{
    for (const char *q = low; (q = strstr(q, s)); q++)
        if (!alnum((unsigned char)*s) || q == low || !alnum((unsigned char)q[-1])) return q;
    return NULL;
}

static int any(const char *low, const char *list)
{
    char w[48];
    while (*list) {
        int n = 0;
        while (list[n] && list[n] != '|') n++;
        if (n < (int)sizeof(w)) {
            memcpy(w, list, n), w[n] = 0;
            if (has(low, w)) return 1;
        }
        list += n + (list[n] == '|');
    }
    return 0;
}

static void flag(Plugin *p, int cat, const char *rel, int line, const char *what)
{
    int k = p->nflag[cat]++;
    if (k >= PL_WHY) return;
    if (line) snprintf(p->flag[cat][k], sizeof(p->flag[0][0]), "%.36s:%d %.30s", rel, line, what);
    else snprintf(p->flag[cat][k], sizeof(p->flag[0][0]), "%.36s %.36s", rel, what);
}

static int rm_root(const char *low)
{
    for (const char *q = low; (q = has(q, "rm ")); q++) {
        const char *s = q + 3;
        int r = 0;
        while (*s == '-') {
            for (s++; alnum((unsigned char)*s) || *s == '-'; s++)
                if (*s == 'r') r = 1;
            while (*s == ' ') s++;
        }
        if (!r) continue;
        if (*s == '"' || *s == '\'') s++;
        if (*s == '~' || !strncmp(s, "$home", 5) || !strncmp(s, "${home}", 7)) return 1;
        if (*s == '/' && !alnum((unsigned char)s[1]) && s[1] != '.') return 1;
        static const char *top[] = {"home", "etc", "usr", "boot", "bin", "var", "root", "lib", "opt"};
        for (int k = 0; *s == '/' && k < 9; k++) {
            size_t l = strlen(top[k]);
            if (!strncmp(s + 1, top[k], l) && !alnum((unsigned char)s[1 + l]) && s[1 + l] != '.') return 1;
        }
    }
    return 0;
}

static int url_at(const char *low, char *host, int n)
{
    static const char *sch[] = {"http://", "https://", "ws://", "wss://", "ftp://"};
    for (int k = 0; k < 5; k++)
        for (const char *q = low; (q = has(q, sch[k])); q++) {
            const char *h = q + strlen(sch[k]);
            if (!strncmp(h, "localhost", 9) || !strncmp(h, "127.", 4) || !strncmp(h, "[::1]", 5) || !strncmp(h, "0.0.0.0", 7)) continue;
            int l = 0;
            while (h[l] && (alnum((unsigned char)h[l]) || h[l] == '.' || h[l] == '-') && l < n - 1) host[l] = h[l], l++;
            host[l] = 0;
            if (l && strchr(host, '.')) return 1;
        }
    return 0;
}

static int ip_at(const char *s, char *out, int n)
{
    for (const char *q = s; *q; q++) {
        if (*q < '0' || *q > '9' || (q > s && (alnum((unsigned char)q[-1]) || q[-1] == '.'))) continue;
        const char *e = q;
        int o[4], k = 0;
        for (; k < 4; k++) {
            int v = 0, d = 0;
            while (*e >= '0' && *e <= '9' && d < 4) v = v * 10 + *e++ - '0', d++;
            if (!d || d > 3 || v > 255) break;
            o[k] = v;
            if (k < 3) {
                if (*e != '.') break;
                e++;
            }
        }
        if (k < 4 || (*e >= '0' && *e <= '9') || (*e == '.' && e[1] >= '0' && e[1] <= '9') || alnum((unsigned char)*e)) continue;
        if (o[0] == 127 || o[0] == 0 || o[0] == 255 || (o[0] == 1 && o[1] == 2 && o[2] == 3)) continue; // 1.2.3.x is a version
        snprintf(out, n, "%d.%d.%d.%d", o[0], o[1], o[2], o[3]);
        return 1;
    }
    return 0;
}

static int blob(const char *s) // long base64/hex looking runs, or a pile of \x escapes
{
    int run = 0, up = 0, lo = 0, dig = 0, esc = 0;
    for (const char *q = s;; q++) {
        char c = *q;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            run++, up |= c >= 'A' && c <= 'Z', lo |= c >= 'a' && c <= 'z', dig |= c >= '0' && c <= '9';
            continue;
        }
        if (run >= 160 && ((up && lo && dig) || (!up || !lo))) return 1; // mixed case base64, or one case hex
        if (!c) break;
        run = up = lo = dig = 0;
        if (c == '\\' && (q[1] == 'x' || q[1] == 'X') && ++esc >= 32) return 1;
    }
    return 0;
}

static int ends(const char *s, const char *e)
{
    size_t a = strlen(s), b = strlen(e);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++)
        if ((s[a - b + i] | 32) != e[i]) return 0;
    return 1;
}

static void scan_text(Plugin *p, FILE *f, const char *rel, int doc)
{
    char ln[4097], low[4097], w[48];
    int line = 0, cont = 0;
    while (fgets(ln, sizeof(ln), f)) {
        if (!cont) line++;
        size_t l = strlen(ln);
        cont = l && ln[l - 1] != '\n';
        for (size_t i = 0; i <= l; i++) low[i] = ln[i] >= 'A' && ln[i] <= 'Z' ? ln[i] | 32 : ln[i];
        if (doc == 2 && *low == '#') continue; // plugin.txt comments are just notes, its keys are what counts
        int got = doc == 1 ? 1 << PF_NET | 1 << PF_RUN : 0; // docs talk about stuff, only the scary bits count there
        for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
            const Rule *r = &rules[i];
            if (got >> r->cat & 1 || !has(low, r->s)) continue;
            if ((r->also && !any(low, r->also)) || (r->also2 && !any(low, r->also2))) continue;
            got |= 1 << r->cat;
            flag(p, r->cat, rel, line, r->what);
        }
        if (!(got >> PF_WIPE & 1) && rm_root(low)) got |= 1 << PF_WIPE, flag(p, PF_WIPE, rel, line, "rm -rf on home or /");
        if (!(got >> PF_SHADY & 1) && blob(ln)) got |= 1 << PF_SHADY, flag(p, PF_SHADY, rel, line, "long encoded blob");
        if (doc || got & 1 << PF_NET) continue;
        char what[64];
        if (url_at(low, w, sizeof(w)) || ip_at(low, w, sizeof(w))) snprintf(what, sizeof(what), "%s", w), flag(p, PF_NET, rel, line, what);
    }
}

static uint64_t seen[1600];
static int nseen;
static void scan_dir_all(Plugin *p);
static void plug_check(Plugin *p);
static char data_dir[1024];

static int first_word(const char *s, char *out, int n)
{
    while (*s == ' ') s++;
    int l = 0;
    while (s[l] && s[l] != ' ' && l < n - 1) out[l] = s[l], l++;
    out[l] = 0;
    return l;
}

// kept in the data folder so a plugin cant write its own list
static void made_path(const Plugin *p, char *out, size_t n) { snprintf(out, n, "%.980s/plugin-builds/%s.txt", data_dir, p->name); }

static void made_read(Plugin *p)
{
    char path[1200], ln[64];
    made_path(p, path, sizeof(path));
    p->nmade = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(ln, sizeof(ln), f) && p->nmade < (int)(sizeof(p->made) / sizeof(p->made[0]))) {
        uint64_t v = 0;
        for (const char *q = ln; *q && *q != '\n'; q++) v = v << 4 | (uint64_t)(*q <= '9' ? *q - '0' : (*q | 32) - 'a' + 10);
        p->made[p->nmade++] = v;
    }
    fclose(f);
}

static uint64_t pre[1600];
static int npre;
static char pre_name[32];

int plug_cleared(Plugin *p)
{
    plug_check(p);
    memcpy(pre, seen, sizeof(seen[0]) * (size_t)nseen), npre = nseen; // whatever shows up after this, the build made
    snprintf(pre_name, sizeof(pre_name), "%s", p->name);
    return p->trusted;
}

void plug_built(Plugin *p)
{
    if (strcmp(pre_name, p->name)) return;
    int ok = p->trusted;
    uint64_t was = p->hash;
    p->nmade = 0;
    scan_dir_all(p);
    for (int i = 0; i < nseen; i++) {
        int old = 0;
        for (int k = 0; k < npre && !old; k++) old = pre[k] == seen[i];
        if (!old && p->nmade < (int)(sizeof(p->made) / sizeof(p->made[0]))) p->made[p->nmade++] = seen[i];
    }
    char path[1200];
    snprintf(path, sizeof(path), "%.980s/plugin-builds", data_dir);
    make_dir(path);
    made_path(p, path, sizeof(path));
    FILE *f = fopen(path, "w");
    for (int i = 0; f && i < p->nmade; i++) fprintf(f, "%016llx\n", (unsigned long long)p->made[i]);
    if (f) fclose(f);
    pre_name[0] = 0;
    plug_check(p);
    if (ok && p->hash != was) plug_trust(p); // the build moved its own files around, the user already said go
}

static void scan_file(Plugin *p, const char *path, const char *rel, long size)
{
    const char *nm = strrchr(rel, '/');
    nm = nm ? nm + 1 : rel;
    if (!strcmp(rel, "frame.ppm") || !strcmp(rel, "frame.ppm.tmp")) return; // ours
    if (*p->build && (!strcmp(rel, "build.log") || !strcmp(rel, ".built"))) return; // the build writes these, even when it fails
    FILE *f = fopen(path, "rb");
    if (!f) return;
    uint64_t fh = fnv(0xCBF29CE484222325ull, rel, strlen(rel) + 1);
    fh = fnv(fh, &size, sizeof(size));
    unsigned char h[4096], buf[8192];
    size_t n = fread(h, 1, sizeof(h), f), got;
    fh = fnv(fh, h, n);
    long left = 32l << 20;
    while (n == sizeof(h) && left > 0 && (got = fread(buf, 1, sizeof(buf), f)) > 0) fh = fnv(fh, buf, got), left -= (long)got;
    if (nseen < (int)(sizeof(seen) / sizeof(seen[0]))) seen[nseen++] = fh;
    for (int i = 0; i < p->nmade; i++)
        if (p->made[i] == fh) {
            fclose(f);
            return;
        }
    p->hash ^= fh; // xor so the folder order doesnt matter
    static const char *exts[] = {".exe", ".dll", ".so", ".dylib", ".scr", ".msi", ".com"};
    int bin = (n >= 4 && !memcmp(h, "\177ELF", 4)) || (n >= 2 && h[0] == 'M' && h[1] == 'Z');
    if (n >= 4) {
        uint32_t m = (uint32_t)h[0] << 24 | h[1] << 16 | h[2] << 8 | h[3];
        bin |= m == 0xFEEDFACE || m == 0xFEEDFACF || m == 0xCEFAEDFE || m == 0xCFFAEDFE || m == 0xCAFEBABE;
    }
    for (int k = 0; k < 7; k++) bin |= ends(nm, exts[k]);
    if (strstr(nm, ".so.")) bin = 1;
    if (bin) flag(p, PF_BIN, rel, 0, "is a program, cant read it");
    if (!strcmp(nm, "build.rs")) flag(p, PF_BUILD, rel, 0, "runs while cargo builds");
    if (!bin && !memchr(h, 0, n) && size <= (2 << 20)) {
        int doc = !strcmp(rel, "plugin.txt") ? 2 : ends(nm, ".md") || ends(nm, ".txt") || ends(nm, ".rst") || (!strchr(nm, '.') && !(n >= 2 && h[0] == '#' && h[1] == '!'));
        rewind(f);
        scan_text(p, f, rel, doc);
    }
    fclose(f);
}

static void scan_dir(Plugin *p, const char *dir, const char *rel, int depth, int *left)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && *left > 0) {
        const char *n = e->d_name;
        if (!strcmp(n, ".") || !strcmp(n, "..") || !strcmp(n, ".git") || !strcmp(n, "__pycache__")) continue;
        char path[1400], r[400];
        snprintf(path, sizeof(path), "%.1100s/%.250s", dir, n);
        snprintf(r, sizeof(r), "%s%s%.250s", rel, *rel ? "/" : "", n);
        struct stat st;
#ifdef _WIN32
        if (stat(path, &st)) continue;
#else
        if (lstat(path, &st) || S_ISLNK(st.st_mode)) continue;
#endif
        if (S_ISDIR(st.st_mode)) {
            if (depth < 8) scan_dir(p, path, r, depth + 1, left);
        } else if (S_ISREG(st.st_mode)) {
            (*left)--;
            scan_file(p, path, r, (long)st.st_size);
        }
    }
    closedir(d);
}

static void scan_dir_all(Plugin *p)
{
    memset(p->nflag, 0, sizeof(p->nflag));
    p->hash = fnv(0xCBF29CE484222325ull, p->run, strlen(p->run));
    nseen = 0;
    int left = 1500;
    scan_dir(p, p->dir, "", 0, &left);
    if (left <= 0) flag(p, PF_SHADY, "", 0, "too many files to read them all");
}

static void plug_check_build(Plugin *p) // build lines in plugin.txt that run more than a compiler
{
    static const char *runs[] = {"make", "gmake", "cmake", "cargo", "npm", "npx", "yarn", "pnpm", "bun", "msbuild", "gradle", "mvn", "meson",
                                 "ninja", "sh", "bash", "zsh", "python", "python3", "py", "node", "deno", "powershell", "pwsh", "cmd", "perl"};
    char mf[1200], ln[600], w[40], f[1200];
    struct stat st;
    snprintf(mf, sizeof(mf), "%s/plugin.txt", p->dir);
    FILE *in = fopen(mf, "r");
    while (in && fgets(ln, sizeof(ln), in)) {
        char *k = ln, *eq = strchr(ln, '=');
        while (*k == ' ') k++;
        if (strncmp(k, "build", 5) || (k[5] != ' ' && k[5] != '=' && k[5] != '.') || !eq) continue;
        eq[strcspn(eq, "\r\n")] = 0;
        first_word(eq + 1, w, sizeof(w));
        int hit = 0;
        for (int i = 0; *w && i < (int)(sizeof(runs) / sizeof(runs[0])) && !hit; i++)
            if (!strcmp(w, runs[i])) {
                char why[60];
                snprintf(why, sizeof(why), "build runs %s", w);
                flag(p, PF_BUILD, "plugin.txt", 0, why);
                hit = 1;
            }
        snprintf(f, sizeof(f), "%.1000s/%.39s", p->dir, w);
        if (!hit && *w && !strchr(w, '/') && !stat(f, &st)) flag(p, PF_BUILD, "plugin.txt", 0, "build runs a file from the plugin");
    }
    if (in) fclose(in);
    if (strstr(p->run, "frame.ppm") || strstr(p->run, "build.log") || strstr(p->run, ".built")) // the check skips those, so no running them
        flag(p, PF_SHADY, "plugin.txt", 0, "runs a file the app writes");
}

static void plug_check(Plugin *p)
{
    made_read(p);
    scan_dir_all(p);
    plug_check_build(p);
    p->okv = (int)(p->hash % 999999999) + 1; // settings only hold small numbers
    char k[80];
    snprintf(k, sizeof(k), "plugin.%s:ok", p->name);
    Saved *s = saved_get(k, 0);
    p->trusted = !plug_flags(p) || (s && s->v == p->okv);
    snprintf(k, sizeof(k), "plugin.%s:netok", p->name);
    s = saved_get(k, 0);
    p->net_ok = s && s->v == p->okv;
}

void plug_recheck(Plugin *p)
{
    int was = p->trusted;
    plug_check(p);
    if (was && !p->trusted && p->alive) plug_stop(p, "its files changed, waiting for your ok");
}

void plug_trust(Plugin *p)
{
    char k[80];
    snprintf(k, sizeof(k), "plugin.%s:ok", p->name);
    Saved *s = saved_get(k, 1);
    if (s) s->v = p->okv;
    p->trusted = 1;
}

int plug_flags(const Plugin *p)
{
    int m = 0;
    for (int k = 0; k < PF_N; k++)
        if (p->nflag[k]) m |= 1 << k;
    return m;
}

void plug_says(const Plugin *p, char *out, int n)
{
    int m = plug_flags(p), cnt = 0, done = 0;
    for (int k = 0; k < PF_N; k++) cnt += m >> k & 1;
    *out = 0;
    for (int k = PF_N - 1; k >= 0; k--) { // worst first
        if (!(m >> k & 1)) continue;
        size_t l = strlen(out);
        done++;
        snprintf(out + l, n - l, "%s%s", done == 1 ? "" : done == cnt ? " and " : ", ", plug_flag_says[k]);
    }
}

void plug_net_fine(Plugin *p, int ok)
{
    char k[80];
    snprintf(k, sizeof(k), "plugin.%s:netok", p->name);
    Saved *s = saved_get(k, ok);
    if (s) s->v = ok ? p->okv : 0;
    p->net_ok = ok;
}

void plug_scan(const char *data, int verbose)
{
    loud = verbose;
    snprintf(data_dir, sizeof(data_dir), "%s", data);
    char base[1100];
    snprintf(base, sizeof(base), "%s/plugins", data);
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nplugs < PL_MAX) {
        if (!ok_name(e->d_name)) continue;
        int have = 0;
        for (int i = 0; i < nplugs; i++) have |= !strcmp(plugs[i].name, e->d_name);
        if (have) continue; // rescan after an import, keep the running ones as they are
        Plugin *p = &plugs[nplugs];
        memset(p, 0, sizeof(*p));
        snprintf(p->name, sizeof(p->name), "%.31s", e->d_name);
        snprintf(p->dir, sizeof(p->dir), "%.980s/%.31s", base, e->d_name);
        char mf[1200];
        snprintf(mf, sizeof(mf), "%s/plugin.txt", p->dir);
        read_manifest(p, mf);
        if (!*p->run) continue;
        if (!*p->title) memcpy(p->title, p->name, sizeof(p->name));
        snprintf(p->status, sizeof(p->status), "off");
        p->pid = 0, p->to = p->from = -1;
        char k[80];
        snprintf(k, sizeof(k), "plugin.%s", p->name);
        Saved *s = saved_get(k, 0);
        p->on = s && s->v;
        plug_check(p);
        nplugs++;
    }
    closedir(d);
    for (int i = 0; i < nplugs; i++) // the folder order is random, the settings tab shouldnt be
        for (int j = i + 1; j < nplugs; j++)
            if (strcmp(plugs[j].name, plugs[i].name) < 0) {
                Plugin t = plugs[i];
                plugs[i] = plugs[j], plugs[j] = t;
            }
}

static int is_dir(const char *path)
{
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

static int has_manifest(const char *dir)
{
    char f[1200];
    struct stat st;
    snprintf(f, sizeof(f), "%s/plugin.txt", dir);
    return !stat(f, &st) && S_ISREG(st.st_mode);
}

int plug_found(const char *tmp, const char *sub, const char *repo, char src[][1100], char name[][32], int max)
{
    char top[1100] = "", base[1100];
    DIR *d = opendir(tmp);
    struct dirent *e;
    while (d && (e = readdir(d))) { // github puts it all in one repo-branch folder
        if (*e->d_name == '.') continue;
        snprintf(top, sizeof(top), "%.800s/%.200s", tmp, e->d_name);
        if (is_dir(top)) break;
        *top = 0;
    }
    if (d) closedir(d);
    if (!*top) return 0;
    snprintf(base, sizeof(base), "%.880s%s%.200s", top, *sub ? "/" : "", sub);
    int n = 0;
    if (has_manifest(base)) {
        const char *nm = strrchr(sub, '/');
        nm = *sub ? (nm ? nm + 1 : sub) : repo;
        if (!ok_name(nm)) return 0;
        snprintf(src[0], 1100, "%s", base);
        snprintf(name[0], 32, "%s", nm);
        return 1;
    }
    d = opendir(base); // no plugin.txt up top, take every folder that has one
    while (d && (e = readdir(d)) && n < max) {
        if (!ok_name(e->d_name)) continue;
        snprintf(src[n], 1100, "%.880s/%.200s", base, e->d_name);
        if (!is_dir(src[n]) || !has_manifest(src[n])) continue;
        snprintf(name[n++], 32, "%.31s", e->d_name);
    }
    if (d) closedir(d);
    return n;
}

void plug_launch(Plugin *p)
{
    if (p->alive) return;
    p->want = 0, p->nmenu = 0;
    if (!p->trusted) { // plug_start just looked, this is in case something skipped it
        snprintf(p->status, sizeof(p->status), "waiting for your ok");
        return;
    }
    p->nset = p->nitem = p->nnext = p->nin = p->nout = p->junk = p->tick_ms = 0;
    p->panel.nline = p->npanel.nline = 0;
    p->panel.title[0] = p->npanel.title[0] = p->tab[0] = 0;
    p->said[0] = 0, p->net_t = 0;
    if (!proc_cmd(p->run, p->dir, !loud, &p->pid, &p->to, &p->from)) {
        snprintf(p->status, sizeof(p->status), "couldnt start it, check run in plugin.txt");
        return;
    }
    p->alive = p->fresh = 1;
    p->started = -1;
    snprintf(p->status, sizeof(p->status), "running");
    plug_send(p, "hello 1 %s", p->name);
}

void plug_stop(Plugin *p, const char *why)
{
    p->want = 0;
    if (p->bjob) {
        plug_run_stop(p);
        snprintf(p->status, sizeof(p->status), "%s", why);
    }
    if (!p->alive) {
        if (!p->on) snprintf(p->status, sizeof(p->status), "%s", why);
        return;
    }
    fd_close(p->to);
    fd_close(p->from);
    p->to = p->from = -1;
    proc_kill(p->pid);
    proc_reap(p->pid);
    p->pid = 0;
    p->alive = 0;
    p->nitem = p->nnext = p->nmenu = 0;
    p->panel.nline = 0, p->panel.title[0] = 0;
    snprintf(p->status, sizeof(p->status), "%s", why);
    if (loud) fprintf(stderr, "plugin %s: %s\n", p->name, why);
}

static void vsend(Plugin *p, const char *fmt, va_list ap)
{
    if (!p->alive) return;
    char ln[1200];
    int n = vsnprintf(ln, sizeof(ln) - 1, fmt, ap);
    if (n < 0) return;
    if (n > (int)sizeof(ln) - 2) n = sizeof(ln) - 2;
    ln[n++] = '\n';
    if (p->nout + n > (int)sizeof(p->out)) { // its not reading, dont let it pile up forever
        plug_stop(p, "stopped reading, turned it off");
        return;
    }
    memcpy(p->out + p->nout, ln, n);
    p->nout += n;
}

void plug_send(Plugin *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsend(p, fmt, ap);
    va_end(ap);
}

void plug_send_all(const char *fmt, ...)
{
    for (int i = 0; i < nplugs; i++) {
        va_list ap;
        va_start(ap, fmt);
        vsend(&plugs[i], fmt, ap);
        va_end(ap);
    }
}

int plug_running(void)
{
    for (int i = 0; i < nplugs; i++)
        if (plugs[i].alive) return 1;
    return plug_busy(); // checks and builds get polled too
}

int plug_menu_list(int *pi, int *k, int max)
{
    int n = 0;
    long last = -1;
    while (n < max) { // tiny list, no sort
        int bi = -1, bk = -1;
        for (int i = 0; i < nplugs; i++)
            for (int j = 0; plugs[i].alive && j < plugs[i].nmenu; j++) {
                long s = plugs[i].menu[j].seq;
                if (s > last && (bi < 0 || s < plugs[bi].menu[bk].seq)) bi = i, bk = j;
            }
        if (bi < 0) break;
        pi[n] = bi, k[n++] = bk, last = plugs[bi].menu[bk].seq;
    }
    return n;
}

void plug_menu_click(int pi, int k)
{
    if (pi < 0 || pi >= nplugs || k < 0 || k >= plugs[pi].nmenu) return;
    plug_send(&plugs[pi], "click %s", plugs[pi].menu[k].key);
}

void plug_quit(void)
{
    for (int i = 0; i < nplugs; i++) plug_stop(&plugs[i], "off");
}

void plug_change(Plugin *p, int k, int v)
{
    PlugSet *s = &p->set[k];
    if (s->kind == PS_BUTTON) {
        plug_send(p, "click %s", s->key);
        return;
    }
    if (v < s->lo) v = s->lo;
    if (v > s->hi) v = s->hi;
    s->v = v;
    char key[80];
    snprintf(key, sizeof(key), "plugin.%s.%s", p->name, s->key);
    Saved *sv = saved_get(key, 1);
    if (sv) sv->v = v;
    plug_send(p, "set %s %d", s->key, v);
}

static char *word(char **s) // next space separated word, NULL at the end
{
    char *w = *s;
    while (*w == ' ') w++;
    if (!*w) return NULL;
    char *e = w;
    while (*e && *e != ' ') e++;
    if (*e) *e++ = 0;
    *s = e;
    return w;
}

static int peek(const char *s, char *out, int n) // next word without eating it, returns its length
{
    while (*s == ' ') s++;
    int l = 0;
    while (s[l] && s[l] != ' ' && l < n - 1) out[l] = s[l], l++;
    out[l] = 0;
    return s[l] && s[l] != ' ' ? 0 : l;
}

static int color(const char *s, uint32_t *out) // #rgb #rrggbb #aarrggbb
{
    if (*s != '#') return 0;
    s++;
    uint32_t v = 0;
    int n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        int d = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1;
        if (d < 0 || n >= 8) return 0;
        v = v * 16 + d;
    }
    if (n == 3) v = ((v >> 8) * 17) << 16 | ((v >> 4 & 15) * 17) << 8 | (v & 15) * 17;
    if (n == 3 || n == 6) v |= 0xFF000000u;
    else if (n != 8) return 0;
    *out = v;
    return 1;
}

static int pos(char **s, PlugItem *it) // a square name or x y in board px
{
    char w[8];
    if (peek(*s, w, sizeof(w)) == 2 && plug_square(w) >= 0) {
        it->sq = plug_square(w), it->x = it->y = 0;
        word(s);
        return 1;
    }
    const char *q = num(*s, &it->x);
    if (!q || !(q = num(q, &it->y))) return 0;
    it->sq = -1;
    *s = (char *)q;
    return 1;
}

static void cut(char *dst, size_t n, const char *src)
{
    while (*src == ' ') src++;
    snprintf(dst, n, "%s", src);
    size_t l = strlen(dst);
    while (l && dst[l - 1] == ' ') dst[--l] = 0;
}

static void label_tip(PlugSet *st, char *rest)
{
    char *bar = strchr(rest, '|');
    if (bar) *bar = 0, cut(st->tip, sizeof(st->tip), bar + 1);
    else st->tip[0] = 0;
    cut(st->label, sizeof(st->label), rest);
    if (!*st->label) snprintf(st->label, sizeof(st->label), "%s", st->key);
}

static int menu_cmd(Plugin *p, char *s)
{
    static long seq;
    char *key = word(&s);
    if (!key || strlen(key) >= sizeof(p->menu[0].key) || !ok_name(key)) return 0;
    int k = 0;
    while (k < p->nmenu && strcmp(p->menu[k].key, key)) k++;
    while (*s == ' ') s++;
    if (!*s) { // no label, take it away
        if (k == p->nmenu) return 0;
        memmove(&p->menu[k], &p->menu[k + 1], sizeof(p->menu[0]) * (size_t)(p->nmenu - k - 1));
        p->nmenu--;
        return 1;
    }
    if (k == PL_MENU) return 0;
    PlugMenu *m = &p->menu[k];
    char *bar = strchr(s, '|');
    if (bar) *bar = 0, cut(m->tip, sizeof(m->tip), bar + 1);
    else m->tip[0] = 0;
    cut(m->label, sizeof(m->label), s);
    if (!*m->label) snprintf(m->label, sizeof(m->label), "%s", key);
    if (k == p->nmenu) snprintf(m->key, sizeof(m->key), "%s", key), m->seq = ++seq, p->nmenu++;
    return 1;
}

static void declare(Plugin *p, char *s)
{
    char *kind = word(&s), *key = word(&s);
    if (!kind || !key || strlen(key) >= sizeof(p->set[0].key) || !ok_name(key)) return;
    int k = 0;
    while (k < p->nset && strcmp(p->set[k].key, key)) k++;
    if (k == PL_SETS) return;
    PlugSet st = {0};
    snprintf(st.key, sizeof(st.key), "%s", key);
    const char *q = s;
    if (!strcmp(kind, "bool")) {
        st.kind = PS_BOOL, st.hi = 1;
        if (!(q = inum(q, &st.def))) return;
    } else if (!strcmp(kind, "slider")) {
        st.kind = PS_SLIDER;
        if (!(q = inum(q, &st.def)) || !(q = inum(q, &st.lo)) || !(q = inum(q, &st.hi)) || st.hi <= st.lo || st.hi - st.lo > 100000) return;
    } else if (!strcmp(kind, "choice")) {
        st.kind = PS_CHOICE;
        if (!(q = inum(q, &st.def))) return;
        char *r = (char *)q, *list = word(&r);
        if (!list) return;
        for (char *c = list; *c && st.nch < PL_CHOICES;) {
            char *comma = strchr(c, ',');
            if (comma) *comma = 0;
            snprintf(st.ch[st.nch], sizeof(st.ch[0]), "%s", c);
            for (char *u = st.ch[st.nch]; *u; u++)
                if (*u == '_') *u = ' ';
            if (*st.ch[st.nch]) st.nch++;
            if (!comma) break;
            c = comma + 1;
        }
        if (!st.nch) return;
        st.hi = st.nch - 1;
        q = r;
    } else if (!strcmp(kind, "button")) {
        st.kind = PS_BUTTON;
    } else
        return;
    label_tip(&st, (char *)q);
    if (st.def < st.lo) st.def = st.lo;
    if (st.def > st.hi) st.def = st.hi;
    char sk[80];
    snprintf(sk, sizeof(sk), "plugin.%.31s.%.31s", p->name, key);
    Saved *sv = saved_get(sk, 0);
    st.v = sv ? sv->v : k < p->nset ? p->set[k].v : st.def;
    if (st.v < st.lo || st.v > st.hi) st.v = st.def;
    p->set[k] = st;
    if (k == p->nset) p->nset++;
    if (st.kind != PS_BUTTON) plug_send(p, "set %s %d", st.key, st.v);
}

static int draw_cmd(Plugin *p, char *s)
{
    static const char *kinds[] = {"text", "big", "rect", "box", "arrow", "square"};
    char *w = word(&s);
    if (!w || p->nnext >= PL_ITEMS) return 0;
    int kind = -1;
    for (int i = 0; i < 6; i++)
        if (!strcmp(w, kinds[i])) kind = i;
    if (kind < 0) return 0;
    PlugItem it = {.kind = kind, .sq = -1, .sq2 = -1};
    if (kind == PD_ARROW || kind == PD_SQUARE) {
        char *a = word(&s), *b = kind == PD_ARROW ? word(&s) : a;
        if (!a || !b || (it.sq = plug_square(a)) < 0 || (it.sq2 = plug_square(b)) < 0) return 0;
    } else if (!pos(&s, &it))
        return 0;
    if (kind == PD_RECT || kind == PD_BOX) {
        const char *q = num(s, &it.w);
        if (!q || !(q = num(q, &it.h)) || it.w <= 0 || it.h <= 0) return 0;
        s = (char *)q;
        if (it.w > 4000) it.w = 4000;
        if (it.h > 4000) it.h = 4000;
    }
    char *c = word(&s);
    if (!c || !color(c, &it.col)) return 0;
    if (kind == PD_TEXT || kind == PD_BIG) {
        cut(it.text, sizeof(it.text), s);
        if (!*it.text) return 0;
    }
    if (it.x < -4000 || it.x > 4000 || it.y < -4000 || it.y > 4000) return 0;
    p->next[p->nnext++] = it;
    return 1;
}

static void frame_cmd(Plugin *p, char *s, long now)
{
    double v[4];
    const char *q = s;
    for (int i = 0; i < 4; i++)
        if (!(q = num(q, &v[i]))) {
            plug_send(p, "frame none bad numbers");
            return;
        }
    if (v[2] < 1 || v[3] < 1) {
        plug_send(p, "frame none too small");
        return;
    }
    if (p->last_frame && now - p->last_frame < 100) {
        plug_send(p, "frame none busy");
        return;
    }
    p->last_frame = now ? now : 1;
    char path[1100];
    snprintf(path, sizeof(path), "%s/frame.ppm", p->dir);
    int w = 0, h = 0;
    if (!plug_grab || !plug_grab(v[0], v[1], v[2] > 1024 ? 1024 : v[2], v[3] > 1024 ? 1024 : v[3], path, &w, &h))
        plug_send(p, "frame none no board");
    else
        plug_send(p, "frame %d %d %s", w, h, path);
}

static int handle(Plugin *p, char *ln, long now) // 1 = something on screen changed
{
    tidy(ln);
    char *s = ln, *cmd = word(&s);
    if (!cmd) return 0;
    while (*s == ' ') s++;
    if (!strcmp(cmd, "draw")) return draw_cmd(p, s), 0;
    if (!strcmp(cmd, "show")) {
        memcpy(p->item, p->next, sizeof(PlugItem) * p->nnext);
        p->nitem = p->nnext, p->nnext = 0;
        p->panel = p->npanel;
        p->npanel.nline = 0, p->npanel.title[0] = 0;
        return 1;
    }
    if (!strcmp(cmd, "clear")) {
        int had = p->nitem || p->panel.nline || *p->panel.title;
        p->nitem = p->nnext = 0;
        p->panel.nline = p->npanel.nline = 0;
        p->panel.title[0] = p->npanel.title[0] = 0;
        return had;
    }
    if (!strcmp(cmd, "panel")) {
        cut(p->npanel.title, sizeof(p->npanel.title), s);
        return 0;
    }
    if (!strcmp(cmd, "line")) {
        PlugPanel *pn = &p->npanel;
        if (pn->nline >= PL_LINES) return 0;
        uint32_t col = 0;
        char c[12];
        if (peek(s, c, sizeof(c)) && color(c, &col)) word(&s);
        cut(pn->line[pn->nline], sizeof(pn->line[0]), s);
        pn->col[pn->nline++] = col;
        return 0;
    }
    if (!strcmp(cmd, "setting")) return declare(p, s), 1;
    if (!strcmp(cmd, "menu")) return menu_cmd(p, s);
    if (!strcmp(cmd, "tab")) {
        cut(p->tab, sizeof(p->tab), s);
        return 1;
    }
    if (!strcmp(cmd, "note")) {
        if (*s && plug_note) plug_note(p, s);
        return 1;
    }
    if (!strcmp(cmd, "log")) {
        snprintf(p->said, sizeof(p->said), "%s", s);
        if (loud) fprintf(stderr, "plugin %s: %s\n", p->name, s);
        return 0;
    }
    if (!strcmp(cmd, "frame")) return frame_cmd(p, s, now), 0;
    if (!strcmp(cmd, "tick")) {
        int ms = 0;
        if (inum(s, &ms)) p->tick_ms = ms <= 0 ? 0 : ms < 100 ? 100 : ms;
        return 0;
    }
    return 0; // anything else gets ignored so newer plugins still run on older versions
}

static int flush(Plugin *p, long now)
{
    if (!p->nout) return 1;
    int put = fd_write_some(p->to, p->out, p->nout);
    if (put < 0) {
        plug_stop(p, now - p->started < 3000 ? "quit right away, check it runs" : "it quit");
        return 0;
    }
    memmove(p->out, p->out + put, p->nout - put);
    p->nout -= put;
    return 1;
}

int plug_poll(long now)
{
    int dirty = plug_run_poll(now);
    for (int i = 0; i < nplugs; i++) {
        Plugin *p = &plugs[i];
        if (!p->alive) continue;
        if (p->started < 0) p->started = now;
        if (!*p->online && now - p->net_t >= 2000) { // went online? only the first time counts
            char at[64];
            p->net_t = now;
            if (proc_net(p->pid, at, sizeof(at))) {
                snprintf(p->online, sizeof(p->online), "%s", at);
                if (loud) fprintf(stderr, "plugin %s: went online, %s\n", p->name, at);
                if (!p->net_ok) p->net_new = 1;
                if (!p->net_ok && !*p->net_why) { // undeclared
                    char k[80];
                    snprintf(k, sizeof(k), "plugin.%.31s:ok", p->name);
                    Saved *sv = saved_get(k, 0);
                    if (sv) sv->v = 0;
                    p->trusted = 0;
                    plug_stop(p, "went online without saying so, stopped it");
                }
                dirty = 1;
                if (!p->alive) continue;
            }
        }
        if (p->tick_ms && now - p->last_tick >= p->tick_ms) p->last_tick = now, plug_send(p, "tick %ld", now);
        if (!p->alive) continue;
        if (!flush(p, now)) {
            dirty = 1;
            continue;
        }
        int lines = 0, bytes = 0, gone = 0;
        while (lines < 300 && bytes < 65536) { // a chatty one gets a slice, the rest waits for the next round
            int room = (int)sizeof(p->in) - p->nin;
            int got = fd_read_some(p->from, p->in + p->nin, room > 4096 ? 4096 : room);
            if (got < 0) gone = 1;
            if (got <= 0) break;
            bytes += got;
            p->nin += got;
            char *start = p->in, *end = p->in + p->nin, *nl;
            while ((nl = memchr(start, '\n', end - start))) {
                *nl = 0;
                if (nl > start && nl[-1] == '\r') nl[-1] = 0;
                if (!p->junk) dirty |= handle(p, start, now);
                p->junk = 0;
                start = nl + 1;
                lines++;
                if (!p->alive) break;
            }
            if (!p->alive) break;
            p->nin = (int)(end - start);
            memmove(p->in, start, p->nin);
            if (p->nin == (int)sizeof(p->in)) p->nin = 0, p->junk = 1; // a line this long is garbage, skip to the next newline
        }
        if (p->alive && !flush(p, now)) dirty = 1;
        if (gone && p->alive) {
            plug_stop(p, now - p->started < 3000 ? "quit right away, check it runs" : "it quit");
            dirty = 1;
        }
    }
    return dirty;
}

int plug_check_dir(const char *dir, FILE *out)
{
    static Plugin p;
    memset(&p, 0, sizeof(p));
    const char *nm = dir + strlen(dir);
    while (nm > dir && (nm[-1] == '/' || nm[-1] == '\\')) nm--;
    const char *e = nm;
    while (nm > dir && nm[-1] != '/' && nm[-1] != '\\') nm--;
    snprintf(p.name, sizeof(p.name), "%.*s", (int)(e - nm) > 31 ? 31 : (int)(e - nm), nm);
    snprintf(p.dir, sizeof(p.dir), "%.*s", (int)(e - dir), dir);
    char mf[1200];
    snprintf(mf, sizeof(mf), "%s/plugin.txt", p.dir);
    read_manifest(&p, mf);
    scan_dir_all(&p);
    plug_check_build(&p);
    fprintf(out, "%s ok %d\n", p.name, (int)(p.hash % 999999999) + 1);
    int m = plug_flags(&p);
    for (int k = 0; k < PF_N; k++) {
        if (!(m >> k & 1)) continue;
        fprintf(out, "%s %d\n", plug_flag_name[k], p.nflag[k]);
        for (int j = 0; j < p.nflag[k] && j < PL_WHY; j++) fprintf(out, "  %s\n", p.flag[k][j]);
    }
    if (!m) fprintf(out, "clean\n");
    return 0;
}
