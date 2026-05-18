# `include/xrpl/protocol/jss.h` — Centralized JSON Field Name Registry

## Purpose

`jss.h` is the single authoritative source of truth for every JSON key name used across the XRPL rippled implementation. Rather than scattering string literals throughout hundreds of translation units, this header declares each key exactly once as a `constexpr Json::StaticString` inside the `xrpl::jss` namespace. Any code that builds or inspects a `Json::Value` object — RPC handlers, ledger serializers, transaction processors, network operation code — includes this header and writes `jss::account_data` instead of `"account_data"`.

## The `JSS` Macro

The entire file is built around a single macro defined at line 9:

```cpp
#define JSS(x) constexpr ::Json::StaticString x(#x)
```

`JSS(account_data)` expands to `constexpr ::Json::StaticString account_data("account_data")`. The `#x` stringification operator ensures the C++ identifier and the JSON key string are always identical — it is impossible for a rename of the identifier to silently diverge from the string it carries. After all declarations, the macro is `#undef`-ed so it does not leak beyond the namespace block.

## Why `StaticString`?

`Json::StaticString` is a thin wrapper over `char const*` defined in `json_value.h`. Its sole purpose is to act as a tag type that routes `Json::Value::operator[]` through a distinct, optimized code path. When you index a `Json::Value` object with a regular `std::string` or `char const*` key, the JSON library copies the key string into its internal map node. When you index with a `StaticString`, the library stores only the pointer — no allocation, no copy — because it knows the pointed-to string has static lifetime (it came from a string literal in the binary). Since `jss.h` constants hold pointers into the compiler-generated string table, this contract is always satisfied. At the call site the difference looks like:

```cpp
result[jss::ledger_index] = 42;       // stores pointer, no allocation
result["ledger_index"] = 42;          // copies "ledger_index" into heap storage
```

Across the thousands of JSON field accesses per second on a busy validator this matters, and the API makes the efficient path the default for all named fields.

## Naming Conventions

The file mixes two conventions that map onto distinct JSON domains. **PascalCase** names (e.g., `Account`, `Amount`, `Flags`, `TransactionType`, `SigningPubKey`) are canonical transaction and ledger-entry field names defined by the XRPL wire protocol. Their casing is part of the protocol specification and must match exactly what gets serialized into ledger objects. **snake_case** names (e.g., `account_data`, `ledger_index`, `engine_result_code`) belong to the RPC API layer — the JSON objects that flow between client applications and the server over HTTP or WebSocket. Keeping both in the same file means a developer can trace a field from RPC parameter, through processing logic, to serialized ledger entry or transaction without switching files.

## Inline Documentation Convention

Each declaration carries a trailing comment using a compact notation explained in the file header:

- `in:` — the given RPC handler reads this field from its input `Json::Value`
- `out:` — the given handler writes this field into its response `Json::Value`
- `field:` — this is a protocol-level field of at least one transaction or ledger entry type
- `RPC:` — common infrastructure of RPC request/response envelopes
- `error:` — part of the standard error response shape

This embedded cross-reference serves as a lightweight index into the codebase. Looking up `jss::engine_result_code` immediately reveals it is written by `NetworkOPs`, `TransactionSign`, and `Submit` — three distinct subsystems that all agree on the same key name precisely because they share this header.

## Macro-Generated Transaction and Ledger Entry Names

The tail of the file uses a pattern that appears elsewhere in the XRPL protocol infrastructure: it temporarily redefines the `TRANSACTION` and `LEDGER_ENTRY` X-macros to emit `JSS(name)` declarations, then includes the canonical macro tables:

```cpp
#pragma push_macro("TRANSACTION")
#undef TRANSACTION
#define TRANSACTION(tag, value, name, ...) JSS(name);
#include <xrpl/protocol/detail/transactions.macro>
#undef TRANSACTION
#pragma pop_macro("TRANSACTION")
```

The same pattern repeats for `LEDGER_ENTRY` and `LEDGER_ENTRY_DUPLICATE`. This ensures that `jss::Payment`, `jss::EscrowCreate`, `jss::Offer`, and every other transaction or ledger-entry type name are automatically available as `StaticString` constants without manual duplication. Adding a new transaction type to `transactions.macro` automatically registers its name in `jss` — the registry stays in sync by construction.

## ODR and Translation Unit Safety

Because every declaration uses `constexpr`, the variables have internal linkage in C++17 and later. Including the header from dozens of `.cpp` files does not violate the One Definition Rule — each translation unit gets its own copy of the `char const*` wrapper, but all copies point to identical string literal data deduplicated by the linker. The `constexpr` qualifier also enables compile-time use of these values in template arguments or `static_assert` contexts if needed.

## Relationship to the Rest of the Protocol Layer

`jss.h` sits at the boundary between the protocol serialization layer (`include/xrpl/protocol/`) and the RPC/network layer (`src/xrpld/rpc/`). Almost every RPC handler file includes it directly. It has no dependencies beyond `json_value.h`, making it cheap to include anywhere. The absence of a corresponding `.cpp` file is intentional — this header is pure compile-time data with no runtime initialization cost.