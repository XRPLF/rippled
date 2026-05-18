# `EscrowFinish.h` — Auto-Generated EscrowFinish Transaction Wrapper

This file is auto-generated and sits within the `xrpl/protocol_autogen/transactions/` layer, which provides type-safe C++ wrappers over the raw `STTx` serialization type for every XRPL transaction kind. `EscrowFinish.h` covers `ttESCROW_FINISH` (type code 2), the third of the three escrow lifecycle transactions alongside `EscrowCreate` (type 1) and `EscrowCancel` (type 4).

## Role in the Escrow Lifecycle

An escrow on XRPL is a conditional payment held in ledger state. `EscrowCreate` locks funds; `EscrowFinish` releases them to the destination once the release conditions are satisfied; `EscrowCancel` returns them to the sender after a deadline. `EscrowFinish` is the most field-rich of the three because it must carry proof that the release conditions have been met — either a time condition (verified by the ledger) or a PREIMAGE-SHA-256 crypto-condition (verified by presenting a cryptographic fulfillment).

Comparing the three escrow headers makes the differences clear:
- `EscrowCancel` has only `sfOwner` and `sfOfferSequence` as required fields — no optional fields at all.
- `EscrowCreate` has `sfDestination` and `sfAmount` as required, plus optional time/condition locks.
- `EscrowFinish` has `sfOwner` and `sfOfferSequence` as required, plus optional `sfFulfillment`, `sfCondition`, and `sfCredentialIDs`.

## The Two-Class Pattern

The file defines two classes in `namespace xrpl::transactions`:

**`EscrowFinish`** is an immutable read-only wrapper. It holds a `std::shared_ptr<STTx const>` — the `const` qualifier on the pointed-to type prevents any mutation through this class, making concurrent reads safe without any locking. The constructor eagerly validates the transaction type and throws `std::runtime_error` if a non-`ttESCROW_FINISH` `STTx` is passed in. This is a deliberate defensive check: the `STTx` type system does not enforce transaction kinds at compile time, so the runtime guard at construction prevents silent misuse of a wrapper with the wrong underlying data.

**`EscrowFinishBuilder`** is the mutable construction side. It extends `TransactionBuilderBase<EscrowFinishBuilder>` using CRTP. The CRTP choice matters for ergonomics: all inherited setters in `TransactionBuilderBase` return `Derived&` (resolved at compile time to `EscrowFinishBuilder&`) rather than a base-class reference, so method chains like `.setLastLedgerSequence(n).setFulfillment(blob).build(pub, sec)` compile without casts or intermediate variables.

## Field Design

The two required fields — `sfOwner` and `sfOfferSequence` — together identify the escrow object in the ledger. `sfOwner` is the account that created the escrow (not necessarily the finisher), and `sfOfferSequence` is the sequence number of the original `EscrowCreate` transaction, acting as a unique key for the escrow object within that owner's namespace. Both are mandatory in the builder constructor, preventing construction of a structurally invalid transaction.

The three optional fields tell a richer story:

- **`sfFulfillment`** (`SF_VL`, a variable-length blob): the PREIMAGE-SHA-256 preimage from the Crypto-Conditions RFC. It is only required when the escrow was created with a `sfCondition` lock. The ledger hashes the fulfillment and checks it against the stored condition.
- **`sfCondition`** (`SF_VL`): the SHA-256 condition that must be satisfied. Echoing the condition in `EscrowFinish` allows the ledger to verify the fulfillment without having to retrieve and decode the escrow's stored condition separately from the STTx itself during validation — though in practice the ledger does both.
- **`sfCredentialIDs`** (`SF_VECTOR256`, a vector of `uint256`): a newer field reflecting XRPL's deposit-preauth credential system. When the destination account requires credential-based authorization (via `DepositPreauth` with credential requirements), the finisher can supply credential object IDs proving they meet those requirements.

Each optional field follows a paired accessor pattern: `hasFulfillment()` returns `bool` for a cheap presence check, while `getFulfillment()` returns `protocol_autogen::Optional<SF_VL::type::value_type>` (an alias for `std::optional`) and returns `std::nullopt` if the field is absent. This avoids callers having to catch exceptions from `STTx::at()` on missing fields.

All getter methods are annotated `[[nodiscard]]`, enforcing that callers actually consume the return value rather than accidentally discarding it.

## Builder Construction Modes

`EscrowFinishBuilder` offers two construction paths:

1. **From scratch**: the primary constructor takes `account`, `owner`, and `offerSequence` as required values, then passes the transaction type and account on to `TransactionBuilderBase`. Optional fields are added via method chaining before calling `build()`.

2. **From an existing `STTx`**: the secondary constructor copies the underlying `STObject` from a fully-formed transaction (`object_ = *tx`). This enables re-signing an already-serialized `EscrowFinish` — for example when bridging or reprocessing transactions — without re-parsing JSON.

The `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes the `STObject` with the `HashPrefix::txSign` prefix, signs it with the provided key pair, then wraps the resulting `STTx` in an `EscrowFinish` wrapper. At this point the builder's `STObject` is `std::move`d into the `STTx`, so the builder should not be used after calling `build()`.

## Relationship to the Broader Autogen Layer

This file follows exactly the same structural template as every other header in `protocol_autogen/transactions/`. The "Do not edit" banner signals that the source of truth is a code-generation tool, not this file — changes belong upstream. The `TransactionBase` base class (read in `TransactionBase.h`) provides getters for all common XRPL transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfMemos`, `sfSigners`, `sfDelegate`, and more), so `EscrowFinish` inherits a full read interface without any boilerplate. The `validate()` method on `TransactionBase` runs schema validation via `TxFormats::getInstance()` and local ledger checks, and is available on all wrappers including `EscrowFinish`.