//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/STIssue.h>

namespace ripple {
namespace test {

class STIssue_test : public beast::unit_test::suite
{
public:
    void
    testConstructor()
    {
        testcase("Constructor");
        using namespace jtx;
        Account const alice{"alice"};
        auto const USD = alice["USD"];
        Issue issue;

        try
        {
            issue = xrpIssue();
            issue.account = alice;
            STIssue stissue(sfAsset, Asset{issue});
            fail("Inconsistent XRP Issue doesn't fail");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            issue = USD;
            issue.account = xrpAccount();
            STIssue stissue(sfAsset, Asset{issue});
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
            base_uint<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue stissue(iter, sfAsset);
            fail("Inconsistent IOU Issue doesn't fail on serializer");
        }
        catch (...)
        {
            pass();
        }

        try
        {
            STIssue stissue(sfAsset, Asset{xrpIssue()});
        }
        catch (...)
        {
            fail("XRP issue failed");
        }

        try
        {
            STIssue stissue(sfAsset, Asset{USD});
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
            base_uint<320> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue stissue(iter, sfAsset);
            BEAST_EXPECT(stissue.value() == USD);
        }
        catch (...)
        {
            fail("USD Issue fails on serializer");
        }

        try
        {
            auto const data = "0000000000000000000000000000000000000000";
            base_uint<160> uint;
            (void)uint.parseHex(data);
            SerialIter iter(Slice(uint.data(), uint.size()));
            STIssue stissue(iter, sfAsset);
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
        auto const USD = alice["USD"];
        Asset const asset1{xrpIssue()};
        Asset const asset2{USD};
        Asset const asset3{MPTID{2}};

        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset2);
        BEAST_EXPECT(STIssue(sfAsset, asset1) != asset3);
        BEAST_EXPECT(STIssue(sfAsset, asset1) == asset1);
        BEAST_EXPECT(STIssue(sfAsset, asset1).getText() == "XRP");
        BEAST_EXPECT(
            STIssue(sfAsset, asset2).getText() ==
            "USD/rG1QQv2nh2gr7RCZ1P8YYcBUKCCN633jCn");
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

BEAST_DEFINE_TESTSUITE(STIssue, protocol, ripple);

}  // namespace test
}  // namespace ripple
