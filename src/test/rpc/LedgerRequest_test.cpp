

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <string>
#include <utility>

namespace xrpl::rpc {

class LedgerRequest_test : public beast::unit_test::Suite
{
    static constexpr char const* kHash1 =
        "8520BB17C478632B910814803743457CFDB94668B7B74CABB9A4511C03CC66AA";
    static constexpr char const* kAccountHash1 =
        "3BA579989E7A881E38F529962AA72740EEF951623012F4D4AADAB476FB8AA84B";

    static constexpr char const* kZeroHASH =
        "0000000000000000000000000000000000000000000000000000000000000000";

public:
    void
    testLedgerRequest()
    {
        using namespace test::jtx;

        Env env(*this);

        env.close();
        env.close();
        BEAST_EXPECT(env.current()->header().seq == 5);

        {
            // arbitrary text is converted to 0.
            auto const result = env.rpc("ledger_request", "arbitrary_text");
            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] == "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "-1");
            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] == "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "0");
            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] == "Ledger index too small");
        }

        {
            auto const result = env.rpc("ledger_request", "1");
            BEAST_EXPECT(
                !rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 1 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "2");
            BEAST_EXPECT(
                !rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 2 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());
        }

        {
            auto const result = env.rpc("ledger_request", "3");
            BEAST_EXPECT(
                !rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::ledger_index] == 3 &&
                result[jss::result].isMember(jss::ledger));
            BEAST_EXPECT(
                result[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                result[jss::result][jss::ledger][jss::ledger_hash].isString());

            auto const ledgerHash = result[jss::result][jss::ledger][jss::ledger_hash].asString();

            {
                auto const r = env.rpc("ledger_request", ledgerHash);
                BEAST_EXPECT(
                    !rpc::containsError(r[jss::result]) && r[jss::result][jss::ledger_index] == 3 &&
                    r[jss::result].isMember(jss::ledger));
                BEAST_EXPECT(
                    r[jss::result][jss::ledger].isMember(jss::ledger_hash) &&
                    r[jss::result][jss::ledger][jss::ledger_hash] == ledgerHash);
            }
        }

        {
            std::string const ledgerHash(64, 'q');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] ==
                    "Invalid field 'ledger_hash', not hex string.");
        }

        {
            std::string const ledgerHash(64, '1');

            auto const result = env.rpc("ledger_request", ledgerHash);

            BEAST_EXPECT(
                !rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::have_header] == false);
        }

        {
            auto const result = env.rpc("ledger_request", "4");
            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] == "Ledger index too large");
        }

        {
            auto const result = env.rpc("ledger_request", "5");
            BEAST_EXPECT(
                rpc::containsError(result[jss::result]) &&
                result[jss::result][jss::error_message] == "Ledger index too large");
        }
    }

    void
    testEvolution()
    {
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->fees.referenceFee = 10;
        Env env{*this, std::move(cfg), FeatureBitset{}};  // the hashes being checked below
                                                          // assume no amendments
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
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
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kZeroHASH);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == kAccountHash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == kZeroHASH);

        result = env.rpc("ledger_request", "2")[jss::result];
        static constexpr char const* kHash2 =
            "81B485208CBB93989F2D675F61D385CE9E15A03C68B8CEF1B071C0DD5C5781E1";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "2");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash2);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kHash1);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "A2C367B31313EACE2670747A0CC4947FB79055795FEEA1E730E31756E73E012C");
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == kZeroHASH);

        result = env.rpc("ledger_request", "3")[jss::result];
        static constexpr char const* kHash3 =
            "925EAD8F55A13B5138DB7E63122066305E3931431364A2F311A9F646D373D631";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "3");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "99999999999999980");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash3);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kHash2);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "400903DC93B7A8CF9ABC74216A900C187C8D3CAF1B40C1688D1965C73EF79C39");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "CBD7F0948EBFA2241DE4EA627939A0FFEE6B80A90FE09C42C825DA546E9B73FF");

        result = env.rpc("ledger_request", "4")[jss::result];
        static constexpr char const* kHash4 =
            "93EDC738C6FC00AC5992BAE98274F8B214719730C4A938FE177B4DDEC497C670";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "4");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "99999999999999960");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash4);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kHash3);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "5FC723ED8E323EDB2E37B4CE63070CF7D722F7BD35F9337BD55B6271FE146511");
        BEAST_EXPECT(
            result[jss::ledger][jss::transaction_hash] ==
            "9BBDDBF926100DFFF364E16268F544B19F5B9BC6ECCBBC104F98D13FA9F3BC35");

        result = env.rpc("ledger_request", "5")[jss::result];
        static constexpr char const* kHash5 =
            "0A069F4B2CE5D5892EBF7C1BB1F7C3A023BF67B7309D10CA5C5607F959DF4C2F";
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "5");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "99999999999999940");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash5);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kHash4);
        BEAST_EXPECT(
            result[jss::ledger][jss::account_hash] ==
            "0941305F3FD175CD3F956BF9840D811C937A5DD8C443019621F58496796B6FD4");
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
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw);
        env.close();

        {
            json::Value jvParams;
            jvParams[jss::ledger_hash] =
                "AB868A6CFEEC779C2FF845C0AF00A642259986AF40C01976A7F842B6918936"
                "C7";
            jvParams[jss::ledger_index] = "1";
            auto const result =
                env.rpc("json", "ledger_request", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Exactly one of 'ledger_hash' or 'ledger_index' can be "
                "specified.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "index";
            auto const result =
                env.rpc("json", "ledger_request", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'ledger_index', not number.");
        }

        // the purpose in this test is to force the ledger expiration/out of
        // date check to trigger
        env.timeKeeper().adjustCloseTime(weeks{3});
        auto const result = env.rpc(apiVersion, "ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::status] == "error");
        if (apiVersion == 1)
        {
            BEAST_EXPECT(result[jss::error] == "noCurrent");
            BEAST_EXPECT(result[jss::error_message] == "Current ledger is unavailable.");
        }
        else
        {
            BEAST_EXPECT(result[jss::error] == "notSynced");
            BEAST_EXPECT(result[jss::error_message] == "Not synced to the network.");
        }
    }

    void
    testMoreThan256Closed()
    {
        using namespace test::jtx;
        using namespace std::chrono_literals;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.referenceFee = 10;
                    cfg->nodeSize = 0;
                    return cfg;
                })};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw);

        int const maxLimit = 256;

        for (auto i = 0; i < maxLimit + 10; i++)
        {
            Account const bob{std::string("bob") + std::to_string(i)};
            env.fund(XRP(1000), bob);
            env.close();
        }

        auto result = env.rpc("ledger_request", "1")[jss::result];
        BEAST_EXPECT(result[jss::ledger][jss::ledger_index] == "1");
        BEAST_EXPECT(result[jss::ledger][jss::total_coins] == "100000000000000000");
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::ledger][jss::ledger_hash] == kHash1);
        BEAST_EXPECT(result[jss::ledger][jss::parent_hash] == kZeroHASH);
        BEAST_EXPECT(result[jss::ledger][jss::account_hash] == kAccountHash1);
        BEAST_EXPECT(result[jss::ledger][jss::transaction_hash] == kZeroHASH);
    }

    void
    testNonAdmin()
    {
        using namespace test::jtx;
        Env env{*this, envconfig(noAdmin)};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(100000), gw);

        env.setRetries(0);
        auto const result = env.rpc("ledger_request", "1")[jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error.  The JSONRPCClient just eats
        // that error and returns an null result.
        BEAST_EXPECT(result.type() == json::ValueType::Null);
    }

    void
    run() override
    {
        testLedgerRequest();
        testEvolution();
        forAllApiVersions([this](unsigned apiVersion) { testBadInput(apiVersion); });
        testMoreThan256Closed();
        testNonAdmin();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerRequest, rpc, xrpl);

}  // namespace xrpl::rpc
