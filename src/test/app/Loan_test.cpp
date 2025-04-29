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

#include <xrpld/app/tx/detail/LoanSet.h>

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

class Loan_test : public beast::unit_test::suite
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
            Account const bob{"bob"};
            env.fund(XRP(10000), alice, bob);

            auto const keylet = keylet::loanbroker(alice, env.seq(alice));

            using namespace std::chrono_literals;
            using namespace loan;

            // counter party signature is required on LoanSet
            auto setTx = env.jt(
                set(alice, keylet.key, Number(10000), env.now() + 720h),
                ter(temMALFORMED));
            env(setTx);

            // All loan transactions are disabled.
            // 1. LoanSet
            setTx = env.jt(
                setTx, sig(sfCounterpartySignature, bob), ter(temDISABLED));
            env(setTx);
#if 0
            auto const loanKeylet =
                keylet::loan(alice.id(), keylet.key, env.seq(alice));
            // Other Loan transactions are disabled, too.
            // 2. LoanDelete
            env(delete(alice, loanKeylet.key),
                ter(temDISABLED));
            // 3. LoanManage
            env(manage(alice, loanKeylet.key, tfLoanImpair),
                ter(temDISABLED));
            // 4. LoanDraw
            env(draw(alice, loanKeylet.key, Number(500)), ter(temDISABLED));
            // 5. LoanPay
            env(pay(alice, loanKeylet.key, Number(500)), ter(temDISABLED));
#endif
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol, true);
    }

    struct BrokerInfo
    {
        jtx::PrettyAsset asset;
        uint256 brokerID;
        BrokerInfo(jtx::PrettyAsset const& asset_, uint256 const& brokerID_)
            : asset(asset_), brokerID(brokerID_)
        {
        }
    };

    void
    lifecycle(
        const char* label,
        jtx::Env& env,
        jtx::Account const& alice,
        jtx::Account const& evan,
        BrokerInfo const& vault,
        std::function<jtx::JTx(jtx::JTx const&)> modifyJTx,
        std::function<void(SLE::const_ref)> checkBroker,
        std::function<void(SLE::const_ref)> changeBroker,
        std::function<void(SLE::const_ref)> checkChangedBroker)
    {
#if 0
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
        auto const keylet = keylet::loanbroker(alice.id(), env.seq(alice));
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
#endif
    }

    void
    testLifecycle()
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for an
        // MPT. That'll require three corresponding SAVs.
        Env env(*this, all);

        Account const issuer{"issuer"};
        // For simplicity, lender will be the sole actor for the vault &
        // brokers.
        Account const lender{"lender"};
        // Borrower only wants to borrow
        Account const borrower{"borrower"};
        // Evan will attempt to be naughty
        Account const evan{"evan"};
        // Do not fund alice
        Account const alice{"alice"};
        Vault vault{env};

        // Fund the accounts and trust lines with the same amount so that tests
        // can use the same values regardless of the asset.
        env.fund(XRP(100'000), issuer, noripple(lender, borrower, evan));
        env.close();

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        std::string const iouCurrency{"IOU"};
        PrettyAsset const iouAsset = issuer[iouCurrency];
        env(trust(lender, iouAsset(1'000'000)));
        env(trust(evan, iouAsset(1'000'000)));
        env(pay(issuer, evan, iouAsset(100'000)));
        env(pay(issuer, lender, iouAsset(100'000)));
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = lender});
        mptt.authorize({.account = evan});
        env(pay(issuer, lender, mptAsset(100'000)));
        env(pay(issuer, evan, mptAsset(100'000)));
        env.close();

        std::array const assets{xrpAsset, iouAsset, mptAsset};

        // Create vaults and loan brokers
        std::vector<BrokerInfo> brokers;
        for (auto const& asset : assets)
        {
            auto [tx, vaultKeylet] =
                vault.create({.owner = lender, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(vaultKeylet));

            env(vault.deposit(
                {.depositor = lender,
                 .id = vaultKeylet.key,
                 .amount = asset(50'000)}));
            env.close();

            auto const keylet =
                keylet::loanbroker(lender.id(), env.seq(lender));
            auto const testData = "spam spam spam spam";

            using namespace loanBroker;
            env(set(lender, vaultKeylet.key),
                fee(increment),
                data(testData),
                managementFeeRate(TenthBips16(100)),
                debtMaximum(Number(25'000)),
                coverRateMinimum(TenthBips32(percentageToTenthBips(10))),
                coverRateLiquidation(TenthBips32(percentageToTenthBips(25))));

            env(coverDeposit(lender, keylet.key, asset(1000)));

            brokers.emplace_back(asset, keylet.key);
        }

        // Create and update Loans
        for (auto const& broker : brokers)
        {
            using namespace loan;
            using namespace std::chrono_literals;

            auto const principalRequested = Number(1000);
            auto const startDate = env.now() + 3600s;
            auto const loanSetFee = fee(env.current()->fees().base * 2);

            auto badKeylet = keylet::vault(lender.id(), env.seq(lender));
            // Try some failure cases
            // insufficient fee - single sign
            env(set(borrower, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                ter(telINSUF_FEE_P));
            // insufficient fee - multisign
            env(set(borrower, broker.brokerID, principalRequested, startDate),
                counterparty(lender),
                msig(evan, lender),
                msig(sfCounterpartySignature, evan, borrower),
                fee(env.current()->fees().base * 5 - 1),
                ter(telINSUF_FEE_P));
            // multisign sufficient fee, but no signers set up
            env(set(borrower, broker.brokerID, principalRequested, startDate),
                counterparty(lender),
                msig(evan, lender),
                msig(sfCounterpartySignature, evan, borrower),
                fee(env.current()->fees().base * 5),
                ter(tefNOT_MULTI_SIGNING));
            // not the broker owner, no counterparty, not signed by broker owner
            env(set(borrower, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, evan),
                loanSetFee,
                ter(tefBAD_AUTH));
            // not the broker owner, counterparty is borrower
            env(set(evan, broker.brokerID, principalRequested, startDate),
                counterparty(borrower),
                sig(sfCounterpartySignature, borrower),
                loanSetFee,
                ter(tecNO_PERMISSION));
            // not a LoanBroker object, no counterparty
            env(set(lender, badKeylet.key, principalRequested, startDate),
                sig(sfCounterpartySignature, evan),
                loanSetFee,
                ter(temBAD_SIGNER));
            // not a LoanBroker object, counterparty is valid
            env(set(lender, badKeylet.key, principalRequested, startDate),
                counterparty(borrower),
                sig(sfCounterpartySignature, borrower),
                loanSetFee,
                ter(tecNO_ENTRY));
            // borrower doesn't exist
            env(set(lender, broker.brokerID, principalRequested, startDate),
                counterparty(alice),
                sig(sfCounterpartySignature, alice),
                loanSetFee,
                ter(terNO_ACCOUNT));
            // flags are checked first
            env(set(evan,
                    broker.brokerID,
                    principalRequested,
                    startDate,
                    tfLoanSetMask),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(temINVALID_FLAG));

            // Frozen trust line / locked MPT issuance
            // XRP can not be frozen
            if (!broker.asset.raw().native())
            {
                auto const brokerSLE =
                    env.le(keylet::loanbroker(broker.brokerID));
                if (!BEAST_EXPECT(brokerSLE))
                    return;
                auto const brokerPseudo = brokerSLE->at(sfAccount);
                auto const pseudoAcct =
                    Account("Broker pseudo-account", brokerPseudo);

                std::function<void(Account const& holder)> freeze;
                std::function<void(Account const& holder)> unfreeze;
                // Freeze / lock the asset
                if (broker.asset.raw().holds<Issue>())
                {
                    freeze = [&](Account const& holder) {
                        env(trust(issuer, holder[iouCurrency](0), tfSetFreeze));
                    };
                    unfreeze = [&](Account const& holder) {
                        env(trust(
                            issuer, holder[iouCurrency](0), tfClearFreeze));
                    };
                }
                else
                {
                    freeze = [&](Account const& holder) {
                        mptt.set(
                            {.account = issuer,
                             .holder = holder,
                             .flags = tfMPTLock});
                    };
                    unfreeze = [&](Account const& holder) {
                        mptt.set(
                            {.account = issuer,
                             .holder = holder,
                             .flags = tfMPTUnlock});
                    };
                }
            }

            // field length validation
            // sfData: good length, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                data(std::string(maxDataPayloadLength, 'X')),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfData: too long
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                data(std::string(maxDataPayloadLength + 1, 'Y')),
                loanSetFee,
                ter(temINVALID));

            // field range validation
            // sfOverpaymentFee: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                overpaymentFee(maxOverpaymentFee),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfOverpaymentFee: too big
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                overpaymentFee(maxOverpaymentFee + 1),
                loanSetFee,
                ter(temINVALID));

            // sfLateInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                lateInterestRate(maxLateInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfLateInterestRate: too big
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                lateInterestRate(maxLateInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfCloseInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                closeInterestRate(maxCloseInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfCloseInterestRate: too big
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                closeInterestRate(maxCloseInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfOverpaymentInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                overpaymentInterestRate(maxOverpaymentInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfOverpaymentInterestRate: too big
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                overpaymentInterestRate(maxOverpaymentInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfPaymentTotal: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentTotal(LoanSet::minPaymentTotal),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfPaymentTotal: too small (there is no max)
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                paymentTotal(LoanSet::minPaymentTotal - 1),
                loanSetFee,
                ter(temINVALID));

            // sfPaymentInterval: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentInterval(LoanSet::minPaymentInterval),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfPaymentInterval: too small (there is no max)
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                paymentInterval(LoanSet::minPaymentInterval - 1),
                loanSetFee,
                ter(temINVALID));

            // sfGracePeriod: good value, bad account
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentInterval(LoanSet::minPaymentInterval * 2),
                gracePeriod(LoanSet::minPaymentInterval * 2),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfGracePeriod: larger than paymentInterval
            env(set(evan, broker.brokerID, principalRequested, startDate),
                sig(sfCounterpartySignature, lender),
                paymentInterval(LoanSet::minPaymentInterval * 2),
                gracePeriod(LoanSet::minPaymentInterval * 3),
                loanSetFee,
                ter(temINVALID));

#if 0
            std::string testData;
            lifecycle(
                "default fields",
                env,
                lender,
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
                        keylet::loanbroker(lender.id(), env.seq(lender));

                    // fields that can't be changed
                    // LoanBrokerID
                    env(set(lender, broker.brokerID),
                        loanBrokerID(nextKeylet.key),
                        ter(tecNO_ENTRY));
                    // VaultID
                    env(set(lender, nextKeylet.key),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // Owner
                    env(set(evan, broker.brokerID),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // ManagementFeeRate
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        managementFeeRate(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateMinimum
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        coverRateMinimum(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateLiquidation
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        coverRateLiquidation(maxManagementFeeRate),
                        ter(temINVALID));

                    // fields that can be changed
                    testData = "Test Data 1234";
                    // Bad data: too long
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        data(std::string(maxDataPayloadLength + 1, 'W')),
                        ter(temINVALID));

                    // Bad debt maximum
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        debtMaximum(Number(-175, -1)),
                        ter(temINVALID));
                    // Data & Debt maximum
                    env(set(lender, broker.brokerID),
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
                lender,
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
                    env(set(lender, broker.brokerID),
                        loanBrokerID(broker->key()),
                        data(""),
                        debtMaximum(Number(0)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                });
#endif
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

BEAST_DEFINE_TESTSUITE(Loan, tx, ripple);

}  // namespace test
}  // namespace ripple
