/** @file
 *  Implementation of the AMMBid transactor: auction slot bidding for AMM pools.
 *
 *  The file-local `applyBid` free function contains the full pricing and slot
 *  mutation logic. The public `AMMBid` methods are thin wrappers that delegate
 *  to it inside a `Sandbox` for atomic rollback on failure.
 */
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
#include <memory>
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
        if (authAccounts.size() > kAUCTION_SLOT_MAX_AUTH_ACCOUNTS)
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
    if (lpTokensBalance == beast::kZERO)
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
    if (lpTokens == beast::kZERO)
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

/** Apply an AMMBid transaction against a mutable sandbox view.
 *
 *  Computes the minimum slot price (`lptAMMBalance × tradingFee / 25`),
 *  determines the current auction time slot (0–19, each ~72 minutes), and
 *  branches on whether the slot is unowned/expired or held by an active owner:
 *
 *  - **Unowned/expired** — bidder pays `max(minSlotPrice, sfBidMin)`, capped
 *    by `sfBidMax`.  The full payment is burned as LP tokens.
 *  - **Occupied (intervals 0–18)** — bidder pays the decay-adjusted market
 *    price `X × 1.05 × (1 − x^60) + minSlotPrice`, where `X` is the price the
 *    current holder paid and `x = (timeSlot + 1) / 20`.  The previous holder
 *    is refunded `(1 − x) × X` LP tokens; the remainder is burned.
 *  - **Tailing (interval 19)** — treated as unowned; holder receives no refund
 *    and pays only minimum price, making rational behavior to let the slot
 *    expire.
 *
 *  Slot mutation and token burn are performed by the `updateSlot` lambda.
 *  `sfBidMin`/`sfBidMax` clamping is applied by `getPayPrice`.
 *
 *  Under `fixInnerObjTemplate`, `sfAuctionSlot` must already be present on the
 *  AMM ledger entry (eager initialization at AMM creation); if it is absent the
 *  function returns `tecINTERNAL`.  Before the amendment, the field is lazily
 *  created via `makeFieldPresent`.
 *
 *  @param ctx      Apply context providing the transaction, view, and journal.
 *  @param sb       Mutable sandbox view; committed by the caller only on success.
 *  @param account  Submitting account (the prospective new slot holder).
 *  @param j        Journal for diagnostic logging (unused; ctx.journal is used
 *      directly inside the function for consistency with other AMM helpers).
 *  @return A pair of `{TER, bool}` where the bool is true only on
 *      `tesSUCCESS` and signals the caller to commit the sandbox.
 */
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
    auto const discountedFee = (*ammSle)[sfTradingFee] / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION;
    auto const tradingFee = getFee((*ammSle)[sfTradingFee]);
    auto const minSlotPrice = lptAMMBalance * tradingFee / kAUCTION_SLOT_MIN_FEE_FRACTION;

    std::uint32_t constexpr kTAILING_SLOT = kAUCTION_SLOT_TIME_INTERVALS - 1;

    // Slot range is {0-19}; absent means the auction slot is unowned/expired.
    auto const timeSlot = ammAuctionTimeSlot(current, auctionSlot);

    /** Returns true when @p account owns an active, non-tailing slot.
     *
     *  Interval 19 (the tailing slot) is excluded: at that stage the holder
     *  pays only the minimum price and receives no refund, so the code falls
     *  through to the unowned path rather than computing a decay price for them.
     */
    auto validOwner = [&](AccountID const& account) {
        return timeSlot && *timeSlot < kTAILING_SLOT && sb.read(keylet::account(account));
    };

    /** Overwrite the `sfAuctionSlot` object and burn the bid's net cost.
     *
     *  Sets the new owner, expiration (`now + 86400 s`), discounted fee,
     *  slot price record, and auth accounts.  If @p fee is zero the
     *  `sfDiscountedFee` field is removed (zero-fee pools need no discount
     *  entry).  The net burn amount @p burn is adjusted for IOU 16-digit
     *  precision via `adjustLPTokens` before calling `redeemIOU` to destroy
     *  the tokens; `sfLPTokenBalance` on the AMM SLE is decremented to match.
     *
     *  @param fee       New discounted fee to record (`sfTradingFee / 10`).
     *  @param minPrice  Purchase price to record in `sfPrice` (LP tokens).
     *  @param burn      Net LP tokens to burn (pay price minus any refund).
     *  @return `tesSUCCESS` or a `tec*` code if the token burn fails.
     *
     *  @note The LCOV_EXCL_START guard around `saBurn >= lptAMMBalance` is
     *      mathematically unreachable given valid preclaim inputs; it exists as
     *      a regression sentinel for future numerical changes.
     */
    auto updateSlot = [&](std::uint32_t fee, Number const& minPrice, Number const& burn) -> TER {
        auctionSlot.setAccountID(sfAccount, account);
        auctionSlot.setFieldU32(sfExpiration, current + kTOTAL_TIME_SLOT_SECS);
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

    /** Clamp @p computedPrice to the caller-supplied `[sfBidMin, sfBidMax]` range.
     *
     *  Decision table:
     *  | sfBidMin | sfBidMax | computedPrice ≤ bidMax? | Result |
     *  |----------|----------|------------------------|--------|
     *  | present  | present  | yes  | `max(computedPrice, bidMin)` |
     *  | present  | present  | no   | `tecAMM_FAILED` |
     *  | present  | absent   | —    | `max(computedPrice, bidMin)` |
     *  | absent   | present  | yes  | `computedPrice` |
     *  | absent   | present  | no   | `tecAMM_FAILED` |
     *  | absent   | absent   | —    | `computedPrice` |
     *
     *  Returns `tecAMM_INVALID_TOKENS` when the resulting pay price would
     *  exceed the caller's current LP token holdings.
     *
     *  @param computedPrice  Market price derived from the slot state.
     *  @return The actual pay price, or an error TER wrapped in `Unexpected`.
     */
    auto getPayPrice = [&](Number const& computedPrice) -> Expected<Number, TER> {
        auto const payPrice = [&]() -> std::optional<Number> {
            if (bidMin && bidMax)
            {
                if (computedPrice <= *bidMax)
                    return std::max(computedPrice, Number(*bidMin));
                JLOG(ctx.journal.debug()) << "AMM Bid: not in range " << computedPrice << " "
                                          << *bidMin << " " << *bidMax;
                return std::nullopt;
            }
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

    // Unowned or expired slot: bidder pays minimum price; full amount is burned.
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
        STAmount const pricePurchased = auctionSlot[sfPrice];
        XRPL_ASSERT(timeSlot, "xrpl::applyBid : timeSlot is set");
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const fractionUsed = (Number(*timeSlot) + 1) / kAUCTION_SLOT_TIME_INTERVALS;
        auto const fractionRemaining = Number(1) - fractionUsed;
        auto const computedPrice = [&]() -> Number {
            auto const p105 = Number(105, -2);  // 1.05 premium prevents zero-cost squatting
            if (*timeSlot == 0)
                // Interval 0: decay term (1 - x^60) would be ~0.95, omitted for simplicity;
                // full 5% premium applied directly.
                return pricePurchased * p105 + minSlotPrice;
            // Intervals 1–18: decay makes outbidding cheaper the further into the slot.
            return pricePurchased * p105 * (1 - power(fractionUsed, 60)) + minSlotPrice;
        }();
        // NOLINTEND(bugprone-unchecked-optional-access)

        auto const payPrice = getPayPrice(computedPrice);

        if (!payPrice)
            return {payPrice.error(), false};

        // Refund the outgoing holder their unused slot fraction; burn the rest.
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
    Sandbox sb(&ctx_.view());

    auto const result = applyBid(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

void
AMMBid::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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
