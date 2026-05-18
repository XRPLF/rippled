# `MPTIssue.h` — MPT Issuance Adapter for Protocol Polymorphism

## Role in the System

`MPTIssue` exists because the XRPL codebase originally modeled all fungible assets through the `Issue` type, which pairs a 160-bit `Currency` with an `AccountID` issuer. When Multi-Purpose Tokens (MPTs) were introduced, code throughout the engine needed to handle a third asset class alongside XRP and IOU. Rather than templating every function on a raw `MPTID`, the designers introduced `MPTIssue` as a thin wrapper that mirrors `Issue`'s public interface precisely, allowing `Asset` (and any generic template code constrained by `ValidIssueType`) to treat the two types uniformly.

## The MPTID Encoding

`MPTID` (defined in `UintTypes.h`) is a `base_uint<192>` — 24 bytes stored in big-endian order. Its layout is a simple concatenation:

```
[ 4 bytes: uint32_t sequence | 20 bytes: AccountID ]
```

This encoding is the canonical identifier for an MPT issuance on the ledger. The sequence number disambiguates multiple issuances from the same account. `MPTIssue` holds exactly one `MPTID mptID_` and exposes this structure through its API.

## Interface Mirroring Strategy

The class deliberately replicates the public surface of `Issue`:

| Method | `Issue` behavior | `MPTIssue` behavior |
|---|---|---|
| `getIssuer()` | returns `account` field | extracts bytes 4–23 of `mptID_` |
| `getText()` | formats currency+account | formats `mptID_` as hex |
| `setJson(jv)` | writes `currency`/`issuer` fields | writes `mpt_issuance_id` field |
| `native()` | `true` for XRP | always `false` |
| `integral()` | `true` only for XRP | always `true` |

`native()` returning `false` and `integral()` returning `true` is significant: MPTs carry 64-bit integer amounts (like `MPTAmount`), whereas IOUs use multi-precision rational arithmetic. The `Asset::getAmountType()` method in `Asset.h` dispatches on these flags to select between `XRPAmount`, `IOUAmount`, and `MPTAmount` at compile time.

The `ValidIssueType` concept in `Concepts.h` constrains templates to accept only `Issue` or `MPTIssue`, which is how `Asset::get<T>()` and `Asset::holds<T>()` enforce type safety at compile time. The implicit conversion operator `operator MPTID const&()` allows an `MPTIssue` to be passed wherever a raw `MPTID` is expected without a cast.

## Issuer Extraction: Two Approaches

There are two ways to extract the `AccountID` from a `MPTID`, and the header deliberately provides both:

**`getIssuer()` (member function):** Uses `reinterpret_cast` on the underlying byte buffer, skipping the first 4 bytes. This is zero-copy and returns a reference, but it works only because `AccountID` is a `base_uint<160>` with a compatible memory layout. The `static_assert` on sizes guards this assumption.

**`getMPTIssuer()` (free function):** Uses `std::bit_cast` after copying 20 bytes into a `std::array`. The comment explicitly notes that `bit_cast` is a compiler intrinsic typically optimized to nothing in the final assembly, so the copy exists only at the C++ source level. The free function returns by value.

Crucially, `getMPTIssuer()` **deletes its rvalue-reference overloads**:

```cpp
inline AccountID const& getMPTIssuer(MPTID const&&) = delete;
inline AccountID const& getMPTIssuer(MPTID&&)       = delete;
```

This prevents a dangling-reference bug: if a caller passed a temporary `MPTID`, the returned `AccountID const&` would immediately dangle. The deleted overloads turn this into a compile-time error. The member `getIssuer()` doesn't have this problem because it returns a reference into `mptID_`, which lives as long as the `MPTIssue` object.

## Sentinel Values

`noMPT()` and `badMPT()` replicate the sentinel pattern from `Issue.h`:

- `noMPT()` encodes `{ sequence=0, account=noAccount() }` — all-zero bits, representing "no MPT".
- `badMPT()` encodes `{ sequence=0, account=xrpAccount() }` — the XRP account address, a conventionally invalid issuer for MPTs.

The `BadAsset` comparison in `Asset.h` detects a bad MPT by checking `issue.getIssuer() == xrpAccount()`, matching this sentinel definition.

## Comparisons and Hashing

Equality and three-way comparison (`<=>`) are both `constexpr` and delegate directly to `MPTID`'s operators, which compare the raw 192-bit value. This means two `MPTIssue` instances are equal if and only if their full `MPTID` — sequence and account combined — matches. There is no partial equality ignoring the sequence, unlike `Issue` equality which ignores the issuer account when the currency is XRP.

The `hash_append` template plugs `MPTIssue` into the Beast hashing framework, and the `std::hash<MPTID>` specialization placed in the `std` namespace at the bottom of the header makes `MPTID` usable directly as a key in `std::unordered_map` and similar containers without going through `MPTIssue`.

## JSON Serialization

`setJson()` writes a single `mpt_issuance_id` key, contrasted with `Issue::setJson()` which writes `currency` and `issuer` separately. `mptIssueFromJson()` strictly validates this distinction: it throws if `currency` or `issuer` keys are present, enforcing that MPT JSON cannot be confused with IOU JSON. The parsing path `parseHex` on `MPTID` validates the 48-character hex string before constructing the object.