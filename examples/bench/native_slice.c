#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int64_t slice_checksum(int64_t n) {
    int64_t *values = malloc((size_t)n * sizeof(*values));
    if (!values) abort();
    int64_t state = 1;
    for (int64_t i = 0; i < n; i++) {
        state = (state * 48271) % 2147483647;
        values[i] = state;
    }
    int64_t sum = 0;
    for (int64_t i = 0; i < n; i++) sum += values[i];
    free(values);
    return sum;
}

int main(void) {
    printf("%lld\n", (long long)slice_checksum(5000000));
    return 0;
}
