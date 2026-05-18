# `PayChannel.h` — Auto-generated PayChannel Ledger Entry Wrapper

`PayChannel.h` is a machine-generated header (do not edit manually) that provides the two-class pattern used throughout the `protocol_autogen` layer of the XRPL codebase: an immutable read-only accessor (`PayChannel`) and a fluent construction helper (`PayChannelBuilder`). Together they wrap the raw `SLE` (Serialized Ledger Entry) representation of a payment channel with type-safe, name-safe field access, living under `ltPAYCHAN` (discriminator `0x0078`).

## What a PayChannel Ledger Entry Represents

In the XRP Ledger, a payment channel allows a source account to pre-allocate XRP for a series of off-chain micropayments to a destination. The source signs individual claim messages off-chain; the destination can cash the highest-value one at any time, or the channel can be closed after a settlement delay elapses. The on-ledger entry holds the allocated `sfAmount`, the already-redeemed `sfBalance`, the cryptographic `sfPublicKey` used to verify off-chain claim signatures, and the `sfSettleDelay` (in seconds) that governs how long the source must wait before unilaterally closing the channel.

## Class `PayChannel`

`PayChannel` extends `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` as `sle_`. The `const`-ness is structural: the shared pointer's pointee is immutable, so no field setter exists on the wrapper — all mutations go through `PayChannelBuilder`.

The constructor performs a single defensive check against `sle_->getType() != ltPAYCHAN` and throws `std::runtime_error` on mismatch. This is the only runtime guard; all other correctness guarantees come from the type system.

Required fields (`soeREQUIRED`) are exposed via plain getters that forward directly to `sle_->at(sfXxx)`: `getAccount()`, `getDestination()`, `getAmount()`, `getBalance()`, `getPublicKey()`, `getSettleDelay()`, `getOwnerNode()`, `getPreviousTxnID()`, and `getPreviousTxnLgrSeq()`. These will assert or throw internally if the field is somehow absent, consistent with the ledger's own invariant that required fields must always be present on a well-formed entry.

Optional fields (`soeOPTIONAL`) follow a paired `has*()`/`get*()` pattern and return `protocol_autogen::Optional<T>`. That alias, defined in `Utils.h`, is a conditional type: for reference-typed `T` it becomes `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>`, and for value types it collapses to plain `std::optional<T>`. This ensures reference-typed SField results (which normally can't be stored in `std::optional` directly) are handled correctly. The optional fields are: `sfSequence` (channel-creating transaction's sequence number, used for deduplication), `sfExpiration` (mutable soft expiry adjustable by either party), `sfCancelAfter` (immutable hard deadline set once at creation), `sfSourceTag`, `sfDestinationTag`, and `sfDestinationNode` (the destination's owner-directory back-pointer, absent on channels created before that field was added).

The distinction between `sfExpiration` and `sfCancelAfter` is semantically significant. `sfCancelAfter` is the absolute hard limit set at channel creation and is never updated. `sfExpiration` is a softer limit that either party can advance forward (never back) as a way to coordinate channel closure. Both are represented as 32-bit XRP Ledger time values (seconds since the Ripple epoch).

`sfOwnerNode` and `sfDestinationNode` are 64-bit indices into the owner-directory B-tree pages for the source and destination accounts respectively. They are present to allow O(1) deletion from the directory when the channel is closed. `sfDestinationNode` is optional because it was introduced after the payment channel feature and may be absent on older entries.

## Class `PayChannelBuilder`

`PayChannelBuilder` is a CRTP builder inheriting from `LedgerEntryBuilderBase<PayChannelBuilder>`. The base maintains an `STObject object_{sfLedgerEntry}` that accumulates field assignments, and it deliberately avoids calling `object_.set(soTemplate)` — keeping the object "free" — so that default-value fields are not pre-populated, which would conflict with `SLE`'s own `applyTemplate()` call in its constructor.

The primary constructor accepts all nine required fields as arguments, setting them immediately via the corresponding `set*()` calls. This design enforces, at compile time, that required fields cannot be omitted: there is no default constructor, and the optional fields are only reachable via chained `set*()` calls after construction. Each setter returns `PayChannelBuilder&`, enabling fluent chaining.

A secondary constructor takes an `std::shared_ptr<SLE const>` and copies the SLE's field state into `object_` via `object_ = *sle`. This supports the pattern of "load an existing channel entry, modify it, and rebuild" — relevant when processing `PaymentChannelClaim` or `PaymentChannelFund` transactions that update `sfBalance` or `sfExpiration`.

`build(uint256 const& index)` moves `object_` into a freshly constructed `SLE` keyed at `index`, then wraps it in a `PayChannel`. The move means the builder is consumed after a single `build()` call; reusing it after that would access a moved-from `STObject`.

## Relationship to Surrounding Infrastructure

`PayChannel.h` is one of roughly 30 auto-generated files in `ledger_entries/`, each following an identical structural pattern. The code generator distinguishes required vs optional fields and emits the `Optional<T>` wrapper only where needed. The common behaviors — `validate()`, `getKey()`, `getType()`, `getFlags()`, `getLedgerIndex()` — are handled entirely in `LedgerEntryBase` and `LedgerEntryBuilderBase`, keeping the per-entry files focused solely on the fields that differentiate each ledger object type.