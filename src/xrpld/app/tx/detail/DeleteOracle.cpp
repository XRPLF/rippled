#include <xrpld/app/tx/detail/DeleteOracle.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
DeleteOracle::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
DeleteOracle::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.exists(keylet::account(ctx.tx.getAccountID(sfAccount))))
        return terNO_ACCOUNT;  // LCOV_EXCL_LINE

    if (auto const sle = ctx.view.read(keylet::oracle(
            ctx.tx.getAccountID(sfAccount), ctx.tx[sfOracleDocumentID]));
        !sle)
    {
        JLOG(ctx.j.debug()) << "Oracle Delete: Oracle does not exist.";
        return tecNO_ENTRY;
    }
    else if (ctx.tx.getAccountID(sfAccount) != sle->getAccountID(sfOwner))
    {
        // this can't happen because of the above check
        // LCOV_EXCL_START
        JLOG(ctx.j.debug()) << "Oracle Delete: invalid account.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    return tesSUCCESS;
}

TER
DeleteOracle::deleteOracle(
    ApplyView& view,
    std::shared_ptr<SLE> const& sle,
    AccountID const& account,
    beast::Journal j)
{
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view.dirRemove(
            keylet::ownerDir(account), (*sle)[sfOwnerNode], sle->key(), true))
    {
        // LCOV_EXCL_START
        JLOG(j.fatal()) << "Unable to delete Oracle from owner.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }

    auto const sleOwner = view.peek(keylet::account(account));
    if (!sleOwner)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const count =
        sle->getFieldArray(sfPriceDataSeries).size() > 5 ? -2 : -1;

    adjustOwnerCount(view, sleOwner, count, j);

    view.erase(sle);

    return tesSUCCESS;
}

TER
DeleteOracle::doApply()
{
    if (auto sle = ctx_.view().peek(
            keylet::oracle(account_, ctx_.tx[sfOracleDocumentID])))
        return deleteOracle(ctx_.view(), sle, account_, j_);

    return tecINTERNAL;  // LCOV_EXCL_LINE
}

}  // namespace ripple
