# `OracleSet.h` — Auto-Generated OracleSet Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` layer, a code-generated subsystem that provides a typed C++ API over XRPL's loosely-typed `STTx` transaction model. Every transaction type on the ledger gets its own header here; `OracleSet.h` handles transaction type `ttORACLE_SET` (type code 51), the operation that creates or updates a Price Oracle object on the XRPL. The oracle feature — gated behind the `featurePriceOracle` amendment — allows account operators to publish a series of asset price data points to the ledger, forming the basis for on-chain price feeds.

The file lives alongside 70+ sibling files in `include/xrpl/protocol_autogen/transactions/`, each following the same two-class pattern. The comment on line 1, `// This file is auto-generated. Do not edit.`, signals that this code is produced by a generator script from a transaction schema definition, not handwritten. Modifications belong in the generator or the schema, not here.

## The Wrapper/Builder Pattern

The design splits read and write concerns into two classes. `OracleSet` is an immutable view and `OracleSetBuilder` is the mutable construction surface. This separation is intentional: once a transaction is signed and submitted to the network, nothing should be able to mutate it through the same handle. The wrapper holds a `std::shared_ptr<STTx const>` (inherited as `tx_` from `TransactionBase`), making the const-ness structural rather than advisory.

### `OracleSet` (Immutable Wrapper)

`OracleSet` inherits `TransactionBase`, which provides type-safe getters for the universal fields shared by every transaction: `sfAccount`, `sfSequence`, `sfFee`, `sfSigningPubKey`, optional `sfFlags`, `sfMemos`, `sfSigners`, `sfLastLedgerSequence`, and others including the `sfDelegate` field added for the delegable transaction feature. `OracleSet` then adds its own oracle-specific getters on top.

The constructor enforces type safety through a runtime check:

```cpp
explicit OracleSet(std::shared_ptr<STTx const> tx)
    : TransactionBase(std::move(tx))
{
    if (tx_->getTxnType() != txType)
        throw std::runtime_error("Invalid transaction type for OracleSet");
}
```

The check happens *after* `std::move(tx)` into the base, which is why it accesses `tx_` rather than the parameter. The `static constexpr txType` member lets callers compare types without an instance.

**Field optionality is reflected in the API.** Required fields return a value directly:

- `getOracleDocumentID()` → `uint32_t`: a per-account identifier that uniquely addresses a particular oracle object, allowing one account to maintain multiple independent oracles.
- `getLastUpdateTime()` → `uint32_t`: the ripple epoch timestamp of the data batch being submitted.
- `getPriceDataSeries()` → `STArray const&`: the array of price entries. Each element in the `STArray` represents a `PriceData` inner object containing asset pair and price fields. This is returned as a raw `STArray` rather than a typed container because the inner structure is an untyped heterogeneous array in the `STObject` model; the comment `@note This is an untyped field` calls this out explicitly.

Optional fields — `Provider`, `URI`, and `AssetClass` — return `protocol_autogen::Optional<T>`, a type alias defined in `Utils.h`. For non-reference value types this collapses to `std::optional<T>`, but for reference types (like `STArray`) it becomes `std::optional<std::reference_wrapper<T>>` to avoid copying. Each optional getter is paired with a `has*()` predicate, so callers can check presence without constructing a temporary:

```cpp
if (tx.hasProvider())
    doSomethingWith(tx.getProvider().value());
```

`Provider`, `URI`, and `AssetClass` all carry the `SF_VL` (variable-length blob) type, used to store arbitrary byte strings — the provider's name, a URL pointing to off-chain data, and a category label like `"currency"` or `"commodity"`.

### `OracleSetBuilder` (Fluent Builder)

`OracleSetBuilder` inherits `TransactionBuilderBase<OracleSetBuilder>` using CRTP. The template parameter lets every setter in the base class return `Derived&` — that is, `OracleSetBuilder&` — so method chains stay coherent without any virtual dispatch. The base class stores a mutable `STObject object_{sfTransaction}` that accumulates fields before the final `STTx` is constructed.

The primary constructor enforces required fields at construction time by accepting them as mandatory parameters:

```cpp
OracleSetBuilder(SF_ACCOUNT::type::value_type account,
                 std::decay_t<typename SF_UINT32::type::value_type> const& oracleDocumentID,
                 std::decay_t<typename SF_UINT32::type::value_type> const& lastUpdateTime,
                 STArray const& priceDataSeries,
                 std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                 std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt)
```

The use of `std::decay_t<typename SF_UINT32::type::value_type>` decays the field type alias (removing references and cv-qualifiers) before taking a `const&`. This defensive pattern prevents the parameter from accidentally binding to a temporary field accessor return value and dangling.

Sequence and fee are optional at construction time because in some workflows the network assigns these automatically (auto-fill mode). The builder's secondary constructor, taking an existing `std::shared_ptr<STTx const>`, enables round-trip modification: unwrap an existing oracle transaction, mutate optional fields, re-sign. Like the wrapper's constructor, it checks `getTxnType()` and throws on mismatch.

`setPriceDataSeries()` uses `setFieldArray()` rather than the subscript operator because `STArray` fields require their own setter path in `STObject`.

The `build()` method terminates the chain: it calls `sign()` (inherited from the base, which serializes without signing fields, prepends the `HashPrefix::txSign` prefix, and appends the signature), then wraps the finalized `STObject` in a `std::shared_ptr<STTx>` and returns an `OracleSet` wrapper. After `build()`, the builder's internal `object_` has been moved out via `std::move`, so the builder should not be reused.

## Relationship to the Broader System

The file pairs with `OracleDelete.h` (type 52), which shares the same amendment gate and carries only `OracleDocumentID` as its required field — deletion only needs to identify the target oracle. Together they form the complete CRUD surface for Price Oracle objects.

`TransactionBase::validate()` connects this layer back to the schema-enforcement machinery by calling `validateSTObject()` against the `SOTemplate` registered in `TxFormats`, ensuring that the auto-generated field set stays consistent with the canonical format definitions. This means any oracle transaction that passes through `OracleSet` has already been type-checked at the construction call site and can be re-validated at any time against the ledger's format rules.