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

#include <BeastConfig.h>
#include <ripple/basics/SHA512Half.h>
#include <ripple/protocol/Indexes.h>

namespace ripple {

// get the index of the node that holds the last 256 ledgers
uint256
getLedgerHashIndex ()
{
    return sha512Half(std::uint16_t(spaceSkipList));
}

// Get the index of the node that holds the set of 256 ledgers that includes
// this ledger's hash (or the first ledger after it if it's not a multiple
// of 256).
uint256
getLedgerHashIndex (std::uint32_t desiredLedgerIndex)
{
    return sha512Half(
        std::uint16_t(spaceSkipList),
        std::uint32_t(desiredLedgerIndex >> 16));
}

// get the index of the node that holds the enabled amendments
uint256
getLedgerAmendmentIndex ()
{
    return sha512Half(std::uint16_t(spaceAmendment));
}

// get the index of the node that holds the fee schedule
uint256
getLedgerFeeIndex ()
{
    return sha512Half(std::uint16_t(spaceFee));
}

uint256
getAccountRootIndex (Account const& account)
{
    return sha512Half(
        std::uint16_t(spaceAccount),
        account);
}

uint256
getAccountRootIndex (const RippleAddress & account)
{
    return getAccountRootIndex (account.getAccountID ());
}

uint256
getGeneratorIndex (Account const& uGeneratorID)
{
    return sha512Half(
        std::uint16_t(spaceGenerator),
        uGeneratorID);
}

uint256
getBookBase (Book const& book)
{
    assert (isConsistent (book));
    // Return with quality 0.
    return getQualityIndex(sha512Half(
        std::uint16_t(spaceBookDir),
        book.in.currency,
        book.out.currency,
        book.in.account,
        book.out.account));
}

uint256
getOfferIndex (Account const& account, std::uint32_t uSequence)
{
    return sha512Half(
        std::uint16_t(spaceOffer),
        account,
        std::uint32_t(uSequence));
}

uint256
getOwnerDirIndex (Account const& account)
{
    return sha512Half(
        std::uint16_t(spaceOwnerDir),
        account);
}


uint256
getDirNodeIndex (uint256 const& uDirRoot, const std::uint64_t uNodeIndex)
{
    if (uNodeIndex == 0)
        return uDirRoot;

    return sha512Half(
        std::uint16_t(spaceDirNode),
        uDirRoot,
        std::uint64_t(uNodeIndex));
}

uint256
getQualityIndex (uint256 const& uBase, const std::uint64_t uNodeDir)
{
    // Indexes are stored in big endian format: they print as hex as stored.
    // Most significant bytes are first.  Least significant bytes represent
    // adjacent entries.  We place uNodeDir in the 8 right most bytes to be
    // adjacent.  Want uNodeDir in big endian format so ++ goes to the next
    // entry for indexes.
    uint256 uNode (uBase);

    // TODO(tom): there must be a better way.
    // VFALCO [base_uint] This assumes a certain storage format
    ((std::uint64_t*) uNode.end ())[-1] = htobe64 (uNodeDir);

    return uNode;
}

uint256
getQualityNext (uint256 const& uBase)
{
    // VFALCO TODO remove this unnecessary constructor
    static uint256 const uNext ("10000000000000000");
    return uBase + uNext;
}

std::uint64_t
getQuality (uint256 const& uBase)
{
    // VFALCO [base_uint] This assumes a certain storage format
    return be64toh (((std::uint64_t*) uBase.end ())[-1]);
}

uint256
getTicketIndex (Account const& account, std::uint32_t uSequence)
{
    return sha512Half(
        std::uint16_t(spaceTicket),
        account,
        std::uint32_t(uSequence));
}

uint256
getRippleStateIndex (Account const& a, Account const& b, Currency const& currency)
{
    if (a < b)
        return sha512Half(
            std::uint16_t(spaceRipple),
            a,
            b,
            currency);
    return sha512Half(
        std::uint16_t(spaceRipple),
        b,
        a,
        currency);
}

uint256
getRippleStateIndex (Account const& a, Issue const& issue)
{
    return getRippleStateIndex (a, issue.account, issue.currency);
}

uint256
getSignerListIndex (Account const& account)
{
    return sha512Half(
        std::uint16_t(spaceSignerList),
        account);
}

} // ripple
