#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t io_checksum(int rounds, int chunk) {
    const char *path = "/tmp/mako_native_io_bench.dat";
    char *payload = malloc((size_t)chunk + 1);
    if (!payload) abort();
    memset(payload, 'x', (size_t)chunk);
    payload[chunk] = '\0';
    int64_t acc = 0;
    char *buf = malloc((size_t)chunk + 1);
    if (!buf) abort();
    for (int r = 0; r < rounds; ++r) {
        FILE *wf = fopen(path, "wb");
        if (!wf) abort();
        size_t nw = fwrite(payload, 1, (size_t)chunk, wf);
        fclose(wf);
        /* write_file in Mako returns 0 on success — match that for output. */
        (void)nw;
        acc += 0;
        FILE *rf = fopen(path, "rb");
        if (!rf) abort();
        size_t nr = fread(buf, 1, (size_t)chunk, rf);
        fclose(rf);
        acc += (int64_t)nr;
    }
    free(payload);
    free(buf);
    return acc;
}

int main(void) {
    printf("%lld\n", (long long)io_checksum(50, 4096));
    return 0;
}
