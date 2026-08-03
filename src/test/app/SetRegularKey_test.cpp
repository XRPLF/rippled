
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>

namespace xrpl {

class SetRegularKey_test : public beast::unit_test::Suite
{
public:
    void
    testDisabledMasterKey()
    {
        using namespace test::jtx;

        testcase("Set regular key");
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice, bob);

        env(regkey(alice, bob));
        env(noop(alice), Sig(bob));
        env(noop(alice), Sig(alice));

        testcase("Disable master key");
        env(fset(alice, asfDisableMaster), Sig(alice));
        env(noop(alice), Sig(bob));
        env(noop(alice), Sig(alice), Ter(tefMASTER_DISABLED));

        testcase("Re-enable master key");
        env(fclear(alice, asfDisableMaster), Sig(alice), Ter(tefMASTER_DISABLED));

        env(fclear(alice, asfDisableMaster), Sig(bob));
        env(noop(alice), Sig(bob));
        env(noop(alice), Sig(alice));

        testcase("Revoke regular key");
        env(regkey(alice, kDisabled));
        env(noop(alice), Sig(bob), Ter(tefBAD_AUTH));
        env(noop(alice), Sig(alice));
    }

    void
    testDisabledRegularKey()
    {
        using namespace test::jtx;

        testcase("Set regular key to master key");
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        env.fund(XRP(10000), alice);

        env(regkey(alice, alice), Ter(temBAD_REGKEY));
    }

    void
    testNoAlternativeKey()
    {
        using namespace test::jtx;

        testcase("Cannot remove last signing method");
        Env env{*this, testableAmendments()};
        Account const alice("alice");
        Account const bob("bob");
        env.fund(XRP(10000), alice);

        env(regkey(alice, bob));
        env(fset(alice, asfDisableMaster), Sig(alice));

        env(regkey(alice, kDisabled), Sig(bob), Ter(tecNO_ALTERNATIVE_KEY));

        auto const sle = env.le(alice);
        BEAST_EXPECT(
            sle && sle->isFlag(lsfDisableMaster) && sle->getAccountID(sfRegularKey) == bob.id());
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
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && !ar->isFlag(lsfPasswordSpent));

        env(regkey(alice, bob), Sig(alice), Fee(0));

        ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && ar->isFlag(lsfPasswordSpent));

        // The second SetRegularKey transaction with Fee=0 should fail.
        env(regkey(alice, bob), Sig(alice), Fee(0), Ter(telINSUF_FEE_P));

        env.trust(bob["USD"](1), alice);
        env(pay(bob, alice, bob["USD"](1)));
        ar = env.le(alice);
        BEAST_EXPECT(ar->isFieldPresent(sfFlags) && !ar->isFlag(lsfPasswordSpent));
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
        env(jv, Ter(temINVALID_FLAG));
    }

    void
    testTicketRegularKey()
    {
        using namespace test::jtx;

        testcase("Ticket regular key");
        Env env{*this};
        Account const alice{"alice", KeyType::Ed25519};
        env.fund(XRP(1000), alice);
        env.close();

        // alice makes herself some tickets.
        env(ticket::create(alice, 4));
        env.close();
        std::uint32_t ticketSeq{env.seq(alice)};

        // Make sure we can give a regular key using a ticket.
        Account const alie{"alie", KeyType::Secp256k1};
        env(regkey(alice, alie), ticket::Use(--ticketSeq));
        env.close();

        // Disable alice's master key using a ticket.
        env(fset(alice, asfDisableMaster), Sig(alice), ticket::Use(--ticketSeq));
        env.close();

        // alice should be able to sign using the regular key but not the
        // master key.
        std::uint32_t const aliceSeq{env.seq(alice)};
        env(noop(alice), Sig(alice), Ter(tefMASTER_DISABLED));
        env(noop(alice), Sig(alie), Ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSeq + 1);

        // Re-enable the master key using a ticket.
        env(fclear(alice, asfDisableMaster), Sig(alie), ticket::Use(--ticketSeq));
        env.close();

        // Disable the regular key using a ticket.
        env(regkey(alice, kDisabled), Sig(alie), ticket::Use(--ticketSeq));
        env.close();

        // alice should be able to sign using the master key but not the
        // regular key.
        env(noop(alice), Sig(alice), Ter(tesSUCCESS));
        env(noop(alice), Sig(alie), Ter(tefBAD_AUTH));
        env.close();
    }

    void
    run() override
    {
        testDisabledMasterKey();
        testDisabledRegularKey();
        testNoAlternativeKey();
        testPasswordSpent();
        testUniversalMask();
        testTicketRegularKey();
    }
};

BEAST_DEFINE_TESTSUITE(SetRegularKey, app, xrpl);

}  // namespace xrpl
