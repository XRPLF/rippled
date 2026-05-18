# `src/libxrpl/crypto/secure_erase.cpp`

## Role and Purpose

This file provides the single-function implementation of `xrpl::secure_erase`, the codebase's canonical mechanism for wiping sensitive cryptographic material from memory. Its entire body is a one-line delegation to OpenSSL's `OPENSSL_cleanse`. The smallness is intentional: the hard problem being solved is not algorithmic but rather a battle against the C++ optimizer.

## The Compiler-Optimization Problem

A naive `memset(buf, 0, size)` on a buffer that is never read afterward will be silently removed by any optimizing compiler — the write is provably dead from the compiler's perspective, so it eliminates it as a no-op. The consequence in cryptographic code is that secret key material, seed bytes, and intermediate derivation buffers survive in process memory long after the developer intended them to be gone. They become exploitable via heap inspection, core dumps, swap files, cold-boot attacks, or speculative-execution side-channels.

`OPENSSL_cleanse` is specifically designed to defeat this class of optimization. Its implementation uses techniques such as a volatile write or a call through a function pointer that the optimizer cannot inline away, ensuring the zeroing survives to the final generated machine code. The header `secure_erase.h` cites Colin Percival's pair of blog posts (2014) for the full theoretical background and notes that even this best-effort approach cannot guarantee data has not leaked into CPU registers or caches — it only prevents the memory itself from being left dirty.

## Design Decision: Delegating to OpenSSL

The XRPL codebase already takes a hard dependency on OpenSSL for ECDSA, SHA-512, and CSPRNG operations via `csprng.cpp`. Delegating `secure_erase` to `OPENSSL_cleanse` rather than rolling a platform-specific solution (`memset_s` on C11, `explicit_bzero` on BSDs, a `SecureZeroMemory` import on Windows) keeps portability logic in one place — the OpenSSL build system — while ensuring the semantics are correct across all supported platforms.

## Usage Pattern in Sensitive Types

The callers seen in `SecretKey.cpp` and `Seed.cpp` reveal a consistent RAII cleanup discipline:

- **Destructor scrubbing**: `SecretKey::~SecretKey()` and `Seed::~Seed()` each call `secure_erase` on their internal fixed-size byte buffers as the very first and only action. This guarantees that every code path out of the object's lifetime — including exception unwind — wipes the raw key bytes.
- **Intermediate buffer scrubbing**: Ephemeral key material created during derivation (e.g., the `buf` array in `randomSecretKey()`, the SHA-512 half-digest in `derivePrivateKey()`, and the `rpk` scratch buffer in the secp256k1 generator loop) is erased immediately after the derived `SecretKey` object has taken ownership of a copy. The pattern is always: construct the permanent holder, then `secure_erase` the source buffer while it is still in scope.

This usage makes `secure_erase` the final line of defense ensuring that raw secret key entropy never persists on the heap or stack beyond its minimum necessary lifetime, regardless of how many copies or transformations the key material undergoes on the way to its final form.

## Interface

```cpp
// include/xrpl/crypto/secure_erase.h
namespace xrpl {
void secure_erase(void* dest, std::size_t bytes);
}
```

The function accepts a raw `void*` and a byte count with no null-pointer or zero-length guards. Callers are expected to provide valid arguments; the function is a low-level primitive, not a safe wrapper. Passing a null pointer or zero bytes delegates directly to `OPENSSL_cleanse`, whose own behavior in those edge cases is implementation-defined but generally harmless.