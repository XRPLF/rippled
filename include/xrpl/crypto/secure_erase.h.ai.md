# `include/xrpl/crypto/secure_erase.h`

This header declares a single function, `secure_erase(void* dest, std::size_t bytes)`, that exists to solve a specific and subtle problem in cryptographic software: **compiler-optimized dead-store elimination silently removing memory-clearing code**.

## The Problem It Solves

When sensitive key material — private keys, seeds, derived intermediates — is held in a local buffer, good practice demands that buffer be zeroed before it goes out of scope. A plain `memset(buf, 0, size)` is the obvious approach, but a conforming C++ compiler is allowed to remove any write to memory that is never subsequently read. Since a zeroing `memset` right before a `return` or end-of-scope is definitionally a dead store, optimizers routinely eliminate it. The result is that sensitive bytes linger on the stack or heap far longer than the programmer intended, potentially surfacing in crash dumps, `swap` pages, or cold-boot attacks.

The function declaration intentionally provides no inline body — it lives in a separate translation unit (`src/libxrpl/crypto/secure_erase.cpp`). This alone forces the compiler to treat the call as an opaque side effect, but the implementation goes further by delegating to `OPENSSL_cleanse()`, OpenSSL's purpose-built routine for this exact scenario. `OPENSSL_cleanse` uses platform-specific techniques (volatile pointer casts, memory barriers, or processor-level serialization depending on target) specifically designed to survive optimization passes that would kill a naive `memset`.

The header comment is notably candid: it says the function "attempts to" clear memory and explicitly acknowledges that register contents, CPU caches, and other micro-architectural artifacts are outside its reach. This is a reference to Colin Percival's two-part 2014 analysis (linked in the comment), which established both the right technique for beating dead-store elimination *and* the uncomfortable truth that zeroing RAM is never a complete solution.

## Usage Patterns in the Codebase

`secure_erase` appears consistently in the two most security-sensitive files in the crypto layer: `SecretKey.cpp` and `Seed.cpp`.

In both files, the pattern repeats in two forms. First, in destructors: `SecretKey::~SecretKey()` and `Seed::~Seed()` each call `secure_erase` on their internal byte buffers as the very first action, ensuring the raw key bytes are wiped whenever an object goes out of scope regardless of how the destructor is triggered. Second, in key derivation and generation functions: after deriving a key into a temporary stack or heap buffer and copying it into the final `SecretKey` object, the source buffer is immediately wiped. For example, after `randomKeyPair()` fills a 32-byte stack buffer with random data and wraps it in a `SecretKey`, it calls `secure_erase(buf, sizeof(buf))` before returning — the intent being that the raw entropy never persists beyond the call site even if the `SecretKey` copy is later destroyed.

## Design Rationale

Wrapping `OPENSSL_cleanse` behind an `xrpl`-namespace function (rather than calling it directly) provides two benefits: it insulates call sites from the OpenSSL header dependency, and it gives the codebase a single, auditable choke point if the underlying strategy ever needs to change (e.g., switching to `explicit_bzero` on platforms where that is available and preferred). The thin wrapper adds zero runtime overhead — it is a single call with no branching.

The honest limitation documented in the comment is architecturally important: callers should not treat `secure_erase` as a guarantee of complete erasure, but rather as a best-effort mitigation that eliminates the most exploitable residue — heap and stack memory — while being transparent about what it cannot control.