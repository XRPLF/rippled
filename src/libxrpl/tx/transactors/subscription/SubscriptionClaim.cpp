#include <xrpl/tx/transactors/subscription/SubscriptionClaim.h>

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/SubscriptionHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <variant>

namespace xrpl {

NotTEC
SubscriptionClaim::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
SubscriptionClaim::preclaim(PreclaimContext const& ctx)
{
    auto const sleSub = ctx.view.read(keylet::subscription(ctx.tx.getFieldH256(sfSubscriptionID)));
    if (!sleSub)
    {
        JLOG(ctx.j.trace()) << "SubscriptionClaim: Subscription does not exist.";
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
            JLOG(ctx.j.trace()) << "SubscriptionClaim: source or destination not in ledger";
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
            return tecLIMIT_EXCEEDED;
        }

        // Time/period context
        std::uint32_t const currentTime =
            ctx.view.header().parentCloseTime.time_since_epoch().count();
        std::uint32_t const nextClaimTime = sleSub->getFieldU32(sfNextClaimTime);
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
            JLOG(ctx.j.trace()) << "SubscriptionClaim: Claim amount exceeds remaining "
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
                        return canTransferTokenHelper<T>(ctx.view, account, dest, amount, ctx.j);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }

    // An expired subscription can no longer be claimed; it can only be
    // cancelled.
    if (hasExpired(ctx.view, (*sleSub)[~sfExpiration]))
    {
        JLOG(ctx.j.trace()) << "SubscriptionClaim: The subscription has expired.";
        return tecEXPIRED;
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

TER
SubscriptionClaim::doApply()
{
    PaymentSandbox psb(&ctx_.view());
    auto viewJ = ctx_.registry.get().getJournal("View");

    auto sleSub = psb.peek(keylet::subscription(ctx_.tx.getFieldH256(sfSubscriptionID)));
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
    std::uint32_t const currentTime = psb.header().parentCloseTime.time_since_epoch().count();
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
        if (TER const ter{transferXRP(psb, account, dest, deliverAmount, viewJ)}; ter != tesSUCCESS)
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
                        preFeeBalance_,
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

    // Metered accounting: advance/reset the period. Unmetered subscriptions
    // (Frequency == 0) cap each claim at Amount and never touch Balance or
    // NextClaimTime.
    if (frequency != 0)
    {
        STAmount const newBalance = availableBalance - deliverAmount;
        if (newBalance == sleAmount.zeroed())
        {
            // Full period claimed: advance exactly one period and reset next
            // period balance.
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
    }

    // Single-use subscriptions are removed on the first successful claim,
    // regardless of Frequency or whether the claim was partial.
    if (sleSub->isFlag(lsfSingleUse))
    {
        if (auto const ter = deleteSubscription(psb, sleSub, viewJ); !isTesSuccess(ter))
            return ter;
    }
    else
    {
        psb.update(sleSub);
    }

    psb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
SubscriptionClaim::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
SubscriptionClaim::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
