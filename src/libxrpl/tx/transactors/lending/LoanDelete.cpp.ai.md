# `LoanDelete.cpp` — Loan Teardown Transactor

## Purpose and System Context

`LoanDelete.cpp` implements the `LoanDelete` transaction type for the XRPL lending protocol (XLS-66). Its job is straightforward on the surface — remove a fully-repaid `Loan` ledger object — but the surrounding invariants, permission model, and debt-forgiveness edge case make it architecturally significant.

The lending protocol maintains a three-tier object graph: a `Vault` holds pooled assets, a `LoanBroker` intermediates between the vault and individual borrowers, and `Loan` objects represent individual debt agreements. `LoanDelete` sits at the bottom of this hierarchy, cleaning up leaf nodes once borrowers have satisfied their obligations.

`LoanDelete` inherits from `Transactor` and follows the standard XRPL three-phase processing pipeline: `preflight` (stateless), `preclaim` (stateful read-only), and `doApply` (ledger mutation). The additional `checkExtraFeatures` override simply delegates to `checkLendingProtocolDependencies`, ensuring the required protocol amendments are active before any lending transaction is processed.

## Validation Phases

**`preflight`** is intentionally minimal — it only checks that `sfLoanID` is not zero (`temINVALID`). Since a zero ID indicates a structurally malformed transaction, this is a cheap early exit that saves a ledger lookup.

**`preclaim`** does the real gating work, executing four ordered checks:

1. **Existence**: The `Loan` object must exist for the given `sfLoanID`, returning `tecNO_ENTRY` if not.
2. **Outstanding balance**: `sfPaymentRemaining > 0` returns `tecHAS_OBLIGATIONS`. This is the core business rule — active loans cannot be deleted.
3. **Broker consistency**: The loan's `sfLoanBrokerID` must resolve to an existing `LoanBroker` object. If not, `tecINTERNAL` is returned, annotated `LCOV_EXCL_LINE` — the ledger would have to be corrupted for this to happen, since the broker ID is embedded at loan creation.
4. **Authorization**: The submitting account must be either the `LoanBroker`'s owner (`sfOwner`) or the loan's borrower (`sfBorrower`). Both parties have symmetric authority to clean up a settled loan — neither needs the other's consent to release the ledger objects.

## Apply Logic and the Dual Owner Count System

`doApply` begins with defensive `peek` lookups on the loan, borrower account, broker, and vault SLEs. All four `tefBAD_LEDGER` returns in this phase are `LCOV_EXCL_LINE`-annotated because `preclaim` already confirmed their existence; these guards are structural contracts rather than user-facing validation paths.

The core teardown proceeds in order:

1. **Directory removal**: The loan's ID is removed from two owner directories — the `LoanBroker`'s pseudo-account directory (`sfLoanBrokerNode`) and the borrower's account directory (`sfOwnerNode`). Both must succeed or the entire transaction rolls back with `tefBAD_LEDGER`.
2. **SLE erasure**: The `Loan` object is deleted from the ledger.
3. **Broker owner count decrement**: `adjustOwnerCount` decrements `sfOwnerCount` on the broker SLE itself (not on the broker's pseudo-account). This count tracks the number of outstanding loans brokered — it is distinct from the pseudo-account's owner count, which governs XRP reserve requirements for objects the pseudo-account directly owns. `LoanBrokerDelete` decrements the pseudo-account count by two (for both the broker object and the pseudo-account); `LoanDelete` only touches the broker-level count.

## The Last-Loan Debt Forgiveness Invariant

After the owner count decrement, there is a critical edge case: if `sfOwnerCount` has reached zero — meaning this was the last outstanding loan — any residual value in `sfDebtTotal` on the broker is wiped to zero:

```cpp
if (brokerSle->at(sfOwnerCount) == 0)
{
    auto debtTotalProxy = brokerSle->at(sfDebtTotal);
    if (*debtTotalProxy != beast::zero)
    {
        XRPL_ASSERT_PARTS(
            roundToAsset(..., debtTotalProxy, ..., Number::towards_zero) == beast::zero,
            ...
            "last loan, remaining debt rounds to zero");
        debtTotalProxy = 0;
    }
}
```

The design rationale is that `sfDebtTotal` accumulates rounding dust over the lifetime of many loans. Once no loans remain, there is no mechanism to recover that dust — no more payments can reduce it. Rather than leaving the `LoanBroker` permanently stranded with unreclaimable residue (which would block `LoanBrokerDelete`, which independently checks that debt rounds to zero), the last `LoanDelete` forgives it. The `XRPL_ASSERT_PARTS` immediately before the zero assignment acts as an invariant check: the debt must already round to zero, so the forgiveness is purely cleaning up floating-point noise, not writing off real value.

## Borrower Owner Count and Asset Association

After the broker count is handled, `adjustOwnerCount` decrements the borrower's own account owner count — the `Loan` object was registered as a borrower-owned object at creation, so deletion releases that reserve slot.

Finally, `associateAsset` is called on the loan, broker, and vault SLEs with the vault's asset type. The code comment is candid: "These associations shouldn't do anything, but do them just to be safe." This is a protocol convention: `STNumber` and `STTakesAsset`-derived fields may carry asset-precision metadata that needs to be kept consistent. Calling `associateAsset` at the end of `doApply` is idiomatic in the lending transactors, and the defensive call here ensures the convention is respected even across deletion paths where no write-back of these fields is expected.

## Relationship to Sibling Transactors

`LoanDelete` is intentionally simpler than `LoanBrokerDelete`. The broker deletion is more complex because it must handle cover-asset refunds, pseudo-account teardown, and ensuring no remaining obligations exist at the broker level. `LoanDelete` is a prerequisite: `LoanBrokerDelete` requires `sfOwnerCount == 0` on the broker, which can only happen once all loans have been deleted. The two transactors form an ordered teardown sequence mandated by the protocol.