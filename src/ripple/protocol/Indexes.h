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

#ifndef RIPPLE_PROTOCOL_INDEXES_H_INCLUDED
#define RIPPLE_PROTOCOL_INDEXES_H_INCLUDED

#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/RippleAddress.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Book.h>
#include <cstdint>

namespace ripple {

// get the index of the node that holds the last 256 ledgers
uint256
getLedgerHashIndex ();

// Get the index of the node that holds the set of 256 ledgers that includes
// this ledger's hash (or the first ledger after it if it's not a multiple
// of 256).
uint256
getLedgerHashIndex (std::uint32_t desiredLedgerIndex);

// get the index of the node that holds the enabled amendments
uint256
getLedgerAmendmentIndex ();

// get the index of the node that holds the fee schedule
uint256
getLedgerFeeIndex ();

uint256
getAccountRootIndex (Account const& account);

uint256
getAccountRootIndex (const RippleAddress & account);

uint256
getGeneratorIndex (Account const& uGeneratorID);

uint256
getBookBase (Book const& book);

uint256
getOfferIndex (Account const& account, std::uint32_t uSequence);

uint256
getOwnerDirIndex (Account const& account);

uint256
getDirNodeIndex (uint256 const& uDirRoot, const std::uint64_t uNodeIndex);

uint256
getQualityIndex (uint256 const& uBase, const std::uint64_t uNodeDir = 0);

uint256
getQualityNext (uint256 const& uBase);

// VFALCO This name could be better
std::uint64_t
getQuality (uint256 const& uBase);

uint256
getTicketIndex (Account const& account, std::uint32_t uSequence);

uint256
getRippleStateIndex (Account const& a, Account const& b, Currency const& currency);

uint256
getRippleStateIndex (Account const& a, Issue const& issue);

uint256
getSignerListIndex (Account const& account);

//------------------------------------------------------------------------------

/** A pair of SHAMap key and LedgerEntryType.
    
    A Keylet identifies both a key in the state map
    and its ledger entry type.
*/
struct Keylet
{
    LedgerEntryType type;
    uint256 key;

    Keylet (LedgerEntryType type_,
            uint256 const& key_)
        : type(type_)
        , key(key_)
    {
    }
};

/** Keylet computation funclets. */
namespace keylet {

/** Account root */
struct account_t
{
    Keylet operator()(Account const& id) const;

    // DEPRECATED
    Keylet operator()(RippleAddress const& ra) const;
};
static account_t const account {};

/** OWner directory */
struct owndir_t
{
    Keylet operator()(Account const& id) const;
};
static owndir_t const ownerDir {};

/** Skip list */
struct skip_t
{
    Keylet operator()() const;

    Keylet operator()(LedgerIndex ledger) const;
};
static skip_t const skip {};

/** The amendment table */
struct amendments_t
{
    Keylet operator()() const;
};
static amendments_t const amendments {};

/** The ledger fees */
struct fee_t
{
    Keylet operator()() const;
};
static fee_t const fee {};

/** The beginning of an order book */
struct book_t
{
    Keylet operator()(Book const& b) const;
};
static book_t const book {};

/** An offer from an account */
struct offer_t
{
    Keylet operator()(Account const& id,
        std::uint32_t seq) const;
};
static offer_t const offer {};

/** An item in a directory */
struct item_t
{
    Keylet operator()(Keylet const& k,
        std::uint64_t index,
            LedgerEntryType type) const;
};
static item_t const item {};

/** The directory for a specific quality */
struct quality_t
{
    Keylet operator()(Keylet const& k,
        std::uint64_t q) const;
};
static quality_t const quality {};

/** The directry for the next lower quality */
struct next_t
{
    Keylet operator()(Keylet const& k) const;
};
static next_t const next {};

/** A ticket belonging to an account */
struct ticket_t
{
    Keylet operator()(Account const& id,
        std::uint32_t seq) const;
};
static ticket_t const ticket {};

/** A trust line */
struct trust_t
{
    Keylet operator()(Account const& id0,
        Account const& id1, Currency const& currency) const;

    Keylet operator()(Account const& id,
        Issue const& issue) const;
};
static trust_t const trust {};

/** A SignerList */
struct signers_t
{
    Keylet operator()(Account const& id) const;
};
static signers_t const signers {};

} // keylet

}

#endif
