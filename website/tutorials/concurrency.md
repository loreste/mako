# Concurrency Patterns

This tutorial covers `crew` blocks, channels, `fan`, actors, `CMap`,
and cancellation. Jobs cannot outlive their `crew` block.

---

## Crew Blocks

A `crew` block scopes concurrent tasks. `t.kick(expr)` launches work;
`.join()` blocks until complete and returns the value.

```mko
fn compute(n: int) -> int { return n * n }

fn main() {
    crew t {
        let a = t.kick(compute(7))
        let b = t.kick(compute(9))
        print(a.join() + b.join())  // 130
    }
}
```

---

## Channels

Channels are typed, bounded queues for communicating between tasks.

```mko
fn main() {
    let ch = chan_new(4)  // buffered channel, capacity 4

    crew t {
        // Producer: send 5 values
        let p = t.kick(produce(ch))
        // Consumer: receive values
        let c = t.kick(consume(ch))
        let _ = p.join()
        print(c.join())
    }
}

fn produce(ch: chan[int]) -> int {
    for i in 5 {
        let _ = ch.send(i + 1)
    }
    ch.close()
    return 5
}

fn consume(ch: chan[int]) -> int {
    let mut sum = 0
    for v in range ch {
        sum = sum + v
    }
    return sum
}
```

`chan_new(cap)` creates a channel. `ch.send(v)` sends, `ch.recv()`
receives, and `ch.close()` signals no more values. Use `for v in range ch`
to drain until close.

---

## Channel Pipelines

Chain stages through channels: each stage reads from an input channel,
transforms values, and writes to an output channel.

```mko
fn generate(out: chan[int], n: int) -> int {
    for i in n { let _ = out.send(i + 1) }
    out.close()
    return n
}

fn double(input: chan[int], out: chan[int]) -> int {
    for v in range input { let _ = out.send(v * 2) }
    out.close()
    return 0
}

fn collect(input: chan[int]) -> int {
    let mut sum = 0
    for v in range input { sum = sum + v }
    return sum
}

fn main() {
    let s1 = chan_new(8)
    let s2 = chan_new(8)
    crew t {
        let _ = t.kick(generate(s1, 5))
        let _ = t.kick(double(s1, s2))
        let c = t.kick(collect(s2))
        print(c.join())  // 2+4+6+8+10 = 30
    }
}
```

---

## Select: Multiplexing Channels

`select` waits on multiple channels. The first with data wins.

```mko
fn sender(ch: chan[int], v: int, delay: int) -> int {
    sleep_ms(delay)
    let _ = ch.send(v)
    return 0
}

fn main() {
    let a = chan_new(2)
    let b = chan_new(2)
    crew t {
        let _ = t.kick(sender(a, 10, 50))
        let _ = t.kick(sender(b, 20, 20))
        select timeout 500 {
            a => { print("got a") print(chan_select_value()) }
            b => { print("got b") print(chan_select_value()) }
            default => { print("nothing ready") }
        }
    }
}
```

Programmatic: `chan_select2(a, b, 500)` returns 0/1/-1 (timeout).
Use `chan_select_value()` to get the value. Also `chan_select3`/`chan_select4`.

---

## Fan-Out / Fan-In

`fan` applies a function across a slice in parallel, distributing work
across cores and collecting results.

```mko
fn main() {
    let data = [1, 2, 3, 4, 5, 6, 7, 8]
    let squared = fan(data, fn(x) { x * x })
    for v in squared {
        print(v)
    }
}
```

For more control, fan out manually by kicking multiple workers that
read from a shared `jobs` channel and write results to a shared
`results` channel. Close `jobs` after sending all work so workers
exit their `for v in range jobs` loops.

---

## Actor-Based Session Management

Actors encapsulate state behind a mailbox. Each `receive` block handles
a message type. The runtime desugars actors into channels and a crew
loop.

```mko
actor Session {
    receive Invite {
        print("invite received")
    }
    receive Timer {
        print("timer tick")
    }
    receive Bye {
        print("session ending")
    }
}

fn main() {
    let session = Session_spawn()

    crew t {
        let loopj = t.kick(Session_loop(session))

        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Bye())

        print(loopj.join())
    }
}
```

`Session_spawn()` creates the actor. `Session_loop` runs until `Bye`.

---

## Shared State with CMap

`CMap` is a lock-free concurrent hashmap. Multiple crew tasks can read
and write without channels or mutexes.

```mko
fn writer(store: int, id: int) -> int {
    cmap_set(store, "worker-" + string(id), string(id * 100))
    return id
}

fn main() {
    let store = cmap_new()
    crew t {
        let a = t.kick(writer(store, 1))
        let b = t.kick(writer(store, 2))
        let _ = a.join()
        let _ = b.join()
    }
    print(cmap_len(store))
    print(cmap_get(store, "worker-1"))
}
```

`cmap_incr(m, key, delta)` provides atomic increment for shared counters.

---

## Cancel and Timeout

`t.cancel()` stops scheduling new work. `t.cancelled()` checks status.

```mko
fn work(n: int) -> int { return n * n }

fn main() {
    crew t {
        let a = t.kick(work(3))
        print(a.join())
        t.cancel()
        if t.cancelled() { print("crew cancelled") }
    }
}
```

For channel timeouts, use `select timeout`:

```mko
let ch = chan_new(1)
select timeout 100 {
    ch => { print(chan_select_value()) }
    _ => { print("timed out") }
}
```

---

## Worker Pool Pattern

Combine channels and crew to build a bounded worker pool. Kick N
workers that read from a shared `jobs` channel and write to a `done`
channel. Close `jobs` after enqueueing, then drain `done`.

```mko
fn process(id: int, jobs: chan[int], done: chan[int]) -> int {
    for task in range jobs {
        let _ = done.send(task * task)
    }
    return id
}

fn main() {
    let jobs = chan_new(32)
    let done = chan_new(32)

    crew t {
        let mut w = 0
        while w < 4 {
            let _ = t.kick(process(w, jobs, done))
            w = w + 1
        }
        let mut i = 0
        while i < 20 {
            let _ = jobs.send(i + 1)
            i = i + 1
        }
        jobs.close()
        let mut sum = 0
        let mut got = 0
        while got < 20 {
            sum = sum + done.recv()
            got = got + 1
        }
        print(sum)
    }
}
```

---

## Crew Drain

When a crew block has many outstanding jobs and you want to wait for all of
them with a timeout, use `crew_drain`. This avoids manually joining each job
handle.

```mko
fn long_task(id: int) -> int {
    sleep_ms(500)
    return id
}

fn main() {
    crew t {
        let _ = t.kick(long_task(1))
        let _ = t.kick(long_task(2))
        let _ = t.kick(long_task(3))
        // Wait up to 5 seconds for all jobs to finish
        let status = crew_drain(5000)
        if status == 0 {
            print("all jobs completed")
        } else {
            print("drain timed out")
        }
    }
}
```

`crew_drain(timeout_ms)` returns 0 when all jobs finish before the timeout,
or 1 if the timeout elapsed with jobs still running.

---

## Checked Arithmetic

For numeric operations where overflow must not go undetected, use the
`checked_*` family. They return `Result[int, string]` so overflow is handled
explicitly rather than wrapping silently.

```mko
fn safe_total(prices: []int) -> Result[int, string] {
    let mut total = 0
    for _, p in range prices {
        match checked_add(total, p) {
            Ok(sum) => { total = sum }
            Err(e) => { return error("total overflow: " + e) }
        }
    }
    return Ok(total)
}

fn main() {
    let prices = [1000000000, 2000000000, 3000000000]
    match safe_total(prices) {
        Ok(t) => print_int(t),
        Err(e) => print(e),
    }

    // Pre-check before computing
    let x = 9000000000000000000
    let y = 9000000000000000000
    if would_overflow_mul(x, y) == 1 {
        print("multiplication would overflow")
    }
}
```

| Function | Purpose |
|----------|---------|
| `checked_add(a, b)` | Add, returns Err on overflow |
| `checked_sub(a, b)` | Subtract, returns Err on overflow |
| `checked_mul(a, b)` | Multiply, returns Err on overflow |
| `would_overflow_add(a, b)` | 1 if addition would overflow |
| `would_overflow_sub(a, b)` | 1 if subtraction would overflow |
| `would_overflow_mul(a, b)` | 1 if multiplication would overflow |

---

## Key Takeaways

- `crew` blocks guarantee that no task outlives its scope
- `t.kick(expr)` launches work; `.join()` collects the result
- Channels (`chan_new`, `send`, `recv`, `close`) are the primary
  communication primitive
- `select timeout` multiplexes multiple channels with a deadline
- `fan(slice, fn(x) { ... })` provides one-line data parallelism
- Actors (`actor` / `receive`) encapsulate stateful message handling
- `CMap` gives lock-free shared state across tasks
- `t.cancel()` / `t.cancelled()` enable cooperative cancellation
