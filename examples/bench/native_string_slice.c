#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const int64_t n = 100000;
    char **values = calloc((size_t)n, sizeof(*values));
    if (!values) abort();
    for (int64_t i = 0; i < n; ++i) {
        const char *source = (i % 2 == 0) ? "alpha" : "beta";
        values[i] = strdup(source);
        if (!values[i]) abort();
    }
    puts(values[0]);
    puts(values[n - 1]);
    printf("%lld\n", (long long)n);
    for (int64_t i = 0; i < n; ++i) free(values[i]);
    free(values);
    return 0;
}
