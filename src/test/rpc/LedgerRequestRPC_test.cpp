#include <test/jtx.h>

#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <functional>

namespace ripple {

namespace RPC {

class LedgerRequestRPC_test : public beast::unit_test::suite
{
    static constexpr char const* hash1 =
        "3020EB9E7BE24EF7D7A060CB051583EC117384636D1781AFB5B87F3E348DA489";
    static constexpr char const* accounthash1 =
        "BD8A3D72CA73DDE887AD63666EC2BAD07875CBA997A102579B5B95ECDFFEAED8";

    static constexpr char const* zerohash =
        "0000000000000000000000000000000000000000000000000000000000000000";

public:
    void
    testLedgerRequest()
    {
        using namespace test::jtx;

        Env env(*this);

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->info().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const result = env.rpc("ledger_request", "arbitrary_text");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "-1");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "0");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "1");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 1 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "2");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 2 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "3");
            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 3 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());

            auto const ledgerHash =
                result[jss::result][jss::ledger][jss::ledger_hash].asString();

            {
                auto const r = env.rpc("ledger_request", ledgerHash);
                BEAST_EXPECT(
                    !RPC::contains_error(r[jss::result]) &&
                    r[jss::result][jss::ledger_index] == 3 &&
                    r[jss::result].isMember(jss::ledger));
                BEAST_EXPECT(
                    r[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                    r[jss::result][jss::ledger][jss::ledger_hash] ==
                        ledgerHash);
            }
        }

        {
            std::string ledgerHash(64, 'q');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Invalid field 'ledger_hash'.");
        }

        {
            std::string ledgerHash(64, '1');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                !RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::have_header] == false);
        }

        {
            auto const result = env.rpc("ledger_request", "4");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }

        {
            auto const result = env.rpc("ledger_request", "5");
            BEAST_EXPECT(
                RPC::contains_error(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Ledger index too large");
        }
    }

    void
    testEvolution()
    {
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->FEES.reference_fee = 10;
        Env env{
            *this,
            std::move(cfg),
            FeatureBitset{}};  // the hashes being checked below
                               // assume no amendments
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);
        env.close();

        env.memoize("bob");
        env.fund(XRP(1000), "bob");
        env.close();

        env.memoize("alice");
        env.fund(XRP(1000), "alice");
        env.close();

        env.memoize("carol");
        env.fund(XRP(1000), "carol");
        env.close();

        auto result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "1");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == zerohash);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == accounthash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);

        result = env.rpc("ledger_request", "2")[jss::result];
        constexpr char const* hash2 =
            "CCC3B3E88CCAC17F1BE6B4A648A55999411F19E3FE55EB721960EB0DF28EDDA5";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "2");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash2);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash1);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "3C834285F7F464FBE99AFEB84D354A968EB2CAA24523FF26797A973D906A3D29");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);

        result = env.rpc("ledger_request", "3")[jss::result];
        constexpr char const* hash3 =
            "9FFD8AE09190D5002FE4252A1B29EABCF40DABBCE3B42619C6BD0BE381D51103";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "3");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999980");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash3);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash2);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "35738B8517F37D08983AF6BC7DA483CCA9CF6B41B1FECB31A20028D78FE0BB22");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "CBD7F0948EBFA2241DE4EA627939A0FFEE6B80A90FE09C42C825DA546E9B73FF");

        result = env.rpc("ledger_request", "4")[jss::result];
        constexpr char const* hash4 =
            "7C9B614445517B8C6477E0AB10A35FFC1A23A34FEA41A91ECBDE884CC097C6E1";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "4");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999960");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash4);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash3);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "1EE701DD2A150205173E1EDE8D474DF6803EC95253DAAEE965B9D896CFC32A04");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "9BBDDBF926100DFFF364E16268F544B19F5B9BC6ECCBBC104F98D13FA9F3BC35");

        result = env.rpc("ledger_request", "5")[jss::result];
        constexpr char const* hash5 =
            "98885D02145CCE4AD2605F1809F17188DB2053B14ED399CAC985DD8E03DCA8C0";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "5");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "99999999999999940");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash5);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == hash4);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "41D64D64796468DEA7AE2A7282C0BB525D6FD7ABC29453C5E5BC6406E947CBCE");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "8FE8592EF22FBC2E8C774C7A1ED76AA3FCE64BED17D748CBA9AFDF7072FE36C7");

        result = env.rpc("ledger_request", "6")[jss::result];
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(result[jss::error_message] == "Ledger index too large");
    }

    void
    testBadInput(unsigned apiVersion)
    {
        using namespace test::jtx;
        Env env{*this};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);
        env.close();

        Json::Value jvParams;
        jvParams[jss::ledger_hash] =
            "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936C7";
        jvParams[jss::ledger_index] = "1";
        auto result = env.rpc(
            "json", "ledger_request", jvParams.toStyledString())[jss::result];
        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::status] == "error");
        BEAST_EXPECT(
            result[jss::error_message] ==
            "Exactly one of ledger_hash and ledger_index can be set.");

        // the purpose in this test is to force the ledger expiration/out of
        // date check to trigger
        env.timeKeeper().adjustCloseTime(weeks{3});
        result = env.rpc(apiVersion, "ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::status] == "error");
        if (apiVersion == 1)
        {
            BEAST_EXPECT(result[jss::error] == "noCurrent");
            BEAST_EXPECT(
                result[jss::error_message] == "Current ledger is unavailable.");
        }
        else
        {
            BEAST_EXPECT(result[jss::error] == "notSynced");
            BEAST_EXPECT(
                result[jss::error_message] == "Not synced to the network.");
        }
    }

    void
    testMoreThan256Closed()
    {
        using namespace test::jtx;
        using namespace std::chrono_literals;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->FEES.reference_fee = 10;
                    cfg->NODE_SIZE = 0;
                    return cfg;
                })};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        int const max_limit = 256;

        for (auto i = 0; i < max_limit + 10; i++)
        {
            Account const bob{std::string("bob") + std::to_string(i)};
            env.fund(XRP(1000), bob);
            env.close();
        }

        auto result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "1");
        BEAST_EXPECT(
            result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == hash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == zerohash);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == accounthash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == zerohash);
    }

    void
    testNonAdmin()
    {
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(100000), gw);

        env.set_retries(0);
        auto const result = env.rpc("ledger_request", "1")[jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error.  The JSONRPCClient just eats
        // that error and returns an null result.
        BEAST_EXPECT(result.type() == Json::nullValue);
    }

    void
    run() override
    {
        testLedgerRequest();
        testEvolution();
        forAllApiVersions(
            std::bind_front(&LedgerRequestRPC_test::testBadInput, this));
        testMoreThan256Closed();
        testNonAdmin();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRequestRPC, rpc, ripple);

}  // namespace RPC
}  // namespace ripple
