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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/container/aged_unordered_map.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/protocol/PublicKey.h>

#include <functional>
#include <optional>

namespace ripple {
namespace reduce_relay {

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
     * @param callback a callback to register that a validator was squelched
     */
    virtual void
    squelchAll(
        PublicKey const& validator,
        std::uint32_t duration,
        std::function<void(Peer::id_t)> callback) = 0;

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
class Slot final
{
    friend class Slots;

    // a callback to report ignored squelches
    using ignored_squelch_callback = std::function<void()>;
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;
    using time_point = clock_type::time_point;

public:
    /** Data maintained for each peer */
    struct PeerInfo
    {
        PeerState state;            // peer's state
        std::size_t count;          // message count
        time_point expire;          // squelch expiration time
        time_point lastMessage;     // time last message received
        std::size_t timesSelected;  // number of times the peer was selected
    };

    /** Get all peers of the slot. This methos is only to be used in
     * unit-tests.
     */
    std::unordered_map<Peer::id_t, PeerInfo> const&
    getPeers() const
    {
        return peers_;
    }

    /** Get the slots state. */
    SlotState
    getState() const
    {
        return state_;
    }

private:
    /** Constructor
     * @param journal Journal for logging
     * @param handler Squelch/Unsquelch implementation
     * @param maxSelectedPeers the maximum number of peers to be selected as
     * validator message source
     * @param istrusted indicate if the slot is for a trusted validator
     * @param clock a reference to a steady clock
     */
    Slot(
        SquelchHandler const& handler,
        beast::Journal journal,
        uint16_t maxSelectedPeers,
        bool isTrusted,
        clock_type& clock)
        : reachedThreshold_(0)
        , lastSelected_(clock.now())
        , state_(SlotState::Counting)
        , handler_(handler)
        , journal_(journal)
        , maxSelectedPeers_(maxSelectedPeers)
        , isTrusted_(isTrusted)
        , clock_(clock)
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
        Peer::id_t id,
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
    deletePeer(PublicKey const& validator, Peer::id_t id, bool erase);

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
    getSquelchDuration(std::size_t npeers) const;

    /** Reset counts of peers in Selected or Counting state */
    void
    resetCounts();

    /** Initialize slot to Counting state */
    void
    initCounting();

    void
    onWrite(beast::PropertyStream::Map& stream) const;

    std::unordered_map<Peer::id_t, PeerInfo> peers_;  // peer's data

    // pool of peers considered as the source of messages
    // from validator - peers that reached MIN_MESSAGE_THRESHOLD
    std::unordered_set<Peer::id_t> considered_;

    // number of peers that reached MAX_MESSAGE_THRESHOLD
    std::uint16_t reachedThreshold_;

    // last time peers were selected, used to age the slot
    time_point lastSelected_;

    SlotState state_;                // slot's state
    SquelchHandler const& handler_;  // squelch/unsquelch handler
    beast::Journal const journal_;   // logging

    // the maximum number of peers that should be selected as a validator
    // message source
    uint16_t const maxSelectedPeers_;

    // indicate if the slot is for a trusted validator
    bool const isTrusted_;

    clock_type& clock_;
};

/** Slots is a container for validator's Slot and handles Slot update
 * when a message is received from a validator. It also handles Slot aging
 * and checks for peers which are disconnected or stopped relaying the
 * messages.
 */
class Slots
{
public:
    using clock_type = beast::abstract_clock<std::chrono::steady_clock>;
    using time_point = clock_type::time_point;

    using messages = beast::aged_unordered_map<
        uint256,
        std::unordered_set<Peer::id_t>,
        clock_type::clock_type,
        hardened_hash<strong_hash>>;
    using validators = beast::aged_unordered_map<
        PublicKey,
        std::unordered_set<Peer::id_t>,
        clock_type::clock_type,
        hardened_hash<strong_hash>>;
    using slots_map = hash_map<PublicKey, Slot>;

    /**
     * @param logs reference to the logger
     * @param handler Squelch/unsquelch implementation
     * @param config reference to the global config
     * @param clock a reference to a steady clock
     */
    Slots(
        Logs& logs,
        SquelchHandler& handler,
        Config const& config,
        clock_type& clock)
        : handler_(handler)
        , logs_(logs)
        , journal_(logs.journal("Slots"))
        , baseSquelchEnabled_(config.VP_REDUCE_RELAY_BASE_SQUELCH_ENABLE)
        , maxSelectedPeers_(config.VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS)
        , enhancedSquelchEnabled_(
              config.VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE)
        , clock_(clock)
        , peersWithMessage_(clock)
        , peersWithSquelchedValidators_(clock)
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
    reduceRelayReady();

    /** Updates untrusted validator slot. Do not call for trusted
     * validators. The caller must ensure passed messages are unique.
     * @param key Message hash
     * @param validator Validator public key
     * @param id The ID of the peer that sent the message
     */
    void
    updateUntrustedValidatorSlot(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id)
    {
        updateUntrustedValidatorSlot(key, validator, id, []() {});
    }

    /** Updates untrusted validator slot. Do not call for trusted
     * validators. The caller must ensure passed messages are unique.
     * @param key Message hash
     * @param validator Validator public key
     * @param id The ID of the peer that sent the message
     * @param callback A callback to report ignored validations
     */
    void
    updateUntrustedValidatorSlot(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id,
        typename Slot::ignored_squelch_callback callback);

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
        Peer::id_t id,
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
        Peer::id_t id,
        typename Slot::ignored_squelch_callback callback,
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
    deletePeer(Peer::id_t id, bool erase);

    void
    onWrite(beast::PropertyStream::Map& stream) const;

protected:
    /** Add message/peer if have not seen this message
     * from the peer. A message is aged after IDLED seconds.
     * Return true if added */
    bool
    addPeerMessage(uint256 const& key, Peer::id_t id);

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

    /** Expires old validators and checks whether a given validator is
     * squelched.
     * @param validatorKey Validator public key
     * @return true if a given validator was squelched
     */
    bool
    expireAndIsValidatorSquelched(PublicKey const& validatorKey);

    /** Expires old validators and checks whether a given peer was recently
     * squelched for a given validator.
     * @param validatorKey Validator public key
     * @param peerID Peer id
     * @return true if a given validator was squelched for a given peer
     */
    bool
    expireAndIsPeerSquelched(PublicKey const& validatorKey, Peer::id_t peerID);

    /** Called to register that a given validator was squelched for a given
     * peer. It is expected that this method is called by SquelchHandler.
     *
     * @param validatorKey Validator public key
     * @param peerID peer ID
     */
    void
    registerSquelchedValidator(
        PublicKey const& validatorKey,
        Peer::id_t peerID);

    std::atomic_bool reduceRelayReady_{false};

    // Maintain an open number of slots for trusted validators to reduce
    // duplicate traffic from trusted validators.
    slots_map trustedSlots_;

    // Maintain slots for untrusted validators to reduce duplicate traffic from
    // untrusted validators. If enhanced squelching is enabled, the number of
    // untrustedSlots_ is capped at reduce_relay::MAX_UNTRUSTED_SLOTS.
    // Otherwise, there is no limit.
    slots_map untrustedSlots_;

    SquelchHandler& handler_;  // squelch/unsquelch handler
    Logs& logs_;
    beast::Journal const journal_;

    bool const baseSquelchEnabled_;
    uint16_t const maxSelectedPeers_;
    bool const enhancedSquelchEnabled_;

    clock_type& clock_;

    // Maintain aged container of message/peers. This is required
    // to discard duplicate message from the same peer. A message
    // is aged after IDLED seconds. A message received IDLED seconds
    // after it was relayed is ignored by PeerImp.
    messages peersWithMessage_;

    // Maintain aged container of validator/peers. This is used to track
    // which validator/peer were squelced. A peer that whose squelch
    // has expired is removed.
    validators peersWithSquelchedValidators_;

    struct ValidatorInfo
    {
        size_t count;  // the number of messages sent from this validator
        time_point lastMessage;                // timestamp of the last message
        std::unordered_set<Peer::id_t> peers;  // a list of peer IDs that sent a
                                               // message for this validator
    };

    // Untrusted validators considered for open untrusted slots
    hash_map<PublicKey, ValidatorInfo> consideredValidators_;
};

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
