# `DIDDelete.h` — Auto-Generated DIDDelete Transaction Wrapper

## Role in the System

`DIDDelete.h` belongs to the `xrpl/protocol_autogen/transactions/` layer — a code-generated tier of the XRPL C++ codebase that provides strongly-typed, transaction-specific wrappers over the underlying `STTx` serialized-type infrastructure. This file defines the machinery for transaction type `ttDID_DELETE` (type code 50), the operation that removes a Decentralized Identifier (DID) object from an XRPL account. DID support is gated behind the `featureDID` amendment.

The file is stamped `// This file is auto-generated. Do not edit.`, meaning it is produced by a code-generation pipeline rather than handwritten. Its structure mirrors every other file in the same directory — an immutable read accessor class paired with a fluent builder class — making the whole family of transaction types mechanically consistent.

## Two-Class Pattern: Wrapper and Builder

The file cleanly separates the two usage modes for a transaction:

**`DIDDelete`** is a read-only wrapper. It takes ownership of a `std::shared_ptr<STTx const>` and exposes typed accessors via its `TransactionBase` parent. The `const` qualifier on `STTx` is intentional and enforced: once wrapped, the transaction is immutable. The constructor performs an immediate type-check — if the caller somehow wraps an `STTx` of the wrong type, a `std::runtime_error` is thrown right there at construction rather than silently returning garbage from a field lookup later. This is a boundary-enforcement pattern that trades fail-fast behavior for the lack of compile-time guarantees that naturally arise from working with a generic serialized-type store.

**`DIDDeleteBuilder`** is a mutable, CRTP-based builder inheriting from `TransactionBuilderBase<DIDDeleteBuilder>`. It accumulates fields into a live `STObject object_` and, when ready, calls `build()`, which signs the payload and promotes the mutable object into an immutable `DIDDelete` by constructing a new `STTx`.

This separation is deliberately asymmetric: the builder is mutable and makes no validity guarantees; the wrapper is immutable and trusted to represent a consistent, signed transaction.

## Why `DIDDelete` Has No Transaction-Specific Fields

Comparing this file with its sibling `DIDSet.h` (type 49) reveals a meaningful difference. `DIDSet` carries three optional fields — `sfDIDDocument`, `sfURI`, and `sfData` — corresponding to the content of the DID being established or updated. `DIDDelete` carries none. Deletion semantics on the XRPL require only the account identity (from `sfAccount`, inherited from `TransactionBase`) to identify which DID ledger object to remove; no payload data is needed. The generator therefore emits an empty `// Transaction-specific field getters` comment block and an equally empty `/** @brief Transaction-specific field setters */` comment on the builder, reflecting the schema faithfully rather than adding any scaffolding.

## Builder Construction and Signing Flow

`DIDDeleteBuilder` offers two construction paths. The primary path accepts an `AccountID`, optional sequence, and optional fee, immediately recording `ttDID_DELETE` as the `sfTransactionType` in the underlying `STObject`. The secondary path accepts an existing `STTx const` and copies it into `object_`, after type-checking, allowing a pre-existing transaction to be "reopened" for re-signing or parameter adjustment. This pattern is useful in test harnesses or situations where a transaction template must be cloned and modified.

The `build()` method calls `sign()` from `TransactionBuilderBase`, which serializes the `STObject` under `HashPrefix::txSign`, computes the signature, stamps `sfSigningPubKey` and `sfTxnSignature`, then constructs an `STTx` via `std::move(object_)`. After `build()`, the builder's internal `object_` has been moved away, making the builder instance unusable — this is a one-shot design consistent with move semantics, and callers should not reuse the builder after calling `build()`.

A subtle but important detail in `TransactionBuilderBase`'s constructor: it deliberately does not call `object_.set(soTemplate)`. Calling `applyTemplate()` prematurely would create `STBase` placeholder values for `soeDEFAULT` fields, which in turn causes the `STTx` constructor to throw "may not be explicitly set to default". By leaving `object_` as a free object, the template is applied correctly during `STTx` construction.

## Delegation and Amendment Context

The class comment documents `Delegation::delegable`, meaning a `DIDDelete` transaction can be submitted on behalf of an account by a delegate (via `sfDelegate`), which is why `TransactionBase` exposes `getDelegate()` and `hasDelegate()`. This is not specific to `DIDDelete` but is worth noting because not all transaction types are delegable. The `featureDID` amendment context means that attempting to submit this transaction type on a network without the amendment enabled will be rejected at the consensus layer; the C++ layer itself does not enforce amendment activation.

## Relationship to Siblings

`DIDDelete.h` and `DIDSet.h` together form the complete DID transaction family on the XRPL. `DIDSet` creates or updates a DID ledger object with document data, URI references, and raw byte payloads; `DIDDelete` tears that object down. The two files share an identical structural skeleton — same base classes, same constructor logic, same builder-to-wrapper `build()` flow — differing only in type code, field presence, and class name. This regularity is the direct consequence of code generation and makes the autogenerated layer predictable to read and easy to extend when new transaction types are added.