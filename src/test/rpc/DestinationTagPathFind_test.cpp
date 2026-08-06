#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class DestinationTagPathFind_test : public beast::unit_test::Suite
{
    static json::Value
    pathFindParams(jtx::Account const& src, jtx::Account const& dst)
    {
        json::Value params;
        params[jss::source_account] = src.human();
        params[jss::destination_account] = dst.human();
        params[jss::destination_amount] = "1000000";
        return params;
    }

    void
    testDestinationTagIsBoolean()
    {
        // Regression test for issue #6790 / PR #6837:
        // destination_tag in path_find / ripple_path_find responses must be a
        // JSON boolean, not the raw lsfRequireDestTag bitmask (131072).
        testcase("ripple_path_find destination_tag is boolean");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        auto checkTag = [this, &env](Account const& src, Account const& dst, bool expect) {
            auto const resp =
                env.rpc("json", "ripple_path_find", to_string(pathFindParams(src, dst)));
            auto const& result = resp[jss::result];
            BEAST_EXPECT(!result.isMember(jss::error));
            BEAST_EXPECT(result.isMember(jss::destination_tag));
            auto const& tag = result[jss::destination_tag];
            BEAST_EXPECT(tag.isBool());
            BEAST_EXPECT(tag.asBool() == expect);
            // Guard against the old bug: integer 131072 / 0
            BEAST_EXPECT(!tag.isUInt() && !tag.isInt());
        };

        // Default: no RequireDestTag
        checkTag(alice, bob, false);

        env(fset(bob, asfRequireDest));
        env.close();
        checkTag(alice, bob, true);

        env(fclear(bob, asfRequireDest));
        env.close();
        checkTag(alice, bob, false);
    }

    void
    run() override
    {
        testDestinationTagIsBoolean();
    }
};

BEAST_DEFINE_TESTSUITE(DestinationTagPathFind, rpc, xrpl);

}  // namespace xrpl::test
