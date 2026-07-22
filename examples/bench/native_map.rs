use std::collections::HashMap;

fn map_checksum(n: i64) -> i64 {
    let mut m = HashMap::with_capacity(n as usize);
    for i in 0..n {
        m.insert(i, i * 2);
    }
    let mut sum = 0i64;
    for i in 0..n {
        sum += m.get(&i).copied().unwrap_or(0);
    }
    sum
}

fn main() {
    println!("{}", map_checksum(1_000_000));
}
