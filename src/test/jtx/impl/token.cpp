#include <test/jtx/token.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/flags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/transactors/nft/NFTokenMint.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace xrpl::test::jtx::token {

json::Value
mint(jtx::Account const& account, std::uint32_t nfTokenTaxon)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenTaxon.jsonName] = nfTokenTaxon;
    jv[sfTransactionType.jsonName] = jss::NFTokenMint;
    return jv;
}

void
XferFee::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfTransferFee.jsonName] = xferFee_;
}

void
Issuer::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfIssuer.jsonName] = issuer_;
}

void
Uri::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfURI.jsonName] = uri_;
}

void
Amount::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfAmount.jsonName] = amount_.getJson(JsonOptions::Values::None);
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
    std::uint32_t const nftSeq = {env.le(issuer)->at(~sfMintedNFTokens).value_or(0)};
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
    // We must add issuer's FirstNFTokenSequence to offset the starting NFT
    // sequence number.
    nftSeq += env.le(issuer)->at(~sfFirstNFTokenSequence).value_or(env.seq(issuer));
    return xrpl::NFTokenMint::createNFTokenID(
        flags, xferFee, issuer, nft::toTaxon(nfTokenTaxon), nftSeq);
}

json::Value
burn(jtx::Account const& account, uint256 const& nftokenID)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[jss::TransactionType] = jss::NFTokenBurn;
    return jv;
}

json::Value
createOffer(jtx::Account const& account, uint256 const& nftokenID, STAmount const& amount)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[sfAmount.jsonName] = amount.getJson(JsonOptions::Values::None);
    jv[jss::TransactionType] = jss::NFTokenCreateOffer;
    return jv;
}

void
Owner::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfOwner.jsonName] = owner_;
}

void
Expiration::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfExpiration.jsonName] = expires_;
}

void
Destination::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfDestination.jsonName] = dest_;
}

template <typename T>
static json::Value
cancelOfferImpl(jtx::Account const& account, T const& nftokenOffers)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    if (!empty(nftokenOffers))
    {
        jv[sfNFTokenOffers.jsonName] = json::ValueType::Array;
        for (uint256 const& nftokenOffer : nftokenOffers)
            jv[sfNFTokenOffers.jsonName].append(to_string(nftokenOffer));
    }
    jv[jss::TransactionType] = jss::NFTokenCancelOffer;
    return jv;
}

json::Value
cancelOffer(jtx::Account const& account, std::initializer_list<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

json::Value
cancelOffer(jtx::Account const& account, std::vector<uint256> const& nftokenOffers)
{
    return cancelOfferImpl(account, nftokenOffers);
}

void
RootIndex::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfRootIndex.jsonName] = rootIndex_;
}

json::Value
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(offerIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

json::Value
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenSellOffer.jsonName] = to_string(offerIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

json::Value
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenBuyOffer.jsonName] = to_string(buyOfferIndex);
    jv[sfNFTokenSellOffer.jsonName] = to_string(sellOfferIndex);
    jv[jss::TransactionType] = jss::NFTokenAcceptOffer;
    return jv;
}

void
BrokerFee::operator()(Env& env, JTx& jt) const
{
    jt.jv[sfNFTokenBrokerFee.jsonName] = brokerFee_.getJson(JsonOptions::Values::None);
}

json::Value
setMinter(jtx::Account const& account, jtx::Account const& minter)
{
    json::Value jt = fset(account, asfAuthorizedNFTokenMinter);
    jt[sfNFTokenMinter.fieldName] = minter.human();
    return jt;
}

json::Value
clearMinter(jtx::Account const& account)
{
    return fclear(account, asfAuthorizedNFTokenMinter);
}

json::Value
modify(jtx::Account const& account, uint256 const& nftokenID)
{
    json::Value jv;
    jv[sfAccount.jsonName] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nftokenID);
    jv[jss::TransactionType] = jss::NFTokenModify;
    return jv;
}

}  // namespace xrpl::test::jtx::token
