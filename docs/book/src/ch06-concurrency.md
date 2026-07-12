# 6. Concurrency: Crews, Channels, and Actors

Mako concurrency is **structured**. Every concurrent task lives inside a `crew`
block, and no task can outlive its crew. When the crew block ends, all kicked
tasks are joined automatically. This eliminates orphan threads, dangling
references, and the fire-and-forget bugs that plague concurrent systems.

This chapter covers:

- Crew blocks and task management (`crew`, `t.kick()`, `t.join()`)
- Cancellation (`t.cancel()`, `t.cancelled()`)
- Data-parallel fan
- Channels (`chan_new`, `send`, `recv`, `close`, `for v in range ch`)
- Channel select (`select`, `chan_select2`, `chan_select_value`)
- Actors (`actor`, `receive`, `Session_spawn`, `Session_send`, `Session_loop`)
- Practical concurrent patterns

---

## Crew Blocks

A `crew` block declares a structured concurrency scope. You name the crew handle
(conventionally `t` or `c`) and use it to kick off concurrent tasks:

```mko
fn work(n: int) -> int {
    return n * n
}

fn main() {
    crew t {
        let a = t.kick(work(7))
        let b = t.kick(work(9))
        let x = a.join()
        let y = b.join()
        print_int(x + y)   // 49 + 81 = 130
    }
}
```

### Key rules

1. **`t.kick(expr)`** spawns a new concurrent task that evaluates `expr`. It
   returns a join handle immediately.
2. **`handle.join()`** blocks the caller until the kicked task completes and
   returns its result value.
3. **Automatic join on scope exit**: when the closing `}` of the crew block is
   reached, any un-joined tasks are joined implicitly. No task escapes.
4. **One crew at a time per scope**: you cannot nest crews in the same function
   without giving them different names.

### Why structured?

Because tasks cannot outlive their crew, the compiler can guarantee that any
reference passed into a kicked task remains valid for the task's entire lifetime.
This is what makes `hold` and `share` ownership work correctly across threads.

### `go f()` — fire-and-forget

When you don't need the join handle, `go f()` schedules a call onto the innermost
enclosing crew — the same as `t.kick(f())` with the result discarded. The crew
still joins it at scope exit, so there are no orphan tasks:

```mko
fn main() {
    crew t {
        go log_metrics()      // == t.kick(log_metrics())
        go flush_cache()
        // both are joined here, at the end of the crew
    }
}
```

Unlike Go's `go`, this is not a detached goroutine: `go` outside any `crew` is a
compile error, because Mako never lets a task escape its scope.

---

## t.kick() In Detail

`t.kick()` accepts any expression that returns a value. The expression becomes
the body of a new thread:

```mko
fn fetch_user(id: int) -> string {
    // ... network call ...
    return "user_" + string(id)
}

fn fetch_score(id: int) -> int {
    // ... database call ...
    return id * 10
}

fn main() {
    crew t {
        let user_handle = t.kick(fetch_user(42))
        let score_handle = t.kick(fetch_score(42))

        // Both tasks run concurrently. Join them:
        let user = user_handle.join()
        let score = score_handle.join()

        print(user)
        print_int(score)
    }
}
```

Each kicked task runs on its own OS thread. The runtime tracks spawned and joined
counts for observability (accessible via `runtime_stats_json()`).

---

## t.join()

Calling `.join()` on a kick handle does two things:

1. Blocks the calling thread until the task finishes.
2. Returns the task's result value.

You can join tasks in any order. Join the fastest-completing task first to keep
the pipeline flowing:

```mko
fn slow_work() -> int {
    sleep_ms(100)
    return 1
}

fn fast_work() -> int {
    sleep_ms(10)
    return 2
}

fn main() {
    crew t {
        let slow = t.kick(slow_work())
        let fast = t.kick(fast_work())

        // Join fast first — we can use its result while slow is still running
        let f = fast.join()
        print_int(f)

        let s = slow.join()
        print_int(s)
    }
}
```

---

## Cancellation

### t.cancel()

Calling `t.cancel()` on a crew handle signals that no new work should be
scheduled. Tasks already kicked continue to run, but subsequent `t.kick()` calls
return immediately with a zero/default value instead of spawning a thread:

```mko
fn work(n: int) -> int {
    return n * n
}

fn main() {
    crew t {
        let a = t.kick(work(3))
        print_int(a.join())     // 9

        t.cancel()

        // After cancel, new kicks do not start threads
        let b = t.kick(work(9))
        print_int(b.join())     // 0 (never executed)

        if t.cancelled() {
            print("crew cancelled")
        }
    }
}
```

### t.cancelled()

Returns `true` if `cancel()` has been called on this crew. Use it for
cooperative checking inside long-running tasks:

```mko
fn long_task(t_ref: crew_ref) -> int {
    let mut i = 0
    while i < 1000000 {
        if t_ref.cancelled() {
            return i    // early exit
        }
        i = i + 1
    }
    return i
}
```

### Cancellation semantics

- Cancel is **cooperative**, not preemptive. Running tasks are not killed mid-instruction.
- Cancel affects only the crew it is called on.
- On crew scope exit, all tasks (cancelled or not) are joined.

---

## Data-Parallel Fan

For simple map-over-a-collection parallelism, use `fan`. It kicks one task per
element, distributes across available cores, and collects results in order:

```mko
fn main() {
    let xs = [1, 2, 3, 4, 5, 6, 7, 8]
    let ys = fan(xs, |x| x * x)
    for v in ys {
        print_int(v)
    }
}
```

Output:
```
1
4
9
16
25
36
49
64
```

`fan` is syntactic sugar over a crew block with one kick per element. Use it when
the operation is uniform and order-preserving. For heterogeneous tasks or tasks
that need different scheduling, use explicit crew blocks.

---

## Channels

Channels are typed, bounded FIFO queues for communication between tasks.

### Creating a channel

```mko
let ch = chan_new(4)    // buffered channel with capacity 4
```

The argument is the buffer size. A capacity of 0 creates an unbuffered
(rendezvous) channel where `send` blocks until a receiver is ready.

### Sending and receiving

```mko
let _ = ch.send(42)    // blocks if buffer is full
let v = ch.recv()      // blocks if buffer is empty
```

Both `send` and `recv` block the calling thread when the buffer is at capacity
(for send) or empty (for recv).

### Closing a channel

```mko
ch.close()
```

After close:
- Further sends are no-ops (return immediately).
- Receivers drain any remaining buffered values, then receive the zero value.

### Range over a channel

Use `for v in range ch` to receive values until the channel is closed and
drained:

```mko
fn producer(ch: chan[int], n: int) -> int {
    for i in n {
        let _ = ch.send(i + 1)
    }
    ch.close()
    return n
}

fn main() {
    let ch = chan_new(4)
    crew t {
        let p = t.kick(producer(ch, 5))
        let mut sum = 0
        for v in range ch {
            sum = sum + v
        }
        let _ = p.join()
        print_int(sum)  // 1+2+3+4+5 = 15
    }
}
```

### Producer-consumer pattern

The classic pattern: one task produces, another consumes, connected by a channel:

```mko
fn producer(ch: chan[int], n: int) -> int {
    for i in n {
        let _ = ch.send(i + 1)
    }
    ch.close()
    return n
}

fn consumer(ch: chan[int]) -> int {
    let mut sum = 0
    for v in range ch {
        sum = sum + v
    }
    return sum
}

fn main() {
    let ch = chan_new(4)
    crew t {
        let p = t.kick(producer(ch, 5))
        let c = t.kick(consumer(ch))
        let _ = p.join()
        print_int(c.join())   // 15
    }
}
```

---

## Channel Select

When you need to wait on multiple channels simultaneously, use `select`:

### Select syntax

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
        let _ = t.kick(sender(a, 11, 40))
        let _ = t.kick(sender(b, 22, 15))

        select timeout 500 {
            a => {
                print("got a")
                print_int(chan_select_value())
            }
            b => {
                print("got b")
                print_int(chan_select_value())
            }
        }
    }
}
```

The `select` block waits until one of the listed channels has a value ready, then
executes the corresponding arm. `timeout` specifies the maximum milliseconds to
wait.

### chan_select_value()

After a select arm fires, call `chan_select_value()` to retrieve the value that
was received. This returns the integer value from whichever channel became ready.

### Default arm

If no channel is ready within the timeout, the `default` (or `_`) arm fires:

```mko
select timeout 30 {
    a => { print("got a") }
    b => { print("got b") }
    default => { print("nothing ready") }
}
```

### chan_select2 / chan_select3 / chan_select4

For programmatic use (outside the `select` syntax), call the builtin functions
directly:

```mko
// Returns which channel fired: 1 for first, 2 for second, 0 for timeout
let which = chan_select2(a, b, 500)
let value = chan_select_value()
print_int(which)
print_int(value)
```

`chan_select3` takes three channels, `chan_select4` takes four. All accept a
timeout in milliseconds as the last argument.

### Fairness

When multiple channels are ready simultaneously, selection uses round-robin
ordering. This prevents starvation of any single channel.

### Multi-channel drain example

```mko
fn sender(ch: chan[int], v: int, delay: int) -> int {
    sleep_ms(delay)
    let _ = ch.send(v)
    return 0
}

fn main() {
    let a = chan_new(2)
    let b = chan_new(2)
    let c = chan_new(2)
    let d = chan_new(2)

    crew t {
        let _ = t.kick(sender(a, 11, 50))
        let _ = t.kick(sender(b, 22, 20))
        let _ = t.kick(sender(c, 33, 35))
        let _ = t.kick(sender(d, 44, 10))

        // Drain all four with repeated select
        let w1 = chan_select4(a, b, c, d, 500)
        print_int(w1)
        print_int(chan_select_value())

        let w2 = chan_select4(a, b, c, d, 500)
        print_int(w2)
        print_int(chan_select_value())

        let w3 = chan_select4(a, b, c, d, 500)
        print_int(w3)
        print_int(chan_select_value())

        let w4 = chan_select4(a, b, c, d, 500)
        print_int(w4)
        print_int(chan_select_value())
    }
}
```

---

## Actors

Actors provide a higher-level concurrency pattern: a mailbox-driven event loop
with typed messages. They desugar to a channel (the mailbox) plus a crew loop.

### Defining an actor

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
```

This declaration generates:

- **Message tags**: `Session_Invite()`, `Session_Timer()`, `Session_Bye()`
- **Spawn**: `Session_spawn()` creates a new actor instance (allocates mailbox)
- **Send**: `Session_send(actor, message)` posts a message to the mailbox
- **Loop**: `Session_loop(actor)` runs the receive loop (blocking, processes messages in order)

### Using an actor

```mko
fn main() {
    let session = Session_spawn()
    crew t {
        let loopj = t.kick(Session_loop(session))
        let _ = Session_send(session, Session_Invite())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Timer())
        let _ = Session_send(session, Session_Bye())
        print_int(loopj.join())
    }
}
```

Output:
```
invite received
tick
tick
goodbye
0
```

### Termination convention

By convention, a `Bye` or `Stop` message type ends the actor loop. The loop
function returns 0 when it receives the termination message. Without a
termination message, the actor loop runs indefinitely (or until the crew is
cancelled).

### Actor design patterns

**State machine actor**: use the receive handlers to transition between states.
Each message type represents an event in the state machine.

```mko
actor Connection {
    receive Connect {
        print("connected")
    }
    receive Data {
        print("processing data")
    }
    receive Disconnect {
        print("disconnected")
    }
}

fn main() {
    let conn = Connection_spawn()
    crew t {
        let loop_handle = t.kick(Connection_loop(conn))
        let _ = Connection_send(conn, Connection_Connect())
        let _ = Connection_send(conn, Connection_Data())
        let _ = Connection_send(conn, Connection_Data())
        let _ = Connection_send(conn, Connection_Disconnect())
        print_int(loop_handle.join())
    }
}
```

**Supervision pattern**: spawn multiple actors under one crew. If one actor needs
to notify another, send a message to the other actor's mailbox:

```mko
actor Worker {
    receive Job {
        print("working")
    }
    receive Stop {
        print("worker stopping")
    }
}

actor Supervisor {
    receive Start {
        print("supervisor started")
    }
    receive Stop {
        print("supervisor stopping")
    }
}

fn main() {
    let sup = Supervisor_spawn()
    let w1 = Worker_spawn()
    let w2 = Worker_spawn()

    crew t {
        let sl = t.kick(Supervisor_loop(sup))
        let wl1 = t.kick(Worker_loop(w1))
        let wl2 = t.kick(Worker_loop(w2))

        let _ = Supervisor_send(sup, Supervisor_Start())
        let _ = Worker_send(w1, Worker_Job())
        let _ = Worker_send(w2, Worker_Job())
        let _ = Worker_send(w1, Worker_Stop())
        let _ = Worker_send(w2, Worker_Stop())
        let _ = Supervisor_send(sup, Supervisor_Stop())

        let _ = wl1.join()
        let _ = wl2.join()
        let _ = sl.join()
    }
}
```

---

## Practical Concurrent Patterns

### Pipeline (chain of channels)

```mko
fn stage1(input: chan[int], output: chan[int]) -> int {
    for v in range input {
        let _ = output.send(v * 2)
    }
    output.close()
    return 0
}

fn stage2(input: chan[int], output: chan[int]) -> int {
    for v in range input {
        let _ = output.send(v + 1)
    }
    output.close()
    return 0
}

fn main() {
    let ch1 = chan_new(4)
    let ch2 = chan_new(4)
    let ch3 = chan_new(4)

    crew t {
        let s1 = t.kick(stage1(ch1, ch2))
        let s2 = t.kick(stage2(ch2, ch3))

        // Feed the pipeline
        let _ = ch1.send(1)
        let _ = ch1.send(2)
        let _ = ch1.send(3)
        ch1.close()

        // Collect results: (1*2)+1=3, (2*2)+1=5, (3*2)+1=7
        for v in range ch3 {
            print_int(v)
        }

        let _ = s1.join()
        let _ = s2.join()
    }
}
```

### Fan-out / fan-in

Distribute work across N workers, collect results through a single channel:

```mko
fn worker(id: int, jobs: chan[int], results: chan[int]) -> int {
    for job in range jobs {
        let _ = results.send(job * job)
    }
    return 0
}

fn main() {
    let jobs = chan_new(10)
    let results = chan_new(10)

    crew t {
        // Fan out: 3 workers
        let w1 = t.kick(worker(1, jobs, results))
        let w2 = t.kick(worker(2, jobs, results))
        let w3 = t.kick(worker(3, jobs, results))

        // Send work
        for i in 9 {
            let _ = jobs.send(i + 1)
        }
        jobs.close()

        // Wait for workers, then close results
        let _ = w1.join()
        let _ = w2.join()
        let _ = w3.join()
        results.close()

        // Fan in: collect all results
        let mut total = 0
        for v in range results {
            total = total + v
        }
        print_int(total)
    }
}
```

### Timeout pattern

Use select with a timeout to implement deadline-aware operations:

```mko
fn slow_operation(ch: chan[int]) -> int {
    sleep_ms(200)
    let _ = ch.send(42)
    return 0
}

fn main() {
    let ch = chan_new(1)
    crew t {
        let _ = t.kick(slow_operation(ch))

        select timeout 50 {
            ch => {
                print("got result")
                print_int(chan_select_value())
            }
            default => {
                print("timed out")
            }
        }
    }
}
```

---

## Concurrent Maps (CMap)

When multiple crew tasks need to share mutable key-value state, `CMap` is the
built-in solution. It is a concurrent hashmap with lock-free reads and
per-stripe spinlock writes (512 stripes, FNV-1a hash, 1M initial capacity).

Unlike regular maps, a `CMap` can be read and written from any number of crew
tasks simultaneously without channels, mutexes, or `hold`/`share` annotations:

```mko
fn worker(m: CMap, id: int) -> int {
    let key = "worker_" + string(id)
    cmap_set(m, key, "done")
    let _ = cmap_incr(m, "total", 1)
    return 0
}

fn main() {
    let m = cmap_new()
    cmap_set(m, "total", "0")

    crew t {
        let w1 = t.kick(worker(m, 1))
        let w2 = t.kick(worker(m, 2))
        let w3 = t.kick(worker(m, 3))
        let _ = w1.join()
        let _ = w2.join()
        let _ = w3.join()
    }

    print_int(cmap_len(m))             // 4 (total + 3 worker keys)
    print(cmap_get(m, "worker_1"))     // "done"
    print_int(cmap_incr(m, "total", 0)) // 3 (read current value)
}
```

### CMap API

| Function | Purpose |
|----------|---------|
| `cmap_new()` | Create a new concurrent map |
| `cmap_set(m, key, value)` | Set a key-value pair |
| `cmap_get(m, key)` | Get value (`""` if missing) |
| `cmap_has(m, key)` | Check if key exists (1 or 0) |
| `cmap_del(m, key)` | Delete key (returns 1 if existed) |
| `cmap_len(m)` | Number of entries |
| `cmap_incr(m, key, delta)` | Atomic increment, returns new value |

### When to use CMap vs Channels

Use **CMap** when you need shared mutable state with random-access reads and
writes -- counters, caches, lookup tables, result aggregation.

Use **channels** when you need ordered message passing, pipeline stages, or
producer-consumer coordination.

CMap has no ordering guarantees between operations from different tasks. If you
need "set A then read A" ordering across tasks, coordinate with channels or
join handles.

### Shared synchronization handles

`CMap` is not the only value you can hand to a kicked task. The thread-safe
handles — `CMap`, `Mutex`, `RWMutex`, and `AtomicInt` — are all **sendable**:
passing one into `kick` shares the same underlying object, so tasks coordinate
through it directly. Use `AtomicInt` for lock-free counters, `Mutex` / `RWMutex`
to guard a critical section, and `CMap` for shared key-value state.

```mko
fn add_worker(c: AtomicInt, n: int) -> int {
    var i = 0
    for i < n { let _ = atomic_add(c, 1); i++ }
    return 0
}

fn main() {
    let total = atomic_new(0)
    crew t {
        let a = t.kick(add_worker(total, 1000))
        let b = t.kick(add_worker(total, 1000))
        let _ = a.join()
        let _ = b.join()
    }
    print(atomic_load(total))   // 2000 — race-free
}
```

Ordinary structs, arrays, and arenas are **not** sendable — the compiler rejects
kicking them; pass their contents through a channel or a shared handle instead.

---

## Event Loop Integration

For high-concurrency servers that need to handle thousands of connections, Mako
provides an event loop that multiplexes non-blocking I/O across many file
descriptors without spawning a thread per connection:

```mko
fn main() {
    let el = evloop_new()
    let server_fd = nb_listen(8080)
    let _ = evloop_add(el, server_fd, 1)

    let mut running = true
    while running {
        let n = evloop_wait(el, 100)
        let mut i = 0
        while i < n {
            let fd = evloop_event_fd(el, i)
            if fd == server_fd {
                let client = nb_accept(server_fd)
                let _ = evloop_add(el, client, 1)
            } else {
                let data = nb_read(fd)
                let _ = nb_write(fd, "hello\n")
                let _ = evloop_del(el, fd)
                let _ = nb_close(fd)
            }
            i = i + 1
        }
    }
    let _ = evloop_close(el)
}
```

The event loop uses epoll on Linux and kqueue on macOS. It integrates naturally
with crew blocks -- you can run the event loop in one kicked task while other
tasks handle computation. The `evloop_wait` call returns the number of ready
events; iterate them with `evloop_event_fd` and `evloop_event_flags`.

| Function | Purpose |
|----------|---------|
| `evloop_new()` | Create event loop instance |
| `evloop_add(el, fd, flags)` | Register fd for monitoring |
| `evloop_mod(el, fd, flags)` | Update interest flags |
| `evloop_del(el, fd)` | Stop monitoring fd |
| `evloop_wait(el, timeout_ms)` | Block until events ready, returns count |
| `evloop_event_fd(el, i)` | Get fd at index i |
| `evloop_event_flags(el, i)` | Get event flags at index i |
| `evloop_close(el)` | Destroy event loop |

---

## Colorless I/O

Mako uses **colorless** concurrency: there is no `async`/`await` distinction.
All functions have the same calling convention regardless of whether they perform
I/O. Instead of coloring functions, you use crews to run blocking I/O operations
concurrently:

```mko
fn fetch_data(url: string, ch: chan[string]) -> int {
    let body = http_get(url)
    let _ = ch.send(body)
    return 0
}

fn main() {
    let ch = chan_new(2)
    crew t {
        let _ = t.kick(fetch_data("/api/users", ch))
        let _ = t.kick(fetch_data("/api/posts", ch))

        let r1 = ch.recv()
        let r2 = ch.recv()
        print(r1)
        print(r2)
    }
}
```

This means every function in your codebase composes the same way. No function
signatures change when you add concurrency. No viral annotations propagate
through your call graph.

---

## Runtime Observability

The runtime tracks concurrency metrics automatically:

- `tasks_spawned` / `tasks_joined` counts
- `channels_created` / `channel_sends` / `channel_recvs` counts
- `channel_select_timeouts` count
- `channel_peak_depth` (high-water mark)

Access them via `runtime_stats_json()` for diagnostics:

```mko
fn main() {
    let ch = chan_new(4)
    crew t {
        let _ = ch.send(1)
        let _ = ch.send(2)
        let _ = ch.recv()
    }
    let stats = runtime_stats_json()
    print(stats)
}
```

---

## Summary

| Primitive | Purpose |
|-----------|---------|
| `crew t { }` | Structured scope — tasks cannot escape |
| `t.kick(expr)` | Spawn concurrent task, get join handle |
| `handle.join()` | Block until task completes, get result |
| `t.cancel()` | Signal no more new work |
| `t.cancelled()` | Check if cancel was signaled |
| `fan(xs, fn)` | Data-parallel map across cores |
| `chan_new(cap)` | Create bounded channel |
| `ch.send(v)` | Send value (blocks if full) |
| `ch.recv()` | Receive value (blocks if empty) |
| `ch.close()` | Close channel |
| `for v in range ch` | Drain channel until closed |
| `select timeout N { }` | Multiplex across channels |
| `chan_select2/3/4(...)` | Programmatic multi-channel wait |
| `chan_select_value()` | Get value from last select |
| `actor Name { }` | Declare actor with typed messages |
| `Name_spawn()` | Create actor instance |
| `Name_send(a, msg)` | Post message to actor |
| `Name_loop(a)` | Run actor receive loop |

Next: [Standard Library](ch07-stdlib.md).
