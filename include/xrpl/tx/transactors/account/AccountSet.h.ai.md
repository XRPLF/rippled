# `AccountSet.h` — AccountSet Transaction Transactor

## Role in the System

`AccountSet` is the transactor responsible for processing `AccountSet` transactions on the XRP Ledger — the primary mechanism by which account owners configure their account root entry. This covers a wide surface area: behavioral flags (require destination tags, disable master key, no-freeze, global freeze, deposit auth, disallow-incoming variants), metadata fields (domain, email hash, message key, wallet locator), economic parameters (transfer rate, tick size), and NFT-related settings (authorized minter). The class lives in the `account/` transactor group alongside `AccountDelete`, `SetRegularKey`, and `SignerListSet`.

## Class Structure and Inheritance

`AccountSet` extends `Transactor`, the abstract base for all transaction processors. The interface follows the framework's three-phase pipeline executed by `Transactor::operator()()`:

1. **`preflight`** — stateless, fee-free validation against the transaction's raw fields.
2. **`preclaim`** — read-only ledger checks that may still reject the transaction.
3. **`doApply`** — the mutating phase that writes changes to the ledger.

Two additional static methods complete the interface: `makeTxConsequences` and `getFlagsMask`.

Because the base class uses compile-time polymorphism (via the `invokePreflight<T>` template selecting static methods by name) rather than virtual dispatch, the `static` keyword on `preflight`, `preclaim`, `getFlagsMask`, `makeTxConsequences`, and `checkPermission` is architecturally significant, not incidental. The constructor is a trivial forwarding constructor to `Transactor(ctx)`.

## Custom Consequences

The `ConsequencesFactory{Custom}` declaration tells the framework to call `makeTxConsequences` rather than using the generic normal or blocker factories. `AccountSet` needs this because most AccountSet transactions are *normal* (they don't affect the ordering of other transactions in the queue), but transactions that set or clear `asfRequireAuth`, `asfDisableMaster`, or `asfAccountTxnID` — or use the legacy `tfRequireAuth`/`tfOptionalAuth` flags — are classified as *blockers*. Blockers prevent subsequent transactions from the same account from being queued until the blocker confirms, guarding against state-dependent sequences like establishing trust lines before enabling RequireAuth.

## Preflight: Dual-Path Flag System

`getFlagsMask` simply returns `tfAccountSetMask`, routing unknown transaction flags to rejection in the framework's `preflight1` call.

`preflight` must handle a historical dual-path flag system: some flags can be specified as bitfield flags in the transaction's `Flags` field (the legacy `tfRequireAuth`, `tfRequireDestTag`, `tfDisallowXRP` family) *or* as a numeric value in the `sfSetFlag`/`sfClearFlag` fields (the modern `asf*` constants). The validation logic consolidates both paths into boolean variables like `bSetRequireAuth` and `bClearRequireAuth`, then checks they don't conflict. This duplication exists because the flag-field interface predates the `sfSetFlag` interface; both remain live for backward compatibility.

Beyond flag coherence, `preflight` validates:
- `TransferRate`: must be zero (meaning "unset") or in the range `[QUALITY_ONE, 2×QUALITY_ONE]`. Values below `QUALITY_ONE` are invalid (a discount rate would let issuers create value from nothing).
- `TickSize`: must be zero (unset) or within `[Quality::minTickSize, Quality::maxTickSize]`.
- `MessageKey`: if present and non-empty, must be a valid public key type.
- `Domain`: bounded by `maxDomainLength`.
- `sfNFTokenMinter` presence is validated against the set/clear flag — setting `asfAuthorizedNFTokenMinter` requires the field present, clearing it requires it absent.

## `checkPermission`: Granular Delegation Model

`checkPermission` handles the delegate-signing case, where a third-party account submits an AccountSet on behalf of the account owner. The method is deliberately restrictive: if a `sfDelegate` field is present, the transaction is rejected unless the delegate's `DelegateObject` in the ledger explicitly lists granular permissions for each field being modified.

The design reflects a deliberate policy choice: AccountSet is too sensitive to grant wholesale. Flags and `sfSetFlag`/`sfClearFlag` fields are categorically blocked for delegates — any attempt to set or clear behavioral flags returns `terNO_DELEGATE_PERMISSION`. Only the narrow metadata fields (`sfEmailHash`, `sfMessageKey`, `sfDomain`, `sfTransferRate`, `sfTickSize`) are delegatable, and only if the corresponding granular permission constant (e.g., `AccountDomainSet`, `AccountTransferRateSet`) has been granted. `sfWalletLocator` and `sfNFTokenMinter` are unconditionally blocked from delegates.

## `preclaim`: Ledger-State Constraints

Two important state-dependent checks occur here, after the ledger is readable but before any state is mutated:

- **RequireAuth**: Setting `asfRequireAuth` is rejected if the account already has entries in its owner directory (`tecOWNERS`/`terOWNERS` depending on the retry flag). This prevents retroactively breaking existing trust relationships — you cannot mandate authorization after the fact.
- **Clawback / NoFreeze mutual exclusion**: When the `featureClawback` amendment is enabled, `asfAllowTrustLineClawback` cannot be set if `lsfNoFreeze` is already set (and vice versa). Clawback also requires an empty owner directory for the same reason as RequireAuth. These are enforced in `preclaim` rather than `doApply` because they need to produce consensus-safe error codes (`tecNO_PERMISSION`, `tecOWNERS`) that fee-charge the submitter.

## `doApply`: The Mutation Phase

`doApply` reads the current account SLE via `view().peek()`, computes the new `sfFlags` bitmask by applying each flag change in sequence, updates non-flag fields directly on the SLE, and calls `ctx_.view().update(sle)` once at the end.

Two security-sensitive flags require extra checks at apply time:

- **DisableMaster** (`asfDisableMaster`): The transaction must have been signed with the master key itself (`sigWithMaster` determined by comparing the signing public key's derived account ID against `account_`), and the account must already have a `sfRegularKey` or a multi-signer list (`keylet::signers`). This prevents an account from permanently locking itself out.
- **NoFreeze** (`asfNoFreeze`): Likewise requires a master key signature when master is still enabled. Note that `asfNoFreeze` is permanent — once set, it cannot be cleared, so the code only handles the set path.

The interlock between `asfGlobalFreeze` and `asfNoFreeze` is notable: once `lsfNoFreeze` is active, the account cannot clear `lsfGlobalFreeze`. The rationale is anti-manipulation — without this constraint, an issuer with NoFreeze could still selectively freeze and unfreeze the market by toggling GlobalFreeze.

Amendment-gated flag handling (`featureTokenEscrow`, `featureClawback`) uses runtime checks via `ctx_.view().rules().enabled(...)`, ensuring that new flag semantics only activate after the corresponding network amendment is live.

For scalar fields (domain, email hash, message key, wallet locator, transfer rate, tick size), an empty or zero value signals deletion (`makeFieldAbsent`) rather than an explicit clear flag, which keeps the account SLE compact.