# `xrpl::Resource::Disposition` — Load-Control Decision Signal

## Role in the System

`Disposition.h` defines a three-state enum that is the return type of every load-charge evaluation in the XRPL resource-management subsystem. It is deliberately minimal: a single `enum` in the `xrpl::Resource` namespace with no dependencies whatsoever. Its job is to carry the verdict that `Logic` reaches about a network endpoint back through the `Consumer` API to whatever code is handling that peer connection.

## The Three States

`ok`, `warn`, and `drop` map cleanly onto the three actions the node software can take toward a misbehaving or overloaded peer:

- **`ok`** — the endpoint's load balance is within acceptable bounds; do nothing.
- **`warn`** — consumption is elevated but not yet critical; the caller may signal the remote peer that it is approaching its limit.
- **`drop`** — the endpoint has exceeded its allowance; the caller must disconnect it.

The ordering of the enumerators matters implicitly: higher numeric value signals a more severe outcome, which allows comparison logic in `Logic` to find the "worst" disposition across multiple charges without needing a separate severity table.

## How It Flows Through the Codebase

`Consumer::charge()` and `Consumer::disposition()` both return a `Disposition`. In `Consumer.cpp`, every charge path initialises a local `Disposition d = ok;` and then delegates to `Logic::charge()`, whose return value becomes the function's result. Privileged (unlimited) endpoints skip the charge entirely and always return `ok`.

The calling code — typically peer-management layers like `OverlayImpl` or `PeerImp` — receives this value and decides whether to issue a warning message to the remote peer or close the connection outright. The enum keeps that decision boundary sharp: the resource subsystem computes the verdict, the network layer executes it, and neither bleeds into the other's concerns.

## Design Rationale

Using a plain unscoped `enum` (rather than `enum class`) is intentional: the values are used directly as comparison targets throughout the `Logic` and `Consumer` implementations without requiring casts. The three-state design separates the "approaching limit" signal from the hard "disconnect" signal, giving operators a grace window before connections are terminated. A binary `ok/drop` would lose that nuance and force more aggressive disconnection policies.

Because `Disposition.h` has no `#include` directives of its own, it can be included anywhere in the resource subsystem — including in headers that are themselves included widely — without pulling in any additional dependencies.