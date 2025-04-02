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
#include <test/jtx/TestHelpers.h>
#include <test/jtx/mpt.h>
#include <test/jtx/vault.h>

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
        // Lending Protocol depends on Single Asset Vault (SAV). Test
        // combinations of the two amendments.
        // Single Asset Vault depends on MPTokensV1, but don't test every combo
        // of that.
        using namespace jtx;
        FeatureBitset const all{jtx::supported_amendments()};
        auto failAll = [this](FeatureBitset features, bool goodVault = false) {
            Env env(*this, features);

            Account const alice{"alice"};
            env.fund(XRP(10000), alice);

            // Try to create a vault
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            Vault vault{env};
            auto const [tx, keylet] =
                vault.create({.owner = alice, .asset = asset});
            env(tx, ter(goodVault ? ter(tesSUCCESS) : ter(temDISABLED)));
            env.close();
            BEAST_EXPECT(static_cast<bool>(env.le(keylet)) == goodVault);

            using namespace loanBroker;
            // Can't create a loan broker regardless of whether the vault exists
            env(set(alice, keylet.key), ter(temDISABLED));
        };
        // failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol, true);
    }

    void
    testCreateAndUpdate()
    {
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for an
        // MPT. That'll require three corresponding SAVs.
        Env env(*this);

        Account issuer{"issuer"};
        // For simplicity, alice will be the sole actor for the vault & brokers.
        Account alice{"alice"};
        // Evan will attempt to be naughty
        Account evan{"evan"};
        Vault vault{env};
        env.fund(XRP(1000), issuer, alice, evan);

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        PrettyAsset const iouAsset = issuer["IOU"];
        env(trust(alice, iouAsset(1000)));
        env(trust(evan, iouAsset(1000)));
        env(pay(issuer, evan, iouAsset(1000)));
        env(pay(issuer, alice, iouAsset(1000)));
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = alice});
        mptt.authorize({.account = evan});
        env(pay(issuer, alice, mptAsset(1000)));
        env(pay(issuer, evan, mptAsset(1000)));
        env.close();

        std::array const assets{xrpAsset, iouAsset, mptAsset};

        // Create vaults
        struct Vault
        {
            PrettyAsset const& asset;
            uint256 vaultID;
        };
        std::vector<Vault> vaults;
        for (auto const& asset : assets)
        {
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(keylet));

            vaults.emplace_back(asset, keylet.key);

            env(vault.deposit(
                {.depositor = alice, .id = keylet.key, .amount = asset(50)}));
        }

        // Create and update Loan Brokers
        for (auto const& vault : vaults)
        {
            using namespace loanBroker;

            // Try some failure cases
            env(set(evan, vault.vaultID), ter(tecNO_PERMISSION));
            // flags are checked first
            env(set(evan, vault.vaultID, ~tfUniversal), ter(temINVALID_FLAG));
            // field length validation
            // sfData: good length, bad account
            env(set(evan, vault.vaultID),
                data(strHex(std::string(maxDataPayloadLength, '0'))),
                ter(tecNO_PERMISSION));
            // sfData: too long
            env(set(evan, vault.vaultID),
                data(strHex(std::string(maxDataPayloadLength + 1, '0'))),
                ter(temINVALID));
            // sfManagementFeeRate: good value, bad account
            env(set(evan, vault.vaultID),
                managementFeeRate(maxFeeRate),
                ter(tecNO_PERMISSION));
            // sfManagementFeeRate: too big
            env(set(evan, vault.vaultID),
                managementFeeRate(maxFeeRate + 1),
                ter(temINVALID));
            // sfCoverRateMinimum: good value, bad account
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate),
                ter(tecNO_PERMISSION));
            // sfCoverRateMinimum: too big
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate + 1),
                ter(temINVALID));
            // sfCoverRateLiquidation: good value, bad account
            env(set(evan, vault.vaultID),
                coverRateLiquidation(maxCoverRate),
                ter(tecNO_PERMISSION));
            // sfCoverRateLiquidation: too big
            env(set(evan, vault.vaultID),
                coverRateLiquidation(maxCoverRate + 1),
                ter(temINVALID));

            auto keylet = keylet::loanbroker(alice.id(), env.seq(alice));
            env(set(alice, vault.vaultID));
            BEAST_EXPECT(env.le(keylet));
        }
    }

public:
    void
    run() override
    {
        testDisabled();
        testCreateAndUpdate();
    }
};

BEAST_DEFINE_TESTSUITE(LoanBroker, tx, ripple);

}  // namespace test
}  // namespace ripple
