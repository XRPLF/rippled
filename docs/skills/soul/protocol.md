# Protocol and Serialization

Macro-driven system for defining features, transactions, ledger entries, and serialized fields. Canonical binary format is required for signatures and consensus.

## Key Invariants

- Fields are sorted by (type code, field code) for canonical serialization; sorting by Field ID bytes produces WRONG results
- Field ID encoding: 1-3 bytes depending on type/field code values (both < 16 = 1 byte)
- Signing hash prefix: `0x53545800` for single-signing, `0x534D5400` for multi-signing
- `STObject[sfFoo]` returns value or default; `STObject[~sfFoo]` returns optional (nothing if absent)
- All ST types inherit from `STBase`; `STVar` provides type-erased storage with stack/heap allocation

## Macro System

- `XRPL_FEATURE(name, supported, vote)` / `XRPL_FIX` / `XRPL_RETIRE` in `features.macro`
- `TRANSACTION(tag, value, class, delegation, fields)` in `transactions.macro`
- `LEDGER_ENTRY(type, code, class, name, fields)` in `ledger_entries.macro`
- `TYPED_SFIELD(name, TYPE, code)` in `sfields.macro`
- Adding any new definition requires updating the count in the corresponding header

## Serialization Format

- XRP Amount: 8 bytes, MSB=0, next bit=1 for positive, remaining 62 bits = value
- Token Amount: 8 bytes mantissa/exponent + 20 bytes currency + 20 bytes issuer
- AccountID: 20 bytes, length-prefixed when top-level
- STArray: elements between start (`0xf0`) and end (`0xf1`) markers
- STObject: fields in canonical order between start (`0xe0`) and end (`0xe1`) markers
- Length prefixing: 1 byte (0-192), 2 bytes (193-12480), 3 bytes (12481-918744)

## Common Bug Patterns

- Adding a field to `transactions.macro` without adding it to `sfields.macro` causes silent serialization failures
- Forgetting to increment `numFeatures` after adding to `features.macro` causes out-of-bounds access
- Non-canonical field ordering in hand-built binary blobs causes signature verification failures
- `soeMPTSupported` flag on amount fields enables MPT token support; omitting it silently rejects MPT payments

## Key Patterns

### Amendment Registration
```cpp
// In features.macro — REQUIRED format:
XRPL_FEATURE(MyNewFeature,  Supported::yes, VoteBehavior::DefaultNo)
XRPL_FIX    (MyBugFix,      Supported::yes, VoteBehavior::DefaultNo)
// MUST also increment numFeatures in Feature.h — omitting causes OOB access
```

### STObject Field Access
```cpp
// Safe: optional access — returns std::optional, never throws
if (auto const val = tx[~sfAmount])
    use(*val);

// Throws if field is absent — only safe when preflight guarantees presence
auto const amount = tx[sfAmount];
```

## Key Files

- `include/xrpl/protocol/detail/features.macro` - amendment definitions
- `include/xrpl/protocol/detail/transactions.macro` - transaction types
- `include/xrpl/protocol/detail/ledger_entries.macro` - ledger objects
- `include/xrpl/protocol/detail/sfields.macro` - field definitions
- `include/xrpl/protocol/Feature.h` - `numFeatures` constant
- `src/libxrpl/protocol/STObject.cpp` - core serialized object
