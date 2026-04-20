// Fee collection for concentrated liquidity positions. Fee growth accounting
// pattern (global/outside/inside) from Discussion #427 by Roman Thpt (@RomThpt).

#include <xrpl/tx/transactors/dex/AMMCollectFees.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

static uint256
uint256Sub(uint256 const& a, uint256 const& b)
{
    auto neg = ~b;
    ++neg;
    return a + neg;
}

static uint256
feeGrowthBelow(
    std::int32_t currentTick,
    std::int32_t tick,
    uint256 const& feeGrowthGlobal,
    uint256 const& feeGrowthOutside)
{
    if (currentTick >= tick)
        return feeGrowthOutside;
    return uint256Sub(feeGrowthGlobal, feeGrowthOutside);
}

static uint256
feeGrowthAbove(
    std::int32_t currentTick,
    std::int32_t tick,
    uint256 const& feeGrowthGlobal,
    uint256 const& feeGrowthOutside)
{
    if (currentTick < tick)
        return feeGrowthOutside;
    return uint256Sub(feeGrowthGlobal, feeGrowthOutside);
}

bool
AMMCollectFees::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureAMMCurves);
}

NotTEC
AMMCollectFees::preflight(PreflightContext const& ctx)
{
    if (!ctx.tx.isFieldPresent(sfNFTokenID))
        return temMALFORMED;
    if (!ctx.tx.isFieldPresent(sfAsset) || !ctx.tx.isFieldPresent(sfAsset2))
        return temMALFORMED;
    return tesSUCCESS;
}

TER
AMMCollectFees::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammKeylet = keylet::amm(asset, asset2, curveType);
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
        return terNO_AMM;

    if (getCurveType(*ammSle) != CtConcentratedLiquidity)
        return tecAMM_FAILED;

    return tesSUCCESS;
}

TER
AMMCollectFees::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const account = ctx_.tx[sfAccount];
    auto const nfTokenID = ctx_.tx[sfNFTokenID];
    auto const asset = ctx_.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx_.tx[sfAsset2].get<Issue>();
    auto const curveType = ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType)
                                                               : std::uint8_t(CtConstantProduct);

    auto ammSle = sb.peek(keylet::amm(asset, asset2, curveType));
    if (!ammSle)
        return tecINTERNAL;

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const ammID = ammSle->key();
    auto const currentTick = ammSle->getFieldI32(sfCurrentTick);
    auto const feeGrowthGlobal0 = ammSle->getFieldH256(sfFeeGrowthGlobal0);
    auto const feeGrowthGlobal1 = ammSle->getFieldH256(sfFeeGrowthGlobal1);

    // 1. Find position by scanning owner directory for matching NFTokenID
    Keylet posKeylet{ltAMM_POSITION, uint256{}};
    bool found = false;
    forEachItem(sb, account, [&](std::shared_ptr<SLE const> const& item) {
        if (!found && item->getType() == ltAMM_POSITION && item->getFieldH256(sfAMMID) == ammID &&
            item->isFieldPresent(sfNFTokenID) && item->getFieldH256(sfNFTokenID) == nfTokenID)
        {
            posKeylet = Keylet{ltAMM_POSITION, item->key()};
            found = true;
        }
    });
    if (!found)
        return tecNO_ENTRY;

    auto posSle = sb.peek(posKeylet);
    if (!posSle)
        return tecINTERNAL;

    if ((*posSle)[sfAccount] != account)
        return tecNO_PERMISSION;

    auto const tickLower = posSle->getFieldI32(sfTickLower);
    auto const tickUpper = posSle->getFieldI32(sfTickUpper);
    auto const posLiquidity = posSle->getFieldU64(sfPositionLiquidity);
    auto const insideLast0 = posSle->getFieldH256(sfFeeGrowthInsideLast0);
    auto const insideLast1 = posSle->getFieldH256(sfFeeGrowthInsideLast1);

    // 2. Look up tick entries
    auto const lowerTickSle = sb.read(keylet::ammTick(ammID, tickLower));
    auto const upperTickSle = sb.read(keylet::ammTick(ammID, tickUpper));
    if (!lowerTickSle || !upperTickSle)
        return tecINTERNAL;

    // 3. Compute feeGrowthInside using Uniswap V3 formula:
    //    inside = global - below(lower) - above(upper)
    auto const inside0 = uint256Sub(
        uint256Sub(
            feeGrowthGlobal0,
            feeGrowthBelow(
                currentTick,
                tickLower,
                feeGrowthGlobal0,
                lowerTickSle->getFieldH256(sfFeeGrowthOutside0))),
        feeGrowthAbove(
            currentTick,
            tickUpper,
            feeGrowthGlobal0,
            upperTickSle->getFieldH256(sfFeeGrowthOutside0)));

    auto const inside1 = uint256Sub(
        uint256Sub(
            feeGrowthGlobal1,
            feeGrowthBelow(
                currentTick,
                tickLower,
                feeGrowthGlobal1,
                lowerTickSle->getFieldH256(sfFeeGrowthOutside1))),
        feeGrowthAbove(
            currentTick,
            tickUpper,
            feeGrowthGlobal1,
            upperTickSle->getFieldH256(sfFeeGrowthOutside1)));

    // 4. Calculate fees owed = liquidity * (inside - insideLast)
    //    Fee growth is Q128.128 fixed-point stored in uint256.
    //    Big-endian: bytes 0-15 = integer part, bytes 16-31 = fractional.
    //
    //    The 128-bit integer part is split into two 64-bit halves:
    //      value = intHi * 2^64 + intLo + fracHi / 2^64
    //
    //    Number has a 16-digit mantissa (~53 bits of precision). 2^64
    //    is 20 digits, so the constant is truncated at Number's
    //    precision floor (~8.7e-17 relative error). This only matters
    //    when intHi > 0, which requires cumulative fee growth exceeding
    //    2^64 tokens — not reachable in practice.
    //
    //    We split intHi into 32-bit halves to keep each intermediate
    //    product within Number's 16-digit precision:
    //      intHi * 2^64 = intHiHi * 2^96 + intHiLo * 2^64
    //    where 2^32 = 4294967296 (10 digits) fits cleanly.
    static Number const kTwoTo32{4294967296, 0};
    static Number const kTwoTo64 = kTwoTo32 * kTwoTo32;
    static Number const kTwoTo96 = kTwoTo64 * kTwoTo32;

    auto toFees = [&](uint256 const& inside, uint256 const& last, Issue const& iss) -> STAmount {
        auto const delta = uint256Sub(inside, last);
        if (delta == uint256{})
            return STAmount{iss, 0};

        auto const* b = delta.data();

        std::uint64_t intHi = 0;
        for (int i = 0; i < 8; ++i)
            intHi = (intHi << 8) | b[i];
        std::uint64_t intLo = 0;
        for (int i = 8; i < 16; ++i)
            intLo = (intLo << 8) | b[i];
        std::uint64_t fracHi = 0;
        for (int i = 16; i < 24; ++i)
            fracHi = (fracHi << 8) | b[i];

        Number d{0};
        if (intHi != 0)
        {
            auto const hi32 = static_cast<std::int64_t>(intHi >> 32);
            auto const lo32 = static_cast<std::int64_t>(intHi & 0xFFFFFFFF);
            d = Number(hi32) * kTwoTo96 + Number(lo32) * kTwoTo64 +
                Number(static_cast<std::int64_t>(intLo));
        }
        else
        {
            d = Number(static_cast<std::int64_t>(intLo));
        }

        if (fracHi != 0)
        {
            d = d + Number(static_cast<std::int64_t>(fracHi)) / kTwoTo64;
        }

        Number const fees = d * Number(posLiquidity);
        if (fees <= Number{0})
            return STAmount{iss, 0};
        return toSTAmount(iss, fees);
    };

    auto const fees0 = toFees(inside0, insideLast0, asset);
    auto const fees1 = toFees(inside1, insideLast1, asset2);

    // 5. Transfer fees from AMM account to position owner
    if (fees0 > beast::kZero)
    {
        if (auto const ter = accountSend(sb, ammAccount, account, fees0, ctx_.journal);
            !isTesSuccess(ter))
            return ter;
    }
    if (fees1 > beast::kZero)
    {
        if (auto const ter = accountSend(sb, ammAccount, account, fees1, ctx_.journal);
            !isTesSuccess(ter))
            return ter;
    }

    // 6. Update position's feeGrowthInsideLast snapshot and reset owed
    posSle->setFieldH256(sfFeeGrowthInsideLast0, inside0);
    posSle->setFieldH256(sfFeeGrowthInsideLast1, inside1);
    if (posSle->isFieldPresent(sfTokensOwed0))
        posSle->setFieldAmount(sfTokensOwed0, STAmount{asset, 0});
    if (posSle->isFieldPresent(sfTokensOwed1))
        posSle->setFieldAmount(sfTokensOwed1, STAmount{asset2, 0});
    sb.update(posSle);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
AMMCollectFees::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AMMCollectFees::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
