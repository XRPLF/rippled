# `include/xrpl/protocol/detail/secp256k1.h`

## Role in the System

This header provides a single, process-wide `secp256k1_context` instance for use throughout the XRPL protocol layer. The secp256k1 library — the same elliptic curve library used by Bitcoin — requires callers to allocate and initialize a context object before performing any cryptographic operations. Creating that context is relatively expensive and, more importantly, the context itself is safe to share across threads for read-only operations. This file centralizes that lifecycle into one place, ensuring the context is created once and destroyed cleanly.

## Design: Template-Based Singleton Without a Separate `.cpp`

The function `secp256k1Context()` is declared as a function template with a defaulted, unused type parameter (`template <class = void>`). This is a well-known C++ idiom for defining a function with a `static` local variable in a header file without violating the One Definition Rule (ODR). A plain `inline` function would also work in C++17, but the template trick predates widespread `inline`-variable support and remains common in headers that need to be compatible with older toolchains and translation units that include the header independently.

The internal `holder` struct owns the `secp256k1_context*` pointer and manages its lifetime through RAII. Construction calls `secp256k1_context_create` with both `SECP256K1_CONTEXT_VERIFY` and `SECP256K1_CONTEXT_SIGN` flags, pre-initializing the context for both signature signing and verification so the same instance can service either operation. The destructor calls `secp256k1_context_destroy`, ensuring the context is properly freed when the process exits. The `static holder const h` inside the function body is initialized once on the first call and lives for the remainder of the program.

## Usage in the Protocol Layer

Both `SecretKey.cpp` and `PublicKey.cpp` include this header and call `secp256k1Context()` heavily. In `SecretKey.cpp` it is passed to functions like `secp256k1_ec_seckey_verify`, `secp256k1_ec_pubkey_create`, `secp256k1_ec_seckey_tweak_add`, and `secp256k1_ecdsa_sign` — covering the full key derivation and signing path for the secp256k1 (ECDSA) key type that XRPL supports alongside Ed25519. In `PublicKey.cpp` it services the verification side: `secp256k1_ecdsa_signature_normalize` and `secp256k1_ecdsa_verify`. Both callers receive a `const*` to the context, which matches the secp256k1 library's thread-safety contract — concurrent reads from a const context require no additional locking.

## Why `detail/`?

Placement under `protocol/detail/` signals that this is an implementation convenience header, not part of the public API surface. Consumers of the XRPL library should interact with `SecretKey` and `PublicKey` directly; they have no reason to touch the raw secp256k1 context. Keeping this header in `detail/` enforces that convention by convention, even if the C++ language does not enforce directory-based access control.

## Tradeoffs

Combining `SECP256K1_CONTEXT_VERIFY | SECP256K1_CONTEXT_SIGN` in a single context trades a small amount of additional initialization memory for the convenience of one shared instance. The alternative — separate sign and verify contexts — would double the allocation and add code paths for selecting the right one. For a node that signs transactions and verifies signatures continuously, a unified context is the pragmatic choice. The `const` qualifier on both the `holder` instance and the returned pointer makes the sharing intent explicit and prevents any caller from accidentally randomizing or otherwise mutating the shared context.