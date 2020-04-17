//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>

namespace ripple {

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
        auto const USD = gw["USD"];
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        Account const ali{"ali", KeyType::secp256k1};
        env.fund(XRP(10000), alice, bob, gw);
        env.memoize(ali);
        env.trust(USD(600), alice);
        env.trust(USD(700), bob);
        env(pay(gw, alice, USD(70)));
        env(pay(gw, bob, USD(50)));
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
        Json::Value pf_req;
        pf_req[jss::subcommand] = "create";
        pf_req[jss::source_account] = alice.human();
        pf_req[jss::destination_account] = bob.human();
        pf_req[jss::destination_amount] =
            bob["USD"](20).value().getJson(JsonOptions::none);
        jr = wsc->invoke("path_find", pf_req)[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::alternatives) && jr[jss::alternatives].isArray() &&
            jr[jss::alternatives].size() == 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        auto jt = env.jt(noop(alice));
        Serializer s;
        jt.stx->add(s);
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::engine_result) &&
            jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        env(signers(bob, 1, {{alice, 1}}), sig(bob));
        env(regkey(alice, ali));
        env.close();

        Json::Value set_tx;
        set_tx[jss::Account] = bob.human();
        set_tx[jss::TransactionType] = jss::AccountSet;
        set_tx[jss::Fee] = (8 * env.current()->fees().base).jsonClipped();
        set_tx[jss::Sequence] = env.seq(bob);
        set_tx[jss::SigningPubKey] = "";

        Json::Value sign_for;
        sign_for[jss::tx_json] = set_tx;
        sign_for[jss::account] = alice.human();
        sign_for[jss::secret] = ali.name();
        jr = env.rpc("json", "sign_for", to_string(sign_for))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        Json::Value ms_req;
        ms_req[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc(
            "json", "submit_multisigned", to_string(ms_req))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::engine_result) &&
            jr[jss::engine_result] == "tesSUCCESS");
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
        pf_req[jss::subcommand] = "create";
        pf_req[jss::source_account] = alice.human();
        pf_req[jss::destination_account] = bob.human();
        pf_req[jss::destination_amount] =
            bob["USD"](20).value().getJson(JsonOptions::none);
        jr = wsc->invoke("path_find", pf_req)[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::alternatives) && jr[jss::alternatives].isArray() &&
            jr[jss::alternatives].size() == 1);
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        jt = env.jt(noop(alice));
        s.erase();
        jt.stx->add(s);
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::engine_result) &&
            jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        env(signers(bob, 1, {{alice, 1}}), sig(bob));
        env(regkey(alice, ali));
        env.close();

        set_tx[jss::Account] = bob.human();
        set_tx[jss::TransactionType] = jss::AccountSet;
        set_tx[jss::Fee] = (8 * env.current()->fees().base).jsonClipped();
        set_tx[jss::Sequence] = env.seq(bob);
        set_tx[jss::SigningPubKey] = "";

        sign_for[jss::tx_json] = set_tx;
        sign_for[jss::account] = alice.human();
        sign_for[jss::secret] = ali.name();
        jr = env.rpc("json", "sign_for", to_string(sign_for))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        ms_req[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc(
            "json", "submit_multisigned", to_string(ms_req))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::engine_result) &&
            jr[jss::engine_result] == "tesSUCCESS");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // make the network amendment blocked...now all the same
        // requests should fail

        env.app().getOPs().setAmendmentBlocked();

        // ledger_accept
        jr = env.rpc("ledger_accept")[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // ledger_current
        jr = env.rpc("ledger_current")[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // owner_info
        jr = env.rpc("owner_info", alice.human())[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // path_find
        jr = wsc->invoke("path_find", pf_req)[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit
        jr = env.rpc("submit", strHex(s.slice()))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(jr[jss::status] == "error");
        BEAST_EXPECT(!jr.isMember(jss::warnings));

        // submit_multisigned
        set_tx[jss::Sequence] = env.seq(bob);
        sign_for[jss::tx_json] = set_tx;
        jr = env.rpc("json", "sign_for", to_string(sign_for))[jss::result];
        BEAST_EXPECT(jr[jss::status] == "success");
        ms_req[jss::tx_json] = jr[jss::tx_json];
        jr = env.rpc(
            "json", "submit_multisigned", to_string(ms_req))[jss::result];
        BEAST_EXPECT(
            jr.isMember(jss::error) && jr[jss::error] == "amendmentBlocked");
        BEAST_EXPECT(!jr.isMember(jss::warnings));
    }

public:
    void
    run() override
    {
        testBlockedMethods();
    }
};

BEAST_DEFINE_TESTSUITE(AmendmentBlocked, app, ripple);

}  // namespace ripple
