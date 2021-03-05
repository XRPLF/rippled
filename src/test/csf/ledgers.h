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
#ifndef RIPPLE_TEST_CSF_LEDGERS_H_INCLUDED
#define RIPPLE_TEST_CSF_LEDGERS_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/comparators.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/consensus/LedgerTiming.h>
#include <ripple/json/json_value.h>
#include <boost/bimap/bimap.hpp>
#include <boost/optional.hpp>
#include <set>
#include <test/csf/Tx.h>

namespace ripple {
namespace test {
namespace csf {

/** A ledger is a set of observed transactions and a sequence number
    identifying the ledger.

    Peers in the consensus process are trying to agree on a set of transactions
    to include in a ledger. For simulation, each transaction is a single
    integer and the ledger is the set of observed integers. This means future
    ledgers have prior ledgers as subsets, e.g.

        Ledger 0 :  {}
        Ledger 1 :  {1,4,5}
        Ledger 2 :  {1,2,4,5,10}
        ....

    Ledgers are immutable value types. All ledgers with the same sequence
    number, transactions, close time, etc. will have the same ledger ID. The
    LedgerOracle class below manges ID assignments for a simulation and is the
    only way to close and create a new ledger. Since the parent ledger ID is
    part of type, this also means ledgers with distinct histories will have
    distinct ids, even if they have the same set of transactions, sequence
    number and close time.
*/
class Ledger
{
    friend class LedgerOracle;

public:
    struct SeqTag;
    using Seq = tagged_integer<std::uint32_t, SeqTag>;

    struct IdTag;
    using ID = tagged_integer<std::uint32_t, IdTag>;

    struct MakeGenesis
    {
    };

private:
    // The instance is the common immutable data that will be assigned a unique
    // ID by the oracle
    struct Instance
    {
        Instance()
        {
        }

        // Sequence number
        Seq seq{0};

        // Transactions added to generate this ledger
        TxSetType txs;

        // Resolution used to determine close time
        NetClock::duration closeTimeResolution = ledgerDefaultTimeResolution;

        //! When the ledger closed (up to closeTimeResolution)
        NetClock::time_point closeTime;

        //! Whether consensus agreed on the close time
        bool closeTimeAgree = true;

        //! Parent ledger id
        ID parentID{0};

        //! Parent ledger close time
        NetClock::time_point parentCloseTime;

        //! IDs of this ledgers ancestors. Since each ledger already has unique
        //! ancestors based on the parentID, this member is not needed for any
        //! of the operators below.
        std::vector<Ledger::ID> ancestors;

        auto
        asTie() const
        {
            return std::tie(
                seq,
                txs,
                closeTimeResolution,
                closeTime,
                closeTimeAgree,
                parentID,
                parentCloseTime);
        }

        friend bool
        operator==(Instance const& a, Instance const& b)
        {
            return a.asTie() == b.asTie();
        }

        friend bool
        operator!=(Instance const& a, Instance const& b)
        {
            return a.asTie() != b.asTie();
        }

        friend bool
        operator<(Instance const& a, Instance const& b)
        {
            return a.asTie() < b.asTie();
        }

        template <class Hasher>
        friend void
        hash_append(Hasher& h, Ledger::Instance const& instance)
        {
            using beast::hash_append;
            hash_append(h, instance.asTie());
        }
    };

    // Single common genesis instance
    static const Instance genesis;

    Ledger(ID id, Instance const* i) : id_{id}, instance_{i}
    {
    }

public:
    Ledger(MakeGenesis) : instance_(&genesis)
    {
    }

    // This is required by the generic Consensus for now and should be
    // migrated to the MakeGenesis approach above.
    Ledger() : Ledger(MakeGenesis{})
    {
    }

    ID
    id() const
    {
        return id_;
    }

    Seq
    seq() const
    {
        return instance_->seq;
    }

    NetClock::duration
    closeTimeResolution() const
    {
        return instance_->closeTimeResolution;
    }

    bool
    closeAgree() const
    {
        return instance_->closeTimeAgree;
    }

    NetClock::time_point
    closeTime() const
    {
        return instance_->closeTime;
    }

    NetClock::time_point
    parentCloseTime() const
    {
        return instance_->parentCloseTime;
    }

    ID
    parentID() const
    {
        return instance_->parentID;
    }

    TxSetType const&
    txs() const
    {
        return instance_->txs;
    }

    /** Determine whether ancestor is really an ancestor of this ledger */
    bool
    isAncestor(Ledger const& ancestor) const;

    /** Return the id of the ancestor with the given seq (if exists/known)
     */
    ID
    operator[](Seq seq) const;

    /** Return the sequence number of the first mismatching ancestor
     */
    friend Ledger::Seq
    mismatch(Ledger const& a, Ledger const& o);

    Json::Value
    getJson() const;

    friend bool
    operator<(Ledger const& a, Ledger const& b)
    {
        return a.id() < b.id();
    }

private:
    ID id_{0};
    Instance const* instance_;
};

/** Oracle maintaining unique ledgers for a simulation.
 */
class LedgerOracle
{
    using InstanceMap = boost::bimaps::bimap<
        boost::bimaps::set_of<Ledger::Instance, ripple::less<Ledger::Instance>>,
        boost::bimaps::set_of<Ledger::ID, ripple::less<Ledger::ID>>>;
    using InstanceEntry = InstanceMap::value_type;

    // Set of all known ledgers; note this is never pruned
    InstanceMap instances_;

    // ID for the next unique ledger
    Ledger::ID
    nextID() const;

public:
    LedgerOracle();

    /** Find the ledger with the given ID */
    boost::optional<Ledger>
    lookup(Ledger::ID const& id) const;

    /** Accept the given txs and generate a new ledger

        @param curr The current ledger
        @param txs The transactions to apply to the current ledger
        @param closeTimeResolution Resolution used in determining close time
        @param consensusCloseTime The consensus agreed close time, no valid time
                                  if 0
    */
    Ledger
    accept(
        Ledger const& curr,
        TxSetType const& txs,
        NetClock::duration closeTimeResolution,
        NetClock::time_point const& consensusCloseTime);

    Ledger
    accept(Ledger const& curr, Tx tx)
    {
        using namespace std::chrono_literals;
        return accept(
            curr,
            TxSetType{tx},
            curr.closeTimeResolution(),
            curr.closeTime() + 1s);
    }

    /** Determine the number of distinct branches for the set of ledgers.

        Ledgers A and B are on different branches if A != B, A is not an
       ancestor of B and B is not an ancestor of A, e.g.

          /--> A
        O
          \--> B
    */
    std::size_t
    branches(std::set<Ledger> const& ledgers) const;
};

/** Helper for writing unit tests with controlled ledger histories.

    This class allows clients to refer to distinct ledgers as strings, where
    each character in the string indicates a unique ledger. It enforces the
    uniqueness at runtime, but this simplifies creation of alternate ledger
    histories, e.g.

     HistoryHelper hh;
     hh["a"]
     hh["ab"]
     hh["ac"]
     hh["abd"]

   Creates a history like
           b - d
         /
       a - c

*/
struct LedgerHistoryHelper
{
    LedgerOracle oracle;
    Tx::ID nextTx{0};
    std::unordered_map<std::string, Ledger> ledgers;
    std::set<char> seen;

    LedgerHistoryHelper()
    {
        ledgers[""] = Ledger{Ledger::MakeGenesis{}};
    }

    /** Get or create the ledger with the given string history.

        Creates any necessary intermediate ledgers, but asserts if
        a letter is re-used (e.g. "abc" then "adc" would assert)
    */
    Ledger const&
    operator[](std::string const& s)
    {
        auto it = ledgers.find(s);
        if (it != ledgers.end())
            return it->second;

        // enforce that the new suffix has never been seen
        assert(seen.emplace(s.back()).second);

        Ledger const& parent = (*this)[s.substr(0, s.size() - 1)];
        return ledgers.emplace(s, oracle.accept(parent, ++nextTx))
            .first->second;
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
