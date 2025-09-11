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
#include <test/jtx/batch.h>
#include <test/jtx/fee.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/tx/detail/LoanSet.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
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
        jtx::testable_amendments() | featureMPTokensV1 |
        featureSingleAssetVault | featureLendingProtocol};

    static constexpr auto const coverDepositParameter = 1000;
    static constexpr auto const coverRateMinParameter =
        percentageToTenthBips(10);
    static constexpr auto const coverRateLiquidationParameter =
        percentageToTenthBips(25);
    static constexpr auto const maxCoveredLoanValue = 1000 * 100 / 10;
    static constexpr auto const vaultDeposit = 1'000'000;
    static constexpr auto const debtMaximumParameter = 25'000;
    std::string const iouCurrency{"IOU"};

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

            // counter party signature is optional on LoanSet. Confirm that by
            // sending transaction without one.
            auto setTx =
                env.jt(set(alice, keylet.key, Number(10000)), ter(temDISABLED));
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
#if LOANDRAW && 0
            // 4. LoanDraw
            env(draw(alice, loanKeylet.key, XRP(500)), ter(temDISABLED));
#endif
            // 5. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), ter(temDISABLED));
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
        Number const principalRequested;
        Number principalOutstanding = 0;
        std::uint32_t flags = 0;
        std::uint32_t paymentInterval = 0;
    };

    struct VerifyLoanStatus
    {
    public:
        jtx::Env const& env;
        BrokerInfo const& broker;
        Number const& loanAmount;
        jtx::Account const& pseudoAccount;
        Keylet const& keylet;

        VerifyLoanStatus(
            jtx::Env const& env_,
            BrokerInfo const& broker_,
            Number const& loanAmount_,
            jtx::Account const& pseudo_,
            Keylet const& keylet_)
            : env(env_)
            , broker(broker_)
            , loanAmount(loanAmount_)
            , pseudoAccount(pseudo_)
            , keylet(keylet_)
        {
        }

        void
        checkBroker(
            Number const& principalRequested,
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
                auto const loanInterest = loanInterestOutstandingMinusFee(
                    broker.asset,
                    principalRequested,
                    principalOutstanding,
                    interestRate,
                    paymentInterval,
                    paymentsRemaining,
                    managementFeeRate);
                auto const brokerDebt = brokerSle->at(sfDebtTotal);
                auto const expectedDebt = principalOutstanding + loanInterest;
                env.test.BEAST_EXPECT(
                    // Allow some slop for rounding
                    brokerDebt == expectedDebt ||
                    (expectedDebt != Number(0) &&
                     ((brokerDebt - expectedDebt) / expectedDebt <
                      Number(1, -8))));
                env.test.BEAST_EXPECT(
                    env.balance(pseudoAccount, broker.asset).number() ==
                    brokerSle->at(sfCoverAvailable));
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
                        // Allow some slop for rounding IOUs
                        auto const total = vaultSle->at(sfAssetsTotal);
                        auto const available = vaultSle->at(sfAssetsAvailable);
                        env.test.BEAST_EXPECT(
                            total == available ||
                            (!broker.asset.raw().native() &&
                             broker.asset.raw().holds<Issue>() &&
                             available != 0 &&
                             ((total - available) / available <
                              Number(1, -6))));
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
                state.principalRequested,
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
            Number const& principalRequested,
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
#if LOANDRAW
                env.test.BEAST_EXPECT(loan->at(sfAssetsAvailable) == 0);
#endif
                env.test.BEAST_EXPECT(
                    loan->at(sfPrincipalRequested) == principalRequested);
                env.test.BEAST_EXPECT(
                    loan->at(sfPrincipalOutstanding) == principalOutstanding);
                env.test.BEAST_EXPECT(
                    loan->at(sfPrincipalRequested) ==
                    broker.asset(loanAmount).value());
                env.test.BEAST_EXPECT(loan->at(sfFlags) == flags);

                auto const interestRate = TenthBips32{loan->at(sfInterestRate)};
                auto const paymentInterval = loan->at(sfPaymentInterval);
                checkBroker(
                    principalRequested,
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
                                    loanInterestOutstandingMinusFee(
                                        broker.asset,
                                        principalRequested,
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
                state.principalRequested,
                state.principalOutstanding,
                state.flags);
        };
    };

    BrokerInfo
    createVaultAndBroker(
        jtx::Env& env,
        jtx::PrettyAsset const& asset,
        jtx::Account const& lender,
        std::optional<Number> debtMax = std::nullopt)
    {
        using namespace jtx;

        Vault vault{env};

        auto const deposit = asset(vaultDeposit);
        auto const debtMaximumValue = debtMax
            ? STAmount{asset.raw(), *debtMax}
            : asset(debtMaximumParameter).value();
        auto const coverDepositValue = asset(coverDepositParameter).value();

        auto [tx, vaultKeylet] =
            vault.create({.owner = lender, .asset = asset});
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        env(vault.deposit(
            {.depositor = lender, .id = vaultKeylet.key, .amount = deposit}));
        env.close();
        if (auto const vault = env.le(keylet::vault(vaultKeylet.key));
            BEAST_EXPECT(vault))
        {
            BEAST_EXPECT(vault->at(sfAssetsAvailable) == deposit.value());
        }

        auto const keylet = keylet::loanbroker(lender.id(), env.seq(lender));
        auto const testData = "spam spam spam spam";

        using namespace loanBroker;
        env(set(lender, vaultKeylet.key),
            data(testData),
            managementFeeRate(TenthBips16(100)),
            debtMaximum(debtMaximumValue),
            coverRateMinimum(TenthBips32(coverRateMinParameter)),
            coverRateLiquidation(TenthBips32(coverRateLiquidationParameter)));

        env(coverDeposit(lender, keylet.key, coverDepositValue));

        env.close();

        return {asset, keylet.key};
    }

    void
    lifecycle(
        std::string const& caseLabel,
        char const* label,
        jtx::Env& env,
        Number const& loanAmount,
        int interestExponent,
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
            env, broker, loanAmount, pseudoAcct, keylet);

        // No loans yet
        verifyLoanStatus.checkBroker(
            broker.asset(loanAmount).value(), 0, TenthBips32{0}, 1, 0, 0);

        if (!BEAST_EXPECT(loanSequence != 0))
            return;

        testcase << caseLabel << " " << label;

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        auto const borrowerOwnerCount = env.ownerCount(borrower);

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest = broker.asset(loanAmount).value();
        auto const originationFee = broker.asset(1).value();
        auto const serviceFee = broker.asset(2).value();
        auto const lateFee = broker.asset(3).value();
        auto const closeFee = broker.asset(4).value();

        auto applyExponent = [interestExponent,
                              this](TenthBips32 value) mutable {
            BEAST_EXPECT(value > TenthBips32(0));
            while (interestExponent > 0)
            {
                auto const oldValue = value;
                value *= 10;
                --interestExponent;
                BEAST_EXPECT(value / 10 == oldValue);
            }
            while (interestExponent < 0)
            {
                auto const oldValue = value;
                value /= 10;
                ++interestExponent;
                BEAST_EXPECT(value * 10 == oldValue);
            }
            return value;
        };

        auto const overFee = applyExponent(percentageToTenthBips(5) / 10);
        auto const interest = applyExponent(percentageToTenthBips(12));
        // 2.4%
        auto const lateInterest = applyExponent(percentageToTenthBips(24) / 10);
        auto const closeInterest =
            applyExponent(percentageToTenthBips(36) / 10);
        auto const overpaymentInterest =
            applyExponent(percentageToTenthBips(48) / 10);
        auto const total = 12;
        auto const interval = 600;
        auto const grace = 60;

        auto const borrowerStartbalance = env.balance(borrower, broker.asset);

        // Use the defined values
        auto createJtx = env.jt(
            set(borrower, broker.brokerID, principalRequest, flags),
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

        auto const startDate =
            env.current()->info().parentCloseTime.time_since_epoch().count();

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 1);
        }

        {
            // Need to account for fees if the loan is in XRP
            PrettyAmount adjustment = broker.asset(0);
            if (broker.asset.raw().native())
            {
                adjustment = 2 * env.current()->fees().base;
            }

            BEAST_EXPECT(
                env.balance(borrower, broker.asset).value() ==
                borrowerStartbalance.value() + principalRequest -
                    originationFee - adjustment.value());
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
            BEAST_EXPECT(loan->at(sfStartDate) == startDate);
            BEAST_EXPECT(loan->at(sfPaymentInterval) == interval);
            BEAST_EXPECT(loan->at(sfGracePeriod) == grace);
            BEAST_EXPECT(loan->at(sfPreviousPaymentDate) == 0);
            BEAST_EXPECT(
                loan->at(sfNextPaymentDueDate) == startDate + interval);
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == total);
#if LOANDRAW
            BEAST_EXPECT(loan->at(sfAssetsAvailable) == beast::zero);
#endif
            BEAST_EXPECT(loan->at(sfPrincipalRequested) == principalRequest);
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == principalRequest);
        }

        verifyLoanStatus(
            0,
            startDate + interval,
            total,
            principalRequest,
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

        auto const nextDueDate = startDate + interval;

        env.close();

        verifyLoanStatus(
            0,
            nextDueDate,
            total,
            principalRequest,
            principalRequest,
            loanFlags | 0);

        // Can't delete the loan yet. It has payments remaining.
        env(del(lender, keylet.key), ter(tecHAS_OBLIGATIONS));

        if (BEAST_EXPECT(toEndOfLife))
            toEndOfLife(keylet, verifyLoanStatus);
        env.close();

        // Verify the loan is at EOL
        if (auto loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == 0);
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == 0);
        }
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
        verifyLoanStatus.checkBroker(
            broker.asset(1000).value(), 0, interest, 1, 0, 0);

        BEAST_EXPECT(
            env.balance(borrower, broker.asset).value() ==
            borrowerStartingBalance.value());
        BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwnerCount);

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
        }
    }

    template <class TAsset, std::size_t NAsset>
    void
    testCaseWrapper(
        jtx::Env& env,
        jtx::MPTTester& mptt,
        std::array<TAsset, NAsset> const& assets,
        BrokerInfo const& broker,
        Number const& loanAmount,
        int interestExponent)
    {
        using namespace jtx;

        auto const& asset = broker.asset.raw();
        auto const caseLabel = [&]() {
            std::stringstream ss;
            ss << "Lifecycle: " << loanAmount << " "
               << (asset.native()                ? "XRP"
                       : asset.holds<Issue>()    ? "IOU"
                       : asset.holds<MPTIssue>() ? "MPT"
                                                 : "Unknown")
               << " Scale interest to: " << interestExponent << " ";
            return ss.str();
        }();
        testcase << caseLabel;

        using namespace loan;
        using namespace std::chrono_literals;
        using d = NetClock::duration;
        using tp = NetClock::time_point;

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

        Number const principalRequest = broker.asset(loanAmount).value();
        Number const maxCoveredLoanRequest =
            broker.asset(maxCoveredLoanValue).value();
        Number const totalVaultRequest = broker.asset(vaultDeposit).value();
        Number const debtMaximumRequest =
            broker.asset(debtMaximumParameter).value();

        auto const loanSetFee = fee(env.current()->fees().base * 2);

        auto const pseudoAcct = [&]() {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return lender;
            auto const brokerPseudo = brokerSle->at(sfAccount);
            return Account("Broker pseudo-account", brokerPseudo);
        }();

        auto badKeylet = keylet::vault(lender.id(), env.seq(lender));
        // Try some failure cases
        // flags are checked first
        env(set(evan, broker.brokerID, principalRequest, tfLoanSetMask),
            sig(sfCounterpartySignature, lender),
            loanSetFee,
            ter(temINVALID_FLAG));

        // field length validation
        // sfData: good length, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            data(std::string(maxDataPayloadLength, 'X')),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfData: too long
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            data(std::string(maxDataPayloadLength + 1, 'Y')),
            loanSetFee,
            ter(temINVALID));

        // field range validation
        // sfOverpaymentFee: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            overpaymentFee(maxOverpaymentFee),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfOverpaymentFee: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            overpaymentFee(maxOverpaymentFee + 1),
            loanSetFee,
            ter(temINVALID));

        // sfInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            interestRate(maxInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            interestRate(maxInterestRate + 1),
            loanSetFee,
            ter(temINVALID));

        // sfLateInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            lateInterestRate(maxLateInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfLateInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            lateInterestRate(maxLateInterestRate + 1),
            loanSetFee,
            ter(temINVALID));

        // sfCloseInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            closeInterestRate(maxCloseInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfCloseInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            closeInterestRate(maxCloseInterestRate + 1),
            loanSetFee,
            ter(temINVALID));

        // sfOverpaymentInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            overpaymentInterestRate(maxOverpaymentInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfOverpaymentInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            overpaymentInterestRate(maxOverpaymentInterestRate + 1),
            loanSetFee,
            ter(temINVALID));

        // sfPaymentTotal: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            paymentTotal(LoanSet::minPaymentTotal),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfPaymentTotal: too small (there is no max)
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            paymentTotal(LoanSet::minPaymentTotal - 1),
            loanSetFee,
            ter(temINVALID));

        // sfPaymentInterval: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            paymentInterval(LoanSet::minPaymentInterval),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfPaymentInterval: too small (there is no max)
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            paymentInterval(LoanSet::minPaymentInterval - 1),
            loanSetFee,
            ter(temINVALID));

        // sfGracePeriod: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            paymentInterval(LoanSet::minPaymentInterval * 2),
            gracePeriod(LoanSet::minPaymentInterval * 2),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfGracePeriod: larger than paymentInterval
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            paymentInterval(LoanSet::minPaymentInterval * 2),
            gracePeriod(LoanSet::minPaymentInterval * 3),
            loanSetFee,
            ter(temINVALID));

        // insufficient fee - single sign
        env(set(borrower, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            ter(telINSUF_FEE_P));
        // insufficient fee - multisign
        env(set(borrower, broker.brokerID, principalRequest),
            counterparty(lender),
            msig(evan, lender),
            msig(sfCounterpartySignature, evan, borrower),
            fee(env.current()->fees().base * 5 - 1),
            ter(telINSUF_FEE_P));
        // multisign sufficient fee, but no signers set up
        env(set(borrower, broker.brokerID, principalRequest),
            counterparty(lender),
            msig(evan, lender),
            msig(sfCounterpartySignature, evan, borrower),
            fee(env.current()->fees().base * 5),
            ter(tefNOT_MULTI_SIGNING));
        // not the broker owner, no counterparty, not signed by broker
        // owner
        env(set(borrower, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, evan),
            loanSetFee,
            ter(tefBAD_AUTH));
        // not the broker owner, counterparty is borrower
        env(set(evan, broker.brokerID, principalRequest),
            counterparty(borrower),
            sig(sfCounterpartySignature, borrower),
            loanSetFee,
            ter(tecNO_PERMISSION));
        // not a LoanBroker object, no counterparty
        env(set(lender, badKeylet.key, principalRequest),
            sig(sfCounterpartySignature, evan),
            loanSetFee,
            ter(temBAD_SIGNER));
        // not a LoanBroker object, counterparty is valid
        env(set(lender, badKeylet.key, principalRequest),
            counterparty(borrower),
            sig(sfCounterpartySignature, borrower),
            loanSetFee,
            ter(tecNO_ENTRY));
        // borrower doesn't exist
        env(set(lender, broker.brokerID, principalRequest),
            counterparty(alice),
            sig(sfCounterpartySignature, alice),
            loanSetFee,
            ter(terNO_ACCOUNT));

        // Request more funds than the vault has available
        env(set(evan, broker.brokerID, totalVaultRequest + 1),
            sig(sfCounterpartySignature, lender),
            loanSetFee,
            ter(tecINSUFFICIENT_FUNDS));

        // Request more funds than the broker's first-loss capital can
        // cover.
        env(set(evan, broker.brokerID, maxCoveredLoanRequest + 1),
            sig(sfCounterpartySignature, lender),
            loanSetFee,
            ter(tecINSUFFICIENT_FUNDS));

        // Frozen trust line / locked MPT issuance
        // XRP can not be frozen, but run through the loop anyway to test
        // the tecLIMIT_EXCEEDED case
        {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return;

            auto const vaultPseudo = [&]() {
                auto const vaultSle =
                    env.le(keylet::vault(brokerSle->at(sfVaultID)));
                if (!BEAST_EXPECT(vaultSle))
                    // This will be wrong, but the test has failed anyway.
                    return lender;
                auto const vaultPseudo =
                    Account("Vault pseudo-account", vaultSle->at(sfAccount));
                return vaultPseudo;
            }();

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
                        env(trust(issuer, holder[iouCurrency](0), tfSetFreeze));
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
                    return std::make_tuple(freeze, empty, unfreeze, tecLOCKED);
                }
            }();

            // Try freezing the accounts that can't be frozen
            if (freeze)
            {
                for (auto const& account : {vaultPseudo, evan})
                {
                    // Freeze the account
                    freeze(account);

                    // Try to create a loan with a frozen line
                    env(set(evan, broker.brokerID, debtMaximumRequest),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(expectedResult));

                    // Unfreeze the account
                    BEAST_EXPECT(unfreeze);
                    unfreeze(account);

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan, broker.brokerID, debtMaximumRequest + 1),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(tecLIMIT_EXCEEDED));
                }
            }

            // Deep freeze the borrower, which prevents them from receiving
            // funds
            if (deepfreeze)
            {
                // Make sure evan has a trust line that so the issuer can
                // freeze it. (Don't need to do this for the borrower,
                // because LoanSet will create a line to the borrower
                // automatically.)
                env(trust(evan, issuer[iouCurrency](100'000)));

                for (auto const& account :
                     {// these accounts can't be frozen, which deep freeze
                      // implies
                      vaultPseudo,
                      evan,
                      // these accounts can't be deep frozen
                      lender})
                {
                    // Freeze evan
                    deepfreeze(account);

                    // Try to create a loan with a deep frozen line
                    env(set(evan, broker.brokerID, debtMaximumRequest),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(expectedResult));

                    // Unfreeze evan
                    BEAST_EXPECT(unfreeze);
                    unfreeze(account);

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan, broker.brokerID, debtMaximumRequest + 1),
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        ter(tecLIMIT_EXCEEDED));
                }
            }
        }

        // Finally! Create a loan
        std::string testData;

        auto currentState = [&](Keylet const& loanKeylet,
                                VerifyLoanStatus const& verifyLoanStatus) {
            // Lookup the current loan state
            if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                LoanState state{
                    .previousPaymentDate = loan->at(sfPreviousPaymentDate),
                    .startDate = tp{d{loan->at(sfStartDate)}},
                    .nextPaymentDate = loan->at(sfNextPaymentDueDate),
                    .paymentRemaining = loan->at(sfPaymentRemaining),
                    .principalRequested = loan->at(sfPrincipalRequested),
                    .principalOutstanding = loan->at(sfPrincipalOutstanding),
                    .flags = loan->at(sfFlags),
                    .paymentInterval = loan->at(sfPaymentInterval),
                };
                BEAST_EXPECT(state.previousPaymentDate == 0);
                BEAST_EXPECT(
                    tp{d{state.nextPaymentDate}} == state.startDate + 600s);
                BEAST_EXPECT(state.paymentRemaining == 12);
                BEAST_EXPECT(
                    state.principalOutstanding == broker.asset(1000).value());
                BEAST_EXPECT(
                    state.principalOutstanding == state.principalRequested);
                BEAST_EXPECT(state.paymentInterval == 600);

                verifyLoanStatus(state);

                return state;
            }

            return LoanState{
                .previousPaymentDate = 0,
                .startDate = tp{d{0}},
                .nextPaymentDate = 0,
                .paymentRemaining = 0,
                .principalRequested = 0,
                .principalOutstanding = 0,
                .flags = 0,
                .paymentInterval = 0,
            };
        };

        auto getInterestRate = [&env, this](Keylet const& loanKeylet) {
            if (auto const loan = env.le(loanKeylet); BEAST_EXPECT(loan))
            {
                return TenthBips32{loan->at(sfInterestRate)};
            }
            return TenthBips32{0};
        };
        auto coverAvailable =
            [&env, this](uint256 const& brokerID, Number const& expected) {
                if (auto const brokerSle = env.le(keylet::loanbroker(brokerID));
                    BEAST_EXPECT(brokerSle))
                {
                    auto const available = brokerSle->at(sfCoverAvailable);
                    BEAST_EXPECT(available == expected);
                    return available;
                }
                return Number{};
            };
        auto getDefaultInfo = [&env, this](
                                  LoanState const& state,
                                  BrokerInfo const& broker,
                                  TenthBips32 const interestRate) {
            if (auto const brokerSle =
                    env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(
                    state.principalRequested == state.principalOutstanding);
                auto const interestOutstanding =
                    loanInterestOutstandingMinusFee(
                        broker.asset,
                        state.principalRequested,
                        state.principalOutstanding,
                        interestRate,
                        state.paymentInterval,
                        state.paymentRemaining,
                        TenthBips32{brokerSle->at(sfManagementFeeRate)});
                auto const defaultAmount = roundToAsset(
                    broker.asset,
                    std::min(
                        tenthBipsOfValue(
                            tenthBipsOfValue(
                                brokerSle->at(sfDebtTotal),
                                coverRateMinParameter),
                            coverRateLiquidationParameter),
                        state.principalOutstanding + interestOutstanding),
                    state.principalRequested);
                return std::make_pair(defaultAmount, brokerSle->at(sfOwner));
            }
            return std::make_pair(Number{}, AccountID{});
        };
        auto replenishCover = [&env, &coverAvailable, this](
                                  BrokerInfo const& broker,
                                  AccountID const& brokerAcct,
                                  Number const& startingCoverAvailable,
                                  Number const& amountToBeCovered) {
            coverAvailable(
                broker.brokerID, startingCoverAvailable - amountToBeCovered);
            env(loanBroker::coverDeposit(
                brokerAcct,
                broker.brokerID,
                STAmount{broker.asset, amountToBeCovered}));
            coverAvailable(broker.brokerID, startingCoverAvailable);
        };

        auto defaultImmediately = [&](std::uint32_t baseFlag,
                                      bool impair = true) {
            return [&, impair, baseFlag](
                       Keylet const& loanKeylet,
                       VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                // Default the loan

                // Initialize values with the current state
                auto state = currentState(loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == baseFlag);

                auto const interestRate = getInterestRate(loanKeylet);

                auto const& broker = verifyLoanStatus.broker;
                auto const startingCoverAvailable = coverAvailable(
                    broker.brokerID,
                    broker.asset(coverDepositParameter).number());

                if (impair)
                {
                    // Impair the loan
                    env(manage(lender, loanKeylet.key, tfLoanImpair));

                    state.flags |= tfLoanImpair;
                    state.nextPaymentDate =
                        env.now().time_since_epoch().count();
                    verifyLoanStatus(state);

                    // Once the loan is impaired, it can't be impaired again
                    env(manage(lender, loanKeylet.key, tfLoanImpair),
                        ter(tecNO_PERMISSION));
                }

                auto const nextDueDate = tp{d{state.nextPaymentDate}};

                // Can't default the loan yet. The grace period hasn't
                // expired
                env(manage(lender, loanKeylet.key, tfLoanDefault),
                    ter(tecTOO_SOON));

                // Let some time pass so that the loan can be
                // defaulted
                env.close(nextDueDate + 60s);

#if LOANDRAW && 0
                if (impair)
                {
                    // Impaired loans can't be drawn against
                    env(draw(borrower, loanKeylet.key, broker.asset(100)),
                        ter(tecNO_PERMISSION));
                }
#endif

                auto const [amountToBeCovered, brokerAcct] =
                    getDefaultInfo(state, broker, interestRate);

                // Default the loan
                env(manage(lender, loanKeylet.key, tfLoanDefault));

                // The LoanBroker just lost some of it's first-loss capital.
                // Replenish it.
                replenishCover(
                    broker,
                    brokerAcct,
                    startingCoverAvailable,
                    amountToBeCovered);

                state.flags |= tfLoanDefault;
                state.paymentRemaining = 0;
                state.principalOutstanding = 0;
                verifyLoanStatus(state);

#if LOANDRAW && 0
                // Defaulted loans can't be drawn against, either
                env(draw(borrower, loanKeylet.key, broker.asset(100)),
                    ter(tecNO_PERMISSION));
#endif

                // Once a loan is defaulted, it can't be managed
                env(manage(lender, loanKeylet.key, tfLoanUnimpair),
                    ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanImpair),
                    ter(tecNO_PERMISSION));
            };
        };

        auto immediatePayoff = [&](std::uint32_t baseFlag) {
            return [&, baseFlag](
                       Keylet const& loanKeylet,
                       VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                auto state = currentState(loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == baseFlag);
#if LOANDRAW && 0
                auto const borrowerStartingBalance =
                    env.balance(borrower, broker.asset);

                // Try to make a payment before the loan starts
                env(pay(borrower, loanKeylet.key, broker.asset(500)),
                    ter(tecTOO_SOON));

                // Advance to the start date of the loan
                env.close(state.startDate + 5s);

                verifyLoanStatus(state);

                // Need to account for fees if the loan is in XRP
                PrettyAmount adjustment = broker.asset(0);
                if (broker.asset.raw().native())
                {
                    adjustment = 2 * env.current()->fees().base;
                }

                // Draw the entire available balance
                // Need to create the STAmount directly to avoid
                // PrettyAsset scaling.
                STAmount const drawAmount{broker.asset, state.assetsAvailable};
                env(draw(borrower, loanKeylet.key, drawAmount));
#else
                STAmount const drawAmount =
                    STAmount(broker.asset, state.principalRequested - 1);
#endif
                env.close(state.startDate + 20s);
                auto const loanAge = (env.now() - state.startDate).count();
                BEAST_EXPECT(loanAge == 30);

                verifyLoanStatus(state);
#if LOANDRAW && 0
                BEAST_EXPECT(
                    env.balance(borrower, broker.asset) ==
                    borrowerStartingBalance + drawAmount - adjustment);
#endif

                // Send some bogus pay transactions
                env(pay(borrower,
                        keylet::loan(uint256(0)).key,
                        broker.asset(10)),
                    ter(temINVALID));
                env(pay(borrower, loanKeylet.key, broker.asset(-100)),
                    ter(temBAD_AMOUNT));
                env(pay(borrower, broker.brokerID, broker.asset(100)),
                    ter(tecNO_ENTRY));
                env(pay(evan, loanKeylet.key, broker.asset(500)),
                    ter(tecNO_PERMISSION));

                {
                    auto const otherAsset =
                        broker.asset.raw() == assets[0].raw() ? assets[1]
                                                              : assets[0];
                    env(pay(borrower, loanKeylet.key, otherAsset(100)),
                        ter(tecWRONG_ASSET));
                }

                // Amount doesn't cover a single payment
                env(pay(borrower, loanKeylet.key, STAmount{broker.asset, 1}),
                    ter(tecINSUFFICIENT_PAYMENT));

                // Get the balance after these failed transactions take
                // fees
                auto const borrowerBalanceBeforePayment =
                    env.balance(borrower, broker.asset);

                // Full payoff amount will consist of
                // 1. principal outstanding (1000)
                // 2. accrued interest (at 12%)
                // 3. prepayment penalty (closeInterest at 3.6%)
                // 4. close payment fee (4)
                // Calculate these values without the helper functions
                // to verify they're working correctly The numbers in
                // the below BEAST_EXPECTs may not hold across assets.
                Number const interval = state.paymentInterval;
                auto const periodicRate =
                    interval * Number(12, -2) / (365 * 24 * 60 * 60);
                BEAST_EXPECT(
                    periodicRate ==
                    Number(2283105022831050, -21, Number::unchecked{}));
                STAmount const accruedInterest{
                    broker.asset,
                    state.principalOutstanding * periodicRate * loanAge /
                        interval};
                BEAST_EXPECT(
                    accruedInterest ==
                    broker.asset(Number(1141552511415525, -19)));
                STAmount const prepaymentPenalty{
                    broker.asset, state.principalOutstanding * Number(36, -3)};
                BEAST_EXPECT(prepaymentPenalty == broker.asset(36));
                STAmount const closePaymentFee = broker.asset(4);
                auto const payoffAmount =
                    STAmount{broker.asset, state.principalOutstanding} +
                    accruedInterest + prepaymentPenalty + closePaymentFee;
                BEAST_EXPECT(
                    payoffAmount ==
                    broker.asset(Number(1040000114155251, -12)));
                BEAST_EXPECT(payoffAmount > drawAmount);
                // Try to pay a little extra to show that it's _not_
                // taken
                auto const transactionAmount = payoffAmount + broker.asset(10);
                BEAST_EXPECT(
                    transactionAmount ==
                    broker.asset(Number(1050000114155251, -12)));
                env(pay(borrower, loanKeylet.key, transactionAmount));

                env.close();

                // Need to account for fees if the loan is in XRP
                PrettyAmount adjustment = broker.asset(0);
                if (broker.asset.raw().native())
                {
                    adjustment = env.current()->fees().base;
                }

                state.paymentRemaining = 0;
                state.principalOutstanding = 0;
                verifyLoanStatus(state);

                BEAST_EXPECT(
                    env.balance(borrower, broker.asset) ==
                    borrowerBalanceBeforePayment - payoffAmount - adjustment);

                // Can't impair or default a paid off loan
                env(manage(lender, loanKeylet.key, tfLoanImpair),
                    ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanDefault),
                    ter(tecNO_PERMISSION));
            };
        };

        // There are a lot of fields that can be set on a loan, but most
        // of them only affect the "math" when a payment is made. The
        // only one that really affects behavior is the
        // `tfLoanOverpayment` flag.
        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Impair and Default",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            tfLoanOverpayment,
            defaultImmediately(lsfLoanOverpayment));

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Impair and Default",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            0,
            defaultImmediately(0));

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Default without Impair",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            tfLoanOverpayment,
            defaultImmediately(lsfLoanOverpayment, false));

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Default without Impair",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            0,
            defaultImmediately(0, false));

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Draw then default",
            env,
            loanAmount,
            interestExponent,
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

                auto const interestRate = getInterestRate(loanKeylet);

                auto const& broker = verifyLoanStatus.broker;
                auto const startingCoverAvailable = coverAvailable(
                    broker.brokerID,
                    broker.asset(coverDepositParameter).number());

#if LOANDRAW && 0
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
#endif

                // move past the due date + grace period (60s)
                env.close(tp{d{state.nextPaymentDate}} + 60s + 20s);
#if LOANDRAW && 0
                // Try to draw
                env(draw(borrower, loanKeylet.key, broker.asset(100)),
                    ter(tecNO_PERMISSION));
#endif

                auto const [amountToBeCovered, brokerAcct] =
                    getDefaultInfo(state, broker, interestRate);

                // default the loan
                env(manage(lender, loanKeylet.key, tfLoanDefault));

                // The LoanBroker just lost some of it's first-loss capital.
                // Replenish it.
                replenishCover(
                    broker,
                    brokerAcct,
                    startingCoverAvailable,
                    amountToBeCovered);

                state.paymentRemaining = 0;
                state.principalOutstanding = 0;
                state.flags |= tfLoanDefault;

                verifyLoanStatus(state);

#if LOANDRAW && 0
                // Same error, different check
                env(draw(borrower, loanKeylet.key, broker.asset(100)),
                    ter(tecNO_PERMISSION));
#endif

                // Can't make a payment on it either
                env(pay(borrower, loanKeylet.key, broker.asset(300)),
                    ter(tecKILLED));

                // Default
            });

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Pay off immediately",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            0,
            immediatePayoff(0));

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Pay off immediately",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            tfLoanOverpayment,
            immediatePayoff(lsfLoanOverpayment));

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Make payments",
            env,
            loanAmount,
            interestExponent,
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
                // Draw and make multiple payments
                auto state = currentState(loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == 0);
                env.close();

                verifyLoanStatus(state);

#if LOANDRAW && 0
                auto const borrowerStartingBalance =
                    env.balance(borrower, broker.asset);

                // Need to account for fees if the loan is in XRP
                PrettyAmount adjustment = broker.asset(0);
                if (broker.asset.raw().native())
                {
                    adjustment = env.current()->fees().base;
                }

                // Draw the entire available balance
                // Need to create the STAmount directly to avoid
                // PrettyAsset scaling.
                STAmount const drawAmount{broker.asset, state.assetsAvailable};
                env(draw(borrower, loanKeylet.key, drawAmount));
#endif
                env.close(state.startDate + 20s);
                auto const loanAge = (env.now() - state.startDate).count();
                BEAST_EXPECT(loanAge == 30);

#if LOANDRAW && 0
                verifyLoanStatus(state);
                BEAST_EXPECT(
                    env.balance(borrower, broker.asset) ==
                    borrowerStartingBalance + drawAmount - adjustment);
#endif

                // Periodic payment amount will consist of
                // 1. principal outstanding (1000)
                // 2. interest interest rate (at 12%)
                // 3. payment interval (600s)
                // 4. loan service fee (2)
                // Calculate these values without the helper functions
                // to verify they're working correctly The numbers in
                // the below BEAST_EXPECTs may not hold across assets.
                Number const interval = state.paymentInterval;
                auto const periodicRate =
                    interval * Number(12, -2) / (365 * 24 * 60 * 60);
                BEAST_EXPECT(
                    periodicRate ==
                    Number(2283105022831050, -21, Number::unchecked{}));

                while (state.paymentRemaining > 0)
                {
                    testcase << "Payments remaining: "
                             << state.paymentRemaining;

                    STAmount const principalRequestedAmount{
                        broker.asset, state.principalRequested};
                    // Compute the payment based on the number of
                    // payments remaining
                    auto const rateFactor =
                        power(1 + periodicRate, state.paymentRemaining);
                    Number const rawPeriodicPayment =
                        state.principalOutstanding * periodicRate * rateFactor /
                        (rateFactor - 1);
                    STAmount const periodicPayment = roundToReference(
                        STAmount{broker.asset, rawPeriodicPayment},
                        principalRequestedAmount);
                    // Only check the first payment since the rounding
                    // may drift as payments are made
                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        STAmount(broker.asset, rawPeriodicPayment) ==
                            broker.asset(Number(8333457001162141, -14)));
                    // Include the service fee
                    STAmount const totalDue = roundToReference(
                        periodicPayment + broker.asset(2),
                        principalRequestedAmount);
                    // Only check the first payment since the rounding
                    // may drift as payments are made
                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        totalDue ==
                            roundToReference(
                                broker.asset(Number(8533457001162141, -14)),
                                principalRequestedAmount));

                    // Try to pay a little extra to show that it's _not_
                    // taken
                    STAmount const transactionAmount =
                        STAmount{broker.asset, totalDue} + broker.asset(10);
                    // Only check the first payment since the rounding
                    // may drift as payments are made
                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        transactionAmount ==
                            roundToReference(
                                broker.asset(Number(9533457001162141, -14)),
                                principalRequestedAmount));

                    auto const totalDueAmount =
                        STAmount{broker.asset, totalDue};

                    // Compute the expected principal amount
                    Number const rawInterest = state.paymentRemaining == 1
                        ? rawPeriodicPayment - state.principalOutstanding
                        : state.principalOutstanding * periodicRate;
                    STAmount const interest = roundToReference(
                        STAmount{broker.asset, rawInterest},
                        principalRequestedAmount);
                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToReference(
                            STAmount{broker.asset, rawInterest},
                            principalRequestedAmount) ==
                            roundToReference(
                                broker.asset(Number(2283105022831050, -18)),
                                principalRequestedAmount));
                    BEAST_EXPECT(interest >= Number(0));

                    auto const rawPrincipal = rawPeriodicPayment - rawInterest;
                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToReference(
                            STAmount{broker.asset, rawPrincipal},
                            principalRequestedAmount) ==
                            roundToReference(
                                broker.asset(Number(8333228690659858, -14)),
                                principalRequestedAmount));
                    BEAST_EXPECT(
                        state.paymentRemaining > 1 ||
                        rawPrincipal == state.principalOutstanding);
                    auto const principal = roundToReference(
                        STAmount{broker.asset, periodicPayment - interest},
                        principalRequestedAmount);
                    BEAST_EXPECT(
                        principal > Number(0) &&
                        principal <= state.principalOutstanding);
                    BEAST_EXPECT(
                        state.paymentRemaining > 1 ||
                        principal == state.principalOutstanding);
                    BEAST_EXPECT(
                        rawPrincipal + rawInterest == rawPeriodicPayment);
                    BEAST_EXPECT(principal + interest == periodicPayment);

                    auto const borrowerBalanceBeforePayment =
                        env.balance(borrower, broker.asset);

                    // Make the payment
                    env(pay(borrower, loanKeylet.key, transactionAmount));

                    env.close();

                    // Need to account for fees if the loan is in XRP
                    PrettyAmount adjustment = broker.asset(0);
                    if (broker.asset.raw().native())
                    {
                        adjustment = env.current()->fees().base;
                    }

                    // Check the result
                    auto const borrowerBalance =
                        env.balance(borrower, broker.asset);
                    auto const expectedBalance = borrowerBalanceBeforePayment -
                        totalDueAmount - adjustment;
                    BEAST_EXPECT(
                        borrowerBalance == expectedBalance ||
                        (!broker.asset.raw().native() &&
                         broker.asset.raw().holds<Issue>() &&
                         ((borrowerBalance - expectedBalance) /
                              expectedBalance <
                          Number(1, -4))));

                    --state.paymentRemaining;
                    state.previousPaymentDate = state.nextPaymentDate;
                    state.nextPaymentDate += state.paymentInterval;
                    state.principalOutstanding -= principal;

                    verifyLoanStatus(state);
                }

                // Loan is paid off
                BEAST_EXPECT(state.paymentRemaining == 0);
                BEAST_EXPECT(state.principalOutstanding == 0);

                // Can't impair or default a paid off loan
                env(manage(lender, loanKeylet.key, tfLoanImpair),
                    ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanDefault),
                    ter(tecNO_PERMISSION));
            });
    }

    void
    testLifecycle()
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
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

        // Fund the accounts and trust lines with the same amount so that
        // tests can use the same values regardless of the asset.
        env.fund(XRP(100'000'000), issuer, noripple(lender, borrower, evan));
        env.close();

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        PrettyAsset const iouAsset = issuer[iouCurrency];
        env(trust(lender, iouAsset(10'000'000)));
        env(trust(borrower, iouAsset(10'000'000)));
        env(trust(evan, iouAsset(10'000'000)));
        env(pay(issuer, evan, iouAsset(1'000'000)));
        env(pay(issuer, lender, iouAsset(10'000'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, iouAsset(10'000)));
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});
        mptt.authorize({.account = evan});
        env(pay(issuer, lender, mptAsset(10'000'000)));
        env(pay(issuer, evan, mptAsset(1'000'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, mptAsset(10'000)));
        env.close();

        std::array const assets{xrpAsset, mptAsset, iouAsset};

        // Create vaults and loan brokers
        std::vector<BrokerInfo> brokers;
        for (auto const& asset : assets)
        {
            brokers.emplace_back(createVaultAndBroker(env, asset, lender));
        }

        // Create and update Loans
        for (auto const& broker : brokers)
        {
            for (int amountExponent = 3; amountExponent >= 3; --amountExponent)
            {
                Number const loanAmount{1, amountExponent};
                for (int interestExponent = 0; interestExponent >= 0;
                     --interestExponent)
                {
                    testCaseWrapper(
                        env,
                        mptt,
                        assets,
                        broker,
                        loanAmount,
                        interestExponent);
                }
            }

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

    void
    testSelfLoan()
    {
        testcase << "Self Loan";

        using namespace jtx;
        using namespace std::chrono_literals;
        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
        Env env(*this, all);

        Account const issuer{"issuer"};
        // For simplicity, lender will be the sole actor for the vault &
        // brokers.
        Account const lender{"lender"};

        // Fund the accounts and trust lines with the same amount so that
        // tests can use the same values regardless of the asset.
        env.fund(XRP(100'000'000), issuer, noripple(lender));
        env.close();

        // Use an XRP asset for simplicity
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        // Create vaults and loan brokers
        BrokerInfo broker{createVaultAndBroker(env, xrpAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        // The LoanSet json can be created without a counterparty signature,
        // but it will not pass preflight
        auto createJson = env.json(
            set(lender,
                broker.brokerID,
                broker.asset(principalRequest).value()),
            fee(loanSetFee));
        env(createJson, ter(temBAD_SIGNER));

        // Adding an empty counterparty signature object also fails, but
        // at the RPC level.
        createJson = env.json(
            createJson, json(sfCounterpartySignature, Json::objectValue));
        env(createJson, ter(telENV_RPC_FAILED));

        if (auto const jt = env.jt(createJson); BEAST_EXPECT(jt.stx))
        {
            Serializer s;
            jt.stx->add(s);
            auto const jr = env.rpc("submit", strHex(s.slice()));

            BEAST_EXPECT(jr.isMember(jss::result));
            auto const jResult = jr[jss::result];
            BEAST_EXPECT(jResult[jss::error] == "invalidTransaction");
            BEAST_EXPECT(
                jResult[jss::error_exception] ==
                "fails local checks: Transaction has bad signature.");
        }

        // Copy the transaction signature into the counterparty signature.
        Json::Value counterpartyJson{Json::objectValue};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        // The duplicated signature works
        createJson = env.json(
            createJson, json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();

        auto const startDate = env.current()->info().parentCloseTime;

        // Loan is successfully created
        {
            auto const res = env.rpc("account_objects", lender.human());
            auto const objects = res[jss::result][jss::account_objects];

            std::map<std::string, std::size_t> types;
            BEAST_EXPECT(objects.size() == 4);
            for (auto const& object : objects)
            {
                ++types[object[sfLedgerEntryType].asString()];
            }
            BEAST_EXPECT(types.size() == 4);
            for (std::string const type :
                 {"MPToken", "Vault", "LoanBroker", "Loan"})
            {
                BEAST_EXPECT(types[type] == 1);
            }
        }
        auto const loanID = [&]() {
            Json::Value params(Json::objectValue);
            params[jss::account] = lender.human();
            params[jss::type] = "Loan";
            auto const res =
                env.rpc("json", "account_objects", to_string(params));
            auto const objects = res[jss::result][jss::account_objects];

            BEAST_EXPECT(objects.size() == 1);

            auto const loan = objects[0u];
            BEAST_EXPECT(loan[sfBorrower] == lender.human());
            BEAST_EXPECT(loan[sfCloseInterestRate] == 0);
            BEAST_EXPECT(loan[sfClosePaymentFee] == "0");
            BEAST_EXPECT(loan[sfFlags] == 0);
            BEAST_EXPECT(loan[sfGracePeriod] == 60);
            BEAST_EXPECT(loan[sfInterestRate] == 0);
            BEAST_EXPECT(loan[sfLateInterestRate] == 0);
            BEAST_EXPECT(loan[sfLatePaymentFee] == "0");
            BEAST_EXPECT(loan[sfLoanBrokerID] == to_string(broker.brokerID));
            BEAST_EXPECT(loan[sfLoanOriginationFee] == "0");
            BEAST_EXPECT(loan[sfLoanSequence] == 1);
            BEAST_EXPECT(loan[sfLoanServiceFee] == "0");
            BEAST_EXPECT(
                loan[sfNextPaymentDueDate] == loan[sfStartDate].asUInt() + 60);
            BEAST_EXPECT(loan[sfOverpaymentFee] == 0);
            BEAST_EXPECT(loan[sfOverpaymentInterestRate] == 0);
            BEAST_EXPECT(loan[sfPaymentInterval] == 60);
            BEAST_EXPECT(loan[sfPaymentRemaining] == 1);
            BEAST_EXPECT(loan[sfPreviousPaymentDate] == 0);
            BEAST_EXPECT(loan[sfPrincipalOutstanding] == "1000000000");
            BEAST_EXPECT(loan[sfPrincipalRequested] == "1000000000");
            BEAST_EXPECT(
                loan[sfStartDate].asUInt() ==
                startDate.time_since_epoch().count());

            return loan["index"].asString();
        }();
        auto const loanKeylet{keylet::loan(uint256{std::string_view(loanID)})};

        env.close(startDate);

#if LOANDRAW && 0
        // Draw the loan
        env(draw(lender, loanKeylet.key, broker.asset(1000)));
        env.close();
#endif
        // Make a payment
        env(pay(lender, loanKeylet.key, broker.asset(1000)));
    }

    void
    testBatchBypassCounterparty()
    {
        testcase << "Batch Bypass Counterparty";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo broker{createVaultAndBroker(env, xrpAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto forgedLoanSet =
            set(borrower, broker.brokerID, principalRequest, 0);

        Json::Value randomData{Json::objectValue};
        randomData[jss::SigningPubKey] = Json::StaticString{"2600"};
        Json::Value sigObject{Json::objectValue};
        sigObject[jss::SigningPubKey] = strHex(lender.pk().slice());
        Serializer ss;
        ss.add32(HashPrefix::txSign);
        parse(randomData).addWithoutSigningFields(ss);
        auto const sig = ripple::sign(borrower.pk(), borrower.sk(), ss.slice());
        sigObject[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});

        forgedLoanSet[Json::StaticString{"CounterpartySignature"}] = sigObject;

        // ? Fails because the lender hasn't signed the tx
        env(env.json(forgedLoanSet, fee(loanSetFee)), ter(telENV_RPC_FAILED));

        auto const seq = env.seq(borrower);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        // ! Should fail because the lender hasn't signed the tx
        env(batch::outer(borrower, seq, batchFee, tfAllOrNothing),
            batch::inner(forgedLoanSet, seq + 1),
            batch::inner(pay(borrower, lender, XRP(1)), seq + 2),
            ter(temBAD_SIGNATURE));
        env.close();

        // ? Check that the loan was created
        {
            Json::Value params(Json::objectValue);
            params[jss::account] = borrower.human();
            params[jss::type] = "Loan";
            auto const res =
                env.rpc("json", "account_objects", to_string(params));
            auto const objects = res[jss::result][jss::account_objects];
            BEAST_EXPECT(objects.size() == 0);
        }
    }

    BrokerInfo
    createVaultAndBrokerNoMaxDebt(
        jtx::Env& env,
        jtx::PrettyAsset const& asset,
        jtx::Account const& lender)
    {
        return createVaultAndBroker(env, asset, lender, Number(0));
    }

    void
    testWrongMaxDebtBehavior()
    {
        testcase << "Wrong Max Debt Behavior";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        env.fund(XRP(100'000), issuer, noripple(lender));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo broker{createVaultAndBrokerNoMaxDebt(env, xrpAsset, lender)};

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfDebtMaximum) == 0);
        }

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(lender, broker.brokerID, principalRequest), fee(loanSetFee));

        Json::Value counterpartyJson{Json::objectValue};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        createJson = env.json(
            createJson, json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();
    }

    void
    testLoanPayComputePeriodicPaymentValidRateInvariant()
    {
        testcase << "LoanPay ripple::detail::computePeriodicPayment : "
                    "valid rate";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerInfo broker{createVaultAndBroker(env, xrpAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{640562, -5};

        Number const serviceFee{2462611968};
        std::uint32_t const numPayments{4294967295};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            loanServiceFee(serviceFee),
            paymentTotal(numPayments),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["CloseInterestRate"] = 55374;
        createJson["ClosePaymentFee"] = "3825205248";
        createJson["GracePeriod"] = 0;
        createJson["LatePaymentFee"] = "237";
        createJson["LoanOriginationFee"] = "0";
        createJson["OverpaymentFee"] = 35167;
        createJson["OverpaymentInterestRate"] = 1360;
        createJson["PaymentInterval"] = 727;

        Number const actualPrincipal{6};

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tesSUCCESS));
        env.close();

        if (auto const loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            // Verify the payment decreased the principal
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == numPayments);
            BEAST_EXPECT(loan->at(sfPrincipalRequested) == actualPrincipal);
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == actualPrincipal);
        }

#if LOANDRAW && 0
        auto loanDrawTx =
            env.json(draw(borrower, keylet.key, STAmount{broker.asset, Number {
                                                             6
                                                         }}));
        env(loanDrawTx, ter(tesSUCCESS));
        env.close();

        if (auto const loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            // Verify the payment decreased the principal
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == numPayments);
            BEAST_EXPECT(loan->at(sfPrincipalRequested) == actualPrincipal);
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == actualPrincipal);
        }
#endif

        auto loanPayTx = env.json(
            pay(borrower, keylet.key, STAmount{broker.asset, serviceFee + 6}));
        env(loanPayTx, ter(tesSUCCESS));
        env.close();

        if (auto const loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            // Verify the payment decreased the principal
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == numPayments - 1);
            BEAST_EXPECT(loan->at(sfPrincipalRequested) == actualPrincipal);
            BEAST_EXPECT(
                loan->at(sfPrincipalOutstanding) == actualPrincipal - 1);
        }
    }

    void
    testRPC()
    {
        // This will expand as more test cases are added. Some functionality
        // is tested in other test functions.
        testcase("RPC");

        using namespace jtx;

        Env env(*this, all);

        Account const alice{"alice"};
        std::string const borrowerPass = "borrower";
        std::string const borrowerSeed = "ssBRAsLpH4778sLNYC4ik1JBJsBVf";
        Account borrower{borrowerPass, KeyType::ed25519};
        auto const lenderPass = "lender";
        std::string const lenderSeed = "shPTCZGwTEhJrYT8NbcNkeaa8pzPM";
        Account lender{lenderPass, KeyType::ed25519};

        env.fund(XRP(1'000'000), alice, lender, borrower);
        env.close();
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env(noop(lender));
        env.close();

        {
            testcase("RPC AccountSet");
            Json::Value txJson{Json::objectValue};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const signParams = [&]() {
                Json::Value signParams{Json::objectValue};
                signParams[jss::passphrase] = borrowerPass;
                signParams[jss::key_type] = "ed25519";
                signParams[jss::tx_json] = txJson;
                return signParams;
            }();
            auto const jSign = env.rpc("json", "sign", to_string(signParams));
            BEAST_EXPECT(
                jSign.isMember(jss::result) &&
                jSign[jss::result].isMember(jss::tx_json));
            auto txSignResult = jSign[jss::result][jss::tx_json];
            auto txSignBlob = jSign[jss::result][jss::tx_blob].asString();
            txSignResult.removeMember(jss::hash);

            auto const jtx = env.jt(txJson, sig(borrower));
            BEAST_EXPECT(txSignResult == jtx.jv);

            auto const jSubmit = env.rpc("submit", txSignBlob);
            BEAST_EXPECT(
                jSubmit.isMember(jss::result) &&
                jSubmit[jss::result].isMember(jss::engine_result) &&
                jSubmit[jss::result][jss::engine_result].asString() ==
                    "tesSUCCESS");

            env(jtx.jv, sig(none), seq(none), fee(none), ter(tefPAST_SEQ));
        }

        {
            testcase("RPC LoanSet - illegal signature_target");

            Json::Value txJson{Json::objectValue};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const borrowerSignParams = [&]() {
                Json::Value params{Json::objectValue};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "Destination";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower =
                env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::error) &&
                jSignBorrower[jss::result][jss::error] == "invalidParams" &&
                jSignBorrower[jss::result].isMember(jss::error_message) &&
                jSignBorrower[jss::result][jss::error_message] ==
                    "Destination");
        }
        {
            testcase("RPC LoanSet - sign and submit borrower initiated");
            // 1. Borrower creates the transaction
            Json::Value txJson{Json::objectValue};
            txJson[sfTransactionType] = "LoanSet";
            txJson[sfAccount] = borrower.human();
            txJson[sfCounterparty] = lender.human();
            txJson[sfLoanBrokerID] =
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FECF83F"
                "5C";
            txJson[sfPrincipalRequested] = "100000000";
            txJson[sfStartDate] = 807730340;
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = "24";

            // 2. Borrower signs the transaction
            auto const borrowerSignParams = [&]() {
                Json::Value params{Json::objectValue};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower =
                env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::tx_json));
            auto const txBorrowerSignResult =
                jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob =
                jSignBorrower[jss::result][jss::tx_blob].asString();

            // 2a. Borrower attempts to submit the transaction. It doesn't
            // work
            {
                auto const jSubmitBlob = env.rpc("submit", txBorrowerSignBlob);
                BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
                auto const jSubmitBlobResult = jSubmitBlob[jss::result];
                BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
                // Transaction fails because the CounterpartySignature is
                // missing
                BEAST_EXPECT(
                    jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        "temBAD_SIGNER");
            }

            // 3. Borrower sends the signed transaction to the lender
            // 4. Lender signs the transaction
            auto const lenderSignParams = [&]() {
                Json::Value params{Json::objectValue};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txBorrowerSignResult;
                return params;
            }();
            auto const jSignLender =
                env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult =
                jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob =
                jSignLender[jss::result][jss::tx_blob].asString();

            // 5. Lender submits the signed transaction blob
            auto const jSubmitBlob = env.rpc("submit", txLenderSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECT(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                jSubmitBlobResult[jss::engine_result].asString() ==
                    "tecNO_ENTRY");

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) &&
                !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Lender submits the transaction json originally
            // received from the Borrower. It gets signed, but is now a
            // duplicate, so fails. Borrower could done this instead of
            // steps 4 and 5.
            auto const jSubmitJson =
                env.rpc("json", "submit", to_string(lenderSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECT(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                jSubmitJsonResult[jss::engine_result].asString() ==
                    "tefPAST_SEQ");

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) &&
                !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }

        {
            testcase("RPC LoanSet - sign and submit lender initiated");
            // 1. Lender creates the transaction
            Json::Value txJson{Json::objectValue};
            txJson[sfTransactionType] = "LoanSet";
            txJson[sfAccount] = lender.human();
            txJson[sfCounterparty] = borrower.human();
            txJson[sfLoanBrokerID] =
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FECF83F"
                "5C";
            txJson[sfPrincipalRequested] = "100000000";
            txJson[sfStartDate] = 807730340;
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = "24";

            // 2. Lender signs the transaction
            auto const lenderSignParams = [&]() {
                Json::Value params{Json::objectValue};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignLender =
                env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult =
                jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob =
                jSignLender[jss::result][jss::tx_blob].asString();

            // 2a. Lender attempts to submit the transaction. It doesn't
            // work
            {
                auto const jSubmitBlob = env.rpc("submit", txLenderSignBlob);
                BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
                auto const jSubmitBlobResult = jSubmitBlob[jss::result];
                BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
                // Transaction fails because the CounterpartySignature is
                // missing
                BEAST_EXPECT(
                    jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        "temBAD_SIGNER");
            }

            // 3. Lender sends the signed transaction to the Borrower
            // 4. Borrower signs the transaction
            auto const borrowerSignParams = [&]() {
                Json::Value params{Json::objectValue};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txLenderSignResult;
                return params;
            }();
            auto const jSignBorrower =
                env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::tx_json));
            auto const txBorrowerSignResult =
                jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob =
                jSignBorrower[jss::result][jss::tx_blob].asString();

            // 5. Borrower submits the signed transaction blob
            auto const jSubmitBlob = env.rpc("submit", txBorrowerSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECT(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                jSubmitBlobResult[jss::engine_result].asString() ==
                    "tecNO_ENTRY");

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) &&
                !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Borrower submits the transaction json originally
            // received from the Lender. It gets signed, but is now a
            // duplicate, so fails. Lender could done this instead of steps
            // 4 and 5.
            auto const jSubmitJson =
                env.rpc("json", "submit", to_string(borrowerSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECT(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                jSubmitJsonResult[jss::engine_result].asString() ==
                    "tefPAST_SEQ");

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) &&
                !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }
    }

    void
    testBasicMath()
    {
        // Test the functions defined in LendingHelpers.h
        testcase("Basic Math");

        pass();
    }

public:
    void
    run() override
    {
        testDisabled();
        testSelfLoan();
        testLifecycle();
        testBatchBypassCounterparty();
        testWrongMaxDebtBehavior();
        testLoanPayComputePeriodicPaymentValidRateInvariant();

        testRPC();
        testBasicMath();
    }
};

BEAST_DEFINE_TESTSUITE(Loan, tx, ripple);

}  // namespace test
}  // namespace ripple
