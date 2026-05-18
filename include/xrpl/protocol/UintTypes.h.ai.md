# `include/xrpl/protocol/UintTypes.h`

## Role in the System

`UintTypes.h` is the canonical source for the XRPL protocol's fixed-width integer identifiers. Rather than passing plain `uint160` or `uint256` values throughout the codebase — which would let the compiler silently accept a `NodeID` where a `Currency` was expected — this header creates a family of **strongly typed** wrappers using the tag-parameter mechanism built into `base_uint`. The result is that a `Currency`, a `NodeID`, and an `AccountID` are all 160-bit values at the hardware level but are mutually incompatible at the type level.

## Strong Typedef Mechanism

The key pattern is `base_uint<Bits, Tag>`. The `Tag` parameter is an arbitrary type whose sole purpose is to make two otherwise-identical instantiations name distinct types. The tags (`CurrencyTag`, `DirectoryTag`, `NodeIDTag`) live inside `namespace xrpl::detail` and have no members beyond a default constructor; they exist only to make the C++ type system treat them as different. Compare the parallel approach in `AccountID.h`, which uses `AccountIDTag` identically.

The five named types defined here are:

| Alias | Bits | Tag | Purpose |
|---|---|---|---|
| `Currency` | 160 | `CurrencyTag` | Identifies a currency (XRP or IOU) |
| `NodeID` | 160 | `NodeIDTag` | Identifies a validator node |
| `Directory` | 256 | `DirectoryTag` | Index into the DEX offer book; last 64 bits encode quality |
| `MPTID` | 192 | *(none)* | MPT Issuance ID: 32-bit sequence + 160-bit account |
| `Domain` | 256 | *(none)* | Generic domain hash |

`MPTID` and `Domain` intentionally omit a tag, accepting the `void` default. This is a conscious tradeoff: there is no other 192-bit or 256-bit type to confuse them with (unlike the three competing 160-bit types), so the added friction of defining an empty tag class is not justified.

## Currency Sentinel Values

Three static singletons define the sentinel currencies used throughout the ledger:

**`xrpCurrency()`** returns a `Currency` set to all-zero bits (`beast::zero`). XRP is not a token in the IOU sense; this all-zeros encoding is what the binary protocol and all in-memory checks use to mean "this is native XRP." The `isXRP(Currency const&)` predicate is a simple equality test against `beast::zero`.

**`noCurrency()`** returns a `Currency` with a value of 1. This functions as a null / placeholder value used when no meaningful currency is present but the code still needs a valid `Currency` object to pass around.

**`badCurrency()`** returns a `Currency` whose byte representation spells out the ASCII characters `"XRP"` in the exact position where a 3-character ISO code would appear — specifically, the hex constant `0x5852500000000000` places 'X', 'R', 'P' at bytes 12–14 of the 20-byte field. This type was explicitly forbidden because users kept trying to use the ISO-style `"XRP"` string to represent native XRP when they should have used the all-zero form. The sentinel exists so the codebase can detect and reject that mistake.

## Currency Serialization

The XRPL wire format packs a `Currency` into 20 bytes (160 bits). When a currency is a standard 3-character code it occupies bytes 12–14, with all surrounding bytes set to zero. `to_string()` detects this layout using a bitmask (`sIsoBits`) that covers every byte except positions 12–14; if the masked value is zero and the three characters are all in the allowed character set (`isoCharSet`), the ISO code is returned directly. Otherwise the full 40-character hex representation is returned, which covers arbitrary custom token codes that don't fit the ISO convention.

`to_currency()` handles the inverse: an empty string or `"XRP"` maps to `beast::zero` (native XRP); a 3-character string maps through the ISO encoding; anything else is parsed as raw hex. The documented legacy caveat — that `to_currency()` accepts and returns `badCurrency()` without error — is preserved intentionally: the comment acknowledges that changing this would require auditing every call site, and the risk of unintentional breakage outweighs the cleanup benefit.

## Hash Specializations

The `std::hash` specializations at the bottom of the file follow the same pattern established in `AccountID.h`: they inherit from the `hasher` inner type that `base_uint` provides. This enables `Currency`, `NodeID`, and `Directory` to be used directly as keys in `std::unordered_map` and `std::unordered_set`. The `uint256` specialization is also included here even though `uint256` (which uses `base_uint<256>` with no tag) is not exclusively defined in this file, since it is a frequently used type in the same contexts.

## Design Observations

The empty-tag-class idiom is the idiomatic C++ alternative to `enum class` or `STRONG_TYPEDEF` macros for creating distinct fixed-width integer types. It has zero runtime overhead: the tag type contributes no bytes to the layout and no instructions to the generated code. The only cost is the boilerplate of defining a new empty class per distinct type. Given the importance of never mixing a `NodeID` with a `Currency` in protocol code, that boilerplate cost is clearly justified.