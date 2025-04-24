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

#include <xrpld/overlay/Slot.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/SecretKey.h>

#include "xrpld/overlay/ReduceRelayCommon.h"

#include <ripple.pb.h>

#include <chrono>
#include <functional>
#include <ratio>

namespace ripple {
namespace test {

using namespace std::chrono;

/** Manually advanced clock. */
class ManualClock
{
public:
    typedef uint64_t rep;
    typedef std::milli period;
    typedef std::chrono::duration<std::uint32_t, period> duration;
    typedef std::chrono::time_point<ManualClock> time_point;
    inline static bool const is_steady = false;

    static void
    advance(duration d) noexcept
    {
        now_ += d;
    }

    static void
    reset() noexcept
    {
        now_ = time_point(seconds(0));
    }

    static time_point
    now() noexcept
    {
        return now_;
    }

    explicit ManualClock() = default;

private:
    inline static time_point now_ = time_point(seconds(0));
};

class TestHandler : public reduce_relay::SquelchHandler
{
public:
    using squelch_method =
        std::function<void(PublicKey const&, Peer::id_t, std::uint32_t)>;

    using unsquelch_method = std::function<void(PublicKey const&, Peer::id_t)>;

    inline static squelch_method const noop_squelch =
        [](PublicKey const&, Peer::id_t, std::uint32_t) {};
    inline static const unsquelch_method noop_unsquelch = [](PublicKey const&,
                                                             Peer::id_t) {};

    squelch_method const squelch_f_;
    unsquelch_method const unsquelch_f_;

    TestHandler(
        squelch_method const& squelch_f,
        unsquelch_method const& unsquelch_f)
        : squelch_f_(squelch_f), unsquelch_f_(unsquelch_f)
    {
    }

    void
    squelch(PublicKey const& validator, Peer::id_t peer, std::uint32_t duration)
        const override
    {
        squelch_f_(validator, peer, duration);
    }

    void
    unsquelch(PublicKey const& validator, Peer::id_t peer) const override
    {
        unsquelch_f_(validator, peer);
    }
};

class selector_test : public beast::unit_test::suite
{
    void
    testUpdate()
    {
        testcase("update");
        reduce_relay::ValidatorSelector<ManualClock> selector;
        // insert some random validator key
        auto const val = randomKeyPair(KeyType::ed25519).first;
        BEAST_EXPECTS(selector.update(val), "failed to update selector");

        ManualClock::advance(std::chrono::seconds{30});
        // simulate the 2nd validator not sending message for some time
        // we expect that the selector will not update the idled validator
        BEAST_EXPECTS(!selector.update(val), "idle validator was updated");
    }

    void
    testSelect()
    {
        using namespace std::chrono;

        testcase("select");
        reduce_relay::ValidatorSelector<ManualClock> selector;
        auto const fillSelector = [&](PublicKey const& validator) {
            // send validator messages until we reach selection threshold
            for (int i = 0; i <= reduce_relay::MAX_MESSAGE_THRESHOLD; ++i)
            {
                BEAST_EXPECTS(
                    selector.update(validator), "failed to update selector");

                ManualClock::advance(reduce_relay::IDLED - seconds(1));
            }
        };

        // insert some random validator key to simulate some inactive validator
        selector.update(randomKeyPair(KeyType::ed25519).first);

        auto const expectedValidatorKey = randomKeyPair(KeyType::ed25519).first;
        fillSelector(expectedValidatorKey);

        auto const selectedValidatorKey = selector.select();
        // we expect that a selector will take the first validator the reached
        // the message threshold
        BEAST_EXPECTS(
            selectedValidatorKey &&
                *selectedValidatorKey == expectedValidatorKey,
            "failed to select a validator");

        // we expect that the selector will not return some other invalid
        // validator
        BEAST_EXPECTS(!selector.select(), "selected unexpected validator");

        auto const lateValidatorKey = randomKeyPair(KeyType::ed25519).first;
        fillSelector(lateValidatorKey);

        // simulate the validator idling
        ManualClock::advance(reduce_relay::IDLED + seconds(1));

        // we expect that even though the validator reached messagethreshold,
        // because it idled before selection, the selector will not choose it
        BEAST_EXPECTS(!selector.select(), "selected unexpected validator");
    }

    void
    run() override
    {
        testUpdate();
        testSelect();
    }
};

class slot_test : public beast::unit_test::suite
{
public:
    // noop_handler is passed as a place holder Handler to slots
    inline static TestHandler const noop_handler = {
        TestHandler::noop_squelch,
        TestHandler::noop_unsquelch,
    };

    jtx::Env env_;

    slot_test() : env_(*this)
    {
    }

    void
    testIsSquelched()
    {
        testcase("isSquelched");
        reduce_relay::Slots<ManualClock> slots(env_.app().logs(), noop_handler);
        auto const publicKey = randomKeyPair(KeyType::ed25519).first;

        // a new key should not be squelched
        BEAST_EXPECTS(!slots.isSquelched(publicKey), "validator squelched");

        slots.updateValidatorSquelch(publicKey);

        // after squelching a peer we expect isSquelched to return true
        BEAST_EXPECTS(slots.isSquelched(publicKey), "validator not squelched");

        // advance the manual clock to after expiration
        ManualClock::advance(
            reduce_relay::MAX_UNSQUELCH_EXPIRE_DEFAULT +
            std::chrono::seconds{10});

        // we expect isSquelched to return false for expired squelches
        BEAST_EXPECTS(!slots.isSquelched(publicKey), "expired squelch");
    }

    void
    testUpdateValidatorSlot_newValidator()
    {
        testcase("updateValidatorSlot_newValidator");
        // ID of the peer that sent the message
        Peer::id_t const peerID = 1;

        // Validator public key from where the message originated
        auto const publicKey = randomKeyPair(KeyType::ed25519).first;

        // Unique identifier of the message
        uint256 key{0};

        TestHandler::squelch_method const squelch_f =
            [&](PublicKey const&, Peer::id_t, std::uint32_t) {
                // squelch should not be called for a fresh validator
                BEAST_EXPECTS(false, "unexpected call to squelch handler");
            };

        TestHandler handler{squelch_f, TestHandler::noop_unsquelch};
        reduce_relay::Slots<ManualClock> slots(env_.app().logs(), noop_handler);

        slots.updateValidatorSlot(
            key, publicKey, peerID, protocol::mtVALIDATION);

        // adding untrusted slot does not effect trusted slots
        BEAST_EXPECTS(
            slots.getSlots(true).size() == 0, "trusted slots changed");

        // we expect that the validator was not added to untrusted slots
        BEAST_EXPECTS(
            slots.getSlots(false).size() == 0, "untrusted slot changed");
    }

    void
    testUpdateValidatorSlot_slotsFull()
    {
        testcase("updateValidatorSlot_slotsFull");
        // ID of the peer that sent the message
        Peer::id_t const peerID = 1;

        // Unique identifier of the message
        uint256 const key{0};

        TestHandler::squelch_method const squelch_f =
            [&](PublicKey const&, Peer::id_t, std::uint32_t) {
                // squelch should not be called for a fresh validator
                BEAST_EXPECTS(false, "unexpected call to squelch handler");
            };

        TestHandler handler{squelch_f, TestHandler::noop_unsquelch};
        reduce_relay::Slots<ManualClock> slots(env_.app().logs(), noop_handler);

        // saturate slots
        for (int i = 0; i < reduce_relay::MAX_UNTRUSTED_SLOTS; i++)
        {
            // Validator public key from where the message originated
            auto const publicKey = randomKeyPair(KeyType::ed25519).first;

            slots.updateValidatorSlot(
                key, publicKey, peerID, protocol::mtVALIDATION);
        }

        // adding untrusted slot does not effect trusted slots
        BEAST_EXPECTS(
            slots.getSlots(true).size() == 0, "trusted slots changed");

        auto const untrusted_slots = slots.getSlots(false);

        // we expect that an untrusted slot was added for the validator
        BEAST_EXPECT(
            untrusted_slots.size() == reduce_relay::MAX_UNTRUSTED_SLOTS);

        // Once the slots are saturated, we expect that every other validator
        // will be squelched
        auto const publicKey = randomKeyPair(KeyType::ed25519).first;

        slots.updateValidatorSlot(
            key, publicKey, peerID, protocol::mtVALIDATION);

        BEAST_EXPECTS(
            slots.isSquelched(publicKey), "untrusted validator not squelched");
    }

    void
    run() override
    {
        testIsSquelched();
        testUpdateValidatorSlot_newValidator();
        testUpdateValidatorSlot_slotsFull();
    }
};

BEAST_DEFINE_TESTSUITE(slot, overlay, ripple);
BEAST_DEFINE_TESTSUITE(selector, overlay, ripple);

}  // namespace test

}  // namespace ripple