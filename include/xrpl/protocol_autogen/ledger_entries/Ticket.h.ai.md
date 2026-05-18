# `include/xrpl/protocol_autogen/ledger_entries/Ticket.h`

## Role and Context

This auto-generated header defines the `Ticket` ledger entry type (`ltTICKET`, type code `0x0054`) and its companion builder for the XRP Ledger's typed access layer. A Ticket is a reserved sequence number: when an account issues a `TicketCreate` transaction, the ledger writes one `ltTICKET` object per reserved slot, each holding a specific sequence number that a future transaction may consume in lieu of the account's current sequence counter. This mechanism enables out-of-order transaction submission, which is useful for multi-party workflows where signers may apply transactions in any order without invalidating other pending ones.

The file is entirely auto-generated — the comment at line 1 makes that explicit — and follows the same structural template as every other entry under `protocol_autogen/ledger_entries/`. Changing it by hand would be overwritten by the code-generation step.

## `Ticket` — The Immutable Wrapper

`Ticket` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` and exposes getters for the universal fields (`sfLedgerEntryType`, `sfFlags`, `sfLedgerIndex`) as well as `validate()` and `getSle()`. The subclass extends that with five required field accessors specific to `ltTICKET`:

- `getAccount()` — the `AccountID` of the owning account.
- `getOwnerNode()` — a 64-bit page index into that account's owner directory, needed when the ledger must locate and remove this entry without a full directory scan.
- `getTicketSequence()` — the reserved sequence number this ticket represents.
- `getPreviousTxnID()` / `getPreviousTxnLgrSeq()` — the standard provenance pair recording which transaction last modified this object and in which ledger it appeared.

All five are `[[nodiscard]]` to make silent value drops a compile-time warning rather than a runtime mystery.

The constructor takes a `std::shared_ptr<SLE const>` and immediately checks `sle_->getType() != entryType`, throwing `std::runtime_error` if the underlying object is not actually an `ltTICKET`. This guard prevents subtle bugs where a caller retrieves the wrong entry type from the ledger view and silently reads garbage field values through the strongly-typed accessors.

## `TicketBuilder` — Fluent Construction via CRTP

`TicketBuilder` inherits from `LedgerEntryBuilderBase<TicketBuilder>`, a CRTP base whose common setters (`setLedgerIndex`, `setFlags`) return `Derived&` rather than `LedgerEntryBuilderBase&`. This lets callers chain entry-specific setters with universal ones without breaking the fluent interface through upcasting.

The builder holds an `STObject object_{sfLedgerEntry}` rather than an `SLE`. The base class intentionally avoids calling `object_.set(soTemplate)` at construction time because doing so would install `STBase` placeholder objects for `soeDEFAULT` fields — placeholders that would later cause `applyTemplate()` inside the `SLE` constructor to throw "may not be explicitly set to default." Keeping `object_` as a free `STObject` sidesteps this trap; the `SLE` constructor handles template application correctly when `build()` materialises it.

`build(uint256 const& index)` is the terminal step: it moves the assembled `STObject` into a new `SLE` keyed at `index`, wraps it in a `shared_ptr<SLE const>`, and passes it to `Ticket`'s constructor. The result is a fully validated, immutable `Ticket` value.

The secondary constructor `TicketBuilder(std::shared_ptr<SLE const> sle)` is a "copy-from-existing" path. It verifies the entry type via `sle->at(sfLedgerEntryType) != ltTICKET`, then copies the dereferenced `SLE` into `object_`. This allows code to round-trip through the builder — for example, to read an existing ticket, apply a field update, and produce a modified entry — without touching the raw `SLE` API.

The setter parameter type `std::decay_t<typename SF_XXX::type::value_type> const&` ensures that reference qualifiers and cv-qualifiers from the SField's declared value type are stripped before the `const&` is applied. This prevents double-reference issues for field types that are themselves aliases for reference-qualified types.

## Relationship to `TicketCreate`

In `TicketCreate::doApply()` (in `src/libxrpl/tx/transactors/system/TicketCreate.cpp`), tickets are created using the raw `SLE` API rather than `TicketBuilder`. The transactor loop writes `sfAccount`, `sfOwnerNode`, and `sfTicketSequence` directly onto freshly allocated `SLE` objects, inserts each into the ledger view, and inserts it into the account's owner directory. `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` are stamped later by the transaction application machinery on all modified entries. `TicketBuilder` is therefore primarily a consumer-facing construction tool — used in tests and higher-level protocol code — while the transactor layer works directly with the underlying serialisation primitives for performance.

## Error Handling and Invariants

Both `Ticket` and `TicketBuilder` enforce the same type-identity invariant on construction: an entry whose type field doesn't match `ltTICKET` is an immediate `std::runtime_error`. The test suite in `TicketTests.cpp` verifies this in two dedicated test cases (`WrapperThrowsOnWrongEntryType`, `BuilderThrowsOnWrongEntryType`) by constructing a `Check` entry and confirming that attempting to wrap it as either a `Ticket` or a `TicketBuilder` throws. The round-trip tests additionally call `validate()` on both the builder's in-progress `STObject` and the final `Ticket` wrapper to confirm that the `LedgerFormats` template for `ltTICKET` is fully satisfied before and after `build()`.