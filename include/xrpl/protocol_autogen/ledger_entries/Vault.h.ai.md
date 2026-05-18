# `Vault.h` — Auto-generated Vault Ledger Entry Wrapper

## Role in the System

This file is auto-generated (editing is prohibited) and provides two companion classes for the `ltVAULT` ledger entry type (type code `0x0084`): a read-only wrapper `Vault` and a fluent builder `VaultBuilder`. It lives in the `xrpl::ledger_entries` namespace alongside every other ledger entry type in the `protocol_autogen/ledger_entries/` directory, forming a layer of ergonomic, type-safe C++ API over the raw `SLE` (Serialized Ledger Entry) infrastructure.

The Vault entry represents a DeFi-style tokenized yield vault on the XRP Ledger. Depositors receive Multi-Purpose Token (MPT) shares in exchange for depositing a designated asset; the vault tracks total, available, and maximum asset balances along with unrealized losses, and enforces a configurable withdrawal policy. The vault's share token is identified by a 192-bit `sfShareMPTID` linking it to an `MPTokenIssuance` object elsewhere on the ledger.

## Class Design: Immutable Wrapper + Fluent Builder

The split between `Vault` and `VaultBuilder` enforces a clear immutability boundary. `Vault` extends `LedgerEntryBase` and holds a `std::shared_ptr<SLE const>`, making every accessor a `const` operation. It is not constructible empty and cannot mutate the underlying entry. `VaultBuilder` extends the CRTP base `LedgerEntryBuilderBase<VaultBuilder>`, which accumulates field values into an internal `STObject object_{sfLedgerEntry}` and delegates `setFlags()`/`setLedgerIndex()` to the base — again without ever touching an `SLE` until `build()` is explicitly called.

The reason `LedgerEntryBuilderBase` deliberately avoids calling `object_.set(soTemplate)` is captured in a comment: pre-setting `soeDEFAULT` placeholders causes the `SLE` constructor's `applyTemplate()` to throw "may not be explicitly set to default." The builder accumulates only the fields the caller sets; the `SLE` construction at `build()` time then fills in defaults correctly.

## Field Access Patterns

Required fields (`soeREQUIRED`) — `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`, `sfSequence`, `sfOwnerNode`, `sfOwner`, `sfAccount`, `sfAsset`, `sfShareMPTID`, `sfWithdrawalPolicy` — are exposed as simple getters that dereference directly through `sle_->at(sf...)` with no null check; callers may rely on them always being present for a validly constructed entry.

Optional and defaultable fields use the paired `has*()`/`get*()` pattern. The getters return `protocol_autogen::Optional<T>`, a small type alias defined in `Utils.h`:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This handles the case where `sle_->at(sf...)` returns an internal reference (e.g., for blob fields) by wrapping it in `std::reference_wrapper` rather than a dangling `std::optional<T&>`. The caller checks the `has*()` variant before dereferencing. Fields in this category are `sfData` (`soeOPTIONAL`), `sfAssetsTotal`, `sfAssetsAvailable`, `sfAssetsMaximum`, `sfLossUnrealized`, and `sfScale` (all `soeDEFAULT`).

## Asset Tracking with `SF_NUMBER`

The balance fields (`sfAssetsTotal`, `sfAssetsAvailable`, `sfAssetsMaximum`, `sfLossUnrealized`) use `SF_NUMBER`, whose underlying type is `STNumber`. As documented in `STNumber.h`, `STNumber` is an `STAmount` without embedded asset information — it stores only the numeric value and acquires its token-type context (XRP, IOU, or MPT) at runtime via `associateAsset()`. For vault entries, all four fields represent amounts of the vault's `sfAsset`, so the asset association must be established before arithmetic operations during transaction processing. This design avoids duplicating the asset descriptor in every balance field at the cost of requiring explicit association before use.

## The `sfOwner` / `sfAccount` Distinction

The vault holds two account-typed fields. `sfOwner` is the human account that created the vault and holds administrative control over it. `sfAccount` is the vault's own pseudo-account on the ledger — on XRPL, vaults are represented as first-class ledger accounts to support direct balance holding and trust line interactions. This two-account structure mirrors the approach used by Automated Market Makers and bridges.

## Builder Constructors

`VaultBuilder` exposes two constructors. The primary constructor accepts all nine required fields explicitly, sets them immediately, and is the canonical path for creating new vault entries. The secondary constructor accepts an existing `std::shared_ptr<SLE const>` and reconstitutes the builder from it (copying the SLE data into `object_`), which supports mutation-via-copy workflows. Both constructors validate the `sfLedgerEntryType` and throw `std::runtime_error` on a mismatch — the same guard the `Vault` reader applies in its own constructor after moving the shared pointer into the base.

The `setAsset()` setter has one notable divergence from the other setters: it wraps the value in `STIssue(sfAsset, value)` before assigning it into `object_`. This is necessary because `sfAsset` is typed as `SF_ISSUE`, whose serialization expects an `STIssue` wrapper rather than a raw `Issue` value type.

`build()` finalizes construction by moving `object_` into a freshly allocated `SLE`, binding it to the caller-supplied `uint256` index, and returning a fully constructed `Vault` wrapper. The index is the entry's key in the ledger state map and is computed externally (typically via `keylet::vault()`), keeping the builder free of key-derivation logic.

## Withdrawal Policy and Scale

`sfWithdrawalPolicy` is a `uint8_t` required field. The only currently defined value is `vaultStrategyFirstComeFirstServe = 1` (from `Protocol.h`), meaning withdrawals are honored in submission order as long as liquidity is available. `sfScale` is a `uint8_t` defaultable field that governs the decimal precision of IOU-denominated shares, defaulting to `vaultDefaultIOUScale = 6` and capped at `vaultMaximumIOUScale = 18` — the cap is chosen so that 1 IOU can always be converted to at least one MPToken share given the maximum MPToken supply of 2⁶⁴ − 1.