/* Mako direct I/O — high-performance database primitives.
 * mmap, pread/pwrite, fsync, O_DIRECT, fallocate, WAL-style append.
 * Designed for building databases, LSM trees, B-trees. */
#ifndef MAKO_DIO_H
#define MAKO_DIO_H

#include "mako_rt.h"

#if defined(_WIN32)
/* Direct I/O primitives are POSIX-only for now. Stubs on Windows. */
typedef struct { int fd; } MakoMMap;
static inline int64_t mako_file_open(MakoString p, int64_t m, int64_t f) { (void)p;(void)m;(void)f; return -1; }
static inline int64_t mako_file_close(int64_t fd) { (void)fd; return -1; }
static inline MakoString mako_pread(int64_t f, int64_t c, int64_t o) { (void)f;(void)c;(void)o; return mako_str_from_cstr(""); }
static inline int64_t mako_pwrite(int64_t f, MakoString d, int64_t o) { (void)f;(void)d;(void)o; return -1; }
static inline int64_t mako_file_append(int64_t f, MakoString d) { (void)f;(void)d; return -1; }
static inline int64_t mako_file_append2(int64_t f, MakoString a, MakoString b) { (void)f;(void)a;(void)b; return -1; }
static inline int64_t mako_file_append3(int64_t f, MakoString a, MakoString b, MakoString c) { (void)f;(void)a;(void)b;(void)c; return -1; }
static inline int64_t mako_fsync(int64_t f) { (void)f; return -1; }
static inline int64_t mako_fdatasync(int64_t f) { (void)f; return -1; }
static inline int64_t mako_fallocate(int64_t f, int64_t s) { (void)f;(void)s; return -1; }
static inline int64_t mako_file_size(int64_t f) { (void)f; return -1; }
static inline int64_t mako_path_file_size(MakoString p) { (void)p; return -1; }
static inline int64_t mako_file_truncate(int64_t f, int64_t s) { (void)f;(void)s; return -1; }
static inline int64_t mako_file_seek(int64_t f, int64_t o, int64_t w) { (void)f;(void)o;(void)w; return -1; }
static inline MakoString mako_file_read_exact(int64_t f, int64_t n) { (void)f;(void)n; return mako_str_from_cstr(""); }
static inline int64_t mako_file_writev(int64_t f, MakoString *p, int64_t c) { (void)f;(void)p;(void)c; return -1; }
static inline MakoMMap *mako_mmap_open(MakoString p, int64_t m) { (void)p;(void)m; return NULL; }
static inline MakoMMap *mako_mmap_create(MakoString p, int64_t s) { (void)p;(void)s; return NULL; }
static inline MakoString mako_mmap_read(MakoMMap *m, int64_t o, int64_t c) { (void)m;(void)o;(void)c; return mako_str_from_cstr(""); }
static inline int64_t mako_mmap_write(MakoMMap *m, int64_t o, MakoString d) { (void)m;(void)o;(void)d; return -1; }
static inline int64_t mako_mmap_sync(MakoMMap *m, int64_t f) { (void)m;(void)f; return -1; }
static inline int64_t mako_mmap_size(MakoMMap *m) { (void)m; return -1; }
static inline int64_t mako_mmap_close(MakoMMap *m) { (void)m; return -1; }
typedef struct { int fd; char path[512]; } MakoWal;
typedef struct { char *data; size_t size; } MakoPage;
static inline MakoPage *mako_page_alloc(int64_t s) {
    size_t n = s > 0 ? (size_t)s : 4096;
    MakoPage *p = (MakoPage *)calloc(1, sizeof(MakoPage));
    if (!p) return NULL;
    p->data = (char *)calloc(1, n);
    if (!p->data) { free(p); return NULL; }
    p->size = n;
    return p;
}
static inline int64_t mako_page_size(MakoPage *p) { return p ? (int64_t)p->size : -1; }
static inline int64_t mako_page_write(MakoPage *p, int64_t o, MakoString d) {
    if (!p || !p->data || o < 0 || (size_t)o + d.len > p->size) return -1;
    if (d.len && d.data) memcpy(p->data + (size_t)o, d.data, d.len);
    return (int64_t)d.len;
}
static inline MakoString mako_page_read(MakoPage *p, int64_t o, int64_t c) {
    if (!p || !p->data || o < 0 || c < 0 || (size_t)o >= p->size) return mako_str_from_cstr("");
    size_t n = (size_t)c;
    if ((size_t)o + n > p->size) n = p->size - (size_t)o;
    char *b = (char *)malloc(n + 1);
    if (!b) return mako_str_from_cstr("");
    if (n) memcpy(b, p->data + (size_t)o, n);
    b[n] = 0;
    return (MakoString){b, n};
}
static inline int64_t mako_page_free(MakoPage *p) {
    if (!p) return 0; free(p->data); free(p); return 0;
}
static inline MakoWal *mako_wal_open(MakoString p) { (void)p; return NULL; }
static inline int64_t mako_wal_append(MakoWal *w, MakoString r) { (void)w;(void)r; return -1; }
static inline int64_t mako_wal_sync(MakoWal *w) { (void)w; return -1; }
static inline int64_t mako_wal_size(MakoWal *w) { (void)w; return -1; }
static inline MakoString mako_wal_read_at(MakoWal *w, int64_t o) { (void)w;(void)o; return mako_str_from_cstr(""); }
static inline int64_t mako_wal_next_off(void) { return -1; }
static inline int64_t mako_wal_close(MakoWal *w) { free(w); return 0; }
#else /* POSIX */

/* macOS fcntl constants — use raw values to avoid _DARWIN_C_SOURCE ordering issues */
#if defined(__APPLE__)
#ifndef F_NOCACHE
#define F_NOCACHE 48
#endif
#ifndef F_FULLFSYNC
#define F_FULLFSYNC 51
#endif
#ifndef F_PREALLOCATE
#define F_PREALLOCATE 42
#endif
#endif
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <string.h>
#include <stdlib.h>
#if defined(__APPLE__)
#include <sys/param.h>
#endif

/* ---- File descriptor operations ---- */

/* Open file with flags. Returns fd or -1.
 * mode: 0=read-only, 1=write-only, 2=read-write
 * flags: bit 0=create, bit 1=truncate, bit 2=append, bit 3=sync,
 *        bit 4=direct (O_DIRECT / F_NOCACHE), bit 5=exclusive create (O_EXCL).
 * Always sets O_CLOEXEC when available (production default). */
static inline int64_t mako_file_open(MakoString path, int64_t mode, int64_t flags) {
    char pbuf[4096];
    if (!path.data || path.len == 0 || path.len >= sizeof(pbuf)) return -1;
    for (size_t i = 0; i < path.len; i++) {
        if (path.data[i] == '\0') return -1; /* reject embedded NUL */
    }
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;

    int oflags = 0;
    if (mode == 0) oflags = O_RDONLY;
    else if (mode == 1) oflags = O_WRONLY;
    else oflags = O_RDWR;

    if (flags & 1) oflags |= O_CREAT;
    if (flags & 2) oflags |= O_TRUNC;
    if (flags & 4) oflags |= O_APPEND;
#ifdef O_DSYNC
    if (flags & 8) oflags |= O_DSYNC;
#endif
#ifdef O_DIRECT
    if (flags & 16) oflags |= O_DIRECT;
#endif
#ifdef O_EXCL
    if (flags & 32) oflags |= O_EXCL;
#endif
#ifdef O_CLOEXEC
    oflags |= O_CLOEXEC;
#endif
#if defined(__APPLE__) && !defined(O_DIRECT)
    /* macOS: use F_NOCACHE after open instead */
#endif

    int fd = open(pbuf, oflags, 0644);
    if (fd < 0) return -1;

#if defined(__APPLE__)
    if (flags & 16) {
        fcntl(fd, F_NOCACHE, 1);   /* macOS equivalent of O_DIRECT */
    }
#endif
#if !defined(O_CLOEXEC)
    /* Best-effort CLOEXEC if open flag unavailable. */
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) (void)fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
#endif

    return (int64_t)fd;
}

/* Close a file descriptor. */
static inline int64_t mako_file_close(int64_t fd) {
    if (fd < 0) return -1;
    return close((int)fd) == 0 ? 0 : -1;
}

/* ---- Positional I/O (no seek needed, thread-safe) ---- */

/* Read up to `count` bytes at file offset. Returns bytes read or -1. */
static inline MakoString mako_pread(int64_t fd, int64_t count, int64_t offset) {
    if (fd < 0 || count <= 0) return mako_str_from_cstr("");
    char *buf = (char *)malloc((size_t)count + 1);
    if (!buf) return mako_str_from_cstr("");
    ssize_t n = pread((int)fd, buf, (size_t)count, (off_t)offset);
    if (n <= 0) {
        free(buf);
        return mako_str_from_cstr("");
    }
    buf[n] = 0;
    return (MakoString){buf, (size_t)n};
}

/* Write data at file offset. Returns bytes written or -1. */
static inline int64_t mako_pwrite(int64_t fd, MakoString data, int64_t offset) {
    if (fd < 0 || !data.data || data.len == 0) return -1;
    ssize_t n = pwrite((int)fd, data.data, data.len, (off_t)offset);
    return (int64_t)n;
}

/* Append data to end of file. Returns bytes written. */
static inline int64_t mako_file_append(int64_t fd, MakoString data) {
    if (fd < 0 || !data.data || data.len == 0) return -1;
    ssize_t n = write((int)fd, data.data, data.len);
    return (int64_t)n;
}

/* ---- Durability ---- */

/* Flush file data + metadata to disk. */
static inline int64_t mako_fsync(int64_t fd) {
    if (fd < 0) return -1;
#if defined(__APPLE__)
    return fcntl((int)fd, F_FULLFSYNC) == 0 ? 0 : -1;  /* True flush on macOS */
#else
    return fsync((int)fd) == 0 ? 0 : -1;
#endif
}

/* Flush file data only (no metadata). Faster than fsync. */
static inline int64_t mako_fdatasync(int64_t fd) {
    if (fd < 0) return -1;
#if defined(__APPLE__)
    return fcntl((int)fd, F_FULLFSYNC) == 0 ? 0 : -1;
#elif defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
    return fdatasync((int)fd) == 0 ? 0 : -1;
#else
    return fsync((int)fd) == 0 ? 0 : -1;
#endif
}

/* ---- Memory-mapped I/O ---- */

/* MMap handle: tracks pointer + size for unmap */
typedef struct {
    void *ptr;
    size_t length;
    int fd;
} MakoMMap;

/* Memory-map a file. mode: 0=read-only, 1=read-write, 2=private (copy-on-write).
 * Returns MakoMMap pointer or NULL. */
static inline MakoMMap *mako_mmap_open(MakoString path, int64_t mode) {
    char pbuf[4096];
    if (!path.data || path.len >= sizeof(pbuf)) return NULL;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;

    int oflags = (mode >= 1) ? O_RDWR : O_RDONLY;
    int fd = open(pbuf, oflags);
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    size_t len = (size_t)st.st_size;
    if (len == 0) { close(fd); return NULL; }

    int prot = PROT_READ;
    int flags = MAP_SHARED;
    if (mode == 1) { prot |= PROT_WRITE; }
    if (mode == 2) { prot |= PROT_WRITE; flags = MAP_PRIVATE; }

    void *ptr = mmap(NULL, len, prot, flags, fd, 0);
    if (ptr == MAP_FAILED) { close(fd); return NULL; }

    MakoMMap *m = (MakoMMap *)malloc(sizeof(MakoMMap));
    if (!m) { munmap(ptr, len); close(fd); return NULL; }
    m->ptr = ptr;
    m->length = len;
    m->fd = fd;
    return m;
}

/* Create/extend a file and mmap it read-write. Good for pre-allocated DB files. */
static inline MakoMMap *mako_mmap_create(MakoString path, int64_t size) {
    char pbuf[4096];
    if (!path.data || path.len >= sizeof(pbuf) || size <= 0) return NULL;
    memcpy(pbuf, path.data, path.len);
    pbuf[path.len] = 0;

    int fd = open(pbuf, O_RDWR | O_CREAT, 0644);
    if (fd < 0) return NULL;

    /* Extend file to requested size */
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return NULL; }

    void *ptr = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) { close(fd); return NULL; }

    MakoMMap *m = (MakoMMap *)malloc(sizeof(MakoMMap));
    if (!m) { munmap(ptr, (size_t)size); close(fd); return NULL; }
    m->ptr = ptr;
    m->length = (size_t)size;
    m->fd = fd;
    return m;
}

/* Read bytes from mmap at offset. Zero-copy — returns a view into the mapped region. */
static inline MakoString mako_mmap_read(MakoMMap *m, int64_t offset, int64_t count) {
    if (!m || !m->ptr || offset < 0) return mako_str_from_cstr("");
    if ((size_t)(offset + count) > m->length) {
        count = (int64_t)(m->length - (size_t)offset);
    }
    if (count <= 0) return mako_str_from_cstr("");
    /* Return a copy (safe across potential remap) */
    char *buf = (char *)malloc((size_t)count + 1);
    if (!buf) return mako_str_from_cstr("");
    memcpy(buf, (char *)m->ptr + offset, (size_t)count);
    buf[count] = 0;
    return (MakoString){buf, (size_t)count};
}

/* Write bytes into mmap at offset. Direct memory write — very fast. */
static inline int64_t mako_mmap_write(MakoMMap *m, int64_t offset, MakoString data) {
    if (!m || !m->ptr || offset < 0 || !data.data) return -1;
    if ((size_t)(offset + (int64_t)data.len) > m->length) return -1;
    memcpy((char *)m->ptr + offset, data.data, data.len);
    return (int64_t)data.len;
}

/* Flush mmap dirty pages to disk. flags: 0=async, 1=sync. */
static inline int64_t mako_mmap_sync(MakoMMap *m, int64_t flags) {
    if (!m || !m->ptr) return -1;
    int mflags = (flags & 1) ? MS_SYNC : MS_ASYNC;
    return msync(m->ptr, m->length, mflags) == 0 ? 0 : -1;
}

/* Get the total size of the mmap. */
static inline int64_t mako_mmap_size(MakoMMap *m) {
    if (!m) return 0;
    return (int64_t)m->length;
}

/* Unmap and close. */
static inline int64_t mako_mmap_close(MakoMMap *m) {
    if (!m) return -1;
    int r = 0;
    if (m->ptr && m->length > 0) {
        r = munmap(m->ptr, m->length);
    }
    if (m->fd >= 0) close(m->fd);
    free(m);
    return r == 0 ? 0 : -1;
}

/* ---- Pre-allocation ---- */

/* Pre-allocate disk space for a file (avoids fragmentation). */
static inline int64_t mako_fallocate(int64_t fd, int64_t size) {
    if (fd < 0 || size <= 0) return -1;
#if defined(__APPLE__)
    /* fstore_t: { fst_flags, fst_posmode, fst_offset, fst_length, fst_bytesalloc } */
    struct {
        unsigned int fst_flags;
        int fst_posmode;
        off_t fst_offset;
        off_t fst_length;
        off_t fst_bytesalloc;
    } store = {2 /* F_ALLOCATECONTIG */, 3 /* F_PEOFPOSMODE */, 0, (off_t)size, 0};
    int r = fcntl((int)fd, F_PREALLOCATE, &store);
    if (r < 0) {
        store.fst_flags = 4; /* F_ALLOCATEALL */
        r = fcntl((int)fd, F_PREALLOCATE, &store);
    }
    if (r >= 0) ftruncate((int)fd, (off_t)size);
    return r >= 0 ? 0 : -1;
#elif defined(__linux__)
    return posix_fallocate((int)fd, 0, (off_t)size) == 0 ? 0 : -1;
#else
    return ftruncate((int)fd, (off_t)size) == 0 ? 0 : -1;
#endif
}

/* ---- File size/stat ---- */

/* Get file size by fd. */
static inline int64_t mako_file_size(int64_t fd) {
    if (fd < 0) return -1;
    struct stat st;
    if (fstat((int)fd, &st) < 0) return -1;
    return (int64_t)st.st_size;
}

/* Get file size by path (stat). -1 on error / missing. */
static inline int64_t mako_path_file_size(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 4096) return -1;
    char buf[4096];
    memcpy(buf, path.data, path.len);
    buf[path.len] = 0;
    struct stat st;
    if (stat(buf, &st) < 0) return -1;
    return (int64_t)st.st_size;
}

/* Truncate or extend file to given size. */
static inline int64_t mako_file_truncate(int64_t fd, int64_t size) {
    if (fd < 0) return -1;
    return ftruncate((int)fd, (off_t)size) == 0 ? 0 : -1;
}

/* Seek to position (for sequential read/write with file_append patterns).
 * whence: 0=SET, 1=CUR, 2=END. Returns new offset or -1. */
static inline int64_t mako_file_seek(int64_t fd, int64_t offset, int64_t whence) {
    if (fd < 0) return -1;
    int w = SEEK_SET;
    if (whence == 1) w = SEEK_CUR;
    if (whence == 2) w = SEEK_END;
    off_t r = lseek((int)fd, (off_t)offset, w);
    return (int64_t)r;
}

/* ---- Batch / vectored I/O ---- */

/* Write multiple strings in one syscall (writev). Returns total bytes written. */
static inline int64_t mako_file_writev(int64_t fd, MakoString *parts, int64_t count) {
    if (fd < 0 || !parts || count <= 0) return -1;
    struct iovec *iov = (struct iovec *)malloc(sizeof(struct iovec) * (size_t)count);
    if (!iov) return -1;
    for (int64_t i = 0; i < count; i++) {
        iov[i].iov_base = (void *)parts[i].data;
        iov[i].iov_len = parts[i].len;
    }
    ssize_t n = writev((int)fd, iov, (int)count);
    free(iov);
    return (int64_t)n;
}

/* Append two strings in one writev (group-commit / multi-record flush). */
static inline int64_t mako_file_append2(int64_t fd, MakoString a, MakoString b) {
    if (fd < 0) return -1;
    MakoString parts[2];
    int64_t n = 0;
    if (a.data && a.len > 0) parts[n++] = a;
    if (b.data && b.len > 0) parts[n++] = b;
    if (n == 0) return 0;
    if (n == 1) return mako_file_append(fd, parts[0]);
    return mako_file_writev(fd, parts, 2);
}

/* Append three strings in one writev. */
static inline int64_t mako_file_append3(int64_t fd, MakoString a, MakoString b, MakoString c) {
    if (fd < 0) return -1;
    MakoString parts[3];
    int64_t n = 0;
    if (a.data && a.len > 0) parts[n++] = a;
    if (b.data && b.len > 0) parts[n++] = b;
    if (c.data && c.len > 0) parts[n++] = c;
    if (n == 0) return 0;
    if (n == 1) return mako_file_append(fd, parts[0]);
    return mako_file_writev(fd, parts, n);
}

/* ---- Read helpers ---- */

/* Read exactly `count` bytes from fd at current position. Retries on short reads. */
static inline MakoString mako_file_read_exact(int64_t fd, int64_t count) {
    if (fd < 0 || count <= 0) return mako_str_from_cstr("");
    char *buf = (char *)malloc((size_t)count + 1);
    if (!buf) return mako_str_from_cstr("");
    size_t total = 0;
    while (total < (size_t)count) {
        ssize_t n = read((int)fd, buf + total, (size_t)count - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    if (total == 0) { free(buf); return mako_str_from_cstr(""); }
    buf[total] = 0;
    return (MakoString){buf, total};
}

/* ---- Storage primitives seed: fixed pages + WAL append log ---- */

#ifndef MAKO_PAGE_SIZE_DEFAULT
#define MAKO_PAGE_SIZE_DEFAULT 4096
#endif

typedef struct {
    char *data;
    size_t size;
} MakoPage;

/* Allocate a zeroed page (size bytes, default 4096 if size<=0). */
static inline MakoPage *mako_page_alloc(int64_t size) {
    size_t n = size > 0 ? (size_t)size : (size_t)MAKO_PAGE_SIZE_DEFAULT;
    MakoPage *p = (MakoPage *)calloc(1, sizeof(MakoPage));
    if (!p) return NULL;
    p->data = (char *)calloc(1, n);
    if (!p->data) {
        free(p);
        return NULL;
    }
    p->size = n;
    return p;
}

static inline int64_t mako_page_size(MakoPage *p) {
    return p ? (int64_t)p->size : -1;
}

static inline int64_t mako_page_write(MakoPage *p, int64_t off, MakoString data) {
    if (!p || !p->data || off < 0) return -1;
    if ((size_t)off + data.len > p->size) return -1;
    if (data.len && data.data) memcpy(p->data + (size_t)off, data.data, data.len);
    return (int64_t)data.len;
}

static inline MakoString mako_page_read(MakoPage *p, int64_t off, int64_t count) {
    if (!p || !p->data || off < 0 || count < 0) return mako_str_from_cstr("");
    if ((size_t)off >= p->size) return mako_str_from_cstr("");
    size_t n = (size_t)count;
    if ((size_t)off + n > p->size) n = p->size - (size_t)off;
    char *d = (char *)malloc(n + 1);
    if (!d) return mako_str_from_cstr("");
    if (n) memcpy(d, p->data + (size_t)off, n);
    d[n] = 0;
    return (MakoString){d, n};
}

static inline int64_t mako_page_free(MakoPage *p) {
    if (!p) return 0;
    free(p->data);
    free(p);
    return 0;
}

/* Simple file-backed write-ahead log: length-prefixed records. */
typedef struct {
    int fd;
    char path[512];
} MakoWal;

static inline MakoWal *mako_wal_open(MakoString path) {
    if (!path.data || path.len == 0 || path.len >= 500) return NULL;
#if defined(_WIN32)
    return NULL;
#else
    MakoWal *w = (MakoWal *)calloc(1, sizeof(MakoWal));
    if (!w) return NULL;
    memcpy(w->path, path.data, path.len);
    w->path[path.len] = 0;
    w->fd = open(w->path, O_RDWR | O_CREAT, 0644);
    if (w->fd < 0) {
        free(w);
        return NULL;
    }
    return w;
#endif
}

/* Append one record: [u32 le len][bytes]. Returns 0 ok, -1 fail. */
static inline int64_t mako_wal_append(MakoWal *w, MakoString rec) {
    if (!w || w->fd < 0) return -1;
#if defined(_WIN32)
    return -1;
#else
    uint32_t len = (uint32_t)rec.len;
    unsigned char hdr[4] = {
        (unsigned char)(len & 0xff),
        (unsigned char)((len >> 8) & 0xff),
        (unsigned char)((len >> 16) & 0xff),
        (unsigned char)((len >> 24) & 0xff),
    };
    if (write(w->fd, hdr, 4) != 4) return -1;
    if (len > 0) {
        if (!rec.data) return -1;
        if (write(w->fd, rec.data, len) != (ssize_t)len) return -1;
    }
    return 0;
#endif
}

static inline int64_t mako_wal_sync(MakoWal *w) {
    if (!w || w->fd < 0) return -1;
#if defined(_WIN32)
    return -1;
#else
    return fsync(w->fd) == 0 ? 0 : -1;
#endif
}

/* Byte length of WAL file. */
static inline int64_t mako_wal_size(MakoWal *w) {
    if (!w || w->fd < 0) return -1;
#if defined(_WIN32)
    return -1;
#else
    off_t cur = lseek(w->fd, 0, SEEK_CUR);
    off_t end = lseek(w->fd, 0, SEEK_END);
    if (cur >= 0) lseek(w->fd, cur, SEEK_SET);
    return (int64_t)end;
#endif
}

/* Last next-offset after wal_read_at (thread-local). */
static __thread int64_t mako_wal_last_next_off = -1;

static inline int64_t mako_wal_next_off(void) {
    return mako_wal_last_next_off;
}

/* Read record at byte offset; returns payload (empty on fail).
 * Updates wal_next_off() to the following record offset, or -1. */
static inline MakoString mako_wal_read_at(MakoWal *w, int64_t off) {
    mako_wal_last_next_off = -1;
    if (!w || w->fd < 0 || off < 0) return mako_str_from_cstr("");
#if defined(_WIN32)
    return mako_str_from_cstr("");
#else
    if (lseek(w->fd, (off_t)off, SEEK_SET) < 0) return mako_str_from_cstr("");
    unsigned char hdr[4];
    if (read(w->fd, hdr, 4) != 4) return mako_str_from_cstr("");
    uint32_t len = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16)
                   | ((uint32_t)hdr[3] << 24);
    if (len > 64 * 1024 * 1024) return mako_str_from_cstr(""); /* sanity */
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) return mako_str_from_cstr("");
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(w->fd, buf + got, len - got);
        if (n <= 0) {
            free(buf);
            return mako_str_from_cstr("");
        }
        got += (size_t)n;
    }
    buf[len] = 0;
    mako_wal_last_next_off = off + 4 + (int64_t)len;
    return (MakoString){buf, len};
#endif
}

static inline int64_t mako_wal_close(MakoWal *w) {
    if (!w) return 0;
#if !defined(_WIN32)
    if (w->fd >= 0) close(w->fd);
#endif
    free(w);
    return 0;
}

#endif /* !_WIN32 (POSIX) */

/* ---- Hash index + transactional store seed (portable) ---- */

#define MAKO_HINDEX_EMPTY  ((int64_t)0x7fffffffffffffffLL)
#define MAKO_HINDEX_TOMB   ((int64_t)0x7ffffffffffffffeLL)
#define MAKO_STORE_UNDO_MAX 256

typedef struct {
    int64_t *keys;
    int64_t *vals;
    size_t cap;
    size_t len; /* live entries */
} MakoHIndex;

static inline uint64_t mako_hindex_hash(int64_t k) {
    uint64_t x = (uint64_t)k;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static inline MakoHIndex *mako_hindex_new(int64_t cap) {
    size_t c = cap > 8 ? (size_t)cap : 16;
    /* power of two */
    size_t p = 16;
    while (p < c) p *= 2;
    MakoHIndex *h = (MakoHIndex *)calloc(1, sizeof(MakoHIndex));
    if (!h) return NULL;
    h->keys = (int64_t *)malloc(p * sizeof(int64_t));
    h->vals = (int64_t *)malloc(p * sizeof(int64_t));
    if (!h->keys || !h->vals) {
        free(h->keys);
        free(h->vals);
        free(h);
        return NULL;
    }
    for (size_t i = 0; i < p; i++) h->keys[i] = MAKO_HINDEX_EMPTY;
    h->cap = p;
    h->len = 0;
    return h;
}

static inline int64_t mako_hindex_len(MakoHIndex *h) {
    return h ? (int64_t)h->len : 0;
}

static inline int64_t mako_hindex_get(MakoHIndex *h, int64_t key) {
    if (!h || !h->keys) return -1;
    size_t mask = h->cap - 1;
    size_t i = (size_t)mako_hindex_hash(key) & mask;
    for (size_t n = 0; n < h->cap; n++) {
        int64_t k = h->keys[i];
        if (k == MAKO_HINDEX_EMPTY) return -1;
        if (k == key) return h->vals[i];
        i = (i + 1) & mask;
    }
    return -1;
}

static inline int mako_hindex_put_raw(MakoHIndex *h, int64_t key, int64_t val) {
    if (!h || key == MAKO_HINDEX_EMPTY || key == MAKO_HINDEX_TOMB) return -1;
    if (h->len * 10 >= h->cap * 7) {
        /* grow */
        size_t ncap = h->cap * 2;
        int64_t *nk = (int64_t *)malloc(ncap * sizeof(int64_t));
        int64_t *nv = (int64_t *)malloc(ncap * sizeof(int64_t));
        if (!nk || !nv) {
            free(nk);
            free(nv);
            return -1;
        }
        for (size_t i = 0; i < ncap; i++) nk[i] = MAKO_HINDEX_EMPTY;
        size_t oldc = h->cap;
        int64_t *ok = h->keys;
        int64_t *ov = h->vals;
        h->keys = nk;
        h->vals = nv;
        h->cap = ncap;
        h->len = 0;
        for (size_t i = 0; i < oldc; i++) {
            if (ok[i] != MAKO_HINDEX_EMPTY && ok[i] != MAKO_HINDEX_TOMB) {
                (void)mako_hindex_put_raw(h, ok[i], ov[i]);
            }
        }
        free(ok);
        free(ov);
    }
    size_t mask = h->cap - 1;
    size_t i = (size_t)mako_hindex_hash(key) & mask;
    size_t tomb = (size_t)-1;
    for (size_t n = 0; n < h->cap; n++) {
        int64_t k = h->keys[i];
        if (k == key) {
            h->vals[i] = val;
            return 0;
        }
        if (k == MAKO_HINDEX_TOMB && tomb == (size_t)-1) tomb = i;
        if (k == MAKO_HINDEX_EMPTY) {
            size_t slot = (tomb != (size_t)-1) ? tomb : i;
            if (h->keys[slot] == MAKO_HINDEX_EMPTY) h->len++;
            else if (h->keys[slot] == MAKO_HINDEX_TOMB) h->len++;
            h->keys[slot] = key;
            h->vals[slot] = val;
            return 0;
        }
        i = (i + 1) & mask;
    }
    return -1;
}

static inline int64_t mako_hindex_put(MakoHIndex *h, int64_t key, int64_t val) {
    return mako_hindex_put_raw(h, key, val) == 0 ? 0 : -1;
}

static inline int64_t mako_hindex_del(MakoHIndex *h, int64_t key) {
    if (!h) return -1;
    size_t mask = h->cap - 1;
    size_t i = (size_t)mako_hindex_hash(key) & mask;
    for (size_t n = 0; n < h->cap; n++) {
        int64_t k = h->keys[i];
        if (k == MAKO_HINDEX_EMPTY) return -1;
        if (k == key) {
            h->keys[i] = MAKO_HINDEX_TOMB;
            if (h->len > 0) h->len--;
            return 0;
        }
        i = (i + 1) & mask;
    }
    return -1;
}

static inline int64_t mako_hindex_free(MakoHIndex *h) {
    if (!h) return 0;
    free(h->keys);
    free(h->vals);
    free(h);
    return 0;
}

/* In-memory store with optional undo (txn) and optional WAL logging of commits. */
typedef struct {
    int64_t key;
    int64_t old_val;
    int had; /* 1 if key existed before put/del */
    int is_del;
} MakoStoreUndo;

typedef struct {
    MakoHIndex *idx;
    MakoWal *wal; /* optional, not owned */
    int in_txn;
    MakoStoreUndo undo[MAKO_STORE_UNDO_MAX];
    int undo_len;
} MakoStore;

static inline MakoStore *mako_store_new(int64_t cap) {
    MakoStore *s = (MakoStore *)calloc(1, sizeof(MakoStore));
    if (!s) return NULL;
    s->idx = mako_hindex_new(cap);
    if (!s->idx) {
        free(s);
        return NULL;
    }
    return s;
}

static inline int64_t mako_store_attach_wal(MakoStore *s, MakoWal *w) {
    if (!s) return -1;
    s->wal = w;
    return 0;
}

static inline int64_t mako_store_get(MakoStore *s, int64_t key) {
    return s ? mako_hindex_get(s->idx, key) : -1;
}

static inline int64_t mako_store_begin(MakoStore *s) {
    if (!s || s->in_txn) return -1;
    s->in_txn = 1;
    s->undo_len = 0;
    return 0;
}

static inline void mako_store_note_undo(MakoStore *s, int64_t key, int is_del) {
    if (!s || !s->in_txn || s->undo_len >= MAKO_STORE_UNDO_MAX) return;
    int64_t old = mako_hindex_get(s->idx, key);
    MakoStoreUndo *u = &s->undo[s->undo_len++];
    u->key = key;
    u->old_val = old;
    u->had = (old != -1) ? 1 : 0;
    u->is_del = is_del;
}

static inline int64_t mako_store_put(MakoStore *s, int64_t key, int64_t val) {
    if (!s) return -1;
    if (s->in_txn) mako_store_note_undo(s, key, 0);
    return mako_hindex_put(s->idx, key, val);
}

static inline int64_t mako_store_del(MakoStore *s, int64_t key) {
    if (!s) return -1;
    if (s->in_txn) mako_store_note_undo(s, key, 1);
    return mako_hindex_del(s->idx, key);
}

static inline int64_t mako_store_rollback(MakoStore *s) {
    if (!s || !s->in_txn) return -1;
    for (int i = s->undo_len - 1; i >= 0; i--) {
        MakoStoreUndo *u = &s->undo[i];
        if (u->had) {
            (void)mako_hindex_put(s->idx, u->key, u->old_val);
        } else {
            (void)mako_hindex_del(s->idx, u->key);
        }
    }
    s->undo_len = 0;
    s->in_txn = 0;
    return 0;
}

static inline int64_t mako_store_commit(MakoStore *s) {
    if (!s || !s->in_txn) return -1;
#if !defined(_WIN32)
    if (s->wal) {
        /* Log one record per undo entry as "P,key,val" or "D,key" */
        for (int i = 0; i < s->undo_len; i++) {
            char buf[64];
            MakoStoreUndo *u = &s->undo[i];
            int64_t cur = mako_hindex_get(s->idx, u->key);
            int n;
            if (u->is_del && cur < 0) {
                n = snprintf(buf, sizeof(buf), "D,%lld", (long long)u->key);
            } else {
                n = snprintf(
                    buf, sizeof(buf), "P,%lld,%lld", (long long)u->key, (long long)cur
                );
            }
            if (n > 0) {
                MakoString rec = {(char *)buf, (size_t)n};
                if (mako_wal_append(s->wal, rec) != 0) return -1;
            }
        }
        if (mako_wal_sync(s->wal) != 0) return -1;
    }
#else
    (void)s;
#endif
    s->undo_len = 0;
    s->in_txn = 0;
    return 0;
}

static inline int64_t mako_store_len(MakoStore *s) {
    return s ? mako_hindex_len(s->idx) : 0;
}

static inline int64_t mako_store_free(MakoStore *s) {
    if (!s) return 0;
    mako_hindex_free(s->idx);
    free(s);
    return 0;
}

/* ---- Game snapshot seed: pack/unpack int64 entity slots ---- */

/* Encode n int64 values as little-endian bytes (owned string buffer). */
static inline MakoString mako_snap_encode(const int64_t *vals, int64_t n) {
    if (!vals || n <= 0) return mako_str_from_cstr("");
    size_t bytes = (size_t)n * 8;
    char *d = (char *)malloc(bytes + 1);
    if (!d) return mako_str_from_cstr("");
    for (int64_t i = 0; i < n; i++) {
        uint64_t v = (uint64_t)vals[i];
        size_t o = (size_t)i * 8;
        d[o + 0] = (char)(v & 0xff);
        d[o + 1] = (char)((v >> 8) & 0xff);
        d[o + 2] = (char)((v >> 16) & 0xff);
        d[o + 3] = (char)((v >> 24) & 0xff);
        d[o + 4] = (char)((v >> 32) & 0xff);
        d[o + 5] = (char)((v >> 40) & 0xff);
        d[o + 6] = (char)((v >> 48) & 0xff);
        d[o + 7] = (char)((v >> 56) & 0xff);
    }
    d[bytes] = 0;
    return (MakoString){d, bytes};
}

/* Encode up to 8 int64 args (convenience for Mako without int arrays). */
static inline MakoString mako_snap_encode2(int64_t a, int64_t b) {
    int64_t v[2] = {a, b};
    return mako_snap_encode(v, 2);
}

static inline MakoString mako_snap_encode4(int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t v[4] = {a, b, c, d};
    return mako_snap_encode(v, 4);
}

static inline int64_t mako_snap_count(MakoString s) {
    return (int64_t)(s.len / 8);
}

static inline int64_t mako_snap_get(MakoString s, int64_t i) {
    if (i < 0 || (size_t)(i + 1) * 8 > s.len || !s.data) return 0;
    size_t o = (size_t)i * 8;
    uint64_t v = 0;
    v |= (uint64_t)(unsigned char)s.data[o + 0];
    v |= (uint64_t)(unsigned char)s.data[o + 1] << 8;
    v |= (uint64_t)(unsigned char)s.data[o + 2] << 16;
    v |= (uint64_t)(unsigned char)s.data[o + 3] << 24;
    v |= (uint64_t)(unsigned char)s.data[o + 4] << 32;
    v |= (uint64_t)(unsigned char)s.data[o + 5] << 40;
    v |= (uint64_t)(unsigned char)s.data[o + 6] << 48;
    v |= (uint64_t)(unsigned char)s.data[o + 7] << 56;
    return (int64_t)v;
}

/* Client prediction seed: apply input delta to a local state slot. */
/* Per-slot delta of two snap blobs (n = min count). Returns encode4 of first 4 deltas. */
static inline MakoString mako_snap_diff(MakoString a, MakoString b) {
    int64_t na = mako_snap_count(a);
    int64_t nb = mako_snap_count(b);
    int64_t n = na < nb ? na : nb;
    if (n < 1) return mako_snap_encode4(0, 0, 0, 0);
    int64_t d0 = n > 0 ? mako_snap_get(b, 0) - mako_snap_get(a, 0) : 0;
    int64_t d1 = n > 1 ? mako_snap_get(b, 1) - mako_snap_get(a, 1) : 0;
    int64_t d2 = n > 2 ? mako_snap_get(b, 2) - mako_snap_get(a, 2) : 0;
    int64_t d3 = n > 3 ? mako_snap_get(b, 3) - mako_snap_get(a, 3) : 0;
    return mako_snap_encode4(d0, d1, d2, d3);
}

/* Apply delta snap to base: out[i] = base[i] + delta[i]. */
static inline MakoString mako_snap_apply_delta(MakoString base, MakoString delta) {
    int64_t n = mako_snap_count(base);
    int64_t nd = mako_snap_count(delta);
    if (n < 1) return mako_snap_encode4(0, 0, 0, 0);
    int64_t v0 = mako_snap_get(base, 0) + (nd > 0 ? mako_snap_get(delta, 0) : 0);
    int64_t v1 = (n > 1 ? mako_snap_get(base, 1) : 0) + (nd > 1 ? mako_snap_get(delta, 1) : 0);
    int64_t v2 = (n > 2 ? mako_snap_get(base, 2) : 0) + (nd > 2 ? mako_snap_get(delta, 2) : 0);
    int64_t v3 = (n > 3 ? mako_snap_get(base, 3) : 0) + (nd > 3 ? mako_snap_get(delta, 3) : 0);
    return mako_snap_encode4(v0, v1, v2, v3);
}

/* Lag-compensated tick: server_tick - rtt_ms / tick_ms (seed). */
static inline int64_t mako_netcode_lag_comp_tick(int64_t server_tick, int64_t rtt_ms, int64_t tick_ms) {
    if (tick_ms <= 0) tick_ms = 16;
    int64_t lag = rtt_ms / tick_ms;
    if (lag < 0) lag = 0;
    return server_tick - lag;
}

/* Linear interpolate fixed-point milli: a + (b-a)*t_milli/1000 */
static inline int64_t mako_netcode_interp(int64_t a, int64_t b, int64_t t_milli) {
    if (t_milli <= 0) return a;
    if (t_milli >= 1000) return b;
    return a + ((b - a) * t_milli) / 1000;
}

static inline int64_t mako_snap_predict(int64_t state, int64_t input_delta) {
    return state + input_delta;
}

/* Server reconciliation seed: if predicted != authoritative, snap to auth. */
static inline int64_t mako_snap_reconcile(int64_t predicted, int64_t auth) {
    return auth;
}

#endif /* MAKO_DIO_H */
