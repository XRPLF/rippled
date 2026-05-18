# `SeqProxy.h` — Unified Sequence/Ticket Identifier

## Role in the System

`SeqProxy` is a small discriminated-value type — a type-tagged `uint32_t` — that represents either a traditional account **sequence number** or a **ticket sequence number**. It lives in `include/xrpl/protocol/SeqProxy.h` and has no dependencies beyond `<cstdint>` and `<ostream>`, making it a foundational primitive that the rest of the protocol layer can include cheaply.

The class was introduced when XRPL added the Tickets feature. Before tickets, every XRPL transaction consumed exactly one account sequence number in order, so a plain `uint32_t` sufficed as a transaction identifier. Tickets allow an account to pre-reserve sequence slots and use them out-of-order, which creates a second namespace of transaction identifiers. Rather than scattering `bool isTicket` flags throughout every piece of code that tracks transaction identity, `SeqProxy` encapsulates the choice in one place.

## Safety of the Shared Numeric Space

At first glance it seems dangerous to use `SeqProxy::value()` as a bare number for ledger-object keys (offers, checks, escrows, payment channels all use it this way). The class comment explains the two-part invariant that makes it safe:

1. A `TicketCreate` transactor always creates tickets whose numeric values fall within the range that the account's root sequence has already advanced past — so a ticket value can never match any sequence value that will be used in the future for that account.
2. When a batch of tickets is created, the account root's sequence is advanced to one past the highest ticket number in the batch. This means every ticket in the batch has a unique value that is permanently retired from the sequence namespace.

Together these guarantee that the numeric values of ticket proxies and sequence proxies for a given account never collide, even when stored without type metadata.

## Design Decisions

**`constexpr` throughout.** All constructors, accessors, and comparison operators are `constexpr`. The type is trivially copyable and small enough to pass by value everywhere. There is no indirection, no heap allocation, and no virtual dispatch.

**Factory function only for the common case.** `SeqProxy::sequence(v)` is a named static factory for the `seq` type, but tickets are constructed directly with `SeqProxy{SeqProxy::ticket, value}`. This asymmetry reflects usage patterns: normal transaction processing almost always starts with a sequence number; ticket construction is an uncommon, explicit act.

**`advanceBy()` instead of `operator+`.** The only mutating operation on a `SeqProxy` is `advanceBy(amount)`, which increments `value_` in place and returns `*this`. The deliberate choice of a named method over `operator+` or `operator+=` makes accidental arithmetic visible — you have to consciously invoke it. It is currently used only in tests to increment a `SeqProxy` across a sequence of dummy transactions.

**Sorting: sequences always before tickets.** The `operator<` implementation first compares the `Type` tag. Since `seq == 0` and `ticket == 1`, all sequence-typed proxies sort strictly before all ticket-typed proxies, regardless of numeric value. The comment explicitly calls out the benefit: in `CanonicalTXSet`, transactions from the same account are sorted by their `SeqProxy`. This means `TicketCreate` transactions (which carry a sequence number) always precede the ticket-consuming transactions they enable, preventing ordering inversions during consensus replay.

## Key Relationships

**`STTx::getSeqProxy()`** (`src/libxrpl/protocol/STTx.cpp`) is the primary construction site in production code. It reads the raw transaction's `sfSequence` field; if non-zero that becomes a `SeqProxy::sequence`. If zero, it checks for `sfTicketSequence` and returns a ticket-typed proxy. The fallback to `SeqProxy::sequence(0)` for transactions with neither field set preserves backward compatibility.

**`CanonicalTXSet`** (`include/xrpl/ledger/CanonicalTXSet.h`) stores a `SeqProxy` inside its internal `Key` struct. The set provides the ordered, per-account transaction queue used during consensus to apply deferred transactions in canonical order. The `SeqProxy` ordering guarantee is what makes that ordering correct for ticket-bearing transaction sets.

**`Indexes.h`** forward-declares `SeqProxy` and uses it in `getTicketIndex()` to compute the ledger object key for a ticket entry. Passing the full `SeqProxy` (rather than a bare `uint32_t`) keeps the type-check in the call path and makes the interface self-documenting.

## Summary

`SeqProxy` is an intentionally minimal abstraction — just 5 bytes of data and a handful of `constexpr` methods — that eliminates an entire class of bugs that would arise from mixing sequence numbers and ticket numbers as bare integers. Its asymmetric sort order (sequences before tickets) is a deliberate, documented protocol-level contract that the consensus ordering machinery depends on.