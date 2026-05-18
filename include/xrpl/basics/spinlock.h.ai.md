# `include/xrpl/basics/spinlock.h`

## Purpose

This header provides two template spinlock classes — `packed_spinlock` and `spinlock` — along with an architecture-specific CPU hint helper. Both classes meet the C++ `Lockable` named requirement, making them directly usable with `std::lock_guard`. They are deliberately low-level, narrowly scoped primitives intended for specific high-performance use cases rather than general synchronization; the file's own documentation warns that `std::mutex` frequently outperforms spinlocks on modern platforms and should be preferred unless profiling data justifies otherwise.

## `detail::spin_pause()`

The private helper `spin_pause()` abstracts away the CPU instruction used to reduce pipeline pressure inside spin loops. On x86/x86-64 it calls `_mm_pause()` (the `PAUSE` instruction), and on AArch64 it emits `yield` via inline assembly. Without this hint, a tight compare-loop saturates the processor's out-of-order execution pipeline with speculative loads, causing an expensive pipeline flush the moment the lock is finally released. Including the hint allows the processor to throttle speculative work and reduces the misprediction penalty on acquisition.

## `packed_spinlock<T>`

This class packs multiple independent spinlocks into a single `std::atomic<T>`, where each lock occupies one bit of the integer. The constructor takes a reference to the shared atomic and an index (0 to `sizeof(T)*8 - 1`); it precomputes the bitmask `1 << index` for use in every subsequent operation.

Three static assertions enforce correctness at compile time: `T` must be unsigned, `std::atomic<T>` must be always-lock-free (no fallback mutex), and the atomic must expose `fetch_or`/`fetch_and` — the primitive operations the lock depends on.

**Lock acquisition** (`try_lock`) uses `fetch_or(mask_, memory_order_acquire)`. This atomically ORs the bit in and returns the *previous* value; if the returned value had that bit clear, the lock was uncontested and is now held. If the bit was already set, the lock was already taken and `try_lock` returns `false`.

**The spin loop** in `lock()` implements the classic test-and-test-and-set (TATAS) pattern. After a failed `try_lock`, the thread spins on a `load(memory_order_relaxed)` rather than repeatedly issuing `fetch_or`. The relaxed load is intentional and critical: an exclusive read-modify-write operation like `fetch_or` always triggers a cache-line ownership transfer, so spinning with it under contention would flood the interconnect. The relaxed load allows the CPU to read from its local cache copy without broadcasting invalidation messages, dramatically reducing coherency traffic.

**Unlocking** uses `fetch_and(~mask_, memory_order_release)`, atomically clearing only this lock's bit while leaving all sibling locks in the same word undisturbed.

## `spinlock<T>`

This is a whole-word spinlock built on the same external `std::atomic<T>`. It treats `0` as unlocked and `std::numeric_limits<T>::max()` (all bits set) as locked. `try_lock()` uses `compare_exchange_weak(expected=0, desired=max, acquire, relaxed)` — acquiring on success, relaxing on failure. `unlock()` is a simple `store(0, memory_order_release)`.

The same TATAS pattern governs the spin loop, and the same relaxed-load reasoning applies.

**Critical compatibility caveat:** the file explicitly warns against mixing `spinlock` and `packed_spinlock` against the same atomic. A `spinlock` CAS checks for the full `0` state before acquiring; if any `packed_spinlock` elsewhere holds even a single bit, the `spinlock` will spin indefinitely. This is not merely a theoretical concern — in `SHAMapInnerNode.cpp`, both lock types share the same `lock_` member, and the code carefully partitions their usage (`spinlock` for whole-node operations, `packed_spinlock` for per-child-index operations) to avoid this conflict.

## Memory Ordering Rationale

The acquire-on-lock / release-on-unlock pairing is the minimum required for correctness: it creates a happens-before edge so that writes inside the critical section are visible to the next locker. The deliberate use of `memory_order_relaxed` on the polling loads is a performance optimization, not a correctness shortcut — it only governs how the spin loop observes the lock bit itself, not the protected data.

## Real Usage in the Codebase

In `AccountID.cpp`, `packed_spinlock` enables fine-grained sharding of a base58-encoded AccountID cache. A single `std::atomic<uint64_t>` holds 64 independent spinlocks. When looking up an account, the hash of the `AccountID` is reduced modulo 64 to select a slot, and only that one bit is contested — no serialization occurs across unrelated slots. This is the paradigm case for packed spinlocks: many logically independent entries each need minimal mutual exclusion, and allocating a full mutex per entry would cost orders of magnitude more memory.

In `SHAMapInnerNode.cpp`, individual child-node slots in the Merkle-Patricia trie are protected by per-slot `packed_spinlock` instances, enabling concurrent child pointer access. The whole-node `spinlock` protects coarser operations like cloning where all children need consistent visibility at once.

## Design Trade-offs

The inline documentation is candid about costs. Packing multiple locks into one word creates false-sharing at the *word level*: acquiring any one lock invalidates the entire cache line for every CPU that holds a copy, even if their target bits are uncontested. When contention is high across many bits simultaneously, this can make packed spinlocks *worse* than independent mutexes. The recommendation to use them only under profiling pressure is genuine — these are specialized tools whose value is space efficiency and NUMA-friendly hot-path latency, not broad-purpose locking.