# Concurrency

Mako uses structured concurrency: all parallel work lives inside `crew` blocks,
and every job is guaranteed to complete before the block exits. No leaked
threads, no forgotten futures.

## Crew blocks

A `crew` spawns jobs with `kick` and collects results with `join`:

```mko
fn compute(n: int) -> int {
    return n * n
}

fn main() {
    crew t {
        let a = t.kick(compute(7))
        let b = t.kick(compute(9))
        print_int(a.join())   // 49
        print_int(b.join())   // 81
    }
    // All jobs finished here -- guaranteed
}
```

Jobs cannot escape their crew. When the block ends, all kicked work has joined.

## Channels

Communicate between jobs using typed channels. Element types: int family, bool,
float, string, and **named structs** (`chan_open[Point](n)` / `make(chan[Point], n)`).

```mko
fn producer(ch: chan[int], count: int) -> int {
    for i in range count {
        let _ = ch.send(i + 1)
    }
    ch.close()
    return count
}

fn consumer(ch: chan[int]) -> int {
    let mut sum = 0
    for v in range ch {
        sum = sum + v
    }
    return sum
}

fn main() {
    let ch = chan_new(4)          // buffered channel, capacity 4
    crew t {
        let p = t.kick(producer(ch, 5))
        let c = t.kick(consumer(ch))
        let _ = p.join()
        print_int(c.join())      // 15
    }
}
```

### Struct results (no int bit-packing)

Prefer a POD struct on a channel when a worker returns several fields:

```mko
struct Done {
    err: int
    status: int
    bytes: int
}

fn worker(out: chan[Done]) -> int {
    let _ = out.send(Done { err: 0, status: 200, bytes: 42 })
    return 0
}

fn main() {
    let ch = chan_open[Done](4)
    crew t {
        let j = t.kick(worker(ch))
        let d = ch.recv()
        let _ = j.join()
        print_int(d.status)
    }
}
```

Deep-POD structs (scalar/string fields only) may also cross **`kick` as args**.
Maps, arrays, and non-POD structs cannot — use channels. Details:
[SPEED.md](../SPEED.md) · [ERGONOMICS.md](../ERGONOMICS.md).

Channel operations:

| Operation | Meaning |
|-----------|---------|
| `chan_new(cap)` / `chan_open[T](cap)` / `make(chan[T], cap)` | Create buffered channel |
| `ch.send(val)` | Send a value (blocks if full) |
| `ch.recv()` | Receive a value (blocks if empty) |
| `ch.close()` | Signal no more sends |
| `for v in range ch` | Receive until closed |

## Select

Wait on multiple channels, with timeout and default arms:

```mko
fn main() {
    let a = chan_new(2)
    let b = chan_new(2)

    crew t {
        let _ = t.kick(sender(a, 11))
        let _ = t.kick(sender(b, 22))

        select timeout 500 {
            a => {
                print("got from a")
                print_int(chan_select_value())
            }
            b => {
                print("got from b")
                print_int(chan_select_value())
            }
            default => {
                print("nothing ready")
            }
        }
    }
}

fn sender(ch: chan[int], val: int) -> int {
    sleep_ms(30)
    let _ = ch.send(val)
    return 0
}
```

The `timeout` value is in milliseconds. Use `default` for a non-blocking poll.
Up to 16 channel arms are supported. Fairness is round-robin when multiple
channels are ready simultaneously.

Helper functions for programmatic select:

```mko
let which = chan_select2(a, b, 500)        // returns 0 or 1 (-1 on timeout)
let which = chan_select4(a, b, c, d, 500)  // returns 0..3 (-1 on timeout)
let val = chan_select_value()              // value from whichever fired
```

## Fan (parallel map)

Apply a function to every element in parallel:

```mko
fn main() {
    let xs = [1, 2, 3, 4, 5]
    let squares = fan(xs, |x| x * x)
    for v in squares {
        print_int(v)
    }
}
```

`fan` spawns one job per element and returns results in order.

## Cancel

Cooperatively cancel remaining work in a crew:

```mko
fn main() {
    crew t {
        let a = t.kick(work(3))
        print_int(a.join())

        t.cancel()

        // After cancel, new kicks do not start threads
        let b = t.kick(work(9))
        print_int(b.join())

        if t.cancelled() {
            print("crew cancelled")
        }
    }
}

fn work(n: int) -> int {
    return n * n
}
```

After `t.cancel()`, subsequent `t.kick(...)` calls still return a handle but
the work may not actually execute on a new thread.

## Actors

For message-passing concurrency, define an actor with receive handlers:

```mko
actor Session {
    receive Invite {
        print("invite received")
    }
    receive Timer {
        print("tick")
    }
    receive Bye {
        print("goodbye")
    }
}

fn main() {
    let session = Session_spawn()
    crew t {
        let loopj = t.kick(Session_loop(session))
        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Bye())
        print_int(loopj.join())
    }
}
```

An actor desugars to a mailbox channel and a crew loop. Send `Bye` (or `Stop`)
to end the loop by convention.

Generated functions for an actor named `Foo`:

| Function | Purpose |
|----------|---------|
| `Foo_spawn()` | Create mailbox, return handle |
| `Foo_send(handle, msg)` | Enqueue a message |
| `Foo_loop(handle)` | Process messages until stop |
| `Foo_Invite()` | Construct a message tag |

## Practical pattern: worker pool

```mko
fn worker(id: int, ch: chan[int]) -> int {
    let mut done = 0
    for task in range ch {
        // process task
        done = done + 1
    }
    return done
}

fn main() {
    let ch = chan_new(16)
    crew t {
        // Start 4 workers
        let w1 = t.kick(worker(1, ch))
        let w2 = t.kick(worker(2, ch))
        let w3 = t.kick(worker(3, ch))
        let w4 = t.kick(worker(4, ch))

        // Feed work
        for i in range 100 {
            let _ = ch.send(i)
        }
        ch.close()

        // Collect results
        let total = w1.join() + w2.join() + w3.join() + w4.join()
        print_int(total)   // 100
    }
}
```

## Rules to remember

1. All concurrency lives inside `crew` blocks -- no global spawning
2. Every `kick` must eventually `join` (enforced by scope exit)
3. Channels are typed -- `chan[int]` only carries `int` values
4. `select` timeout is in milliseconds; 0 means poll once
5. No shared mutable state -- communicate through channels or actors

## Next steps

- [Memory management](06-memory.md)
- [Testing concurrent code](08-testing.md)
