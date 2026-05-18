# `predicates.h` — Peer Selection and Message Dispatch Combinators

This header solves a recurring problem in overlay network management: how do you send a message to a filtered subset of connected peers without writing bespoke iteration logic at every call site? Rather than scattering `if` statements throughout broadcast code, this file provides a small combinator library of callable structs that compose selection logic with message dispatch. All types are designed to be passed as functors into `Overlay::foreach()`, which iterates over all active peers and invokes the functor with each peer's `shared_ptr<Peer>`.

## The Dispatch/Predicate Split

The core design separates two orthogonal concerns: *which peers to select* and *whether to send*. This produces two conceptual layers:

**Dispatch functors** — `send_always`, `send_if_pred<Predicate>`, and `send_if_not_pred<Predicate>` — take a `Message` reference and implement `operator()(shared_ptr<Peer>)`. These are what get passed to the iteration mechanism.

**Selection predicates** — `match_peer`, `peer_in_cluster`, and `peer_in_set` — take a `shared_ptr<Peer>` and return `bool`. They are composable, reusable, and independent of message dispatch.

The free functions `send_if()` and `send_if_not()` wire these two layers together by deducing the predicate type from its argument, sparing callers from manually spelling out `send_if_pred<SomePredicate>`. This template helper pattern was especially useful before C++17 class template argument deduction became widespread and remains the idiomatic call pattern here.

## Reference Semantics and Lifetime

All dispatch functors hold `std::shared_ptr<Message> const&` — a reference to a shared pointer, not a copy of it. `Message` objects can be substantial (they carry serialized, optionally compressed protobuf payloads), and the `shared_ptr` already manages the object's lifetime. Because these functors are ephemeral stack-allocated temporaries created and consumed within a single `foreach` call frame, the reference is safe as long as the caller's `shared_ptr` stays alive for the loop's duration — a reasonable invariant that the typical usage pattern naturally satisfies.

Similarly, `send_if_pred` and `send_if_not_pred` store `Predicate const&`. The predicate must therefore outlive the dispatch functor, which again holds naturally in the intended usage of same-frame temporaries.

## Predicate Details

`match_peer` identifies a specific peer by raw pointer identity (`peer.get() == matchPeer`). The null check (`if (matchPeer)`) means a default-constructed `match_peer` with `nullptr` never matches anything, making it a safe no-op "skip nobody" sentinel. This default-null behavior is exploited by `peer_in_cluster`, which embeds a `match_peer skipPeer` member to optionally exclude one peer (typically the message originator) from a cluster broadcast, without requiring separate exclusion logic at the call site.

`peer_in_cluster` chains these checks: first it skips the excluded peer via `skipPeer(peer)`, then verifies `peer->cluster()`. Composing `match_peer` rather than duplicating the raw-pointer comparison keeps the intent clear and avoids subtle mistakes with null handling.

`peer_in_set` performs a lookup against a `std::set<Peer::id_t>` held by `const&`, providing O(log n) membership tests at each peer visitation. Holding a reference keeps the predicate lightweight and delegates lifetime ownership to the caller. This predicate supports the targeted data-request workflows in `PeerSet.h`, where a working set of peer IDs is assembled and messages need to be delivered only to those specific participants.

## Why Named Functors Over Lambdas

Structuring these as named types rather than raw lambdas serves composability: `peer_in_cluster` can be combined with `send_if` or `send_if_not` interchangeably, and the types appear in error messages, making misuse diagnosable. The `return_type = void` alias on each dispatch functor is a documentation-level constraint marker signalling the expected interface for anything consumed by `foreach`. A pure lambda approach would be terser per call site but would lose the reuse, the named documentation, and the composability that makes complex relay logic readable.

## Relationship to Sibling Files

This header sits at the intersection of `Peer.h` — which defines `Peer::id_t`, `Peer::cluster()`, and the pure-virtual `Peer::send()` — and `Message.h`, which provides the `Message` type being dispatched. The iteration host, `Overlay::foreach()`, lives in `Overlay.h` but is not included here; the predicates themselves have no dependency on the collection that iterates them. This separation keeps the dependency graph shallow: a consumer who only needs to call `Overlay::foreach(send_always{msg})` pulls in only `Message.h` and `Peer.h`, not the heavier `Overlay.h` infrastructure.