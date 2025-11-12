#include <xrpld/app/tx/detail/LedgerStateFix.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

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
    switch (ctx.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink: {
            AccountID const owner{ctx.tx[sfOwner]};
            if (!ctx.view.read(keylet::account(owner)))
                return tecOBJECT_NOT_FOUND;

            return tesSUCCESS;
        }
    }

    // preflight is supposed to verify that only valid FixTypes get to preclaim.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

TER
LedgerStateFix::doApply()
{
    switch (ctx_.tx[sfLedgerFixType])
    {
        case FixType::nfTokenPageLink:
            if (!nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner]))
                return tecFAILED_PROCESSING;

            return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to doApply.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

}  // namespace ripple
