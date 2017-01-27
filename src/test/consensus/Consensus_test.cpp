//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc->

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
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <test/jtx/BasicNetwork.h>
#include <ripple/beast/clock/manual_clock.h>
#include <boost/container/flat_set.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/function_output_iterator.hpp>
#include <ripple/beast/hash/hash_append.h>
#include <utility>

namespace ripple {
namespace test {

namespace bc = boost::container;

class Consensus_test : public beast::unit_test::suite
{
public:
    using NodeID = std::int32_t;

    /** Consensus unit test types

        Peers in the consensus process are trying to agree on a set of transactions
        to include in a ledger.  For unit testing, each transaction is a
        single integer and the ledger is a set of observed integers.  This means
        future ledgers have prior ledgers as subsets, e.g.

            Ledger 0 :  {}
            Ledger 1 :  {1,4,5}
            Ledger 2 :  {1,2,4,5,10}
            ....

        Tx - Integer
        TxSet/MutableTxSet - Set of Tx
        Ledger - Set of Tx and sequence number

    */


    //! A single transaction
    class Txn
    {
    public:
        using ID = int;

        Txn(ID i) : id_{ i } {}

        ID
        id() const
        {
            return id_;
        }

        bool
        operator<(Txn const & o) const
        {
            return id_ < o.id_;
        }

        bool
        operator==(Txn const & o) const
        {
            return id_ == o.id_;
        }

    private:
        ID id_;

    };

    //!-------------------------------------------------------------------------
    //! All sets of Tx are represented as a flat_set.
    using TxSetType = bc::flat_set<Txn>;

    //! TxSet is a set of transactions to consider including in the ledger
    class TxSet
    {
    public:
        using ID = TxSetType;
        using Tx = Txn;
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

    /** A ledger is a set of observed transactions and a sequence number
        identifying the ledger.
    */
    class Ledger
    {
    public:

        struct ID
        {
            std::uint32_t seq = 0;
            TxSetType txs = TxSetType{};

            bool operator==(ID const & o) const
            {
                return seq == o.seq && txs == o.txs;
            }

            bool operator!=(ID const & o) const
            {
                return !(*this == o);
            }

            bool operator<(ID const & o) const
            {
                return std::tie(seq, txs) < std::tie(o.seq, o.txs);
            }
        };


        auto const &
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

        auto const &
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
        close(TxSetType const & txs,
            NetClock::duration const & closeTimeResolution,
            NetClock::time_point const & consensusCloseTime,
            bool closeTimeAgree) const
        {
            Ledger res{ *this };
            res.id_.txs.insert(txs.begin(), txs.end());
            res.id_ .seq= seq() + 1;
            res.closeTimeResolution_ = closeTimeResolution;
            res.closeTime_ = consensusCloseTime;
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

    /** Proposal is a position taken in the consensus process and is represented
        directly from the generic types.
    */
    using Proposal = ConsensusProposal<NodeID, Ledger::ID, TxSetType>;

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

        friend std::ostream& operator<< (std::ostream&, MissingTx const&);
    };


    struct Peer;

    using Network = BasicNetwork<Peer*>;

    struct Traits
    {
        using Ledger_t = Ledger;
        using NodeID_t = NodeID;
        using TxSet_t = TxSet;
        using MissingTxException_t = MissingTx;
    };

    /** Represents a single node participating in the consensus process.
        It implements the Callbacks required by Consensus.
    */
    struct Peer : public Consensus<Peer, Traits>
    {
        using Base = Consensus<Peer, Traits>;

        //! Our unique ID
        NodeID id;

        //! last time a ledger closed
        NetClock::time_point lastCloseTime;

        //! openTxs that haven't been closed in a ledger yet
        TxSetType openTxs;

        //! last ledger this peer closed
        Ledger lastClosedLedger;

        //! Handle to network for sending messages
        Network & net;

        // The ledgers, proposals, TxSets and Txs this peer has seen
        bc::flat_map<Ledger::ID, Ledger> ledgers;

        //! Map from Ledger::ID to vector of Positions with that ledger
        //! as the prior ledger
        bc::flat_map<Ledger::ID, std::vector<Proposal>> peerPositions_;
        bc::flat_map<TxSet::ID, TxSet> txSets;
        bc::flat_set<Txn::ID> seenTxs;

        int completedRounds = 0;
        int targetRounds = std::numeric_limits<int>::max();
        const std::chrono::milliseconds timerFreq = 100ms;

        //! All peers start from the default constructed ledger
        Peer(NodeID i, Network & n, beast::Journal j)
            : Consensus<Peer, Traits>( n.clock(), j)
            , id{i}
            , net{n}
        {
            ledgers[lastClosedLedger.id()] = lastClosedLedger;
            lastCloseTime = lastClosedLedger.closeTime();
        }

        // @return whether we are validating,proposing
        // TODO: Bit akward that this is in callbacks, would be nice to extract
        std::pair<bool, bool>
        getMode()
        {
            // in RCL this hits NetworkOps to decide whether we are proposing
            // validating
            return{ true, true };
        }

        Ledger const *
        acquireLedger(Ledger::ID const & ledgerHash)
        {
            auto it = ledgers.find(ledgerHash);
            if (it != ledgers.end())
                return &(it->second);
            // TODO: acquire from network?
            return nullptr;
        }

        auto const &
        proposals(Ledger::ID const & ledgerHash)
        {
            return peerPositions_[ledgerHash];
        }

        TxSet const *
        acquireTxSet(TxSet::ID const & setId)
        {
            auto it = txSets.find(setId);
            if(it != txSets.end())
                return &(it->second);
            // TODO Ask network for it?
            return nullptr;
        }


        bool
        hasOpenTransactions() const
        {
            return !openTxs.empty();
        }

        std::size_t
        proposersValidated(Ledger::ID const & prevLedger)
        {
            // everything auto-validates, so just count the number of peers
            // who have this as the last closed ledger
            int res = 0;
            net.bfs(this, [&](auto, Peer * p)
            {
                if (this == p) return;
                if (p->lastClosedLedger.id() == prevLedger)
                    res++;
            });
            return res;
        }

        std::size_t
        proposersFinished(Ledger::ID const & prevLedger)
        {
            // everything auto-validates, so just count the number of peers
            // who have this as a PRIOR ledger
            int res = 0;
            net.bfs(this, [&](auto, Peer * p)
            {
                if (this == p) return;
                auto const & pLedger = p->lastClosedLedger.id();
                // prevLedger preceeds pLedger iff it has a smaller
                // sequence number AND its Tx's are a subset of pLedger's
                if(prevLedger.seq < pLedger.seq
                    && std::includes(pLedger.txs.begin(), pLedger.txs.end(),
                        prevLedger.txs.begin(), prevLedger.txs.end()))
                {
                    res++;
                }
            });
            return res;
        }

        void
        onStartRound(Ledger const &) {}

        void
        onClose(Ledger const &, bool ) {}

        // don't really offload
        void
        dispatchAccept(TxSet const & f)
        {
            Base::accept(f);
        }

        void
        share(TxSet const &s)
        {
            relay(s);
        }

        Ledger::ID
        getLCL(Ledger::ID const & currLedger,
            Ledger::ID const & priorLedger,
            bool haveCorrectLCL)
        {
            // TODO: cases where this peer is behind others ?
            return lastClosedLedger.id();
        }

        void
        propose(Proposal const & pos)
        {
            relay(pos);
        }

        void
        relay(DisputedTx<Txn, NodeID> const & dispute)
        {
            relay(dispute.tx());
        }

        std::pair <TxSet, Proposal>
        makeInitialPosition(
                Ledger const & prevLedger,
                bool isProposing,
                bool isCorrectLCL,
                NetClock::time_point const & closeTime,
                NetClock::time_point const & now)
        {
            TxSet res{ openTxs };

            return { res,
                Proposal{prevLedger.id(), Proposal::seqJoin, res.id(),
                NetClock::time_point{closeTime}, NetClock::time_point{now}, id} };
        }

        // Process the accepted transaction set, generating the newly closed ledger
        // and clearing out the openTxs that were included.
        // TODO: Kinda nasty it takes so many arguments . . . sign of bad coupling
        bool
        accept(TxSet const& set,
            NetClock::time_point const & consensusCloseTime,
            bool proposing_,
            bool validating_,
            bool haveCorrectLCL_,
            bool consensusFail_,
            Ledger::ID const & prevLedgerHash_,
            Ledger const & previousLedger_,
            NetClock::duration const & closeResolution_,
            NetClock::time_point const &  now,
            std::chrono::milliseconds const & roundTime_,
            hash_map<Txn::ID, DisputedTx <Txn, NodeID>> const & disputes_,
            std::map <NetClock::time_point, int> closeTimes_,
            NetClock::time_point const & closeTime,
            Json::Value && json)
        {
            auto newLedger = previousLedger_.close(set.txs_, closeResolution_,
                closeTime, consensusCloseTime != NetClock::time_point{});
            ledgers[newLedger.id()] = newLedger;

            lastClosedLedger = newLedger;

            auto it = std::remove_if(openTxs.begin(), openTxs.end(),
                [&](Txn const & tx)
                {
                    return set.exists(tx.id());
                });
            openTxs.erase(it, openTxs.end());
            return validating_;
        }

        void
        endConsensus(bool correct)
        {
           // kick off the next round...
           // in the actual implementation, this passes back through
           // network ops
           ++completedRounds;
           // startRound sets the LCL state, so we need to call it once after
           // the last requested round completes
           if(completedRounds <= targetRounds)
           {
             startRound(now(), lastClosedLedger.id(),
                    lastClosedLedger);
           }
        }

        //-------------------------------------------------------------------------
        // non-callback helpers
        void
        receive(Proposal const & p)
        {
            // filter proposals already seen?
            peerPositions_[p.prevLedger()].push_back(p);
            peerProposal(now(), p);

        }

        void
        receive(TxSet const & txs)
        {
            // save and map complete?
            auto it = txSets.insert(std::make_pair(txs.id(), txs));
            if(it.second)
                gotTxSet(now(), txs);
        }

        void
        receive(Txn const & tx)
        {
            if (seenTxs.find(tx.id()) == seenTxs.end())
            {
                openTxs.insert(tx);
                seenTxs.insert(tx.id());
            }
        }

        template <class T>
        void
        relay(T const & t)
        {
            for(auto const& link : net.links(this))
                net.send(this, link.to,
                    [&, msg = t, to = link.to]
                    {
                        to->receive(msg);
                    });
        }

        // Receive a locally submitted transaction
        void
        submit(Txn const & tx)
        {
            receive(tx);
        }

        void
        timerEntry()
        {
            Base::timerEntry(now());
            // only reschedule if not completed
            if(completedRounds < targetRounds)
                net.timer(timerFreq, [&]() { timerEntry(); });
        }
        void
        start()
        {
            net.timer(timerFreq, [&]() { timerEntry(); });
            startRound(now(), lastClosedLedger.id(),
                lastClosedLedger);
        }

        NetClock::time_point
        now() const
        {
            using namespace std::chrono;
            // For the purposes of this test, the manual clock is only
            // used to measure durations, so its epoch relative to the
            // NetClock epoch doesn't matter. We just take the NetClock time
            // to be the duration elapsed on the manual clock.
            return NetClock::time_point{duration_cast<NetClock::duration>(
                       net.now().time_since_epoch())};
        }
    };

    void
    testStandalone()
    {
        Network n;
        Peer p{ 0, n, beast::Journal{} };

        p.targetRounds = 1;
        p.start();
        p.receive(Txn{ 1 });

        n.step();

        // Inspect that the proper ledger was created
        BEAST_EXPECT(p.LCL().seq == 1);
        BEAST_EXPECT(p.LCL() == p.lastClosedLedger.id());
        BEAST_EXPECT(p.lastClosedLedger.id().txs.size() == 1);
        BEAST_EXPECT(p.lastClosedLedger.id().txs.find(Txn{ 1 })
            != p.lastClosedLedger.id().txs.end());
        BEAST_EXPECT(p.getLastCloseProposers() == 0);
    }


    void
    run_consensus(Network & n, std::vector<std::shared_ptr<Peer>> & peers, int rounds)
    {
        for (auto & p : peers)
        {
            p->targetRounds = p->completedRounds + rounds;
            p->start();
        }
        n.step();
    }

    void
    testPeersAgree()
    {
        Network n;
        std::vector<std::shared_ptr<Peer>> peers;
        for (int i = 0; i < 5; ++i)
            peers.push_back(std::make_shared<Peer>(i, n, beast::Journal{}));

        // fully connect the graph?
        for (int i = 0; i < peers.size(); ++i )
            for (int j = 0; j < peers.size(); ++j)
            {
                if (i != j)
                    n.connect(peers[i].get(), peers[j].get(), 200ms * (i + 1));
            }

        // everyone submits their own ID as a TX
        for (auto & p : peers)
            p->submit(Txn{ p->id });


        run_consensus(n, peers, 1);
        // No transactions make the initial ledger
        // since each peer only saw its local Tx
        for (auto & p : peers)
        {
            auto const &lgrID = p->LCL();
            BEAST_EXPECT(lgrID.seq == 1);
            BEAST_EXPECT(p->getLastCloseProposers() == peers.size() - 1);
            for (int i = 0; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.empty());
        }

        run_consensus(n, peers, 1);

        // Tx should now be shared, so verify all peers have
        // same LCL and it has all the TXs
        for (auto & p : peers)
        {
            auto const &lgrID = p->LCL();
            BEAST_EXPECT(lgrID.seq == 2);
            BEAST_EXPECT(p->getLastCloseProposers() == peers.size() - 1);
            for(int i = 0; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.find(Txn{ i }) != lgrID.txs.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.txs == peers[0]->LCL().txs);
        }
    }

    void
    testSlowPeer()
    {
        Network n;
        std::vector<std::shared_ptr<Peer>> peers;
        for (int i = 0; i < 5; ++i)
            peers.push_back(std::make_shared<Peer>(i, n, beast::Journal{}));

        // Fully connected, but node 0 has a delay to peers
        for (int i = 0; i < peers.size(); ++i )
            for (int j = 0; j < peers.size(); ++j)
            {
                if (i != j)
                {
                    auto delay = (i == 0 || j == 0) ? 1000ms : 0ms;
                    n.connect(peers[i].get(), peers[j].get(), delay);
                }
            }

        // All but peer 0 submits their own ID as a TX
        for (auto & p : peers)
        {
            if(p->id != 0)
                p->submit(Txn{ p->id });
        }
        // Get initial round out of way
        run_consensus(n, peers, 1);

        // Tx to Peer 0 that won't arrive to peers in
        // time for next round
        peers[0]->submit(Txn{ 0 });

        run_consensus(n, peers, 1);

        // Verify all peers have same LCL but are missing transaction 0 which
        // was not received by all peers before the ledger closed
        for (auto & p : peers)
        {
            auto const &lgrID = p->LCL();
            BEAST_EXPECT(lgrID.seq == 2);
            BEAST_EXPECT(p->getLastCloseProposers() == peers.size() - 1);
            // Peer 0 closes first because it sees a quorum of agreeing positions
            // from all other peers in one hop (1->0, 2->0, ..)
            // The other peers take an extra timer period before they find that
            // Peer 0 agrees with them ( 1->0->1,  2->0->2, ...)
            if(p->id != 0)
                BEAST_EXPECT(p->getLastConvergeDuration()
                    > peers[0]->getLastConvergeDuration());

            BEAST_EXPECT(lgrID.txs.find(Txn{ 0 }) == lgrID.txs.end());
            for(int i = 1; i < peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.find(Txn{ i }) != lgrID.txs.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.txs == peers[0]->LCL().txs);
        }
        BEAST_EXPECT(peers[0]->openTxs.find(Txn{ 0 }) != peers[0]->openTxs.end());
    }

    void
    run() override
    {
        testStandalone();
        testPeersAgree();
        testSlowPeer();
    }
};

inline std::ostream& operator<<(std::ostream & o, Consensus_test::TxSetType const & txs)
{
    o << "{ ";
    bool do_comma = false;
    for (auto const & tx : txs)
    {
        if (do_comma)
            o << ", ";
        else
            do_comma = true;
        o << tx.id();


    }
    o << " }";
    return o;

}

inline std::string to_string(Consensus_test::TxSetType const & txs)
{
    std::stringstream ss;
    ss << txs;
    return ss.str();
}

inline std::ostream & operator<<(std::ostream & o, Consensus_test::Ledger::ID const & id)
{
    return o << id.seq << "," << id.txs;
}

inline std::string to_string(Consensus_test::Ledger::ID const & id)
{
    std::stringstream ss;
    ss << id;
    return ss.str();
}

std::ostream& operator<< (std::ostream & o, Consensus_test::MissingTx const &m)
{
    return o << m.what();
}

template <class Hasher>
void
inline hash_append(Hasher& h, Consensus_test::Txn const & tx)
{
    using beast::hash_append;
    hash_append(h, tx.id());
}
BEAST_DEFINE_TESTSUITE(Consensus, consensus, ripple);
} // test
} // ripple
