#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plat.h"

static HANDLE job;

static HANDLE kill_job(void)
{
    if (!job) {
        job = CreateJobObjectA(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof(li));
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
    return job;
}

static void quote_arg(char *cmd, size_t n, const char *a)
{
    size_t l = strlen(cmd);
    if (l + 2 >= n) return;
    if (l) cmd[l++] = ' ';
    cmd[l++] = '"';
    for (const char *p = a; *p && l + 4 < n; p++) {
        int bs = 0;
        while (*p == '\\') bs++, p++;
        if (!*p) {
            while (bs-- && l + 3 < n) cmd[l++] = '\\', cmd[l++] = '\\';
            break;
        }
        if (*p == '"') {
            while (bs-- && l + 4 < n) cmd[l++] = '\\', cmd[l++] = '\\';
            cmd[l++] = '\\';
        } else
            while (bs-- && l + 3 < n) cmd[l++] = '\\';
        cmd[l++] = *p;
    }
    cmd[l++] = '"';
    cmd[l] = 0;
}

static int spawn(const char *app, char *cmd, const char *dir, HANDLE in, HANDLE out, HANDLE err, PROCESS_INFORMATION *pi)
{
    HANDLE list[3] = {in, out, err};
    int nl = 0;
    for (int i = 0; i < 3; i++) {
        int dup = 0;
        for (int k = 0; k < nl; k++) dup |= list[k] == list[i];
        if (list[i] && list[i] != INVALID_HANDLE_VALUE && !dup) list[nl++] = list[i]; // a handle twice in the list = invalid parameter
    }
    SIZE_T sz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &sz);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = malloc(sz);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &sz)) return free(attrs), 0;
    if (nl) UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, nl * sizeof(HANDLE), NULL, NULL);
    STARTUPINFOEXA si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = in;
    si.StartupInfo.hStdOutput = out;
    si.StartupInfo.hStdError = err;
    si.lpAttributeList = attrs;
    BOOL ok = CreateProcessA(app, cmd, NULL, NULL, nl > 0, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                             NULL, dir, &si.StartupInfo, pi);
    DeleteProcThreadAttributeList(attrs);
    free(attrs);
    if (!ok) return 0;
    AssignProcessToJobObject(kill_job(), pi->hProcess);
    ResumeThread(pi->hThread);
    return 1;
}

static HANDLE nul_handle(void)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    return CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
}

int proc_start(const char *path, int quiet, proc_t *p, int *to_fd, int *from_fd)
{
    char full[4096], dir[4096], cmd[4200] = "";
    if (!_fullpath(full, path, sizeof(full))) return 0;
    snprintf(dir, sizeof(dir), "%s", full);
    char *slash = strrchr(dir, '\\');
    if (slash) *slash = 0;
    quote_arg(cmd, sizeof(cmd), full);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE in_r, in_w, out_r, out_w;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return 0;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
        CloseHandle(in_r), CloseHandle(in_w);
        return 0;
    }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE err = INVALID_HANDLE_VALUE;
    if (quiet) err = nul_handle();
    else DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &err, 0, TRUE, DUPLICATE_SAME_ACCESS);
    PROCESS_INFORMATION pi;
    int ok = spawn(full, cmd, dir, in_r, out_w, err, &pi);
    CloseHandle(in_r), CloseHandle(out_w);
    if (err && err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (!ok) {
        CloseHandle(in_w), CloseHandle(out_r);
        return 0;
    }
    CloseHandle(pi.hThread);
    *p = (proc_t)pi.hProcess;
    *to_fd = _open_osfhandle((intptr_t)in_w, _O_WRONLY | _O_BINARY);
    *from_fd = _open_osfhandle((intptr_t)out_r, _O_RDONLY | _O_BINARY);
    return 1;
}

void proc_kill(proc_t p)
{
    if (p) TerminateProcess((HANDLE)p, 1);
}

void proc_reap(proc_t p)
{
    if (!p) return;
    WaitForSingleObject((HANDLE)p, 3000);
    CloseHandle((HANDLE)p);
}

int fd_wait(int fd, int ms) // windows cant poll() a pipe, so peek at it
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD start = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return 1;
        if (avail) return 1;
        if ((int)(GetTickCount() - start) >= ms) return 0;
        Sleep(5);
    }
}

void no_sigpipe(void) {}

int cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors ? (int)si.dwNumberOfProcessors : 1;
}

int is_exec(const char *path)
{
    if (!*path) return 0;
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int make_dir(const char *path) { return CreateDirectoryA(path, NULL) ? 0 : -1; }

int full_path(const char *in, char *out) { return _fullpath(out, in, 4096) && GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES; }

void exe_dir(char *out, size_t n)
{
    char exe[4096];
    DWORD l = GetModuleFileNameA(NULL, exe, sizeof(exe));
    exe[l < sizeof(exe) ? l : 0] = 0;
    char *slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    snprintf(out, n, "%s", exe);
}

void default_data(char *out, size_t n)
{
    const char *base = getenv("LOCALAPPDATA");
    if (!base) base = getenv("TEMP");
    snprintf(out, n, "%s\\open-chease", base ? base : ".");
}

int data_dir_ok(const char *dir)
{
    CreateDirectoryA(dir, NULL);
    DWORD a = GetFileAttributesA(dir);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

int open_folder(const char *dir) { return (intptr_t)ShellExecuteA(NULL, "open", dir, NULL, NULL, SW_SHOWNORMAL) > 32 ? 0 : 1; }

// bare names get looked up in the current folder first on windows
static const char *sys_exe(const char *name, char *out, size_t n)
{
    const char *sub = !strcmp(name, "curl.exe") ? "curl.exe" : !strcmp(name, "tar.exe") ? "tar.exe"
                    : !strcmp(name, "powershell") ? "WindowsPowerShell\\v1.0\\powershell.exe" : NULL;
    char dir[MAX_PATH];
    UINT l = sub ? GetSystemDirectoryA(dir, sizeof(dir)) : 0;
    if (!l || l >= sizeof(dir)) return NULL;
    snprintf(out, n, "%s\\%s", dir, sub);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES ? out : NULL;
}

static int run_cmd(char *const argv[], DWORD *code)
{
    char cmd[8192] = "", app[MAX_PATH + 64];
    for (int i = 0; argv[i]; i++) quote_arg(cmd, sizeof(cmd), argv[i]);
    HANDLE nul = nul_handle();
    PROCESS_INFORMATION pi;
    int ok = spawn(sys_exe(argv[0], app, sizeof(app)), cmd, NULL, nul, nul, nul, &pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
    return 1;
}

int run_wait(char *const argv[])
{
    DWORD code = 1;
    return run_cmd(argv, &code) ? (int)code : -1;
}

void relaunch_self(char **argv, const char *home)
{
    char exe[4096], cmd[8192] = "";
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    quote_arg(cmd, sizeof(cmd), exe);
    for (int i = 1; argv[i]; i++) quote_arg(cmd, sizeof(cmd), argv[i]);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi) ||
        CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}

int spawn_detached(char *const argv[])
{
    char cmd[8192] = "", app[MAX_PATH + 64];
    for (int i = 0; argv[i]; i++) quote_arg(cmd, sizeof(cmd), argv[i]);
    const char *exe = sys_exe(argv[0], app, sizeof(app));
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi) &&
        !CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        return -1;
    CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
    return 0;
}

static void (*quit_fn)(int);
static BOOL WINAPI on_ctrl(DWORD t)
{
    if (quit_fn) quit_fn(SIGINT);
    return TRUE;
}

void quit_signals(void (*fn)(int))
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX); // so a missing vc++ dll just fails, no popup
    quit_fn = fn;
    SetConsoleCtrlHandler(on_ctrl, TRUE);
}

static void rm_tree(const char *path)
{
    char pat[4200];
    snprintf(pat, sizeof(pat), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            char sub[4200];
            snprintf(sub, sizeof(sub), "%s\\%s", path, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) rm_tree(sub);
            else SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL), DeleteFileA(sub);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
}

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    if (b > a) return 0;
    for (size_t i = 0; i < b; i++) {
        char x = s[a - b + i], y = tail[i];
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if ((x | 32) != (y | 32)) return 0;
    }
    return 1;
}

static void find_exe(const char *dir, const char *pick, char *best, long long *bsz)
{
    char pat[4200];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char sub[4200];
        snprintf(sub, sizeof(sub), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            find_exe(sub, pick, best, bsz);
            continue;
        }
        long long sz = ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        if (!ends_with(sub, ".exe") || (*pick && !ends_with(sub, pick)) || sz <= *bsz) continue;
        FILE *f = fopen(sub, "rb");
        char mz[2] = {0, 0};
        if (f) fread(mz, 1, 2, f), fclose(f);
        if (mz[0] == 'M' && mz[1] == 'Z') snprintf(best, 4096, "%.4095s", sub), *bsz = sz;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static int write_text(const char *path, const char *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fputs(s, f);
    return fclose(f) == 0;
}

typedef struct {
    DlJob j;
    char a[64], b[64];
    int lists;
} Job;

static DWORD WINAPI dl_thread(void *arg)
{
    Job *jb = arg;
    DlJob *j = &jb->j;
    char engines[1200], tmp[1300], got[1400], dir[1300];
    snprintf(engines, sizeof(engines), "%s\\engines", j->data);
    CreateDirectoryA(engines, NULL);
    snprintf(tmp, sizeof(tmp), "%s\\dl.%lu", j->data, (unsigned long)GetTickCount());
    CreateDirectoryA(tmp, NULL);
    snprintf(got, sizeof(got), "%s\\got", tmp);
    snprintf(dir, sizeof(dir), "%s\\%s", engines, j->slug);
    int ok = 0, wiped = 0;
    char *curl[] = {"curl.exe", "-fsL", "--speed-time", "60", "--speed-limit", "1000", j->url, "-o", got, NULL};
    if (!strcmp(j->kind, "lc0") || run_wait(curl) != 0) goto done;
    rm_tree(dir);
    wiped = 1;
    CreateDirectoryA(dir, NULL);
    char exe[4096] = "", mark[1400];
    if (!strcmp(j->kind, "bin")) {
        snprintf(exe, sizeof(exe), "%s\\%s.exe", dir, j->slug);
        if (!MoveFileExA(got, exe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) goto done;
    } else {
        char *tar[] = {"tar.exe", "-xf", got, "-C", dir, NULL}; // windows tar is bsdtar, it does zip too
        if (run_wait(tar) != 0) goto done;
        long long sz = -1;
        find_exe(dir, j->pick, exe, &sz);
        if (!*exe) goto done;
    }
    snprintf(mark, sizeof(mark), "%s\\.engine", dir);
    ok = write_text(mark, exe);
done:
    rm_tree(tmp);
    if (!ok && wiped) rm_tree(dir);
    free(jb);
    return ok ? 0 : 1;
}

static DWORD WINAPI lists_thread(void *arg)
{
    Job *jb = arg;
    const char *names[2] = {jb->a, jb->b};
    for (int i = 0; i < 2; i++) {
        if (!*names[i]) continue;
        char url[300], dst[1300], tmp[1400];
        const char *br = getenv("OPEN_CHEASE_BRANCH");
        snprintf(url, sizeof(url), "https://raw.githubusercontent.com/sevvy-wevvy/open-chease/%.40s/%s", br && *br ? br : "main", names[i]);
        snprintf(dst, sizeof(dst), "%s\\%s", jb->j.data, names[i]);
        snprintf(tmp, sizeof(tmp), "%s.new", dst);
        char *curl[] = {"curl.exe", "-fsL", "--max-time", "15", url, "-o", tmp, NULL};
        int good = run_wait(curl) == 0;
        if (good) { // a captive portal page isnt an engine list
            FILE *f = fopen(tmp, "rb");
            char buf[4096];
            size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
            if (f) fclose(f);
            buf[n] = 0;
            good = strchr(buf, '|') != NULL;
        }
        if (good) MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING);
        DeleteFileA(tmp);
    }
    free(jb);
    return 0;
}

static DWORD WINAPI fetch_thread(void *arg)
{
    Job *jb = arg;
    char tmp[1300];
    snprintf(tmp, sizeof(tmp), "%s.new", jb->j.out);
    DeleteFileA(tmp);
    char *curl[] = {"curl.exe", "-fsL", "--max-time", "10", jb->j.url, "-o", tmp, NULL};
    int ok = run_wait(curl) == 0 && MoveFileExA(tmp, jb->j.out, MOVEFILE_REPLACE_EXISTING);
    DeleteFileA(tmp);
    free(jb);
    return ok ? 0 : 1;
}

static intptr_t start_job(Job *jb, LPTHREAD_START_ROUTINE fn)
{
    HANDLE h = CreateThread(NULL, 0, fn, jb, 0, NULL);
    if (!h) free(jb);
    return (intptr_t)h;
}

intptr_t dl_start(const DlJob *j)
{
    Job *jb = calloc(1, sizeof(Job));
    if (!jb) return 0;
    jb->j = *j;
    return start_job(jb, dl_thread);
}

intptr_t lists_start(const char *data, const char *a, const char *b)
{
    Job *jb = calloc(1, sizeof(Job));
    if (!jb) return 0;
    snprintf(jb->j.data, sizeof(jb->j.data), "%s", data);
    snprintf(jb->a, sizeof(jb->a), "%s", a);
    snprintf(jb->b, sizeof(jb->b), "%s", b);
    return start_job(jb, lists_thread);
}

intptr_t fetch_start(const char *url, const char *out)
{
    Job *jb = calloc(1, sizeof(Job));
    if (!jb) return 0;
    snprintf(jb->j.url, sizeof(jb->j.url), "%s", url);
    snprintf(jb->j.out, sizeof(jb->j.out), "%s", out);
    return start_job(jb, fetch_thread);
}

int job_done(intptr_t h, int *ok)
{
    if (!h || WaitForSingleObject((HANDLE)h, 0) != WAIT_OBJECT_0) return 0;
    DWORD code = 1;
    GetExitCodeThread((HANDLE)h, &code);
    CloseHandle((HANDLE)h);
    *ok = code == 0;
    return 1;
}

void job_kill(intptr_t h)
{
    if (h) CloseHandle((HANDLE)h);
}

void engine_path_for(const char *data, const char *slug, char *out, size_t n)
{
    char mark[1300];
    snprintf(mark, sizeof(mark), "%s\\engines\\%s\\.engine", data, slug);
    FILE *f = fopen(mark, "rb");
    if (f) {
        char buf[4096];
        size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[got] = 0;
        buf[strcspn(buf, "\r\n")] = 0;
        if (*buf) {
            snprintf(out, n, "%s", buf);
            return;
        }
    }
    snprintf(out, n, "%s\\engines\\%s\\%s.exe", data, slug, slug);
}

static int under(const char *path, const char *dir, char *rest, size_t n)
{
    char p[4096], d[4096];
    if (!_fullpath(p, path, sizeof(p)) || !_fullpath(d, dir, sizeof(d))) return 0;
    size_t l = strlen(d);
    if (_strnicmp(p, d, l) || (p[l] != '\\' && p[l] != '/')) return 0;
    snprintf(rest, n, "%s", p + l + 1);
    return 1;
}

void engine_remove(const char *data, const char *path)
{
    char eng[1200], sf[1200], rest[4096];
    snprintf(eng, sizeof(eng), "%s\\engines", data);
    snprintf(sf, sizeof(sf), "%s\\stockfish", data);
    if (under(path, sf, rest, sizeof(rest))) {
        DeleteFileA(path);
        return;
    }
    if (!under(path, eng, rest, sizeof(rest))) return;
    char *sub = strpbrk(rest, "\\/");
    if (!sub) {
        DeleteFileA(path);
        return;
    }
    *sub = 0;
    char dir[5400];
    snprintf(dir, sizeof(dir), "%s\\%s", eng, rest);
    for (int i = 0; i < 5; i++) { // the exe can stay locked for a bit after the engine got killed
        rm_tree(dir);
        if (GetFileAttributesA(dir) == INVALID_FILE_ATTRIBUTES) return;
        Sleep(300);
    }
}

void engine_list(const char *dir, void (*add)(const char *label, const char *path))
{
    char pat[1300];
    snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char f[4096];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char mark[4200];
            snprintf(mark, sizeof(mark), "%s\\%s\\.engine", dir, fd.cFileName);
            FILE *m = fopen(mark, "rb");
            if (!m) continue;
            size_t got = fread(f, 1, sizeof(f) - 1, m);
            fclose(m);
            f[got] = 0;
            f[strcspn(f, "\r\n")] = 0;
            if (is_exec(f)) add(fd.cFileName, f);
        } else if (ends_with(fd.cFileName, ".exe")) {
            snprintf(f, sizeof(f), "%s\\%s", dir, fd.cFileName);
            char label[300];
            snprintf(label, sizeof(label), "%s", fd.cFileName);
            label[strlen(label) - 4] = 0;
            add(label, f);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
