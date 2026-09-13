#include <xrpl/tx/transactors/token/TokenIssuanceSet.h>

#include <xrpl/ledger/helpers/TokenIssuanceHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

bool
TokenIssuanceSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfMPTokenIssuanceID) || ctx.rules.enabled(featureMPTokensV1);
}

std::uint32_t
TokenIssuanceSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfTokenIssuanceSetMask;
}

NotTEC
TokenIssuanceSet::preflight(PreflightContext const& ctx)
{
    auto const flags = ctx.tx.getFlags();

    if ((flags & tfTokenLock) && (flags & tfTokenUnlock))
        return temINVALID_FLAG;

    // Renouncing the lock while requesting it is contradictory.
    if ((flags & tfTokenCannotLock) && (flags & tfTokenLock))
        return temINVALID_FLAG;

    if (auto const fee = ctx.tx[~sfTransferFee]; fee && *fee > kMaxTransferFee)
        return temBAD_TRANSFER_FEE;

    if (auto const metadata = ctx.tx[~sfMPTokenMetadata];
        metadata && (metadata->empty() || metadata->length() > kMaxMpTokenMetadataLength))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
TokenIssuanceSet::preclaim(PreclaimContext const& ctx)
{
    AccountID const account = ctx.tx[sfAccount];
    Currency const currency = ctx.tx[sfCurrency];

    auto const sle = ctx.view.read(keylet::tokenIssuance(account, currency));
    if (!sle)
        return tecNO_ENTRY;

    if (sle->at(sfIssuer) != account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Nobody speaks for a dead issuer: wrapped issuances are not configurable.
    if (sle->isFlag(lsfTokenWrapped))
        return tecNO_PERMISSION;

    if (ctx.tx.isFlag(tfTokenLock) && sle->isFlag(lsfTokenCannotLock))
        return tecNO_PERMISSION;

    if (auto const mptId = ctx.tx[~sfMPTokenIssuanceID])
    {
        // The binding is write-once.
        if (sle->isFieldPresent(sfMPTokenIssuanceID))
            return tecNO_PERMISSION;

        return validateTokenBinding(
            ctx.view, *mptId, account, sle->at(~sfMaximumAmount), sle->at(sfTokenScale));
    }

    return tesSUCCESS;
}

TER
TokenIssuanceSet::doApply()
{
    auto const& tx = ctx_.tx;
    Currency const currency = tx[sfCurrency];

    auto const sle = view().peek(keylet::tokenIssuance(accountID_, currency));
    if (!sle)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t flags = sle->getFlags();
    if (tx.isFlag(tfTokenLock))
        flags |= lsfTokenLocked;
    else if (tx.isFlag(tfTokenUnlock))
        flags &= ~lsfTokenLocked;
    if (tx.isFlag(tfTokenCannotLock))
        flags |= lsfTokenCannotLock;
    (*sle)[sfFlags] = flags;

    if (auto const fee = tx[~sfTransferFee])
        sle->at(sfTransferFee) = *fee;

    if (auto const metadata = tx[~sfMPTokenMetadata])
        (*sle)[sfMPTokenMetadata] = *metadata;

    if (auto const mptId = tx[~sfMPTokenIssuanceID])
    {
        (*sle)[sfMPTokenIssuanceID] = *mptId;

        auto const sleMpt = view().peek(keylet::mptokenIssuance(*mptId));
        if (!sleMpt || sleMpt->isFieldPresent(sfTokenIssuanceID))
            return tecINTERNAL;  // LCOV_EXCL_LINE
        (*sleMpt)[sfTokenIssuanceID] = sle->key();
        view().update(sleMpt);
    }

    view().update(sle);
    return tesSUCCESS;
}

}  // namespace xrpl
