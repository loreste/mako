/* Hand-C pointer-chase equivalent of nested_index_read.mko (issue #66). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int64_t score;
    const char *label;
} Cell;

typedef struct {
    Cell *cells;
    int64_t n_cells;
    int64_t weight;
} Row;

typedef struct {
    Row *rows;
    int64_t n_rows;
} Grid;

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static int64_t black_box(int64_t x) {
    __asm__ __volatile__("" : "+r"(x));
    return x;
}

static Grid make_grid(int64_t n_rows, int64_t n_cells) {
    Grid g;
    g.n_rows = n_rows;
    g.rows = (Row *)calloc((size_t)n_rows, sizeof(Row));
    if (!g.rows) abort();
    for (int64_t r = 0; r < n_rows; r++) {
        g.rows[r].weight = r + 1;
        g.rows[r].n_cells = n_cells;
        g.rows[r].cells = (Cell *)calloc((size_t)n_cells, sizeof(Cell));
        if (!g.rows[r].cells) abort();
        for (int64_t c = 0; c < n_cells; c++) {
            g.rows[r].cells[c].score = r * n_cells + c;
            g.rows[r].cells[c].label = "x";
        }
    }
    return g;
}

static int64_t scan_scores(Grid *g, int64_t n_rows, int64_t n_cells) {
    int64_t sum = 0;
    for (int64_t r = 0; r < n_rows; r++) {
        Row *row = &g->rows[r];
        for (int64_t c = 0; c < n_cells; c++) {
            sum += row->cells[c].score + row->weight;
        }
    }
    return black_box(sum);
}

static int64_t scan_labels(Grid *g, int64_t n_rows, int64_t n_cells) {
    int64_t n = 0;
    for (int64_t r = 0; r < n_rows; r++) {
        Row *row = &g->rows[r];
        for (int64_t c = 0; c < n_cells; c++) {
            n += (int64_t)strlen(row->cells[c].label);
        }
    }
    return black_box(n);
}

int main(void) {
    int64_t n_rows = black_box(80);
    int64_t n_cells = black_box(80);
    int64_t scans = black_box(400);
    Grid g = make_grid(n_rows, n_cells);

    int64_t acc = 0;
    int64_t t0 = now_ns();
    for (int64_t i = 0; i < scans; i++) {
        acc += scan_scores(&g, n_rows, n_cells);
    }
    int64_t t1 = now_ns();
    int64_t nlab = 0;
    for (int64_t i = 0; i < scans; i++) {
        nlab += scan_labels(&g, n_rows, n_cells);
    }
    int64_t t2 = now_ns();

    printf("lang\n");
    printf("c\n");
    printf("nested_index_scores\n");
    printf("%lld\n", (long long)acc);
    printf("%lld\n", (long long)(t1 - t0));
    printf("nested_index_labels\n");
    printf("%lld\n", (long long)nlab);
    printf("%lld\n", (long long)(t2 - t1));
    return 0;
}
