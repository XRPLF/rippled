# `include/xrpl/tx/paths/Offer.h` — `TOffer`: CLOB Offer Wrapper for Path Engine

## Role in the System

`TOffer` is the canonical representation of a Central Limit Order Book (CLOB) offer during path-finding and payment execution in the XRPL. It bridges the raw ledger object (an `SLE` — Shared Ledger Entry) and the generic payment-step machinery in `BookStep`, giving the engine a clean, typed interface for reading offer amounts, applying partial fills, and routing funds. The template parameters `TIn` and `TOut` — constrained by the `StepAmount` concept to `XRPAmount`, `IOUAmount`, or `MPTAmount` — let a single class body handle every combination of asset types the ledger supports, while allowing compile-time branches where the serialization path differs.

Its sibling class `AMMOffer` (in `AMMOffer.h`) mirrors the exact same public interface but wraps an AMM liquidity pool instead of a ledger entry. Both types are consumed interchangeably by the generic `BookStep` template, which is the primary reason `TOffer`'s interface is designed as it is: the template parameters and method signatures are a deliberate duck-typing contract.

## Construction and Data Layout

The constructor takes a shared pointer to an `SLE` and a pre-computed `Quality`. Amounts are extracted immediately from the ledger entry's `sfTakerPays` (input) and `sfTakerGets` (output) fields and converted to the strongly-typed `TIn`/`TOut` values via `toAmount<T>()`. The asset identities (`assetIn_`, `assetOut_`) are captured at this point from the `STAmount::asset()` accessors. After construction the object is self-contained; it no longer reads from the ledger until `consume()` writes back to it.

## Quality Immutability — An Explicit Business Rule

The inline `quality()` accessor returns `m_quality`, which is documented with care: quality is computed at the moment an offer is placed and **never recalculated**, even as the offer is partially filled. This is a deliberate ledger invariant. Partial fills only reduce the absolute amounts; the exchange rate stays fixed. This prevents accumulated rounding drift from silently worsening the effective rate for later takers and makes the order-book sort order stable. The `Quality` type stores the rate internally as an inverted ratio (input/output) so that ascending integer order corresponds to descending quality — a detail the path engine exploits when iterating the book.

## Partial Consumption

`consume()` is the mutation point. It decrements `m_amounts` by the consumed pair, calls `setFieldAmounts()` to write the updated values back into the `SLE` fields, and then calls `view.update(m_entry)` to stage the change in the `ApplyView`. The method throws `std::logic_error` (via `Throw<>`) if the caller tries to consume more than available — this is a hard invariant since the calling code in `BookStep` is supposed to clamp consumption first using `limitOut`/`limitIn`. `fully_consumed()` returns true the moment either side touches zero, handling the normal post-fill case where the offer must be removed.

`setFieldAmounts()` uses `if constexpr` to branch on whether the type is `XRPAmount` (calls `toSTAmount(amount)` without an asset context) or IOU/MPT (calls `toSTAmount(amount, asset_)` with the asset). This is a compile-time dispatching strategy that avoids runtime polymorphism and ensures type safety while sharing the same function body.

## Limiting Logic and Amendment Guards

`limitOut()` always delegates to `Quality::ceil_out_strict()`, which uses a tighter rounding algorithm than the older `ceil_out`. `limitIn()` conditionally uses `ceil_in_strict()` only when the `fixReducedOffersV2` amendment is active, falling back to `ceil_in` otherwise. This guarded behavior preserves transaction-outcome compatibility: the stricter ceiling removes rounding slop that caused tiny residual amounts to keep offers alive longer than they should be, but because it changes observable outcomes it had to be deployed behind an amendment. The asymmetry between the two directions — `limitOut` always strict, `limitIn` amendment-gated — reflects the order in which these fixes were deployed on the network.

## Transfer Fee Semantics: CLOB vs AMM

The `send()` static method forwards to `accountSend(...)` with `WaiveTransferFee::No`, meaning CLOB offer owners **pay** the issuer's transfer fee on the output asset. This is contrast to `AMMOffer::send()`, which passes `WaiveTransferFee::Yes` to waive the fee, because AMM pools operate differently under the protocol rules. The static `adjustRates()` method reinforces this: `TOffer` returns both in-rate and out-rate unchanged, while `AMMOffer::adjustRates()` zeroes the out-rate to `QUALITY_ONE`, reflecting that the AMM pool itself absorbs no transfer fee on Payment transactions.

## Funding Check

`isFunded()` returns `true` only when the offer owner's `AccountID` equals the issuer of the output asset AND the output asset is an `Issue` (not MPT or XRP). An issuer can always deliver their own IOU without holding a balance, so the path engine can skip the real-balance check for those offers. For MPT and XRP offers this always returns `false` and the engine verifies actual owner funds through the usual `ownerFunds_` mechanism in `TOfferStreamBase`.

## Invariant Check

`checkInvariant()` is gated on the `fixAMMv1_3` amendment and verifies that the consumed amounts do not exceed `m_amounts`. While this check is logically a no-op for well-behaved callers (the `consume()` method already enforces this), having it as a separate call allows `BookStep` to invoke it in a uniform way across both `TOffer` and `AMMOffer` — the AMM version performs a far more expensive constant-product pool invariant check. The `// LCOV_EXCL_START` marker indicates this branch is considered unreachable under normal test coverage, existing purely as a defense-in-depth guard.

## Relationship to `TOfferStreamBase`

`TOfferStreamBase<TIn, TOut>` (in `OfferStream.h`) holds a `TOffer<TIn, TOut>` by value and advances through the order book one offer at a time. Each call to `step()` on the stream loads a new `SLE` via `BookTip`, constructs a fresh `TOffer`, and returns it via the `tip()` accessor. The stream owns the lifecycle; `TOffer` is a value type that is move-assigned into `offer_` each step and whose `consume()` writes go directly into the stream's `ApplyView`. This design keeps the offer class stateless beyond its own fields, making it straightforward to reason about correctness even in multi-step payment paths.