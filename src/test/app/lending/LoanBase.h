#pragma once

#include <xrpl/beast/unit_test/suite.h>
//
#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/transactors/lending/LendingHelpers.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <chrono>

namespace xrpl::test {
class LoanBase : public beast::unit_test::suite
{
protected:
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    FeatureBitset const all{
        jtx::testable_amendments() | featureMPTokensV1 | featureSingleAssetVault |
        featureLendingProtocol};

    std::string const iouCurrency{"IOU"};
    struct BrokerParameters
    {
        Number vaultDeposit = 1'000'000;
        Number debtMax = 25'000;
        TenthBips32 coverRateMin = percentageToTenthBips(10);
        int coverDeposit = 1000;
        TenthBips16 managementFeeRate{100};
        TenthBips32 coverRateLiquidation = percentageToTenthBips(25);
        std::string data = {};  // NOLINT(readability-redundant-member-init)
        std::uint32_t flags = 0;

        Number
        maxCoveredLoanValue(Number const& currentDebt) const
        {
            NumberRoundModeGuard mg(Number::downward);
            auto debtLimit = coverDeposit * tenthBipsPerUnity.value() / coverRateMin.value();

            return debtLimit - currentDebt;
        }

        static BrokerParameters const&
        defaults()
        {
            static BrokerParameters const result{};
            return result;
        }

        // TODO: create an operator() which returns a transaction similar to
        // LoanParameters
    };

    struct BrokerInfo
    {
        jtx::PrettyAsset asset;
        uint256 brokerID;
        uint256 vaultID;
        BrokerParameters params;
        BrokerInfo(
            jtx::PrettyAsset const& asset_,
            Keylet const& brokerKeylet_,
            Keylet const& vaultKeylet_,
            BrokerParameters const& p)
            : asset(asset_), brokerID(brokerKeylet_.key), vaultID(vaultKeylet_.key), params(p)
        {
        }

        Keylet
        brokerKeylet() const
        {
            return keylet::loanbroker(brokerID);
        }
        Keylet
        vaultKeylet() const
        {
            return keylet::vault(vaultID);
        }

        int
        vaultScale(jtx::Env const& env) const
        {
            using namespace jtx;

            auto const vaultSle = env.le(keylet::vault(vaultID));
            return getAssetsTotalScale(vaultSle);
        }
    };

    struct LoanParameters
    {
        // The account submitting the transaction. May be borrower or broker.
        jtx::Account account;
        // The counterparty. Should be the other of borrower or broker.
        jtx::Account counter;
        // Whether the counterparty is specified in the `counterparty` field, or
        // only signs.
        bool counterpartyExplicit = true;
        Number principalRequest;
        // NOLINTBEGIN(readability-redundant-member-init)
        std::optional<STAmount> setFee = std::nullopt;
        std::optional<Number> originationFee = std::nullopt;
        std::optional<Number> serviceFee = std::nullopt;
        std::optional<Number> lateFee = std::nullopt;
        std::optional<Number> closeFee = std::nullopt;
        std::optional<TenthBips32> overFee = std::nullopt;
        std::optional<TenthBips32> interest = std::nullopt;
        std::optional<TenthBips32> lateInterest = std::nullopt;
        std::optional<TenthBips32> closeInterest = std::nullopt;
        std::optional<TenthBips32> overpaymentInterest = std::nullopt;
        std::optional<std::uint32_t> payTotal = std::nullopt;
        std::optional<std::uint32_t> payInterval = std::nullopt;
        std::optional<std::uint32_t> gracePd = std::nullopt;
        std::optional<std::uint32_t> flags = std::nullopt;
        // NOLINTEND(readability-redundant-member-init)

        Json::Value
        getTransaction(jtx::Env& env, BrokerInfo const& broker) const
        {
            using namespace jtx;
            using namespace jtx::loan;

            JTx jt{loan::set(
                account,
                broker.brokerID,
                broker.asset(principalRequest).number(),
                flags.value_or(0))};

            sig(sfCounterpartySignature, counter)(env, jt);

            fee{setFee.value_or(env.current()->fees().base * 2)}(env, jt);

            if (counterpartyExplicit)
                counterparty(counter)(env, jt);
            if (originationFee)
                loanOriginationFee(broker.asset(*originationFee).number())(env, jt);
            if (serviceFee)
                loanServiceFee(broker.asset(*serviceFee).number())(env, jt);
            if (lateFee)
                latePaymentFee(broker.asset(*lateFee).number())(env, jt);
            if (closeFee)
                closePaymentFee(broker.asset(*closeFee).number())(env, jt);
            if (overFee)
                overpaymentFee (*overFee)(env, jt);
            if (interest)
                interestRate (*interest)(env, jt);
            if (lateInterest)
                lateInterestRate (*lateInterest)(env, jt);
            if (closeInterest)
                closeInterestRate (*closeInterest)(env, jt);
            if (overpaymentInterest)
                overpaymentInterestRate (*overpaymentInterest)(env, jt);
            if (payTotal)
                paymentTotal (*payTotal)(env, jt);
            if (payInterval)
                paymentInterval (*payInterval)(env, jt);
            if (gracePd)
                gracePeriod (*gracePd)(env, jt);

            return jt.jv;
        }

        template <class... FN>
        jtx::JTx
        operator()(jtx::Env& env, BrokerInfo const& broker, FN const&... fN) const
        {
            using namespace jtx;
            using namespace jtx::loan;

            auto tx = getTransaction(env, broker);
            auto jt = env.jt(tx, std::forward<FN>(fN)...);
            sig(sfCounterpartySignature, counter)(env, jt);
            return jt;
        }
    };

    struct PaymentParameters
    {
        Number overpaymentFactor = Number{1};
        std::optional<Number> overpaymentExtra = std::nullopt;
        std::uint32_t flags = 0;
        bool showStepBalances = false;
        bool validateBalances = true;

        static PaymentParameters const&
        defaults()
        {
            static PaymentParameters const result{};
            return result;
        }
    };

    struct LoanState
    {
        std::uint32_t previousPaymentDate = 0;
        NetClock::time_point startDate;
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
        jtx::Account const& pseudoAccount;
        Keylet const& loanKeylet;

        VerifyLoanStatus(
            jtx::Env const& env_,
            BrokerInfo const& broker_,
            jtx::Account const& pseudo_,
            Keylet const& keylet_)
            : env(env_), broker(broker_), pseudoAccount(pseudo_), loanKeylet(keylet_)
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
                TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
                auto const brokerDebt = brokerSle->at(sfDebtTotal);
                auto const expectedDebt = principalOutstanding + interestOwed;
                env.test.BEAST_EXPECT(brokerDebt == expectedDebt);
                env.test.BEAST_EXPECT(
                    env.balance(pseudoAccount, broker.asset).number() ==
                    brokerSle->at(sfCoverAvailable));
                env.test.BEAST_EXPECT(brokerSle->at(sfOwnerCount) == ownerCount);

                if (auto vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                    env.test.BEAST_EXPECT(vaultSle))
                {
                    Account const vaultPseudo{"vaultPseudoAccount", vaultSle->at(sfAccount)};
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
                             ((total - available) / available < Number(1, -6))));
                        env.test.BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == 0);
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
            auto const borrowerScale = std::max(loanScale, balanceBefore.number().exponent());

            STAmount const balanceChangeAmount{
                broker.asset,
                roundToAsset(broker.asset, expectedPayment + adjustment, borrowerScale)};
            {
                auto const difference = roundToScale(
                    env.balance(account, broker.asset) - (balanceBefore - balanceChangeAmount),
                    borrowerScale);
                env.test.expect(
                    roundToScale(difference, loanScale) >= beast::zero,
                    "Balance before: " + to_string(balanceBefore.value()) +
                        ", expected change: " + to_string(balanceChangeAmount) +
                        ", difference (balance after - expected): " + to_string(difference),
                    __FILE__,
                    __LINE__);
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
            if (auto loan = env.le(loanKeylet); env.test.BEAST_EXPECT(loan))
            {
                env.test.BEAST_EXPECT(loan->at(sfPreviousPaymentDueDate) == previousPaymentDate);
                env.test.BEAST_EXPECT(loan->at(sfPaymentRemaining) == paymentRemaining);
                env.test.BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == nextPaymentDate);
                env.test.BEAST_EXPECT(loan->at(sfLoanScale) == loanScale);
                env.test.BEAST_EXPECT(loan->at(sfTotalValueOutstanding) == totalValue);
                env.test.BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == principalOutstanding);
                env.test.BEAST_EXPECT(
                    loan->at(sfManagementFeeOutstanding) == managementFeeOutstanding);
                env.test.BEAST_EXPECT(loan->at(sfPeriodicPayment) == periodicPayment);
                env.test.BEAST_EXPECT(loan->at(sfFlags) == flags);

                auto const ls = constructRoundedLoanState(loan);

                auto const interestRate = TenthBips32{loan->at(sfInterestRate)};
                auto const paymentInterval = loan->at(sfPaymentInterval);
                checkBroker(
                    principalOutstanding,
                    ls.interestDue,
                    interestRate,
                    paymentInterval,
                    paymentRemaining,
                    1);

                if (auto brokerSle = env.le(keylet::loanbroker(broker.brokerID));
                    env.test.BEAST_EXPECT(brokerSle))
                {
                    if (auto vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                        env.test.BEAST_EXPECT(vaultSle))
                    {
                        if (((flags & lsfLoanImpaired) != 0u) && ((flags & lsfLoanDefault) == 0u))
                        {
                            env.test.BEAST_EXPECT(
                                vaultSle->at(sfLossUnrealized) ==
                                totalValue - managementFeeOutstanding);
                        }
                        else
                        {
                            env.test.BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == 0);
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

    struct VaultAndBrokerTransactions
    {
        Json::Value vaultCreateTx;
        Keylet vaultKeylet;
        Json::Value vaultDepositTx;
        Json::Value brokerSetTx;
        Keylet brokerKeylet;
        std::optional<Json::Value> coverDepositTx;
        jtx::PrettyAsset asset;
        BrokerParameters params;
    };

    static VaultAndBrokerTransactions
    createVaultAndBrokerTransactions(
        jtx::Env& env,
        jtx::PrettyAsset const& asset,
        jtx::Account const& lender,
        BrokerParameters const& params = BrokerParameters::defaults(),
        std::optional<std::uint32_t> lenderSeq = std::nullopt)
    {
        uint32_t sequence = lenderSeq ? *lenderSeq : env.seq(lender);

        using namespace jtx;

        Vault vault{env};

        auto const deposit = asset(params.vaultDeposit);
        auto const debtMaximumValue = asset(params.debtMax).value();
        auto const coverDepositValue = asset(params.coverDeposit).value();

        auto const coverRateMinValue = params.coverRateMin;

        auto [vaultCreateTx, vaultKeylet] =
            vault.create({.owner = lender, .asset = asset, .sequence = sequence});

        auto vaultDepositTx =
            vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = deposit});

        auto const brokerKeylet = keylet::loanbroker(lender.id(), sequence + 2);

        using namespace loanBroker;

        JTx brokerSetJtx = JTx{set(lender, vaultKeylet.key, params.flags)};
        data(params.data)(env, brokerSetJtx);
        managementFeeRate(params.managementFeeRate)(env, brokerSetJtx);
        debtMaximum(debtMaximumValue)(env, brokerSetJtx);
        coverRateMinimum(coverRateMinValue)(env, brokerSetJtx);
        coverRateLiquidation(TenthBips32(params.coverRateLiquidation))(env, brokerSetJtx);

        auto brokerSetTx = brokerSetJtx.jv;

        std::optional<Json::Value> coverDepositTxOpt;
        if (coverDepositValue != beast::zero)
            coverDepositTxOpt = coverDeposit(lender, brokerKeylet.key, coverDepositValue);

        return {
            .vaultCreateTx = vaultCreateTx,
            .vaultKeylet = vaultKeylet,
            .vaultDepositTx = vaultDepositTx,
            .brokerSetTx = brokerSetTx,
            .brokerKeylet = brokerKeylet,
            .coverDepositTx = coverDepositTxOpt,
            .asset = asset,
            .params = params};
    }

    void
    checkVaultAndBroker(jtx::Env& env, VaultAndBrokerTransactions const& txs)
    {
        using namespace jtx;

        auto const deposit = txs.asset(txs.params.vaultDeposit);

        // Check vault exists
        BEAST_EXPECT(env.le(txs.vaultKeylet));

        // Check vault deposit
        if (auto const vault = env.le(keylet::vault(txs.vaultKeylet.key)); BEAST_EXPECT(vault))
        {
            BEAST_EXPECT(vault->at(sfAssetsAvailable) == deposit.value());
        }
    }

    BrokerInfo
    createVaultAndBroker(
        jtx::Env& env,
        jtx::PrettyAsset const& asset,
        jtx::Account const& lender,
        BrokerParameters const& params = BrokerParameters::defaults())
    {
        using namespace jtx;

        auto txs = createVaultAndBrokerTransactions(env, asset, lender, params);

        env(txs.vaultCreateTx);
        env.close();

        env(txs.vaultDepositTx);
        env.close();

        env(txs.brokerSetTx);
        env.close();

        if (txs.coverDepositTx)
            env(*txs.coverDepositTx);

        env.close();

        checkVaultAndBroker(env, txs);

        return {asset, txs.brokerKeylet, txs.vaultKeylet, params};
    }

    /// Get the state without checking anything
    LoanState
    getCurrentState(jtx::Env const& env, BrokerInfo const& broker, Keylet const& loanKeylet)
    {
        using d = NetClock::duration;
        using tp = NetClock::time_point;

        // Lookup the current loan state
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            return LoanState{
                .previousPaymentDate = loan->at(sfPreviousPaymentDueDate),
                .startDate = tp{d{loan->at(sfStartDate)}},
                .nextPaymentDate = loan->at(sfNextPaymentDueDate),
                .paymentRemaining = loan->at(sfPaymentRemaining),
                .loanScale = loan->at(sfLoanScale),
                .totalValue = loan->at(sfTotalValueOutstanding),
                .principalOutstanding = loan->at(sfPrincipalOutstanding),
                .managementFeeOutstanding = loan->at(sfManagementFeeOutstanding),
                .periodicPayment = loan->at(sfPeriodicPayment),
                .flags = loan->at(sfFlags),
                .paymentInterval = loan->at(sfPaymentInterval),
                .interestRate = TenthBips32{loan->at(sfInterestRate)},
            };
        }
        return LoanState{};
    }

    /// Get the state and check the values against the parameters used in
    /// `lifecycle`
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

        auto const state = getCurrentState(env, broker, loanKeylet);
        BEAST_EXPECT(state.previousPaymentDate == 0);
        BEAST_EXPECT(tp{d{state.nextPaymentDate}} == state.startDate + 600s);
        BEAST_EXPECT(state.paymentRemaining == 12);
        BEAST_EXPECT(state.principalOutstanding == broker.asset(1000).value());
        BEAST_EXPECT(
            state.loanScale >=
            (broker.asset.integral()
                 ? 0
                 : std::max(broker.vaultScale(env), state.principalOutstanding.exponent())));
        BEAST_EXPECT(state.paymentInterval == 600);
        {
            NumberRoundModeGuard mg(Number::upward);
            BEAST_EXPECT(
                state.totalValue ==
                roundToAsset(
                    broker.asset, state.periodicPayment * state.paymentRemaining, state.loanScale));
        }
        BEAST_EXPECT(
            state.managementFeeOutstanding ==
            computeManagementFee(
                broker.asset,
                state.totalValue - state.principalOutstanding,
                broker.params.managementFeeRate,
                state.loanScale));

        verifyLoanStatus(state);

        return state;
    }

    bool
    canImpairLoan(jtx::Env const& env, BrokerInfo const& broker, LoanState const& state)
    {
        if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            if (auto const vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                BEAST_EXPECT(vaultSle))
            {
                // log << vaultSle->getJson() << std::endl;
                auto const assetsUnavailable =
                    vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable);
                auto const unrealizedLoss = vaultSle->at(sfLossUnrealized) + state.totalValue -
                    state.managementFeeOutstanding;

                if (!BEAST_EXPECT(unrealizedLoss <= assetsUnavailable))
                {
                    return false;
                }
            }
        }
        return true;
    }

    enum class AssetType { XRP = 0, IOU = 1, MPT = 2 };

    // Specify the accounts as params to allow other accounts to be used
    jtx::PrettyAsset
    createAsset(
        jtx::Env& env,
        AssetType assetType,
        BrokerParameters const& brokerParams,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower)
    {
        using namespace jtx;

        switch (assetType)
        {
            case AssetType::XRP:
                // TODO: remove the factor, and set up loans in drops
                return PrettyAsset{xrpIssue(), 1'000'000};

            case AssetType::IOU: {
                PrettyAsset const asset{issuer[iouCurrency]};

                auto const limit =
                    asset(100 * (brokerParams.vaultDeposit + brokerParams.coverDeposit));
                if (lender != issuer)
                    env(trust(lender, limit));
                if (borrower != issuer)
                    env(trust(borrower, limit));

                return asset;
            }

            case AssetType::MPT: {
                // Enough to cover initial fees
                if (!env.le(keylet::account(issuer)))
                    env.fund(env.current()->fees().accountReserve(10) * 10, issuer);
                if (!env.le(keylet::account(lender)))
                    env.fund(env.current()->fees().accountReserve(10) * 10, noripple(lender));
                if (!env.le(keylet::account(borrower)))
                    env.fund(env.current()->fees().accountReserve(10) * 10, noripple(borrower));

                MPTTester mptt{env, issuer, mptInitNoFund};
                mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                // Scale the MPT asset so interest is interesting
                PrettyAsset const asset{mptt.issuanceID(), 10'000};
                // Need to do the authorization here because mptt isn't
                // accessible outside
                if (lender != issuer)
                    mptt.authorize({.account = lender});
                if (borrower != issuer)
                    mptt.authorize({.account = borrower});

                env.close();

                return asset;
            }

            default:
                throw std::runtime_error("Unknown asset type");
        }
    }

    void
    describeLoan(
        jtx::Env& env,
        BrokerParameters const& brokerParams,
        LoanParameters const& loanParams,
        AssetType assetType,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower)
    {
        using namespace jtx;

        auto const asset = createAsset(env, assetType, brokerParams, issuer, lender, borrower);
        auto const principal = asset(loanParams.principalRequest).number();
        auto const interest = loanParams.interest.value_or(TenthBips32{});
        auto const interval = loanParams.payInterval.value_or(LoanSet::defaultPaymentInterval);
        auto const total = loanParams.payTotal.value_or(LoanSet::defaultPaymentTotal);
        auto const feeRate = brokerParams.managementFeeRate;
        auto const props = computeLoanProperties(
            asset,
            principal,
            interest,
            interval,
            total,
            feeRate,
            asset(brokerParams.vaultDeposit).number().exponent());
        log << "Loan properties:\n"
            << "\tPrincipal: " << principal << std::endl
            << "\tInterest rate: " << interest << std::endl
            << "\tPayment interval: " << interval << std::endl
            << "\tManagement Fee Rate: " << feeRate << std::endl
            << "\tTotal Payments: " << total << std::endl
            << "\tPeriodic Payment: " << props.periodicPayment << std::endl
            << "\tTotal Value: " << props.loanState.valueOutstanding << std::endl
            << "\tManagement Fee: " << props.loanState.managementFeeDue << std::endl
            << "\tLoan Scale: " << props.loanScale << std::endl
            << "\tFirst payment principal: " << props.firstPaymentPrincipal << std::endl;

        // checkGuards returns a TER, so success is 0
        BEAST_EXPECT(!checkLoanGuards(
            asset,
            asset(loanParams.principalRequest).number(),
            loanParams.interest.value_or(TenthBips32{}) != beast::zero,
            loanParams.payTotal.value_or(LoanSet::defaultPaymentTotal),
            props,
            env.journal));
    }

    std::optional<std::tuple<BrokerInfo, Keylet, jtx::Account>>
    createLoan(
        jtx::Env& env,
        AssetType assetType,
        BrokerParameters const& brokerParams,
        LoanParameters const& loanParams,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower)
    {
        using namespace jtx;

        // Enough to cover initial fees
        env.fund(env.current()->fees().accountReserve(10) * 10, issuer);
        if (lender != issuer)
            env.fund(env.current()->fees().accountReserve(10) * 10, noripple(lender));
        if (borrower != issuer && borrower != lender)
            env.fund(env.current()->fees().accountReserve(10) * 10, noripple(borrower));

        describeLoan(env, brokerParams, loanParams, assetType, issuer, lender, borrower);

        // Make the asset
        auto const asset = createAsset(env, assetType, brokerParams, issuer, lender, borrower);

        env.close();
        if (asset.native() || lender != issuer)
        {
            env(
                pay((asset.native() ? env.master : issuer),
                    lender,
                    asset(brokerParams.vaultDeposit + brokerParams.coverDeposit)));
        }
        // Fund the borrower later once we know the total loan
        // size

        BrokerInfo const broker = createVaultAndBroker(env, asset, lender, brokerParams);

        auto const pseudoAcctOpt = [&]() -> std::optional<Account> {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::nullopt;
            auto const brokerPseudo = brokerSle->at(sfAccount);
            return Account("Broker pseudo-account", brokerPseudo);
        }();
        if (!pseudoAcctOpt)
            return std::nullopt;
        Account const& pseudoAcct = *pseudoAcctOpt;

        auto const loanKeyletOpt = [&]() -> std::optional<Keylet> {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::nullopt;

            // Broker has no loans
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);

            // The loan keylet is based on the LoanSequence of the
            // _LOAN_BROKER_ object.
            auto const loanSequence = brokerSle->at(sfLoanSequence);
            return keylet::loan(broker.brokerID, loanSequence);
        }();
        if (!loanKeyletOpt)
            return std::nullopt;
        Keylet const& loanKeylet = *loanKeyletOpt;

        env(loanParams(env, broker));

        env.close();

        return std::make_tuple(broker, loanKeylet, pseudoAcct);
    }

    static void
    topUpBorrower(
        jtx::Env& env,
        BrokerInfo const& broker,
        jtx::Account const& issuer,
        jtx::Account const& borrower,
        LoanState const& state,
        std::optional<Number> const& servFee)
    {
        using namespace jtx;

        STAmount const serviceFee = broker.asset(servFee.value_or(0));

        // Ensure the borrower has enough funds to make the payments
        // (including tx fees, if necessary)
        auto const borrowerBalance = env.balance(borrower, broker.asset);

        auto const baseFee = env.current()->fees().base;

        // Add extra for transaction fees and reserves, if appropriate, or a
        // tiny amount for the extra paid in each transaction
        auto const totalNeeded = state.totalValue + (serviceFee * state.paymentRemaining) +
            (broker.asset.native()
                 ? Number(
                       baseFee * state.paymentRemaining +
                       env.current()->fees().accountReserve(env.ownerCount(borrower)))
                 : broker.asset(15).number());

        auto const shortage = totalNeeded - borrowerBalance.number();

        if (shortage > beast::zero && (broker.asset.native() || issuer != borrower))
        {
            env(
                pay((broker.asset.native() ? env.master : issuer),
                    borrower,
                    STAmount{broker.asset, shortage}));
        }
    }

    void
    makeLoanPayments(
        jtx::Env& env,
        BrokerInfo const& broker,
        LoanParameters const& loanParams,
        Keylet const& loanKeylet,
        VerifyLoanStatus const& verifyLoanStatus,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower,
        PaymentParameters const& paymentParams = PaymentParameters::defaults())
    {
        // Make all the individual payments
        using namespace jtx;
        using namespace jtx::loan;
        using namespace std::chrono_literals;
        using d = NetClock::duration;

        bool const showStepBalances = paymentParams.showStepBalances;

        auto const currencyLabel = getCurrencyLabel(broker.asset);

        auto const baseFee = env.current()->fees().base;

        env.close();
        auto state = getCurrentState(env, broker, loanKeylet);

        verifyLoanStatus(state);

        STAmount const serviceFee = broker.asset(loanParams.serviceFee.value_or(0));

        topUpBorrower(env, broker, issuer, borrower, state, loanParams.serviceFee);

        // Periodic payment amount will consist of
        // 1. principal outstanding (1000)
        // 2. interest interest rate (at 12%)
        // 3. payment interval (600s)
        // 4. loan service fee (2)
        // Calculate these values without the helper functions
        // to verify they're working correctly The numbers in
        // the below BEAST_EXPECTs may not hold across assets.
        auto const periodicRate = loanPeriodicRate(state.interestRate, state.paymentInterval);
        STAmount const roundedPeriodicPayment{
            broker.asset,
            roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};

        if (!showStepBalances)
        {
            log << currencyLabel << " Payment components: "
                << "Payments remaining, "
                << "rawInterest, rawPrincipal, "
                   "rawMFee, "
                << "trackedValueDelta, trackedPrincipalDelta, "
                   "trackedInterestDelta, trackedMgmtFeeDelta, special"
                << std::endl;
        }

        // Include the service fee
        STAmount const totalDue =
            roundToScale(roundedPeriodicPayment + serviceFee, state.loanScale, Number::upward);

        auto currentRoundedState = constructLoanState(
            state.totalValue, state.principalOutstanding, state.managementFeeOutstanding);
        {
            auto const raw = computeTheoreticalLoanState(
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining,
                broker.params.managementFeeRate);

            if (showStepBalances)
            {
                log << currencyLabel << " Starting loan balances: "
                    << "\n\tTotal value: " << currentRoundedState.valueOutstanding
                    << "\n\tPrincipal: " << currentRoundedState.principalOutstanding
                    << "\n\tInterest: " << currentRoundedState.interestDue
                    << "\n\tMgmt fee: " << currentRoundedState.managementFeeDue
                    << "\n\tPayments remaining " << state.paymentRemaining << std::endl;
            }
            else
            {
                log << currencyLabel << " Loan starting state: " << state.paymentRemaining << ", "
                    << raw.interestDue << ", " << raw.principalOutstanding << ", "
                    << raw.managementFeeDue << ", " << currentRoundedState.valueOutstanding << ", "
                    << currentRoundedState.principalOutstanding << ", "
                    << currentRoundedState.interestDue << ", "
                    << currentRoundedState.managementFeeDue << std::endl;
            }
        }

        // Try to pay a little extra to show that it's _not_
        // taken
        auto const extraAmount = paymentParams.overpaymentExtra
            ? broker.asset(*paymentParams.overpaymentExtra).value()
            : std::min(broker.asset(10).value(), STAmount{broker.asset, totalDue / 20});

        STAmount const transactionAmount =
            STAmount{broker.asset, totalDue * paymentParams.overpaymentFactor} + extraAmount;

        auto const borrowerInitialBalance = env.balance(borrower, broker.asset).number();
        auto const initialState = state;
        detail::PaymentComponents totalPaid{
            .trackedValueDelta = 0, .trackedPrincipalDelta = 0, .trackedManagementFeeDelta = 0};
        Number totalInterestPaid = 0;
        Number totalFeesPaid = 0;
        std::size_t totalPaymentsMade = 0;

        xrpl::LoanState currentTrueState = computeTheoreticalLoanState(
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            broker.params.managementFeeRate);

        auto validateBorrowerBalance = [&]() {
            if (borrower == issuer || !paymentParams.validateBalances)
                return;
            auto const totalSpent =
                (totalPaid.trackedValueDelta + totalFeesPaid +
                 (broker.asset.native() ? Number(baseFee) * totalPaymentsMade : numZero));
            BEAST_EXPECT(
                env.balance(borrower, broker.asset).number() ==
                borrowerInitialBalance - totalSpent);
        };

        auto const defaultRound = broker.asset.integral() ? 3 : 0;
        auto truncate = [defaultRound](Number const& n, std::optional<int> places = std::nullopt) {
            auto const p = places.value_or(defaultRound);
            if (p == 0)
                return n;
            auto const factor = Number{1, p};
            return (n * factor).truncate() / factor;
        };
        while (state.paymentRemaining > 0)
        {
            validateBorrowerBalance();
            // Compute the expected principal amount
            auto const paymentComponents = detail::computePaymentComponents(
                broker.asset.raw(),
                state.loanScale,
                state.totalValue,
                state.principalOutstanding,
                state.managementFeeOutstanding,
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining,
                broker.params.managementFeeRate);

            BEAST_EXPECT(
                paymentComponents.trackedValueDelta <= roundedPeriodicPayment ||
                (paymentComponents.specialCase == detail::PaymentSpecialCase::final &&
                 paymentComponents.trackedValueDelta >= roundedPeriodicPayment));
            BEAST_EXPECT(
                paymentComponents.trackedValueDelta ==
                paymentComponents.trackedPrincipalDelta + paymentComponents.trackedInterestPart() +
                    paymentComponents.trackedManagementFeeDelta);

            xrpl::LoanState const nextTrueState = computeTheoreticalLoanState(
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining - 1,
                broker.params.managementFeeRate);
            detail::LoanStateDeltas const deltas = currentTrueState - nextTrueState;
            BEAST_EXPECT(
                deltas.total() == deltas.principal + deltas.interest + deltas.managementFee);
            BEAST_EXPECT(
                paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                deltas.total() == state.periodicPayment ||
                (state.loanScale - (deltas.total() - state.periodicPayment).exponent()) > 14);

            if (!showStepBalances)
            {
                log << currencyLabel << " Payment components: " << state.paymentRemaining << ", "

                    << deltas.interest << ", " << deltas.principal << ", " << deltas.managementFee
                    << ", " << paymentComponents.trackedValueDelta << ", "
                    << paymentComponents.trackedPrincipalDelta << ", "
                    << paymentComponents.trackedInterestPart() << ", "
                    << paymentComponents.trackedManagementFeeDelta << ", " << [&]() -> char const* {
                    if (paymentComponents.specialCase == detail::PaymentSpecialCase::final)
                        return "final";
                    if (paymentComponents.specialCase == detail::PaymentSpecialCase::extra)
                        return "extra";
                    return "none";
                }() << std::endl;
            }

            auto const totalDueAmount =
                STAmount{broker.asset, paymentComponents.trackedValueDelta + serviceFee};

            if (paymentParams.validateBalances)
            {
                // Due to the rounding algorithms to keep the interest and
                // principal in sync with "true" values, the computed amount
                // may be a little less than the rounded fixed payment
                // amount. For integral types, the difference should be < 3
                // (1 unit for each of the interest and management fee). For
                // IOUs, the difference should be dust.
                Number const diff = totalDue - totalDueAmount;
                BEAST_EXPECT(
                    paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                    diff == beast::zero ||
                    (diff > beast::zero &&
                     ((broker.asset.integral() && (static_cast<Number>(diff) < 3)) ||
                      (state.loanScale - diff.exponent() > 13))));

                BEAST_EXPECT(
                    paymentComponents.trackedPrincipalDelta >= beast::zero &&
                    paymentComponents.trackedPrincipalDelta <= state.principalOutstanding);
                BEAST_EXPECT(
                    paymentComponents.specialCase != detail::PaymentSpecialCase::final ||
                    paymentComponents.trackedPrincipalDelta == state.principalOutstanding);
            }

            auto const borrowerBalanceBeforePayment = env.balance(borrower, broker.asset);

            // Make the payment
            env(pay(borrower, loanKeylet.key, transactionAmount, paymentParams.flags));

            env.close(d{state.paymentInterval / 2});

            if (paymentParams.validateBalances)
            {
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
            }

            if (showStepBalances)
            {
                auto const loanSle = env.le(loanKeylet);
                if (!BEAST_EXPECT(loanSle))
                {
                    // No reason for this not to exist
                    return;
                }
                auto const current = constructRoundedLoanState(loanSle);
                auto const errors = nextTrueState - current;
                log << currencyLabel << " Loan balances: "
                    << "\n\tAmount taken: " << paymentComponents.trackedValueDelta
                    << "\n\tTotal value: " << current.valueOutstanding
                    << " (true: " << truncate(nextTrueState.valueOutstanding)
                    << ", error: " << truncate(errors.total())
                    << ")\n\tPrincipal: " << current.principalOutstanding
                    << " (true: " << truncate(nextTrueState.principalOutstanding)
                    << ", error: " << truncate(errors.principal)
                    << ")\n\tInterest: " << current.interestDue
                    << " (true: " << truncate(nextTrueState.interestDue)
                    << ", error: " << truncate(errors.interest)
                    << ")\n\tMgmt fee: " << current.managementFeeDue
                    << " (true: " << truncate(nextTrueState.managementFeeDue)
                    << ", error: " << truncate(errors.managementFee) << ")\n\tPayments remaining "
                    << loanSle->at(sfPaymentRemaining) << std::endl;

                currentRoundedState = current;
            }

            --state.paymentRemaining;
            state.previousPaymentDate = state.nextPaymentDate;
            if (paymentComponents.specialCase == detail::PaymentSpecialCase::final)
            {
                state.paymentRemaining = 0;
                state.nextPaymentDate = 0;
            }
            else
            {
                state.nextPaymentDate += state.paymentInterval;
            }
            state.principalOutstanding -= paymentComponents.trackedPrincipalDelta;
            state.managementFeeOutstanding -= paymentComponents.trackedManagementFeeDelta;
            state.totalValue -= paymentComponents.trackedValueDelta;

            if (paymentParams.validateBalances)
                verifyLoanStatus(state);

            totalPaid.trackedValueDelta += paymentComponents.trackedValueDelta;
            totalPaid.trackedPrincipalDelta += paymentComponents.trackedPrincipalDelta;
            totalPaid.trackedManagementFeeDelta += paymentComponents.trackedManagementFeeDelta;
            totalInterestPaid += paymentComponents.trackedInterestPart();
            totalFeesPaid += serviceFee;
            ++totalPaymentsMade;

            currentTrueState = nextTrueState;
        }
        validateBorrowerBalance();

        // Loan is paid off
        BEAST_EXPECT(state.paymentRemaining == 0);
        BEAST_EXPECT(state.principalOutstanding == 0);

        auto const initialInterestDue = initialState.totalValue -
            (initialState.principalOutstanding + initialState.managementFeeOutstanding);
        if (paymentParams.validateBalances)
        {
            // Make sure all the payments add up
            BEAST_EXPECT(totalPaid.trackedValueDelta == initialState.totalValue);
            BEAST_EXPECT(totalPaid.trackedPrincipalDelta == initialState.principalOutstanding);
            BEAST_EXPECT(
                totalPaid.trackedManagementFeeDelta == initialState.managementFeeOutstanding);
            // This is almost a tautology given the previous checks, but
            // check it anyway for completeness.
            BEAST_EXPECT(totalInterestPaid == initialInterestDue);
            BEAST_EXPECT(totalPaymentsMade == initialState.paymentRemaining);
        }

        if (showStepBalances)
        {
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
            {
                // No reason for this not to exist
                return;
            }
            log << currencyLabel << " Total amounts paid: "
                << "\n\tTotal value: " << totalPaid.trackedValueDelta
                << " (initial: " << truncate(initialState.totalValue)
                << ", error: " << truncate(initialState.totalValue - totalPaid.trackedValueDelta)
                << ")\n\tPrincipal: " << totalPaid.trackedPrincipalDelta
                << " (initial: " << truncate(initialState.principalOutstanding) << ", error: "
                << truncate(initialState.principalOutstanding - totalPaid.trackedPrincipalDelta)
                << ")\n\tInterest: " << totalInterestPaid
                << " (initial: " << truncate(initialInterestDue)
                << ", error: " << truncate(initialInterestDue - totalInterestPaid)
                << ")\n\tMgmt fee: " << totalPaid.trackedManagementFeeDelta
                << " (initial: " << truncate(initialState.managementFeeOutstanding) << ", error: "
                << truncate(
                       initialState.managementFeeOutstanding - totalPaid.trackedManagementFeeDelta)
                << ")\n\tTotal payments made: " << totalPaymentsMade << std::endl;
        }
    }

    void
    runLoan(
        AssetType assetType,
        BrokerParameters const& brokerParams,
        LoanParameters const& loanParams)
    {
        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        Env env(*this, all);

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);
        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            PaymentParameters{.showStepBalances = true});
    }

    static std::string
    getCurrencyLabel(Asset const& asset)
    {
        if (asset.native())
            return "XRP";
        if (asset.holds<Issue>())
            return "IOU";
        if (asset.holds<MPTIssue>())
            return "MPT";
        return "Unknown";
    }
};
}  // namespace xrpl::test
