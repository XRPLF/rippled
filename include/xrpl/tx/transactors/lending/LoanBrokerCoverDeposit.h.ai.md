# `LoanBrokerCoverDeposit.h` — Transactor for Depositing Cover into a Loan Broker

## Purpose

`LoanBrokerCoverDeposit` is the transactor responsible for depositing cover assets into a `LoanBroker` object within the XRPL lending protocol (XLS-66). Cover represents capital held in a broker's pseudo-account that backs broker obligations — most notably fee payments and risk coverage that the broker provides to the associated vault. This transactor allows the broker owner to increase the broker's available cover balance by transferring assets from their own account.

## Role in the Lending Module

The lending module defines a family of transactors under `include/xrpl/tx/transactors/lending/`, all following the same `Transactor` base-class pattern. The cover lifecycle is managed by three complementary transactors: `LoanBrokerCoverDeposit` (this file), `LoanBrokerCoverWithdraw`, and `LoanBrokerCoverClawback`. Deposit and withdraw are the standard credit/debit operations; clawback handles forced recovery. This separation of concerns maps each operation to a distinct on-ledger transaction type, keeping authorization and side-effect logic cleanly isolated.

## Three-Phase Validation Pattern

Like every `Transactor` subclass, `LoanBrokerCoverDeposit` is invoked through the framework's `invokePreflight` / `preclaim` / `doApply` pipeline, which the base class documents as compile-time polymorphism through static name-hiding rather than virtual dispatch (only `doApply` is virtual).

**`checkExtraFeatures`** delegates to `checkLendingProtocolDependencies`, which gates the entire lending feature family behind its amendment. This is the appropriate place for amendment checks — the base-class comment explicitly prohibits doing amendment checks inside `preflight` itself.

**`preflight`** is deliberately lightweight and stateless: it works only against the transaction fields in `ctx.tx`, not the ledger. It rejects a zero `sfLoanBrokerID` (a null broker reference is always invalid) and validates `sfAmount` for positivity and legality via `isLegalNet`. This catches obviously malformed transactions before any ledger I/O is attempted.

**`preclaim`** does the substantive validation against the current ledger state (a read-only `ReadView`):

1. Confirms the `LoanBroker` object exists via `keylet::loanbroker`.
2. Enforces ownership — only `sfOwner` of the broker may make a cover deposit, returning `tecNO_PERMISSION` otherwise.
3. Looks up the broker's associated vault to resolve the canonical `sfAsset` type. A missing vault is treated as a fatal ledger corruption (`tefBAD_LEDGER`, excluded from coverage with `LCOV_EXCL_*` markers because it should be structurally impossible).
4. Verifies that the deposited `Amount` matches the vault's asset type (`tecWRONG_ASSET`).
5. Runs a chain of asset transfer guards — non-transferable asset check (`canTransfer`), source-side freeze check (`checkFrozen`), deep-freeze check on the broker pseudo-account (`checkDeepFrozen`), and strong authorization check (`requireAuth`). These mirror the checks performed for any asset movement on XRPL and ensure the deposit respects the token issuer's rules.
6. Finally confirms the depositor has sufficient spendable balance (`tecINSUFFICIENT_FUNDS`), using `FreezeHandling::fhZERO_IF_FROZEN` and `AuthHandling::ahZERO_IF_UNAUTHORIZED` so frozen or unauthorized balances are treated as zero for sufficiency purposes.

## `doApply` — Ledger Mutation

The apply phase is concise. It re-peeks the `LoanBroker` SLE for mutable access, then:

1. Calls `accountSend` to transfer the specified amount from the transaction sender (`account_`) to the broker's `sfAccount` (its pseudo-account), passing `WaiveTransferFee::Yes` — cover deposits are exempt from transfer fees, which is economically sensible since the broker owner is moving their own capital into infrastructure they control rather than making a market transfer.
2. Increments `sfCoverAvailable` on the broker SLE by the deposited amount and flushes the update with `view().update(broker)`.
3. Calls `associateAsset` to record the association between the broker and the vault asset, ensuring the broker's asset tracking remains consistent after the deposit.

## Design Notes

`ConsequencesFactory` is set to `Normal`, meaning this transaction claims a fee under standard circumstances and doesn't block other transactions from the same account. The constructor is `explicit` and simply forwards `ApplyContext` to the base class — all state lives in the context, not in the transactor instance.

The choice to store cover in a broker-owned pseudo-account (rather than a simple numeric field) means the assets are held on-ledger with the full XRPL trust line and freeze machinery intact. This is why `preclaim` must run the same asset transfer guards as any other movement — the pseudo-account is a first-class ledger account subject to all the usual rules, not an off-ledger accounting entry.