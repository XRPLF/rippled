# `WalletPropose.cpp` — Admin Key Generation RPC Handler

## Role and Purpose

This file implements the `wallet_propose` admin RPC command, which generates a complete XRPL account identity — seed, key pair, and account address — from a caller-supplied or randomly-generated seed. It sits in the `admin/keygen` handler path, reflecting that key generation is exclusively an admin-level operation not exposed to untrusted clients.

The entry point `doWalletPropose()` is a thin wrapper that extracts `context.params` and delegates to the free function `walletPropose()`. Separating the two allows `walletPropose()` to be tested and reused independently of the RPC dispatch context — the test suite in `KeyGeneration_test.cpp` calls it directly with raw `Json::Value` parameters.

## Seed Resolution Chain

The core of `walletPropose()` is a priority-ordered seed resolution strategy. The caller may supply a seed through three mutually-exclusive channels: `passphrase` (a human-readable string hashed to a seed), `seed` (a base58-encoded seed), or `seed_hex` (a hex-encoded seed). If none are provided, `randomSeed()` generates a cryptographically secure random seed.

Before falling through to the standard `getSeedFromRPC()` helper, there is a first-pass check for XrplLib-encoded seeds. The XrplLib JavaScript library historically encoded Ed25519 seeds using a non-standard base58 prefix of `0xE1 0x4B`, distinct from rippled's own encoding. The `parseXrplLibSeed()` helper in `RPCHelpers.cpp` detects this 18-byte form and returns the 16-byte seed content, setting the `libSeed` flag. This matters because XrplLib seeds are unambiguously Ed25519; if a caller supplies one but also requests `key_type: "secp256k1"`, the handler returns `rpcBAD_SEED` rather than silently producing a wrong key type.

## Key Type Defaulting

`keyType` starts as `std::nullopt`. If the caller requests an explicit `key_type`, the string is validated via `keyTypeFromString()` (returning `nullopt` on unrecognized values, which yields `rpcINVALID_PARAMS`). If a XrplLib seed is detected, `keyType` is forced to `KeyType::ed25519`. If no key type is ever specified, it defaults to `KeyType::secp256k1` — preserving historical compatibility with clients that predate Ed25519 support on the ledger.

## Output Fields

After `generateKeyPair(*keyType, *seed)` produces the public key, `walletPropose()` assembles a result object with six fields representing the same seed and key in different encodings:

- `master_seed` — base58-encoded seed (the canonical wallet backup format)
- `master_seed_hex` — raw hex of the seed bytes
- `master_key` — [RFC 1751](https://www.rfc-editor.org/rfc/rfc1751) mnemonic encoding via `seedAs1751()`, intended for humans writing it down
- `account_id` — base58check address derived from `calcAccountID(publicKey)`
- `public_key` — base58-encoded public key with `AccountPublic` token prefix
- `public_key_hex` — raw hex public key
- `key_type` — the resolved algorithm as a string

## Passphrase Entropy Warning

The `estimate_entropy()` function computes [Shannon entropy](https://en.wikipedia.org/wiki/Entropy_(information_theory)) over character frequencies in the passphrase, then multiplies by length to get a total bit estimate, floored to be conservative. If the estimate falls below 80 bits, the response includes a strong warning about brute-force vulnerability. If it equals or exceeds 80 bits, a softer advisory is still emitted, because any deterministic passphrase-to-seed derivation ("brain wallet") is inherently weaker than a truly random seed.

One subtle defensive check prevents spurious warnings: before running the entropy test, the handler compares the raw passphrase string against the seed's own 1751 encoding, base58 form, and hex form. If they match, the user is passing the seed itself — just formatted as a passphrase — not a memorable phrase. In that case, no warning is emitted, since the entropy of a randomly-generated seed is already adequate regardless of how it was transmitted in the request.

The `libSeed` flag also suppresses the warning for XrplLib-detected seeds, since those follow a defined deterministic format rather than being user-invented phrases.

## Error Handling

The handler uses three distinct error paths: `RPC::expected_field_error()` for type mismatches (non-string `key_type`), `rpcError(rpcINVALID_PARAMS)` for unrecognized key type strings, `rpcError(rpcBAD_SEED)` for XrplLib seed / key type conflicts, and an out-parameter `err` Json value populated by `getSeedFromRPC()` for malformed seed inputs. No exceptions are used; all failure paths return early with a `Json::Value` error object.

## Relationship to Test Coverage

`KeyGeneration_test.cpp` exercises `walletPropose()` directly with known constant vectors for both `secp256k1` and `ed25519`, validating all six output fields plus the entropy warning text. The test data (`"REINDEER FLOTILLA"` as a low-entropy passphrase and a high-entropy random-looking passphrase) explicitly covers both warning tiers, giving good confidence in the entropy threshold logic.