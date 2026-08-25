// Equivalent Rust microbench (release).
use std::collections::HashMap;
use std::hint::black_box;
use std::sync::mpsc::sync_channel;
use std::time::Instant;

struct Pair {
    x: i64,
    y: i64,
}

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

fn bench_struct() -> i64 {
    let n = opaque_add(1_000_000, 0);
    let mut sum = 0i64;
    for i in 0..n {
        let x = black_box(i);
        let p = Pair { x, y: x + 1 };
        sum += p.x + p.y;
    }
    black_box(sum)
}

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

fn bench_strings() -> i64 {
    let n = opaque_add(20_000, 0);
    let mut total = 0i64;
    for i in 0..n {
        let s = "item-".to_owned() + &i.to_string();
        total += s.len() as i64;
    }
    black_box(total)
}

fn bench_channels() -> i64 {
    let n = opaque_add(50_000, 0);
    let (tx, rx) = sync_channel::<i64>(1);
    let mut sum = 0i64;
    for i in 0..n {
        black_box(&tx).send(black_box(i)).unwrap();
        sum += black_box(black_box(&rx).recv().unwrap());
    }
    black_box(sum)
}

fn main() {
    let _ = bench_fib();
    let _ = bench_struct();
    let _ = bench_slice();
    let _ = bench_map();
    let _ = bench_strings();
    let _ = bench_channels();

    let t0 = Instant::now();
    let f = bench_fib();
    let t1 = Instant::now();
    let st = bench_struct();
    let t2 = Instant::now();
    let s = bench_slice();
    let t3 = Instant::now();
    let m = bench_map();
    let t4 = Instant::now();
    let str_ = bench_strings();
    let t5 = Instant::now();
    let ch = bench_channels();
    let t6 = Instant::now();

    println!("lang");
    println!("rust");
    println!("fib30x5");
    println!("{f}");
    println!("{}", t1.duration_since(t0).as_nanos());
    println!("struct1m");
    println!("{st}");
    println!("{}", t2.duration_since(t1).as_nanos());
    println!("slice100k");
    println!("{s}");
    println!("{}", t3.duration_since(t2).as_nanos());
    println!("map50k");
    println!("{m}");
    println!("{}", t4.duration_since(t3).as_nanos());
    println!("string20k");
    println!("{str_}");
    println!("{}", t5.duration_since(t4).as_nanos());
    println!("chan50k");
    println!("{ch}");
    println!("{}", t6.duration_since(t5).as_nanos());
}
