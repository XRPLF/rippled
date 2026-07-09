#pragma once

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>

#include <memory>
#include <set>

namespace xrpl {

/** Sends a message to all peers */
struct SendAlways
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;

    SendAlways(std::shared_ptr<Message> const& m) : msg(m)
    {
    }

    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        peer->send(msg);
    }
};

//------------------------------------------------------------------------------

/** Sends a message to match peers */
template <typename Predicate>
struct SendIfPred
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;
    Predicate const& predicate;

    SendIfPred(std::shared_ptr<Message> const& m, Predicate const& p) : msg(m), predicate(p)
    {
    }

    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (predicate(peer))
            peer->send(msg);
    }
};

/** Helper function to aid in type deduction */
template <typename Predicate>
SendIfPred<Predicate>
sendIf(std::shared_ptr<Message> const& m, Predicate const& f)
{
    return SendIfPred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Sends a message to non-matching peers */
template <typename Predicate>
struct SendIfNotPred
{
    using return_type = void;

    std::shared_ptr<Message> const& msg;
    Predicate const& predicate;

    SendIfNotPred(std::shared_ptr<Message> const& m, Predicate const& p) : msg(m), predicate(p)
    {
    }

    void
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        if (!predicate(peer))
            peer->send(msg);
    }
};

/** Helper function to aid in type deduction */
template <typename Predicate>
SendIfNotPred<Predicate>
sendIfNot(std::shared_ptr<Message> const& m, Predicate const& f)
{
    return SendIfNotPred<Predicate>(m, f);
}

//------------------------------------------------------------------------------

/** Select the specific peer */
struct MatchPeer
{
    Peer const* matchPeer;

    MatchPeer(Peer const* match = nullptr) : matchPeer(match)
    {
    }

    bool
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        return (matchPeer != nullptr) && (peer.get() == matchPeer);
    }
};

//------------------------------------------------------------------------------

/** Select all peers (except optional excluded) that are in our cluster */
struct PeerInCluster
{
    MatchPeer skipPeer;

    PeerInCluster(Peer const* skip = nullptr) : skipPeer(skip)
    {
    }

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

/** Select all peers that are in the specified set */
struct PeerInSet
{
    std::set<Peer::id_t> const& peerSet;

    PeerInSet(std::set<Peer::id_t> const& peers) : peerSet(peers)
    {
    }

    bool
    operator()(std::shared_ptr<Peer> const& peer) const
    {
        return peerSet.contains(peer->id());
    }
};

}  // namespace xrpl
