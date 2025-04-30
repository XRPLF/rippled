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
        jtx::Account const& lender,
        jtx::Account const& borrower,
        jtx::Account const& evan,
        BrokerInfo const& broker,
        std::uint32_t flags,
        std::function<jtx::JTx(jtx::JTx const&)> modifyJTx,
        std::function<void(SLE::const_ref)> checkLoan,
        std::function<void(SLE::const_ref)> changeLoan,
        std::function<void(SLE::const_ref)> checkChangedLoan)
    {
        auto const [keylet, loanSequence] = [&]() {
            auto const brokerSLE = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSLE))
                // will be invalid
                return std::make_pair(
                    keylet::loan(broker.brokerID), std::uint32_t(0));

            // The loan keylet is based on the LoanSequence of the _LOAN_BROKER_
            // object.
            auto const loanSequence = brokerSLE->at(sfLoanSequence);
            return std::make_pair(
                keylet::loan(broker.brokerID, loanSequence), loanSequence);
        }();
        if (!BEAST_EXPECT(loanSequence != 0))
            return;
        {
            auto const& asset = broker.asset.raw();
            testcase << "Lifecycle: "
                     << (asset.native()                ? "XRP "
                             : asset.holds<Issue>()    ? "IOU "
                             : asset.holds<MPTIssue>() ? "MPT "
                                                       : "Unknown ")
                     << label;
        }

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest = broker.asset(1000).value();
        auto const startDate = env.now() + 3600s;
        auto const originationFee = broker.asset(1).value();
        auto const serviceFee = broker.asset(2).value();
        auto const lateFee = broker.asset(3).value();
        auto const closeFee = broker.asset(4).value();
        auto const overFee = percentageToTenthBips(5) / 10;
        auto const interest = percentageToTenthBips(12);
        // 2.4%
        auto const lateInterest = percentageToTenthBips(24) / 10;
        auto const closeInterest = percentageToTenthBips(36) / 10;
        auto const overpaymentInterest = percentageToTenthBips(48) / 10;
        auto const total = 12;
        auto const interval = 600;
        auto const grace = 60;

        // Use the defined values
        auto createJtx = env.jt(
            set(borrower, broker.brokerID, principalRequest, startDate, flags),
            sig(sfCounterpartySignature, lender),
            loanOriginationFee(originationFee),
            loanServiceFee(serviceFee),
            latePaymentFee(lateFee),
            closePaymentFee(closeFee),
            overpaymentFee(overFee),
            interestRate(interest),
            lateInterestRate(lateInterest),
            closeInterestRate(closeInterest),
            overpaymentInterestRate(overpaymentInterest),
            paymentTotal(total),
            paymentInterval(interval),
            gracePeriod(grace),
            fee(loanSetFee));
        // Modify as desired
        if (modifyJTx)
            createJtx = modifyJTx(createJtx);
        // Successfully create a Loan
        env(createJtx);

        env.close();

        if (auto loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            // log << "loan after create: " << to_string(loan->getJson())
            //     << std::endl;
            BEAST_EXPECT(
                loan->isFlag(lsfLoanOverpayment) ==
                createJtx.stx->isFlag(tfLoanOverpayment));
            BEAST_EXPECT(loan->at(sfLoanSequence) == loanSequence);
            BEAST_EXPECT(loan->at(sfBorrower) == borrower.id());
            BEAST_EXPECT(loan->at(sfLoanBrokerID) == broker.brokerID);
            BEAST_EXPECT(loan->at(sfLoanOriginationFee) == originationFee);
            BEAST_EXPECT(loan->at(sfLoanServiceFee) == serviceFee);
            BEAST_EXPECT(loan->at(sfLatePaymentFee) == lateFee);
            BEAST_EXPECT(loan->at(sfClosePaymentFee) == closeFee);
            BEAST_EXPECT(loan->at(sfOverpaymentFee) == overFee);
            BEAST_EXPECT(loan->at(sfInterestRate) == interest);
            BEAST_EXPECT(loan->at(sfLateInterestRate) == lateInterest);
            BEAST_EXPECT(loan->at(sfCloseInterestRate) == closeInterest);
            BEAST_EXPECT(
                loan->at(sfOverpaymentInterestRate) == overpaymentInterest);
            BEAST_EXPECT(
                loan->at(sfStartDate) == startDate.time_since_epoch().count());
            BEAST_EXPECT(loan->at(sfPaymentInterval) == interval);
            BEAST_EXPECT(loan->at(sfGracePeriod) == grace);
            BEAST_EXPECT(loan->at(sfPreviousPaymentDate) == 0);
            BEAST_EXPECT(
                loan->at(sfNextPaymentDueDate) ==
                startDate.time_since_epoch().count() + interval);
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == total);
            BEAST_EXPECT(
                loan->at(sfAssetsAvailable) ==
                principalRequest - originationFee);
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == principalRequest);
            if (checkLoan)
                checkLoan(loan);

#if 0
            auto verifyStatus = [&env, &broker, &loan, this](
                                    auto previousPaymentDate,
                                    auto nextPaymentDate,
                                    auto paymentRemaining,
                                    auto assetsAvailable,
                                    auto principalOutstanding) {
                auto const available = broker.asset(assetsAvailable);
                auto const outstanding = broker.asset(principalOutstanding);
                BEAST_EXPECT(
                    loan->at(sfPreviousPaymentDate) == previousPaymentDate);
                BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == nextPaymentDate);
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == paymentRemaining);
                BEAST_EXPECT(loan->at(sfAssetsAvailable) == available.number());
                BEAST_EXPECT(
                    loan->at(sfPrincipalOutstanding) == outstanding.number());
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
#endif
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

        auto const coverDepositParameter = 1000;
        auto const coverRateMinParameter = percentageToTenthBips(10);
        auto const maxCoveredLoanValue = 1000 * 100 / 10;
        auto const vaultDeposit = 50'000;
        auto const debtMaximumParameter = 25'000;

        // Create vaults and loan brokers
        std::vector<BrokerInfo> brokers;
        for (auto const& asset : assets)
        {
            auto const deposit = asset(vaultDeposit);
            auto const debtMaximumValue = asset(debtMaximumParameter).value();
            auto const coverDepositValue = asset(coverDepositParameter).value();

            auto [tx, vaultKeylet] =
                vault.create({.owner = lender, .asset = asset});
            env(tx);
            env.close();
            BEAST_EXPECT(env.le(vaultKeylet));

            env(vault.deposit(
                {.depositor = lender,
                 .id = vaultKeylet.key,
                 .amount = deposit}));
            env.close();
            if (auto const vault = env.le(keylet::vault(vaultKeylet.key));
                BEAST_EXPECT(vault))
            {
                BEAST_EXPECT(vault->at(sfAssetsAvailable) == deposit.value());
            }

            auto const keylet =
                keylet::loanbroker(lender.id(), env.seq(lender));
            auto const testData = "spam spam spam spam";

            using namespace loanBroker;
            env(set(lender, vaultKeylet.key),
                fee(increment),
                data(testData),
                managementFeeRate(TenthBips16(100)),
                debtMaximum(debtMaximumValue),
                coverRateMinimum(TenthBips32(coverRateMinParameter)),
                coverRateLiquidation(TenthBips32(percentageToTenthBips(25))));

            env(coverDeposit(lender, keylet.key, coverDepositValue));

            brokers.emplace_back(asset, keylet.key);
        }

        // Create and update Loans
        for (auto const& broker : brokers)
        {
            using namespace loan;
            using namespace std::chrono_literals;

            Number const principalRequest = broker.asset(1000).value();
            Number const maxCoveredLoanRequest =
                broker.asset(maxCoveredLoanValue).value();
            Number const totalVaultRequest = broker.asset(vaultDeposit).value();
            Number const debtMaximumRequest =
                broker.asset(debtMaximumParameter).value();

            auto const startDate = env.now() + 3600s;
            auto const loanSetFee = fee(env.current()->fees().base * 2);

            auto badKeylet = keylet::vault(lender.id(), env.seq(lender));
            // Try some failure cases
            // flags are checked first
            env(set(evan,
                    broker.brokerID,
                    principalRequest,
                    startDate,
                    tfLoanSetMask),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(temINVALID_FLAG));

            // field length validation
            // sfData: good length, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                data(std::string(maxDataPayloadLength, 'X')),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfData: too long
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                data(std::string(maxDataPayloadLength + 1, 'Y')),
                loanSetFee,
                ter(temINVALID));

            // field range validation
            // sfOverpaymentFee: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                overpaymentFee(maxOverpaymentFee),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfOverpaymentFee: too big
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                overpaymentFee(maxOverpaymentFee + 1),
                loanSetFee,
                ter(temINVALID));

            // sfLateInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                lateInterestRate(maxLateInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfLateInterestRate: too big
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                lateInterestRate(maxLateInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfCloseInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                closeInterestRate(maxCloseInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfCloseInterestRate: too big
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                closeInterestRate(maxCloseInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfOverpaymentInterestRate: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                overpaymentInterestRate(maxOverpaymentInterestRate),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfOverpaymentInterestRate: too big
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                overpaymentInterestRate(maxOverpaymentInterestRate + 1),
                loanSetFee,
                ter(temINVALID));

            // sfPaymentTotal: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentTotal(LoanSet::minPaymentTotal),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfPaymentTotal: too small (there is no max)
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                paymentTotal(LoanSet::minPaymentTotal - 1),
                loanSetFee,
                ter(temINVALID));

            // sfPaymentInterval: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentInterval(LoanSet::minPaymentInterval),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfPaymentInterval: too small (there is no max)
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                paymentInterval(LoanSet::minPaymentInterval - 1),
                loanSetFee,
                ter(temINVALID));

            // sfGracePeriod: good value, bad account
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, borrower),
                paymentInterval(LoanSet::minPaymentInterval * 2),
                gracePeriod(LoanSet::minPaymentInterval * 2),
                loanSetFee,
                ter(tefBAD_AUTH));
            // sfGracePeriod: larger than paymentInterval
            env(set(evan, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                paymentInterval(LoanSet::minPaymentInterval * 2),
                gracePeriod(LoanSet::minPaymentInterval * 3),
                loanSetFee,
                ter(temINVALID));

            // insufficient fee - single sign
            env(set(borrower, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                ter(telINSUF_FEE_P));
            // insufficient fee - multisign
            env(set(borrower, broker.brokerID, principalRequest, startDate),
                counterparty(lender),
                msig(evan, lender),
                msig(sfCounterpartySignature, evan, borrower),
                fee(env.current()->fees().base * 5 - 1),
                ter(telINSUF_FEE_P));
            // multisign sufficient fee, but no signers set up
            env(set(borrower, broker.brokerID, principalRequest, startDate),
                counterparty(lender),
                msig(evan, lender),
                msig(sfCounterpartySignature, evan, borrower),
                fee(env.current()->fees().base * 5),
                ter(tefNOT_MULTI_SIGNING));
            // not the broker owner, no counterparty, not signed by broker
            // owner
            env(set(borrower, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, evan),
                loanSetFee,
                ter(tefBAD_AUTH));
            // bad start date - in the past
            env(set(evan,
                    broker.brokerID,
                    principalRequest,
                    env.closed()->info().closeTime - 1s),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecEXPIRED));
            // not the broker owner, counterparty is borrower
            env(set(evan, broker.brokerID, principalRequest, startDate),
                counterparty(borrower),
                sig(sfCounterpartySignature, borrower),
                loanSetFee,
                ter(tecNO_PERMISSION));
            // can not lend money to yourself
            env(set(lender, broker.brokerID, principalRequest, startDate),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecNO_PERMISSION));
            // not a LoanBroker object, no counterparty
            env(set(lender, badKeylet.key, principalRequest, startDate),
                sig(sfCounterpartySignature, evan),
                loanSetFee,
                ter(temBAD_SIGNER));
            // not a LoanBroker object, counterparty is valid
            env(set(lender, badKeylet.key, principalRequest, startDate),
                counterparty(borrower),
                sig(sfCounterpartySignature, borrower),
                loanSetFee,
                ter(tecNO_ENTRY));
            // borrower doesn't exist
            env(set(lender, broker.brokerID, principalRequest, startDate),
                counterparty(alice),
                sig(sfCounterpartySignature, alice),
                loanSetFee,
                ter(terNO_ACCOUNT));

            // Request more funds than the vault has available
            env(set(evan, broker.brokerID, totalVaultRequest + 1, startDate),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecINSUFFICIENT_FUNDS));

            // Request more funds than the broker's first-loss capital can
            // cover.
            env(set(evan,
                    broker.brokerID,
                    maxCoveredLoanRequest + 1,
                    startDate),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecINSUFFICIENT_FUNDS));

            // Frozen trust line / locked MPT issuance
            // XRP can not be frozen, but run through the loop anyway to test
            // the tecLIMIT_EXCEEDED case
            {
                auto const brokerSLE =
                    env.le(keylet::loanbroker(broker.brokerID));
                if (!BEAST_EXPECT(brokerSLE))
                    return;
                auto const brokerPseudo = brokerSLE->at(sfAccount);
                auto const pseudoAcct =
                    Account("Broker pseudo-account", brokerPseudo);

                auto const [freeze, deepfreeze, unfreeze, expectedResult] =
                    [&]() -> std::tuple<
                              std::function<void(Account const& holder)>,
                              std::function<void(Account const& holder)>,
                              std::function<void(Account const& holder)>,
                              TER> {
                    // Freeze / lock the asset
                    std::function<void(Account const& holder)> empty;
                    if (broker.asset.raw().native())
                    {
                        // XRP can't be frozen
                        return std::make_tuple(empty, empty, empty, tesSUCCESS);
                    }
                    else if (broker.asset.raw().holds<Issue>())
                    {
                        auto freeze = [&](Account const& holder) {
                            env(trust(
                                issuer, holder[iouCurrency](0), tfSetFreeze));
                        };
                        auto deepfreeze = [&](Account const& holder) {
                            env(trust(
                                issuer,
                                holder[iouCurrency](0),
                                tfSetFreeze | tfSetDeepFreeze));
                        };
                        auto unfreeze = [&](Account const& holder) {
                            env(trust(
                                issuer,
                                holder[iouCurrency](0),
                                tfClearFreeze | tfClearDeepFreeze));
                        };
                        return std::make_tuple(
                            freeze, deepfreeze, unfreeze, tecFROZEN);
                    }
                    else
                    {
                        auto freeze = [&](Account const& holder) {
                            mptt.set(
                                {.account = issuer,
                                 .holder = holder,
                                 .flags = tfMPTLock});
                        };
                        auto unfreeze = [&](Account const& holder) {
                            mptt.set(
                                {.account = issuer,
                                 .holder = holder,
                                 .flags = tfMPTUnlock});
                        };
                        return std::make_tuple(
                            freeze, empty, unfreeze, tecLOCKED);
                    }
                }();

                // Try freezing both the lender and the pseudo-account
                for (auto const& account : {lender, pseudoAcct})
                {
                    if (freeze)
                    {
                        // Freeze the account
                        freeze(account);

                        // Try to create a loan with a frozen line
                        env(set(evan,
                                broker.brokerID,
                                debtMaximumRequest,
                                startDate),
                            sig(sfCounterpartySignature, lender),
                            loanSetFee,
                            ter(expectedResult));

                        // Unfreeze the account
                        BEAST_EXPECT(unfreeze);
                        unfreeze(account);
                    }

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan,
                            broker.brokerID,
                            debtMaximumRequest + 1,
                            startDate),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(tecLIMIT_EXCEEDED));
                }

                // Deep freeze the borrower, which prevents them from receiving
                // funds
                if (deepfreeze)
                {
                    // Make sure evan has a trust line that so the issuer can
                    // freeze it. (Don't need to do this for the borrower,
                    // because LoanDraw will create a line to the borrower
                    // automatically.)
                    env(trust(evan, issuer[iouCurrency](100'000)));

                    // Freeze evan
                    deepfreeze(evan);

                    // Try to create a loan with a deep frozen line
                    env(set(evan,
                            broker.brokerID,
                            debtMaximumRequest,
                            startDate),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(expectedResult));

                    // Unfreeze evan
                    BEAST_EXPECT(unfreeze);
                    unfreeze(evan);

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan,
                            broker.brokerID,
                            debtMaximumRequest + 1,
                            startDate),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(tecLIMIT_EXCEEDED));
                }
            }

            // Finally! Create a loan
            std::string testData;
            // There are a lot of fields that can be set on a loan, but most of
            // them only affect the "math" when a payment is made. The only one
            // that really affects behavior is the `tfLoanOverpayment` flag.
            lifecycle(
                "Loan overpayment allowed",
                env,
                lender,
                borrower,
                evan,
                broker,
                tfLoanOverpayment,
                {},
                [&](SLE::const_ref broker) {
                    // Extra checks
                },
                [&](SLE::const_ref broker) {
                    // Modifications
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                });

#if 0
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
