#include <xrpl/tx/transactors/system/LedgerStateFix.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
LedgerStateFix::preflight(PreflightContext const& ctx)
{
    switch (ctx.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink:
            if (!ctx.tx.isFieldPresent(sfOwner))
                return temINVALID;
            break;

        default:
            return tefINVALID_LEDGER_FIX_TYPE;
    }

    return tesSUCCESS;
}

XRPAmount
LedgerStateFix::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for LedgerStateFix is one owner reserve, just like
    // the fee for AccountDelete.
    return calculateOwnerReserveFee(view, tx);
}

TER
LedgerStateFix::preclaim(PreclaimContext const& ctx)
{
    if (ctx.tx[sfLedgerFixType] == FixType::nfTokenPageLink)
    {
        AccountID const owner{ctx.tx[sfOwner]};
        if (!ctx.view.read(keylet::account(owner)))
            return tecOBJECT_NOT_FOUND;

        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to preclaim.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

TER
LedgerStateFix::doApply()
{
    if (ctx_.tx[sfLedgerFixType] == FixType::nfTokenPageLink)
    {
        if (!nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner]))
            return tecFAILED_PROCESSING;

        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to doApply.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

}  // namespace xrpl
