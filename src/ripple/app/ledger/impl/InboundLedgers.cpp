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

#include <ripple/app/ledger/InboundLedgers.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/DecayingSample.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/container/aged_map.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/JobQueue.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/protocol/jss.h>
#include <memory>
#include <mutex>

namespace ripple {

class InboundLedgersImp : public InboundLedgers, public Stoppable
{
private:
    Application& app_;
    std::mutex fetchRateMutex_;
    // measures ledgers per second, constants are important
    DecayWindow<30, clock_type> fetchRate_;
    beast::Journal const j_;

public:
    using u256_acq_pair = std::pair<uint256, std::shared_ptr<InboundLedger>>;

    // How long before we try again to acquire the same ledger
    static const std::chrono::minutes kReacquireInterval;

    InboundLedgersImp(
        Application& app,
        clock_type& clock,
        Stoppable& parent,
        beast::insight::Collector::ptr const& collector)
        : Stoppable("InboundLedgers", parent)
        , app_(app)
        , fetchRate_(clock.now())
        , j_(app.journal("InboundLedger"))
        , m_clock(clock)
        , mRecentFailures(clock)
        , mCounter(collector->make_counter("ledger_fetches"))
    {
    }

    /** @callgraph */
    std::shared_ptr<Ledger const>
    acquire(
        uint256 const& hash,
        std::uint32_t seq,
        InboundLedger::Reason reason) override
    {
        assert(hash.isNonZero());
        assert(
            reason != InboundLedger::Reason::SHARD ||
            (seq != 0 && app_.getShardStore()));
        if (isStopping())
            return {};

        bool isNew = true;
        std::shared_ptr<InboundLedger> inbound;
        {
            ScopedLockType sl(mLock);
            auto it = mLedgers.find(hash);
            if (it != mLedgers.end())
            {
                isNew = false;
                inbound = it->second;
            }
            else
            {
                inbound = std::make_shared<InboundLedger>(
                    app_, hash, seq, reason, std::ref(m_clock));
                mLedgers.emplace(hash, inbound);
                inbound->init(sl);
                ++mCounter;
            }
        }

        if (inbound->isFailed())
            return {};

        if (!isNew)
            inbound->update(seq);

        if (!inbound->isComplete())
            return {};

        if (reason == InboundLedger::Reason::HISTORY)
        {
            if (inbound->getLedger()->stateMap().family().isShardBacked())
                app_.getNodeStore().storeLedger(inbound->getLedger());
        }
        else if (reason == InboundLedger::Reason::SHARD)
        {
            auto shardStore = app_.getShardStore();
            if (!shardStore)
            {
                JLOG(j_.error())
                    << "Acquiring shard with no shard store available";
                return {};
            }
            if (inbound->getLedger()->stateMap().family().isShardBacked())
                shardStore->setStored(inbound->getLedger());
            else
                shardStore->storeLedger(inbound->getLedger());
        }
        return inbound->getLedger();
    }

    std::shared_ptr<InboundLedger>
    find(uint256 const& hash) override
    {
        assert(hash.isNonZero());

        std::shared_ptr<InboundLedger> ret;

        {
            ScopedLockType sl(mLock);

            auto it = mLedgers.find(hash);
            if (it != mLedgers.end())
            {
                ret = it->second;
            }
        }

        return ret;
    }

    /*
    This gets called when
        "We got some data from an inbound ledger"

    inboundLedgerTrigger:
      "What do we do with this partial data?"
      Figures out what to do with the responses to our requests for information.

    */
    // means "We got some data from an inbound ledger"

    // VFALCO TODO Remove the dependency on the Peer object.
    /** We received a TMLedgerData from a peer.
     */
    bool
    gotLedgerData(
        LedgerHash const& hash,
        std::shared_ptr<Peer> peer,
        std::shared_ptr<protocol::TMLedgerData> packet_ptr) override
    {
        protocol::TMLedgerData& packet = *packet_ptr;

        JLOG(j_.trace()) << "Got data (" << packet.nodes().size()
                         << ") for acquiring ledger: " << hash;

        auto ledger = find(hash);

        if (!ledger)
        {
            JLOG(j_.trace()) << "Got data for ledger we're no longer acquiring";

            // If it's state node data, stash it because it still might be
            // useful.
            if (packet.type() == protocol::liAS_NODE)
            {
                app_.getJobQueue().addJob(
                    jtLEDGER_DATA, "gotStaleData", [this, packet_ptr](Job&) {
                        gotStaleData(packet_ptr);
                    });
            }

            return false;
        }

        // Stash the data for later processing and see if we need to dispatch
        if (ledger->gotData(std::weak_ptr<Peer>(peer), packet_ptr))
            app_.getJobQueue().addJob(
                jtLEDGER_DATA, "processLedgerData", [this, hash](Job&) {
                    doLedgerData(hash);
                });

        return true;
    }

    void
    logFailure(uint256 const& h, std::uint32_t seq) override
    {
        ScopedLockType sl(mLock);

        mRecentFailures.emplace(h, seq);
    }

    bool
    isFailure(uint256 const& h) override
    {
        ScopedLockType sl(mLock);

        beast::expire(mRecentFailures, kReacquireInterval);
        return mRecentFailures.find(h) != mRecentFailures.end();
    }

    /** Called (indirectly) only by gotLedgerData(). */
    void
    doLedgerData(LedgerHash hash)
    {
        if (auto ledger = find(hash))
            ledger->runData();
    }

    /** We got some data for a ledger we are no longer acquiring Since we paid
        the price to receive it, we might as well stash it in case we need it.

        Nodes are received in wire format and must be stashed/hashed in prefix
        format
    */
    void
    gotStaleData(std::shared_ptr<protocol::TMLedgerData> packet_ptr) override
    {
        const uint256 uZero;
        Serializer s;
        try
        {
            for (int i = 0; i < packet_ptr->nodes().size(); ++i)
            {
                auto const& node = packet_ptr->nodes(i);

                if (!node.has_nodeid() || !node.has_nodedata())
                    return;

                auto id_string = node.nodeid();
                auto newNode = SHAMapAbstractNode::makeFromWire(
                    makeSlice(node.nodedata()),
                    0,
                    SHAMapNodeID(id_string.data(), id_string.size()));

                if (!newNode)
                    return;

                s.erase();
                newNode->addRaw(s, snfPREFIX);

                app_.getLedgerMaster().addFetchPack(
                    newNode->getNodeHash().as_uint256(),
                    std::make_shared<Blob>(s.begin(), s.end()));
            }
        }
        catch (std::exception const&)
        {
        }
    }

    void
    clearFailures() override
    {
        ScopedLockType sl(mLock);

        mRecentFailures.clear();
        mLedgers.clear();
    }

    std::size_t
    fetchRate() override
    {
        std::lock_guard lock(fetchRateMutex_);
        return 60 * fetchRate_.value(m_clock.now());
    }

    // Should only be called with an inboundledger that has
    // a reason of history or shard
    void
    onLedgerFetched() override
    {
        std::lock_guard lock(fetchRateMutex_);
        fetchRate_.add(1, m_clock.now());
    }

    Json::Value
    getInfo() override
    {
        Json::Value ret(Json::objectValue);

        std::vector<u256_acq_pair> acquires;
        {
            ScopedLockType sl(mLock);

            acquires.reserve(mLedgers.size());
            for (auto const& it : mLedgers)
            {
                assert(it.second);
                acquires.push_back(it);
            }
            for (auto const& it : mRecentFailures)
            {
                if (it.second > 1)
                    ret[beast::lexicalCastThrow<std::string>(it.second)]
                       [jss::failed] = true;
                else
                    ret[to_string(it.first)][jss::failed] = true;
            }
        }

        for (auto const& it : acquires)
        {
            // getJson is expensive, so call without the lock
            std::uint32_t seq = it.second->getSeq();
            if (seq > 1)
                ret[std::to_string(seq)] = it.second->getJson(0);
            else
                ret[to_string(it.first)] = it.second->getJson(0);
        }

        return ret;
    }

    void
    gotFetchPack() override
    {
        std::vector<std::shared_ptr<InboundLedger>> acquires;
        {
            ScopedLockType sl(mLock);

            acquires.reserve(mLedgers.size());
            for (auto const& it : mLedgers)
            {
                assert(it.second);
                acquires.push_back(it.second);
            }
        }

        for (auto const& acquire : acquires)
        {
            acquire->checkLocal();
        }
    }

    void
    sweep() override
    {
        clock_type::time_point const now(m_clock.now());

        // Make a list of things to sweep, while holding the lock
        std::vector<MapType::mapped_type> stuffToSweep;
        std::size_t total;
        {
            ScopedLockType sl(mLock);
            MapType::iterator it(mLedgers.begin());
            total = mLedgers.size();
            stuffToSweep.reserve(total);

            while (it != mLedgers.end())
            {
                if (it->second->getLastAction() > now)
                {
                    it->second->touch();
                    ++it;
                }
                else if (
                    (it->second->getLastAction() + std::chrono::minutes(1)) <
                    now)
                {
                    stuffToSweep.push_back(it->second);
                    // shouldn't cause the actual final delete
                    // since we are holding a reference in the vector.
                    it = mLedgers.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            beast::expire(mRecentFailures, kReacquireInterval);
        }

        JLOG(j_.debug()) << "Swept " << stuffToSweep.size() << " out of "
                         << total << " inbound ledgers.";
    }

    void
    onStop() override
    {
        ScopedLockType lock(mLock);

        mLedgers.clear();
        mRecentFailures.clear();

        stopped();
    }

private:
    clock_type& m_clock;

    using ScopedLockType = std::unique_lock<std::recursive_mutex>;
    std::recursive_mutex mLock;

    using MapType = hash_map<uint256, std::shared_ptr<InboundLedger>>;
    MapType mLedgers;

    beast::aged_map<uint256, std::uint32_t> mRecentFailures;

    beast::insight::Counter mCounter;
};

//------------------------------------------------------------------------------

decltype(InboundLedgersImp::kReacquireInterval)
    InboundLedgersImp::kReacquireInterval{5};

InboundLedgers::~InboundLedgers() = default;

std::unique_ptr<InboundLedgers>
make_InboundLedgers(
    Application& app,
    InboundLedgers::clock_type& clock,
    Stoppable& parent,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<InboundLedgersImp>(app, clock, parent, collector);
}

}  // namespace ripple
