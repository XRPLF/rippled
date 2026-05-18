# `include/xrpl/protocol/TER.h` — Transaction Engine Result Codes

## Purpose and System Role

`TER.h` defines the complete taxonomy of transaction result codes used throughout the XRP Ledger transaction processing pipeline. Every transaction that passes through the engine — from initial validation to consensus application — produces a `TER` result. These codes determine whether a transaction is applied to the ledger, forwarded to peers, queued for later, rejected outright, or silently dropped.

The file solves two related problems simultaneously: it defines the raw codes as enumerations aligned with the external `ripple-binary-codec` encoding (making them part of the wire protocol), and it provides a strongly-typed C++ wrapper that prevents misuse at compile time.

## The Six Result Categories

Six enum types partition a signed integer space into semantically distinct bands:

| Prefix | Range | Meaning |
|--------|-------|---------|
| `tel` | −399..−300 | **Local** error — rejected by this node only, not forwarded, no fee check |
| `tem` | −299..−200 | **Malformed** — transaction is structurally corrupt; cannot succeed in any ledger |
| `tef` | −199..−100 | **Failure** — transaction failed due to current ledger state; no fee charged |
| `ter` | −99..−1 | **Retry** — might succeed after other transactions; may be queued |
| `tes` | 0 | **Success** — the single value `tesSUCCESS` |
| `tec` | 100..255 | **Claim** — the fee is consumed and sequence number spent, but no other effect |

The ranges are not arbitrary — they are stable across releases because they are encoded into historical ledger metadata and referenced by external libraries like `ripple-binary-codec`. The comment `// DO NOT CHANGE THESE NUMBERS: They appear in ledger meta data` in `TECcodes` makes this constraint explicit. Adding new codes means appending within the range; removing or renumbering codes would corrupt historical ledger interpretation.

`TECcodes` notably has a comment distinguishing `tecNO_ENTRY` (primary object not found) from `tecOBJECT_NOT_FOUND` (auxiliary object not found), which reflects an emergent naming convention the team is documenting to avoid future inconsistency.

## The `TERSubset<Trait>` Template

The most architecturally significant design in this file is `TERSubset<Trait>`, a policy-based wrapper around a plain `TERUnderlyingType` (a `typedef` for `int`). The template parameter `Trait` is a class template that, when specialized for a given enum type, inherits from `std::true_type` if that enum is allowed, or from `std::false_type` if it is not.

Constructors and assignment operators use `std::enable_if_t<Trait<T>::value>` to gate which enum values can be implicitly converted in. This means an attempt to assign a `TECcodes` value to a `NotTEC` variable fails at compile time — not at runtime — with no casts required by the caller.

The file defines two concrete instantiations:

- **`NotTEC`** — permits `tel`, `tem`, `tef`, `ter`, and `tes`, but explicitly **excludes `TECcodes`**.
- **`TER`** — permits all six categories, including `NotTEC` (allowing widening assignment from a `NotTEC` to a `TER`).

The `NotTEC` restriction is a security invariant, not just a style choice. The comment in the header explains the attack vector: `preflight` validation runs *before* signature checking. If `preflight` could return a `tec` code, a malicious actor could craft a transaction with a very large fee and get that fee deducted from an account without providing a valid signature — fee theft via an unsigned transaction. Restricting `preflight` return types to `NotTEC` at the type level makes this class of exploit structurally impossible.

## The `TERtoInt` Overload Set and Comparison Operators

Rather than using an explicit conversion operator on `TERSubset`, the design provides a friend free function `TERtoInt(TERSubset v)` and overloads of the same name for each of the six raw enum types. This is deliberate: the comment in the header explains that explicit conversion operators on the class would allow silent implicit conversions in contexts like constructor initialization (`Status(TER ter) : code_(ter) {}`), which compiles silently even with `explicit`. A named function forces the conversion to be visible.

The six comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) are all implemented as free function templates gated by `std::enable_if_t` on whether both operands have a valid `TERtoInt` overload returning `int`. This unified approach means comparisons work across *any combination* of raw enum types and `TERSubset` wrappers without writing a combinatorial set of operator overloads.

## Boolean Semantics

`TERSubset::operator bool()` is `explicit` and returns `code_ != tesSUCCESS` — i.e., truthy means "something went wrong." This mirrors conventional error-code idioms. The `isTesSuccess()` free function exploits this: it simply negates the boolean conversion, relying on `tesSUCCESS == 0`.

## Category Inspection Helpers

Six `inline bool isXxx(TER x)` functions perform range checks against the numeric boundaries between categories. These functions take a `TER` (not a raw enum), so they work on any code value regardless of how it arrived. `isTecClaim(x)` checks `x >= tecCLAIM`, correctly treating everything from 100 upward as a fee-claim result.

## Lookup and Serialization Utilities

`transResults()` in the `.cpp` file returns a static `const` `unordered_map` keyed by `TERUnderlyingType`, mapping each code to a `{token, description}` pair. The `MAKE_ERROR` macro stringifies the enum name via `#code` to avoid manually duplicating the token string, guaranteeing the token in the map matches the enum identifier exactly. The map is function-local static, ensuring thread-safe initialization under C++11 and later.

`transResultInfo()`, `transToken()`, and `transHuman()` provide lookups from code to string. `transCode()` inverts the mapping — it builds a second `unordered_map` (keyed by token string, lazily initialized as a local static) by inverting the primary map at first call, then returns an `std::optional<TER>` to communicate lookup failures without exceptions.