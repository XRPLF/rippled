# `include/xrpl/resource/Fees.h` — Resource Charge Schedule

`Fees.h` is the fee schedule for the XRPL server's resource-management subsystem. It declares a fixed set of named `Charge` constants that quantify the load cost of every category of misbehaving, burdensome, or invalid request a node might receive. These constants serve as the vocabulary through which calling code communicates resource events to the `Resource::Manager`.

## Role in the Resource System

The `xrpl::Resource` subsystem implements a leaky-bucket rate-limiter over network endpoints. Each connected peer or RPC client is represented by a `Consumer` object. Every time the server processes a request from that consumer it calls `Consumer::charge(fee)`, passing one of the constants declared here. Charges accumulate in the consumer's running balance, which decays exponentially over a 32-second window (`Tuning.h`: `decayWindowSeconds = 32`). When the balance crosses `warningThreshold` (5000 units) the consumer receives a warning; when it crosses `dropThreshold` (25000 units) the connection is severed. `Fees.h` is not involved in that accounting machinery — it purely defines the numeric weights used as inputs.

## The Constants and Their Magnitudes

The file groups charges into four logical tiers, and the relative magnitudes reveal the threat model directly.

**General protocol charges** cover events that span both RPC and peer paths:

| Constant | Cost | Rationale |
|---|---|---|
| `feeRequestNoReply` | 10 | Unsatisfiable but not obviously malformed; very cheap. |
| `feeMalformedRequest` | 200 | Detectable as invalid immediately, low CPU, but clearly misbehaving. |
| `feeUselessData` | 150 | Data received that serves no purpose. |
| `feeInvalidData` | 400 | Data that required verification work before rejection. |
| `feeInvalidSignature` | 2000 | Cryptographic verification was done and failed — expensive CPU work was wasted. |

The ordering here is important: `feeInvalidSignature` costs ten times more than `feeMalformedRequest` because crypto verification is a significant CPU operation, while malformation checking is a cheap parse step.

**RPC-tier charges** range from `feeReferenceRPC` (20), a baseline for a well-formed but unspecified load, up to `feeHeavyBurdenRPC` (3000) for queries that require substantial server work. `feeMediumBurdenRPC` (400) and `feeHeavyBurdenRPC` (3000) are used by RPC handlers that do index scans, ledger traversals, or other resource-intensive operations.

**Peer-tier charges** reflect the peer-to-peer gossip and data-propagation paths. `feeTrivialPeer` is deliberately set at 1 — the lowest possible unit — because many peer interactions (such as simple acknowledgments) impose negligible load and must not count against well-behaved peers. `feeModerateBurdenPeer` (250) and `feeHeavyBurdenPeer` (2000) cover validation messages and large data transfers.

**Administrative charges**, `feeWarning` (4000) and `feeDrop` (6000), are sentinel values charged when the system issues a warning or drops a consumer. At 4000, `feeWarning` is just below `warningThreshold` (5000), meaning a consumer that already has any balance will be pushed into warning territory as soon as it receives one. At 6000, `feeDrop` itself exceeds the warning threshold, ensuring that any endpoint being forcibly dropped crosses into the warning band immediately. These charges serve as accounting bookkeeping that makes the consumer's balance reflect its history even after it reconnects.

## Design: Named `extern` Constants vs. Enum

A natural alternative would be to use an `enum` or `constexpr int` values directly. The choice of `extern Charge const` objects is deliberate: `Charge` bundles a numeric cost with a human-readable string label (`"invalid signature"`, `"heavy RPC"`, etc.). This label is surfaced in log output, in `Consumer::charge()` call sites, and in diagnostics without any additional lookup table. The label travels with the cost from the point of declaration all the way to log emission, which makes triage of abusive consumers significantly easier.

The `Charge` class also supports `operator*(value_type)` multiplication, so callers can express proportional cost (e.g. `feeMediumBurdenRPC * 3`) while retaining the base label — a pattern useful when batch operations should proportionally penalize based on the number of items processed.

## Call Site Pattern

The typical use in `PeerImp.cpp` is:

```cpp
if ((usage_.charge(fee, context) == Resource::drop) && usage_.disconnect(p_journal_))
    ...
```

The `fee` passed here is always one of the constants from this file, selected based on the nature of the violation or load event. `Logic.h` `#include`s `Fees.h` directly, making the entire schedule available to the core accounting engine, while individual protocol handlers only need to select the appropriate constant for each event type.