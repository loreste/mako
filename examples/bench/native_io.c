#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Same syscall shape as Mako read_file/write_file: open + write/read + close. */
static int64_t io_checksum(int rounds, int chunk) {
    const char *path = "/tmp/mako_native_io_bench.dat";
    char *payload = malloc((size_t)chunk);
    if (!payload) abort();
    memset(payload, 'x', (size_t)chunk);
    int64_t acc = 0;
    char *buf = malloc((size_t)chunk + 1);
    if (!buf) abort();
    for (int r = 0; r < rounds; ++r) {
        int wfd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (wfd < 0) abort();
        size_t put = 0;
        while (put < (size_t)chunk) {
            ssize_t w = write(wfd, payload + put, (size_t)chunk - put);
            if (w < 0) abort();
            if (w == 0) break;
            put += (size_t)w;
        }
        if (close(wfd) != 0) abort();
        /* write_file in Mako returns 0 on success — match that for output. */
        acc += 0;

        int rfd = open(path, O_RDONLY);
        if (rfd < 0) abort();
        struct stat st;
        if (fstat(rfd, &st) != 0) abort();
        size_t want = (size_t)st.st_size;
        if (want > (size_t)chunk) want = (size_t)chunk;
        size_t got = 0;
        while (got < want) {
            ssize_t n = read(rfd, buf + got, want - got);
            if (n < 0) abort();
            if (n == 0) break;
            got += (size_t)n;
        }
        close(rfd);
        acc += (int64_t)got;
    }
    free(payload);
    free(buf);
    return acc;
}

int main(void) {
    printf("%lld\n", (long long)io_checksum(500, 4096));
    return 0;
}
