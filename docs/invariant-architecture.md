# Proposal: Separating Domain and Transaction Invariants

## Problem

XRPL on-chain protocols are implemented as semantically cohesive groups of transactions (see
`include/xrpl/tx/transactors/` — vault, dex, lending, etc.). Each protocol also has invariant
checks that run after every transaction to catch bugs before they reach the ledger.

The current invariant system conflates two distinct concerns in a single class:

1. **Domain invariants** — properties that must hold regardless of which transaction ran.
   Example: "a vault with zero shares must have zero assets."

2. **Transaction invariants** — properties specific to a particular transaction type.
   Example: "VaultDeposit must increase vault balance by the deposit amount."

This leads to:

- **Monolithic switch statements**: `ValidVault::finalize` is 800+ lines with a switch
  dispatching per transaction type. `ValidAMM::finalize` has a similar pattern.
- **Scattered rules**: the invariant rules for `VaultDeposit` are split between
  `VaultDeposit.cpp` (business logic) and `VaultInvariant.cpp` (validation), making it hard
  to reason about a transaction holistically.
- **No isolation**: transaction-specific invariant logic cannot be unit-tested without the
  full invariant infrastructure.
- **Unbounded growth**: every new transaction type adds another case to the switch.

---

## Current Architecture

### Two-Phase Invariant Checking

After a transaction is applied, `ApplyContext::checkInvariantsHelper` runs all invariant
checkers in two phases:

```
Phase 1 — State Collection
  For each modified ledger entry:
    call visitEntry() on every checker in the InvariantChecks tuple

Phase 2 — Validation
  For each checker:
    call finalize() → returns true (pass) or false (fail)
```

This is implemented via a `std::tuple` of checker classes, iterated at compile time with
`std::index_sequence`. Each checker implements `visitEntry` and `finalize` as duck-typed
methods — no base class, no virtual dispatch.

### Existing Invariant Categories

The 24 current checkers fall into three informal categories:

| Category            | Description                                                | Examples                                                            |
| ------------------- | ---------------------------------------------------------- | ------------------------------------------------------------------- |
| **Universal**       | Always run, no tx-type awareness                           | `XRPNotCreated`, `TransactionFeeCheck`, `LedgerEntryTypesMatch`     |
| **Privilege-gated** | Use `hasPrivilege()` to differentiate by tx-type           | `AccountRootsNotDeleted`, `ValidNewAccountRoot`, `ValidMPTIssuance` |
| **Domain**          | Large switch on tx-type mixing domain + tx-specific checks | `ValidVault`, `ValidAMM`, `ValidLoan`                               |

The first two categories work well. The third is where the problem lies.

---

## Proposed Architecture

### Goal

Separate domain invariants from transaction invariants with minimal machinery:

- **Domain invariants** stay in domain invariant classes (e.g., `ValidVault`). They always
  run when the domain is touched, regardless of transaction type.
- **Transaction invariants** move to static methods on the transactors themselves. They run
  only for their specific transaction type.
- **Opt-in with compile-time enforcement**: transactors that declare they have invariants
  must implement them, or compilation fails.
- **Two-phase process preserved**: state collection in Phase 1, validation in Phase 2.
- **ApplyContext unchanged**: the tuple, `visitEntry`, and `finalize` interface stay as-is.

### Overview

```
┌─────────────────────────────────────────────────────────┐
│                    InvariantChecks tuple                 │
│                                                         │
│  XRPNotCreated │ ... │ ValidVault │ ValidAMM │ ...      │
└──────────────────────────┬──────────┬──────────────────-┘
                           │          │
              ┌────────────┘          └─────────────┐
              ▼                                      ▼
  ┌─────────────────────┐             ┌─────────────────────┐
  │     ValidVault      │             │      ValidAMM       │
  │                     │             │                     │
  │  visitEntry()       │             │  visitEntry()       │
  │    → collect state  │             │    → collect state  │
  │                     │             │                     │
  │  finalize()         │             │  finalize()         │
  │    1. domain checks │             │    1. domain checks │
  │    2. dispatch tx   │             │    2. dispatch tx   │
  │       invariant     │             │       invariant     │
  └─────────┬───────────┘             └─────────┬───────────┘
            │                                    │
            │ if ttVAULT_DEPOSIT                 │ if ttAMM_DEPOSIT
            ▼                                    ▼
  VaultDeposit::checkInvariants()    AMMDeposit::checkInvariants()
```

### Design

There are four components:

1. **Domain state** — extracted from the current invariant class into its own type
2. **Domain invariant class** — owns the state, runs domain-wide checks, delegates tx dispatch
3. **Transaction invariants** — static methods on transactors
4. **Opt-in trait + dispatch** — connects transactors to domain state with compile-time safety

Each is described below.

---

### 1. Domain State

Each domain extracts its accumulated state into a standalone class. This class is populated
during Phase 1 (`visitEntry`) and read during Phase 2 by both domain checks and transaction
checks.

```cpp
// include/xrpl/tx/invariants/VaultInvariantState.h

class VaultInvariantState
{
public:
    struct Vault
    {
        uint256   key;
        Asset     asset;
        AccountID pseudoId;
        AccountID owner;
        uint192   shareMPTID;
        Number    assetsTotal;
        Number    assetsAvailable;
        Number    assetsMaximum;
        Number    lossUnrealized;

        static Vault make(SLE const&);
    };

    struct Shares
    {
        MPTIssue      share;
        std::uint64_t sharesTotal;
        std::uint64_t sharesMaximum;

        static Shares make(SLE const&);
    };

    void visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    // Read-only accessors
    std::vector<Vault>  const& beforeVault() const { return beforeVault_; }
    std::vector<Vault>  const& afterVault()  const { return afterVault_;  }
    std::vector<Shares> const& beforeMPTs()  const { return beforeMPTs_;  }
    std::vector<Shares> const& afterMPTs()   const { return afterMPTs_;   }

    std::optional<Number> deltaAssets(Asset const&, AccountID const&) const;

private:
    std::vector<Vault>                  beforeVault_;
    std::vector<Vault>                  afterVault_;
    std::vector<Shares>                 beforeMPTs_;
    std::vector<Shares>                 afterMPTs_;
    std::unordered_map<uint256, Number> deltas_;
};
```

The `visitEntry` implementation is extracted directly from the current `ValidVault::visitEntry`
— no logic changes, just moved to a new class.

### 2. Domain Invariant Class

Each domain invariant class owns its state, delegates `visitEntry`, and splits `finalize` into
two parts: domain checks (always run) and transaction dispatch.

```cpp
// include/xrpl/tx/invariants/VaultInvariant.h

class ValidVault
{
    VaultInvariantState state_;

public:
    void visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after)
    {
        state_.visitEntry(isDelete, before, after);
    }

    bool finalize(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j);
};
```

```cpp
// src/libxrpl/tx/invariants/VaultInvariant.cpp

bool ValidVault::finalize(
    STTx const& tx, TER result, XRPAmount fee,
    ReadView const& view, beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    // --- Domain invariants (always run) ---
    // These are the checks currently before/outside the switch statement:
    //   - vault operation must modify exactly one vault
    //   - vault immutable fields (asset, pseudoId, shareMPTID) unchanged
    //   - vault with zero shares has zero assets
    //   - assets available <= assets total
    //   - MPT issuance consistent with vault shareMPTID
    if (!checkDomainInvariants(state_, tx, result, view, j))
        return false;

    // --- Transaction invariants (per-transaction) ---
    return dispatchTransactionInvariant<VaultInvariantState>(
        state_, tx, result, view, j);
}
```

`checkDomainInvariants` is a private method (or free function in the .cpp) containing the
checks that currently live outside the switch statement in `ValidVault::finalize`. These checks
run for every transaction that touches a vault.

### 3. Transaction Invariants

Each transactor that has transaction-specific invariant logic declares and implements a static
`checkInvariants` method:

```cpp
// include/xrpl/tx/transactors/vault/VaultDeposit.h

class VaultInvariantState;  // forward declaration suffices

class VaultDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultDeposit(ApplyContext& ctx) : Transactor(ctx) {}

    static NotTEC preflight(PreflightContext const& ctx);
    static TER    preclaim(PreclaimContext const& ctx);
    TER           doApply() override;

    // Transaction invariant: validates post-conditions specific to VaultDeposit
    static bool checkInvariants(
        VaultInvariantState const& state,
        STTx const& tx,
        TER result,
        ReadView const& view,
        beast::Journal const& j);
};
```

```cpp
// src/libxrpl/tx/transactors/vault/VaultDeposit.cpp

#include <xrpl/tx/invariants/VaultInvariantState.h>  // full definition needed here

bool VaultDeposit::checkInvariants(
    VaultInvariantState const& state,
    STTx const& tx,
    TER result,
    ReadView const& view,
    beast::Journal const& j)
{
    // Logic currently in the ttVAULT_DEPOSIT case of ValidVault::finalize:
    //   - vault balance increased by deposit amount
    //   - depositor balance decreased by same amount
    //   - vault shares outstanding increased
    //   - assets maximum not exceeded
}
```

Transactors that have no transaction-specific invariants (e.g., `Payment` for the vault
domain) define nothing — the dispatch skips them.

### 4. Opt-In Trait and Dispatch

#### The Trait

A transactor declares participation in a domain by specializing `InvariantDomains<T>`:

```cpp
// include/xrpl/tx/invariants/InvariantDomains.h

#pragma once
#include <tuple>
#include <type_traits>

namespace xrpl {

// Primary template: no domain invariants by default.
template <typename Transactor>
struct InvariantDomains
{
    using types = std::tuple<>;
};

// Compile-time tuple membership check.
template <typename State, typename Tuple>
struct tuple_contains : std::false_type {};

template <typename State, typename... Ts>
struct tuple_contains<State, std::tuple<State, Ts...>> : std::true_type {};

template <typename State, typename T, typename... Ts>
struct tuple_contains<State, std::tuple<T, Ts...>>
    : tuple_contains<State, std::tuple<Ts...>> {};

template <typename State, typename Tuple>
inline constexpr bool tuple_contains_v = tuple_contains<State, Tuple>::value;

}  // namespace xrpl
```

Transactors opt in by specializing the trait in their header:

```cpp
// At the bottom of VaultDeposit.h, after the class definition

template <>
struct InvariantDomains<VaultDeposit>
{
    using types = std::tuple<VaultInvariantState>;
};
```

#### Why Traits Instead of SFINAE

An alternative is to use `if constexpr (requires { T::checkInvariants(...) })` to detect
whether a transactor provides invariant checks. This is dangerous because:

- If the `checkInvariants` signature changes (e.g., parameter reordering), the `requires`
  expression quietly evaluates to `false` and the check is **silently skipped**.
- If someone deletes `checkInvariants` during refactoring, the same silent skip occurs.
- A method name typo (`checkInvariant` vs `checkInvariants`) disables the check silently.

The traits approach separates **declaration of intent** from **implementation**:

| Scenario                    | SFINAE          | Traits                    |
| --------------------------- | --------------- | ------------------------- |
| Opted in, method correct    | Runs            | Runs                      |
| Opted in, method missing    | **Silent skip** | **Compile error**         |
| Opted in, wrong signature   | **Silent skip** | **Compile error**         |
| Not opted in, no method     | Skips (correct) | Skips (correct)           |
| Method deleted in refactor  | **Silent skip** | **Compile error**         |
| Opt-in removed deliberately | N/A             | Skips (visible in review) |

The only way to disable a check is to remove the trait specialization — a deliberate,
reviewable change.

#### The Dispatch Function

The dispatch function is **declared** in a lightweight header and **defined** in a single
`.cpp` file. This keeps all 45+ transactor `#include`s out of invariant headers.

```cpp
// include/xrpl/tx/invariants/InvariantDispatch.h — declaration only

#pragma once
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

namespace xrpl {

class ReadView;

template <typename State>
bool dispatchTransactionInvariant(
    State const& state,
    STTx const& tx,
    TER result,
    ReadView const& view,
    beast::Journal const& j);

}  // namespace xrpl
```

The implementation uses `transactions.macro` to generate a switch over all transaction types.
For each type, `tuple_contains_v` checks whether that transactor opted in for the given
domain state. If it did, the transactor's `checkInvariants` is called — and if the method
is missing or has the wrong signature, **compilation fails**.

```cpp
// src/libxrpl/tx/invariants/InvariantDispatch.cpp

#define TRANSACTION_INCLUDE 1
#include <xrpl/protocol/detail/transactions.macro>

#include <xrpl/tx/invariants/InvariantDomains.h>
#include <xrpl/tx/invariants/VaultInvariantState.h>
#include <xrpl/tx/invariants/AMMInvariantState.h>

namespace xrpl {

template <typename State>
bool dispatchTransactionInvariant(
    State const& state,
    STTx const& tx,
    TER result,
    ReadView const& view,
    beast::Journal const& j)
{
    switch (tx.getTxnType())
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, ...)                                \
    case tag: {                                                           \
        if constexpr (tuple_contains_v<State,                             \
                          typename InvariantDomains<name>::types>) {       \
            return name::checkInvariants(state, tx, result, view, j);     \
        }                                                                 \
        return true;                                                      \
    }
#include <xrpl/protocol/detail/transactions.macro>
#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            return true;
    }
}

// Explicit instantiation — one line per domain
template bool dispatchTransactionInvariant<VaultInvariantState>(
    VaultInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);
template bool dispatchTransactionInvariant<AMMInvariantState>(
    AMMInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);

}  // namespace xrpl
```

Each new domain adds one explicit instantiation line. Forgetting it produces a linker error.

**Note on RAII number guards:** `Transactor::operator()` establishes `NumberSO` and
`CurrentTransactionRulesGuard` before calling `checkInvariants`, so the dispatch function
does not need to duplicate them.

---

## Execution Flow

### Phase 1 — State Collection

```
ApplyContext::checkInvariantsHelper()
  │
  │  for each modified ledger entry:
  │
  ├──▶ TransactionFeeCheck::visitEntry()    (no-op)
  ├──▶ XRPNotCreated::visitEntry()          → accumulates drops_
  ├──▶ ...
  └──▶ ValidVault::visitEntry()
            │
            └──▶ VaultInvariantState::visitEntry()
                      │
                      ├─ ltVAULT             → push to beforeVault_ / afterVault_
                      ├─ ltMPTOKEN_ISSUANCE  → push to beforeMPTs_ / afterMPTs_
                      ├─ ltACCOUNT_ROOT      → record in deltas_
                      └─ (other types ignored)
```

### Phase 2 — Validation

```
ApplyContext::checkInvariantsHelper()
  │
  ├──▶ TransactionFeeCheck::finalize()       → checks fee
  ├──▶ XRPNotCreated::finalize()             → checks drops_
  ├──▶ ...
  └──▶ ValidVault::finalize()
            │
            ├─ 1. checkDomainInvariants(state_)
            │        "exactly one vault modified"
            │        "immutable fields unchanged"
            │        "vault with zero shares has zero assets"
            │
            └─ 2. dispatchTransactionInvariant<VaultInvariantState>(state_, ...)
                       │
                       │  switch(tx.getTxnType())
                       │
                       │  case ttVAULT_DEPOSIT:
                       │    InvariantDomains<VaultDeposit>::types
                       │      contains VaultInvariantState? YES
                       │    → VaultDeposit::checkInvariants(state_, ...)
                       │        "vault balance increased by deposit amount"
                       │        "depositor balance decreased by same amount"
                       │        "shares outstanding increased"
                       │
                       │  case ttPAYMENT:
                       │    InvariantDomains<Payment>::types
                       │      contains VaultInvariantState? NO
                       │    → return true (skip)
```

---

## Include Dependencies

The design avoids circular dependencies and minimizes compile-time impact:

```
InvariantDomains.h          ← lightweight, only <tuple>
InvariantDispatch.h         ← lightweight, only forward declarations + STTx/TER
VaultInvariantState.h       ← protocol types only (Number, Asset, MPTIssue, etc.)

VaultInvariant.h            → VaultInvariantState.h, InvariantDispatch.h
VaultDeposit.h              → Transactor.h, InvariantDomains.h
                               (forward-declares VaultInvariantState)

InvariantDispatch.cpp       → transactions.macro (TRANSACTION_INCLUDE=1)
                               ALL transactor headers, ALL state headers
                               (heavy includes confined to one .cpp)

VaultDeposit.cpp            → VaultInvariantState.h (full definition)
```

No invariant header includes any transactor header. No transactor header includes any
invariant state header (forward declaration suffices). The heavy fan-out from
`transactions.macro` is confined to `InvariantDispatch.cpp`.

---

## What Changes and What Doesn't

| Component                  | Changes? | Details                                                                                        |
| -------------------------- | -------- | ---------------------------------------------------------------------------------------------- |
| `ApplyContext`             | **No**   | Tuple iteration, `visitEntry`/`finalize` interface unchanged                                   |
| `InvariantChecks` tuple    | **No**   | `ValidVault`, `ValidAMM` stay in the tuple                                                     |
| Universal invariants       | **No**   | `XRPNotCreated`, `TransactionFeeCheck`, etc. untouched                                         |
| Privilege-gated invariants | **No**   | `AccountRootsNotDeleted`, `ValidMPTIssuance`, etc. untouched                                   |
| `ValidVault` interface     | **No**   | Still has `visitEntry` + `finalize` with same signatures                                       |
| `ValidVault` internals     | **Yes**  | State extracted; finalize split into domain + dispatch                                         |
| `ValidAMM` internals       | **Yes**  | Same treatment as `ValidVault`                                                                 |
| Transactor headers         | **Yes**  | Add `checkInvariants` declaration + trait specialization                                       |
| Transactor `.cpp` files    | **Yes**  | Add `checkInvariants` implementation                                                           |
| New files                  | **Yes**  | `InvariantDomains.h`, `InvariantDispatch.h`, `InvariantDispatch.cpp`, per-domain state headers |

---

## Migration Path

| Phase | Change                                                                                               | Files                                                      |
| ----- | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| 1     | Add `InvariantDomains.h` trait and `InvariantDispatch.h` declaration                                 | 2 new headers                                              |
| 2     | Add `InvariantDispatch.cpp` with dispatch template (initially no instantiations)                     | 1 new `.cpp`                                               |
| 3     | Extract `VaultInvariantState` from `ValidVault`                                                      | `VaultInvariantState.h`, `VaultInvariantState.cpp`         |
| 4     | Refactor `ValidVault`: own state, split finalize into domain checks + dispatch                       | `VaultInvariant.h`, `VaultInvariant.cpp`                   |
| 5     | Migrate one transactor (e.g., `VaultDeposit`): add `checkInvariants` + trait, remove its switch case | `VaultDeposit.h`, `VaultDeposit.cpp`, `VaultInvariant.cpp` |
| 6     | Repeat for remaining vault transactors (`VaultCreate`, `VaultSet`, `VaultWithdraw`, `VaultClawback`) | Vault transactor files, `VaultInvariant.cpp`               |
| 7     | Delete empty switch from `ValidVault::finalize`                                                      | `VaultInvariant.cpp`                                       |
| 8     | Repeat 3–7 for `ValidAMM` and lending invariants                                                     | AMM/loan files, `InvariantDispatch.cpp`                    |

Each phase is independently deployable. The switch shrinks one case at a time.

---

## Alternatives Considered

### A. CRTP Base Class (`DomainInvariantBase<Derived, State>`)

A CRTP base could provide `visitEntry` delegation and the two-phase `finalize` pattern
generically, eliminating ~5 lines of boilerplate per domain.

**Rejected because:**

- The codebase has zero CRTP in the invariant system today. Adding it raises the template
  literacy bar for all contributors.
- `static_cast<Derived*>(this)` is a known source of subtle bugs if the CRTP contract is
  violated.
- Each domain's `finalize` has unique structure (feature gates, result filtering, helper
  methods). A generic base cannot capture this without becoming complex itself.

### B. Virtual Base Class

A virtual `DomainInvariant` base class could provide a common interface.

**Rejected because:**

- The existing invariant system is entirely duck-typed via `std::tuple`. Introducing virtual
  dispatch changes the paradigm for only some checkers, creating inconsistency.
- Does not solve the dispatch-to-transactor problem — you still need a mechanism to call
  `VaultDeposit::checkInvariants` from the invariant system.

### C. SFINAE Detection Instead of Traits

Use `if constexpr (requires { T::checkInvariants(...) })` to detect transactor methods.

**Rejected because:**

- Silent failure on signature mismatch or method removal (see "Why Traits Instead of SFINAE"
  above). This is the single most dangerous failure mode for a safety-critical system like
  invariant checking.

### D. Runtime Registry

Transactors register `std::function` callbacks keyed by `TxType` at startup.

**Rejected because:**

- Static initialization order fiasco across translation units.
- Type safety loss (`void*` casts or type-erased state).
- A transactor that forgets to register silently passes — same problem as SFINAE.
- Runtime overhead on every transaction for no benefit.

### E. Extend `transactions.macro` with Invariant Domain Field

See **Proposal B** below — this is presented as a full alternative design rather than a
rejected option. It uses a domain bitfield in the macro (analogous to the existing `privileges`
field) instead of per-transactor trait specializations.

---

## Proposal B: Macro-Based Domain Opt-In

This is an alternative to the traits-based opt-in described above. Everything else in the
architecture (state extraction, domain invariant classes, `checkInvariants` on transactors,
the dispatch function, the two-phase process) stays the same. Only **how a transactor declares
its domain membership** changes.

### Motivation

The `transactions.macro` already serves as the single source of truth for transaction
metadata: tag, name, delegation, amendments, privileges, fields. The `privileges` bitfield
is already queried by invariant code via `hasPrivilege()`. A domain bitfield follows the same
established pattern — it belongs in the macro alongside everything else, rather than scattered
across transactor headers as trait specializations.

### Domain Enum

```cpp
// include/xrpl/tx/invariants/InvariantCheckDomain.h

#pragma once
#include <cstdint>

namespace xrpl {

enum Domain : std::uint16_t {
    noDomain    = 0x0000,
    vaultDomain = 0x0001,
    ammDomain   = 0x0002,
    loanDomain  = 0x0004,
};

constexpr Domain operator|(Domain a, Domain b)
{
    return static_cast<Domain>(
        static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

constexpr Domain operator&(Domain a, Domain b)
{
    return static_cast<Domain>(
        static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

}  // namespace xrpl
```

### Macro Change

Add `domain` as the 7th parameter, pushing `fields` to 8th:

```
// Before (7 parameters):
TRANSACTION(tag, value, name, delegable, amendments, privileges, fields)

// After (8 parameters):
TRANSACTION(tag, value, name, delegable, amendments, privileges, domain, fields)
```

Example entries:

```cpp
TRANSACTION(ttVAULT_DEPOSIT, 68, VaultDeposit,
    Delegation::delegable,
    featureSingleAssetVault,
    mayAuthorizeMPT | mustModifyVault,
    vaultDomain,
    ({
    {sfVaultID, soeREQUIRED},
    {sfAmount, soeREQUIRED, soeMPTSupported},
}))

TRANSACTION(ttPAYMENT, 0, Payment,
    Delegation::notDelegable,
    uint256{},
    noPriv,
    noDomain,
    ({
    {sfDestination, soeREQUIRED},
    ...
}))

// A transaction participating in multiple domains:
TRANSACTION(ttLOAN_MANAGE, 81, LoanManage,
    Delegation::delegable,
    featureLendingProtocol,
    mustModifyVault | ...,
    loanDomain | vaultDomain,
    ({...}))
```

### Impact on Existing Macro Consumers

Most consumers use `TRANSACTION(tag, value, name, ...)` and are **unaffected**:

| Consumer              | Current definition                                                        | Impact          |
| --------------------- | ------------------------------------------------------------------------- | --------------- |
| `TxFormats.h`         | `TRANSACTION(tag, value, ...)`                                            | None            |
| `jss.h`               | `TRANSACTION(tag, value, name, ...)`                                      | None            |
| `applySteps.cpp`      | `TRANSACTION(tag, value, name, ...)`                                      | None            |
| `Permissions.cpp` (1) | `TRANSACTION(tag, value, name, delegable, amendment, ...)`                | None            |
| `Permissions.cpp` (2) | `TRANSACTION(tag, value, name, delegable, ...)`                           | None            |
| `InvariantCheck.cpp`  | `TRANSACTION(tag, value, name, delegable, amendment, privileges, ...)`    | None            |
| `TxFormats.cpp`       | `TRANSACTION(tag, value, name, delegable, amendment, privileges, fields)` | **Must update** |

Only `TxFormats.cpp` uses all 7 positional parameters and must be updated to accept 8:

```cpp
// TxFormats.cpp — only change needed
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, domain, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());
```

### Domain-to-State Mapping

A template variable maps each `State` type to its domain flag:

```cpp
// include/xrpl/tx/invariants/InvariantCheckDomain.h

template <typename State>
inline constexpr Domain domainFor = noDomain;
```

Each domain state header specializes it:

```cpp
// include/xrpl/tx/invariants/VaultInvariantState.h (at the bottom)

template <>
inline constexpr Domain domainFor<VaultInvariantState> = vaultDomain;
```

### Dispatch Function

The dispatch is the same structure as Proposal A, but uses the macro's domain field instead
of a traits lookup:

```cpp
// src/libxrpl/tx/invariants/InvariantDispatch.cpp

#define TRANSACTION_INCLUDE 1
#include <xrpl/protocol/detail/transactions.macro>

#include <xrpl/tx/invariants/InvariantCheckDomain.h>
#include <xrpl/tx/invariants/VaultInvariantState.h>
#include <xrpl/tx/invariants/AMMInvariantState.h>

namespace xrpl {

template <typename State>
bool dispatchTransactionInvariant(
    State const& state,
    STTx const& tx,
    TER result,
    ReadView const& view,
    beast::Journal const& j)
{
    constexpr auto target = domainFor<State>;

    switch (tx.getTxnType())
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, delegable, amend, priv, domain, fields) \
    case tag: {                                                            \
        if constexpr ((domain & target) != noDomain) {                     \
            return name::checkInvariants(state, tx, result, view, j);      \
        }                                                                  \
        return true;                                                       \
    }
#include <xrpl/protocol/detail/transactions.macro>
#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            return true;
    }
}

// Explicit instantiations — one per domain
template bool dispatchTransactionInvariant<VaultInvariantState>(
    VaultInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);
template bool dispatchTransactionInvariant<AMMInvariantState>(
    AMMInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);

}  // namespace xrpl
```

**Compile-time enforcement**: when the macro expands for `ttVAULT_DEPOSIT`, `domain` is
literally `vaultDomain`, so `(vaultDomain & vaultDomain) != noDomain` is a true `constexpr`
expression. The `if constexpr` branch is compiled, and `VaultDeposit::checkInvariants` **must
exist** with the correct signature — otherwise compilation fails.

For `ttPAYMENT`, `domain` is `noDomain`, so `(noDomain & vaultDomain) != noDomain` is false.
The branch is discarded. `Payment::checkInvariants` is never referenced.

### `hasDomain` Query Function

Analogous to `hasPrivilege()`, for use in domain invariant `finalize` methods:

```cpp
// include/xrpl/tx/invariants/InvariantCheckDomain.h

bool hasDomain(STTx const& tx, Domain domain);
```

```cpp
// src/libxrpl/tx/invariants/InvariantCheckDomain.cpp

bool hasDomain(STTx const& tx, Domain domain)
{
    switch (tx.getTxnType())
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, delegable, amend, priv, txDomain, fields) \
    case tag:                                                                \
        return (txDomain & domain) != noDomain;
#include <xrpl/protocol/detail/transactions.macro>
#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            return false;
    }
}
```

This allows domain invariant classes to use `hasDomain(tx, vaultDomain)` in their domain-wide
checks, analogous to how existing invariants use `hasPrivilege(tx, mustModifyVault)`.

### Proposal A vs Proposal B

| Aspect                       | A: Traits                           | B: Macro domain field                    |
| ---------------------------- | ----------------------------------- | ---------------------------------------- |
| Opt-in location              | Scattered across transactor headers | Centralized in `transactions.macro`      |
| Single source of truth       | No — trait + macro both describe tx | Yes — macro is the one place             |
| Follows existing patterns    | New pattern (traits)                | Extends existing pattern (`privileges`)  |
| New transactor               | Add trait in header                 | Add domain flag in macro                 |
| New domain                   | Add `domainFor<>` + tuple helper    | Add `domainFor<>` + enum value           |
| Macro migration              | None                                | One-time: update `TxFormats.cpp`         |
| Compile-time enforcement     | Same (`if constexpr` → hard error)  | Same (`if constexpr` → hard error)       |
| Auditability                 | Grep headers for specializations    | Read one macro file                      |
| Runtime query                | N/A                                 | `hasDomain()` parallels `hasPrivilege()` |
| No transactor header changes | Trait specialization needed         | Only `checkInvariants` declaration       |

Proposal B is **recommended** because it follows the established `privileges` pattern, keeps
all transaction metadata in one file, and requires fewer new concepts (no traits template, no
`tuple_contains` helper).
