# AccountSet.cpp

`AccountSet.cpp` implements the `AccountSet` transactor — the primary mechanism by which XRPL account holders configure the behavioural properties of their accounts. It governs a wide range of settings: access controls (`RequireAuth`, `DepositAuth`), XRP reception preferences (`DisallowXRP`), freezing authority (`NoFreeze`, `GlobalFreeze`), trust-line policy (`DefaultRipple`, `AllowTrustLineClawback`, `AllowTrustLineLocking`), transaction tracking (`AccountTxnID`), and metadata fields such as domain, email hash, message key, and NFT minting delegation.

## Transactor Lifecycle

`AccountSet` extends the `Transactor` base class and follows the four-phase execution model used by all XRPL transaction types: `makeTxConsequences` → `preflight` → `preclaim` → `doApply`. Each phase has distinct access rights — `preflight` sees only the transaction itself with no ledger access, `preclaim` has read-only ledger access, and `doApply` has full mutable ledger access via the apply view.

## Dual Flag Interface and Legacy Complexity

One of the most structurally unusual aspects of `AccountSet` is that it exposes account flags through *two* parallel mechanisms: legacy transaction-level bitflags (e.g., `tfRequireAuth`, `tfOptionalAuth`, `tfRequireDestTag`) set in the transaction's `Flags` field, and the more modern `sfSetFlag`/`sfClearFlag` integer fields carrying `asf*` constants. Both paths must produce the same logical effect. In `preflight` and `doApply`, the code computes booleans like `bSetRequireAuth` and `bClearRequireAuth` by ORing both signal paths together. This makes the code verbose but is unavoidable: the legacy flag interface predates the `SetFlag`/`ClearFlag` fields and remains supported for protocol compatibility.

`preflight` enforces that neither path allows contradictory intent within a single transaction. If both `bSetRequireAuth` and `bClearRequireAuth` resolve to true — via any combination of the two input mechanisms — the transaction is rejected with `temINVALID_FLAG`. The same guard applies to `RequireDestTag` and `DisallowXRP`. An additional sanity check prevents `sfSetFlag` and `sfClearFlag` from carrying the same value simultaneously.

## Consequence Classification

`makeTxConsequences` uses a custom factory (`ConsequencesFactory{Custom}`) to classify transactions as either `blocker` or `normal`. Transactions that set or clear `asfRequireAuth`, `asfDisableMaster`, or `asfAccountTxnID` — and equivalently, the legacy `tfRequireAuth`/`tfOptionalAuth` bitflags — are classified as blockers. This matters within the batch transaction mechanism: a blocker prevents later transactions from the same account in the same batch from being reordered past it, since these flags fundamentally change how subsequent transactions are processed or signed.

## Delegation and Granular Permissions

`checkPermission` enforces a nuanced access-control policy for delegated transactions. The overall `AccountSet` transaction type is not delegable at the transaction level — a delegate with only a transaction-type permission for `ttACCOUNT_SET` cannot execute it. However, a more granular delegation layer (`GranularPermissionType`) permits specific field-level operations. The code checks individual fields: `sfEmailHash`, `sfMessageKey`, `sfDomain`, `sfTransferRate`, and `sfTickSize` each require their own `AccountEmailHashSet`, `AccountMessageKeySet`, `AccountDomainSet`, `AccountTransferRateSet`, or `AccountTickSizeSet` granular permission respectively. Flag changes (any non-zero `sfSetFlag`, `sfClearFlag`, or transaction-level flags) are explicitly prohibited — there is no granular permission pathway for toggling account flags. `sfWalletLocator` and `sfNFTokenMinter` are also always denied to delegates.

## Stateful Constraints in preclaim

`preclaim` performs two checks that require ledger state. First, setting `RequireAuth` on an account that currently lacks it is only valid if the account's owner directory is empty — no existing trust lines can be retroactively subjected to an authorization requirement. If the retry flag `tapRETRY` is set, the error code is softened from `tecOWNERS` to `terOWNERS`, signalling that the transaction could succeed later if the owner directory empties.

Second, when the `featureClawback` amendment is enabled, `asfAllowTrustLineClawback` and `asfNoFreeze` are mutually exclusive and each requires an empty owner directory when being set. Setting clawback while `NoFreeze` is active returns `tecNO_PERMISSION`; setting `NoFreeze` while clawback is active does likewise. The empty-directory requirement for clawback prevents an issuer from silently acquiring clawback rights over existing trust lines.

## Critical Irreversibility Guards in doApply

Several flag operations in `doApply` are guarded by master-key authentication. The `sigWithMaster` lambda inspects the transaction's signing public key and verifies it derives to the account's own address — meaning the master private key was used to sign. Both `asfDisableMaster` and `asfNoFreeze` require this. For `asfDisableMaster` specifically, the code additionally verifies that a `sfRegularKey` field is present on the account SLE or that a signer list object exists at `keylet::signers(account_)` before disabling the master key — otherwise the account would be locked out permanently, returning `tecNO_ALTERNATIVE_KEY`.

The `NoFreeze`/`GlobalFreeze` interaction is also carefully guarded: once `NoFreeze` is active, `GlobalFreeze` can still be *set* (to protect all counterparties at once) but can never be *cleared*. The condition `(uSetFlag != asfGlobalFreeze) && (uClearFlag == asfGlobalFreeze) && ((uFlagsOut & lsfNoFreeze) == 0)` ensures that a `GlobalFreeze` clear is only applied when `NoFreeze` is absent — preventing issuers who have committed to never-freezing from using GlobalFreeze strategically as a temporary lever.

## Flag Mutation Pattern and Metadata Fields

`doApply` reads the current `sfFlags` value from the account's SLE into `uFlagsIn`, builds a modified copy `uFlagsOut` through bitwise OR and AND operations, and only writes the result back if the value changed. This avoids unnecessary SLE mutations that would force ledger serialization overhead for no-op transactions.

Metadata fields (`sfEmailHash`, `sfWalletLocator`, `sfMessageKey`, `sfDomain`, `sfTransferRate`, `sfTickSize`) follow a consistent clear-or-set pattern: a zero or empty value causes `makeFieldAbsent` (removing the field from the SLE entirely), while a non-zero value calls the appropriate setter. Removing absent fields keeps account SLEs compact and avoids encoding default values on-chain.

Feature-gated flags (`asfAllowTrustLineLocking` under `featureTokenEscrow`, `asfAllowTrustLineClawback` under `featureClawback`) are only processed when the corresponding ledger rule is enabled, following the amendment activation pattern used throughout the XRPL protocol to introduce new capabilities without breaking consensus on older nodes.