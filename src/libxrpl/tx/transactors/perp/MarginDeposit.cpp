#include <xrpl/tx/transactors/perp/MarginDeposit.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

bool
MarginDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
MarginDeposit::getFlagsMask(PreflightContext const& ctx)
{
    return tfMarginDepositMask;
}

NotTEC
MarginDeposit::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfExchangeID] == beast::kZero)
        return temMALFORMED;

    if (tx[sfAmount] <= beast::kZero)
        return temBAD_AMOUNT;

    // A cover deposit touches no margin account.
    if (tx.isFlag(tfPerpCover) && tx.isFieldPresent(sfSubaccount))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MarginDeposit::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
MarginDeposit::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
MarginDeposit::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
