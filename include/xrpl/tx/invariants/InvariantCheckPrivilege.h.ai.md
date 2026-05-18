# `InvariantCheckPrivilege.h`

## Role in the System

This header exists to answer a focused question that every invariant checker must ask: *what is this transaction type actually allowed to do?* The XRPL invariant checking system runs after every transaction is applied to detect impossible or forbidden ledger mutations — but not every mutation is forbidden to every transaction type. An `AccountDelete` must delete an account root; a `Payment` may create one; an ordinary `EscrowCreate` must never touch one. Without a way to query these per-type permissions, invariant checks would need hardcoded `switch` statements scattered across every checker that cares about transaction type, producing duplicated logic that drifts as new transaction types are added.

`InvariantCheckPrivilege.h` solves this by centralising all transaction-type permissions into a single bitmask `Privilege` enum and a single query function `hasPrivilege()`.

## The `Privilege` Enum

`Privilege` is a plain `enum` (not `enum class`) whose enumerators are power-of-two bitmask values:

```cpp
createAcct        = 0x0001   // may create a new ACCOUNT_ROOT
createPseudoAcct  = 0x0002   // may create a pseudo-account (implies createAcct)
mustDeleteAcct    = 0x0004   // must delete an ACCOUNT_ROOT on success
mayDeleteAcct     = 0x0008   // may optionally delete an ACCOUNT_ROOT
overrideFreeze    = 0x0010   // may bypass certain freeze rules
changeNFTCounts   = 0x0020   // may mint or burn an NFT
createMPTIssuance = 0x0040   // may create a new MPT issuance object
destroyMPTIssuance= 0x0080   // may destroy an MPT issuance
mustAuthorizeMPT  = 0x0100   // must create or delete an MPT auth object
mayAuthorizeMPT   = 0x0200   // may create or delete an MPT auth object
mayDeleteMPT      = 0x0400   // may delete an MPT object (not create)
mustModifyVault   = 0x0800   // must modify/create/delete a vault
mayModifyVault    = 0x1000   // may modify/create/delete a vault
mayCreateMPT      = 0x2000   // may create an MPT object (non-issuer)
```

The distinction between `must*` and `may*` variants is architecturally significant. An invariant checker examining account-root deletions (`AccountRootsNotDeleted`) must distinguish between a transaction that is *required* to delete exactly one account (e.g., `ttACCOUNT_DELETE` carries `mustDeleteAcct`) versus one that is *permitted* to delete one incidentally (e.g., `ttAMM_WITHDRAW` carries `mayDeleteAcct`). Conflating the two would produce either false failures or missed violations.

## Composing Privileges with `operator|`

Because `Privilege` is a plain `enum`, C++ does not automatically expose bitwise operators on it. The header provides a `constexpr operator|` that delegates through `safe_cast` to the underlying integer type and back:

```cpp
constexpr Privilege operator|(Privilege lhs, Privilege rhs)
{
    return safe_cast<Privilege>(
        safe_cast<std::underlying_type_t<Privilege>>(lhs) |
        safe_cast<std::underlying_type_t<Privilege>>(rhs));
}
```

`safe_cast` is used rather than a raw C-style cast to guard against accidental out-of-range integer conversions. The result is that privilege sets can be expressed naturally in `transactions.macro`:

```
ttPAYMENT  →  createAcct | mayCreateMPT
ttAMM_WITHDRAW  →  mayDeleteAcct | overrideFreeze | mayAuthorizeMPT
ttVAULT_CREATE  →  createPseudoAcct | createMPTIssuance | mustModifyVault
```

## How `hasPrivilege()` Is Implemented

The function declared here is defined in `InvariantCheck.cpp` using a macro-expansion technique against `transactions.macro`. The macro file requires the caller to `#define TRANSACTION(tag, value, name, delegable, amendment, privileges, ...)` before including it. In `hasPrivilege`, the macro is redefined to emit a `switch` case that returns `(privileges) & priv` for each transaction type:

```cpp
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, ...) \
    case tag: { return (privileges) & priv; }

bool hasPrivilege(STTx const& tx, Privilege priv) {
    switch (tx.getTxnType()) {
#include <xrpl/protocol/detail/transactions.macro>
        default: return false;
    }
}
```

This design ensures that adding a new transaction type to `transactions.macro` with its privilege bitmask automatically makes `hasPrivilege()` correct for the new type without requiring any changes to the function body or any of the individual invariant checkers. The `default: return false` means unknown or deprecated transaction types carry no privileges, which is the safe conservative choice.

## The `assert(enforce)` Pattern

The file header documents a subtle convention used throughout the invariant implementation files. When an invariant fires, some checkers write `XRPL_ASSERT(enforce, ...)` where `enforce` is a boolean that is `true` only when the relevant amendment is active. This looks suspicious at first glance — why assert a variable rather than a condition?

The rationale is a deliberate two-layer defence strategy. Invariant failures should *never* occur in production except in tests that deliberately corrupt the ledger to exercise the invariant machinery. The `assert(enforce)` fires only in debug builds and only when a developer has introduced code that violates an invariant *without* the protecting amendment being enabled. It is designed to be painful for developers while being invisible to validators — catching bugs early, in unit tests and debug builds, before they can reach consensus-critical paths.

## Usage in Invariant Checkers

`hasPrivilege()` is called in the `finalize()` methods of several invariant checkers defined in `InvariantCheck.h`. For example, `AccountRootsNotDeleted::finalize()` accepts the deletion of exactly one account root only when `hasPrivilege(tx, mustDeleteAcct)` returns true and the transaction succeeded; it accepts the optional deletion of one account root when `hasPrivilege(tx, mayDeleteAcct)` is set. Similarly, `ValidNewAccountRoot::finalize()` requires `hasPrivilege(tx, createAcct | createPseudoAcct)` before allowing a newly created account root to pass inspection.

The privilege system thus acts as a declarative contract: each transaction type publishes what it is permitted to do, and the invariant checkers enforce that nothing else happens.