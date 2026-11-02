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
int proc_reap_code(proc_t p); // reap and give back the exit code, -1 if it didnt exit normally
int fd_wait(int fd, int ms); // >0 readable (or eof), 0 timed out, <0 broken
int proc_cmd(const char *cmd, const char *dir, int quiet, proc_t *p, int *to_fd, int *from_fd); // runs inside dir, neither pipe blocks
int fd_read_some(int fd, char *buf, int n); // >0 bytes, 0 nothing yet, -1 closed
int fd_write_some(int fd, const char *buf, int n); // bytes it took, 0 = pipe full, -1 broken
void fd_close(int fd);
void no_sigpipe(void);
int cpu_count(void);
int proc_net(proc_t p, char *out, int n); // 1 = it or a child has a socket open to something thats not this pc, out says where

int is_exec(const char *path);
int make_dir(const char *path);
int full_path(const char *in, char *out); // out needs 4096
void exe_dir(char *out, size_t n);
int data_dir_ok(const char *dir);
void default_data(char *out, size_t n);
int open_folder(const char *dir);
int run_wait(char *const argv[]);
void relaunch_self(char **argv, const char *home);
int spawn_detached(char *const argv[], int hidden); // windows wont replace a running exe, so updates need us gone
void drop_console(void); // windows only, gets rid of the console a shortcut opens
void quit_signals(void (*fn)(int));
int ctl_open(const char *data); // fifo for open-chease --send, wayland has no global hotkeys. -1 = none (windows)
int ctl_read(int fd, char *buf, int n); // never blocks, bytes read
int ctl_send(const char *data, const char *msg); // 0 sent, -1 nothing is listening

typedef struct {
    char url[600], kind[8], pick[256], out[1200], data[1100], helper[1024], slug[48];
} DlJob;

intptr_t dl_start(const DlJob *j);
intptr_t lists_start(const char *data, const char *a, const char *b);
intptr_t fetch_start(const char *url, const char *out); // small file, short timeout, out only shows up if it worked
intptr_t stage_start(const char *home, const char *branch); // get.sh --stage, next version into app.new
intptr_t tgz_start(const char *url, const char *dir); // download a .tar.gz and unpack it into dir
int job_done(intptr_t h, int *ok);
int one_instance(const char *data); // 0 = another one is already running on this data folder
void job_kill(intptr_t h);
void rm_tree(const char *path);
int clip_get(char *out, int n); // clipboard text, 0 if theres none
int on_path(const char *name, char *out, size_t n);
intptr_t cmd_start(const char *cmd, const char *dir, const char *log); // shell command inside dir, all its output into log. job_done/job_kill it

void engine_path_for(const char *data, const char *slug, char *out, size_t n);
void engine_remove(const char *data, const char *path);
void engine_list(const char *dir, void (*add)(const char *label, const char *path));

#endif
