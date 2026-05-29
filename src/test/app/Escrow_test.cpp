
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/credentials.h>
#include <test/jtx/deposit.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/seq.h>
#include <test/jtx/tag.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

namespace xrpl::test {

struct Escrow_test : public beast::unit_test::Suite
{
    void
    testEnablement(FeatureBitset features)
    {
        testcase("Enablement");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, features);
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(5000), "alice", "bob");
        env(escrow::create("alice", "bob", XRP(1000)), escrow::kFinishTime(env.now() + 1s));
        env.close();

        auto const seq1 = env.seq("alice");

        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::kCondition(escrow::kCb1),
            escrow::kFinishTime(env.now() + 1s),
            Fee(baseFee * 150));
        env.close();
        env(escrow::finish("bob", "alice", seq1),
            escrow::kCondition(escrow::kCb1),
            escrow::kFulfillment(escrow::kFb1),
            Fee(baseFee * 150));

        auto const seq2 = env.seq("alice");

        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::kCondition(escrow::kCb2),
            escrow::kFinishTime(env.now() + 1s),
            escrow::kCancelTime(env.now() + 2s),
            Fee(baseFee * 150));
        env.close();
        env(escrow::cancel("bob", "alice", seq2), Fee(baseFee * 150));
    }

    void
    testTiming(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: Finish Only");
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)), escrow::kFinishTime(ts));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));

            env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150));
        }

        {
            testcase("Timing: Cancel Only");
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const ts = env.now() + 117s;

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::kCondition(escrow::kCb1),
                escrow::kCancelTime(ts));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));

            // Verify that a finish won't work anymore.
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(baseFee * 150),
                Ter(tecNO_PERMISSION));

            // Verify that the cancel will succeed
            env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150));
        }

        {
            testcase("Timing: Finish and Cancel -> Finish");
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 117s;
            auto const cts = env.now() + 192s;

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::kFinishTime(fts),
                escrow::kCancelTime(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));
                env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));
            }

            // Verify that a cancel still won't work
            env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));

            // And verify that a finish will
            env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150));
        }

        {
            testcase("Timing: Finish and Cancel -> Cancel");
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 109s;
            auto const cts = env.now() + 184s;

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::kFinishTime(fts),
                escrow::kCancelTime(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));
                env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));
            }

            // Continue advancing, verifying that the cancel won't complete
            // prematurely. At this point a finish would succeed.
            for (; env.now() < cts; env.close())
                env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));

            // Verify that finish will no longer work, since we are past the
            // cancel activation time.
            env(escrow::finish("bob", "alice", seq), Fee(baseFee * 150), Ter(tecNO_PERMISSION));

            // And verify that a cancel will succeed.
            env(escrow::cancel("bob", "alice", seq), Fee(baseFee * 150));
        }
    }

    void
    testTags(FeatureBitset features)
    {
        testcase("Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, features);

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(5000), alice, bob);

        // Check to make sure that we correctly detect if tags are really
        // required:
        env(fset(bob, asfRequireDest));
        env(escrow::create(alice, bob, XRP(1000)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(tecDST_TAG_NEEDED));

        // set source and dest tags
        auto const seq = env.seq(alice);

        env(escrow::create(alice, bob, XRP(1000)),
            escrow::kFinishTime(env.now() + 1s),
            Stag(1),
            Dtag(2));

        auto const sle = env.le(keylet::escrow(alice.id(), seq));
        BEAST_EXPECT(sle);
        BEAST_EXPECT((*sle)[sfSourceTag] == 1);
        BEAST_EXPECT((*sle)[sfDestinationTag] == 2);
        if (features[fixIncludeKeyletFields])
        {
            BEAST_EXPECT((*sle)[sfSequence] == seq);
        }
        else
        {
            BEAST_EXPECT(!sle->isFieldPresent(sfSequence));
        }
    }

    void
    testDisallowXRP(FeatureBitset features)
    {
        testcase("Disallow XRP");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Ignore the "asfDisallowXRP" account flag, which we should
            // have been doing before.
            Env env(*this, features);

            env.fund(XRP(5000), "bob", "george");
            env(fset("george", asfDisallowXRP));
            env(escrow::create("bob", "george", XRP(10)), escrow::kFinishTime(env.now() + 1s));
        }
    }

    void
    testRequiresConditionOrFinishAfter(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        testcase("RequiresConditionOrFinishAfter");

        Env env(*this, features);
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(5000), "alice", "bob", "carol");
        env.close();

        // Creating an escrow with only a cancel time is not allowed:
        env(escrow::create("alice", "bob", XRP(100)),
            escrow::kCancelTime(env.now() + 90s),
            Fee(baseFee * 150),
            Ter(temMALFORMED));

        // Creating an escrow with only a cancel time and a kCondition is
        // allowed:
        auto const seq = env.seq("alice");
        env(escrow::create("alice", "bob", XRP(100)),
            escrow::kCancelTime(env.now() + 90s),
            escrow::kCondition(escrow::kCb1),
            Fee(baseFee * 150));
        env.close();
        env(escrow::finish("carol", "alice", seq),
            escrow::kCondition(escrow::kCb1),
            escrow::kFulfillment(escrow::kFb1),
            Fee(baseFee * 150));
        BEAST_EXPECT(env.balance("bob") == XRP(5100));

        // Creating an escrow with only a cancel time and a finish time is
        // allowed:
        auto const seqFt = env.seq("alice");
        env(escrow::create("alice", "bob", XRP(100)),
            escrow::kFinishTime(env.now()),  // Set finish time to now so that
                                             // we can call finish immediately.
            escrow::kCancelTime(env.now() + 50s),
            Fee(baseFee * 150));
        env.close();
        env(escrow::finish("carol", "alice", seqFt), Fee(150 * baseFee));
        BEAST_EXPECT(env.balance("bob") == XRP(5200));  // 5100 (from last transaction) + 100
    }

    void
    testFails(FeatureBitset features)
    {
        testcase("Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, features);
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(5000), "alice", "bob", "gw");
        env.close();

        // temINVALID_FLAG
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::kFinishTime(env.now() + 5s),
            Txflags(tfPassive),
            Ter(temINVALID_FLAG));

        // Finish time is in the past
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::kFinishTime(env.now() - 5s),
            Ter(tecNO_PERMISSION));

        // Cancel time is in the past
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::kCondition(escrow::kCb1),
            escrow::kCancelTime(env.now() - 5s),
            Ter(tecNO_PERMISSION));

        // no destination account
        env(escrow::create("alice", "carol", XRP(1000)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(tecNO_DST));

        env.fund(XRP(5000), "carol");

        // Using non-XRP:
        bool const withTokenEscrow = env.current()->rules().enabled(featureTokenEscrow);
        {
            // tecNO_PERMISSION: token escrow is enabled but the issuer did not
            // set the asfAllowTrustLineLocking flag
            auto const txResult = withTokenEscrow ? Ter(tecNO_PERMISSION) : Ter(temBAD_AMOUNT);
            env(escrow::create("alice", "carol", Account("alice")["USD"](500)),
                escrow::kFinishTime(env.now() + 5s),
                txResult);
        }

        // Sending zero or no XRP:
        env(escrow::create("alice", "carol", XRP(0)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(temBAD_AMOUNT));
        env(escrow::create("alice", "carol", XRP(-1000)),
            escrow::kFinishTime(env.now() + 1s),
            Ter(temBAD_AMOUNT));

        // Fail if neither CancelAfter nor FinishAfter are specified:
        env(escrow::create("alice", "carol", XRP(1)), Ter(temBAD_EXPIRATION));

        // Fail if neither a FinishTime nor a kCondition are attached:
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kCancelTime(env.now() + 1s),
            Ter(temMALFORMED));

        // Fail if FinishAfter has already passed:
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kFinishTime(env.now() - 1s),
            Ter(tecNO_PERMISSION));

        // If both CancelAfter and FinishAfter are set, then CancelAfter must
        // be strictly later than FinishAfter.
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kCondition(escrow::kCb1),
            escrow::kFinishTime(env.now() + 10s),
            escrow::kCancelTime(env.now() + 10s),
            Ter(temBAD_EXPIRATION));

        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kCondition(escrow::kCb1),
            escrow::kFinishTime(env.now() + 10s),
            escrow::kCancelTime(env.now() + 5s),
            Ter(temBAD_EXPIRATION));

        // Carol now requires the use of a destination tag
        env(fset("carol", asfRequireDest));

        // missing destination tag
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kCondition(escrow::kCb1),
            escrow::kCancelTime(env.now() + 1s),
            Ter(tecDST_TAG_NEEDED));

        // Success!
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::kCondition(escrow::kCb1),
            escrow::kCancelTime(env.now() + 1s),
            Dtag(1));

        {  // Fail if the sender wants to send more than he has:
            auto const accountReserve = drops(env.current()->fees().reserve);
            auto const accountIncrement = drops(env.current()->fees().increment);

            env.fund(accountReserve + accountIncrement + XRP(50), "daniel");
            env(escrow::create("daniel", "bob", XRP(51)),
                escrow::kFinishTime(env.now() + 1s),
                Ter(tecUNFUNDED));

            env.fund(accountReserve + accountIncrement + XRP(50), "evan");
            env(escrow::create("evan", "bob", XRP(50)),
                escrow::kFinishTime(env.now() + 1s),
                Ter(tecUNFUNDED));

            env.fund(accountReserve, "frank");
            env(escrow::create("frank", "bob", XRP(1)),
                escrow::kFinishTime(env.now() + 1s),
                Ter(tecINSUFFICIENT_RESERVE));
        }

        {  // Specify incorrect sequence number
            env.fund(XRP(5000), "hannah");
            auto const seq = env.seq("hannah");
            env(escrow::create("hannah", "hannah", XRP(10)),
                escrow::kFinishTime(env.now() + 1s),
                Fee(150 * baseFee));
            env.close();
            env(escrow::finish("hannah", "hannah", seq + 7), Fee(150 * baseFee), Ter(tecNO_TARGET));
        }

        {  // Try to specify a kCondition for a non-conditional payment
            env.fund(XRP(5000), "ivan");
            auto const seq = env.seq("ivan");

            env(escrow::create("ivan", "ivan", XRP(10)), escrow::kFinishTime(env.now() + 1s));
            env.close();
            env(escrow::finish("ivan", "ivan", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
        }
    }

    void
    testLockup(FeatureBitset features)
    {
        testcase("Lockup");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Unconditional
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)), escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(escrow::finish("bob", "alice", seq));
            env.require(Balance("alice", XRP(5000) - drops(baseFee)));
        }
        {
            // Unconditionally pay from Alice to Bob.  Zelda (neither source nor
            // destination) signs all cancels and finishes.  This shows that
            // Escrow will make a payment to Bob with no intervention from Bob.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)), escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(escrow::cancel("zelda", "alice", seq), Ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(escrow::finish("zelda", "alice", seq));
            env.close();

            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            env.require(Balance("bob", XRP(6000)));
            env.require(Balance("zelda", XRP(5000) - drops(4 * baseFee)));
        }
        {
            // Bob sets DepositAuth so only Bob can finish the escrow.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)), escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish will only succeed for
            // Bob, because of DepositAuth.
            env(escrow::cancel("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq));
            env.close();

            env.require(Balance("alice", XRP(4000) - (baseFee * 5)));
            env.require(Balance("bob", XRP(6000) - (baseFee * 5)));
            env.require(Balance("zelda", XRP(5000) - (baseFee * 4)));
        }
        {
            // Bob sets DepositAuth but preauthorizes Zelda, so Zelda can
            // finish the escrow.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();
            env(deposit::auth("bob", "zelda"));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)), escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(escrow::finish("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq));
            env.close();

            env.require(Balance("alice", XRP(4000) - (baseFee * 2)));
            env.require(Balance("bob", XRP(6000) - (baseFee * 2)));
            env.require(Balance("zelda", XRP(5000) - (baseFee * 1)));
        }
        {
            // Conditional
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::kCondition(escrow::kCb2),
                escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee),
                Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee),
                Ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish is possible but
            // requires the kFulfillment associated with the escrow.
            env(escrow::cancel("alice", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("alice", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            env.close();

            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::kCondition(escrow::kCb3),
                escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(escrow::finish("bob", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("alice", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));

            // Enable deposit authorization. After this only Alice can finish
            // the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(escrow::finish("alice", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee),
                Ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth and DepositPreauth.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::kCondition(escrow::kCb3),
                escrow::kFinishTime(env.now() + 5s));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // Alice preauthorizes Zelda for deposit, even though Alice has not
            // set the lsfDepositAuth flag (yet).
            env(deposit::auth("alice", "zelda"));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(escrow::finish("alice", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("zelda", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));

            // Alice enables deposit authorization. After this only Alice or
            // Zelda (because Zelda is preauthorized) can finish the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(escrow::finish("alice", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee),
                Ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee));
        }
    }

    void
    testEscrowConditions(FeatureBitset features)
    {
        testcase("Escrow with CryptoConditions");

        using namespace jtx;
        using namespace std::chrono;

        {  // Test cryptoconditions
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(escrow::kCb1),
                escrow::kCancelTime(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            env.require(Balance("carol", XRP(5000)));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish without a kFulfillment
            env(escrow::finish("bob", "alice", seq), Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with a kCondition instead of a kFulfillment
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kCb1),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kCb2),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kCb3),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with an incorrect kCondition and various
            // combinations of correct and incorrect fulfillments.
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb1),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with the correct kCondition & kFulfillment
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(150 * baseFee));

            // SLE removed on finish
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(Balance("carol", XRP(6000)));
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow::cancel("bob", "carol", 1), Ter(tecNO_TARGET));
        }
        {  // Test cancel when kCondition is present
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(escrow::kCb2),
                escrow::kCancelTime(env.now() + 1s));
            env.close();
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
            // balance restored on cancel
            env(escrow::cancel("bob", "alice", seq));
            env.require(Balance("alice", XRP(5000) - drops(baseFee)));
            // SLE removed on cancel
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
        }
        {
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(escrow::kCb3),
                escrow::kCancelTime(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            // cancel fails before expiration
            env(escrow::cancel("bob", "alice", seq), Ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.close();
            // finish fails after expiration
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee),
                Ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(Balance("carol", XRP(5000)));
        }
        {  // Test long & short conditions during creation
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> v;
            v.resize(escrow::kCb1.size() + 2, 0x78);
            std::memcpy(v.data() + 1, escrow::kCb1.data(), escrow::kCb1.size());

            auto const p = v.data();
            auto const s = v.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // kCondition we pass in is malformed in some way
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p, s}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p, s - 1}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p, s - 2}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p + 1, s - 1}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p + 1, s - 3}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p + 2, s - 2}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p + 2, s - 3}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{p + 1, s - 2}),
                escrow::kCancelTime(ts),
                Fee(10 * baseFee));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(150 * baseFee));
            env.require(Balance("alice", XRP(4000) - drops(10 * baseFee)));
            env.require(Balance("bob", XRP(5000) - drops(150 * baseFee)));
            env.require(Balance("carol", XRP(6000)));
        }
        {  // Test long and short conditions & fulfillments during finish
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> cv;
            cv.resize(escrow::kCb2.size() + 2, 0x78);
            std::memcpy(cv.data() + 1, escrow::kCb2.data(), escrow::kCb2.size());

            auto const cp = cv.data();
            auto const cs = cv.size();

            std::vector<std::uint8_t> fv;
            fv.resize(escrow::kFb2.size() + 2, 0x13);
            std::memcpy(fv.data() + 1, escrow::kFb2.data(), escrow::kFb2.size());

            auto const fp = fv.data();
            auto const fs = fv.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // kCondition we pass in is malformed in some way
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp, cs}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp, cs - 1}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp, cs - 2}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp + 1, cs - 1}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp + 1, cs - 3}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp + 2, cs - 2}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp + 2, cs - 3}),
                escrow::kCancelTime(ts),
                Ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kCancelTime(ts),
                Fee(10 * baseFee));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp, cs}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp, cs - 1}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp, cs - 2}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 1}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 3}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 2, cs - 2}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 2, cs - 3}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct kCondition, try malformed fulfillments:
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp, fs}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp, fs - 1}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp, fs - 2}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp + 1, fs - 1}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp + 1, fs - 3}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp + 1, fs - 3}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp + 2, fs - 2}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{cp + 1, cs - 2}),
                escrow::kFulfillment(Slice{fp + 2, fs - 3}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb2),
                escrow::kFulfillment(escrow::kFb2),
                Fee(150 * baseFee));
            env.require(Balance("alice", XRP(4000) - drops(10 * baseFee)));
            env.require(Balance("carol", XRP(6000)));
        }
        {  // Test empty kCondition during creation and
           // empty kCondition & kFulfillment during finish
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(Slice{}),
                escrow::kCancelTime(env.now() + 1s),
                Ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::kCondition(escrow::kCb3),
                escrow::kCancelTime(env.now() + 1s));

            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{}),
                escrow::kFulfillment(Slice{}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(Slice{}),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(Slice{}),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee),
                Ter(tecCRYPTOCONDITION_ERROR));

            // Assemble finish that is missing the Condition or the Fulfillment
            // since either both must be present, or neither can:
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                Ter(temMALFORMED));
            env(escrow::finish("bob", "alice", seq),
                escrow::kFulfillment(escrow::kFb3),
                Ter(temMALFORMED));

            // Now finish it.
            env(escrow::finish("bob", "alice", seq),
                escrow::kCondition(escrow::kCb3),
                escrow::kFulfillment(escrow::kFb3),
                Fee(150 * baseFee));
            env.require(Balance("carol", XRP(6000)));
            env.require(Balance("alice", XRP(4000) - drops(baseFee)));
        }
        {  // Test a kCondition other than PreimageSha256, which
           // would require a separate amendment
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob");

            std::array<std::uint8_t, 45> const cb = {
                {0xA2, 0x2B, 0x80, 0x20, 0x42, 0x4A, 0x70, 0x49, 0x49, 0x52, 0x92, 0x67,
                 0xB6, 0x21, 0xB3, 0xD7, 0x91, 0x19, 0xD7, 0x29, 0xB2, 0x38, 0x2C, 0xED,
                 0x8B, 0x29, 0x6C, 0x3C, 0x02, 0x8F, 0xA9, 0x7D, 0x35, 0x0F, 0x6D, 0x07,
                 0x81, 0x03, 0x06, 0x34, 0xD2, 0x82, 0x02, 0x03, 0xC8}};

            // FIXME: this transaction should, eventually, return temDISABLED
            //        instead of temMALFORMED.
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::kCondition(cb),
                escrow::kCancelTime(env.now() + 1s),
                Ter(temMALFORMED));
        }
    }

    void
    testMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bruce = Account("bruce");
        auto const carol = Account("carol");

        {
            testcase("Metadata to self");

            Env env(*this, features);
            env.fund(XRP(5000), alice, bruce, carol);
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bruce);

            env(escrow::create(alice, alice, XRP(1000)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);

            {
                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            env(escrow::create(bruce, bruce, XRP(1000)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bb);

            {
                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bb) == bod.end());
            }
        }
        {
            testcase("Metadata to other");

            Env env(*this, features);
            env.fund(XRP(5000), alice, bruce, carol);
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bruce);

            env(escrow::create(alice, bruce, XRP(1000)), escrow::kFinishTime(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bruce, carol, XRP(1000)),
                escrow::kFinishTime(env.now() + 1s),
                escrow::kCancelTime(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] == static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bc);

            {
                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(cod.begin(), cod.end(), bc) != cod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bruce.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
            }

            env.close(5s);
            env(escrow::cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));

                xrpl::Dir const aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                xrpl::Dir const bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    // NOLINTNEXTLINE(modernize-use-ranges)
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                xrpl::Dir const cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 0);
            }
        }
    }

    void
    testConsequences(FeatureBitset features)
    {
        testcase("Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env(*this, features);
        auto const baseFee = env.current()->fees().base;

        env.memoize("alice");
        env.memoize("bob");
        env.memoize("carol");

        {
            auto const jtx = env.jt(
                escrow::create("alice", "carol", XRP(1000)),
                escrow::kFinishTime(env.now() + 1s),
                Seq(1),
                Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(1000));
        }

        {
            auto const jtx = env.jt(escrow::cancel("bob", "alice", 3), Seq(1), Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx = env.jt(escrow::finish("bob", "alice", 3), Seq(1), Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }
    }

    void
    testEscrowWithTickets(FeatureBitset features)
    {
        testcase("Escrow with tickets");

        using namespace jtx;
        using namespace std::chrono;
        Account const alice{"alice"};
        Account const bob{"bob"};

        {
            // Create escrow and finish using tickets.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), alice, bob);
            env.close();

            // alice creates a ticket.
            std::uint32_t const aliceTicket{env.seq(alice) + 1};
            env(ticket::create(alice, 1));

            // bob creates a bunch of tickets because he will be burning
            // through them with tec transactions.  Just because we can
            // we'll use them up starting from largest and going smaller.
            static constexpr std::uint32_t kBobTicketCount{20};
            env(ticket::create(bob, kBobTicketCount));
            env.close();
            std::uint32_t bobTicket{env.seq(bob)};
            env.require(tickets(alice, 1));
            env.require(tickets(bob, kBobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::kFinishTime(ts),
                ticket::Use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, kBobTicketCount));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.  Note that each tec consumes one of bob's tickets.
            for (; env.now() < ts; env.close())
            {
                env(escrow::finish(bob, alice, escrowSeq),
                    Fee(150 * baseFee),
                    ticket::Use(--bobTicket),
                    Ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // bob tries to re-use a ticket, which is rejected.
            env(escrow::finish(bob, alice, escrowSeq),
                Fee(150 * baseFee),
                ticket::Use(bobTicket),
                Ter(tefNO_TICKET));

            // bob uses one of his remaining tickets.  Success!
            env(escrow::finish(bob, alice, escrowSeq),
                Fee(150 * baseFee),
                ticket::Use(--bobTicket));
            env.close();
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);
        }
        {
            // Create escrow and cancel using tickets.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), alice, bob);
            env.close();

            // alice creates a ticket.
            std::uint32_t const aliceTicket{env.seq(alice) + 1};
            env(ticket::create(alice, 1));

            // bob creates a bunch of tickets because he will be burning
            // through them with tec transactions.
            static constexpr std::uint32_t kBobTicketCount{20};
            std::uint32_t bobTicket{env.seq(bob) + 1};
            env(ticket::create(bob, kBobTicketCount));
            env.close();
            env.require(tickets(alice, 1));
            env.require(tickets(bob, kBobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future.
            auto const ts = env.now() + 117s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::kCondition(escrow::kCb1),
                escrow::kCancelTime(ts),
                ticket::Use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, kBobTicketCount));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
            {
                env(escrow::cancel(bob, alice, escrowSeq),
                    Fee(150 * baseFee),
                    ticket::Use(bobTicket++),
                    Ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // Verify that a finish won't work anymore.
            env(escrow::finish(bob, alice, escrowSeq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(150 * baseFee),
                ticket::Use(bobTicket++),
                Ter(tecNO_PERMISSION));
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that the cancel succeeds.
            env(escrow::cancel(bob, alice, escrowSeq),
                Fee(150 * baseFee),
                ticket::Use(bobTicket++));
            env.close();
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that bob actually consumed his tickets.
            env.require(tickets(bob, env.seq(bob) - bobTicket));
        }
    }

    void
    testCredentials(FeatureBitset features)
    {
        testcase("Test with credentials");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dillon{"dillon "};
        Account const zelda{"zelda"};

        char const credType[] = "abcde";

        {
            // Credentials amendment not enabled
            Env env(*this, features - featureCredentials);
            env.fund(XRP(5000), alice, bob);
            env.close();

            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(1000)), escrow::kFinishTime(env.now() + 1s));
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::auth(bob, alice));
            env.close();

            std::string const credIdx =
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4";
            env(escrow::finish(bob, alice, seq), credentials::Ids({credIdx}), Ter(temDISABLED));
        }

        {
            Env env(*this, features);

            env.fund(XRP(5000), alice, bob, carol, dillon, zelda);
            env.close();

            env(credentials::create(carol, zelda, credType));
            env.close();
            auto const jv = credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(1000)), escrow::kFinishTime(env.now() + 50s));
            env.close();

            // Bob require pre-authorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // Fail, credentials not accepted
            env(escrow::finish(carol, alice, seq),
                credentials::Ids({credIdx}),
                Ter(tecBAD_CREDENTIALS));

            env.close();

            env(credentials::accept(carol, zelda, credType));
            env.close();

            // Fail, credentials doesn’t belong to root account
            env(escrow::finish(dillon, alice, seq),
                credentials::Ids({credIdx}),
                Ter(tecBAD_CREDENTIALS));

            // Fail, no depositPreauth
            env(escrow::finish(carol, alice, seq),
                credentials::Ids({credIdx}),
                Ter(tecNO_PERMISSION));

            env(deposit::authCredentials(bob, {{zelda, credType}}));
            env.close();

            // Success
            env.close();
            env(escrow::finish(carol, alice, seq), credentials::Ids({credIdx}));
            env.close();
        }

        {
            testcase("Escrow with credentials without depositPreauth");
            using namespace std::chrono;

            Env env(*this, features);

            env.fund(XRP(5000), alice, bob, carol, dillon, zelda);
            env.close();

            env(credentials::create(carol, zelda, credType));
            env.close();
            env(credentials::accept(carol, zelda, credType));
            env.close();
            auto const jv = credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(1000)), escrow::kFinishTime(env.now() + 50s));
            // time advance
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();

            // Succeed, Bob doesn't require pre-authorization
            env(escrow::finish(carol, alice, seq), credentials::Ids({credIdx}));
            env.close();

            {
                char const credType2[] = "random";

                env(credentials::create(bob, zelda, credType2));
                env.close();
                env(credentials::accept(bob, zelda, credType2));
                env.close();
                auto const credIdxBob =
                    credentials::ledgerEntry(env, bob, zelda, credType2)[jss::result][jss::index]
                        .asString();

                auto const seq = env.seq(alice);
                env(escrow::create(alice, bob, XRP(1000)), escrow::kFinishTime(env.now() + 1s));
                env.close();

                // Bob require pre-authorization
                env(fset(bob, asfDepositAuth));
                env.close();
                env(deposit::authCredentials(bob, {{zelda, credType}}));
                env.close();

                // Use any valid credentials if account == dst
                env(escrow::finish(bob, alice, seq), credentials::Ids({credIdxBob}));
                env.close();
            }
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnablement(features);
        testTiming(features);
        testTags(features);
        testDisallowXRP(features);
        testRequiresConditionOrFinishAfter(features);
        testFails(features);
        testLockup(features);
        testEscrowConditions(features);
        testMetaAndOwnership(features);
        testConsequences(features);
        testEscrowWithTickets(features);
        testCredentials(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
        testWithFeats(all - featureTokenEscrow);
        testTags(all - fixIncludeKeyletFields);
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, xrpl);

}  // namespace xrpl::test
