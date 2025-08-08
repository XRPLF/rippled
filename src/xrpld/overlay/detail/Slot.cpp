//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/Slot.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/container/aged_unordered_map.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace ripple {
namespace reduce_relay {

void
Slot::deleteIdlePeer(PublicKey const& validator)
{
    using namespace std::chrono;
    auto const now = clock_.now();
    for (auto it = peers_.begin(); it != peers_.end();)
    {
        auto const& peer = it->second;
        auto const id = it->first;
        ++it;
        if (now - peer.lastMessage > reduce_relay::PEER_IDLED)
        {
            JLOG(journal_.trace())
                << "deleteIdlePeer: deleting idle peer "
                << formatLogMessage(validator, id)
                << " peer_state: " << to_string(peer.state)
                << " idle for: " << (now - peer.lastMessage).count();
            deletePeer(validator, id, false);
        }
    }
}

void
Slot::update(
    PublicKey const& validator,
    Peer::id_t id,
    ignored_squelch_callback report)
{
    using namespace std::chrono;
    auto const now = clock_.now();
    auto const it = peers_.find(id);

    // First message from this peer
    if (it == peers_.end())
    {
        JLOG(journal_.trace())
            << "update: adding new slot" << formatLogMessage(validator, id);
        peers_.emplace(std::make_pair(
            id,
            PeerInfo{
                .state = PeerState::Counting,
                .count = 0,
                .expire = now,
                .lastMessage = now,
                .timesSelected = 0}));
        initCounting();
        return;
    }
    // Message from a peer with expired squelch
    if (it->second.state == PeerState::Squelched && now > it->second.expire)
    {
        JLOG(journal_.trace())
            << "update: squelch expired" << formatLogMessage(validator, id);
        it->second.state = PeerState::Counting;
        it->second.lastMessage = now;
        initCounting();
        return;
    }

    auto& peer = it->second;

    peer.lastMessage = now;

    // report if we received a message from a squelched peer
    if (peer.state == PeerState::Squelched)
        report();

    if (getState() != SlotState::Counting || peer.state == PeerState::Squelched)
        return;

    if (++peer.count > reduce_relay::MIN_MESSAGE_THRESHOLD)
        considered_.insert(id);
    if (peer.count == (reduce_relay::MAX_MESSAGE_THRESHOLD + 1))
        ++reachedThreshold_;

    if (now - lastSelected_ > 2 * reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT)
    {
        JLOG(journal_.warn())
            << "update: resetting due to inactivity"
            << formatLogMessage(validator, id) << " inactive for: "
            << duration_cast<seconds>(now - lastSelected_).count();
        initCounting();
        return;
    }

    if (reachedThreshold_ == maxSelectedPeers_)
    {
        // Randomly select maxSelectedPeers_ peers from considered.
        // Exclude peers that have been idling > IDLED -
        // it's possible that deleteIdlePeer() has not been called yet.
        // If number of remaining peers != maxSelectedPeers_
        // then reset the Counting state and let deleteIdlePeer() handle
        // idled peers.
        std::unordered_set<Peer::id_t> selected;
        std::stringstream str;
        while (selected.size() != maxSelectedPeers_ && considered_.size() != 0)
        {
            auto const i =
                considered_.size() == 1 ? 0 : rand_int(considered_.size() - 1);
            auto const it = std::next(considered_.begin(), i);
            auto const id = *it;
            considered_.erase(it);

            auto const& peersIt = peers_.find(id);
            if (peersIt == peers_.end())
            {
                JLOG(journal_.error()) << "update: peer not found"
                                       << formatLogMessage(validator, id);

                continue;
            }
            if (now - peersIt->second.lastMessage < reduce_relay::PEER_IDLED)
            {
                selected.insert(id);
                str << id << " ";
            }
        }

        if (selected.size() != maxSelectedPeers_)
        {
            JLOG(journal_.error()) << "update: selection failed"
                                   << formatLogMessage(validator, std::nullopt);

            initCounting();
            return;
        }

        lastSelected_ = now;

        JLOG(journal_.trace()) << "update: selected peers "
                               << formatLogMessage(validator, std::nullopt)
                               << " peers: " << str.str();

        XRPL_ASSERT(
            peers_.size() >= maxSelectedPeers_,
            "ripple::reduce_relay::Slot::update : minimum peers");

        // squelch peers which are not selected and
        // not already squelched
        str.clear();
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
                if (journal_.trace())
                    str << k << " ";
                v.state = PeerState::Squelched;
                std::chrono::seconds duration =
                    getSquelchDuration(peers_.size() - maxSelectedPeers_);
                v.expire = now + duration;
                handler_.squelch(validator, k, duration.count());
            }
        }
        JLOG(journal_.trace()) << "update: squelched peers "
                               << formatLogMessage(validator, std::nullopt)
                               << " peers: " << str.str();
        considered_.clear();
        reachedThreshold_ = 0;
        state_ = SlotState::Selected;
    }
}

std::chrono::seconds
Slot::getSquelchDuration(std::size_t npeers) const
{
    using namespace std::chrono;
    auto m = std::max(
        reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT,
        seconds{reduce_relay::SQUELCH_PER_PEER * npeers});
    if (m > reduce_relay::MAX_UNSQUELCH_EXPIRE_PEERS)
    {
        m = reduce_relay::MAX_UNSQUELCH_EXPIRE_PEERS;
        JLOG(journal_.warn())
            << "getSquelchDuration: unexpected squelch duration " << npeers;
    }
    return seconds{
        ripple::rand_int(reduce_relay::MIN_UNSQUELCH_EXPIRE / 1s, m / 1s)};
}

void
Slot::deletePeer(PublicKey const& validator, Peer::id_t id, bool erase)
{
    auto it = peers_.find(id);
    if (it == peers_.end())
        return;

    std::vector<Peer::id_t> toUnsquelch;
    auto const now = clock_.now();
    if (it->second.state == PeerState::Selected)
    {
        JLOG(journal_.debug())
            << "deletePeer: unsquelching selected peer "
            << formatLogMessage(validator, id)
            << " peer_state: " << to_string(it->second.state)
            << " considered: " << (considered_.find(id) != considered_.end())
            << " erase: " << erase;

        for (auto& [k, v] : peers_)
        {
            if (v.state == PeerState::Squelched)
                toUnsquelch.push_back(k);
            v.state = PeerState::Counting;
            v.count = 0;
            v.expire = now;
        }

        considered_.clear();
        reachedThreshold_ = 0;
        state_ = SlotState::Counting;
    }
    else if (considered_.contains(id))
    {
        if (it->second.count > reduce_relay::MAX_MESSAGE_THRESHOLD)
            --reachedThreshold_;
        considered_.erase(id);
    }

    it->second.lastMessage = now;
    it->second.count = 0;

    if (erase)
        peers_.erase(it);
    
    // Must be after peers_.erase(it)
    for (auto const& k : toUnsquelch)
        handler_.unsquelch(validator, k);
}

void
Slot::onWrite(beast::PropertyStream::Map& stream) const
{
    auto const now = clock_.now();
    stream["state"] = to_string(getState());
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
        item["state"] = to_string(info.state);
    }
}

void
Slot::initCounting()
{
    state_ = SlotState::Counting;
    considered_.clear();
    reachedThreshold_ = 0;
    for (auto& [_, peer] : peers_)
    {
        (void)_;
        peer.count = 0;
    }
}

std::string
Slot::formatLogMessage(PublicKey const& validator, std::optional<Peer::id_t> id)
    const
{
    std::stringstream ss;
    ss << "validator: " << toBase58(TokenType::NodePublic, validator);
    if (id)
        ss << " peer: " << *id;
    ss << " trusted: " << isTrusted_;
    ss << " slot_state: " << to_string(getState());
    return ss.str();
}

// --------------------------------- Slots --------------------------------- //

bool
Slots::reduceRelayReady()
{
    if (!reduceRelayReady_)
        reduceRelayReady_ =
            std::chrono::duration_cast<std::chrono::minutes>(
                clock_.now().time_since_epoch()) > reduce_relay::WAIT_ON_BOOTUP;

    return reduceRelayReady_;
}

void
Slots::registerSquelchedValidator(
    PublicKey const& validatorKey,
    Peer::id_t peerID)
{
    peersWithSquelchedValidators_[validatorKey].insert(peerID);
}

bool
Slots::expireAndIsValidatorSquelched(PublicKey const& validatorKey)
{
    beast::expire(
        peersWithSquelchedValidators_,
        reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT);

    return peersWithSquelchedValidators_.find(validatorKey) !=
        peersWithSquelchedValidators_.end();
}

bool
Slots::expireAndIsPeerSquelched(
    PublicKey const& validatorKey,
    Peer::id_t peerID)
{
    beast::expire(
        peersWithSquelchedValidators_,
        reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT);

    auto const it = peersWithSquelchedValidators_.find(validatorKey);

    // if validator was not squelched, the peer was also not squelched
    if (it == peersWithSquelchedValidators_.end())
        return false;

    // if a peer is found the squelch for it has not expired
    return it->second.find(peerID) != it->second.end();
}

bool
Slots::expireAndIsPeerMessageCached(uint256 const& key, Peer::id_t id)
{
    beast::expire(peersWithMessage_, reduce_relay::PEER_IDLED);

    // return false if the ID was not inserted
    if (key.isNonZero())
        return !peersWithMessage_[key].insert(id).second;

    return false;
}

void
Slots::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t id,
    typename Slot::ignored_squelch_callback report,
    bool isTrusted)
{
    if (expireAndIsPeerMessageCached(key, id))
        return;

    // If we receive a message from a trusted validator either update an
    // existing slot or insert a new one. If we are not running enhanced
    // squelching also deduplicate untrusted validator messages
    if (isTrusted || !enhancedSquelchEnabled_)
    {
        // if enhanced squelching is disabled, keep untrusted validator slots
        // separately from trusted ones
        auto it = (isTrusted ? trustedSlots_ : untrustedSlots_)
                      .emplace(std::make_pair(
                          validator,
                          Slot(
                              handler_,
                              logs_.journal("Slot"),
                              maxSelectedPeers_,
                              isTrusted,
                              clock_)))
                      .first;

        it->second.update(validator, id, report);
    }
    else
    {
        auto it = untrustedSlots_.find(validator);
        // If we received a message from a validator that is not
        // selected, and is not squelched, there is nothing to do. It
        // will be squelched later when `updateValidatorSlot` is called.
        if (it == untrustedSlots_.end())
            return;

        it->second.update(validator, id, report);
    }
}

void
Slots::updateUntrustedValidatorSlot(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t id,
    typename Slot::ignored_squelch_callback report)
{
    // We received a message from an already selected validator
    // we can ignore this message
    if (untrustedSlots_.find(validator) != untrustedSlots_.end())
        return;

    // Did we receive a message from an already squelched validator?
    // This could happen in few cases:
    //      1. It happened so that the squelch for a particular peer expired
    //      before our local squelch.
    //      2. We receive a message from a new peer that did not receive the
    //      squelch request.
    //      3. The peer is ignoring our squelch request and we have not sent
    //      the control message in a while.
    // In all of these cases we can only send them a squelch request again.
    if (expireAndIsValidatorSquelched(validator))
    {
        if (!expireAndIsPeerSquelched(validator, id))
        {
            JLOG(journal_.debug())
                << "updateUntrustedValidatorSlot: received a message from a "
                   "squelched validator "
                << "validator: " << toBase58(TokenType::NodePublic, validator)
                << " peer: " << id;

            registerSquelchedValidator(validator, id);
            handler_.squelch(
                validator,
                id,
                reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
        }
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
    if (untrustedSlots_.size() == reduce_relay::MAX_UNTRUSTED_SLOTS)
    {
        JLOG(journal_.debug())
            << "updateUntrustedValidatorSlot: slots full squelching validator "
            << "validator: " << toBase58(TokenType::NodePublic, validator);

        handler_.squelchAll(
            validator,
            MAX_UNSQUELCH_EXPIRE_DEFAULT.count(),
            [&](Peer::id_t id) { registerSquelchedValidator(validator, id); });
        return;
    }

    if (auto const v = updateConsideredValidator(validator, id))
    {
        JLOG(journal_.debug())
            << "updateUntrustedValidatorSlot: selected untrusted validator "
            << "validator: " << toBase58(TokenType::NodePublic, *v);

        untrustedSlots_.emplace(std::make_pair(
            *v,
            Slot(
                handler_,
                logs_.journal("Slot"),
                maxSelectedPeers_,
                false,
                clock_)));
    }
    // When we reach MAX_UNTRUSTED_SLOTS, don't  explicitly clean them.
    // Since we stop updating their counters, they will idle, and will be
    // removed and squelched.
}

std::optional<PublicKey>
Slots::updateConsideredValidator(PublicKey const& validator, Peer::id_t peer)
{
    auto const now = clock_.now();

    auto it = consideredValidators_.find(validator);
    if (it == consideredValidators_.end())
    {
        consideredValidators_.emplace(std::make_pair(
            validator,
            ValidatorInfo{
                .count = 1,
                .lastMessage = now,
                .peers = {peer},
            }));

        return std::nullopt;
    }

    it->second.peers.insert(peer);
    it->second.lastMessage = now;
    ++it->second.count;

    // if the validator has not met selection criteria yet
    if (it->second.count < reduce_relay::MAX_MESSAGE_THRESHOLD)
        return std::nullopt;

    auto const key = it->first;
    consideredValidators_.erase(it);

    return key;
}

void
Slots::squelchUntrustedValidator(PublicKey const& validator)
{
    JLOG(journal_.info())
        << "squelchUntrustedValidator: squelching untrusted validator: "
        << toBase58(TokenType::NodePublic, validator);
    // to prevent the validator from being reinserted squelch the validator
    // before removing the validator from consideration and slots
    handler_.squelchAll(
        validator, MAX_UNSQUELCH_EXPIRE_DEFAULT.count(), [&](Peer::id_t id) {
            registerSquelchedValidator(validator, id);
        });

    consideredValidators_.erase(validator);
    untrustedSlots_.erase(validator);
}

void
Slots::deletePeer(Peer::id_t id, bool erase)
{
    auto const f = [&](slots_map& slots) {
        for (auto& [validator, slot] : slots)
            slot.deletePeer(validator, id, erase);
    };

    f(trustedSlots_);
    f(untrustedSlots_);
}

void
Slots::deleteIdlePeers()
{
    auto const f = [&](slots_map& slots) {
        auto const now = clock_.now();

        for (auto it = slots.begin(); it != slots.end();)
        {
            auto const& validator = it->first;
            auto& slot = it->second;
            slot.deleteIdlePeer(validator);

            // delete the slot if the untrusted slot no longer meets the
            // selection critera or it has not been selected for a while
            if ((!slot.isTrusted_ &&
                 slot.getPeers().size() < maxSelectedPeers_) ||
                now - it->second.getLastSelected() >
                    reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT)
            {
                JLOG(journal_.trace())
                    << "deleteIdlePeers: deleting "
                    << (slot.isTrusted_ ? "trusted" : "untrusted") << " slot "
                    << toBase58(TokenType::NodePublic, it->first) << " reason: "
                    << (now - it->second.getLastSelected() >
                                reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT
                            ? " inactive "
                            : " insufficient peers");

                // if an untrusted validator slot idled - peers stopped
                // sending messages for this validator squelch it
                if (!it->second.isTrusted_)
                    handler_.squelchAll(
                        it->first,
                        reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT.count(),
                        [&](Peer::id_t id) {
                            registerSquelchedValidator(it->first, id);
                        });

                it = slots.erase(it);
            }
            else
                ++it;
        }
    };

    f(trustedSlots_);
    f(untrustedSlots_);

    // remove and squelch all validators that the selector deemed unsuitable
    // there might be some good validators in this set that "lapsed".
    // However, since these are untrusted validators we're not concerned
    for (auto const& validator : cleanConsideredValidators())
        handler_.squelchAll(
            validator,
            reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT.count(),
            [&](Peer::id_t id) { registerSquelchedValidator(validator, id); });
}

std::vector<PublicKey>
Slots::cleanConsideredValidators()
{
    auto const now = clock_.now();

    std::vector<PublicKey> keys;
    std::stringstream ss;
    for (auto it = consideredValidators_.begin();
         it != consideredValidators_.end();)
    {
        if (now - it->second.lastMessage >
            reduce_relay::MAX_UNTRUSTED_VALIDATOR_IDLE)
        {
            keys.push_back(it->first);
            ss << " " << toBase58(TokenType::NodePublic, it->first);
            it = consideredValidators_.erase(it);
        }
        // Due to some reason the validator idled, reset their progress
        else if (now - it->second.lastMessage > reduce_relay::PEER_IDLED)
        {
            it->second.reset();
            ++it;
        }
        else
            ++it;
    }

    if (keys.size() > 0)
    {
        JLOG(journal_.info())
            << "cleanConsideredValidators: removed considered validators "
            << ss.str();
    }
    return keys;
}

void
Slots::onWrite(beast::PropertyStream::Map& stream) const
{
    auto const writeSlot = [](beast::PropertyStream::Set& set,
                              hash_map<PublicKey, Slot> const& slots) {
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
        writeSlot(set, trustedSlots_);
    }

    {
        beast::PropertyStream::Set set("untrusted", slots);
        writeSlot(set, untrustedSlots_);
    }

    {
        beast::PropertyStream::Set set("considered", slots);

        auto const now = clock_.now();

        for (auto const& [validator, info] : consideredValidators_)
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
