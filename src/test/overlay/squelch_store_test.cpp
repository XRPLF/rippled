#include <test/jtx/Env.h>

#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/SquelchStore.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>

#include <chrono>

namespace xrpl::test {

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

class squelch_store_test : public beast::unit_test::Suite
{
    using seconds = std::chrono::seconds;

public:
    jtx::Env env;

    squelch_store_test() : env(*this)
    {
    }

    void
    testHandleSquelch()
    {
        testcase("SquelchStore handleSquelch");

        TestStopwatch clock;
        auto store = TestSquelchStore(env.journal, clock);

        auto const validator = randomKeyPair(KeyType::Ed25519).first;

        // attempt to squelch the peer with a too small duration
        store.handleSquelch(validator, true, reduce_relay::kMinUnsquelchExpire - seconds{1});

        // the peer must not be squelched
        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");

        // attempt to squelch the peer with a too big duration
        store.handleSquelch(validator, true, reduce_relay::kMaxUnsquelchExpirePeers + seconds{1});

        // the peer must not be squelched
        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");

        // squelch the peer with a good duration
        store.handleSquelch(validator, true, reduce_relay::kMinUnsquelchExpire + seconds{1});

        // the peer for the validator should be squelched
        BEAST_EXPECTS(store.isSquelched(validator), "peer and validator are not squelched");

        // unsquelch the validator
        store.handleSquelch(validator, false, seconds{0});

        BEAST_EXPECTS(!store.isSquelched(validator), "peer is squelched");
    }

    void
    testIsSquelched()
    {
        testcase("SquelchStore IsSquelched");
        TestStopwatch clock;
        auto store = TestSquelchStore(env.journal, clock);

        auto const validator = randomKeyPair(KeyType::Ed25519).first;
        auto const duration = reduce_relay::kMinUnsquelchExpire + seconds{1};

        store.handleSquelch(validator, true, reduce_relay::kMinUnsquelchExpire + seconds{1});
        BEAST_EXPECTS(store.isSquelched(validator), "peer and validator are not squelched");

        clock.advance(duration + seconds{1});

        // the peer with short squelch duration must be not squelched
        BEAST_EXPECTS(!store.isSquelched(validator), "peer and validator are squelched");
    }

    void
    testClearExpiredSquelches()
    {
        testcase("SquelchStore testClearExpiredSquelches");
        TestStopwatch clock;
        auto store = TestSquelchStore(env.journal, clock);

        auto const validator = randomKeyPair(KeyType::Ed25519).first;
        auto const duration = reduce_relay::kMinUnsquelchExpire + seconds{1};
        store.handleSquelch(validator, true, duration);
        BEAST_EXPECTS(
            store.getSquelched().size() == 1, "validators were not registered in the store");

        clock.advance(duration + seconds{1});

        auto const validator2 = randomKeyPair(KeyType::Ed25519).first;
        auto const duration2 = reduce_relay::kMinUnsquelchExpire + seconds{2};
        store.handleSquelch(validator2, true, duration2);

        BEAST_EXPECTS(!store.getSquelched().contains(validator), "expired squelch was not deleted");

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

BEAST_DEFINE_TESTSUITE(squelch_store, overlay, xrpl);

}  // namespace xrpl::test
