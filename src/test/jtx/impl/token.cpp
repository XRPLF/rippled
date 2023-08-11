//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <test/jtx/flags.h>
#include <test/jtx/token.h>

#include <ripple/app/tx/impl/NFTokenMint.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {
namespace token {

Json::Value
mint(jtx::Account const& account, std::uint32_t nfTokenTaxon)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenTaxon.jsonName] = nfTokenTaxon;
    jv[sfTransactionType.jsonName] = jss::NFTokenMint;
    return jv;
}

void
xferFee::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfTransferFee.jsonName] = xferFee_;
}

void
issuer::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfIssuer.jsonName] = issuer_;
}

void
uri::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfURI.jsonName] = uri_;
}

uint256
getNextID(
    jtx::Env const& env,
    jtx::Account const& issuer,
    std::uint32_t nfTokenTaxon,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    // Get the nftSeq from the account root of the issuer.
    std::uint32_t const nftSeq = {
        env.le(issuer)->at(~sfMintedNFTokens).value_or(0)};
    return token::getID(env, issuer, nfTokenTaxon, nftSeq, flags, xferFee);
}

uint256
getID(
    jtx::Env const& env,
    jtx::Account const& issuer,
    std::uint32_t nfTokenTaxon,
    std::uint32_t nftSeq,
    std::uint16_t flags,
    std::uint16_t xferFee)
{
    if (env.current()->rules().enabled(fixNFTokenRemint))
    {
        // If fixNFTokenRemint is enabled, we must add issuer's
        // FirstNFTokenSequence to offset the starting NFT sequence number.
        nftSeq += env.le(issuer)
                      ->at(~sfFirstNFTokenSequence)
                      .value_or(env.seq(issuer));
    }
    return ripple::NFTokenMint::createNFTokenID(
        flags, xferFee, issuer, nft::toTaxon(nfTokenTaxon), nftSeq);
}

Json::Value
burn(jtx::Account const& account, uint256 const& nftokenID)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[jss::TransactionType] = jss::NFTokenBurn;
    return jv;
}

Json::Value
createOffer(
    jtx::Account const& account,
    uint256 const& nftokenID,
    STAmount const& amount)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::none);
    jv[jss::TransactionType] = jss::NFTokenCreateOffer;
    return jv;
}

void
owner::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfOwner.jsonName] = owner_;
}

void
expiration::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfExpiration.jsonName] = expires_;
}

void
destination::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfDestination.jsonName] = dest_;
}

template <typename T>
static Json::Value
cancelOfferImpl(jtx::Account const& account, T const& nftokenOffers)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    if (!empty(nftokenOffers))
    {
        jv[sfNFTokenOffers.jsonName] = Json::arrayValue;
        for (uint256 const& nftokenOffer : nftokenOffers)
            jv[sfNFTokenOffers.jsonName].append(to_string(nftokenOffer));
    }
    jv[jss::TransactionType] = jss::NFTokenCancelOffer;
    return jv;
}

Json::Value
cancelOffer(
    jtx::Account const& account,
    std::initializer_list<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

Json::Value
cancelOffer(
    jtx::Account const& account,
    std::vector<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

void
rootIndex::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfRootIndex.jsonName] = rootIndex_;
}

Json::Value
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(offerIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

Json::Value
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenSellOffer.jsonName] = to_string(offerIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

Json::Value
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(buyOfferIndex);
    jv[sfNFTokenSellOffer.jsonName] = to_string(sellOfferIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

void
brokerFee::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfNFTokenBrokerFee.jsonName] = brokerFee_.getJson(JsonOptions::none);
}

Json::Value
setMinter(jtx::Account const& account, jtx::Account const& minter)
{
    Json::Value jt = fset(account, asfAuthorizedNFTokenMinter);
    jt[sfNFTokenMinter.fieldName] = minter.human();
    return jt;
}

Json::Value
clearMinter(jtx::Account const& account)
{
    return fclear(account, asfAuthorizedNFTokenMinter);
}

}  // namespace token
}  // namespace jtx
}  // namespace test
}  // namespace ripple
