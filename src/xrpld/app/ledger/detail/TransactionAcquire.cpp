#include <xrpld/app/ledger/detail/TransactionAcquire.h>

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/InboundTransactions.h>
#include <xrpld/app/ledger/detail/LedgerSpanNames.h>
#include <xrpld/app/ledger/detail/TimeoutCounter.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/PeerSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/Job.h>
#include <xrpl/server/NetworkOPs.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAddNode.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>
#include <xrpl/telemetry/SpanGuard.h>

#include <xrpl.pb.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl {

using namespace std::chrono_literals;

// Timeout interval in milliseconds
constexpr auto kTxAcquireTimeout = 250ms;

static constexpr auto kNormTimeouts = 4;
static constexpr auto kMaxTimeouts = 20;

TransactionAcquire::TransactionAcquire(
    Application& app,
    uint256 const& hash,
    std::unique_ptr<PeerSet> peerSet)
    : TimeoutCounter(
          app,
          hash,
          kTxAcquireTimeout,
          {.jobType = JtTxnData, .jobName = "TxAcq", .jobLimit = {}},
          app.getJournal("TransactionAcquire"))
    , peerSet_(std::move(peerSet))
{
    map_ = std::make_shared<SHAMap>(SHAMapType::TRANSACTION, hash, app_.getNodeFamily());
    map_->setUnbacked();
}

TransactionAcquire::~TransactionAcquire()
{
    // Last exit. A set dropped here (swept by newRound because it never
    // arrived, or torn down at shutdown) reached no result, so this is what
    // stamps outcome=abandoned instead of exporting a span with no outcome at
    // all. Already-finalized acquisitions are untouched: the helper is
    // idempotent.
    finalizeAcquireSpan();
}

void
TransactionAcquire::finalizeAcquireSpan() noexcept
{
    // Idempotent: the handle is cleared below, so a later exit finds nothing to
    // finalize and cannot overwrite the outcome the real exit recorded.
    if (!acquireSpan_)
        return;

    // The attribute writes are wrapped because the destructor is one of the
    // callers: an exception escaping there during unwinding would terminate the
    // process. Each setAttribute is itself noexcept today; the try is the
    // structural guarantee that stays correct if that ever changes.
    try
    {
        if (*acquireSpan_)
        {
            using namespace telemetry;
            // Derived from this object's own flags by the shared rule, so no
            // call site can mislabel an exit and every exit gets an outcome.
            // No flag set means the set was dropped while still in flight.
            acquireSpan_->setAttribute(
                ledger_span::attr::outcome,
                ledger_span::phaseOutcome(failed_, complete_, timedOut_));
            acquireSpan_->setAttribute(
                ledger_span::attr::timeouts, static_cast<std::int64_t>(timeouts_));
            // Recorded explicitly as well as implied by the span's own
            // duration: this is the number an operator reads straight off a
            // trace when asking how long a proposed set took to arrive.
            acquireSpan_->setAttribute(
                ledger_span::attr::durationMs,
                static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - acquireStart_)
                                              .count()));
            // Peers still tracked for this fetch. Safe here, unlike the ledger
            // equivalent: PeerSet::getPeerIds() returns its own member set and
            // takes no Overlay lock.
            acquireSpan_->setAttribute(
                ledger_span::attr::peerCount,
                static_cast<std::int64_t>(peerSet_->getPeerIds().size()));
        }
    }
    catch (...)  // NOLINT(bugprone-empty-catch)
    {
        // Telemetry must never break an acquisition, and this also runs from
        // the destructor. A span missing one attribute is still worth
        // exporting, so fall through and end it below.
    }

    // End the span, outside the try so it happens on every path. Unconditional
    // so the span never leaks even when it was inactive, and so this helper is
    // exactly-once: a later exit sees an empty handle and returns above.
    // ~SpanGuard is implicitly noexcept, so this cannot throw out of here.
    acquireSpan_.reset();
}

void
TransactionAcquire::done()
{
    // We hold a PeerSet lock and so cannot do real work here

    // Keep the span active as the ambient context across the outcome log below
    // so that line carries the span's trace_id. The activation is non-owning;
    // acquireSpan_ still owns the span. It pops at the end of this block, while
    // the span is still alive, and only then is the span finalized and ended.
    {
        auto acquireActivation = telemetry::activateIfLive(acquireSpan_);

        if (failed_)
        {
            JLOG(journal_.debug()) << "Failed to acquire TX set " << hash_;
        }
        else
        {
            JLOG(journal_.debug()) << "Acquired TX set " << hash_;
            map_->setImmutable();

            uint256 const& hash(hash_);
            std::shared_ptr<SHAMap> const& map(map_);
            auto const pap = &app_;
            // Note that, when we're in the process of shutting down, addJob()
            // may reject the request.  If that happens then giveSet() will
            // not be called.  That's fine.  According to David the giveSet()
            // call just updates the consensus and related structures when we
            // acquire a transaction set. No need to update them if we're
            // shutting down.
            app_.getJobQueue().addJob(JtTxnData, "ComplAcquire", [pap, hash, map]() {
                pap->getInboundTransactions().giveSet(hash, map, true);
            });
        }
        // acquireActivation pops here, before the span is ended below.
    }

    // The normal exit. done() is reached from trigger() (complete or invalid
    // data) and from onTimer() (timeout budget exhausted), and both are
    // terminal, so the span ends here rather than waiting for the object to be
    // released. TimeoutCounter::isDone() is already true by now, so the timer
    // loop will not call back in.
    finalizeAcquireSpan();
}

void
TransactionAcquire::onTimer(bool progress, ScopedLockType& psl)
{
    if (timeouts_ > kMaxTimeouts)
    {
        // Record WHY before done() finalizes the span. failed_ alone would
        // report `failed`, which reads as bad data; this path is instead "no
        // peer ever supplied the set", and that is the distinction a stuck
        // fresh sync turns on.
        timedOut_ = true;
        failed_ = true;
        done();
        return;
    }

    if (timeouts_ >= kNormTimeouts)
        trigger(nullptr);

    addPeers(1);
}

std::weak_ptr<TimeoutCounter>
TransactionAcquire::pmDowncast()
{
    return shared_from_this();
}

void
TransactionAcquire::trigger(std::shared_ptr<Peer> const& peer)
{
    if (complete_)
    {
        JLOG(journal_.info()) << "trigger after complete";
        return;
    }
    if (failed_)
    {
        JLOG(journal_.info()) << "trigger after fail";
        return;
    }

    if (!haveRoot_)
    {
        JLOG(journal_.trace()) << "TransactionAcquire::trigger " << (peer ? "havePeer" : "noPeer")
                               << " no root";
        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash(hash_.begin(), hash_.size());
        tmGL.set_itype(protocol::liTS_CANDIDATE);
        tmGL.set_querydepth(3);  // We probably need the whole thing

        if (timeouts_ != 0)
            tmGL.set_querytype(protocol::qtINDIRECT);

        *(tmGL.add_nodeids()) = SHAMapNodeID().getRawString();
        peerSet_->sendRequest(tmGL, peer);
    }
    else if (!map_->isValid())
    {
        failed_ = true;
        done();
    }
    else
    {
        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());
        auto nodes = map_->getMissingNodes(256, &sf);

        if (nodes.empty())
        {
            if (map_->isValid())
            {
                complete_ = true;
            }
            else
            {
                failed_ = true;
            }

            done();
            return;
        }

        protocol::TMGetLedger tmGL;
        tmGL.set_ledgerhash(hash_.begin(), hash_.size());
        tmGL.set_itype(protocol::liTS_CANDIDATE);

        if (timeouts_ != 0)
            tmGL.set_querytype(protocol::qtINDIRECT);

        for (auto const& node : nodes)
        {
            *tmGL.add_nodeids() = node.first.getRawString();
        }
        peerSet_->sendRequest(tmGL, peer);
    }
}

SHAMapAddNode
TransactionAcquire::takeNodes(
    std::vector<std::pair<SHAMapNodeID, SHAMapTreeNodePtr>> data,
    std::shared_ptr<Peer> const& peer)
{
    ScopedLockType const sl(mtx_);

    if (complete_)
    {
        JLOG(journal_.trace()) << "TX set complete";
        return SHAMapAddNode();
    }

    if (failed_)
    {
        JLOG(journal_.trace()) << "TX set failed";
        return SHAMapAddNode();
    }

    try
    {
        if (data.empty())
            return SHAMapAddNode::invalid();

        ConsensusTransSetSF sf(app_, app_.getTempNodeCache());

        for (auto& d : data)
        {
            if (d.first.isRoot())
            {
                if (haveRoot_)
                {
                    JLOG(journal_.debug()) << "Got root TXS node, already have it";
                }
                else if (!map_->addRootNode(SHAMapHash{hash_}, std::move(d.second), nullptr)
                              .isGood())
                {
                    JLOG(journal_.warn()) << "TX acquire got bad root node for TX set " << hash_
                                          << " from peer " << peer->id();
                    return SHAMapAddNode::invalid();
                }
                else
                {
                    haveRoot_ = true;
                }
            }
            else if (!map_->addKnownNode(d.first, std::move(d.second), &sf).isGood())
            {
                JLOG(journal_.warn()) << "TX acquire got bad non-root node " << d.first
                                      << " for TX set " << hash_ << " from peer " << peer->id();
                return SHAMapAddNode::invalid();
            }
        }

        trigger(peer);
        progress_ = true;
        return SHAMapAddNode::useful();
    }
    catch (std::exception const& ex)
    {
        JLOG(journal_.error()) << "Peer " << peer->id()
                               << " sent us junky transaction node data: " << ex.what();
        return SHAMapAddNode::invalid();
    }
}

void
TransactionAcquire::addPeers(std::size_t limit)
{
    peerSet_->addPeers(
        limit,
        [this](auto peer) { return peer->hasTxSet(hash_); },
        [this](auto peer) { trigger(peer); });
}

void
TransactionAcquire::init(int numPeers)
{
    ScopedLockType sl(mtx_);

    // Start the clock and the span before the first peer is asked. addPeers()
    // below calls trigger() per peer, and trigger() can reach done() in the
    // same call stack, so anything set up after it could be missed entirely by
    // a set that resolves immediately.
    acquireStart_ = std::chrono::steady_clock::now();

    // Span the acquisition so a proposed tx set that never arrives is
    // traceable. TransactionAcquire had no telemetry at all before this, so a
    // consensus round stalled waiting on a set looked identical to an idle one.
    // Finalized by finalizeAcquireSpan() on whichever exit this object takes,
    // including the destructor.
    {
        using namespace telemetry;
        // acquireSpan_ is emplaced here but may be reset on a JtTxnData worker
        // thread. A SpanGuard is thread-free (owns no thread-local Scope), so it
        // can be created here and destroyed on the worker with no scope to
        // strip. Category Ledger: a tx set is ledger-fetch traffic, and this
        // shares the `trace_ledger` flag with ledger.acquire so the two halves
        // of a stuck sync cannot be enabled apart.
        acquireSpan_.emplace(
            SpanGuard::span(
                TraceCategory::Ledger, ledger_span::prefix::txset, ledger_span::op::acquire));
        if (*acquireSpan_)
        {
            // The set's root hash is the only identity it has -- there is no
            // sequence number -- so it is what makes a stalled fetch findable
            // in a trace search.
            acquireSpan_->setAttribute(ledger_span::attr::txSetHash, to_string(hash_).c_str());
        }
    }

    addPeers(numPeers);

    setTimer(sl);
}

void
TransactionAcquire::stillNeed()
{
    ScopedLockType const sl(mtx_);

    timeouts_ = std::min<int>(timeouts_, kNormTimeouts);
    failed_ = false;
}

}  // namespace xrpl
