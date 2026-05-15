#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

#include <xrpl/basics/strHex.h>

#include <initializer_list>

namespace xrpl::test::jtx::token {

/** Mint an NFToken. */
json::Value
mint(jtx::Account const& account, std::uint32_t tokenTaxon = 0);

/** Sets the optional TransferFee on an NFTokenMint. */
class XferFee
{
private:
    std::uint16_t xferFee_;

public:
    explicit XferFee(std::uint16_t fee) : xferFee_(fee)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Issuer on an NFTokenMint. */
class Issuer
{
private:
    std::string issuer_;

public:
    explicit Issuer(jtx::Account const& issue) : issuer_(issue.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional URI on an NFTokenMint. */
class Uri
{
private:
    std::string uri_;

public:
    explicit Uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional amount field on an NFTokenMint. */
class Amount
{
private:
    STAmount const amount_;

public:
    explicit Amount(STAmount const amount) : amount_(amount)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Get the next NFTokenID that will be issued. */
uint256
getNextID(
    jtx::Env const& env,
    jtx::Account const& account,
    std::uint32_t nftokenTaxon,
    std::uint16_t flags = 0,
    std::uint16_t xferFee = 0);

/** Get the NFTokenID for a particular nftSequence. */
uint256
getID(
    jtx::Env const& env,
    jtx::Account const& account,
    std::uint32_t tokenTaxon,
    std::uint32_t nftSeq,
    std::uint16_t flags = 0,
    std::uint16_t xferFee = 0);

/** Burn an NFToken. */
json::Value
burn(jtx::Account const& account, uint256 const& nftokenID);

/** Create an NFTokenOffer. */
json::Value
createOffer(jtx::Account const& account, uint256 const& nftokenID, STAmount const& amount);

/** Sets the optional Owner on an NFTokenOffer. */
class Owner
{
private:
    std::string owner_;

public:
    explicit Owner(jtx::Account const& ownedBy) : owner_(ownedBy.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Expiration field on an NFTokenOffer. */
class Expiration
{
private:
    std::uint32_t expires_;

public:
    explicit Expiration(std::uint32_t const& expires) : expires_(expires)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Destination field on an NFTokenOffer. */
class Destination
{
private:
    std::string dest_;

public:
    explicit Destination(jtx::Account const& dest) : dest_(dest.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Cancel NFTokenOffers. */
json::Value
cancelOffer(jtx::Account const& account, std::initializer_list<uint256> const& nftokenOffers = {});

json::Value
cancelOffer(jtx::Account const& account, std::vector<uint256> const& nftokenOffers);

/** Sets the optional RootIndex field when canceling NFTokenOffers. */
class RootIndex
{
private:
    std::string rootIndex_;

public:
    explicit RootIndex(uint256 const& index) : rootIndex_(to_string(index))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Accept an NFToken buy offer. */
json::Value
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex);

/** Accept an NFToken sell offer. */
json::Value
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex);

/** Broker two NFToken offers. */
json::Value
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex);

/** Sets the optional NFTokenBrokerFee field in a brokerOffer transaction. */
class BrokerFee
{
private:
    STAmount const brokerFee_;

public:
    explicit BrokerFee(STAmount const fee) : brokerFee_(fee)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Set the authorized minter on an account root. */
json::Value
setMinter(jtx::Account const& account, jtx::Account const& minter);

/** Clear any authorized minter from an account root. */
json::Value
clearMinter(jtx::Account const& account);

/** Modify an NFToken. */
json::Value
modify(jtx::Account const& account, uint256 const& nftokenID);

}  // namespace xrpl::test::jtx::token
