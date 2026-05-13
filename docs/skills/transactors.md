# Transactors

Transaction processing pipeline: preflight (static validation) -> preclaim (ledger state checks) -> doApply (state mutation). Base class `Transactor` in `src/libxrpl/tx/`.

## Pipeline Architecture

### Three Phases

1. **`preflight`** — stateless, no ledger access. Validates fields, flags, signatures (cached via HashRouter). Cheap, parallelizable. Returns `NotTEC`. Caller-cacheable.
2. **`preclaim`** — read-only `ReadView` access. Checks account exists, fee sufficient, sequence valid. Returns `TER`. Sets `likelyToClaimFee` for relay decisions.
3. **`doApply`** — mutable `ApplyView` access. Only runs if `preclaim` returned `tesSUCCESS` and `likelyToClaimFee` is true.

`apply()` in `apply.cpp` composes all three; templated on a preflight callable so the same `preclaim`+`doApply` machinery serves normal and batch-inner transactions. `applyTransaction()` adds `tapRETRY` semantics and dispatches to `applyBatchTransactions()` after a successful `ttBATCH`.

### Compile-time Polymorphism (Name Hiding, Not Virtual)

`Transactor::invokePreflight<T>` calls `T::checkExtraFeatures`, `preflight1`, `T::getFlagsMask`, `T::preflight`, `preflight2`, `T::preflightSigValidated` by name. The static methods on derived classes participate via name hiding — derived classes MUST NOT define `invokePreflight` themselves, nor call `preflight1`/`preflight2` directly. Only `doApply()` is virtual.

`with_txn_type()` in `applySteps.cpp` uses an X-macro over `transactions.macro` to convert runtime `TxType` to a compile-time template parameter via a switch dispatch — no virtual dispatch, no included transactor headers (forbidden in `applySteps.cpp`).

### `ConsequencesFactoryType`

Each transactor declares `static constexpr ConsequencesFactoryType ConsequencesFactory{...}`:
- **`Normal`** — standard fee/sequence consequences. Most transactors.
- **`Blocker`** — queues block later transactions from same account. Examples: `SetRegularKey`, `AccountDelete`, `SignerListSet`, `XChainAddClaimAttestation`, `XChainClaim` (any attestation may trigger immediate fund movement).
- **`Custom`** — derived class implements `makeTxConsequences(PreflightContext const&)`. Examples: `Payment` (XRP spend), `OfferCreate` (XRP TakerGets), `XChainCommit`, `TicketCreate` (multi-sequence), `AccountSet` (conditional blocker on auth/master flags), `LoanSet` (counterparty signers).

The `consequences_helper` dispatch in `applySteps.cpp` uses C++20 `requires` clauses to pick the right factory at compile time.

## Key Invariants

- Pipeline is strict: preflight runs WITHOUT ledger state, preclaim runs WITH read-only view, doApply runs with mutable view
- `preflight` validates all fields exist and are well-formed; this is the ONLY place to reject malformed transactions cheaply
- Fee is always deducted on `tecCLAIM`; `payFee` runs before `doApply`
- Sequence/ticket consumption happens in `consumeSeqProxy`; must succeed before any state changes
- Invariant checkers run after `doApply`; they can veto the transaction post-execution
- Amendment gating belongs in `checkExtraFeatures`, NOT in `preflight`. The framework guards on the central permission registry first.
- `tem*`/`tef*`/`tel*` results: fee NOT charged, transaction not included. `tec*` results: fee charged, transaction included.

## State Commitment & tec* Rollback (CRITICAL for review)

**`doApply` mutations are NOT committed until `ctx_.apply()` is called at the end of `operator()`.** All peek/insert/update/erase during `doApply` go into an `ApplyContext` view (`view_`) layered on top of `base_`. Whether that view gets flushed to `base_` depends entirely on the TER that `doApply` returns.

`ApplyContext::discard()` ([src/libxrpl/tx/ApplyContext.cpp](src/libxrpl/tx/ApplyContext.cpp)) replaces `view_` with a fresh view on `base_` — **every doApply mutation is thrown away**:
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

- **A `tec*` return from doApply acts as a full-transaction rollback.** You do NOT need to order mutations defensively. If a helper called late in doApply returns `tec*`, everything mutated earlier in the same doApply is discarded via `discard()`.
- **Orphan-state bugs of the form "we mutated X then returned tec* so X is now in an inconsistent state" are not possible at the transactor boundary.**
- **The real failure mode is within `doApply` itself**: stale SLE pointers, missing `view().update(sle)` after mutation, mutating values read by value instead of peek. These are in-memory bugs, not state-commit bugs.
- **Sandboxes inside `doApply` add nesting, not safety.** `PaymentSandbox` / nested `ApplyView` are useful when you need to conditionally commit a subset of changes *within* a single doApply (e.g., apply offers but revert if the net outcome fails). They are not needed to protect against doApply's own `tec*` return.
- **Only `ctx_.apply(result)` publishes to `base_`**; a doApply that returns early, throws, or crashes never reaches that call.

### Verifying a suspected orphan-state bug

Before claiming "directory removed but SLE not erased because tec\*":
1. Read the caller of `doApply` — confirm the TER path (`operator()` in Transactor.cpp).
2. Check whether `discard()` is reached via `reset()` or the `tapFAIL_HARD` branch.
3. If both paths call `discard()`, the mutations cannot persist on tec\*.
4. Look instead for: missing `view().update(sle)` after mutation, stale SLE pointers, or genuine non-atomic side effects (e.g., hash router flags, which are NOT in the ApplyContext view).

## Apply Loop Details (Transactor::operator()())

1. RAII guards: `NumberSO`, `CurrentTransactionRulesGuard` (for `fixUniversalNumber`, `featureSingleAssetVault`, `featureLendingProtocol`)
2. Debug builds: serialize/re-parse round-trip catches serdes mismatches
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
- Two-object structures (`Vault`, `LoanBroker`) charge +2 for object + pseudo-account
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
- Many transactors explicitly reject pseudo-account destinations (`tecPSEUDO_ACCOUNT`): `Payment` direct, `CheckCreate`, `PaymentChannelCreate`, `VaultCreate` (asset issuer), `Clawback` (holder)
- `MPTokenAuthorize` issuer-path skips pseudo-account holders (they are implicitly always authorized)
- Pseudo-accounts cannot sign — when `featureLendingProtocol` active, `checkSign` rejects with `tefBAD_AUTH`

### `associateAsset` Convention

After mutating any SLE that contains `STNumber` or `STTakesAsset`-derived fields (loan, broker, vault objects), call `associateAsset(*sle, asset)` at the end of `doApply`. This re-rounds stored numeric values to the asset's precision. Per `STTakesAsset.h` contract, this must be the last operation after all writes are complete. Failing to call it produces silent precision corruption.

## Permission & Delegation System

### `checkPermission` (called from preclaim)

Validates the optional `sfDelegate` field. If absent, normal account signing applies. If present:
1. Read `DelegateObject` at `keylet::delegate(account, delegate)`; missing → `terNO_DELEGATE_PERMISSION`
2. Try full transaction-type permission via `checkTxPermission()` (uses `TxType + 1` encoding)
3. Fall back to granular permission via `loadGranularPermission()` + per-transactor logic

### Granular Permissions

Permission values store both forms in single `uint32_t`:
- Transaction types: `TxType + 1` (always ≤ `UINT16_MAX`, since `+1` shift avoids ambiguous zero)
- Granular permissions: values `> UINT16_MAX`, enumerated in `permissions.macro`

`DelegateUtils.cpp` provides:
- `checkTxPermission()` — linear scan for `TxType + 1` match
- `loadGranularPermission()` — populates per-tx-type granular set via `Permission::getInstance().getGranularTxType()` reverse-map

Examples of granular permissions:
- `Payment` direct only: `PaymentMint` (issuer source), `PaymentBurn` (issuer destination) — blocked if `sfPaths` or asset conversion
- `AccountSet`: field-level grants per metadata field (`AccountDomainSet`, `AccountTransferRateSet`, etc.); flag changes blocked entirely
- `TrustSet`: `TrustlineAuthorize`, `TrustlineFreeze`, `TrustlineUnfreeze`; cannot create new lines, cannot change limit
- `MPTokenIssuanceSet`: `MPTokenIssuanceLock`, `MPTokenIssuanceUnlock`

## Permission Model & Cross-Transactor Static Interfaces

Several transactors expose `static deleteSLE`/`removeFromLedger`/equivalent methods on `ApplyView` so other transactors (especially `AccountDelete`) can clean up owned objects without constructing a fake transaction:

- `DepositPreauth::removeFromLedger(ApplyView&, uint256, Journal)`
- `DIDDelete::deleteSLE(ApplyView&, SLE, AccountID, Journal)`
- `OracleDelete::deleteOracle(ApplyView&, SLE, AccountID, Journal)`
- `DelegateSet::deleteDelegate(ApplyView&, SLE, AccountID, Journal)`
- `SignerListSet::removeFromLedger(ApplyView&, ServiceRegistry&, AccountID, Journal)`
- `MPTokenIssuanceCreate::create(ApplyView&, Journal, MPTCreateArgs)` — used by `VaultCreate` to mint share token
- `AMMWithdraw::withdraw`/`equalWithdrawTokens` — used by `AMMClawback`, `AMMDelete`
- `LoanManage::unimpairLoan/impairLoan/defaultLoan` — used by `LoanPay`

`AccountDelete` uses a `nonObligationDeleter()` switch over `LedgerEntryType` returning a `DeleterFuncPtr`. `nullptr` means "obligation, cannot delete". Recognized as non-obligations: offers, signer lists, tickets, deposit preauth, NFT offers, DIDs, oracles, credentials, delegates.

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

### `failInvariantCheck` Escalation

- First failure → `tecINVARIANT_FAILED` (committed to ledger, fee charged)
- Repeated failure during retry → `tefINVARIANT_FAILED` (not included in ledger)

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

`Privilege` bitmask enum + `hasPrivilege(STTx, Privilege)` (implemented via `transactions.macro` X-macro). Used by checkers to know what each transaction type may legitimately do:

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
| `XRPNotCreated` | Net XRP delta across accounts/paychans/escrows = -fee |
| `XRPBalanceChecks` | Every account balance is native XRP in [0, INITIAL_XRP] |
| `NoBadOffers` | No negative-amount, no XRP-for-XRP offers |
| `NoZeroEscrow` | Escrow/MPT amounts within bounds; MPT locked ≤ outstanding |
| `AccountRootsNotDeleted` | Account deletion cardinality matches `must`/`may` privilege |
| `AccountRootsDeletedClean` | Deleted account had zero balance + zero owner count + no orphaned objects (trust lines, escrows, offers, NFT pages, paychans, pseudo-account linked objects) |
| `ValidNewAccountRoot` | New accounts only from `createAcct`/`createPseudoAcct`; correct initial seq + flags |
| `ValidPseudoAccounts` | Exactly one discriminator, sequence unchanged, required flags, no regular key |
| `ValidClawback` | At most one trust line/MPT modified, holder balance non-negative |
| `NoModifiedUnmodifiableFields` | `sfLedgerEntryType`/`sfLedgerIndex` immutable; loan/broker origination fields immutable |
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
| `ValidVault` | Per-tx-type rules: deposit/withdraw asset/share conservation, immutable fields unchanged, loss only via loan ops |
| `ValidLoan` | Payment completion bidirectional (paymentRemaining=0 ↔ all outstanding=0), `lsfLoanOverpayment` immutable, non-negative fees, positive `sfPeriodicPayment` |
| `ValidLoanBroker` | Sequence monotonic, non-negative cover/debt, vault exists, cover ≤ pseudo-account balance (== under `fixSecurity3_1_3` except at delete) |

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
- `tecINTERNAL` / `tefBAD_LEDGER`: ledger corruption sentinels. Often marked `LCOV_EXCL` because preclaim should have prevented them
- `terNO_AMM`, `terNO_DELEGATE_PERMISSION`, `terNO_ACCOUNT`, `terNO_LINE`: retryable failures

## Hash Router Caching

Some expensive operations cache results in the `HashRouter` using private flag bits to avoid recomputation across multiple validation passes:

- **Signature verification** (`apply.cpp` `checkValidity`): `SF_SIGBAD`, `SF_SIGGOOD`, `SF_LOCALBAD`, `SF_LOCALGOOD` (PRIVATE1–PRIVATE4)
- **Crypto-condition validation** (`EscrowFinish::preflightSigValidated`): `SF_CF_VALID`, `SF_CF_INVALID` (PRIVATE5–PRIVATE6)

The `forceValidity()` API can promote cached state but cannot downgrade (never sets `SF_SIGBAD`) — used to mark locally-submitted transactions as pre-verified.

## Batch Transactions

`Batch` (in `system/Batch.cpp`) bundles 2-8 inner transactions with one of four execution policies (mutually exclusive, enforced via `std::popcount`):
- `tfAllOrNothing`: any failure aborts, full rollback
- `tfUntilFailure`: stop at first failure, keep prior successes
- `tfOnlyOne`: stop at first success
- `tfIndependent`: run all, commit successes

**Critical for new transactors:** Update `disabledTxTypes` in `Batch.cpp` if your type cannot run inside a batch. Currently disabled: all `ttVAULT_*` and `ttLOAN_*` types (multi-step state machines whose invariants are difficult to reason about under batch atomicity).

Inner transaction rules (enforced in `Batch::preflight`):
- `tfInnerBatchTxn` flag must be set
- Empty `sfSigningPubKey`, no `sfTxnSignature`, no `sfSigners`
- Fee = 0 XRP
- Exactly one of `sfSequence` (nonzero) or `sfTicketSequence`
- For `tfAllOrNothing`/`tfUntilFailure`: no duplicate sequence/ticket values across same-account inner txs

`Batch::preflightSigValidated` reconciles `sfBatchSigners` against the set of inner-tx accounts that differ from outer account (plus any `sfCounterparty` accounts). The outer account is explicitly excluded from `sfBatchSigners`.

`Batch::doApply()` returns `tesSUCCESS` and does nothing — `applyBatchTransactions()` in `apply.cpp` is called separately by `applyTransaction()` after the outer apply succeeds, executing inner txs in a nested `wholeBatchView`/`perTxBatchView` sandbox structure.

`Batch::calculateBaseFee` = `baseFee + Σ(inner tx fees) + numSigners × baseFee`. Overflow guards everywhere (marked `LCOV_EXCL`).

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
- Two-pass execution: reverse pass (compute required input for desired output) then forward pass (compute output for actual input)
- Limiting step detection: if reverse pass cannot satisfy desired output, that step is identified as the bottleneck and used as the anchor for forward pass
- Multi-strand flow uses `ActiveStrands` priority queue sorted by `qualityUpperBound`; one strand consumed per outer iteration (probe-and-push)
- Safety limits: `MaxOffersToConsume` = 1000 per book step, `maxTries` = 1000 outer iterations, `maxOffersToConsider` = 1500 cumulative, `AMMContext::MaxIterations` = 30
- `PaymentSandbox` (not regular `Sandbox`) is required because `flow()` uses deferred-credit accounting
- AMM offers are synthesized by `AMMLiquidity` to look like CLOB offers to `BookStep`; single-path uses `changeSpotPriceQuality`, multi-path uses Fibonacci-scaled offer sizes

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
