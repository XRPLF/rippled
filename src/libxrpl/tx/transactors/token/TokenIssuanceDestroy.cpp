#include <xrpl/tx/transactors/token/TokenIssuanceDestroy.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

NotTEC
TokenIssuanceDestroy::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
TokenIssuanceDestroy::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    Currency const currency = ctx.tx[sfCurrency];

    auto const sle = ctx.view.read(keylet::tokenIssuance(account, currency));
    if (!sle)
        return tecNO_ENTRY;

    if (sle->isFlag(lsfTokenWrapped))
        return tecNO_PERMISSION;

    if (Number const issued = sle->at(sfIssuedAmount); issued != Number{})
        return tecHAS_OBLIGATIONS;

    if (auto const mptId = sle->at(~sfMPTokenIssuanceID))
    {
        // A missing bound issuance leaves nothing outstanding on that side.
        if (auto const sleMpt = ctx.view.read(keylet::mptokenIssuance(*mptId)); sleMpt &&
            (sleMpt->at(sfOutstandingAmount) != 0 || sleMpt->at(~sfLockedAmount).value_or(0) != 0))
            return tecHAS_OBLIGATIONS;
    }

    return tesSUCCESS;
}

TER
TokenIssuanceDestroy::doApply()
{
    Currency const currency = ctx_.tx[sfCurrency];

    auto const sle = view().peek(keylet::tokenIssuance(accountID_, currency));
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Unbind the MPT issuance; with both sides at zero this is harmless and
    // lets the MPT issuance outlive the TokenIssuance.
    if (auto const mptId = sle->at(~sfMPTokenIssuanceID))
    {
        if (auto const sleMpt = view().peek(keylet::mptokenIssuance(*mptId)))
        {
            sleMpt->makeFieldAbsent(sfTokenIssuanceID);
            view().update(sleMpt);
        }
    }

    if (!view().dirRemove(keylet::ownerDir(accountID_), (*sle)[sfOwnerNode], sle->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    decreaseOwnerCountForObject(view(), accountID_, sle, 1, j_);
    view().erase(sle);

    return tesSUCCESS;
}

}  // namespace xrpl
