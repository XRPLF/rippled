# `VaultDeposit.h` — Auto-Generated Vault Deposit Transaction Wrapper

This file is part of the `protocol_autogen` layer in the XRPL codebase — a set of auto-generated headers that expose every ledger transaction type as a pair of C++ classes: an immutable read wrapper and a mutable builder. `VaultDeposit.h` covers transaction type `ttVAULT_DEPOSIT` (type code 68), which deposits assets into a Single Asset Vault created by the `featureSingleAssetVault` amendment.

## Role in the Vault Transaction Family

Five sibling files in the same directory — `VaultCreate.h`, `VaultSet.h`, `VaultDelete.h`, `VaultWithdraw.h`, and `VaultClawback.h` — together form the complete lifecycle surface for on-ledger vaults. A vault is a yield-bearing container for a single asset type; accounts deposit assets and receive share tokens (MPTs) back proportional to the pool, then redeem those shares on withdrawal. `VaultDeposit` handles the ingress side: an account sends assets in and the ledger mints vault share tokens to them.

The transaction carries only two required fields: `sfVaultID` (a 256-bit hash identifying the target vault ledger object) and `sfAmount` (the quantity being deposited). Compared with `VaultWithdraw`, which adds optional `sfDestination` and `sfDestinationTag` to route redeemed assets elsewhere, `VaultDeposit` has no routing fields — the deposited assets go into the vault and the share tokens are credited back to the sending account.

The transaction is marked `notDelegable`, meaning it cannot be submitted via an `sfDelegate` account acting on another's behalf, and it carries the `mayAuthorizeMPT | mustModifyVault` privilege flags, which the XRPL transaction processor uses to decide which ledger objects to lock and which capabilities to pre-authorize.

## `VaultDeposit` — Immutable Read Wrapper

`VaultDeposit` extends `TransactionBase` and holds a `std::shared_ptr<STTx const>` — the `const` qualifier on the `STTx` being load-bearing. Once a transaction is wrapped, neither the pointer nor the underlying object can be mutated; all getters return by value. This prevents accidental modification after signing and makes the wrapper safe to pass and copy freely in contexts like validation, transaction replay, or fee estimation.

The constructor receives an already-constructed `STTx` by `shared_ptr`, forwards it to `TransactionBase`, and then validates the transaction type against the compile-time `txType` constant. The validation reads from `tx_` rather than the local `tx` parameter because the `std::move` happens in the base class initialiser — by the time the body executes, the original `tx` pointer is empty and `tx_` holds the value. The `std::runtime_error` thrown on type mismatch is intentionally unchecked at the call site in most usage, because constructing a `VaultDeposit` from anything other than a vault deposit transaction is a programming error.

`getVaultID()` returns an `SF_UINT256::type::value_type` — the concrete C++ type (`uint256`) that the serialized field system maps to this field kind. `getAmount()` returns an `SF_AMOUNT::type::value_type`, which can carry XRP drops, IOU amounts, or MPT amounts. The note that this field "supports MPT" is significant: vault shares are themselves MPTs, and a deposit can potentially name an MPT as the deposited asset if the vault was created with an MPT asset type.

## `VaultDepositBuilder` — Fluent Construction

`VaultDepositBuilder` inherits from `TransactionBuilderBase<VaultDepositBuilder>` via CRTP. The template parameter `Derived = VaultDepositBuilder` allows all setters in the base class to return `Derived&` through a `static_cast`, preserving the concrete type through method chains without virtual dispatch. This means callers can write:

```cpp
VaultDepositBuilder builder(account, vaultID, amount, seq, fee);
builder.setFlags(tfSomeFlag).setLastLedgerSequence(lls);
auto tx = builder.build(pubKey, secKey);
```

and each call in the chain remains typed as `VaultDepositBuilder&`, not the base.

The constructor deliberately avoids calling `object_.set(soTemplate)` on the internal `STObject`. As the base class comment explains, doing so would pre-populate default-value placeholders for `soeDEFAULT` fields, which then causes `applyTemplate()` inside the `STTx` constructor to throw "may not be explicitly set to default." By keeping `object_` as a free `STObject` and letting `STTx` apply the template itself at build time, optional fields that weren't set are handled cleanly.

The alternative constructor `VaultDepositBuilder(std::shared_ptr<STTx const> tx)` round-trips an existing transaction back into mutable form by copying the `STTx` content into `object_`. This is useful for test fixtures or mutation-based testing patterns where a valid signed transaction needs to be tweaked and re-signed.

`build()` calls the protected `sign()` method (which serialises the object under `HashPrefix::txSign`, signs it, and embeds the signature and public key), then wraps the resulting `STTx` in a `VaultDeposit`. Calling `build()` consumes the internal `STObject` via `std::move`, so the builder is in a valid-but-unspecified state afterwards and should not be reused.

## Auto-Generation Notes

The `// This file is auto-generated. Do not edit.` header signals that the source of truth is a schema or code-generation script, not this file. All Vault* transaction headers follow an identical structural pattern, and differences between them are purely in their field lists. This design ensures that adding or removing fields from a transaction type requires only updating the generator input, not manually maintaining parallel C++ class bodies.