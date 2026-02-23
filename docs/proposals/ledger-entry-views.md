# Proposal: Typed Ledger Entry Views

## Problem Statement

When transactors interact with ledger entries, they must understand internal implementation details that are error-prone and repetitive. The most prominent example is **RippleState (trustlines)**, where every piece of code must:

1. Compute which account is "high" vs "low": `bool const bHigh = account > issuer;`
2. Select the correct flag: `bHigh ? lsfHighFreeze : lsfLowFreeze`
3. Select the correct field: `bHigh ? sfHighLimit : sfLowLimit`
4. Handle balance sign based on position

This pattern appears **50+ times** across the codebase and is a common source of bugs.

Similar issues exist for other ledger entry types:

- **AccountRoot**: 15+ flags that must be checked with manual bitmask operations
- **Escrow**: Time-based state logic (`canFinish`, `canCancel`) repeated in multiple transactors
- **Delegate**: Permission array traversal duplicated wherever delegation is checked
- **MPTIssuance**: Capability flags checked inconsistently

## Proposed Solution

Introduce **typed view classes** that encapsulate ledger entry access patterns, exposing a clean domain-specific API.

### Example: RippleStateView

**Before (current code):**

```cpp
auto const sleRippleState = view.read(keylet::line(holder, issuer, currency));
if (!sleRippleState)
    return tecNO_LINE;

bool const bHigh = holder > issuer;
auto const uFlags = sleRippleState->getFieldU32(sfFlags);

// Check if frozen by issuer
if (uFlags & (bHigh ? lsfHighFreeze : lsfLowFreeze))
    return tecFROZEN;

// Get holder's limit
auto const limit = sleRippleState->getFieldAmount(
    bHigh ? sfHighLimit : sfLowLimit);
```

**After (with RippleStateView):**

```cpp
RippleStateView rippleState(view, holder, issuer, currency);
if (!rippleState)
    return tecNO_LINE;

if (rippleState.isFrozenByPeer())
    return tecFROZEN;

auto const limit = rippleState.myLimit();
```

### Example: AccountRootView

**Before:**

```cpp
auto const sle = view.read(keylet::account(account));
if (!sle)
    return terNO_ACCOUNT;

if (sle->isFlag(lsfDepositAuth) && src != dst)
{
    if (!view.exists(keylet::depositPreauth(dst, src)))
        return tecNO_PERMISSION;
}

if (sle->isFlag(lsfRequireAuth))
    // ...

if (sle->isFlag(lsfGlobalFreeze))
    // ...
```

**After:**

```cpp
AccountRootView account(view, accountId);
if (!account)
    return terNO_ACCOUNT;

if (account.hasDepositAuth() && src != dst)
{
    if (!view.exists(keylet::depositPreauth(dst, src)))
        return tecNO_PERMISSION;
}

if (account.requiresAuth())
    // ...

if (account.isGloballyFrozen())
    // ...
```

### Example: EscrowView

**Before:**

```cpp
auto const slep = view.peek(keylet::escrow(owner, seq));
if (!slep)
    return tecNO_TARGET;

auto const now = view.header().parentCloseTime;

if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
    return tecNO_PERMISSION;

if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
    return tecNO_PERMISSION;
```

**After:**

```cpp
EscrowView escrow(view, owner, seq);
if (!escrow)
    return tecNO_TARGET;

if (!escrow.canFinish(now))
    return tecNO_PERMISSION;

if (escrow.hasExpired(now))
    return tecNO_PERMISSION;
```

## Proposed View Classes

| View Class        | Ledger Entry | Key Benefit               |
| ----------------- | ------------ | ------------------------- |
| `RippleStateView` | RippleState  | Hides high/low complexity |
| `AccountRootView` | AccountRoot  | Named flag accessors      |
| `EscrowView`      | Escrow       | Time-based state methods  |
| `DelegateView`    | Delegate     | Permission checking API   |
| `CredentialView`  | Credential   | Expiry + acceptance state |
| `MPTIssuanceView` | MPTIssuance  | Capability flag accessors |
| `MPTokenView`     | MPToken      | Holder-perspective access |

Each view class would have a mutable variant (e.g., `MutableTrustlineView`) for use in `doApply()`.

## Benefits

| Aspect              | Current                   | With Views              |
| ------------------- | ------------------------- | ----------------------- |
| **Correctness**     | Easy to get `bHigh` wrong | Impossible to get wrong |
| **Readability**     | Must understand internals | Self-documenting API    |
| **Discoverability** | Read macro files          | IDE autocomplete        |
| **Consistency**     | Patterns vary by author   | Enforced API            |
| **Testing**         | Test each transactor      | Test view class once    |
| **Onboarding**      | Steep learning curve      | Intuitive interface     |

## Implementation Plan

### Phase 1: Core Views (High Impact)

1. `RippleStateView` - Eliminates 50+ instances of high/low logic
2. `AccountRootView` - Most frequently accessed ledger entry
3. `DelegateView` - Simplifies permission checking boilerplate

### Phase 2: Time-Sensitive Views

4. `EscrowView`
5. `CredentialView`
6. `CheckView`

### Phase 3: Asset Views

7. `MPTIssuanceView`
8. `MPTokenView`

### Migration Strategy

- Views are **additive** - no breaking changes to existing code
- New code should use views; existing code migrated incrementally
- Existing helper functions (`isFrozen`, `accountHolds`, etc.) can be refactored to use views internally

## Design Details

### Base Class

```cpp
class LedgerEntryViewBase
{
public:
    bool exists() const { return sle_ != nullptr; }
    explicit operator bool() const { return exists(); }
    std::shared_ptr<SLE const> const& sle() const { return sle_; }

protected:
    std::shared_ptr<SLE const> sle_;
};
```

### File Organization

```
include/xrpl/ledger/
├── View.h                    # Existing - keep as-is
├── views/
│   ├── LedgerEntryViewBase.h
│   ├── RippleStateView.h
│   ├── AccountRootView.h
│   ├── EscrowView.h
│   ├── DelegateView.h
│   └── ...
```

## FAQ

**Q: Doesn't this add overhead?**
A: Minimal. The view is a thin wrapper holding a `shared_ptr<SLE>`. All methods are inline-able. The high/low calculation happens once at construction.

**Q: What about existing helper functions like `isFrozen()`?**
A: They remain as the simple API for one-off checks. Views are for when you need multiple operations on the same entry.

**Q: Should views hold a reference to the ReadView?**
A: For read-only views, no - the SLE snapshot is sufficient. Mutable views need an `ApplyView&` for modifications.
