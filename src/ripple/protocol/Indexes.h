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

#include <ripple/basics/base_uint.h>
#include <ripple/protocol/Keylet.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>
#include <cstdint>

namespace ripple {

class SeqProxy;
/** Keylet computation funclets.

    Entries in the ledger are located using 256-bit locators. The locators are
    calculated using a wide range of parameters specific to the entry whose
    locator we are calculating (e.g. an account's locator is derived from the
    account's address, whereas the locator for an offer is derived from the
    account and the offer sequence.)

    To enhance type safety during lookup and make the code more robust, we use
    keylets, which contain not only the locator of the object but also the type
    of the object being referenced.

    These functions each return a type-specific keylet.
*/
namespace keylet {

/** AccountID root */
Keylet
account(AccountID const& id) noexcept;

/** The index of the amendment table */
Keylet const&
amendments() noexcept;

/** Any item that can be in an owner dir. */
Keylet
child(uint256 const& key) noexcept;

/** The index of the "short" skip list

    The "short" skip list is a node (at a fixed index) that holds the hashes
    of ledgers since the last flag ledger. It will contain, at most, 256 hashes.
*/
Keylet const&
skip() noexcept;

/** The index of the long skip for a particular ledger range.

    The "long" skip list is a node that holds the hashes of (up to) 256 flag
    ledgers.

    It can be used to efficiently skip back to any ledger using only two hops:
    the first hop gets the "long" skip list for the ledger it wants to retrieve
    and uses it to get the hash of the flag ledger whose short skip list will
    contain the hash of the requested ledger.
*/
Keylet
skip(LedgerIndex ledger) noexcept;

/** The (fixed) index of the object containing the ledger fees. */
Keylet const&
fees() noexcept;

/** The (fixed) index of the object containing the ledger negativeUNL. */
Keylet const&
negativeUNL() noexcept;

///** The directory for the next lower quality */
// struct next_t
//{
//     explicit next_t() = default;
//
//     Keylet
//     operator()(Keylet const& k) const;
// };
// static next_t const next{};

/** A SignerList */
Keylet
signers(AccountID const& account) noexcept;

//------------------------------------------------------------------------------

/** Any ledger entry */
Keylet
unchecked(uint256 const& key) noexcept;

/** The root page of an account's directory */
Keylet
ownerDir(AccountID const& id) noexcept;

/** A page in a directory */
/** @{ */
Keylet
page(uint256 const& root, std::uint64_t index = 0) noexcept;

inline Keylet
page(Keylet const& root, std::uint64_t index = 0) noexcept
{
    assert(root.type == ltDIR_NODE);
    return page(root.key, index);
}
/** @} */

}  // namespace ripple

}
#endif