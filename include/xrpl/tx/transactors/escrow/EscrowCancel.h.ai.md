# `EscrowCancel.h` — Escrow Cancellation Transactor

## Role in the System

`EscrowCancel` implements the transactor responsible for returning locked funds to their original owner when an escrow has passed its expiry time. It is the "reclaim" counterpart to `EscrowFinish` in the escrow lifecycle: where `EscrowFinish` releases funds to the intended recipient when conditions are met, `EscrowCancel` unwinds the escrow entirely and restores the held amount to the account that created it.

The file is a minimal header declaring the class interface — the substantive logic lives in `EscrowCancel.cpp` — but the structural choices expressed here are architecturally meaningful.

## Relationship to `Transactor`

`EscrowCancel` inherits from `Transactor`, the base class for every transaction type processed by the XRPL node. The `Transactor` framework enforces a three-phase pipeline: `preflight` (stateless sanity checks on the raw transaction fields), `preclaim` (read-only checks against the current ledger state), and `doApply` (the state-mutating application). Each phase is invoked by the framework through template-based static dispatch via `invokePreflight<T>`, meaning the compiler resolves the call chain at compile time without virtual dispatch overhead.

## `ConsequencesFactory` Classification

The `ConsequencesFactory` is declared as `Normal`, distinguishing it from `EscrowCreate`, which uses `Custom`. This tells the framework that `EscrowCancel` follows the standard fee-consumption model for transaction consequences: it can claim a fee, but it does not tie up additional XRP beyond that fee. `EscrowCreate` needs a `Custom` factory because it locks up an arbitrary amount of XRP or tokens, requiring a bespoke `makeTxConsequences` calculation. Cancelling an escrow unlocks funds rather than locking them, so the standard model applies.

## The Three-Phase Interface

**`preflight`** takes a `PreflightContext const&` and returns `NotTEC`. The implementation simply returns `tesSUCCESS` — there are no field-level constraints to validate beyond what the framework's `preflight1`/`preflight2` wrappers already check (fee, flags, signatures). Unlike `EscrowFinish`, which overrides `checkExtraFeatures` and `preflightSigValidated` for crypto-condition and partial-signature logic, `EscrowCancel` has no such domain-specific preflight concerns.

**`preclaim`** takes a `PreclaimContext const&` and returns `TER`. Here the read-only ledger state is consulted. Under the `featureTokenEscrow` amendment, the method validates that the escrow object exists, and if the escrowed amount is non-XRP, it dispatches to one of two template specialisations — `escrowCancelPreclaimHelper<Issue>` for IOU/trust-line assets or `escrowCancelPreclaimHelper<MPTIssue>` for MPT (Multi-Purpose Token) assets. Both specialisations guard against `requireAuth` violations: if the original escrow account is no longer authorized to hold the asset (e.g., the issuer enabled authorization requirements after the escrow was created), cancellation is blocked. Without the `featureTokenEscrow` amendment, preclaim unconditionally succeeds.

**`doApply`** performs the mutating work. It locates the escrow object by `{sfOwner, sfOfferSequence}`, enforces that `sfCancelAfter` is set and has been passed (returning `tecNO_PERMISSION` otherwise), then removes the escrow from the owner directory and optionally from the recipient's owner directory if `sfDestinationNode` is present. For XRP escrows, the locked balance is added directly back to the owner's account balance. For non-XRP amounts (gated on `featureTokenEscrow`), `escrowUnlockApplyHelper` handles the token-type-specific transfer back to the owner, and an additional directory removal step purges the entry from the issuer's owner directory via `sfIssuerNode`. Finally, `adjustOwnerCount` decrements the escrow owner's reserve count and the escrow object is erased from the ledger.

## Design Observations

The `ConsequencesFactory` constant and the three static methods form a compile-time interface contract that the `Transactor` framework expects via name-hiding rather than virtual dispatch. The comment in `Transactor.h` makes this explicit: "these are not really virtual and so don't have the compiler-time protection that comes with it." The discipline required is that derived classes must match the exact signatures — the header enforces this implicitly by declaring them.

The `preflight` returning unconditional success is a deliberate design choice: there is nothing about the `EscrowCancel` transaction format itself (beyond universal field rules) that can be checked without ledger state. All the meaningful constraints — does the escrow exist, has the cancel time passed, is authorization still valid — require state access and therefore belong in `preclaim` or `doApply`.

The `preclaim` / `doApply` split for the existence check is also intentional. `preclaim` runs against a read-only view and short-circuits fee collection only when there is definitively no target. `doApply` re-checks existence with a writable `peek` because between the two phases, in theory, ledger state may differ. Finding no escrow in `doApply` after `featureTokenEscrow` is active returns `tecINTERNAL` (marked `LCOV_EXCL_LINE` indicating it is considered unreachable in practice), while the legacy path returns `tecNO_TARGET` for backward compatibility.