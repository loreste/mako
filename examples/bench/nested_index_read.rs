// Rust equivalent of nested_index_read.mko (issue #66).
use std::hint::black_box;
use std::time::Instant;

struct Cell {
    score: i64,
    label: &'static str,
}

struct Row {
    cells: Vec<Cell>,
    weight: i64,
}

struct Grid {
    rows: Vec<Row>,
}

fn make_grid(n_rows: i64, n_cells: i64) -> Grid {
    let mut rows = Vec::with_capacity(n_rows as usize);
    for r in 0..n_rows {
        let mut cells = Vec::with_capacity(n_cells as usize);
        for c in 0..n_cells {
            cells.push(Cell {
                score: r * n_cells + c,
                label: "x",
            });
        }
        rows.push(Row {
            cells,
            weight: r + 1,
        });
    }
    Grid { rows }
}

fn scan_scores(g: &Grid, n_rows: i64, n_cells: i64) -> i64 {
    let mut sum = 0i64;
    for r in 0..n_rows as usize {
        let row = &g.rows[r];
        for c in 0..n_cells as usize {
            sum += row.cells[c].score + row.weight;
        }
    }
    black_box(sum)
}

fn scan_labels(g: &Grid, n_rows: i64, n_cells: i64) -> i64 {
    let mut n = 0i64;
    for r in 0..n_rows as usize {
        let row = &g.rows[r];
        for c in 0..n_cells as usize {
            n += row.cells[c].label.len() as i64;
        }
    }
    black_box(n)
}

fn main() {
    let n_rows = black_box(80i64);
    let n_cells = black_box(80i64);
    let scans = black_box(400i64);
    let g = make_grid(n_rows, n_cells);

    let mut acc = 0i64;
    let t0 = Instant::now();
    for _ in 0..scans {
        acc += scan_scores(&g, n_rows, n_cells);
    }
    let t1 = Instant::now();
    let mut nlab = 0i64;
    for _ in 0..scans {
        nlab += scan_labels(&g, n_rows, n_cells);
    }
    let t2 = Instant::now();

    println!("lang");
    println!("rust");
    println!("nested_index_scores");
    println!("{acc}");
    println!("{}", t1.duration_since(t0).as_nanos());
    println!("nested_index_labels");
    println!("{nlab}");
    println!("{}", t2.duration_since(t1).as_nanos());
}
