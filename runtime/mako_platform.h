/* Mako OS portability — sockets, threads, clocks.
 * Windows MSVC: Winsock2 + CriticalSection/ConditionVariable shims.
 * Windows mingw/zig: Winsock2 + winpthreads (real pthread.h).
 * Unix: pthread + BSD sockets.
 */
#ifndef MAKO_PLATFORM_H
#define MAKO_PLATFORM_H

#if !defined(_WIN32) && !defined(_WIN64)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if defined(__APPLE__)
extern void arc4random_buf(void *buf, size_t nbytes);
#endif

#if defined(_WIN32) || defined(_WIN64)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#pragma comment(lib, "ws2_32.lib")

typedef SOCKET mako_sock_t;
#define MAKO_INVALID_SOCK INVALID_SOCKET
#define MAKO_SOCK_ERR SOCKET_ERROR
#define MAKO_NO_TIMEDJOIN 1

#ifndef ssize_t
#if defined(_WIN64)
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#endif

static inline int mako_net_init(void) {
    static LONG once = 0;
    static int ok = 0;
    if (InterlockedCompareExchange(&once, 1, 0) == 0) {
        WSADATA wsa;
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? 1 : 0;
    }
    return ok;
}

static inline int mako_sock_close(mako_sock_t s) {
    return closesocket(s) == 0 ? 0 : -1;
}

static inline void mako_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
    Sleep((DWORD)ms);
}

#if !defined(CLOCK_REALTIME)
#define CLOCK_REALTIME 0
#endif
#if !defined(CLOCK_MONOTONIC)
#define CLOCK_MONOTONIC 1
#endif

static inline int mako_gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    ULONGLONG t = u.QuadPart - 116444736000000000ULL;
    tv->tv_sec = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10);
    return 0;
}

static inline int mako_setenv(const char *k, const char *v) {
#if defined(_MSC_VER)
    return _putenv_s(k, v ? v : "") == 0 ? 0 : -1;
#else
    size_t n = strlen(k) + 1 + (v ? strlen(v) : 0) + 1;
    char *buf = (char *)malloc(n);
    if (!buf) return -1;
    snprintf(buf, n, "%s=%s", k, v ? v : "");
    int rc = _putenv(buf);
    free(buf);
    return rc == 0 ? 0 : -1;
#endif
}

/* Use winpthreads only when explicitly requested; otherwise native shims keep
 * zig/mingw cross builds independent of pthread.h. */
#if defined(MAKO_USE_WINPTHREADS)
#include <pthread.h>
/* clock_gettime / nanosleep come from winpthreads */
#else

/* MinGW's time.h (including Zig's MinGW libc) already declares these POSIX
 * clock/sleep functions. MSVC does not, so only provide the shims there. A
 * redeclaration is an error with Zig's Windows target headers, not merely a
 * harmless compatibility warning. */
#if !defined(MAKO_HAS_CLOCK_GETTIME) && !defined(__MINGW32__) && !defined(__MINGW64__)
static inline int clock_gettime(int clock_id, struct timespec *ts) {
    (void)clock_id;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    ULONGLONG t = u.QuadPart - 116444736000000000ULL;
    ts->tv_sec = (time_t)(t / 10000000ULL);
    ts->tv_nsec = (long)((t % 10000000ULL) * 100);
    return 0;
}

static inline int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (!req) return -1;
    int64_t ms = (int64_t)req->tv_sec * 1000 + req->tv_nsec / 1000000;
    if (ms < 0) ms = 0;
    if (ms == 0 && req->tv_nsec > 0) ms = 1;
    mako_sleep_ms(ms <= 0 ? 1 : ms);
    return 0;
}
#endif /* !MAKO_HAS_CLOCK_GETTIME && !MinGW */

typedef CRITICAL_SECTION pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef HANDLE pthread_t;
typedef void *pthread_attr_t;
typedef void *pthread_mutexattr_t;
typedef void *pthread_condattr_t;

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) {
    (void)a;
    InitializeCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_destroy(pthread_mutex_t *m) {
    DeleteCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_lock(pthread_mutex_t *m) {
    EnterCriticalSection(m);
    return 0;
}
static inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) {
    (void)a;
    InitializeConditionVariable(c);
    return 0;
}
static inline int pthread_cond_destroy(pthread_cond_t *c) {
    (void)c;
    return 0;
}
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1;
}
/* Absolute-time timed wait (POSIX-shaped). Converts abstime → relative ms. */
#ifndef ETIMEDOUT
#define ETIMEDOUT 138 /* WSAETIMEDOUT-ish; MSVC may lack POSIX ETIMEDOUT */
#endif
static inline int pthread_cond_timedwait(
    pthread_cond_t *c,
    pthread_mutex_t *m,
    const struct timespec *abstime
) {
    if (!c || !m || !abstime) return -1;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int64_t wait_ms =
        ((int64_t)abstime->tv_sec - (int64_t)now.tv_sec) * 1000 +
        ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec) / 1000000;
    if (wait_ms <= 0) {
        SetLastError(ERROR_TIMEOUT);
        return ETIMEDOUT;
    }
    if (wait_ms > 0x7fffffff) wait_ms = 0x7fffffff;
    if (SleepConditionVariableCS(c, m, (DWORD)wait_ms)) return 0;
    if (GetLastError() == ERROR_TIMEOUT) return ETIMEDOUT;
    return -1;
}
static inline int pthread_cond_signal(pthread_cond_t *c) {
    WakeConditionVariable(c);
    return 0;
}
static inline int pthread_cond_broadcast(pthread_cond_t *c) {
    WakeAllConditionVariable(c);
    return 0;
}

typedef struct {
    void *(*fn)(void *);
    void *arg;
} mako_win_thread_arg;

static unsigned __stdcall mako_win_thread_trampoline(void *p) {
    mako_win_thread_arg *a = (mako_win_thread_arg *)p;
    void *(*fn)(void *) = a->fn;
    void *arg = a->arg;
    free(a);
    fn(arg);
    return 0;
}

static inline int pthread_create(
    pthread_t *t,
    const pthread_attr_t *attr,
    void *(*start)(void *),
    void *arg
) {
    (void)attr;
    mako_win_thread_arg *a = (mako_win_thread_arg *)malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = start;
    a->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, mako_win_thread_trampoline, a, 0, NULL);
    if (!h) {
        free(a);
        return -1;
    }
    *t = (HANDLE)h;
    return 0;
}

static inline int pthread_join(pthread_t t, void **retval) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    if (retval) *retval = NULL;
    return 0;
}

#endif /* MSVC vs mingw */

#else /* Unix */

#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#if !defined(MAKO_WASI)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#endif

typedef int mako_sock_t;
#define MAKO_INVALID_SOCK (-1)
#define MAKO_SOCK_ERR (-1)

static inline int mako_net_init(void) { return 1; }
static inline int mako_sock_close(mako_sock_t s) { return close(s); }

static inline void mako_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
}

static inline int mako_gettimeofday(struct timeval *tv, void *tz) {
    return gettimeofday(tv, (struct timezone *)tz);
}

static inline int mako_setenv(const char *k, const char *v) {
    return setenv(k, v, 1) == 0 ? 0 : -1;
}

#if defined(__APPLE__) || defined(__wasi__)
#define MAKO_NO_TIMEDJOIN 1
#endif

#endif /* _WIN32 */

#endif /* MAKO_PLATFORM_H */
