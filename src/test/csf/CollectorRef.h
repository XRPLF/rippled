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
#ifndef RIPPLE_TEST_CSF_COLLECTOREF_H_INCLUDED
#define RIPPLE_TEST_CSF_COLLECTOREF_H_INCLUDED

#include <test/csf/events.h>
#include <test/csf/SimTime.h>

namespace ripple {
namespace test {
namespace csf {

/** Holds a type-erased reference to an arbitray collector.

    A collector is any class that implements

        on(NodeID, SimTime, Event)

    for all events emitted by a Peer.

    This class is used to type-erase the actual collector used by each peer in
    the simulation. The idea is to compose complicated and typed collectors using
    the helpers in collectors.h, then only type erase at the higher-most level
    when adding to the simulation.

    The example code below demonstrates the reason for storing the collector
    as a reference.  The collector's lifetime will generally be be longer than
    the simulation; perhaps several simulations are run for a single collector
    instance.  The collector potentially stores lots of data as well, so the
    simulation needs to point to the single instance, rather than requiring
    collectors to manage copying that data efficiently in their design.

    @code
        // Initialize a specific collector that might write to a file.
        SomeFancyCollector collector{"out.file"};

        // Setup your simulation
        Sim sim(trustgraph, topology, collector);

        // Run the simulation
        sim.run(100);

        // do any reported related to the collector
        collector.report();

    @endcode

    @note If a new event type is added, it needs to be added to the interfaces
    below.

*/
class CollectorRef
{
    using tp = SimTime;

    // Interface for type-erased collector instance
    struct ICollector
    {
        virtual ~ICollector() = default;

        virtual void
        on(PeerID node, tp when, Share<Tx> const&) = 0;

        virtual void
        on(PeerID node, tp when, Share<TxSet> const&) = 0;

        virtual void
        on(PeerID node, tp when, Share<Validation> const&) = 0;

        virtual void
        on(PeerID node, tp when, Share<Ledger> const&) = 0;

        virtual void
        on(PeerID node, tp when, Share<Proposal> const&) = 0;

        virtual void
        on(PeerID node, tp when, Receive<Tx> const&) = 0;

        virtual void
        on(PeerID node, tp when, Receive<TxSet> const&) = 0;

        virtual void
        on(PeerID node, tp when, Receive<Validation> const&) = 0;

        virtual void
        on(PeerID node, tp when, Receive<Ledger> const&) = 0;

        virtual void
        on(PeerID node, tp when, Receive<Proposal> const&) = 0;

        virtual void
        on(PeerID node, tp when, Relay<Tx> const&) = 0;

        virtual void
        on(PeerID node, tp when, Relay<TxSet> const&) = 0;

        virtual void
        on(PeerID node, tp when, Relay<Validation> const&) = 0;

        virtual void
        on(PeerID node, tp when, Relay<Ledger> const&) = 0;

        virtual void
        on(PeerID node, tp when, Relay<Proposal> const&) = 0;

        virtual void
        on(PeerID node, tp when, SubmitTx const&) = 0;

        virtual void
        on(PeerID node, tp when, StartRound const&) = 0;

        virtual void
        on(PeerID node, tp when, CloseLedger const&) = 0;

        virtual void
        on(PeerID node, tp when, AcceptLedger const&) = 0;

        virtual void
        on(PeerID node, tp when, WrongPrevLedger const&) = 0;

        virtual void
        on(PeerID node, tp when, FullyValidateLedger const&) = 0;
    };

    // Bridge between type-ful collector T and type erased instance
    template <class T>
    class Any final : public ICollector
    {
        T & t_;

    public:
        Any(T & t) : t_{t}
        {
        }

        // Can't copy
        Any(Any const & ) = delete;
        Any& operator=(Any const & ) = delete;

        Any(Any && ) = default;
        Any& operator=(Any && ) = default;

        virtual void
        on(PeerID node, tp when, Share<Tx> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Share<TxSet> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Share<Validation> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Share<Ledger> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Share<Proposal> const& e) override
        {
            t_.on(node, when, e);
        }

        void
        on(PeerID node, tp when, Receive<Tx> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Receive<TxSet> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Receive<Validation> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Receive<Ledger> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Receive<Proposal> const& e) override
        {
            t_.on(node, when, e);
        }

        void
        on(PeerID node, tp when, Relay<Tx> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Relay<TxSet> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Relay<Validation> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Relay<Ledger> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, Relay<Proposal> const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, SubmitTx const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, StartRound const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, CloseLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, AcceptLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, WrongPrevLedger const& e) override
        {
            t_.on(node, when, e);
        }

        virtual void
        on(PeerID node, tp when, FullyValidateLedger const& e) override
        {
            t_.on(node, when, e);
        }
    };

    std::unique_ptr<ICollector> impl_;

public:
    template <class T>
    CollectorRef(T& t) : impl_{new Any<T>(t)}
    {
    }

    // Non-copyable
    CollectorRef(CollectorRef const& c) = delete;
    CollectorRef& operator=(CollectorRef& c) = delete;

    CollectorRef(CollectorRef&&) = default;
    CollectorRef& operator=(CollectorRef&&) = default;

    template <class E>
    void
    on(PeerID node, tp when, E const& e)
    {
        impl_->on(node, when, e);
    }
};

/** A container of CollectorRefs

    A set of CollectorRef instances that process the same events. An event is
    processed by collectors in the order the collectors were added.

    This class type-erases the collector instances. By contract, the
    Collectors/collectors class/helper in collectors.h are not type erased and
    offer an opportunity for type transformations and combinations with
    improved compiler optimizations.
*/
class CollectorRefs
{
    std::vector<CollectorRef> collectors_;
public:

    template <class Collector>
    void add(Collector & collector)
    {
        collectors_.emplace_back(collector);
    }

    template <class E>
    void
    on(PeerID node, SimTime when, E const& e)
    {
        for (auto & c : collectors_)
        {
            c.on(node, when, e);
        }
    }

};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
