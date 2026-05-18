# `Fixed.h` — Fixed Peer Connection Backoff Metadata

`Fixed` is a small state-management class inside the `PeerFinder` subsystem that tracks reconnection timing for *fixed slots* — peer endpoints that are explicitly configured in the node's settings (e.g., trusted validators or known-good relays). Unlike dynamically-discovered peers, fixed peers must be persistently pursued: if a connection attempt fails, the system should retry, but not so aggressively that it floods an unreachable host.

## What a "Fixed Slot" Means

In PeerFinder's connection strategy (`Logic.h`), the peer set is divided into slots discovered through normal gossip and slots that are statically configured. The latter are stored in `fixed_`, a `std::map<beast::IP::Endpoint, Fixed>`. Every entry in that map owns one `Fixed` instance, which answers a single question for the connection scheduler: *is it time to try this address again?*

## Class Design

`Fixed` holds two private members:

- `m_when` — a `clock_type::time_point` representing the earliest moment a new connection attempt is permitted.
- `m_failures` — a `std::size_t` counting consecutive failures since the last successful connection (zero-initialized).

The constructor takes a `clock_type&` reference and sets `m_when = clock.now()`, so a freshly-registered fixed peer is immediately eligible for its first connection attempt. The copy constructor is defaulted, allowing `Fixed` objects to be stored by value in the map without ceremony.

## Backoff Logic

`failure()` and `success()` are the only mutating methods, and together they implement a Fibonacci-based exponential backoff. When a connection attempt fails:

```cpp
m_failures = std::min(m_failures + 1, Tuning::connectionBackoff.size() - 1);
m_when = now + std::chrono::minutes(Tuning::connectionBackoff[m_failures]);
```

`Tuning::connectionBackoff` is the array `{1, 1, 2, 3, 5, 8, 13, 21, 34, 55}`. Each element is a wait time in minutes. The failure index advances through this sequence with each consecutive failure, capping at index 9 (55 minutes) so the backoff never grows beyond that. The Fibonacci progression is a deliberate choice: it is gentler than a pure exponential doubling yet still grows quickly enough to avoid hammering a down peer every few seconds.

When a connection succeeds, `success()` resets `m_failures` to 0 and sets `m_when = now`, making the peer immediately eligible again if it were to disconnect and need reconnection.

## How the Scheduler Uses It

`Logic::get_fixed()` iterates the `fixed_` map and selects peers whose `when() <= now`, skipping any that are on cooldown or already connected:

```cpp
if (iter->second.when() <= now && squelches.find(address) == squelches.end() && ...)
```

`Logic` is also responsible for calling `failure()` or `success()` on the appropriate `Fixed` instance when a connection outcome is known. `Fixed` itself has no knowledge of socket state — it is purely a timing record, keeping the backoff policy cleanly separated from connection mechanics.

## Design Notes

The cap at `connectionBackoff.size() - 1` is a defensive invariant: it prevents the `m_failures` counter from ever indexing out-of-bounds even if `failure()` is called far more times than there are backoff levels. The maximum retry interval of 55 minutes is also pragmatically bounded — a fixed peer should never be abandoned entirely, so waiting longer than an hour would undermine the purpose of the fixed-slot concept.

Because `Fixed` is only ever accessed while `Logic`'s internal mutex is held, there is no per-object synchronization needed; the class is deliberately free of any concurrency machinery.