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

#include <test/jtx.h>

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/tx/detail/LoanSet.h>

#include <xrpl/beast/unit_test/suite.h>

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
    static constexpr TenthBips16 const managementFeeRateParameter{100};
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
        auto failAll = [this](FeatureBitset features) {
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
            // 4. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), ter(temDISABLED));
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol);
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
        std::int32_t const loanScale = 0;
        Number totalValue = 0;
        Number principalOutstanding = 0;
        Number managementFeeOutstanding = 0;
        Number periodicPayment = 0;
        std::uint32_t flags = 0;
        std::uint32_t const paymentInterval = 0;
        TenthBips32 const interestRate{};
    };

    /** Helper class to compare the expected state of a loan and loan broker
     * against the data in the ledger.
     */
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

        /** Checks the expected broker state against the ledger
         */
        void
        checkBroker(
            Number const& principalOutstanding,
            Number const& interestOwed,
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
                auto const brokerDebt = brokerSle->at(sfDebtTotal);
                auto const expectedDebt = principalOutstanding + interestOwed;
                env.test.BEAST_EXPECT(brokerDebt == expectedDebt);
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

                        // TODO: This needs to be an exact match once all the
                        // other rounding issues are worked out.
                        auto const total = vaultSle->at(sfAssetsTotal);
                        auto const available = vaultSle->at(sfAssetsAvailable);
                        env.test.BEAST_EXPECT(
                            total == available ||
                            (!broker.asset.integral() && available != 0 &&
                             ((total - available) / available <
                              Number(1, -6))));
                        env.test.BEAST_EXPECT(
                            vaultSle->at(sfLossUnrealized) == 0);
                    }
                }
            }
        }

        void
        checkPayment(
            std::int32_t loanScale,
            jtx::Account const& account,
            jtx::PrettyAmount const& balanceBefore,
            STAmount const& expectedPayment,
            jtx::PrettyAmount const& adjustment) const
        {
            auto const borrowerScale =
                std::max(loanScale, balanceBefore.number().exponent());

            STAmount const balanceChangeAmount{
                broker.asset,
                roundToAsset(
                    broker.asset, expectedPayment + adjustment, borrowerScale)};
            {
                auto const difference = roundToScale(
                    env.balance(account, broker.asset) -
                        (balanceBefore - balanceChangeAmount),
                    borrowerScale);
                env.test.BEAST_EXPECT(
                    roundToScale(difference, loanScale) >= beast::zero);
            }
        }

        /** Checks both the loan and broker expect states against the ledger */
        void
        operator()(
            std::uint32_t previousPaymentDate,
            std::uint32_t nextPaymentDate,
            std::uint32_t paymentRemaining,
            Number const& loanScale,
            Number const& totalValue,
            Number const& principalOutstanding,
            Number const& managementFeeOutstanding,
            Number const& periodicPayment,
            std::uint32_t flags) const
        {
            using namespace jtx;
            if (auto loan = env.le(keylet); env.test.BEAST_EXPECT(loan))
            {
                env.test.BEAST_EXPECT(
                    loan->at(sfPreviousPaymentDate) == previousPaymentDate);
                env.test.BEAST_EXPECT(
                    loan->at(sfPaymentRemaining) == paymentRemaining);
                if (paymentRemaining == 0)
                    env.test.BEAST_EXPECT(!loan->at(~sfNextPaymentDueDate));
                else
                    env.test.BEAST_EXPECT(
                        loan->at(sfNextPaymentDueDate) == nextPaymentDate);
                env.test.BEAST_EXPECT(loan->at(sfLoanScale) == loanScale);
                env.test.BEAST_EXPECT(
                    loan->at(sfTotalValueOutstanding) == totalValue);
                env.test.BEAST_EXPECT(
                    loan->at(sfPrincipalOutstanding) == principalOutstanding);
                env.test.BEAST_EXPECT(
                    loan->at(sfManagementFeeOutstanding) ==
                    managementFeeOutstanding);
                env.test.BEAST_EXPECT(
                    loan->at(sfPeriodicPayment) == periodicPayment);
                env.test.BEAST_EXPECT(loan->at(sfFlags) == flags);

                auto const ls = calculateRoundedLoanState(loan);

                auto const interestRate = TenthBips32{loan->at(sfInterestRate)};
                auto const paymentInterval = loan->at(sfPaymentInterval);
                checkBroker(
                    principalOutstanding,
                    ls.interestDue,
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
                            env.test.BEAST_EXPECT(
                                vaultSle->at(sfLossUnrealized) ==
                                totalValue - managementFeeOutstanding);
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

        /** Checks both the loan and broker expect states against the ledger */
        void
        operator()(LoanState const& state) const
        {
            operator()(
                state.previousPaymentDate,
                state.nextPaymentDate,
                state.paymentRemaining,
                state.loanScale,
                state.totalValue,
                state.principalOutstanding,
                state.managementFeeOutstanding,
                state.periodicPayment,
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
            managementFeeRate(managementFeeRateParameter),
            debtMaximum(debtMaximumValue),
            coverRateMinimum(TenthBips32(coverRateMinParameter)),
            coverRateLiquidation(TenthBips32(coverRateLiquidationParameter)));

        env(coverDeposit(lender, keylet.key, coverDepositValue));

        env.close();

        return {asset, keylet.key};
    }

    LoanState
    getCurrentState(
        jtx::Env const& env,
        BrokerInfo const& broker,
        Keylet const& loanKeylet,
        VerifyLoanStatus const& verifyLoanStatus)
    {
        using namespace std::chrono_literals;
        using d = NetClock::duration;
        using tp = NetClock::time_point;
        // Lookup the current loan state
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            LoanState state{
                .previousPaymentDate = loan->at(sfPreviousPaymentDate),
                .startDate = tp{d{loan->at(sfStartDate)}},
                .nextPaymentDate = loan->at(sfNextPaymentDueDate),
                .paymentRemaining = loan->at(sfPaymentRemaining),
                .loanScale = loan->at(sfLoanScale),
                .totalValue = loan->at(sfTotalValueOutstanding),
                .principalOutstanding = loan->at(sfPrincipalOutstanding),
                .managementFeeOutstanding =
                    loan->at(sfManagementFeeOutstanding),
                .periodicPayment = loan->at(sfPeriodicPayment),
                .flags = loan->at(sfFlags),
                .paymentInterval = loan->at(sfPaymentInterval),
                .interestRate = TenthBips32{loan->at(sfInterestRate)},
            };
            BEAST_EXPECT(state.previousPaymentDate == 0);
            BEAST_EXPECT(
                tp{d{state.nextPaymentDate}} == state.startDate + 600s);
            BEAST_EXPECT(state.paymentRemaining == 12);
            BEAST_EXPECT(
                state.principalOutstanding == broker.asset(1000).value());
            BEAST_EXPECT(
                state.loanScale ==
                (broker.asset.integral()
                     ? 0
                     : state.principalOutstanding.exponent()));
            BEAST_EXPECT(state.paymentInterval == 600);
            BEAST_EXPECT(
                state.totalValue ==
                roundToAsset(
                    broker.asset,
                    state.periodicPayment * state.paymentRemaining,
                    state.loanScale));
            BEAST_EXPECT(
                state.managementFeeOutstanding ==
                computeFee(
                    broker.asset,
                    state.totalValue - state.principalOutstanding,
                    managementFeeRateParameter,
                    state.loanScale));

            verifyLoanStatus(state);

            return state;
        }

        return LoanState{};
    }

    bool
    canImpairLoan(
        jtx::Env const& env,
        BrokerInfo const& broker,
        LoanState const& state)
    {
        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            if (auto const vaultSle =
                    env.le(keylet::vault(brokerSle->at(sfVaultID)));
                BEAST_EXPECT(vaultSle))
            {
                // log << vaultSle->getJson() << std::endl;
                auto const assetsUnavailable = vaultSle->at(sfAssetsTotal) -
                    vaultSle->at(sfAssetsAvailable);
                auto const unrealizedLoss = vaultSle->at(sfLossUnrealized) +
                    state.totalValue - state.managementFeeOutstanding;

                if (unrealizedLoss > assetsUnavailable)
                {
                    return false;
                }
            }
        }
        return true;
    }

    /** Runs through the complete lifecycle of a loan
     *
     * 1. Create a loan.
     * 2. Test a bunch of transaction failure conditions.
     * 3. Use the `toEndOfLife` callback to take the loan to 0. How that is done
     *    depends on the callback. e.g. Default, Early payoff, make all the
     * normal payments, etc.
     * 4. Delete the loan. The loan will alternate between being deleted by the
     *    lender and the borrower.
     */
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
        verifyLoanStatus.checkBroker(0, 0, TenthBips32{0}, 1, 0, 0);

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
            if (broker.asset.native())
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
            BEAST_EXPECT(
                loan->at(sfLoanScale) ==
                (broker.asset.integral() ? 0 : principalRequest.exponent()));
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == principalRequest);
        }

        auto state = getCurrentState(env, broker, keylet, verifyLoanStatus);

        auto const loanProperties = computeLoanProperties(
            broker.asset.raw(),
            state.principalOutstanding,
            state.interestRate,
            state.paymentInterval,
            state.paymentRemaining,
            managementFeeRateParameter);

        verifyLoanStatus(
            0,
            startDate + interval,
            total,
            broker.asset.integral() ? 0 : principalRequest.exponent(),
            loanProperties.totalValueOutstanding,
            principalRequest,
            loanProperties.managementFeeOwedToBroker,
            loanProperties.periodicPayment,
            loanFlags | 0);

        // Manage the loan
        // no-op
        env(manage(lender, keylet.key, 0));
        {
            // no flags
            auto jt = manage(lender, keylet.key, 0);
            jt.removeMember(sfFlags.getName());
            env(jt);
        }
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

        // Check the vault
        bool const canImpair = canImpairLoan(env, broker, state);
        // Impair the loan, if possible
        env(manage(lender, keylet.key, tfLoanImpair),
            canImpair ? ter(tesSUCCESS) : ter(tecLIMIT_EXCEEDED));
        // Unimpair the loan
        env(manage(lender, keylet.key, tfLoanUnimpair),
            canImpair ? ter(tesSUCCESS) : ter(tecNO_PERMISSION));

        auto const nextDueDate = startDate + interval;

        env.close();

        verifyLoanStatus(
            0,
            nextDueDate,
            total,
            broker.asset.integral() ? 0 : principalRequest.exponent(),
            loanProperties.totalValueOutstanding,
            principalRequest,
            loanProperties.managementFeeOwedToBroker,
            loanProperties.periodicPayment,
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
        // Either the borrower or the lender can delete the loan. Alternate
        // between who does it across tests.
        static unsigned deleteCounter = 0;
        auto const deleter = ++deleteCounter % 2 ? lender : borrower;
        env(del(deleter, keylet.key));
        env.close();

        PrettyAmount adjustment = broker.asset(0);
        if (deleter == borrower)
        {
            // Need to account for fees if the loan is in XRP
            if (broker.asset.native())
            {
                adjustment = env.current()->fees().base;
            }
        }

        // No loans left
        verifyLoanStatus.checkBroker(0, 0, interest, 1, 0, 0);

        BEAST_EXPECT(
            env.balance(borrower, broker.asset).value() ==
            borrowerStartingBalance.value() - adjustment);
        BEAST_EXPECT(env.ownerCount(borrower) == borrowerOwnerCount);

        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
        }
    }

    /** Wrapper to run a series of lifecycle tests for a given asset and loan
     * amount
     *
     * Will be used in the future to vary the loan parameters. For now, it is
     * only called once.
     *
     * Tests a bunch of LoanSet failure conditions before lifecycle.
     */
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
                if (broker.asset.native())
                {
                    // XRP can't be frozen
                    return std::make_tuple(empty, empty, empty, tesSUCCESS);
                }
                else if (broker.asset.holds<Issue>())
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
                                  BrokerInfo const& broker) {
            if (auto const brokerSle =
                    env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(
                    state.loanScale ==
                    (broker.asset.integral()
                         ? 0
                         : state.principalOutstanding.exponent()));
                auto const defaultAmount = roundToAsset(
                    broker.asset,
                    std::min(
                        tenthBipsOfValue(
                            tenthBipsOfValue(
                                brokerSle->at(sfDebtTotal),
                                coverRateMinParameter),
                            coverRateLiquidationParameter),
                        state.totalValue - state.managementFeeOutstanding),
                    state.loanScale);
                return std::make_pair(defaultAmount, brokerSle->at(sfOwner));
            }
            return std::make_pair(Number{}, AccountID{});
        };
        auto replenishCover = [&env, &coverAvailable](
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
            env.close();
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
                auto state =
                    getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == baseFlag);

                auto const& broker = verifyLoanStatus.broker;
                auto const startingCoverAvailable = coverAvailable(
                    broker.brokerID,
                    broker.asset(coverDepositParameter).number());

                if (impair)
                {
                    // Check the vault
                    bool const canImpair = canImpairLoan(env, broker, state);
                    // Impair the loan, if possible
                    env(manage(lender, loanKeylet.key, tfLoanImpair),
                        canImpair ? ter(tesSUCCESS) : ter(tecLIMIT_EXCEEDED));

                    if (canImpair)
                    {
                        state.flags |= tfLoanImpair;
                        state.nextPaymentDate =
                            env.now().time_since_epoch().count();

                        // Once the loan is impaired, it can't be impaired again
                        env(manage(lender, loanKeylet.key, tfLoanImpair),
                            ter(tecNO_PERMISSION));
                    }
                    verifyLoanStatus(state);
                }

                auto const nextDueDate = tp{d{state.nextPaymentDate}};

                // Can't default the loan yet. The grace period hasn't
                // expired
                env(manage(lender, loanKeylet.key, tfLoanDefault),
                    ter(tecTOO_SOON));

                // Let some time pass so that the loan can be
                // defaulted
                env.close(nextDueDate + 60s);

                auto const [amountToBeCovered, brokerAcct] =
                    getDefaultInfo(state, broker);

                // Default the loan
                env(manage(lender, loanKeylet.key, tfLoanDefault));
                env.close();

                // The LoanBroker just lost some of it's first-loss capital.
                // Replenish it.
                replenishCover(
                    broker,
                    brokerAcct,
                    startingCoverAvailable,
                    amountToBeCovered);

                state.flags |= tfLoanDefault;
                state.paymentRemaining = 0;
                state.totalValue = 0;
                state.principalOutstanding = 0;
                state.managementFeeOutstanding = 0;
                verifyLoanStatus(state);

                // Once a loan is defaulted, it can't be managed
                env(manage(lender, loanKeylet.key, tfLoanUnimpair),
                    ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanImpair),
                    ter(tecNO_PERMISSION));
                // Can't make a payment on it either
                env(pay(borrower, loanKeylet.key, broker.asset(300)),
                    ter(tecKILLED));
            };
        };

        auto singlePayment = [&](Keylet const& loanKeylet,
                                 VerifyLoanStatus const& verifyLoanStatus,
                                 LoanState& state,
                                 STAmount const& payoffAmount,
                                 std::uint32_t numPayments,
                                 std::uint32_t baseFlag,
                                 std::uint32_t txFlags) {
            // toEndOfLife
            //
            verifyLoanStatus(state);

            // Send some bogus pay transactions
            env(pay(borrower,
                    keylet::loan(uint256(0)).key,
                    broker.asset(10),
                    txFlags),
                ter(temINVALID));
            env(pay(borrower, loanKeylet.key, broker.asset(-100), txFlags),
                ter(temBAD_AMOUNT));
            env(pay(borrower, broker.brokerID, broker.asset(100), txFlags),
                ter(tecNO_ENTRY));
            env(pay(evan, loanKeylet.key, broker.asset(500), txFlags),
                ter(tecNO_PERMISSION));

            // TODO: Write a general "isFlag" function? See STObject::isFlag.
            // Maybe add a static overloaded member?
            if (!(state.flags & lsfLoanOverpayment))
            {
                // If the loan does not allow overpayments, send a payment that
                // tries to make an overpayment
                env(pay(borrower,
                        loanKeylet.key,
                        broker.asset(state.periodicPayment * 2),
                        tfLoanOverpayment | txFlags),
                    ter(temINVALID_FLAG));
            }

            {
                auto const otherAsset = broker.asset.raw() == assets[0].raw()
                    ? assets[1]
                    : assets[0];
                env(pay(borrower, loanKeylet.key, otherAsset(100), txFlags),
                    ter(tecWRONG_ASSET));
            }

            // Amount doesn't cover a single payment
            env(pay(borrower,
                    loanKeylet.key,
                    STAmount{broker.asset, 1},
                    txFlags),
                ter(tecINSUFFICIENT_PAYMENT));

            // Get the balance after these failed transactions take
            // fees
            auto const borrowerBalanceBeforePayment =
                env.balance(borrower, broker.asset);

            BEAST_EXPECT(payoffAmount > state.principalOutstanding);
            // Try to pay a little extra to show that it's _not_
            // taken
            auto const transactionAmount = payoffAmount + broker.asset(10);

            // Send a transaction that tries to pay more than the borrowers's
            // balance
            env(pay(borrower,
                    loanKeylet.key,
                    STAmount{
                        broker.asset,
                        borrowerBalanceBeforePayment.number() * 2},
                    txFlags),
                ter(tecINSUFFICIENT_FUNDS));

            env(pay(borrower, loanKeylet.key, transactionAmount, txFlags));

            env.close();

            // Need to account for fees if the loan is in XRP
            PrettyAmount adjustment = broker.asset(0);
            if (broker.asset.native())
            {
                adjustment = env.current()->fees().base * 2;
            }

            state.paymentRemaining = 0;
            state.principalOutstanding = 0;
            state.totalValue = 0;
            state.managementFeeOutstanding = 0;
            state.previousPaymentDate = state.nextPaymentDate +
                state.paymentInterval * (numPayments - 1);
            verifyLoanStatus(state);

            verifyLoanStatus.checkPayment(
                state.loanScale,
                borrower,
                borrowerBalanceBeforePayment,
                payoffAmount,
                adjustment);

            // Can't impair or default a paid off loan
            env(manage(lender, loanKeylet.key, tfLoanImpair),
                ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanDefault),
                ter(tecNO_PERMISSION));
        };

        auto fullPayment = [&](std::uint32_t baseFlag) {
            return [&, baseFlag](
                       Keylet const& loanKeylet,
                       VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                auto state =
                    getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                env.close(state.startDate + 20s);
                auto const loanAge = (env.now() - state.startDate).count();
                BEAST_EXPECT(loanAge == 30);

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
                STAmount const principalOutstanding{
                    broker.asset, state.principalOutstanding};
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
                auto const payoffAmount = roundToScale(
                    principalOutstanding + accruedInterest + prepaymentPenalty +
                        closePaymentFee,
                    state.loanScale);
                BEAST_EXPECT(
                    payoffAmount ==
                    broker.asset(Number(1040000114155251, -12)));

                // The terms of this loan actually make the early payoff
                // more expensive than just making payments
                BEAST_EXPECT(
                    payoffAmount > state.paymentRemaining *
                        (state.periodicPayment + broker.asset(2).value()));

                singlePayment(
                    loanKeylet,
                    verifyLoanStatus,
                    state,
                    payoffAmount,
                    1,
                    baseFlag,
                    tfLoanFullPayment);
            };
        };

        auto combineAllPayments = [&](std::uint32_t baseFlag) {
            return [&, baseFlag](
                       Keylet const& loanKeylet,
                       VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //

                auto state =
                    getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                env.close();

                // Make all the payments in one transaction
                // service fee is 2
                auto const startingPayments = state.paymentRemaining;
                auto const rawPayoff = startingPayments *
                    (state.periodicPayment + broker.asset(2).value());
                STAmount const payoffAmount{broker.asset, rawPayoff};
                BEAST_EXPECT(
                    payoffAmount ==
                    broker.asset(Number(1024014840139457, -12)));
                BEAST_EXPECT(payoffAmount > state.principalOutstanding);

                singlePayment(
                    loanKeylet,
                    verifyLoanStatus,
                    state,
                    payoffAmount,
                    state.paymentRemaining,
                    baseFlag,
                    0);
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
            fullPayment(0));

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
            fullPayment(lsfLoanOverpayment));

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Combine all payments",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            0,
            combineAllPayments(0));

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Combine all payments",
            env,
            loanAmount,
            interestExponent,
            lender,
            borrower,
            evan,
            broker,
            pseudoAcct,
            tfLoanOverpayment,
            combineAllPayments(lsfLoanOverpayment));

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
                auto state =
                    getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == 0);
                env.close();

                verifyLoanStatus(state);

                env.close(state.startDate + 20s);
                auto const loanAge = (env.now() - state.startDate).count();
                BEAST_EXPECT(loanAge == 30);

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
                STAmount const roundedPeriodicPayment{
                    broker.asset,
                    roundPeriodicPayment(
                        broker.asset, state.periodicPayment, state.loanScale)};

                testcase << "\tPayment components: "
                         << "Payments remaining, rawInterest, rawPrincipal, "
                            "rawMFee, roundedInterest, roundedPrincipal, "
                            "roundedMFee, special";

                auto const serviceFee = broker.asset(2);

                BEAST_EXPECT(
                    roundedPeriodicPayment ==
                    roundToScale(
                        broker.asset(
                            Number(8333457001162141, -14), Number::upward),
                        state.loanScale,
                        Number::upward));
                // 83334570.01162141
                // Include the service fee
                STAmount const totalDue = roundToScale(
                    roundedPeriodicPayment + serviceFee,
                    state.loanScale,
                    Number::upward);
                // Only check the first payment since the rounding
                // may drift as payments are made
                BEAST_EXPECT(
                    totalDue ==
                    roundToScale(
                        broker.asset(
                            Number(8533457001162141, -14), Number::upward),
                        state.loanScale,
                        Number::upward));

                {
                    auto const raw = calculateRawLoanState(
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining,
                        managementFeeRateParameter);
                    auto const rounded = calculateRoundedLoanState(
                        state.totalValue,
                        state.principalOutstanding,
                        state.managementFeeOutstanding);
                    testcase
                        << "\tLoan starting state: " << state.paymentRemaining
                        << ", " << raw.interestDue << ", "
                        << raw.principalOutstanding << ", "
                        << raw.managementFeeDue << ", " << rounded.interestDue
                        << ", " << rounded.principalOutstanding << ", "
                        << rounded.managementFeeDue;
                }

                while (state.paymentRemaining > 0)
                {
                    // Try to pay a little extra to show that it's _not_
                    // taken
                    STAmount const transactionAmount =
                        STAmount{broker.asset, totalDue} + broker.asset(10);
                    // Only check the first payment since the rounding
                    // may drift as payments are made
                    BEAST_EXPECT(
                        transactionAmount ==
                        roundToScale(
                            broker.asset(
                                Number(9533457001162141, -14), Number::upward),
                            state.loanScale,
                            Number::upward));

                    // Compute the expected principal amount
                    auto const paymentComponents = computePaymentComponents(
                        broker.asset.raw(),
                        state.loanScale,
                        state.totalValue,
                        state.principalOutstanding,
                        state.managementFeeOutstanding,
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining,
                        managementFeeRateParameter);

                    testcase
                        << "\tPayment components: " << state.paymentRemaining
                        << ", " << paymentComponents.rawInterest << ", "
                        << paymentComponents.rawPrincipal << ", "
                        << paymentComponents.rawManagementFee << ", "
                        << paymentComponents.roundedInterest << ", "
                        << paymentComponents.roundedPrincipal << ", "
                        << paymentComponents.roundedManagementFee << ", "
                        << (paymentComponents.specialCase == SpecialCase::final
                                ? "final"
                                : paymentComponents.specialCase ==
                                    SpecialCase::final
                                ? "extra"
                                : "none");

                    auto const totalDueAmount = STAmount{
                        broker.asset,
                        paymentComponents.roundedPrincipal +
                            paymentComponents.roundedInterest +
                            paymentComponents.roundedManagementFee +
                            serviceFee.number()};

                    // Due to the rounding algorithms to keep the interest and
                    // principal in sync with "true" values, the computed amount
                    // may be a little less than the rounded fixed payment
                    // amount. For integral types, the difference should be < 3
                    // (1 unit for each of the interest and management fee). For
                    // IOUs, the difference should be after the 8th digit.
                    Number const diff = totalDue - totalDueAmount;
                    BEAST_EXPECT(
                        paymentComponents.specialCase == SpecialCase::final ||
                        diff == beast::zero ||
                        (diff > beast::zero &&
                         ((broker.asset.integral() &&
                           (static_cast<Number>(diff) < 3)) ||
                          (totalDue.exponent() - diff.exponent() > 8))));

                    BEAST_EXPECT(
                        paymentComponents.roundedInterest >= Number(0));

                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToAsset(
                            broker.asset,
                            paymentComponents.rawPrincipal,
                            state.loanScale,
                            Number::upward) ==
                            roundToScale(
                                broker.asset(
                                    Number(8333228690659858, -14),
                                    Number::upward),
                                state.loanScale,
                                Number::upward));
                    BEAST_EXPECT(
                        paymentComponents.roundedPrincipal >= Number(0) &&
                        paymentComponents.roundedPrincipal <=
                            state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase != SpecialCase::final ||
                        paymentComponents.roundedPrincipal ==
                            state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase == SpecialCase::final ||
                        (state.periodicPayment.exponent() -
                         (paymentComponents.rawPrincipal +
                          paymentComponents.rawInterest +
                          paymentComponents.rawManagementFee -
                          state.periodicPayment)
                             .exponent()) > 14);

                    auto const borrowerBalanceBeforePayment =
                        env.balance(borrower, broker.asset);

                    if (canImpairLoan(env, broker, state))
                        // Making a payment will unimpair the loan
                        env(manage(lender, loanKeylet.key, tfLoanImpair));

                    env.close();

                    // Make the payment
                    env(pay(borrower, loanKeylet.key, transactionAmount));

                    env.close();

                    // Need to account for fees if the loan is in XRP
                    PrettyAmount adjustment = broker.asset(0);
                    if (broker.asset.native())
                    {
                        adjustment = env.current()->fees().base;
                    }

                    // Check the result
                    verifyLoanStatus.checkPayment(
                        state.loanScale,
                        borrower,
                        borrowerBalanceBeforePayment,
                        totalDueAmount,
                        adjustment);

                    --state.paymentRemaining;
                    state.previousPaymentDate = state.nextPaymentDate;
                    if (paymentComponents.specialCase == SpecialCase::final)
                    {
                        state.paymentRemaining = 0;
                    }
                    else
                    {
                        state.nextPaymentDate += state.paymentInterval;
                    }
                    state.principalOutstanding -=
                        paymentComponents.roundedPrincipal;
                    state.managementFeeOutstanding -=
                        paymentComponents.roundedManagementFee;
                    state.totalValue -= paymentComponents.roundedPrincipal +
                        paymentComponents.roundedInterest +
                        paymentComponents.roundedManagementFee;

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

#if LOANCOMPLETE
        // TODO

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Explicit overpayment",
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
                VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

        lifecycle(
            caseLabel,
            "Loan overpayment prohibited - Late payment",
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
                VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Late payment",
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
                VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

        lifecycle(
            caseLabel,
            "Loan overpayment allowed - Late payment and overpayment",
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
                VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

#endif
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
        // Scale the MPT asset a little bit so we can get some interest
        PrettyAsset const mptAsset{mptt.issuanceID(), 100};
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
            // soeDEFAULT fields are not returned if they're in the default
            // state
            BEAST_EXPECT(!loan.isMember(sfCloseInterestRate));
            BEAST_EXPECT(!loan.isMember(sfClosePaymentFee));
            BEAST_EXPECT(loan[sfFlags] == 0);
            BEAST_EXPECT(loan[sfGracePeriod] == 60);
            BEAST_EXPECT(!loan.isMember(sfInterestRate));
            BEAST_EXPECT(!loan.isMember(sfLateInterestRate));
            BEAST_EXPECT(!loan.isMember(sfLatePaymentFee));
            BEAST_EXPECT(loan[sfLoanBrokerID] == to_string(broker.brokerID));
            BEAST_EXPECT(!loan.isMember(sfLoanOriginationFee));
            BEAST_EXPECT(loan[sfLoanSequence] == 1);
            BEAST_EXPECT(!loan.isMember(sfLoanServiceFee));
            BEAST_EXPECT(
                loan[sfNextPaymentDueDate] == loan[sfStartDate].asUInt() + 60);
            BEAST_EXPECT(!loan.isMember(sfOverpaymentFee));
            BEAST_EXPECT(!loan.isMember(sfOverpaymentInterestRate));
            BEAST_EXPECT(loan[sfPaymentInterval] == 60);
            BEAST_EXPECT(loan[sfPeriodicPayment] == "1000000000");
            BEAST_EXPECT(loan[sfPaymentRemaining] == 1);
            BEAST_EXPECT(!loan.isMember(sfPreviousPaymentDate));
            BEAST_EXPECT(loan[sfPrincipalOutstanding] == "1000000000");
            BEAST_EXPECT(loan[sfTotalValueOutstanding] == "1000000000");
            BEAST_EXPECT(!loan.isMember(sfLoanScale));
            BEAST_EXPECT(
                loan[sfStartDate].asUInt() ==
                startDate.time_since_epoch().count());

            return loan["index"].asString();
        }();
        auto const loanKeylet{keylet::loan(uint256{std::string_view(loanID)})};

        env.close(startDate);

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

        env.fund(XRP(vaultDeposit * 100), lender, borrower);
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

        env.fund(XRP(vaultDeposit * 100), issuer, noripple(lender));
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

        env.fund(XRP(vaultDeposit * 100), issuer, lender, borrower);
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

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        // Fails in preclaim because principal requested can't be represented as
        // XRP
        env(createJson, ter(tecPRECISION_LOSS));
        env.close();

        BEAST_EXPECT(!env.le(keylet));

        Number const actualPrincipal{6};

        createJson[sfPrincipalRequested] = actualPrincipal;
        createJson.removeMember(sfSequence.jsonName);
        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        // Fails in doApply because the payment is too small to be represented
        // as XRP.
        env(createJson, ter(tecPRECISION_LOSS));
        env.close();
    }

    void
    testRPC()
    {
        // This will expand as more test cases are added. Some functionality
        // is tested in other test functions.
        testcase("RPC");

        using namespace jtx;

        Env env(*this, all);

        auto lowerFee = [&]() {
            // Run the local fee back down.
            while (env.app().getFeeTrack().lowerLocalFee())
                ;
        };

        auto const baseFee = env.current()->fees().base;

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

            lowerFee();
            auto const jSubmit = env.rpc("submit", txSignBlob);
            BEAST_EXPECT(
                jSubmit.isMember(jss::result) &&
                jSubmit[jss::result].isMember(jss::engine_result) &&
                jSubmit[jss::result][jss::engine_result].asString() ==
                    "tesSUCCESS");

            lowerFee();
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
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = to_string(24 * baseFee / 10);

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
            BEAST_EXPECTS(
                jSignBorrower.isMember(jss::result) &&
                    jSignBorrower[jss::result].isMember(jss::tx_json),
                to_string(jSignBorrower));
            auto const txBorrowerSignResult =
                jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob =
                jSignBorrower[jss::result][jss::tx_blob].asString();

            // 2a. Borrower attempts to submit the transaction. It doesn't
            // work
            {
                lowerFee();
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
            lowerFee();
            auto const jSubmitBlob = env.rpc("submit", txLenderSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECTS(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) &&
                !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Lender submits the transaction json originally
            // received from the Borrower. It gets signed, but is now a
            // duplicate, so fails. Borrower could done this instead of
            // steps 4 and 5.
            lowerFee();
            auto const jSubmitJson =
                env.rpc("json", "submit", to_string(lenderSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() ==
                        "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

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
            txJson[sfPaymentTotal] = 10000;
            txJson[sfPaymentInterval] = 3600;
            txJson[sfGracePeriod] = 300;
            txJson[sfFlags] = 65536;  // tfLoanOverpayment
            txJson[sfFee] = to_string(24 * baseFee / 10);

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
                lowerFee();
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
            lowerFee();
            auto const jSubmitBlob = env.rpc("submit", txBorrowerSignBlob);
            BEAST_EXPECT(jSubmitBlob.isMember(jss::result));
            auto const jSubmitBlobResult = jSubmitBlob[jss::result];
            BEAST_EXPECT(jSubmitBlobResult.isMember(jss::tx_json));
            auto const jSubmitBlobTx = jSubmitBlobResult[jss::tx_json];
            // To get far enough to return tecNO_ENTRY means that the
            // signatures all validated. Of course the transaction won't
            // succeed because no Vault or Broker were created.
            BEAST_EXPECTS(
                jSubmitBlobResult.isMember(jss::engine_result) &&
                    jSubmitBlobResult[jss::engine_result].asString() ==
                        "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) &&
                !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Borrower submits the transaction json originally
            // received from the Lender. It gets signed, but is now a
            // duplicate, so fails. Lender could done this instead of steps
            // 4 and 5.
            lowerFee();
            auto const jSubmitJson =
                env.rpc("json", "submit", to_string(borrowerSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() ==
                        "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) &&
                !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }
    }

    void
    testServiceFeeOnBrokerDeepFreeze()
    {
        testcase << "Service Fee On Broker Deep Freeze";
        using namespace jtx;
        using namespace loan;
        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");
        auto const IOU = issuer["IOU"];

        for (bool const deepFreeze : {true, false})
        {
            Env env(*this);

            auto getCoverBalance = [&](BrokerInfo const& brokerInfo,
                                       auto const& accountField) {
                if (auto const le =
                        env.le(keylet::loanbroker(brokerInfo.brokerID));
                    BEAST_EXPECT(le))
                {
                    auto const account = le->at(accountField);
                    if (auto const sleLine = env.le(keylet::line(account, IOU));
                        BEAST_EXPECT(sleLine))
                    {
                        STAmount balance = sleLine->at(sfBalance);
                        if (account > issuer.id())
                            balance.negate();
                        return balance;
                    }
                }
                return STAmount{IOU};
            };

            env.fund(XRP(20'000), issuer, broker, borrower);
            env.close();

            env(trust(broker, IOU(20'000'000)));
            env(pay(issuer, broker, IOU(10'000'000)));
            env.close();

            auto const brokerInfo = createVaultAndBroker(env, IOU, broker);

            BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == IOU(1'000));

            auto const keylet = keylet::loan(brokerInfo.brokerID, 1);

            env(set(borrower, brokerInfo.brokerID, 10'000),
                sig(sfCounterpartySignature, broker),
                loanServiceFee(IOU(100).value()),
                paymentInterval(100),
                fee(XRP(100)));
            env.close();

            env(trust(borrower, IOU(20'000'000)));
            // The borrower increases their limit and acquires some IOU so they
            // can pay interest
            env(pay(issuer, borrower, IOU(500)));
            env.close();

            if (auto const le = env.le(keylet::loan(keylet.key));
                BEAST_EXPECT(le))
            {
                if (deepFreeze)
                {
                    env(trust(
                        issuer,
                        broker["IOU"](0),
                        tfSetFreeze | tfSetDeepFreeze));
                    env.close();
                }

                env(pay(borrower, keylet.key, IOU(10'100)), fee(XRP(100)));
                env.close();

                if (deepFreeze)
                {
                    // The fee goes to the broker pseudo-account
                    BEAST_EXPECT(
                        getCoverBalance(brokerInfo, sfAccount) == IOU(1'100));
                    BEAST_EXPECT(
                        getCoverBalance(brokerInfo, sfOwner) == IOU(8'999'000));
                }
                else
                {
                    // The fee goes to the broker account
                    BEAST_EXPECT(
                        getCoverBalance(brokerInfo, sfOwner) == IOU(8'999'100));
                    BEAST_EXPECT(
                        getCoverBalance(brokerInfo, sfAccount) == IOU(1'000));
                }
            }
        };
    }

    void
    testBasicMath()
    {
        // Test the functions defined in LendingHelpers.h
        testcase("Basic Math");

        pass();
    }

    void
    testIssuerLoan()
    {
        testcase << "Issuer Loan";

        using namespace jtx;
        using namespace loan;
        Account const issuer("issuer");
        Account const borrower = issuer;
        Account const lender("lender");
        Env env(*this);

        env.fund(XRP(1'000), issuer, lender);

        std::int64_t constexpr issuerBalance = 10'000'000;
        MPTTester asset(
            {.env = env,
             .issuer = issuer,
             .holders = {lender},
             .pay = issuerBalance});

        auto const broker = createVaultAndBroker(env, asset, lender, 200);
        auto const loanSetFee = fee(env.current()->fees().base * 2);
        // Create Loan
        env(set(borrower, broker.brokerID, 200),
            sig(sfCounterpartySignature, lender),
            loanSetFee);
        env.close();
        // Issuer should not create MPToken
        BEAST_EXPECT(!env.le(keylet::mptoken(asset.issuanceID(), issuer)));
        // Issuer "borrowed" 200, OutstandingAmount decreased by 200
        BEAST_EXPECT(env.balance(issuer, asset) == asset(-issuerBalance + 200));
        // Pay Loan
        auto const loanKeylet = keylet::loan(broker.brokerID, 1);
        env(pay(borrower, loanKeylet.key, asset(200)));
        env.close();
        // Issuer "re-payed" 200, OutstandingAmount increased by 200
        BEAST_EXPECT(env.balance(issuer, asset) == asset(-issuerBalance));
    }

    void
    testInvalidLoanDelete()
    {
        testcase("Invalid LoanDelete");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(del(alice, beast::zero), ter(temINVALID));
        }
    }

    void
    testInvalidLoanManage()
    {
        testcase("Invalid LoanManage");
        using namespace jtx;
        using namespace loan;

        // preflight: temINVALID, LoanID == zero
        {
            Account const alice{"alice"};
            Env env(*this);
            env.fund(XRP(1'000), alice);
            env.close();
            env(manage(alice, beast::zero, tfLoanDefault), ter(temINVALID));
        }
    }

    void
    testInvalidLoanPay()
    {
        testcase("Invalid LoanPay");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        auto const IOU = issuer["IOU"];

        // preclaim
        Env env(*this);
        env.fund(XRP(1'000), lender, issuer, borrower);
        env(trust(lender, IOU(10'000'000)));
        env(pay(issuer, lender, IOU(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
            sig(sfCounterpartySignature, lender),
            loanSetFee);

        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        env(fset(issuer, asfGlobalFreeze));
        env.close();

        // preclaim: tecFROZEN
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), ter(tecFROZEN));
        env.close();

        env(fclear(issuer, asfGlobalFreeze));
        env.close();

        auto const pseudoBroker = [&]() -> std::optional<Account> {
            if (auto brokerSle =
                    env.le(keylet::loanbroker(brokerInfo.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                return Account{"pseudo", brokerSle->at(sfAccount)};
            }
            else
            {
                return std::nullopt;
            }
        }();
        if (!pseudoBroker)
            return;

        // Lender and pseudoaccount must both be frozen
        env(trust(
            issuer,
            lender["IOU"](1'000),
            lender,
            tfSetFreeze | tfSetDeepFreeze));
        env(trust(
            issuer,
            (*pseudoBroker)["IOU"](1'000),
            *pseudoBroker,
            tfSetFreeze | tfSetDeepFreeze));
        env.close();

        // preclaim: tecFROZEN due to deep frozen
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), ter(tecFROZEN));
        env.close();

        // Only one needs to be unfrozen
        env(trust(
            issuer, lender["IOU"](1'000), tfClearFreeze | tfClearDeepFreeze));
        env.close();

        env(pay(borrower, loanKeylet.key, debtMaximumRequest));
        env.close();

        // preclaim: tecKILLED
        // note that tecKILLED in loanMakePayment()
        // doesn't happen because of the preclaim check.
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), ter(tecKILLED));
    }

    void
    testInvalidLoanSet()
    {
        testcase("Invalid LoanSet");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        auto const IOU = issuer["IOU"];

        auto testWrapper = [&](auto&& test) {
            Env env(*this);
            env.fund(XRP(1'000), lender, issuer, borrower);
            env(trust(lender, IOU(10'000'000)));
            env(pay(issuer, lender, IOU(5'000'000)));
            BrokerInfo brokerInfo{
                createVaultAndBroker(env, issuer["IOU"], lender)};

            auto const loanSetFee = fee(env.current()->fees().base * 2);
            Number const debtMaximumRequest = brokerInfo.asset(1'000).value();
            test(env, brokerInfo, loanSetFee, debtMaximumRequest);
        };

        // preflight:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            // first temBAD_SIGNER: TODO

            // preflightCheckSigningKey() failure:
            // can it happen? the signature is checked before transactor
            // executes

            JTx tx = env.jt(
                set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                sig(sfCounterpartySignature, lender),
                loanSetFee);
            STTx local = *(tx.stx);
            auto counterpartySig =
                local.getFieldObject(sfCounterpartySignature);
            auto badPubKey = counterpartySig.getFieldVL(sfSigningPubKey);
            badPubKey[20] ^= 0xAA;
            counterpartySig.setFieldVL(sfSigningPubKey, badPubKey);
            local.setFieldObject(sfCounterpartySignature, counterpartySig);
            Json::Value jvResult;
            jvResult[jss::tx_blob] = strHex(local.getSerializer().slice());
            auto res = env.rpc("json", "submit", to_string(jvResult))["result"];
            BEAST_EXPECT(
                res[jss::error] == "invalidTransaction" &&
                res[jss::error_exception] ==
                    "fails local checks: Counterparty: Invalid signature.");
        });

        // preclaim:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            // canAddHoldingFailure (IOU only, if MPT doesn't have
            // MPTCanTransfer set, then can't create Vault/LoanBroker,
            // and LoanSet will fail with different error
            env(fclear(issuer, asfDefaultRipple));
            env.close();
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(terNO_RIPPLE));
        });

        // doApply:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            auto const amt = env.balance(borrower) -
                env.current()->fees().accountReserve(env.ownerCount(borrower));
            env(pay(borrower, issuer, amt));

            // tecINSUFFICIENT_RESERVE
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecINSUFFICIENT_RESERVE));

            // addEmptyHolding failure
            env(pay(issuer, borrower, amt));
            env(fset(issuer, asfGlobalFreeze));
            env.close();

            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                ter(tecFROZEN));
        });
    }

public:
    void
    run() override
    {
        testIssuerLoan();
        testDisabled();
        testSelfLoan();
        testLifecycle();
        testBatchBypassCounterparty();
        testWrongMaxDebtBehavior();
        testLoanPayComputePeriodicPaymentValidRateInvariant();
        testServiceFeeOnBrokerDeepFreeze();

        testRPC();
        testBasicMath();

        testInvalidLoanDelete();
        testInvalidLoanManage();
        testInvalidLoanPay();
        testInvalidLoanSet();
    }
};

BEAST_DEFINE_TESTSUITE(Loan, tx, ripple);

}  // namespace test
}  // namespace ripple
