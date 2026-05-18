# `include/xrpl/protocol/Rate.h` — Transfer Rate Type and Arithmetic

## Purpose

`Rate.h` defines the `Rate` struct and its associated arithmetic free functions, which together form the protocol-level abstraction for XRPL transfer fees. Whenever an issuer charges a percentage on IOU transfers, or an NFT carries a creator royalty, the engine expresses and applies that fee through this type. The header is thin by design: the struct itself is barely more than a tagged integer, while the computation-heavy work lives in the matching implementation file `Rate2.cpp`.

## Encoding Convention

Transfer rates in the XRPL protocol are stored as fractions of one billion. A raw value of `1,000,000,000` means exactly 1:1 — no fee. A value of `1,010,000,000` means the sender must deliver 1.01 units for every 1 unit the recipient receives, i.e., a 1% fee. This scale matches the `QUALITY_ONE` constant defined in `Quality.h` (`#define QUALITY_ONE 1'000'000'000`), which ties transfer rates directly to the ledger's quality/price representation. Rates are read straight from `sfTransferRate` on account ledger objects; the `transferRate()` helper in `AccountRootHelpers.cpp` returns `parityRate` (the `Rate{QUALITY_ONE}` sentinel) when the field is absent, meaning most accounts simply have no fee.

The globally defined `parityRate` constant is the critical sentinel. Because it indicates a 1:1 exchange, every arithmetic function in `Rate2.cpp` short-circuits immediately when it detects this value — returning the input `STAmount` unchanged — which avoids the more expensive `STAmount` multiply/divide path for the common case of fee-free transfers.

## The `Rate` Struct

`Rate` wraps a single `std::uint32_t` and inherits from `boost::totally_ordered<Rate>`. This CRTP mixin generates `!=`, `>`, `<=`, and `>=` from just the two manually provided operators (`==` and `<`), keeping the header concise while delivering a fully ordered type. The constructor is `explicit` to prevent accidental implicit conversion from raw integers — a meaningful guard given that rate values look like ordinary large numbers and could be confused with amounts. The default constructor is deleted because a `Rate` with an unspecified value is meaningless: zero would be nonsensical (it fails the `XRPL_ASSERT(rate.value)` guards in all arithmetic functions), and leaving it uninitialised would be silently dangerous.

## Arithmetic Interface

Six free functions apply a `Rate` to an `STAmount`:

- `multiply` / `divide` — exact arithmetic (no rounding control)
- `multiplyRound(amount, rate, roundUp)` / `divideRound(amount, rate, roundUp)` — controlled rounding, preserving the asset type of the input amount
- `multiplyRound(amount, rate, asset, roundUp)` / `divideRound(amount, rate, asset, roundUp)` — controlled rounding with an explicit output asset, used when the result asset differs from the input (e.g., offer crossing through a gateway)

The two-overload pattern exists because offer crossing in `OfferCreate.cpp` must compute fees where the input and output are different currencies, whereas simple transfer-fee accounting (IOU payment routing in `TokenHelpers.cpp`) always works in the same currency. The implementation converts a `Rate` into an `STAmount` via `detail::as_amount()`, which constructs `{noIssue(), rate.value, -9, false}` — expressing the billion-scale integer as a decimal with exponent −9 so that `STAmount`'s existing multiply/divide infrastructure handles the fixed-point arithmetic correctly. This reuse of `STAmount` arithmetic is intentional: it keeps fee calculation consistent with the same precision model used everywhere else in the ledger engine.

## NFT Transfer Fees — `nft::transferFeeAsRate`

NFT transfer fees are stored in a separate field as a `uint16_t` in units of basis points (0–50,000 representing 0%–50%). `nft::transferFeeAsRate()` converts this scale by multiplying by 10,000:

```
Rate{static_cast<std::uint32_t>(fee) * 10'000}
```

A maximum NFT fee of 50,000 basis points (50%) becomes `500,000,000`, well within `uint32_t` range and below `QUALITY_ONE`. The result is then used directly with `multiply()` in `NFTokenAcceptOffer.cpp` to compute the creator's royalty cut before paying the seller. This conversion is intentionally placed in the `nft` sub-namespace to make the unit difference explicit at the call site — callers working with standard IOU transfer rates (already billion-scaled) should never call this function.

## Design Notes

The choice to make `Rate` a struct with a public `value` member rather than a full encapsulating class reflects the XRPL codebase's philosophy of transparency for protocol-level types: the raw value is the on-ledger representation, and code that checks `rate.value != QUALITY_ONE` (as in `OfferCreate.cpp`) is directly comparing against the wire format. This avoids indirection while the `explicit` constructor and deleted default constructor still prevent the most common misuse. The `boost::totally_ordered` mixin and the stream operator make the type behave naturally in sorted containers and log output without additional boilerplate.