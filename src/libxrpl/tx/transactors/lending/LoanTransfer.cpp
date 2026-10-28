#include <xrpl/tx/transactors/lending/LoanTransfer.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

NotTEC
LoanTransfer::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

NotTEC
LoanTransfer::checkSign(PreclaimContext const& ctx)
{
    return Transactor::checkSign(ctx);
}

XRPAmount
LoanTransfer::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

TER
LoanTransfer::preclaim(PreclaimContext const& ctx)
{
    return tecNO_ENTRY;
}

TER
LoanTransfer::doApply()
{
    return tefINTERNAL;
}

void
LoanTransfer::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanTransfer::finalizeInvariants(
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
