# `LoanBrokerDelete.h` — Transactor for Deleting a Lending Protocol Broker

## Role and Context

`LoanBrokerDelete` is the transactor that processes `ttLOAN_BROKER_DELETE` (type 75) transactions within the XRPL lending protocol (amendment `featureLendingProtocol`). A `LoanBroker` is a ledger object created by the `LoanBrokerSet` transaction that acts as an intermediary between a vault (the lender-side liquidity pool) and individual borrowers: it holds cover collateral, tracks cumulative debt, and earns management fees. This transactor implements the teardown path — removing the broker, recovering any remaining cover assets, and cleaning up its associated pseudo-account.

The class sits in the `include/xrpl/tx/transactors/lending/` module alongside `LoanBrokerSet`, `LoanBrokerCoverDeposit`, `LoanBrokerCoverWithdraw`, `LoanBrokerCoverClawback`, and the `Loan*` family, all of which share the `LendingHelpers` utility layer.

## Class Design

```cpp
class LoanBrokerDelete : public Transactor
```

`LoanBrokerDelete` follows the standard XRPL transactor pattern. It inherits from `Transactor` and overrides only `doApply()`, delegating signature validation, fee checks, and sequence number management entirely to the base class. The static member `ConsequencesFactory{Normal}` tells the transaction-processing infrastructure that this is a routine (non-blocking) transaction whose fee consequences are normal — it will not prevent other transactions in a batch from running.

Like all transactors in this codebase, `LoanBrokerDelete` uses compile-time polymorphism via name hiding rather than virtual dispatch for `preflight` and `preclaim`. This is intentional: it allows the `invokePreflight<T>` template in `Transactor` to call `T::checkExtraFeatures`, `T::preflight`, and so on at compile time without a vtable lookup, while still permitting per-transactor customization.

## Three-Phase Processing

**`checkExtraFeatures`** delegates immediately to `checkLendingProtocolDependencies(ctx)` from `LendingHelpers.h`. This centralizes the amendment-gate logic for the entire lending subsystem — rather than each lending transactor individually checking `featureLendingProtocol` (and any prerequisite amendments), this single call enforces all dependencies uniformly. Returning `false` causes `invokePreflight` to emit `temDISABLED`.

**`preflight`** performs only the lightest possible validation: it rejects the transaction if `sfLoanBrokerID` is the zero hash (`beast::zero`), returning `temINVALID`. This is a pure field-validity check that requires no ledger access and can be performed before any state is read.

**`preclaim`** performs stateful validation against a read-only ledger view:

1. Looks up the `LoanBroker` SLE via `keylet::loanbroker(brokerID)`. If absent, returns `tecNO_ENTRY`.
2. Enforces ownership: `sfAccount` on the transaction must match `sfOwner` on the broker SLE. Any other account gets `tecNO_PERMISSION`.
3. Checks that `sfOwnerCount` on the broker is zero. A non-zero count means active `Loan` objects still reference this broker; deletion would orphan them, so `tecHAS_OBLIGATIONS` is returned.
4. Reads the associated `Vault` SLE (via `sfVaultID`) to obtain the vault's `sfAsset`. A missing vault is a ledger-consistency failure reported as `tefBAD_LEDGER` (guarded with `LCOV_EXCL` because it should be unreachable in practice).
5. Checks `sfDebtTotal` against rounding. The comment is explicit: any remaining dust-level debt should already have been zeroed out by the last `LoanDelete` transaction. This is a *defensive* guard — if rounded debt is non-zero, the broker still has economic obligations and `tecHAS_OBLIGATIONS` is returned. The double-`LCOV_EXCL` annotation signals this path should never be reached in normal operation.
6. If `sfCoverAvailable > 0`, the remaining cover will be returned to the broker owner during `doApply`. Before allowing that asset transfer, a `checkDeepFrozen` call verifies the owner is not frozen for that asset. Deep-freeze is a compliance mechanism in which an issuer can prevent an account from receiving tokens; attempting to pay a frozen account would fail, so this check surfaces `tecFROZEN` at the cheaper preclaim stage.

**`doApply`** executes the actual ledger mutations:

1. **Directory removal** — the broker is removed from two owner directories: the human owner's `ownerDir` (tracked by `sfOwnerNode`) and the vault pseudo-account's `ownerDir` (tracked by `sfVaultNode`). Directory removal failure is an internal inconsistency and returns `tefBAD_LEDGER`.
2. **Cover asset recovery** — any `sfCoverAvailable` amount is transferred from the broker's pseudo-account to the human owner via `accountSend(..., WaiveTransferFee::Yes)`. The fee waiver prevents the transfer from being charged, since this is a cleanup payment, not a user-initiated one.
3. **Empty holding removal** — `removeEmptyHolding` clears the trust line or MPT holding on the broker pseudo-account for the vault asset, if it is now empty.
4. **Pseudo-account validation** — before erasing the pseudo-account SLE, the code triple-checks that no residual balance, owner count, or owner directory remains. These checks are marked `LCOV_EXCL` because the prior steps should have eliminated all obligations. If any remain, `tecHAS_OBLIGATIONS` is returned rather than silently corrupting ledger state.
5. **SLE erasure** — both the broker pseudo-account SLE and the broker SLE are erased from the ledger in that order.
6. **Owner count adjustment** — the human owner's `sfOwnerCount` is decremented by 2: one for the `LoanBroker` object itself, and one for its pseudo-account. This is explicit in a comment in `doApply`, making the coupling between these two objects clear to future readers.
7. **Asset association** — `associateAsset(*broker, vaultAsset)` is called after erasure, recording the asset type touched by this transaction for downstream processing (e.g., fee or reserve tracking).

## Design Notes

The pseudo-account pattern — where a `LoanBroker` object is paired with an ephemeral `AccountRoot` SLE that holds the cover collateral — mirrors the vault design elsewhere in the lending protocol. This gives each broker its own on-ledger identity for holding trust lines and MPT positions, without those holdings appearing directly in the human owner's account. The deletion transactor must therefore coordinate a two-SLE teardown, which is why the owner count is decremented by two and the pseudo-account health is validated before erasure.

The header itself is minimal by design: it exposes only the four public entry points required by the transactor framework (`checkExtraFeatures`, `preflight`, `preclaim`, `doApply`) and the `ConsequencesFactory` tag. All business logic lives in the corresponding `.cpp` file, keeping the include cost low for the many translation units that include transactor headers during registration.