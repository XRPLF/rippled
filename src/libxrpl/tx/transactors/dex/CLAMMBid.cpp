#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/AmountConversions.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMBid.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <chrono>
#include <cmath>

namespace xrpl {

namespace {

// Compute the current time slot index (0..19) and whether the slot is expired.
// Returns {timeSlot, isExpired}.
std::pair<std::uint16_t, bool>
computeTimeSlot(std::uint32_t now, std::uint32_t expiration)
{
    if (now >= expiration)
        return {0, true};

    auto const remaining = expiration - now;
    auto const elapsed = CLAMM_TOTAL_TIME_SLOT_SECS - remaining;
    auto const intervalLen =
        CLAMM_TOTAL_TIME_SLOT_SECS / CLAMM_AUCTION_SLOT_TIME_INTERVALS;
    auto const slot = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(
            elapsed / intervalLen, CLAMM_AUCTION_SLOT_TIME_INTERVALS - 1));
    return {slot, false};
}

// Number-based power function: base^exp using repeated multiplication.
Number
numPower(Number const& base, int exp)
{
    Number result{1};
    for (int i = 0; i < exp; ++i)
        result = result * base;
    return result;
}

}  // namespace

bool
CLAMMBid::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMBid::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    // Validate authorized accounts
    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        auto const& authAccounts = ctx.tx.getFieldArray(sfAuthAccounts);
        if (authAccounts.size() > CLAMM_AUCTION_SLOT_MAX_AUTH_ACCOUNTS)
        {
            JLOG(ctx.j.debug()) << "CLAMM Bid: too many auth accounts.";
            return temMALFORMED;
        }

        auto const bidder = ctx.tx[sfAccount];
        std::set<AccountID> unique;
        for (auto const& obj : authAccounts)
        {
            auto const authAccount = obj[sfAccount];
            if (authAccount == bidder || unique.contains(authAccount))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Bid: duplicate or self auth account.";
                return temMALFORMED;
            }
            unique.insert(authAccount);
        }
    }

    // Validate BidMin <= BidMax when both are specified
    if (ctx.tx.isFieldPresent(sfBidMin) && ctx.tx.isFieldPresent(sfBidMax))
    {
        auto const bidMin = ctx.tx.getFieldAmount(sfBidMin);
        auto const bidMax = ctx.tx.getFieldAmount(sfBidMax);
        if (bidMin > bidMax)
        {
            JLOG(ctx.j.debug())
                << "CLAMM Bid: BidMin is greater than BidMax.";
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

TER
CLAMMBid::preclaim(PreclaimContext const& ctx)
{
    if (auto const poolID = resolvePoolID(ctx.tx))
    {
        auto const sleClamm = ctx.view.read(keylet::clamm(*poolID));
        if (!sleClamm)
        {
            JLOG(ctx.j.debug()) << "CLAMM Bid: pool not found.";
            return tecNO_ENTRY;
        }

        // Check if pool assets are frozen
        auto const& clammRef = std::as_const(*sleClamm);
        auto const issue0 = clammRef[sfAsset].get<Issue>();
        auto const issue1 = clammRef[sfAsset2].get<Issue>();
        auto const ammAccountID = sleClamm->getAccountID(sfAccount);

        for (auto const& issue : {issue0, issue1})
        {
            if (isXRP(issue))
                continue;
            if (isFrozen(ctx.view, ammAccountID, issue))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Bid: pool asset is frozen, " << issue;
                return tecFROZEN;
            }
        }
    }

    // Verify all auth accounts exist
    if (ctx.tx.isFieldPresent(sfAuthAccounts))
    {
        for (auto const& obj : ctx.tx.getFieldArray(sfAuthAccounts))
        {
            if (!ctx.view.read(keylet::account(obj[sfAccount])))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Bid: auth account not found.";
                return terNO_ACCOUNT;
            }
        }
    }

    return tesSUCCESS;
}

TER
CLAMMBid::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const optPoolID = resolvePoolID(ctx_.tx);
    if (!optPoolID)
    {
        JLOG(j_.debug()) << "CLAMM Bid: no pool identifier provided.";
        return temMALFORMED;
    }
    auto const poolID = *optPoolID;

    Sandbox sb(&ctx_.view());

    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tecNO_ENTRY;

    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto const tradingFee = sleClamm->getFieldU16(sfTradingFee);

    // Discounted fee = tradingFee / 10 (same formula as AMM)
    auto const discountedFee =
        static_cast<std::uint16_t>(tradingFee / 10);

    using namespace std::chrono;
    auto const now = static_cast<std::uint32_t>(
        duration_cast<seconds>(
            ctx_.view().header().parentCloseTime.time_since_epoch())
            .count());

    // Minimum slot price: proportional to trading fee.
    // MinSlotPrice = max(1, token0_balance * tradingFee / 25000)
    constexpr std::uint32_t AUCTION_SLOT_MIN_FEE_FRACTION = 25000;
    auto const poolBalance0 =
        accountHolds(sb, ammAccountID, issue0, FreezeHandling::fhZERO_IF_FROZEN, j_);
    Number const computedMinPrice =
        Number(poolBalance0) * tradingFee / AUCTION_SLOT_MIN_FEE_FRACTION;
    // Floor at 1 drop/unit to prevent zero-cost slot acquisition
    Number const minSlotPrice =
        computedMinPrice > Number(0)
        ? computedMinPrice
        : Number(STAmount(issue0, 1, -6));

    // Ensure auction slot exists
    if (!sleClamm->isFieldPresent(sfAuctionSlot))
        sleClamm->makeFieldPresent(sfAuctionSlot);

    auto& auctionSlot = sleClamm->peekFieldObject(sfAuctionSlot);

    // Determine current slot state
    bool slotOccupied = false;
    bool slotExpired = true;
    std::uint16_t timeSlot = 0;
    AccountID previousHolder;
    STAmount pricePurchased(issue0, 0);

    if (auctionSlot.isFieldPresent(sfAccount))
    {
        previousHolder = auctionSlot.getAccountID(sfAccount);
        auto const expiration = auctionSlot[~sfExpiration].value_or(0u);
        auto const [slot, expired] = computeTimeSlot(now, expiration);
        timeSlot = slot;
        slotExpired = expired;
        slotOccupied = !slotExpired;

        if (auctionSlot.isFieldPresent(sfPrice))
            pricePurchased = auctionSlot.getFieldAmount(sfPrice);
    }

    // Tailing slot (last interval) is treated as expired
    constexpr std::uint16_t tailingSlot =
        CLAMM_AUCTION_SLOT_TIME_INTERVALS - 1;
    if (timeSlot >= tailingSlot)
        slotOccupied = false;

    // Compute the required bid price
    Number computedPriceNum;

    if (!slotOccupied)
    {
        computedPriceNum = minSlotPrice;
    }
    else
    {
        auto const fractionUsed =
            Number(timeSlot + 1) / CLAMM_AUCTION_SLOT_TIME_INTERVALS;
        auto const p1_05 = Number(105, -2);
        auto const purchasedNum = Number(pricePurchased);

        if (timeSlot == 0)
        {
            computedPriceNum = purchasedNum * p1_05 + minSlotPrice;
        }
        else
        {
            computedPriceNum = purchasedNum * p1_05 *
                        (Number(1) - numPower(fractionUsed, 60)) +
                    minSlotPrice;
        }
    }

    auto computedPrice = toSTAmount(issue0, computedPriceNum);

    // Apply BidMin / BidMax constraints
    STAmount payPrice = computedPrice;

    if (ctx_.tx.isFieldPresent(sfBidMin) &&
        ctx_.tx.isFieldPresent(sfBidMax))
    {
        auto const bidMin = ctx_.tx.getFieldAmount(sfBidMin);
        auto const bidMax = ctx_.tx.getFieldAmount(sfBidMax);
        if (computedPrice < bidMin)
            payPrice = bidMin;
        else if (computedPrice > bidMax)
        {
            JLOG(j_.debug())
                << "CLAMM Bid: computed price exceeds bidMax.";
            return tecINSUFFICIENT_PAYMENT;
        }
    }
    else if (ctx_.tx.isFieldPresent(sfBidMin))
    {
        auto const bidMin = ctx_.tx.getFieldAmount(sfBidMin);
        if (computedPrice < bidMin)
            payPrice = bidMin;
    }
    else if (ctx_.tx.isFieldPresent(sfBidMax))
    {
        auto const bidMax = ctx_.tx.getFieldAmount(sfBidMax);
        if (computedPrice > bidMax)
        {
            JLOG(j_.debug())
                << "CLAMM Bid: computed price exceeds bidMax.";
            return tecINSUFFICIENT_PAYMENT;
        }
    }

    // Process payment
    if (slotOccupied && previousHolder != account)
    {
        // Refund previous holder: proportional to remaining time
        auto const fractionUsed =
            Number(timeSlot + 1) / CLAMM_AUCTION_SLOT_TIME_INTERVALS;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto const refund =
            toSTAmount(issue0, fractionRemaining * Number(pricePurchased));

        if (refund > beast::zero)
        {
            // Refund previous holder
            auto const res = accountSend(
                sb, account, previousHolder, refund, j_);
            if (res != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "CLAMM Bid: refund to previous holder failed: "
                    << transHuman(res);
                return res;
            }
        }

        // Remainder goes to pool (benefits all LPs).
        // Clamp to zero: refund can exceed payPrice when the previous
        // holder's original payment was larger than the current bid.
        auto const toPool = payPrice > refund
            ? payPrice - refund
            : STAmount(payPrice.issue(), 0);
        if (toPool > beast::zero)
        {
            auto const res = accountSend(
                sb, account, ammAccountID, toPool, j_, WaiveTransferFee::Yes);
            if (res != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "CLAMM Bid: payment to pool failed: "
                    << transHuman(res);
                return res;
            }
        }
    }
    else if (payPrice > beast::zero)
    {
        // No previous holder (or same holder rebidding):
        // full amount goes to pool
        auto const res = accountSend(
            sb, account, ammAccountID, payPrice, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug())
                << "CLAMM Bid: payment to pool failed: "
                << transHuman(res);
            return res;
        }
    }

    // Update auction slot
    auctionSlot.setAccountID(sfAccount, account);
    auctionSlot.setFieldU32(
        sfExpiration, now + CLAMM_TOTAL_TIME_SLOT_SECS);
    auctionSlot.setFieldAmount(sfPrice, payPrice);

    if (discountedFee != 0)
        auctionSlot.setFieldU16(sfDiscountedFee, discountedFee);
    else if (auctionSlot.isFieldPresent(sfDiscountedFee))
        auctionSlot.makeFieldAbsent(sfDiscountedFee);

    // Set authorized accounts
    if (ctx_.tx.isFieldPresent(sfAuthAccounts))
        auctionSlot.setFieldArray(
            sfAuthAccounts, ctx_.tx.getFieldArray(sfAuthAccounts));
    else if (auctionSlot.isFieldPresent(sfAuthAccounts))
        auctionSlot.makeFieldAbsent(sfAuthAccounts);

    sleClamm->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
    sleClamm->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
    sb.update(sleClamm);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl
