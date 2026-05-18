# `EnableAmendment.h` — Auto-generated Pseudo-Transaction Wrapper

## Role in the System

`EnableAmendment.h` is part of the auto-generated `xrpl::transactions` layer that provides type-safe C++ wrappers over the raw `STTx` serialized transaction format. It covers transaction type `ttAMENDMENT` (numeric code 100), the pseudo-transaction injected by the XRPL ledger's amendment voting machinery when a protocol change achieves validator supermajority and becomes active.

This is not a transaction that any regular account submits to the network. The file header comment captures this with `Amendment: uint256{}` (the zero hash, meaning this transaction type is not itself guarded by any feature flag) and `Delegation::notDelegable`. The pseudo-transaction is inserted into a validated ledger by the consensus engine, which records which amendment was enabled (`sfAmendment`) and at which ledger (`sfLedgerSequence`).

The `// This file is auto-generated. Do not edit.` banner on line 1 signals that the real source of truth is the `TRANSACTION(ttAMENDMENT, ...)` macro entry in `include/xrpl/protocol/detail/transactions.macro`. That macro expands through code generation tooling to produce this header; changes to the transaction's field list must go through the macro, not this file.

## Class Design: Wrapper + Builder Pair

The file exports two classes that follow the same paired pattern as every other transaction in the `protocol_autogen` directory:

**`EnableAmendment`** is an immutable read-only wrapper around a `shared_ptr<STTx const>`. It inherits all common transaction field accessors (`getAccount()`, `getFee()`, `getSequence()`, etc.) from `TransactionBase`, then adds two transaction-specific getters:

- `getLedgerSequence()` → `SF_UINT32::type::value_type`: returns `sfLedgerSequence`, the ledger index at which the amendment became effective.
- `getAmendment()` → `SF_UINT256::type::value_type`: returns `sfAmendment`, the 256-bit hash identifying the amendment, which corresponds to the SHA-512Half of the amendment's feature string.

Both fields are `soeREQUIRED` per the macro definition, so `tx_->at(field)` is safe — there is no optional-return variant here. The constructor validates the transaction type at runtime with `tx_->getTxnType() != txType` and throws `std::runtime_error` on mismatch. This is a deliberate fail-fast approach: wrapping the wrong `STTx` in a typed view is a programming error, not a recoverable condition.

**`EnableAmendmentBuilder`** inherits from `TransactionBuilderBase<EnableAmendmentBuilder>` via CRTP. The template enables the base class's fluent setters (e.g., `setFee()`, `setNetworkID()`) to return `Derived&` — that is, `EnableAmendmentBuilder&` — so calls chain without casts. The constructor accepts `account`, `ledgerSequence`, and `amendment` as required positional arguments, plus optional `sequence` and `fee`. Internally it builds on a mutable `STObject object_{sfTransaction}` rather than an `STTx`, deliberately deferring template application so that `soeDEFAULT` fields never get explicit placeholder entries that would cause `applyTemplate()` to throw "may not be explicitly set to default" during `STTx` construction — an important subtlety documented in `TransactionBuilderBase`.

The second builder constructor accepting a `shared_ptr<STTx const>` allows round-tripping: load an existing serialized pseudo-transaction, copy its fields into the mutable `STObject`, modify if needed, and rebuild. This path also guards the type with an explicit check.

`build()` calls the inherited `sign()` method, which serializes `HashPrefix::txSign || object_` without signing fields, computes an `secp256k1` signature, and sets both `sfSigningPubKey` and `sfTxnSignature`. For real pseudo-transactions produced by the consensus engine, these fields are empty (zero-length blobs) — the validate path in `TransactionBase::validate()` handles this correctly by detecting pseudo-transactions via `isPseudoTx(*tx_)` and returning `true` early, bypassing `passesLocalChecks()` which would otherwise reject an unsigned transaction.

## Validation Shortcircuit for Pseudo-Transactions

The `Change.cpp` transactor that processes `ttAMENDMENT` (and `ttFEE`) enforces unusually strict invariants: the source account must be `beast::zero`, the fee must be zero XRP, and the transaction must have no signature at all. These are the exact opposite of normal transaction requirements. The autogen layer's `validate()` correctly skips `passesLocalChecks()` for these transactions — the check `if (isPseudoTx(*tx_)) return true;` in `TransactionBase` is what makes the test suite's `EXPECT_TRUE(tx.validate(reason))` pass even when testing with a signing key pair.

## Relationship to Other Files

`EnableAmendment.h` sits in a directory of roughly 70 sibling auto-generated files — one per transaction type. All share the same inheritance hierarchy (`TransactionBase` / `TransactionBuilderBase<Derived>`) and the same construction discipline. The `transactions.macro` file is the single authoritative definition: the field names, their optionality (`soeREQUIRED`/`soeOPTIONAL`/`soeDEFAULT`), and the delegation policy all flow from that one `TRANSACTION(...)` macro call into every generated artifact.

The corresponding test file `EnableAmendmentTests.cpp` validates the full round-trip: build from scratch, verify fields; reconstruct from `STTx`, rebuild, verify fields again; confirm that constructing either class from the wrong transaction type throws.