//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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
#ifndef RIPPLE_TEST_CSF_TX_H_INCLUDED
#define RIPPLE_TEST_CSF_TX_H_INCLUDED
#include <ripple/beast/hash/hash_append.h>
#include <ripple/beast/hash/uhash.h>
#include <boost/container/flat_set.hpp>
#include <boost/iterator/function_output_iterator.hpp>
#include <map>
#include <ostream>
#include <string>

namespace ripple {
namespace test {
namespace csf {

//! A single transaction
class Tx
{
public:
    using ID = std::uint32_t;

    Tx(ID i) : id_{i}
    {
    }

    ID
    id() const
    {
        return id_;
    }

    bool
    operator<(Tx const& o) const
    {
        return id_ < o.id_;
    }

    bool
    operator==(Tx const& o) const
    {
        return id_ == o.id_;
    }

private:
    ID id_;
};

//!-------------------------------------------------------------------------
//! All sets of Tx are represented as a flat_set for performance.
using TxSetType = boost::container::flat_set<Tx>;

//! TxSet is a set of transactions to consider including in the ledger
class TxSet
{
public:
    using ID = beast::uhash<>::result_type;
    using Tx = csf::Tx;

    static ID
    calcID(TxSetType const& txs)
    {
        return beast::uhash<>{}(txs);
    }

    class MutableTxSet
    {
        friend class TxSet;

        TxSetType txs_;

    public:
        MutableTxSet(TxSet const& s) : txs_{s.txs_}
        {
        }

        bool
        insert(Tx const& t)
        {
            return txs_.insert(t).second;
        }

        bool
        erase(Tx::ID const& txId)
        {
            return txs_.erase(Tx{txId}) > 0;
        }
    };

    TxSet() = default;
    TxSet(TxSetType const& s) : txs_{s}, id_{calcID(txs_)}
    {
    }

    TxSet(MutableTxSet&& m) : txs_{std::move(m.txs_)}, id_{calcID(txs_)}
    {
    }

    bool
    exists(Tx::ID const txId) const
    {
        auto it = txs_.find(Tx{txId});
        return it != txs_.end();
    }

    Tx const*
    find(Tx::ID const& txId) const
    {
        auto it = txs_.find(Tx{txId});
        if (it != txs_.end())
            return &(*it);
        return nullptr;
    }

    TxSetType const&
    txs() const
    {
        return txs_;
    }

    ID
    id() const
    {
        return id_;
    }

    /** @return Map of Tx::ID that are missing. True means
                    it was in this set and not other. False means
                    it was in the other set and not this
    */
    std::map<Tx::ID, bool>
    compare(TxSet const& other) const
    {
        std::map<Tx::ID, bool> res;

        auto populate_diffs = [&res](auto const& a, auto const& b, bool s) {
            auto populator = [&](auto const& tx) { res[tx.id()] = s; };
            std::set_difference(
                a.begin(),
                a.end(),
                b.begin(),
                b.end(),
                boost::make_function_output_iterator(std::ref(populator)));
        };

        populate_diffs(txs_, other.txs_, true);
        populate_diffs(other.txs_, txs_, false);
        return res;
    }

private:
    //! The set contains the actual transactions
    TxSetType txs_;

    //! The unique ID of this tx set
    ID id_;
};

//------------------------------------------------------------------------------
// Helper functions for debug printing

inline std::ostream&
operator<<(std::ostream& o, const Tx& t)
{
    return o << t.id();
}

template <class T>
inline std::ostream&
operator<<(std::ostream& o, boost::container::flat_set<T> const& ts)
{
    o << "{ ";
    bool do_comma = false;
    for (auto const& t : ts)
    {
        if (do_comma)
            o << ", ";
        else
            do_comma = true;
        o << t;
    }
    o << " }";
    return o;
}

inline std::string
to_string(TxSetType const& txs)
{
    std::stringstream ss;
    ss << txs;
    return ss.str();
}

template <class Hasher>
inline void
hash_append(Hasher& h, Tx const& tx)
{
    using beast::hash_append;
    hash_append(h, tx.id());
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
