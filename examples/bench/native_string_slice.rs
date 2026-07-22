fn main() {
    let n = 100_000usize;
    let mut values = Vec::with_capacity(n);
    for i in 0..n {
        values.push(if i % 2 == 0 { "alpha".to_owned() } else { "beta".to_owned() });
    }
    println!("{}", values[0]);
    println!("{}", values[n - 1]);
    println!("{}", values.len());
}
