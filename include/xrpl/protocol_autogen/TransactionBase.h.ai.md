# `TransactionBase.h` — Immutable Base for Auto-Generated Transaction Wrappers

`TransactionBase` is the hand-authored root of the `xrpl::transactions` type hierarchy. It lives inside the `protocol_autogen` module, which generates a separate strongly-typed C++ class for every XRPL transaction kind (over seventy at present: `Payment`, `AMMBid`, `EscrowCreate`, etc.). Those concrete classes are produced at CMake configure time from `.macro` definition files via Mako templates and Python scripts, but `TransactionBase` itself is intentionally **not** generated — it is maintained by hand and listed in the README as a file that must be updated when new universal transaction fields are added.

## Role in the System

The class solves a specific coupling problem: `STTx` is the protocol's serialization-level representation of a transaction. It stores fields as a dynamic bag accessed via `sfXxx` identifiers, returns values by name at runtime, and throws on missing required fields. That interface is correct and necessary for the protocol engine, but it is fragile for higher-level application code because nothing in the C++ type system prevents accessing a field that doesn't belong to a given transaction type, or forgetting to check whether an optional field is present before reading it.

`TransactionBase` wraps `STTx` behind a typed, read-only facade. The concrete subclass `Payment` inherits from it and adds `getDestination()`, `getAmount()`, etc.; everything in `TransactionBase` is available to every transaction type by virtue of inheritance. This mirrors exactly what `LedgerEntryBase` does for `SLE` (serialized ledger entry) objects in the parallel `xrpl::ledger_entries` namespace.

## Ownership Model

The constructor takes a `std::shared_ptr<STTx const>` by value and moves it into the protected `tx_` member. Wrapping a `const` `STTx` via `shared_ptr` has two consequences: first, there are no setters anywhere in `TransactionBase` or its subclasses — mutation goes through the companion `TransactionBuilderBase<Derived>` and the generated `PaymentBuilder`, `AMMBidBuilder`, etc., which construct a fresh `STTx` and hand a `shared_ptr` to the read wrapper's constructor. Second, the `shared_ptr` allows multiple `TransactionBase`-derived objects to cheaply share ownership of the same underlying transaction without copies, with thread-safe reference counting.

`getSTTx()` returns that same `shared_ptr` as an explicit escape hatch for callers that need to pass the raw transaction to protocol-engine APIs that have not yet been wrapped.

## Field Accessor Pattern

Required fields — `sfAccount`, `sfSequence`, `sfFee`, `sfSigningPubKey` — are exposed with plain value-returning getters that call `tx_->at(sfXxx)` directly. These will throw if the field is somehow absent, but since an `STTx` with a missing required field cannot survive deserialization or pass validation, the throw is treated as a programming error.

Optional fields follow a uniform dual-accessor pattern: `getX()` returns `std::optional<T>` (or `std::nullopt` when absent), and `hasX()` returns `bool`. This lets callers avoid the `std::optional::value()` overhead when they only need to check presence, and avoids unintentional default-construction of values.

Array-typed optional fields — `getMemos()` and `getSigners()` — return `std::optional<std::reference_wrapper<STArray const>>` rather than `std::optional<STArray>`. This is the correct choice because `STArray` is not cheaply copyable; returning a `reference_wrapper` avoids the copy while preserving optional semantics. Callers must be aware that the reference is only valid for the lifetime of the `TransactionBase` object (and by extension, the underlying `STTx`).

## Validation Logic

`validate(std::string& reason)` performs a two-stage check:

1. **Schema conformance** via `protocol_autogen::validateSTObject`, which iterates the `SOTemplate` retrieved from `TxFormats::getInstance()` and verifies that every `soeREQUIRED` field is present and that fields marked `soeMPTNotSupported` do not carry MPT (Multi-Purpose Token) amounts or issues. This check is exclusively a guard against bugs in the code-generation pipeline — valid `STTx` objects constructed through the normal path will always pass, which is why the failure branch is annotated `LCOV_EXCL_START`/`LCOV_EXCL_STOP` to exclude it from coverage metrics.

2. **Local checks** via `passesLocalChecks`, which enforces network-submission rules (signature presence, fee sanity, etc.). This stage is skipped for pseudo-transactions (identified by `isPseudoTx`) because those are injected internally by the consensus process and are never signed or submitted externally.

## Notable Fields

`sfTicketSequence` and its accessor `getTicketSequence()` deserve mention because when a ticket is consumed the regular `sfSequence` is set to `0` — the two concepts are mutually exclusive in practice, which is enforced in `TransactionBuilderBase::setTicketSequence()`.

`sfNetworkID` guards against cross-network replay: a transaction signed for one XRPL sidechain is rejected by nodes running a different network ID. Exposing it through the base class ensures every transaction type can be inspected for network origin without downcasting.

`sfDelegate` is a newer field that supports delegated transaction submission, allowing one account to act on behalf of another under protocol-enforced permission grants.

## Relationship to Generated Classes

Every generated transaction header (e.g., `transactions/Payment.h`) begins with `// This file is auto-generated. Do not edit.`, includes `TransactionBase.h` and `TransactionBuilderBase.h`, and defines exactly two classes: a read wrapper inheriting `TransactionBase` (which adds `getXxx()` accessors for type-specific fields and a `static constexpr TxType txType` discriminator), and a Builder inheriting `TransactionBuilderBase<Derived>`. The Builder's `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, wraps the result in a `shared_ptr<STTx>`, and constructs the read wrapper. This clean separation between mutation (Builder) and observation (`TransactionBase` subclass) is the central architectural commitment of the entire `protocol_autogen` layer.