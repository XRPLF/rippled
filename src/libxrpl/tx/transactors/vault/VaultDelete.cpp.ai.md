# `VaultDelete.cpp` — Vault Teardown Transaction

`VaultDelete.cpp` implements the three-phase transactor lifecycle — `preflight`, `preclaim`, and `doApply` — for the `VaultDelete` transaction type. Its purpose is to completely dismantle a vault from the XRPL ledger, reclaiming all associated objects and adjusting owner reserves accordingly.

## What a Vault Is

To understand why deletion is non-trivial, consider what `VaultCreate` assembles. A vault is not a single ledger entry; it is a cluster of objects:

- A **vault SLE** (`keylet::vault(...)`) that records ownership, asset type, share MPT identifier, and running asset totals.
- A **pseudo-account SLE** (`keylet::account(pseudoID)`) — a synthetic account address that actually holds the vault's underlying assets. This pseudo-account owns no private key; it exists solely to hold trust lines or MPT balances on behalf of depositors.
- An **`MPTokenIssuance` SLE** recording the vault's share token, issued by the pseudo-account at sequence 1. This is what depositors receive when they deposit assets.
- Optionally, an **`MPToken` SLE** on the vault owner's account if they themselves hold shares.

When `VaultCreate` runs, it calls `adjustOwnerCount(view(), owner, 2, j_)` — one count for the vault, one for the pseudo-account, both charged to the real owner's reserve. `VaultDelete` must invert all of this.

## Validation Phases

`preflight` is deliberately minimal: it only checks that `sfVaultID` is not `beast::zero`. This is the only stateless check possible — the vault's actual state lives in the ledger.

`preclaim` performs the substantive gate-keeping against the read-only ledger view. It enforces several invariants, all of which return `tecHAS_OBLIGATIONS` if violated:

- **`sfAssetsAvailable == 0`** and **`sfAssetsTotal == 0`** — these two fields are checked separately because a vault can carry unrealized losses where total differs from available. Both must be zero before deletion is allowed; any assets must be fully withdrawn first.
- **`sfOutstandingAmount == 0`** on the `MPTokenIssuance` — no depositor may still hold vault shares. This is the share-ledger counterpart to the asset checks: even if assets are zero, an outstanding share balance would indicate an unresolved obligation.

The ownership check (`vault->at(sfOwner) != ctx.tx[sfAccount]`) ensures only the vault creator can trigger deletion. The two `MPTokenIssuance` checks (existence and issuer match) are guarded by `LCOV_EXCL_START` because they guard against ledger invariant violations that cannot arise through normal transaction flow — they are reachable only if prior transactions corrupted ledger state.

## Teardown Order in `doApply`

The destruction sequence in `doApply` follows a strict dependency order:

**Step 1 — Remove the asset holding.** `removeEmptyHolding(view(), vault->at(sfAccount), asset, j_)` removes whatever the pseudo-account used to hold the underlying asset — a trust line (`RippleState`), or an `MPToken`. The holding must be empty (zero balance) by this point; `preclaim` has already verified `sfAssetsTotal == 0`. This call also removes the object from the pseudo-account's owner directory, decrementing its owner count.

**Step 2 — Remove the vault owner's MPToken for shares.** If the vault creator holds an `MPToken` for the share issuance (`keylet::mptoken(shareMPTID, account_)`), it is cleaned up via a second `removeEmptyHolding` call. The vault owner's share balance must already be zero — `preclaim` verified `sfOutstandingAmount == 0` across all holders.

**Step 3 — Remove the `MPTokenIssuance`.** Rather than delegating to the `MPTokenIssuanceDestroy` transaction (which carries fee logic and extra checks irrelevant here), `doApply` directly removes the issuance from the pseudo-account's owner directory via `view().dirRemove(...)`, calls `adjustOwnerCount(view(), pseudoAcct, -1, j_)`, then erases the SLE. The comment explicitly notes this bypass: *"Do not use MPTokenIssuanceDestroy for this, no special logic needed."*

**Step 4 — Verify the pseudo-account is clean.** After the above removals, the pseudo-account's owner directory should be empty. The code explicitly checks `view().peek(keylet::ownerDir(pseudoID))` and returns `tecHAS_OBLIGATIONS` if the directory still exists — this is a defensive invariant check. It also verifies that `sfBalance` is zero and `sfOwnerCount` is zero before erasing the pseudo-account. These checks are all `LCOV_EXCL_LINE`-guarded because they should be unreachable in valid ledger state, but their presence prevents silent corruption if they are somehow reached.

**Step 5 — Remove the vault from the real owner's directory and erase all remaining SLEs.** The vault itself is removed from the real owner's `ownerDir`, then `adjustOwnerCount(view(), owner, -2, j_)` fires — the single `-2` adjustment is the exact inverse of `VaultCreate`'s `+2`, accounting for both the vault SLE and the pseudo-account that was just destroyed. Finally, the vault SLE is erased.

## Error Code Semantics

The distinction between `tec*` and `tef*` codes is meaningful here. Errors in `preclaim` that a submitter could reasonably encounter — wrong owner, nonzero assets, outstanding shares — return `tec` codes (transaction engine codes), which consume the transaction fee. Errors in `doApply` that indicate impossible ledger states — missing pseudo-account, balance on the pseudo-account, nonzero owner count — return `tef` (transaction engine failure) or `tefBAD_LEDGER`, signalling an internal inconsistency rather than a user-correctable condition.

The mid-apply check `view().peek(keylet::ownerDir(pseudoID))` returning `tecHAS_OBLIGATIONS` is notable: it is the one `tec` code in `doApply`, which the comment marks as `LCOV_EXCL_LINE`. It exists because a future ledger feature could attach additional objects to the pseudo-account's directory; the check is a forward-safety valve to prevent destroying a pseudo-account that still owns unhandled objects.