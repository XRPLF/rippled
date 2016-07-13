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

namespace ripple {

// Transactions, as seen by the consensus code in the rippled app
class RCLCxTx
{
public:

    RCLCxTx (SHAMapItem const& txn) : txn_ (txn)
    { }

    uint256 const& getID() const
    {
        return txn_.key ();
    }

    SHAMapItem const& txn() const
    {
        return txn_;
    }

protected:

    SHAMapItem const txn_;
};

class RCLTxSet;

class MutableRCLTxSet
{
public:

    MutableRCLTxSet (RCLTxSet const&);

    bool
    addEntry (RCLCxTx const& p)
    {
        return map_->addItem (
            SHAMapItem {p.getID(), p.txn().peekData()},
            true, false);
    }

    bool
    removeEntry (uint256 const& entry)
    {
        return map_->delItem (entry);
    }

    std::shared_ptr <SHAMap> const& map() const
    {
        return map_;
    }

protected:

    std::shared_ptr <SHAMap> map_;
};

// Sets of transactions
// as seen by the consensus code in the rippled app
class RCLTxSet
{
public:

    using mutable_t = MutableRCLTxSet;

    RCLTxSet (std::shared_ptr<SHAMap> map) :
        map_ (std::move(map))
    {
        assert (map_);
    }

    RCLTxSet (MutableRCLTxSet const& set) :
        map_ (set.map()->snapShot (false))
    { }

    bool hasEntry (uint256 const& entry) const
    {
        return map_->hasItem (entry);
    }

    boost::optional <RCLCxTx const>
    getEntry (uint256 const& entry) const
    {
        auto item = map_->peekItem (entry);
        if (item)
            return RCLCxTx(*item);
        return boost::none;
    }

    uint256 getID() const
    {
        return map_->getHash().as_uint256();
    }

    std::map <uint256, bool>
    getDifferences (RCLTxSet const& j) const
    {
        SHAMap::Delta delta;
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

    std::shared_ptr<SHAMap> const& map() const
    {
        return map_;
    }

protected:

    std::shared_ptr <SHAMap> map_;
};

inline MutableRCLTxSet::MutableRCLTxSet (RCLTxSet const& set)
    : map_ (set.map()->snapShot (true))
{ }

}
#endif
