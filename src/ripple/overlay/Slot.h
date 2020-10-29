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

#ifndef RIPPLE_OVERLAY_SLOT_H_INCLUDED
#define RIPPLE_OVERLAY_SLOT_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/chrono.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/ReduceRelayCommon.h>
#include <ripple/overlay/Squelch.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple.pb.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace ripple {

namespace reduce_relay {

template <typename clock_type>
class Slots;

/** Peer's State */
enum class PeerState : uint8_t {
    Counting,   // counting messages
    Selected,   // selected to relay, counting if Slot in Counting
    Squelched,  // squelched, doesn't relay
};
/** Slot's State */
enum class SlotState : uint8_t {
    Counting,  // counting messages
    Selected,  // peers selected, stop counting
};

template <typename Unit, typename TP>
Unit
epoch(TP const& t)
{
    return std::chrono::duration_cast<Unit>(t.time_since_epoch());
}

/** Abstract class. Declares squelch and unsquelch handlers.
 * OverlayImpl inherits from this class. Motivation is
 * for easier unit tests to facilitate on the fly
 * changing callbacks. */
class SquelchHandler
{
public:
    virtual ~SquelchHandler()
    {
    }
    /** Squelch handler
     * @param validator Public key of the source validator
     * @param id Peer's id to squelch
     * @param duration Squelch duration in seconds
     */
    virtual void
    squelch(PublicKey const& validator, Peer::id_t id, std::uint32_t duration)
        const = 0;
    /** Unsquelch handler
     * @param validator Public key of the source validator
     * @param id Peer's id to unsquelch
     */
    virtual void
    unsquelch(PublicKey const& validator, Peer::id_t id) const = 0;
};

/**
 * Slot is associated with a specific validator via validator's public key.
 * Slot counts messages from a validator, selects peers to be the source
 * of the messages, and communicates the peers to be squelched. Slot can be
 * in the following states: 1) Counting. This is the peer selection state
 * when Slot counts the messages and selects the peers; 2) Selected. Slot
 * doesn't count messages in Selected state. A message received from
 * unsquelched, disconnected peer, or idling peer may transition Slot to
 * Counting state.
 */
template <typename clock_type>
class Slot final
{
private:
    friend class Slots<clock_type>;
    using id_t = Peer::id_t;
    using time_point = typename clock_type::time_point;

    /** Constructor
     * @param journal Journal for logging
     * @param handler Squelch/Unsquelch implementation
     */
    Slot(SquelchHandler const& handler, beast::Journal journal)
        : reachedThreshold_(0)
        , lastSelected_(clock_type::now())
        , state_(SlotState::Counting)
        , handler_(handler)
        , journal_(journal)
    {
    }

    /** Update peer info. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If time of last
     * selection round is > 2 * MAX_UNSQUELCH_EXPIRE then switch the slot's
     * state to Counting. If the number of messages for the peer
     * is > MIN_MESSAGE_THRESHOLD then add peer to considered peers pool.
     * If the number of considered peers who reached MAX_MESSAGE_THRESHOLD is
     * MAX_SELECTED_PEERS then randomly select MAX_SELECTED_PEERS from
     * considered peers, and call squelch handler for each peer, which is not
     * selected and not already in Squelched state. Set the state for those
     * peers to Squelched and reset the count of all peers. Set slot's state to
     * Selected. Message count is not updated when the slot is in Selected
     * state.
     * @param validator Public key of the source validator
     * @param id Peer id which received the message
     * @param type  Message type (Validation and Propose Set only,
     *     others are ignored, future use)
     */
    void
    update(PublicKey const& validator, id_t id, protocol::MessageType type);

    /** Handle peer deletion when a peer disconnects.
     * If the peer is in Selected state then
     * call unsquelch handler for every peer in squelched state and reset
     * every peer's state to Counting. Switch Slot's state to Counting.
     * @param validator Public key of the source validator
     * @param id Deleted peer id
     * @param erase If true then erase the peer. The peer is not erased
     *      when the peer when is idled. The peer is deleted when it
     *      disconnects
     */
    void
    deletePeer(PublicKey const& validator, id_t id, bool erase);

    /** Get the time of the last peer selection round */
    const time_point&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Return number of peers in state */
    std::uint16_t
    inState(PeerState state) const;

    /** Return number of peers not in state */
    std::uint16_t
    notInState(PeerState state) const;

    /** Return Slot's state */
    SlotState
    getState() const
    {
        return state_;
    }

    /** Return selected peers */
    std::set<id_t>
    getSelected() const;

    /** Get peers info. Return map of peer's state, count, squelch
     * expiration milsec, and last message time milsec.
     */
    std::
        unordered_map<id_t, std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
        getPeers() const;

    /** Check if peers stopped relaying messages. If a peer is
     * selected peer then call unsquelch handler for all
     * currently squelched peers and switch the slot to
     * Counting state.
     * @param validator Public key of the source validator
     */
    void
    deleteIdlePeer(PublicKey const& validator);

    /** Get random squelch duration between MIN_UNSQUELCH_EXPIRE and
     * max(MAX_UNSQUELCH_EXPIRE, UNSQUELCH_EXPIRE_MULTIPLIER * npeers)
     * @param npeers number of peers that can be squelched in the Slot
     * MAX_UNSQUELCH_EXPIRE */
    std::chrono::seconds
    getSquelchDuration(std::size_t npeers);

private:
    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts();

    /** Initialize slot to Counting state */
    void
    initCounting();

    /** Data maintained for each peer */
    struct PeerInfo
    {
        PeerState state;         // peer's state
        std::size_t count;       // message count
        time_point expire;       // squelch expiration time
        time_point lastMessage;  // time last message received
    };
    std::unordered_map<id_t, PeerInfo> peers_;  // peer's data
    // pool of peers considered as the source of messages
    // from validator - peers that reached MIN_MESSAGE_THRESHOLD
    std::unordered_set<id_t> considered_;
    // number of peers that reached MAX_MESSAGE_THRESHOLD
    std::uint16_t reachedThreshold_;
    // last time peers were selected, used to age the slot
    typename clock_type::time_point lastSelected_;
    SlotState state_;                // slot's state
    SquelchHandler const& handler_;  // squelch/unsquelch handler
    beast::Journal const journal_;   // logging
};

template <typename clock_type>
void
Slot<clock_type>::deleteIdlePeer(PublicKey const& validator)
{
    using namespace std::chrono;
    auto now = clock_type::now();
    for (auto it = peers_.begin(); it != peers_.end();)
    {
        auto& peer = it->second;
        auto id = it->first;
        ++it;
        if (now - peer.lastMessage > IDLED)
        {
            JLOG(journal_.debug())
                << "deleteIdlePeer: " << Slice(validator) << " " << id
                << " idled "
                << duration_cast<seconds>(now - peer.lastMessage).count()
                << " selected " << (peer.state == PeerState::Selected);
            deletePeer(validator, id, false);
        }
    }
}

template <typename clock_type>
void
Slot<clock_type>::update(
    PublicKey const& validator,
    id_t id,
    protocol::MessageType type)
{
    using namespace std::chrono;
    auto now = clock_type::now();
    auto it = peers_.find(id);
    // First message from this peer
    if (it == peers_.end())
    {
        JLOG(journal_.debug())
            << "update: adding peer " << Slice(validator) << " " << id;
        peers_.emplace(
            std::make_pair(id, PeerInfo{PeerState::Counting, 0, now, now}));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    if (it->second.state == PeerState::Squelched && now > it->second.expire)
    {
        JLOG(journal_.debug())
            << "update: squelch expired " << Slice(validator) << " " << id;
        it->second.state = PeerState::Counting;
        it->second.lastMessage = now;
        initCounting();
        return;
    }

    auto& peer = it->second;

    JLOG(journal_.debug())
        << "update: existing peer " << Slice(validator) << " " << id
        << " slot state " << static_cast<int>(state_) << " peer state "
        << static_cast<int>(peer.state) << " count " << peer.count << " last "
        << duration_cast<milliseconds>(now - peer.lastMessage).count()
        << " pool " << considered_.size() << " threshold " << reachedThreshold_
        << " " << (type == protocol::mtVALIDATION ? "validation" : "proposal");

    peer.lastMessage = now;

    if (state_ != SlotState::Counting || peer.state == PeerState::Squelched)
        return;

    if (++peer.count > MIN_MESSAGE_THRESHOLD)
        considered_.insert(id);
    if (peer.count == (MAX_MESSAGE_THRESHOLD + 1))
        ++reachedThreshold_;

    if (now - lastSelected_ > 2 * MAX_UNSQUELCH_EXPIRE)
    {
        JLOG(journal_.debug())
            << "update: resetting due to inactivity " << Slice(validator) << " "
            << id << " " << duration_cast<seconds>(now - lastSelected_).count();
        initCounting();
        return;
    }

    if (reachedThreshold_ == MAX_SELECTED_PEERS)
    {
        // Randomly select MAX_SELECTED_PEERS peers from considered.
        // Exclude peers that have been idling > IDLED -
        // it's possible that deleteIdlePeer() has not been called yet.
        // If number of remaining peers != MAX_SELECTED_PEERS
        // then reset the Counting state and let deleteIdlePeer() handle
        // idled peers.
        std::unordered_set<id_t> selected;
        auto const consideredPoolSize = considered_.size();
        while (selected.size() != MAX_SELECTED_PEERS && considered_.size() != 0)
        {
            auto i =
                considered_.size() == 1 ? 0 : rand_int(considered_.size() - 1);
            auto it = std::next(considered_.begin(), i);
            auto id = *it;
            considered_.erase(it);
            auto const& itpeers = peers_.find(id);
            if (itpeers == peers_.end())
            {
                JLOG(journal_.error()) << "update: peer not found "
                                       << Slice(validator) << " " << id;
                continue;
            }
            if (now - itpeers->second.lastMessage < IDLED)
                selected.insert(id);
        }

        if (selected.size() != MAX_SELECTED_PEERS)
        {
            JLOG(journal_.debug())
                << "update: selection failed " << Slice(validator) << " " << id;
            initCounting();
            return;
        }

        lastSelected_ = now;

        auto s = selected.begin();
        JLOG(journal_.debug())
            << "update: " << Slice(validator) << " " << id << " pool size "
            << consideredPoolSize << " selected " << *s << " "
            << *std::next(s, 1) << " " << *std::next(s, 2);

        // squelch peers which are not selected and
        // not already squelched
        std::stringstream str;
        for (auto& [k, v] : peers_)
        {
            v.count = 0;

            if (selected.find(k) != selected.end())
                v.state = PeerState::Selected;
            else if (v.state != PeerState::Squelched)
            {
                if (journal_.debug())
                    str << k << " ";
                v.state = PeerState::Squelched;
                seconds duration =
                    getSquelchDuration(peers_.size() - MAX_SELECTED_PEERS);
                v.expire = now + duration;
                handler_.squelch(
                    validator, k, duration_cast<seconds>(duration).count());
            }
        }
        JLOG(journal_.debug()) << "update: squelching " << Slice(validator)
                               << " " << id << " " << str.str();
        considered_.clear();
        reachedThreshold_ = 0;
        state_ = SlotState::Selected;
    }
}

template <typename clock_type>
std::chrono::seconds
Slot<clock_type>::getSquelchDuration(std::size_t npeers)
{
    long const maxExpire = UNSQUELCH_EXPIRE_MULTIPLIER * npeers;
    auto m = std::max(MAX_UNSQUELCH_EXPIRE.count(), maxExpire);
    if (m > OVERALL_MAX_UNSQUELCH_EXPIRE.count())
    {
        m = OVERALL_MAX_UNSQUELCH_EXPIRE.count();
        JLOG(journal_.warn())
            << "getSquelchDuration: unexpected squelch duration " << npeers;
    }
    auto d =
        std::chrono::seconds(ripple::rand_int(MIN_UNSQUELCH_EXPIRE.count(), m));
    return d;
}

template <typename clock_type>
void
Slot<clock_type>::deletePeer(PublicKey const& validator, id_t id, bool erase)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        JLOG(journal_.debug())
            << "deletePeer: " << Slice(validator) << " " << id << " selected "
            << (it->second.state == PeerState::Selected) << " considered "
            << (considered_.find(id) != considered_.end()) << " erase "
            << erase;
        auto now = clock_type::now();
        if (it->second.state == PeerState::Selected)
        {
            for (auto& [k, v] : peers_)
            {
                if (v.state == PeerState::Squelched)
                    handler_.unsquelch(validator, k);
                v.state = PeerState::Counting;
                v.count = 0;
                v.expire = now;
            }

            considered_.clear();
            reachedThreshold_ = 0;
            state_ = SlotState::Counting;
        }
        else if (considered_.find(id) != considered_.end())
        {
            if (it->second.count > MAX_MESSAGE_THRESHOLD)
                --reachedThreshold_;
            considered_.erase(id);
        }

        it->second.lastMessage = now;
        it->second.count = 0;

        if (erase)
            peers_.erase(it);
    }
}

template <typename clock_type>
void
Slot<clock_type>::resetCounts()
{
    for (auto& [_, peer] : peers_)
    {
        (void)_;
        peer.count = 0;
    }
}

template <typename clock_type>
void
Slot<clock_type>::initCounting()
{
    state_ = SlotState::Counting;
    considered_.clear();
    reachedThreshold_ = 0;
    resetCounts();
}

template <typename clock_type>
std::uint16_t
Slot<clock_type>::inState(PeerState state) const
{
    return std::count_if(peers_.begin(), peers_.end(), [&](auto const& it) {
        return (it.second.state == state);
    });
}

template <typename clock_type>
std::uint16_t
Slot<clock_type>::notInState(PeerState state) const
{
    return std::count_if(peers_.begin(), peers_.end(), [&](auto const& it) {
        return (it.second.state != state);
    });
}

template <typename clock_type>
std::set<typename Peer::id_t>
Slot<clock_type>::getSelected() const
{
    std::set<id_t> init;
    return std::accumulate(
        peers_.begin(), peers_.end(), init, [](auto& init, auto const& it) {
            if (it.second.state == PeerState::Selected)
            {
                init.insert(it.first);
                return init;
            }
            return init;
        });
}

template <typename clock_type>
std::unordered_map<
    typename Peer::id_t,
    std::tuple<PeerState, uint16_t, uint32_t, uint32_t>>
Slot<clock_type>::getPeers() const
{
    using namespace std::chrono;
    auto init = std::unordered_map<
        id_t,
        std::tuple<PeerState, std::uint16_t, std::uint32_t, std::uint32_t>>();
    return std::accumulate(
        peers_.begin(), peers_.end(), init, [](auto& init, auto const& it) {
            init.emplace(std::make_pair(
                it.first,
                std::move(std::make_tuple(
                    it.second.state,
                    it.second.count,
                    epoch<milliseconds>(it.second.expire).count(),
                    epoch<milliseconds>(it.second.lastMessage).count()))));
            return init;
        });
}

/** Slots is a container for validator's Slot and handles Slot update
 * when a message is received from a validator. It also handles Slot aging
 * and checks for peers which are disconnected or stopped relaying the messages.
 */
template <typename clock_type>
class Slots final
{
    using time_point = typename clock_type::time_point;
    using id_t = typename Peer::id_t;
    using messages = beast::aged_unordered_map<
        uint256,
        std::unordered_set<Peer::id_t>,
        clock_type,
        hardened_hash<strong_hash>>;

public:
    /**
     * @param app Applicaton reference
     * @param handler Squelch/unsquelch implementation
     */
    Slots(Application& app, SquelchHandler const& handler)
        : handler_(handler), app_(app), journal_(app.journal("Slots"))
    {
    }
    ~Slots() = default;
    /** Calls Slot::update of Slot associated with the validator.
     * @param key Message's hash
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param type Received protocol message type
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        protocol::MessageType type);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator.
     */
    void
    deleteIdlePeers();

    /** Return number of peers in state */
    std::optional<std::uint16_t>
    inState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.inState(state);
        return {};
    }

    /** Return number of peers not in state */
    std::optional<std::uint16_t>
    notInState(PublicKey const& validator, PeerState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.notInState(state);
        return {};
    }

    /** Return true if Slot is in state */
    bool
    inState(PublicKey const& validator, SlotState state) const
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.state_ == state;
        return false;
    }

    /** Get selected peers */
    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getSelected();
        return {};
    }

    /** Get peers info. Return map of peer's state, count, and squelch
     * expiration milliseconds.
     */
    std::unordered_map<
        typename Peer::id_t,
        std::tuple<PeerState, uint16_t, uint32_t, std::uint32_t>>
    getPeers(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getPeers();
        return {};
    }

    /** Get Slot's state */
    std::optional<SlotState>
    getState(PublicKey const& validator)
    {
        auto const& it = slots_.find(validator);
        if (it != slots_.end())
            return it->second.getState();
        return {};
    }

    /** Called when a peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param erase If true then erase the peer
     */
    void
    deletePeer(id_t id, bool erase);

private:
    /** Add message/peer if have not seen this message
     * from the peer. A message is aged after IDLED seconds.
     * Return true if added */
    bool
    addPeerMessage(uint256 const& key, id_t id);

    hash_map<PublicKey, Slot<clock_type>> slots_;
    SquelchHandler const& handler_;  // squelch/unsquelch handler
    Application& app_;
    beast::Journal const journal_;
    // Maintain aged container of message/peers. This is required
    // to discard duplicate message from the same peer. A message
    // is aged after IDLED seconds. A message received IDLED seconds
    // after it was relayed is ignored by PeerImp.
    inline static messages peersWithMessage_{
        beast::get_abstract_clock<clock_type>()};
};

template <typename clock_type>
bool
Slots<clock_type>::addPeerMessage(uint256 const& key, id_t id)
{
    beast::expire(peersWithMessage_, reduce_relay::IDLED);

    if (key.isNonZero())
    {
        auto it = peersWithMessage_.find(key);
        if (it == peersWithMessage_.end())
        {
            JLOG(journal_.trace())
                << "addPeerMessage: new " << to_string(key) << " " << id;
            peersWithMessage_.emplace(key, std::unordered_set<id_t>{id});
            return true;
        }

        if (it->second.find(id) != it->second.end())
        {
            JLOG(journal_.trace()) << "addPeerMessage: duplicate message "
                                   << to_string(key) << " " << id;
            return false;
        }

        JLOG(journal_.trace())
            << "addPeerMessage: added " << to_string(key) << " " << id;

        it->second.insert(id);
    }

    return true;
}

template <typename clock_type>
void
Slots<clock_type>::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    id_t id,
    protocol::MessageType type)
{
    if (!addPeerMessage(key, id))
        return;

    auto it = slots_.find(validator);
    if (it == slots_.end())
    {
        JLOG(journal_.debug())
            << "updateSlotAndSquelch: new slot " << Slice(validator);
        auto it = slots_
                      .emplace(std::make_pair(
                          validator,
                          Slot<clock_type>(handler_, app_.journal("Slot"))))
                      .first;
        it->second.update(validator, id, type);
    }
    else
        it->second.update(validator, id, type);
}

template <typename clock_type>
void
Slots<clock_type>::deletePeer(id_t id, bool erase)
{
    for (auto& [validator, slot] : slots_)
        slot.deletePeer(validator, id, erase);
}

template <typename clock_type>
void
Slots<clock_type>::deleteIdlePeers()
{
    auto now = clock_type::now();

    for (auto it = slots_.begin(); it != slots_.end();)
    {
        it->second.deleteIdlePeer(it->first);
        if (now - it->second.getLastSelected() > MAX_UNSQUELCH_EXPIRE)
        {
            JLOG(journal_.debug())
                << "deleteIdlePeers: deleting idle slot " << Slice(it->first);
            it = slots_.erase(it);
        }
        else
            ++it;
    }
}

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
