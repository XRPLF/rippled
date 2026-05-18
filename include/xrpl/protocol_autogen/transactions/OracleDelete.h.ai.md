# OracleDelete.h — Auto-Generated Transaction Wrapper for `ttORACLE_DELETE`

This file is part of the `xrpl/protocol_autogen` layer, a code-generated collection of strongly-typed C++ wrappers for every XRPL transaction type. It defines two classes — `OracleDelete` and `OracleDeleteBuilder` — that together provide a safe, ergonomic API for consuming and constructing `ttORACLE_DELETE` transactions (type code 52) without touching the raw `STTx` machinery directly.

## Role in the System

`OracleDelete` is one half of the Price Oracle transaction family introduced by the `featurePriceOracle` amendment. Its counterpart, `OracleSet` (type 51), creates or updates an on-ledger oracle object, populating fields like `sfProvider`, `sfURI`, `sfAssetClass`, `sfLastUpdateTime`, and `sfPriceDataSeries`. `OracleDelete`, by contrast, is a deletion transaction: it requires only a single field — `sfOracleDocumentID` — to identify and remove the oracle object from the ledger. The stark difference in surface area between the two transaction types is reflected directly in the generated code: `OracleSet` exposes six field accessors and five field setters; `OracleDelete` exposes exactly one of each.

## Immutable Wrapper: `OracleDelete`

`OracleDelete` extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides read-only accessors for the common transaction fields shared by all transaction types (`sfAccount`, `sfFee`, `sfSequence`, `sfFlags`, `sfMemos`, `sfDelegate`, etc.). The derived `OracleDelete` adds a single transaction-specific accessor, `getOracleDocumentID()`, which returns the `uint32_t` document ID directly via `tx_->at(sfOracleDocumentID)`.

The constructor enforces type safety with an explicit runtime check:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for OracleDelete");
```

This guard prevents an `OracleDelete` wrapper from accidentally being constructed around a different transaction type (e.g., an `OracleSet` or a `Payment`). Because the underlying `STTx` is `const`-qualified and held through a shared pointer, the wrapper itself is truly immutable after construction — callers cannot mutate the transaction through this class.

## Builder: `OracleDeleteBuilder`

`OracleDeleteBuilder` inherits from `TransactionBuilderBase<OracleDeleteBuilder>` via CRTP (Curiously Recurring Template Pattern). This template base class provides all common field setters (`setAccount()`, `setFee()`, `setSequence()`, `setLastLedgerSequence()`, `setDelegate()`, etc.) and returns `Derived&` from each setter, enabling fluent method chaining without virtual dispatch overhead.

The builder constructor accepts `sfOracleDocumentID` as a required parameter (alongside the account; sequence and fee are optional) and immediately calls `setOracleDocumentID()` to store it in the internal `STObject object_{sfTransaction}`. A critical implementation note is that the base class constructor deliberately avoids calling `object_.set(soTemplate)`. This ensures no `soeDEFAULT` placeholder fields are pre-populated, which would cause `STTx::applyTemplate()` to throw "may not be explicitly set to default" during the final `build()` step.

A second constructor accepts an existing `std::shared_ptr<STTx const>`, copying the transaction's fields into `object_` so the builder can reconstruct or modify a previously deserialized transaction. This path also validates the transaction type and throws if mismatched.

The `build()` method finalises the transaction lifecycle:
1. It calls `sign(publicKey, secretKey)` from the base class, which serializes the object (excluding signing fields), prepends `HashPrefix::txSign`, and appends the computed signature to `sfTxnSignature`.
2. It moves `object_` into a new `STTx` wrapped in a `shared_ptr`, then passes it to the `OracleDelete` constructor, returning an immutable wrapper ready for submission.

## Auto-Generated Nature

The file header reads `// This file is auto-generated. Do not edit.` The entire `protocol_autogen/transactions/` directory contains one file per XRPL transaction type following an identical structural template. Adding a new transaction type or changing an existing field schema regenerates these files from a canonical source-of-truth definition rather than requiring manual edits scattered across many files. This design eliminates the risk of accessor/setter drift and ensures that the type system precisely mirrors the protocol definition at all times.

## Design Tradeoffs

The wrapper/builder split is a deliberate design choice over a single mutable class. Keeping `OracleDelete` read-only means code that receives a completed transaction can never accidentally mutate it, even though `STTx` itself is not entirely immune to misuse through raw field access. The `[[nodiscard]]` attribute on all getters enforces that return values are not silently discarded. The use of `std::decay_t<typename SF_UINT32::type::value_type>` for builder setter parameters is a defensive measure to handle reference-qualified type aliases cleanly regardless of how the `SField` type traits resolve in a given compilation context.