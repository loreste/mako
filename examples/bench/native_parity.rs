fn fib(n: i64) -> i64 {
    if n < 2 {
        n
    } else {
        fib(n - 1) + fib(n - 2)
    }
}

fn slice_checksum(n: usize) -> i64 {
    let mut values = Vec::with_capacity(n);
    let mut state = 1i64;
    for _ in 0..n {
        state = (state * 48271) % 2147483647;
        values.push(state);
    }
    values.iter().sum()
}

fn main() {
    println!("{}", fib(40));
    println!("{}", slice_checksum(5_000_000));
}
