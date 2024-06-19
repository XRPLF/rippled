//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_NFT_H_INCLUDED
#define RIPPLE_TEST_JTX_NFT_H_INCLUDED

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/owners.h>

#include <ripple/basics/strHex.h>

#include <initializer_list>

namespace ripple {
namespace test {
namespace jtx {

namespace token {

/** Mint an NFToken. */
Json::Value
mint(jtx::Account const& account, std::uint32_t tokenTaxon = 0);

/** Sets the optional TransferFee on an NFTokenMint. */
class xferFee
{
private:
    std::uint16_t xferFee_;

public:
    explicit xferFee(std::uint16_t fee) : xferFee_(fee)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Issuer on an NFTokenMint. */
class issuer
{
private:
    std::string issuer_;

public:
    explicit issuer(jtx::Account const& issue) : issuer_(issue.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional URI on an NFTokenMint. */
class uri
{
private:
    std::string uri_;

public:
    explicit uri(std::string const& u) : uri_(strHex(u))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional amount field on an NFTokenMint. */
class amount
{
private:
    STAmount const amount_;

public:
    explicit amount(STAmount const amount) : amount_(amount)
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
Json::Value
burn(jtx::Account const& account, uint256 const& nftokenID);

/** Create an NFTokenOffer. */
Json::Value
createOffer(
    jtx::Account const& account,
    uint256 const& nftokenID,
    STAmount const& amount);

/** Sets the optional Owner on an NFTokenOffer. */
class owner
{
private:
    std::string owner_;

public:
    explicit owner(jtx::Account const& ownedBy) : owner_(ownedBy.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Expiration field on an NFTokenOffer. */
class expiration
{
private:
    std::uint32_t expires_;

public:
    explicit expiration(std::uint32_t const& expires) : expires_(expires)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Sets the optional Destination field on an NFTokenOffer. */
class destination
{
private:
    std::string dest_;

public:
    explicit destination(jtx::Account const& dest) : dest_(dest.human())
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Cancel NFTokenOffers. */
Json::Value
cancelOffer(
    jtx::Account const& account,
    std::initializer_list<uint256> const& nftokenOffers = {});

Json::Value
cancelOffer(
    jtx::Account const& account,
    std::vector<uint256> const& nftokenOffers);

/** Sets the optional RootIndex field when canceling NFTokenOffers. */
class rootIndex
{
private:
    std::string rootIndex_;

public:
    explicit rootIndex(uint256 const& index) : rootIndex_(to_string(index))
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Accept an NFToken buy offer. */
Json::Value
acceptBuyOffer(jtx::Account const& account, uint256 const& offerIndex);

/** Accept an NFToken sell offer. */
Json::Value
acceptSellOffer(jtx::Account const& account, uint256 const& offerIndex);

/** Broker two NFToken offers. */
Json::Value
brokerOffers(
    jtx::Account const& account,
    uint256 const& buyOfferIndex,
    uint256 const& sellOfferIndex);

/** Sets the optional NFTokenBrokerFee field in a brokerOffer transaction. */
class brokerFee
{
private:
    STAmount const brokerFee_;

public:
    explicit brokerFee(STAmount const fee) : brokerFee_(fee)
    {
    }

    void
    operator()(Env&, JTx& jtx) const;
};

/** Set the authorized minter on an account root. */
Json::Value
setMinter(jtx::Account const& account, jtx::Account const& minter);

/** Clear any authorized minter from an account root. */
Json::Value
clearMinter(jtx::Account const& account);

}  // namespace token

}  // namespace jtx

}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_NFT_H_INCLUDED
