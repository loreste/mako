/* GPU / accelerator seed — portable compute for AI workloads.
 *
 * North star: building blocks for inference / training in Mako
 * (matmul, activations, bias, reductions) — not a graphics stack,
 * not a full ML framework. Compose layers in Mako; we supply tensors
 * as f32 buffers + kernels on multi-vendor GPUs.
 *
 * Backends (prefer first that works):
 *   1. OpenCL  — NVIDIA / AMD / Intel (Linux, Windows ICDs) and macOS
 *   2. host    — always available CPU reference path
 *
 * Same buffer + f32 kernel surface regardless of backend.
 * Cap: 64 MiB per buffer. Handles are process-local ints.
 * Define MAKO_HAS_OPENCL (+ link OpenCL) to enable the OpenCL path.
 */
#ifndef MAKO_GPU_H
#define MAKO_GPU_H

#include "mako_rt.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#if defined(MAKO_HAS_OPENCL)
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif
#define MAKO_GPU_OPENCL 1
#else
#define MAKO_GPU_OPENCL 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_GPU_MAX_DEVICES 4
#define MAKO_GPU_MAX_BUFS 64
#define MAKO_GPU_MAX_BYTES (64 * 1024 * 1024)

#define MAKO_GPU_KIND_HOST 0
#define MAKO_GPU_KIND_OPENCL 1

#if MAKO_GPU_OPENCL
/* AI-oriented f32 kernels — one program, multi-vendor OpenCL C.
 * Layouts are row-major. Matmul: C[m,n] = A[m,k] @ B[k,n]. */
static const char *mako_gpu_ocl_src =
    "__kernel void mako_add_f32(__global float *o, __global const float *a,\n"
    "                           __global const float *b, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = a[i] + b[i];\n"
    "}\n"
    "__kernel void mako_mul_f32(__global float *o, __global const float *a,\n"
    "                           __global const float *b, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = a[i] * b[i];\n"
    "}\n"
    "__kernel void mako_scale_f32(__global float *o, __global const float *a,\n"
    "                             const float s, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = a[i] * s;\n"
    "}\n"
    "__kernel void mako_fill_f32(__global float *o, const float v, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = v;\n"
    "}\n"
    "__kernel void mako_relu_f32(__global float *o, __global const float *a, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = a[i] > 0.0f ? a[i] : 0.0f;\n"
    "}\n"
    "__kernel void mako_saxpy_f32(__global float *o, __global const float *a,\n"
    "                             __global const float *b, const float alpha, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) o[i] = alpha * a[i] + b[i];\n"
    "}\n"
    /* out[row*n + col] += bias[col] — broadcast bias over batch/rows */
    "__kernel void mako_bias_add_f32(__global float *o, __global const float *a,\n"
    "                                __global const float *bias, const int rows, const int cols) {\n"
    "  int i = get_global_id(0);\n"
    "  int total = rows * cols;\n"
    "  if (i < total) o[i] = a[i] + bias[i % cols];\n"
    "}\n"
    "__kernel void mako_matmul_f32(__global float *c, __global const float *a,\n"
    "                              __global const float *b,\n"
    "                              const int m, const int n, const int k) {\n"
    "  int row = get_global_id(0);\n"
    "  int col = get_global_id(1);\n"
    "  if (row < m && col < n) {\n"
    "    float acc = 0.0f;\n"
    "    for (int t = 0; t < k; t++)\n"
    "      acc += a[row * k + t] * b[t * n + col];\n"
    "    c[row * n + col] = acc;\n"
    "  }\n"
    "}\n"
    /* Softmax over last dim: each row of length cols (rows batches). */
    "__kernel void mako_softmax_rows_f32(__global float *o, __global const float *a,\n"
    "                                    const int rows, const int cols) {\n"
    "  int r = get_global_id(0);\n"
    "  if (r >= rows) return;\n"
    "  float mval = a[r * cols];\n"
    "  for (int j = 1; j < cols; j++) {\n"
    "    float v = a[r * cols + j];\n"
    "    if (v > mval) mval = v;\n"
    "  }\n"
    "  float sum = 0.0f;\n"
    "  for (int j = 0; j < cols; j++) {\n"
    "    float e = exp(a[r * cols + j] - mval);\n"
    "    o[r * cols + j] = e;\n"
    "    sum += e;\n"
    "  }\n"
    "  float inv = sum > 0.0f ? 1.0f / sum : 0.0f;\n"
    "  for (int j = 0; j < cols; j++) o[r * cols + j] *= inv;\n"
    "}\n"
    "__kernel void mako_gelu_f32(__global float *o, __global const float *a, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) {\n"
    "    float x = a[i];\n"
    "    float c = 0.7978845608f; /* sqrt(2/pi) */\n"
    "    float u = c * (x + 0.044715f * x * x * x);\n"
    "    o[i] = 0.5f * x * (1.0f + tanh(u));\n"
    "  }\n"
    "}\n"
    "__kernel void mako_silu_f32(__global float *o, __global const float *a, const int n) {\n"
    "  int i = get_global_id(0);\n"
    "  if (i < n) {\n"
    "    float x = a[i];\n"
    "    o[i] = x / (1.0f + exp(-x));\n"
    "  }\n"
    "}\n"
    "__kernel void mako_transpose_f32(__global float *o, __global const float *a,\n"
    "                                 const int rows, const int cols) {\n"
    "  int i = get_global_id(0);\n"
    "  int j = get_global_id(1);\n"
    "  if (i < rows && j < cols) o[j * rows + i] = a[i * cols + j];\n"
    "}\n"
    "__kernel void mako_layernorm_f32(__global float *o, __global const float *a,\n"
    "                                 __global const float *gamma, __global const float *beta,\n"
    "                                 const int rows, const int cols, const float eps,\n"
    "                                 const int use_affine) {\n"
    "  int r = get_global_id(0);\n"
    "  if (r >= rows) return;\n"
    "  float mean = 0.0f;\n"
    "  for (int j = 0; j < cols; j++) mean += a[r * cols + j];\n"
    "  mean /= (float)cols;\n"
    "  float var = 0.0f;\n"
    "  for (int j = 0; j < cols; j++) {\n"
    "    float d = a[r * cols + j] - mean;\n"
    "    var += d * d;\n"
    "  }\n"
    "  var /= (float)cols;\n"
    "  float inv = rsqrt(var + eps);\n"
    "  for (int j = 0; j < cols; j++) {\n"
    "    float v = (a[r * cols + j] - mean) * inv;\n"
    "    if (use_affine) v = v * gamma[j] + beta[j];\n"
    "    o[r * cols + j] = v;\n"
    "  }\n"
    "}\n";
#endif

typedef struct {
    int live;
    int kind; /* HOST or OPENCL */
    char backend[16];
    char name[128];
    char vendor[128];
#if MAKO_GPU_OPENCL
    cl_context ctx;
    cl_command_queue q;
    cl_device_id device;
    cl_program prog;
    cl_kernel k_add;
    cl_kernel k_mul;
    cl_kernel k_scale;
    cl_kernel k_fill;
    cl_kernel k_relu;
    cl_kernel k_saxpy;
    cl_kernel k_bias;
    cl_kernel k_matmul;
    cl_kernel k_softmax;
    cl_kernel k_gelu;
    cl_kernel k_silu;
    cl_kernel k_transpose;
    cl_kernel k_layernorm;
#endif
} MakoGpuDev;

typedef struct {
    int live;
    int dev; /* index into mako_gpu_devs */
    size_t cap;
    size_t len;
    unsigned char *data; /* host shadow (always) */
#if MAKO_GPU_OPENCL
    cl_mem mem;
    int host_dirty;   /* 1 = host shadow needs upload before next kernel */
    int device_dirty; /* 1 = device has newer data than host shadow */
#endif
} MakoGpuBuf;

static MakoGpuDev mako_gpu_devs[MAKO_GPU_MAX_DEVICES];
static MakoGpuBuf mako_gpu_bufs[MAKO_GPU_MAX_BUFS];
static int mako_gpu_prefer_host = 0; /* tests / debug: force host */

static inline float mako_gpu_load_f32(const unsigned char *p) {
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline void mako_gpu_store_f32(unsigned char *p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    p[0] = (unsigned char)(u & 0xff);
    p[1] = (unsigned char)((u >> 8) & 0xff);
    p[2] = (unsigned char)((u >> 16) & 0xff);
    p[3] = (unsigned char)((u >> 24) & 0xff);
}

/* ---- OpenCL discovery ---- */

static inline int64_t mako_gpu_opencl_linked(void) {
#if MAKO_GPU_OPENCL
    return 1;
#else
    return 0;
#endif
}

#if MAKO_GPU_OPENCL
static inline int mako_gpu_ocl_pick_device(cl_platform_id *out_plat, cl_device_id *out_dev) {
    cl_uint np = 0;
    if (clGetPlatformIDs(0, NULL, &np) != CL_SUCCESS || np == 0) return 0;
    cl_platform_id *plats = (cl_platform_id *)calloc(np, sizeof(cl_platform_id));
    if (!plats) return 0;
    if (clGetPlatformIDs(np, plats, NULL) != CL_SUCCESS) {
        free(plats);
        return 0;
    }
    /* Prefer GPU, then any device (CPU OpenCL as last OpenCL resort). */
    cl_device_type prefs[2] = {CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_ALL};
    for (int pass = 0; pass < 2; pass++) {
        for (cl_uint p = 0; p < np; p++) {
            cl_uint nd = 0;
            if (clGetDeviceIDs(plats[p], prefs[pass], 0, NULL, &nd) != CL_SUCCESS || nd == 0)
                continue;
            cl_device_id *devs = (cl_device_id *)calloc(nd, sizeof(cl_device_id));
            if (!devs) continue;
            if (clGetDeviceIDs(plats[p], prefs[pass], nd, devs, NULL) != CL_SUCCESS) {
                free(devs);
                continue;
            }
            *out_plat = plats[p];
            *out_dev = devs[0];
            free(devs);
            free(plats);
            return 1;
        }
    }
    free(plats);
    return 0;
}

static inline int mako_gpu_ocl_build(MakoGpuDev *d) {
    cl_int err = 0;
    d->prog = clCreateProgramWithSource(d->ctx, 1, &mako_gpu_ocl_src, NULL, &err);
    if (err != CL_SUCCESS || !d->prog) return 0;
    err = clBuildProgram(d->prog, 1, &d->device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        size_t logn = 0;
        clGetProgramBuildInfo(d->prog, d->device, CL_PROGRAM_BUILD_LOG, 0, NULL, &logn);
        if (logn > 1 && logn < 4096) {
            char *log = (char *)malloc(logn);
            if (log) {
                clGetProgramBuildInfo(
                    d->prog, d->device, CL_PROGRAM_BUILD_LOG, logn, log, NULL
                );
                fprintf(stderr, "mako gpu: OpenCL build failed:\n%s\n", log);
                free(log);
            }
        }
        clReleaseProgram(d->prog);
        d->prog = NULL;
        return 0;
    }
    d->k_add = clCreateKernel(d->prog, "mako_add_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_mul = clCreateKernel(d->prog, "mako_mul_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_scale = clCreateKernel(d->prog, "mako_scale_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_fill = clCreateKernel(d->prog, "mako_fill_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_relu = clCreateKernel(d->prog, "mako_relu_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_saxpy = clCreateKernel(d->prog, "mako_saxpy_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_bias = clCreateKernel(d->prog, "mako_bias_add_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_matmul = clCreateKernel(d->prog, "mako_matmul_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_softmax = clCreateKernel(d->prog, "mako_softmax_rows_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_gelu = clCreateKernel(d->prog, "mako_gelu_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_silu = clCreateKernel(d->prog, "mako_silu_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_transpose = clCreateKernel(d->prog, "mako_transpose_f32", &err);
    if (err != CL_SUCCESS) return 0;
    d->k_layernorm = clCreateKernel(d->prog, "mako_layernorm_f32", &err);
    if (err != CL_SUCCESS) return 0;
    return 1;
}

static inline int mako_gpu_ocl_init_slot(int di) {
    cl_platform_id plat;
    cl_device_id dev;
    if (!mako_gpu_ocl_pick_device(&plat, &dev)) return 0;
    cl_int err = 0;
    cl_context ctx = clCreateContext(NULL, 1, &dev, NULL, NULL, &err);
    if (err != CL_SUCCESS || !ctx) return 0;
    /* 1.x queue API — portable across Apple, NVIDIA, AMD, Intel ICDs. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    cl_command_queue q = clCreateCommandQueue(ctx, dev, 0, &err);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if (err != CL_SUCCESS || !q) {
        clReleaseContext(ctx);
        return 0;
    }
    MakoGpuDev *d = &mako_gpu_devs[di];
    memset(d, 0, sizeof(*d));
    d->live = 1;
    d->kind = MAKO_GPU_KIND_OPENCL;
    snprintf(d->backend, sizeof(d->backend), "opencl");
    d->ctx = ctx;
    d->q = q;
    d->device = dev;
    clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(d->name), d->name, NULL);
    clGetDeviceInfo(dev, CL_DEVICE_VENDOR, sizeof(d->vendor), d->vendor, NULL);
    if (!mako_gpu_ocl_build(d)) {
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        memset(d, 0, sizeof(*d));
        return 0;
    }
    return 1;
}

static inline void mako_gpu_ocl_release_dev(MakoGpuDev *d) {
    if (!d || d->kind != MAKO_GPU_KIND_OPENCL) return;
    if (d->k_add) clReleaseKernel(d->k_add);
    if (d->k_mul) clReleaseKernel(d->k_mul);
    if (d->k_scale) clReleaseKernel(d->k_scale);
    if (d->k_fill) clReleaseKernel(d->k_fill);
    if (d->k_relu) clReleaseKernel(d->k_relu);
    if (d->k_saxpy) clReleaseKernel(d->k_saxpy);
    if (d->k_bias) clReleaseKernel(d->k_bias);
    if (d->k_matmul) clReleaseKernel(d->k_matmul);
    if (d->k_softmax) clReleaseKernel(d->k_softmax);
    if (d->k_gelu) clReleaseKernel(d->k_gelu);
    if (d->k_silu) clReleaseKernel(d->k_silu);
    if (d->k_transpose) clReleaseKernel(d->k_transpose);
    if (d->k_layernorm) clReleaseKernel(d->k_layernorm);
    if (d->prog) clReleaseProgram(d->prog);
    if (d->q) clReleaseCommandQueue(d->q);
    if (d->ctx) clReleaseContext(d->ctx);
}

static inline int mako_gpu_ocl_sync_to_device(MakoGpuBuf *b, MakoGpuDev *d) {
    if (!b || !b->mem || !d) return -1;
    if (!b->host_dirty || b->len == 0) return 0; /* device already current */
    cl_int err = clEnqueueWriteBuffer(
        d->q, b->mem, CL_TRUE, 0, b->len, b->data, 0, NULL, NULL
    );
    if (err != CL_SUCCESS) return -1;
    b->host_dirty = 0;
    b->device_dirty = 0;
    return 0;
}

static inline int mako_gpu_ocl_sync_to_host(MakoGpuBuf *b, MakoGpuDev *d) {
    if (!b || !b->mem || !d) return -1;
    if (!b->device_dirty || b->len == 0) return 0;
    cl_int err = clEnqueueReadBuffer(
        d->q, b->mem, CL_TRUE, 0, b->len, b->data, 0, NULL, NULL
    );
    if (err != CL_SUCCESS) return -1;
    b->device_dirty = 0;
    b->host_dirty = 0;
    return 0;
}
#endif /* MAKO_GPU_OPENCL */

/* ---- Public probes ---- */

static inline int64_t mako_gpu_available(void) {
    return 1; /* host always; OpenCL when linked and devices exist */
}

/* What the next gpu_device_open() prefers. */
static inline MakoString mako_gpu_backend(void) {
    if (mako_gpu_prefer_host) return mako_str_from_cstr("host");
#if MAKO_GPU_OPENCL
    cl_platform_id plat;
    cl_device_id dev;
    if (mako_gpu_ocl_pick_device(&plat, &dev))
        return mako_str_from_cstr("opencl");
#endif
    return mako_str_from_cstr("host");
}

static inline int64_t mako_gpu_opencl_ok(void) {
#if MAKO_GPU_OPENCL
    if (mako_gpu_prefer_host) return 0;
    cl_platform_id plat;
    cl_device_id dev;
    return mako_gpu_ocl_pick_device(&plat, &dev) ? 1 : 0;
#else
    return 0;
#endif
}

/* Metal / CUDA / Vulkan availability stubs (0 unless build flags). Product backends Later. */
static inline int64_t mako_gpu_metal_ok(void) {
#if defined(MAKO_HAS_METAL)
    return 1;
#else
    return 0;
#endif
}

static inline int64_t mako_gpu_cuda_ok(void) {
#if defined(MAKO_HAS_CUDA)
    return 1;
#else
    return 0;
#endif
}

static inline int64_t mako_gpu_vulkan_ok(void) {
#if defined(MAKO_HAS_VULKAN)
    return 1;
#else
    return 0;
#endif
}

/* Force host path (1) or restore prefer-OpenCL (0). Returns previous. */
static inline int64_t mako_gpu_set_prefer_host(int64_t on) {
    int prev = mako_gpu_prefer_host;
    mako_gpu_prefer_host = on ? 1 : 0;
    return prev;
}

/* ---- Device open/close ---- */

static inline int64_t mako_gpu_open_host_slot(void) {
    for (int i = 0; i < MAKO_GPU_MAX_DEVICES; i++) {
        if (!mako_gpu_devs[i].live) {
            memset(&mako_gpu_devs[i], 0, sizeof(MakoGpuDev));
            mako_gpu_devs[i].live = 1;
            mako_gpu_devs[i].kind = MAKO_GPU_KIND_HOST;
            snprintf(mako_gpu_devs[i].backend, sizeof(mako_gpu_devs[i].backend), "host");
            snprintf(mako_gpu_devs[i].name, sizeof(mako_gpu_devs[i].name), "host-cpu");
            snprintf(mako_gpu_devs[i].vendor, sizeof(mako_gpu_devs[i].vendor), "mako");
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_gpu_device_open(void) {
#if MAKO_GPU_OPENCL
    if (!mako_gpu_prefer_host) {
        for (int i = 0; i < MAKO_GPU_MAX_DEVICES; i++) {
            if (!mako_gpu_devs[i].live) {
                if (mako_gpu_ocl_init_slot(i)) return (int64_t)(i + 1);
                break; /* one attempt; fall through to host */
            }
        }
    }
#endif
    return mako_gpu_open_host_slot();
}

static inline MakoGpuDev *mako_gpu_dev_ref(int64_t dev) {
    if (dev < 1 || dev > MAKO_GPU_MAX_DEVICES) return NULL;
    MakoGpuDev *d = &mako_gpu_devs[dev - 1];
    return d->live ? d : NULL;
}

static inline int64_t mako_gpu_device_close(int64_t dev) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return 0;
    int di = (int)dev - 1;
    for (int i = 0; i < MAKO_GPU_MAX_BUFS; i++) {
        if (mako_gpu_bufs[i].live && mako_gpu_bufs[i].dev == di) {
#if MAKO_GPU_OPENCL
            if (mako_gpu_bufs[i].mem) {
                clReleaseMemObject(mako_gpu_bufs[i].mem);
                mako_gpu_bufs[i].mem = NULL;
            }
#endif
            free(mako_gpu_bufs[i].data);
            memset(&mako_gpu_bufs[i], 0, sizeof(MakoGpuBuf));
        }
    }
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL) mako_gpu_ocl_release_dev(d);
#endif
    memset(d, 0, sizeof(MakoGpuDev));
    return 1;
}

static inline MakoString mako_gpu_device_backend(int64_t dev) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return mako_str_from_cstr("");
    return mako_str_from_cstr(d->backend);
}

static inline MakoString mako_gpu_device_name(int64_t dev) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return mako_str_from_cstr("");
    return mako_str_from_cstr(d->name);
}

static inline MakoString mako_gpu_device_vendor(int64_t dev) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return mako_str_from_cstr("");
    return mako_str_from_cstr(d->vendor);
}

static inline int64_t mako_gpu_device_is_gpu(int64_t dev) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return 0;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL) {
        cl_device_type t = 0;
        clGetDeviceInfo(d->device, CL_DEVICE_TYPE, sizeof(t), &t, NULL);
        return (t & CL_DEVICE_TYPE_GPU) ? 1 : 0;
    }
#endif
    (void)d;
    return 0;
}

/* ---- Buffers ---- */

static inline MakoGpuBuf *mako_gpu_buf_ref(int64_t h) {
    if (h < 1 || h > MAKO_GPU_MAX_BUFS) return NULL;
    MakoGpuBuf *b = &mako_gpu_bufs[h - 1];
    return b->live ? b : NULL;
}

static inline int64_t mako_gpu_buf_new(int64_t dev, int64_t nbytes) {
    MakoGpuDev *d = mako_gpu_dev_ref(dev);
    if (!d) return -1;
    if (nbytes < 0 || nbytes > MAKO_GPU_MAX_BYTES) return -1;
    size_t cap = (size_t)nbytes;
    if (cap == 0) cap = 1;
    for (int i = 0; i < MAKO_GPU_MAX_BUFS; i++) {
        if (!mako_gpu_bufs[i].live) {
            unsigned char *data = (unsigned char *)calloc(1, cap);
            if (!data) return -1;
#if MAKO_GPU_OPENCL
            cl_mem mem = NULL;
            if (d->kind == MAKO_GPU_KIND_OPENCL) {
                cl_int err = 0;
                mem = clCreateBuffer(d->ctx, CL_MEM_READ_WRITE, cap, NULL, &err);
                if (err != CL_SUCCESS || !mem) {
                    free(data);
                    return -1;
                }
            }
#endif
            mako_gpu_bufs[i].live = 1;
            mako_gpu_bufs[i].dev = (int)dev - 1;
            mako_gpu_bufs[i].cap = cap;
            mako_gpu_bufs[i].len = (size_t)nbytes;
            mako_gpu_bufs[i].data = data;
#if MAKO_GPU_OPENCL
            mako_gpu_bufs[i].mem = mem;
            mako_gpu_bufs[i].host_dirty = 0;
            mako_gpu_bufs[i].device_dirty = 0;
#endif
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_gpu_buf_len(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    return b ? (int64_t)b->len : -1;
}

static inline int64_t mako_gpu_buf_cap(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    return b ? (int64_t)b->cap : -1;
}

static inline int64_t mako_gpu_buf_free(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b) return 0;
#if MAKO_GPU_OPENCL
    if (b->mem) {
        clReleaseMemObject(b->mem);
        b->mem = NULL;
    }
#endif
    free(b->data);
    memset(b, 0, sizeof(MakoGpuBuf));
    return 1;
}

static inline int64_t mako_gpu_buf_write(int64_t h, MakoString data) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b) return -1;
    if (data.len > b->cap) return -1;
    if (data.len && data.data) memcpy(b->data, data.data, data.len);
    if (data.len < b->cap) memset(b->data + data.len, 0, b->cap - data.len);
    b->len = data.len;
#if MAKO_GPU_OPENCL
    b->host_dirty = 1;
    b->device_dirty = 0;
    if (b->mem) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live && d->kind == MAKO_GPU_KIND_OPENCL && b->len) {
            if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        }
    }
#endif
    return (int64_t)data.len;
}

static inline MakoString mako_gpu_buf_read(int64_t h, int64_t max_bytes) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b) return mako_str_from_cstr("");
#if MAKO_GPU_OPENCL
    if (b->mem && b->device_dirty) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live) (void)mako_gpu_ocl_sync_to_host(b, d);
    }
#endif
    size_t n = b->len;
    if (max_bytes > 0 && (size_t)max_bytes < n) n = (size_t)max_bytes;
    char *out = (char *)malloc(n + 1);
    if (!out) mako_abort("gpu buf_read OOM");
    if (n) memcpy(out, b->data, n);
    out[n] = 0;
    return (MakoString){out, n};
}

static inline int64_t mako_gpu_upload_f32(int64_t h, MakoFloatArray vals) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b) return -1;
    size_t need = vals.len * 4u;
    if (need > b->cap) return -1;
    for (size_t i = 0; i < vals.len; i++) {
        float f = (float)(vals.data ? vals.data[i] : 0.0);
        mako_gpu_store_f32(b->data + i * 4, f);
    }
    b->len = need;
#if MAKO_GPU_OPENCL
    b->host_dirty = 1;
    b->device_dirty = 0;
    if (b->mem && need) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live && d->kind == MAKO_GPU_KIND_OPENCL) {
            if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        }
    }
#endif
    return (int64_t)vals.len;
}

static inline MakoFloatArray mako_gpu_download_f32(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b || b->len < 4) return mako_float_array_make(0, 0);
#if MAKO_GPU_OPENCL
    if (b->mem && b->device_dirty) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live) (void)mako_gpu_ocl_sync_to_host(b, d);
    }
#endif
    size_t n = b->len / 4;
    MakoFloatArray out = mako_float_array_make((int64_t)n, (int64_t)n);
    for (size_t i = 0; i < n; i++)
        out.data[i] = (double)mako_gpu_load_f32(b->data + i * 4);
    return out;
}

static inline int64_t mako_gpu_f32_count(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b) return -1;
    return (int64_t)(b->len / 4);
}

/* ---- Host kernels ---- */

static inline int64_t mako_gpu_add_f32_host(MakoGpuBuf *o, MakoGpuBuf *a, MakoGpuBuf *b) {
    size_t n = a->len / 4;
    if (b->len / 4 < n) n = b->len / 4;
    if (n * 4 > o->cap) return -1;
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        float y = mako_gpu_load_f32(b->data + i * 4);
        mako_gpu_store_f32(o->data + i * 4, x + y);
    }
    o->len = n * 4;
    return (int64_t)n;
}

static inline int64_t mako_gpu_mul_f32_host(MakoGpuBuf *o, MakoGpuBuf *a, MakoGpuBuf *b) {
    size_t n = a->len / 4;
    if (b->len / 4 < n) n = b->len / 4;
    if (n * 4 > o->cap) return -1;
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        float y = mako_gpu_load_f32(b->data + i * 4);
        mako_gpu_store_f32(o->data + i * 4, x * y);
    }
    o->len = n * 4;
    return (int64_t)n;
}

static inline int64_t mako_gpu_scale_f32_host(MakoGpuBuf *o, MakoGpuBuf *a, float s) {
    size_t n = a->len / 4;
    if (n * 4 > o->cap) return -1;
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        mako_gpu_store_f32(o->data + i * 4, x * s);
    }
    o->len = n * 4;
    return (int64_t)n;
}

static inline int64_t mako_gpu_fill_f32_host(MakoGpuBuf *o, int64_t n, float v) {
    if (n < 0) return -1;
    size_t need = (size_t)n * 4u;
    if (need > o->cap) return -1;
    for (int64_t i = 0; i < n; i++) mako_gpu_store_f32(o->data + (size_t)i * 4, v);
    o->len = need;
    return n;
}

/* ---- Dispatch (OpenCL or host) ---- */

static inline int64_t mako_gpu_add_f32(int64_t out_h, int64_t a_h, int64_t b_h) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    MakoGpuBuf *b = mako_gpu_buf_ref(b_h);
    if (!o || !a || !b) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem && b->mem) {
        size_t n = a->len / 4;
        if (b->len / 4 < n) n = b->len / 4;
        if (n * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_add, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_add, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_add, 2, sizeof(cl_mem), &b->mem);
        clSetKernelArg(d->k_add, 3, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_add, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
    return mako_gpu_add_f32_host(o, a, b);
}

static inline int64_t mako_gpu_mul_f32(int64_t out_h, int64_t a_h, int64_t b_h) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    MakoGpuBuf *b = mako_gpu_buf_ref(b_h);
    if (!o || !a || !b) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem && b->mem) {
        size_t n = a->len / 4;
        if (b->len / 4 < n) n = b->len / 4;
        if (n * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_mul, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_mul, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_mul, 2, sizeof(cl_mem), &b->mem);
        clSetKernelArg(d->k_mul, 3, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_mul, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
    return mako_gpu_mul_f32_host(o, a, b);
}

static inline int64_t mako_gpu_scale_f32(int64_t out_h, int64_t a_h, double scale) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    if (!o || !a) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
    float s = (float)scale;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem) {
        size_t n = a->len / 4;
        if (n * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_scale, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_scale, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_scale, 2, sizeof(float), &s);
        clSetKernelArg(d->k_scale, 3, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_scale, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
    return mako_gpu_scale_f32_host(o, a, s);
}

static inline int64_t mako_gpu_fill_f32(int64_t out_h, int64_t n, double value) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    if (!o || n < 0) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
    float v = (float)value;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem) {
        size_t need = (size_t)n * 4u;
        if (need > o->cap) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_fill, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_fill, 1, sizeof(float), &v);
        clSetKernelArg(d->k_fill, 2, sizeof(int), &ni);
        size_t g = n > 0 ? (size_t)n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_fill, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = need;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return n;
    }
#endif
    return mako_gpu_fill_f32_host(o, n, v);
}

/* ---- AI building blocks ---- */

static inline int64_t mako_gpu_relu_f32_host(MakoGpuBuf *o, MakoGpuBuf *a) {
    size_t n = a->len / 4;
    if (n * 4 > o->cap) return -1;
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        mako_gpu_store_f32(o->data + i * 4, x > 0.0f ? x : 0.0f);
    }
    o->len = n * 4;
    return (int64_t)n;
}

static inline int64_t mako_gpu_relu_f32(int64_t out_h, int64_t a_h) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    if (!o || !a) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem) {
        size_t n = a->len / 4;
        if (n * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_relu, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_relu, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_relu, 2, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_relu, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
    return mako_gpu_relu_f32_host(o, a);
}

/* out = alpha * a + b  (residual / affine). */
static inline int64_t mako_gpu_saxpy_f32_host(
    MakoGpuBuf *o, MakoGpuBuf *a, MakoGpuBuf *b, float alpha
) {
    size_t n = a->len / 4;
    if (b->len / 4 < n) n = b->len / 4;
    if (n * 4 > o->cap) return -1;
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        float y = mako_gpu_load_f32(b->data + i * 4);
        mako_gpu_store_f32(o->data + i * 4, alpha * x + y);
    }
    o->len = n * 4;
    return (int64_t)n;
}

static inline int64_t mako_gpu_saxpy_f32(
    int64_t out_h, int64_t a_h, int64_t b_h, double alpha
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    MakoGpuBuf *b = mako_gpu_buf_ref(b_h);
    if (!o || !a || !b) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
    float al = (float)alpha;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem && b->mem) {
        size_t n = a->len / 4;
        if (b->len / 4 < n) n = b->len / 4;
        if (n * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        int ni = (int)n;
        clSetKernelArg(d->k_saxpy, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_saxpy, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_saxpy, 2, sizeof(cl_mem), &b->mem);
        clSetKernelArg(d->k_saxpy, 3, sizeof(float), &al);
        clSetKernelArg(d->k_saxpy, 4, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_saxpy, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
    return mako_gpu_saxpy_f32_host(o, a, b, al);
}

/* out[i] = a[i] + bias[i % cols]; a is rows*cols, bias is cols. */
static inline int64_t mako_gpu_bias_add_f32_host(
    MakoGpuBuf *o, MakoGpuBuf *a, MakoGpuBuf *bias, int64_t rows, int64_t cols
) {
    if (rows <= 0 || cols <= 0) return -1;
    size_t total = (size_t)rows * (size_t)cols;
    if (a->len / 4 < total || bias->len / 4 < (size_t)cols || total * 4 > o->cap) return -1;
    for (size_t i = 0; i < total; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        float bi = mako_gpu_load_f32(bias->data + (i % (size_t)cols) * 4);
        mako_gpu_store_f32(o->data + i * 4, x + bi);
    }
    o->len = total * 4;
    return (int64_t)total;
}

static inline int64_t mako_gpu_bias_add_f32(
    int64_t out_h, int64_t a_h, int64_t bias_h, int64_t rows, int64_t cols
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    MakoGpuBuf *bias = mako_gpu_buf_ref(bias_h);
    if (!o || !a || !bias) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem && bias->mem) {
        if (rows <= 0 || cols <= 0) return -1;
        size_t total = (size_t)rows * (size_t)cols;
        if (a->len / 4 < total || bias->len / 4 < (size_t)cols || total * 4 > o->cap)
            return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        if (mako_gpu_ocl_sync_to_device(bias, d) < 0) return -1;
        int r = (int)rows, c = (int)cols;
        clSetKernelArg(d->k_bias, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_bias, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_bias, 2, sizeof(cl_mem), &bias->mem);
        clSetKernelArg(d->k_bias, 3, sizeof(int), &r);
        clSetKernelArg(d->k_bias, 4, sizeof(int), &c);
        size_t g = total ? total : 1;
        if (clEnqueueNDRangeKernel(d->q, d->k_bias, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = total * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)total;
    }
#endif
    return mako_gpu_bias_add_f32_host(o, a, bias, rows, cols);
}

/* C[m,n] = A[m,k] @ B[k,n], row-major. Returns m*n or -1. */
static inline int64_t mako_gpu_matmul_f32_host(
    MakoGpuBuf *c, MakoGpuBuf *a, MakoGpuBuf *b, int64_t m, int64_t n, int64_t k
) {
    if (m <= 0 || n <= 0 || k <= 0) return -1;
    size_t need_a = (size_t)m * (size_t)k;
    size_t need_b = (size_t)k * (size_t)n;
    size_t need_c = (size_t)m * (size_t)n;
    if (a->len / 4 < need_a || b->len / 4 < need_b || need_c * 4 > c->cap) return -1;
    for (int64_t i = 0; i < m; i++) {
        for (int64_t j = 0; j < n; j++) {
            float acc = 0.0f;
            for (int64_t t = 0; t < k; t++) {
                float av = mako_gpu_load_f32(a->data + ((size_t)i * (size_t)k + (size_t)t) * 4);
                float bv = mako_gpu_load_f32(b->data + ((size_t)t * (size_t)n + (size_t)j) * 4);
                acc += av * bv;
            }
            mako_gpu_store_f32(c->data + ((size_t)i * (size_t)n + (size_t)j) * 4, acc);
        }
    }
    c->len = need_c * 4;
    return (int64_t)need_c;
}

static inline int64_t mako_gpu_matmul_f32(
    int64_t out_h, int64_t a_h, int64_t b_h, int64_t m, int64_t n, int64_t k
) {
    MakoGpuBuf *c = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    MakoGpuBuf *b = mako_gpu_buf_ref(b_h);
    if (!c || !a || !b) return -1;
    MakoGpuDev *d = &mako_gpu_devs[c->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && c->mem && a->mem && b->mem) {
        if (m <= 0 || n <= 0 || k <= 0) return -1;
        size_t need_a = (size_t)m * (size_t)k;
        size_t need_b = (size_t)k * (size_t)n;
        size_t need_c = (size_t)m * (size_t)n;
        if (a->len / 4 < need_a || b->len / 4 < need_b || need_c * 4 > c->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        int mi = (int)m, ni = (int)n, ki = (int)k;
        clSetKernelArg(d->k_matmul, 0, sizeof(cl_mem), &c->mem);
        clSetKernelArg(d->k_matmul, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_matmul, 2, sizeof(cl_mem), &b->mem);
        clSetKernelArg(d->k_matmul, 3, sizeof(int), &mi);
        clSetKernelArg(d->k_matmul, 4, sizeof(int), &ni);
        clSetKernelArg(d->k_matmul, 5, sizeof(int), &ki);
        size_t g[2] = {(size_t)m, (size_t)n};
        if (clEnqueueNDRangeKernel(d->q, d->k_matmul, 2, NULL, g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        c->len = need_c * 4;
        c->host_dirty = 0;
        c->device_dirty = 1;
        return (int64_t)need_c;
    }
#endif
    return mako_gpu_matmul_f32_host(c, a, b, m, n, k);
}

/* Row-wise softmax: a is rows*cols, out same shape. Returns element count. */
static inline int64_t mako_gpu_softmax_rows_f32_host(
    MakoGpuBuf *o, MakoGpuBuf *a, int64_t rows, int64_t cols
) {
    if (rows <= 0 || cols <= 0) return -1;
    size_t total = (size_t)rows * (size_t)cols;
    if (a->len / 4 < total || total * 4 > o->cap) return -1;
    for (int64_t r = 0; r < rows; r++) {
        size_t base = (size_t)r * (size_t)cols;
        float mval = mako_gpu_load_f32(a->data + base * 4);
        for (int64_t j = 1; j < cols; j++) {
            float v = mako_gpu_load_f32(a->data + (base + (size_t)j) * 4);
            if (v > mval) mval = v;
        }
        float sum = 0.0f;
        for (int64_t j = 0; j < cols; j++) {
            float e = expf(mako_gpu_load_f32(a->data + (base + (size_t)j) * 4) - mval);
            mako_gpu_store_f32(o->data + (base + (size_t)j) * 4, e);
            sum += e;
        }
        float inv = sum > 0.0f ? 1.0f / sum : 0.0f;
        for (int64_t j = 0; j < cols; j++) {
            float e = mako_gpu_load_f32(o->data + (base + (size_t)j) * 4);
            mako_gpu_store_f32(o->data + (base + (size_t)j) * 4, e * inv);
        }
    }
    o->len = total * 4;
    return (int64_t)total;
}

static inline int64_t mako_gpu_softmax_rows_f32(
    int64_t out_h, int64_t a_h, int64_t rows, int64_t cols
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    if (!o || !a) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem) {
        if (rows <= 0 || cols <= 0) return -1;
        size_t total = (size_t)rows * (size_t)cols;
        if (a->len / 4 < total || total * 4 > o->cap) return -1;
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        int r = (int)rows, c = (int)cols;
        clSetKernelArg(d->k_softmax, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_softmax, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_softmax, 2, sizeof(int), &r);
        clSetKernelArg(d->k_softmax, 3, sizeof(int), &c);
        size_t g = (size_t)rows;
        if (clEnqueueNDRangeKernel(d->q, d->k_softmax, 1, NULL, &g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = total * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)total;
    }
#endif
    return mako_gpu_softmax_rows_f32_host(o, a, rows, cols);
}

/* Sum all f32 elements → scalar (host download if needed). */
static inline double mako_gpu_sum_f32(int64_t h) {
    MakoGpuBuf *b = mako_gpu_buf_ref(h);
    if (!b || b->len < 4) return 0.0;
#if MAKO_GPU_OPENCL
    if (b->mem && b->device_dirty) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live) (void)mako_gpu_ocl_sync_to_host(b, d);
    }
#endif
    size_t n = b->len / 4;
    double s = 0.0;
    for (size_t i = 0; i < n; i++) s += (double)mako_gpu_load_f32(b->data + i * 4);
    return s;
}

/* ---- Transformer-oriented kernels ---- */

static inline int64_t mako_gpu_unary_elem(
    int64_t out_h, int64_t a_h, int which
) {
    /* which: 0=gelu, 1=silu — host path */
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    if (!o || !a) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
    size_t n = a->len / 4;
    if (n * 4 > o->cap) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem) {
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        int ni = (int)n;
        cl_kernel k = which == 0 ? d->k_gelu : d->k_silu;
        clSetKernelArg(k, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(k, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(k, 2, sizeof(int), &ni);
        size_t g = n ? n : 1;
        if (clEnqueueNDRangeKernel(d->q, k, 1, NULL, &g, NULL, 0, NULL, NULL) != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = n * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)n;
    }
#endif
#if MAKO_GPU_OPENCL
    if (a->mem && a->device_dirty) (void)mako_gpu_ocl_sync_to_host(a, d);
#endif
    for (size_t i = 0; i < n; i++) {
        float x = mako_gpu_load_f32(a->data + i * 4);
        float y;
        if (which == 0) {
            float c = 0.7978845608f;
            float u = c * (x + 0.044715f * x * x * x);
            y = 0.5f * x * (1.0f + tanhf(u));
        } else {
            y = x / (1.0f + expf(-x));
        }
        mako_gpu_store_f32(o->data + i * 4, y);
    }
    o->len = n * 4;
#if MAKO_GPU_OPENCL
    o->host_dirty = 1;
    o->device_dirty = 0;
#endif
    return (int64_t)n;
}

static inline int64_t mako_gpu_gelu_f32(int64_t out_h, int64_t a_h) {
    return mako_gpu_unary_elem(out_h, a_h, 0);
}

static inline int64_t mako_gpu_silu_f32(int64_t out_h, int64_t a_h) {
    return mako_gpu_unary_elem(out_h, a_h, 1);
}

/* a is rows×cols → out is cols×rows */
static inline int64_t mako_gpu_transpose_f32(
    int64_t out_h, int64_t a_h, int64_t rows, int64_t cols
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *a = mako_gpu_buf_ref(a_h);
    if (!o || !a || rows <= 0 || cols <= 0) return -1;
    size_t need = (size_t)rows * (size_t)cols;
    if (a->len / 4 < need || need * 4 > o->cap) return -1;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && a->mem) {
        if (mako_gpu_ocl_sync_to_device(a, d) < 0) return -1;
        int r = (int)rows, c = (int)cols;
        clSetKernelArg(d->k_transpose, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_transpose, 1, sizeof(cl_mem), &a->mem);
        clSetKernelArg(d->k_transpose, 2, sizeof(int), &r);
        clSetKernelArg(d->k_transpose, 3, sizeof(int), &c);
        size_t g[2] = {(size_t)rows, (size_t)cols};
        if (clEnqueueNDRangeKernel(d->q, d->k_transpose, 2, NULL, g, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = need * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)need;
    }
#endif
#if MAKO_GPU_OPENCL
    if (a->mem && a->device_dirty) (void)mako_gpu_ocl_sync_to_host(a, d);
#endif
    for (int64_t i = 0; i < rows; i++) {
        for (int64_t j = 0; j < cols; j++) {
            float v = mako_gpu_load_f32(a->data + ((size_t)i * (size_t)cols + (size_t)j) * 4);
            mako_gpu_store_f32(o->data + ((size_t)j * (size_t)rows + (size_t)i) * 4, v);
        }
    }
    o->len = need * 4;
#if MAKO_GPU_OPENCL
    o->host_dirty = 1;
    o->device_dirty = 0;
#endif
    return (int64_t)need;
}

/* gamma_h / beta_h may be -1 to skip affine. */
static inline int64_t mako_gpu_layernorm_f32(
    int64_t out_h,
    int64_t x_h,
    int64_t gamma_h,
    int64_t beta_h,
    int64_t rows,
    int64_t cols,
    double eps
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *x = mako_gpu_buf_ref(x_h);
    if (!o || !x || rows <= 0 || cols <= 0) return -1;
    size_t total = (size_t)rows * (size_t)cols;
    if (x->len / 4 < total || total * 4 > o->cap) return -1;
    MakoGpuBuf *g = gamma_h > 0 ? mako_gpu_buf_ref(gamma_h) : NULL;
    MakoGpuBuf *b = beta_h > 0 ? mako_gpu_buf_ref(beta_h) : NULL;
    int use_affine = (g && b && g->len / 4 >= (size_t)cols && b->len / 4 >= (size_t)cols) ? 1 : 0;
    MakoGpuDev *d = &mako_gpu_devs[o->dev];
    if (!d->live) return -1;
    float e = (float)(eps > 0 ? eps : 1e-5);
#if MAKO_GPU_OPENCL
    if (d->kind == MAKO_GPU_KIND_OPENCL && o->mem && x->mem
        && (!use_affine || (g->mem && b->mem))) {
        if (mako_gpu_ocl_sync_to_device(x, d) < 0) return -1;
        if (use_affine) {
            if (mako_gpu_ocl_sync_to_device(g, d) < 0) return -1;
            if (mako_gpu_ocl_sync_to_device(b, d) < 0) return -1;
        }
        /* dummy buffer if no affine — OpenCL still needs a pointer; reuse x */
        cl_mem gm = use_affine ? g->mem : x->mem;
        cl_mem bm = use_affine ? b->mem : x->mem;
        int r = (int)rows, c = (int)cols, ua = use_affine;
        clSetKernelArg(d->k_layernorm, 0, sizeof(cl_mem), &o->mem);
        clSetKernelArg(d->k_layernorm, 1, sizeof(cl_mem), &x->mem);
        clSetKernelArg(d->k_layernorm, 2, sizeof(cl_mem), &gm);
        clSetKernelArg(d->k_layernorm, 3, sizeof(cl_mem), &bm);
        clSetKernelArg(d->k_layernorm, 4, sizeof(int), &r);
        clSetKernelArg(d->k_layernorm, 5, sizeof(int), &c);
        clSetKernelArg(d->k_layernorm, 6, sizeof(float), &e);
        clSetKernelArg(d->k_layernorm, 7, sizeof(int), &ua);
        size_t gs = (size_t)rows;
        if (clEnqueueNDRangeKernel(d->q, d->k_layernorm, 1, NULL, &gs, NULL, 0, NULL, NULL)
            != CL_SUCCESS)
            return -1;
        clFinish(d->q);
        o->len = total * 4;
        o->host_dirty = 0;
        o->device_dirty = 1;
        return (int64_t)total;
    }
#endif
#if MAKO_GPU_OPENCL
    if (x->mem && x->device_dirty) (void)mako_gpu_ocl_sync_to_host(x, d);
    if (use_affine) {
        if (g->mem && g->device_dirty) (void)mako_gpu_ocl_sync_to_host(g, d);
        if (b->mem && b->device_dirty) (void)mako_gpu_ocl_sync_to_host(b, d);
    }
#endif
    for (int64_t r = 0; r < rows; r++) {
        size_t base = (size_t)r * (size_t)cols;
        float mean = 0.0f;
        for (int64_t j = 0; j < cols; j++)
            mean += mako_gpu_load_f32(x->data + (base + (size_t)j) * 4);
        mean /= (float)cols;
        float var = 0.0f;
        for (int64_t j = 0; j < cols; j++) {
            float dd = mako_gpu_load_f32(x->data + (base + (size_t)j) * 4) - mean;
            var += dd * dd;
        }
        var /= (float)cols;
        float inv = 1.0f / sqrtf(var + e);
        for (int64_t j = 0; j < cols; j++) {
            float v = (mako_gpu_load_f32(x->data + (base + (size_t)j) * 4) - mean) * inv;
            if (use_affine) {
                float gj = mako_gpu_load_f32(g->data + (size_t)j * 4);
                float bj = mako_gpu_load_f32(b->data + (size_t)j * 4);
                v = v * gj + bj;
            }
            mako_gpu_store_f32(o->data + (base + (size_t)j) * 4, v);
        }
    }
    o->len = total * 4;
#if MAKO_GPU_OPENCL
    o->host_dirty = 1;
    o->device_dirty = 0;
#endif
    return (int64_t)total;
}

/* Scaled dot-product attention (single head): Q,K,V are [seq, dim] → out [seq, dim].
 * Composes transpose + matmul + scale + softmax + matmul (OpenCL or host). */
static inline int64_t mako_gpu_attention_f32(
    int64_t out_h, int64_t q_h, int64_t k_h, int64_t v_h, int64_t seq, int64_t dim
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *q = mako_gpu_buf_ref(q_h);
    MakoGpuBuf *k = mako_gpu_buf_ref(k_h);
    MakoGpuBuf *v = mako_gpu_buf_ref(v_h);
    if (!o || !q || !k || !v || seq <= 0 || dim <= 0) return -1;
    size_t sd = (size_t)seq * (size_t)dim;
    size_t ss = (size_t)seq * (size_t)seq;
    if (q->len / 4 < sd || k->len / 4 < sd || v->len / 4 < sd || sd * 4 > o->cap) return -1;
    int64_t dev = (int64_t)q->dev + 1;
    int64_t kt = mako_gpu_buf_new(dev, (int64_t)(sd * 4));
    int64_t scores = mako_gpu_buf_new(dev, (int64_t)(ss * 4));
    int64_t weights = mako_gpu_buf_new(dev, (int64_t)(ss * 4));
    if (kt < 0 || scores < 0 || weights < 0) {
        if (kt > 0) mako_gpu_buf_free(kt);
        if (scores > 0) mako_gpu_buf_free(scores);
        if (weights > 0) mako_gpu_buf_free(weights);
        return -1;
    }
    int64_t rc = -1;
    if (mako_gpu_transpose_f32(kt, k_h, seq, dim) < 0) goto done;
    if (mako_gpu_matmul_f32(scores, q_h, kt, seq, seq, dim) < 0) goto done;
    double scale = 1.0 / sqrt((double)dim);
    if (mako_gpu_scale_f32(scores, scores, scale) < 0) goto done;
    if (mako_gpu_softmax_rows_f32(weights, scores, seq, seq) < 0) goto done;
    if (mako_gpu_matmul_f32(out_h, weights, v_h, seq, dim, seq) < 0) goto done;
    rc = (int64_t)sd;
done:
    mako_gpu_buf_free(kt);
    mako_gpu_buf_free(scores);
    mako_gpu_buf_free(weights);
    return rc;
}

/* Sync buffer to host for head gather/scatter. */
static inline int mako_gpu_ensure_host(MakoGpuBuf *b) {
    if (!b) return -1;
#if MAKO_GPU_OPENCL
    if (b->mem && b->device_dirty) {
        MakoGpuDev *d = &mako_gpu_devs[b->dev];
        if (d->live && mako_gpu_ocl_sync_to_host(b, d) < 0) return -1;
    }
#endif
    return 0;
}

/* Multi-head attention: Q,K,V,out are [seq, n_heads * head_dim] row-major.
 * Heads are contiguous in the last dimension. Returns seq * n_heads * head_dim. */
static inline int64_t mako_gpu_mha_f32(
    int64_t out_h,
    int64_t q_h,
    int64_t k_h,
    int64_t v_h,
    int64_t seq,
    int64_t n_heads,
    int64_t head_dim
) {
    MakoGpuBuf *o = mako_gpu_buf_ref(out_h);
    MakoGpuBuf *q = mako_gpu_buf_ref(q_h);
    MakoGpuBuf *k = mako_gpu_buf_ref(k_h);
    MakoGpuBuf *v = mako_gpu_buf_ref(v_h);
    if (!o || !q || !k || !v || seq <= 0 || n_heads <= 0 || head_dim <= 0) return -1;
    int64_t d_model = n_heads * head_dim;
    size_t total = (size_t)seq * (size_t)d_model;
    if (q->len / 4 < total || k->len / 4 < total || v->len / 4 < total || total * 4 > o->cap)
        return -1;
    if (mako_gpu_ensure_host(q) < 0 || mako_gpu_ensure_host(k) < 0 || mako_gpu_ensure_host(v) < 0)
        return -1;

    int64_t dev = (int64_t)q->dev + 1;
    size_t head_elems = (size_t)seq * (size_t)head_dim;
    int64_t qh = mako_gpu_buf_new(dev, (int64_t)(head_elems * 4));
    int64_t kh = mako_gpu_buf_new(dev, (int64_t)(head_elems * 4));
    int64_t vh = mako_gpu_buf_new(dev, (int64_t)(head_elems * 4));
    int64_t oh = mako_gpu_buf_new(dev, (int64_t)(head_elems * 4));
    if (qh < 0 || kh < 0 || vh < 0 || oh < 0) {
        if (qh > 0) mako_gpu_buf_free(qh);
        if (kh > 0) mako_gpu_buf_free(kh);
        if (vh > 0) mako_gpu_buf_free(vh);
        if (oh > 0) mako_gpu_buf_free(oh);
        return -1;
    }
    MakoGpuBuf *qb = mako_gpu_buf_ref(qh);
    MakoGpuBuf *kb = mako_gpu_buf_ref(kh);
    MakoGpuBuf *vb = mako_gpu_buf_ref(vh);
    MakoGpuBuf *ob = mako_gpu_buf_ref(oh);
    int64_t rc = -1;

    for (int64_t h = 0; h < n_heads; h++) {
        /* gather head slice into qh/kh/vh */
        for (int64_t s = 0; s < seq; s++) {
            for (int64_t d = 0; d < head_dim; d++) {
                size_t src = ((size_t)s * (size_t)d_model) + (size_t)h * (size_t)head_dim
                    + (size_t)d;
                size_t dst = (size_t)s * (size_t)head_dim + (size_t)d;
                float qv = mako_gpu_load_f32(q->data + src * 4);
                float kv = mako_gpu_load_f32(k->data + src * 4);
                float vv = mako_gpu_load_f32(v->data + src * 4);
                mako_gpu_store_f32(qb->data + dst * 4, qv);
                mako_gpu_store_f32(kb->data + dst * 4, kv);
                mako_gpu_store_f32(vb->data + dst * 4, vv);
            }
        }
        qb->len = head_elems * 4;
        kb->len = head_elems * 4;
        vb->len = head_elems * 4;
#if MAKO_GPU_OPENCL
        qb->host_dirty = 1;
        qb->device_dirty = 0;
        kb->host_dirty = 1;
        kb->device_dirty = 0;
        vb->host_dirty = 1;
        vb->device_dirty = 0;
        if (qb->mem) {
            MakoGpuDev *dd = &mako_gpu_devs[qb->dev];
            if (mako_gpu_ocl_sync_to_device(qb, dd) < 0) goto mha_done;
            if (mako_gpu_ocl_sync_to_device(kb, dd) < 0) goto mha_done;
            if (mako_gpu_ocl_sync_to_device(vb, dd) < 0) goto mha_done;
        }
#endif
        if (mako_gpu_attention_f32(oh, qh, kh, vh, seq, head_dim) < 0) goto mha_done;
        if (mako_gpu_ensure_host(ob) < 0) goto mha_done;
        /* scatter head into out */
        for (int64_t s = 0; s < seq; s++) {
            for (int64_t d = 0; d < head_dim; d++) {
                size_t src = (size_t)s * (size_t)head_dim + (size_t)d;
                size_t dst = ((size_t)s * (size_t)d_model) + (size_t)h * (size_t)head_dim
                    + (size_t)d;
                float ov = mako_gpu_load_f32(ob->data + src * 4);
                mako_gpu_store_f32(o->data + dst * 4, ov);
            }
        }
    }
    o->len = total * 4;
#if MAKO_GPU_OPENCL
    o->host_dirty = 1;
    o->device_dirty = 0;
    if (o->mem) {
        MakoGpuDev *dd = &mako_gpu_devs[o->dev];
        (void)mako_gpu_ocl_sync_to_device(o, dd);
    }
#endif
    rc = (int64_t)total;
mha_done:
    mako_gpu_buf_free(qh);
    mako_gpu_buf_free(kh);
    mako_gpu_buf_free(vh);
    mako_gpu_buf_free(oh);
    return rc;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_GPU_H */
