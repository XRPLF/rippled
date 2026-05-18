# `include/xrpl/protocol_autogen/ledger_entries/Loan.h`

## Role in the System

This file is part of the XRPL `protocol_autogen` layer — a code-generated, type-safe API over the raw serialized ledger objects (`SLE`) that underlie the XRP Ledger's state. It defines two classes for the `ltLOAN` ledger entry type (code `0x0089`): an immutable read wrapper `Loan` and a mutable construction helper `LoanBuilder`. Both live in `xrpl::ledger_entries`.

The `Loan` entry represents an on-ledger loan record issued through the XRPL Lending Protocol (gated by the `featureLendingProtocol` amendment). It stores the full economic and schedule state of a loan: the borrower's identity, the broker that originated it, payment schedule, fee structure, interest rates, and running outstanding balances. Its sibling entry `LoanBroker` (`ltLOAN_BROKER`, `0x0088`) acts as the broker registry; `Loan` points back to it via `sfLoanBrokerID` and tracks its position in the broker's directory via `sfLoanBrokerNode`.

## `Loan` — Immutable SLE Wrapper

`Loan` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` named `sle_`. Because the underlying SLE is const and reference-counted, a `Loan` object is inherently thread-safe to read and cannot mutate ledger state. The constructor verifies that the wrapped SLE is actually `ltLOAN` before accepting it, throwing `std::runtime_error` on mismatch — a fail-fast guard against type confusion when reading ledger state from the database or incoming transactions.

All getters are marked `[[nodiscard]]`, which prevents callers from silently ignoring returned values and acts as a compile-time reminder that query results must be consumed.

### Required vs. Optional Fields

The design clearly separates two categories of fields:

**Required fields** (`soeREQUIRED`) return their value directly:
- `getPreviousTxnID()` / `getPreviousTxnLgrSeq()` — standard ledger bookkeeping linking the entry to its last modifying transaction.
- `getOwnerNode()` — position in the borrower's owner directory.
- `getLoanBrokerNode()` / `getLoanBrokerID()` — back-reference to the originating `LoanBroker` entry and its directory slot.
- `getLoanSequence()` — per-broker monotonically increasing counter that disambiguates multiple loans from the same broker without ledger key collisions.
- `getBorrower()` — the `SF_ACCOUNT` address of the borrower.
- `getStartDate()` / `getPaymentInterval()` — temporal anchors for the repayment schedule, stored as XRPL epoch timestamps (`SF_UINT32`).
- `getPeriodicPayment()` — the fixed instalment amount, as an `SF_NUMBER` (XRPL's arbitrary-precision decimal type).

**Optional/default fields** (`soeDEFAULT`) return `protocol_autogen::Optional<T>` (a `std::optional` alias), always paired with a `has*()` predicate:
- **Fee schedule**: `getLoanOriginationFee()`, `getLoanServiceFee()`, `getLatePaymentFee()`, `getClosePaymentFee()` — all `SF_NUMBER`, capturing financial amounts with decimal precision. `getOverpaymentFee()` is `SF_UINT32`, suggesting a basis-points representation rather than an absolute amount.
- **Interest rates**: `getInterestRate()`, `getLateInterestRate()`, `getCloseInterestRate()`, `getOverpaymentInterestRate()` — all `SF_UINT32`, likely encoded as basis points. These are optional because a broker may offer zero-interest or interest-free loans.
- **Amortization state**: `getPrincipalOutstanding()`, `getTotalValueOutstanding()`, `getManagementFeeOutstanding()` — `SF_NUMBER` running balances updated as payments are applied.
- **Schedule tracking**: `getGracePeriod()`, `getPreviousPaymentDueDate()`, `getNextPaymentDueDate()`, `getPaymentRemaining()` — temporal and counter fields that advance through the repayment life cycle.
- **`getLoanScale()`** — the sole `SF_INT32` field, a signed decimal scaling factor for interpreting the loan's numeric fields. Optional to allow default (unscaled) behavior.

The four interest-rate variants (normal, late, close, overpayment) and the parallel fee variants reflect distinct contract phases: active repayment, delinquency, early payoff, and overpayment scenarios. Keeping these as separate optional fields rather than a lookup table allows the protocol to omit rates that don't apply to a given loan product without allocating ledger space.

## `LoanBuilder` — Fluent Construction

`LoanBuilder` extends `LedgerEntryBuilderBase<LoanBuilder>`, a CRTP base that holds a mutable `STObject object_{sfLedgerEntry}`. The CRTP pattern allows all `set*()` calls in the base to return `Derived&` for method chaining without virtual dispatch overhead.

The primary constructor enforces the required-field contract by accepting all ten mandatory fields as arguments and calling the corresponding setters immediately. This prevents constructing a `Loan` in a partially-initialized, invalid state — the type system itself makes "missing required field" a compile error rather than a runtime failure.

An important design choice in `LedgerEntryBuilderBase` (documented in its constructor) is that `object_.set(soTemplate)` is deliberately not called. Applying the SO template early would insert `soeDEFAULT` placeholder objects for all optional fields; these placeholders would later trigger `"may not be explicitly set to default"` errors inside `SLE::applyTemplate()`. By keeping `object_` as a free-form `STObject`, only fields explicitly set by the caller are present when `build()` finally constructs the `SLE`.

A secondary constructor `LoanBuilder(std::shared_ptr<SLE const> sle)` enables mutation of an existing entry: it copies the SLE's field data into `object_` via `object_ = *sle`, stripping the const qualification and allowing subsequent `set*()` calls before re-building. This round-trip pattern is how ledger-modifying transaction handlers read-modify-write a `Loan` entry.

`build(uint256 const& index)` consumes the builder by `std::move`-ing `object_` into a new `SLE` at the given ledger key, then wraps it in a `Loan`. After calling `build()`, the builder's internal state is moved-from; callers should treat the builder as expired.

## Relationship to Surrounding Infrastructure

`Loan.h` is one of ~30 auto-generated entry files under `protocol_autogen/ledger_entries/`. All share the same structural pattern — each file is machine-generated from a protocol schema definition and should never be edited by hand. The generation approach lets the schema source of truth drive both the serialization layer (`SField`, `STObject`) and the strongly-typed C++ API simultaneously, eliminating hand-written marshalling code. The `LoanBroker.h` counterpart follows an identical structure for `ltLOAN_BROKER` and exposes complementary fields like the broker's cover balances and loan limits that govern which `Loan` entries it may issue.