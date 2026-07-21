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
    println!("{}", slice_checksum(5_000_000));
}
