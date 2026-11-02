#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

int gpu_mode = 1, gpu_verbose; // 0 off, 1 use it if theres one

#ifdef __APPLE__
void gpu_frame(void) {}
int gpu_blur_rows(const Frame *f, int y0, int step, int nrows, uint8_t *sm, uint16_t *ed) { return 0; }
int gpu_edges(const Frame *f, const int *jobs, int n, int o, const uint8_t *light, const uint8_t *dark, int *out) { return 0; }
void gpu_broken(const char *what) {}
#else

#ifdef _WIN32
#include <windows.h>
#define CLAPI __stdcall
#else
#include <dlfcn.h>
#define CLAPI
#endif

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_bits;
typedef void *cl_obj;

#define CL_DEVICE_TYPE_GPU (1 << 2)
#define CL_DEVICE_TYPE_ALL 0xFFFFFFFF
#define CL_DEVICE_NAME 0x102B
#define CL_PROGRAM_BUILD_LOG 0x1183
#define CL_CONTEXT_PLATFORM 0x1084
#define CL_MEM_READ_WRITE 1
#define CL_MEM_WRITE_ONLY 2
#define CL_MEM_READ_ONLY 4

static struct {
    cl_int(CLAPI *GetPlatformIDs)(cl_uint, cl_obj *, cl_uint *);
    cl_int(CLAPI *GetDeviceIDs)(cl_obj, cl_bits, cl_uint, cl_obj *, cl_uint *);
    cl_int(CLAPI *GetDeviceInfo)(cl_obj, cl_uint, size_t, void *, size_t *);
    cl_obj(CLAPI *CreateContext)(const intptr_t *, cl_uint, const cl_obj *, void *, void *, cl_int *);
    cl_obj(CLAPI *CreateCommandQueue)(cl_obj, cl_obj, cl_bits, cl_int *);
    cl_obj(CLAPI *CreateProgramWithSource)(cl_obj, cl_uint, const char **, const size_t *, cl_int *);
    cl_int(CLAPI *BuildProgram)(cl_obj, cl_uint, const cl_obj *, const char *, void *, void *);
    cl_int(CLAPI *GetProgramBuildInfo)(cl_obj, cl_obj, cl_uint, size_t, void *, size_t *);
    cl_obj(CLAPI *CreateKernel)(cl_obj, const char *, cl_int *);
    cl_obj(CLAPI *CreateBuffer)(cl_obj, cl_bits, size_t, void *, cl_int *);
    cl_int(CLAPI *ReleaseMemObject)(cl_obj);
    cl_int(CLAPI *SetKernelArg)(cl_obj, cl_uint, size_t, const void *);
    cl_int(CLAPI *EnqueueWriteBuffer)(cl_obj, cl_obj, cl_uint, size_t, size_t, const void *, cl_uint, const void *, void *);
    cl_int(CLAPI *EnqueueReadBuffer)(cl_obj, cl_obj, cl_uint, size_t, size_t, void *, cl_uint, const void *, void *);
    cl_int(CLAPI *EnqueueNDRangeKernel)(cl_obj, cl_obj, cl_uint, const size_t *, const size_t *, const size_t *, cl_uint, const void *, void *);
    cl_int(CLAPI *Finish)(cl_obj);
} C;

// same math as soft_row and edge_count in vision.c, integers only so it comes out bit for bit the same
static const char *src =
    "__kernel void blur(__global const uchar *img, int stride, int w, int y0, int step, int bgr, __global uchar *sm)\n"
    "{\n"
    "    int x = get_global_id(0), row = get_global_id(1), y = y0 + row * step;\n"
    "    int a = max(x - 2, 0), b = min(x + 3, w), s0 = 0, s1 = 0, s2 = 0;\n"
    "    for (int yy = y - 2; yy <= y + 2; yy++) {\n"
    "        __global const uchar *p = img + (size_t)yy * stride;\n"
    "        for (int xx = a; xx < b; xx++) s0 += p[xx * 4], s1 += p[xx * 4 + 1], s2 += p[xx * 4 + 2];\n"
    "    }\n"
    "    int d = 5 * (b - a);\n"
    "    __global uchar *o = sm + ((size_t)row * w + x) * 4;\n"
    "    o[0] = (uchar)((bgr ? s2 : s0) / d), o[1] = (uchar)(s1 / d), o[2] = (uchar)((bgr ? s0 : s2) / d), o[3] = 0;\n"
    "}\n"
    "__kernel void edge(__global const uchar *sm, int w, __global ushort *ed)\n"
    "{\n"
    "    int x = get_global_id(0), row = get_global_id(1);\n"
    "    __global const uchar *r = sm + (size_t)row * w * 4;\n"
    "    int a = max(x - 3, 0) * 4, b = min(x + 3, w - 1) * 4;\n"
    "    ed[(size_t)row * w + x] = (ushort)(abs((int)r[b] - r[a]) + abs((int)r[b + 1] - r[a + 1]) + abs((int)r[b + 2] - r[a + 2]));\n"
    "}\n"
    "int cd(__global const uchar *p, int bgr, int c)\n"
    "{\n"
    "    int r = bgr ? p[2] : p[0], b = bgr ? p[0] : p[2];\n"
    "    return abs(r - (c & 255)) + abs(p[1] - ((c >> 8) & 255)) + abs(b - ((c >> 16) & 255));\n"
    "}\n"
    "int flip(__global const uchar *a, __global const uchar *b, int bgr, int l, int d)\n"
    "{\n"
    "    return (cd(a, bgr, l) < 40 && cd(b, bgr, d) < 40) || (cd(a, bgr, d) < 40 && cd(b, bgr, l) < 40);\n"
    "}\n"
    "__kernel void lines(__global const uchar *img, int stride, int w, int h, int bgr, __global const int *jobs, int o, int l, int d,\n"
    "                    __global int *out)\n"
    "{\n"
    "    int i = get_global_id(0), dir = jobs[i * 4], pos = jobs[i * 4 + 1], a0 = jobs[i * 4 + 2], a1 = jobs[i * 4 + 3], n = 0;\n"
    "    int lim = dir ? h : w, len = dir ? w : h;\n"
    "    if (pos - 1 - o < 0 || pos + o >= lim) {\n"
    "        out[i] = 0;\n"
    "        return;\n"
    "    }\n"
    "    a0 = max(a0, 0), a1 = min(a1, len);\n"
    "    for (int t = a0; t < a1; t++) {\n"
    "        size_t pa, pb, p2;\n"
    "        if (dir) pa = (size_t)(pos - 1 - o) * stride + t * 4, pb = (size_t)(pos + o) * stride + t * 4, p2 = (size_t)(pos - 2 - o) * stride + t * 4;\n"
    "        else pa = (size_t)t * stride + (pos - 1 - o) * 4, pb = (size_t)t * stride + (pos + o) * 4, p2 = (size_t)t * stride + (pos - 2 - o) * 4;\n"
    "        if (flip(img + pa, img + pb, bgr, l, d)) n += 2;\n"
    "        else if (pos - 2 - o >= 0 && flip(img + p2, img + pb, bgr, l, d)) n++;\n"
    "    }\n"
    "    out[i] = n;\n"
    "}\n";

static int ready; // 0 not tried yet, 1 good, -1 none or broken
static cl_obj ctx, q, prog, k_blur, k_edge, k_lines;
static char devname[128];

typedef struct {
    cl_obj m;
    size_t cap;
} Buf;

static Buf b_img, b_sm, b_ed, b_jobs, b_out;
static const uint8_t *up_px; // whats in b_img right now
static int up_w, up_h, up_stride, up_gen, gen;

void gpu_frame(void) { gen++; } // new screen grab, the old upload is stale

void gpu_broken(const char *what)
{
    if (ready == 1) fprintf(stderr, "gpu vision broke (%s), back to the cpu\n", what);
    ready = -1;
}

static void *lib_sym(void *lib, const char *name)
{
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

static int start(void)
{
#ifdef _WIN32
    void *lib = (void *)LoadLibraryA("OpenCL.dll");
#else
    void *lib = dlopen("libOpenCL.so.1", RTLD_NOW);
    if (!lib) lib = dlopen("libOpenCL.so", RTLD_NOW);
#endif
    if (!lib) return 0;
#define SYM(field, name)                                   \
    if (!(*(void **)(&C.field) = lib_sym(lib, name))) return 0;
    SYM(GetPlatformIDs, "clGetPlatformIDs")
    SYM(GetDeviceIDs, "clGetDeviceIDs")
    SYM(GetDeviceInfo, "clGetDeviceInfo")
    SYM(CreateContext, "clCreateContext")
    SYM(CreateCommandQueue, "clCreateCommandQueue")
    SYM(CreateProgramWithSource, "clCreateProgramWithSource")
    SYM(BuildProgram, "clBuildProgram")
    SYM(GetProgramBuildInfo, "clGetProgramBuildInfo")
    SYM(CreateKernel, "clCreateKernel")
    SYM(CreateBuffer, "clCreateBuffer")
    SYM(ReleaseMemObject, "clReleaseMemObject")
    SYM(SetKernelArg, "clSetKernelArg")
    SYM(EnqueueWriteBuffer, "clEnqueueWriteBuffer")
    SYM(EnqueueReadBuffer, "clEnqueueReadBuffer")
    SYM(EnqueueNDRangeKernel, "clEnqueueNDRangeKernel")
    SYM(Finish, "clFinish")
#undef SYM
    cl_obj plats[8], dev = NULL, plat = NULL;
    cl_uint np = 0;
    if (C.GetPlatformIDs(8, plats, &np) || !np) return 0;
    if (np > 8) np = 8;
    cl_bits want = getenv("OPEN_CHEASE_GPU_ANY") ? CL_DEVICE_TYPE_ALL : CL_DEVICE_TYPE_GPU; // test hook, lets pocl on the cpu stand in
    for (cl_uint i = 0; i < np && !dev; i++) {
        cl_uint nd = 0;
        if (C.GetDeviceIDs(plats[i], want, 1, &dev, &nd) || !nd) dev = NULL;
        else plat = plats[i];
    }
    if (!dev) return 0;
    if (C.GetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(devname) - 1, devname, NULL)) snprintf(devname, sizeof(devname), "?");
    cl_int err = 0;
    intptr_t props[3] = {CL_CONTEXT_PLATFORM, (intptr_t)plat, 0};
    ctx = C.CreateContext(props, 1, &dev, NULL, NULL, &err);
    if (!ctx || err) return 0;
    q = C.CreateCommandQueue(ctx, dev, 0, &err);
    if (!q || err) return 0;
    prog = C.CreateProgramWithSource(ctx, 1, &src, NULL, &err);
    if (!prog || err) return 0;
    if (C.BuildProgram(prog, 1, &dev, "", NULL, NULL)) {
        if (gpu_verbose) {
            char log[2048] = "";
            C.GetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, sizeof(log) - 1, log, NULL);
            fprintf(stderr, "gpu vision: kernels didnt build on %s\n%s\n", devname, log);
        }
        return 0;
    }
    k_blur = C.CreateKernel(prog, "blur", &err);
    if (!k_blur || err) return 0;
    k_edge = C.CreateKernel(prog, "edge", &err);
    if (!k_edge || err) return 0;
    k_lines = C.CreateKernel(prog, "lines", &err);
    if (!k_lines || err) return 0;
    if (gpu_verbose) fprintf(stderr, "gpu vision on %s\n", devname);
    return 1;
}

static int on(void)
{
    if (!gpu_mode) return 0;
    if (!ready) {
        ready = start() ? 1 : -1;
        if (ready < 0 && gpu_verbose) fprintf(stderr, "no opencl gpu, vision stays on the cpu\n");
    }
    return ready == 1;
}

static int need(Buf *b, size_t n, cl_bits flags)
{
    if (b->m && b->cap >= n) return 1;
    if (b->m) C.ReleaseMemObject(b->m);
    cl_int err = 0;
    b->cap = n + n / 4;
    b->m = C.CreateBuffer(ctx, flags, b->cap, NULL, &err);
    if (!b->m || err) {
        b->m = NULL, b->cap = 0;
        return 0;
    }
    return 1;
}

static int upload(const Frame *f)
{
    if (f->stride % 4 || f->w < 1 || f->h < 1) return 0;
    if (up_px == f->px && up_w == f->w && up_h == f->h && up_stride == f->stride && up_gen == gen) return 1;
    size_t n = (size_t)(f->h - 1) * f->stride + (size_t)f->w * 4; // a crop shares rows with the full frame, dont read past it
    if (!need(&b_img, n, CL_MEM_READ_ONLY) || C.EnqueueWriteBuffer(q, b_img.m, 1, 0, n, f->px, 0, NULL, NULL)) return 0;
    up_px = f->px, up_w = f->w, up_h = f->h, up_stride = f->stride, up_gen = gen;
    return 1;
}

#define ARG(k, i, v)                                     \
    if (C.SetKernelArg(k, i, sizeof(v), &(v))) goto fail;

int gpu_blur_rows(const Frame *f, int y0, int step, int nrows, uint8_t *sm, uint16_t *ed)
{
    if ((size_t)f->w * nrows < 100000 || !on()) return 0; // small ones are quicker on the cpu
    int w = f->w, stride = f->stride, bgr = f->bgr;
    size_t ns = (size_t)w * nrows * 4, ne = (size_t)w * nrows * 2, gs[2] = {(size_t)w, (size_t)nrows};
    if (!upload(f) || !need(&b_sm, ns, CL_MEM_READ_WRITE) || !need(&b_ed, ne, CL_MEM_WRITE_ONLY)) goto fail;
    ARG(k_blur, 0, b_img.m)
    ARG(k_blur, 1, stride)
    ARG(k_blur, 2, w)
    ARG(k_blur, 3, y0)
    ARG(k_blur, 4, step)
    ARG(k_blur, 5, bgr)
    ARG(k_blur, 6, b_sm.m)
    ARG(k_edge, 0, b_sm.m)
    ARG(k_edge, 1, w)
    ARG(k_edge, 2, b_ed.m)
    if (C.EnqueueNDRangeKernel(q, k_blur, 2, NULL, gs, NULL, 0, NULL, NULL) || C.EnqueueNDRangeKernel(q, k_edge, 2, NULL, gs, NULL, 0, NULL, NULL))
        goto fail;
    if (C.EnqueueReadBuffer(q, b_sm.m, 1, 0, ns, sm, 0, NULL, NULL) || C.EnqueueReadBuffer(q, b_ed.m, 1, 0, ne, ed, 0, NULL, NULL)) goto fail;
    return 1;
fail:
    gpu_broken("blur");
    return 0;
}

int gpu_edges(const Frame *f, const int *jobs, int n, int o, const uint8_t *light, const uint8_t *dark, int *out)
{
    if (n < 200 || !on()) return 0;
    int w = f->w, h = f->h, stride = f->stride, bgr = f->bgr;
    int l = light[0] | light[1] << 8 | light[2] << 16, d = dark[0] | dark[1] << 8 | dark[2] << 16;
    size_t gs = (size_t)n;
    if (!upload(f) || !need(&b_jobs, (size_t)n * 16, CL_MEM_READ_ONLY) || !need(&b_out, (size_t)n * 4, CL_MEM_WRITE_ONLY)) goto fail;
    if (C.EnqueueWriteBuffer(q, b_jobs.m, 1, 0, (size_t)n * 16, jobs, 0, NULL, NULL)) goto fail;
    ARG(k_lines, 0, b_img.m)
    ARG(k_lines, 1, stride)
    ARG(k_lines, 2, w)
    ARG(k_lines, 3, h)
    ARG(k_lines, 4, bgr)
    ARG(k_lines, 5, b_jobs.m)
    ARG(k_lines, 6, o)
    ARG(k_lines, 7, l)
    ARG(k_lines, 8, d)
    ARG(k_lines, 9, b_out.m)
    if (C.EnqueueNDRangeKernel(q, k_lines, 1, NULL, &gs, NULL, 0, NULL, NULL)) goto fail;
    if (C.EnqueueReadBuffer(q, b_out.m, 1, 0, (size_t)n * 4, out, 0, NULL, NULL)) goto fail;
    return 1;
fail:
    gpu_broken("lines");
    return 0;
}
#endif
