# `include/xrpl/protocol_autogen/transactions/Batch.h`

## Role and Purpose

This file is part of the auto-generated typed transaction layer for the XRPL C++ implementation. It provides two classes — `Batch` and `BatchBuilder` — that represent the `ttBATCH` transaction type (type code 71), introduced by the `featureBatch` amendment. The Batch transaction allows an account to submit multiple inner transactions atomically in a single on-ledger operation, a capability unique among XRPL transaction types in that no other transaction type may nest a Batch inside itself.

Because this file is auto-generated (the first line explicitly warns against manual edits), it exists as the per-transaction-type output of a code generation step that produces one typed wrapper pair for every transaction kind defined in the XRPL protocol. This generation strategy avoids the error-prone alternative of hand-writing dozens of type-erased field accesses spread across the codebase.

## Class Design: Immutable Wrapper + Separate Builder

The design follows a two-class pattern shared by every transaction type in `protocol_autogen/transactions/`:

`Batch` is a read-only view. It holds a `std::shared_ptr<STTx const>` inherited from `TransactionBase`, giving it immutable, reference-counted access to the serialised transaction object. All field getters are `[[nodiscard]] const` and operate directly on the underlying `STTx`. The constructor validates the transaction type immediately and throws `std::runtime_error` on mismatch, so a successfully constructed `Batch` is always a genuine `ttBATCH` transaction.

`BatchBuilder` is the mutable counterpart. It inherits from the CRTP base `TransactionBuilderBase<BatchBuilder>`, which provides chainable setters for all common transaction fields (`setAccount`, `setFee`, `setSequence`, `setFlags`, etc.) and the `sign()` helper that serialises the transaction body, prefixes it with `HashPrefix::txSign`, and writes the resulting signature into `sfTxnSignature`. The CRTP pattern ensures every inherited setter returns `BatchBuilder&` rather than the base type, preserving fluent chaining without virtual dispatch. The builder stores an `STObject` named `object_` (initialised to `sfTransaction`), and intentionally avoids calling `object_.set(soTemplate)` — this is a deliberate defensive choice noted in `TransactionBuilderBase`: pre-setting template fields would create `soeDEFAULT` placeholders that cause `applyTemplate()` to throw when the `STTx` constructor later processes them.

The terminal `BatchBuilder::build()` method signs the transaction, wraps `object_` in a `std::make_shared<STTx>`, and returns a `Batch` instance. Ownership transfers cleanly: the builder's `STObject` is moved into the shared `STTx`, after which the builder is logically consumed.

## Batch-Specific Fields

`Batch` exposes two fields beyond the common transaction fields inherited from `TransactionBase`:

- **`sfRawTransactions`** (`soeREQUIRED`): An `STArray` of inner transactions. Each element is itself a complete serialised transaction object encoded as an `STObject`. The transactor in `Batch.cpp` iterates this array, reconstructs each inner `STTx`, validates it is not itself a `ttBATCH` (nesting is explicitly prohibited), and accumulates base fees. The accessor `getRawTransactions()` returns a `const` reference directly; there is no optional wrapper because the field is required.

- **`sfBatchSigners`** (`soeOPTIONAL`): An `STArray` carrying additional signers who have authorised the batch. This field is marked `notSigning` in the sfields macro (`SField::notSigning`), which means it is excluded from the transaction signing hash. This is significant: it allows `sfBatchSigners` to be appended after the primary signature is computed, supporting multi-party batch authorisation flows where the original submitter cannot know all batch signers in advance.

For the optional field, `getBatchSigners()` returns `std::optional<std::reference_wrapper<STArray const>>` — the reference wrapper avoids copying the potentially large array while the `optional` correctly models absence. The companion `hasBatchSigners()` predicate avoids the need to dereference and inspect the optional in boolean contexts.

## Delegation and Amendment Constraints

The class comment documents `Delegation::notDelegable`, meaning a `Batch` transaction cannot be submitted on behalf of another account via the delegation mechanism. This is architecturally consistent: a Batch wraps multiple inner transactions each of which may have their own delegation rules, so permitting batch-level delegation would create ambiguous authority chains. The `featureBatch` amendment guard means the entire transaction type is unavailable until that amendment activates on the network.

## Relationship to the Broader Autogen Layer

Every file in `protocol_autogen/transactions/` follows the identical `Foo` / `FooBuilder` pattern. The common infrastructure in `TransactionBase` and `TransactionBuilderBase<Derived>` supplies all shared fields and the signing logic, so individual transaction files are thin — typically under 170 lines — and only add the fields unique to their type. The auto-generation approach ensures field names, optionality semantics, and return types remain consistent as the protocol evolves, and the round-trip test in `BatchTests.cpp` validates that every setter maps to a correct getter after `build()` and `validate()`.