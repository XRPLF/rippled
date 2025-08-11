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

#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/SquelchStore.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/PublicKey.h>

#include <chrono>

namespace ripple {

namespace test {

class TestSquelchStore : public reduce_relay::SquelchStore
{
public:
    TestSquelchStore(beast::Journal journal, TestStopwatch& clock)
        : reduce_relay::SquelchStore(journal, clock)
    {
    }

    hash_map<PublicKey, TestStopwatch::time_point> const&
    getSquelched() const
    {
        return squelched_;
    }
};

class squelch_store_test : public beast::unit_test::suite
{
    using seconds = std::chrono::seconds;

public:
    jtx::Env env_;

    squelch_store_test() : env_(*this)
    {
    }

    void
    testHandleSquelch()
    {
        testcase("SquelchStore handleSquelch");

        TestStopwatch clock;
        auto store = TestSquelchStore(env_.journal, clock);

        auto const validator = randomKeyPair(KeyType::ed25519).first;

        // attempt to squelch the peer with a too small duration
        store.handleSquelch(
            validator, true, reduce_relay::MIN_UNSQUELCH_EXPIRE - seconds{1});

        // the peer must not be squelched
        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");

        // attempt to squelch the peer with a too big duration
        store.handleSquelch(
            validator,
            true,
            reduce_relay::MAX_UNSQUELCH_EXPIRE_PEERS + seconds{1});

        // the peer must not be squelched
        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");

        // squelch the peer with a good duration
        store.handleSquelch(
            validator, true, reduce_relay::MIN_UNSQUELCH_EXPIRE + seconds{1});

        // the peer for the validator should be squelched
        BEAST_EXPECTS(
            store.isSquelched(validator),
            "peer and validator are not squelched");

        // unsquelch the validator
        store.handleSquelch(validator, false, seconds{0});

        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");
    }

    void
    testIsSquelched()
    {
        testcase("SquelchStore IsSquelched");
        TestStopwatch clock;
        auto store = TestSquelchStore(env_.journal, clock);

        auto const validator = randomKeyPair(KeyType::ed25519).first;
        auto const duration = reduce_relay::MIN_UNSQUELCH_EXPIRE + seconds{1};

        store.handleSquelch(
            validator, true, reduce_relay::MIN_UNSQUELCH_EXPIRE + seconds{1});
        BEAST_EXPECTS(
            store.isSquelched(validator),
            "peer and validator are not squelched");

        clock.advance(duration + seconds{1});

        // the peer with short squelch duration must be not squelched
        BEAST_EXPECTS(
            !store.isSquelched(validator), "peer and validator are squelched");
    }

    void
    testClearExpiredSquelches()
    {
        testcase("SquelchStore testClearExpiredSquelches");
        TestStopwatch clock;
        auto store = TestSquelchStore(env_.journal, clock);

        auto const validator = randomKeyPair(KeyType::ed25519).first;
        auto const duration = reduce_relay::MIN_UNSQUELCH_EXPIRE + seconds{1};
        store.handleSquelch(validator, true, duration);
        BEAST_EXPECTS(
            store.getSquelched().size() == 1,
            "validators were not registered in the store");

        clock.advance(duration + seconds{1});

        auto const validator2 = randomKeyPair(KeyType::ed25519).first;
        auto const duration2 = reduce_relay::MIN_UNSQUELCH_EXPIRE + seconds{2};
        store.handleSquelch(validator2, true, duration2);

        BEAST_EXPECTS(
            !store.getSquelched().contains(validator),
            "expired squelch was not deleted");

        BEAST_EXPECTS(
            store.getSquelched().contains(validator2),
            "validators were not registered in the store");
    }
    void
    run() override
    {
        testHandleSquelch();
        testIsSquelched();
        testClearExpiredSquelches();
    }
};

BEAST_DEFINE_TESTSUITE(squelch_store, ripple_data, ripple);

}  // namespace test
}  // namespace ripple
