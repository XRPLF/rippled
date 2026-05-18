# `VaultWithdraw.h` — Vault Withdrawal Transactor Declaration

`VaultWithdraw` is the transaction transactor that handles the `VaultWithdraw` ledger transaction on the XRP Ledger. It sits inside the vault subsystem alongside `VaultCreate`, `VaultDeposit`, `VaultSet`, `VaultDelete`, and `VaultClawback`, collectively implementing the on-ledger single-sided vault (yield-bearing pool) feature.

## Role and Context

A vault in XRPL is a pseudo-account that holds a pool of assets and issues fungible MPT-based shares to depositors in proportion to their contribution. `VaultWithdraw` is the inverse of `VaultDeposit`: it accepts either a fixed asset amount or a fixed share amount from the submitter, computes the complementary quantity via the vault's share-to-asset exchange rate, burns the shares, and credits the underlying assets to a destination account.

The header itself is deliberately thin — it declares the class and its three pipeline methods. All logic lives in `VaultWithdraw.cpp`.

## Transactor Pipeline

Like every XRPL transactor, `VaultWithdraw` is processed through a mandatory three-phase pipeline orchestrated by `Transactor::invokePreflight` and the broader apply machinery:

**`preflight(PreflightContext const& ctx)`** runs before any ledger state is consulted. It validates the raw transaction fields: a zero `sfVaultID` is rejected as `temMALFORMED`, a non-positive `sfAmount` returns `temBAD_AMOUNT`, and a zero explicit `sfDestination` is also malformed. This phase is intentionally cheap — no disk reads, no ledger lookups.

**`preclaim(PreclaimContext const& ctx)`** is a read-only pass against an immutable `ReadView`. It resolves the vault object and checks:
- The vault exists (`tecNO_ENTRY` otherwise).
- The requested `sfAmount` is denominated in either the vault's underlying asset or its share MPT — any other asset fails with `tecWRONG_ASSET`.
- The underlying asset is transferable to the destination (`canTransfer` check).
- The vault's withdrawal policy is `vaultStrategyFirstComeFirstServe` (the only supported policy; any other triggers `tefINTERNAL` as an invariant violation).
- When the `fixSecurity3_1_3` amendment is active and the amount is share-denominated, the shares are first converted to an equivalent asset amount before running the withdrawal limit check via `canWithdraw`. This plugged a pre-amendment gap where share-denominated withdrawals bypassed limit enforcement entirely. Overflow errors during this conversion are caught and returned as `tecPATH_DRY`.
- The destination account's authorization: if withdrawing to self (`sfAccount == sfDestination`), `WeakAuth` suffices (the `doApply` phase will create any missing trust line or MPToken); if withdrawing to a third party, `StrongAuth` is required and the receiving account's trust line or MPToken must already exist.
- The vault's underlying asset must not be frozen for the destination account, and the vault's share MPT must not be frozen for the submitting account.

**`doApply()`** is the state-mutation phase, called only when both prior phases return success. It:
1. Resolves the vault and share issuance SLEs with write access.
2. Branches on whether `sfAmount` is asset-denominated or share-denominated, calling `assetsToSharesWithdraw` or `sharesToAssetsWithdraw` from `VaultHelpers` to compute the complementary quantity. Both conversion paths then call `sharesToAssetsWithdraw` to establish the canonical `assetsWithdrawn` amount, which may differ slightly from the requested amount due to fixed-point truncation.
3. Returns `tecPRECISION_LOSS` if an asset-denominated request would resolve to zero shares — this prevents economically meaningless dust withdrawals.
4. Verifies the submitter holds at least `sharesRedeemed` shares via `accountHolds`, returning `tecINSUFFICIENT_FUNDS` if not.
5. Checks `sfAssetsAvailable` (not the raw pseudo-account balance) against `assetsWithdrawn`. Vaults can pledge assets to external lending positions, making some held assets unavailable; using `sfAssetsAvailable` correctly reflects only the liquid portion.
6. Decrements both `sfAssetsTotal` and `sfAssetsAvailable` on the vault by `assetsWithdrawn`.
7. Calls `accountSend` to transfer the shares from the submitter back to the vault's pseudo-account with `WaiveTransferFee::Yes` — shares are internal bookkeeping tokens, not economic transfers.
8. Attempts to remove the submitter's now-empty MPToken holding for the share via `removeEmptyHolding`. A result of `tecHAS_OBLIGATIONS` (balance non-zero) is silently tolerated; other errors are returned. This housekeeping is skipped when the submitter is the vault owner.
9. Finally calls `doWithdraw` from `View.h` to execute the asset credit from the vault pseudo-account to the destination.

## Notable Design Decisions

**Private vault bypass at withdrawal time.** `doApply` deliberately ignores the `lsfVaultPrivate` flag. The reasoning, stated in a code comment, is that possession of vault shares is itself proof of prior authorized deposit. Forcing a second authorization gate at withdrawal time would be user-hostile and is not part of the protocol semantics.

**Dual-denomination flexibility.** Allowing users to specify either assets or shares in `sfAmount` is a deliberate UX choice. Asset-denomination means "I want exactly X of the underlying back"; share-denomination means "I want to redeem exactly N shares". Both are valid, and the rounding direction differs: asset amounts drive rounding in `assetsToSharesWithdraw`, while share redemptions are exact and only the resulting asset amount varies.

**`sfAssetsAvailable` vs balance.** Using the vault's tracked `sfAssetsAvailable` field rather than the underlying pseudo-account balance is essential for correctness when vaults participate in lending — pledged assets appear in the balance but must not be counted as withdrawable.

**Overflow handling.** The conversion math using `Number`-based arithmetic can overflow for extreme scale and total values. Both `preflight` and `doApply` explicitly catch `std::overflow_error` and map it to `tecPATH_DRY`, avoiding log spam by downgrading to `debug` severity.

**`ConsequencesFactory{Normal}`** signals to the transaction consequence machinery that this transaction does not block other transactions from the same account — it is non-escalating and can be processed concurrently with other normal transactions in the queue.