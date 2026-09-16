// Same work, capacities, producers, and shard topology as actor_scaling.mko.
use std::sync::mpsc::sync_channel;
use std::thread;
use std::time::Instant;

fn shard(producers: usize, capacity: usize, count: i64) -> i64 {
    let (tx, rx) = sync_channel::<i64>(capacity);
    thread::scope(|scope| {
        let actor = scope.spawn(move || {
            let mut sum = 0;
            while let Ok(value) = rx.recv() {
                if value == 0 { break; }
                sum += value;
            }
            sum
        });
        thread::scope(|writers| {
            for _ in 0..producers {
                let tx = tx.clone();
                writers.spawn(move || {
                    for _ in 0..count / producers as i64 { tx.send(1).unwrap(); }
                });
            }
        });
        tx.send(0).unwrap();
        actor.join().unwrap()
    })
}

fn measure(producers: usize, capacity: usize, shards: usize) {
    let start = Instant::now();
    let (tx, rx) = sync_channel(shards);
    let sum: i64 = thread::scope(|scope| {
        for _ in 0..shards {
            let tx = tx.clone();
            scope.spawn(move || tx.send(shard(producers, capacity, 200_000 / shards as i64)).unwrap());
        }
        (0..shards).map(|_| rx.recv().unwrap()).sum()
    });
    assert_eq!(sum, 200_000);
    println!("actor_scaling\n{producers}\n{capacity}\n{shards}\n{}", start.elapsed().as_nanos());
}

fn main() {
    for capacity in [64, 1024] {
        for producers in [1, 2, 4, 8] { measure(producers, capacity, 1); }
    }
    for shards in [2, 4, 8] { measure(1, 1024, shards); }
}
