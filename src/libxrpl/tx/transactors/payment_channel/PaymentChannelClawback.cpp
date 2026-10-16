#include <xrpl/tx/transactors/payment_channel/PaymentChannelClawback.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
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
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
PaymentChannelClawback::preflight(PreflightContext const& ctx)
{
    if (auto const amount = ctx.tx[~sfAmount])
    {
        if (amount->native() || *amount <= beast::kZero)
            return temBAD_AMOUNT;

        if (amount->holds<MPTIssue>() && amount->mpt() > MPTAmount{kMaxMpTokenAmount})
            return temBAD_AMOUNT;

        if (amount->holds<Issue>() && badCurrency() == amount->get<Issue>().currency)
            return temBAD_CURRENCY;
    }

    return tesSUCCESS;
}

TER
PaymentChannelClawback::preclaim(PreclaimContext const& ctx)
{
    Keylet const k{ltPAYCHAN, ctx.tx[sfChannel]};
    auto const slep = ctx.view.read(k);
    if (!slep)
        return tecNO_TARGET;

    auto const& amount = slep->getFieldAmount(sfAmount);

    // XRP cannot be clawed back.
    if (isXRP(amount))
        return tecNO_PERMISSION;

    auto const& issuer = amount.getIssuer();

    // Only the issuer of the locked asset may claw it back.
    if (ctx.tx[sfAccount] != issuer)
        return tecNO_PERMISSION;

    // Defensive: the issuer can never be the channel owner.
    if ((*slep)[sfAccount] == issuer)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    if (auto const clawAmount = ctx.tx[~sfAmount];
        clawAmount && clawAmount->asset() != amount.asset())
        return tecWRONG_ASSET;

    if (amount.holds<Issue>())
    {
        auto const sleIssuer = ctx.view.read(keylet::account(issuer));
        if (!sleIssuer)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        if (!sleIssuer->isFlag(lsfAllowTrustLineClawback) || sleIssuer->isFlag(lsfNoFreeze))
            return tecNO_PERMISSION;
    }
    else
    {
        auto const sleIssuance =
            ctx.view.read(keylet::mptokenIssuance(amount.get<MPTIssue>().getMptID()));
        if (!sleIssuance)
            return tecOBJECT_NOT_FOUND;  // LCOV_EXCL_LINE

        if (!sleIssuance->isFlag(lsfMPTCanClawback))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
PaymentChannelClawback::doApply()
{
    Keylet const k{ltPAYCHAN, ctx_.tx[sfChannel]};
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const& chanAmt = slep->getFieldAmount(sfAmount);
    auto const& chanBalance = slep->getFieldAmount(sfBalance);

    // Only the unclaimed remainder can be clawed; the destination's earned
    // balance (sfBalance) is untouched.
    STAmount const lockedRemaining = chanAmt - chanBalance;
    if (lockedRemaining <= beast::kZero)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const clawAmount = ctx_.tx[~sfAmount];
    bool const full = !clawAmount || *clawAmount >= lockedRemaining;

    // For a full claw, set the new amount to sfBalance directly rather than
    // computing chanAmt - lockedRemaining: for IOUs the latter is a - (a - b)
    // and can round, leaving a dust channel undeleted or pushing sfAmount
    // below sfBalance. claw is the exact amount removed.
    STAmount const newAmount = full ? chanBalance : STAmount{chanAmt - *clawAmount};
    STAmount const claw = full ? lockedRemaining : *clawAmount;

    AccountID const owner = (*slep)[sfAccount];

    // MPT: release the locked accounting back to the issuer (a redemption).
    // IOU: the locked value already sits with the issuer, so only the channel
    // obligation shrinks.
    if (chanAmt.holds<MPTIssue>())
    {
        if (auto const ret = unlockEscrowMPT(ctx_.view(), owner, accountID_, claw, claw, j_);
            !isTesSuccess(ret))
            return ret;  // LCOV_EXCL_LINE
    }

    (*slep)[sfAmount] = newAmount;

    // Fully drained: nothing remains beyond the paid-out balance, close the
    // channel (the zero remainder makes closeChannel's refund a no-op).
    if (full)
    {
        return closeChannel(
            slep,
            ctx_.getApplyViewContext(),
            k.key,
            accountID_,
            ctx_.registry.get().getJournal("View"));
    }

    ctx_.view().update(slep);

    return tesSUCCESS;
}

void
PaymentChannelClawback::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
PaymentChannelClawback::finalizeInvariants(
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
