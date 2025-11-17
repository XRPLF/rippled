#include <xrpld/app/tx/detail/NFTokenCreateOffer.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

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

    // Use implementation shared with NFTokenMint
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
    std::uint32_t const txFlags = {ctx.tx.getFlags()};

    if (!nft::findToken(
            ctx.view,
            ctx.tx[(txFlags & tfSellNFToken) ? sfAccount : sfOwner],
            nftokenID))
        return tecNO_ENTRY;

    // Use implementation shared with NFTokenMint
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
    // Use implementation shared with NFTokenMint
    return nft::tokenOfferCreateApply(
        view(),
        ctx_.tx[sfAccount],
        ctx_.tx[sfAmount],
        ctx_.tx[~sfDestination],
        ctx_.tx[~sfExpiration],
        ctx_.tx.getSeqProxy(),
        ctx_.tx[sfNFTokenID],
        mPriorBalance,
        j_,
        ctx_.tx.getFlags());
}

}  // namespace ripple
