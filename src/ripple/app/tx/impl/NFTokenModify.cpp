
#include <ripple/app/tx/impl/NFTokenModify.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
NFTokenModify::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1_1) ||
        !ctx.rules.enabled(featureDNFT))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (auto owner = ctx.tx[~sfOwner]; owner == ctx.tx[sfAccount])
        return temMALFORMED;

    if (auto uri = ctx.tx[~sfURI])
    {
        if (uri->length() == 0 || uri->length() > maxTokenURILength)
            return temMALFORMED;
    }

    return preflight2(ctx);
}

TER
NFTokenModify::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];
    auto const owner =
        ctx.tx[ctx.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    if (!nft::findToken(ctx.view, owner, ctx.tx[sfNFTokenID]))
        return tecNO_ENTRY;

    // Check if the NFT is mutable
    if (!(nft::getFlags(ctx.tx[sfNFTokenID]) & nft::flagMutable))
        return tecNO_PERMISSION;

    // Verify permissions for the issuer
    if (auto const issuer = nft::getIssuer(ctx.tx[sfNFTokenID]);
        issuer != account)
    {
        if (auto const sle = ctx.view.read(keylet::account(issuer)); sle)
        {
            if (auto const minter = (*sle)[~sfNFTokenMinter]; minter != account)
                return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
NFTokenModify::doApply()
{
    auto const nftokenID = ctx_.tx[sfNFTokenID];
    auto const owner =
        ctx_.tx[ctx_.tx.isFieldPresent(sfOwner) ? sfOwner : sfAccount];

    // Find the token and its page
    auto tokenAndPage = nft::findTokenAndPage(view(), owner, nftokenID);
    if (!tokenAndPage)
        return tecINTERNAL;

    // Replace the URI if present in the transaction
    if (auto const newURI = ctx_.tx[~sfURI])
        tokenAndPage->token.setFieldVL(sfURI, *newURI);
    else
        tokenAndPage->token.makeFieldAbsent(sfURI);

    // Apply the changes to the token
    if (auto const ret = nft::updateToken(
            view(),
            owner,
            std::move(tokenAndPage->token),
            std::move(tokenAndPage->page));
        !isTesSuccess(ret))
        return ret;

    return tesSUCCESS;
}
}  // namespace ripple
