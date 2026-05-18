# `TxFormats.cpp` — Transaction Format Registry

`TxFormats.cpp` is the central registry for every transaction type the XRP Ledger protocol recognizes. Its job is to declare, once and definitively, what fields each transaction is permitted to carry, whether those fields are required or optional, and which fields are universal across all transactions. Every path that parses, constructs, or validates a transaction — wire deserialization via `STObject`, JSON submission via `doSubmit`, or programmatic construction via `Transaction` — traces back to the templates built here.

## Inheritance and the Singleton Pattern

`TxFormats` inherits from `KnownFormats<TxType, TxFormats>`, a CRTP template defined in `KnownFormats.h`. `KnownFormats` maintains two `boost::container::flat_map` lookup tables — one keyed by the string name of the transaction type (`names_`) and one keyed by the integer `TxType` enum value (`types_`). Each entry in these maps points to a `KnownFormats::Item`, which bundles together the transaction's numeric type, its string name, and a fully-constructed `SOTemplate`.

`getInstance()` returns the singleton via a function-local `static`, making initialization thread-safe under C++11's guarantee that local statics are initialized exactly once even under concurrent first-calls. The object is expensive to construct (it registers dozens of transaction types), so it must never be re-created; the Meyer's singleton is the right choice.

## The Common Fields Contract

`getCommonFields()` returns a static `vector<SOElement>` containing the 17 fields that every XRPL transaction must or may carry, regardless of type. These are intentionally separated from transaction-specific fields so that both categories can be merged into a single `SOTemplate` at registration time without duplication.

The required fields — `sfTransactionType`, `sfAccount`, `sfSequence`, `sfFee`, and `sfSigningPubKey` — form the minimum viable transaction skeleton. The absence of any of them causes validation to fail immediately when the resulting `SOTemplate` is checked during `STObject` construction.

Several optional fields tell a story about protocol evolution:
- `sfPreviousTxnID` carries the comment `// emulate027`, signaling backward compatibility with a historical wire format (pre-amendment 027). It is kept optional rather than removed to avoid breaking existing transaction blobs.
- `sfSigners` (annotated `// submit_multisigned`) is the container for multi-signature arrays. It coexists with `sfSigningPubKey` because single-sig and multi-sig are orthogonal modes at the format level.
- `sfTicketSequence` allows a transaction to consume a ticket instead of the account's current sequence number, enabling out-of-order transaction submission.
- `sfNetworkID` was added to let sidechain networks distinguish their transactions from mainnet ones at the wire level.
- `sfDelegate` is the newest addition, supporting the delegation feature that allows an account to authorize another to act on its behalf.

## The Macro Expansion Technique

The constructor body is the most architecturally significant part of the file. Rather than manually calling `add()` for each of the dozens of transaction types, it exploits a single `#include` of `transactions.macro` with a bespoke `TRANSACTION` macro definition:

```cpp
#define TRANSACTION(tag, value, name, delegable, amendment, privileges, fields) \
    add(jss::name, tag, UNWRAP fields, getCommonFields());
```

The `transactions.macro` file contains one `TRANSACTION(...)` invocation per transaction type. Each invocation carries: the `TxType` enum tag (e.g., `ttPAYMENT`), a numeric wire value (e.g., `0`), a C++ class name that maps to a `jss::` string constant, a `Delegation` enum, amendment prerequisites, a privilege bitfield used by `InvariantCheck`, and a parenthesized list of `SOElement` entries specific to that transaction.

The `UNWRAP(...)` helper macro strips the extra parentheses from the fields argument, which are required because the fields list itself contains commas that would otherwise confuse the preprocessor's argument parsing. This is a standard idiom for passing brace-enclosed initializer lists through variadic macros.

The `#pragma push_macro` / `#undef` / `#pragma pop_macro` sandwich guards any pre-existing `TRANSACTION` definition in the translation unit — defensive hygiene for a macro that has a common name and might appear in platform headers.

The `add()` method (inherited from `KnownFormats`) checks for duplicate `TxType` values at construction time and calls `LogicError` if one is found, making type-id collisions a hard crash at startup rather than a silent bug.

## The SOTemplate and Validation Flow

When `add()` stores a format, it constructs a `SOTemplate` from the union of the transaction-specific fields and the common fields. `SOTemplate` internally builds a position index from `SField` number to element index, enabling O(1) lookup during serialization.

At parse time, `STObject::set` calls `findByType` on the `TxFormats` singleton to retrieve the `SOTemplate` for the transaction's type code, then validates every field in the incoming byte stream or JSON object against it. Fields marked `soeREQUIRED` that are missing cause immediate rejection; unknown fields not present in the template are equally rejected. This template-driven validation is why there is no explicit per-field null-checking elsewhere in the codebase — the `SOTemplate` system enforces presence and type for every field declaratively.

The `SOETxMPTIssue` annotation visible in `transactions.macro` entries (e.g., `{sfAmount, soeREQUIRED, soeMPTSupported}` on `ttPAYMENT`) extends this validation further: `SOElement` carries a flag indicating whether an amount field may carry an MPT (Multi-Purpose Token) amount rather than a classic XRP/IOU amount, enabling the template system to police MPT usage without bespoke code in each transactor.

## Adding a New Transaction Type

The design ensures that the only file that must change when a new transaction type is introduced is `transactions.macro`. The `TxFormats` constructor, the `TxType` enum in `TxFormats.h`, and all downstream validation machinery all derive their knowledge from that single macro file, minimizing the surface area for omission bugs.