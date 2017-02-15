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
#include <boost/container/flat_set.hpp>
#include <ostream>
#include <string>
#include <map>

namespace ripple {
namespace test {
namespace csf {

//! A single transaction
class Tx
{
public:
    using ID = std::uint32_t;

    Tx(ID i) : id_{ i } {}

    ID
    id() const
    {
        return id_;
    }

    bool
    operator<(Tx const & o) const
    {
        return id_ < o.id_;
    }

    bool
    operator==(Tx const & o) const
    {
        return id_ == o.id_;
    }


private:
    ID id_;

};

//!-------------------------------------------------------------------------
//! All sets of Tx are represented as a flat_set.
using TxSetType = boost::container::flat_set<Tx>;

//! TxSet is a set of transactions to consider including in the ledger
class TxSet
{
public:
    using ID = TxSetType;
    using Tx = csf::Tx;
    using MutableTxSet = TxSet;

    TxSet() = default;
    TxSet(TxSetType const & s) : txs_{ s } {}

    auto
    mutableSet() const
    {
        return *this;
    }

    bool
    insert(Tx const & t)
    {
        return txs_.insert(t).second;
    }

    bool
    erase(Tx::ID const & txId)
    {
        return txs_.erase(Tx{ txId }) > 0;
    }

    bool
    exists(Tx::ID const txId) const
    {
        auto it = txs_.find(Tx{ txId });
        return it != txs_.end();
    }

    Tx const *
    find(Tx::ID const& txId) const
    {
        auto it = txs_.find(Tx{ txId });
        if (it != txs_.end())
            return &(*it);
        return nullptr;
    }

    auto const &
    id() const
    {
        return txs_;
    }

    /** @return Map of Tx::ID that are missing. True means
                    it was in this set and not other. False means
                    it was in the other set and not this
    */
    std::map<Tx::ID, bool>
    compare(TxSet const& other) const
    {
        std::map<Tx::ID, bool> res;

        auto populate_diffs = [&res](auto const & a, auto const & b, bool s)
        {
            auto populator = [&](auto const & tx)
            {
                        res[tx.id()] = s;
            };
            std::set_difference(
                a.begin(), a.end(),
                b.begin(), b.end(),
                boost::make_function_output_iterator(
                    std::ref(populator)
                )
            );
        };

        populate_diffs(txs_, other.txs_, true);
        populate_diffs(other.txs_, txs_, false);
        return res;
    }

    //! The set contains the actual transactions
    TxSetType txs_;
};


/** The RCL consensus process catches missing node SHAMap error
    in several points. This exception is meant to represent a similar
    case for the unit test.
*/
class MissingTx : public std::runtime_error
{
public:
    MissingTx()
        : std::runtime_error("MissingTx")
    {}
};


//------------------------------------------------------------------------------
// Helper functions for debug printing

inline
std::ostream&
operator<<(std::ostream & o, const Tx & t)
{
    return o << t.id();
}

template <class T>
inline
std::ostream&
operator<<(std::ostream & o, boost::container::flat_set<T> const & ts)
{
    o << "{ ";
    bool do_comma = false;
    for (auto const & t : ts)
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

inline
std::string
to_string(TxSetType const & txs)
{
    std::stringstream ss;
    ss << txs;
    return ss.str();
}

template <class Hasher>
inline
void
hash_append(Hasher& h, Tx const & tx)
{
    using beast::hash_append;
    hash_append(h, tx.id());
}

std::ostream&
operator<<(std::ostream & o, MissingTx const &m)
{
    return o << m.what();
}


} // csf
} // test
} // ripple

#endif
