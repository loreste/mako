use std::fs;
use std::io::{Read, Write};

fn io_checksum(rounds: i64, chunk: usize) -> i64 {
    let path = "/tmp/mako_native_io_bench.dat";
    let payload = vec![b'x'; chunk];
    let mut acc = 0i64;
    for _ in 0..rounds {
        {
            let mut f = fs::File::create(path).expect("create");
            f.write_all(&payload).expect("write");
        }
        // Match Mako write_file success code 0 in the accumulator.
        acc += 0;
        let mut buf = vec![0u8; chunk];
        {
            let mut f = fs::File::open(path).expect("open");
            let n = f.read(&mut buf).expect("read");
            acc += n as i64;
        }
    }
    acc
}

fn main() {
    println!("{}", io_checksum(50, 4096));
}
