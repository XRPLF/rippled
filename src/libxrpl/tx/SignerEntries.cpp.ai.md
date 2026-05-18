# `SignerEntries.cpp` — Deserializing Multi-Signer Lists from Wire and Ledger Data

This file provides the single implementation point for `SignerEntries::deserialize()`, a static factory method that extracts a validated signer list from an `STObject`. The code is deliberately small: the complex policy decisions about what a legal signer list looks like live in callers such as `SignerListSet` and `XChainBridge`; this layer is concerned only with structural correctness and data extraction.

## What Problem This Solves

XRPL's multi-signing feature allows an account to be controlled by a weighted quorum of up to 32 co-signers, each identified by their account address, a signing weight, and an optional routing tag (`sfWalletLocator`). This signer list appears in two different contexts: embedded in a `SignerListSet` transaction being submitted to the network, and stored directly in a ledger state entry (`SLE`) after the transaction is applied. `deserialize()` handles both cases identically, with the `annotation` parameter (`"transaction"` or `"ledger"`) injected into trace-level log messages so that a journal entry names its source.

## The `SignerEntry` Struct and Its Comparison Semantics

Defined in the header, `SignerEntry` bundles three fields: `AccountID account`, `std::uint16_t weight`, and `std::optional<uint256> tag`. The `operator<` and `operator==` implementations compare **only `account`** and ignore weight and tag entirely. This is not an oversight — callers such as `SignerListSet` sort the resulting vector by `account` and then scan for adjacent duplicates using `std::adjacent_find`. The weight is irrelevant to identity; two `SignerEntry` objects are "the same signer" regardless of their weights.

The `SignerEntries` class itself has an explicitly deleted default constructor, making it a pure namespace-style container. There is no object to create; the only entry point is the static `deserialize()` method.

## The `deserialize()` Function

```cpp
Expected<std::vector<SignerEntries::SignerEntry>, NotTEC>
SignerEntries::deserialize(STObject const& obj, beast::Journal journal, std::string_view annotation)
```

The return type is `Expected<T, NotTEC>` — the XRPL codebase's pre-C++23 analogue of `std::expected`. `NotTEC` is a restricted TER subset that excludes `tec`-class codes, which matter here because this function can be called from `preflight` — before signature checking — where returning a `tec` would be a security problem (it would allow fee-burning without a valid signature). Returning `temMALFORMED` via `Unexpected(temMALFORMED)` correctly indicates a client error that should be rejected immediately.

The function performs two structural guards before extracting data:

1. **Presence check**: If `sfSignerEntries` is absent from the incoming `STObject`, the object is malformed. The journal logs the annotation text so developers know whether the bad object came from a transaction or from the ledger.

2. **Type check per element**: The `sfSignerEntries` field is an `STArray` — an ordered list of `STObject` elements. Each element's field name (retrieved via `getFName()`) must equal `sfSignerEntry`. If something else appears in that array slot, the whole object is rejected.

After passing those guards, each `STObject` in the array yields three fields via straightforward `STObject` accessors: `sfAccount` as an `AccountID`, `sfSignerWeight` as `uint16_t`, and `sfWalletLocator` as `std::optional<uint256>` using the tilde-prefix optional-field accessor (`~sfWalletLocator`). No additional per-field validation is done here — weight-range checks, account-validity checks, and the prohibition on signing one's own account all happen in the callers.

The vector is pre-allocated with `reserve(STTx::maxMultiSigners)`, where `maxMultiSigners` is the protocol constant 32. This avoids repeated heap allocations during iteration without over-allocating in the common case.

## Call Sites and Their Error Mapping

`SignerListSet.cpp` calls `deserialize()` with `(tx, j, "transaction")` during `preflight`, checks the `Expected` result, and propagates the `NotTEC` error directly if deserialization fails. It then sorts the returned vector and runs duplicate-account detection — work that `deserialize()` deliberately leaves to the caller.

`XChainBridge.cpp` calls `deserialize()` with `(*sleS, j, "ledger")` when reading an account's signer list from a ledger state entry. Here a failure maps to `tecINTERNAL` rather than being propagated directly, because a corrupted on-ledger signer list represents an internal consistency problem rather than a client input error.

The symmetric handling of both sources in a single function is the key architectural choice: it prevents the field-extraction logic from diverging between the two contexts over time.