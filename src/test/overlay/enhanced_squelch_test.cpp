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

#include <test/jtx/Env.h>

#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/Slot.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/SecretKey.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ripple {
namespace test {

class TestHandler : public reduce_relay::SquelchHandler
{
public:
    using squelch_method =
        std::function<void(PublicKey const&, Peer::id_t, std::uint32_t)>;
    using squelchAll_method = std::function<
        void(PublicKey const&, std::uint32_t, std::function<void(Peer::id_t)>)>;
    using unsquelch_method = std::function<void(PublicKey const&, Peer::id_t)>;

    squelch_method squelch_f_;
    squelchAll_method squelchAll_f_;
    unsquelch_method unsquelch_f_;

    TestHandler(
        squelch_method const& squelch_f,
        squelchAll_method const& squelchAll_f,
        unsquelch_method const& unsquelch_f)
        : squelch_f_(squelch_f)
        , squelchAll_f_(squelchAll_f)
        , unsquelch_f_(unsquelch_f)
    {
    }

    TestHandler(TestHandler& copy)
    {
        squelch_f_ = copy.squelch_f_;
        squelchAll_f_ = copy.squelchAll_f_;
        unsquelch_f_ = copy.unsquelch_f_;
    }

    void
    squelch(PublicKey const& validator, Peer::id_t peer, std::uint32_t duration)
        const override
    {
        squelch_f_(validator, peer, duration);
    }

    void
    squelchAll(
        PublicKey const& validator,
        std::uint32_t duration,
        std::function<void(Peer::id_t)> callback) override
    {
        squelchAll_f_(validator, duration, callback);
    }

    void
    unsquelch(PublicKey const& validator, Peer::id_t peer) const override
    {
        unsquelch_f_(validator, peer);
    }
};

class EnhancedSquelchingTestSlots : public reduce_relay::Slots
{
    using Slots = reduce_relay::Slots;

public:
    EnhancedSquelchingTestSlots(
        Logs& logs,
        reduce_relay::SquelchHandler& handler,
        Config const& config,
        reduce_relay::Slots::clock_type& clock)
        : Slots(logs, handler, config, clock)
    {
    }

    Slots::slots_map const&
    getSlots(bool trusted) const
    {
        if (trusted)
            return trustedSlots_;

        return untrustedSlots_;
    }

    hash_map<PublicKey, ValidatorInfo> const&
    getConsideredValidators()
    {
        return consideredValidators_;
    }

    std::optional<PublicKey>
    updateConsideredValidator(PublicKey const& validator, Peer::id_t peerID)
    {
        return Slots::updateConsideredValidator(validator, peerID);
    }

    void
    squelchValidator(PublicKey const& validatorKey, Peer::id_t peerID)
    {
        Slots::registerSquelchedValidator(validatorKey, peerID);
    }

    bool
    validatorSquelched(PublicKey const& validatorKey)
    {
        return Slots::expireAndIsValidatorSquelched(validatorKey);
    }

    bool
    peerSquelched(PublicKey const& validatorKey, Peer::id_t peerID)
    {
        return Slots::expireAndIsPeerSquelched(validatorKey, peerID);
    }
};

class enhanced_squelch_test : public beast::unit_test::suite
{
public:
    TestHandler::squelch_method noop_squelch =
        [&](PublicKey const&, Peer::id_t, std::uint32_t) {
            BEAST_EXPECTS(false, "unexpected call to squelch handler");
        };

    TestHandler::squelchAll_method noop_squelchAll =
        [&](PublicKey const&, std::uint32_t, std::function<void(Peer::id_t)>) {
            BEAST_EXPECTS(false, "unexpected call to squelchAll handler");
        };

    TestHandler::unsquelch_method noop_unsquelch = [&](PublicKey const&,
                                                       Peer::id_t) {
        BEAST_EXPECTS(false, "unexpected call to unsquelch handler");
    };

    // noop_handler is passed as a place holder Handler to slots
    TestHandler noop_handler = {
        noop_squelch,
        noop_squelchAll,
        noop_unsquelch,
    };

    jtx::Env env_;

    enhanced_squelch_test() : env_(*this)
    {
        env_.app().config().VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE = true;
    }

    void
    testConfig()
    {
        testcase("Test Config - enabled enhanced squelching");
        Config c;

        std::string toLoad(R"rippleConfig(
[reduce_relay]
vp_enhanced_squelch_enable=1
)rippleConfig");

        c.loadFromString(toLoad);
        BEAST_EXPECT(c.VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE == true);

        toLoad = R"rippleConfig(
[reduce_relay]
vp_enhanced_squelch_enable=0
)rippleConfig";

        c.loadFromString(toLoad);
        BEAST_EXPECT(c.VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE == false);

        toLoad = R"rippleConfig(
[reduce_relay]
)rippleConfig";

        c.loadFromString(toLoad);
        BEAST_EXPECT(c.VP_REDUCE_RELAY_ENHANCED_SQUELCH_ENABLE == false);
    }

    /** Tests tracking for squelched validators and peers */
    void
    testSquelchTracking()
    {
        testcase("squelchTracking");
        Peer::id_t const squelchedPeerID = 0;
        Peer::id_t const newPeerID = 1;
        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);
        auto const publicKey = randomKeyPair(KeyType::ed25519).first;

        // a new key should not be squelched
        BEAST_EXPECTS(
            !slots.validatorSquelched(publicKey), "validator squelched");

        slots.squelchValidator(publicKey, squelchedPeerID);

        // after squelching a peer, the validator must be squelched
        BEAST_EXPECTS(
            slots.validatorSquelched(publicKey), "validator not squelched");

        // the peer must also be squelched
        BEAST_EXPECTS(
            slots.peerSquelched(publicKey, squelchedPeerID),
            "peer not squelched");

        // a new peer must not be squelched
        BEAST_EXPECTS(
            !slots.peerSquelched(publicKey, newPeerID), "new peer squelched");

        // advance the manual clock to after expiration
        stopwatch.advance(
            reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT +
            std::chrono::seconds{11});

        // validator squelch should expire
        BEAST_EXPECTS(
            !slots.validatorSquelched(publicKey),
            "validator squelched after expiry");

        // peer squelch should also expire
        BEAST_EXPECTS(
            !slots.peerSquelched(publicKey, squelchedPeerID),
            "validator squelched after expiry");
    }

    void
    testUpdateValidatorSlot_newValidator()
    {
        testcase("updateValidatorSlot_newValidator");
        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        Peer::id_t const peerID = 1;
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        uint256 const message{0};

        slots.updateUntrustedValidatorSlot(message, validator, peerID);

        // adding untrusted slot does not effect trusted slots
        BEAST_EXPECTS(
            slots.getSlots(true).size() == 0, "trusted slots changed");

        // we expect that the validator was not added to untrusted slots
        BEAST_EXPECTS(
            slots.getSlots(false).size() == 0, "untrusted slot changed");

        // we expect that the validator was added to th consideration list
        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(validator),
            "new validator was not considered");
    }

    void
    testUpdateValidatorSlot_squelchedValidator()
    {
        testcase("testUpdateValidatorSlot_squelchedValidator");

        Peer::id_t const squelchedPeerID = 0;
        Peer::id_t const newPeerID = 1;
        auto const validator = randomKeyPair(KeyType::ed25519).first;

        TestHandler::squelch_method const squelch_f =
            [&](PublicKey const& key, Peer::id_t id, std::uint32_t duration) {
                BEAST_EXPECTS(
                    key == validator,
                    "squelch called for unknown validator key");

                BEAST_EXPECTS(
                    id == newPeerID, "squelch called for the wrong peer");
            };

        TestHandler handler{squelch_f, noop_squelchAll, noop_unsquelch};

        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        slots.squelchValidator(validator, squelchedPeerID);

        // this should not trigger squelch assertions, the peer is squelched
        slots.updateUntrustedValidatorSlot(
            sha512Half(validator), validator, squelchedPeerID);

        slots.updateUntrustedValidatorSlot(
            sha512Half(validator), validator, newPeerID);

        // the squelched peer remained squelched
        BEAST_EXPECTS(
            slots.peerSquelched(validator, squelchedPeerID),
            "peer not squelched");

        // because the validator was squelched, the new peer was also squelched
        BEAST_EXPECTS(
            slots.peerSquelched(validator, newPeerID),
            "new peer was not squelched");

        // a squelched validator must not be considered
        BEAST_EXPECTS(
            !slots.getConsideredValidators().contains(validator),
            "squelched validator was added for consideration");
    }

    void
    testUpdateValidatorSlot_slotsFull()
    {
        testcase("updateValidatorSlot_slotsFull");
        Peer::id_t const peerID = 1;

        // while there are open untrusted slots, no calls should be made to
        // squelch any validators
        TestHandler handler{noop_handler};

        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        // saturate validator slots
        auto const validators = fillUntrustedSlots(slots);

        // adding untrusted slot does not effect trusted slots
        BEAST_EXPECTS(
            slots.getSlots(true).size() == 0, "trusted slots changed");

        // simulate additional messages from already selected validators
        for (auto const& validator : validators)
            for (int i = 0; i < reduce_relay::MAX_MESSAGE_THRESHOLD; ++i)
                slots.updateUntrustedValidatorSlot(
                    sha512Half(validator) + static_cast<uint256>(i),
                    validator,
                    peerID);

        // an untrusted slot was added for each validator
        BEAST_EXPECT(
            slots.getSlots(false).size() == reduce_relay::MAX_UNTRUSTED_SLOTS);

        for (auto const& validator : validators)
            BEAST_EXPECTS(
                !slots.validatorSquelched(validator),
                "selected validator was squelched");

        auto const newValidator = randomKeyPair(KeyType::ed25519).first;

        // once slots are full squelchAll must be called for new peer/validator
        handler.squelchAll_f_ = [&](PublicKey const& key,
                                    std::uint32_t,
                                    std::function<void(Peer::id_t)> callback) {
            BEAST_EXPECTS(
                key == newValidator, "unexpected validator squelched");
            callback(peerID);
        };

        slots.updateUntrustedValidatorSlot(
            sha512Half(newValidator), newValidator, peerID);

        // Once the slots are saturated every other validator is squelched
        BEAST_EXPECTS(
            slots.validatorSquelched(newValidator),
            "untrusted validator not squelched");

        BEAST_EXPECTS(
            slots.peerSquelched(newValidator, peerID),
            "peer for untrusted validator not squelched");
    }

    void
    testDeleteIdlePeers_deleteIdleSlots()
    {
        testcase("deleteIdlePeers");
        TestHandler handler{noop_handler};
        TestStopwatch stopwatch;

        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);
        auto keys = fillUntrustedSlots(slots);

        //  verify that squelchAll is called for each idled slot validator
        handler.squelchAll_f_ = [&](PublicKey const& actualKey,
                                    std::uint32_t duration,
                                    std::function<void(Peer::id_t)> callback) {
            for (auto it = keys.begin(); it != keys.end(); ++it)
            {
                if (*it == actualKey)
                {
                    keys.erase(it);
                    return;
                }
            }
            BEAST_EXPECTS(false, "unexpected key passed to squelchAll");
        };

        BEAST_EXPECTS(
            slots.getSlots(false).size() == reduce_relay::MAX_UNTRUSTED_SLOTS,
            "unexpected number of untrusted slots");

        // advance the manual clock to after slot expiration
        stopwatch.advance(
            reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT +
            std::chrono::seconds{1});

        slots.deleteIdlePeers();

        BEAST_EXPECTS(
            slots.getSlots(false).size() == 0,
            "unexpected number of untrusted slots");

        BEAST_EXPECTS(keys.empty(), "not all validators were squelched");
    }

    void
    testDeleteIdlePeers_deleteIdleUntrustedPeer()
    {
        testcase("deleteIdleUntrustedPeer");
        Peer::id_t const peerID = 1;
        Peer::id_t const peerID2 = 2;
        TestStopwatch stopwatch;

        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        // fill one untrustd validator slot
        auto const validator = fillUntrustedSlots(slots, 1)[0];

        BEAST_EXPECTS(
            slots.getSlots(false).size() == 1,
            "unexpected number of untrusted slots");

        slots.updateSlotAndSquelch(
            sha512Half(validator) + static_cast<uint256>(100),
            validator,
            peerID,
            false);

        slots.updateSlotAndSquelch(
            sha512Half(validator) + static_cast<uint256>(100),
            validator,
            peerID2,
            false);

        slots.deletePeer(peerID, true);

        auto const slotPeers = getUntrustedSlotPeers(validator, slots);
        BEAST_EXPECTS(
            slotPeers.size() == 1, "untrusted validator slot is missing");

        BEAST_EXPECTS(
            !slotPeers.contains(peerID),
            "peer was not removed from untrusted slots");

        BEAST_EXPECTS(
            slotPeers.contains(peerID2),
            "peer was removed from untrusted slots");
    }

    /** Test that untrusted validator slots are correctly updated by
     * updateSlotAndSquelch
     */
    void
    testUpdateSlotAndSquelch_untrustedValidator()
    {
        testcase("updateUntrsutedValidatorSlot");
        TestHandler handler{noop_handler};

        handler.squelch_f_ = [](PublicKey const&, Peer::id_t, std::uint32_t) {};

        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        // peers that will be source of validator messages
        std::vector<Peer::id_t> peers = {};

        // prepare n+1 peers, we expect the n+1st peer will be squelched
        for (int i = 0; i <
             env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS + 1;
             ++i)
            peers.push_back(i);

        auto const validator = fillUntrustedSlots(slots, 1)[0];

        // Squelching logic resets all counters each time a new peer is added
        // Therfore we need to populate counters for each peer before sending
        // new messages
        for (auto const& peer : peers)
        {
            auto const now = stopwatch.now();
            slots.updateSlotAndSquelch(
                sha512Half(validator) +
                    static_cast<uint256>(now.time_since_epoch().count()),
                validator,
                peer,
                false);

            stopwatch.advance(std::chrono::milliseconds{10});
        }

        // simulate new, unique validator messages sent by peers
        for (auto const& peer : peers)
            for (int i = 0; i < reduce_relay::MAX_MESSAGE_THRESHOLD + 1; ++i)
            {
                auto const now = stopwatch.now();
                slots.updateSlotAndSquelch(
                    sha512Half(validator) +
                        static_cast<uint256>(now.time_since_epoch().count()),
                    validator,
                    peer,
                    false);

                stopwatch.advance(std::chrono::milliseconds{10});
            }

        auto const slotPeers = getUntrustedSlotPeers(validator, slots);
        BEAST_EXPECTS(
            slotPeers.size() ==
                env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS +
                    1,
            "untrusted validator slot is missing");

        int selected = 0;
        int squelched = 0;
        for (auto const& [_, info] : slotPeers)
        {
            switch (info.state)
            {
                case reduce_relay::PeerState::Selected:
                    ++selected;
                    break;
                case reduce_relay::PeerState::Squelched:
                    ++squelched;
                    break;
                case reduce_relay::PeerState::Counting:
                    BEAST_EXPECTS(
                        false, "peer should not be in counting state");
            }
        }

        BEAST_EXPECTS(squelched == 1, "expected one squelched peer");
        BEAST_EXPECTS(
            selected ==
                env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS,
            "wrong number of peers selected");
    }

    void
    testUpdateConsideredValidator_new()
    {
        testcase("testUpdateConsideredValidator_new");
        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        // insert some random validator key
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t const peerID = 0;
        Peer::id_t const peerID2 = 1;

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");

        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(validator),
            "new validator was not added for consideration");

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");

        // expect that a peer will be registered once as a message source
        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).peers.size() == 1,
            "duplicate peer was registered");

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID2),
            "validator was selected with insufficient number of peers");

        // expect that each distinct peer will be registered
        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).peers.size() == 2,
            "distinct peers were not registered");
    }

    void
    testUpdateConsideredValidator_idle()
    {
        testcase("testUpdateConsideredValidator_idle");
        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        // insert some random validator key
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t peerID = 0;

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");

        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(validator),
            "new validator was not added for consideration");

        auto const state = slots.getConsideredValidators().at(validator);

        // simulate a validator sending a new message before the idle timer
        stopwatch.advance(reduce_relay::PEER_IDLED - std::chrono::seconds(1));

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");
        auto const newState = slots.getConsideredValidators().at(validator);

        BEAST_EXPECTS(
            state.count + 1 == newState.count,
            "non-idling validator was updated");

        // simulate a validator idling
        stopwatch.advance(reduce_relay::PEER_IDLED + std::chrono::seconds(1));

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");
    }

    void
    testUpdateConsideredValidator_selectQualifying()
    {
        testcase("testUpdateConsideredValidator_selectQualifying");

        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        // insert some random validator key
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t peerID = 0;

        for (int i = 0; i < reduce_relay::MAX_MESSAGE_THRESHOLD - 1; ++i)
        {
            BEAST_EXPECTS(
                !slots.updateConsideredValidator(validator, peerID),
                "validator was selected before reaching message threshold");

            stopwatch.advance(
                reduce_relay::PEER_IDLED - std::chrono::seconds(1));
        }

        auto const consideredValidator =
            slots.updateConsideredValidator(validator, peerID);
        BEAST_EXPECTS(
            consideredValidator && *consideredValidator == validator,
            "expected validator was not selected");

        // expect that selected peer was removed
        BEAST_EXPECTS(
            !slots.getConsideredValidators().contains(validator),
            "selected validator was not removed from considered list");
    }

    void
    testCleanConsideredValidators_resetIdle()
    {
        testcase("testCleanConsideredValidators_resetIdle");
        auto const validator = randomKeyPair(KeyType::ed25519).first;

        TestStopwatch stopwatch;

        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), noop_handler, env_.app().config(), stopwatch);

        // send enough messages for a slot to meet peer requirements
        for (int i = 0;
             i < env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS;
             ++i)
            slots.updateUntrustedValidatorSlot(
                sha512Half(validator) + static_cast<uint256>(i), validator, i);

        // send enough messages from some peer to be one message away from
        // meeting the selection criteria
        for (int i = 0; i < reduce_relay::MAX_MESSAGE_THRESHOLD -
                 (env_.app()
                      .config()
                      .VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS +
                  1);
             ++i)
            slots.updateUntrustedValidatorSlot(
                sha512Half(validator) + static_cast<uint256>(i), validator, 0);

        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).count ==
                reduce_relay::MAX_MESSAGE_THRESHOLD - 1,
            "considered validator information is in an invalid state");

        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).peers.size() ==
                env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS,
            "considered validator information is in an invalid state");

        stopwatch.advance(reduce_relay::PEER_IDLED + std::chrono::seconds{1});

        // deleteIdlePeers must reset the progress of a validator that idled
        slots.deleteIdlePeers();

        slots.updateUntrustedValidatorSlot(
            sha512Half(validator) + static_cast<uint256>(1), validator, 0);

        // we expect that the validator was not selected
        BEAST_EXPECTS(
            slots.getSlots(false).size() == 0, "untrusted slot was created");

        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).count == 1,
            "considered validator information is in an invalid state");

        BEAST_EXPECTS(
            slots.getConsideredValidators().at(validator).peers.size() == 1,
            "considered validator information is in an invalid state");
    }

    void
    testCleanConsideredValidators_deletePoorlyConnected()
    {
        testcase("cleanConsideredValidators_deletePoorlyConnected");
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t const peerID = 0;
        TestHandler handler{noop_handler};

        //  verify that squelchAll is called for poorly connected validator
        handler.squelchAll_f_ = [&](PublicKey const& actualKey,
                                    std::uint32_t duration,
                                    std::function<void(Peer::id_t)> callback) {
            BEAST_EXPECTS(
                actualKey == validator, "unexpected key passed to squelchAll");
            callback(peerID);
        };

        TestStopwatch stopwatch;

        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        // send enough messages from a single peer
        for (int i = 0; i < 2 * reduce_relay::MAX_MESSAGE_THRESHOLD + 1; ++i)
            slots.updateUntrustedValidatorSlot(
                sha512Half(validator) + static_cast<uint256>(i),
                validator,
                peerID);

        stopwatch.advance(reduce_relay::PEER_IDLED + std::chrono::seconds{1});

        // deleteIdlePeers must squelch the validator as it failed to reach
        // peering requirements
        slots.deleteIdlePeers();

        BEAST_EXPECTS(
            slots.getConsideredValidators().size() == 0,
            "poorly connected validator was not deleted");
    }

    void
    testCleanConsideredValidators_deleteSilent()
    {
        testcase("cleanConsideredValidators_deleteSilent");
        // insert some random validator key
        auto const idleValidator = randomKeyPair(KeyType::ed25519).first;
        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t const peerID = 0;

        TestHandler handler{noop_handler};

        //  verify that squelchAll is called for idle validator
        handler.squelchAll_f_ = [&](PublicKey const& actualKey,
                                    std::uint32_t duration,
                                    std::function<void(Peer::id_t)> callback) {
            BEAST_EXPECTS(
                actualKey == idleValidator,
                "unexpected key passed to squelchAll");
            callback(peerID);
        };

        TestStopwatch stopwatch;

        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        BEAST_EXPECTS(
            !slots.updateConsideredValidator(idleValidator, peerID),
            "validator was selected with insufficient number of peers");

        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(idleValidator),
            "new validator was not added for consideration");

        // simulate a validator idling
        stopwatch.advance(
            reduce_relay::MAX_UNTRUSTED_VALIDATOR_IDLE +
            std::chrono::seconds(1));
        BEAST_EXPECTS(
            !slots.updateConsideredValidator(validator, peerID),
            "validator was selected with insufficient number of peers");

        slots.deleteIdlePeers();

        BEAST_EXPECTS(
            !slots.getConsideredValidators().contains(idleValidator),
            "late validator was not removed");
        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(validator),
            "timely validator was removed");
    }

    void
    testSquelchUntrustedValidator_consideredListCleared()
    {
        testcase("testSquelchUntrustedValidator");

        auto const validator = randomKeyPair(KeyType::ed25519).first;
        Peer::id_t const peerID = 0;

        TestHandler handler{noop_handler};
        //  verify that squelchAll is called for idle validator
        handler.squelchAll_f_ = [&](PublicKey const& actualKey,
                                    std::uint32_t duration,
                                    std::function<void(Peer::id_t)> callback) {
            BEAST_EXPECTS(
                actualKey == validator, "unexpected key passed to squelchAll");
        };

        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        // add the validator to the considered list
        slots.updateUntrustedValidatorSlot(
            sha512Half(validator), validator, peerID);

        BEAST_EXPECTS(
            slots.getConsideredValidators().contains(validator),
            "validator was not added to considered list");

        slots.squelchUntrustedValidator(validator);

        BEAST_EXPECTS(
            !slots.getConsideredValidators().contains(validator),
            "validator was not removed from considered list");
    }

    void
    testSquelchUntrustedValidator_slotEvicted()
    {
        testcase("testSquelchUntrustedValidator_slotEvicted");

        TestHandler handler{noop_handler};
        TestStopwatch stopwatch;
        EnhancedSquelchingTestSlots slots(
            env_.app().logs(), handler, env_.app().config(), stopwatch);

        // assign a slot to the untrusted validator
        auto const validators = fillUntrustedSlots(slots, 1);

        //  verify that squelchAll is called for idle validator
        handler.squelchAll_f_ = [&](PublicKey const& actualKey,
                                    std::uint32_t duration,
                                    std::function<void(Peer::id_t)> callback) {
            BEAST_EXPECTS(
                actualKey == validators[0],
                "unexpected key passed to squelchAll");
        };

        BEAST_EXPECTS(
            slots.getSlots(false).contains(validators[0]),
            "a slot was not assigned to a validator");

        slots.squelchUntrustedValidator(validators[0]);

        BEAST_EXPECTS(
            !slots.getSlots(false).contains(validators[0]),
            "a slot was not evicted");
    }

private:
    /** A helper method to fill untrusted slots of a given Slots instance
     * with random validator messages*/
    std::vector<PublicKey>
    fillUntrustedSlots(
        EnhancedSquelchingTestSlots& slots,
        int64_t maxSlots = reduce_relay::MAX_UNTRUSTED_SLOTS)
    {
        std::vector<PublicKey> keys;
        for (int i = 0; i < maxSlots; ++i)
        {
            auto const validator = randomKeyPair(KeyType::ed25519).first;
            keys.push_back(validator);
            for (int j = 0; j <
                 env_.app().config().VP_REDUCE_RELAY_SQUELCH_MAX_SELECTED_PEERS;
                 ++j)
                // send enough messages so that a validator slot is selected
                for (int k = 0; k < reduce_relay::MAX_MESSAGE_THRESHOLD; ++k)
                    slots.updateUntrustedValidatorSlot(
                        sha512Half(validator) + static_cast<uint256>(k),
                        validator,
                        j);
        }

        return keys;
    }

    std::unordered_map<Peer::id_t, reduce_relay::Slot::PeerInfo>
    getUntrustedSlotPeers(
        PublicKey const& validator,
        EnhancedSquelchingTestSlots const& slots)
    {
        auto const& it = slots.getSlots(false).find(validator);
        if (it == slots.getSlots(false).end())
            return {};

        auto r = std::unordered_map<Peer::id_t, reduce_relay::Slot::PeerInfo>();

        for (auto const& [id, info] : it->second.getPeers())
            r.emplace(std::make_pair(id, info));

        return r;
    }

    void
    run() override
    {
        testConfig();
        testSquelchTracking();
        testUpdateValidatorSlot_newValidator();
        testUpdateValidatorSlot_slotsFull();
        testUpdateValidatorSlot_squelchedValidator();
        testDeleteIdlePeers_deleteIdleSlots();
        testDeleteIdlePeers_deleteIdleUntrustedPeer();
        testUpdateSlotAndSquelch_untrustedValidator();
        testUpdateConsideredValidator_new();
        testUpdateConsideredValidator_idle();
        testUpdateConsideredValidator_selectQualifying();
        testCleanConsideredValidators_deleteSilent();
        testCleanConsideredValidators_resetIdle();
        testCleanConsideredValidators_deletePoorlyConnected();
        testSquelchUntrustedValidator_consideredListCleared();
        testSquelchUntrustedValidator_slotEvicted();
    }
};

BEAST_DEFINE_TESTSUITE(enhanced_squelch, overlay, ripple);

}  // namespace test

}  // namespace ripple