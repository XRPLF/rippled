//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class CFToken_test : public beast::unit_test::suite
{
    void
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");

        using namespace test::jtx;
        {
            // If the CFT amendment is not enabled, you should not be able to
            // create CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(master, "0158415500000000C1F76FF6ECB0BAC600000000"),
                ter(temDISABLED));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment IS enabled, you should be able to create
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            env(cft::create(
                master, "0158415500000000C1F76FF6ECB0BAC600000000"));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 1);
        }
        {
            // If the CFT amendment is not enabled, you should not be able to
            // destroy CFTokenIssuances
            Env env{*this, features - featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            std::string const assetStr =
                "0158415500000000C1F76FF6ECB0BAC600000000";
            ripple::uint160 asset;
            BEAST_EXPECT(asset.parseHex(assetStr));
            auto const id = keylet::cftIssuance(master.id(), asset);
            env(cft::destroy(master, ripple::to_string(id.key)),
                ter(temDISABLED));
            env.close();

            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
        {
            // If the CFT amendment IS enabled, you should be able to destroy
            // CFTokenIssuances
            Env env{*this, features | featureCFTokensV1};
            Account const& master = env.master;

            BEAST_EXPECT(env.ownerCount(master) == 0);

            std::string const assetStr =
                "0158415500000000C1F76FF6ECB0BAC600000000";
            ripple::uint160 asset;
            BEAST_EXPECT(asset.parseHex(assetStr));

            env(cft::create(master, assetStr));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 1);

            auto const id = keylet::cftIssuance(master.id(), asset);

            env(cft::destroy(master, ripple::to_string(id.key)));
            env.close();
            BEAST_EXPECT(env.ownerCount(master) == 0);
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};

        testEnabled(all);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CFToken, tx, ripple, 2);

}  // namespace ripple
