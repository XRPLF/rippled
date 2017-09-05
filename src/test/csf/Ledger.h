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
#ifndef RIPPLE_TEST_CSF_LEDGER_H_INCLUDED
#define RIPPLE_TEST_CSF_LEDGER_H_INCLUDED

#include <ripple/basics/chrono.h>
#include <ripple/consensus/LedgerTiming.h>
#include <test/csf/Tx.h>

namespace ripple {
namespace test {
namespace csf {

/** A ledger is a set of observed transactions and a sequence number
    identifying the ledger.

    Peers in the consensus process are trying to agree on a set of transactions
    to include in a ledger.  For unit testing, each transaction is a
    single integer and the ledger is a set of observed integers.  This means
    future ledgers have prior ledgers as subsets, e.g.

        Ledger 0 :  {}
        Ledger 1 :  {1,4,5}
        Ledger 2 :  {1,2,4,5,10}
        ....

    Tx - Integer
    TxSet - Set of Tx
    Ledger - Set of Tx and sequence number
*/

class Ledger
{
public:
    struct ID
    {
        std::uint32_t seq = 0;
        TxSetType txs = TxSetType{};

        bool
        operator==(ID const& o) const
        {
            return seq == o.seq && txs == o.txs;
        }

        bool
        operator!=(ID const& o) const
        {
            return !(*this == o);
        }

        bool
        operator<(ID const& o) const
        {
            return std::tie(seq, txs) < std::tie(o.seq, o.txs);
        }
    };

    auto const&
    id() const
    {
        return id_;
    }

    auto
    seq() const
    {
        return id_.seq;
    }

    auto
    closeTimeResolution() const
    {
        return closeTimeResolution_;
    }

    auto
    closeAgree() const
    {
        return closeTimeAgree_;
    }

    auto
    closeTime() const
    {
        return closeTime_;
    }

    auto
    parentCloseTime() const
    {
        return parentCloseTime_;
    }

    auto const&
    parentID() const
    {
        return parentID_;
    }

    Json::Value
    getJson() const
    {
        Json::Value res(Json::objectValue);
        res["seq"] = seq();
        return res;
    }

    //! Apply the given transactions to this ledger
    Ledger
    close(
        TxSetType const& txs,
        NetClock::duration closeTimeResolution,
        NetClock::time_point const& consensusCloseTime,
        bool closeTimeAgree) const
    {
        Ledger res{*this};
        res.id_.txs.insert(txs.begin(), txs.end());
        res.id_.seq = seq() + 1;
        res.closeTimeResolution_ = closeTimeResolution;
        res.closeTime_ = effCloseTime(
            consensusCloseTime, closeTimeResolution, closeTime());
        res.closeTimeAgree_ = closeTimeAgree;
        res.parentCloseTime_ = closeTime();
        res.parentID_ = id();
        return res;
    }

private:
    //! Unique identifier of ledger is combination of sequence number and id
    ID id_;

    //! Bucket resolution used to determine close time
    NetClock::duration closeTimeResolution_ = ledgerDefaultTimeResolution;

    //! When the ledger closed
    NetClock::time_point closeTime_;

    //! Whether consenssus agreed on the close time
    bool closeTimeAgree_ = true;

    //! Parent ledger id
    ID parentID_;

    //! Parent ledger close time
    NetClock::time_point parentCloseTime_;
};

inline std::ostream&
operator<<(std::ostream& o, Ledger::ID const& id)
{
    return o << id.seq << "," << id.txs;
}

inline std::string
to_string(Ledger::ID const& id)
{
    std::stringstream ss;
    ss << id;
    return ss.str();
}

}  // csf
}  // test
}  // ripple

#endif
