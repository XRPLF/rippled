#pragma once

#include <xrpl/beast/unit_test/suite.h>
//
#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/transactors/Batch.h>
#include <xrpl/tx/transactors/Lending/LendingHelpers.h>
#include <xrpl/tx/transactors/Lending/LoanSet.h>

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
        std::string data{};
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
        std::optional<STAmount> setFee{};
        std::optional<Number> originationFee{};
        std::optional<Number> serviceFee{};
        std::optional<Number> lateFee{};
        std::optional<Number> closeFee{};
        std::optional<TenthBips32> overFee{};
        std::optional<TenthBips32> interest{};
        std::optional<TenthBips32> lateInterest{};
        std::optional<TenthBips32> closeInterest{};
        std::optional<TenthBips32> overpaymentInterest{};
        std::optional<std::uint32_t> payTotal{};
        std::optional<std::uint32_t> payInterval{};
        std::optional<std::uint32_t> gracePd{};
        std::optional<std::uint32_t> flags{};

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

            auto tx = getTransaction(env, broker, std::forward<FN>(fN)...);
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
                        if ((flags & lsfLoanImpaired) && !(flags & lsfLoanDefault))
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

        auto [vaultCreateTx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});

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
            env(
                pay((asset.native() ? env.master : issuer),
                    lender,
                    asset(brokerParams.vaultDeposit + brokerParams.coverDeposit)));
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

    void
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
            env(
                pay((broker.asset.native() ? env.master : issuer),
                    borrower,
                    STAmount{broker.asset, shortage}));
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
            log << currencyLabel << " Payment components: "
                << "Payments remaining, "
                << "rawInterest, rawPrincipal, "
                   "rawMFee, "
                << "trackedValueDelta, trackedPrincipalDelta, "
                   "trackedInterestDelta, trackedMgmtFeeDelta, special"
                << std::endl;

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
                log << currencyLabel << " Payment components: " << state.paymentRemaining << ", "

                    << deltas.interest << ", " << deltas.principal << ", " << deltas.managementFee
                    << ", " << paymentComponents.trackedValueDelta << ", "
                    << paymentComponents.trackedPrincipalDelta << ", "
                    << paymentComponents.trackedInterestPart() << ", "
                    << paymentComponents.trackedManagementFeeDelta << ", "
                    << (paymentComponents.specialCase == detail::PaymentSpecialCase::final ? "final"
                            : paymentComponents.specialCase == detail::PaymentSpecialCase::extra
                            ? "extra"
                            : "none")
                    << std::endl;

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
                    // No reason for this not to exist
                    return;
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
                // No reason for this not to exist
                return;
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
        if (!BEAST_EXPECT(loanResult))
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
        std::function<void(Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus)>
            toEndOfLife)
    {
        auto const [keylet, loanSequence] = [&]() {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                // will be invalid
                return std::make_pair(keylet::loan(broker.brokerID), std::uint32_t(0));

            // Broker has no loans
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);

            // The loan keylet is based on the LoanSequence of the _LOAN_BROKER_
            // object.
            auto const loanSequence = brokerSle->at(sfLoanSequence);
            return std::make_pair(keylet::loan(broker.brokerID, loanSequence), loanSequence);
        }();

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, keylet);

        // No loans yet
        verifyLoanStatus.checkBroker(0, 0, TenthBips32{0}, 1, 0, 0);

        if (!BEAST_EXPECT(loanSequence != 0))
            return;

        testcase << caseLabel << " " << label;

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        auto applyExponent = [interestExponent, this](TenthBips32 value) mutable {
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

        auto const borrowerOwnerCount = env.ownerCount(borrower);

        auto const loanSetFee = env.current()->fees().base * 2;
        LoanParameters const loanParams{
            .account = borrower,
            .counter = lender,
            .counterpartyExplicit = false,
            .principalRequest = loanAmount,
            .setFee = loanSetFee,
            .originationFee = 1,
            .serviceFee = 2,
            .lateFee = 3,
            .closeFee = 4,
            .overFee = applyExponent(percentageToTenthBips(5) / 10),
            .interest = applyExponent(percentageToTenthBips(12)),
            // 2.4%
            .lateInterest = applyExponent(percentageToTenthBips(24) / 10),
            .closeInterest = applyExponent(percentageToTenthBips(36) / 10),
            .overpaymentInterest = applyExponent(percentageToTenthBips(48) / 10),
            .payTotal = 12,
            .payInterval = 600,
            .gracePd = 60,
            .flags = flags,
        };
        Number const principalRequestAmount = broker.asset(loanParams.principalRequest).value();
        auto const originationFeeAmount = broker.asset(*loanParams.originationFee).value();
        auto const serviceFeeAmount = broker.asset(*loanParams.serviceFee).value();
        auto const lateFeeAmount = broker.asset(*loanParams.lateFee).value();
        auto const closeFeeAmount = broker.asset(*loanParams.closeFee).value();

        auto const borrowerStartbalance = env.balance(borrower, broker.asset);

        auto createJtx = loanParams(env, broker);
        // Successfully create a Loan
        env(createJtx);

        env.close();

        auto const startDate = env.current()->header().parentCloseTime.time_since_epoch().count();

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
                borrowerStartbalance.value() + principalRequestAmount - originationFeeAmount -
                    adjustment.value());
        }

        auto const loanFlags =
            createJtx.stx->isFlag(tfLoanOverpayment) ? lsfLoanOverpayment : LedgerSpecificFlags(0);

        if (auto loan = env.le(keylet); BEAST_EXPECT(loan))
        {
            // log << "loan after create: " << to_string(loan->getJson())
            //     << std::endl;
            BEAST_EXPECT(
                loan->isFlag(lsfLoanOverpayment) == createJtx.stx->isFlag(tfLoanOverpayment));
            BEAST_EXPECT(loan->at(sfLoanSequence) == loanSequence);
            BEAST_EXPECT(loan->at(sfBorrower) == borrower.id());
            BEAST_EXPECT(loan->at(sfLoanBrokerID) == broker.brokerID);
            BEAST_EXPECT(loan->at(sfLoanOriginationFee) == originationFeeAmount);
            BEAST_EXPECT(loan->at(sfLoanServiceFee) == serviceFeeAmount);
            BEAST_EXPECT(loan->at(sfLatePaymentFee) == lateFeeAmount);
            BEAST_EXPECT(loan->at(sfClosePaymentFee) == closeFeeAmount);
            BEAST_EXPECT(loan->at(sfOverpaymentFee) == *loanParams.overFee);
            BEAST_EXPECT(loan->at(sfInterestRate) == *loanParams.interest);
            BEAST_EXPECT(loan->at(sfLateInterestRate) == *loanParams.lateInterest);
            BEAST_EXPECT(loan->at(sfCloseInterestRate) == *loanParams.closeInterest);
            BEAST_EXPECT(loan->at(sfOverpaymentInterestRate) == *loanParams.overpaymentInterest);
            BEAST_EXPECT(loan->at(sfStartDate) == startDate);
            BEAST_EXPECT(loan->at(sfPaymentInterval) == *loanParams.payInterval);
            BEAST_EXPECT(loan->at(sfGracePeriod) == *loanParams.gracePd);
            BEAST_EXPECT(loan->at(sfPreviousPaymentDueDate) == 0);
            BEAST_EXPECT(loan->at(sfNextPaymentDueDate) == startDate + *loanParams.payInterval);
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == *loanParams.payTotal);
            BEAST_EXPECT(
                loan->at(sfLoanScale) >=
                (broker.asset.integral()
                     ? 0
                     : std::max(broker.vaultScale(env), principalRequestAmount.exponent())));
            BEAST_EXPECT(loan->at(sfPrincipalOutstanding) == principalRequestAmount);
        }

        auto state = getCurrentState(env, broker, keylet, verifyLoanStatus);

        auto const loanProperties = computeLoanProperties(
            broker.asset.raw(),
            state.principalOutstanding,
            state.interestRate,
            state.paymentInterval,
            state.paymentRemaining,
            broker.params.managementFeeRate,
            state.loanScale);

        verifyLoanStatus(
            0,
            startDate + *loanParams.payInterval,
            *loanParams.payTotal,
            state.loanScale,
            loanProperties.loanState.valueOutstanding,
            principalRequestAmount,
            loanProperties.loanState.managementFeeDue,
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
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanImpair), ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanImpair | tfLoanDefault), ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanDefault), ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanImpair | tfLoanDefault),
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

        auto const nextDueDate = startDate + *loanParams.payInterval;

        env.close();

        verifyLoanStatus(
            0,
            nextDueDate,
            *loanParams.payTotal,
            loanProperties.loanScale,
            loanProperties.loanState.valueOutstanding,
            principalRequestAmount,
            loanProperties.loanState.managementFeeDue,
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
        auto const borrowerStartingBalance = env.balance(borrower, broker.asset);

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
        verifyLoanStatus.checkBroker(0, 0, *loanParams.interest, 1, 0, 0);

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

    std::string
    getCurrencyLabel(Asset const& asset)
    {
        return (
            asset.native()                ? "XRP"
                : asset.holds<Issue>()    ? "IOU"
                : asset.holds<MPTIssue>() ? "MPT"
                                          : "Unknown");
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
        using namespace Lending;

        auto const& asset = broker.asset.raw();
        auto const currencyLabel = getCurrencyLabel(asset);
        auto const caseLabel = [&]() {
            std::stringstream ss;
            ss << "Lifecycle: " << loanAmount << " " << currencyLabel
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
        Number const maxCoveredLoanValue = broker.params.maxCoveredLoanValue(0);
        BEAST_EXPECT(maxCoveredLoanValue == 1000 * 100 / 10);
        Number const maxCoveredLoanRequest = broker.asset(maxCoveredLoanValue).value();
        Number const totalVaultRequest = broker.asset(broker.params.vaultDeposit).value();
        Number const debtMaximumRequest = broker.asset(broker.params.debtMax).value();

        auto const loanSetFee = fee(env.current()->fees().base * 2);

        auto const pseudoAcct = [&]() {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return lender;
            auto const brokerPseudo = brokerSle->at(sfAccount);
            return Account("Broker pseudo-account", brokerPseudo);
        }();

        auto const baseFee = env.current()->fees().base;

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
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            interestRate(TenthBips32(0)),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            interestRate(maxInterestRate + 1),
            loanSetFee,
            ter(temINVALID));
        // sfInterestRate: too small
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            interestRate(TenthBips32(-1)),
            loanSetFee,
            ter(temINVALID));

        // sfLateInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            lateInterestRate(maxLateInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            lateInterestRate(TenthBips32(0)),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfLateInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            lateInterestRate(maxLateInterestRate + 1),
            loanSetFee,
            ter(temINVALID));
        // sfLateInterestRate: too small
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            lateInterestRate(TenthBips32(-1)),
            loanSetFee,
            ter(temINVALID));

        // sfCloseInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            closeInterestRate(maxCloseInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            closeInterestRate(TenthBips32(0)),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfCloseInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            closeInterestRate(maxCloseInterestRate + 1),
            loanSetFee,
            ter(temINVALID));
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            closeInterestRate(TenthBips32(-1)),
            loanSetFee,
            ter(temINVALID));

        // sfOverpaymentInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            overpaymentInterestRate(maxOverpaymentInterestRate),
            loanSetFee,
            ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, borrower),
            overpaymentInterestRate(TenthBips32(0)),
            loanSetFee,
            ter(tefBAD_AUTH));
        // sfOverpaymentInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            overpaymentInterestRate(maxOverpaymentInterestRate + 1),
            loanSetFee,
            ter(temINVALID));
        env(set(evan, broker.brokerID, principalRequest),
            sig(sfCounterpartySignature, lender),
            overpaymentInterestRate(TenthBips32(-1)),
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
        env(signers(lender, 2, {{evan, 1}, {borrower, 1}}));
        env(signers(borrower, 2, {{evan, 1}, {lender, 1}}));
        env(set(borrower, broker.brokerID, principalRequest),
            counterparty(lender),
            msig(evan, lender),
            msig(sfCounterpartySignature, evan, borrower),
            fee(env.current()->fees().base * 5 - 1),
            ter(telINSUF_FEE_P));
        // Bad multisign signatures for borrower (Account)
        env(set(borrower, broker.brokerID, principalRequest),
            counterparty(lender),
            msig(alice, issuer),
            msig(sfCounterpartySignature, evan, borrower),
            fee(env.current()->fees().base * 5),
            ter(tefBAD_SIGNATURE));
        // Bad multisign signatures for issuer (Counterparty)
        env(set(borrower, broker.brokerID, principalRequest),
            counterparty(lender),
            msig(evan, lender),
            msig(sfCounterpartySignature, alice, issuer),
            fee(env.current()->fees().base * 5 - 1),
            ter(tefBAD_SIGNATURE));
        env(signers(lender, none));
        env(signers(borrower, none));
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
                auto const vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                if (!BEAST_EXPECT(vaultSle))
                    // This will be wrong, but the test has failed anyway.
                    return lender;
                auto const vaultPseudo = Account("Vault pseudo-account", vaultSle->at(sfAccount));
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
                        env(trust(issuer, holder[iouCurrency](0), tfSetFreeze | tfSetDeepFreeze));
                    };
                    auto unfreeze = [&](Account const& holder) {
                        env(trust(
                            issuer, holder[iouCurrency](0), tfClearFreeze | tfClearDeepFreeze));
                    };
                    return std::make_tuple(freeze, deepfreeze, unfreeze, tecFROZEN);
                }
                else
                {
                    auto freeze = [&](Account const& holder) {
                        mptt.set({.account = issuer, .holder = holder, .flags = tfMPTLock});
                    };
                    auto unfreeze = [&](Account const& holder) {
                        mptt.set({.account = issuer, .holder = holder, .flags = tfMPTUnlock});
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

                for (auto const& account : {// these accounts can't be frozen, which deep freeze
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

        auto coverAvailable = [&env, this](uint256 const& brokerID, Number const& expected) {
            if (auto const brokerSle = env.le(keylet::loanbroker(brokerID));
                BEAST_EXPECT(brokerSle))
            {
                auto const available = brokerSle->at(sfCoverAvailable);
                BEAST_EXPECT(available == expected);
                return available;
            }
            return Number{};
        };
        auto getDefaultInfo = [&env, this](LoanState const& state, BrokerInfo const& broker) {
            if (auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(
                    state.loanScale >=
                    (broker.asset.integral()
                         ? 0
                         : std::max(
                               broker.vaultScale(env), state.principalOutstanding.exponent())));
                NumberRoundModeGuard mg(Number::upward);
                auto const totalDefaultAmount = state.totalValue - state.managementFeeOutstanding;
                auto const defaultAmount = [&] {
                    if (env.enabled(fixLendingProtocolV1_1))
                    {
                        // New formula: DefaultCovered = min(DefaultAmount × CoverRateMinimum,
                        // CoverAvailable)
                        return roundToAsset(
                            broker.asset,
                            tenthBipsOfValue(totalDefaultAmount, broker.params.coverRateMin),
                            state.loanScale);
                    }
                    else
                    {
                        // Old formula from XLS-66 spec, section 3.2.3.2:
                        // DefaultCovered = min(DebtTotal × CoverRateMinimum × CoverRateLiquidation,
                        // DefaultAmount, CoverAvailable)
                        return roundToAsset(
                            broker.asset,
                            std::min(
                                tenthBipsOfValue(
                                    tenthBipsOfValue(
                                        brokerSle->at(sfDebtTotal), broker.params.coverRateMin),
                                    broker.params.coverRateLiquidation),
                                totalDefaultAmount),
                            state.loanScale);
                    }
                }();
                return std::make_pair(defaultAmount, brokerSle->at(sfOwner));
            }
            return std::make_pair(Number{}, AccountID{});
        };
        auto replenishCover = [&env, &coverAvailable](
                                  BrokerInfo const& broker,
                                  AccountID const& brokerAcct,
                                  Number const& startingCoverAvailable,
                                  Number const& amountToBeCovered) {
            coverAvailable(broker.brokerID, startingCoverAvailable - amountToBeCovered);
            env(loanBroker::coverDeposit(
                brokerAcct, broker.brokerID, STAmount{broker.asset, amountToBeCovered}));
            coverAvailable(broker.brokerID, startingCoverAvailable);
            env.close();
        };

        auto defaultImmediately = [&](std::uint32_t baseFlag, bool impair = true) {
            return [&, impair, baseFlag](
                       Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                // Default the loan

                // Initialize values with the current state
                auto state = getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                BEAST_EXPECT(state.flags == baseFlag);

                auto const& broker = verifyLoanStatus.broker;
                auto const startingCoverAvailable = coverAvailable(
                    broker.brokerID, broker.asset(broker.params.coverDeposit).number());

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
                        state.nextPaymentDate = env.now().time_since_epoch().count();

                        // Once the loan is impaired, it can't be impaired again
                        env(manage(lender, loanKeylet.key, tfLoanImpair), ter(tecNO_PERMISSION));
                    }
                    verifyLoanStatus(state);
                }

                auto const nextDueDate = tp{d{state.nextPaymentDate}};

                // Can't default the loan yet. The grace period hasn't
                // expired
                env(manage(lender, loanKeylet.key, tfLoanDefault), ter(tecTOO_SOON));

                // Let some time pass so that the loan can be
                // defaulted
                env.close(nextDueDate + 60s);

                auto const [amountToBeCovered, brokerAcct] = getDefaultInfo(state, broker);

                // Default the loan
                env(manage(lender, loanKeylet.key, tfLoanDefault));
                env.close();

                // The LoanBroker just lost some of it's first-loss capital.
                // Replenish it.
                replenishCover(broker, brokerAcct, startingCoverAvailable, amountToBeCovered);

                state.flags |= tfLoanDefault;
                state.paymentRemaining = 0;
                state.totalValue = 0;
                state.principalOutstanding = 0;
                state.managementFeeOutstanding = 0;
                state.nextPaymentDate = 0;
                verifyLoanStatus(state);

                // Once a loan is defaulted, it can't be managed
                env(manage(lender, loanKeylet.key, tfLoanUnimpair), ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanImpair), ter(tecNO_PERMISSION));
                // Can't make a payment on it either
                env(pay(borrower, loanKeylet.key, broker.asset(300)), ter(tecKILLED));
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
            env(pay(borrower, keylet::loan(uint256(0)).key, broker.asset(10), txFlags),
                ter(temINVALID));
            // broker.asset(80) is less than a single payment, but all these
            // checks fail before that matters
            env(pay(borrower, loanKeylet.key, broker.asset(-80), txFlags), ter(temBAD_AMOUNT));
            env(pay(borrower, broker.brokerID, broker.asset(80), txFlags), ter(tecNO_ENTRY));
            env(pay(evan, loanKeylet.key, broker.asset(80), txFlags), ter(tecNO_PERMISSION));

            // TODO: Write a general "isFlag" function? See STObject::isFlag.
            // Maybe add a static overloaded member?
            if (!(state.flags & lsfLoanOverpayment))
            {
                // If the loan does not allow overpayments, send a payment that
                // tries to make an overpayment. Do not include `txFlags`, so we
                // don't end up duplicating the next test transaction.
                env(pay(borrower,
                        loanKeylet.key,
                        STAmount{broker.asset, state.periodicPayment * Number{15, -1}},
                        tfLoanOverpayment),
                    fee(XRPAmount{baseFee * (Number{15, -1} / loanPaymentsPerFeeIncrement + 1)}),
                    ter(temINVALID_FLAG));
            }
            // Try to send a payment marked as multiple mutually exclusive
            // payment types. Do not include `txFlags`, so we don't duplicate
            // the prior test transaction.
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanFullPayment),
                ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanOverpayment),
                ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanOverpayment | tfLoanFullPayment),
                ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanOverpayment | tfLoanFullPayment),
                ter(temINVALID_FLAG));

            {
                auto const otherAsset =
                    broker.asset.raw() == assets[0].raw() ? assets[1] : assets[0];
                env(pay(borrower, loanKeylet.key, otherAsset(100), txFlags), ter(tecWRONG_ASSET));
            }

            // Amount doesn't cover a single payment
            env(pay(borrower, loanKeylet.key, STAmount{broker.asset, 1}, txFlags),
                ter(tecINSUFFICIENT_PAYMENT));

            // Get the balance after these failed transactions take
            // fees
            auto const borrowerBalanceBeforePayment = env.balance(borrower, broker.asset);

            BEAST_EXPECT(payoffAmount > state.principalOutstanding);
            // Try to pay a little extra to show that it's _not_
            // taken
            auto const transactionAmount = payoffAmount + broker.asset(10);

            // Send a transaction that tries to pay more than the borrowers's
            // balance
            XRPAmount const badFee{
                baseFee *
                (borrowerBalanceBeforePayment.number() * 2 / state.periodicPayment /
                     loanPaymentsPerFeeIncrement +
                 1)};
            env(pay(borrower,
                    loanKeylet.key,
                    STAmount{broker.asset, borrowerBalanceBeforePayment.number() * 2},
                    txFlags),
                fee(badFee),
                ter(tecINSUFFICIENT_FUNDS));

            XRPAmount const goodFee{baseFee * (numPayments / loanPaymentsPerFeeIncrement + 1)};
            env(pay(borrower, loanKeylet.key, transactionAmount, txFlags), fee(goodFee));

            env.close();

            // log << env.meta()->getJson() << std::endl;

            // Need to account for fees if the loan is in XRP
            PrettyAmount adjustment = broker.asset(0);
            if (broker.asset.native())
            {
                adjustment = badFee + goodFee;
            }

            state.paymentRemaining = 0;
            state.principalOutstanding = 0;
            state.totalValue = 0;
            state.managementFeeOutstanding = 0;
            state.previousPaymentDate =
                state.nextPaymentDate + state.paymentInterval * (numPayments - 1);
            state.nextPaymentDate = 0;
            verifyLoanStatus(state);

            verifyLoanStatus.checkPayment(
                state.loanScale, borrower, borrowerBalanceBeforePayment, payoffAmount, adjustment);

            // Can't impair or default a paid off loan
            env(manage(lender, loanKeylet.key, tfLoanImpair), ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanDefault), ter(tecNO_PERMISSION));
        };

        auto fullPayment = [&](std::uint32_t baseFlag) {
            return [&, baseFlag](
                       Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                auto state = getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
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
                auto const periodicRate = interval * Number(12, -2) / secondsInYear;
                BEAST_EXPECT(
                    periodicRate == Number(2283105022831050228ULL, -24, Number::normalized{}));
                STAmount const principalOutstanding{broker.asset, state.principalOutstanding};
                STAmount const accruedInterest{
                    broker.asset, state.principalOutstanding * periodicRate * loanAge / interval};
                BEAST_EXPECT(accruedInterest == broker.asset(Number(1141552511415525, -19)));
                STAmount const prepaymentPenalty{
                    broker.asset, state.principalOutstanding * Number(36, -3)};
                BEAST_EXPECT(prepaymentPenalty == broker.asset(36));
                STAmount const closePaymentFee = broker.asset(4);
                auto const payoffAmount = roundToScale(
                    principalOutstanding + accruedInterest + prepaymentPenalty + closePaymentFee,
                    state.loanScale);
                BEAST_EXPECT(
                    payoffAmount ==
                    roundToAsset(
                        broker.asset,
                        broker.asset(Number(1040000114155251, -12)).number(),
                        state.loanScale));

                // The terms of this loan actually make the early payoff
                // more expensive than just making payments
                BEAST_EXPECT(
                    payoffAmount >
                    state.paymentRemaining * (state.periodicPayment + broker.asset(2).value()));

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
            return
                [&, baseFlag](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) {
                    // toEndOfLife
                    //

                    auto state = getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
                    env.close();

                    BEAST_EXPECT(
                        STAmount(broker.asset, state.periodicPayment) ==
                        broker.asset(Number(8333457002039338267, -17)));

                    // Make all the payments in one transaction
                    // service fee is 2
                    auto const startingPayments = state.paymentRemaining;
                    STAmount const payoffAmount = [&]() {
                        NumberRoundModeGuard mg(Number::upward);
                        auto const rawPayoff =
                            startingPayments * (state.periodicPayment + broker.asset(2).value());
                        STAmount payoffAmount{broker.asset, rawPayoff};
                        BEAST_EXPECTS(
                            payoffAmount == broker.asset(Number(1024014840244721, -12)),
                            to_string(payoffAmount));
                        BEAST_EXPECT(payoffAmount > state.principalOutstanding);

                        payoffAmount = roundToScale(payoffAmount, state.loanScale);

                        return payoffAmount;
                    }();

                    auto const totalPayoffValue =
                        state.totalValue + startingPayments * broker.asset(2).value();
                    STAmount const totalPayoffAmount{broker.asset, totalPayoffValue};

                    BEAST_EXPECTS(
                        totalPayoffAmount == payoffAmount,
                        "Payoff amount: " + to_string(payoffAmount) +
                            ". Total Value: " + to_string(totalPayoffAmount));

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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) {
                // toEndOfLife
                //
                // Draw and make multiple payments
                auto state = getCurrentState(env, broker, loanKeylet, verifyLoanStatus);
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
                auto const periodicRate = interval * Number(12, -2) / secondsInYear;
                BEAST_EXPECT(
                    periodicRate == Number(2283105022831050228, -24, Number::normalized{}));
                STAmount const roundedPeriodicPayment{
                    broker.asset,
                    roundPeriodicPayment(broker.asset, state.periodicPayment, state.loanScale)};

                testcase << currencyLabel << " Payment components: "
                         << "Payments remaining, rawInterest, rawPrincipal, "
                            "rawMFee, trackedValueDelta, trackedPrincipalDelta, "
                            "trackedInterestDelta, trackedMgmtFeeDelta, special";

                auto const serviceFee = broker.asset(2);

                BEAST_EXPECT(
                    roundedPeriodicPayment ==
                    roundToScale(
                        broker.asset(Number(8333457002039338267, -17), Number::upward),
                        state.loanScale,
                        Number::upward));
                // 83334570.01162141
                // Include the service fee
                STAmount const totalDue = roundToScale(
                    roundedPeriodicPayment + serviceFee, state.loanScale, Number::upward);
                // Only check the first payment since the rounding
                // may drift as payments are made
                BEAST_EXPECT(
                    totalDue ==
                    roundToScale(
                        broker.asset(Number(8533457002039338267, -17), Number::upward),
                        state.loanScale,
                        Number::upward));

                {
                    auto const raw = computeTheoreticalLoanState(
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining,
                        broker.params.managementFeeRate);
                    auto const rounded = constructLoanState(
                        state.totalValue,
                        state.principalOutstanding,
                        state.managementFeeOutstanding);
                    testcase << currencyLabel << " Loan starting state: " << state.paymentRemaining
                             << ", " << raw.interestDue << ", " << raw.principalOutstanding << ", "
                             << raw.managementFeeDue << ", " << rounded.valueOutstanding << ", "
                             << rounded.principalOutstanding << ", " << rounded.interestDue << ", "
                             << rounded.managementFeeDue;
                }

                // Try to pay a little extra to show that it's _not_
                // taken
                STAmount const transactionAmount =
                    STAmount{broker.asset, totalDue} + broker.asset(10);
                // Only check the first payment since the rounding
                // may drift as payments are made
                BEAST_EXPECT(
                    transactionAmount ==
                    roundToScale(
                        broker.asset(Number(9533457002039400, -14), Number::upward),
                        state.loanScale,
                        Number::upward));

                auto const initialState = state;
                detail::PaymentComponents totalPaid{
                    .trackedValueDelta = 0,
                    .trackedPrincipalDelta = 0,
                    .trackedManagementFeeDelta = 0};
                Number totalInterestPaid = 0;
                std::size_t totalPaymentsMade = 0;

                xrpl::LoanState currentTrueState = computeTheoreticalLoanState(
                    state.periodicPayment,
                    periodicRate,
                    state.paymentRemaining,
                    broker.params.managementFeeRate);

                while (state.paymentRemaining > 0)
                {
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

                    BEAST_EXPECTS(
                        paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                            paymentComponents.trackedValueDelta <= roundedPeriodicPayment,
                        "Delta: " + to_string(paymentComponents.trackedValueDelta) +
                            ", periodic payment: " + to_string(roundedPeriodicPayment));

                    xrpl::LoanState const nextTrueState = computeTheoreticalLoanState(
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining - 1,
                        broker.params.managementFeeRate);
                    detail::LoanStateDeltas const deltas = currentTrueState - nextTrueState;

                    testcase << currencyLabel << " Payment components: " << state.paymentRemaining
                             << ", " << deltas.interest << ", " << deltas.principal << ", "
                             << deltas.managementFee << ", " << paymentComponents.trackedValueDelta
                             << ", " << paymentComponents.trackedPrincipalDelta << ", "
                             << paymentComponents.trackedInterestPart() << ", "
                             << paymentComponents.trackedManagementFeeDelta << ", "
                             << (paymentComponents.specialCase == detail::PaymentSpecialCase::final
                                     ? "final"
                                     : paymentComponents.specialCase ==
                                         detail::PaymentSpecialCase::extra
                                     ? "extra"
                                     : "none");

                    auto const totalDueAmount = STAmount{
                        broker.asset, paymentComponents.trackedValueDelta + serviceFee.number()};

                    // Due to the rounding algorithms to keep the interest and
                    // principal in sync with "true" values, the computed amount
                    // may be a little less than the rounded fixed payment
                    // amount. For integral types, the difference should be < 3
                    // (1 unit for each of the interest and management fee). For
                    // IOUs, the difference should be after the 8th digit.
                    Number const diff = totalDue - totalDueAmount;
                    BEAST_EXPECT(
                        paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                        diff == beast::zero ||
                        (diff > beast::zero &&
                         ((broker.asset.integral() && (static_cast<Number>(diff) < 3)) ||
                          (state.loanScale - diff.exponent() > 13))));

                    BEAST_EXPECT(
                        paymentComponents.trackedValueDelta ==
                        paymentComponents.trackedPrincipalDelta +
                            paymentComponents.trackedInterestPart() +
                            paymentComponents.trackedManagementFeeDelta);
                    BEAST_EXPECT(
                        paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                        paymentComponents.trackedValueDelta <= roundedPeriodicPayment);

                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToAsset(
                            broker.asset, deltas.principal, state.loanScale, Number::upward) ==
                            roundToScale(
                                broker.asset(Number(8333228691531218890, -17), Number::upward),
                                state.loanScale,
                                Number::upward));
                    BEAST_EXPECT(
                        paymentComponents.trackedPrincipalDelta >= beast::zero &&
                        paymentComponents.trackedPrincipalDelta <= state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase != detail::PaymentSpecialCase::final ||
                        paymentComponents.trackedPrincipalDelta == state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase == detail::PaymentSpecialCase::final ||
                        (state.periodicPayment.exponent() -
                         (deltas.principal + deltas.interest + deltas.managementFee -
                          state.periodicPayment)
                             .exponent()) > 14);

                    auto const borrowerBalanceBeforePayment = env.balance(borrower, broker.asset);

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

                    verifyLoanStatus(state);

                    totalPaid.trackedValueDelta += paymentComponents.trackedValueDelta;
                    totalPaid.trackedPrincipalDelta += paymentComponents.trackedPrincipalDelta;
                    totalPaid.trackedManagementFeeDelta +=
                        paymentComponents.trackedManagementFeeDelta;
                    totalInterestPaid += paymentComponents.trackedInterestPart();
                    ++totalPaymentsMade;

                    currentTrueState = nextTrueState;
                }

                // Loan is paid off
                BEAST_EXPECT(state.paymentRemaining == 0);
                BEAST_EXPECT(state.principalOutstanding == 0);

                // Make sure all the payments add up
                BEAST_EXPECT(totalPaid.trackedValueDelta == initialState.totalValue);
                BEAST_EXPECT(totalPaid.trackedPrincipalDelta == initialState.principalOutstanding);
                BEAST_EXPECT(
                    totalPaid.trackedManagementFeeDelta == initialState.managementFeeOutstanding);
                // This is almost a tautology given the previous checks, but
                // check it anyway for completeness.
                BEAST_EXPECT(
                    totalInterestPaid ==
                    initialState.totalValue -
                        (initialState.principalOutstanding +
                         initialState.managementFeeOutstanding));
                BEAST_EXPECT(totalPaymentsMade == initialState.paymentRemaining);

                // Can't impair or default a paid off loan
                env(manage(lender, loanKeylet.key, tfLoanImpair), ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanDefault), ter(tecNO_PERMISSION));
            });
    }
};
}  // namespace xrpl::test
