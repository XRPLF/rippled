# `SetRegularKey.h` — SetRegularKey Transaction Transactor

## Role in the System

`SetRegularKey` is the transactor responsible for processing `SetRegularKey` transactions on the XRP Ledger. This transaction type allows an account holder to assign a secondary cryptographic keypair — the "regular key" — to their account, or to revoke a previously assigned one. By separating signing authority from account ownership, XRPL accounts can protect their master key in cold storage while using the regular key for day-to-day operations. This file declares the class; the implementation lives in `src/libxrpl/tx/transactors/account/SetRegularKey.cpp`.

## Class Structure and Base Contract

`SetRegularKey` extends `Transactor`, the abstract base for all XRPL transaction types. The `Transactor` base enforces a three-phase processing pipeline:

- **Preflight**: stateless validity checks against the raw transaction (no ledger access)
- **Preclaim**: ledger-aware checks before fee deduction (inherited as a no-op here)
- **Apply** (`doApply`): the actual mutation of ledger state

The static methods (`preflight`, `calculateBaseFee`) participate in a deliberate compile-time polymorphism scheme via name-hiding rather than virtual dispatch, as documented in `Transactor.h`. The `invokePreflight<T>` template calls `T::preflight` and `T::calculateBaseFee` directly, so `SetRegularKey` overrides these without marking them `virtual`.

## `ConsequencesFactory{Blocker}`

The class declares `ConsequencesFactory` as `Blocker`, distinguishing it from `Normal` and `Custom` variants. A blocker transaction prevents other transactions from the same account from being queued ahead of or alongside it. This is semantically correct: changing which key signs for an account can invalidate the signatures of any other queued transactions, so the ledger conservatively blocks the queue until this transaction settles.

## Custom Fee Logic: The One-Time Free Regular Key

`calculateBaseFee` overrides the base implementation to implement a fee-waiver mechanic. If the transaction is signed directly with the account's master key — verified by checking that `calcAccountID(signingPublicKey) == accountID` — and the account's `lsfPasswordSpent` flag is not yet set, the function returns `XRPAmount{0}`, making the transaction free.

This exists to support a bootstrapping pattern where an account is funded with a pre-configured "password" (a well-known or operator-assigned key), and the first act of the new account owner is to replace that key with their own. The ledger waives the fee for this first use, lowering the barrier to entry. Once used, `doApply` sets `lsfPasswordSpent` on the account SLE, ensuring the waiver is consumed exactly once.

## Preflight Validation

`preflight` performs a single but important semantic check: it rejects the transaction with `temBAD_REGKEY` if the regular key being assigned is the same as the account itself (`sfRegularKey == sfAccount`). Setting the regular key to the master key's account ID is a degenerate no-op that would likely indicate a mistake, so it is caught early before any ledger state is touched.

## Apply Logic and Safety Invariants

`doApply` handles two distinct operations depending on whether `sfRegularKey` is present in the transaction:

**Setting the key**: If `sfRegularKey` is present, it is written directly to the account SLE via `setAccountID`. Before doing so, if the fee was not charged (i.e., `minimumFee` returns zero, signaling the password-waiver path was used), the `lsfPasswordSpent` flag is set on the account to prevent future reuse.

**Removing the key**: If `sfRegularKey` is absent, the transactor interprets this as a removal request. Here, a critical safety invariant is enforced: before calling `makeFieldAbsent(sfRegularKey)`, the code checks whether the master key is disabled (`lsfDisableMaster`) and no multisig signer list exists (`keylet::signers(account_)`). If both conditions hold, removing the regular key would leave the account with no valid signing path, permanently locked. This is rejected with `tecNO_ALTERNATIVE_KEY`. Only when at least one alternative remains is the field removed.

This guard is architecturally essential: the XRPL has no account recovery mechanism, so the ledger itself must prevent self-inflicted lockout.

## Relationship to Sibling Transactors

Among the account-management transactors (`AccountSet`, `AccountDelete`, `SignerListSet`, `SetRegularKey`), this one has the simplest interface — no `preclaim` override, no custom `getFlagsMask`. The `AccountSet` transactor, by contrast, uses `ConsequencesFactory{Custom}` and provides `makeTxConsequences`, reflecting its more complex flag interactions. `SetRegularKey`'s simplicity is appropriate: it mutates exactly one field on one SLE, with one safety gate.