#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/token/MPTokenIssuanceDestroy.h>

namespace xrpl {

NotTEC
MPTokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    MPTokenIssuance const mptIssuance(ctx.view, ctx.tx[sfMPTokenIssuanceID]);
    if (!mptIssuance)
        return tecOBJECT_NOT_FOUND;

    // ensure it is issued by the tx submitter
    if (mptIssuance.getIssuer() != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    // ensure it has no outstanding balances
    if (mptIssuance->at(sfOutstandingAmount) != 0)
        return tecHAS_OBLIGATIONS;

    if (mptIssuance->at(~sfLockedAmount).value_or(0) != 0)
        return tecHAS_OBLIGATIONS;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

TER
MPTokenIssuanceDestroy::doApply()
{
    WritableMPTokenIssuance mptIssuance(view(), ctx_.tx[sfMPTokenIssuanceID]);
    if (accountID_ != mptIssuance.getIssuer())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (!view().dirRemove(
            keylet::ownerDir(accountID_), (*mptIssuance)[sfOwnerNode], mptIssuance->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    mptIssuance.erase();

    WritableAccountRoot acct(accountID_, view());
    acct.adjustOwnerCount(-1, j_);

    return tesSUCCESS;
}

}  // namespace xrpl
