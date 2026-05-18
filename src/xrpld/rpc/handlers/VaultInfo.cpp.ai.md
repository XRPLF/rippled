# `VaultInfo.cpp` — RPC Handler for Vault and Share Issuance Lookup

This file implements the `doVaultInfo` RPC handler, introduced in XRPL server version 3.1.0 as part of the XLS-66 Lending Protocol. It allows clients to query the current state of a vault ledger object together with its associated MPT (Multi-Purpose Token) share issuance. Vaults are a compound ledger construct: the vault object (`ltVAULT`) carries ownership, configuration, and a reference to a share token ID, while a separate `ltMPTOKEN_ISSUANCE` object tracks the share tokens distributed to vault participants. The handler must therefore locate and assemble two distinct ledger entries before returning a coherent response.

## Vault Identifier Parsing — `parseVault`

The static helper `parseVault` handles the dual addressing scheme for vaults. Clients can locate a vault in two mutually exclusive ways:

1. **Direct lookup** via `vault_id`: a hex-encoded 256-bit ledger object key passed as a string.
2. **Derived lookup** via `owner` + `seq`: the creating account's base58-encoded address and the sequence number from the creating transaction, which together deterministically produce the same 256-bit key via `keylet::vault(accountID, seq)`.

The function enforces strict mutual exclusivity — exactly one combination must be present. Providing all three fields, mixing forms (e.g. `owner` without `seq`), or providing none all trigger `rpcINVALID_PARAMS`. This mirrors the addressing pattern used elsewhere in the XRPL RPC layer for objects that can be referenced by either a canonical ID or a logical key pair, and eliminates any ambiguity about which form takes precedence.

The `seq` validation is deliberately layered. It first checks that the JSON value is an integer or unsigned integer type (rejecting strings and floats), then checks range via `asDouble()`: the value must be positive and must not exceed `Json::Value::maxUInt`. The double-comparison approach is a practical hedge against JSON parsers that may lose integer precision or fail to distinguish zero from a negative float at the type level; it also guards against sequence zero, which is invalid in XRPL.

`parseVault` returns `std::optional<uint256>`, using `std::nullopt` to signal parse failure, but notably it injects the error into `jvResult` before returning rather than leaving that to the caller. The caller in `doVaultInfo` then checks for `beast::zero` as a sentinel after calling `.value_or(beast::zero)`. This is a minor coupling: `beast::zero` technically represents the all-zeros 256-bit value, but in practice XRPL ledger object keys are derived from cryptographic hashes and will never be the zero value, making this sentinel safe.

## Handler Logic — `doVaultInfo`

`doVaultInfo` follows the standard XRPL RPC handler structure: resolve the target ledger via `RPC::lookupLedger`, then operate on an immutable `ReadView const`. The ledger is resolved before vault parsing — if the ledger reference is invalid the handler returns immediately without parsing vault parameters, since there is nothing to look up.

The two-phase ledger read is the structural core of the handler:

1. Read the vault SLE via `keylet::vault(uNodeIndex)`, using the already-resolved 256-bit key (the inline overload from `Indexes.h` that wraps the key as `ltVAULT`).
2. If the vault SLE exists, read the MPT issuance via `keylet::mptIssuance(sleVault->at(sfShareMPTID))`, extracting the share token ID from the vault object itself.

The conditional chain collapses both cases into a single null check: `sleIssuance` is initialized to `nullptr` when `sleVault` is null, so the check `!sleVault || !sleIssuance` handles vault-not-found and orphaned-vault-without-issuance with the same `"entryNotFound"` error. A vault without a corresponding issuance object would represent ledger inconsistency, so treating both absences identically is correct — there is no useful partial response to return.

The response structure embeds the issuance JSON under `vault.shares`, nesting share token metadata directly within the vault object rather than returning both as parallel top-level fields. This reflects the conceptual model of XLS-66: shares are a property of the vault, not a separate entity at the API level. Both the vault SLE and issuance SLE are serialized in full via `getJson(JsonOptions::none)`.

## Relationship to the Broader RPC System

`doVaultInfo` is declared in `Handlers.h` alongside the full set of RPC command handlers and reaches callers via the standard dispatch table, using the same `Json::Value(RPC::JsonContext&)` signature. The handler directory at the time of writing contains relatively few files, making `VaultInfo.cpp` one of the more recent additions. Its dual-identifier addressing pattern and two-object ledger read are somewhat distinctive compared to simpler handlers like `ChannelVerify.cpp`, but both are grounded in the same `lookupLedger` + `ReadView` idiom that characterizes the entire RPC handler layer.