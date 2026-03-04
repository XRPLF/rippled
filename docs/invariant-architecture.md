# Proposal: Per-Transactor Invariant Checks

## Problem

Domain invariants (e.g. `ValidVault`, `ValidAMM`) accumulate transaction-specific business rules
in a switch statement inside `finalize`. As new transaction types are added, these files grow
unboundedly and mix rules from different transactions in a single place:

- `VaultInvariant.cpp` is 900+ lines with a 450-line switch in `finalize`
- Rules for `VaultDeposit` are split across `VaultDeposit.cpp` and `VaultInvariant.cpp`
- Transaction-specific invariant logic cannot be unit-tested in isolation

## Proposed Solution

Split each domain invariant into two responsibilities:

1. **State collection** — a focused `[Domain]InvariantState` class that accumulates only the
   ledger changes relevant to its domain during Phase 1 (`visitEntry`)
2. **Validation** — cross-cutting checks stay in `Valid[Domain]::finalizeCommon`; per-transaction
   checks move to a static `checkInvariants` method on the transactor itself

A CRTP base `DomainInvariantBase<Derived, State>` provides the `visitEntry` delegation and
per-transactor dispatch generically, so no boilerplate is repeated across domains.

`ApplyContext` is unchanged.

---

## Design

### `DomainInvariantState` Concept

Every domain state class must implement `visitEntry`. This is enforced via a C++20 concept:

```cpp
// include/xrpl/tx/invariants/InvariantCheck.h

template <typename T>
concept DomainInvariantState = requires(
    T& state,
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    state.visitEntry(isDelete, before, after);
};
```

### `DomainInvariantBase` CRTP Base

Provides `visitEntry` delegation and per-transactor dispatch. Domain-specific cross-cutting
checks are injected via the CRTP `finalizeCommon` hook:

```cpp
// include/xrpl/tx/invariants/InvariantCheck.h

template <typename Derived, DomainInvariantState State>
class DomainInvariantBase
{
    State state_;

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
        beast::Journal const& j)
    {
        // 1. Cross-cutting checks — overridden by Derived, default is pass
        if (!static_cast<Derived*>(this)->finalizeCommon(state_, tx, result, fee, view, j))
            return false;

        // 2. Per-transactor dispatch — calls T::checkInvariants(State const&, ...)
        //    if defined, otherwise passes
        return with_txn_type(view.rules(), tx.getTxnType(),
            [&]<typename T>() -> bool {
                if constexpr (requires {
                    T::checkInvariants(
                        std::declval<State const&>(),
                        std::declval<STTx const&>(),
                        std::declval<TER>(),
                        std::declval<ReadView const&>(),
                        std::declval<beast::Journal const&>());
                })
                    return T::checkInvariants(state_, tx, result, view, j);
                return true;
            });
    }

    // Default: no cross-cutting checks. Derived overrides if needed.
    bool finalizeCommon(
        State const&,
        STTx const&,
        TER,
        XRPAmount,
        ReadView const&,
        beast::Journal const&)
    {
        return true;
    }
};
```

### Domain State Classes

Each domain defines a focused accumulator that satisfies `DomainInvariantState`. It collects
only what is relevant to its domain and exposes read-only accessors for use in `finalizeCommon`
and per-transactor `checkInvariants`.

```cpp
// include/xrpl/tx/invariants/VaultInvariant.h

class VaultInvariantState
{
public:
    struct Vault
    {
        uint256   key             = beast::zero;
        Asset     asset           = {};
        AccountID pseudoId        = {};
        AccountID owner           = {};
        uint192   shareMPTID      = beast::zero;
        Number    assetsTotal     = 0;
        Number    assetsAvailable = 0;
        Number    assetsMaximum   = 0;
        Number    lossUnrealized  = 0;

        static Vault make(SLE const&);
    };

    struct Shares
    {
        MPTIssue      share         = {};
        std::uint64_t sharesTotal   = 0;
        std::uint64_t sharesMaximum = 0;

        static Shares make(SLE const&);
    };

    void visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    std::vector<Vault> const&  beforeVault() const { return beforeVault_; }
    std::vector<Vault> const&  afterVault()  const { return afterVault_;  }
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

### `Valid[Domain]` Wrappers

Each `Valid[Domain]` inherits from `DomainInvariantBase` and overrides `finalizeCommon` only if
it has cross-cutting checks. The tuple membership and `visitEntry`/`finalize` interface are
provided by the base — the wrapper needs no boilerplate.

```cpp
// include/xrpl/tx/invariants/VaultInvariant.h

class ValidVault : public DomainInvariantBase<ValidVault, VaultInvariantState>
{
public:
    bool finalizeCommon(
        VaultInvariantState const& state,
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j);
};
```

```cpp
// include/xrpl/tx/invariants/AMMInvariant.h

class ValidAMM : public DomainInvariantBase<ValidAMM, AMMInvariantState>
{
public:
    // Cross-cutting: DEX transactions (Payment, OfferCreate, CheckCash)
    // must not modify the AMM object.
    bool finalizeCommon(
        AMMInvariantState const& state,
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j);
};
```

A domain with no cross-cutting checks requires no override:

```cpp
class ValidLoan : public DomainInvariantBase<ValidLoan, LoanInvariantState> {};
```

### Per-Transactor `checkInvariants`

Each transactor that has transaction-specific invariant logic defines a static
`checkInvariants` taking its domain state as the first parameter. Transactors that have no
transaction-specific checks define nothing — the base silently passes.

```cpp
// include/xrpl/tx/transactors/Vault/VaultDeposit.h

class VaultDeposit : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit VaultDeposit(ApplyContext& ctx) : Transactor(ctx) {}

    static NotTEC preflight(PreflightContext const& ctx);
    static TER    preclaim(PreclaimContext const& ctx);
    TER           doApply() override;

    static bool checkInvariants(
        VaultInvariantState const& state,
        STTx const& tx,
        TER result,
        ReadView const& view,
        beast::Journal const& j);
};
```

```cpp
// src/libxrpl/tx/transactors/Vault/VaultDeposit.cpp

bool VaultDeposit::checkInvariants(
    VaultInvariantState const& state,
    STTx const& tx,
    TER result,
    ReadView const& view,
    beast::Journal const& j)
{
    // Logic previously in the ttVAULT_DEPOSIT case of ValidVault::finalize
    // e.g.:
    //   vault balance increased by deposit amount
    //   depositor balance decreased by same amount
    //   vault shares outstanding increased
    //   assets maximum not exceeded
}
```

### `ApplyContext` is unchanged

`checkInvariantsHelper` calls `visitEntry` and `finalize` via parameter pack expansion over the
`InvariantChecks` tuple. Because `Valid[Domain]` inherits `visitEntry` and `finalize` from
`DomainInvariantBase`, it satisfies the existing interface without any changes to `ApplyContext`.

```cpp
// ApplyContext.cpp — no changes

auto checkers = getInvariantChecks();

visit([&checkers](uint256 const&, bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) {
    (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
});

std::array<bool, sizeof...(Is)> finalizers{
    {std::get<Is>(checkers).finalize(tx, result, fee, *view_, journal)...}};
```

---

## Invariant Architecture Diagrams

### Component relationships

```
┌─────────────────────────────────────────────────────────────────────┐
│                          InvariantChecks tuple                      │
│                                                                     │
│  TransactionFeeCheck │ XRPNotCreated │ ... │ ValidVault │ ValidAMM  │
└───────────────────────────────────────┬─────────────┬──────────────┘
                                        │             │
                    ┌───────────────────┘             └──────────────────┐
                    ▼                                                     ▼
     ┌──────────────────────────┐                      ┌──────────────────────────┐
     │        ValidVault        │                      │         ValidAMM         │
     │  DomainInvariantBase     │                      │  DomainInvariantBase     │
     │  <ValidVault,            │                      │  <ValidAMM,              │
     │   VaultInvariantState>   │                      │   AMMInvariantState>     │
     └────────────┬─────────────┘                      └────────────┬─────────────┘
                  │ owns                                             │ owns
                  ▼                                                  ▼
     ┌────────────────────────┐                      ┌────────────────────────┐
     │   VaultInvariantState  │                      │   AMMInvariantState    │
     │                        │                      │                        │
     │  visitEntry()          │                      │  visitEntry()          │
     │  beforeVault_          │                      │  ammAccount_           │
     │  afterVault_           │                      │  lptBalanceBefore_     │
     │  deltas_               │                      │  lptBalanceAfter_      │
     └────────────────────────┘                      └────────────────────────┘
```

### Phase 1 — state collection

```
ApplyContext::checkInvariantsHelper()
  │
  │  for each modified ledger entry e:
  │
  ├──▶ TransactionFeeCheck::visitEntry(e)   (no-op)
  ├──▶ XRPNotCreated::visitEntry(e)         → accumulates drops_
  ├──▶ ...
  └──▶ ValidVault::visitEntry(e)            (inherited from DomainInvariantBase)
            │
            └──▶ VaultInvariantState::visitEntry(e)
                      │
                      ├─ ltVAULT             → push to beforeVault_ / afterVault_
                      ├─ ltMPTOKEN_ISSUANCE  → push to beforeMPTs_ / afterMPTs_
                      ├─ ltACCOUNT_ROOT      → record in deltas_
                      └─ (all other types ignored)
```

### Phase 2 — finalize and per-transactor dispatch

```
ApplyContext::checkInvariantsHelper()
  │
  ├──▶ TransactionFeeCheck::finalize()   → checks fee directly from tx
  ├──▶ XRPNotCreated::finalize()         → checks accumulated drops_
  ├──▶ ...
  └──▶ ValidVault::finalize()            (inherited from DomainInvariantBase)
            │
            ├─ 1. finalizeCommon(state_)
            │        "exactly one vault modified per transaction"
            │        "MPT issuance consistent with vault shareMPTID"
            │        "vault without shares has zero assets"
            │
            └─ 2. with_txn_type(tx.getTxnType())
                       │
                       │  tx is ttVAULT_DEPOSIT → T = VaultDeposit
                       │
                       │  requires { T::checkInvariants(VaultInvariantState const&, ...) } ✓
                       │
                       └──▶ VaultDeposit::checkInvariants(state_, tx, result, view, j)
                                  "vault balance increased by deposit amount"
                                  "depositor balance decreased by same amount"
                                  "vault shares outstanding increased"
```

---

## Migration Path

| Phase | Change                                                                                                      | Files                                                      |
| ----- | ----------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| 1     | Add `DomainInvariantState` concept and `DomainInvariantBase` template                                       | `InvariantCheck.h`                                         |
| 2     | Extract `VaultInvariantState` from `ValidVault`; migrate `ValidVault` to inherit from `DomainInvariantBase` | `VaultInvariant.h`, `VaultInvariant.cpp`                   |
| 3     | Add `checkInvariants` to one transactor (e.g. `VaultDeposit`), migrate its case from the switch             | `VaultDeposit.h`, `VaultDeposit.cpp`, `VaultInvariant.cpp` |
| 4     | Repeat for remaining vault transactors (`VaultCreate`, `VaultSet`, `VaultWithdraw`, `VaultClawback`)        | Vault transactor files, `VaultInvariant.cpp`               |
| 5     | Remove switch from `ValidVault::finalize` (now empty after migration)                                       | `VaultInvariant.cpp`                                       |
| 6     | Repeat phases 2–5 for `ValidAMM` and lending invariants as they are implemented                             | AMM and loan transactor files                              |

Each phase is independently deployable. The switch shrinks one case at a time with no big-bang
migration required.
