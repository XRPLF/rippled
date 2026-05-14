/** @file
 *  Composable peer-selection predicates and message-dispatch functors for
 *  use with `Overlay::foreach()`.
 *
 *  Two orthogonal concerns are separated here:
 *  - **Dispatch functors** (`SendAlways`, `SendIfPred`, `SendIfNotPred`) own a
 *    `Message` reference and implement `operator()(shared_ptr<Peer>)`. They
 *    are passed directly to `Overlay::foreach()`.
 *  - **Selection predicates** (`MatchPeer`, `PeerInCluster`, `PeerInSet`) own
 *    peer-matching logic and return `bool`. They are composed with the dispatch
 *    functors via `sendIf()` and `sendIfNot()`.
 *
 *  All types hold references (not copies) to their `Message` and `Predicate`
 *  arguments. They are intended as short-lived stack temporaries whose
 *  lifetime does not exceed the `foreach` call that consumes them.
 */

#pragma once

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>

#include <set>

namespace xrpl {

/** Dispatch functor that unconditionally sends a message to every peer.
 *
 *  Holds a `const&` to the caller's `shared_ptr<Message>` to avoid copying
 *  the shared pointer on each peer visit. The caller must keep the
 *  `shared_ptr` alive for the duration of the `Overlay::foreach` call.
 *
 *  @see sendIf, sendIfNot
 */
struct SendAlways
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;

    /** Construct with the message to broadcast.
     *
     *  @param m Reference to the shared message to send; must outlive this
     *      functor.
     */
    SendAlways(std::shared_ptr<Message> const& m) : msg(m)
    {
    }

    /** Send @ref msg to @a peer unconditionally.
     *
     *  @param peer The peer to deliver the message to.
     */
    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        peer->send(msg);
    }
};

//------------------------------------------------------------------------------

/** Dispatch functor that sends a message only to peers that satisfy a
 *  predicate.
 *
 *  Holds `const&` to both the `shared_ptr<Message>` and the predicate; both
 *  must outlive this functor. Use the `sendIf()` factory to construct without
 *  spelling out the template argument.
 *
 *  @tparam Predicate A callable with signature `bool(shared_ptr<Peer> const&)`.
 *  @see sendIf, SendIfNotPred
 */
template <typename Predicate>
struct SendIfPred
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;
    Predicate const& predicate;

    /** Construct with the message and selection predicate.
     *
     *  @param m Reference to the shared message to send; must outlive this
     *      functor.
     *  @param p Reference to the predicate; must outlive this functor.
     */
    SendIfPred(std::shared_ptr<Message> const& m, Predicate const& p) : msg(m), predicate(p)
    {
    }

    /** Send @ref msg to @a peer if @ref predicate returns `true` for it.
     *
     *  @param peer The peer being evaluated.
     */
    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (predicate(peer))
            peer->send(msg);
    }
};

/** Construct a `SendIfPred` with deduced predicate type.
 *
 *  Preferred over direct `SendIfPred<P>` construction; spares callers from
 *  spelling out the template argument.
 *
 *  @tparam Predicate Deduced callable type with signature
 *      `bool(shared_ptr<Peer> const&)`.
 *  @param m  Message to send; `shared_ptr` must outlive the returned functor.
 *  @param f  Predicate to evaluate per peer; must outlive the returned functor.
 *  @return A `SendIfPred<Predicate>` ready to pass to `Overlay::foreach()`.
 */
template <typename Predicate>
SendIfPred<Predicate>
sendIf(std::shared_ptr<Message> const& m, Predicate const& f)
{
    return SendIfPred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Dispatch functor that sends a message to every peer that does NOT satisfy
 *  a predicate.
 *
 *  The logical complement of `SendIfPred`: useful for excluding a known subset
 *  (e.g., the message originator) while broadcasting to all others. Holds
 *  `const&` to both the `shared_ptr<Message>` and the predicate; both must
 *  outlive this functor. Use the `sendIfNot()` factory to construct without
 *  spelling out the template argument.
 *
 *  @tparam Predicate A callable with signature `bool(shared_ptr<Peer> const&)`.
 *  @see sendIfNot, SendIfPred
 */
template <typename Predicate>
struct SendIfNotPred
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;
    Predicate const& predicate;

    /** Construct with the message and exclusion predicate.
     *
     *  @param m Reference to the shared message to send; must outlive this
     *      functor.
     *  @param p Reference to the predicate; must outlive this functor.
     */
    SendIfNotPred(std::shared_ptr<Message> const& m, Predicate const& p) : msg(m), predicate(p)
    {
    }

    /** Send @ref msg to @a peer if @ref predicate returns `false` for it.
     *
     *  @param peer The peer being evaluated.
     */
    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (!predicate(peer))
            peer->send(msg);
    }
};

/** Construct a `SendIfNotPred` with deduced predicate type.
 *
 *  Preferred over direct `SendIfNotPred<P>` construction; spares callers from
 *  spelling out the template argument.
 *
 *  @tparam Predicate Deduced callable type with signature
 *      `bool(shared_ptr<Peer> const&)`.
 *  @param m  Message to send; `shared_ptr` must outlive the returned functor.
 *  @param f  Exclusion predicate; must outlive the returned functor.
 *  @return A `SendIfNotPred<Predicate>` ready to pass to `Overlay::foreach()`.
 */
template <typename Predicate>
SendIfNotPred<Predicate>
sendIfNot(std::shared_ptr<Message> const& m, Predicate const& f)
{
    return SendIfNotPred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Selection predicate that matches a single peer by raw pointer identity.
 *
 *  Returns `true` only for the peer whose address equals `matchPeer`. A
 *  default-constructed (or `nullptr`-constructed) `MatchPeer` never matches
 *  any peer, making it a safe no-op sentinel. This null-safe behavior is
 *  exploited by `PeerInCluster`, which embeds a `MatchPeer` to optionally
 *  skip one peer without requiring separate exclusion logic at the call site.
 *
 *  @note Identity is compared via `peer.get() == matchPeer` (address equality),
 *      not by peer ID or public key.
 */
struct MatchPeer
{
    Peer const* matchPeer;

    /** Construct with the peer to match, or `nullptr` to match nothing.
     *
     *  @param match Raw pointer to the peer to identify; `nullptr` produces a
     *      no-op predicate that never returns `true`.
     */
    MatchPeer(Peer const* match = nullptr) : matchPeer(match)
    {
    }

    /** Return `true` if @a peer is the tracked peer.
     *
     *  @param peer The peer being evaluated.
     *  @return `true` iff `matchPeer != nullptr && peer.get() == matchPeer`.
     */
    bool
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        return (matchPeer != nullptr) && (peer.get() == matchPeer);
    }
};

//------------------------------------------------------------------------------

/** Selection predicate that matches all cluster peers, with optional exclusion
 *  of one peer (typically the message originator).
 *
 *  Composes a `MatchPeer` skip guard with a `Peer::cluster()` membership check.
 *  Passing `nullptr` (the default) as the skip peer makes the predicate select
 *  every cluster peer with no exclusions.
 *
 *  Typical usage — broadcast a `mtCLUSTER` message to all cluster peers:
 *  @code
 *  overlay.foreach(sendIf(msg, PeerInCluster()));
 *  @endcode
 */
struct PeerInCluster
{
    MatchPeer skipPeer;

    /** Construct with an optional peer to exclude from selection.
     *
     *  @param skip Raw pointer to a peer to skip (e.g., the originator of a
     *      relayed message), or `nullptr` to skip nobody.
     */
    PeerInCluster(Peer const* skip = nullptr) : skipPeer(skip)
    {
    }

    /** Return `true` if @a peer is a cluster member and is not the skip peer.
     *
     *  @param peer The peer being evaluated.
     *  @return `true` iff the peer is not the skipped peer and `peer->cluster()`
     *      returns `true`.
     */
    bool
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (skipPeer(peer))
            return false;

        if (!peer->cluster())
            return false;

        return true;
    }
};

//------------------------------------------------------------------------------

/** Selection predicate that matches peers whose ID is present in a given set.
 *
 *  Provides O(log n) membership tests per peer visit against a caller-owned
 *  `std::set<Peer::id_t>`. The set is held by `const&`; the caller must keep it
 *  alive for the duration of the `Overlay::foreach` call. This predicate is
 *  used by `PeerSet`-based data-acquisition workflows to target exactly the
 *  subset of peers from which data was requested.
 *
 *  @note Holds a reference to the set, not a copy. The set must outlive this
 *      predicate.
 */
struct PeerInSet
{
    std::set<Peer::id_t> const& peerSet;

    /** Construct with the set of peer IDs to match against.
     *
     *  @param peers Reference to the set of target peer IDs; must outlive this
     *      predicate.
     */
    PeerInSet(std::set<Peer::id_t> const& peers) : peerSet(peers)
    {
    }

    /** Return `true` if @a peer's ID is present in @ref peerSet.
     *
     *  @param peer The peer being evaluated.
     *  @return `true` iff `peerSet.contains(peer->id())`.
     */
    bool
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        return peerSet.contains(peer->id());
    }
};

}  // namespace xrpl
