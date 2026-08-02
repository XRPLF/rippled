#include <xrpl/tx/transactors/payment_channel/PaymentChannelFund.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <memory>
#include <variant>

namespace xrpl {

TxConsequences
PaymentChannelFund::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, isXRP(ctx.tx[sfAmount]) ? ctx.tx[sfAmount].xrp() : beast::kZero};
}

bool
PaymentChannelFund::checkExtraFeatures(PreflightContext const& ctx)
{
    // Only require featureMPTokensV1 when the funding amount is an MPT and
    // fixCleanup3_2_0 is active; XRP/IOU channels are unaffected by this gate.
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx[sfAmount].holds<MPTIssue>())
        return ctx.rules.enabled(featureMPTokensV1);
    return true;
}

NotTEC
PaymentChannelFund::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx[sfChannel] == beast::kZero)
        return temMALFORMED;

    auto const amount = ctx.tx[sfAmount];
    if (!isXRP(amount))
    {
        if (!ctx.rules.enabled(featureTokenPaychan))
            return temBAD_AMOUNT;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return payChanAmountPreflightHelper<T>(ctx.rules, amount);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    else if (amount <= beast::kZero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
PaymentChannelFund::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureTokenPaychan))
    {
        Keylet const k{ltPAYCHAN, ctx.tx[sfChannel]};
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_ENTRY;

        // The funded asset must match the channel's asset; otherwise the
        // STAmount accumulation in doApply would throw on mismatched issues.
        if (ctx.tx[sfAmount].asset() != (*slep)[sfAmount].asset())
            return tecWRONG_ASSET;
    }

    return tesSUCCESS;
}

TER
PaymentChannelFund::doApply()
{
    Keylet const k(ltPAYCHAN, ctx_.tx[sfChannel]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_ENTRY;

    AccountID const src = (*slep)[sfAccount];
    AccountID const dst = (*slep)[sfDestination];
    auto const curExpiration = (*slep)[~sfExpiration];

    if (isChannelExpired(ctx_.view(), (*slep)[~sfCancelAfter]) ||
        isChannelExpired(ctx_.view(), curExpiration))
    {
        return closeChannel(
            slep,
            ctx_.getApplyViewContext(),
            k.key,
            accountID_,
            ctx_.registry.get().getJournal("View"));
    }

    if (src != accountID_)
    {
        // only the owner can add funds or extend
        return tecNO_PERMISSION;
    }

    if (auto newExpiration = ctx_.tx[~sfExpiration])
    {
        auto minExpiration = saturatingAdd(
            ctx_.view().rules(),
            ctx_.view().header().parentCloseTime.time_since_epoch().count(),
            (*slep)[sfSettleDelay]);
        if (curExpiration && *curExpiration < minExpiration)
            minExpiration = *curExpiration;

        if (*newExpiration < minExpiration)
        {
            return ctx_.view().rules().enabled(fixCleanup3_2_0) ? TER{tecNO_PERMISSION}
                                                                : TER{temBAD_EXPIRATION};
        }
        (*slep)[~sfExpiration] = *newExpiration;
        ctx_.view().update(slep);
    }

    auto const sle = ctx_.view().peek(keylet::account(accountID_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const amount = ctx_.tx[sfAmount];
    auto const& chanAmt = slep->getFieldAmount(sfAmount);

    // Asset consistency between the funded amount and the channel was
    // validated in preclaim.
    XRPL_ASSERT(
        amount.asset() == chanAmt.asset(),
        "xrpl::PaymentChannelFund::doApply : amount matches channel asset");

    {
        // Check reserve and funds availability
        STAmount const balance = (*sle)[sfBalance];
        if (auto const ret = checkReserve(ctx_.getApplyViewContext(), sle, balance.xrp(), {}, j_);
            !isTesSuccess(ret))
            return ret;

        // After locking sfAmount in the channel, the source must still meet
        // its own reserve floor. We compare directly (rather than via
        // checkReserve) because that helper diverts to the sponsor's balance
        // when a sponsor is present and would ignore the source's post-lock
        // balance entirely. Funding an existing channel adds no owned object,
        // so there is no owner-count delta.
        if (isXRP(amount) && balance < accountReserve(ctx_.view(), sle, j_) + amount)
            return tecUNFUNDED;
    }

    // do not allow adding funds if dst does not exist
    if (!ctx_.view().read(keylet::account(dst)))
    {
        return tecNO_DST;
    }

    if (!isXRP(amount))
    {
        // Funding is subject to the same issuer controls (locking opt-in,
        // authorization, freeze/lock, transferability, spendable balance)
        // as channel creation.
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowLockPreclaimHelper<T>(ctx_.view(), accountID_, dst, amount, j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;

        // Guard the channel-amount accumulation itself: a small IOU amount
        // added to a much larger channel amount would be rounded away below
        // after the source had already been debited. MPT amounts are exact
        // integers bounded by the outstanding supply, so only IOUs can lose
        // precision here.
        if (amount.holds<Issue>() && !canAdd(chanAmt, amount))
            return tecPRECISION_LOSS;
    }

    if (isXRP(amount))
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] - amount;
    }
    else
    {
        auto const& issuer = amount.getIssuer();
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowLockApplyHelper<T>(ctx_.view(), issuer, accountID_, amount, j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    (*slep)[sfAmount] = chanAmt + amount;
    ctx_.view().update(slep);
    ctx_.view().update(sle);

    return tesSUCCESS;
}

void
PaymentChannelFund::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
PaymentChannelFund::finalizeInvariants(
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
