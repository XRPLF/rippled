# `Random.cpp` — RPC Handler for Cryptographic Random Value Generation

This file implements `doRandom`, the handler for the `random` RPC command. Its sole responsibility is to draw 256 bits of entropy from the node's cryptographically secure PRNG and return the value to the caller as a hex-encoded JSON string. The handler requires no ledger state and accepts no user input, making it one of the simplest endpoints in the RPC surface while still serving a meaningful purpose: giving clients a convenient way to obtain trusted randomness sourced directly from a live XRPL node.

## Role in the RPC Framework

`doRandom` is declared in `Handlers.h` alongside all other RPC handler functions and is registered in `src/xrpld/rpc/detail/Handler.cpp` as:

```cpp
{"random", byRef(&doRandom), Role::USER, NO_CONDITION}
```

The `Role::USER` designation means any connected client — not just admins — can invoke it. `NO_CONDITION` means no particular ledger state (open, closed, validated) is required before dispatching: the handler runs unconditionally, which makes sense because it never touches ledger data at all.

## Entropy Source: `crypto_prng()` and `beast::rngfill`

The core of the handler is a single call:

```cpp
uint256 rand;
beast::rngfill(rand.begin(), rand.size(), crypto_prng());
```

`crypto_prng()` returns a reference to the process-wide singleton `csprng_engine`. That engine is mutex-protected and meets the C++ `UniformRandomNumberEngine` named requirement, so it integrates cleanly with standard library utilities. Its constructor seeds from `std::random_device` and it supports explicit entropy injection via `mix_entropy()`. The engine is deliberately non-copyable and non-movable — there is exactly one CSPRNG for the process, accessed only through the global accessor.

`beast::rngfill` takes an iterator range and a generator and fills the byte range by repeatedly calling `operator()` on the engine. Because `csprng_engine` also exposes a bulk `operator()(void*, size_t)` overload, efficient implementations of `rngfill` can bypass the 64-bit-at-a-time loop, but the interface stays the same either way.

The result is stored in a `uint256`, XRPL's fixed-size 32-byte integer type, which is then serialized to a 64-character lowercase hex string via `to_string()` and placed under the `jss::random` key in the response JSON.

## Exception Handling

The body is wrapped in a `try/catch(std::exception const&)` that returns `rpcError(rpcINTERNAL)` on failure, marked `LCOV_EXCL_LINE` because it is never expected to execute. The inline comment acknowledges the redundancy directly: a top-level catch already exists in the RPC dispatch layer that would handle any propagated exception. The local catch is a legacy defensive pattern that has not been removed, likely because the cost of keeping it is zero and removing it requires auditing whether the top-level handler actually covers this code path everywhere `doRandom` might be called.

## Design Observations

There is a deliberate asymmetry between this handler and most others: it receives a `RPC::JsonContext&` but ignores it entirely. No fields are read from the request, no role or permission checks beyond the dispatch-table registration are needed, and no ledger objects are touched. This makes the handler stateless from the perspective of XRPL data — it is essentially a thin RPC wrapper around a syscall-backed entropy pool.

The choice to expose node-generated randomness over RPC is useful for clients building applications where they want an external entropy source they did not generate themselves (e.g., for combined randomness schemes). The trust model is straightforward: the client trusts the node operator's CSPRNG seeding, which relies on `std::random_device` and any additional `mix_entropy` calls the node makes during its lifecycle.