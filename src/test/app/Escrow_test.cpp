#include <test/jtx.h>

#include <xrpld/app/tx/applySteps.h>

#include <xrpl/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct Escrow_test : public beast::unit_test::suite
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
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::finish_time(env.now() + 1s));
        env.close();

        auto const seq1 = env.seq("alice");

        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150));
        env.close();
        env(escrow::finish("bob", "alice", seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150));

        auto const seq2 = env.seq("alice");

        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::condition(escrow::cb2),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 2s),
            fee(baseFee * 150));
        env.close();
        env(escrow::cancel("bob", "alice", seq2), fee(baseFee * 150));
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
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::finish_time(ts));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(escrow::finish("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            env(escrow::finish("bob", "alice", seq), fee(baseFee * 150));
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
                escrow::condition(escrow::cb1),
                escrow::cancel_time(ts));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(escrow::cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            // Verify that a finish won't work anymore.
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // Verify that the cancel will succeed
            env(escrow::cancel("bob", "alice", seq), fee(baseFee * 150));
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
                escrow::finish_time(fts),
                escrow::cancel_time(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(escrow::finish("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
                env(escrow::cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
            }

            // Verify that a cancel still won't work
            env(escrow::cancel("bob", "alice", seq),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // And verify that a finish will
            env(escrow::finish("bob", "alice", seq), fee(baseFee * 150));
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
                escrow::finish_time(fts),
                escrow::cancel_time(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(escrow::finish("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
                env(escrow::cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
            }

            // Continue advancing, verifying that the cancel won't complete
            // prematurely. At this point a finish would succeed.
            for (; env.now() < cts; env.close())
                env(escrow::cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            // Verify that finish will no longer work, since we are past the
            // cancel activation time.
            env(escrow::finish("bob", "alice", seq),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // And verify that a cancel will succeed.
            env(escrow::cancel("bob", "alice", seq), fee(baseFee * 150));
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
            escrow::finish_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // set source and dest tags
        auto const seq = env.seq(alice);

        env(escrow::create(alice, bob, XRP(1000)),
            escrow::finish_time(env.now() + 1s),
            stag(1),
            dtag(2));

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
            env(escrow::create("bob", "george", XRP(10)),
                escrow::finish_time(env.now() + 1s));
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
            escrow::cancel_time(env.now() + 90s),
            fee(baseFee * 150),
            ter(temMALFORMED));

        // Creating an escrow with only a cancel time and a condition is
        // allowed:
        auto const seq = env.seq("alice");
        env(escrow::create("alice", "bob", XRP(100)),
            escrow::cancel_time(env.now() + 90s),
            escrow::condition(escrow::cb1),
            fee(baseFee * 150));
        env.close();
        env(escrow::finish("carol", "alice", seq),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150));
        BEAST_EXPECT(env.balance("bob") == XRP(5100));

        // Creating an escrow with only a cancel time and a finish time is
        // allowed:
        auto const seqFt = env.seq("alice");
        env(escrow::create("alice", "bob", XRP(100)),
            escrow::finish_time(env.now()),  // Set finish time to now so that
                                             // we can call finish immediately.
            escrow::cancel_time(env.now() + 50s),
            fee(baseFee * 150));
        env.close();
        env(escrow::finish("carol", "alice", seqFt), fee(150 * baseFee));
        BEAST_EXPECT(
            env.balance("bob") ==
            XRP(5200));  // 5100 (from last transaction) + 100
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
            escrow::finish_time(env.now() + 5s),
            txflags(tfPassive),
            ter(temINVALID_FLAG));

        // Finish time is in the past
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::finish_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // Cancel time is in the past
        env(escrow::create("alice", "bob", XRP(1000)),
            escrow::condition(escrow::cb1),
            escrow::cancel_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // no destination account
        env(escrow::create("alice", "carol", XRP(1000)),
            escrow::finish_time(env.now() + 1s),
            ter(tecNO_DST));

        env.fund(XRP(5000), "carol");

        // Using non-XRP:
        bool const withTokenEscrow =
            env.current()->rules().enabled(featureTokenEscrow);
        {
            // tecNO_PERMISSION: token escrow is enabled but the issuer did not
            // set the asfAllowTrustLineLocking flag
            auto const txResult =
                withTokenEscrow ? ter(tecNO_PERMISSION) : ter(temBAD_AMOUNT);
            env(escrow::create("alice", "carol", Account("alice")["USD"](500)),
                escrow::finish_time(env.now() + 5s),
                txResult);
        }

        // Sending zero or no XRP:
        env(escrow::create("alice", "carol", XRP(0)),
            escrow::finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));
        env(escrow::create("alice", "carol", XRP(-1000)),
            escrow::finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));

        // Fail if neither CancelAfter nor FinishAfter are specified:
        env(escrow::create("alice", "carol", XRP(1)), ter(temBAD_EXPIRATION));

        // Fail if neither a FinishTime nor a condition are attached:
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::cancel_time(env.now() + 1s),
            ter(temMALFORMED));

        // Fail if FinishAfter has already passed:
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::finish_time(env.now() - 1s),
            ter(tecNO_PERMISSION));

        // If both CancelAfter and FinishAfter are set, then CancelAfter must
        // be strictly later than FinishAfter.
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 10s),
            escrow::cancel_time(env.now() + 10s),
            ter(temBAD_EXPIRATION));

        env(escrow::create("alice", "carol", XRP(1)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 10s),
            escrow::cancel_time(env.now() + 5s),
            ter(temBAD_EXPIRATION));

        // Carol now requires the use of a destination tag
        env(fset("carol", asfRequireDest));

        // missing destination tag
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::condition(escrow::cb1),
            escrow::cancel_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // Success!
        env(escrow::create("alice", "carol", XRP(1)),
            escrow::condition(escrow::cb1),
            escrow::cancel_time(env.now() + 1s),
            dtag(1));

        {  // Fail if the sender wants to send more than he has:
            auto const accountReserve = drops(env.current()->fees().reserve);
            auto const accountIncrement =
                drops(env.current()->fees().increment);

            env.fund(accountReserve + accountIncrement + XRP(50), "daniel");
            env(escrow::create("daniel", "bob", XRP(51)),
                escrow::finish_time(env.now() + 1s),
                ter(tecUNFUNDED));

            env.fund(accountReserve + accountIncrement + XRP(50), "evan");
            env(escrow::create("evan", "bob", XRP(50)),
                escrow::finish_time(env.now() + 1s),
                ter(tecUNFUNDED));

            env.fund(accountReserve, "frank");
            env(escrow::create("frank", "bob", XRP(1)),
                escrow::finish_time(env.now() + 1s),
                ter(tecINSUFFICIENT_RESERVE));
        }

        {  // Specify incorrect sequence number
            env.fund(XRP(5000), "hannah");
            auto const seq = env.seq("hannah");
            env(escrow::create("hannah", "hannah", XRP(10)),
                escrow::finish_time(env.now() + 1s),
                fee(150 * baseFee));
            env.close();
            env(escrow::finish("hannah", "hannah", seq + 7),
                fee(150 * baseFee),
                ter(tecNO_TARGET));
        }

        {  // Try to specify a condition for a non-conditional payment
            env.fund(XRP(5000), "ivan");
            auto const seq = env.seq("ivan");

            env(escrow::create("ivan", "ivan", XRP(10)),
                escrow::finish_time(env.now() + 1s));
            env.close();
            env(escrow::finish("ivan", "ivan", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
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
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(escrow::finish("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(baseFee)));
        }
        {
            // Unconditionally pay from Alice to Bob.  Zelda (neither source nor
            // destination) signs all cancels and finishes.  This shows that
            // Escrow will make a payment to Bob with no intervention from Bob.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(escrow::cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(escrow::finish("zelda", "alice", seq));
            env.close();

            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.require(balance("bob", XRP(6000)));
            env.require(balance("zelda", XRP(5000) - drops(4 * baseFee)));
        }
        {
            // Bob sets DepositAuth so only Bob can finish the escrow.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish will only succeed for
            // Bob, because of DepositAuth.
            env(escrow::cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq));
            env.close();

            env.require(balance("alice", XRP(4000) - (baseFee * 5)));
            env.require(balance("bob", XRP(6000) - (baseFee * 5)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 4)));
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
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(escrow::finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq));
            env.close();

            env.require(balance("alice", XRP(4000) - (baseFee * 2)));
            env.require(balance("bob", XRP(6000) - (baseFee * 2)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 1)));
        }
        {
            // Conditional
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(escrow::cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish is possible but
            // requires the fulfillment associated with the escrow.
            env(escrow::cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(escrow::finish("bob", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("alice", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            env.close();

            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::condition(escrow::cb3),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(escrow::finish("bob", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("alice", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));

            // Enable deposit authorization. After this only Alice can finish
            // the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(escrow::finish("alice", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(escrow::finish("alice", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth and DepositPreauth.
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow::create("alice", "alice", XRP(1000)),
                escrow::condition(escrow::cb3),
                escrow::finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // Alice preauthorizes Zelda for deposit, even though Alice has not
            // set the lsfDepositAuth flag (yet).
            env(deposit::auth("alice", "zelda"));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(escrow::finish("alice", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("zelda", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));

            // Alice enables deposit authorization. After this only Alice or
            // Zelda (because Zelda is preauthorized) can finish the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(escrow::finish("alice", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(escrow::finish("zelda", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee));
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
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.require(balance("carol", XRP(5000)));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish without a fulfillment
            env(escrow::finish("bob", "alice", seq),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with a condition instead of a fulfillment
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::cb1),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::cb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::cb3),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with an incorrect condition and various
            // combinations of correct and incorrect fulfillments.
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb1),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with the correct condition & fulfillment
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(150 * baseFee));

            // SLE removed on finish
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(balance("carol", XRP(6000)));
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow::cancel("bob", "carol", 1), ter(tecNO_TARGET));
        }
        {  // Test cancel when condition is present
            Env env(*this, features);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(escrow::cb2),
                escrow::cancel_time(env.now() + 1s));
            env.close();
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            // balance restored on cancel
            env(escrow::cancel("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(baseFee)));
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
                escrow::condition(escrow::cb3),
                escrow::cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            // cancel fails before expiration
            env(escrow::cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.close();
            // finish fails after expiration
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("carol", XRP(5000)));
        }
        {  // Test long & short conditions during creation
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> v;
            v.resize(escrow::cb1.size() + 2, 0x78);
            std::memcpy(v.data() + 1, escrow::cb1.data(), escrow::cb1.size());

            auto const p = v.data();
            auto const s = v.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p, s}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p, s - 1}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p, s - 2}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p + 1, s - 1}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p + 1, s - 3}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p + 2, s - 2}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p + 2, s - 3}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{p + 1, s - 2}),
                escrow::cancel_time(ts),
                fee(10 * baseFee));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(150 * baseFee));
            env.require(balance("alice", XRP(4000) - drops(10 * baseFee)));
            env.require(balance("bob", XRP(5000) - drops(150 * baseFee)));
            env.require(balance("carol", XRP(6000)));
        }
        {  // Test long and short conditions & fulfillments during finish
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> cv;
            cv.resize(escrow::cb2.size() + 2, 0x78);
            std::memcpy(cv.data() + 1, escrow::cb2.data(), escrow::cb2.size());

            auto const cp = cv.data();
            auto const cs = cv.size();

            std::vector<std::uint8_t> fv;
            fv.resize(escrow::fb2.size() + 2, 0x13);
            std::memcpy(fv.data() + 1, escrow::fb2.data(), escrow::fb2.size());

            auto const fp = fv.data();
            auto const fs = fv.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp, cs}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp, cs - 1}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp, cs - 2}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp + 1, cs - 1}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp + 1, cs - 3}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp + 2, cs - 2}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp + 2, cs - 3}),
                escrow::cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::cancel_time(ts),
                fee(10 * baseFee));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp, cs}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp, cs - 1}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp, cs - 2}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 1}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 3}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 2, cs - 2}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 2, cs - 3}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct condition, try malformed fulfillments:
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp, fs - 1}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp, fs - 2}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp + 1, fs - 1}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp + 1, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp + 1, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp + 2, fs - 2}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{cp + 1, cs - 2}),
                escrow::fulfillment(Slice{fp + 2, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb2),
                escrow::fulfillment(escrow::fb2),
                fee(150 * baseFee));
            env.require(balance("alice", XRP(4000) - drops(10 * baseFee)));
            env.require(balance("carol", XRP(6000)));
        }
        {  // Test empty condition during creation and
           // empty condition & fulfillment during finish
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob", "carol");

            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(Slice{}),
                escrow::cancel_time(env.now() + 1s),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            auto const baseFee = env.current()->fees().base;
            env(escrow::create("alice", "carol", XRP(1000)),
                escrow::condition(escrow::cb3),
                escrow::cancel_time(env.now() + 1s));

            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{}),
                escrow::fulfillment(Slice{}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(Slice{}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(Slice{}),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));

            // Assemble finish that is missing the Condition or the Fulfillment
            // since either both must be present, or neither can:
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                ter(temMALFORMED));
            env(escrow::finish("bob", "alice", seq),
                escrow::fulfillment(escrow::fb3),
                ter(temMALFORMED));

            // Now finish it.
            env(escrow::finish("bob", "alice", seq),
                escrow::condition(escrow::cb3),
                escrow::fulfillment(escrow::fb3),
                fee(150 * baseFee));
            env.require(balance("carol", XRP(6000)));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
        }
        {  // Test a condition other than PreimageSha256, which
           // would require a separate amendment
            Env env(*this, features);
            env.fund(XRP(5000), "alice", "bob");

            std::array<std::uint8_t, 45> cb = {
                {0xA2, 0x2B, 0x80, 0x20, 0x42, 0x4A, 0x70, 0x49, 0x49,
                 0x52, 0x92, 0x67, 0xB6, 0x21, 0xB3, 0xD7, 0x91, 0x19,
                 0xD7, 0x29, 0xB2, 0x38, 0x2C, 0xED, 0x8B, 0x29, 0x6C,
                 0x3C, 0x02, 0x8F, 0xA9, 0x7D, 0x35, 0x0F, 0x6D, 0x07,
                 0x81, 0x03, 0x06, 0x34, 0xD2, 0x82, 0x02, 0x03, 0xC8}};

            // FIXME: this transaction should, eventually, return temDISABLED
            //        instead of temMALFORMED.
            env(escrow::create("alice", "bob", XRP(1000)),
                escrow::condition(cb),
                escrow::cancel_time(env.now() + 1s),
                ter(temMALFORMED));
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
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            env(escrow::create(bruce, bruce, XRP(1000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bb);

            {
                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) == bod.end());
            }
        }
        {
            testcase("Metadata to other");

            Env env(*this, features);
            env.fund(XRP(5000), alice, bruce, carol);
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bruce);

            env(escrow::create(alice, bruce, XRP(1000)),
                escrow::finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bruce, carol, XRP(1000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), bc) != cod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bruce.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
            }

            env.close(5s);
            env(escrow::cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
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
                escrow::finish_time(env.now() + 1s),
                seq(1),
                fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(1000));
        }

        {
            auto const jtx =
                env.jt(escrow::cancel("bob", "alice", 3), seq(1), fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx =
                env.jt(escrow::finish("bob", "alice", 3), seq(1), fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
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
            constexpr static std::uint32_t bobTicketCount{20};
            env(ticket::create(bob, bobTicketCount));
            env.close();
            std::uint32_t bobTicket{env.seq(bob)};
            env.require(tickets(alice, 1));
            env.require(tickets(bob, bobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::finish_time(ts),
                ticket::use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, bobTicketCount));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.  Note that each tec consumes one of bob's tickets.
            for (; env.now() < ts; env.close())
            {
                env(escrow::finish(bob, alice, escrowSeq),
                    fee(150 * baseFee),
                    ticket::use(--bobTicket),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // bob tries to re-use a ticket, which is rejected.
            env(escrow::finish(bob, alice, escrowSeq),
                fee(150 * baseFee),
                ticket::use(bobTicket),
                ter(tefNO_TICKET));

            // bob uses one of his remaining tickets.  Success!
            env(escrow::finish(bob, alice, escrowSeq),
                fee(150 * baseFee),
                ticket::use(--bobTicket));
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
            constexpr std::uint32_t bobTicketCount{20};
            std::uint32_t bobTicket{env.seq(bob) + 1};
            env(ticket::create(bob, bobTicketCount));
            env.close();
            env.require(tickets(alice, 1));
            env.require(tickets(bob, bobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future.
            auto const ts = env.now() + 117s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(ts),
                ticket::use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, bobTicketCount));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
            {
                env(escrow::cancel(bob, alice, escrowSeq),
                    fee(150 * baseFee),
                    ticket::use(bobTicket++),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // Verify that a finish won't work anymore.
            env(escrow::finish(bob, alice, escrowSeq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(150 * baseFee),
                ticket::use(bobTicket++),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that the cancel succeeds.
            env(escrow::cancel(bob, alice, escrowSeq),
                fee(150 * baseFee),
                ticket::use(bobTicket++));
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
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::finish_time(env.now() + 1s));
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::auth(bob, alice));
            env.close();

            std::string const credIdx =
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4";
            env(escrow::finish(bob, alice, seq),
                credentials::ids({credIdx}),
                ter(temDISABLED));
        }

        {
            Env env(*this, features);

            env.fund(XRP(5000), alice, bob, carol, dillon, zelda);
            env.close();

            env(credentials::create(carol, zelda, credType));
            env.close();
            auto const jv =
                credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::finish_time(env.now() + 50s));
            env.close();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // Fail, credentials not accepted
            env(escrow::finish(carol, alice, seq),
                credentials::ids({credIdx}),
                ter(tecBAD_CREDENTIALS));

            env.close();

            env(credentials::accept(carol, zelda, credType));
            env.close();

            // Fail, credentials doesn’t belong to root account
            env(escrow::finish(dillon, alice, seq),
                credentials::ids({credIdx}),
                ter(tecBAD_CREDENTIALS));

            // Fail, no depositPreauth
            env(escrow::finish(carol, alice, seq),
                credentials::ids({credIdx}),
                ter(tecNO_PERMISSION));

            env(deposit::authCredentials(bob, {{zelda, credType}}));
            env.close();

            // Success
            env.close();
            env(escrow::finish(carol, alice, seq), credentials::ids({credIdx}));
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
            auto const jv =
                credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(1000)),
                escrow::finish_time(env.now() + 50s));
            // time advance
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();

            // Succeed, Bob doesn't require preauthorization
            env(escrow::finish(carol, alice, seq), credentials::ids({credIdx}));
            env.close();

            {
                char const credType2[] = "fghijk";

                env(credentials::create(bob, zelda, credType2));
                env.close();
                env(credentials::accept(bob, zelda, credType2));
                env.close();
                auto const credIdxBob =
                    credentials::ledgerEntry(
                        env, bob, zelda, credType2)[jss::result][jss::index]
                        .asString();

                auto const seq = env.seq(alice);
                env(escrow::create(alice, bob, XRP(1000)),
                    escrow::finish_time(env.now() + 1s));
                env.close();

                // Bob require preauthorization
                env(fset(bob, asfDepositAuth));
                env.close();
                env(deposit::authCredentials(bob, {{zelda, credType}}));
                env.close();

                // Use any valid credentials if account == dst
                env(escrow::finish(bob, alice, seq),
                    credentials::ids({credIdxBob}));
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
        FeatureBitset const all{testable_amendments()};
        testWithFeats(all);
        testWithFeats(all - featureTokenEscrow);
        testTags(all - fixIncludeKeyletFields);
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, ripple);

}  // namespace test
}  // namespace ripple
