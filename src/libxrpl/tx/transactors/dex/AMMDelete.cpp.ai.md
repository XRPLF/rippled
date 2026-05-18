# `AMMDelete.cpp` — AMM Account Cleanup Transactor

## Role in the System

`AMMDelete.cpp` implements the `AMMDelete` transactor, which handles the on-ledger transaction that permanently removes an Automated Market Maker pool from the XRP Ledger. An AMM pool consists of a synthetic account object (`ltAMM` SLE), a dedicated root account entry, zero or more LP-token trustlines held by former liquidity providers, and optionally MPToken objects for Multi-Purpose Token pools. All of these ledger entries must be reclaimed once a pool has been drained to zero. This transactor is the mechanism that performs that reclamation.

The file is compact — just four methods, each occupying its designated phase in the XRPL transactor lifecycle — but it orchestrates a non-trivial multi-transaction deletion protocol through its delegation to `deleteAMMAccount()` in `AMMHelpers`.

## Transactor Lifecycle

XRPL transactors decompose processing into distinct phases, each evaluated under progressively stronger ledger access.

**`checkExtraFeatures`** is a static feature-gate, called before `preflight`, that decides whether this transaction type is even permissible under the current amendment ruleset. Two conditions must both hold: the core AMM amendment must be active (`ammEnabled(ctx.rules)`), and — crucially — if the `featureMPTokensV2` amendment is not yet live, neither `sfAsset` nor `sfAsset2` may carry an `MPTIssue`. This prevents MPT-backed AMM pools from being submitted before the network is ready to reason about them, guarding against nodes that support the transaction format but not the full MPT semantics.

**`preflight`** is intentionally empty, returning `tesSUCCESS` unconditionally. All syntactic validation for the transaction fields (`sfAsset`, `sfAsset2`) is handled by the framework-level field validators and by `checkExtraFeatures`; there is nothing left to check in a fee-free, ledger-agnostic context.

**`preclaim`** reads the ledger before any state changes to enforce two hard prerequisites. First, it resolves the AMM ledger entry via `keylet::amm(sfAsset, sfAsset2)`. If no such entry exists the transaction fails immediately with `terNO_AMM` — a retriable error code signalling an invalid precondition rather than a malformed transaction. Second, and more importantly, it reads `sfLPTokenBalance` off the AMM SLE and rejects with `tecAMM_NOT_EMPTY` if any LP tokens remain outstanding. This is the fundamental invariant: deletion is only permitted once all liquidity providers have withdrawn their shares and the pool holds no obligations. There is no way to force-close a pool that still has external holders.

**`doApply`** performs the actual ledger mutations, working against a `Sandbox` — a copy-on-write view layered on top of the current ledger state. The sandbox ensures that partial work can be discarded if anything goes wrong, and that changes are only committed atomically via `sb.apply(ctx_.rawView())`.

## Chunked Deletion and `tecINCOMPLETE`

The most architecturally significant design decision in this file is how `doApply` handles the return value from `deleteAMMAccount`:

```cpp
auto const ter = deleteAMMAccount(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], j_);
if (isTesSuccess(ter) || ter == tecINCOMPLETE)
    sb.apply(ctx_.rawView());
return ter;
```

`deleteAMMAccount` (in `AMMHelpers.cpp`) deletes up to `maxDeletableAMMTrustLines` (512) trustlines per call. A popular AMM pool may accumulate many thousands of LP-token trustlines from holders who exited their positions without explicitly closing the trustline. If more than 512 remain when deletion is attempted, `deleteAMMAccount` returns `tecINCOMPLETE` rather than blocking the transaction entirely.

The key design insight is that `tecINCOMPLETE` is treated as a *partial commit*: `doApply` calls `sb.apply` even when it receives `tecINCOMPLETE`. Up to 512 trustlines are deleted, those changes are written to the ledger, and the transaction succeeds in a partial sense — the submitter pays the transaction fee. Because `preclaim` already confirmed zero LP token balance, the AMM is guaranteed to be draining toward zero without the possibility of new liquidity entering. Anyone can resubmit the `AMMDelete` transaction repeatedly until all trustlines are gone and the AMM SLE and root account are finally erased, at which point `deleteAMMAccount` returns `tesSUCCESS`.

This incremental approach is necessary because XRPL transactions must complete within bounded ledger-close time; a single transaction cannot iterate over an unbounded number of ledger objects without risking consensus timeouts or excessive resource consumption.

## Relationship to `AMMHelpers`

The actual deletion sequence — iterating the AMM account's owner directory, deleting trustlines, then deleting MPToken objects, then removing the directory and erasing the AMM SLE and account SLE — all lives in `deleteAMMAccount` and its helpers (`deleteAMMTrustLines`, `deleteAMMMPTokens`). `AMMDelete.cpp` is intentionally thin: it owns only the three-phase validation logic and the `Sandbox`/commit pattern. This separation means the same deletion logic can be reused by other transactors (e.g., `AMMWithdraw` can trigger cleanup when the pool reaches zero) without duplicating the ledger-mutation code.

## Error Handling Notes

`deleteAMMAccount` internally guards against impossible states — missing AMM account, non-zero trustline balances, unexpected SLE types — by returning `tecINTERNAL`, which are marked `LCOV_EXCL` as unreachable under normal operation. Those paths represent invariant violations that should never occur given correct preceding validation in `preclaim` and correct operation of the withdrawal transactors that zero-out balances before trustlines are deleted.