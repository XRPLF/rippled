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
 * @brief Manages the set of peers relaying messages for a single validator.
 *
 * @details A Slot represents a single validator and tracks all peers that
 * forward messages from that validator. It implements a state machine to
 * observe peer behavior, with the goal of selecting a small, optimal set of
 * peers to serve as the primary source for that validator's messages.
 *
 * The class operates in two main states:
 * - **Counting**: It gathers statistics on message delivery from all peers.
 * - **Selected**: After sufficient data is gathered, it selects a small
 * number of the best-performing peers and "squelches" (temporarily
 * suppresses) the rest to reduce redundant traffic.
 *
 * The Slot dynamically handles peer disconnections and idleness, resetting
 * its state as needed to maintain a reliable set of message sources.
 * Instances of this class are created and managed by the `Slots` class.
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

    /** Get all peers of the slot. */
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

    /**
     * @brief Processes a message from a peer and updates the slot's state.
     *
     * @details This is the core logic method for the Slot. It is called each
     * time a message from the validator is received via a peer. The function
     * manages the lifecycle of the slot and its peers, transitioning between
     * counting messages and selecting the best peers.
     *
     * The logic proceeds as follows:
     * 1. Peer Initialization: If the message is from a new peer, or a peer
     * whose squelch has expired, the peer is initialized in the `Counting`
     * state. If this action occurs, the entire slot may be reset to
     * `Counting`.
     * 2. Message Counting: If the slot is in the `Counting` state, the
     * message count for the reporting peer is incremented.
     * 3. Peer Consideration: Once a peer's message count exceeds
     * `MIN_MESSAGE_THRESHOLD`, it is added to a pool of `considered` peers.
     * 4. Selection Trigger: When the number of peers reaching
     * `MAX_MESSAGE_THRESHOLD` equals `maxSelectedPeers_`, the selection
     * process is triggered:
     * - A random subset of `maxSelectedPeers_` is chosen from the
     * `considered` pool.
     * - All other `considered` peers that were not selected are squelched
     * via the `SquelchHandler`.
     * - The slot's state transitions to `Selected`.
     * - All peer message counts are reset for the next counting round.
     * 5. Selected State: While the slot is in the `Selected` state,
     * message counts are not updated.
     *
     * @param validator The public key of the validator this slot represents.
     * @param id The ID of the peer that relayed the message.
     * @param callback A function to call if a squelch action is intentionally
     * ignored during the process.
     */
    void
    update(
        PublicKey const& validator,
        Peer::id_t id,
        ignored_squelch_callback callback);

    /**
     * @brief Handles the removal of a peer from the slot.
     *
     * @details This function is called when a peer disconnects or is
     * temporarily idled. Its primary role is to clean up the peer's state and
     * ensure the slot remains in a consistent state.
     *
     * A critical side-effect occurs if the slot is in the `Selected` state
     * when a peer is removed: the disconnection invalidates the previous
     * selection round. In this case, the function will:
     * 1. Unsquelch all currently squelched peers in the slot.
     * 2. Reset the state of all remaining peers to `Counting`.
     * 3. Switch the slot's overall state back to `Counting` to begin a
     * new evaluation round.
     *
     * @param validator The public key of the validator this slot represents.
     * @param id The ID of the peer to be removed.
     * @param erase If `true`, the peer's data is permanently erased from the
     * slot (on disconnect). If `false`, the peer is simply marked
     * as inactive and its data is reset.
     */

    void
    deletePeer(PublicKey const& validator, Peer::id_t id, bool erase);

    /** Get the time of the last peer selection round */
    time_point const&
    getLastSelected() const
    {
        return lastSelected_;
    }

    /**
     * @brief Scans for and handles peers that have stopped relaying messages.
     *
     * @details This function is a maintenance routine that identifies inactive
     * peers by checking if they have been silent for a significant period.
     *
     * The most critical case this function handles is when one of the currently
     * `Selected` peers becomes idle. This indicates a potential interruption in
     * message flow from the validator. To ensure reliability, this condition
     * triggers a full reset of the slot:
     * 1. The `unsquelch` handler is called for all currently squelched peers.
     * 2. The slot's state is switched back to `Counting` to start a new
     * peer evaluation and selection round.
     *
     * The idle peer itself is then removed from the slot's tracking.
     *
     * @param validator The public key of the validator this slot represents,
     * used for logging and context.
     */
    void
    deleteIdlePeer(PublicKey const& validator);

    /**
     * @brief Calculates an appropriate, randomized squelch duration.
     *
     * @details The squelch duration is designed to prevent message
     * flood that could occur if many peers were unsquelched simultaneously.
     * The duration is a random value calculated within a dynamic range:
     *
     * - Lower Bound: A fixed minimum (`MIN_UNSQUELCH_EXPIRE`).
     * - Upper Bound: This is calculated adaptively:
     * - It starts at a default value (`MAX_UNSQUELCH_EXPIRE_DEFAULT`).
     * - It increases proportionally with the number of peers being
     * squelched (`SQUELCH_PER_PEER * npeers`).
     * - It is capped by an absolute maximum (`MAX_UNSQUELCH_EXPIRE_PEERS`)
     * to prevent excessively long squelch times.
     *
     * This ensures that squelch times are longer when more peers are involved,
     * staggering their return to an active state.
     *
     * @param npeers The number of peers that are candidates for being
     * squelched.
     * @return A randomized `std::chrono::seconds` duration within the
     * calculated range.
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

    // Holds state information for each peer tracked by this slot.
    std::unordered_map<Peer::id_t, PeerInfo> peers_;

    // A pool of peers that have passed the minimum message threshold and are
    // candidates for selection.
    std::unordered_set<Peer::id_t> considered_;

    // A counter of peers that have reached the max message threshold.
    std::uint16_t reachedThreshold_;

    // The timestamp of the last peer selection, used to determine when the
    // current selection has become stale.
    time_point lastSelected_;

    // The current state of the slot.
    SlotState state_;

    // A reference to an external handler that executes squelch/unsquelch
    // operations on peers.
    SquelchHandler const& handler_;

    // The logging interface used by this slot.
    beast::Journal const journal_;

    // The number of peers to select as a message sources during a selection
    // round.
    uint16_t const maxSelectedPeers_;

    // A flag indicating if the validator for this slot is trusted.
    bool const isTrusted_;

    // A reference to the clock used for all time-based operations, allowing
    // for deterministic testing.
    clock_type& clock_;
};

/**
 * @brief A container that manages `Slot` instances for all validators.
 *
 * @details This class acts as the central orchestrator for the validator
 * message squelching feature. It maintains two distinct collections of
 * `Slot` objects: one for trusted validators and one for untrusted
 * validators.
 *
 * Its primary responsibilities include:
 * - Slot Lifecycle: Creating, updating, and destroying `Slot` instances
 * as validators become active or inactive.
 * - Message Dispatching: Receiving notifications of new messages and
 * dispatching them to the appropriate `Slot` for processing.
 * - Peer Management: Handling peer connection and disconnection events
 * at a global level and propagating these changes to all relevant slots.
 * - Maintenance: Clean up of idle peers and stale slots.
 * - Feature Toggling: Checking if the base and enhanced squelching
 * features are enabled and ready for use.
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
     * @brief Constructor.
     *
     * @param logs A reference to the logger, used to create a dedicated journal
     * for this class.
     * @param handler A reference to the squelch handler implementation that
     * will be used to squelch or unsquelch peers.
     * @param config A reference to the configuration, used to
     * determine if squelching features are enabled and to set
     * operational parameters like the number of selected peers.
     * @param clock A reference to a clock, used for all time-sensitive
     * operations, including aging out old data and managing
     * squelch durations.
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

    /**
     * @brief Checks if the base message relay reduction feature is active.
     *
     * @details The feature is considered active only if it is both enabled in
     * the configuration and the initial boot-up delay (`reduceRelayReady`) has
     * passed.
     *
     * @return `true` if base squelching can be performed.
     */
    bool
    baseSquelchReady()
    {
        return baseSquelchEnabled_ && reduceRelayReady();
    }

    /**
     * @brief Checks if the enhanced message relay reduction feature is active.
     *
     * @details The feature is considered active only if it is both enabled in
     * the configuration and the initial boot-up delay (`reduceRelayReady`) has
     * passed.
     *
     * @return `true` if enhanced squelching can be performed.
     */
    bool
    enhancedSquelchReady()
    {
        return enhancedSquelchEnabled_ && reduceRelayReady();
    }

    /**
     * @brief Determines if the initial boot-up delay for relay reduction has
     * passed.
     *
     * @details This function acts as a safety mechanism to prevent squelching
     * peers immediately on server startup, allowing time for the node to
     * establish a stable view of the network.
     *
     * @return `true` if the `reduce_relay::WAIT_ON_BOOTUP` duration has
     * elapsed.
     */
    bool
    reduceRelayReady();

    /**
     * @brief Processes a message from an untrusted validator with a no-op
     * callback.
     *
     * @details This is a convenience overload that calls the main
     * `updateUntrustedValidatorSlot` function with a default, empty callback.
     *
     * @note This should only be called for untrusted validators. The caller is
     * responsible for ensuring message uniqueness.
     *
     * @param key The unique hash of the message.
     * @param validator The public key of the untrusted validator.
     * @param id The ID of the peer that relayed the message.
     */
    void
    updateUntrustedValidatorSlot(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id)
    {
        updateUntrustedValidatorSlot(key, validator, id, []() {});
    }

    /**
     * @brief Manages slot admission and squelching for untrusted validators.
     *
     * @details This function acts as the gatekeeper for untrusted validators,
     * deciding whether to grant them one of the limited available monitoring
     * slots, squelch them, or ignore their messages. The logic proceeds
     * through a series of checks:
     *
     * 1. Ignore if Already Selected: If the validator already has an
     * active slot, it's in the `Selected` state, and this message is ignored.
     * 2. Re-Squelch if Globally Squelched: If the validator is known to be
     * squelched but a message is received from a peer, it implies the
     * peer may have missed the original request. The function re-sends a
     * squelch command to that specific peer.
     * 3. Squelch if at Capacity: If all untrusted slots are full, the
     * function squelches the new validator across all peers to prevent it
     * from propagating further.
     * 4. Consider for a New Slot: If the validator passes all checks and
     * there is capacity, it is added to a pool of "considered"
     * validators. Once it meets certain criteria (managed by
     * `updateConsideredValidator`), a new slot is created for it, and it
     * begins the standard counting/selection process.
     *
     * @note This should only be called for untrusted validators. The caller is
     * responsible for ensuring that each message is unique per peer.
     *
     * @param key The unique hash of the message.
     * @param validator The public key of the untrusted validator.
     * @param id The ID of the peer that relayed the message.
     * @param callback A function to call if the slot intentionally ignores a
     * squelch action (not used in this specific logic path).
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

    /**
     * @brief Processes a message and updates the corresponding validator slot.
     *
     * @details This function acts as a dispatcher, routing a received message
     * to the appropriate validator slot for processing. It first ensures the
     * message is not a duplicate from the same peer. The subsequent logic
     * depends on the validator's trust status and the currently enabled
     * squelching mode.
     *
     * - For Trusted Validators (or when Enhanced Squelching is disabled):
     * The function will find or create a slot for the validator in the
     * appropriate map (`trustedSlots_` or `untrustedSlots_`). It then calls
     * the slot's `update` method to count the message, which contributes to
     * the peer selection process.
     *
     * - For Untrusted Validators (when Enhanced Squelching is enabled):
     * The function only acts if a slot has already been explicitly allocated
     * for the validator. If no slot exists (i.e., the validator is not one
     * of the actively monitored untrusted validators), the message is ignored,
     * as another process is responsible for managing and squelching it.
     *
     * @param key The unique hash of the message, used for deduplication.
     * @param validator The public key of the validator.
     * @param id The ID of the peer that relayed the message.
     * @param callback A function to call if the slot intentionally ignores a
     * squelch action.
     * @param isTrusted `true` if the message is from a trusted validator.
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id,
        typename Slot::ignored_squelch_callback callback,
        bool isTrusted);

    /**
     * @brief Forcefully squelches an untrusted validator and removes it from
     * active monitoring.
     *
     * @details This function takes immediate action to stop all message flow
     * from a specific untrusted validator. It performs two key operations in
     * order:
     * 1. It first broadcasts a squelch command to all connected peers via the
     * `SquelchHandler`, ensuring suppression before altering internal state.
     * 2. It then removes the validator from any internal tracking, both from
     * the pool of candidates (`consideredValidators_`) and from the map of
     * active monitoring slots (`untrustedSlots_`), if present.
     *
     * This ensures the validator is completely silenced and will not be
     * reconsidered for a slot until it expires from the various squelch lists.
     *
     * @param validator The public key of the untrusted validator to squelch.
     */
    void
    squelchUntrustedValidator(PublicKey const& validatorKey);

    /**
     * @brief Performs cleanup of idle peers, stale slots, and unviable
     * validator candidates.
     *
     * @details This function is responsible for the health of the entire slot
     * system. It executes three distinct cleanup phases:
     *
     * 1. Delegated Peer Cleanup: It iterates through every active slot
     * (both trusted and untrusted) and calls `Slot::deleteIdlePeer`. This
     * allows each slot to manage its own set of peers that have stopped
     * relaying messages.
     *
     * 2. Stale Slot Removal: It then identifies and removes entire slots
     * that have become stale. A slot is considered stale if it has been too
     * long since its last peer selection, or if it's an untrusted slot that no
     * longer has enough participating peers. When an untrusted validator's
     * slot is removed for staleness, the validator is globally squelched to
     * prevent it from immediately re-entering the selection process.
     *
     * 3. Candidate Pool Pruning: Finally, it cleans the pool of untrusted
     * validators being considered for new slots. Any validator that no
     * longer meets the criteria to be a candidate is removed and globally
     * squelched.
     */
    void
    deleteIdlePeers();

    /**
     * @brief Propagates a peer deletion event to all active slots.
     *
     * @details The function iterates through both the trusted and untrusted
     * slot collections and delegates the actual cleanup logic to the
     * `deletePeer` method of each individual slot. This allows each slot to
     * correctly update its internal state, which is especially critical if the
     * removed peer was one of its selected message sources.
     *
     * @param id The ID of the peer that has been deleted.
     * @param erase If `true`, instructs the slot to permanently erase the
     * peer's data.
     */
    void
    deletePeer(Peer::id_t id, bool erase);

    void
    onWrite(beast::PropertyStream::Map& stream) const;

protected:
    /**
     * @brief Records a message from a peer and checks for duplicates.
     *
     * @details This function serves as a deduplication filter to prevent
     * processing the same message from the same peer multiple times. It
     * maintains an aged map (`peersWithMessage_`) that tracks which peers have
     * relayed which messages.
     *
     * Before performing its check, it first expires any old message records
     * from its cache. It then determines if the given message `key` has already
     * been recorded from the specific peer `id`.
     *
     * @param key The unique hash of the message.
     * @param id The ID of the peer that relayed the message.
     * @return `false` if this exact message has already been received from this
     * exact peer. Returns `true` in all other cases (e.g., a new
     * message, or a new peer for an existing message), indicating the
     * message should be processed further.
     */
    bool
    addPeerMessage(uint256 const& key, Peer::id_t id);

    /**
     * @brief Tracks and evaluates an untrusted validator to see if it qualifies
     * for a slot.
     *
     * @details This function manages a pool of untrusted validators that are
     * candidates for being assigned a full slot. When a message from a new
     * validator is seen, it's added to the pool. For existing candidates, it
     * increments their message count and tracks the peers relaying their
     * messages.
     *
     * A validator "graduates" from this consideration pool once its message
     * count reaches `reduce_relay::MAX_MESSAGE_THRESHOLD`. Upon graduation,
     * it is removed from the pool and its public key is returned to the
     * caller, signaling that it is now eligible to be assigned a `Slot`.
     *
     * @param validator The public key of the validator being considered.
     * @param peer The ID of the peer that relayed the message from the
     * validator.
     * @return The validator's public key if it has met the selection criteria,
     * otherwise `std::nullopt`.
     */
    std::optional<PublicKey>
    updateConsideredValidator(PublicKey const& validator, Peer::id_t peer);

    /**
     * @brief Prunes the pool of untrusted validators being considered for
     * slots.
     *
     * @details This function iterates through the candidates in the
     * `consideredValidators_` pool and performs cleanup based on how long
     * they have been inactive.
     *
     * - If a validator has been idle for a short duration (greater than
     * `PEER_IDLED`), its progress is reset, giving it a chance to be
     * reconsidered without being fully removed.
     *
     * - If a validator has been idle for a prolonged duration (greater than
     * `MAX_UNTRUSTED_VALIDATOR_IDLE`), it is deemed stale, removed from the
     * consideration pool, and its public key is returned.
     *
     * @return A vector of public keys for validators that were removed due to
     * prolonged inactivity and should now be globally squelched.
     */
    std::vector<PublicKey>
    cleanConsideredValidators();

    /**
     * @brief Checks if a validator is currently squelched for any peer.
     *
     * @details This function first purges any squelch records that have aged
     * past the default expiration time. It then checks if there are any
     * remaining squelch records associated with the given validator.
     *
     * @param validatorKey The public key of the validator to check.
     * @return `true` if the validator is still squelched for at least one
     * peer, `false` otherwise.
     */
    bool
    expireAndIsValidatorSquelched(PublicKey const& validatorKey);

    /**
     * @brief Checks if a specific peer is currently squelched for a validator.
     *
     * @details This function first purges any squelch records that have aged
     * past the default expiration time. It then performs a targeted check to
     * see if a squelch record exists for the specific validator/peer pair.
     *
     * @param validatorKey The public key of the validator.
     * @param peerID The ID of the peer to check.
     * @return `true` if the specified peer is currently squelched for the
     * given validator, `false` otherwise.
     */
    bool
    expireAndIsPeerSquelched(PublicKey const& validatorKey, Peer::id_t peerID);

    /**
     * @brief Records that a specific peer has been squelched for a validator.
     *
     * @details This function is typically called by the `SquelchHandler` to
     * update the central tracking map (`peersWithSquelchedValidators_`). It
     * creates a record indicating that a particular peer should not relay
     * messages from the specified validator.
     *
     * @param validatorKey The public key of the validator being squelched.
     * @param peerID The ID of the peer for which the squelch applies.
     */
    void
    registerSquelchedValidator(
        PublicKey const& validatorKey,
        Peer::id_t peerID);

    /**
     * @brief A flag indicating if the initial boot-up delay has passed.
     * @details This is used as a safety mechanism to prevent squelching peers
     * immediately on server startup, allowing time for the node to
     * establish a stable view of the network.
     */
    std::atomic_bool reduceRelayReady_{false};

    /**
     * @brief A map holding the active monitoring slots for trusted validators.
     * @details Each `Slot` in this map manages the peers for a single trusted
     * validator. There is no limit to the number of trusted slots.
     */
    slots_map trustedSlots_;

    /**
     * @brief A map holding the active monitoring slots for untrusted
     * validators.
     * @details When enhanced squelching is enabled, this map is capped at a
     * fixed size (`reduce_relay::MAX_UNTRUSTED_SLOTS`). Validators must
     * qualify through the `consideredValidators_` pool to be granted a slot.
     */
    slots_map untrustedSlots_;

    /**
     * @brief A reference to the handler that executes squelch/unsquelch
     * commands.
     */
    SquelchHandler& handler_;

    /**
     * @brief A reference to the application's logging infrastructure.
     */
    Logs& logs_;

    /**
     * @brief A dedicated journal for logging events specific to the Slots
     * class.
     */
    beast::Journal const journal_;

    /**
     * @brief A configuration flag that enables the base message squelching
     * feature.
     */
    bool const baseSquelchEnabled_;

    /**
     * @brief The target number of peers to select as primary sources in each
     * slot.
     */
    uint16_t const maxSelectedPeers_;

    /**
     * @brief A configuration flag that enables the enhanced squelching feature,
     * which includes limiting the number of untrusted slots.
     */
    bool const enhancedSquelchEnabled_;

    /**
     * @brief A reference to the clock used for all time-based operations.
     */
    clock_type& clock_;

    /**
     * @brief An aged cache to track recently seen messages for deduplication.
     * @details This prevents processing the same message from the same peer
     * more than once within a short time frame.
     */
    messages peersWithMessage_;

    /**
     * @brief An aged cache that tracks which peers are actively squelched for
     * which validators.
     * @details This serves as the central record for active squelches, which is
     * checked before processing messages and cleaned periodically.
     */
    validators peersWithSquelchedValidators_;

    /**
     * @brief A temporary data structure holding statistics for a validator
     * being considered for an untrusted slot.
     */
    struct ValidatorInfo
    {
        // The number of messages received from this validator.
        size_t count;
        // The timestamp of the last message received.
        time_point lastMessage;
        // The set of unique peers that have relayed messages for this
        // validator.
        std::unordered_set<Peer::id_t> peers;

        // Resets the validator's progress.
        void
        reset()
        {
            count = 0;
            peers.clear();
        }
    };

    /**
     * @brief A pool of untrusted validators that are candidates for a full
     * slot.
     * @details Before an untrusted validator is allocated one of the limited
     * `untrustedSlots_`, it is monitored here until it meets the criteria
     * for graduation (e.g., message count).
     */
    hash_map<PublicKey, ValidatorInfo> consideredValidators_;
};

}  // namespace reduce_relay

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_SLOT_H_INCLUDED
