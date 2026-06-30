#include <xrpl/tx/transactors/payment_channel/PaymentChannelFund.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

namespace xrpl {

TxConsequences
PaymentChannelFund::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfAmount].xrp()};
}

NotTEC
PaymentChannelFund::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx[sfChannel] == beast::kZero)
        return temMALFORMED;

    if (!isXRP(ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::kZero))
        return temBAD_AMOUNT;

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
    auto const txAccount = ctx_.tx[sfAccount];
    auto const curExpiration = (*slep)[~sfExpiration];

    if (isChannelExpired(ctx_.view(), (*slep)[~sfCancelAfter]) ||
        isChannelExpired(ctx_.view(), curExpiration))
    {
        return closeChannel(slep, ctx_.view(), k.key, ctx_.registry.get().getJournal("View"));
    }

    if (src != txAccount)
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

    auto const sle = ctx_.view().peek(keylet::account(txAccount));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        // Check reserve and funds availability
        auto const balance = (*sle)[sfBalance];
        auto const reserve = ctx_.view().fees().accountReserve((*sle)[sfOwnerCount]);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx_.tx[sfAmount])
            return tecUNFUNDED;
    }

    // do not allow adding funds if dst does not exist
    if (AccountID const dst = (*slep)[sfDestination]; !ctx_.view().read(keylet::account(dst)))
    {
        return tecNO_DST;
    }

    (*slep)[sfAmount] = (*slep)[sfAmount] + ctx_.tx[sfAmount];
    ctx_.view().update(slep);

    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
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
