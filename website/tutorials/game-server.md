# Game Server Networking

This tutorial covers UDP sockets, peer tracking, broadcast, tick-rate
control, and binary protocol encoding for multiplayer game servers.

---

## Binding a UDP Socket

`game_udp_bind` creates a non-blocking UDP socket bound to a port. It
returns a handle used by all subsequent game networking calls.

```mko
fn main() {
    let u = game_udp_bind(27015)
    if u < 0 {
        print("bind failed")
        return
    }
    print("game server on :27015")

    // Main loop here...

    game_udp_close(u)
}
```

---

## Receiving and Sending Packets

`game_udp_recv` reads the next packet and records the sender peer.
`game_udp_sender` returns the peer ID of whoever sent the last packet.
`game_udp_send` sends data to a specific peer.

```mko
let data = game_udp_recv(u)
if data != "" {
    let peer = game_udp_sender(u)
    print("received from peer:")
    print(peer)
    print(data)

    // Echo back to sender
    let _ = game_udp_send(u, peer, "ack:" + data)
}
```

---

## Peer Tracking and Broadcast

The runtime automatically tracks peers as they send packets. You can
broadcast to all connected peers, check the peer count, or kick a
specific peer.

```mko
// Send a message to every connected peer
let _ = game_udp_broadcast(u, "server:hello-all")

// Check how many peers are connected
let count = game_udp_peers(u)
print("connected peers:")
print(count)

// Kick a misbehaving peer
game_udp_kick(u, bad_peer_id)
```

---

## Fixed Tick Rate

Games run at a fixed tick rate (e.g. 60 ticks per second = 16,667
microseconds per tick). Use `tick_now_us` and `tick_sleep_us` to
maintain consistent timing.

```mko
fn game_loop(u: int) {
    let tick_interval = 16667  // ~60 Hz in microseconds

    while 1 == 1 {
        let start = tick_now_us()

        // Process all pending packets this tick
        let mut processed = 0
        while processed < 64 {
            let data = game_udp_recv(u)
            if data == "" {
                break
            }
            let peer = game_udp_sender(u)
            handle_packet(u, peer, data)
            processed = processed + 1
        }

        // Send state updates
        if game_udp_peers(u) > 0 {
            let _ = game_udp_broadcast(u, "tick")
        }

        // Sleep for the remainder of the tick
        tick_sleep_us(start, tick_interval)
    }
}

fn handle_packet(u: int, peer: int, data: string) {
    print("peer:")
    print(peer)
    print(data)
    let _ = game_udp_send(u, peer, "ack:" + data)
}
```

---

## Binary Protocol with Buf

For real game protocols, use the `Buf` type to pack and parse binary
messages. This avoids string parsing overhead and gives precise control
over the wire format.

### Packing a Message

You can organize protocol helpers using `on` methods on a struct:

```mko
struct GamePacket {
    msg_type: int
    seq: int
}

on GamePacket {
    fn is_move(self) -> bool { return self.msg_type == 2 }
    fn is_leave(self) -> bool { return self.msg_type == 4 }
}

fn pack_move(seq: int, x: int, y: int) -> string {
    let b = buf_pack_new(64)
    buf_write_u8(b, 2)            // msg_type = move
    buf_write_u32(b, seq)         // sequence number
    buf_write_u16(b, 8)           // payload: two i32 values = 8 bytes
    buf_write_i32(b, x)           // x position
    buf_write_i32(b, y)           // y position
    let wire = buf_to_string(b)
    buf_free(b)
    return wire
}
```

### Parsing a Message

```mko
fn parse_message(wire: string) -> int {
    let b = buf_from_string(wire)
    let msg_type = buf_read_u8(b)
    let seq = buf_read_u32(b)
    let payload_len = buf_read_u16(b)

    if msg_type == 1 {
        print("join")
    } else {
        if msg_type == 2 {
            let x = buf_read_i32(b)
            let y = buf_read_i32(b)
            print("move to:")
            print(x)
            print(y)
        } else {
            if msg_type == 3 {
                let text = buf_read_str(b, payload_len)
                print("chat:")
                print(text)
            } else {
                if msg_type == 4 {
                    print("leave")
                }
            }
        }
    }
    buf_free(b)
    return msg_type
}
```

---

## Event Loop Integration

Integrate with the event loop to avoid busy-waiting. Use `game_udp_fd`
to get the raw file descriptor for `evloop_add`.

```mko
let u = game_udp_bind(27015)
let el = evloop_new()
let _ = evloop_add(el, game_udp_fd(u), 1)  // 1 = readable

while 1 == 1 {
    let n = evloop_wait(el, 16)  // ~60 Hz
    if n > 0 {
        let data = game_udp_recv(u)
        if data != "" {
            let peer = game_udp_sender(u)
            let _ = game_udp_send(u, peer, "echo:" + data)
        }
    }
}
```

---

## Complete Multiplayer Echo Server

This full example combines UDP binding, peer tracking, a fixed tick
loop, binary protocol handling, and broadcast into a single runnable
server.

```mko
fn pack_state(tick: int, peers: int) -> string {
    let b = buf_pack_new(32)
    buf_write_u8(b, 255)          // msg_type = state update
    buf_write_u32(b, tick)        // current tick
    buf_write_u16(b, peers)       // connected peer count
    let wire = buf_to_string(b)
    buf_free(b)
    return wire
}

fn main() {
    let u = game_udp_bind(27015)
    if u < 0 {
        print("bind failed")
        return
    }
    print("echo-server on :27015")

    let tick_rate = 16667  // ~60 Hz
    let mut tick_count = 0
    let mut running = 1

    while running == 1 {
        let start = tick_now_us()

        // Drain all pending packets
        let mut pkt = 0
        while pkt < 128 {
            let data = game_udp_recv(u)
            if data == "" {
                break
            }
            let peer = game_udp_sender(u)

            // Parse first byte as message type
            if len(data) > 0 {
                let b = buf_from_string(data)
                let msg_type = buf_read_u8(b)
                buf_free(b)

                if msg_type == 4 {
                    // Leave message — kick the peer
                    print("peer leaving:")
                    print(peer)
                    game_udp_kick(u, peer)
                } else {
                    // Echo back to sender
                    let _ = game_udp_send(u, peer, data)
                }
            }
            pkt = pkt + 1
        }

        // Every 60 ticks (~1 second), broadcast state
        if tick_count % 60 == 0 {
            let peers = game_udp_peers(u)
            if peers > 0 {
                let state = pack_state(tick_count, peers)
                let _ = game_udp_broadcast(u, state)
            }
        }

        tick_count = tick_count + 1
        tick_sleep_us(start, tick_rate)
    }

    game_udp_close(u)
    print("server stopped")
}
```

---

## API Reference

| Function | Purpose |
|----------|---------|
| `game_udp_bind(port)` | Bind UDP game socket |
| `game_udp_recv(u)` | Receive packet (tracks sender) |
| `game_udp_sender(u)` | Peer ID of last sender |
| `game_udp_send(u, peer, data)` | Send to specific peer |
| `game_udp_broadcast(u, data)` | Send to all peers |
| `game_udp_kick(u, peer)` | Disconnect a peer |
| `game_udp_peers(u)` | Connected peer count |
| `game_udp_fd(u)` | Raw fd for event loop |
| `game_udp_close(u)` | Close socket |
| `tick_now_us()` | Microsecond timestamp |
| `tick_sleep_us(start, interval)` | Sleep to hold tick rate |
| `buf_pack_new(cap)` | New write buffer |
| `buf_from_string(s)` | Buffer from existing data |
| `buf_write_u8/u16/u32` | Write unsigned integers |
| `buf_read_u8/u16/u32` | Read unsigned integers |
| `buf_write_i32/f32/f64` | Write signed/float values |
| `buf_read_i32/f32/f64` | Read signed/float values |
| `buf_to_string(b)` | Extract buffer contents |
| `buf_free(b)` | Free buffer memory |
