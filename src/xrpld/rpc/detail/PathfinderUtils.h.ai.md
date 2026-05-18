# PathfinderUtils.h

This header provides three small inline utilities that implement the "send/receive all available liquidity" semantic for the XRPL pathfinding subsystem. It lives in `src/xrpld/rpc/detail/` alongside `Pathfinder.cpp` and `PathRequest.cpp`, both of which include it directly.

## The "Convert All" Sentinel Pattern

The XRPL path-finding RPC supports a mode where a sender wants to drain the maximum available liquidity through discovered paths rather than satisfy a fixed destination amount. This is signaled at the RPC layer by passing a destination amount equal to the largest representable value for the given asset type — a sentinel value rather than a separate flag or `std::optional`.

`largestAmount()` constructs this sentinel. It dispatches on the asset type via the variant visitor pattern on `STAmount::asset()`:

- **XRP**: returns `INITIAL_XRP` (100,000,000,000 XRP = 10^17 drops, the entire initial supply — the largest valid XRP amount by protocol convention).
- **IOU**: returns an `STAmount` with mantissa `cMaxValue` (9,999,999,999,999,999) and exponent `cMaxOffset` (80). These are the boundary values of XRPL's floating-point IOU encoding, yielding approximately 10^96 — effectively unbounded for any realistic liquidity.
- **MPT (Multi-Purpose Token)**: returns an `STAmount` with mantissa `maxMPTokenAmount` (0x7FFF'FFFF'FFFF'FFFF = 2^63−1) and exponent 0. MPTs use integer rather than floating-point encoding, so `cMaxValue`/`cMaxOffset` are inapplicable; the maximum is the largest signed 63-bit integer the protocol permits.

The three-branch design reflects that XRPL now has three fundamentally different amount representations (native drops, IOU floating-point, MPT integer), each with its own maximum. Using a single `largestAmount` abstraction prevents callers from having to know which representation applies.

## `convertAmount` and `convertAllCheck`

`convertAmount(amt, all)` is the entry point for consumer code: when `all` is `false`, the original amount passes through unchanged; when `true`, it delegates to `largestAmount`. This is the function called by `PathRequest::doUpdate()` and `Pathfinder::computePathRanks()` when computing the destination amount to feed into `RippleCalc`.

`convertAllCheck(a)` is the inverse detection function. It compares an amount against `largestAmount(a)` to determine whether it was already the sentinel, returning `true` if so. In `Pathfinder`'s constructor, `convert_all_` is initialized as:

```cpp
convert_all_(convertAllCheck(mDstAmount))
```

This boolean then controls two downstream behaviours: `convertAmount` selects either the real or sentinel amount, and — critically — `partialPaymentAllowed` is set to `true` in the `RippleCalc` input when `convert_all_` is active. Requiring a partial payment is what makes the pathfinder search for maximum liquidity rather than insisting on an exact fill. During path ranking, `largestAmount` is used again as the minimum acceptable destination amount, which biases selection toward high-liquidity paths.

## Design Tradeoffs

The sentinel-value approach conflates the amount field with a semantic flag, which is non-obvious. The upside is seamless compatibility with `STAmount`-typed interfaces throughout the pathfinding stack — no overloads, no optionals, no protocol changes needed. The downside is that a legitimate request for the exact maximum representable amount is indistinguishable from the "convert all" intent; in practice this is not a problem because no real payment would ever specify `INITIAL_XRP` or IOU `cMaxValue` as an exact destination.

The functions are all `inline` and header-only, appropriate for their trivial size and the fact that they are called from only two translation units in the same directory.