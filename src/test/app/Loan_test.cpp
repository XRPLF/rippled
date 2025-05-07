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

#include <xrpld/app/misc/LendingHelpers.h>
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
            // Actual sequence will be based off the loan broker, but we
            // obviously don't have one of those if the amendment is disabled
            auto const loanKeylet = keylet::loan(keylet.key, env.seq(alice));
            // Other Loan transactions are disabled, too.
            // 2. LoanDelete
            env(del(alice, loanKeylet.key), ter(temDISABLED));
            // 3. LoanManage
            env(manage(alice, loanKeylet.key, tfLoanImpair), ter(temDISABLED));
            // 4. LoanDraw
            env(draw(alice, loanKeylet.key, XRP(500)), ter(temDISABLED));
#if 0
            // 5. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), ter(temDISABLED));
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

    struct LoanState
    {
        std::uint32_t previousPaymentDate = 0;
        NetClock::time_point startDate = {};
        std::uint32_t nextPaymentDate = 0;
        std::uint32_t paymentRemaining = 0;
        Number assetsAvailable = 0;
        Number principalOutstanding = 0;
        std::uint32_t flags = 0;
        std::uint32_t paymentInterval = 0;
    };

    struct VerifyLoanStatus
    {
    public:
        jtx::Env const& env;
        BrokerInfo const& broker;
        jtx::Account const& pseudoAccount;
        Keylet const& keylet;

        VerifyLoanStatus(
            jtx::Env const& env_,
            BrokerInfo const& broker_,
            jtx::Account const& pseudo_,
            Keylet const& keylet_)
            : env(env_)
            , broker(broker_)
            , pseudoAccount(pseudo_)
            , keylet(keylet_)
        {
        }

        void
        checkBroker(
            Number const& assetsAvailable,
            Number const& principalOutstanding,
            TenthBips32 interestRate,
            std::uint32_t paymentInterval,
            std::uint32_t paymentsRemaining,
            std::uint32_t ownerCount) const
        {
            using namespace jtx;
            if (auto brokerSle = env.le(keylet::loanbroker(broker.brokerID));
                env.test.BEAST_EXPECT(brokerSle))
            {
                TenthBips16 const managementFeeRate{
                    brokerSle->at(sfManagementFeeRate)};
                auto const loanInterest = LoanInterestOutstandingToVault(
                    broker.asset,
                    principalOutstanding,
                    interestRate,
                    paymentInterval,
                    paymentsRemaining,
                    managementFeeRate);
                auto const expectedDebt = principalOutstanding + loanInterest;
                env.test.BEAST_EXPECT(
                    brokerSle->at(sfDebtTotal) == expectedDebt);
                env.test.BEAST_EXPECT(
                    env.balance(pseudoAccount, broker.asset).number() ==
                    brokerSle->at(sfCoverAvailable) + assetsAvailable);
                env.test.BEAST_EXPECT(
                    brokerSle->at(sfOwnerCount) == ownerCount);

                if (auto vaultSle =
                        env.le(keylet::vault(brokerSle->at(sfVaultID)));
                    env.test.BEAST_EXPECT(vaultSle))
                {
                    Account const vaultPseudo{
                        "vaultPseudoAccount", vaultSle->at(sfAccount)};
                    env.test.BEAST_EXPECT(
                        vaultSle->at(sfAssetsAvailable) ==
                        env.balance(vaultPseudo, broker.asset).number());
                    if (ownerCount == 0)
                    {
                        env.test.BEAST_EXPECT(
                            vaultSle->at(sfAssetsTotal) ==
                            vaultSle->at(sfAssetsAvailable));
                        env.test.BEAST_EXPECT(
                            vaultSle->at(sfLossUnrealized) == 0);
                    }
                }
            }
        }

        void
        checkBroker(
            LoanState const& state,
            TenthBips32 interestRate,
            std::uint32_t ownerCount) const
        {
            checkBroker(
                state.assetsAvailable,
                state.principalOutstanding,
                interestRate,
                state.paymentInterval,
                state.paymentRemaining,
                ownerCount);
        }

        void
        operator()(
            std::uint32_t previousPaymentDate,
            std::uint32_t nextPaymentDate,
            std::uint32_t paymentRemaining,
            Number const& assetsAvailable,
            Number const& principalOutstanding,
            std::uint32_t flags) const
        {
            using namespace jtx;
            if (auto loan = env.le(keylet); env.test.BEAST_EXPECT(loan))
            {
                env.test.BEAST_EXPECT(
                    loan->at(sfPreviousPaymentDate) == previousPaymentDate);
                env.test.BEAST_EXPECT(
                    loan->at(sfNextPaymentDueDate) == nextPaymentDate);
                env.test.BEAST_EXPECT(
                    loan->at(sfPaymentRemaining) == paymentRemaining);
                env.test.BEAST_EXPECT(
                    loan->at(sfAssetsAvailable) == assetsAvailable);
                env.test.BEAST_EXPECT(
                    loan->at(sfPrincipalOutstanding) == principalOutstanding);
                env.test.BEAST_EXPECT(loan->at(sfFlags) == flags);

                auto const interestRate = TenthBips32{loan->at(sfInterestRate)};
                auto const paymentInterval = loan->at(sfPaymentInterval);
                checkBroker(
                    assetsAvailable,
                    principalOutstanding,
                    interestRate,
                    paymentInterval,
                    paymentRemaining,
                    1);

                if (auto brokerSle =
                        env.le(keylet::loanbroker(broker.brokerID));
                    env.test.BEAST_EXPECT(brokerSle))
                {
                    if (auto vaultSle =
                            env.le(keylet::vault(brokerSle->at(sfVaultID)));
                        env.test.BEAST_EXPECT(vaultSle))
                    {
                        if ((flags & lsfLoanImpaired) &&
                            !(flags & lsfLoanDefault))
                        {
                            TenthBips32 const managementFeeRate{
                                brokerSle->at(sfManagementFeeRate)};
                            env.test.BEAST_EXPECT(
                                vaultSle->at(sfLossUnrealized) ==
                                principalOutstanding +
                                    LoanInterestOutstandingToVault(
                                        broker.asset,
                                        principalOutstanding,
                                        interestRate,
                                        paymentInterval,
                                        paymentRemaining,
                                        managementFeeRate));
                        }
                        else
                        {
                            env.test.BEAST_EXPECT(
                                vaultSle->at(sfLossUnrealized) == 0);
                        }
                    }
                }
            }
        }

        void
        operator()(LoanState const& state) const
        {
            operator()(
                state.previousPaymentDate,
                state.nextPaymentDate,
                state.paymentRemaining,
                state.assetsAvailable,
                state.principalOutstanding,
                state.flags);
        };
    };

    void
    lifecycle(
        const char* label,
        jtx::Env& env,
        jtx::Account const& lender,
        jtx::Account const& borrower,
        jtx::Account const& evan,
        BrokerInfo const& broker,
        jtx::Account const& pseudoAcct,
        std::uint32_t flags,
        // The end of life callback is expected to take the loan to 0 payments
        // remaining, one way or another
        std::function<void(
            Keylet const& loanKeylet,
            VerifyLoanStatus const& verifyLoanStatus)> toEndOfLife)
    {
        auto const [keylet, loanSequence] = [&]() {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                // will be invalid
                return std::make_pair(
                    keylet::loan(broker.brokerID), std::uint32_t(0));

            // Broker has no loans
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);

            // The loan keylet is based on the LoanSequence of the _LOAN_BROKER_
            // object.
            auto const loanSequence = brokerSle->at(sfLoanSequence);
            return std::make_pair(
                keylet::loan(broker.brokerID, loanSequence), loanSequence);
        }();

        VerifyLoanStatus const verifyLoanStatus(
            env, broker, pseudoAcct, keylet);

        // No loans yet
        verifyLoanStatus.checkBroker(0, 0, TenthBips32{0}, 1, 0, 0);

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

        auto const borrowerOwnerCount = env.ownerCount(borrower);

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
        // Successfully create a Loan
        env(createJtx);

        env.close();

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 1);
        }

        auto const loanFlags = createJtx.stx->isFlag(tfLoanOverpayment)
            ? lsfLoanOverpayment
            : LedgerSpecificFlags(0);

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
        }

        verifyLoanStatus(
            0,
            startDate.time_since_epoch().count() + interval,
            total,
            principalRequest - originationFee,
            principalRequest,
            loanFlags | 0);

        // Manage the loan
        // no-op
        env(manage(lender, keylet.key, 0));
        // Only the lender can manage
        env(manage(evan, keylet.key, 0), ter(tecNO_PERMISSION));
        // unknown flags
        env(manage(lender, keylet.key, tfLoanManageMask), ter(temINVALID_FLAG));
        // combinations of flags are not allowed
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanImpair),
            ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanImpair | tfLoanDefault),
            ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanDefault),
            ter(temINVALID_FLAG));
        env(manage(
                lender,
                keylet.key,
                tfLoanUnimpair | tfLoanImpair | tfLoanDefault),
            ter(temINVALID_FLAG));
        // invalid loan ID
        env(manage(lender, broker.brokerID, tfLoanImpair), ter(tecNO_ENTRY));
        // Loan is unimpaired, can't unimpair it again
        env(manage(lender, keylet.key, tfLoanUnimpair), ter(tecNO_PERMISSION));
        // Loan is unimpaired, it can go into default, but only after it's past
        // due
        env(manage(lender, keylet.key, tfLoanDefault), ter(tecTOO_SOON));

        // Impair the loan
        env(manage(lender, keylet.key, tfLoanImpair));
        // Unimpair the loan
        env(manage(lender, keylet.key, tfLoanUnimpair));

        auto const nextDueDate =
            startDate.time_since_epoch().count() + interval;

        env.close();

        verifyLoanStatus(
            0,
            nextDueDate,
            total,
            principalRequest - originationFee,
            principalRequest,
            loanFlags | 0);

        // Can't delete the loan yet. It has payments remaining.
        env(del(lender, keylet.key), ter(tecHAS_OBLIGATIONS));

        if (BEAST_EXPECT(toEndOfLife))
            toEndOfLife(keylet, verifyLoanStatus);
        env.close();

        // Verify the loan is at EOL
        auto const assetsAvailable = [&, &keylet = keylet]() {
            if (auto loan = env.le(keylet); BEAST_EXPECT(loan))
            {
                BEAST_EXPECT(loan->at(sfPaymentRemaining) == 0);
                BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == 0);
                return loan->at(sfAssetsAvailable);
            }
            return Number(0);
        }();
        auto const borrowerStartingBalance =
            env.balance(borrower, broker.asset);

        // Try to delete the loan broker with an active loan
        env(loanBroker::del(lender, broker.brokerID), ter(tecHAS_OBLIGATIONS));
        // Ensure the above tx doesn't get ordered after the LoanDelete and
        // delete our broker!
        env.close();

        // Test failure cases
        env(del(lender, keylet.key, tfLoanOverpayment), ter(temINVALID_FLAG));
        env(del(evan, keylet.key), ter(tecNO_PERMISSION));
        env(del(lender, broker.brokerID), ter(tecNO_ENTRY));

        // Delete the loan
        env(del(lender, keylet.key));
        env.close();

        // No loans left
        verifyLoanStatus.checkBroker(0, 0, interest, 1, 0, 0);

        BEAST_EXPECT(
            env.balance(borrower, broker.asset).value() ==
            borrowerStartingBalance.value() + assetsAvailable);
        BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwnerCount);

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
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
        env(trust(borrower, iouAsset(1'000'000)));
        env(trust(evan, iouAsset(1'000'000)));
        env(pay(issuer, evan, iouAsset(100'000)));
        env(pay(issuer, lender, iouAsset(100'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, iouAsset(1'000)));
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});
        mptt.authorize({.account = evan});
        env(pay(issuer, lender, mptAsset(100'000)));
        env(pay(issuer, evan, mptAsset(100'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, mptAsset(1'000)));
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
            auto const& asset = broker.asset.raw();
            testcase << "Lifecycle: "
                     << (asset.native()                ? "XRP "
                             : asset.holds<Issue>()    ? "IOU "
                             : asset.holds<MPTIssue>() ? "MPT "
                                                       : "Unknown ");

            using namespace loan;
            using namespace std::chrono_literals;
            using d = NetClock::duration;
            using tp = NetClock::time_point;

            Number const principalRequest = broker.asset(1000).value();
            Number const maxCoveredLoanRequest =
                broker.asset(maxCoveredLoanValue).value();
            Number const totalVaultRequest = broker.asset(vaultDeposit).value();
            Number const debtMaximumRequest =
                broker.asset(debtMaximumParameter).value();

            auto const startDate = env.now() + 3600s;
            auto const loanSetFee = fee(env.current()->fees().base * 2);

            auto const pseudoAcct = [&]() {
                auto const brokerSle =
                    env.le(keylet::loanbroker(broker.brokerID));
                if (!BEAST_EXPECT(brokerSle))
                    return lender;
                auto const brokerPseudo = brokerSle->at(sfAccount);
                return Account("Broker pseudo-account", brokerPseudo);
            }();

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
                auto const brokerSle =
                    env.le(keylet::loanbroker(broker.brokerID));
                if (!BEAST_EXPECT(brokerSle))
                    return;

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

            auto currentState = [&](Keylet const& loanKeylet,
                                    VerifyLoanStatus const& verifyLoanStatus) {
                // Lookup the current loan state
                LoanState state;
                if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
                {
                    state.previousPaymentDate = loan->at(sfPreviousPaymentDate);
                    BEAST_EXPECT(state.previousPaymentDate == 0);
                    state.startDate = tp{d{loan->at(sfStartDate)}};
                    state.nextPaymentDate = loan->at(sfNextPaymentDueDate);
                    BEAST_EXPECT(
                        tp{d{state.nextPaymentDate}} == state.startDate + 600s);
                    state.paymentRemaining = loan->at(sfPaymentRemaining);
                    BEAST_EXPECT(state.paymentRemaining == 12);
                    state.assetsAvailable = loan->at(sfAssetsAvailable);
                    BEAST_EXPECT(
                        state.assetsAvailable == broker.asset(999).value());
                    state.principalOutstanding =
                        loan->at(sfPrincipalOutstanding);
                    BEAST_EXPECT(
                        state.principalOutstanding ==
                        broker.asset(1000).value());
                    state.paymentInterval = loan->at(sfPaymentInterval);
                    BEAST_EXPECT(state.paymentInterval == 600);
                    state.flags = loan->at(sfFlags);
                }

                verifyLoanStatus(state);

                return state;
            };

            auto defaultBeforeStartDate = [&](std::uint32_t baseFlag) {
                return [&, baseFlag](
                           Keylet const& loanKeylet,
                           VerifyLoanStatus const& verifyLoanStatus) {
                    // toEndOfLife
                    //
                    // Default the loan

                    // Initialize values with the current state
                    auto state = currentState(loanKeylet, verifyLoanStatus);
                    BEAST_EXPECT(state.flags == baseFlag);

                    // Impair the loan
                    env(manage(lender, loanKeylet.key, tfLoanImpair));

                    state.flags |= tfLoanImpair;
                    state.nextPaymentDate =
                        env.now().time_since_epoch().count();
                    verifyLoanStatus(state);

                    // Once the loan is impaired, it can't be impaired again
                    env(manage(lender, loanKeylet.key, tfLoanImpair),
                        ter(tecNO_PERMISSION));

                    auto const nextDueDate = tp{d{state.nextPaymentDate}};

                    // Can't default the loan yet. The grace period hasn't
                    // expired
                    env(manage(lender, loanKeylet.key, tfLoanDefault),
                        ter(tecTOO_SOON));

                    // Let some time pass so that the loan can be
                    // defaulted
                    env.close(nextDueDate + 60s);

                    // Impaired loans can't be drawn against
                    env(draw(borrower, loanKeylet.key, broker.asset(100)),
                        ter(tecNO_PERMISSION));

                    // Default the loan
                    env(manage(lender, loanKeylet.key, tfLoanDefault));

                    state.flags |= tfLoanDefault;
                    state.paymentRemaining = 0;
                    state.assetsAvailable = 0;
                    state.principalOutstanding = 0;
                    verifyLoanStatus(state);

                    // Defaulted loans can't be drawn against, either
                    env(draw(borrower, loanKeylet.key, broker.asset(100)),
                        ter(tecNO_PERMISSION));

                    // Once a loan is defaulted, it can't be managed
                    env(manage(lender, loanKeylet.key, tfLoanUnimpair),
                        ter(tecNO_PERMISSION));
                    env(manage(lender, loanKeylet.key, tfLoanImpair),
                        ter(tecNO_PERMISSION));
                };
            };

            // There are a lot of fields that can be set on a loan, but most of
            // them only affect the "math" when a payment is made. The only one
            // that really affects behavior is the `tfLoanOverpayment` flag.
            lifecycle(
                "Loan overpayment allowed - Default before start date",
                env,
                lender,
                borrower,
                evan,
                broker,
                pseudoAcct,
                tfLoanOverpayment,
                defaultBeforeStartDate(lsfLoanOverpayment));

            lifecycle(
                "Loan overpayment prohibited - Default before start date",
                env,
                lender,
                borrower,
                evan,
                broker,
                pseudoAcct,
                0,
                defaultBeforeStartDate(0));

            lifecycle(
                "Loan overpayment allowed - Draw then default",
                env,
                lender,
                borrower,
                evan,
                broker,
                pseudoAcct,
                tfLoanOverpayment,
                [&](Keylet const& loanKeylet,
                    VerifyLoanStatus const& verifyLoanStatus) {
                    // toEndOfLife
                    //
                    // Initialize values with the current state
                    auto state = currentState(loanKeylet, verifyLoanStatus);
                    BEAST_EXPECT(state.flags == lsfLoanOverpayment);
                    auto const borrowerStartingBalance =
                        env.balance(borrower, broker.asset);

                    // Draw the balance
                    env(draw(
                            borrower,
                            keylet::loan(uint256(0)).key,
                            broker.asset(10)),
                        ter(temINVALID));
                    env(draw(borrower, loanKeylet.key, broker.asset(-100)),
                        ter(temBAD_AMOUNT));
                    env(draw(borrower, broker.brokerID, broker.asset(100)),
                        ter(tecNO_ENTRY));
                    env(draw(evan, loanKeylet.key, broker.asset(500)),
                        ter(tecNO_PERMISSION));
                    env(draw(borrower, loanKeylet.key, broker.asset(500)),
                        ter(tecTOO_SOON));

                    // Advance to the start date of the loan
                    env.close(state.startDate + 5s);
                    env(draw(borrower, loanKeylet.key, broker.asset(10000)),
                        ter(tecINSUFFICIENT_FUNDS));
                    {
                        auto const otherAsset =
                            broker.asset.raw() == assets[0].raw() ? assets[1]
                                                                  : assets[0];
                        env(draw(borrower, loanKeylet.key, otherAsset(100)),
                            ter(tecWRONG_ASSET));
                    }

                    verifyLoanStatus(state);

                    // Need to account for fees if the loan is in XRP
                    PrettyAmount adjustment = broker.asset(0);
                    if (broker.asset.raw().native())
                    {
                        adjustment = 5 * env.current()->fees().base;
                    }

                    // Draw about half the balance
                    auto const drawAmount = broker.asset(500);
                    env(draw(borrower, loanKeylet.key, drawAmount));

                    state.assetsAvailable -= drawAmount.number();
                    verifyLoanStatus(state);
                    BEAST_EXPECT(
                        env.balance(borrower, broker.asset) ==
                        borrowerStartingBalance + drawAmount - adjustment);

                    // move past the due date + grace period (60s)
                    env.close(tp{d{state.nextPaymentDate}} + 60s + 20s);
                    // Try to draw
                    env(draw(borrower, loanKeylet.key, broker.asset(100)),
                        ter(tecNO_PERMISSION));

                    // default the loan
                    env(manage(lender, loanKeylet.key, tfLoanDefault));
                    state.paymentRemaining = 0;
                    state.assetsAvailable = 0;
                    state.principalOutstanding = 0;
                    state.flags |= tfLoanDefault;

                    verifyLoanStatus(state);

                    // Same error, different check
                    env(draw(borrower, loanKeylet.key, broker.asset(100)),
                        ter(tecNO_PERMISSION));

                    // Default
                });

#if 0 
            lifecycle(
                "Loan overpayment prohibited - Pay off",
                env,
                lender,
                borrower,
                evan,
                broker,
                pseudoAcct,
                0,
                [&](Keylet const& loanKeylet,
                    VerifyLoanStatus const& verifyLoanStatus) {
                    // toEndOfLife
                    //
        // TODO: Draw and make some payments

                    // Make payments down to 0

                    // TODO: Try to impair a paid off loan
                });
#endif

            if (auto brokerSle = env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);

                auto const coverAvailable = brokerSle->at(sfCoverAvailable);
                env(loanBroker::coverWithdraw(
                    lender,
                    broker.brokerID,
                    STAmount(broker.asset, coverAvailable)));
                env.close();

                brokerSle = env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerSle && brokerSle->at(sfCoverAvailable) == 0);
            }
            // Verify we can delete the loan broker
            env(loanBroker::del(lender, broker.brokerID));
            env.close();
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
