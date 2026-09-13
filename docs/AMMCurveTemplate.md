# AMM Curve Implementation Template

Guide for adding a new curve type to the XRPL pluggable AMM curve framework.

## Architecture Overview

Each token pair can have **multiple AMM pools**, one per curve type. The keylet
hash includes `curveType`, so `RLUSD/USD ConstantProduct` and `RLUSD/USD
StableSwap` are separate ledger entries with separate pseudo-accounts and LP
tokens. BookStep automatically routes through the deepest-liquidity pool.

### Key Components

| Component             | File                                                | Purpose                                 |
| --------------------- | --------------------------------------------------- | --------------------------------------- |
| CurveInterface        | `include/xrpl/ledger/helpers/AMMCurve.h`            | Abstract base for all curves            |
| Curve implementations | `src/libxrpl/ledger/helpers/AMMCurve.cpp`           | CP, CL, StableSwap, Weighted            |
| Tick math             | `src/libxrpl/ledger/helpers/AMMTickMath.cpp`        | CL tick/sqrt price conversions          |
| Fee collection        | `src/libxrpl/tx/transactors/dex/AMMCollectFees.cpp` | CL position fee harvesting              |
| Multi-curve routing   | `src/libxrpl/tx/paths/BookStep.cpp`                 | Picks best pool per pair                |
| Keylet hashing        | `src/libxrpl/protocol/Indexes.cpp`                  | `amm(asset1, asset2, curveType)`        |
| LP token identity     | `src/libxrpl/protocol/AMMCore.cpp`                  | `ammLPTCurrency(cur1, cur2, curveType)` |

## Step 1: Define the Curve Type

Add to `include/xrpl/protocol/AMMCore.h`:

```cpp
enum CurveType : std::uint8_t
{
    ctCONSTANT_PRODUCT = 0,
    ctCONCENTRATED_LIQUIDITY = 1,
    ctSTABLE_SWAP = 2,
    ctWEIGHTED = 3,
    ctYOUR_CURVE = N,  // next available ID
};
```

## Step 2: Define SFields for Curve Parameters

Add to `include/xrpl/protocol/detail/sfields.macro`:

```cpp
// Use the appropriate type (UINT8, UINT16, UINT32, UINT64, UINT256, AMOUNT, etc.)
// Check existing field codes to avoid collisions.
TYPED_SFIELD(sfYourParam, UINT32, <next_available_code>)
```

## Step 3: Implement the CurveInterface

Add a new class in `src/libxrpl/ledger/helpers/AMMCurve.cpp`:

```cpp
class YourCurve final : public CurveInterface
{
public:
    Expected<STAmount, TER>
    swapIn(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetIn,
        std::uint16_t tfee,
        STObject const* curveParams) const override
    {
        if (!curveParams)
            return Unexpected(tecINTERNAL);

        auto const param = curveParams->getFieldU32(sfYourParam);

        auto const f = feeMult(tfee);  // fee multiplier (1 - fee)
        Number const x = poolIn;       // STAmount -> Number via implicit conversion
        Number const y = poolOut;
        Number const dx = Number(assetIn) * f;

        // --- YOUR INVARIANT MATH HERE ---
        // Compute output amount `dy` from your invariant
        // F(x, y) = k  =>  F(x + dx, y - dy) = k  =>  solve for dy
        Number const dy = /* ... */;

        if (dy <= Number{0})
            return Unexpected(tecAMM_FAILED);

        // Round output DOWN (favorable to pool)
        NumberRoundModeGuard const mg(Number::downward);
        return toSTAmount(poolOut.issue(), dy);
    }

    Expected<STAmount, TER>
    swapOut(
        STAmount const& poolIn,
        STAmount const& poolOut,
        STAmount const& assetOut,
        std::uint16_t tfee,
        STObject const* curveParams) const override
    {
        if (!curveParams)
            return Unexpected(tecINTERNAL);

        auto const param = curveParams->getFieldU32(sfYourParam);

        auto const f = feeMult(tfee);
        Number const x = poolIn;
        Number const y = poolOut;

        // --- YOUR INVARIANT MATH (INVERSE) ---
        // Given desired output, compute required input
        // F(x, y) = k  =>  F(x + dx, y - assetOut) = k  =>  solve for dx
        Number const dx = /* ... */ / f;

        if (dx <= Number{0})
            return Unexpected(tecAMM_FAILED);

        // Round input UP (favorable to pool)
        NumberRoundModeGuard const mg(Number::upward);
        return toSTAmount(poolIn.issue(), dx);
    }

    Expected<Number, TER>
    spotPrice(
        STAmount const& poolIn,
        STAmount const& poolOut,
        std::uint16_t tfee,
        STObject const* curveParams) const override
    {
        if (!curveParams)
            return Unexpected(tecINTERNAL);

        auto const param = curveParams->getFieldU32(sfYourParam);
        auto const f = feeMult(tfee);

        // Marginal price: -dF/dx / dF/dy evaluated at current reserves
        // Divided by (1 - fee) for the taker-facing price
        Number const price = /* partial derivatives of your invariant */;
        return price / f;
    }

    TER
    validateParams(STObject const& curveParams) const override
    {
        // Validate curve-specific parameters at pool creation
        if (!curveParams.isFieldPresent(sfYourParam))
            return temMALFORMED;

        auto const param = curveParams.getFieldU32(sfYourParam);
        if (param < MIN_YOUR_PARAM || param > MAX_YOUR_PARAM)
            return temMALFORMED;

        return tesSUCCESS;
    }

    Expected<STAmount, TER>
    initialLPTokens(
        STAmount const& asset1,
        STAmount const& asset2,
        Issue const& lptIssue,
        STObject const* curveParams) const override
    {
        if (!curveParams)
            return Unexpected(tecINTERNAL);

        // Compute initial LP token amount from deposits
        // For many curves: geometric mean sqrt(asset1 * asset2)
        // Or curve-specific: D for StableSwap, weighted geometric mean, etc.
        Number const lp = /* ... */;
        return toSTAmount(lptIssue, lp);
    }
};
```

## Step 4: Register the Singleton and Dispatch

In `AMMCurve.cpp`, add the singleton and switch case:

```cpp
// At file scope (inside anonymous namespace)
static YourCurve const yourCurve_;

// In getCurve():
case ctYOUR_CURVE:
    if (rules.enabled(featureAMMCurves))
        return &yourCurve_;
    return nullptr;
```

## Step 5: Update AMMCreate Validation

In `src/libxrpl/tx/transactors/dex/AMMCreate.cpp`:

1. Update the max curve type check: `if (curveType > ctYOUR_CURVE)`
2. Add params setup in the `applyGuts` section:

```cpp
else if (curveType == ctYOUR_CURVE)
{
    ammSle->setFieldU32(
        sfYourParam, ctx_.tx.getFieldU32(sfYourParam));
}
```

## Step 6: Multi-Curve Routing (BookStep)

BookStep automatically discovers and routes through the best pool for each
token pair. When a new curve type is added, update the loop upper bound in
`src/libxrpl/tx/paths/BookStep.cpp`:

```cpp
for (std::uint8_t ct = 0; ct <= ctYOUR_CURVE; ++ct)
{
    auto const ammSle = ctx.view.read(keylet::amm(in, out, ct));
    if (!ammSle || ammSle->getFieldAmount(sfLPTokenBalance) == beast::zero)
        continue;
    if (!bestAmm ||
        ammSle->getFieldAmount(sfLPTokenBalance) >
            bestAmm->getFieldAmount(sfLPTokenBalance))
        bestAmm = ammSle;
}
```

The pool with the highest LP token balance wins. Curve-specific swap dispatch
happens automatically via `getCurve()` in `AMMLiquidity`/`AMMOffer`.

## Step 7: Keylet and LP Token Identity

Each curve type for the same token pair gets a unique ledger key and LP token:

- **Keylet**: `keylet::amm(asset1, asset2, curveType)` hashes `curveType` into
  the AMM's ledger key (when curveType != 0, for backward compatibility)
- **LP token**: `ammLPTCurrency(cur1, cur2, curveType)` hashes `curveType` into
  the LP token currency code

No changes needed here when adding a new curve — the default parameter
propagates automatically.

## Concentrated Liquidity Extras

The CL curve type uses additional infrastructure not needed by other curves:

- **AMMTickMath** (`AMMTickMath.h/cpp`): `tickToSqrtPrice()`, `sqrtPriceToTick()`,
  `isValidTick()` for tick-based price representation
- **AMMCollectFees** (`AMMCollectFees.h/cpp`): Transactor for position owners to
  collect accumulated swap fees using the Uniswap V3 fee growth formula
- **Ledger entries**: `ltAMM_POSITION` (per-user tick range + liquidity) and
  `ltAMM_TICK` (per-tick fee growth and liquidity tracking)
- **SFields**: `sfFeeGrowthGlobal0/1`, `sfFeeGrowthOutside0/1`,
  `sfFeeGrowthInsideLast0/1` (UINT256, Q128.128 fixed-point),
  `sfActiveLiquidity`, `sfPositionLiquidity`, `sfLiquidityGross/Net` (UINT64)

## Math Utilities Available

- `Number`: arbitrary-precision decimal arithmetic (see `include/xrpl/basics/Number.h`)
- `power(f, n)`: f^n (integer exponent)
- `power(f, n, d)`: f^(n/d) (rational exponent)
- `root(f, d)`: f^(1/d)
- `root2(f)`: sqrt(f)
- `feeMult(tfee)`: returns `1 - fee` as Number
- `feeMultHalf(tfee)`: returns `1 - fee/2` as Number
- `toSTAmount(issue, number)`: convert Number to STAmount
- `NumberRoundModeGuard`: RAII guard for rounding direction

## Rounding Convention

- `swapIn` output: round DOWN (pool keeps the rounding dust)
- `swapOut` input: round UP (taker pays the rounding dust)
- Use `NumberRoundModeGuard` to set the rounding mode before `toSTAmount()`

## Testing

Add tests in `src/test/app/AMMCurves_test.cpp`:

1. `swapIn` and `swapOut` are inverses (within rounding tolerance)
2. Invariant is preserved: `F(reserves_new) >= F(reserves_old)` after every swap
3. `spotPrice` matches actual swap rate at infinitesimal amounts
4. Edge cases: zero input, max input, min reserves
5. Parameter validation: `validateParams` rejects out-of-range values
6. Integration: create pool, deposit, swap, withdraw full cycle

E2E tests go in `src/test/app/AMMCurvesE2E_test.cpp` for full transaction
lifecycle tests (AMMCreate with curve params, swap through payment engine,
deposit/withdraw).
