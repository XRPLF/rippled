#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/AMMClawback.h>
#include <xrpld/app/tx/detail/AMMWithdraw.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/st.h>

#include <tuple>

namespace ripple {

std::uint32_t
AMMClawback::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMClawbackMask;
}

NotTEC
AMMClawback::preflight(PreflightContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.trace())
            << "AMMClawback: holder cannot be the same as issuer.";
        return temMALFORMED;
    }

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];
    auto const asset = ctx.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx.tx[sfAsset2].get<Issue>();

    if (isXRP(asset))
        return temMALFORMED;

    auto const flags = ctx.tx.getFlags();

    if (flags & tfClawTwoAssets && asset.account != asset2.account)
    {
        JLOG(ctx.j.trace())
            << "AMMClawback: tfClawTwoAssets can only be enabled when two "
               "assets in the AMM pool are both issued by the issuer";
        return temINVALID_FLAG;
    }

    if (asset.account != issuer)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Asset's account does not "
                               "match Account field.";
        return temMALFORMED;
    }

    if (clawAmount && clawAmount->get<Issue>() != asset)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Amount's issuer/currency subfield "
                               "does not match Asset field";
        return temBAD_AMOUNT;
    }

    if (clawAmount && *clawAmount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
AMMClawback::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx.tx[sfAsset2].get<Issue>();
    auto const sleIssuer = ctx.view.read(keylet::account(ctx.tx[sfAccount]));
    if (!sleIssuer)
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    if (!ctx.view.read(keylet::account(ctx.tx[sfHolder])))
        return terNO_ACCOUNT;

    auto const ammSle = ctx.view.read(keylet::amm(asset, asset2));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Clawback: Invalid asset pair.";
        return terNO_AMM;
    }

    std::uint32_t const issuerFlagsIn = sleIssuer->getFieldU32(sfFlags);

    // If AllowTrustLineClawback is not set or NoFreeze is set, return no
    // permission
    if (!(issuerFlagsIn & lsfAllowTrustLineClawback) ||
        (issuerFlagsIn & lsfNoFreeze))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
AMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (ter == tesSUCCESS)
        sb.apply(ctx_.rawView());

    return ter;
}

TER
AMMClawback::applyGuts(Sandbox& sb)
{
    std::optional<STAmount> const clawAmount = ctx_.tx[~sfAmount];
    AccountID const issuer = ctx_.tx[sfAccount];
    AccountID const holder = ctx_.tx[sfHolder];
    Issue const asset = ctx_.tx[sfAsset].get<Issue>();
    Issue const asset2 = ctx_.tx[sfAsset2].get<Issue>();

    auto ammSle = sb.peek(keylet::amm(asset, asset2));
    if (!ammSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const accountSle = sb.read(keylet::account(ammAccount));
    if (!accountSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (sb.rules().enabled(fixAMMClawbackRounding))
    {
        // retrieve LP token balance inside the amendment gate to avoid
        // inconsistent error behavior
        auto const lpTokenBalance = ammLPHolds(sb, *ammSle, holder, j_);
        if (lpTokenBalance == beast::zero)
            return tecAMM_BALANCE;

        if (auto const res = verifyAndAdjustLPTokenBalance(
                sb, lpTokenBalance, ammSle, holder);
            !res)
            return res.error();  // LCOV_EXCL_LINE
    }

    auto const expected = ammHolds(
        sb,
        *ammSle,
        asset,
        asset2,
        FreezeHandling::fhIGNORE_FREEZE,
        ctx_.journal);

    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    TER result;
    STAmount newLPTokenBalance;
    STAmount amountWithdraw;
    std::optional<STAmount> amount2Withdraw;

    auto const holdLPtokens = ammLPHolds(sb, *ammSle, holder, j_);
    if (holdLPtokens == beast::zero)
        return tecAMM_BALANCE;

    if (!clawAmount)
        // Because we are doing a two-asset withdrawal,
        // tfee is actually not used, so pass tfee as 0.
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            AMMWithdraw::equalWithdrawTokens(
                sb,
                *ammSle,
                holder,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                holdLPtokens,
                holdLPtokens,
                0,
                FreezeHandling::fhIGNORE_FREEZE,
                WithdrawAll::Yes,
                mPriorBalance,
                ctx_.journal);
    else
        std::tie(result, newLPTokenBalance, amountWithdraw, amount2Withdraw) =
            equalWithdrawMatchingOneAmount(
                sb,
                *ammSle,
                holder,
                ammAccount,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                holdLPtokens,
                *clawAmount);

    if (result != tesSUCCESS)
        return result;  // LCOV_EXCL_LINE

    auto const res = AMMWithdraw::deleteAMMAccountIfEmpty(
        sb, ammSle, newLPTokenBalance, asset, asset2, j_);
    if (!res.second)
        return res.first;  // LCOV_EXCL_LINE

    JLOG(ctx_.journal.trace())
        << "AMM Withdraw during AMMClawback: lptoken new balance: "
        << to_string(newLPTokenBalance.iou())
        << " old balance: " << to_string(lptAMMBalance.iou());

    auto const ter = rippleCredit(sb, holder, issuer, amountWithdraw, true, j_);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE

    // if the issuer issues both assets and sets flag tfClawTwoAssets, we
    // will claw the paired asset as well. We already checked if
    // tfClawTwoAssets is enabled, the two assets have to be issued by the
    // same issuer.
    if (!amount2Withdraw)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const flags = ctx_.tx.getFlags();
    if (flags & tfClawTwoAssets)
        return rippleCredit(sb, holder, issuer, *amount2Withdraw, true, j_);

    return tesSUCCESS;
}

std::tuple<TER, STAmount, STAmount, std::optional<STAmount>>
AMMClawback::equalWithdrawMatchingOneAmount(
    Sandbox& sb,
    SLE const& ammSle,
    AccountID const& holder,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& holdLPtokens,
    STAmount const& amount)
{
    auto frac = Number{amount} / amountBalance;
    auto amount2Withdraw = amount2Balance * frac;

    auto const lpTokensWithdraw =
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac);

    if (lpTokensWithdraw > holdLPtokens)
        // if lptoken balance less than what the issuer intended to clawback,
        // clawback all the tokens. Because we are doing a two-asset withdrawal,
        // tfee is actually not used, so pass tfee as 0.
        return AMMWithdraw::equalWithdrawTokens(
            sb,
            ammSle,
            holder,
            ammAccount,
            amountBalance,
            amount2Balance,
            lptAMMBalance,
            holdLPtokens,
            holdLPtokens,
            0,
            FreezeHandling::fhIGNORE_FREEZE,
            WithdrawAll::Yes,
            mPriorBalance,
            ctx_.journal);

    auto const& rules = sb.rules();
    if (rules.enabled(fixAMMClawbackRounding))
    {
        auto tokensAdj =
            getRoundedLPTokens(rules, lptAMMBalance, frac, IsDeposit::No);

        // LCOV_EXCL_START
        if (tokensAdj == beast::zero)
            return {
                tecAMM_INVALID_TOKENS, STAmount{}, STAmount{}, std::nullopt};
        // LCOV_EXCL_STOP

        frac = adjustFracByTokens(rules, lptAMMBalance, tokensAdj, frac);
        auto amount2Rounded =
            getRoundedAsset(rules, amount2Balance, frac, IsDeposit::No);

        auto amountRounded =
            getRoundedAsset(rules, amountBalance, frac, IsDeposit::No);

        return AMMWithdraw::withdraw(
            sb,
            ammSle,
            ammAccount,
            holder,
            amountBalance,
            amountRounded,
            amount2Rounded,
            lptAMMBalance,
            tokensAdj,
            0,
            FreezeHandling::fhIGNORE_FREEZE,
            WithdrawAll::No,
            mPriorBalance,
            ctx_.journal);
    }

    // Because we are doing a two-asset withdrawal,
    // tfee is actually not used, so pass tfee as 0.
    return AMMWithdraw::withdraw(
        sb,
        ammSle,
        ammAccount,
        holder,
        amountBalance,
        amount,
        toSTAmount(amount2Balance.issue(), amount2Withdraw),
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac),
        0,
        FreezeHandling::fhIGNORE_FREEZE,
        WithdrawAll::No,
        mPriorBalance,
        ctx_.journal);
}

}  // namespace ripple
