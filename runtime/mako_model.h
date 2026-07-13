/* Local AI model weight store — load existing weights or author your own.
 *
 * Two paths for "using AI models" in Mako:
 *   1. Remote / API models  → llm_* (OpenAI-compatible HTTPS)
 *   2. Local weights + GPU  → model_* (this file) + gpu_* kernels
 *
 * Existing models: **safetensors** (HF) and **GGUF** (llama.cpp ecosystem)
 * tensor load (F32 / F16 → f32). Your own models: set tensors, **.makomodel**,
 * compose forward with gpu_* (matmul, attention, layernorm, gelu, …).
 *
 * Not a full llama.cpp runtime — weight I/O + kernels to build real models.
 */
#ifndef MAKO_MODEL_H
#define MAKO_MODEL_H

#include "mako_gpu.h"
#include "mako_rt.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAKO_MODEL_MAX 8
#define MAKO_MODEL_MAX_TENSORS 256
#define MAKO_MODEL_NAME_MAX 128
#define MAKO_MODEL_MAX_DIMS 8
#define MAKO_MODEL_MAX_FILE (512ULL * 1024 * 1024)

typedef struct {
    int live;
    char name[MAKO_MODEL_NAME_MAX];
    int64_t buf; /* gpu buffer handle */
    int ndim;
    int64_t dims[MAKO_MODEL_MAX_DIMS];
    int64_t nelem;
} MakoModelTensor;

typedef struct {
    int live;
    int64_t dev; /* gpu device handle */
    int n;
    MakoModelTensor tensors[MAKO_MODEL_MAX_TENSORS];
} MakoModel;

static MakoModel mako_models[MAKO_MODEL_MAX];

static inline MakoModel *mako_model_ref(int64_t h) {
    if (h < 1 || h > MAKO_MODEL_MAX) return NULL;
    MakoModel *m = &mako_models[h - 1];
    return m->live ? m : NULL;
}

static inline int64_t mako_model_new(int64_t dev) {
    if (dev < 1) return -1;
    for (int i = 0; i < MAKO_MODEL_MAX; i++) {
        if (!mako_models[i].live) {
            memset(&mako_models[i], 0, sizeof(MakoModel));
            mako_models[i].live = 1;
            mako_models[i].dev = dev;
            return (int64_t)(i + 1);
        }
    }
    return -1;
}

static inline int64_t mako_model_free(int64_t h) {
    MakoModel *m = mako_model_ref(h);
    if (!m) return 0;
    for (int i = 0; i < m->n; i++) {
        if (m->tensors[i].live && m->tensors[i].buf > 0)
            (void)mako_gpu_buf_free(m->tensors[i].buf);
    }
    memset(m, 0, sizeof(MakoModel));
    return 1;
}

static inline int mako_model_find(MakoModel *m, const char *name) {
    if (!m || !name) return -1;
    for (int i = 0; i < m->n; i++) {
        if (m->tensors[i].live && strcmp(m->tensors[i].name, name) == 0) return i;
    }
    return -1;
}

static inline int64_t mako_model_tensor_count(int64_t h) {
    MakoModel *m = mako_model_ref(h);
    return m ? (int64_t)m->n : -1;
}

static inline MakoString mako_model_tensor_name(int64_t h, int64_t i) {
    MakoModel *m = mako_model_ref(h);
    if (!m || i < 0 || i >= m->n || !m->tensors[i].live)
        return mako_str_from_cstr("");
    return mako_str_from_cstr(m->tensors[i].name);
}

static inline int64_t mako_model_tensor_buf(int64_t h, MakoString name) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !name.data) return -1;
    char key[MAKO_MODEL_NAME_MAX];
    size_t n = name.len < sizeof(key) - 1 ? name.len : sizeof(key) - 1;
    memcpy(key, name.data, n);
    key[n] = 0;
    int idx = mako_model_find(m, key);
    if (idx < 0) return -1;
    return m->tensors[idx].buf;
}

static inline int64_t mako_model_tensor_elems(int64_t h, MakoString name) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !name.data) return -1;
    char key[MAKO_MODEL_NAME_MAX];
    size_t n = name.len < sizeof(key) - 1 ? name.len : sizeof(key) - 1;
    memcpy(key, name.data, n);
    key[n] = 0;
    int idx = mako_model_find(m, key);
    if (idx < 0) return -1;
    return m->tensors[idx].nelem;
}

static inline int64_t mako_model_tensor_ndim(int64_t h, MakoString name) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !name.data) return -1;
    char key[MAKO_MODEL_NAME_MAX];
    size_t n = name.len < sizeof(key) - 1 ? name.len : sizeof(key) - 1;
    memcpy(key, name.data, n);
    key[n] = 0;
    int idx = mako_model_find(m, key);
    if (idx < 0) return -1;
    return m->tensors[idx].ndim;
}

static inline int64_t mako_model_tensor_dim(int64_t h, MakoString name, int64_t axis) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !name.data || axis < 0) return -1;
    char key[MAKO_MODEL_NAME_MAX];
    size_t n = name.len < sizeof(key) - 1 ? name.len : sizeof(key) - 1;
    memcpy(key, name.data, n);
    key[n] = 0;
    int idx = mako_model_find(m, key);
    if (idx < 0 || axis >= m->tensors[idx].ndim) return -1;
    return m->tensors[idx].dims[axis];
}

/* Replace or insert named f32 tensor from host values. shape via dims array length. */
static inline int64_t mako_model_set_f32(
    int64_t h, MakoString name, MakoFloatArray vals, int64_t d0, int64_t d1, int64_t d2, int64_t d3
) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !name.data || name.len == 0 || name.len >= MAKO_MODEL_NAME_MAX) return -1;
    int64_t dims[4] = {d0, d1, d2, d3};
    int ndim = 0;
    int64_t nelem = 1;
    for (int i = 0; i < 4; i++) {
        if (dims[i] <= 0) break;
        ndim++;
        nelem *= dims[i];
    }
    if (ndim == 0) {
        /* flat vector from vals.len */
        ndim = 1;
        dims[0] = (int64_t)vals.len;
        nelem = (int64_t)vals.len;
    }
    if (vals.len != (size_t)nelem && nelem > 0) {
        /* allow flat set when only d0.. given mismatch — require exact */
        if ((size_t)nelem != vals.len) return -1;
    }
    if (nelem <= 0 || nelem * 4 > MAKO_GPU_MAX_BYTES) return -1;

    char key[MAKO_MODEL_NAME_MAX];
    size_t kn = name.len < sizeof(key) - 1 ? name.len : sizeof(key) - 1;
    memcpy(key, name.data, kn);
    key[kn] = 0;

    int idx = mako_model_find(m, key);
    if (idx < 0) {
        if (m->n >= MAKO_MODEL_MAX_TENSORS) return -1;
        idx = m->n++;
        memset(&m->tensors[idx], 0, sizeof(MakoModelTensor));
        m->tensors[idx].live = 1;
        snprintf(m->tensors[idx].name, sizeof(m->tensors[idx].name), "%s", key);
    } else if (m->tensors[idx].buf > 0) {
        (void)mako_gpu_buf_free(m->tensors[idx].buf);
        m->tensors[idx].buf = 0;
    }

    int64_t buf = mako_gpu_buf_new(m->dev, nelem * 4);
    if (buf < 0) return -1;
    if (mako_gpu_upload_f32(buf, vals) != nelem) {
        mako_gpu_buf_free(buf);
        return -1;
    }
    m->tensors[idx].buf = buf;
    m->tensors[idx].ndim = ndim;
    m->tensors[idx].nelem = nelem;
    for (int i = 0; i < MAKO_MODEL_MAX_DIMS; i++)
        m->tensors[idx].dims[i] = i < ndim ? dims[i] : 0;
    return 1;
}

/* ---- half → float ---- */
static inline float mako_model_f16_to_f32(uint16_t h) {
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3ff;
            out = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000u | (mant << 13);
    } else {
        out = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &out, 4);
    return f;
}

/* ---- Minimal safetensors header scan ---- */

typedef struct {
    char name[MAKO_MODEL_NAME_MAX];
    char dtype[16];
    int ndim;
    int64_t dims[MAKO_MODEL_MAX_DIMS];
    int64_t off0;
    int64_t off1;
} MakoStEntry;

static inline const char *mako_st_skip_ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
    return p;
}

static inline int mako_st_parse_string(const char **pp, const char *end, char *out, size_t outcap) {
    const char *p = mako_st_skip_ws(*pp, end);
    if (p >= end || *p != '"') return 0;
    p++;
    size_t n = 0;
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) p++;
        if (n + 1 < outcap) out[n++] = *p;
        p++;
    }
    if (p >= end || *p != '"') return 0;
    out[n] = 0;
    *pp = p + 1;
    return 1;
}

static inline int mako_st_parse_i64(const char **pp, const char *end, int64_t *out) {
    const char *p = mako_st_skip_ws(*pp, end);
    if (p >= end) return 0;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (p >= end || !isdigit((unsigned char)*p)) return 0;
    int64_t v = 0;
    while (p < end && isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out = neg ? -v : v;
    *pp = p;
    return 1;
}

/* Parse one tensor object body into e (name already set). */
static inline int mako_st_parse_tensor_obj(const char **pp, const char *end, MakoStEntry *e) {
    const char *p = mako_st_skip_ws(*pp, end);
    if (p >= end || *p != '{') return 0;
    p++;
    e->ndim = 0;
    e->off0 = e->off1 = -1;
    e->dtype[0] = 0;
    while (p < end) {
        p = mako_st_skip_ws(p, end);
        if (p < end && *p == '}') {
            p++;
            *pp = p;
            return e->off0 >= 0 && e->off1 >= e->off0 && e->dtype[0];
        }
        char key[64];
        if (!mako_st_parse_string(&p, end, key, sizeof(key))) return 0;
        p = mako_st_skip_ws(p, end);
        if (p >= end || *p != ':') return 0;
        p++;
        p = mako_st_skip_ws(p, end);
        if (strcmp(key, "dtype") == 0) {
            if (!mako_st_parse_string(&p, end, e->dtype, sizeof(e->dtype))) return 0;
        } else if (strcmp(key, "shape") == 0) {
            if (p >= end || *p != '[') return 0;
            p++;
            e->ndim = 0;
            while (p < end) {
                p = mako_st_skip_ws(p, end);
                if (p < end && *p == ']') {
                    p++;
                    break;
                }
                int64_t d = 0;
                if (!mako_st_parse_i64(&p, end, &d)) return 0;
                if (e->ndim < MAKO_MODEL_MAX_DIMS) e->dims[e->ndim++] = d;
                p = mako_st_skip_ws(p, end);
                if (p < end && *p == ',') p++;
            }
        } else if (strcmp(key, "data_offsets") == 0) {
            if (p >= end || *p != '[') return 0;
            p++;
            if (!mako_st_parse_i64(&p, end, &e->off0)) return 0;
            p = mako_st_skip_ws(p, end);
            if (p < end && *p == ',') p++;
            if (!mako_st_parse_i64(&p, end, &e->off1)) return 0;
            p = mako_st_skip_ws(p, end);
            if (p < end && *p == ']') p++;
        } else {
            /* skip unknown value: string, number, array, object — brace depth */
            if (p < end && *p == '"') {
                char dump[256];
                if (!mako_st_parse_string(&p, end, dump, sizeof(dump))) return 0;
            } else if (p < end && (*p == '{' || *p == '[')) {
                char open = *p;
                char close = open == '{' ? '}' : ']';
                int depth = 1;
                p++;
                while (p < end && depth > 0) {
                    if (*p == '"') {
                        p++;
                        while (p < end && *p != '"') {
                            if (*p == '\\' && p + 1 < end) p++;
                            p++;
                        }
                        if (p < end) p++;
                        continue;
                    }
                    if (*p == open) depth++;
                    else if (*p == close) depth--;
                    p++;
                }
            } else {
                while (p < end && *p != ',' && *p != '}') p++;
            }
        }
        p = mako_st_skip_ws(p, end);
        if (p < end && *p == ',') p++;
    }
    return 0;
}

static inline int mako_st_parse_header(
    const char *hdr, size_t hdr_len, MakoStEntry *out, int max_out, int *n_out
) {
    const char *p = hdr;
    const char *end = hdr + hdr_len;
    p = mako_st_skip_ws(p, end);
    if (p >= end || *p != '{') return 0;
    p++;
    int n = 0;
    while (p < end) {
        p = mako_st_skip_ws(p, end);
        if (p < end && *p == '}') break;
        char name[MAKO_MODEL_NAME_MAX];
        if (!mako_st_parse_string(&p, end, name, sizeof(name))) return 0;
        p = mako_st_skip_ws(p, end);
        if (p >= end || *p != ':') return 0;
        p++;
        if (strcmp(name, "__metadata__") == 0) {
            /* skip object */
            p = mako_st_skip_ws(p, end);
            if (p < end && *p == '{') {
                int depth = 1;
                p++;
                while (p < end && depth > 0) {
                    if (*p == '"') {
                        p++;
                        while (p < end && *p != '"') {
                            if (*p == '\\' && p + 1 < end) p++;
                            p++;
                        }
                        if (p < end) p++;
                        continue;
                    }
                    if (*p == '{') depth++;
                    else if (*p == '}') depth--;
                    p++;
                }
            }
        } else {
            MakoStEntry e;
            memset(&e, 0, sizeof(e));
            snprintf(e.name, sizeof(e.name), "%s", name);
            if (!mako_st_parse_tensor_obj(&p, end, &e)) return 0;
            if (n < max_out) out[n++] = e;
        }
        p = mako_st_skip_ws(p, end);
        if (p < end && *p == ',') p++;
    }
    *n_out = n;
    return 1;
}

static inline int64_t mako_model_load_safetensors(int64_t h, MakoString path) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !path.data || path.len == 0) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;

    FILE *f = fopen(pth, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long fsz = ftell(f);
    if (fsz < 16 || (uint64_t)fsz > MAKO_MODEL_MAX_FILE) {
        fclose(f);
        return 0;
    }
    rewind(f);
    uint64_t header_len = 0;
    if (fread(&header_len, 1, 8, f) != 8) {
        fclose(f);
        return 0;
    }
    if (header_len == 0 || header_len > (uint64_t)fsz - 8 || header_len > 64 * 1024 * 1024) {
        fclose(f);
        return 0;
    }
    char *hdr = (char *)malloc((size_t)header_len + 1);
    if (!hdr) {
        fclose(f);
        return 0;
    }
    if (fread(hdr, 1, (size_t)header_len, f) != header_len) {
        free(hdr);
        fclose(f);
        return 0;
    }
    hdr[header_len] = 0;

    MakoStEntry entries[MAKO_MODEL_MAX_TENSORS];
    int nent = 0;
    if (!mako_st_parse_header(hdr, (size_t)header_len, entries, MAKO_MODEL_MAX_TENSORS, &nent)) {
        free(hdr);
        fclose(f);
        return 0;
    }
    free(hdr);

    long data_base = 8 + (long)header_len;
    int loaded = 0;
    for (int i = 0; i < nent; i++) {
        MakoStEntry *e = &entries[i];
        int is_f32 = strcmp(e->dtype, "F32") == 0 || strcmp(e->dtype, "f32") == 0;
        int is_f16 = strcmp(e->dtype, "F16") == 0 || strcmp(e->dtype, "f16") == 0
            || strcmp(e->dtype, "FLOAT16") == 0;
        if (!is_f32 && !is_f16) continue; /* skip BF16/I64 etc. for now */

        int64_t nbytes = e->off1 - e->off0;
        if (nbytes <= 0 || e->off0 < 0) continue;
        int64_t nelem = 1;
        if (e->ndim == 0) {
            e->ndim = 1;
            e->dims[0] = is_f32 ? nbytes / 4 : nbytes / 2;
        }
        for (int d = 0; d < e->ndim; d++) {
            if (e->dims[d] <= 0) {
                nelem = 0;
                break;
            }
            nelem *= e->dims[d];
        }
        if (nelem <= 0) continue;
        int64_t expect = is_f32 ? nelem * 4 : nelem * 2;
        if (expect != nbytes || nelem * 4 > MAKO_GPU_MAX_BYTES) continue;

        unsigned char *raw = (unsigned char *)malloc((size_t)nbytes);
        if (!raw) continue;
        if (fseek(f, data_base + (long)e->off0, SEEK_SET) != 0
            || fread(raw, 1, (size_t)nbytes, f) != (size_t)nbytes) {
            free(raw);
            continue;
        }

        MakoFloatArray vals = mako_float_array_make(nelem, nelem);
        if (is_f32) {
            for (int64_t j = 0; j < nelem; j++) {
                uint32_t u;
                memcpy(&u, raw + j * 4, 4);
                float fv;
                memcpy(&fv, &u, 4);
                vals.data[j] = (double)fv;
            }
        } else {
            for (int64_t j = 0; j < nelem; j++) {
                uint16_t h16;
                memcpy(&h16, raw + j * 2, 2);
                vals.data[j] = (double)mako_model_f16_to_f32(h16);
            }
        }
        free(raw);

        MakoString nm = mako_str_from_cstr(e->name);
        int64_t d0 = e->ndim > 0 ? e->dims[0] : 0;
        int64_t d1 = e->ndim > 1 ? e->dims[1] : 0;
        int64_t d2 = e->ndim > 2 ? e->dims[2] : 0;
        int64_t d3 = e->ndim > 3 ? e->dims[3] : 0;
        if (mako_model_set_f32(h, nm, vals, d0, d1, d2, d3) == 1) loaded++;
        mako_str_free(nm);
        free(vals.data);
    }
    fclose(f);
    return loaded > 0 ? 1 : 0;
}

/* ---- GGUF (llama.cpp ecosystem) tensor load ----
 * Loads F32/F16 and dequantizes Q4_0 / Q8_0 into f32 device tensors.
 * Other quant types skipped. Spec: ggml GGUF docs.
 */
enum {
    MAKO_GGUF_F32 = 0,
    MAKO_GGUF_F16 = 1,
    MAKO_GGUF_Q4_0 = 2,
    MAKO_GGUF_Q8_0 = 8
};

#define MAKO_QK4_0 32
#define MAKO_QK8_0 32

static inline int64_t mako_gguf_type_nbytes(uint32_t typ, int64_t nelem) {
    if (nelem <= 0) return -1;
    switch (typ) {
    case MAKO_GGUF_F32:
        return nelem * 4;
    case MAKO_GGUF_F16:
        return nelem * 2;
    case MAKO_GGUF_Q4_0: {
        int64_t nb = (nelem + MAKO_QK4_0 - 1) / MAKO_QK4_0;
        return nb * (2 + MAKO_QK4_0 / 2); /* half d + 16 nibbles */
    }
    case MAKO_GGUF_Q8_0: {
        int64_t nb = (nelem + MAKO_QK8_0 - 1) / MAKO_QK8_0;
        return nb * (2 + MAKO_QK8_0); /* half d + 32 int8 */
    }
    default:
        return -1;
    }
}

/* Dequantize Q4_0 / Q8_0 blocks into f32 vals[nelem]. */
static inline int mako_gguf_dequant(
    uint32_t typ, const unsigned char *raw, int64_t nbytes, MakoFloatArray *vals, int64_t nelem
) {
    if (!raw || !vals || !vals->data || nelem <= 0) return 0;
    if (typ == MAKO_GGUF_Q8_0) {
        int64_t nblocks = (nelem + MAKO_QK8_0 - 1) / MAKO_QK8_0;
        int64_t expect = nblocks * (2 + MAKO_QK8_0);
        if (nbytes < expect) return 0;
        int64_t o = 0;
        size_t off = 0;
        for (int64_t b = 0; b < nblocks; b++) {
            uint16_t dh;
            memcpy(&dh, raw + off, 2);
            off += 2;
            float d = mako_model_f16_to_f32(dh);
            for (int i = 0; i < MAKO_QK8_0 && o < nelem; i++, o++) {
                int8_t q = (int8_t)raw[off + (size_t)i];
                vals->data[o] = (double)(d * (float)q);
            }
            off += MAKO_QK8_0;
        }
        return 1;
    }
    if (typ == MAKO_GGUF_Q4_0) {
        int64_t nblocks = (nelem + MAKO_QK4_0 - 1) / MAKO_QK4_0;
        int64_t expect = nblocks * (2 + MAKO_QK4_0 / 2);
        if (nbytes < expect) return 0;
        int64_t o = 0;
        size_t off = 0;
        for (int64_t b = 0; b < nblocks; b++) {
            uint16_t dh;
            memcpy(&dh, raw + off, 2);
            off += 2;
            float d = mako_model_f16_to_f32(dh);
            const unsigned char *qs = raw + off;
            /* First 16 values: low nibble; next 16: high nibble (ggml layout). */
            for (int i = 0; i < 16 && o < nelem; i++, o++) {
                int x0 = (int)(qs[i] & 0x0f) - 8;
                vals->data[o] = (double)(d * (float)x0);
            }
            for (int i = 0; i < 16 && o < nelem; i++, o++) {
                int x1 = (int)(qs[i] >> 4) - 8;
                vals->data[o] = (double)(d * (float)x1);
            }
            off += MAKO_QK4_0 / 2;
        }
        return 1;
    }
    return 0;
}

static inline int mako_gguf_read_u32(FILE *f, uint32_t *v) {
    return fread(v, 4, 1, f) == 1;
}
static inline int mako_gguf_read_u64(FILE *f, uint64_t *v) {
    return fread(v, 8, 1, f) == 1;
}
static inline int mako_gguf_read_string(FILE *f, char *out, size_t outcap) {
    uint64_t n = 0;
    if (!mako_gguf_read_u64(f, &n)) return 0;
    if (n >= outcap) {
        /* skip oversized */
        if (fseek(f, (long)n, SEEK_CUR) != 0) return 0;
        if (outcap) out[0] = 0;
        return 1;
    }
    if (n && fread(out, 1, (size_t)n, f) != (size_t)n) return 0;
    out[n] = 0;
    return 1;
}

static inline int mako_gguf_skip_value(FILE *f, uint32_t vtype) {
    switch (vtype) {
    case 0: /* u8 */
    case 1: /* i8 */
    case 7: /* bool */
        return fseek(f, 1, SEEK_CUR) == 0;
    case 2: /* u16 */
    case 3: /* i16 */
        return fseek(f, 2, SEEK_CUR) == 0;
    case 4: /* u32 */
    case 5: /* i32 */
    case 6: /* f32 */
        return fseek(f, 4, SEEK_CUR) == 0;
    case 10: /* u64 */
    case 11: /* i64 */
    case 12: /* f64 */
        return fseek(f, 8, SEEK_CUR) == 0;
    case 8: { /* string */
        char dump[4096];
        return mako_gguf_read_string(f, dump, sizeof(dump));
    }
    case 9: { /* array */
        uint32_t at = 0;
        uint64_t cnt = 0;
        if (!mako_gguf_read_u32(f, &at) || !mako_gguf_read_u64(f, &cnt)) return 0;
        for (uint64_t i = 0; i < cnt; i++) {
            if (!mako_gguf_skip_value(f, at)) return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

static inline int64_t mako_model_load_gguf(int64_t h, MakoString path) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "rb");
    if (!f) return 0;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GGUF", 4) != 0) {
        fclose(f);
        return 0;
    }
    uint32_t version = 0;
    if (!mako_gguf_read_u32(f, &version) || version < 2 || version > 3) {
        fclose(f);
        return 0;
    }
    uint64_t n_tensors = 0, n_kv = 0;
    if (!mako_gguf_read_u64(f, &n_tensors) || !mako_gguf_read_u64(f, &n_kv)) {
        fclose(f);
        return 0;
    }
    if (n_tensors > MAKO_MODEL_MAX_TENSORS) n_tensors = MAKO_MODEL_MAX_TENSORS;
    /* skip metadata KV */
    for (uint64_t i = 0; i < n_kv; i++) {
        char key[256];
        uint32_t vt = 0;
        if (!mako_gguf_read_string(f, key, sizeof(key))) {
            fclose(f);
            return 0;
        }
        if (!mako_gguf_read_u32(f, &vt) || !mako_gguf_skip_value(f, vt)) {
            fclose(f);
            return 0;
        }
    }
    typedef struct {
        char name[MAKO_MODEL_NAME_MAX];
        uint32_t ndim;
        uint64_t dims[MAKO_MODEL_MAX_DIMS];
        uint32_t typ;
        uint64_t offset;
    } Gguft;
    Gguft *infos = (Gguft *)calloc((size_t)n_tensors, sizeof(Gguft));
    if (!infos) {
        fclose(f);
        return 0;
    }
    for (uint64_t i = 0; i < n_tensors; i++) {
        if (!mako_gguf_read_string(f, infos[i].name, sizeof(infos[i].name))) {
            free(infos);
            fclose(f);
            return 0;
        }
        if (!mako_gguf_read_u32(f, &infos[i].ndim) || infos[i].ndim > MAKO_MODEL_MAX_DIMS) {
            free(infos);
            fclose(f);
            return 0;
        }
        for (uint32_t d = 0; d < infos[i].ndim; d++) {
            if (!mako_gguf_read_u64(f, &infos[i].dims[d])) {
                free(infos);
                fclose(f);
                return 0;
            }
        }
        if (!mako_gguf_read_u32(f, &infos[i].typ) || !mako_gguf_read_u64(f, &infos[i].offset)) {
            free(infos);
            fclose(f);
            return 0;
        }
    }
    /* data section: aligned to 32 from current pos */
    long pos = ftell(f);
    if (pos < 0) {
        free(infos);
        fclose(f);
        return 0;
    }
    long align = 32;
    long pad = (align - (pos % align)) % align;
    if (pad && fseek(f, pad, SEEK_CUR) != 0) {
        free(infos);
        fclose(f);
        return 0;
    }
    long data_base = ftell(f);
    int loaded = 0;
    for (uint64_t i = 0; i < n_tensors; i++) {
        Gguft *t = &infos[i];
        int is_f32 = t->typ == MAKO_GGUF_F32;
        int is_f16 = t->typ == MAKO_GGUF_F16;
        int is_q4 = t->typ == MAKO_GGUF_Q4_0;
        int is_q8 = t->typ == MAKO_GGUF_Q8_0;
        if (!is_f32 && !is_f16 && !is_q4 && !is_q8) continue;
        int64_t nelem = 1;
        int64_t dims4[4] = {0, 0, 0, 0};
        for (uint32_t d = 0; d < t->ndim; d++) {
            if (t->dims[d] == 0) {
                nelem = 0;
                break;
            }
            nelem *= (int64_t)t->dims[d];
            if (d < 4) dims4[d] = (int64_t)t->dims[d];
        }
        if (nelem <= 0 || nelem * 4 > MAKO_GPU_MAX_BYTES) continue;
        int64_t nbytes = mako_gguf_type_nbytes(t->typ, nelem);
        if (nbytes <= 0) continue;
        unsigned char *raw = (unsigned char *)malloc((size_t)nbytes);
        if (!raw) continue;
        if (fseek(f, data_base + (long)t->offset, SEEK_SET) != 0
            || fread(raw, 1, (size_t)nbytes, f) != (size_t)nbytes) {
            free(raw);
            continue;
        }
        MakoFloatArray vals = mako_float_array_make(nelem, nelem);
        if (is_f32) {
            for (int64_t j = 0; j < nelem; j++) {
                float fv;
                memcpy(&fv, raw + j * 4, 4);
                vals.data[j] = (double)fv;
            }
        } else if (is_f16) {
            for (int64_t j = 0; j < nelem; j++) {
                uint16_t h16;
                memcpy(&h16, raw + j * 2, 2);
                vals.data[j] = (double)mako_model_f16_to_f32(h16);
            }
        } else if (!mako_gguf_dequant(t->typ, raw, nbytes, &vals, nelem)) {
            free(raw);
            free(vals.data);
            continue;
        }
        free(raw);
        MakoString nm = mako_str_from_cstr(t->name);
        if (mako_model_set_f32(h, nm, vals, dims4[0], dims4[1], dims4[2], dims4[3]) == 1)
            loaded++;
        mako_str_free(nm);
        free(vals.data);
    }
    free(infos);
    fclose(f);
    return loaded > 0 ? 1 : 0;
}

/* ---- Native .makomodel archive (models you author in Mako) ----
 * magic "MAKOMODEL" (9) + ver u32 LE + n u32 LE
 * each: name_len u32, name, ndim u32, dims[ndim] i64, nelem i64, f32[nelem]
 */
static inline int64_t mako_model_save(int64_t h, MakoString path) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "wb");
    if (!f) return 0;
    fwrite("MAKOMODEL", 1, 9, f);
    uint32_t ver = 1, n = (uint32_t)m->n;
    fwrite(&ver, 4, 1, f);
    fwrite(&n, 4, 1, f);
    for (int i = 0; i < m->n; i++) {
        MakoModelTensor *t = &m->tensors[i];
        if (!t->live) continue;
        uint32_t nl = (uint32_t)strlen(t->name);
        fwrite(&nl, 4, 1, f);
        fwrite(t->name, 1, nl, f);
        uint32_t ndim = (uint32_t)t->ndim;
        fwrite(&ndim, 4, 1, f);
        for (uint32_t d = 0; d < ndim; d++) {
            int64_t dim = t->dims[d];
            fwrite(&dim, 8, 1, f);
        }
        int64_t ne = t->nelem;
        fwrite(&ne, 8, 1, f);
        MakoFloatArray vals = mako_gpu_download_f32(t->buf);
        for (int64_t j = 0; j < ne; j++) {
            float fv = (float)(j < (int64_t)vals.len ? vals.data[j] : 0.0);
            fwrite(&fv, 4, 1, f);
        }
        free(vals.data);
    }
    fclose(f);
    return 1;
}

static inline int64_t mako_model_load(int64_t h, MakoString path) {
    MakoModel *m = mako_model_ref(h);
    if (!m || !path.data) return 0;
    char pth[1024];
    size_t pl = path.len < sizeof(pth) - 1 ? path.len : sizeof(pth) - 1;
    memcpy(pth, path.data, pl);
    pth[pl] = 0;
    FILE *f = fopen(pth, "rb");
    if (!f) return 0;
    char magic[9];
    if (fread(magic, 1, 9, f) != 9 || memcmp(magic, "MAKOMODEL", 9) != 0) {
        fclose(f);
        return 0;
    }
    uint32_t ver = 0, n = 0;
    if (fread(&ver, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || ver != 1) {
        fclose(f);
        return 0;
    }
    int loaded = 0;
    for (uint32_t i = 0; i < n && i < MAKO_MODEL_MAX_TENSORS; i++) {
        uint32_t nl = 0;
        if (fread(&nl, 4, 1, f) != 1 || nl == 0 || nl >= MAKO_MODEL_NAME_MAX) break;
        char name[MAKO_MODEL_NAME_MAX];
        if (fread(name, 1, nl, f) != nl) break;
        name[nl] = 0;
        uint32_t ndim = 0;
        if (fread(&ndim, 4, 1, f) != 1 || ndim > MAKO_MODEL_MAX_DIMS) break;
        int64_t dims[MAKO_MODEL_MAX_DIMS] = {0};
        for (uint32_t d = 0; d < ndim; d++) {
            if (fread(&dims[d], 8, 1, f) != 1) {
                fclose(f);
                return loaded > 0 ? 1 : 0;
            }
        }
        int64_t ne = 0;
        if (fread(&ne, 8, 1, f) != 1 || ne <= 0 || ne * 4 > MAKO_GPU_MAX_BYTES) break;
        MakoFloatArray vals = mako_float_array_make(ne, ne);
        for (int64_t j = 0; j < ne; j++) {
            float fv = 0;
            if (fread(&fv, 4, 1, f) != 1) {
                free(vals.data);
                fclose(f);
                return loaded > 0 ? 1 : 0;
            }
            vals.data[j] = (double)fv;
        }
        MakoString nm = mako_str_from_cstr(name);
        int64_t d0 = ndim > 0 ? dims[0] : ne;
        int64_t d1 = ndim > 1 ? dims[1] : 0;
        int64_t d2 = ndim > 2 ? dims[2] : 0;
        int64_t d3 = ndim > 3 ? dims[3] : 0;
        if (mako_model_set_f32(h, nm, vals, d0, d1, d2, d3) == 1) loaded++;
        mako_str_free(nm);
        free(vals.data);
    }
    fclose(f);
    return loaded > 0 ? 1 : 0;
}

/* y = x @ W^T style is NOT used — row-major y = x @ W with W [in, out] stored as [out, in]?
 * Convention: W is [in_features, out_features] row-major so x[batch,in] @ W[in,out] = y[batch,out].
 * Many HF linears store weight as [out, in]. Use model_linear_f32 with weight_layout:
 *   0 = W is [in, out] (Mako native)
 *   1 = W is [out, in] (PyTorch / HF nn.Linear) — we still matmul as x @ W^T via dims swap note
 *
 * For HF [out, in]: compute y[b,o] = sum_i x[b,i] * W[o,i]
 * = x @ W^T. Implement by treating W as [out, in] and doing matmul with k=in, m=batch, n=out
 * where A=x [batch,in], B needs to be [in,out] = W^T.
 *
 * Simple path for seed: model_linear expects W as [in, out] Mako layout.
 * model_linear_hf expects W [out, in] and transposes on the fly via host for small, or
 * dedicated kernel.
 *
 * For AI interop, implement HF layout as primary optional flag.
 */
static inline int64_t mako_model_linear_f32(
    int64_t h,
    int64_t out_buf,
    int64_t x_buf,
    MakoString w_name,
    MakoString b_name,
    int64_t batch,
    int64_t in_f,
    int64_t out_f,
    int64_t hf_weight /* 1 = weight is [out, in] like PyTorch */
) {
    MakoModel *m = mako_model_ref(h);
    if (!m || batch <= 0 || in_f <= 0 || out_f <= 0) return -1;
    int64_t wb = mako_model_tensor_buf(h, w_name);
    if (wb < 0) return -1;
    int64_t tmp = -1;
    int64_t w_use = wb;
    if (hf_weight) {
        /* Transpose W[out,in] → Wt[in,out] into a temp buffer on device. */
        MakoFloatArray wvals = mako_gpu_download_f32(wb);
        if ((int64_t)wvals.len < out_f * in_f) {
            free(wvals.data);
            return -1;
        }
        MakoFloatArray wt = mako_float_array_make(in_f * out_f, in_f * out_f);
        for (int64_t i = 0; i < in_f; i++) {
            for (int64_t o = 0; o < out_f; o++) {
                /* W[o, i] → Wt[i, o] */
                wt.data[i * out_f + o] = wvals.data[o * in_f + i];
            }
        }
        free(wvals.data);
        tmp = mako_gpu_buf_new(m->dev, in_f * out_f * 4);
        if (tmp < 0) {
            free(wt.data);
            return -1;
        }
        if (mako_gpu_upload_f32(tmp, wt) != in_f * out_f) {
            free(wt.data);
            mako_gpu_buf_free(tmp);
            return -1;
        }
        free(wt.data);
        w_use = tmp;
    }
    int64_t rc = mako_gpu_matmul_f32(out_buf, x_buf, w_use, batch, out_f, in_f);
    if (tmp > 0) mako_gpu_buf_free(tmp);
    if (rc < 0) return -1;
    if (b_name.data && b_name.len > 0) {
        int64_t bb = mako_model_tensor_buf(h, b_name);
        if (bb < 0) return -1;
        if (mako_gpu_bias_add_f32(out_buf, out_buf, bb, batch, out_f) < 0) return -1;
    }
    return rc;
}

#ifdef __cplusplus
}
#endif

#endif /* MAKO_MODEL_H */
