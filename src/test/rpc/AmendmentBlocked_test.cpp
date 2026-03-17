#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

class AmendmentBlocked_test : public beast::unit_test::suite
{
    void
    testBlockedMethods()
    {
        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString("[" SECTION_SIGNING_SUPPORT "]\ntrue");
                    return cfg;
                })};
        auto const gw = Account{"gateway"};
        auto const usd = gw["USD"];
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        Account const ali{"ali", KeyType::secp256k1};
        env.fund(XRP(10000), alice, bob, gw);
        env.memoize(ali);
        // This close() ensures that all the accounts get created and their
        // default ripple flag gets set before the trust lines are created.
        // Without it, the ordering manages to create alice's trust line with
        // noRipple set on gw's end. The existing tests pass either way, but
        // better to do it right.
        env.close();
        env.trust(usd(600), alice);
        env.trust(usd(700), bob);
        env(pay(gw, alice, usd(70)));
        env(pay(gw, bob, usd(50)));
        env.close();

        auto wsc = test::makeWSClient(env.app().config());

        auto current = env.current();
        // ledger_accept
        auto jr = env.rpc("ledger_accept")[jss::result];
        BEAST_EXPECT(jr[jss::ledger_current_index] == current->seq() + 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // ledger_current
        jr = env.rpc("ledger_current")[jss::result];
        BEAST_EXPECT(jr[jss::ledger_current_index] == current->seq() + 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // owner_info
        jr = env.rpc("owner_info", alice.human())[jss::result];
        BEAST_EXPECT(jr.isMember(jss::accepted) && jr.isMember(jss::current));
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // path_find
        Json::Value pfReq;
        pfReq[jss::subcommand] = "create";
        pfReq[jss::source_account] = alice.human();
        pfReq[jss::destination_account] = bob.human();
        pfReq[jss::destination_amount] = bob["USD"](20).value().getJson(JsonOptions::none);
        jr = wsc->invoke("path_find", pfReq)[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::alternatives) && jr[jss::alternatives].isArray() &&
            jr[jss::alternatives].size() == 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        auto jt = env.jt(noop(alice));
        Serializer s;
        jt.stx->add(s);
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::engine_result) && jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        env(signers(bob, 1, {{alice, 1}}), sig(bob));
        env(regkey(alice, ali));
        env.close();

        Json::Value setTx;
        setTx[jss::Account] = bob.human();
        setTx[jss::TransactionType] = jss::AccountSet;
        setTx[jss::Fee] = (8 * env.current()->fees().base).jsonClipped();
        setTx[jss::Sequence] = env.seq(bob);
        setTx[jss::SigningPubKey] = "";

        Json::Value signFor;
        signFor[jss::tx_json] = setTx;
        signFor[jss::account] = alice.human();
        signFor[jss::secret] = ali.name();
        jr = env.rpc("json", "sign_for", to_string(signFor))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        Json::Value msReq;
        msReq[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc("json", "submit_multisigned", to_string(msReq))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::engine_result) && jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // set up an amendment warning. Nothing changes

        env.app().getOPs().setAmendmentWarned();

        current = env.current();
        // ledger_accept
        jr = env.rpc("ledger_accept")[jss::result];
        BEAST_EXPECT(jr[jss::ledger_current_index] == current->seq() + 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // ledger_current
        jr = env.rpc("ledger_current")[jss::result];
        BEAST_EXPECT(jr[jss::ledger_current_index] == current->seq() + 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // owner_info
        jr = env.rpc("owner_info", alice.human())[jss::result];
        BEAST_EXPECT(jr.isMember(jss::accepted) && jr.isMember(jss::current));
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // path_find
        pfReq[jss::subcommand] = "create";
        pfReq[jss::source_account] = alice.human();
        pfReq[jss::destination_account] = bob.human();
        pfReq[jss::destination_amount] = bob["USD"](20).value().getJson(JsonOptions::none);
        jr = wsc->invoke("path_find", pfReq)[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::alternatives) && jr[jss::alternatives].isArray() &&
            jr[jss::alternatives].size() == 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        jt = env.jt(noop(alice));
        s.erase();
        jt.stx->add(s);
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::engine_result) && jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        env(signers(bob, 1, {{alice, 1}}), sig(bob));
        env(regkey(alice, ali));
        env.close();

        setTx[jss::Account] = bob.human();
        setTx[jss::TransactionType] = jss::AccountSet;
        setTx[jss::Fee] = (8 * env.current()->fees().base).jsonClipped();
        setTx[jss::Sequence] = env.seq(bob);
        setTx[jss::SigningPubKey] = "";

        signFor[jss::tx_json] = setTx;
        signFor[jss::account] = alice.human();
        signFor[jss::secret] = ali.name();
        jr = env.rpc("json", "sign_for", to_string(signFor))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        msReq[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc("json", "submit_multisigned", to_string(msReq))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::engine_result) && jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // make the network amendment blocked...now all the same
        // requests should fail

        env.app().getOPs().setAmendmentBlocked();

        // ledger_accept
        jr = env.rpc("ledger_accept")[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // ledger_current
        jr = env.rpc("ledger_current")[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // owner_info
        jr = env.rpc("owner_info", alice.human())[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // path_find
        jr = wsc->invoke("path_find", pfReq)[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        setTx[jss::Sequence] = env.seq(bob);
        signFor[jss::tx_json] = setTx;
        jr = env.rpc("json", "sign_for", to_string(signFor))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        msReq[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc("json", "submit_multisigned", to_string(msReq))[jss::result];
        BEAST_EXPECT(jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(!jr.isMember(jss::warnings));
    }

public:
    void
    run() override
    {
        testBlockedMethods();
    }
};

BEAST_DEFINE_TESTSUITE(AmendmentBlocked, rpc, xrpl);

}  // namespace xrpl
