# Transactors

Transaction processing pipeline: preflight (static validation) -> preclaim (ledger state checks) -> doApply (state mutation). Base class `Transactor` in `src/libxrpl/tx/`.

## Key Invariants

- Pipeline is strict: preflight runs WITHOUT ledger state, preclaim runs WITH read-only view, doApply runs with mutable view
- `preflight` validates all fields exist and are well-formed; this is the ONLY place to reject malformed transactions cheaply
- Fee is always deducted even if the transaction fails (`tecCLAIM` pattern); `payFee` runs before `doApply`
- Sequence/ticket consumption happens in `consumeSeqProxy`; must succeed before any state changes
- Invariant checkers run after `doApply`; they can veto the transaction post-execution

## Common Bug Patterns

- New transaction type missing preflight validation for new fields = malformed transactions reach doApply and corrupt state
- Forgetting to handle `tecCLAIM` in doApply: fee is deducted but no other state changes should occur
- Batch transactions (`Batch` type) have their own signing path (`checkBatchSign`); changes to signing must cover both paths
- `calculateBaseFee` override without updating `minimumFee` causes fee calculation divergence between nodes
- Missing invariant checker update for new ledger entry types = silent constraint violations

## Transactor Template

### Header (`include/xrpl/tx/transactors/MyTx.h`)
```cpp
#pragma once
#include <xrpl/tx/Transactor.h>

namespace xrpl {
class MyTransaction : public Transactor {
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};
    explicit MyTransaction(ApplyContext& ctx) : Transactor(ctx) {}

    static bool checkExtraFeatures(PreflightContext const& ctx);
    static std::uint32_t getFlagsMask(PreflightContext const& ctx);
    static NotTEC preflight(PreflightContext const& ctx);  // NO ledger
    static TER preclaim(PreclaimContext const& ctx);        // read-only
    TER doApply() override;                                 // read-write
};
}
```

### Implementation (`src/libxrpl/tx/transactors/MyFeature/MyTx.cpp`)
```cpp
bool MyTransaction::checkExtraFeatures(PreflightContext const& ctx)
{   // REQUIRED: gate on amendment
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
// 1. transactions.macro: TRANSACTION(ttMY_TYPE, N, MyTx, delegation, fields)
// 2. applySteps.cpp:     case ttMY_TYPE: return invoke<MyTransaction>(...);
// 3. features.macro:     XRPL_FEATURE(MyFeature, Supported::yes, DefaultNo)
// 4. Feature.h:          increment numFeatures
// 5. InvariantCheck.cpp:  update if new ledger objects created
// 6. Batch.cpp:          add to disabledTxTypes if not batch-compatible
```

## Transaction Lifecycle

1. `preflight` (static checks, no ledger) -> `PreflightResult`
2. `preclaim` (ledger state, read-only) -> TER
3. `operator()` orchestrates: `checkSeqProxy` -> `checkPriorTxAndLastLedger` -> `checkFee` -> `checkSign` -> `apply`
4. `Transactor::apply()` runs `consumeSeqProxy` -> `payFee` -> `doApply` and returns a TER
5. `operator()` inspects the TER, decides whether to commit (`ctx_.apply`) or discard (`ctx_.discard`/`reset`)

## State Commitment & tec* Rollback (CRITICAL for review)

**`doApply` mutations are NOT committed until `ctx_.apply()` is called at the end of `operator()`.** All peek/insert/update/erase during `doApply` go into an `ApplyContext` view (`view_`) layered on top of `base_`. Whether that view gets flushed to `base_` depends entirely on the TER that `doApply` returns.

`ApplyContext::discard()` ([src/libxrpl/tx/ApplyContext.cpp](src/libxrpl/tx/ApplyContext.cpp)) replaces `view_` with a fresh view on `base_` — **every doApply mutation is thrown away**:
```cpp
void ApplyContext::discard() { view_.emplace(&base_, flags_); }
```

### Return-code decision table (in `Transactor::operator()`, [src/libxrpl/tx/Transactor.cpp](src/libxrpl/tx/Transactor.cpp))

| doApply returns | What commits to the ledger |
|---|---|
| `tesSUCCESS` | All doApply mutations + fee + seq (via `ctx_.apply`) |
| `tec*` (normal, `!tapRETRY`) | `reset(fee)` calls `discard()`, then re-applies fee + seq only. **All doApply mutations reverted.** |
| `tec*` with `tapFAIL_HARD` | `discard()` called directly, nothing committed (not even fee) |
| `tec*` with `tapRETRY` | `applied=false`, `ctx_.apply` never called, tx re-queued |
| `tef*` / `tem*` / `ter*` | `applied=false`, `ctx_.apply` never called |
| `tecINVARIANT_FAILED` after invariants | reset again, commit fee only |

`isTecClaimHardFail(ter, flags) = isTecClaim(ter) && !(flags & tapRETRY)` ([include/xrpl/tx/applySteps.h](include/xrpl/tx/applySteps.h)) — this predicate is what drives the reset path for normal consensus application.

### What this means for transactor authors and reviewers

- **A `tec*` return from doApply acts as a full-transaction rollback.** You do NOT need to order mutations defensively so that all checks come before any state changes. If a helper called late in doApply returns `tec*`, everything mutated earlier in the same doApply is discarded via `discard()`.
- **Orphan-state bugs of the form "we mutated X then returned tec* so X is now in an inconsistent state" are not possible at the transactor boundary.** The ApplyContext isolates the whole doApply as an atomic unit.
- **The real failure mode is within `doApply` itself**: if you call `view().update(sle)` on a stale SLE pointer, or mutate a variable you read by value instead of peek, those are real bugs — but they are in-memory bugs, not state-commit bugs.
- **Sandboxes inside `doApply` add nesting, not safety.** `PaymentSandbox` / nested `ApplyView` are useful when you need to conditionally commit a subset of changes *within* a single doApply (e.g., apply offers but revert if the net outcome fails). They are not needed to protect against doApply's own `tec*` return — that rollback is automatic.
- **Only `ctx_.apply(result)` publishes to `base_`**; a doApply that `return`s early, throws, or crashes never reaches that call, so base_ stays clean.

### Verifying a suspected orphan-state bug

Before claiming "directory removed but SLE not erased because tec\*":
1. Read the caller of `doApply` — confirm the TER path (`operator()` in Transactor.cpp).
2. Check whether `discard()` is reached via `reset()` or the `tapFAIL_HARD` branch.
3. If both paths call `discard()`, the mutations cannot persist on tec\*.
4. Look instead for: missing `view().update(sle)` after mutation, stale SLE pointers, or genuine non-atomic side effects (e.g., hash router flags, which are NOT in the ApplyContext view).

## Permission System

- `checkSign` dispatches to `checkSingleSign`, `checkMultiSign`, or `checkBatchSign`
- `checkPermission` validates delegated authority for delegatable transaction types
- Multi-sign requires M-of-N signers matching the signer list; weight threshold must be met

## Key Files

- `src/xrpld/app/tx/detail/Transactor.cpp` - base class and pipeline
- `include/xrpl/protocol/detail/transactions.macro` - type definitions
- `src/xrpld/app/tx/detail/` - per-type implementations (Payment.cpp, OfferCreate.cpp, etc.)
- `src/xrpld/app/tx/detail/InvariantCheck.cpp` - post-execution invariant checks
