// Equivalent Rust microbench (release).
use std::collections::HashMap;
use std::hint::black_box;
use std::time::Instant;

fn fib(n: i64) -> i64 {
    if n < 2 {
        n
    } else {
        fib(n - 1) + fib(n - 2)
    }
}

fn opaque_add(a: i64, b: i64) -> i64 {
    let t = Instant::now().elapsed().as_nanos() as i64;
    a + b + (t - t)
}

fn bench_fib() -> i64 {
    let n = opaque_add(30, 0);
    let iters = opaque_add(5, 0);
    let mut acc = 0i64;
    for _ in 0..iters {
        acc += fib(black_box(n));
    }
    black_box(acc)
}

fn bench_slice() -> i64 {
    let n = opaque_add(100_000, 0) as usize;
    let mut a: Vec<i64> = Vec::with_capacity(n);
    for i in 0..n {
        a.push(i as i64);
    }
    black_box(a.len() as i64)
}

// Rust's default HashMap hashes with SipHash-1-3, which buys DoS resistance
// Mako's integer map does not attempt. Comparing against it measures that
// choice, not the compiler: with an identity hasher on both sides the gap on
// this kernel is about 1.4x, not 8x. Kept as the default-vs-default number,
// which is what a user actually gets, but it is not a like-for-like hash.
fn bench_map() -> i64 {
    let n = opaque_add(50_000, 0);
    let mut m: HashMap<i64, i64> = HashMap::with_capacity(n as usize);
    for i in 0..n {
        m.insert(i, i * 2);
    }
    let mut sum = 0i64;
    for i in 0..n {
        sum += m[&i];
    }
    black_box(sum)
}

fn main() {
    let _ = bench_fib();
    let _ = bench_slice();
    let _ = bench_map();

    let t0 = Instant::now();
    let f = bench_fib();
    let t1 = Instant::now();
    let s = bench_slice();
    let t2 = Instant::now();
    let m = bench_map();
    let t3 = Instant::now();

    println!("lang");
    println!("rust");
    println!("fib30x5");
    println!("{f}");
    println!("{}", t1.duration_since(t0).as_nanos());
    println!("slice100k");
    println!("{s}");
    println!("{}", t2.duration_since(t1).as_nanos());
    println!("map50k");
    println!("{m}");
    println!("{}", t3.duration_since(t2).as_nanos());
}
