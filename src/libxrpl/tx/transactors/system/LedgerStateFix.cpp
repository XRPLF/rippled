/** @file
 *  Implements the LedgerStateFix transactor (`ttLEDGER_STATE_FIX`), a
 *  privileged maintenance mechanism for correcting corrupted ledger state.
 *
 *  Currently supports one repair operation (`NfTokenPageLink`) that heals
 *  broken doubly-linked page chains in an account's NFToken directory.
 *  New repair types can be added by extending `FixType` and adding matching
 *  cases to each pipeline phase.
 *
 *  Gated on the `fixNFTokenPageLinks` amendment; the framework rejects the
 *  transaction type entirely before preflight when the amendment is inactive.
 */

#include <xrpl/tx/transactors/system/LedgerStateFix.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

NotTEC
LedgerStateFix::preflight(PreflightContext const& ctx)
{
    switch (static_cast<FixType>(ctx.tx[sfLedgerFixType]))
    {
        case FixType::NfTokenPageLink:
            if (!ctx.tx.isFieldPresent(sfOwner))
                return temINVALID;
            break;

        default:
            return tefINVALID_LEDGER_FIX_TYPE;
    }

    return tesSUCCESS;
}

/** Override that prices the transaction at one owner reserve rather than the
 *  standard base fee.
 *
 *  The elevated fee (identical to `AccountDelete`'s pricing) deters speculative
 *  repair submissions: an operator pays a full reserve increment whether or not
 *  `nft::repairNFTokenDirectoryLinks` finds anything to fix. This signals that
 *  the transaction should only be submitted when there is strong reason to
 *  believe the target account's NFToken directory is actually corrupt.
 */
XRPAmount
LedgerStateFix::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return calculateOwnerReserveFee(view, tx);
}

TER
LedgerStateFix::preclaim(PreclaimContext const& ctx)
{
    if (static_cast<FixType>(ctx.tx[sfLedgerFixType]) == FixType::NfTokenPageLink)
    {
        AccountID const owner{ctx.tx[sfOwner]};
        if (!ctx.view.read(keylet::account(owner)))
            return tecOBJECT_NOT_FOUND;

        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to preclaim.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

/** @note A `false` return from `nft::repairNFTokenDirectoryLinks` means the
 *  directory was already consistent — no corrections were applied. `doApply`
 *  maps this to `tecFAILED_PROCESSING` so the submitter is charged the owner
 *  reserve fee with no ledger state change; submitting a repair for a healthy
 *  account is not an internal error, but is a failed operation from the
 *  transaction's point of view.
 */
TER
LedgerStateFix::doApply()
{
    if (static_cast<FixType>(ctx_.tx[sfLedgerFixType]) == FixType::NfTokenPageLink)
    {
        if (!nft::repairNFTokenDirectoryLinks(view(), ctx_.tx[sfOwner]))
            return tecFAILED_PROCESSING;

        return tesSUCCESS;
    }

    // preflight is supposed to verify that only valid FixTypes get to doApply.
    return tecINTERNAL;  // LCOV_EXCL_LINE
}

void
LedgerStateFix::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
LedgerStateFix::finalizeInvariants(
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
