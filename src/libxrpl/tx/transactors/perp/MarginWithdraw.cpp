#include <xrpl/tx/transactors/perp/MarginWithdraw.h>

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
MarginWithdraw::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
MarginWithdraw::getFlagsMask(PreflightContext const& ctx)
{
    return tfMarginWithdrawMask;
}

NotTEC
MarginWithdraw::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfExchangeID] == beast::kZero)
        return temMALFORMED;

    bool const cover = tx.isFlag(tfPerpCover);
    bool const ownerFees = tx.isFlag(tfPerpOwnerFees);
    bool const deleteAccount = tx.isFlag(tfPerpDeleteAccount);
    if (static_cast<int>(cover) + static_cast<int>(ownerFees) + static_cast<int>(deleteAccount) > 1)
        return temINVALID_FLAG;

    // Cover and owner-fee withdrawals touch no margin account.
    if ((cover || ownerFees) && tx.isFieldPresent(sfSubaccount))
        return temMALFORMED;

    // Only a withdrawal that deletes the account may move nothing.
    auto const amount = tx[sfAmount];
    if (amount < beast::kZero || (amount == beast::kZero && !deleteAccount))
        return temBAD_AMOUNT;

    if (auto const destination = tx[~sfDestination]; destination && *destination == beast::kZero)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
MarginWithdraw::doApply()
{
    // The transaction's behaviour is not built yet, so every application fails.
    return tecINTERNAL;
}

void
MarginWithdraw::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
MarginWithdraw::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
