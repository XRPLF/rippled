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

#ifndef RIPPLE_TX_IMPL_DETAILS_NFTOKENUTILS_H_INCLUDED
#define RIPPLE_TX_IMPL_DETAILS_NFTOKENUTILS_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/base_uint.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/protocol/AccountID.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/nft.h>

namespace ripple {

namespace nft {

/** Delete up to a specified number of offers from the specified token offer
 * directory. */
std::size_t
removeTokenOffersWithLimit(
    ApplyView& view,
    Keylet const& directory,
    std::size_t maxDeletableOffers);

/** Returns tesSUCCESS if NFToken has few enough offers that it can be burned */
TER
notTooManyOffers(ReadView const& view, uint256 const& nftokenID);

/** Finds the specified token in the owner's token directory. */
std::optional<STObject>
findToken(
    ReadView const& view,
    AccountID const& owner,
    uint256 const& nftokenID);

/** Finds the token in the owner's token directory.  Returns token and page. */
struct TokenAndPage
{
    STObject token;
    std::shared_ptr<SLE> page;

    TokenAndPage(STObject const& token_, std::shared_ptr<SLE> page_)
        : token(token_), page(std::move(page_))
    {
    }
};
std::optional<TokenAndPage>
findTokenAndPage(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID);

/** Insert the token in the owner's token directory. */
TER
insertToken(ApplyView& view, AccountID owner, STObject&& nft);

/** Remove the token from the owner's token directory. */
TER
removeToken(ApplyView& view, AccountID const& owner, uint256 const& nftokenID);

TER
removeToken(
    ApplyView& view,
    AccountID const& owner,
    uint256 const& nftokenID,
    std::shared_ptr<SLE>&& page);

/** Deletes the given token offer.

    An offer is tracked in two separate places:
        - The token's 'buy' directory, if it's a buy offer; or
        - The token's 'sell' directory, if it's a sell offer; and
        - The owner directory of the account that placed the offer.

    The offer also consumes one incremental reserve.
 */
bool
deleteTokenOffer(ApplyView& view, std::shared_ptr<SLE> const& offer);

bool
compareTokens(uint256 const& a, uint256 const& b);

/** Preflight checks shared by NFTokenCreateOffer and NFTokenMint */
NotTEC
tokenOfferCreatePreflight(
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    std::uint16_t nftFlags,
    Rules const& rules,
    std::optional<AccountID> const& owner = std::nullopt,
    std::uint32_t txFlags = lsfSellNFToken);

/** Preclaim checks shared by NFTokenCreateOffer and NFTokenMint */
TER
tokenOfferCreatePreclaim(
    ReadView const& view,
    AccountID const& acctID,
    AccountID const& nftIssuer,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::uint16_t nftFlags,
    std::uint16_t xferFee,
    beast::Journal j,
    std::optional<AccountID> const& owner = std::nullopt,
    std::uint32_t txFlags = lsfSellNFToken);

/** doApply implementation shared by NFTokenCreateOffer and NFTokenMint */
TER
tokenOfferCreateApply(
    ApplyView& view,
    AccountID const& acctID,
    STAmount const& amount,
    std::optional<AccountID> const& dest,
    std::optional<std::uint32_t> const& expiration,
    SeqProxy seqProxy,
    uint256 const& nftokenID,
    XRPAmount const& priorBalance,
    beast::Journal j,
    std::uint32_t txFlags = lsfSellNFToken);

}  // namespace nft

}  // namespace ripple

#endif  // RIPPLE_TX_IMPL_DETAILS_NFTOKENUTILS_H_INCLUDED
