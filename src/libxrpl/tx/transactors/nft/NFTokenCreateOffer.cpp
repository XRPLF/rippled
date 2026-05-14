/**
 * @file NFTokenCreateOffer.cpp
 * @brief Transactor implementation for `ttNFTOKEN_CREATE_OFFER` (type 27).
 *
 * This file is intentionally thin: it acts as an adapter that extracts fields
 * from the transaction context and forwards them to the three shared helpers
 * declared in `NFTokenHelpers.h` — `tokenOfferCreatePreflight`,
 * `tokenOfferCreatePreclaim`, and `tokenOfferCreateApply`. The same helpers
 * are reused by `NFTokenMint`, which can create a sell offer atomically at
 * mint time, ensuring the two transactors cannot drift apart on offer-creation
 * rules.
 *
 * The only logic that lives here rather than in the shared helpers is the
 * buy/sell token-ownership check in `preclaim` (which must switch on
 * `tfSellNFToken` to choose the right account's NFT directory) and the
 * pre-expiration guard (which requires the current ledger close time).
 */
#include <xrpl/tx/transactors/nft/NFTokenCreateOffer.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>

namespace xrpl {

std::uint32_t
NFTokenCreateOffer::getFlagsMask(PreflightContext const& ctx)
{
    return tfNFTokenCreateOfferMask;
}

NotTEC
NFTokenCreateOffer::preflight(PreflightContext const& ctx)
{
    auto const txFlags = ctx.tx.getFlags();

    auto const nftFlags = nft::getFlags(ctx.tx[sfNFTokenID]);

    if (NotTEC notTec = nft::tokenOfferCreatePreflight(
            ctx.tx[sfAccount],
            ctx.tx[sfAmount],
            ctx.tx[~sfDestination],
            ctx.tx[~sfExpiration],
            nftFlags,
            ctx.rules,
            ctx.tx[~sfOwner],
            txFlags);
        !isTesSuccess(notTec))
        return notTec;

    return tesSUCCESS;
}

TER
NFTokenCreateOffer::preclaim(PreclaimContext const& ctx)
{
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
        return tecEXPIRED;

    uint256 const nftokenID = ctx.tx[sfNFTokenID];
    std::uint32_t const txFlags = ctx.tx.getFlags();

    if (!nft::findToken(
            ctx.view, ctx.tx[((txFlags & tfSellNFToken) != 0u) ? sfAccount : sfOwner], nftokenID))
        return tecNO_ENTRY;

    return nft::tokenOfferCreatePreclaim(
        ctx.view,
        ctx.tx[sfAccount],
        nft::getIssuer(nftokenID),
        ctx.tx[sfAmount],
        ctx.tx[~sfDestination],
        nft::getFlags(nftokenID),
        nft::getTransferFee(nftokenID),
        ctx.j,
        ctx.tx[~sfOwner],
        txFlags);
}

TER
NFTokenCreateOffer::doApply()
{
    return nft::tokenOfferCreateApply(
        view(),
        ctx_.tx[sfAccount],
        ctx_.tx[sfAmount],
        ctx_.tx[~sfDestination],
        ctx_.tx[~sfExpiration],
        ctx_.tx.getSeqProxy(),
        ctx_.tx[sfNFTokenID],
        preFeeBalance_,
        j_,
        ctx_.tx.getFlags());
}

void
NFTokenCreateOffer::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
NFTokenCreateOffer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl
