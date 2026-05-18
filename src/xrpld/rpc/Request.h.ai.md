# `RPC::Request` — Minimal RPC Dispatch Envelope

`Request` is a plain data-carrying struct that bundles together everything a single RPC invocation needs: a logging journal, the method name, the caller-supplied JSON parameters, a mutable resource charge, the JSON result buffer, and a reference to the application. It lives in `src/xrpld/rpc/Request.h` and belongs to the `xrpl::RPC` namespace alongside the more elaborate `Context` and `JsonContext` types defined in `Context.h`.

## Design Rationale: Struct Over Class

The choice of `struct` with entirely public members is deliberate. `Request` is not an encapsulated object — it is a pass-by-reference bundle of state for internal RPC dispatch. Handlers read `journal`, `method`, `params`, and `app` directly, and write their output directly into `result`. Accessor functions would add noise with no safety benefit, since the consumers are trusted internal handler implementations, not user-facing abstractions.

## The Resource Charge Field

`fee` is initialized in the constructor to `Resource::feeReferenceRPC`, the standard baseline charge defined in `xrpl/resource/Fees.h`. The comment annotates it `[in, out]`, explicitly marking it as a field that handlers may — and in costly cases should — escalate before returning. The resource management subsystem uses this value to debit the client's resource budget after the handler completes. Defaulting to reference-level rather than zero is a deliberate defensive choice: a handler that forgets to set a fee still imposes a non-trivial cost on the caller, preventing resource exhaustion through repeated low-effort calls.

The `Resource::Charge` type (`xrpl/resource/Charge.h`) pairs a numeric cost with a human-readable label. The fee schedule in `Fees.h` defines a tiered set of charges — from `feeMalformedRPC` for immediately-rejected requests up to `feeHeavyBurdenRPC` for expensive operations — giving handlers a vocabulary for communicating actual load to the resource manager.

## Member Lifetimes

`journal` is stored by value, but `beast::Journal` is a lightweight handle (not a full logger), so copying it is cheap and safe. `params` is accepted by non-const reference in the constructor but stored as a `Json::Value` copy, meaning the `Request` owns its parameter data independently of the caller's buffer. `result` is default-initialized as an empty `Json::Value` and is expected to be populated by the dispatched handler; the caller reads it back after execution.

`app` is stored as a raw reference, which imposes a lifetime constraint: no `Request` instance may outlive the `Application` object. In practice this is safe — `Application` is a process-scoped singleton that outlives all RPC activity — but it is an implicit invariant rather than an enforced one.

## Deleted Assignment Operator

The private, unimplemented `operator=` is a pre-C++11 convention for signaling non-assignability. Reference members (`app`) already cause the compiler to delete copy-assignment, so this declaration is belt-and-suspenders: it makes the design intent explicit and surfaces the constraint at the declaration site rather than in a compiler error downstream.

## Relationship to `RPC::Context`

The sibling `Context.h` defines `Context`, `JsonContext`, and `GRPCContext` — richer dispatch envelopes that carry session-level metadata including the client's `Role`, a `Resource::Consumer` to charge directly, `NetworkOPs`, `LedgerMaster`, a coroutine handle, a subscriber pointer, and an API version number. The active dispatch path in `RPCHandler.cpp` and `ServerHandler.cpp` operates on `JsonContext`, not `Request`.

`Request` is comparatively minimal: it carries only what a pure handler function needs to read inputs and write outputs, with no session policy. This narrower scope suits scenarios where the full session context is not available or not needed, and where the caller prefers to manage resource accounting indirectly through the returned `fee` rather than through a live `Resource::Consumer`.