
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>

namespace xrpl {

class ClawbackMPT_test : public beast::unit_test::Suite
{
    static std::uint32_t
    ticketCount(test::jtx::Env const& env, test::jtx::Account const& acct)
    {
        std::uint32_t ret{0};
        if (auto const sleAcct = env.le(acct))
            ret = sleAcct->at(~sfTicketCount).value_or(0);
        return ret;
    }

    void
    testValidation(FeatureBitset features)
    {
        testcase("Validation");
        using namespace test::jtx;

        // MPT clawback fails when featureMPTokensV1 is disabled
        {
            Env env(*this, features - featureMPTokensV1);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            env(claw(alice, mpt(5), bob), Ter(temDISABLED));
            env.close();
        }

        // MPT clawback fails when tfMPTCanClawback is not set on the issuance
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create({.ownerCount = 1, .holderCount = 0});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 100);

            mptAlice.claw(alice, bob, 10, tecNO_PERMISSION);
        }

        // Test preflight validation failures
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const mpt = xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            // fails due to invalid flag
            env(claw(alice, mpt(5), bob), Txflags(0x00008000), Ter(temINVALID_FLAG));
            env.close();

            // fails due to zero amount
            env(claw(alice, mpt(0), bob), Ter(temBAD_AMOUNT));
            env.close();

            // fails due to negative amount
            env(claw(alice, mpt(-1), bob), Ter(temBAD_AMOUNT));
            env.close();

            // fails when holder is not specified
            env(claw(alice, mpt(5)), Ter(temMALFORMED));
            env.close();

            // fails when issuer and holder are the same account
            env(claw(alice, mpt(5), alice), Ter(temMALFORMED));
            env.close();
        }

        // Test preclaim failures
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});

            auto const fakeMpt =
                xrpl::test::jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));

            // clawback fails when the issuance does not exist
            env(claw(alice, fakeMpt(5), bob), Ter(tecOBJECT_NOT_FOUND));
            env.close();

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            // clawback fails when bob has no MPToken
            mptAlice.claw(alice, bob, 5, tecOBJECT_NOT_FOUND);

            mptAlice.authorize({.account = bob});

            // clawback fails because bob's balance is 0
            mptAlice.claw(alice, bob, 5, tecINSUFFICIENT_FUNDS);
        }
    }

    void
    testPermission(FeatureBitset features)
    {
        testcase("Permission");
        using namespace test::jtx;

        // Clawing back from a non-existent account fails
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            // bob is not funded and does not exist
            MPTTester mptAlice(env, alice, MPTInit{});
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});

            mptAlice.claw(alice, bob, 5, terNO_ACCOUNT);
        }

        // A non-issuer cannot claw back MPT
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            env.fund(XRP(1000), cindy);
            env.close();

            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 1000);

            // cindy fails to claw because she is not the issuer
            mptAlice.claw(cindy, bob, 200, tecNO_PERMISSION);
        }
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enable clawback");
        using namespace test::jtx;

        // Test that alice is able to successfully clawback MPT from bob
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 1000);

        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 1000));

        // alice claws back 200 tokens from bob
        mptAlice.claw(alice, bob, 200);
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 800));

        // alice claws back remaining 800 tokens
        mptAlice.claw(alice, bob, 800);
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 0));
    }

    void
    testMultiIssuance(FeatureBitset features)
    {
        testcase("Multi issuance");
        using namespace test::jtx;

        // Two issuers each issue their own MPT to cindy.
        // Clawback from one does not affect the other.
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            MPTTester mptAlice(env, alice, {.holders = {cindy}});
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
            mptAlice.authorize({.account = cindy});
            mptAlice.pay(alice, cindy, 1000);

            MPTTester mptBob(env, bob, MPTInit{});  // cindy already funded by mptAlice
            mptBob.create({.ownerCount = 1, .flags = tfMPTCanClawback});
            mptBob.authorize({.account = cindy});
            mptBob.pay(bob, cindy, 1000);

            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 1000));
            BEAST_EXPECT(mptBob.checkMPTokenAmount(cindy, 1000));

            // alice claws back 200 from cindy, bob's issuance is unaffected
            mptAlice.claw(alice, cindy, 200);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 800));
            BEAST_EXPECT(mptBob.checkMPTokenAmount(cindy, 1000));

            // bob claws back 600 from cindy, alice's issuance is unaffected
            mptBob.claw(bob, cindy, 600);
            BEAST_EXPECT(mptBob.checkMPTokenAmount(cindy, 400));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 800));
        }

        // One issuer issues MPT to two different holders.
        // Clawback from one holder does not affect the other.
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            MPTTester mptAlice(env, alice, {.holders = {bob, cindy}});
            mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
            mptAlice.authorize({.account = bob});
            mptAlice.authorize({.account = cindy});
            mptAlice.pay(alice, bob, 600);
            mptAlice.pay(alice, cindy, 1000);

            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 600));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 1000));

            // alice claws back 500 from bob, cindy's balance is unchanged
            mptAlice.claw(alice, bob, 500);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 100));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 1000));

            // alice claws back 300 from cindy, bob's balance is unchanged
            mptAlice.claw(alice, cindy, 300);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(cindy, 700));
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 100));
        }
    }

    void
    testZeroBalanceAfterClawback(FeatureBitset features)
    {
        testcase("Zero balance after clawback");
        using namespace test::jtx;

        // After clawback reduces balance to zero, the MPToken object
        // still exists (unlike IOU trustlines which are deleted).
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 1000);

        BEAST_EXPECT(ownerCount(env, bob) == 1);
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 1000));

        // alice claws back the full amount
        mptAlice.claw(alice, bob, 1000);
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 0));

        // bob still holds the MPToken object (balance 0, not deleted)
        BEAST_EXPECT(ownerCount(env, bob) == 1);
    }

    void
    testLockedMPT(FeatureBitset features)
    {
        testcase("Locked MPT");
        using namespace test::jtx;

        // Test that globally locked MPT can still be clawed back
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock | tfMPTCanClawback});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 1000);

            // globally lock the issuance
            mptAlice.set({.account = alice, .flags = tfMPTLock});

            // clawback succeeds despite global lock
            mptAlice.claw(alice, bob, 200);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 800));
        }

        // Test that individually locked MPT can still be clawed back
        {
            Env env(*this, features);
            Account const alice{"alice"};
            Account const bob{"bob"};

            MPTTester mptAlice(env, alice, {.holders = {bob}});
            mptAlice.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanLock | tfMPTCanClawback});
            mptAlice.authorize({.account = bob});
            mptAlice.pay(alice, bob, 1000);

            // individually lock bob's MPToken
            mptAlice.set({.account = alice, .holder = bob, .flags = tfMPTLock});

            // clawback succeeds despite individual lock
            mptAlice.claw(alice, bob, 200);
            BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 800));
        }
    }

    void
    testAmountExceedsAvailable(FeatureBitset features)
    {
        testcase("Amount exceeds available");
        using namespace test::jtx;

        // When alice tries to claw back more than bob holds,
        // only the available balance is clawed back
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 1000);

        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 1000));

        // alice tries to claw back 2000, but bob only has 1000
        mptAlice.claw(alice, bob, 2000);
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 0));
        BEAST_EXPECT(mptAlice.checkMPTokenOutstandingAmount(0));

        // MPToken object still exists with 0 balance
        BEAST_EXPECT(ownerCount(env, bob) == 1);
    }

    void
    testTickets(FeatureBitset features)
    {
        testcase("Tickets");
        using namespace test::jtx;

        // Tests MPT clawback using tickets
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanClawback});
        mptAlice.authorize({.account = bob});
        mptAlice.pay(alice, bob, 100);

        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 100));

        // alice creates 10 tickets
        std::uint32_t ticketCnt = 10;
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, ticketCnt));
        env.close();
        std::uint32_t const aliceSeq{env.seq(alice)};
        BEAST_EXPECT(ticketCount(env, alice) == ticketCnt);
        BEAST_EXPECT(ownerCount(env, alice) == ticketCnt + 1);  // tickets + issuance

        while (ticketCnt > 0)
        {
            // alice claws back 5 tokens using a ticket
            env(claw(alice, mptAlice.mpt(5), bob), ticket::Use(aliceTicketSeq++));
            env.close();

            ticketCnt--;
            BEAST_EXPECT(ticketCount(env, alice) == ticketCnt);
        }

        // alice clawed back 50 tokens total, 50 remain
        BEAST_EXPECT(mptAlice.checkMPTokenAmount(bob, 50));

        // account sequence numbers did not advance
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testValidation(features);
        testPermission(features);
        testEnabled(features);
        testMultiIssuance(features);
        testZeroBalanceAfterClawback(features);
        testLockedMPT(features);
        testAmountExceedsAvailable(features);
        testTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(ClawbackMPT, app, xrpl);
}  // namespace xrpl
