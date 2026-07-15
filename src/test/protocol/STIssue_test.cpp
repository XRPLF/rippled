
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>  // IWYU pragma: keep
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <memory>

#include <array>
#include <cstdint>

namespace xrpl::test {

class STIssue_test : public beast::unit_test::Suite
{
public:
    void
    testConstructor()
    {
        testcase("Constructor");
        using namespace jtx;
        Account const alice{"alice"};
        auto const usd = alice["USD"];
        Issue issue;

        try
        {
            issue = xrpIssue();
            issue.account = alice;
            STIssue const stissue(sfAsset, Asset{issue});
            fail("Inconsistent XRP Issue doesn't fail");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            issue = usd;
            issue.account = xrpAccount();
            STIssue const stissue(sfAsset, Asset{issue});
            fail("Inconsistent IOU Issue doesn't fail");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            // Currency is USD but account is XRP
            auto const data =
                "00000000000000000000000055534400000000000000000000000000000000"
                "000000000000000000";
            BaseUInt<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            fail("Inconsistent IOU Issue doesn't fail on serializer");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            STIssue const stissue(sfAsset, Asset{xrpIssue()});
        }
        catch (...)
        {
            fail("XRP issue failed");
        }

        try
        {
            STIssue const stissue(sfAsset, Asset{usd});
        }
        catch (...)
        {
            fail("USD issue failed");
        }

        try
        {
            auto const data =
                "0000000000000000000000005553440000000000ae123a8556f3cf91154711"
                "376afb0f894f832b3d";
            BaseUInt<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            BEAST_EXPECT(stissue.value() == usd);
        }
        catch (...)
        {
            fail("USD Issue fails on serializer");
        }

        try
        {
            auto const data = "0000000000000000000000000000000000000000";
            BaseUInt<160> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue const stissue(iter, sfAsset);
            BEAST_EXPECT(stissue.value() == xrpCurrency());
        }
        catch (...)
        {
            fail("XRP Issue fails on serializer");
        }
    }

    void
    testCompare()
    {
        testcase("Compare");
        using namespace jtx;
        Account const alice{"alice"};
        auto const usd = alice["USD"];
        Asset const asset1{xrpIssue()};
        Asset const asset2{usd};
        Asset const asset3{MPTID{2}};

        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset2);
        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset3);
        BEAST_EXPECT(STIssue(sfAsset, asset1) == asset1);
        BEAST_EXPECT(STIssue(sfAsset, asset1).getText() == "XRP");
        BEAST_EXPECT(
            STIssue(sfAsset, asset2).getText() == "USD/rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn");
        BEAST_EXPECT(
            STIssue(sfAsset, asset3).getText() ==
            "000000000000000000000000000000000000000000000002");
    }

    void
    testNoAccountIssuerRpc()
    {
        testcase("noAccount issuer rejected via RPC sign");

        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString("[signing_support]\ntrue");
                    return cfg;
                })};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        json::Value txJson;
        txJson[jss::TransactionType] = "AMMDelete";
        txJson[jss::Account] = alice.human();
        txJson[jss::Asset][jss::currency] = "USD";
        txJson[jss::Asset][jss::issuer] = to_string(noAccount());
        txJson[jss::Asset2][jss::currency] = "XRP";

        json::Value req;
        req[jss::tx_json] = txJson;
        req[jss::secret] = alice.name();

        auto const result = env.rpc("json", "sign", to_string(req))[jss::result];

        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::error_message] == "Field 'tx_json.Asset' has invalid data.");
    }

    void
    testNoAccountIssuer()
    {
        testcase("noAccount issuer rejection");

        {
            json::Value jv;
            jv[jss::currency] = "USD";
            jv[jss::issuer] = to_string(noAccount());

            try
            {
                issueFromJson(sfAsset, jv);
                fail("issueFromJson accepted noAccount() as IOU issuer");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            Serializer s;
            s.addBitString(toCurrency("USD"));
            s.addBitString(noAccount());
            SerialIter iter(s.slice());

            try
            {
                STIssue const stissue(iter, sfAsset);
                fail(
                    "STIssue deserialization of [USD][noAccount()] should "
                    "throw");
            }
            catch (...)
            {
                pass();
            }
        }
    }

    void
    testXrpAccountIssuerRpc()
    {
        testcase("xrpAccount issuer rejected via RPC sign");

        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString("[signing_support]\ntrue");
                    return cfg;
                })};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        json::Value txJson;
        txJson[jss::TransactionType] = "AMMDelete";
        txJson[jss::Account] = alice.human();
        txJson[jss::Asset][jss::currency] = "USD";
        txJson[jss::Asset][jss::issuer] = to_string(xrpAccount());
        txJson[jss::Asset2][jss::currency] = "XRP";

        json::Value req;
        req[jss::tx_json] = txJson;
        req[jss::secret] = alice.name();

        auto const result = env.rpc("json", "sign", to_string(req))[jss::result];

        BEAST_EXPECT(result[jss::error] == "invalidParams");
        BEAST_EXPECT(result[jss::error_message] == "Field 'tx_json.Asset' has invalid data.");
    }

    void
    testXrpAccountIssuer()
    {
        testcase("xrpAccount issuer rejection");

        {
            json::Value jv;
            jv[jss::currency] = "USD";
            jv[jss::issuer] = to_string(xrpAccount());

            try
            {
                issueFromJson(sfAsset, jv);
                fail("issueFromJson accepted xrpAccount() as IOU issuer");
            }
            catch (...)
            {
                pass();
            }
        }
    }

    void
    testMPTSerialization()
    {
        testcase("MPT serialization");
        using namespace jtx;
        Account const alice{"alice"};

        // 0x01020304 pins canonical MPTID bytes 01 02 03 04 and
        // preserved STIssue wire bytes 04 03 02 01 on BE and LE.
        std::array<std::uint32_t, 3> const vectors = {0x00000001, 0x01020304, 0xa1b2c3d4};

        for (auto const vector : vectors)
        {
            MPTID const mptID = makeMptID(vector, alice);
            MPTIssue const issue{mptID};
            STIssue const stIssue(sfAsset, Asset{issue});

            Serializer actual;
            stIssue.add(actual);

            // STIssue preserves the existing little-endian validator ledger bytes.
            Serializer expected;
            expected.addBitString(alice.id());
            expected.addBitString(noAccount());
            {
                std::array<unsigned char, 4> const bytes{
                    static_cast<unsigned char>(vector),
                    static_cast<unsigned char>(vector >> 8),
                    static_cast<unsigned char>(vector >> 16),
                    static_cast<unsigned char>(vector >> 24)};
                expected.addRaw(bytes.data(), bytes.size());
            }

            BEAST_EXPECTS(strHex(actual) == strHex(expected), strHex(actual));

            // Decoding the preserved wire format must recover the canonical MPTID.
            SerialIter iter(expected.slice());
            STIssue const decoded(iter, sfAsset);
            BEAST_EXPECT(decoded.holds<MPTIssue>());
            BEAST_EXPECT(decoded.value().get<MPTIssue>().getMptID() == mptID);

            // A decoded ledger value must serialize back to the same bytes.
            Serializer roundTrip;
            decoded.add(roundTrip);
            BEAST_EXPECTS(strHex(roundTrip) == strHex(expected), strHex(roundTrip));
        }
    }

    void
    run() override
    {
        // compliments other unit tests to ensure complete coverage
        testConstructor();
        testCompare();
        testNoAccountIssuerRpc();
        testNoAccountIssuer();
        testXrpAccountIssuerRpc();
        testXrpAccountIssuer();
        testMPTSerialization();
    }
};

BEAST_DEFINE_TESTSUITE(STIssue, protocol, xrpl);

}  // namespace xrpl::test
