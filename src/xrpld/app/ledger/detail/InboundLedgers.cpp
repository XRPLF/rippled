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

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpl/basics/DecayingSample.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/container/aged_map.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/jss.h>

#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace ripple {

class InboundLedgersImp : public InboundLedgers
{
private:
    Application& app_;
    std::mutex fetchRateMutex_;
    // measures ledgers per second, constants are important
    DecayWindow<30, clock_type> fetchRate_;
    beast::Journal const j_;

public:
    // How long before we try again to acquire the same ledger
    static constexpr std::chrono::minutes const kReacquireInterval{5};

    InboundLedgersImp(
        Application& app,
        clock_type& clock,
        beast::insight::Collector::ptr const& collector,
        std::unique_ptr<PeerSetBuilder> peerSetBuilder)
        : app_(app)
        , fetchRate_(clock.now())
        , j_(app.journal("InboundLedger"))
        , m_clock(clock)
        , mRecentFailures(clock)
        , mCounter(collector->make_counter("ledger_fetches"))
        , mPeerSetBuilder(std::move(peerSetBuilder))
    {
    }

    /** @callgraph */
    std::shared_ptr<Ledger const>
    acquire(
        uint256 const& hash,
        std::uint32_t seq,
        InboundLedger::Reason reason) override
    {
        auto doAcquire = [&, seq, reason]() -> std::shared_ptr<Ledger const> {
            assert(hash.isNonZero());

            // probably not the right rule
            if (app_.getOPs().isNeedNetworkLedger() &&
                (reason != InboundLedger::Reason::GENERIC) &&
                (reason != InboundLedger::Reason::CONSENSUS))
                return {};

            bool isNew = true;
            std::shared_ptr<InboundLedger> inbound;
            {
                ScopedLockType sl(mLock);
                if (stopping_)
                {
                    return {};
                }

                auto it = mLedgers.find(hash);
                if (it != mLedgers.end())
                {
                    isNew = false;
                    inbound = it->second;
                }
                else
                {
                    inbound = std::make_shared<InboundLedger>(
                        app_,
                        hash,
                        seq,
                        reason,
                        std::ref(m_clock),
                        mPeerSetBuilder->build());
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

            return inbound->getLedger();
        };
        using namespace std::chrono_literals;
        std::shared_ptr<Ledger const> ledger = perf::measureDurationAndLog(
            doAcquire, "InboundLedgersImp::acquire", 500ms, j_);

        return ledger;
    }

    void
    acquireAsync(
        uint256 const& hash,
        std::uint32_t seq,
        InboundLedger::Reason reason) override
    {
        std::unique_lock lock(acquiresMutex_);
        try
        {
            if (pendingAcquires_.contains(hash))
                return;
            pendingAcquires_.insert(hash);
            scope_unlock unlock(lock);
            acquire(hash, seq, reason);
        }
        catch (std::exception const& e)
        {
            JLOG(j_.warn())
                << "Exception thrown for acquiring new inbound ledger " << hash
                << ": " << e.what();
        }
        catch (...)
        {
            JLOG(j_.warn())
                << "Unknown exception thrown for acquiring new inbound ledger "
                << hash;
        }
        pendingAcquires_.erase(hash);
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
        std::shared_ptr<protocol::TMLedgerData> packet) override
    {
        if (auto ledger = find(hash))
        {
            JLOG(j_.trace()) << "Got data (" << packet->nodes().size()
                             << ") for acquiring ledger: " << hash;

            // Stash the data for later processing and see if we need to
            // dispatch
            if (ledger->gotData(std::weak_ptr<Peer>(peer), packet))
                app_.getJobQueue().addJob(
                    jtLEDGER_DATA, "processLedgerData", [ledger]() {
                        ledger->runData();
                    });

            return true;
        }

        JLOG(j_.trace()) << "Got data for ledger " << hash
                         << " which we're no longer acquiring";

        // If it's state node data, stash it because it still might be
        // useful.
        if (packet->type() == protocol::liAS_NODE)
        {
            app_.getJobQueue().addJob(
                jtLEDGER_DATA, "gotStaleData", [this, packet]() {
                    gotStaleData(packet);
                });
        }

        return false;
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

    /** We got some data for a ledger we are no longer acquiring Since we paid
        the price to receive it, we might as well stash it in case we need it.

        Nodes are received in wire format and must be stashed/hashed in prefix
        format
    */
    void
    gotStaleData(std::shared_ptr<protocol::TMLedgerData> packet_ptr) override
    {
        Serializer s;
        try
        {
            for (int i = 0; i < packet_ptr->nodes().size(); ++i)
            {
                auto const& node = packet_ptr->nodes(i);

                if (!node.has_nodeid() || !node.has_nodedata())
                    return;

                auto newNode =
                    SHAMapTreeNode::makeFromWire(makeSlice(node.nodedata()));

                if (!newNode)
                    return;

                s.erase();
                newNode->serializeWithPrefix(s);

                app_.getLedgerMaster().addFetchPack(
                    newNode->getHash().as_uint256(),
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
    // a reason of history
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

        std::vector<std::pair<uint256, std::shared_ptr<InboundLedger>>> acqs;

        {
            ScopedLockType sl(mLock);

            acqs.reserve(mLedgers.size());
            for (auto const& it : mLedgers)
            {
                assert(it.second);
                acqs.push_back(it);
            }
            for (auto const& it : mRecentFailures)
            {
                if (it.second > 1)
                    ret[std::to_string(it.second)][jss::failed] = true;
                else
                    ret[to_string(it.first)][jss::failed] = true;
            }
        }

        for (auto const& it : acqs)
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
        auto const start = m_clock.now();

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
                auto const la = it->second->getLastAction();

                if (la > start)
                {
                    it->second->touch();
                    ++it;
                }
                else if ((la + std::chrono::minutes(1)) < start)
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

        JLOG(j_.debug())
            << "Swept " << stuffToSweep.size() << " out of " << total
            << " inbound ledgers. Duration: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   m_clock.now() - start)
                   .count()
            << "ms";
    }

    void
    stop() override
    {
        ScopedLockType lock(mLock);
        stopping_ = true;
        mLedgers.clear();
        mRecentFailures.clear();
    }

    std::size_t
    cacheSize() override
    {
        ScopedLockType lock(mLock);
        return mLedgers.size();
    }

private:
    clock_type& m_clock;

    using ScopedLockType = std::unique_lock<std::recursive_mutex>;
    std::recursive_mutex mLock;

    bool stopping_ = false;
    using MapType = hash_map<uint256, std::shared_ptr<InboundLedger>>;
    MapType mLedgers;

    beast::aged_map<uint256, std::uint32_t> mRecentFailures;

    beast::insight::Counter mCounter;

    std::unique_ptr<PeerSetBuilder> mPeerSetBuilder;

    std::set<uint256> pendingAcquires_;
    std::mutex acquiresMutex_;
};

//------------------------------------------------------------------------------

std::unique_ptr<InboundLedgers>
make_InboundLedgers(
    Application& app,
    InboundLedgers::clock_type& clock,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<InboundLedgersImp>(
        app, clock, collector, make_PeerSetBuilder(app));
}

}  // namespace ripple
