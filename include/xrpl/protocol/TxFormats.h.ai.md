# `include/xrpl/protocol/TxFormats.h`

## Role in the System

This header is the authoritative definition point for two foundational protocol constructs: the `TxType` enum that identifies every transaction type in the XRPL binary format, and the `TxFormats` singleton that maps each type to its validated field schema. Together they form the schema registry that the ledger uses to deserialize, validate, and route every signed transaction it processes.

## The `TxType` Enum

`TxType` is declared as `enum TxType : std::uint16_t`. Its enumerators are generated almost entirely by the X-macro pattern: the `transactions.macro` file is included after defining `TRANSACTION(tag, value, ...)` to expand as `tag = value,`. This means the enum values (0 for `ttPAYMENT`, 1 for `ttESCROW_CREATE`, 3 for `ttACCOUNT_SET`, and so on) come from a single source of truth shared with the runtime registration code and, optionally, with transactor header inclusion.

The file's documentation carries an explicit hard-fork warning: because these numeric identifiers are embedded inside **signed** transaction objects, they are immutable protocol surface. A validator that disagrees about what type code 7 means cannot participate in consensus with its peers. This is why deprecated types — `ttNICKNAME_SET` (6), `ttCONTRACT` (9), and `ttSPINAL_TAP` (11) — are not removed from the enum but are instead annotated `[[deprecated]]`. Their slots are tombstoned so no future transaction type can accidentally claim a numeric ID that already exists in historical ledger data, which would cause those old transactions to be misclassified.

`ttHOOK_SET = 22` is annotated `[[maybe_unused]]`, reflecting its status as a Hooks amendment transaction type present in this codebase but not universally activated across all networks.

## The X-Macro Expansion Strategy

The `transactions.macro` file is the canonical list of every transaction type, with each entry carrying the full signature:

```
TRANSACTION(tag, value, name, delegable, amendments, privileges, fields)
```

The fields include whether the transaction can be *delegated* to another account, which amendment (if any) gates the transaction, a privileges bitfield used by `InvariantCheck`, and the transaction-specific `SOElement` fields. By redefining `TRANSACTION` before each `#include`, the same macro file serves three distinct purposes without duplication: generating the `TxType` enum (in this header), registering `KnownFormats::Item` objects at startup (in `TxFormats.cpp`), and pulling in transactor class headers (guarded by `#if TRANSACTION_INCLUDE`).

The `UNWRAP(...)` helper in `TxFormats.cpp` is necessary because the field list argument is a brace-enclosed initializer list that would confuse the preprocessor's argument parsing if passed directly.

## The `TxFormats` Class

`TxFormats` inherits from `KnownFormats<TxType, TxFormats>`, a CRTP-style template that manages a registry of `Item` objects. Each `Item` pairs a string name, a `TxType` key, and an `SOTemplate` — the ordered list of `SOElement` descriptors that defines which serialized object fields (SFields) the transaction accepts, whether each is required or optional, and whether amount fields support Multi-Purpose Tokens (MPT).

Construction happens once through the private `TxFormats()` constructor invoked lazily by `getInstance()`, which returns a `static const` instance. The `static` local guarantees thread-safe initialization under C++11 and later. The class is non-copyable (inherited from `KnownFormats`), enforcing the singleton invariant.

`getCommonFields()` returns the static list of fields shared by every transaction type regardless of which `TRANSACTION` entry registered it: the required `sfTransactionType`, `sfAccount`, `sfSequence`, `sfFee`, and `sfSigningPubKey`; and a range of optional fields covering flags, tags, memos, multi-signature data, network ID, and the delegate field. These common fields are merged with each transaction's unique fields when `KnownFormats::add()` constructs the `SOTemplate` during startup.

## Storage in `KnownFormats`

The base class stores `Item` objects in a `std::forward_list`, a deliberate choice: as a node-based container, it guarantees that inserting new items never invalidates existing pointers. The two lookup indices — `boost::container::flat_map<std::string, Item const*>` for name lookups and `boost::container::flat_map<TxType, Item const*>` for type lookups — store raw pointers into the list. This is safe precisely because `forward_list` never moves its elements. `flat_map` is chosen over `std::map` for cache-friendly iteration, which matters on hot lookup paths.

The `add()` method actively guards against duplicate numeric IDs at startup: if a `TxType` value is already registered, it calls `LogicError()`, which causes an immediate process termination. This is the compile-time protection the header's `@todo` note acknowledges the language cannot provide — the safety net is pushed to the first execution of the singleton constructor.

## Relationship to Validation and Serialization

The `SOTemplate` produced for each transaction type is the schema consulted during deserialization and validation. When a transaction arrives over the wire, the deserializer looks up its `TransactionType` field value in `TxFormats`, retrieves the associated `SOTemplate`, and uses it to parse and validate the remaining fields — rejecting unknown fields and enforcing the `soeREQUIRED` / `soeOPTIONAL` / `soeDEFAULT` constraints encoded in `SOEStyle`. This makes `TxFormats` a critical chokepoint: every transaction that enters the ledger has its structure validated against a schema registered here.