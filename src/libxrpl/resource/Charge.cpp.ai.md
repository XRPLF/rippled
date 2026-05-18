# `Charge.cpp` — Resource Cost Value Type

## Role in the System

`Charge` is the fundamental unit of account in XRPL's resource management subsystem (`xrpl::Resource`). The resource manager exists to throttle and disconnect peers or RPC clients that impose excessive computational load on the server. Every costly operation — verifying an invalid signature, processing a heavy RPC call, receiving a malformed peer message — is tagged with a `Charge` that quantifies its cost in abstract internal units. Those charges accumulate on a per-connection `Entry` and are evaluated by `Logic` to decide whether to warn or disconnect the offending consumer.

`Charge.cpp` provides the implementation for this value type. It is intentionally minimal: just construction, accessors, comparison, scaling, and string conversion. All policy (thresholds, decay rates, disposition decisions) lives elsewhere in `Logic`.

## Design of the Value Type

`Charge` pairs two pieces of data: an integer `m_cost` (aliased as `value_type = int`) and a human-readable `m_label`. The label is purely diagnostic — it appears in log output and the `to_string()` representation (`"heavy RPC ($3000)"`) to make it easy to reason about what triggered a resource charge in a log trace.

The default constructor is explicitly deleted. This forces every charge to carry a meaningful cost at creation time, preventing zero-cost sentinel objects from silently floating through the codebase and understating load.

## Comparison Semantics

Both `operator==` and `operator<=>` compare charges solely by `m_cost`, ignoring the label entirely. This is a deliberate choice: the label is metadata for humans, not part of the charge's semantic identity. Two `Charge` objects with the same numeric cost are considered equal even if they carry different labels. The spaceship operator (`<=>`, C++20) returns `std::strong_ordering`, enabling the full set of relational operators via the standard rewrite rules — useful if charges are ever sorted or placed in ordered containers.

## Scaling Operator

`operator*(value_type m)` returns a new `Charge` with the cost multiplied by `m`, preserving the original label. This enables sites that impose burdens proportional to some runtime quantity (e.g., the number of items in a batch request) to express that as `feeReferenceRPC * batchSize` without constructing an entirely new named charge. The label inheritance is the right call here: diagnostic output still names the operation type even when the cost has been scaled.

## The Fee Schedule

`Charge.cpp` is tightly paired with `Fees.cpp`, which defines the catalog of named constants used throughout the server:

| Constant | Cost | Description |
|---|---|---|
| `feeTrivialPeer` | 1 | Peer message requiring no reply |
| `feeReferenceRPC` | 20 | Baseline RPC load |
| `feeMalformedRPC` / `feeMalformedRequest` | 100–200 | Immediately detectable invalid inputs |
| `feeUselessData` | 150 | Data the node has no use for |
| `feeModerateBurdenPeer` | 250 | Peer work requiring some effort |
| `feeInvalidData` | 400 | Data requiring verification before rejection |
| `feeMediumBurdenRPC` | 400 | Moderately expensive RPC |
| `feeHeavyBurdenPeer` / `feeInvalidSignature` | 2000 | Expensive peer work or failed signature check |
| `feeHeavyBurdenRPC` | 3000 | Very expensive RPC operation |
| `feeWarning` | 4000 | Cost of receiving a warning from a peer |
| `feeDrop` | 6000 | Cost assessed when a connection is dropped |

The range from 1 to 6000 gives the `Logic` layer enough granularity to distinguish between nuisance noise and deliberate abuse. The comment in `Fees.cpp` notes that `Logic::charge` uses log-level cutoffs keyed to these same values, so the numeric scale is semantically meaningful, not arbitrary.

## Usage at the Call Site

`Consumer::charge(Charge const& what, ...)` is the primary consumption path. It delegates to `Logic::charge()` with the `Entry` for the connection and the `Charge` value. `Consumer::disposition()` uses a zero-cost `Charge(0)` probe — checking the entry's accumulated balance without adding to it — to query current standing without imposing load. Both paths make `Charge`'s lightweight value semantics (copy-cheap `int` plus `std::string`) appropriate; there is no need for pointer or reference sharing.