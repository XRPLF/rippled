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
            auto const brokerKeylet =
                keylet::loanbroker(alice.id(), env.seq(alice));
            // Other LoanBroker transactions are disabled, too.
            // 1. LoanBrokerCoverDeposit
            env(coverDeposit(alice, brokerKeylet.key, asset(1000)),
                ter(temDISABLED));
            // 2. LoanBrokerCoverWithdraw
            env(coverWithdraw(alice, brokerKeylet.key, asset(1000)),
                ter(temDISABLED));
            // 3. LoanBrokerDelete
            env(del(alice, brokerKeylet.key), ter(temDISABLED));
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol, true);
    }

    struct VaultInfo
    {
        jtx::PrettyAsset asset;
        uint256 vaultID;
        VaultInfo(jtx::PrettyAsset const& asset_, uint256 const& vaultID_)
            : asset(asset_), vaultID(vaultID_)
        {
        }
    };

    void
    lifecycle(
        const char* label,
        jtx::Env& env,
        jtx::Account const& alice,
        jtx::Account const& evan,
        VaultInfo const& vault,
        std::function<jtx::JTx(jtx::JTx const&)> modifyJTx,
        std::function<void(SLE::const_ref)> checkBroker,
        std::function<void(SLE::const_ref)> changeBroker,
        std::function<void(SLE::const_ref)> checkChangedBroker)
    {
        auto const keylet = keylet::loanbroker(alice.id(), env.seq(alice));
        {
            auto const& asset = vault.asset.raw();
            testcase << "Lifecycle: "
                     << (asset.native()                ? "XRP "
                             : asset.holds<Issue>()    ? "IOU "
                             : asset.holds<MPTIssue>() ? "MPT "
                                                       : "Unknown ")
                     << label;
        }

        using namespace jtx;
        using namespace loanBroker;

        {
            // Start with default values
            auto jtx = env.jt(set(alice, vault.vaultID), fee(increment));
            // Modify as desired
            if (modifyJTx)
                jtx = modifyJTx(jtx);
            // Successfully create a Loan Broker
            env(jtx);
        }

        env.close();
        if (auto broker = env.le(keylet); BEAST_EXPECT(broker))
        {
            // log << "Broker after create: " << to_string(broker->getJson())
            //     << std::endl;
            BEAST_EXPECT(broker->at(sfVaultID) == vault.vaultID);
            BEAST_EXPECT(broker->at(sfAccount) != alice.id());
            BEAST_EXPECT(broker->at(sfOwner) == alice.id());
            BEAST_EXPECT(broker->at(sfFlags) == 0);
            BEAST_EXPECT(broker->at(sfSequence) == env.seq(alice) - 1);
            BEAST_EXPECT(broker->at(sfOwnerCount) == 0);
            BEAST_EXPECT(broker->at(sfDebtTotal) == 0);
            BEAST_EXPECT(broker->at(sfCoverAvailable) == 0);
            if (checkBroker)
                checkBroker(broker);

            // if (auto const vaultSLE = env.le(keylet::vault(vault.vaultID)))
            //{
            //     log << "Vault: " << to_string(vaultSLE->getJson()) <<
            //     std::endl;
            // }
            //  Load the pseudo-account
            Account const pseudoAccount{
                "Broker pseudo-account", broker->at(sfAccount)};
            auto const pseudoKeylet = keylet::account(pseudoAccount);
            if (auto const pseudo = env.le(pseudoKeylet); BEAST_EXPECT(pseudo))
            {
                // log << "Pseudo-account after create: "
                //     << to_string(pseudo->getJson()) << std::endl
                //     << std::endl;
                BEAST_EXPECT(
                    pseudo->at(sfFlags) ==
                    (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth));
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
                BEAST_EXPECT(!pseudo->isFieldPresent(sfFirstNFTokenSequence));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfAMMID));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfVaultID));
                BEAST_EXPECT(pseudo->at(sfLoanBrokerID) == keylet.key);
            }

            auto verifyCoverAmount =
                [&env, &vault, &broker, &pseudoAccount, this](auto n) {
                    auto const amount = vault.asset(n);
                    BEAST_EXPECT(
                        broker->at(sfCoverAvailable) == amount.number());
                    env.require(balance(pseudoAccount, amount));
                };

            // Test Cover funding before allowing alterations
            env(coverDeposit(alice, uint256(0), vault.asset(10)),
                ter(temINVALID));
            env(coverDeposit(evan, keylet.key, vault.asset(10)),
                ter(tecNO_PERMISSION));
            env(coverDeposit(evan, keylet.key, vault.asset(0)),
                ter(temBAD_AMOUNT));
            env(coverDeposit(evan, keylet.key, vault.asset(-10)),
                ter(temBAD_AMOUNT));
            env(coverDeposit(alice, vault.vaultID, vault.asset(10)),
                ter(tecNO_ENTRY));

            verifyCoverAmount(0);

            // Fund the cover deposit
            env(coverDeposit(alice, keylet.key, vault.asset(10)));
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                verifyCoverAmount(10);
            }

            // Test withdrawal failure cases
            env(coverWithdraw(alice, uint256(0), vault.asset(10)),
                ter(temINVALID));
            env(coverWithdraw(evan, keylet.key, vault.asset(10)),
                ter(tecNO_PERMISSION));
            env(coverWithdraw(evan, keylet.key, vault.asset(0)),
                ter(temBAD_AMOUNT));
            env(coverWithdraw(evan, keylet.key, vault.asset(-10)),
                ter(temBAD_AMOUNT));
            env(coverWithdraw(alice, vault.vaultID, vault.asset(10)),
                ter(tecNO_ENTRY));
            env(coverWithdraw(alice, keylet.key, vault.asset(900)),
                ter(tecINSUFFICIENT_FUNDS));

            // Withdraw some of the cover amount
            env(coverWithdraw(alice, keylet.key, vault.asset(7)));
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                verifyCoverAmount(3);
            }

            // Add some more cover
            env(coverDeposit(alice, keylet.key, vault.asset(5)));
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                verifyCoverAmount(8);
            }

            // Withdraw some more
            env(coverWithdraw(alice, keylet.key, vault.asset(2)));
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                verifyCoverAmount(6);
            }

            env.close();

            // no-op
            env(set(alice, vault.vaultID), loanBrokerID(keylet.key));

            // Make modifications to the broker
            if (changeBroker)
                changeBroker(broker);

            env.close();

            // Check the results of modifications
            if (BEAST_EXPECT(broker = env.le(keylet)) && checkChangedBroker)
                checkChangedBroker(broker);

            // Verify that fields get removed when set to default values
            // Debt maximum: explicit 0
            // Data: explicit empty
            env(set(alice, vault.vaultID),
                loanBrokerID(broker->key()),
                debtMaximum(Number(0)),
                data(""));

            // Check the updated fields
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                BEAST_EXPECT(!broker->isFieldPresent(sfData));
            }

            /////////////////////////////////////
            // try to delete the wrong broker object
            env(del(alice, vault.vaultID), ter(tecNO_ENTRY));
            // evan tries to delete the broker
            env(del(evan, keylet.key), ter(tecNO_PERMISSION));

            // TODO: test deletion with an active loan

            // Note alice's balance of the asset and the broker account's cover
            // funds
            auto const aliceBalance = env.balance(alice, vault.asset);
            auto const coverFunds = env.balance(pseudoAccount, vault.asset);
            BEAST_EXPECT(coverFunds.number() == broker->at(sfCoverAvailable));
            BEAST_EXPECT(coverFunds != beast::zero);
            verifyCoverAmount(6);

            // delete the broker
            // log << "Broker before delete: " << to_string(broker->getJson())
            //    << std::endl;
            // if (auto const pseudo = env.le(pseudoKeylet);
            // BEAST_EXPECT(pseudo))
            //{
            //    log << "Pseudo-account before delete: "
            //        << to_string(pseudo->getJson()) << std::endl
            //        << std::endl;
            //}

            env(del(alice, keylet.key));
            env.close();
            {
                broker = env.le(keylet);
                BEAST_EXPECT(!broker);
                auto pseudo = env.le(pseudoKeylet);
                BEAST_EXPECT(!pseudo);
            }
            auto const expectedBalance = aliceBalance + coverFunds -
                (aliceBalance.value().native()
                     ? STAmount(env.current()->fees().base.value())
                     : vault.asset(0));
            env.require(balance(alice, expectedBalance));
            env.require(balance(pseudoAccount, None(vault.asset.raw())));
        }
    }

    void
    testLifecycle()
    {
        testcase("Lifecycle");
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

        // Fund the accounts and trust lines with the same amount so that tests
        // can use the same values regardless of the asset.
        env.fund(XRP(100'000), issuer, noripple(alice, evan));
        env.close();

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        PrettyAsset const iouAsset = issuer["IOU"];
        env(trust(alice, iouAsset(1'000'000)));
        env(trust(evan, iouAsset(1'000'000)));
        env(pay(issuer, evan, iouAsset(100'000)));
        env(pay(issuer, alice, iouAsset(100'000)));
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = alice});
        mptt.authorize({.account = evan});
        env(pay(issuer, alice, mptAsset(100'000)));
        env(pay(issuer, evan, mptAsset(100'000)));
        env.close();

        std::array const assets{xrpAsset, iouAsset, mptAsset};

        // Create vaults
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

            auto badKeylet = keylet::vault(alice.id(), env.seq(alice));
            // Try some failure cases
            // insufficient fee
            env(set(evan, vault.vaultID), ter(telINSUF_FEE_P));
            // not the vault owner
            env(set(evan, vault.vaultID),
                fee(increment),
                ter(tecNO_PERMISSION));
            // not a vault
            env(set(alice, badKeylet.key), fee(increment), ter(tecNO_ENTRY));
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
                managementFeeRate(maxManagementFeeRate),
                fee(increment),
                ter(tecNO_PERMISSION));
            // sfManagementFeeRate: too big
            env(set(evan, vault.vaultID),
                managementFeeRate(maxManagementFeeRate + TenthBips16(10)),
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

            std::string testData;
            lifecycle(
                "default fields",
                env,
                alice,
                evan,
                vault,
                // No modifications
                {},
                [&](SLE::const_ref broker) {
                    // Extra checks
                    BEAST_EXPECT(!broker->isFieldPresent(sfManagementFeeRate));
                    BEAST_EXPECT(!broker->isFieldPresent(sfCoverRateMinimum));
                    BEAST_EXPECT(
                        !broker->isFieldPresent(sfCoverRateLiquidation));
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 0);
                },
                [&](SLE::const_ref broker) {
                    // Modifications

                    // Update the fields
                    auto const nextKeylet =
                        keylet::loanbroker(alice.id(), env.seq(alice));

                    // fields that can't be changed
                    // LoanBrokerID
                    env(set(alice, vault.vaultID),
                        loanBrokerID(nextKeylet.key),
                        ter(tecNO_ENTRY));
                    // VaultID
                    env(set(alice, nextKeylet.key),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // Owner
                    env(set(evan, vault.vaultID),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // ManagementFeeRate
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        managementFeeRate(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateMinimum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        coverRateMinimum(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateLiquidation
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        coverRateLiquidation(maxManagementFeeRate),
                        ter(temINVALID));

                    // fields that can be changed
                    testData = "Test Data 1234";
                    // Bad data: too long
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        data(std::string(maxDataPayloadLength + 1, 'W')),
                        ter(temINVALID));

                    // Bad debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        debtMaximum(Number(-175, -1)),
                        ter(temINVALID));
                    // Data & Debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        data(testData),
                        debtMaximum(Number(175, -1)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(175, -1));
                });

            lifecycle(
                "non-default fields",
                env,
                alice,
                evan,
                vault,
                [&](jtx::JTx const& jv) {
                    testData = "spam spam spam spam";
                    // Finally, create another Loan Broker with none of the
                    // values at default
                    return env.jt(
                        jv,
                        data(testData),
                        managementFeeRate(TenthBips16(123)),
                        debtMaximum(Number(9)),
                        coverRateMinimum(TenthBips32(100)),
                        coverRateLiquidation(TenthBips32(200)));
                },
                [&](SLE::const_ref broker) {
                    // Extra checks
                    BEAST_EXPECT(broker->at(sfManagementFeeRate) == 123);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 100);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 200);
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(9));
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                },
                [&](SLE::const_ref broker) {
                    // Reset Data & Debt maximum to default values
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        data(""),
                        debtMaximum(Number(0)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                });
        }
    }

public:
    void
    run() override
    {
        testDisabled();
        testLifecycle();
    }
};

BEAST_DEFINE_TESTSUITE(LoanBroker, tx, ripple);

}  // namespace test
}  // namespace ripple
