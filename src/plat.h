#ifndef OC_PLAT_H
#define OC_PLAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define EXE ".exe"
#define LIB_LIST "engines-win.txt"
#define CPU_LIST "engines-cpu-win.txt"
#elif defined(__APPLE__) && defined(__aarch64__)
#define EXE ""
#define LIB_LIST "engines-mac-arm.txt"
#define CPU_LIST ""
#elif defined(__APPLE__)
#define EXE ""
#define LIB_LIST "engines-mac-x86.txt"
#define CPU_LIST ""
#else
#define EXE ""
#define LIB_LIST "engines.txt"
#define CPU_LIST "engines-cpu.txt"
#endif

typedef intptr_t proc_t; // pid on unix, process HANDLE on windows, 0 = none

int proc_start(const char *path, int quiet, proc_t *p, int *to_fd, int *from_fd);
void proc_kill(proc_t p);
void proc_reap(proc_t p);
int fd_wait(int fd, int ms); // >0 readable (or eof), 0 timed out, <0 broken
void no_sigpipe(void);
int cpu_count(void);

int is_exec(const char *path);
int make_dir(const char *path);
int full_path(const char *in, char *out); // out needs 4096
void exe_dir(char *out, size_t n);
int data_dir_ok(const char *dir);
void default_data(char *out, size_t n);
int open_folder(const char *dir);
int run_wait(char *const argv[]);
void relaunch_self(char **argv, const char *home);
int spawn_detached(char *const argv[]); // windows wont replace a running exe, so updates need us gone
void quit_signals(void (*fn)(int));

typedef struct {
    char url[600], kind[8], pick[256], out[1200], data[1100], helper[1024], slug[48];
} DlJob;

intptr_t dl_start(const DlJob *j);
intptr_t lists_start(const char *data, const char *a, const char *b);
intptr_t fetch_start(const char *url, const char *out); // small file, short timeout, out only shows up if it worked
int job_done(intptr_t h, int *ok);
void job_kill(intptr_t h);

void engine_path_for(const char *data, const char *slug, char *out, size_t n);
void engine_remove(const char *data, const char *path);
void engine_list(const char *dir, void (*add)(const char *label, const char *path));

#endif
