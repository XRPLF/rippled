# `WalletPropose.h` — Wallet Key Generation RPC Declaration

This header declares `walletPropose()`, the core key-generation function powering the XRPL `wallet_propose` admin RPC command. Its single-line declaration belies a deliberate architectural split: the function accepts a raw `Json::Value` parameter set rather than the full `RPC::JsonContext`, allowing the key-generation logic to be called and tested independently from the RPC machinery.

## Role in the System

`wallet_propose` is an admin-restricted RPC command that generates a new XRPL account — a cryptographic key pair and its derived account ID — without submitting anything to the ledger. It lives under `src/xrpld/rpc/handlers/admin/keygen/`, alongside `ValidationCreate.cpp`, grouping all privileged key-material generation in one place. The companion `doWalletPropose()` function (declared in `Handlers.h` and implemented in `WalletPropose.cpp`) is the thin RPC adapter that unwraps the `JsonContext` and delegates straight to `walletPropose(context.params)`.

## The `walletPropose` Function

The function handles three distinct input scenarios:

1. **No seed material provided** — generates a fresh cryptographically random seed via `randomSeed()`.
2. **User-supplied passphrase, seed, or seed_hex** — first attempts to parse as an XrplLib-encoded seed via `RPC::parseXrplLibSeed()`, falling back to the standard `RPC::getSeedFromRPC()` path. XrplLib uses a non-standard encoding for Ed25519 seeds; detecting it early prevents user confusion about key type mismatches.
3. **XrplLib seed detected** — locks the key type to `Ed25519` and returns an error (`rpcBAD_SEED`) if the caller explicitly requested a conflicting algorithm.

After resolving the seed and key type (defaulting to `secp256k1` when unspecified), it calls `generateKeyPair(*keyType, *seed)` and builds the response object with six fields: `master_seed` (Base58), `master_seed_hex`, `master_key` (1751-word mnemonic encoding), `account_id`, `public_key` (Base58), and `public_key_hex`.

## Entropy Warning Design

A notable feature is the passphrase entropy check. When a passphrase is used and it doesn't look like an already-encoded seed (Base58, hex, or 1751 mnemonic), `estimate_entropy()` computes a Shannon-entropy-based bit estimate. Below 80 bits the response includes a strong "vulnerable to brute-force attacks" warning; above it, a softer advisory is added. This is a deliberate user-safety mechanism — "brain wallets" derived from weak passphrases are a known attack vector on blockchain accounts.

## Why This Header Exists

The separation of the bare `walletPropose(Json::Value const&)` signature into its own header — rather than only exposing `doWalletPropose(RPC::JsonContext&)` — allows the key generation logic to be exercised directly in unit tests (see `test/rpc/KeyGeneration_test.cpp`) without constructing a full server context. The header's only dependency is `<xrpl/json/json_value.h>`, keeping the include footprint minimal and the function usable from any layer that can construct a JSON object.