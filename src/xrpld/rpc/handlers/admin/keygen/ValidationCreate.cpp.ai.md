# `ValidationCreate.cpp` — Admin RPC Handler for Validator Key Generation

## Role in the System

`ValidationCreate.cpp` implements the `validation_create` admin-only RPC command, which generates a secp256k1 key pair suitable for use as a validator's signing identity on the XRP Ledger. Validators use this key pair to sign validation messages that are broadcast during consensus; the public key appears in Unique Node Lists (UNLs) that operators publish, while the private key (or its seed) is stored in the validator's `rippled.cfg`.

The handler lives alongside `WalletPropose.cpp` in the `keygen/` subdirectory, reflecting a conceptual grouping: both commands produce cryptographic key material from seeds. However, their purposes diverge. `wallet_propose` creates account credentials (account IDs, `secp256k1` or `ed25519`, entropy warnings for passphrases), whereas `validation_create` is narrowly scoped to the validator use case — always secp256k1, always returning `NodePublic`/`NodePrivate` token types.

## The `validationSeed()` Helper

The file-local `validationSeed()` function encapsulates the seed-sourcing decision: if no `secret` field is present in the request params, it calls `randomSeed()` to generate 128 bits of cryptographically secure entropy. If a `secret` is provided, it delegates to `parseGenericSeed()`, which accepts multiple formats — Base58-encoded family seeds, passphrase strings (hashed via SHA512-Half to 128 bits), and legacy RFC 1751 mnemonic word lists. On parse failure, `parseGenericSeed()` returns `std::nullopt`, which propagates back as `std::nullopt` from `validationSeed()`. The `Seed` class itself enforces no-default-construction and securely zeroes its 16-byte buffer in its destructor, ensuring key material does not linger in memory after the `Seed` object is destroyed.

## `doValidationCreate()` — The RPC Entry Point

```
validationSeed → generateSecretKey → derivePublicKey → toBase58 / seedAs1751
```

The main handler follows a simple linear pipeline. After extracting the seed, it immediately gates on the `if (!seed)` check and returns `rpcBAD_SEED` before touching any crypto — preventing downstream code from receiving an invalid state. It then calls `generateSecretKey(KeyType::secp256k1, *seed)` and `derivePublicKey(KeyType::secp256k1, private_key)` to produce the key pair deterministically from the seed.

The four fields returned to the caller reflect different operational needs:

| Field | Encoding | Typical Use |
|---|---|---|
| `validation_public_key` | Base58 `NodePublic` token | Published in UNLs; trusted by peers |
| `validation_private_key` | Base58 `NodePrivate` token | Stored locally; signs validation messages |
| `validation_seed` | Base58 `FamilySeed` token | `[validation_seed]` in `rippled.cfg` (older approach) |
| `validation_key` | RFC 1751 mnemonic | Legacy human-readable backup of the seed |

The `NodePublic` / `NodePrivate` token types encode differently from the `AccountPublic` / `AccountPublic` tokens used by `wallet_propose`, making it structurally impossible to confuse a validator key for an account key at the Base58 layer.

## Why Admin-Only?

The source comment captures it directly: *"This command requires Role::ADMIN access because it makes no sense to ask an untrusted server for this."* In `Handler.cpp`, the command is registered as `Role::ADMIN`. The security implication is that if a non-operator server generated your validator seed, the server could log or return a seed it controls, subverting the validator's identity. Requiring admin access forces the operator to call this command against their own node.

## Contrast with `wallet_propose`

`WalletPropose.cpp` is substantially more complex: it handles `ed25519` vs `secp256k1` selection, detects XrplLib-encoded ed25519 seeds, runs a Shannon-entropy check on passphrases to warn about brain-wallet weakness, and returns account-layer identifiers. `ValidationCreate.cpp` deliberately omits all of that — validator keys always use secp256k1 (the XRPL's canonical validator algorithm) and there is no account ID to derive. The simplicity is intentional; the handler does exactly what a validator bootstrap script needs and nothing more.

## Error Handling

The sole error path is `rpcBAD_SEED`, returned when `parseGenericSeed()` cannot interpret the supplied `secret` string. There is no error for a missing `secret` — that case silently falls through to random key generation, which is the expected no-argument behavior documented in the CLI help text (`validation_create [<seed>|<pass_phrase>|<key>]`). No exceptions are thrown or caught; the `std::optional` return type of `validationSeed()` serves as the error channel.