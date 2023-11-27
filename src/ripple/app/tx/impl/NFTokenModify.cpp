#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rate.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>

NotTEC
NFTokenModify::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const nftID = ctx.tx[sfNFTokenID];
    auto const account = ctx.tx[sfAccount];

    if (!account || !nftID)
       return temMALFORMED; 

    if (auto uri = ctx.tx[sfURI])
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

    auto const owner = [&ctx]() {
        if (ctx.tx.isFieldPresent(sfOwner))
            return ctx.tx.getAccountID(sfOwner);

        return ctx.tx[sfAccount];
    }();

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
            if (auto const minter = (*sle)[~sfNFTokenMinter];
                minter != account)
                return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

TER
NFTokenModify::doApply()
{
    auto const nftokenID = ctx_.tx[sfNFTokenID];
    auto const account = ctx_.tx[sfAccount];

    // Find the token and its page
    auto tokenAndPage = nft::findTokenAndPage(view(), account, nftokenID);
    if (!tokenAndPage)
        return tecINTERNAL;

    // Replace the URI if present in the transaction
    if (ctx_.tx.isFieldPresent(sfURI)) {
        auto newURI = ctx_.tx[sfURI];
        (*tokenAndPage->token)[sfURI] = newURI;
    }

    // Apply the changes to the token
    if (auto const ret = nft::updateToken(view(), account, std::move(tokenAndPage->token), std::move(tokenAndPage->page)); !isTesSuccess(ret))
        return ret;

    return tesSUCCESS;    
}
