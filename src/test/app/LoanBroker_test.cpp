//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/vault.h>
#include <test/jtx/TestHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {


class LoanBroker_test : public beast::unit_test::suite
{
    void
    testDisabled()
    {
        // Lending Protocol depends on Single Asset Vault. Test combinations of the two amendments.
        // Single Asset Vault depends on MPTokensV1, but don't test every combo of that.
        using namespace jtx;
        FeatureBitset const all{jtx::supported_amendments()};
        {
            auto const noMPTs =
                all - featureMPTokensV1;
            Env env(*this, noMPTs);

            Account const alice{"alice"};
            env.fund(XRP(10000), alice);

            // Can't create a vault
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            Vault vault{env};
            auto const [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx, ter(temDISABLED));
            env.close();
            BEAST_EXPECT(!env.le(keylet));

            using namespace loanBroker;
            // Can't create a loan broker with the non-existent vault
            env(set(alice, keylet.key), ter(temDISABLED));
        }
        {
            auto const neither = all - featureMPTokensV1 -
                featureSingleAssetVault - featureLendingProtocol;

        }
    }

public:
    void
    run() override
    {
        testDisabled();
    }
};

BEAST_DEFINE_TESTSUITE(LoanBroker, tx, ripple);

}  // namespace test
}  // namespace ripple
