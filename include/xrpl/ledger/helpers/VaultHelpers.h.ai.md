# `VaultHelpers.h` — Vault Asset-to-Share Conversion Utilities

This header belongs to the XRPL ledger's Single-Sided Vault feature (XLS-65d). It provides four pure arithmetic functions that translate between the two token types a vault deals with at all times: the underlying *asset* (XRP, IOU, or MPT that depositors contribute) and the *shares* (an MPT issued by the vault representing proportional ownership). Because MPTokens are always integers, every function in this file must make a deliberate rounding decision — and those decisions differ between the deposit path and the withdrawal path in ways that protect the vault's solvency.

## The Model: Proportional Vault Shares

A vault accumulates assets deposited by its participants and mints share tokens proportionally. The on-ledger `Vault` SLE tracks three key numeric fields that all four functions depend on:

- `sfAssetsTotal` — the total asset value committed to the vault (including assets currently lent out),
- `sfLossUnrealized` — unrealized losses not yet reflected in `sfAssetsTotal`, and
- `sfScale` — a `uint8` that sets the initial exchange ratio when the vault is bootstrapped from empty.

The MPTokenIssuance SLE for the vault's shares provides `sfOutstandingAmount`, the total shares currently in circulation.

The core invariant across all calculations is the proportional exchange: one share is worth `assetsTotal / sharesOutstanding` assets, or equivalently, depositing `x` assets against `assetsTotal` earns `x × sharesOutstanding / assetsTotal` shares.

## Deposit Functions: Ignoring Unrealized Losses

`assetsToSharesDeposit()` and `sharesToAssetsDeposit()` implement the deposit-direction conversions. Both read `sfAssetsTotal` directly, *without* subtracting `sfLossUnrealized`. This is intentional: a depositor pays into the vault based on its full committed assets, including those pledged to lending arrangements. Unrealized losses are a risk borne by existing shareholders, not one that new depositors should get a discount for.

`assetsToSharesDeposit()` handles the empty-vault bootstrap case specially. When `sfAssetsTotal == 0`, no outstanding shares exist and there is no ratio to apply. Instead, the initial share price is established via `sfScale`: the result is `assets × 10^scale`, computed using mantissa/exponent arithmetic on `STAmount` before calling `.truncate()`. This keeps the initial share-to-asset ratio tunable at vault creation. The non-bootstrap path uses the proportion `(shareTotal × assets) / assetTotal`, always truncated because shares must be integral MPT values — depositors always receive a whole number of shares, never more than the assets strictly warrant.

`sharesToAssetsDeposit()` is the inverse: given a whole number of shares, it computes the exact asset cost. The vault deposit transactor (`VaultDeposit::doApply()`) uses these two functions in sequence: first determine how many shares are created (truncated), then back-calculate the true asset cost from those shares. The safety check `if (*maybeAssets > amount) return tecINTERNAL` ensures the vault never extracts more assets than the depositor offered — a consequence of the truncation in the forward direction.

## Withdrawal Functions: Accounting for Unrealized Losses

`assetsToSharesWithdraw()` and `sharesToAssetsWithdraw()` differ from their deposit counterparts in one critical way: both subtract `sfLossUnrealized` from `sfAssetsTotal` before computing the exchange rate. If the vault has recorded unrealized losses — for instance from a lending arrangement gone underwater — withdrawers receive fewer assets per share, reflecting the actual net asset value. This prevents early withdrawers from exiting at inflated prices and passing losses entirely to those who remain.

`assetsToSharesWithdraw()` takes an optional `TruncateShares` enum parameter (default `TruncateShares::no`). When the caller asks for a fixed asset withdrawal, it needs to know how many shares to redeem. With default rounding (nearest), the resulting share count may round up, ensuring the vault is never shortchanged. The vault withdraw transactor then back-calculates actual assets from that share count via `sharesToAssetsWithdraw()`, so the withdrawer receives a precise amount. The `TruncateShares::yes` variant exists for callers that explicitly want floor behavior rather than rounding.

If `sfAssetsTotal - sfLossUnrealized` is zero, both withdraw functions return a zero-valued `STAmount` rather than dividing by zero. This gracefully handles a fully-insolvent vault.

## The `TruncateShares` Enum

The header defines a scoped `enum class TruncateShares : bool`. Using a named enum rather than a bare `bool` prevents the classic boolean parameter legibility problem — call sites read `assetsToSharesWithdraw(vault, issuance, amount, TruncateShares::yes)` unambiguously. This is the only configurable parameter; all other rounding decisions are fixed by the deposit/withdraw semantic.

## Error Handling and `[[nodiscard]]`

All four functions are marked `[[nodiscard]]` and return `std::optional<STAmount>`. They return `std::nullopt` on precondition violations: negative input amounts, or an asset type mismatch between the provided amount and what the vault expects. Both conditions are guarded by `XRPL_ASSERT` macros and a hard conditional check in release builds — the assertions communicate invariants to developers, while the `if`-guard + `return nullopt` provides deterministic error propagation rather than undefined behavior. Callers are expected to treat `nullopt` as an internal error (they emit `tecINTERNAL` in the transactors).

Arithmetic can also throw `std::overflow_error` from XRPL's `Number` class, especially when `sfScale` is large. Both transactors catch this and map it to `tecPATH_DRY`, with a debug-level log to avoid flooding under adversarial input.

## Relationship to Callers

`VaultDeposit.cpp` and `VaultWithdraw.cpp` are the only callers. The deposit transactor uses the deposit-path pair; the withdraw transactor uses both withdraw-path functions together in a two-step computation. The functions are deliberately stateless and side-effect-free: they read from already-fetched `shared_ptr<SLE const>` objects and perform arithmetic only, leaving all ledger mutations to the transactors. This separation makes the rounding logic independently testable and reusable if additional vault transaction types are introduced.