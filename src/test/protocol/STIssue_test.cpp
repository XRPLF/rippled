
#include <test/jtx/Account.h>
#include <test/jtx/amount.h>  // IWYU pragma: keep

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

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
    run() override
    {
        // compliments other unit tests to ensure complete coverage
        testConstructor();
        testCompare();
    }
};

BEAST_DEFINE_TESTSUITE(STIssue, protocol, xrpl);

}  // namespace xrpl::test
