# `TicketCreate.h` — Auto-generated TicketCreate Transaction Wrapper

## Role in the System

This file is one of roughly 70 auto-generated transaction type headers in `include/xrpl/protocol_autogen/transactions/`. Each file follows an identical structural pattern: a read-only `Transaction` class paired with a fluent `TransactionBuilder` class. Together they form a type-safe C++ surface over XRPL's dynamic `STTx` object model, eliminating the need for callers to manipulate raw serialized fields by name.

`TicketCreate` covers transaction type `ttTICKET_CREATE` (numeric type 10). In the XRPL protocol, this transaction reserves one or more sequence number *tickets* — pre-allocated placeholders that allow future transactions to be submitted out of order without gaps in the account's sequence space. Once created, a ticket can be consumed by any subsequent transaction in place of a normal sequence number, enabling use cases like parallel transaction submission and multi-party coordination.

## Class: `TicketCreate`

`TicketCreate` is an immutable wrapper around a `std::shared_ptr<STTx const>`. It inherits from `TransactionBase`, which already exposes getters for all fields common to every transaction type: `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getMemos()`, `getSigners()`, `getLastLedgerSequence()`, `getDelegate()`, and more. Optional fields return `std::optional<T>` and are paired with `hasX()` predicates; required fields are returned by value directly.

The only `TicketCreate`-specific accessor is `getTicketCount()`, which returns a `SF_UINT32::type::value_type`. This field is `soeREQUIRED` in the XRPL schema — it specifies the number of tickets to create (valid range 1–250 per protocol rules, though that enforcement lives in the ledger application layer, not here).

Type safety is enforced at construction: the constructor verifies `tx_->getTxnType() == ttTICKET_CREATE` and throws `std::runtime_error` on mismatch. This guard prevents accidental wrapping of an unrelated `STTx` in a strongly-typed `TicketCreate` handle.

## Class: `TicketCreateBuilder`

`TicketCreateBuilder` inherits from `TransactionBuilderBase<TicketCreateBuilder>` using the Curiously Recurring Template Pattern (CRTP). The template base returns `Derived&` from every setter, making method chaining work without virtual dispatch or downcasting overhead. The mutable `STObject object_` lives in the base and is built up incrementally before being finalized into an immutable `STTx`.

A deliberate design choice in `TransactionBuilderBase` is that the internal `object_` is never initialized from an `SOTemplate`. As the comment in `TransactionBuilderBase` explains, calling `set(soTemplate)` would create `STBase` placeholders for `soeDEFAULT` fields, causing `applyTemplate()` to throw "may not be explicitly set to default" when the `STTx` constructor runs. Instead, only fields that are actually set by the caller are populated; the `STTx` constructor then calls `applyTemplate()` to fill defaults and validate structure.

The builder has two construction paths:
- **Primary constructor**: takes `account` and the required `ticketCount`, plus optional `sequence` and `fee`. This is the standard path for creating a new transaction from scratch.
- **STTx copy constructor**: wraps an existing `std::shared_ptr<STTx const>` for round-trip editing (read an existing transaction, modify, re-sign). It performs the same type-check as the `TicketCreate` wrapper.

`setTicketCount()` assigns directly to `object_[sfTicketCount]` and returns `*this` (as `TicketCreateBuilder&`) to participate in the fluent chain.

`build()` calls the protected `sign()` helper inherited from `TransactionBuilderBase`, which serializes the object without signing fields, prepends the `HashPrefix::txSign` prefix, computes the ECDSA/EdDSA signature using the provided key pair, and stores both `sfSigningPubKey` and `sfTxnSignature` back into `object_`. It then moves `object_` into a freshly allocated `STTx` and wraps it in a `TicketCreate` by value. Because `object_` is `std::move`d, the builder is consumed and must not be reused after `build()`.

## Relationship to the Autogen Layer

The file is explicitly marked `// This file is auto-generated. Do not edit.` The autogen framework generates one such file per transaction type defined in the XRPL protocol specification. The pattern keeps each file minimal: `TicketCreate.h` contains only the single field that distinguishes `TicketCreate` from the common transaction base. All structural decisions — immutability of the wrapper, CRTP on the builder, the signing flow, schema validation via `passesLocalChecks` — live in `TransactionBase.h` and `TransactionBuilderBase.h` and are shared uniformly across every transaction type.

The `Delegation::delegable` annotation in the class comment indicates that `TicketCreate` participates in the XRPL delegate account feature, meaning a delegated signer can submit this transaction type on behalf of the originating account, subject to a matching `DelegateSet` permission grant.