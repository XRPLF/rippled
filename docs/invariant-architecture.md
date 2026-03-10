# Proposal: Separating Protocol and Transaction Invariants

## Terminology

A **protocol** is a cohesive group of ledger object types and the transactions that operate
on them. Examples:

- **Vault protocol**: `ltVAULT`, `ltMPTOKEN_ISSUANCE` (for shares), and the transactions
  `VaultCreate`, `VaultDeposit`, `VaultWithdraw`, `VaultSet`, `VaultClawback`, `VaultDelete`.
- **AMM protocol**: `ltAMM`, LP token trust lines, and the transactions `AMMCreate`,
  `AMMDeposit`, `AMMWithdraw`, `AMMVote`, `AMMBid`, `AMMDelete`, `AMMClawback`.
- **Lending protocol**: `ltLOAN`, `ltLOAN_BROKER`, and the loan/broker transactions.

Transactions may participate in multiple protocols (e.g., loan transactions that modify
vaults).

## Problem

XRPL on-chain protocols are implemented as semantically cohesive groups of transactions (see
`include/xrpl/tx/transactors/` — vault, dex, lending, etc.). Each protocol also has invariant
checks that run after every transaction to catch bugs before they reach the ledger.

The current invariant system conflates two distinct concerns in a single class:

1. **Protocol invariants** — properties that must hold regardless of which transaction ran.
   Examples: "a vault with zero shares must have zero assets", "AMM pool product
   sqrt(A\*B) >= LP tokens", "loan broker collateral covers outstanding loans."

2. **Transaction invariants** — properties specific to a particular transaction type.
   Examples: "VaultDeposit must increase vault balance by the deposit amount",
   "AMMCreate must produce LP tokens equal to sqrt(A\*B)", "Payment must not modify
   the AMM object."

This leads to:

- **Monolithic switch statements**: `ValidVault::finalize` is 800+ lines with a switch
  dispatching per transaction type. `ValidAMM::finalize` has the same pattern with 7 AMM
  transaction types plus 3 DEX transaction types. `ValidLoan` and `ValidLoanBroker` follow
  suit as the lending protocol grows.
- **Scattered rules**: invariant rules for any transaction are split between the transactor
  `.cpp` (business logic) and the protocol invariant `.cpp` (validation), making it hard to
  reason about a transaction holistically.
- **No isolation**: transaction-specific invariant logic cannot be unit-tested without the
  full invariant infrastructure.
- **Unbounded growth**: every new transaction type adds another case to one or more protocol
  invariant switches. Cross-protocol transactions (e.g., loan transactions that touch vaults)
  add cases to multiple switches.
- **No enforcement**: transaction invariants are entirely optional. A new transaction can be
  added without any invariant checks, and there is no compiler error or warning. The
  invariant code lives in a separate file from the transactor, so it's easy for engineers to
  skip invariants during development — especially under time pressure — with no automated
  safeguard catching the omission.

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

| Category            | Description                                                  | Examples                                                            |
| ------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------- |
| **Universal**       | Always run, no tx-type awareness                             | `XRPNotCreated`, `TransactionFeeCheck`, `LedgerEntryTypesMatch`     |
| **Privilege-gated** | Use `hasPrivilege()` to differentiate by tx-type             | `AccountRootsNotDeleted`, `ValidNewAccountRoot`, `ValidMPTIssuance` |
| **Protocol**        | Large switch on tx-type mixing protocol + tx-specific checks | `ValidVault`, `ValidAMM`, `ValidLoan`                               |

The first two categories work well. The third is where the problem lies.

---

## Proposed Architecture

### Goal

Separate protocol invariants from transaction invariants with minimal machinery:

- **Protocol invariants** stay in protocol invariant classes (e.g., `ValidVault`, `ValidAMM`,
  `ValidLoan`). They always run when the protocol is touched, regardless of transaction type.
- **Transaction invariants** move to the transactors themselves. They run only for their
  specific transaction type and check post-conditions specific to that transaction.
- **Compile-time enforcement**: transactors that declare they have invariants must implement
  them, or compilation fails.
- **Two-phase process preserved**: state collection in Phase 1, validation in Phase 2.
- **Cross-protocol support**: transactions that touch multiple protocols (e.g., loan transactions
  that modify vaults) can have invariants spanning all domains they interact with.

The proposals below differ in **how** transaction invariants are declared and dispatched.
The vault protocol is used as the primary example throughout, but the architecture applies
equally to AMM, lending, and any future on-chain protocol.

### Design

There are four components:

1. **Protocol state** — extracted from the current invariant class into its own type
2. **Protocol invariant class** — owns the state, runs protocol-wide checks, delegates tx dispatch
3. **Transaction invariants** — static methods on transactors
4. **Opt-in trait + dispatch** — connects transactors to protocol state with compile-time safety

Each is described below, using the vault protocol as an example. The same pattern applies to
AMM, lending, and any future protocol.

---

### 1. Protocol State

Each protocol extracts its accumulated state into a standalone class. This class is populated
during Phase 1 (`visitEntry`) and read during Phase 2 by both protocol checks and transaction
checks.

```cpp
// Example: vault protocol state
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

### 2. Protocol Invariant Class

Each protocol invariant class owns its state, delegates `visitEntry`, and splits `finalize` into
two parts: protocol checks (always run) and transaction dispatch.

```cpp
// Example: vault protocol invariant
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

    // --- Protocol invariants (always run) ---
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
`checkInvariants` method. The method receives the protocol state collected during Phase 1 and
validates transaction-specific post-conditions.

```cpp
// Example: vault deposit transaction invariant
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
protocol) define nothing — the dispatch skips them.

### 4. Opt-In Trait and Dispatch

#### The Trait

A transactor declares participation in a protocol by specializing `InvariantProtocols<T>`:

```cpp
// include/xrpl/tx/invariants/InvariantProtocols.h

#pragma once
#include <tuple>
#include <type_traits>

namespace xrpl {

// Primary template: no protocol invariants by default.
template <typename Transactor>
struct InvariantProtocols
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
struct InvariantProtocols<VaultDeposit>
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
protocol state. If it did, the transactor's `checkInvariants` is called — and if the method
is missing or has the wrong signature, **compilation fails**.

```cpp
// src/libxrpl/tx/invariants/InvariantDispatch.cpp

#define TRANSACTION_INCLUDE 1
#include <xrpl/protocol/detail/transactions.macro>

#include <xrpl/tx/invariants/InvariantProtocols.h>
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
                          typename InvariantProtocols<name>::types>) {       \
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

// Explicit instantiation — one line per protocol
template bool dispatchTransactionInvariant<VaultInvariantState>(
    VaultInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);
template bool dispatchTransactionInvariant<AMMInvariantState>(
    AMMInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);

}  // namespace xrpl
```

Each new protocol adds one explicit instantiation line. Forgetting it produces a linker error.

**Note on RAII number guards:** `Transactor::operator()` establishes `NumberSO` and
`CurrentTransactionRulesGuard` before calling `checkInvariants`, so the dispatch function
does not need to duplicate them.

---

## Execution Flow (Proposals A and B)

Proposals A and B share the same execution flow — transaction invariants are dispatched from
within protocol invariant `finalize` methods. Proposal C has a different flow; see the
Proposal C section for details.

### Phase 1 — State Collection

```
ApplyContext::checkInvariantsHelper()
  │
  │  for each modified ledger entry:
  │
  ├──▶ TransactionFeeCheck::visitEntry()    (no-op)
  ├──▶ XRPNotCreated::visitEntry()          → accumulates drops_
  ├──▶ ...
  ├──▶ ValidVault::visitEntry()             → collects vault/shares/delta state
  ├──▶ ValidAMM::visitEntry()               → collects AMM account/LP token state
  └──▶ ValidLoan::visitEntry()              → collects loan/broker state
```

### Phase 2 — Validation

The vault protocol is shown as an example. AMM and lending follow the same pattern.

```
ApplyContext::checkInvariantsHelper()
  │
  ├──▶ TransactionFeeCheck::finalize()       → checks fee
  ├──▶ XRPNotCreated::finalize()             → checks drops_
  ├──▶ ...
  ├──▶ ValidVault::finalize()
  │         │
  │         ├─ 1. protocol checks (always run)
  │         │        "exactly one vault modified"
  │         │        "immutable fields unchanged"
  │         │        "vault with zero shares has zero assets"
  │         │
  │         └─ 2. dispatch per-transaction check
  │                    │
  │                    │  ttVAULT_DEPOSIT → VaultDeposit::checkInvariants()
  │                    │    "vault balance increased by deposit amount"
  │                    │    "shares outstanding increased"
  │                    │
  │                    │  ttPAYMENT → not in vault protocol → skip
  │
  ├──▶ ValidAMM::finalize()
  │         │
  │         ├─ 1. protocol checks
  │         │        "AMM pool product sqrt(A*B) >= LP tokens"
  │         │
  │         └─ 2. dispatch per-transaction check
  │                    │
  │                    │  ttAMM_DEPOSIT → AMMDeposit::checkInvariants()
  │                    │  ttPAYMENT → Payment::checkInvariants(AMMState)
  │                    │    "AMM object must not change during DEX trade"
  │
  └──▶ ValidLoan::finalize()
            │
            └─ same pattern for lending transactions
```

---

## Include Dependencies (Proposals A and B)

Proposals A and B require dispatch infrastructure with specific include constraints.
Proposal C has no new include dependencies — it only modifies `Transactor.h/.cpp`.

The design avoids circular dependencies and minimizes compile-time impact:

```
InvariantProtocols.h          ← lightweight, only <tuple>
InvariantDispatch.h         ← lightweight, only forward declarations + STTx/TER
VaultInvariantState.h       ← protocol types only (Number, Asset, MPTIssue, etc.)

VaultInvariant.h            → VaultInvariantState.h, InvariantDispatch.h
VaultDeposit.h              → Transactor.h, InvariantProtocols.h
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

These apply to all three proposals:

| Component                     | Changes?     | Details                                                                  |
| ----------------------------- | ------------ | ------------------------------------------------------------------------ |
| `ApplyContext`                | **No**       | Tuple iteration, `visitEntry`/`finalize` interface unchanged             |
| `InvariantChecks` tuple       | **No**       | `ValidVault`, `ValidAMM`, `ValidLoan`, etc. stay in the tuple            |
| Universal invariants          | **No**       | `XRPNotCreated`, `TransactionFeeCheck`, etc. untouched                   |
| Privilege-gated invariants    | **No**       | `AccountRootsNotDeleted`, `ValidMPTIssuance`, etc. untouched             |
| Protocol invariant interfaces | **No**       | Still have `visitEntry` + `finalize` with same signatures                |
| Protocol invariant internals  | **Yes**      | `finalize` simplified: switch removed, keeps only protocol-wide checks   |
| Transactor classes            | **Yes**      | Gain transaction-specific invariant methods (details vary by proposal)   |
| `Transactor.h/.cpp`           | **C only**   | Add virtual methods + `checkTransactionInvariants` + modify `operator()` |
| `transactions.macro`          | **B only**   | Add `protocol` field to all 78 entries                                   |
| New infrastructure files      | **A/B only** | Dispatch headers/`.cpp`, protocol state classes (see proposals)          |

## Migration Path

The migration applies per-protocol. Each protocol (vault, AMM, lending) can be migrated
independently. Within a protocol, each transactor can be migrated one at a time — the switch
shrinks one case per step with no big-bang migration.

The vault protocol is shown as an example. The same pattern applies to AMM (7 AMM tx types +
3 DEX tx types), lending (loan and loan broker tx types), and any future protocol.

| Phase | Change                                                                           | Files touched                                                     |
| ----- | -------------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| 1     | Infrastructure for transaction invariant dispatch (varies by proposal)           | See proposal-specific details                                     |
| 2     | Simplify one protocol invariant: remove per-tx switch, keep protocol-wide checks | e.g., `VaultInvariant.cpp`                                        |
| 3     | Add transaction invariants to one transactor, remove its switch case             | e.g., `VaultDeposit.h`, `VaultDeposit.cpp`                        |
| 4     | Repeat for remaining transactors in the protocol                                 | e.g., `VaultCreate`, `VaultSet`, `VaultWithdraw`, `VaultClawback` |
| 5     | Delete empty switch from protocol invariant                                      | e.g., `VaultInvariant.cpp`                                        |
| 6     | Repeat phases 2–5 for next protocol (AMM, lending, etc.)                         | AMM/loan transactor and invariant files                           |

Each phase is independently deployable.

---

## Rejected Alternatives

The following approaches were considered and rejected. They are distinct from Proposals A,
B, and C presented below.

### CRTP Base Class

A CRTP base (`ProtocolInvariantBase<Derived, State>`) could provide `visitEntry` delegation
and the two-phase `finalize` pattern generically.

**Rejected because:**

- The codebase has zero CRTP in the invariant system today. Adding it raises the template
  literacy bar for all contributors.
- `static_cast<Derived*>(this)` is a known source of subtle bugs if the CRTP contract is
  violated.
- Each protocol's `finalize` has unique structure (feature gates, result filtering, helper
  methods). A generic base cannot capture this without becoming complex itself.

### Virtual Base Class for Protocol Invariants

A virtual `ProtocolInvariant` base class could provide a common interface for protocol
invariant checkers.

**Rejected because:**

- The existing invariant system is entirely duck-typed via `std::tuple`. Introducing virtual
  dispatch changes the paradigm for only some checkers, creating inconsistency.
- Does not solve the dispatch-to-transactor problem — you still need a mechanism to call
  transactor-specific `checkInvariants` from the invariant system.

### SFINAE Detection

Use `if constexpr (requires { T::checkInvariants(...) })` to detect whether a transactor
provides invariant checks.

**Rejected because:**

- Silent failure on signature mismatch or method removal. If someone changes the
  `checkInvariants` signature, removes it, or typos the name, the `requires` expression
  quietly evaluates to `false` and the check is silently skipped. This is the most dangerous
  failure mode for a safety-critical system like invariant checking.

### Runtime Registry

Transactors register `std::function` callbacks keyed by `TxType` at startup.

**Rejected because:**

- Static initialization order fiasco across translation units.
- Type safety loss (`void*` casts or type-erased state).
- A transactor that forgets to register silently passes — same problem as SFINAE.
- Runtime overhead on every transaction for no benefit.

---

## Proposal B: Macro-Based Protocol Opt-In

This is an alternative to the traits-based opt-in described above. Everything else in the
architecture (state extraction, protocol invariant classes, `checkInvariants` on transactors,
the dispatch function, the two-phase process) stays the same. Only **how a transactor declares
its protocol membership** changes.

### Motivation

The `transactions.macro` already serves as the single source of truth for transaction
metadata: tag, name, delegation, amendments, privileges, fields. The `privileges` bitfield
is already queried by invariant code via `hasPrivilege()`. A protocol bitfield follows the same
established pattern — it belongs in the macro alongside everything else, rather than scattered
across transactor headers as trait specializations.

### Protocol Enum

```cpp
// include/xrpl/tx/invariants/InvariantCheckProtocol.h

#pragma once
#include <cstdint>

namespace xrpl {

enum Protocol : std::uint16_t {
    noProtocol    = 0x0000,
    vaultProtocol = 0x0001,
    ammProtocol   = 0x0002,
    loanProtocol  = 0x0004,
};

constexpr Protocol operator|(Protocol a, Protocol b)
{
    return static_cast<Protocol>(
        static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

constexpr Protocol operator&(Protocol a, Protocol b)
{
    return static_cast<Protocol>(
        static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

}  // namespace xrpl
```

### Macro Change

Add `protocol` as the 7th parameter, pushing `fields` to 8th:

```
// Before (7 parameters):
TRANSACTION(tag, value, name, delegable, amendments, privileges, fields)

// After (8 parameters):
TRANSACTION(tag, value, name, delegable, amendments, privileges, protocol, fields)
```

Example entries:

```cpp
TRANSACTION(ttVAULT_DEPOSIT, 68, VaultDeposit,
    Delegation::delegable,
    featureSingleAssetVault,
    mayAuthorizeMPT | mustModifyVault,
    vaultProtocol,
    ({
    {sfVaultID, soeREQUIRED},
    {sfAmount, soeREQUIRED, soeMPTSupported},
}))

TRANSACTION(ttPAYMENT, 0, Payment,
    Delegation::notDelegable,
    uint256{},
    noPriv,
    noProtocol,
    ({
    {sfDestination, soeREQUIRED},
    ...
}))

// A transaction participating in multiple protocols:
TRANSACTION(ttLOAN_MANAGE, 81, LoanManage,
    Delegation::delegable,
    featureLendingProtocol,
    mustModifyVault | ...,
    loanProtocol | vaultProtocol,
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
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, protocol, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());
```

### Protocol-to-State Mapping

A template variable maps each `State` type to its protocol flag:

```cpp
// include/xrpl/tx/invariants/InvariantCheckProtocol.h

template <typename State>
inline constexpr Protocol protocolFor = noProtocol;
```

Each protocol state header specializes it:

```cpp
// include/xrpl/tx/invariants/VaultInvariantState.h (at the bottom)

template <>
inline constexpr Protocol protocolFor<VaultInvariantState> = vaultProtocol;
```

### Dispatch Function

The dispatch is the same structure as Proposal A, but uses the macro's protocol field instead
of a traits lookup:

```cpp
// src/libxrpl/tx/invariants/InvariantDispatch.cpp

#define TRANSACTION_INCLUDE 1
#include <xrpl/protocol/detail/transactions.macro>

#include <xrpl/tx/invariants/InvariantCheckProtocol.h>
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
    constexpr auto target = protocolFor<State>;

    switch (tx.getTxnType())
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, delegable, amend, priv, protocol, fields) \
    case tag: {                                                            \
        if constexpr ((protocol & target) != noProtocol) {                     \
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

// Explicit instantiations — one per protocol
template bool dispatchTransactionInvariant<VaultInvariantState>(
    VaultInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);
template bool dispatchTransactionInvariant<AMMInvariantState>(
    AMMInvariantState const&, STTx const&, TER,
    ReadView const&, beast::Journal const&);

}  // namespace xrpl
```

**Compile-time enforcement**: when the macro expands for `ttVAULT_DEPOSIT`, `protocol` is
literally `vaultProtocol`, so `(vaultProtocol & vaultProtocol) != noProtocol` is a true `constexpr`
expression. The `if constexpr` branch is compiled, and `VaultDeposit::checkInvariants` **must
exist** with the correct signature — otherwise compilation fails.

For `ttPAYMENT`, `protocol` is `noProtocol`, so `(noProtocol & vaultProtocol) != noProtocol` is false.
The branch is discarded. `Payment::checkInvariants` is never referenced.

### `hasProtocol` Query Function

Analogous to `hasPrivilege()`, for use in protocol invariant `finalize` methods:

```cpp
// include/xrpl/tx/invariants/InvariantCheckProtocol.h

bool hasProtocol(STTx const& tx, Protocol protocol);
```

```cpp
// src/libxrpl/tx/invariants/InvariantCheckProtocol.cpp

bool hasProtocol(STTx const& tx, Protocol protocol)
{
    switch (tx.getTxnType())
    {
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, delegable, amend, priv, txProtocol, fields) \
    case tag:                                                                \
        return (txProtocol & protocol) != noProtocol;
#include <xrpl/protocol/detail/transactions.macro>
#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
        default:
            return false;
    }
}
```

This allows protocol invariant classes to use `hasProtocol(tx, vaultProtocol)` in their protocol-wide
checks, analogous to how existing invariants use `hasPrivilege(tx, mustModifyVault)`.

### Proposal A vs Proposal B

| Aspect                       | A: Traits                           | B: Macro protocol field                    |
| ---------------------------- | ----------------------------------- | ------------------------------------------ |
| Opt-in location              | Scattered across transactor headers | Centralized in `transactions.macro`        |
| Single source of truth       | No — trait + macro both describe tx | Yes — macro is the one place               |
| Follows existing patterns    | New pattern (traits)                | Extends existing pattern (`privileges`)    |
| New transactor               | Add trait in header                 | Add protocol flag in macro                 |
| New protocol                 | Add `protocolFor<>` + tuple helper  | Add `protocolFor<>` + enum value           |
| Macro migration              | None                                | One-time: update `TxFormats.cpp`           |
| Compile-time enforcement     | Same (`if constexpr` → hard error)  | Same (`if constexpr` → hard error)         |
| Auditability                 | Grep headers for specializations    | Read one macro file                        |
| Runtime query                | N/A                                 | `hasProtocol()` parallels `hasPrivilege()` |
| No transactor header changes | Trait specialization needed         | Only `checkInvariants` declaration         |

See the **Comparative Analysis** section at the end of this document for a unified evaluation
of all three proposals.

---

## Proposal C: Virtual Functions on Transactor

This takes a fundamentally different approach. Instead of building dispatch machinery to route
from the invariant system to transactors, it adds virtual functions directly to the `Transactor`
base class. No traits, no macros, no templates, no dispatch functions.

### Motivation

`Transactor` is already virtual (`doApply()` is pure virtual). Adding more virtual functions
has no additional cost — the vtable already exists. Every transactor already inherits from
`Transactor` and already implements `doApply()`. Adding invariant methods follows the same
pattern.

Unlike Proposals A and B, which build opt-in mechanisms, this approach makes transaction
invariants **mandatory**: every transactor must make a deliberate decision about its invariants,
even if that decision is "none" (empty implementation). There is no possibility of forgetting
to opt in — the compiler enforces it via pure virtual.

### Design

#### New Virtual Methods on Transactor

```cpp
// include/xrpl/tx/Transactor.h

class Transactor
{
    // ... existing members ...

protected:
    // --- Transaction Invariants ---
    // Called for each modified ledger entry. Override to collect state
    // needed for transaction-specific invariant checks.
    virtual void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) = 0;

    // Called after all entries visited. Override to validate
    // transaction-specific post-conditions. Return true if all
    // invariants hold, false if any fail.
    virtual bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) = 0;

private:
    // Non-virtual, non-static. Mirrors checkInvariantsHelper but calls
    // the transactor's own virtual methods.
    TER
    checkTransactionInvariants(TER result, XRPAmount fee);
};
```

#### `checkTransactionInvariants` Implementation

```cpp
// src/libxrpl/tx/Transactor.cpp

TER
Transactor::checkTransactionInvariants(TER result, XRPAmount fee)
{
    try
    {
        // Phase 1: visit modified entries
        ctx_.visit(
            [this](
                uint256 const&,
                bool isDelete,
                std::shared_ptr<SLE const> const& before,
                std::shared_ptr<SLE const> const& after) {
                this->visitInvariantEntry(isDelete, before, after);
            });

        // Phase 2: finalize
        if (!this->finalizeInvariants(ctx_.tx, result, fee, *ctx_.view_, j_))
        {
            JLOG(j_.fatal())
                << "Transaction has failed one or more transaction invariants";
            return ctx_.failInvariantCheck(result);
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(j_.fatal())
            << "Transaction invariant check exception:, ex: " << ex.what() << ", tx: " << to_string(ctx_.tx->getJson(JsonOptions::none));
        return ctx_.failInvariantCheck(result);
    }
    return result;
}
```

#### Integration in `Transactor::operator()`

The existing "check, reset if fail, check again" pattern expands to cover both protocol and
transaction invariants:

```cpp
// src/libxrpl/tx/Transactor.cpp — in operator()

if (applied)
{
    // Transaction invariants first (more specific).
    // These check post-conditions of the specific transaction type
    // (e.g., "VaultDeposit increased vault balance by deposit amount").
    // If these fail, the transaction's core logic is wrong — there is
    // no point running protocol invariants on a known-bad state.
    result = checkTransactionInvariants(result, fee);

    // Protocol invariants second (broader), only if transaction invariants passed.
    // These check properties that must hold regardless of transaction type
    // (e.g., "XRP not created", "vault with zero shares has zero assets").
    //  Running protocol invariants after that is wasteful, the transaction is
    //  already going to be rejected. Worse, a transaction invariant failure
    //  could cause protocol invariants to produce misleading secondary failures
    //  (e.g., a broken deposit leaves the vault in a state that also trips
    //  the protocol check, generating confusing double-failure logs).
    if (isTesSuccess(result) || isTecClaim(result))
        result = ctx_.checkInvariants(result, fee);

    if (result == tecINVARIANT_FAILED)
    {
        // Reset to fee-claim only
        auto const resetResult = reset(fee);
        if (!isTesSuccess(resetResult.first))
            result = resetResult.first;
        fee = resetResult.second;

        // After reset, only protocol invariants are re-checked.
        // Transaction invariants are not meaningful here — the
        // transaction's effects have been rolled back, so checks like
        // "deposit increased vault balance" don't apply. Only the
        // protocol/universal invariants matter for the fee-claim-only state.
        if (isTesSuccess(result) || isTecClaim(result))
            result = ctx_.checkInvariants(result, fee);
    }

    if (!isTecClaim(result) && !isTesSuccess(result))
        applied = false;
}
```

#### Transactor Implementations

Each transactor builds its own invariant state — just as every existing invariant checker
(e.g., `XRPNotCreated` accumulates `drops_`, `ValidVault` accumulates vault/shares data) builds
its own state today. A transactor knows which ledger objects it touches because it wrote them
in `doApply`. No shared state infrastructure is needed.

`result` is passed through to `finalizeInvariants` — the transactor decides how to use it.
Transaction invariants can check meaningful post-conditions for both success and failure cases.
For example, a successful `VaultDeposit` should verify the vault balance increased; a failed
one should verify the vault balance was **not** modified.

**Vault transaction — collects vault-specific state:**

```cpp
// include/xrpl/tx/transactors/vault/VaultDeposit.h

class VaultDeposit : public Transactor
{
    // Transactor decides what state to track. It knows what ledger
    // objects it touches — vault, MPT issuance, account balances.
    std::optional<Number> vaultAssetsBefore_;
    std::optional<Number> vaultAssetsAfter_;
    std::optional<std::uint64_t> sharesBefore_;
    std::optional<std::uint64_t> sharesAfter_;

public:
    // ... existing members ...

protected:
    void visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    bool finalizeInvariants(
        STTx const& tx, TER result, XRPAmount fee,
        ReadView const& view, beast::Journal const& j) override;
};
```

```cpp
// src/libxrpl/tx/transactors/vault/VaultDeposit.cpp

void VaultDeposit::visitInvariantEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltVAULT)
    {
        vaultAssetsAfter_ = after->at(sfAssetsTotal);
        if (before)
            vaultAssetsBefore_ = before->at(sfAssetsTotal);
    }
    if (after && after->getType() == ltMPTOKEN_ISSUANCE)
    {
        sharesAfter_ = after->at(sfOutstandingAmount);
        if (before)
            sharesBefore_ = before->at(sfOutstandingAmount);
    }
}

bool VaultDeposit::finalizeInvariants(
    STTx const& tx, TER result, XRPAmount fee,
    ReadView const& view, beast::Journal const& j)
{
    if (isTesSuccess(result))
    {
        // Success invariants:
        //   - vault assets increased by deposit amount
        //   - shares outstanding increased
    }
    else
    {
        // Failure invariants:
        //   - vault assets unchanged
        //   - no shares issued
    }
}
```

**AMM transaction — collects AMM-specific state:**

```cpp
// include/xrpl/tx/transactors/dex/AMMDeposit.h

class AMMDeposit : public Transactor
{
    std::optional<AccountID> ammAccount_;
    std::optional<STAmount> lptBefore_;
    std::optional<STAmount> lptAfter_;

public:
    // ... existing members ...

protected:
    void visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    bool finalizeInvariants(
        STTx const& tx, TER result, XRPAmount fee,
        ReadView const& view, beast::Journal const& j) override;
};
```

```cpp
// src/libxrpl/tx/transactors/dex/AMMDeposit.cpp

void AMMDeposit::visitInvariantEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    if (after && after->getType() == ltAMM)
    {
        ammAccount_ = after->getAccountID(sfAccount);
        lptAfter_ = after->getFieldAmount(sfLPTokenBalance);
        if (before)
            lptBefore_ = before->getFieldAmount(sfLPTokenBalance);
    }
}

bool AMMDeposit::finalizeInvariants(
    STTx const& tx, TER result, XRPAmount fee,
    ReadView const& view, beast::Journal const& j)
{
    if (isTesSuccess(result))
    {
        // Deposit invariant: sqrt(amount * amount2) >= LP tokens
        // AMM account must exist, pool balances > 0
    }
    else
    {
        // AMM state unchanged
    }
}
```

**Cross-protocol transaction — collects state from multiple protocols:**

```cpp
// include/xrpl/tx/transactors/lending/LoanManage.h

class LoanManage : public Transactor
{
    // Touches both loan and vault ledger objects
    std::optional<Number> loanBalance_;
    std::optional<Number> vaultAssetsBefore_;
    std::optional<Number> vaultAssetsAfter_;

public:
    // ... existing members ...

protected:
    void visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override
    {
        if (after && after->getType() == ltLOAN)
            loanBalance_ = after->at(sfBalance);
        if (after && after->getType() == ltVAULT)
        {
            vaultAssetsAfter_ = after->at(sfAssetsTotal);
            if (before)
                vaultAssetsBefore_ = before->at(sfAssetsTotal);
        }
    }

    bool finalizeInvariants(
        STTx const& tx, TER result, XRPAmount fee,
        ReadView const& view, beast::Journal const& j) override;
};
```

**DEX transaction that interacts with AMM — guards AMM state:**

```cpp
// include/xrpl/tx/transactors/payment/Payment.h

class Payment : public Transactor
{
    // Payment can route through AMM pools. Track whether AMM was modified.
    bool ammObjectChanged_ = false;

public:
    // ... existing members ...

protected:
    void visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override
    {
        if (after && after->getType() == ltAMM)
            ammObjectChanged_ = true;
    }

    bool finalizeInvariants(
        STTx const& tx, TER result, XRPAmount fee,
        ReadView const& view, beast::Journal const& j) override
    {
        if (ammObjectChanged_)
        {
            // DEX trades must not modify the AMM object
            JLOG(j.fatal()) << "Payment invariant failed: AMM object changed";
            return false;
        }
        return true;
    }
};
```

#### Protocol Invariants Stay Unchanged

`ValidVault`, `ValidAMM`, etc. remain in the `InvariantChecks` tuple. Their `finalize`
methods are simplified by removing the per-transaction switch — they keep only the protocol-wide
checks that must hold regardless of transaction type (e.g., "vault with zero shares has zero
assets", "AMM pool product invariant").

Protocol invariants and transaction invariants are **independent**. Each builds its own state
during its own visit pass. There is no shared state infrastructure between them — just as
`XRPNotCreated` and `ValidVault` don't share state today.

### Trade-offs

**Advantages over Proposals A and B:**

- **Simplest C++ mechanisms**: virtual dispatch, no templates, no traits, no macro changes,
  no dispatch functions, no explicit instantiations, no protocol state classes.
- **Strongest enforcement**: pure virtual means the compiler rejects any transactor that doesn't
  implement the methods. There is no "forgetting to opt in" — every transactor makes a
  deliberate decision.
- **No new infrastructure**: no `InvariantProtocols.h`, `InvariantDispatch.h`,
  `InvariantDispatch.cpp`, `InvariantCheckProtocol.h`, no protocol state headers. The only new
  code is two virtual methods on an existing class.
- **Follows existing Transactor pattern**: `preflight`, `preclaim`, `doApply`, and now
  `visitInvariantEntry`/`finalizeInvariants` — all virtual lifecycle hooks on the same class.
- **No changes to `transactions.macro`**, `ApplyContext`, or `InvariantChecks` tuple.
- **Natural cross-protocol support**: a transactor that touches multiple protocols simply collects
  state for all of them in one `visitInvariantEntry` and checks all of them in one
  `finalizeInvariants`. No multi-protocol dispatch machinery needed.
- **Transactor owns its invariant knowledge**: each transactor knows exactly which ledger
  objects it touches (it wrote them in `doApply`), so it naturally knows what to collect and
  check. No external system needs to tell it.

**Disadvantages:**

- **78 transactors need implementation**: every existing transactor needs `visitInvariantEntry`
  and `finalizeInvariants` overrides. This is a large mechanical change, though many are not
  truly empty — transactions like `Payment`, `OfferCreate`, and `CheckCash` have meaningful
  AMM guard invariants.
- **Double visit loop**: modified ledger entries are iterated twice — once by
  `checkInvariantsHelper` (for protocol invariants in the tuple) and once by
  `checkTransactionInvariants` (for the transactor's virtual methods). The entries are already
  in memory, so the cost is negligible.
- **No centralized "which transactions have invariants for this protocol?" query**: unlike
  Proposal B's `hasProtocol()`, protocol membership is implicit in what each transactor collects.
  To audit all vault invariants, you search transactor files rather than one macro file.

### Proposal A vs B vs C

| Aspect                         | A: Traits                                                    | B: Macro                                                     | C: Virtual                                               |
| ------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------------- |
| Opt-in mechanism               | Trait specialization                                         | Macro protocol field                                         | Pure virtual (mandatory)                                 |
| New infrastructure             | `InvariantProtocols.h`, dispatch `.cpp`                      | `InvariantCheckProtocol.h`, dispatch `.cpp`                  | None                                                     |
| Compile-time enforcement       | `if constexpr` → error                                       | `if constexpr` → error                                       | Pure virtual → error                                     |
| Transactors without invariants | Do nothing                                                   | Do nothing                                                   | Must implement (empty or non-empty)                      |
| State management               | Protocol state classes, shared with dispatch                 | Protocol state classes, shared with dispatch                 | Transactor owns its own state                            |
| Cross-protocol transactions    | Multiple `checkInvariants` overloads per protocol state type | Multiple `checkInvariants` overloads per protocol state type | Single `finalizeInvariants`, collects all relevant state |
| Visit loop                     | Runs once (in protocol invariant)                            | Runs once (in protocol invariant)                            | Runs twice (protocol + transaction)                      |
| Macro changes                  | None                                                         | Add `protocol` field                                         | None                                                     |
| Transactor.h changes           | None                                                         | None                                                         | Add 2 virtual methods                                    |
| Transactor.cpp changes         | None                                                         | None                                                         | Add `checkTransactionInvariants` + modify `operator()`   |
| Auditability                   | Grep headers for trait specializations                       | Read one macro file                                          | Search transactor implementations                        |
| Follows existing patterns      | New (traits)                                                 | Extends (`privileges`)                                       | Extends (`doApply`)                                      |

---

## Comparative Analysis

### How Each Proposal Addresses the Problems

| Problem               | A: Traits                                                                                                      | B: Macro                                                                                       | C: Virtual                                                                                                                               |
| --------------------- | -------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| **Monolithic switch** | Solved — switch replaced by dispatch                                                                           | Solved — switch replaced by dispatch                                                           | Solved — switch replaced by virtual call                                                                                                 |
| **Scattered rules**   | Partial — tx invariants co-located with transactor, but receive state from a separate protocol state class     | Same as A                                                                                      | Full — tx invariant logic is entirely self-contained in the transactor                                                                   |
| **No isolation**      | Solved — static `checkInvariants` testable with constructed state                                              | Same as A                                                                                      | Solved — `finalizeInvariants` testable on a constructed transactor                                                                       |
| **Unbounded growth**  | Solved — new tx adds to its own file                                                                           | Same as A                                                                                      | Same                                                                                                                                     |
| **No enforcement**    | Partial — opt-in is manual. Forgetting the trait means no invariant. But if you opt in, the method must exist. | Same as A — opt-in is manual (adding protocol flag). But if you opt in, the method must exist. | Strongest — pure virtual forces every transactor to implement. Implementations can be empty, but the decision is visible in code review. |

**On enforcement**: none of the proposals fully prevents an engineer from skipping invariant
logic. A and B prevent forgetting to _implement_ after opting in, but don't prevent
forgetting to _opt in_. C prevents forgetting to _implement_ something, but doesn't prevent
implementing it as empty `{ return true; }`. The real-world difference: in C, an empty
implementation is visible in code review as a deliberate choice. In A/B, the absence of an
opt-in is invisible — there's nothing in the transactor file to review.

### Invasiveness

**Proposal A** — ~20-30 files touched for vault+AMM migration. New infrastructure files
(trait template, dispatch header/`.cpp`, protocol state classes). Trait specializations added
to transactor headers. Conceptually the most complex (templates, `tuple_contains`, explicit
instantiation).

**Proposal B** — Everything in A, plus `transactions.macro` modified (add `protocol` field
to all ~78 TRANSACTION entries) and `TxFormats.cpp` updated. The macro change is a ~100+ line
diff touching a high-traffic file — a merge conflict magnet for concurrent PRs that add or
modify transactions.

**Proposal C** — ~85+ files touched (78 transactors + `Transactor.h/.cpp` + protocol
invariant files). No new infrastructure. Each transactor change is mechanical (add two method
overrides). The largest diff by file count, but the smallest by conceptual complexity.

### Difficulty of Implementation

**Proposal A** — Medium-High. Template metaprogramming (`tuple_contains`, explicit template
instantiation, `if constexpr` with trait lookup) requires template literacy. Protocol state
extraction is careful work. Infrastructure must exist before migrating the first switch case.

**Proposal B** — Medium. Less template complexity than A (no `tuple_contains`). But the
macro change is a large, disruptive diff with high merge conflict risk. Protocol state
extraction and per-transactor migration are the same effort as A.

**Proposal C** — Medium. Conceptually simplest (virtual dispatch). The 78-transactor change
is large in diff size but trivial in complexity — it could be scripted. Protocol invariant
simplification is the same effort as A/B. No infrastructure setup needed before migration
begins.
