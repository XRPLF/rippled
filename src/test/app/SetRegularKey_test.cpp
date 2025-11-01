#include <test/jtx.h>

#include <xrpl/protocol/Feature.h>

namespace ripple {

class SetRegularKey_test : public beast::unit_test::suite
{
public:
    void
    testDisabledMasterKey()
    {
        using namespace test::jtx;

        testcase("Set regular key");
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        env(regkey(alice, bob));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice));

        testcase("Disable master key");
        env(fset(alice, asfDisableMaster), sig(alice));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice), ter(tefMASTER_DISABLED));

        testcase("Re-enable master key");
        env(fclear(alice, asfDisableMaster),
            sig(alice),
            ter(tefMASTER_DISABLED));

        env(fclear(alice, asfDisableMaster), sig(bob));
        env(noop(alice), sig(bob));
        env(noop(alice), sig(alice));

        testcase("Revoke regular key");
        env(regkey(alice, disabled));
        env(noop(alice), sig(bob), ter(tefBAD_AUTH));
        env(noop(alice), sig(alice));
    }

    void
    testDisabledRegularKey()
    {
        using namespace test::jtx;

        testcase("Set regular key to master key");
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        env.fund(XRP(10000), alice);

        env(regkey(alice, alice), ter(temBAD_REGKEY));
    }

    void
    testPasswordSpent()
    {
        using namespace test::jtx;

        testcase("Password spent");
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        auto ar = env.le(alice);
        BEAST_EXPECT(
            ar->isFieldPresent(sfFlags) &&
            ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) == 0));

        env(regkey(alice, bob), sig(alice), fee(0));

        ar = env.le(alice);
        BEAST_EXPECT(
            ar->isFieldPresent(sfFlags) &&
            ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) ==
             lsfPasswordSpent));

        // The second SetRegularKey transaction with Fee=0 should fail.
        env(regkey(alice, bob), sig(alice), fee(0), ter(telINSUF_FEE_P));

        env.trust(bob["USD"](1), alice);
        env(pay(bob, alice, bob["USD"](1)));
        ar = env.le(alice);
        BEAST_EXPECT(
            ar->isFieldPresent(sfFlags) &&
            ((ar->getFieldU32(sfFlags) & lsfPasswordSpent) == 0));
    }

    void
    testUniversalMask()
    {
        using namespace test::jtx;

        testcase("Universal mask");
        Env env(*this);
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        auto jv = regkey(alice, bob);
        jv[sfFlags.fieldName] = tfUniversalMask;
        env(jv, ter(temINVALID_FLAG));
    }

    void
    testTicketRegularKey()
    {
        using namespace test::jtx;

        testcase("Ticket regular key");
        Env env{*this};
        Account const alice{"alice", KeyType::ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // alice makes herself some tickets.
        env(ticket::create(alice, 4));
        env.close();
        std::uint32_t ticketSeq{env.seq(alice)};

        // Make sure we can give a regular key using a ticket.
        Account const alie{"alie", KeyType::secp256k1};
        env(regkey(alice, alie), ticket::use(--ticketSeq));
        env.close();

        // Disable alice's master key using a ticket.
        env(fset(alice, asfDisableMaster),
            sig(alice),
            ticket::use(--ticketSeq));
        env.close();

        // alice should be able to sign using the regular key but not the
        // master key.
        std::uint32_t const aliceSeq{env.seq(alice)};
        env(noop(alice), sig(alice), ter(tefMASTER_DISABLED));
        env(noop(alice), sig(alie), ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Re-enable the master key using a ticket.
        env(fclear(alice, asfDisableMaster),
            sig(alie),
            ticket::use(--ticketSeq));
        env.close();

        // Disable the regular key using a ticket.
        env(regkey(alice, disabled), sig(alie), ticket::use(--ticketSeq));
        env.close();

        // alice should be able to sign using the master key but not the
        // regular key.
        env(noop(alice), sig(alice), ter(tesSUCCESS));
        env(noop(alice), sig(alie), ter(tefBAD_AUTH));
        env.close();
    }

    void
    run() override
    {
        testDisabledMasterKey();
        testDisabledRegularKey();
        testPasswordSpent();
        testUniversalMask();
        testTicketRegularKey();
    }
};

BEAST_DEFINE_TESTSUITE(SetRegularKey, app, ripple);

}  // namespace ripple
