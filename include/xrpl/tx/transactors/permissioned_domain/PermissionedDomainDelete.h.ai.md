# `PermissionedDomainDelete.h` — Transactor for Removing a Permissioned Domain

## Role in the System

`PermissionedDomainDelete` is one of two transactors that manage the lifecycle of permissioned domains on the XRP Ledger — the other being `PermissionedDomainSet`, which handles creation and updates. Together they live under `include/xrpl/tx/transactors/permissioned_domain/` and follow the standard XRPL transactor pattern, where each transaction type is a concrete class inheriting from the `Transactor` base.

A permissioned domain is a ledger object that constrains access for certain ledger operations (such as AMM or DEX interactions) to accounts that satisfy a set of credentials. This transactor provides the controlled teardown path: an account that owns a domain can remove it from the ledger, recovering the owner reserve that was locked when the domain was created.

## Class Design and the Three-Phase Transaction Model

`PermissionedDomainDelete` follows the mandatory three-phase pipeline that all XRPL transactors must implement:

**`preflight`** runs before the ledger is touched and with no access to ledger state. For this transactor it performs only the cheapest possible validation: confirming that the `sfDomainID` field in the transaction is non-zero. A zero `DomainID` is structurally malformed and returns `temMALFORMED`. This is intentionally minimal — anything that requires reading ledger state is deferred to `preclaim`.

**`preclaim`** has read-only access to the current ledger view. It resolves the `sfDomainID` to a `PermissionedDomain` SLE (Serialized Ledger Entry) via `keylet::permissionedDomain`. Two conditions are checked: the domain must actually exist (`tecNO_ENTRY` otherwise), and the submitting account must be the domain's owner (`tecNO_PERMISSION` otherwise). Splitting existence and ownership checks into `preclaim` — rather than `doApply` — is important because `preclaim` determines whether the transaction will *claim a fee* even if it ultimately fails; an account that submits a malformed or unauthorized delete still pays.

**`doApply`** performs the irreversible ledger mutations: it removes the domain's entry from the owner's directory via `view().dirRemove()`, decrements the account's `sfOwnerCount` through `adjustOwnerCount()`, and erases the SLE with `view().erase()`. The directory removal step is critical — XRPL uses owner directories to enforce reserve requirements (each ledger object owned by an account locks a base reserve increment), and skipping this step would leave the account's reserve accounting permanently wrong. The fatal-level log on `dirRemove` failure is marked `LCOV_EXCL` because a corrupt directory at that point would represent a ledger invariant violation that cannot be recovered from gracefully.

## `ConsequencesFactory` and Fee Consequences

The `static constexpr ConsequencesFactoryType ConsequencesFactory{Normal}` declaration tells the transaction consequences framework how to evaluate the potential impact of this transaction on account state when deciding how to queue or batch it. `Normal` means the transaction's worst-case effect on the account's spendable balance is limited to the transaction fee itself — appropriate here since deleting a domain returns a reserve but does not move any XRP between parties in an unbounded way.

## Comparison with `PermissionedDomainSet`

`PermissionedDomainSet` declares an additional `checkExtraFeatures` static override, which gates the transaction on a specific amendment being enabled. `PermissionedDomainDelete` omits this override, falling through to the `Transactor` base implementation that unconditionally returns `true`. This asymmetry reflects the general ledger design principle that once a feature's objects exist in the ledger, their deletion must always be possible regardless of amendment state — you should never create a situation where objects are stranded and unremovable.

## Invariants and Failure Handling

The `XRPL_ASSERT` calls in `doApply` encode two invariants that should always hold by the time execution reaches that method: the `sfDomainID` field is present (guaranteed by preflight), and the owner account's `sfOwnerCount` is positive (it was incremented when the domain was created). Both are defensive; reaching `doApply` with a violated precondition would indicate a serious bug in the pipeline above it, not a recoverable user error. The `tefBAD_LEDGER` return for a failed directory removal is similarly a hard internal error, distinct from the `tec` errors that represent valid but failed user requests.