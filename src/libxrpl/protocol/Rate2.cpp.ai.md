# `src/libxrpl/protocol/Rate2.cpp` — Transfer Rate Arithmetic Implementation

## Role in the System

This file is the implementation counterpart to `include/xrpl/protocol/Rate.h`. It provides the concrete math for applying XRPL transfer rates — the per-transfer fees charged by IOU issuers and NFT creators — to `STAmount` values. The header declares the interface and the `parityRate` extern; this file defines both, making it the single translation unit responsible for fee application across the payment engine, offer-crossing logic, and NFT royalty calculation.

## The `parityRate` Constant

```cpp
Rate const parityRate(QUALITY_ONE);
```

`QUALITY_ONE` is `1'000'000'000` (10⁹), the fixed-point identity value for the ledger's rate encoding. A `Rate` equal to this value means 1:1 — the sender pays exactly what the recipient receives, no fee. Defining `parityRate` as a file-scope constant rather than recomputing it inline lets every arithmetic function short-circuit with a single equality check before entering the more expensive `STAmount` arithmetic path.

## The `detail::as_amount` Bridge

The central design question in this file is: how do you multiply or divide an `STAmount` by a dimensionless ratio stored as a billion-scale integer? The answer is `detail::as_amount()`:

```cpp
STAmount as_amount(Rate const& rate)
{
    return {noIssue(), rate.value, -9, false};
}
```

This constructs an `STAmount` representing `rate.value × 10⁻⁹` — a dimensionless decimal. For example, a `rate.value` of `1,010,000,000` (1% fee) becomes the `STAmount` `1.010000000`. With this encoding, the existing `STAmount::multiply` and `STAmount::divide` infrastructure handles all the fixed-point precision correctly without any custom arithmetic. The `noIssue()` sentinel signals that the value carries no currency identity, which is correct since a rate is dimensionless. This approach keeps fee computation consistent with the same precision model used everywhere else in the ledger engine.

## Arithmetic Functions and the Parity Short-Circuit

All six arithmetic functions share the same structural pattern: assert nonzero rate, short-circuit on parity, then delegate to the underlying `STAmount` arithmetic:

```cpp
if (rate == parityRate)
    return amount;
return multiply(amount, detail::as_amount(rate), amount.asset());
```

The parity short-circuit is a meaningful performance optimisation. The vast majority of accounts have no transfer fee (the engine returns `parityRate` when `sfTransferRate` is absent), so most calls in payment routing never reach the `STAmount` arithmetic path at all.

The two overloads of `multiplyRound` and `divideRound` serve distinct use cases. The single-asset overload (taking only `amount`, `rate`, and `roundUp`) preserves the currency of the input amount — used when fee calculation stays in one currency, as in IOU payment routing. The dual-asset overload (additionally taking an explicit `Asset`) specifies a different output currency, used during offer crossing where the input and output are denominated in different assets. Both delegate to `mulRound`/`divRound` from `STAmount.h`.

## NFT Transfer Fees — `nft::transferFeeAsRate`

NFT royalties are encoded in the ledger as a `uint16_t` in basis points (hundredths of a percent, 0–50,000). The billion-scale `Rate` encoding expects units where 10⁹ equals 100%, so the conversion factor is 10,000:

```cpp
return Rate{static_cast<std::uint32_t>(fee) * 10000};
```

A fee of 50,000 basis points (the protocol maximum of 50%) becomes `500,000,000`, safely within `uint32_t` range and below `QUALITY_ONE`. The cast to `uint32_t` before multiplication prevents overflow since `50000 × 10000 = 500,000,000`, which fits in 32 bits. This function lives in the `nft` sub-namespace to make the unit distinction explicit: code handling ordinary IOU transfer rates (already billion-scaled from `sfTransferRate`) should never call it.

## Validation and Invariants

Every arithmetic function opens with `XRPL_ASSERT(rate.value, ...)`, catching zero rates at the debug boundary. A zero `Rate` is semantically undefined (it would represent an infinitely large fee or division by zero) and should never reach this layer from well-formed ledger data. The assertion fires in debug builds; in production, a zero rate would silently produce garbage arithmetic, making the guard critical for catching upstream construction errors during development. `transferFeeAsRate` carries no such guard — its input range is enforced by transaction validation before the value ever reaches the ledger.