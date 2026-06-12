#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace xrpl {

class Clawback_test : public beast::unit_test::Suite
{
    template <class T>
    static std::string
    toString(T const& t)
    {
        return boost::lexical_cast<std::string>(t);
    }

    // Helper function that returns the number of tickets held by an account.
    static std::uint32_t
    ticketCount(test::jtx::Env const& env, test::jtx::Account const& acct)
    {
        std::uint32_t ret{0};
        if (auto const sleAcct = env.le(acct))
            ret = sleAcct->at(~sfTicketCount).value_or(0);
        return ret;
    }

    // Helper function that returns the freeze status of a trustline
    static bool
    getLineFreezeFlag(
        test::jtx::Env const& env,
        test::jtx::Account const& src,
        test::jtx::Account const& dst,
        Currency const& cur)
    {
        if (auto sle = env.le(keylet::line(src, dst, cur)))
        {
            auto const useHigh = src.id() > dst.id();
            return sle->isFlag(useHigh ? lsfHighFreeze : lsfLowFreeze);
        }
        Throw<std::runtime_error>("No line in getLineFreezeFlag");
        return false;  // silence warning
    }

    void
    testAllowTrustLineClawbackFlag(FeatureBitset features)
    {
        testcase("Enable AllowTrustLineClawback flag");
        using namespace test::jtx;

        // Test that one can successfully set asfAllowTrustLineClawback flag.
        // If successful, asfNoFreeze can no longer be set.
        // Also, asfAllowTrustLineClawback cannot be cleared.
        {
            Env env(*this, features);
            Account const alice{"alice"};

            env.fund(XRP(1000), alice);
            env.close();

            // set asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // clear asfAllowTrustLineClawback does nothing
            env(fclear(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // asfNoFreeze cannot be set when asfAllowTrustLineClawback is set
            env.require(Nflags(alice, asfNoFreeze));
            env(fset(alice, asfNoFreeze), Ter(tecNO_PERMISSION));
            env.close();
        }

        // Test that asfAllowTrustLineClawback cannot be set when
        // asfNoFreeze has been set
        {
            Env env(*this, features);
            Account const alice{"alice"};

            env.fund(XRP(1000), alice);
            env.close();

            env.require(Nflags(alice, asfNoFreeze));

            // set asfNoFreeze
            env(fset(alice, asfNoFreeze));
            env.close();

            // NoFreeze is set
            env.require(Flags(alice, asfNoFreeze));

            // asfAllowTrustLineClawback cannot be set if asfNoFreeze is set
            env(fset(alice, asfAllowTrustLineClawback), Ter(tecNO_PERMISSION));
            env.close();

            env.require(Nflags(alice, asfAllowTrustLineClawback));
        }

        // Test that asfAllowTrustLineClawback is not allowed when owner dir is
        // non-empty
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const usd = alice["USD"];
            env.require(Nflags(alice, asfAllowTrustLineClawback));

            // alice issues 10 USD to bob
            env.trust(usd(1000), bob);
            env(pay(alice, bob, usd(10)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice fails to enable clawback because she has trustline with bob
            env(fset(alice, asfAllowTrustLineClawback), Ter(tecOWNERS));
            env.close();

            // bob sets trustline to default limit and pays alice back to delete
            // the trustline
            env(trust(bob, usd(0), 0));
            env(pay(bob, alice, usd(10)));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // alice now is able to set asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
        }

        // Test that one cannot enable asfAllowTrustLineClawback when
        // featureClawback amendment is disabled
        {
            Env env(*this, features - featureClawback);

            Account const alice{"alice"};

            env.fund(XRP(1000), alice);
            env.close();

            env.require(Nflags(alice, asfAllowTrustLineClawback));

            // alice attempts to set asfAllowTrustLineClawback flag while
            // amendment is disabled. no error is returned, but the flag remains
            // to be unset.
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Nflags(alice, asfAllowTrustLineClawback));

            // now enable clawback amendment
            env.enableFeature(featureClawback);
            env.close();

            // asfAllowTrustLineClawback can be set
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));
        }
    }

    void
    testValidation(FeatureBitset features)
    {
        testcase("Validation");
        using namespace test::jtx;

        // Test that Clawback tx fails for the following:
        // 1. when amendment is disabled
        // 2. when asfAllowTrustLineClawback flag has not been set
        {
            Env env(*this, features - featureClawback);

            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            env.require(Nflags(alice, asfAllowTrustLineClawback));

            auto const usd = alice["USD"];

            // alice issues 10 USD to bob
            env.trust(usd(1000), bob);
            env(pay(alice, bob, usd(10)));
            env.close();

            env.require(Balance(bob, alice["USD"](10)));
            env.require(Balance(alice, bob["USD"](-10)));

            // clawback fails because amendment is disabled
            env(claw(alice, bob["USD"](5)), Ter(temDISABLED));
            env.close();

            // now enable clawback amendment
            env.enableFeature(featureClawback);
            env.close();

            // clawback fails because asfAllowTrustLineClawback has not been set
            env(claw(alice, bob["USD"](5)), Ter(tecNO_PERMISSION));
            env.close();

            env.require(Balance(bob, alice["USD"](10)));
            env.require(Balance(alice, bob["USD"](-10)));
        }

        // Test that Clawback tx fails for the following:
        // 1. invalid flag
        // 2. negative STAmount
        // 3. zero STAmount
        // 4. XRP amount
        // 5. `account` and `issuer` fields are same account
        // 6. trustline has a balance of 0
        // 7. trustline does not exist
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            auto const usd = alice["USD"];

            // alice issues 10 USD to bob
            env.trust(usd(1000), bob);
            env(pay(alice, bob, usd(10)));
            env.close();

            env.require(Balance(bob, alice["USD"](10)));
            env.require(Balance(alice, bob["USD"](-10)));

            // fails due to invalid flag
            env(claw(alice, bob["USD"](5)), Txflags(0x00008000), Ter(temINVALID_FLAG));
            env.close();

            // fails due to negative amount
            env(claw(alice, bob["USD"](-5)), Ter(temBAD_AMOUNT));
            env.close();

            // fails due to zero amount
            env(claw(alice, bob["USD"](0)), Ter(temBAD_AMOUNT));
            env.close();

            // fails because amount is in XRP
            env(claw(alice, XRP(10)), Ter(temBAD_AMOUNT));
            env.close();

            // fails when `issuer` field in `amount` is not token holder
            // NOTE: we are using the `issuer` field for the token holder
            env(claw(alice, alice["USD"](5)), Ter(temBAD_AMOUNT));
            env.close();

            // bob pays alice back, trustline has a balance of 0
            env(pay(bob, alice, usd(10)));
            env.close();

            // bob still owns the trustline that has 0 balance
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            env.require(Balance(bob, alice["USD"](0)));
            env.require(Balance(alice, bob["USD"](0)));

            // clawback fails because because balance is 0
            env(claw(alice, bob["USD"](5)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // set the limit to default, which should delete the trustline
            env(trust(bob, usd(0), 0));
            env.close();

            // bob no longer owns the trustline
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // clawback fails because trustline does not exist
            env(claw(alice, bob["USD"](5)), Ter(tecNO_LINE));
            env.close();
        }
    }

    void
    testPermission(FeatureBitset features)
    {
        // Checks the tx submitter has the permission to clawback.
        // Exercises preclaim code
        testcase("Permission");
        using namespace test::jtx;

        // Clawing back from an non-existent account returns error
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};

            // bob's account is not funded and does not exist
            env.fund(XRP(1000), alice);
            env.close();

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // bob, the token holder, does not exist
            env(claw(alice, bob["USD"](5)), Ter(terNO_ACCOUNT));
            env.close();
        }

        // Test that trustline cannot be clawed by someone who is
        // not the issuer of the currency
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            env.fund(XRP(1000), alice, bob, cindy);
            env.close();

            auto const usd = alice["USD"];

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // cindy sets asfAllowTrustLineClawback
            env(fset(cindy, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(cindy, asfAllowTrustLineClawback));

            // alice issues 1000 USD to bob
            env.trust(usd(1000), bob);
            env(pay(alice, bob, usd(1000)));
            env.close();

            env.require(Balance(bob, alice["USD"](1000)));
            env.require(Balance(alice, bob["USD"](-1000)));

            // cindy tries to claw from bob, and fails because trustline does
            // not exist
            env(claw(cindy, bob["USD"](200)), Ter(tecNO_LINE));
            env.close();
        }

        // When a trustline is created between issuer and holder,
        // we must make sure the holder is unable to claw back from
        // the issuer by impersonating the issuer account.
        //
        // This must be tested bidirectionally for both accounts because the
        // issuer could be either the low or high account in the trustline
        // object
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const usd = alice["USD"];
            auto const cad = bob["CAD"];

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // bob sets asfAllowTrustLineClawback
            env(fset(bob, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(bob, asfAllowTrustLineClawback));

            // alice issues 10 USD to bob.
            // bob then attempts to submit a clawback tx to claw USD from alice.
            // this must FAIL, because bob is not the issuer for this
            // trustline!!!
            {
                // bob creates a trustline with alice, and alice sends 10 USD to
                // bob
                env.trust(usd(1000), bob);
                env(pay(alice, bob, usd(10)));
                env.close();

                env.require(Balance(bob, alice["USD"](10)));
                env.require(Balance(alice, bob["USD"](-10)));

                // bob cannot claw back USD from alice because he's not the
                // issuer
                env(claw(bob, alice["USD"](5)), Ter(tecNO_PERMISSION));
                env.close();
            }

            // bob issues 10 CAD to alice.
            // alice then attempts to submit a clawback tx to claw CAD from bob.
            // this must FAIL, because alice is not the issuer for this
            // trustline!!!
            {
                // alice creates a trustline with bob, and bob sends 10 CAD to
                // alice
                env.trust(cad(1000), alice);
                env(pay(bob, alice, cad(10)));
                env.close();

                env.require(Balance(bob, alice["CAD"](-10)));
                env.require(Balance(alice, bob["CAD"](10)));

                // alice cannot claw back CAD from bob because she's not the
                // issuer
                env(claw(alice, bob["CAD"](5)), Ter(tecNO_PERMISSION));
                env.close();
            }
        }
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enable clawback");
        using namespace test::jtx;

        // Test that alice is able to successfully clawback tokens from bob
        Env env(*this, features);

        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const usd = alice["USD"];

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // alice issues 1000 USD to bob
        env.trust(usd(1000), bob);
        env(pay(alice, bob, usd(1000)));
        env.close();

        env.require(Balance(bob, alice["USD"](1000)));
        env.require(Balance(alice, bob["USD"](-1000)));

        // alice claws back 200 USD from bob
        env(claw(alice, bob["USD"](200)));
        env.close();

        // bob should have 800 USD left
        env.require(Balance(bob, alice["USD"](800)));
        env.require(Balance(alice, bob["USD"](-800)));

        // alice claws back 800 USD from bob again
        env(claw(alice, bob["USD"](800)));
        env.close();

        // trustline has a balance of 0
        env.require(Balance(bob, alice["USD"](0)));
        env.require(Balance(alice, bob["USD"](0)));
    }

    void
    testMultiLine(FeatureBitset features)
    {
        // Test scenarios where multiple trustlines are involved
        testcase("Multi line");
        using namespace test::jtx;

        // Both alice and bob issues their own "USD" to cindy.
        // When alice and bob tries to claw back, they will only
        // claw back from their respective trustline.
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            env.fund(XRP(1000), alice, bob, cindy);
            env.close();

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // bob sets asfAllowTrustLineClawback
            env(fset(bob, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(bob, asfAllowTrustLineClawback));

            // alice sends 1000 USD to cindy
            env.trust(alice["USD"](1000), cindy);
            env(pay(alice, cindy, alice["USD"](1000)));
            env.close();

            // bob sends 1000 USD to cindy
            env.trust(bob["USD"](1000), cindy);
            env(pay(bob, cindy, bob["USD"](1000)));
            env.close();

            // alice claws back 200 USD from cindy
            env(claw(alice, cindy["USD"](200)));
            env.close();

            // cindy has 800 USD left in alice's trustline after clawed by alice
            env.require(Balance(cindy, alice["USD"](800)));
            env.require(Balance(alice, cindy["USD"](-800)));

            // cindy still has 1000 USD in bob's trustline
            env.require(Balance(cindy, bob["USD"](1000)));
            env.require(Balance(bob, cindy["USD"](-1000)));

            // bob claws back 600 USD from cindy
            env(claw(bob, cindy["USD"](600)));
            env.close();

            // cindy has 400 USD left in bob's trustline after clawed by bob
            env.require(Balance(cindy, bob["USD"](400)));
            env.require(Balance(bob, cindy["USD"](-400)));

            // cindy still has 800 USD in alice's trustline
            env.require(Balance(cindy, alice["USD"](800)));
            env.require(Balance(alice, cindy["USD"](-800)));
        }

        // alice issues USD to both bob and cindy.
        // when alice claws back from bob, only bob's USD balance is
        // affected, and cindy's balance remains unchanged, and vice versa.
        {
            Env env(*this, features);

            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const cindy{"cindy"};

            env.fund(XRP(1000), alice, bob, cindy);
            env.close();

            auto const usd = alice["USD"];

            // alice sets asfAllowTrustLineClawback
            env(fset(alice, asfAllowTrustLineClawback));
            env.close();
            env.require(Flags(alice, asfAllowTrustLineClawback));

            // alice sends 600 USD to bob
            env.trust(usd(1000), bob);
            env(pay(alice, bob, usd(600)));
            env.close();

            env.require(Balance(alice, bob["USD"](-600)));
            env.require(Balance(bob, alice["USD"](600)));

            // alice sends 1000 USD to cindy
            env.trust(usd(1000), cindy);
            env(pay(alice, cindy, usd(1000)));
            env.close();

            env.require(Balance(alice, cindy["USD"](-1000)));
            env.require(Balance(cindy, alice["USD"](1000)));

            // alice claws back 500 USD from bob
            env(claw(alice, bob["USD"](500)));
            env.close();

            // bob's balance is reduced
            env.require(Balance(alice, bob["USD"](-100)));
            env.require(Balance(bob, alice["USD"](100)));

            // cindy's balance is unchanged
            env.require(Balance(alice, cindy["USD"](-1000)));
            env.require(Balance(cindy, alice["USD"](1000)));

            // alice claws back 300 USD from cindy
            env(claw(alice, cindy["USD"](300)));
            env.close();

            // bob's balance is unchanged
            env.require(Balance(alice, bob["USD"](-100)));
            env.require(Balance(bob, alice["USD"](100)));

            // cindy's balance is reduced
            env.require(Balance(alice, cindy["USD"](-700)));
            env.require(Balance(cindy, alice["USD"](700)));
        }
    }

    void
    testBidirectionalLine(FeatureBitset features)
    {
        testcase("Bidirectional line");
        using namespace test::jtx;

        // Test when both alice and bob issues USD to each other.
        // This scenario creates only one trustline.
        // In this case, both alice and bob can be seen as the "issuer"
        // and they can send however many USDs to each other.
        // We test that only the person who has a negative balance from their
        // perspective is allowed to clawback
        Env env(*this, features);

        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // bob sets asfAllowTrustLineClawback
        env(fset(bob, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(bob, asfAllowTrustLineClawback));

        // alice issues 1000 USD to bob
        env.trust(alice["USD"](1000), bob);
        env(pay(alice, bob, alice["USD"](1000)));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // bob is the holder, and alice is the issuer
        env.require(Balance(bob, alice["USD"](1000)));
        env.require(Balance(alice, bob["USD"](-1000)));

        // bob issues 1500 USD to alice
        env.trust(bob["USD"](1500), alice);
        env(pay(bob, alice, bob["USD"](1500)));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // bob has negative 500 USD because bob issued 500 USD more than alice
        // bob can now been seen as the issuer, while alice is the holder
        env.require(Balance(bob, alice["USD"](-500)));
        env.require(Balance(alice, bob["USD"](500)));

        // At this point, both alice and bob are the issuers of USD
        // and can send USD to each other through one trustline

        // alice fails to clawback. Even though she is also an issuer,
        // the trustline balance is positive from her perspective
        env(claw(alice, bob["USD"](200)), Ter(tecNO_PERMISSION));
        env.close();

        // bob is able to successfully clawback from alice because
        // the trustline balance is negative from his perspective
        env(claw(bob, alice["USD"](200)));
        env.close();

        env.require(Balance(bob, alice["USD"](-300)));
        env.require(Balance(alice, bob["USD"](300)));

        // alice pays bob 1000 USD
        env(pay(alice, bob, alice["USD"](1000)));
        env.close();

        // bob's balance becomes positive from his perspective because
        // alice issued more USD than the balance
        env.require(Balance(bob, alice["USD"](700)));
        env.require(Balance(alice, bob["USD"](-700)));

        // bob is now the holder and fails to clawback
        env(claw(bob, alice["USD"](200)), Ter(tecNO_PERMISSION));
        env.close();

        // alice successfully claws back
        env(claw(alice, bob["USD"](200)));
        env.close();

        env.require(Balance(bob, alice["USD"](500)));
        env.require(Balance(alice, bob["USD"](-500)));
    }

    void
    testDeleteDefaultLine(FeatureBitset features)
    {
        testcase("Delete default trustline");
        using namespace test::jtx;

        // If clawback results the trustline to be default,
        // trustline should be automatically deleted
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const usd = alice["USD"];

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // alice issues 1000 USD to bob
        env.trust(usd(1000), bob);
        env(pay(alice, bob, usd(1000)));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        env.require(Balance(bob, alice["USD"](1000)));
        env.require(Balance(alice, bob["USD"](-1000)));

        // set limit to default,
        env(trust(bob, usd(0), 0));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // alice claws back full amount from bob, and should also delete
        // trustline
        env(claw(alice, bob["USD"](1000)));
        env.close();

        // bob no longer owns the trustline because it was deleted
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    void
    testFrozenLine(FeatureBitset features)
    {
        testcase("Frozen trustline");
        using namespace test::jtx;

        // Claws back from frozen trustline
        // and the trustline should remain frozen
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const usd = alice["USD"];

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // alice issues 1000 USD to bob
        env.trust(usd(1000), bob);
        env(pay(alice, bob, usd(1000)));
        env.close();

        env.require(Balance(bob, alice["USD"](1000)));
        env.require(Balance(alice, bob["USD"](-1000)));

        // freeze trustline
        env(trust(alice, bob["USD"](0), tfSetFreeze));
        env.close();

        // alice claws back 200 USD from bob
        env(claw(alice, bob["USD"](200)));
        env.close();

        // bob should have 800 USD left
        env.require(Balance(bob, alice["USD"](800)));
        env.require(Balance(alice, bob["USD"](-800)));

        // trustline remains frozen
        BEAST_EXPECT(getLineFreezeFlag(env, alice, bob, usd.currency));
    }

    void
    testAmountExceedsAvailable(FeatureBitset features)
    {
        testcase("Amount exceeds available");
        using namespace test::jtx;

        // When alice tries to claw back an amount that is greater
        // than what bob holds, only the max available balance is clawed
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const usd = alice["USD"];

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // alice issues 1000 USD to bob
        env.trust(usd(1000), bob);
        env(pay(alice, bob, usd(1000)));
        env.close();

        env.require(Balance(bob, alice["USD"](1000)));
        env.require(Balance(alice, bob["USD"](-1000)));

        // alice tries to claw back 2000 USD
        env(claw(alice, bob["USD"](2000)));
        env.close();

        // check alice and bob's balance.
        // alice was only able to claw back 1000 USD at maximum
        env.require(Balance(bob, alice["USD"](0)));
        env.require(Balance(alice, bob["USD"](0)));

        // bob still owns the trustline because trustline is not in default
        // state
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 1);

        // set limit to default,
        env(trust(bob, usd(0), 0));
        env.close();

        // verify that bob's trustline was deleted
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
    }

    void
    testTickets(FeatureBitset features)
    {
        testcase("Tickets");
        using namespace test::jtx;

        // Tests clawback with tickets
        Env env(*this, features);
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const usd = alice["USD"];

        // alice sets asfAllowTrustLineClawback
        env(fset(alice, asfAllowTrustLineClawback));
        env.close();
        env.require(Flags(alice, asfAllowTrustLineClawback));

        // alice issues 100 USD to bob
        env.trust(usd(1000), bob);
        env(pay(alice, bob, usd(100)));
        env.close();

        env.require(Balance(bob, alice["USD"](100)));
        env.require(Balance(alice, bob["USD"](-100)));

        // alice creates 10 tickets
        std::uint32_t ticketCnt = 10;
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, ticketCnt));
        env.close();
        std::uint32_t const aliceSeq{env.seq(alice)};
        BEAST_EXPECT(ticketCount(env, alice) == ticketCnt);
        BEAST_EXPECT(ownerCount(env, alice) == ticketCnt);

        while (ticketCnt > 0)
        {
            // alice claws back 5 USD using a ticket
            env(claw(alice, bob["USD"](5)), ticket::Use(aliceTicketSeq++));
            env.close();

            ticketCnt--;
            BEAST_EXPECT(ticketCount(env, alice) == ticketCnt);
            BEAST_EXPECT(ownerCount(env, alice) == ticketCnt);
        }

        // alice clawed back 50 USD total, trustline has 50 USD remaining
        env.require(Balance(bob, alice["USD"](50)));
        env.require(Balance(alice, bob["USD"](-50)));

        // Verify that the account sequence numbers did not advance.
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testAllowTrustLineClawbackFlag(features);
        testValidation(features);
        testPermission(features);
        testEnabled(features);
        testMultiLine(features);
        testBidirectionalLine(features);
        testDeleteDefaultLine(features);
        testFrozenLine(features);
        testAmountExceedsAvailable(features);
        testTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};

        testWithFeats(all - featureMPTokensV1);
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(Clawback, app, xrpl);
}  // namespace xrpl
