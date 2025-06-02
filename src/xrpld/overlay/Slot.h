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

#include <xrpld/core/Config.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_unordered_map.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/messages.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ripple {
// used to make private members of Slots class accessible for testing
namespace test {
class enhanced_squelch_test;
class base_squelch_test;
class OverlaySim;
}  // namespace test

namespace reduce_relay {

template <typename clock_type>
class Slots;

/** Peer's State */
enum class PeerState : uint8_t {
    Counting,   // counting messages
    Selected,   // selected to relay, counting if Slot in Counting
    Squelched,  // squelched, doesn't relay
};

inline std::string
to_string(PeerState state)
{
    switch (state)
    {
        case PeerState::Counting:
            return "counting";
        case PeerState::Selected:
            return "selected";
        case PeerState::Squelched:
            return "squelched";
        default:
            return "unknown";
    }
}
/** Slot's State */
enum class SlotState : uint8_t {
    Counting,  // counting messages
    Selected,  // peers selected, stop counting
};

inline std::string
to_string(SlotState state)
{
    switch (state)
    {
        case SlotState::Counting:
            return "counting";
        case SlotState::Selected:
            return "selected";
        default:
            return "unknown";
    }
}

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
    /** Squelch handler for a single peer
     * @param validator Public key of the source validator
     * @param id Peer's id to squelch
     * @param duration Squelch duration in seconds
     */
    virtual void
    squelch(PublicKey const& validator, Peer::id_t id, std::uint32_t duration)
        const = 0;

    /** Squelch for all peers, the method must call slots.squelchValidator
     * to register that a (validator,peer) was squelched
     * @param validator Public key of the source validator
     * @param duration Squelch duration in seconds
     */
    virtual void
    squelchAll(PublicKey const& validator, std::uint32_t duration) = 0;

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
    friend class Slots<clock_type>;
    friend class test::enhanced_squelch_test;
    friend class test::OverlaySim;

    using id_t = Peer::id_t;
    using time_point = typename clock_type::time_point;

    // a callback to report ignored squelches
    using ignored_squelch_callback = std::function<void()>;

    /** Constructor
     * @param journal Journal for logging
     * @param handler Squelch/Unsquelch implementation
     * @param maxSelectedPeers the maximum number of peers to be selected as
     * validator message source
     */
    Slot(
        SquelchHandler const& handler,
        beast::Journal journal,
        uint16_t maxSelectedPeers,
        bool isTrusted)
        : reachedThreshold_(0)
        , lastSelected_(clock_type::now())
        , state_(SlotState::Counting)
        , handler_(handler)
        , journal_(journal)
        , maxSelectedPeers_(maxSelectedPeers)
        , isTrusted_(isTrusted)
    {
    }

    /** Update peer info. If the message is from a new
     * peer or from a previously expired squelched peer then switch
     * the peer's and slot's state to Counting. If time of last
     * selection round is > 2 * MAX_UNSQUELCH_EXPIRE_DEFAULT then switch the
     * slot's state to Counting. If the number of messages for the peer is >
     * MIN_MESSAGE_THRESHOLD then add peer to considered peers pool. If the
     * number of considered peers who reached MAX_MESSAGE_THRESHOLD is
     * maxSelectedPeers_ then randomly select maxSelectedPeers_ from
     * considered peers, and call squelch handler for each peer, which is
     * not selected and not already in Squelched state. Set the state for
     * those peers to Squelched and reset the count of all peers. Set slot's
     * state to Selected. Message count is not updated when the slot is in
     * Selected state.
     * @param validator Public key of the source validator
     * @param id Peer id which received the message
     * @param callback A callback to report ignored squelches
     */
    void
    update(
        PublicKey const& validator,
        id_t id,
        ignored_squelch_callback callback);

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
    time_point const&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /** Check if peers stopped relaying messages. If a peer is
     * selected peer then call unsquelch handler for all
     * currently squelched peers and switch the slot to
     * Counting state.
     * @param validator Public key of the source validator
     */
    void
    deleteIdlePeer(PublicKey const& validator);

    /** Get random squelch duration between MIN_UNSQUELCH_EXPIRE and
     * min(max(MAX_UNSQUELCH_EXPIRE_DEFAULT, SQUELCH_PER_PEER * npeers),
     *     MAX_UNSQUELCH_EXPIRE_PEERS)
     * @param npeers number of peers that can be squelched in the Slot
     */
    std::chrono::seconds
    getSquelchDuration(std::size_t npeers);

    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts();

    /** Initialize slot to Counting state */
    void
    initCounting();

    void
    onWrite(beast::PropertyStream::Map& stream) const;

    /** Data maintained for each peer */
    struct PeerInfo
    {
        PeerState state;            // peer's state
        std::size_t count;          // message count
        time_point expire;          // squelch expiration time
        time_point lastMessage;     // time last message received
        std::size_t timesSelected;  // number of times the peer was selected
        std::size_t timesCloseToThreshold;
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

    // the maximum number of peers that should be selected as a validator
    // message source
    uint16_t const maxSelectedPeers_;

    // indicate if the slot is for a trusted validator
    bool const isTrusted_;
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
            JLOG(journal_.trace())
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
    ignored_squelch_callback callback)
{
    using namespace std::chrono;
    auto now = clock_type::now();
    auto it = peers_.find(id);
    // First message from this peer
    if (it == peers_.end())
    {
        JLOG(journal_.trace())
            << "update: adding peer " << Slice(validator) << " " << id;
        peers_.emplace(std::make_pair(
            id,
            PeerInfo{
                .state = PeerState::Counting,
                .count = 0,
                .expire = now,
                .lastMessage = now,
                .timesSelected = 0,
                .timesCloseToThreshold = 0}));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    if (it->second.state == PeerState::Squelched && now > it->second.expire)
    {
        JLOG(journal_.trace())
            << "update: squelch expired " << Slice(validator) << " " << id;
        it->second.state = PeerState::Counting;
        it->second.lastMessage = now;
        initCounting();
        return;
    }

    auto& peer = it->second;

    JLOG(journal_.trace())
        << "update: existing peer " << Slice(validator) << " " << id
        << " slot state " << static_cast<int>(state_) << " peer state "
        << static_cast<int>(peer.state) << " count " << peer.count << " last "
        << duration_cast<milliseconds>(now - peer.lastMessage).count()
        << " pool " << considered_.size() << " threshold " << reachedThreshold_;

    if (now - peer.lastMessage - IDLED <= milliseconds{500})
        ++peer.timesCloseToThreshold;

    peer.lastMessage = now;

    // report if we received a message from a squelched peer
    if (peer.state == PeerState::Squelched)
        callback();

    if (state_ != SlotState::Counting || peer.state == PeerState::Squelched)
        return;

    if (++peer.count > MIN_MESSAGE_THRESHOLD)
        considered_.insert(id);
    if (peer.count == (MAX_MESSAGE_THRESHOLD + 1))
        ++reachedThreshold_;

    if (now - lastSelected_ > 2 * MAX_UNSQUELCH_EXPIRE_DEFAULT)
    {
        JLOG(journal_.trace())
            << "update: resetting due to inactivity " << Slice(validator) << " "
            << id << " " << duration_cast<seconds>(now - lastSelected_).count();
        initCounting();
        return;
    }

    if (reachedThreshold_ == maxSelectedPeers_)
    {
        for (auto const& [id, info] : peers_)
        {
            if (info.state == PeerState::Selected &&
                info.count < MIN_MESSAGE_THRESHOLD)
            {
                JLOG(journal_.debug())
                    << "update: previously selected peer " << id
                    << " failed to reach a threshold with: " << info.count;
            }
        }
        // Randomly select maxSelectedPeers_ peers from considered.
        // Exclude peers that have been idling > IDLED -
        // it's possible that deleteIdlePeer() has not been called yet.
        // If number of remaining peers != maxSelectedPeers_
        // then reset the Counting state and let deleteIdlePeer() handle
        // idled peers.
        std::unordered_set<id_t> selected;
        auto const consideredPoolSize = considered_.size();
        while (selected.size() != maxSelectedPeers_ && considered_.size() != 0)
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

        if (selected.size() != maxSelectedPeers_)
        {
            JLOG(journal_.trace())
                << "update: selection failed " << Slice(validator) << " " << id;
            initCounting();
            return;
        }

        lastSelected_ = now;

        auto s = selected.begin();
        JLOG(journal_.trace())
            << "update: " << Slice(validator) << " " << id << " pool size "
            << consideredPoolSize << " selected " << *s << " "
            << *std::next(s, 1) << " " << *std::next(s, 2);

        XRPL_ASSERT(
            peers_.size() >= maxSelectedPeers_,
            "ripple::reduce_relay::Slot::update : minimum peers");

        // squelch peers which are not selected and
        // not already squelched
        std::stringstream str;
        for (auto& [k, v] : peers_)
        {
            v.count = 0;

            if (selected.find(k) != selected.end())
            {
                v.state = PeerState::Selected;
                ++v.timesSelected;
            }

            else if (v.state != PeerState::Squelched)
            {
                if (v.state == PeerState::Selected)
                {
                    JLOG(journal_.debug())
                        << "squelching previously selected peer";
                }
                if (journal_.trace())
                    str << k << " ";
                v.state = PeerState::Squelched;
                std::chrono::seconds duration =
                    getSquelchDuration(peers_.size() - maxSelectedPeers_);
                v.expire = now + duration;
                handler_.squelch(validator, k, duration.count());
            }
        }
        JLOG(journal_.trace()) << "update: squelching " << Slice(validator)
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
    using namespace std::chrono;
    auto m = std::max(
        MAX_UNSQUELCH_EXPIRE_DEFAULT, seconds{SQUELCH_PER_PEER * npeers});
    if (m > MAX_UNSQUELCH_EXPIRE_PEERS)
    {
        m = MAX_UNSQUELCH_EXPIRE_PEERS;
        JLOG(journal_.warn())
            << "getSquelchDuration: unexpected squelch duration " << npeers;
    }
    return seconds{ripple::rand_int(MIN_UNSQUELCH_EXPIRE / 1s, m / 1s)};
}

template <typename clock_type>
void
Slot<clock_type>::deletePeer(PublicKey const& validator, id_t id, bool erase)
{
    auto it = peers_.find(id);
    if (it != peers_.end())
    {
        JLOG(journal_.trace())
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
Slot<clock_type>::onWrite(beast::PropertyStream::Map& stream) const
{
    auto const now = clock_type::now();
    stream["state"] = to_string(state_);
    stream["reachedThreshold"] = reachedThreshold_;
    stream["considered"] = considered_.size();
    stream["lastSelected"] =
        duration_cast<std::chrono::seconds>(now - lastSelected_).count();
    stream["isTrusted"] = isTrusted_;

    beast::PropertyStream::Set peers("peers", stream);

    for (auto const& [id, info] : peers_)
    {
        beast::PropertyStream::Map item(peers);
        item["id"] = id;
        item["count"] = info.count;
        item["expire"] =
            duration_cast<std::chrono::seconds>(info.expire - now).count();
        item["lastMessage"] =
            duration_cast<std::chrono::seconds>(now - info.lastMessage).count();
        item["timesSelected"] = info.timesSelected;
        item["timesCloseToThreshold"] = info.timesCloseToThreshold;
        item["state"] = to_string(info.state);
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

/** Slots is a container for validator's Slot and handles Slot update
 * when a message is received from a validator. It also handles Slot aging
 * and checks for peers which are disconnected or stopped relaying the
 * messages.
 */
template <typename clock_type>
class Slots final
{
    friend class test::enhanced_squelch_test;
    friend class test::base_squelch_test;
    friend class test::OverlaySim;

    using time_point = typename clock_type::time_point;
    using id_t = typename Peer::id_t;
    using messages = beast::aged_unordered_map<
        uint256,
        std::unordered_set<Peer::id_t>,
        clock_type,
        hardened_hash<strong_hash>>;
    using validators = beast::aged_unordered_map<
        PublicKey,
        std::unordered_set<Peer::id_t>,
        clock_type,
        hardened_hash<strong_hash>>;
    using slots_map = hash_map<PublicKey, Slot<clock_type>>;

public:
    /**
     * @param logs reference to the logger
     * @param handler Squelch/unsquelch implementation
     * @param config reference to the global config
     */
    Slots(Logs& logs, SquelchHandler& handler, Config const& config)
        : handler_(handler)
        , logs_(logs)
        , journal_(logs.journal("Slots"))
        , baseSquelchEnabled_(config.VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
        , maxSelectedPeers_(config.VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS)
        , enhancedSquelchEnabled_(
              config.VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE)
    {
    }

    ~Slots() = default;

    /** Check if base squelching feature is enabled and ready */
    bool
    baseSquelchReady()
    {
        return baseSquelchEnabled_ && reduceRelayReady();
    }

    /** Check if enhanced squelching feature is enabled and ready */
    bool
    enhancedSquelchReady()
    {
        return enhancedSquelchEnabled_ && reduceRelayReady();
    }

    /** Check if reduce_relay::WAIT_ON_BOOTUP time passed since startup */
    bool
    reduceRelayReady()
    {
        if (!reduceRelayReady_)
            reduceRelayReady_ =
                reduce_relay::epoch<std::chrono::minutes>(clock_type::now()) >
                reduce_relay::WAIT_ON_BOOTUP;

        return reduceRelayReady_;
    }

    /** Updates untrusted validator slot. Do not call for trusted
     * validators. The caller must ensure passed messages are unique.
     * @param key Message hash
     * @param validator Validator public key
     * @param id The ID of the peer that sent the message
     */
    void
    updateValidatorSlot(uint256 const& key, PublicKey const& validator, id_t id)
    {
        updateValidatorSlot(key, validator, id, []() {});
    }

    /** Updates untrusted validator slot. Do not call for trusted
     * validators. The caller must ensure passed messages are unique.
     * @param key Message hash
     * @param validator Validator public key
     * @param id The ID of the peer that sent the message
     * @param callback A callback to report ignored validations
     */
    void
    updateValidatorSlot(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        typename Slot<clock_type>::ignored_squelch_callback callback);

    /** Calls Slot::update of Slot associated with the validator, with a
     * noop callback.
     * @param key Message's hash
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param isTrusted Boolean to indicate if the message is from a trusted
     * validator
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        bool isTrusted)
    {
        updateSlotAndSquelch(key, validator, id, []() {}, isTrusted);
    }

    /** Calls Slot::update of Slot associated with the validator.
     * @param key Message's hash
     * @param validator Validator's public key
     * @param id Peer's id which received the message
     * @param callback A callback to report ignored validations
     * @param isTrusted Boolean to indicate if the message is from a trusted
     * validator
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        id_t id,
        typename Slot<clock_type>::ignored_squelch_callback callback,
        bool isTrusted);

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator.
     */
    void
    deleteIdlePeers();
    /** Called when a peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     * @param erase If true then erase the peer
     */
    void
    deletePeer(id_t id, bool erase);

    /** Called to register that a given validator was squelched for a given
     * peer. It is expected that this method is called by SquelchHandler.
     *
     * @param key Validator public key
     * @param id peer ID
     */
    void
    squelchValidator(PublicKey const& key, id_t id)
    {
        auto it = peersWithValidators_.find(key);
        if (it == peersWithValidators_.end())
            peersWithValidators_.emplace(key, std::unordered_set<id_t>{id});

        else if (it->second.find(id) == it->second.end())
            it->second.insert(id);
    }

    void
    onWrite(beast::PropertyStream::Map& stream) const;

private:
    /** Add message/peer if have not seen this message
     * from the peer. A message is aged after IDLED seconds.
     * Return true if added */
    bool
    addPeerMessage(uint256 const& key, id_t id);

    /**
     * Updates the last message sent from a validator.
     * @param validator The validator public key
     * @param peer The peer ID sending the message
     * @return true if the validator was updated, false otherwise
     */
    std::optional<PublicKey>
    updateConsideredValidator(PublicKey const& validator, Peer::id_t peer);

    /** Remove all validators that have become invalid due to selection
     * criteria
     * @return zero or more validators that have been removed.
     */
    std::vector<PublicKey>
    cleanConsideredValidators();

    /** Checks whether a given validator is squelched.
     * @param key Validator public key
     * @return true if a given validator was squelched
     */
    bool
    validatorSquelched(PublicKey const& key) const
    {
        beast::expire(
            peersWithValidators_, reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT);

        return peersWithValidators_.find(key) != peersWithValidators_.end();
    }

    /** Checks whether a given peer was recently sent a squelch message for
     * a given validator.
     * @param key Validator public key
     * @param id Peer id
     * @return true if a given validator was squelched for a given peeru
     */
    bool
    peerSquelched(PublicKey const& key, id_t id) const
    {
        beast::expire(
            peersWithValidators_, reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT);

        auto const it = peersWithValidators_.find(key);

        // if validator was not squelched, the peer was also not squelched
        if (it == peersWithValidators_.end())
            return false;

        // if a peer is found the squelch for it has not expired
        return it->second.find(id) != it->second.end();
    }

    std::atomic_bool reduceRelayReady_{false};

    slots_map slots_;
    slots_map untrusted_slots_;

    SquelchHandler& handler_;  // squelch/unsquelch handler
    Logs& logs_;
    beast::Journal const journal_;

    bool const baseSquelchEnabled_;
    uint16_t const maxSelectedPeers_;
    bool const enhancedSquelchEnabled_;

    // Maintain aged container of message/peers. This is required
    // to discard duplicate message from the same peer. A message
    // is aged after IDLED seconds. A message received IDLED seconds
    // after it was relayed is ignored by PeerImp.
    inline static messages peersWithMessage_{
        beast::get_abstract_clock<clock_type>()};

    // Maintain aged container of validator/peers. This is used to track
    // which validator/peer were squelced. A peer that whose squelch
    // has expired is removed.
    inline static validators peersWithValidators_{
        beast::get_abstract_clock<clock_type>()};

    struct ValidatorInfo
    {
        size_t count;  // the number of messages sent from this validator
        time_point lastMessage;                // timestamp of the last message
        std::unordered_set<Peer::id_t> peers;  // a list of peer IDs that sent a
                                               // message for this validator
    };

    hash_map<PublicKey, ValidatorInfo> considered_validators_;
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
std::optional<PublicKey>
Slots<clock_type>::updateConsideredValidator(
    PublicKey const& validator,
    Peer::id_t peer)
{
    auto const now = clock_type::now();

    auto it = considered_validators_.find(validator);
    if (it == considered_validators_.end())
    {
        considered_validators_.emplace(std::make_pair(
            validator,
            ValidatorInfo{
                .count = 1,
                .lastMessage = now,
                .peers = {peer},
            }));

        return {};
    }

    // the validator idled. Don't update it, it will be cleaned later
    if (now - it->second.lastMessage > IDLED)
        return {};

    it->second.peers.insert(peer);

    it->second.lastMessage = now;
    ++it->second.count;

    if (it->second.count < MAX_MESSAGE_THRESHOLD ||
        it->second.peers.size() < reduce_relay::MAX_SELECTED_PEERS)
        return {};

    auto const key = it->first;
    considered_validators_.erase(it);

    return key;
}

template <typename clock_type>
std::vector<PublicKey>
Slots<clock_type>::cleanConsideredValidators()
{
    auto const now = clock_type::now();

    std::vector<PublicKey> keys;
    for (auto it = considered_validators_.begin();
         it != considered_validators_.end();)
    {
        if (now - it->second.lastMessage > IDLED)
        {
            keys.push_back(it->first);
            it = considered_validators_.erase(it);
        }
        else
            ++it;
    }

    return keys;
}

template <typename clock_type>
void
Slots<clock_type>::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    id_t id,
    typename Slot<clock_type>::ignored_squelch_callback callback,
    bool isTrusted)
{
    if (!addPeerMessage(key, id))
        return;

    // If we receive a message from a trusted validator either update an
    // existing slot or insert a new one. If we are not running enhanced
    // squelching also deduplicate untrusted validator messages
    if (isTrusted || !enhancedSquelchEnabled_)
    {
        JLOG(journal_.trace())
            << "updateSlotAndSquelch: new slot " << Slice(validator);
        auto it = slots_
                      .emplace(std::make_pair(
                          validator,
                          Slot<clock_type>(
                              handler_,
                              logs_.journal("Slot"),
                              maxSelectedPeers_,
                              isTrusted)))
                      .first;
        it->second.update(validator, id, callback);
    }
    else
    {
        auto it = untrusted_slots_.find(validator);
        // If we received a message from a validator that is not
        // selected, and is not squelched, there is nothing to do. It
        // will be squelched later when `updateValidatorSlot` is called.
        if (it == untrusted_slots_.end())
            return;

        it->second.update(validator, id, callback);
    }
}

template <typename clock_type>
void
Slots<clock_type>::updateValidatorSlot(
    uint256 const& key,
    PublicKey const& validator,
    id_t id,
    typename Slot<clock_type>::ignored_squelch_callback callback)
{
    // We received a message from an already selected validator
    // we can ignore this message
    if (untrusted_slots_.find(validator) != untrusted_slots_.end())
        return;

    // We received a message from an already squelched validator.
    // This could happen in few cases:
    //      1. It happened so that the squelch for a particular peer expired
    //      before our local squelch.
    //      2. We receive a message from a new peer that did not receive the
    //      squelch request.
    //      3. The peer is ignoring our squelch request and we have not sent
    //      the controll message in a while.
    // In all of these cases we can only send them a squelch request again.
    if (validatorSquelched(validator))
    {
        if (!peerSquelched(validator, id))
        {
            squelchValidator(validator, id);
            handler_.squelch(
                validator, id, MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
        }
        return;
    }

    // update a slot if the message is from a selected untrusted validator
    if (auto const& it = untrusted_slots_.find(validator);
        it != untrusted_slots_.end())
    {
        it->second.update(validator, id, callback);
        return;
    }

    // Do we have any available slots for additional untrusted validators?
    // This could happen in few cases:
    //      1. We received a message from a new untrusted validator, but we
    //      are at capacity.
    //      2. We received a message from a previously squelched validator.
    // In all of these cases we send a squelch message to all peers.
    // The validator may still  be considered by the selector. However, it
    // will be eventually cleaned and squelched
    if (untrusted_slots_.size() == MAX_UNTRUSTED_SLOTS)
    {
        handler_.squelchAll(validator, MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
        return;
    }

    if (auto const v = updateConsideredValidator(validator, id))
        untrusted_slots_.emplace(std::make_pair(
            *v,
            Slot<clock_type>(
                handler_, logs_.journal("Slot"), maxSelectedPeers_, false)));
    // When we reach MAX_UNTRUSTED_SLOTS, don't  explicitly clean them.
    // Since we stop updating their counters, they will idle, and will be
    // removed and squelched.
}

template <typename clock_type>
void
Slots<clock_type>::deletePeer(id_t id, bool erase)
{
    auto deletePeer = [&](slots_map& slots) {
        for (auto& [validator, slot] : slots)
            slot.deletePeer(validator, id, erase);
    };

    deletePeer(slots_);
    deletePeer(untrusted_slots_);
}

template <typename clock_type>
void
Slots<clock_type>::deleteIdlePeers()
{
    auto deleteSlots = [&](slots_map& slots) {
        auto const now = clock_type::now();

        for (auto it = slots.begin(); it != slots.end();)
        {
            it->second.deleteIdlePeer(it->first);
            if (now - it->second.getLastSelected() >
                MAX_UNSQUELCH_EXPIRE_DEFAULT)
            {
                JLOG(journal_.trace()) << "deleteIdlePeers: deleting idle slot "
                                       << Slice(it->first);

                // if an untrusted validator slot idled - peers stopped
                // sending messages for this validator squelch it
                if (!it->second.isTrusted_)
                    handler_.squelchAll(
                        it->first, MAX_UNSQUELCH_EXPIRE_DEFAULT.count());

                it = slots.erase(it);
            }
            else
                ++it;
        }
    };

    deleteSlots(slots_);
    deleteSlots(untrusted_slots_);

    // remove and squelch all validators that the selector deemed unsuitable
    // there might be some good validators in this set that "lapsed".
    // However, since these are untrusted validators we're not concerned
    for (auto const& validator : cleanConsideredValidators())
        handler_.squelchAll(validator, MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
}

template <typename clock_type>
void
Slots<clock_type>::onWrite(beast::PropertyStream::Map& stream) const
{
    auto const writeSlot =
        [](beast::PropertyStream::Set& set,
           hash_map<PublicKey, Slot<clock_type>> const& slots) {
            for (auto const& [validator, slot] : slots)
            {
                beast::PropertyStream::Map item(set);
                item["validator"] = toBase58(TokenType::NodePublic, validator);
                slot.onWrite(item);
            }
        };

    beast::PropertyStream::Map slots("slots", stream);

    {
        beast::PropertyStream::Set set("trusted", slots);
        writeSlot(set, slots_);
    }

    {
        beast::PropertyStream::Set set("untrusted", slots);
        writeSlot(set, untrusted_slots_);
    }

    {
        beast::PropertyStream::Set set("considered", slots);

        auto const now = clock_type::now();

        for (auto const& [validator, info] : considered_validators_)
        {
            beast::PropertyStream::Map item(set);
            item["validator"] = toBase58(TokenType::NodePublic, validator);
            item["lastMessage"] =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - info.lastMessage)
                    .count();
            item["messageCount"] = info.count;
            item["peers"] = info.peers.size();
        }
    }
}

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
