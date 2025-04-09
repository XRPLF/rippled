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
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    FeatureBitset const all{
        jtx::supported_amendments() | featureMPTokensV1 |
        featureSingleAssetVault | featureLendingProtocol};

    void
    testDisabled()
    {
        testcase("Disabled");
        // Lending Protocol depends on Single Asset Vault (SAV). Test
        // combinations of the two amendments.
        // Single Asset Vault depends on MPTokensV1, but don't test every combo
        // of that.
        using namespace jtx;
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
            env(set(alice, keylet.key), fee(increment), ter(temDISABLED));
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol, true);
    }

    void
    testCreateAndUpdate()
    {
        testcase("Create and update");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for an
        // MPT. That'll require three corresponding SAVs.
        Env env(*this, all);

        Account issuer{"issuer"};
        // For simplicity, alice will be the sole actor for the vault & brokers.
        Account alice{"alice"};
        // Evan will attempt to be naughty
        Account evan{"evan"};
        Vault vault{env};
        env.fund(XRP(100000), issuer, noripple(alice, evan));
        env.close();

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
        struct VaultInfo
        {
            PrettyAsset asset;
            uint256 vaultID;
            VaultInfo(PrettyAsset const& asset_, uint256 const& vaultID_)
                : asset(asset_), vaultID(vaultID_)
            {
            }
        };
        std::vector<VaultInfo> vaults;
        for (auto const& asset : assets)
        {
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(keylet));

            vaults.emplace_back(asset, keylet.key);

            env(vault.deposit(
                {.depositor = alice, .id = keylet.key, .amount = asset(50)}));
            env.close();
        }

        // Create and update Loan Brokers
        for (auto const& vault : vaults)
        {
            using namespace loanBroker;

            {
                auto badKeylet = keylet::vault(alice.id(), env.seq(alice));
                // Try some failure cases
                // insufficient fee
                env(set(evan, vault.vaultID), ter(telINSUF_FEE_P));
                // not the vault owner
                env(set(evan, vault.vaultID),
                    fee(increment),
                    ter(tecNO_PERMISSION));
                // not a vault
                env(set(alice, badKeylet.key),
                    fee(increment),
                    ter(tecNO_ENTRY));
                // flags are checked first
                env(set(evan, vault.vaultID, ~tfUniversal),
                    fee(increment),
                    ter(temINVALID_FLAG));
                // field length validation
                // sfData: good length, bad account
                env(set(evan, vault.vaultID),
                    fee(increment),
                    data(std::string(maxDataPayloadLength, 'X')),
                    ter(tecNO_PERMISSION));
                // sfData: too long
                env(set(evan, vault.vaultID),
                    fee(increment),
                    data(std::string(maxDataPayloadLength + 1, 'Y')),
                    ter(temINVALID));
                // sfManagementFeeRate: good value, bad account
                env(set(evan, vault.vaultID),
                    managementFeeRate(maxFeeRate),
                    fee(increment),
                    ter(tecNO_PERMISSION));
                // sfManagementFeeRate: too big
                env(set(evan, vault.vaultID),
                    managementFeeRate(maxFeeRate + 1),
                    fee(increment),
                    ter(temINVALID));
                // sfCoverRateMinimum: good value, bad account
                env(set(evan, vault.vaultID),
                    coverRateMinimum(maxCoverRate),
                    fee(increment),
                    ter(tecNO_PERMISSION));
                // sfCoverRateMinimum: too big
                env(set(evan, vault.vaultID),
                    coverRateMinimum(maxCoverRate + 1),
                    fee(increment),
                    ter(temINVALID));
                // sfCoverRateLiquidation: good value, bad account
                env(set(evan, vault.vaultID),
                    coverRateLiquidation(maxCoverRate),
                    fee(increment),
                    ter(tecNO_PERMISSION));
                // sfCoverRateLiquidation: too big
                env(set(evan, vault.vaultID),
                    coverRateLiquidation(maxCoverRate + 1),
                    fee(increment),
                    ter(temINVALID));
                // sfDebtMaximum: good value, bad account
                env(set(evan, vault.vaultID),
                    debtMaximum(Number(0)),
                    fee(increment),
                    ter(tecNO_PERMISSION));
                // sfDebtMaximum: overflow
                env(set(evan, vault.vaultID),
                    debtMaximum(Number(1, 100)),
                    fee(increment),
                    ter(temINVALID));
                // sfDebtMaximum: negative
                env(set(evan, vault.vaultID),
                    debtMaximum(Number(-1)),
                    fee(increment),
                    ter(temINVALID));

                auto keylet = keylet::loanbroker(alice.id(), env.seq(alice));
                // Successfully create a Loan Broker with all default values.
                env(set(alice, vault.vaultID), fee(increment));
                env.close();
                if (auto broker = env.le(keylet); BEAST_EXPECT(broker))
                {
                    // Check the fields
                    BEAST_EXPECT(broker->at(sfVaultID) == vault.vaultID);
                    BEAST_EXPECT(broker->at(sfAccount) != alice.id());
                    BEAST_EXPECT(broker->at(sfOwner) == alice.id());
                    BEAST_EXPECT(!broker->isFieldPresent(sfManagementFeeRate));
                    BEAST_EXPECT(!broker->isFieldPresent(sfCoverRateMinimum));
                    BEAST_EXPECT(
                        !broker->isFieldPresent(sfCoverRateLiquidation));
                    BEAST_EXPECT(broker->at(sfFlags) == 0);
                    BEAST_EXPECT(broker->at(sfSequence) == env.seq(alice) - 1);
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(broker->at(sfOwnerCount) == 0);
                    BEAST_EXPECT(broker->at(sfDebtTotal) == 0);
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverAvailable) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 0);

                    // Load the pseudo-account
                    auto const pseudoKeylet =
                        keylet::account(broker->at(sfAccount));
                    if (auto pseudo = env.le(pseudoKeylet);
                        BEAST_EXPECT(pseudo))
                    {
                        BEAST_EXPECT(
                            pseudo->at(sfFlags) ==
                            (lsfDisableMaster | lsfDefaultRipple |
                             lsfDepositAuth));
                        BEAST_EXPECT(pseudo->at(sfSequence) == 0);
                        BEAST_EXPECT(pseudo->at(sfBalance) == beast::zero);
                        BEAST_EXPECT(
                            pseudo->at(sfOwnerCount) ==
                            (vault.asset.raw().native() ? 0 : 1));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfAccountTxnID));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfRegularKey));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfEmailHash));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletLocator));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletSize));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfMessageKey));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTransferRate));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfDomain));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTickSize));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTicketCount));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfNFTokenMinter));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfMintedNFTokens));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfBurnedNFTokens));
                        BEAST_EXPECT(
                            !pseudo->isFieldPresent(sfFirstNFTokenSequence));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfAMMID));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfVaultID));
                        BEAST_EXPECT(pseudo->at(sfLoanBrokerID) == keylet.key);
                    }

                    // Update the fields
                    auto nextKeylet =
                        keylet::loanbroker(alice.id(), env.seq(alice));

                    // no-op
                    env(set(alice, vault.vaultID), loanBrokerID(keylet.key));

                    // fields that can't be changed
                    // LoanBrokerID
                    env(set(alice, vault.vaultID),
                        loanBrokerID(nextKeylet.key),
                        ter(tecNO_ENTRY));
                    // VaultID
                    env(set(alice, nextKeylet.key),
                        loanBrokerID(keylet.key),
                        ter(tecNO_PERMISSION));
                    // Owner
                    env(set(evan, vault.vaultID),
                        loanBrokerID(keylet.key),
                        ter(tecNO_PERMISSION));
                    // ManagementFeeRate
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        managementFeeRate(maxFeeRate),
                        ter(temINVALID));
                    // CoverRateMinimum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        coverRateMinimum(maxFeeRate),
                        ter(temINVALID));
                    // CoverRateLiquidation
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        coverRateLiquidation(maxFeeRate),
                        ter(temINVALID));

                    // fields that can be changed
                    std::string const testData("Test Data 1234");
                    // Bad data: too long
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        data(std::string(maxDataPayloadLength + 1, 'W')),
                        ter(temINVALID));
                    // Debt maximum: explicit 0
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        debtMaximum(Number(0)));
                    // Bad debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        debtMaximum(Number(-175, -1)),
                        ter(temINVALID));
                    // Data & Debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(keylet.key),
                        data(testData),
                        debtMaximum(Number(175, -1)));
                    env.close();
                    // Check the updated fields
                    broker = env.le(keylet);
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(175, -1));
                }

                auto keylet2 = keylet::loanbroker(alice.id(), env.seq(alice));
                std::string const testData2("spam spam spam spam");
                // Finally, create another Loan Broker with none of the values
                // at default
                // Successfully create a Loan Broker with no default values.
                env(set(alice, vault.vaultID),
                    data(testData2),
                    managementFeeRate(123),
                    debtMaximum(Number(9)),
                    coverRateMinimum(100),
                    coverRateLiquidation(200),
                    fee(increment));
                env.close();
                if (auto broker = env.le(keylet2); BEAST_EXPECT(broker))
                {
                    // Check the fields
                    BEAST_EXPECT(broker->at(sfVaultID) == vault.vaultID);
                    BEAST_EXPECT(broker->at(sfAccount) != alice.id());
                    BEAST_EXPECT(broker->at(sfOwner) == alice.id());
                    BEAST_EXPECT(broker->at(sfManagementFeeRate) == 123);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 100);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 200);
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(9));
                    BEAST_EXPECT(broker->at(sfFlags) == 0);
                    BEAST_EXPECT(broker->at(sfSequence) == env.seq(alice) - 1);
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData2));

                    BEAST_EXPECT(broker->at(sfOwnerCount) == 0);
                    BEAST_EXPECT(broker->at(sfDebtTotal) == 0);
                    BEAST_EXPECT(broker->at(sfCoverAvailable) == 0);
                    // Load the pseudo-account
                    auto const pseudoKeylet =
                        keylet::account(broker->at(sfAccount));
                    if (auto pseudo = env.le(pseudoKeylet);
                        BEAST_EXPECT(pseudo))
                    {
                        BEAST_EXPECT(
                            pseudo->at(sfFlags) ==
                            (lsfDisableMaster | lsfDefaultRipple |
                             lsfDepositAuth));
                        BEAST_EXPECT(pseudo->at(sfSequence) == 0);
                        BEAST_EXPECT(pseudo->at(sfBalance) == beast::zero);
                        BEAST_EXPECT(
                            pseudo->at(sfOwnerCount) ==
                            (vault.asset.raw().native() ? 0 : 1));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfAccountTxnID));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfRegularKey));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfEmailHash));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletLocator));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletSize));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfMessageKey));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTransferRate));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfDomain));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTickSize));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfTicketCount));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfNFTokenMinter));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfMintedNFTokens));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfBurnedNFTokens));
                        BEAST_EXPECT(
                            !pseudo->isFieldPresent(sfFirstNFTokenSequence));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfAMMID));
                        BEAST_EXPECT(!pseudo->isFieldPresent(sfVaultID));
                        BEAST_EXPECT(pseudo->at(sfLoanBrokerID) == keylet2.key);
                    }
                }
            }
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
