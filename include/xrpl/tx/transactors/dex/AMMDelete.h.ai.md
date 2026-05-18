# `AMMDelete.h` — AMM Deletion Transactor Interface

## Role and Context

`AMMDelete` declares the transactor responsible for removing an Automated Market Maker instance from the XRPL ledger once it has been fully drained. It sits in the `include/xrpl/tx/transactors/dex/` directory alongside the rest of the AMM lifecycle transactors (`AMMCreate`, `AMMDeposit`, `AMMWithdraw`, `AMMBid`, `AMMVote`, `AMMClawback`) and inherits from the common `Transactor` base class.

An AMM on the XRP Ledger consists of several ledger objects: an `ltAMM` entry keyed by the hash of the two asset identifiers, a synthetic `AccountRoot` with a disabled master key (the "AMM account"), and a collection of trustlines (or MPToken objects) representing LP token holdings. When all liquidity has been withdrawn via `AMMWithdraw` and the `sfLPTokenBalance` field on the `ltAMM` object reaches zero, none of those supporting ledger objects have been automatically cleaned up. `AMMDelete` handles that deferred cleanup.

## Transactor Pipeline

`AMMDelete` participates in the three-stage transactor pipeline mandated by the base class:

**`checkExtraFeatures`** enforces two amendment preconditions: the base AMM feature must be enabled (`ammEnabled`), and if either asset in the pair is an MPT (Multi-Purpose Token), the `featureMPTokensV2` amendment must also be live. This gating is notable because it prevents deletion of MPT-based AMM pools in network versions that don't fully support MPT cleanup, avoiding partial or inconsistent ledger state even if such a pool somehow reached zero balance.

**`preflight`** is deliberately trivial — it returns `tesSUCCESS` immediately. All amendment checks are already handled by `checkExtraFeatures`, and there is no field-level validation of the transaction payload that can be done without consulting ledger state. This is the correct design given the XRPL constraint that `preflight` must be purely stateless: it receives only rules, flags, and the raw transaction object.

**`preclaim`** performs the real gate-check against the current ledger: it reads the `ltAMM` object for the submitted asset pair and verifies that `sfLPTokenBalance` is exactly zero. If the AMM doesn't exist it returns `terNO_AMM`; if it still holds LP tokens it returns `tecAMM_NOT_EMPTY`. Both failures prevent fee collection (`ter*` codes are soft failures that do not charge fees, while `tec*` codes do). The balance check here is important: it prevents a user from using `AMMDelete` as a shortcut to destroy a pool that still has liquidity — the proper drain path is `AMMWithdraw`.

## Incremental Deletion in `doApply`

The most architecturally significant aspect of `AMMDelete` is its chunked deletion model. `doApply()` wraps all work in a `Sandbox` (an isolated copy of the ledger view) and delegates to `deleteAMMAccount()` from `AMMHelpers`. That helper deletes AMM trustlines up to the constant `maxDeletableAMMTrustLines` (512) per invocation. If trustlines remain after the cap is reached, `deleteAMMAccount` returns `tecINCOMPLETE` rather than `tesSUCCESS`.

Crucially, `doApply` applies the sandbox to the raw view on **both** `tesSUCCESS` and `tecINCOMPLETE`:

```cpp
if (isTesSuccess(ter) || ter == tecINCOMPLETE)
    sb.apply(ctx_.rawView());
```

This means partial progress — the deletion of up to 512 trustlines — is committed to the ledger even when the job isn't finished. The `tec` return code causes the transaction to succeed (charging the submitter a fee) while signaling incompleteness. The caller must re-submit `AMMDelete` until the pool is fully removed.

This incremental approach exists to respect the per-transaction ledger-modification limits. An AMM that has been active for a long time might accumulate hundreds or thousands of LP token trustlines from participants who never withdrew. Attempting to delete all of them atomically in a single transaction could exhaust ledger traversal limits or impose unbounded computational cost. The 512-entry cap bounds worst-case per-transaction work while still making guaranteed progress each round.

After all trustlines are cleared, `deleteAMMAccount` also removes any MPToken objects (for MPT-based pools), erases the `ltAMM` entry from the owner directory, and finally erases both the `ltAMM` object and the AMM `AccountRoot` itself.

## Relationship to Other Transactors

`AMMDelete` shares the pattern of `ConsequencesFactory{Normal}`, the same as every other AMM transactor. This tells the consequences system that the transaction is neither a `Blocker` (which would prevent later transactions from the same account in a batch) nor a `Custom` cost calculator. The constructor simply forwards `ApplyContext` to the base `Transactor`, with no additional state needed beyond what the base class provides.

The `deleteAMMAccount` helper is also called from `AMMWithdraw` in the full-withdrawal path, meaning `AMMDelete` is essentially the cleanup fallback for pools that were fully drained but whose accounts weren't automatically removed at withdrawal time — or where the auto-removal path failed due to the same trustline-count constraints.