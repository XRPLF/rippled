/** @file
 *  Implements the `AMMClawback` transactor (XLS-73), which lets a regulated
 *  token issuer forcibly withdraw their assets from a holder's AMM liquidity
 *  position.
 *
 *  The transactor is a policy layer on top of `AMMWithdraw`: it decides *how
 *  much* to withdraw and *where the assets go*, but delegates all AMM-state
 *  mutation (constant-product math, LP-token burn, pool-balance update) to
 *  `AMMWithdraw::equalWithdrawTokens` and `AMMWithdraw::withdraw`. The trading
 *  fee is always zero because the withdrawal is not a voluntary trade.
 */

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
#include <memory>
#include <optional>
#include <tuple>

namespace xrpl {

/** @copydoc AMMClawback::getFlagsMask */
std::uint32_t
AMMClawback::getFlagsMask(PreflightContext const& ctx)
{
    return tfAMMClawbackMask;
}

/** @copydoc AMMClawback::checkExtraFeatures */
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

/** @copydoc AMMClawback::preflight */
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

    auto const flags = ctx.tx.getFlags();

    if (((flags & tfClawTwoAssets) != 0u) && asset.getIssuer() != asset2.getIssuer())
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

    if (clawAmount && *clawAmount <= beast::kZERO)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** @copydoc AMMClawback::preclaim */
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

    std::uint32_t const issuerFlagsIn = sleIssuer->getFieldU32(sfFlags);
    if (!ctx.view.rules().enabled(featureMPTokensV2))
    {
        // Pre-MPTv2: enforce IOU clawback flags eagerly. Once MPTv2 is live,
        // per-asset checks via checkClawAsset (below) supersede this path.
        if (((issuerFlagsIn & lsfAllowTrustLineClawback) == 0u) ||
            ((issuerFlagsIn & lsfNoFreeze) != 0u))
        {
            return tecNO_PERMISSION;
        }
    }

    auto const checkClawAsset = [&](Asset const asset) -> bool {
        return asset.visit(
            [&](Issue const& issue) {
                if (issue.native())
                    return false;  // LCOV_EXCL_LINE

                return ((issuerFlagsIn & lsfAllowTrustLineClawback) != 0u) &&
                    ((issuerFlagsIn & lsfNoFreeze) == 0u);
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

/** @copydoc AMMClawback::doApply */
TER
AMMClawback::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const ter = applyGuts(sb);
    if (isTesSuccess(ter))
        sb.apply(ctx_.rawView());

    return ter;
}

/** Core clawback logic, executed inside a `Sandbox`.
 *
 *  Selects between two withdrawal paths based on whether `sfAmount` is
 *  present in the transaction:
 *
 *  - **No `sfAmount`**: Calls `AMMWithdraw::equalWithdrawTokens` with the
 *    holder's entire LP token balance, liquidating the full position.
 *  - **With `sfAmount`**: Delegates to `equalWithdrawMatchingOneAmount` to
 *    compute the pool fraction corresponding to the requested asset1 cap.
 *
 *  Both paths pass a trading fee of `0`. The AMM fee compensates LPs for
 *  impermanent loss on voluntary swaps; charging it here would make a
 *  regulatory clawback more expensive for the issuer without economic
 *  justification.
 *
 *  When `fixAMMClawbackRounding` is active, `verifyAndAdjustLPTokenBalance`
 *  is called before withdrawal to correct accumulated floating-point drift
 *  in the AMM's `LPTokenBalance` field. The LP balance is re-read afterward
 *  because the helper may have updated the ledger entry.
 *
 *  After withdrawal, `AMMWithdraw::deleteAMMAccountIfEmpty` tears down the
 *  AMM object and its pseudo-account if all LP tokens have been burned.
 *  The recovered asset1 is transferred from the holder to the issuer via
 *  `directSendNoFee`. Asset2 is only transferred when `tfClawTwoAssets` is
 *  set; otherwise it remains with the holder.
 *
 *  @param sb Sandbox accumulating all mutations for this transaction.
 *  @return `tesSUCCESS` on success; a `tec*` error code on failure.
 */
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
        if (lpTokenBalance == beast::kZERO)
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

    // Re-read after the adjustment: verifyAndAdjustLPTokenBalance may have
    // modified the AMM SLE, so the earlier lpTokenBalance snapshot is stale.
    auto const holdLPtokens = ammLPHolds(sb, *ammSle, holder, j_);
    if (holdLPtokens == beast::kZERO)
        return tecAMM_BALANCE;

    if (!clawAmount)
    {
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

    if (!amount2Withdraw)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const flags = ctx_.tx.getFlags();
    if ((flags & tfClawTwoAssets) != 0u)
        return sendAmount(*amount2Withdraw);

    return tesSUCCESS;
}

/** @copydoc AMMClawback::equalWithdrawMatchingOneAmount */
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
        // Holder doesn't have enough LP tokens to cover sfAmount; fall back to
        // a full clawback of whatever LP tokens the holder actually owns.
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
        if (tokensAdj == beast::kZERO)
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

/** @copydoc AMMClawback::visitInvariantEntry */
void
AMMClawback::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** @copydoc AMMClawback::finalizeInvariants */
bool
AMMClawback::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
