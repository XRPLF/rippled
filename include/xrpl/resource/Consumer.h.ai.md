# `include/xrpl/resource/Consumer.h`

## Role in the Resource Management Subsystem

`Consumer` is the public-facing handle for XRPL's peer and RPC rate-limiting system. The broader `Resource` subsystem assigns a running "balance" to every network endpoint — inbound peers, outbound connections, and privileged (unlimited) admin connections — and enforces disconnection or warning when that balance exceeds configured thresholds. `Consumer` is what the rest of the node interacts with: a lightweight, copyable object that represents one endpoint's resource consumption record and provides methods to charge it, query its disposition, and act on overload signals.

The internal data lives in `Entry`, a heap-allocated node managed by `Logic`. `Consumer` is nothing more than a reference-counted pointer pair `{Logic*, Entry*}`. The `Manager` interface (declared in `ResourceManager.h`) is what the application layer uses to mint new `Consumer` objects; the actual construction is gated through `Logic`, which is declared a `friend` so it can invoke the private `Consumer(Logic&, Entry&)` constructor.

## Reference Counting and Lifetime

`Consumer`'s copy constructor, destructor, and copy-assignment operator together implement a manual reference count routed through `Logic::acquire()` and `Logic::release()`:

- **Copy**: increments `Entry::refcount` via `Logic::acquire()`.
- **Destruction / reassignment**: decrements `refcount` via `Logic::release()`.
- **When refcount reaches zero**: `Logic::release()` moves the `Entry` from its active intrusive list (inbound, outbound, or admin) to the `inactive_` list and stamps a `whenExpires` time. A background `periodicActivity()` sweep then erases entries that have lingered in inactive for `secondsUntilExpiration` (300 seconds).

This design avoids separate heap allocation for the refcount itself (the field lives directly in `Entry`) and keeps all mutation inside `Logic`, which holds a `std::recursive_mutex`. `Consumer` objects are therefore safe to copy and destroy from any thread, but they are not independently thread-safe for concurrent mutation of the same instance.

The default constructor produces a null `Consumer` (`m_logic == nullptr`, `m_entry == nullptr`). Many methods short-circuit on null pointers, which allows `Consumer` to be used as an optional-style placeholder before a real endpoint is assigned.

## Privilege Levels and the `charge()` Path

Every `Entry` carries a `Kind`: `kindInbound`, `kindOutbound`, or `kindUnlimited`. The `kindUnlimited` kind is assigned to administrative connections (local or cluster-level) that should not be subject to resource limits. `isUnlimited()` delegates to `Entry::isUnlimited()`, which simply checks `key->kind == kindUnlimited`.

The `charge(Charge const& fee, std::string const& context)` method is the primary operation: it applies a cost to the endpoint's balance and returns a `Disposition` (`ok`, `warn`, or `drop`). Critically, **unlimited consumers are silently exempted** — `charge()` short-circuits for them and always returns `ok`. The balance itself is maintained in `Entry::local_balance`, a `DecayingSample` with a 32-second exponential decay window. This means short bursts decay away quickly; sustained high-rate consumption accumulates. The combined balance (local plus any imported `remote_balance` from gossip peers) is then compared against the `warningThreshold` (5000) and `dropThreshold` (25000) defined in `Tuning.h`.

`disposition()` performs a zero-cost charge (`Charge(0)`) to read the current balance-based state without incrementing it. This is intentionally a bit indirect — rather than exposing the raw integer balance as a `Disposition` directly, it routes through the same code path as a real charge, ensuring the same decay and rounding logic applies.

## Warning and Disconnect Signals

`warn()` and `disconnect()` serve as the action layer on top of `charge()`. Both delegate to `Logic::warn()` and `Logic::disconnect()` respectively, both of which are no-ops for unlimited consumers.

`Logic::warn()` is edge-triggered: it only fires if the balance is at or above `warningThreshold` **and** the current clock time differs from `entry.lastWarningTime`. This prevents the same endpoint from receiving repeated warnings in rapid succession — the warning "consumes itself" by updating `lastWarningTime` and applying a `feeWarning` charge. The `warn()` method on `Consumer` reflects this: its doc comment notes "this consumes the warning," meaning callers should not call it speculatively in a tight loop.

`disconnect()` checks whether the balance is at or above `dropThreshold`. If so, it additionally applies a `feeDrop` charge — a deliberate penalty designed to prevent a just-dropped peer from immediately reconnecting and passing the initial `disposition()` check cleanly.

## Gossip Integration and `remote_balance`

Each `Entry` carries both a `local_balance` (the decaying sample of this node's own observations) and a `remote_balance` (an integer summing contributions from other cluster nodes via the gossip mechanism). When `Logic::importConsumers()` receives gossip data from a peer, it increments `remote_balance` on the relevant entries by the reported values, and decrements the old values when the gossip expires or is superseded. `Consumer::entry()` exposes the raw `Entry&` specifically to allow `importConsumers()` to manipulate `remote_balance` directly — it is labeled as private in spirit even though it is public in declaration.

## The `elevate()` Stub

The header declares `elevate(std::string const& name)`, described as raising a consumer's privilege to a "Named endpoint" and releasing the reference to the original endpoint descriptor. No implementation exists in the codebase. This appears to be either a planned feature that was never completed or a vestigial declaration from an older design that removed the named-endpoint tier. Code calling `elevate()` would produce a linker error, making this a dead declaration rather than a runtime trap.

## `setPublicKey()`

When a connected peer completes its handshake, the server can associate a `PublicKey` with the resource entry via `setPublicKey()`. This feeds into `Entry::to_string()` via `getFingerprint(key->address, publicKey)`, making log messages and JSON output identify peers by their cryptographic identity rather than IP address alone — important in the XRPL context where multiple validators may be reachable through the same NAT address.