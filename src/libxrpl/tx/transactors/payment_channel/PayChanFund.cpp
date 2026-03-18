#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/tx/transactors/payment_channel/PayChanFund.h>

#include <libxrpl/tx/transactors/payment_channel/PayChanHelpers.h>

namespace xrpl {

TxConsequences
PayChanFund::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfAmount].xrp()};
}

NotTEC
PayChanFund::preflight(PreflightContext const& ctx)
{
    if (!isXRP(ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::zero))
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
PayChanFund::doApply()
{
    Keylet const k(ltPAYCHAN, ctx_.tx[sfChannel]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_ENTRY;

    AccountID const src = (*slep)[sfAccount];
    auto const txAccount = ctx_.tx[sfAccount];
    auto const expiration = (*slep)[~sfExpiration];

    {
        auto const cancelAfter = (*slep)[~sfCancelAfter];
        auto const closeTime = ctx_.view().header().parentCloseTime.time_since_epoch().count();
        if ((cancelAfter && closeTime >= *cancelAfter) || (expiration && closeTime >= *expiration))
            return closeChannel(slep, ctx_.view(), k.key, ctx_.registry.journal("View"));
    }

    if (src != txAccount)
    {
        // only the owner can add funds or extend
        return tecNO_PERMISSION;
    }

    if (auto extend = ctx_.tx[~sfExpiration])
    {
        auto minExpiration = ctx_.view().header().parentCloseTime.time_since_epoch().count() +
            (*slep)[sfSettleDelay];
        if (expiration && *expiration < minExpiration)
            minExpiration = *expiration;

        if (*extend < minExpiration)
            return temBAD_EXPIRATION;
        (*slep)[~sfExpiration] = *extend;
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

}  // namespace xrpl
