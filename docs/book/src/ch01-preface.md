# 1. Preface — Why Mako

Backend, infrastructure, and developer-tool work wants four things at once:
**simplicity**, **memory safety**, **predictable performance**, and **fast
iteration**. Most stacks force a trade:

- Managed runtimes give safety and a rich stdlib, but pay with GC pauses and
  heavier deploy stories.
- Low-level systems languages give control, but leave ownership and concurrency
  races to discipline.
- Newer ownership-focused languages give safety, but can feel heavy for everyday
  HTTP and session servers.

Mako’s bet is practical:

> Memory safety · simple concurrency · fast builds · **no mandatory GC** ·
> great tooling · clean errors · single binary · strong stdlib.

## Product Shape

Mako aims to be a **general-purpose backend and infrastructure language** with
broad versatility and easy deployment, strong safety, and low runtime
surprises. It should be natural for REST APIs, CLIs, agents, workers, proxies,
databases, protocol stacks, AI services, and realtime systems.

Mako's syntax should also be its own. The language draws on proven ideas, but the
final surface should feel like Mako: compact, explicit where safety matters, and
consistent under `mako fmt`.

Session-shaped servers are still a proving ground: long-lived connections,
messages, deterministic latency, no shared-memory races. Actors + crews +
arenas fit that shape, but telecom is a domain track, not the whole identity.

```mko
// Session-shaped concurrency (see examples/actor.mko)
actor Session {
    receive Invite { print("invite") }
    receive Timer { print("tick") }
    receive Bye { print("bye") }
}
```

## What “Done” means in this book

As of the Wave 9 inventory:

| Claim | Meaning |
|-------|---------|
| STATUS / MVP **100%** | Current north-star shipped; homebrew-core publish is external |
| Stdlib ~**98%** areas | Major target standard library *areas* for backends — not every symbol |
| Suite **130** | `mako test examples/testing` green without live TLS/QUIC flags |

We will not pretend complete Unicode/PCRE, viewer-perfect Huffman JPEG, or live
reflect field *values* are finished. Those residuals are listed in STATUS.

## How to use this book

Read chapters 2–6 in order if you are new. Jump to 7–10 when building services.
Use chapter 14 as a recipe index into [howto/](../../howto/). When something
looks wrong, re-check with `mako check` and the GUIDE — the compiler is the
source of truth for syntax.
