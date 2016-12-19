//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_CONSENSUS_RCLCXTX_H_INCLUDED
#define RIPPLE_APP_CONSENSUS_RCLCXTX_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/shamap/SHAMap.h>
#include <ripple/app/misc/CanonicalTXSet.h>

namespace ripple {

/** Represents a transaction in the RCL-specific instance of Consensus.

    RCLCxTx is a thin wrapper over the SHAMapItem that corresponds to the
    transaction.
*/
class RCLCxTx
{
public:
    //! Unique identifier/hash of transaction
    using ID = uint256;

    /** Constructor

        @param txn The transaction to wrap
    */
    RCLCxTx(SHAMapItem const& txn) : tx_{ txn }
    { }

    //! @return the unique identifier/hash of the transaction
    ID const&
    id() const
    {
        return tx_.key ();
    }

    //! The SHAMapItem that represents the transaction.
    SHAMapItem const tx_;
};

/** Represents a set of transactions in the RCL-specific instance of Consensus.

    RCLTxSet is a thin wrapper over a SHAMap that stores the set of
    transactions.
*/
class RCLTxSet
{
public:
    //! Unique identifier/hash of the set of transactions
    using ID = uint256;
    //! The type that corresponds to a single transaction
    using Tx = RCLCxTx;

    /** Constructr

        @param m The SHAMap to wrap
    */
    RCLTxSet (std::shared_ptr<SHAMap> m) :
        map_{ std::move(m) }
    {
        assert(map_);
    }

    /** Insert a new transaction into the set.

        @param t The transaction to insert.
        @return Whether the transaction took place.
    */
    bool
    insert (Tx const& t)
    {
        return map_->addItem (
            SHAMapItem {t.id(), t.tx_.peekData()},
            true, false);
    }

    /** Remove a transaction from the set.

        @param entry The ID of the transaction to remove.
        @return Whether the transaction was removed.
    */
    bool
    erase (Tx::ID const& entry)
    {
        return map_->delItem (entry);
    }

    /** Test if a transaction is in the set.

        @param entry The ID of transaction to test.
        @return Whether the transaction is in the set.
    */
    bool
    exists(Tx::ID const& entry) const
    {
        return map_->hasItem (entry);
    }

    /** Lookup a transaction.

        @param entry The ID of the transaction to find.
        @return A shared pointer to the SHAMapItem.

        @note Since find may not succeed, this returns a
              `std::shared_ptr<const SHAMapItem>` rather than a Tx, which
              cannot refer to a missing transaction.  The generic consensus
              code use the shared_ptr semantics to know whether the find
              was succesfully and properly creates a Tx as needed.
    */
    std::shared_ptr<const SHAMapItem> const &
    find(Tx::ID const& entry) const
    {
        return map_->peekItem (entry);
    }

    //! @return the unique ID/hahs of the transaction set
    ID
    id() const
    {
        return map_->getHash().as_uint256();
    }

    /** Find transactions not in common between this and another transaction set.

        @param j The set to compare with
        @return Map of transactions in this set and `j` but not both. The key
                is the transaction ID and the value is a bool of the transaction
                exists in this set.
    */
    std::map<Tx::ID, bool>
    compare (RCLTxSet const& j) const
    {
        SHAMap::Delta delta;

        // Bound the work we do in case of a malicious
        // map_ from a trusted validator
        map_->compare (*(j.map_), delta, 65536);

        std::map <uint256, bool> ret;
        for (auto const& item : delta)
        {
            assert ( (item.second.first && ! item.second.second) ||
                     (item.second.second && ! item.second.first) );

            ret[item.first] = static_cast<bool> (item.second.first);
        }
        return ret;
    }

    //! The SHAMap representing the transactions.
    std::shared_ptr <SHAMap> map_;
};

}
#endif
