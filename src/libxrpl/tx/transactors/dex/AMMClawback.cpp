#include <xrpl/tx/transactors/dex/AMMClawback.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/dex/AMMWithdraw.h>

#include <cstdint>
#include <optional>
#include <tuple>

namespace xrpl {

std::uint32_t
AMMClawback::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMClawbackMask;
}

bool
AMMClawback::checkExtraFeatures(xrpl::PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMMClawback))
        return false;

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];

    return ctx.rules.enabled(featureMPTokensV2) ||
        (!(clawAmount && clawAmount->holds<MPTIssue>()) && !ctx.tx[sfAsset].holds<MPTIssue>() &&
         !ctx.tx[sfAsset2].holds<MPTIssue>());
}

NotTEC
AMMClawback::preflight(PreflightContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    AccountID const holder = ctx.tx[sfHolder];

    if (issuer == holder)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: holder cannot be the same as issuer.";
        return temMALFORMED;
    }

    std::optional<STAmount> const clawAmount = ctx.tx[~sfAmount];
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];

    if (isXRP(asset))
        return temMALFORMED;

    if (ctx.tx.isFlag(tfClawTwoAssets) && asset.getIssuer() != asset2.getIssuer())
    {
        JLOG(ctx.j.trace()) << "AMMClawback: tfClawTwoAssets can only be enabled when two "
                               "assets in the AMM pool are both issued by the issuer";
        return temINVALID_FLAG;
    }

    if (asset.getIssuer() != issuer)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Asset's account does not "
                               "match Account field.";
        return temMALFORMED;
    }

    if (clawAmount && clawAmount->asset() != asset)
    {
        JLOG(ctx.j.trace()) << "AMMClawback: Amount's asset subfield "
                               "does not match Asset field";
        return temBAD_AMOUNT;
    }

    if (clawAmount && *clawAmount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
AMMClawback::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
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

    if (!ctx.view.rules().enabled(featureMPTokensV2))
    {
        // If AllowTrustLineClawback is not set or NoFreeze is set, return no
        // permission
        if (!sleIssuer->isFlag(lsfAllowTrustLineClawback) || sleIssuer->isFlag(lsfNoFreeze))
        {
            return tecNO_PERMISSION;
        }
    }

    auto const checkClawAsset = [&](Asset const asset) -> bool {
        return asset.visit(
            [&](Issue const& issue) {
                if (issue.native())
                    return false;  // LCOV_EXCL_LINE

                return sleIssuer->isFlag(lsfAllowTrustLineClawback) &&
                    !sleIssuer->isFlag(lsfNoFreeze);
            },
            [&](MPTIssue const& issue) {
                auto const sleIssuance = ctx.view.read(keylet::mptIssuance(issue.getMptID()));

                return sleIssuance && sleIssuance->isFlag(lsfMPTCanClawback) &&
                    sleIssuance->getAccountID(sfIssuer) == ctx.tx[sfAccount];
            });
    };

    if (!checkClawAsset(asset))
        return tecNO_PERMISSION;

    if (ctx.tx.isFlag(tfClawTwoAssets) && !checkClawAsset(asset2))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
AMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (isTesSuccess(ter))
        sb.apply(ctx_.rawView());

    return ter;
}

TER
AMMClawback::applyGuts(Sandbox& sb)
{
    std::optional<STAmount> const clawAmount = ctx_.tx[~sfAmount];
    AccountID const issuer = ctx_.tx[sfAccount];
    AccountID const holder = ctx_.tx[sfHolder];
    Asset const asset = ctx_.tx[sfAsset];
    Asset const asset2 = ctx_.tx[sfAsset2];

    auto ammSle = sb.peek(keylet::amm(asset, asset2));
    if (!ammSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ammAccount = (*ammSle)[sfAccount];
    auto const accountSle = sb.read(keylet::account(ammAccount));
    if (!accountSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (sb.rules().enabled(fixAMMClawbackRounding))
    {
        // retrieve LP token balance inside the amendment gate to avoid inconsistent error behavior
        auto const lpTokenBalance = ammLPHolds(sb, *ammSle, holder, j_);
        if (lpTokenBalance == beast::kZero)
            return tecAMM_BALANCE;

        if (auto const res = verifyAndAdjustLPTokenBalance(sb, lpTokenBalance, ammSle, holder);
            !res)
            return res.error();  // LCOV_EXCL_LINE
    }

    auto const expected = ammHolds(
        sb,
        *ammSle,
        asset,
        asset2,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx_.journal);

    if (!expected)
        return expected.error();  // LCOV_EXCL_LINE
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    TER result;
    STAmount newLPTokenBalance;
    STAmount amountWithdraw;
    std::optional<STAmount> amount2Withdraw;

    // calling a second time on purpose since `verifyAndAdjustLPTokenBalance` rounds and may adjust
    // the balance
    auto const holdLPtokens = ammLPHolds(sb, *ammSle, holder, j_);
    if (holdLPtokens == beast::kZero)
        return tecAMM_BALANCE;

    if (!clawAmount)
    {
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
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                WithdrawAll::Yes,
                preFeeBalance_,
                ctx_.journal);
    }
    else
    {
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
    }

    if (!isTesSuccess(result))
        return result;  // LCOV_EXCL_LINE

    auto const res =
        AMMWithdraw::deleteAMMAccountIfEmpty(sb, ammSle, newLPTokenBalance, asset, asset2, j_);
    if (!res.second)
        return res.first;  // LCOV_EXCL_LINE

    JLOG(ctx_.journal.trace()) << "AMM Withdraw during AMMClawback: lptoken new balance: "
                               << to_string(newLPTokenBalance.iou())
                               << " old balance: " << to_string(lptAMMBalance.iou());

    auto sendAmount = [&](STAmount const& saAmount) -> TER {
        bool const checkIssuer = saAmount.holds<Issue>();
        return directSendNoFee(sb, holder, issuer, saAmount, checkIssuer, j_);
    };

    auto const ter = sendAmount(amountWithdraw);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // if the issuer issues both assets and sets flag tfClawTwoAssets, we
    // will claw the paired asset as well. We already checked if
    // tfClawTwoAssets is enabled, the two assets have to be issued by the
    // same issuer.
    if (!amount2Withdraw)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (ctx_.tx.isFlag(tfClawTwoAssets))
        return sendAmount(*amount2Withdraw);

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

    auto const lpTokensWithdraw = toSTAmount(lptAMMBalance.asset(), lptAMMBalance * frac);
    if (lpTokensWithdraw > holdLPtokens)
    {
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
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            WithdrawAll::Yes,
            preFeeBalance_,
            ctx_.journal);
    }

    auto const& rules = sb.rules();
    if (rules.enabled(fixAMMClawbackRounding))
    {
        auto tokensAdj = getRoundedLPTokens(rules, lptAMMBalance, frac, IsDeposit::No);

        // LCOV_EXCL_START
        if (tokensAdj == beast::kZero)
            return {tecAMM_INVALID_TOKENS, STAmount{}, STAmount{}, std::nullopt};
        // LCOV_EXCL_STOP

        frac = adjustFracByTokens(rules, lptAMMBalance, tokensAdj, frac);
        auto amount2Rounded = getRoundedAsset(rules, amount2Balance, frac, IsDeposit::No);

        auto amountRounded = getRoundedAsset(rules, amountBalance, frac, IsDeposit::No);

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
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            WithdrawAll::No,
            preFeeBalance_,
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
        toSTAmount(amount2Balance.asset(), amount2Withdraw),
        lptAMMBalance,
        toSTAmount(lptAMMBalance.asset(), lptAMMBalance * frac),
        0,
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        WithdrawAll::No,
        preFeeBalance_,
        ctx_.journal);
}

void
AMMClawback::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMClawback::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
