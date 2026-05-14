/** @file
 *  Endpoint distribution logic for PeerFinder peer exchange.
 *
 *  Provides `handout()` and three target types — `RedirectHandouts`,
 *  `SlotHandouts`, and `ConnectHandouts` — used by `Logic` to distribute
 *  peer addresses fairly across recipients while enforcing hop limits,
 *  per-slot deduplication, and reconnect-rate squelching.
 */

#pragma once

#include <xrpld/peerfinder/detail/SlotImp.h>
#include <xrpld/peerfinder/detail/Tuning.h>

#include <xrpl/beast/container/aged_set.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <utility>

namespace xrpl::PeerFinder {

namespace detail {

/** Attempt to give one endpoint from a hop container to a single target.
 *
 *  Walks `h` from front to back until `t.tryInsert()` accepts an entry.
 *  On acceptance the entry is rotated to the tail of `h` via `moveBack()`,
 *  providing a weak round-robin so the same endpoint is not handed to
 *  every target in a single pass before others get a turn.
 *
 *  @tparam Target      Duck-typed target with `full() -> bool` and
 *      `tryInsert(Endpoint const&) -> bool`.
 *  @tparam HopContainer  Sequence container whose iterators support
 *      `begin()`, `end()`, and `moveBack(it)`.
 *  @param t  The target to receive the endpoint.
 *  @param h  The hop-level container to draw from.
 *  @return 1 if an endpoint was inserted, 0 if no suitable endpoint was found.
 *  @pre `!t.full()` — the caller must check before invoking.
 *  @note A future optimization could use `splice` for `std::list` containers
 *      instead of erase + push_back.
 */
// VFALCO TODO specialization that handles std::list for SequenceContainer
//             using splice for optimization over erase/push_back
//
template <class Target, class HopContainer>
std::size_t
handoutOne(Target& t, HopContainer& h)
{
    XRPL_ASSERT(!t.full(), "xrpl::PeerFinder::detail::handoutOne : target is not full");
    for (auto it = h.begin(); it != h.end(); ++it)
    {
        auto const& e = *it;
        if (t.tryInsert(e))
        {
            h.moveBack(it);
            return 1;
        }
    }
    return 0;
}

}  // namespace detail

/** Distribute endpoints from a sequence of hop containers to a set of targets.
 *
 *  Drives the multi-target distribution loop: for each pass it iterates every
 *  hop container in `[seqFirst, seqLast)` and offers one endpoint to each
 *  non-full target via `handoutOne`. The outer loop repeats until all targets
 *  are full or a complete pass yields no insertions, whichever comes first.
 *  An `allFull` short-circuit exits immediately when no target has remaining
 *  capacity, avoiding a full scan of remaining hop containers.
 *
 *  This structure ensures endpoints are distributed as evenly as possible
 *  across all recipients and hop levels before any recipient receives a
 *  second item from the same source.
 *
 *  @tparam TargetFwdIter  Forward iterator whose value type satisfies the
 *      `full() -> bool` / `tryInsert(Endpoint const&) -> bool` interface.
 *  @tparam SeqFwdIter  Forward iterator over hop containers whose value
 *      type satisfies the `handoutOne` `HopContainer` concept.
 *  @param first    Beginning of the target range.
 *  @param last     End of the target range.
 *  @param seqFirst Beginning of the hop-container sequence (one entry per
 *      hop level in the Livecache).
 *  @param seqLast  End of the hop-container sequence.
 */
template <class TargetFwdIter, class SeqFwdIter>
void
handout(TargetFwdIter first, TargetFwdIter last, SeqFwdIter seqFirst, SeqFwdIter seqLast)
{
    for (;;)
    {
        std::size_t n(0);
        for (auto si = seqFirst; si != seqLast; ++si)
        {
            auto c = *si;
            bool allFull(true);
            for (auto ti = first; ti != last; ++ti)
            {
                auto& t = *ti;
                if (!t.full())
                {
                    n += detail::handoutOne(t, c);
                    allFull = false;
                }
            }
            if (allFull)
                return;
        }
        if (!n)
            break;
    }
}

//------------------------------------------------------------------------------

/** Collects redirect endpoints to return to a peer that cannot be accepted.
 *
 *  Used when a new inbound connection must be turned away because the node
 *  has no free slots. Gathers up to `Tuning::kREDIRECT_ENDPOINT_COUNT` (10)
 *  alternative addresses to include in the TMEndpoints redirect response so
 *  the rejected peer can try elsewhere.
 *
 *  `tryInsert()` enforces: hop limit ≤ `kMAX_HOPS`, rejects hops-0 (our own
 *  address), rejects the connecting peer's own IP, and deduplicates by address
 *  ignoring port (port dedup prevents an adversary from flooding the list with
 *  the same host on different ports).
 */
class RedirectHandouts
{
public:
    /** Construct with the slot of the connecting peer being redirected.
     *
     *  @param slot  The inbound slot for the peer that will receive the redirect.
     *      Its remote address is used to filter out the peer's own address.
     */
    template <class = void>
    explicit RedirectHandouts(SlotImp::ptr slot);

    /** Attempt to add an endpoint to the redirect list.
     *
     *  Rejects the endpoint if the list is full, the hop count exceeds
     *  `Tuning::kMAX_HOPS`, the hop count is 0 (our own address), the
     *  address belongs to the connecting peer, or the address (ignoring
     *  port) is already in the list.
     *
     *  @param ep  The candidate endpoint.
     *  @return `true` if the endpoint was accepted and added, `false` otherwise.
     */
    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    /** Return `true` when the redirect list has reached `kREDIRECT_ENDPOINT_COUNT`. */
    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= Tuning::kREDIRECT_ENDPOINT_COUNT;
    }

    /** Return the slot of the peer being redirected. */
    [[nodiscard]] SlotImp::ptr const&
    slot() const
    {
        return slot_;
    }

    /** Return a mutable reference to the collected redirect endpoints. */
    std::vector<Endpoint>&
    list()
    {
        return list_;
    }

    /** Return a read-only reference to the collected redirect endpoints. */
    [[nodiscard]] std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
RedirectHandouts::RedirectHandouts(SlotImp::ptr slot) : slot_(std::move(slot))
{
    list_.reserve(Tuning::kREDIRECT_ENDPOINT_COUNT);
}

template <class>
bool
RedirectHandouts::tryInsert(Endpoint const& ep)
{
    if (full())
        return false;

    // VFALCO NOTE This check can be removed when we provide the
    //             addresses in a peer HTTP handshake instead of
    //             the tmENDPOINTS message.
    //
    if (ep.hops > Tuning::kMAX_HOPS)
        return false;

    // Don't send them our address
    if (ep.hops == 0)
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&ep](Endpoint const& other) {
            // Ignore port for security reasons
            return other.address.address() == ep.address.address();
        }))
    {
        return false;
    }

    list_.emplace_back(ep.address, ep.hops);

    return true;
}

//------------------------------------------------------------------------------

/** Collects endpoints for the periodic TMEndpoints gossip broadcast to a peer.
 *
 *  Accumulates up to `Tuning::kNUMBER_OF_ENDPOINTS` (12) entries for one
 *  outbound TMEndpoints message. Beyond the standard filters (hop limit,
 *  own-address exclusion, port-agnostic address dedup), `tryInsert()` consults
 *  `slot_->recent.filter()` to avoid re-sending anything recently exchanged
 *  with this peer. On acceptance it also calls `slot_->recent.insert()` —
 *  a pessimistic update that prevents the same endpoint from being re-sent
 *  on the next broadcast cycle before the far end's cache has expired.
 *
 *  @note `Logic::buildEndpointsForPeers` creates one `SlotHandouts` per active
 *      peer, then calls `handout()` across the full set — this is the primary
 *      multi-target use of `handout()` where its fairness properties matter
 *      most, preventing early slots from monopolising the best endpoints.
 */
class SlotHandouts
{
public:
    /** Construct a handout collector for the given connected peer slot.
     *
     *  @param slot  The active peer slot that will receive the endpoint list.
     */
    template <class = void>
    explicit SlotHandouts(SlotImp::ptr slot);

    /** Attempt to add an endpoint to the broadcast list.
     *
     *  Applies hop-limit, recent-cache, own-address, and port-agnostic
     *  address-dedup filters. On success, records the endpoint in
     *  `slot_->recent` to suppress duplicate sends for the cache's lifetime.
     *
     *  @param ep  The candidate endpoint.
     *  @return `true` if the endpoint was accepted and added, `false` otherwise.
     */
    template <class = void>
    bool
    tryInsert(Endpoint const& ep);

    /** Return `true` when the list has reached `kNUMBER_OF_ENDPOINTS`. */
    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= Tuning::kNUMBER_OF_ENDPOINTS;
    }

    /** Append an endpoint unconditionally (used to inject the self-advertisement entry). */
    void
    insert(Endpoint const& ep)
    {
        list_.push_back(ep);
    }

    /** Return the slot that will receive this broadcast. */
    [[nodiscard]] SlotImp::ptr const&
    slot() const
    {
        return slot_;
    }

    /** Return the collected endpoint list for transmission. */
    [[nodiscard]] std::vector<Endpoint> const&
    list() const
    {
        return list_;
    }

private:
    SlotImp::ptr slot_;
    std::vector<Endpoint> list_;
};

template <class>
SlotHandouts::SlotHandouts(SlotImp::ptr slot) : slot_(std::move(slot))
{
    list_.reserve(Tuning::kNUMBER_OF_ENDPOINTS);
}

template <class>
bool
SlotHandouts::tryInsert(Endpoint const& ep)
{
    if (full())
        return false;

    if (ep.hops > Tuning::kMAX_HOPS)
        return false;

    if (slot_->recent.filter(ep.address, ep.hops))
        return false;

    // Don't send them their own address
    if (slot_->remoteEndpoint().address() == ep.address.address())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&ep](Endpoint const& other) {
            // Ignore port for security reasons
            return other.address.address() == ep.address.address();
        }))
        return false;

    list_.emplace_back(ep.address, ep.hops);

    // Insert into this slot's recent table. Although the endpoint
    // didn't come from the slot, adding it to the slot's table
    // prevents us from sending it again until it has expired from
    // the other end's cache.
    //
    slot_->recent.insert(ep.address, ep.hops);

    return true;
}

//------------------------------------------------------------------------------

/** Collects candidate addresses for the node to actively dial.
 *
 *  Combines filtering and squelch-set management in a single object.
 *  `tryInsert()` atomically checks and writes the shared `Squelches` set:
 *  addresses already present are silently skipped; new addresses are added
 *  to both the squelch set and the output list. Because `squelches_` is
 *  taken by reference, squelches from prior connection rounds persist across
 *  calls — recently-attempted addresses age out after
 *  `Tuning::kRECENT_ATTEMPT_DURATION` (60 s) with no extra bookkeeping in
 *  the caller.
 *
 *  `Logic::autoconnect` feeds both the Livecache (reverse hop order, to
 *  prefer topologically distant nodes) and the Bootcache as a fallback.
 */
class ConnectHandouts
{
public:
    /** Time-bounded set of addresses recently attempted, shared across calls.
     *
     *  An `aged_set` keyed by `beast::IP::Address`; entries expire after
     *  `Tuning::kRECENT_ATTEMPT_DURATION` (60 s).  Held by reference so that
     *  retry suppression persists across multiple `ConnectHandouts` instances
     *  within the same `Logic` lifetime.
     */
    using Squelches = beast::aged_set<beast::IP::Address>;

    /** The output container type for candidate outbound addresses. */
    using list_type = std::vector<beast::IP::Endpoint>;

private:
    std::size_t needed_;
    Squelches& squelches_;
    list_type list_;

public:
    /** Construct with the number of connections needed and the shared squelch set.
     *
     *  @param needed    Number of outbound addresses to collect.
     *  @param squelches Reference to the `Logic`-owned aged set used to
     *      suppress rapid reconnects to the same address.
     */
    template <class = void>
    ConnectHandouts(std::size_t needed, Squelches& squelches);

    /** Attempt to add a raw endpoint to the candidate list.
     *
     *  Rejects the endpoint if the list is full, the address (ignoring port)
     *  is already in the list, or the address is present in `squelches_`.
     *  On acceptance the address is inserted into `squelches_` so subsequent
     *  calls (in this or future rounds) will skip it until it ages out.
     *
     *  @param endpoint  The candidate outbound address.
     *  @return `true` if the endpoint was accepted, `false` otherwise.
     */
    template <class = void>
    bool
    tryInsert(beast::IP::Endpoint const& endpoint);

    /** Return `true` when no candidate addresses have been collected yet. */
    [[nodiscard]] bool
    empty() const
    {
        return list_.empty();
    }

    /** Return `true` when the required number of addresses has been collected. */
    [[nodiscard]] bool
    full() const
    {
        return list_.size() >= needed_;
    }

    /** Overload accepting a PeerFinder `Endpoint`; delegates to the `beast::IP::Endpoint` overload. */
    bool
    tryInsert(Endpoint const& endpoint)
    {
        return tryInsert(endpoint.address);
    }

    /** Return a mutable reference to the collected candidate addresses. */
    list_type&
    list()
    {
        return list_;
    }

    /** Return a read-only reference to the collected candidate addresses. */
    [[nodiscard]] list_type const&
    list() const
    {
        return list_;
    }
};

template <class>
ConnectHandouts::ConnectHandouts(std::size_t needed, Squelches& squelches)
    : needed_(needed), squelches_(squelches)
{
    list_.reserve(needed);
}

template <class>
bool
ConnectHandouts::tryInsert(beast::IP::Endpoint const& endpoint)
{
    if (full())
        return false;

    // Make sure the address isn't already in our list
    if (std::any_of(list_.begin(), list_.end(), [&endpoint](beast::IP::Endpoint const& other) {
            // Ignore port for security reasons
            return other.address() == endpoint.address();
        }))
    {
        return false;
    }

    // Add to squelch list so we don't try it too often.
    // If its already there, then make try_insert fail.
    auto const result(squelches_.insert(endpoint.address()));
    if (!result.second)
        return false;

    list_.push_back(endpoint);

    return true;
}

}  // namespace xrpl::PeerFinder
