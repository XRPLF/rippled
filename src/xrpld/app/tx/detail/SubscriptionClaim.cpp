#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/misc/SubscriptionHelpers.h>
#include <xrpld/app/paths/Flow.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/app/tx/detail/SubscriptionClaim.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
SubscriptionClaim::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureSubscription))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return preflight2(ctx);
}

TER
SubscriptionClaim::preclaim(PreclaimContext const& ctx)
{
    auto const sleSub = ctx.view.read(
        keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.trace())
            << "SubscriptionClaim: Subscription does not exist.";
        return tecNO_ENTRY;
    }

    // Only claim a subscription with this account as the destination.
    AccountID const dest = sleSub->getAccountID(sfDestination);
    if (ctx.tx[sfAccount] != dest)
    {
        JLOG(ctx.j.trace()) << "SubscriptionClaim: Cashing a subscription with "
                               "wrong Destination.";
        return tecNO_PERMISSION;
    }
    AccountID const account = sleSub->getAccountID(sfAccount);
    if (account == dest)
    {
        JLOG(ctx.j.trace()) << "SubscriptionClaim: Malformed transaction: "
                               "Cashing subscription to self.";
        return tecINTERNAL;
    }
    {
        auto const sleSrc = ctx.view.read(keylet::account(account));
        auto const sleDst = ctx.view.read(keylet::account(dest));
        if (!sleSrc || !sleDst)
        {
            JLOG(ctx.j.trace())
                << "SubscriptionClaim: source or destination not in ledger";
            return tecNO_ENTRY;
        }
    }

    {
        STAmount const amount = ctx.tx.getFieldAmount(sfAmount);
        STAmount const sleAmount = sleSub->getFieldAmount(sfAmount);
        if (amount.asset() != sleAmount.asset())
        {
            JLOG(ctx.j.trace()) << "SubscriptionClaim: Subscription claim does "
                                   "not match subscription currency.";
            return tecWRONG_ASSET;
        }

        if (amount > sleAmount)
        {
            JLOG(ctx.j.trace()) << "SubscriptionClaim: Claim amount exceeds "
                                   "subscription amount.";
            return temBAD_AMOUNT;
        }

        // Time/period context
        std::uint32_t const currentTime =
            ctx.view.info().parentCloseTime.time_since_epoch().count();
        std::uint32_t const nextClaimTime =
            sleSub->getFieldU32(sfNextClaimTime);
        std::uint32_t const frequency = sleSub->getFieldU32(sfFrequency);

        // Determine effective available balance:
        // - If we have crossed into a later period AND the previous period had
        // a partial
        //   balance remaining (carryover not allowed), then the effective
        //   period rolls forward once and its balance resets to sleAmount.
        // - Otherwise we operate on the period at nextClaimTime with its stored
        // balance.
        STAmount balance = sleSub->getFieldAmount(sfBalance);
        bool const arrears = currentTime >= nextClaimTime + frequency;
        if (arrears && balance != sleAmount)
        {
            // We will effectively operate on (nextClaimTime + frequency) with a
            // full balance.
            balance = sleAmount;
        }

        if (amount > balance)
        {
            JLOG(ctx.j.trace())
                << "SubscriptionClaim: Claim amount exceeds remaining "
                   "balance for this period.";
            return tecINSUFFICIENT_FUNDS;
        }

        if (isXRP(amount))
        {
            if (xrpLiquid(ctx.view, account, 0, ctx.j) < amount)
                return tecINSUFFICIENT_FUNDS;
        }
        else
        {
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return canTransferTokenHelper<T>(
                            ctx.view, account, dest, amount, ctx.j);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }

    // Must be at or past the start of the effective period.
    if (!hasExpired(ctx.view, sleSub->getFieldU32(sfNextClaimTime)))
    {
        JLOG(ctx.j.trace()) << "SubscriptionClaim: The subscription has not "
                               "reached the next claim time.";
        return tecTOO_SOON;
    }

    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
doTransferTokenHelper(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
TER
doTransferTokenHelper<Issue>(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    Keylet const trustLineKey = keylet::line(receiver, amount.issue());
    bool const recvLow = issuer > receiver;

    // Review Note: We could remove this and just say to use batch to auth the
    // token first
    if (!view.exists(trustLineKey) && createAsset && issuer != receiver)
    {
        // Can the account cover the trust line's reserve?
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            JLOG(journal.trace())
                << "doTransferTokenHelper: Trust line does not exist. "
                   "Insufficent reserve to create line.";

            return tecNO_LINE_INSUF_RESERVE;
        }

        Currency const currency = amount.getCurrency();
        STAmount initialBalance(amount.issue());
        initialBalance.setIssuer(noAccount());

        // clang-format off
        if (TER const ter = trustCreate(
                view,                            // payment sandbox
                recvLow,                        // is dest low?
                issuer,                         // source
                receiver,                           // destination
                trustLineKey.key,               // ledger index
                sleDest,                        // Account to add to
                false,                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,                          // freeze trust line
                false,                          // deep freeze trust line
                initialBalance,                 // zero initial balance
                Issue(currency, receiver),   // limit of zero
                0,                              // quality in
                0,                              // quality out
                journal);                       // journal
            !isTesSuccess(ter))
        {
            JLOG(journal.trace()) << "doTransferTokenHelper: Failed to create trust line: " << transToken(ter);
            return ter;
        }
        // clang-format on

        view.update(sleDest);
    }

    if (!view.exists(trustLineKey) && issuer != receiver)
        return tecNO_LINE;

    auto const ter = accountSend(
        view, sender, receiver, amount, journal, WaiveTransferFee::No);
    if (ter != tesSUCCESS)
    {
        JLOG(journal.trace()) << "doTransferTokenHelper: Failed to send token: "
                              << transToken(ter);
        return ter;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

template <>
TER
doTransferTokenHelper<MPTIssue>(
    ApplyView& view,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    auto const mptID = amount.get<MPTIssue>().getMptID();
    auto const issuanceKey = keylet::mptIssuance(mptID);
    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) && createAsset)
    {
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            JLOG(journal.trace())
                << "doTransferTokenHelper: MPT does not exist. "
                   "Insufficent reserve to create MPT.";
            return tecINSUFFICIENT_RESERVE;
        }

        if (auto const ter =
                MPTokenAuthorize::createMPToken(view, mptID, receiver, 0);
            !isTesSuccess(ter))
        {
            JLOG(journal.trace())
                << "doTransferTokenHelper: Failed to create MPT: "
                << transToken(ter);
            return ter;
        }

        // Update owner count.
        adjustOwnerCount(view, sleDest, 1, journal);
    }

    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)))
    {
        JLOG(journal.trace()) << "doTransferTokenHelper: MPT does not exist.";
        return tecNO_PERMISSION;
    }

    auto const ter = accountSend(
        view, sender, receiver, amount, journal, WaiveTransferFee::No);
    if (ter != tesSUCCESS)
    {
        JLOG(journal.trace())
            << "doTransferTokenHelper: Failed to send MPT: " << transToken(ter);
        return ter;  // LCOV_EXCL_LINE
    }

    return tesSUCCESS;
}

TER
SubscriptionClaim::doApply()
{
    PaymentSandbox psb(&ctx_.view());
    auto viewJ = ctx_.app.journal("View");

    auto sleSub =
        psb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(j_.trace()) << "SubscriptionClaim: Subscription does not exist.";
        return tecINTERNAL;
    }

    AccountID const account = sleSub->getAccountID(sfAccount);
    if (!psb.exists(keylet::account(account)))
    {
        JLOG(j_.trace()) << "SubscriptionClaim: Account does not exist.";
        return tecINTERNAL;
    }

    AccountID const dest = sleSub->getAccountID(sfDestination);
    if (!psb.exists(keylet::account(dest)))
    {
        JLOG(j_.trace()) << "SubscriptionClaim: Account does not exist.";
        return tecINTERNAL;
    }

    if (dest != ctx_.tx.getAccountID(sfAccount))
    {
        JLOG(j_.trace()) << "SubscriptionClaim: Account is not the "
                            "destination of the subscription.";
        return tecNO_PERMISSION;
    }

    STAmount const sleAmount = sleSub->getFieldAmount(sfAmount);
    STAmount const deliverAmount = ctx_.tx.getFieldAmount(sfAmount);

    // Pull current period info
    std::uint32_t const currentTime =
        psb.info().parentCloseTime.time_since_epoch().count();
    std::uint32_t nextClaimTime = sleSub->getFieldU32(sfNextClaimTime);
    std::uint32_t const frequency = sleSub->getFieldU32(sfFrequency);

    STAmount availableBalance = sleSub->getFieldAmount(sfBalance);
    bool const arrears = currentTime >= nextClaimTime + frequency;

    // If we crossed into a later period and the previous period was partially
    // used, forfeit the leftover and roll forward exactly one period; reset the
    // balance.
    if (arrears && availableBalance != sleAmount)
    {
        nextClaimTime += frequency;
        availableBalance = sleAmount;

        // Reflect the rollover immediately in the SLE so subsequent logic is
        // consistent.
        sleSub->setFieldU32(sfNextClaimTime, nextClaimTime);
        sleSub->setFieldAmount(sfBalance, availableBalance);
    }

    // Enforce available balance for the effective period.
    if (deliverAmount > availableBalance)
    {
        JLOG(j_.trace()) << "SubscriptionClaim: Claim amount exceeds remaining "
                         << "balance for this period.";
        return tecINTERNAL;
    }

    // Perform the transfer
    if (isXRP(deliverAmount))
    {
        if (TER const ter{
                transferXRP(psb, account, dest, deliverAmount, viewJ)};
            ter != tesSUCCESS)
        {
            return ter;
        }
    }
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return doTransferTokenHelper<T>(
                        psb,
                        psb.peek(keylet::account(dest)),
                        mPriorBalance,
                        deliverAmount,
                        deliverAmount.getIssuer(),
                        account,
                        dest,
                        true,  // create asset
                        viewJ);
                },
                deliverAmount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    // Update balance and period pointer
    STAmount const newBalance = availableBalance - deliverAmount;

    if (newBalance == sleAmount.zeroed())
    {
        // Full period claimed: advance exactly one period and reset next period
        // balance.
        nextClaimTime += frequency;
        sleSub->setFieldU32(sfNextClaimTime, nextClaimTime);
        sleSub->setFieldAmount(sfBalance, sleAmount);
    }
    else
    {
        // Partial claim within the same effective period.
        sleSub->setFieldAmount(sfBalance, newBalance);
        // Do not advance nextClaimTime; if we had a rollover-forfeit above,
        // we already moved nextClaimTime forward exactly once.
    }

    psb.update(sleSub);

    if (sleSub->isFieldPresent(sfExpiration) &&
        psb.info().parentCloseTime.time_since_epoch().count() >=
            sleSub->getFieldU32(sfExpiration))
    {
        psb.erase(sleSub);
    }

    psb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
