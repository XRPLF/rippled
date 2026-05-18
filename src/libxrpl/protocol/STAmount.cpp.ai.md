# `src/libxrpl/protocol/STAmount.cpp`

## Role and Purpose

`STAmount` is the universal value type for the XRP Ledger. Every amount on the network — whether XRP drops, IOU tokens from a gateway, or Multi-Purpose Tokens (MPT) — is represented, serialized, compared, and arithmetically manipulated through this class and its companion free functions. The file is the implementation counterpart to `include/xrpl/protocol/STAmount.h`, providing everything from constructors and wire-format serialization to multiply-with-rounding and JSON parsing.

The class inherits from `STBase`, which makes it a first-class XRPL serialized type (type tag `STI_AMOUNT`), and from `CountedObject<STAmount>` for memory diagnostics.

---

## Internal Representation

Every `STAmount` stores four fields:

| Field | Type | Meaning |
|---|---|---|
| `mAsset` | `Asset` | A `std::variant` holding either an `Issue` (currency + issuer) or an `MPTIssue` (192-bit MPTID) |
| `mValue` | `uint64_t` | Unsigned mantissa |
| `mOffset` | `int` | Base-10 exponent |
| `mIsNegative` | `bool` | Sign |

The *canonical* internal form differs by asset type:

- **XRP** (`native()` true): `mOffset == 0`, `mValue` is the raw drop count (≤ `cMaxNativeN = 10^17`). Negative zero is illegal.
- **IOU** (issued currency): `mValue` ∈ [`10^15`, `10^16 − 1`] and `mOffset` ∈ [`−96`, `+80`], representing `value × 10^offset`. Zero is the special case `mValue = 0, mOffset = −100`.
- **MPT**: Same as XRP — integer, `mOffset == 0`, no fractional part.

The header documents three static constants (`cMinValue`, `cMaxValue`, `cMinOffset`, `cMaxOffset`) that define the canonical IOU window. These are not just range guards — the window must be exactly [10^15, 10^16) for the multiplication and division algorithms to maintain precision without overflow.

---

## Wire Format and Deserialization

The serialization format packs type, sign, exponent, and mantissa into 64 bits (for the header word), then appends currency/issuer data as needed:

- **Bit 63** (`cIssuedCurrency`, `0x8000000000000000`): 0 means native-or-MPT, 1 means IOU.
- **Bit 62** (`cPositive`, `0x4000000000000000`): 1 means positive, 0 means negative.
- **Bit 61** (`cMPToken`, `0x2000000000000000`): 1 means MPT; only meaningful when bit 63 is 0.

For an **IOU**, the top 10 bits carry `offset + 97` (biased to stay non-negative); the low 54 bits carry the mantissa. Currency (160 bits) and issuer (160 bits) follow inline. For **MPT**, the header word carries the 56-bit value; a 192-bit MPTID (`get192()`) follows. For **XRP**, the 62-bit value sits in the lower bits of the header word — no additional bytes are needed.

The `STAmount(SerialIter&, SField const&)` constructor does the decoding. It rejects "negative zero" XRP, invalid IOU currencies (those colliding with the XRP currency code), and mantissa/exponent values outside the canonical range. The inverse, `add(Serializer&)`, reassembles the same bit pattern. Together they establish the invariant that any round-tripped amount is canonical.

---

## `canonicalize()` — The Core Normalizer

`canonicalize()` is called by most constructors after setting `mValue`, `mOffset`, and `mIsNegative`. Its job is to bring the amount into its canonical form:

For **integral types** (XRP and MPT), it repeatedly divides or multiplies `mValue` by 10 while adjusting `mOffset`, until `mOffset == 0`. It checks `cMaxNativeN` and `maxMPTokenAmount` overflow bounds before each multiply. If `getSTNumberSwitchover()` is enabled — a feature flag that activates a newer arithmetic engine — it delegates to `XRPAmount` or `MPTAmount` conversion via `Number`, which handles the normalization internally.

For **IOU**, it nudges the mantissa up (multiply by 10, decrement offset) while `mValue < cMinValue`, and down (divide by 10, increment offset) while `mValue > cMaxValue`. If the result would underflow the minimum offset it collapses to canonical zero (`mValue = 0, mOffset = −100`). Overflow throws `std::runtime_error("value overflow")`. This normalization enforces the 16-significant-digit precision that the XRPL floating-point format guarantees.

When `getSTNumberSwitchover()` is enabled, IOU canonicalization delegates to `iou()`, which converts to `IOUAmount` and back, using that class's normalizer.

---

## Arithmetic: Addition and Subtraction

`operator+(v1, v2)` begins by calling `areComparable(v1, v2)`. This uses a `std::visit` over the `Asset` variant with compile-time type-trait branches (`is_issue_v`, `is_mptissue_v`). Two amounts are comparable only if they share the same asset type and the same currency/issuer identity. Adding incompatible amounts throws immediately.

For **XRP**: the function extracts `int64_t` drop counts via `getSNValue()` (which validates `native()` is true and that the exponent is zero), adds them, and builds a new `STAmount`.

For **MPT**: same pattern via `getMPTValue()` and the `MPTAmount` value type.

For **IOU** (legacy path): the function aligns exponents by dividing the mantissa of the lower-offset operand, losing the least significant digits. The comment explicitly acknowledges that "this addition cannot overflow an `int64_t`" — though the resulting `STAmount` can overflow after canonicalization. Amounts within `[−10, +10]` after alignment are rounded to zero and returned as the zero of the v1 currency. The switchover path delegates to `IOUAmount::operator+`, which uses the `Number` engine.

Subtraction is simply `v1 + (−v2)`, and the unary negation operator flips `mIsNegative` (guarding against negating zero).

---

## Multiply and Divide

### Core Helpers: `muldiv` and `muldiv_round`

Both use `boost::multiprecision::uint128_t` to compute `(a × b) / c` without overflow. These are private static helpers — the XRPL ledger cannot use 128-bit native integers portably, so Boost provides the intermediate precision. `muldiv_round` adds a rounding bias before the division.

### `multiply()`

For XRP × XRP, the function guards against overflow using the observation that if `min(a,b) > sqrt(cMaxNative)` or `((max >> 32) * min) > cMaxNative / 2^32`, the product must overflow. This avoids a 128-bit multiply for the common all-native case.

For IOU × IOU (or mixed IOU), both mantissas are normalized to the canonical [10^15, 10^16) window (integral amounts are scaled up), then `muldiv(v1_mantissa, v2_mantissa, 10^14)` produces a result in the [10^16, 10^18) range. Adding 7 before division implements banker's-style rounding in the legacy path. The combined offset is `offset1 + offset2 + 14`. Canonicalization then squeezes the mantissa back into the canonical window.

### `divide()`

Same structure but the formula is `muldiv(num, 10^17, den)`, producing a result in [10^15, 10^17), with offset `numOffset − denOffset − 17`. The `+5` rounding bias handles the remainder in the legacy path.

### Rounding Variants

A key design complexity is the existence of **two rounding modes** for multiply and divide:

- **`mulRound` / `divRound`** (legacy): use `canonicalizeRound()`, which rounds up only when the fractional part is ≥ 0.1. This surprising behavior is preserved for backward compatibility because cross-currency AMM-style calculations have it baked in.
- **`mulRoundStrict` / `divRoundStrict`**: use `canonicalizeRoundStrict()`, which tracks the actual remainder through all intermediate division steps and rounds correctly. These also propagate the rounding mode to `Number` via `NumberRoundModeGuard`, so that the `canonicalize()` call on the new `STAmount` uses consistent rounding.

The shared template `mulRoundImpl<CanonicalizeFunc, MightSaveRound>` captures this duality: both the round function and the RAII guard type are template parameters. `DontAffectNumberRoundMode` is a no-op guard used in the non-strict variants to avoid disturbing the thread-local `Number` rounding mode.

---

## Offer Rate Encoding: `getRate()`

`getRate(offerOut, offerIn)` converts an order book offer into a 64-bit sort key. It divides `offerIn` by `offerOut` using the `noIssue()` pseudo-asset, then packs the result as `(exponent + 100) << 56 | mantissa`. Because the canonical mantissa window is [10^15, 10^16), the mantissa fits in 56 bits. Lower sort values represent better rates for takers. Offers that overflow or divide to zero return 0 (treated as worthless). The constant `uRateOne` is initialized at static time via `getRate(STAmount(1), STAmount(1))` and represents a 1:1 rate.

---

## JSON Parsing: `amountFromJson()`

This function handles XRP Ledger's flexible JSON amount encoding across four formats: JSON object (`{value, currency, issuer}` or `{value, mpt_issuance_id}`), JSON array, delimiter-split string, and bare numeric. Asset type is inferred from which fields are present. Fractional specifications are rejected for XRP and MPT since those are integer-only types. `amountFromJsonNoThrow()` wraps this with a catch-all and returns `false` on failure, logging the error.

---

## Safety Predicates: `canAdd` and `canSubtract`

These functions answer "would this arithmetic operation be safe?" without performing it. They are used by the AMM and vault subsystems to validate operations before executing them.

For XRP and MPT, the checks are straightforward integer overflow/underflow bounds. For IOU, `canAdd` uses a round-trip relative error test: it checks whether `(a − b) + b ≈ a` and `(b − a) + a ≈ b` within a `10^−4` tolerance. This catches cases where exponent differences would lose too many significant digits. IOU subtraction is always considered safe since negative IOU balances are valid.

---

## Feature-Gated Behavior

Several code paths branch on runtime feature flags:

- `getSTNumberSwitchover()`: a thread-local flag (used in test contexts and via amendment) that switches arithmetic from the legacy mantissa-shifting code to the `Number` / `IOUAmount` engine.
- `featureSingleAssetVault` and `featureLendingProtocol`: checked in `operator=(Number const&)` to decide whether to use the new `fromNumber()` path or the older direct mantissa assignment.

This layering means the same source file simultaneously supports the historical ledger semantics (for replay) and the newer, more numerically sound code paths (for new transaction types).