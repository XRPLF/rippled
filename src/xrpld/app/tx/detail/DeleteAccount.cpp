#include <xrpld/app/tx/detail/DID.h>
#include <xrpld/app/tx/detail/DelegateSet.h>
#include <xrpld/app/tx/detail/DeleteAccount.h>
#include <xrpld/app/misc/DeleteUtils.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/mulDiv.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

namespace ripple {

bool
DeleteAccount::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return false;

    return true;
}

NotTEC
DeleteAccount::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
        // An account cannot be deleted and give itself the resulting XRP.
        return temDST_IS_SRC;

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j);
        !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
DeleteAccount::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AccountDelete is one owner reserve.
    return calculateOwnerReserveFee(view, tx);
}

TER
DeleteAccount::preclaim(PreclaimContext const& ctx)
{
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dest{ctx.tx[sfDestination]};

    if (auto const res = deletePreclaim(ctx, 255, account, dest);
        !isTesSuccess(res))
        return res;
    return tesSUCCESS;
}

TER
DeleteAccount::doApply()
{
    AccountID const account{ctx_.tx[sfAccount]};
    AccountID const dest{ctx_.tx[sfDestination]};
    if (auto const res = deleteDoApply(ctx_, mSourceBalance, account, dest);
        !isTesSuccess(res))
        return res;
    return tesSUCCESS;
}

}  // namespace ripple
