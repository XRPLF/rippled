#include <xrpl/tx/transactors/dex/AMMBid.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

bool
AMMBid::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return false;

    if (!ctx.rules.enabled(featureMPTokensV2) &&
        (ctx.tx[sfAsset].holds<MPTIssue>() || ctx.tx[sfAsset2].holds<MPTIssue>()))
        return false;

    return true;
}

NotTEC
AMMBid::preflight(PreflightContext const& ctx)
{
    if (auto const res = invalidAMMAssetPair(ctx.tx[sfAsset], ctx.tx[sfAsset2]))
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid asset pair.";
        return res;
    }

    if (auto const bidMin = ctx.tx[~sfBidMin])
    {
        if (auto const res = invalidAMMAmount(*bidMin))
        {
            JLOG(ctx.j.debug()) << "AMM Bid: invalid min slot price.";
            return res;
        }
    }

    if (auto const bidMax = ctx.tx[~sfBidMax])
    {
        if (auto const res = invalidAMMAmount(*bidMax))
        {
            JLOG(ctx.j.debug()) << "AMM Bid: invalid max slot price.";
            return res;
        }
    }

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        auto const authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
        if (authAccounts.size() > kAuctionSlotMaxAuthAccounts)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid number of AuthAccounts.";
            return temMALFORMED;
        }
        if (ctx.rules.enabled(fixAMMv1_3))
        {
            AccountID const account = ctx.tx[sfAccount];
            std::set<AccountID> unique;
            for (auto const& obj : authAccounts)
            {
                auto authAccount = obj[sfAccount];
                if (authAccount == account || unique.contains(authAccount))
                {
                    JLOG(ctx.j.debug()) << "AMM Bid: Invalid auth.account.";
                    return temMALFORMED;
                }
                unique.insert(authAccount);
            }
        }
    }

    return tesSUCCESS;
}

TER
AMMBid::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance == beast::kZero)
        return tecAMM_EMPTY;

    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        for (auto const& account : ctx.tx.getFieldArray(sfAuthAccounts))
        {
            if (!ctx.view.read(keylet::account(account[sfAccount])))
            {
                JLOG(ctx.j.debug()) << "AMM Bid: Invalid Account.";
                return terNO_ACCOUNT;
            }
        }
    }

    auto const lpTokens = ammLPHolds(ctx.view, *ammSle, ctx.tx[sfAccount], ctx.j);
    // Not LP
    if (lpTokens == beast::kZero)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: account is not LP.";
        return tecAMM_INVALID_TOKENS;
    }

    auto const bidMin = ctx.tx[~sfBidMin];

    if (bidMin)
    {
        if (bidMin->asset() != lpTokens.asset())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
        if (*bidMin > lpTokens || *bidMin >= lpTokensBalance)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
    }

    auto const bidMax = ctx.tx[~sfBidMax];
    if (bidMax)
    {
        if (bidMax->asset() != lpTokens.asset())
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid LPToken.";
            return temBAD_AMM_TOKENS;
        }
        if (*bidMax > lpTokens || *bidMax >= lpTokensBalance)
        {
            JLOG(ctx.j.debug()) << "AMM Bid: Invalid Tokens.";
            return tecAMM_INVALID_TOKENS;
        }
    }

    if (bidMin && bidMax && bidMin > bidMax)
    {
        JLOG(ctx.j.debug()) << "AMM Bid: Invalid Max/MinSlotPrice.";
        return tecAMM_INVALID_TOKENS;
    }

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyBid(ApplyContext& ctx, Sandbox& sb, AccountID const& account, beast::Journal j)
{
    using namespace std::chrono;
    auto const ammSle = sb.peek(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
        return {tecINTERNAL, false};
    STAmount const lptAMMBalance = (*ammSle)[sfLPTokenBalance];
    auto const lpTokens = ammLPHolds(sb, *ammSle, account, ctx.journal);
    auto const& rules = ctx.view().rules();
    if (!rules.enabled(fixInnerObjTemplate))
    {
        if (!ammSle->isFieldPresent(sfAuctionSlot))
            ammSle->makeFieldPresent(sfAuctionSlot);
    }
    else
    {
        XRPL_ASSERT(ammSle->isFieldPresent(sfAuctionSlot), "xrpl::applyBid : has auction slot");
        if (!ammSle->isFieldPresent(sfAuctionSlot))
            return {tecINTERNAL, false};
    }
    auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
    auto const current =
        duration_cast<seconds>(ctx.view().header().parentCloseTime.time_since_epoch()).count();
    // Auction slot discounted fee
    auto const discountedFee = (*ammSle)[sfTradingFee] / kAuctionSlotDiscountedFeeFraction;
    auto const tradingFee = getFee((*ammSle)[sfTradingFee]);
    // Min price
    auto const minSlotPrice = lptAMMBalance * tradingFee / kAuctionSlotMinFeeFraction;

    static constexpr std::uint32_t kTailingSlot = kAuctionSlotTimeIntervals - 1;

    // If seated then it is the current slot-holder time slot, otherwise
    // the auction slot is not owned. Slot range is in {0-19}
    auto const timeSlot = ammAuctionTimeSlot(current, auctionSlot);

    // Account must exist and the slot not expired.
    auto validOwner = [&](AccountID const& account) {
        // Valid range is 0-19 but the tailing slot pays MinSlotPrice
        // and doesn't refund so the check is < instead of <= to optimize.
        return timeSlot && *timeSlot < kTailingSlot && sb.read(keylet::account(account));
    };

    auto updateSlot = [&](std::uint32_t fee, Number const& minPrice, Number const& burn) -> TER {
        auctionSlot.setAccountID(sfAccount, account);
        auctionSlot.setFieldU32(sfExpiration, current + kTotalTimeSlotSecs);
        if (fee != 0)
        {
            auctionSlot.setFieldU16(sfDiscountedFee, fee);
        }
        else if (auctionSlot.isFieldPresent(sfDiscountedFee))
        {
            auctionSlot.makeFieldAbsent(sfDiscountedFee);
        }
        auctionSlot.setFieldAmount(sfPrice, toSTAmount(lpTokens.asset(), minPrice));
        if (ctx.tx.isFieldPresent(sfAuthAccounts))
        {
            auctionSlot.setFieldArray(sfAuthAccounts, ctx.tx.getFieldArray(sfAuthAccounts));
        }
        else
        {
            auctionSlot.makeFieldAbsent(sfAuthAccounts);
        }
        // Burn the remaining bid amount
        auto const saBurn =
            adjustLPTokens(lptAMMBalance, toSTAmount(lptAMMBalance.asset(), burn), IsDeposit::No);
        if (saBurn >= lptAMMBalance)
        {
            // This error case should never occur.
            // LCOV_EXCL_START
            JLOG(ctx.journal.fatal())
                << "AMM Bid: LP Token burn exceeds AMM balance " << burn << " " << lptAMMBalance;
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
        auto res = redeemIOU(sb, account, saBurn, lpTokens.get<Issue>(), ctx.journal);
        if (!isTesSuccess(res))
        {
            JLOG(ctx.journal.debug()) << "AMM Bid: failed to redeem.";
            return res;
        }
        ammSle->setFieldAmount(sfLPTokenBalance, lptAMMBalance - saBurn);
        sb.update(ammSle);
        return tesSUCCESS;
    };

    TER res = tesSUCCESS;

    auto const bidMin = ctx.tx[~sfBidMin];
    auto const bidMax = ctx.tx[~sfBidMax];

    auto getPayPrice = [&](Number const& computedPrice) -> Expected<Number, TER> {
        auto const payPrice = [&]() -> std::optional<Number> {
            // Both min/max bid price are defined
            if (bidMin && bidMax)
            {
                if (computedPrice <= *bidMax)
                    return std::max(computedPrice, Number(*bidMin));
                JLOG(ctx.journal.debug()) << "AMM Bid: not in range " << computedPrice << " "
                                          << *bidMin << " " << *bidMax;
                return std::nullopt;
            }
            // Bidder pays max(bidPrice, computedPrice)
            if (bidMin)
            {
                return std::max(computedPrice, Number(*bidMin));
            }
            if (bidMax)
            {
                if (computedPrice <= *bidMax)
                    return computedPrice;
                JLOG(ctx.journal.debug())
                    << "AMM Bid: not in range " << computedPrice << " " << *bidMax;
                return std::nullopt;
            }

            return computedPrice;
        }();
        if (!payPrice)
        {
            return Unexpected(tecAMM_FAILED);
        }
        if (payPrice > lpTokens)
        {
            return Unexpected(tecAMM_INVALID_TOKENS);
        }
        return *payPrice;
    };

    // No one owns the slot or expired slot.
    if (auto const acct = auctionSlot[~sfAccount]; !acct || !validOwner(*acct))
    {
        auto const payPrice = getPayPrice(minSlotPrice);
        if (!payPrice)
        {
            return {payPrice.error(), false};
        }

        res = updateSlot(discountedFee, *payPrice, *payPrice);
    }
    else
    {
        // Price the slot was purchased at.
        STAmount const pricePurchased = auctionSlot[sfPrice];
        XRPL_ASSERT(timeSlot, "xrpl::applyBid : timeSlot is set");
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const fractionUsed = (Number(*timeSlot) + 1) / kAuctionSlotTimeIntervals;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto const computedPrice = [&]() -> Number {
            auto const p105 = Number(105, -2);
            // First interval slot price
            if (*timeSlot == 0)
                return pricePurchased * p105 + minSlotPrice;
            // Other intervals slot price
            return pricePurchased * p105 * (1 - power(fractionUsed, 60)) + minSlotPrice;
        }();
        // NOLINTEND(bugprone-unchecked-optional-access)

        auto const payPrice = getPayPrice(computedPrice);

        if (!payPrice)
            return {payPrice.error(), false};

        // Refund the previous owner. If the time slot is 0 then
        // the owner is refunded 95% of the amount.
        auto const refund = fractionRemaining * pricePurchased;
        if (refund > *payPrice)
        {
            // This error case should never occur.
            JLOG(ctx.journal.fatal())
                << "AMM Bid: refund exceeds payPrice " << refund << " " << *payPrice;
            return {tecINTERNAL, false};
        }
        res = accountSend(
            sb, account, auctionSlot[sfAccount], toSTAmount(lpTokens.asset(), refund), ctx.journal);
        if (!isTesSuccess(res))
        {
            JLOG(ctx.journal.debug()) << "AMM Bid: failed to refund.";
            return {res, false};
        }

        auto const burn = *payPrice - refund;
        res = updateSlot(discountedFee, *payPrice, burn);
    }

    return {res, isTesSuccess(res)};
}

TER
AMMBid::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyBid(ctx_, sb, accountID_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

void
AMMBid::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMBid::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
