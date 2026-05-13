# Transactors

Transaction processing pipeline: preflight (static validation) → preclaim (ledger state checks) → doApply (state mutation). Base class `Transactor` in `src/libxrpl/tx/`. Every transaction type inherits from it; only `doApply()` is virtual — all other dispatch is compile-time.

## Pipeline Architecture

### Three Phases

1. **`preflight`** — stateless, no ledger access. Validates fields, flags, signatures (cached via HashRouter). Cheap, parallelizable. Returns `NotTEC`. Results carry a `TxConsequences` summary used by the transaction queue.
2. **`preclaim`** — read-only `ReadView` access. Checks account exists, fee sufficient, sequence valid, signature valid. Returns `TER`. Sets `likelyToClaimFee` for relay decisions.
3. **`doApply`** — mutable `ApplyView` access. Only runs if preclaim returned `tesSUCCESS` and `likelyToClaimFee` is true.

`apply()` in `apply.cpp` composes all three. It accepts a preflight callable so the same `preclaim`+`doApply` machinery serves normal and batch-inner transactions. `applyTransaction()` adds `tapRETRY` semantics and dispatches to `applyBatchTransactions()` after a successful `ttBATCH`.

**Important preclaim security invariant** (documented in `applySteps.cpp`): every check through and including `checkSign` must return `NotTEC` (not a `tec` code). A `tec` before signature verification would charge a fee without authentication — a critical security property.

### Layered Preflight: `preflight0` → `preflight1` → `T::preflight` → `preflight2` → `T::preflightSigValidated`

`Transactor::invokePreflight<T>` calls (in order): `T::checkExtraFeatures`, `preflight1(ctx, T::getFlagsMask(ctx))`, `T::preflight`, `preflight2`, `T::preflightSigValidated`. Each is a static method; derived classes participate via name hiding — never virtual.

- **`preflight0`** (called from `preflight1`): gates on `sfNetworkID` presence/absence, zero-hash tx ID, valid flag bits, and pseudo-tx/batch-inner exclusivity.
- **`preflight1`**: account is non-zero, `sfFee` is non-negative native XRP, signing key format valid, tickets and `sfAccountTxnID` are mutually exclusive.
- **`preflight2`**: simulation mode (`tapDRY_RUN`), cryptographic signature check via hash router. Skipped entirely for `tfInnerBatchTxn` (outer batch authorizes).

**Rule**: derived `preflight` runs *between* `preflight1` and `preflight2`. Never call `preflight1`/`preflight2` directly.

### Compile-time Polymorphism (Name Hiding, Not Virtual)

`with_txn_type()` in `applySteps.cpp` uses an X-macro over `transactions.macro` to convert runtime `TxType` to a compile-time template parameter via a switch dispatch — no virtual dispatch, no transactor headers included in `applySteps.cpp` (explicitly forbidden).

### `ConsequencesFactoryType`

Each transactor declares `static constexpr ConsequencesFactoryType ConsequencesFactory{...}`:
- **`Normal`** — standard fee/sequence consequences. Most transactors.
- **`Blocker`** — queues block later transactions from same account. Examples: `SetRegularKey`, `AccountDelete`, `SignerListSet`, `XChainAddClaimAttestation`.
- **`Custom`** — derived class implements `makeTxConsequences(PreflightContext const&)`. Examples: `Payment` (XRP spend via `sfSendMax`), `OfferCreate` (XRP TakerGets), `TicketCreate` (multi-sequence), `AccountSet` (conditional blocker on auth/master flags), `LoanSet` (counterparty signers).

C++20 `requires` clauses in `applySteps.cpp` pick the factory at compile time.

### Numeric Precision Guards

`with_txn_type()` installs RAII guards before dispatch:
- When `featureSingleAssetVault` or `featureLendingProtocol` is active: `CurrentTransactionRulesGuard` (thread-local rules access) + `NumberSO` (floating-point-style number arithmetic per `fixUniversalNumber`).
- Otherwise: `NumberMantissaScaleGuard` (legacy small-mantissa mode).

Ideally these would apply everywhere from the start; they were retrofitted into `with_txn_type` for `preflight`/`preclaim` when vault/lending features needed correct numeric rules in read-only phases.

## Key Invariants

- Pipeline is strict: preflight runs WITHOUT ledger state, preclaim WITH read-only view, doApply with mutable view.
- `preflight` validates all fields exist and are well-formed; this is the ONLY place to reject malformed transactions cheaply.
- Fee is always deducted on `tecCLAIM`; `payFee` runs before `doApply`.
- Sequence/ticket consumption happens in `consumeSeqProxy`; must succeed before any state changes.
- Invariant checkers run after `doApply`; they can veto the transaction post-execution.
- Amendment gating belongs in `checkExtraFeatures`, NOT in `preflight`. The framework guards on the central permission registry first.
- `tem*`/`tef*`/`tel*` results: fee NOT charged, transaction not included. `tec*` results: fee charged, transaction included.

## State Commitment & tec* Rollback (CRITICAL for review)

**`doApply` mutations are NOT committed until `ctx_.apply()` is called at the end of `operator()`.** All peek/insert/update/erase during `doApply` go into an `ApplyContext` view (`view_`) layered on top of `base_`. Whether that view gets flushed to `base_` depends entirely on the TER that `doApply` returns.

`ApplyContext::discard()` replaces `view_` with a fresh view on `base_` — **every doApply mutation is thrown away**:
```cpp
void ApplyContext::discard() { view_.emplace(&base_, flags_); }
```

### Return-code decision table (in `Transactor::operator()`)

| doApply returns | What commits to the ledger |
|---|---|
| `tesSUCCESS` | All doApply mutations + fee + seq (via `ctx_.apply`) |
| `tec*` (normal, `!tapRETRY`) | `reset(fee)` calls `discard()`, then re-applies fee + seq only. **All doApply mutations reverted.** |
| `tec*` with `tapFAIL_HARD` | `discard()` called directly, nothing committed (not even fee) |
| `tec*` with `tapRETRY` | `applied=false`, `ctx_.apply` never called, tx re-queued |
| `tef*` / `tem*` / `ter*` | `applied=false`, `ctx_.apply` never called |
| `tecINVARIANT_FAILED` after invariants | reset again, commit fee only |

`isTecClaimHardFail(ter, flags) = isTecClaim(ter) && !(flags & tapRETRY)` — drives the reset path.

### What this means

- **A `tec*` return from doApply acts as a full-transaction rollback.** You do NOT need to order mutations defensively. If a helper called late in doApply returns `tec*`, everything mutated earlier is discarded.
- **Orphan-state bugs "we mutated X then returned tec* so X is now inconsistent" are not possible at the transactor boundary.**
- **The real failure mode**: stale SLE pointers, missing `view().update(sle)` after mutation, mutating values read by value. These are in-memory bugs, not state-commit bugs.
- **Sandboxes inside `doApply` add nesting, not safety.** `PaymentSandbox`/nested `ApplyView` are for conditionally committing a *subset* of changes within a single doApply.
- **Only `ctx_.apply(result)` publishes to `base_`**; a doApply that returns early, throws, or crashes never reaches that call.

### `reset()` Fee Clamping

`reset()` discards all ledger mutations via `ctx_.discard()` then re-deducts the fee, clamping if necessary:
```cpp
if (fee > balance) fee = balance;
```
This ensures a failing transaction can still claim its fee even when the account is over-committed.

### Verifying a suspected orphan-state bug

1. Read the caller of `doApply` — confirm the TER path (`operator()` in Transactor.cpp).
2. Check whether `discard()` is reached via `reset()` or the `tapFAIL_HARD` branch.
3. If both paths call `discard()`, the mutations cannot persist on tec*.
4. Look instead for: missing `view().update(sle)`, stale SLE pointers, or genuine non-atomic side effects (e.g., hash router flags — NOT in ApplyContext view).

## Apply Loop Details (Transactor::operator()())

1. RAII guards: `NumberSO`, `CurrentTransactionRulesGuard` (for `fixUniversalNumber`, `featureSingleAssetVault`, `featureLendingProtocol`)
2. Debug builds: serialize/re-parse round-trip catches serdes mismatches; `trapTransaction()` provides a named breakpoint for replaying specific transactions
3. `apply()` runs `preCompute()` → captures `preFeeBalance_` → `consumeSeqProxy()` → `payFee()` → updates `sfAccountTxnID` → `doApply()`
4. Enforces `tecOVERSIZE` if metadata exceeds `oversizeMetaDataCap`
5. Special `tec` codes (`tecOVERSIZE`, `tecKILLED`, `tecINCOMPLETE`, `tecEXPIRED`) trigger context-diff visitation then targeted cleanup: `removeUnfundedOffers`, `removeExpiredNFTokenOffers`, `removeDeletedTrustLines`, `removeDeletedMPTs`, `removeExpiredCredentials`
6. `ctx_.checkInvariants()` runs all 25+ invariants; failure causes second reset + re-check; second failure escalates to `tefINVARIANT_FAILED` (not included in ledger)
7. `tapDRY_RUN` forces `applied=false` unconditionally

## Common Bug Patterns

- New transaction type missing preflight validation for new fields = malformed transactions reach doApply and corrupt state
- Forgetting to handle `tecCLAIM` in doApply: fee is deducted but no other state changes should occur
- Batch transactions have their own signing path (`checkBatchSign`); changes to signing must cover both paths
- `calculateBaseFee` override without updating `minimumFee` causes fee calculation divergence between nodes
- Missing invariant checker update for new ledger entry types = silent constraint violations
- Forgetting amendment gating: place feature checks in `checkExtraFeatures`, NOT `preflight`
- Using `view().update()` on a stale SLE pointer after another mutation
- Computing reserve against `view().peek(account)->getFieldAmount(sfBalance)` AFTER fee deduction instead of `preFeeBalance_`
- Missing `associateAsset(*sle, asset)` call at end of `doApply` for SLEs with `STNumber` or `STTakesAsset` fields (lending/vault transactors)
- preclaim `Rules` race: if ledger advanced between preflight and preclaim, `applySteps.cpp` silently re-runs preflight with new rules before constructing `PreclaimContext`
- Calling `ter*` codes before signature verification in preclaim (see security invariant above)

## Transactor Template

### Header (`include/xrpl/tx/transactors/.../MyTx.h`)
```cpp
#pragma once
#include <xrpl/tx/Transactor.h>

namespace xrpl {
class MyTransaction : public Transactor {
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
    explicit MyTransaction(ApplyContext& ctx) : Transactor(ctx) {}

    static bool checkExtraFeatures(PreflightContext const& ctx);  // amendment gating
    static std::uint32_t getFlagsMask(PreflightContext const& ctx);
    static NotTEC preflight(PreflightContext const& ctx);  // NO ledger
    static TER preclaim(PreclaimContext const& ctx);        // read-only
    TER doApply() override;                                 // read-write
};
}
```

### Implementation
```cpp
bool MyTransaction::checkExtraFeatures(PreflightContext const& ctx)
{   // PREFERRED location for amendment checks
    return ctx.rules.enabled(featureMyFeature);
}

NotTEC MyTransaction::preflight(PreflightContext const& ctx)
{   // Static validation — NO ctx.view, NO ledger access
    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

TER MyTransaction::preclaim(PreclaimContext const& ctx)
{   // Read-only — ctx.view.read() only, NO peek/insert/erase
    if (!ctx.view.exists(keylet::account(ctx.tx[sfAccount])))
        return terNO_ACCOUNT;
    return tesSUCCESS;
}

TER MyTransaction::doApply()
{   // Mutable — view().peek(), view().insert(), view().update(), view().erase()
    auto sle = view().peek(keylet::account(account_));
    sle->setFieldAmount(sfBalance, newBal);
    view().update(sle);  // REQUIRED after mutation
    return tesSUCCESS;
}
```

### Registration Checklist
```cpp
// ALL of these are REQUIRED for a new transaction type:
// 1. transactions.macro: TRANSACTION(ttMY_TYPE, N, MyTx, delegation, fields, privileges)
// 2. applySteps.cpp:     case ttMY_TYPE: dispatched via X-macro automatically
// 3. features.macro:     XRPL_FEATURE(MyFeature, Supported::yes, DefaultNo)
// 4. Feature.h:          increment numFeatures
// 5. InvariantCheck.cpp: update privilege mask + checkers if new ledger objects
// 6. Batch.cpp:          add to disabledTxTypes if not batch-compatible
// 7. Permission table:   add granular permissions if delegable
```

### Common Field Constraints (constants in `Protocol.h`)
- `maxCredentialURILength` = 256, `maxCredentialTypeLength` = 64
- `maxTokenURILength` = 256 (NFT URI), `dirMaxTokensPerPage` = 32
- `maxMultiSigners` = 32, `MaxPathSize` = 6, `MaxPathLength` = 8
- `maxBatchTxCount` = 8, `maxOracleDataSeries` = 10
- `maxPermissionedDomainCredentialsArraySize` = 10
- `maxDeletableTokenOfferEntries` = 500, `maxDeletableDirEntries` = 1000
- `maxDeletableAMMTrustLines` = 512, `maxMPTokenAmount` = 0x7FFF_FFFF_FFFF_FFFF
- `maxDataPayloadLength`, `maxMPTokenMetadataLength` = 1024

## The Big Patterns

### Sandbox Pattern (Atomic Sub-operation)

Used when multiple mutations must all succeed or all be discarded *within* a single `doApply`:

```cpp
TER doApply() override {
    Sandbox sb(&view());
    auto const result = applyGuts(sb, ...);
    if (isTesSuccess(result))
        sb.apply(ctx_.rawView());
    return result;
}
```

Variants:
- `PaymentSandbox` — for `flow()` calls (used by `Payment`, `CheckCash`, `OfferCreate` crossing). Required because `flow()` uses deferred-credit accounting.
- `RippleCalc::rippleCalculate()` wraps its own `PaymentSandbox` inside the caller's sandbox (double-sandbox pattern) for exception safety — if `flow()` throws, the caller's sandbox remains unmodified.
- Dual sandbox in `OfferCreate`: `sb` (main) + `sbCancel` (offer cleanup); commit one or the other based on `tfFillOrKill` outcome.
- Nested sandboxes: `applyBatchTransactions` uses `wholeBatchView` (over outer view) + `perTxBatchView` (per inner tx).

### Reserve Check Convention

ALWAYS check against `preFeeBalance_` (snapshot before fee deduction), not the current post-fee balance. This deliberately allows accounts to dip into reserve to pay the fee while still requiring full reserve coverage for new owned objects.

```cpp
auto const reserve = view().fees().accountReserve(ownerCount + 1);
if (preFeeBalance_ < reserve)
    return tecINSUFFICIENT_RESERVE;
```

### Owner Directory + Owner Count Pattern

Creating an owned object:
1. `view().dirInsert(keylet::ownerDir(owner), key, ...)` → returns page index
2. Store page index in SLE's `sfOwnerNode` (and `sfDestinationNode`, `sfIssuerNode`, etc., for multi-party objects)
3. `adjustOwnerCount(view, sleOwner, +N, j)` where N is the reserve cost
4. `view().insert(sle)`

Deleting an owned object:
1. Read `sfOwnerNode` (etc.) from SLE
2. `view().dirRemove(keylet::ownerDir(owner), pageIndex, key, false)` — O(1) using cached page
3. `adjustOwnerCount(view, sleOwner, -N, j)`
4. `view().erase(sle)`

Reserve cost is usually 1 unit per object, but:
- `AccountDelete`, `LedgerStateFix`, `AMMCreate` charge a full reserve via `calculateOwnerReserveFee` instead of base fee
- Two-object structures (`Vault`, `LoanBroker`) charge +2 for object + pseudo-account (incremented before reserve check so check reflects true post-creation state)
- `SignerListSet` post-amendment uses `lsfOneOwnerCount` flag (1 unit regardless of N signers); pre-amendment charges 2+N
- `OracleSet` uses tiered count: 1 unit for ≤5 price pairs, 2 units for more

### Pseudo-Account Pattern

Synthetic `AccountRoot` SLEs with disabled master key, used to hold protocol-managed assets on behalf of users. Created via `createPseudoAccount(view, ownerKey, sfDiscriminator)`. Examples and their discriminator fields:

| Construct | Discriminator | Owns |
|---|---|---|
| AMM | `sfAMMID` | LP token issuance, both pool asset trustlines/MPTokens |
| Vault | `sfVaultID` | Vault asset holding, share MPTokenIssuance |
| LoanBroker | `sfLoanBrokerID` | Cover capital holding |

Pseudo-account guard rules:
- `ValidPseudoAccounts` invariant: exactly one discriminator field, sequence never changes, required flags (`lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth`), no `sfRegularKey`
- For pseudo-accounts, initial sequence must be 0 and flags must be exactly `lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth`
- Many transactors explicitly reject pseudo-account destinations (`tecPSEUDO_ACCOUNT`): `Payment` direct, `CheckCreate`, `PaymentChannelCreate`, `VaultCreate` (asset issuer), `Clawback` (holder)
- `MPTokenAuthorize` issuer-path skips pseudo-account holders (they are implicitly always authorized)
- Pseudo-accounts cannot sign — when `featureLendingProtocol` active, `checkSign` rejects with `tefBAD_AUTH`
- Anti-nesting: AMM preclaim detects LP-token-issuer pseudo-accounts via `sfAMMID` on the issuer's `AccountRoot` and rejects using them as AMM assets

### `associateAsset` Convention

After mutating any SLE that contains `STNumber` or `STTakesAsset`-derived fields (loan, broker, vault objects), call `associateAsset(*sle, asset)` at the end of `doApply`. This re-rounds stored numeric values to the asset's precision. Per `STTakesAsset.h` contract, this must be the last operation after all writes are complete. Failing to call it produces silent precision corruption.

## Permission & Delegation System

### `checkPermission` (called from preclaim)

Validates the optional `sfDelegate` field. If absent, normal account signing applies. If present:
1. Read `DelegateObject` at `keylet::delegate(account, delegate)`; missing → `terNO_DELEGATE_PERMISSION`
2. Try full transaction-type permission via `checkTxPermission()` (uses `TxType + 1` encoding)
3. Fall back to granular permission via `loadGranularPermission()` + per-transactor logic

### Encoding Convention

Permission values store both forms in a single `uint32_t`:
- Transaction types: `TxType + 1` (always ≤ `UINT16_MAX`; `+1` avoids ambiguous zero since `ttPAYMENT == 0`)
- Granular permissions: values `> UINT16_MAX`, enumerated in `permissions.macro`

`Permission` singleton asserts this separation at construction time.

### Granular Permissions

`DelegateUtils.cpp` provides:
- `checkTxPermission()` — linear scan for `TxType + 1` match; returns `terNO_DELEGATE_PERMISSION` on null delegate or no match
- `loadGranularPermission()` — populates per-tx-type granular set via `Permission::getInstance().getGranularTxType()` reverse-map; returns silently with empty set on null delegate

Examples of granular permissions:
- `Payment` direct only: `PaymentMint` (issuer source), `PaymentBurn` (issuer destination) — blocked if `sfPaths` present or asset conversion
- `AccountSet`: field-level grants per metadata field (`AccountDomainSet`, `AccountTransferRateSet`, etc.); flag changes blocked entirely
- `TrustSet`: `TrustlineAuthorize`, `TrustlineFreeze`, `TrustlineUnfreeze`; cannot create new lines, cannot change limit
- `MPTokenIssuanceSet`: `MPTokenIssuanceLock`, `MPTokenIssuanceUnlock`

## Permission Model & Cross-Transactor Static Interfaces

Several transactors expose static deletion/creation methods on `ApplyView` so other transactors (especially `AccountDelete`) can clean up owned objects without constructing a fake transaction:

- `DepositPreauth::removeFromLedger(ApplyView&, uint256, Journal)`
- `DIDDelete::deleteSLE(ApplyView&, SLE, AccountID, Journal)`
- `OracleDelete::deleteOracle(ApplyView&, SLE, AccountID, Journal)`
- `DelegateSet::deleteDelegate(ApplyView&, SLE, AccountID, Journal)`
- `SignerListSet::removeFromLedger(ApplyView&, ServiceRegistry&, AccountID, Journal)`
- `MPTokenIssuanceCreate::create(ApplyView&, Journal, MPTCreateArgs)` — used by `VaultCreate` to mint share token
- `AMMWithdraw::withdraw`/`equalWithdrawTokens` — used by `AMMClawback`, `AMMDelete`
- `LoanManage::unimpairLoan/impairLoan/defaultLoan` — used by `LoanPay`

`AccountDelete` uses a `nonObligationDeleter()` switch over `LedgerEntryType` returning a `DeleterFuncPtr`. `nullptr` means "obligation, cannot delete". The same switch is used in both `preclaim` (to detect blockers) and `doApply` (to invoke deletions), keeping type classification in sync. Deletable types: offers, signer lists, tickets, deposit preauth, NFT offers, DIDs, oracles, credentials, delegates.

### AccountDelete-specific preclaim rules

- NFT obligations: `sfMintedNFTokens != sfBurnedNFTokens` → `tecHAS_OBLIGATIONS`; authorized-minting replay guard: `FirstNFTokenSequence + MintedNFTokens + 255` must not exceed current ledger sequence
- Sequence freshness: account seq must be ≥ 256 below current ledger index (`tecTOO_SOON`) — prevents replay after resurrection
- Owner directory: more than `maxDeletableDirEntries` (1000) deletable items → `tefTOO_BIG`

## Signature Verification

`checkSign()` (in preclaim) dispatches:
1. **Batch inner** (`tfInnerBatchTxn`): asserts no key/sig/signers; outer batch authorized them
2. **Dry-run** (`tapDRY_RUN`): skipped if no key/signers
3. **Multi-sign** (`sfSigners` present): delegates to `checkMultiSign()`
4. **Single sig**: derives signer from public key, calls `checkSingleSign()`

`checkSingleSign()` precedence: regular key → enabled master key → `tefMASTER_DISABLED`.

`checkMultiSign()` performs O(n) linear merge of sorted `sfSigners` against the sorted `SignerEntry` list from the account's signer list SLE. Terminates with `tefBAD_QUORUM` if accumulated weight < `sfSignerQuorum`.

`checkBatchSign()` validates the outer batch transaction's `sfBatchSigners` array. Outer account is excluded from `sfBatchSigners`; unsigned-account inner transactions (e.g., funding an account creation) are permitted if signed by their master key.

`LoanSet::checkSign()` overrides to verify both the primary signer AND the `sfCounterpartySignature` sub-object (which may itself be single or multisig). `calculateBaseFee` adds one `baseFee` per counterparty signer.

## Validation Helpers (in `Transactor`)

- `validNumericRange<T>(opt, min, max)` — absent optional is valid
- `validNumericMinimum<T>(opt, min)` — absent optional is valid
- Overloads for `unit::ValueUnit<Unit, T>` for type-safe units

These follow the convention that an absent optional field is valid; only present values are range-checked.

## Invariant Checker Framework

After every successful or fee-claiming transaction, every checker in the `InvariantChecks` tuple runs. Two-phase: `visitEntry(isDelete, before, after)` per modified SLE, then `finalize(tx, result, fee, view, journal)` once.

### Dispatch (in `ApplyContext::checkInvariantsHelper`)

Uses `std::index_sequence` + fold expression for variadic visit. Critically, `finalize()` results are collected into a `std::array<bool>` then checked with `std::all_of` — NOT short-circuited with `&&` — so every failing invariant logs its own diagnostic.

Invariant checkers run even on failed (`tec*`) transactions — bugs or exploits could cause a failed transaction to mutate ledger state unexpectedly.

### `failInvariantCheck` Escalation

- First failure → `tecINVARIANT_FAILED` (committed to ledger, fee charged)
- Repeated failure during retry (recognized because incoming result is already `tecINVARIANT_FAILED` or `tefINVARIANT_FAILED`) → `tefINVARIANT_FAILED` (not included in ledger)

Rationale: if even the minimal fee-charge path breaks invariants, no ledger entry of any kind should be created.

### The `enforce` Pattern (Soft Rollout)

```cpp
bool const enforce = view.rules().enabled(featureX);
if (violation) {
    JLOG(j.fatal()) << "...";
    XRPL_ASSERT(enforce, "...");  // fires in debug builds regardless
    return !enforce;              // returns true (passes) if amendment off
}
```

This lets invariants ship before activation: violations log unconditionally (visible to operators), assertion fires in debug/test builds (catches dev mistakes), but only become consensus-breaking when the gating amendment activates.

### Privilege System (`InvariantCheckPrivilege.h`)

`Privilege` bitmask enum + `hasPrivilege(STTx, Privilege)` (implemented via `transactions.macro` X-macro). Used by checkers to know what each transaction type may legitimately do. `must` vs. `may` variants let invariants enforce cardinality (e.g., `AccountDelete` *must* delete exactly one account root; `AMMWithdraw` *may* delete one).

| Privilege | Granted to (examples) |
|---|---|
| `createAcct` | `Payment` (XRP funding) |
| `createPseudoAcct` | `AMMCreate`, `VaultCreate`, `LoanBrokerSet` |
| `mustDeleteAcct` | `AccountDelete`, `AMMDelete` |
| `mayDeleteAcct` | `AMMWithdraw`, `AMMClawback` |
| `overrideFreeze` | `AMMClawback` (only against AMM trust lines, not global freeze) |
| `changeNFTCounts` | `NFTokenMint`, `NFTokenBurn` |
| `createMPTIssuance` / `destroyMPTIssuance` | `MPTokenIssuanceCreate`/`Destroy`, also `VaultCreate`/`Delete` |
| `mustAuthorizeMPT` / `mayAuthorizeMPT` | `MPTokenAuthorize`, AMM withdraw/clawback |
| `mayCreateMPT` / `mayDeleteMPT` | `Payment`, `CheckCash`, `AMMCreate`, `AMMDelete` |
| `mustModifyVault` / `mayModifyVault` | Vault transactors, loan transactors |

### The 25+ Registered Invariants

| Checker | What it enforces |
|---|---|
| `TransactionFeeCheck` | Fee non-negative, < INITIAL_XRP, ≤ sfFee |
| `XRPNotCreated` | Net XRP delta across accounts/paychans/escrows = -fee (pay channels tracked as `sfAmount - sfBalance`) |
| `XRPBalanceChecks` | Every account balance is native XRP in [0, INITIAL_XRP] |
| `NoBadOffers` | No negative-amount, no XRP-for-XRP offers |
| `NoZeroEscrow` | Escrow/MPT amounts within bounds; MPT locked ≤ outstanding; also validates `ltMPTOKEN_ISSUANCE` and `ltMPTOKEN` entries |
| `AccountRootsNotDeleted` | Account deletion cardinality matches `must`/`may` privilege |
| `AccountRootsDeletedClean` | Deleted account had zero balance + zero owner count + no orphaned objects; uses `before` SLE for pseudo-account linked object keys (fields may be cleared during deletion) |
| `ValidNewAccountRoot` | New accounts only from `createAcct`/`createPseudoAcct`; correct initial seq + flags |
| `ValidPseudoAccounts` | Exactly one discriminator, sequence unchanged, required flags, no regular key; errors accumulated in `vector<string>` and all logged before returning |
| `ValidClawback` | At most one trust line/MPT modified, holder balance non-negative |
| `NoModifiedUnmodifiableFields` | `sfLedgerEntryType`/`sfLedgerIndex` immutable; loan/broker origination fields immutable; gated on `featureLendingProtocol` |
| `LedgerEntryTypesMatch` | Modified entries don't change type; new entries are recognized types |
| `NoXRPTrustLines` | No trust line uses XRP as currency |
| `NoDeepFreezeTrustLinesWithoutFreeze` | DeepFreeze flag requires regular Freeze flag |
| `TransfersNotFrozen` | Trust line transfers respect global/per-line/deep freeze (gated `featureDeepFreeze`) |
| `ValidNFTokenPage` | Page links coherent, size 1-32 tokens, sorted, valid URIs |
| `NFTokenCountTracking` | `sfMintedNFTokens`/`sfBurnedNFTokens` only change with `changeNFTCounts` privilege; strict monotonic increase on success |
| `ValidMPTIssuance` | MPT issuance/holder counts match transaction privileges |
| `ValidMPTPayment` | OutstandingAmount = sum(holder MPTAmount + LockedAmount); overflow detection |
| `ValidAMM` | Per-tx-type rules: create exact `sqrt(A*B)`, deposit/withdraw constant-product invariant `sqrt(x*y) ≥ LPSupply`, vote/bid leave pool unchanged |
| `ValidPermissionedDomain` | AcceptedCredentials non-empty, ≤ max size, unique, sorted |
| `ValidPermissionedDEX` | Domain-scoped tx only touches offers/dirs with matching domain; hybrid offers structurally valid |
| `ValidVault` | Per-tx-type rules: deposit/withdraw asset/share conservation, immutable fields unchanged, loss only via loan ops; XRP vault fee compensation for depositor/withdrawer balance check |
| `ValidLoan` | Payment completion bidirectional (paymentRemaining=0 ↔ all outstanding=0), `lsfLoanOverpayment` immutable, non-negative fees, positive `sfPeriodicPayment` |
| `ValidLoanBroker` | Sequence monotonic, non-negative cover/debt, vault exists, cover ≤ pseudo-account balance (== under `fixSecurity3_1_3` except at delete); no amendment gate (objects can't exist unless amendment is active) |

## doApply Order Convention (Cleanup)

When erasing an SLE that participates in directories, the order is **always**:
1. Remove from owner directory (and destination/issuer directory if applicable) via `dirRemove` with stored `sfOwnerNode`/etc.
2. `adjustOwnerCount(view, sleOwner, -N, j)`
3. `view().erase(sle)`

Erasing first would lose the page index needed for `dirRemove`. Many transactors guard `dirRemove` failure with `tefBAD_LEDGER` and `LCOV_EXCL` markers — these branches represent ledger corruption rather than user error.

## Failure Modes Worth Special-Casing

- `tecOVERSIZE`: metadata too large. `operator()` re-runs `doApply` after `reset()` to collect cleanup targets only
- `tecINCOMPLETE`: progress was made but more work remains. `AMMDelete` and `VaultDelete` commit partial work on this code — caller resubmits
- `tecPATH_DRY`: payment path exhausted. `Payment` converts retry codes from `RippleCalc` to this (forces fee deduction, prevents path-spam)
- `tecKILLED`: order/loan time-window expired or sequence overflow (`LoanSet` arithmetic overflow check)
- `tecEXPIRED`: legitimately expired object; some transactors (e.g., `NFTokenAcceptOffer` under `fixExpiredNFTokenOfferRemoval`) clean up before returning this
- `tecINSUFFICIENT_RESERVE`: reserve check failed against `preFeeBalance_`
- `tecINTERNAL` / `tefBAD_LEDGER`: ledger corruption sentinels. Often marked `LCOV_EXCL` because preclaim should have prevented them. `RippleCalc` converts exceptions to `tecINTERNAL` rather than rethrowing (deterministic fallback all validators agree on)
- `terNO_AMM`, `terNO_DELEGATE_PERMISSION`, `terNO_ACCOUNT`, `terNO_LINE`: retryable failures

## Hash Router Caching

Some expensive operations cache results in the `HashRouter` using private flag bits to avoid recomputation across multiple validation passes:

- **Signature verification** (`apply.cpp` `checkValidity`): `SF_SIGBAD`, `SF_SIGGOOD`, `SF_LOCALBAD`, `SF_LOCALGOOD` (PRIVATE1–PRIVATE4)
- **Crypto-condition validation** (`EscrowFinish::preflightSigValidated`): `SF_CF_VALID`, `SF_CF_INVALID` (PRIVATE5–PRIVATE6)

The `forceValidity()` API can promote cached state (using `[[fallthrough]]`) but cannot downgrade (never sets `SF_SIGBAD`) — used to mark locally-submitted transactions as pre-verified. **Use with extreme care**: bypassing signature verification in the cache affects every subsequent `checkValidity` call on the same hash until cache expiry.

Validity enum → P2P semantics: `SigBad` = don't forward; `SigGoodOnly` = relay but don't apply; `Valid` = relay and apply.

## Batch Transactions

`Batch` (in `system/Batch.cpp`) bundles 2-8 inner transactions with one of four execution policies (mutually exclusive, enforced via `std::popcount`):
- `tfAllOrNothing`: any failure aborts, full rollback (`applyBatchTransactions` returns false)
- `tfUntilFailure`: stop at first failure, keep prior successes (returns false if no inner tx was ever applied)
- `tfOnlyOne`: stop at first success
- `tfIndependent`: run all, commit successes

`Batch::doApply()` returns `tesSUCCESS` and does nothing — `applyBatchTransactions()` in `apply.cpp` is called separately by `applyTransaction()` after the outer apply succeeds, executing inner txs in a nested `wholeBatchView`/`perTxBatchView` sandbox structure.

**Critical for new transactors:** Update `disabledTxTypes` in `Batch.cpp` if your type cannot run inside a batch. Currently disabled: all `ttVAULT_*` and `ttLOAN_*` types (multi-step state machines whose invariants are difficult to reason about under batch atomicity).

Inner transaction rules (enforced in `Batch::preflight`):
- `tfInnerBatchTxn` flag must be set
- Empty `sfSigningPubKey`, no `sfTxnSignature`, no `sfSigners`
- Fee = 0 XRP
- Exactly one of `sfSequence` (nonzero) or `sfTicketSequence`
- For `tfAllOrNothing`/`tfUntilFailure`: no duplicate sequence/ticket values across same-account inner txs (relaxed for `tfIndependent`/`tfOnlyOne`)
- Each inner tx has `xrpl::preflight` called on it with `tapBATCH` and `parentBatchId`; no nested `ttBATCH`

`Batch::preflightSigValidated` reconciles `sfBatchSigners` bidirectionally: each signer removed from `requiredSigners` as matched; any signer not in `requiredSigners` → `temBAD_SIGNER`; outer account explicitly excluded. Then `ctx.tx.checkBatchSign(ctx.rules)` verifies the cryptographic batch signature payload.

`Batch::calculateBaseFee` = `baseFee + Σ(inner tx fees) + numSigners × baseFee`. Overflow guards everywhere (marked `LCOV_EXCL`).

**`fixBatchInnerSigs` amendment**: corrects a bug in the original Batch implementation where inner-batch transactions could be assigned `SF_SIGGOOD` cache entries (implying valid signatures on unsigned objects). After the fix, inner-batch transactions follow the `neverValid` path.

All Batch log messages use `BatchTrace[<parentBatchId>]` prefix for correlation.

## Key Files

- `src/libxrpl/tx/Transactor.cpp` - base class, three-phase pipeline, fee calculation, signature dispatch
- `src/libxrpl/tx/ApplyContext.cpp` - sandboxed view management, `discard()`, invariant orchestration
- `src/libxrpl/tx/apply.cpp` - top-level `apply()`, `checkValidity()` caching, `applyBatchTransactions()`
- `src/libxrpl/tx/applySteps.cpp` - X-macro dispatch via `with_txn_type`, `TxConsequences` factories
- `src/libxrpl/tx/SignerEntries.cpp` - multi-sig signer list deserialization (`SignerEntries::deserialize`)
- `include/xrpl/protocol/detail/transactions.macro` - canonical type definitions, privileges, features
- `src/libxrpl/tx/transactors/.../` - one subdirectory per feature family (account, dex, escrow, lending, vault, etc.)
- `src/libxrpl/tx/invariants/` - 25+ invariant checkers; add new ones to `InvariantChecks` tuple in `InvariantCheck.h`
- `src/libxrpl/tx/paths/` - payment flow engine (`Flow.cpp`, `StrandFlow.h`, `BookStep.cpp`, `RippleCalc.cpp`) used by `Payment`, `CheckCash`, `OfferCreate` crossing

## Payment Path Engine Notes

`Payment`, `OfferCreate` (crossing), and `CheckCash` (IOU/MPT) all route through `flow()` in `Flow.cpp` → `StrandFlow.h`. Key concepts:

- A **strand** is a `std::vector<std::unique_ptr<Step>>`; each `Step` is one hop (`DirectStepI`, `BookStepXX`, `XRPEndpointStep`, `MPTEndpointStep`)
- `flow()` is templated on `(TIn, TOut)` pairs for the three asset types (6 combinations). `Flow.cpp` is the façade that resolves runtime `STAmount`/`Asset` values into compile-time template parameters via `std::visit`, then hands off to `StrandFlow.h`.
- Two-pass execution: reverse pass (compute required input for desired output) then forward pass (compute output for actual input)
- Limiting step detection: if reverse pass cannot satisfy desired output, that step is identified as the bottleneck and used as the anchor for forward pass
- Multi-strand flow uses `ActiveStrands` priority queue sorted by `qualityUpperBound`; one strand consumed per outer iteration (probe-and-push)
- Safety limits: `MaxOffersToConsume` = 1000 per book step, `maxTries` = 1000 outer iterations, `maxOffersToConsider` = 1500 cumulative, `AMMContext::MaxIterations` = 30
- `PaymentSandbox` (not regular `Sandbox`) is required because `flow()` uses deferred-credit accounting
- AMM offers are synthesized by `AMMLiquidity` to look like CLOB offers to `BookStep`; single-path uses `changeSpotPriceQuality`, multi-path uses Fibonacci-scaled offer sizes; `AMMContext` tracks whether multi-path is active (disables quality optimization)
- `RippleCalc::rippleCalculate()` creates a nested `PaymentSandbox` inside the caller's `PaymentSandbox` (exception safety); `flow()` applies its internal sandbox to `flowSB` only on success
- `ter*` retry codes from `RippleCalc` are converted to `tecPATH_DRY` in `Payment::doApply` (forces fee charge, prevents path-spam)
- `sfDeliverMin` + `tfPartialPayment`: if actual delivery < `sfDeliverMin` → `tecPATH_PARTIAL`; `ctx_.deliver()` records actual delivered amount for metadata (critical for partial payment detection downstream)
- `std::optional<uint256> domainID` threads through `toStrands()` for permissioned payment domains

### `sendMax` semantics in `RippleCalc`

`sendMax` is `nullopt` when sending the same IOU that the destination receives with sender as issuer (no separate spending cap needed). Otherwise set to `saMaxAmountReq`. `limitQuality` is only constructed when `pInputs->limitQuality && saMaxAmountReq > beast::zero`.

## Asset Type Dispatch Pattern

Modern transactors that support both IOU (`Issue`) and MPT (`MPTIssue`) assets use template specialization + `std::visit` rather than runtime branching. The pattern:

```cpp
TER MyTx::preclaim(PreclaimContext const& ctx) {
    return std::visit(
        [&]<typename T>(T const&) { return preclaimHelper<T>(ctx); },
        ctx.tx[sfAmount].asset().value());
}

template <ValidIssueType T>
static TER preclaimHelper(PreclaimContext const& ctx);
template <> TER preclaimHelper<Issue>(...);
template <> TER preclaimHelper<MPTIssue>(...);
```

Used by `Clawback`, `Escrow*`, `Vault*`, `AMM*Withdraw/Deposit`, `LoanBrokerCoverClawback`. Each specialization handles asset-type-specific permission flags (`lsfAllowTrustLineClawback`/`lsfNoFreeze` vs `lsfMPTCanClawback`), authorization (`StrongAuth` vs `WeakAuth`), and freeze checks (`tecFROZEN` vs `tecLOCKED`).

## Lending Protocol (XLS-66)

`LendingHelpers.cpp` is the numerical core. Key concepts:

- **Amortization math**: `loanPeriodicPayment()` uses standard `r(1+r)^n / ((1+r)^n - 1)` factor (Eq. 6–7 in XLS-66 Eq. Glossary). Zero-interest path uses equal principal slices (no division by zero).
- **Theoretical vs. rounded state**: `LoanProperties` holds both; `computeTheoreticalLoanState()` computes at full precision; `constructRoundedLoanState()` reflects actual ledger values. Rounding errors are carried forward during re-amortization.
- **Payment types**: regular, late (penalty via `loanLatePaymentInterest`), full/early-closure, overpayment (triggers re-amortization via `tryOverpayment()`).
- **`checkLoanGuards()`** enforces 4 precision invariants at creation/re-amortization: measurable interest, positive first-payment principal, non-zero rounded payment, payment count math. All return `tecPRECISION_LOSS`.
- **Template proxy pattern**: `doPayment<NumberProxy, UInt32Proxy, UInt32OptionalProxy>` runs against real SLE (via `ValueProxy`) or simulation values — same code path for both.
- `loanMakePayment()` dispatches to the correct payment type and runs up to `loanMaximumPaymentsPerTransaction` installments per call.

## Vault Architecture

Six vault transactors: `VaultCreate`, `VaultDeposit`, `VaultWithdraw`, `VaultSet`, `VaultDelete`, `VaultClawback`. Key creation invariants (see `VaultCreate.cpp`):

- `sfWithdrawalPolicy` currently only accepts `vaultStrategyFirstComeFirstServe` (= 1)
- `sfDomainID` is only valid when `tfVaultPrivate` is set
- `sfScale` restricted: meaningless for XRP/MPT assets; bounded above by `vaultMaximumIOUScale` (18)
- Vault pseudo-account asset issuer cannot be another pseudo-account (`tecWRONG_ASSET`) — those assets have no clawback path
- `adjustOwnerCount` increments by **2** (vault + pseudo-account) before reserve check
- MPT share issuance flags derived from transaction flags: tradeable unless `tfVaultShareNonTransferable`; `lsfMPTRequireAuth` added for private vaults
- `associateAsset` is the final call in `doApply`

`ValidVault` invariant uses a delta-map (`uint256 → Number`) with sign conventions per entry type (+1 for share issuance outstanding amount, -1 for asset balances). Entries captured even at zero delta for accounting completeness. Fee compensation applied for XRP vault balance deltas (skipped for delegated transactions).
