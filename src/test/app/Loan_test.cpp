#include <xrpl/beast/unit_test/suite.h>
//
#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/tx/detail/Batch.h>
#include <xrpld/app/tx/detail/LoanSet.h>

#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/protocol/SField.h>

namespace ripple {
namespace test {

class Loan_test : public beast::unit_test::suite
{
protected:
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    FeatureBitset const all{
        jtx::testable_amendments() | featureMPTokensV1 |
        featureSingleAssetVault | featureLendingProtocol};

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
            auto debtLimit =
                coverDeposit * tenthBipsPerUnity.value() / coverRateMin.value();

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
            : asset(asset_)
            , brokerID(brokerKeylet_.key)
            , vaultID(vaultKeylet_.key)
            , params(p)
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
            return getVaultScale(vaultSle);
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

        template <class... FN>
        jtx::JTx
        operator()(jtx::Env& env, BrokerInfo const& broker, FN const&... fN)
            const
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
                loanOriginationFee(broker.asset(*originationFee).number())(
                    env, jt);
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

            return env.jt(jt, fN...);
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
            : env(env_)
            , broker(broker_)
            , pseudoAccount(pseudo_)
            , loanKeylet(keylet_)
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
            if (auto loan = env.le(loanKeylet); env.test.BEAST_EXPECT(loan))
            {
                env.test.BEAST_EXPECT(
                    loan->at(sfPreviousPaymentDate) == previousPaymentDate);
                env.test.BEAST_EXPECT(
                    loan->at(sfPaymentRemaining) == paymentRemaining);
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
        BrokerParameters const& params = BrokerParameters::defaults())
    {
        using namespace jtx;

        Vault vault{env};

        auto const deposit = asset(params.vaultDeposit);
        auto const debtMaximumValue = asset(params.debtMax).value();
        auto const coverDepositValue = asset(params.coverDeposit).value();

        auto const coverRateMinValue = params.coverRateMin;

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

        using namespace loanBroker;
        env(set(lender, vaultKeylet.key, params.flags),
            data(params.data),
            managementFeeRate(params.managementFeeRate),
            debtMaximum(debtMaximumValue),
            coverRateMinimum(coverRateMinValue),
            coverRateLiquidation(TenthBips32(params.coverRateLiquidation)));

        if (coverDepositValue != beast::zero)
            env(coverDeposit(lender, keylet.key, coverDepositValue));

        env.close();

        return {asset, keylet, vaultKeylet, params};
    }

    /// Get the state without checking anything
    LoanState
    getCurrentState(
        jtx::Env const& env,
        BrokerInfo const& broker,
        Keylet const& loanKeylet)
    {
        using d = NetClock::duration;
        using tp = NetClock::time_point;

        // Lookup the current loan state
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            return LoanState{
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
                 : std::max(
                       broker.vaultScale(env),
                       state.principalOutstanding.exponent())));
        BEAST_EXPECT(state.paymentInterval == 600);
        BEAST_EXPECT(
            state.totalValue ==
            roundToAsset(
                broker.asset,
                state.periodicPayment * state.paymentRemaining,
                state.loanScale));
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

                auto const limit = asset(
                    100 *
                    (brokerParams.vaultDeposit + brokerParams.coverDeposit));
                if (lender != issuer)
                    env(trust(lender, limit));
                if (borrower != issuer)
                    env(trust(borrower, limit));

                return asset;
            }

            case AssetType::MPT: {
                // Enough to cover initial fees
                if (!env.le(keylet::account(issuer)))
                    env.fund(
                        env.current()->fees().accountReserve(10) * 10, issuer);
                if (!env.le(keylet::account(lender)))
                    env.fund(
                        env.current()->fees().accountReserve(10) * 10,
                        noripple(lender));
                if (!env.le(keylet::account(borrower)))
                    env.fund(
                        env.current()->fees().accountReserve(10) * 10,
                        noripple(borrower));

                MPTTester mptt{env, issuer, mptInitNoFund};
                mptt.create(
                    {.flags =
                         tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
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

        auto const asset =
            createAsset(env, assetType, brokerParams, issuer, lender, borrower);
        auto const principal = asset(loanParams.principalRequest).number();
        auto const interest = loanParams.interest.value_or(TenthBips32{});
        auto const interval =
            loanParams.payInterval.value_or(LoanSet::defaultPaymentInterval);
        auto const total =
            loanParams.payTotal.value_or(LoanSet::defaultPaymentTotal);
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
            << "\tTotal Value: " << props.totalValueOutstanding << std::endl
            << "\tManagement Fee: " << props.managementFeeOwedToBroker
            << std::endl
            << "\tLoan Scale: " << props.loanScale << std::endl
            << "\tFirst payment principal: " << props.firstPaymentPrincipal
            << std::endl;

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
            env.fund(
                env.current()->fees().accountReserve(10) * 10,
                noripple(lender));
        if (borrower != issuer && borrower != lender)
            env.fund(
                env.current()->fees().accountReserve(10) * 10,
                noripple(borrower));

        describeLoan(
            env, brokerParams, loanParams, assetType, issuer, lender, borrower);

        // Make the asset
        auto const asset =
            createAsset(env, assetType, brokerParams, issuer, lender, borrower);

        env.close();
        if (asset.native() || lender != issuer)
            env(pay(
                (asset.native() ? env.master : issuer),
                lender,
                asset(brokerParams.vaultDeposit + brokerParams.coverDeposit)));
        // Fund the borrower later once we know the total loan
        // size

        BrokerInfo const broker =
            createVaultAndBroker(env, asset, lender, brokerParams);

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
        auto const totalNeeded = state.totalValue +
            (serviceFee * state.paymentRemaining) +
            (broker.asset.native() ? Number(
                                         baseFee * state.paymentRemaining +
                                         env.current()->fees().accountReserve(
                                             env.ownerCount(borrower)))
                                   : broker.asset(15).number());

        auto const shortage = totalNeeded - borrowerBalance.number();

        if (shortage > beast::zero &&
            (broker.asset.native() || issuer != borrower))
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

        // Account const evan{"evan"};
        // Account const alice{"alice"};

        bool const showStepBalances = paymentParams.showStepBalances;

        auto const currencyLabel = getCurrencyLabel(broker.asset);

        auto const baseFee = env.current()->fees().base;

        env.close();
        auto state = getCurrentState(env, broker, loanKeylet);

        verifyLoanStatus(state);

        STAmount const serviceFee =
            broker.asset(loanParams.serviceFee.value_or(0));

        topUpBorrower(
            env, broker, issuer, borrower, state, loanParams.serviceFee);

        // Periodic payment amount will consist of
        // 1. principal outstanding (1000)
        // 2. interest interest rate (at 12%)
        // 3. payment interval (600s)
        // 4. loan service fee (2)
        // Calculate these values without the helper functions
        // to verify they're working correctly The numbers in
        // the below BEAST_EXPECTs may not hold across assets.
        auto const periodicRate =
            loanPeriodicRate(state.interestRate, state.paymentInterval);
        STAmount const roundedPeriodicPayment{
            broker.asset,
            roundPeriodicPayment(
                broker.asset, state.periodicPayment, state.loanScale)};

        if (!showStepBalances)
            log << currencyLabel << " Payment components: "
                << "Payments remaining, "
                << "rawInterest, rawPrincipal, "
                   "rawMFee, "
                << "trackedValueDelta, trackedPrincipalDelta, "
                   "trackedInterestDelta, trackedMgmtFeeDelta, special"
                << std::endl;

        // Include the service fee
        STAmount const totalDue = roundToScale(
            roundedPeriodicPayment + serviceFee,
            state.loanScale,
            Number::upward);

        auto currentRoundedState = constructLoanState(
            state.totalValue,
            state.principalOutstanding,
            state.managementFeeOutstanding);
        {
            auto const raw = computeRawLoanState(
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining,
                broker.params.managementFeeRate);

            if (showStepBalances)
            {
                log << currencyLabel << " Starting loan balances: "
                    << "\n\tTotal value: "
                    << currentRoundedState.valueOutstanding << "\n\tPrincipal: "
                    << currentRoundedState.principalOutstanding
                    << "\n\tInterest: " << currentRoundedState.interestDue
                    << "\n\tMgmt fee: " << currentRoundedState.managementFeeDue
                    << "\n\tPayments remaining " << state.paymentRemaining
                    << std::endl;
            }
            else
            {
                log << currencyLabel
                    << " Loan starting state: " << state.paymentRemaining
                    << ", " << raw.interestDue << ", "
                    << raw.principalOutstanding << ", " << raw.managementFeeDue
                    << ", " << currentRoundedState.valueOutstanding << ", "
                    << currentRoundedState.principalOutstanding << ", "
                    << currentRoundedState.interestDue << ", "
                    << currentRoundedState.managementFeeDue << std::endl;
            }
        }

        // Try to pay a little extra to show that it's _not_
        // taken
        auto const extraAmount = paymentParams.overpaymentExtra
            ? broker.asset(*paymentParams.overpaymentExtra).value()
            : std::min(
                  broker.asset(10).value(),
                  STAmount{broker.asset, totalDue / 20});

        STAmount const transactionAmount =
            STAmount{broker.asset, totalDue * paymentParams.overpaymentFactor} +
            extraAmount;

        auto const borrowerInitialBalance =
            env.balance(borrower, broker.asset).number();
        auto const initialState = state;
        detail::PaymentComponents totalPaid{
            .trackedValueDelta = 0,
            .trackedPrincipalDelta = 0,
            .trackedManagementFeeDelta = 0};
        Number totalInterestPaid = 0;
        Number totalFeesPaid = 0;
        std::size_t totalPaymentsMade = 0;

        ripple::LoanState currentTrueState = computeRawLoanState(
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            broker.params.managementFeeRate);

        auto validateBorrowerBalance = [&]() {
            if (borrower == issuer || !paymentParams.validateBalances)
                return;
            auto const totalSpent =
                (totalPaid.trackedValueDelta + totalFeesPaid +
                 (broker.asset.native() ? Number(baseFee) * totalPaymentsMade
                                        : numZero));
            BEAST_EXPECT(
                env.balance(borrower, broker.asset).number() ==
                borrowerInitialBalance - totalSpent);
        };

        auto const defaultRound = broker.asset.integral() ? 3 : 0;
        auto truncate = [defaultRound](
                            Number const& n,
                            std::optional<int> places = std::nullopt) {
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
                (paymentComponents.specialCase ==
                     detail::PaymentSpecialCase::final &&
                 paymentComponents.trackedValueDelta >=
                     roundedPeriodicPayment));
            BEAST_EXPECT(
                paymentComponents.trackedValueDelta ==
                paymentComponents.trackedPrincipalDelta +
                    paymentComponents.trackedInterestPart() +
                    paymentComponents.trackedManagementFeeDelta);

            ripple::LoanState const nextTrueState = computeRawLoanState(
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining - 1,
                broker.params.managementFeeRate);
            detail::LoanStateDeltas const deltas =
                currentTrueState - nextTrueState;
            BEAST_EXPECT(
                deltas.total() ==
                deltas.principal + deltas.interest + deltas.managementFee);
            BEAST_EXPECT(
                paymentComponents.specialCase ==
                    detail::PaymentSpecialCase::final ||
                deltas.total() == state.periodicPayment ||
                (state.loanScale -
                 (deltas.total() - state.periodicPayment).exponent()) > 14);

            if (!showStepBalances)
                log << currencyLabel
                    << " Payment components: " << state.paymentRemaining << ", "

                    << deltas.interest << ", " << deltas.principal << ", "
                    << deltas.managementFee << ", "
                    << paymentComponents.trackedValueDelta << ", "
                    << paymentComponents.trackedPrincipalDelta << ", "
                    << paymentComponents.trackedInterestPart() << ", "
                    << paymentComponents.trackedManagementFeeDelta << ", "
                    << (paymentComponents.specialCase ==
                                detail::PaymentSpecialCase::final
                            ? "final"
                            : paymentComponents.specialCase ==
                                detail::PaymentSpecialCase::extra
                            ? "extra"
                            : "none")
                    << std::endl;

            auto const totalDueAmount = STAmount{
                broker.asset, paymentComponents.trackedValueDelta + serviceFee};

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
                    paymentComponents.specialCase ==
                        detail::PaymentSpecialCase::final ||
                    diff == beast::zero ||
                    (diff > beast::zero &&
                     ((broker.asset.integral() &&
                       (static_cast<Number>(diff) < 3)) ||
                      (state.loanScale - diff.exponent() > 13))));

                BEAST_EXPECT(
                    paymentComponents.trackedPrincipalDelta >= beast::zero &&
                    paymentComponents.trackedPrincipalDelta <=
                        state.principalOutstanding);
                BEAST_EXPECT(
                    paymentComponents.specialCase !=
                        detail::PaymentSpecialCase::final ||
                    paymentComponents.trackedPrincipalDelta ==
                        state.principalOutstanding);
            }

            auto const borrowerBalanceBeforePayment =
                env.balance(borrower, broker.asset);

            // Make the payment
            env(
                pay(borrower,
                    loanKeylet.key,
                    transactionAmount,
                    paymentParams.flags));

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
                    << "\n\tAmount taken: "
                    << paymentComponents.trackedValueDelta
                    << "\n\tTotal value: " << current.valueOutstanding
                    << " (true: " << truncate(nextTrueState.valueOutstanding)
                    << ", error: " << truncate(errors.total())
                    << ")\n\tPrincipal: " << current.principalOutstanding
                    << " (true: "
                    << truncate(nextTrueState.principalOutstanding)
                    << ", error: " << truncate(errors.principal)
                    << ")\n\tInterest: " << current.interestDue
                    << " (true: " << truncate(nextTrueState.interestDue)
                    << ", error: " << truncate(errors.interest)
                    << ")\n\tMgmt fee: " << current.managementFeeDue
                    << " (true: " << truncate(nextTrueState.managementFeeDue)
                    << ", error: " << truncate(errors.managementFee)
                    << ")\n\tPayments remaining "
                    << loanSle->at(sfPaymentRemaining) << std::endl;

                currentRoundedState = current;
            }

            --state.paymentRemaining;
            state.previousPaymentDate = state.nextPaymentDate;
            if (paymentComponents.specialCase ==
                detail::PaymentSpecialCase::final)
            {
                state.paymentRemaining = 0;
                state.nextPaymentDate = 0;
            }
            else
            {
                state.nextPaymentDate += state.paymentInterval;
            }
            state.principalOutstanding -=
                paymentComponents.trackedPrincipalDelta;
            state.managementFeeOutstanding -=
                paymentComponents.trackedManagementFeeDelta;
            state.totalValue -= paymentComponents.trackedValueDelta;

            if (paymentParams.validateBalances)
                verifyLoanStatus(state);

            totalPaid.trackedValueDelta += paymentComponents.trackedValueDelta;
            totalPaid.trackedPrincipalDelta +=
                paymentComponents.trackedPrincipalDelta;
            totalPaid.trackedManagementFeeDelta +=
                paymentComponents.trackedManagementFeeDelta;
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
            (initialState.principalOutstanding +
             initialState.managementFeeOutstanding);
        if (paymentParams.validateBalances)
        {
            // Make sure all the payments add up
            BEAST_EXPECT(
                totalPaid.trackedValueDelta == initialState.totalValue);
            BEAST_EXPECT(
                totalPaid.trackedPrincipalDelta ==
                initialState.principalOutstanding);
            BEAST_EXPECT(
                totalPaid.trackedManagementFeeDelta ==
                initialState.managementFeeOutstanding);
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
                << ", error: "
                << truncate(
                       initialState.totalValue - totalPaid.trackedValueDelta)
                << ")\n\tPrincipal: " << totalPaid.trackedPrincipalDelta
                << " (initial: " << truncate(initialState.principalOutstanding)
                << ", error: "
                << truncate(
                       initialState.principalOutstanding -
                       totalPaid.trackedPrincipalDelta)
                << ")\n\tInterest: " << totalInterestPaid
                << " (initial: " << truncate(initialInterestDue) << ", error: "
                << truncate(initialInterestDue - totalInterestPaid)
                << ")\n\tMgmt fee: " << totalPaid.trackedManagementFeeDelta
                << " (initial: "
                << truncate(initialState.managementFeeOutstanding)
                << ", error: "
                << truncate(
                       initialState.managementFeeOutstanding -
                       totalPaid.trackedManagementFeeDelta)
                << ")\n\tTotal payments made: " << totalPaymentsMade
                << std::endl;
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

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, borrower);
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
            borrower);
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
            env, broker, pseudoAcct, keylet);

        // No loans yet
        verifyLoanStatus.checkBroker(0, 0, TenthBips32{0}, 1, 0, 0);

        if (!BEAST_EXPECT(loanSequence != 0))
            return;

        testcase << caseLabel << " " << label;

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

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
            .overpaymentInterest =
                applyExponent(percentageToTenthBips(48) / 10),
            .payTotal = 12,
            .payInterval = 600,
            .gracePd = 60,
            .flags = flags,
        };
        Number const principalRequestAmount =
            broker.asset(loanParams.principalRequest).value();
        auto const originationFeeAmount =
            broker.asset(*loanParams.originationFee).value();
        auto const serviceFeeAmount =
            broker.asset(*loanParams.serviceFee).value();
        auto const lateFeeAmount = broker.asset(*loanParams.lateFee).value();
        auto const closeFeeAmount = broker.asset(*loanParams.closeFee).value();

        auto const borrowerStartbalance = env.balance(borrower, broker.asset);

        auto createJtx = loanParams(env, broker);
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
                borrowerStartbalance.value() + principalRequestAmount -
                    originationFeeAmount - adjustment.value());
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
            BEAST_EXPECT(
                loan->at(sfLoanOriginationFee) == originationFeeAmount);
            BEAST_EXPECT(loan->at(sfLoanServiceFee) == serviceFeeAmount);
            BEAST_EXPECT(loan->at(sfLatePaymentFee) == lateFeeAmount);
            BEAST_EXPECT(loan->at(sfClosePaymentFee) == closeFeeAmount);
            BEAST_EXPECT(loan->at(sfOverpaymentFee) == *loanParams.overFee);
            BEAST_EXPECT(loan->at(sfInterestRate) == *loanParams.interest);
            BEAST_EXPECT(
                loan->at(sfLateInterestRate) == *loanParams.lateInterest);
            BEAST_EXPECT(
                loan->at(sfCloseInterestRate) == *loanParams.closeInterest);
            BEAST_EXPECT(
                loan->at(sfOverpaymentInterestRate) ==
                *loanParams.overpaymentInterest);
            BEAST_EXPECT(loan->at(sfStartDate) == startDate);
            BEAST_EXPECT(
                loan->at(sfPaymentInterval) == *loanParams.payInterval);
            BEAST_EXPECT(loan->at(sfGracePeriod) == *loanParams.gracePd);
            BEAST_EXPECT(loan->at(sfPreviousPaymentDate) == 0);
            BEAST_EXPECT(
                loan->at(sfNextPaymentDueDate) ==
                startDate + *loanParams.payInterval);
            BEAST_EXPECT(loan->at(sfPaymentRemaining) == *loanParams.payTotal);
            BEAST_EXPECT(
                loan->at(sfLoanScale) >=
                (broker.asset.integral()
                     ? 0
                     : std::max(
                           broker.vaultScale(env),
                           principalRequestAmount.exponent())));
            BEAST_EXPECT(
                loan->at(sfPrincipalOutstanding) == principalRequestAmount);
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
            loanProperties.totalValueOutstanding,
            principalRequestAmount,
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

        auto const nextDueDate = startDate + *loanParams.payInterval;

        env.close();

        verifyLoanStatus(
            0,
            nextDueDate,
            *loanParams.payTotal,
            loanProperties.loanScale,
            loanProperties.totalValueOutstanding,
            principalRequestAmount,
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
        Number const maxCoveredLoanRequest =
            broker.asset(maxCoveredLoanValue).value();
        Number const totalVaultRequest =
            broker.asset(broker.params.vaultDeposit).value();
        Number const debtMaximumRequest =
            broker.asset(broker.params.debtMax).value();

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
                    state.loanScale >=
                    (broker.asset.integral()
                         ? 0
                         : std::max(
                               broker.vaultScale(env),
                               state.principalOutstanding.exponent())));
                NumberRoundModeGuard mg(Number::upward);
                auto const defaultAmount = roundToAsset(
                    broker.asset,
                    std::min(
                        tenthBipsOfValue(
                            tenthBipsOfValue(
                                brokerSle->at(sfDebtTotal),
                                broker.params.coverRateMin),
                            broker.params.coverRateLiquidation),
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
                    broker.asset(broker.params.coverDeposit).number());

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
                state.nextPaymentDate = 0;
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
            // broker.asset(80) is less than a single payment, but all these
            // checks fail before that matters
            env(pay(borrower, loanKeylet.key, broker.asset(-80), txFlags),
                ter(temBAD_AMOUNT));
            env(pay(borrower, broker.brokerID, broker.asset(80), txFlags),
                ter(tecNO_ENTRY));
            env(pay(evan, loanKeylet.key, broker.asset(80), txFlags),
                ter(tecNO_PERMISSION));

            // TODO: Write a general "isFlag" function? See STObject::isFlag.
            // Maybe add a static overloaded member?
            if (!(state.flags & lsfLoanOverpayment))
            {
                // If the loan does not allow overpayments, send a payment that
                // tries to make an overpayment. Do not include `txFlags`, so we
                // don't end up duplicating the next test transaction.
                env(pay(borrower,
                        loanKeylet.key,
                        STAmount{
                            broker.asset,
                            state.periodicPayment * Number{15, -1}},
                        tfLoanOverpayment),
                    fee(XRPAmount{
                        baseFee *
                        (Number{15, -1} / loanPaymentsPerFeeIncrement + 1)}),
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
            XRPAmount const badFee{
                baseFee *
                (borrowerBalanceBeforePayment.number() * 2 /
                     state.periodicPayment / loanPaymentsPerFeeIncrement +
                 1)};
            env(pay(borrower,
                    loanKeylet.key,
                    STAmount{
                        broker.asset,
                        borrowerBalanceBeforePayment.number() * 2},
                    txFlags),
                fee(badFee),
                ter(tecINSUFFICIENT_FUNDS));

            XRPAmount const goodFee{
                baseFee * (numPayments / loanPaymentsPerFeeIncrement + 1)};
            env(pay(borrower, loanKeylet.key, transactionAmount, txFlags),
                fee(goodFee));

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
            state.previousPaymentDate = state.nextPaymentDate +
                state.paymentInterval * (numPayments - 1);
            state.nextPaymentDate = 0;
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
                    interval * Number(12, -2) / secondsInYear;
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
                    roundToAsset(
                        broker.asset,
                        broker.asset(Number(1040000114155251, -12)).number(),
                        state.loanScale));

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
                    interval * Number(12, -2) / secondsInYear;
                BEAST_EXPECT(
                    periodicRate ==
                    Number(2283105022831050, -21, Number::unchecked{}));
                STAmount const roundedPeriodicPayment{
                    broker.asset,
                    roundPeriodicPayment(
                        broker.asset, state.periodicPayment, state.loanScale)};

                testcase
                    << currencyLabel << " Payment components: "
                    << "Payments remaining, rawInterest, rawPrincipal, "
                       "rawMFee, trackedValueDelta, trackedPrincipalDelta, "
                       "trackedInterestDelta, trackedMgmtFeeDelta, special";

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
                    auto const raw = computeRawLoanState(
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining,
                        broker.params.managementFeeRate);
                    auto const rounded = constructLoanState(
                        state.totalValue,
                        state.principalOutstanding,
                        state.managementFeeOutstanding);
                    testcase
                        << currencyLabel
                        << " Loan starting state: " << state.paymentRemaining
                        << ", " << raw.interestDue << ", "
                        << raw.principalOutstanding << ", "
                        << raw.managementFeeDue << ", "
                        << rounded.valueOutstanding << ", "
                        << rounded.principalOutstanding << ", "
                        << rounded.interestDue << ", "
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
                        broker.asset(
                            Number(9533457001162141, -14), Number::upward),
                        state.loanScale,
                        Number::upward));

                auto const initialState = state;
                detail::PaymentComponents totalPaid{
                    .trackedValueDelta = 0,
                    .trackedPrincipalDelta = 0,
                    .trackedManagementFeeDelta = 0};
                Number totalInterestPaid = 0;
                std::size_t totalPaymentsMade = 0;

                ripple::LoanState currentTrueState = computeRawLoanState(
                    state.periodicPayment,
                    periodicRate,
                    state.paymentRemaining,
                    broker.params.managementFeeRate);

                while (state.paymentRemaining > 0)
                {
                    // Compute the expected principal amount
                    auto const paymentComponents =
                        detail::computePaymentComponents(
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
                        paymentComponents.trackedValueDelta <=
                        roundedPeriodicPayment);

                    ripple::LoanState const nextTrueState = computeRawLoanState(
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining - 1,
                        broker.params.managementFeeRate);
                    detail::LoanStateDeltas const deltas =
                        currentTrueState - nextTrueState;

                    testcase
                        << currencyLabel
                        << " Payment components: " << state.paymentRemaining
                        << ", " << deltas.interest << ", " << deltas.principal
                        << ", " << deltas.managementFee << ", "
                        << paymentComponents.trackedValueDelta << ", "
                        << paymentComponents.trackedPrincipalDelta << ", "
                        << paymentComponents.trackedInterestPart() << ", "
                        << paymentComponents.trackedManagementFeeDelta << ", "
                        << (paymentComponents.specialCase ==
                                    detail::PaymentSpecialCase::final
                                ? "final"
                                : paymentComponents.specialCase ==
                                    detail::PaymentSpecialCase::extra
                                ? "extra"
                                : "none");

                    auto const totalDueAmount = STAmount{
                        broker.asset,
                        paymentComponents.trackedValueDelta +
                            serviceFee.number()};

                    // Due to the rounding algorithms to keep the interest and
                    // principal in sync with "true" values, the computed amount
                    // may be a little less than the rounded fixed payment
                    // amount. For integral types, the difference should be < 3
                    // (1 unit for each of the interest and management fee). For
                    // IOUs, the difference should be after the 8th digit.
                    Number const diff = totalDue - totalDueAmount;
                    BEAST_EXPECT(
                        paymentComponents.specialCase ==
                            detail::PaymentSpecialCase::final ||
                        diff == beast::zero ||
                        (diff > beast::zero &&
                         ((broker.asset.integral() &&
                           (static_cast<Number>(diff) < 3)) ||
                          (state.loanScale - diff.exponent() > 13))));

                    BEAST_EXPECT(
                        paymentComponents.trackedValueDelta ==
                        paymentComponents.trackedPrincipalDelta +
                            paymentComponents.trackedInterestPart() +
                            paymentComponents.trackedManagementFeeDelta);
                    BEAST_EXPECT(
                        paymentComponents.trackedValueDelta <=
                        roundedPeriodicPayment);

                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToAsset(
                            broker.asset,
                            deltas.principal,
                            state.loanScale,
                            Number::upward) ==
                            roundToScale(
                                broker.asset(
                                    Number(8333228695260180, -14),
                                    Number::upward),
                                state.loanScale,
                                Number::upward));
                    BEAST_EXPECT(
                        paymentComponents.trackedPrincipalDelta >=
                            beast::zero &&
                        paymentComponents.trackedPrincipalDelta <=
                            state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase !=
                            detail::PaymentSpecialCase::final ||
                        paymentComponents.trackedPrincipalDelta ==
                            state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase ==
                            detail::PaymentSpecialCase::final ||
                        (state.periodicPayment.exponent() -
                         (deltas.principal + deltas.interest +
                          deltas.managementFee - state.periodicPayment)
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
                    if (paymentComponents.specialCase ==
                        detail::PaymentSpecialCase::final)
                    {
                        state.paymentRemaining = 0;
                        state.nextPaymentDate = 0;
                    }
                    else
                    {
                        state.nextPaymentDate += state.paymentInterval;
                    }
                    state.principalOutstanding -=
                        paymentComponents.trackedPrincipalDelta;
                    state.managementFeeOutstanding -=
                        paymentComponents.trackedManagementFeeDelta;
                    state.totalValue -= paymentComponents.trackedValueDelta;

                    verifyLoanStatus(state);

                    totalPaid.trackedValueDelta +=
                        paymentComponents.trackedValueDelta;
                    totalPaid.trackedPrincipalDelta +=
                        paymentComponents.trackedPrincipalDelta;
                    totalPaid.trackedManagementFeeDelta +=
                        paymentComponents.trackedManagementFeeDelta;
                    totalInterestPaid +=
                        paymentComponents.trackedInterestPart();
                    ++totalPaymentsMade;

                    currentTrueState = nextTrueState;
                }

                // Loan is paid off
                BEAST_EXPECT(state.paymentRemaining == 0);
                BEAST_EXPECT(state.principalOutstanding == 0);

                // Make sure all the payments add up
                BEAST_EXPECT(
                    totalPaid.trackedValueDelta == initialState.totalValue);
                BEAST_EXPECT(
                    totalPaid.trackedPrincipalDelta ==
                    initialState.principalOutstanding);
                BEAST_EXPECT(
                    totalPaid.trackedManagementFeeDelta ==
                    initialState.managementFeeOutstanding);
                // This is almost a tautology given the previous checks, but
                // check it anyway for completeness.
                BEAST_EXPECT(
                    totalInterestPaid ==
                    initialState.totalValue -
                        (initialState.principalOutstanding +
                         initialState.managementFeeOutstanding));
                BEAST_EXPECT(
                    totalPaymentsMade == initialState.paymentRemaining);

                // Can't impair or default a paid off loan
                env(manage(lender, loanKeylet.key, tfLoanImpair),
                    ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanDefault),
                    ter(tecNO_PERMISSION));
            });

#if LOANTODO
        // TODO

        /*
        LoanPay fails with tecINVARIANT_FAILED  error when loan_broker(also
        borrower) tries to do the payment. Here's the sceanrio: Create a XRP
        loan with loan broker as borrower, loan origination fee and loan service
        fee. Loan broker makes the first payment with periodic payment and loan
        service fee.
        */

        auto time = [&](std::string label, std::function<void()> timed) {
            if (!BEAST_EXPECT(timed))
                return;

            using clock_type = std::chrono::steady_clock;
            using duration_type = std::chrono::milliseconds;

            auto const start = clock_type::now();
            timed();
            auto const duration = std::chrono::duration_cast<duration_type>(
                clock_type::now() - start);

            log << label << " took " << duration.count() << "ms" << std::endl;

            return duration;
        };

        lifecycle(
            caseLabel,
            "timing",
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
                // Estimate optimal values for loanPaymentsPerFeeIncrement and
                // loanMaximumPaymentsPerTransaction.
                using namespace loan;

                auto const state =
                    getCurrentState(env, broker, verifyLoanStatus.keylet);
                auto const serviceFee = broker.asset(2).value();

                STAmount const totalDue{
                    broker.asset,
                    roundPeriodicPayment(
                        broker.asset,
                        state.periodicPayment + serviceFee,
                        state.loanScale)};

                // Make a single payment
                time("single payment", [&]() {
                    env(pay(borrower, loanKeylet.key, totalDue));
                });
                env.close();

                // Make all but the final payment
                auto const numPayments = (state.paymentRemaining - 2);
                STAmount const bigPayment{broker.asset, totalDue * numPayments};
                XRPAmount const bigFee{
                    baseFee * (numPayments / loanPaymentsPerFeeIncrement + 1)};
                time("ten payments", [&]() {
                    env(pay(borrower, loanKeylet.key, bigPayment), fee(bigFee));
                });
                env.close();

                time("final payment", [&]() {
                    // Make the final payment
                    env(
                        pay(borrower,
                            loanKeylet.key,
                            totalDue + STAmount{broker.asset, 1}));
                });
                env.close();
            });

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
    testLoanSet()
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        struct CaseArgs
        {
            bool requireAuth = false;
            bool authorizeBorrower = false;
            int initialXRP = 1'000'000;
        };

        auto const testCase =
            [&, this](
                std::function<void(Env&, BrokerInfo const&, MPTTester&)>
                    mptTest,
                std::function<void(Env&, BrokerInfo const&)> iouTest,
                CaseArgs args = {}) {
                Env env(*this, all);
                env.fund(XRP(args.initialXRP), issuer, lender, borrower);
                env.close();
                if (args.requireAuth)
                {
                    env(fset(issuer, asfRequireAuth));
                    env.close();
                }

                // We need two different asset types, MPT and IOU. Prepare MPT
                // first
                MPTTester mptt{env, issuer, mptInitNoFund};

                auto const none = LedgerSpecificFlags(0);
                mptt.create(
                    {.flags = tfMPTCanTransfer | tfMPTCanLock |
                         (args.requireAuth ? tfMPTRequireAuth : none)});
                env.close();
                PrettyAsset mptAsset = mptt.issuanceID();
                mptt.authorize({.account = lender});
                mptt.authorize({.account = borrower});
                env.close();
                if (args.requireAuth)
                {
                    mptt.authorize({.account = issuer, .holder = lender});
                    if (args.authorizeBorrower)
                        mptt.authorize({.account = issuer, .holder = borrower});
                    env.close();
                }

                env(pay(issuer, lender, mptAsset(10'000'000)));
                env.close();

                // Prepare IOU
                PrettyAsset const iouAsset = issuer[iouCurrency];
                env(trust(lender, iouAsset(10'000'000)));
                env(trust(borrower, iouAsset(10'000'000)));
                env.close();
                if (args.requireAuth)
                {
                    env(trust(issuer, iouAsset(0), lender, tfSetfAuth));
                    env(pay(issuer, lender, iouAsset(10'000'000)));
                    if (args.authorizeBorrower)
                    {
                        env(trust(issuer, iouAsset(0), borrower, tfSetfAuth));
                        env(pay(issuer, borrower, iouAsset(10'000)));
                    }
                }
                else
                {
                    env(pay(issuer, lender, iouAsset(10'000'000)));
                    env(pay(issuer, borrower, iouAsset(10'000)));
                }
                env.close();

                // Create vaults and loan brokers
                std::array const assets{mptAsset, iouAsset};
                std::vector<BrokerInfo> brokers;
                for (auto const& asset : assets)
                {
                    brokers.emplace_back(
                        createVaultAndBroker(env, asset, lender));
                }

                if (mptTest)
                    (mptTest)(env, brokers[0], mptt);
                if (iouTest)
                    (iouTest)(env, brokers[1]);
            };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));

                testcase("MPT issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(issuer),
                    sig(sfCounterpartySignature, issuer),
                    fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));

                testcase("IOU issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(issuer),
                    sig(sfCounterpartySignature, issuer),
                    fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_AUTH});

                testcase("MPT unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    sig(sfCounterpartySignature, borrower),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_AUTH});
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_AUTH});

                testcase("IOU unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    sig(sfCounterpartySignature, borrower),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_AUTH});
            },
            CaseArgs{.requireAuth = true});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env env{*this, testable_amendments()};
            return {
                env.current()->fees().accountReserve(0).drops() /
                    DROPS_PER_XRP.drops(),
                env.current()->fees().increment.drops() /
                    DROPS_PER_XRP.drops()};
        }();

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, borrower has "
                    "no reserve");
                mptt.authorize(
                    {.account = borrower, .flags = tfMPTUnauthorize});
                env.close();

                auto const mptoken =
                    keylet::mptoken(mptt.issuanceID(), borrower);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 == nullptr);

                // Burn some XRP
                env(noop(borrower), fee(XRP(acctReserve * 2 + incReserve * 2)));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = acctReserve * 2 + incReserve * 8 + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, borrower has "
                    "no reserve");
                // Remove trust line from borrower to issuer
                env.trust(broker.asset(0), borrower);
                env.close();

                env(pay(borrower, issuer, broker.asset(10'000)));
                env.close();
                auto const trustline =
                    keylet::line(borrower, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 == nullptr);

                // Burn some XRP
                env(noop(borrower), fee(XRP(acctReserve * 2 + incReserve * 2)));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 != nullptr);
            },
            CaseArgs{.initialXRP = acctReserve * 2 + incReserve * 8 + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender has "
                    "no reserve");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(
                    lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Burn some XRP
                env(noop(lender), fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    loanOriginationFee(broker.asset(1).value()),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    loanOriginationFee(broker.asset(1).value()),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = acctReserve * 2 + incReserve * 8 + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender has no "
                    "reserve");
                // Remove trust line from lender to issuer
                env.trust(broker.asset(0), lender);
                env.close();

                auto const trustline =
                    keylet::line(lender, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 != nullptr);

                env(
                    pay(lender,
                        issuer,
                        broker.asset(abs(sleLine1->at(sfBalance).value()))));
                env.close();
                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 == nullptr);

                // Burn some XRP
                env(noop(lender), fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    loanOriginationFee(broker.asset(1).value()),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    loanOriginationFee(broker.asset(1).value()),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine3 = env.le(trustline);
                BEAST_EXPECT(sleLine3 != nullptr);
            },
            CaseArgs{.initialXRP = acctReserve * 2 + incReserve * 8 + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, unauthorized lender");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(
                    lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Cannot create loan, lender not authorized to receive fee
                env(set(borrower, broker.brokerID, principalRequest),
                    loanOriginationFee(broker.asset(1).value()),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5),
                    ter{tecNO_AUTH});
                env.close();

                // Can create loan without origination fee
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
                env.close();

                // No MPToken for lender - no authorization and no payment
                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 == nullptr);
            },
            {},
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    sig(sfCounterpartySignature, lender),
                    fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    sig(sfCounterpartySignature, borrower),
                    fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    sig(sfCounterpartySignature, borrower),
                    fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        jtx::Account const alice{"alice"};
        jtx::Account const bella{"bella"};
        auto const msigSetup = [&](Env& env, Account const& account) {
            Json::Value tx1 = signers(account, 2, {{alice, 1}, {bella, 1}});
            env(tx1);
            env.close();
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    msig(sfCounterpartySignature, alice, bella),
                    fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    counterparty(lender),
                    msig(sfCounterpartySignature, alice, bella),
                    fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    msig(sfCounterpartySignature, alice, bella),
                    fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    counterparty(borrower),
                    msig(sfCounterpartySignature, alice, bella),
                    fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});
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
            brokers.emplace_back(createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{.data = "spam spam spam spam"}));
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
        // From FIND-001
        testcase << "Batch Bypass Counterparty";

        bool const lendingBatchEnabled = !std::any_of(
            Batch::disabledTxTypes.begin(),
            Batch::disabledTxTypes.end(),
            [](auto const& disabled) { return disabled == ttLOAN_BROKER_SET; });

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        BrokerParameters brokerParams;
        env.fund(XRP(brokerParams.vaultDeposit * 100), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo broker{
            createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

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
            ter(lendingBatchEnabled ? temBAD_SIGNATURE
                                    : temINVALID_INNER_BATCH));
        env.close();

        // ? Check that the loan was NOT created
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

    void
    testWrongMaxDebtBehavior()
    {
        // From FIND-003
        testcase << "Wrong Max Debt Behavior";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        BrokerParameters brokerParams{.debtMax = 0};
        env.fund(
            XRP(brokerParams.vaultDeposit * 100), issuer, noripple(lender));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo broker{
            createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

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
        // From FIND-012
        testcase << "LoanPay ripple::detail::computePeriodicPayment : "
                    "valid rate";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        BrokerParameters brokerParams;
        env.fund(
            XRP(brokerParams.vaultDeposit * 100), issuer, lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        BrokerInfo broker{
            createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{640562, -5};

        Number const serviceFee{2462611968};
        std::uint32_t const numPayments{4294967295 / 800};

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
        // Fails in preclaim because principal requested can't be
        // represented as XRP
        env(createJson, ter(tecPRECISION_LOSS));
        env.close();

        BEAST_EXPECT(!env.le(keylet));

        Number const actualPrincipal{6};

        createJson[sfPrincipalRequested] = actualPrincipal;
        createJson.removeMember(sfSequence.jsonName);
        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        // Fails in doApply because the payment is too small to be
        // represented as XRP.
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
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FEC"
                "F83F"
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
                "FF924CD18A236C2B49CF8E80A351CEAC6A10171DC9F110025646894FEC"
                "F83F"
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
            // The borrower increases their limit and acquires some IOU so
            // they can pay interest
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

        BrokerParameters const brokerParams{
            .debtMax = 200,
        };
        auto const broker =
            createVaultAndBroker(env, asset, lender, brokerParams);
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
        env(trust(lender, IOU(10'000'000)), THISLINE);
        env(pay(issuer, lender, IOU(5'000'000)), THISLINE);
        BrokerInfo brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
            sig(sfCounterpartySignature, lender),
            loanSetFee,
            THISLINE);

        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        env(fset(issuer, asfGlobalFreeze), THISLINE);
        env.close();

        // preclaim: tecFROZEN
        env(pay(borrower, loanKeylet.key, debtMaximumRequest),
            ter(tecFROZEN),
            THISLINE);
        env.close();

        env(fclear(issuer, asfGlobalFreeze), THISLINE);
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
                tfSetFreeze | tfSetDeepFreeze),
            THISLINE);
        env(trust(
                issuer,
                (*pseudoBroker)["IOU"](1'000),
                *pseudoBroker,
                tfSetFreeze | tfSetDeepFreeze),
            THISLINE);
        env.close();

        // preclaim: tecFROZEN due to deep frozen
        env(pay(borrower, loanKeylet.key, debtMaximumRequest),
            ter(tecFROZEN),
            THISLINE);
        env.close();

        // Only one needs to be unfrozen
        env(trust(
                issuer,
                lender["IOU"](1'000),
                tfClearFreeze | tfClearDeepFreeze),
            THISLINE);
        env.close();

        // The payment is late by this point
        env(pay(borrower, loanKeylet.key, debtMaximumRequest),
            ter(tecEXPIRED),
            THISLINE);
        env.close();
        env(pay(borrower,
                loanKeylet.key,
                debtMaximumRequest,
                tfLoanLatePayment),
            THISLINE);
        env.close();

        // preclaim: tecKILLED
        // note that tecKILLED in loanMakePayment()
        // doesn't happen because of the preclaim check.
        env(pay(borrower, loanKeylet.key, debtMaximumRequest),
            ter(tecKILLED),
            THISLINE);
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

            // empty/zero broker ID
            {
                auto jv = set(borrower, uint256{}, debtMaximumRequest);

                auto testZeroBrokerID = [&](std::string const& id,
                                            std::uint32_t flags = 0) {
                    // empty broker ID
                    jv[sfLoanBrokerID] = id;
                    env(jv,
                        sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        txflags(flags),
                        ter(temINVALID));
                };
                // empty broker ID
                testZeroBrokerID(std::string(""));
                // zero broker ID
                // needs a flag to distinguish the parsed STTx from the prior
                // test
                testZeroBrokerID(to_string(uint256{}), tfFullyCanonicalSig);
            }

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

    void
    testAccountSendMptMinAmountInvariant()
    {
        // (From FIND-006)
        testcase << "LoanSet trigger ripple::accountSendMPT : minimum amount "
                    "and MPT";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mptAsset = mptt.issuanceID();
        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});
        env(pay(issuer, lender, mptAsset(2'000'000)));
        env(pay(issuer, borrower, mptAsset(1'000)));
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, mptAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["CloseInterestRate"] = 76671;
        createJson["ClosePaymentFee"] = "2061925410";
        createJson["GracePeriod"] = 434;
        createJson["InterestRate"] = 50302;
        createJson["LateInterestRate"] = 30322;
        createJson["LatePaymentFee"] = "294427911";
        createJson["LoanOriginationFee"] = "3250635102";
        createJson["LoanServiceFee"] = "9557386";
        createJson["OverpaymentFee"] = 51249;
        createJson["OverpaymentInterestRate"] = 14304;
        createJson["PaymentInterval"] = 434;
        createJson["PaymentTotal"] = "2891743748";
        createJson["PrincipalRequested"] = "8516.98";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(temINVALID));
        env.close();
    }

    void
    testLoanPayDebtDecreaseInvariant()
    {
        // From FIND-007
        testcase << "LoanPay ripple::LoanPay::doApply : debtDecrease "
                    "rounding good";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx =
            env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(100'000'000));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(1'000'000));
        env(payIssuerTx);
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const baseFee = env.current()->fees().base;
        auto const loanSetFee = fee(baseFee * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["ClosePaymentFee"] = "0";
        createJson["GracePeriod"] = 60;
        createJson["InterestRate"] = 24346;
        createJson["LateInterestRate"] = 65535;
        createJson["LatePaymentFee"] = "0";
        createJson["LoanOriginationFee"] = "218";
        createJson["LoanServiceFee"] = "0";
        createJson["PaymentInterval"] = 60;
        createJson["PaymentTotal"] = 5678;
        createJson["PrincipalRequested"] = "9924.81";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tesSUCCESS));
        env.close();

        auto const pseudoAcct = [&]() {
            auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return lender;
            auto const brokerPseudo = brokerSle->at(sfAccount);
            return Account("Broker pseudo-account", brokerPseudo);
        }();

        VerifyLoanStatus verifyLoanStatus(env, broker, pseudoAcct, keylet);
        auto const originalState = getCurrentState(env, broker, keylet);
        verifyLoanStatus(originalState);

        Number const payment{3'269'349'176'470'588, -12};
        XRPAmount const payFee{
            baseFee *
            ((payment / originalState.periodicPayment) /
                 loanPaymentsPerFeeIncrement +
             1)};
        auto loanPayTx = env.json(
            pay(borrower, keylet.key, STAmount{broker.asset, payment}),
            fee(payFee));
        BEAST_EXPECT(to_string(payment) == "3269.349176470588");
        env(loanPayTx, ter(tesSUCCESS));
        env.close();

        auto const newState = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(isRounded(
            broker.asset,
            newState.managementFeeOutstanding,
            originalState.loanScale));
        BEAST_EXPECT(
            newState.managementFeeOutstanding <
            originalState.managementFeeOutstanding);
        BEAST_EXPECT(isRounded(
            broker.asset, newState.totalValue, originalState.loanScale));
        BEAST_EXPECT(isRounded(
            broker.asset,
            newState.principalOutstanding,
            originalState.loanScale));
    }

    void
    testLoanPayComputePeriodicPaymentValidTotalInterestInvariant()
    {
        // From FIND-010
        testcase << "ripple::loanComputePaymentParts : valid total interest";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx =
            env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(100'000'000));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(1'000'000));
        env(payIssuerTx);
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};
        auto const startDate = env.now() + 60s;

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["CloseInterestRate"] = 47299;
        createJson["ClosePaymentFee"] = "3985819770";
        createJson["GracePeriod"] = 0;
        createJson["InterestRate"] = 92;
        createJson["LatePaymentFee"] = "3866894865";
        createJson["LoanOriginationFee"] = "0";
        createJson["LoanServiceFee"] = "2348810240";
        createJson["OverpaymentFee"] = 58545;
        createJson["PaymentInterval"] = 60;
        createJson["PaymentTotal"] = 1;
        createJson["PrincipalRequested"] = "0.000763058";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tecPRECISION_LOSS));
        env.close(startDate);

        auto loanPayTx = env.json(
            pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
        loanPayTx["Amount"]["value"] = "0.000281284125490196";
        env(loanPayTx, ter(tecNO_ENTRY));
        env.close();
    }

    void
    testDosLoanPay()
    {
        // From FIND-005
        testcase << "DoS LoanPay";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        env(trust(lender, iouAsset(100'000'000)));
        env(trust(borrower, iouAsset(100'000'000)));
        env(pay(issuer, lender, iouAsset(10'000'000)));
        env(pay(issuer, borrower, iouAsset(1'000)));
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};
        auto const baseFee = env.current()->fees().base;

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["ClosePaymentFee"] = "0";
        createJson["GracePeriod"] = 60;
        createJson["InterestRate"] = 20930;
        createJson["LateInterestRate"] = 77049;
        createJson["LatePaymentFee"] = "0";
        createJson["LoanServiceFee"] = "0";
        createJson["OverpaymentFee"] = 7;
        createJson["OverpaymentInterestRate"] = 66653;
        createJson["PaymentInterval"] = 60;
        createJson["PaymentTotal"] = 3239184;
        createJson["PrincipalRequested"] = "3959.37";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tesSUCCESS));
        env.close();

        auto const stateBefore = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(stateBefore.paymentRemaining == 3239184);
        BEAST_EXPECT(
            stateBefore.paymentRemaining > loanMaximumPaymentsPerTransaction);

        auto loanPayTx = env.json(
            pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
        Number const amount{395937, -2};
        loanPayTx["Amount"]["value"] = to_string(amount);
        XRPAmount const payFee{
            baseFee *
            std::int64_t(
                amount / stateBefore.periodicPayment /
                    loanPaymentsPerFeeIncrement +
                1)};
        env(loanPayTx, ter(tesSUCCESS), fee(payFee));
        env.close();

        auto const stateAfter = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(
            stateAfter.paymentRemaining ==
            stateBefore.paymentRemaining - loanMaximumPaymentsPerTransaction);
    }

    void
    testLoanPayComputePeriodicPaymentValidTotalPrincipalPaidInvariant()
    {
        // From FIND-009
        testcase << "ripple::loanComputePaymentParts : totalPrincipalPaid "
                    "rounded";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx =
            env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(100'000'000));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(1'000'000));
        env(payIssuerTx);
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["ClosePaymentFee"] = "0";
        createJson["GracePeriod"] = 0;
        createJson["InterestRate"] = 24346;
        createJson["LateInterestRate"] = 65535;
        createJson["LatePaymentFee"] = "0";
        createJson["LoanOriginationFee"] = "218";
        createJson["LoanServiceFee"] = "0";
        createJson["PaymentInterval"] = 60;
        createJson["PaymentTotal"] = 5678;
        createJson["PrincipalRequested"] = "9924.81";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tesSUCCESS));
        env.close();

        auto const baseFee = env.current()->fees().base;

        auto const stateBefore = getCurrentState(env, broker, keylet);

        {
            auto loanPayTx = env.json(
                pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
            Number const amount{3074'745'058'823'529, -12};
            BEAST_EXPECT(to_string(amount) == "3074.745058823529");
            XRPAmount const payFee{
                baseFee *
                (amount / stateBefore.periodicPayment /
                     loanPaymentsPerFeeIncrement +
                 1)};
            loanPayTx["Amount"]["value"] = to_string(amount);
            env(loanPayTx, fee(payFee), ter(tesSUCCESS));
            env.close();
        }

        {
            auto loanPayTx = env.json(
                pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
            Number const amount{6732'118'170'944'051, -12};
            BEAST_EXPECT(to_string(amount) == "6732.118170944051");
            XRPAmount const payFee{
                baseFee *
                (amount / stateBefore.periodicPayment /
                     loanPaymentsPerFeeIncrement +
                 1)};
            loanPayTx["Amount"]["value"] = to_string(amount);
            env(loanPayTx, fee(payFee), ter(tesSUCCESS));
            env.close();
        }

        auto const stateAfter = getCurrentState(env, broker, keylet);
        // Total interest outstanding is non-negative
        BEAST_EXPECT(stateAfter.totalValue >= stateAfter.principalOutstanding);
        // Principal paid is non-negative
        BEAST_EXPECT(
            stateBefore.principalOutstanding >=
            stateAfter.principalOutstanding);
        // Total value change is non-negative
        BEAST_EXPECT(stateBefore.totalValue >= stateAfter.totalValue);
        // Value delta is larger or same as principal delta (meaning
        // non-negative interest paid)
        BEAST_EXPECT(
            (stateBefore.totalValue - stateAfter.totalValue) >=
            (stateBefore.principalOutstanding -
             stateAfter.principalOutstanding));
    }

    void
    testLoanPayComputePeriodicPaymentValidTotalInterestPaidInvariant()
    {
        // From FIND-008
        testcase << "ripple::loanComputePaymentParts : loanValueChange rounded";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx =
            env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(100'000'000));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(10'000'000));
        env(payIssuerTx);
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, iouAsset, lender)};
        {
            auto const coverDepositValue =
                broker.asset(broker.params.coverDeposit * 10).value();
            env(loanBroker::coverDeposit(
                lender, broker.brokerID, coverDepositValue));
            env.close();
        }

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            fee(loanSetFee),
            json(sfCounterpartySignature, Json::objectValue));

        createJson["ClosePaymentFee"] = "0";
        createJson["GracePeriod"] = 0;
        createJson["InterestRate"] = 12833;
        createJson["LateInterestRate"] = 77048;
        createJson["LatePaymentFee"] = "0";
        createJson["LoanOriginationFee"] = "218";
        createJson["LoanServiceFee"] = "0";
        createJson["PaymentInterval"] = 752;
        createJson["PaymentTotal"] = 5678;
        createJson["PrincipalRequested"] = "9924.81";

        auto const brokerStateBefore =
            env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const keylet = keylet::loan(broker.brokerID, loanSequence);

        createJson = env.json(createJson, sig(sfCounterpartySignature, lender));
        env(createJson, ter(tesSUCCESS));
        env.close();

        auto const baseFee = env.current()->fees().base;

        auto const stateBefore = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(stateBefore.paymentRemaining == 5678);
        BEAST_EXPECT(
            stateBefore.paymentRemaining > loanMaximumPaymentsPerTransaction);

        auto loanPayTx = env.json(
            pay(borrower, keylet.key, STAmount{broker.asset, Number{}}));
        Number const amount{9924'81, -2};
        BEAST_EXPECT(to_string(amount) == "9924.81");
        XRPAmount const payFee{
            baseFee *
            (amount / stateBefore.periodicPayment /
                 loanPaymentsPerFeeIncrement +
             1)};
        loanPayTx["Amount"]["value"] = to_string(amount);
        env(loanPayTx, fee(payFee), ter(tesSUCCESS));
        env.close();

        auto const stateAfter = getCurrentState(env, broker, keylet);
        BEAST_EXPECT(
            stateAfter.paymentRemaining ==
            stateBefore.paymentRemaining - loanMaximumPaymentsPerTransaction);
    }

    void
    testLoanNextPaymentDueDateOverflow()
    {
        // For FIND-013
        testcase << "Prevent nextPaymentDueDate overflow";

        using namespace jtx;
        using namespace std::chrono_literals;
        using namespace Lending;
        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx =
            env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(100'000'000));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(10'000'000));
        env(payIssuerTx);
        env.close();

        BrokerParameters const brokerParams{
            .debtMax = Number{0}, .coverRateMin = TenthBips32{1}};
        BrokerInfo broker{
            createVaultAndBroker(env, iouAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = fee(env.current()->fees().base * 2);

        using timeType = decltype(sfNextPaymentDueDate)::type::value_type;
        static_assert(std::is_same_v<timeType, std::uint32_t>);
        timeType constexpr maxTime = std::numeric_limits<timeType>::max();
        static_assert(maxTime == 4'294'967'295);

        auto const baseJson = [&]() {
            auto createJson = env.json(
                set(borrower, broker.brokerID, Number{55524'81, -2}),
                fee(loanSetFee),
                closePaymentFee(0),
                gracePeriod(0),
                interestRate(TenthBips32(12833)),
                lateInterestRate(TenthBips32(77048)),
                latePaymentFee(0),
                loanOriginationFee(218),
                json(sfCounterpartySignature, Json::objectValue));

            createJson.removeMember(sfSequence.getJsonName());

            return createJson;
        }();

        auto const baseFee = env.current()->fees().base;

        auto parentCloseTime = [&]() {
            return env.current()->parentCloseTime().time_since_epoch().count();
        };
        auto maxLoanTime = [&]() {
            auto const startDate = parentCloseTime();

            BEAST_EXPECT(startDate >= 50);

            return maxTime - startDate;
        };

        {
            // straight-up overflow: interval
            auto const interval = maxLoanTime() + 1;
            auto const total = 1;
            auto createJson = env.json(
                baseJson, paymentInterval(interval), paymentTotal(total));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // straight-up overflow: total
            // min interval is 60
            auto const interval = 60;
            auto const total = maxLoanTime() + 1;
            auto createJson = env.json(
                baseJson, paymentInterval(interval), paymentTotal(total));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // straight-up overflow: grace period
            // min interval is 60
            auto const interval = maxLoanTime() + 1;
            auto const total = 1;
            auto const grace = interval;
            auto createJson = env.json(
                baseJson,
                paymentInterval(interval),
                paymentTotal(total),
                gracePeriod(grace));

            // The grace period can't be larger than the interval.
            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with multiplication of a few large intervals
            auto const interval = 1'000'000'000;
            auto const total = 10;
            auto createJson = env.json(
                baseJson, paymentInterval(interval), paymentTotal(total));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with multiplication of many small payments
            // min interval is 60
            auto const interval = 60;
            auto const total = 1'000'000'000;
            auto createJson = env.json(
                baseJson, paymentInterval(interval), paymentTotal(total));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // Overflow with an absurdly large grace period
            // min interval is 60
            auto const total = 60;
            auto const interval = (maxLoanTime() - total) / total;
            auto const grace = interval;
            auto createJson = env.json(
                baseJson,
                paymentInterval(interval),
                paymentTotal(total),
                gracePeriod(grace));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tecKILLED));
            env.close();
        }
        {
            // Start date when the ledger is closed will be larger
            auto const brokerStateBefore =
                env.le(keylet::loanbroker(broker.brokerID));
            auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
            auto const keylet = keylet::loan(broker.brokerID, loanSequence);

            auto const grace = 100;
            auto const interval = maxLoanTime() - grace;
            auto const total = 1;
            auto createJson = env.json(
                baseJson,
                paymentInterval(interval),
                paymentTotal(total),
                gracePeriod(grace));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tesSUCCESS));
            env.close();

            // The transaction is killed in the closed ledger
            auto const meta = env.meta();
            if (BEAST_EXPECT(meta))
            {
                BEAST_EXPECT(meta->at(sfTransactionResult) == tecKILLED);
            }

            // If the transaction had succeeded, the loan would exist
            auto const loanSle = env.le(keylet);
            // but it doesn't
            BEAST_EXPECT(!loanSle);
        }
        {
            // Start date when the ledger is closed will be larger
            auto const brokerStateBefore =
                env.le(keylet::loanbroker(broker.brokerID));
            auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
            auto const keylet = keylet::loan(broker.brokerID, loanSequence);

            auto const closeStartDate = (parentCloseTime() / 10 + 1) * 10;
            auto const grace = 5'000;
            auto const interval = maxTime - closeStartDate - grace;
            auto const total = 1;
            auto createJson = env.json(
                baseJson,
                paymentInterval(interval),
                paymentTotal(total),
                gracePeriod(grace));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tesSUCCESS));
            env.close();

            // The transaction succeeds in the closed ledger
            auto const meta = env.meta();
            if (BEAST_EXPECT(meta))
            {
                BEAST_EXPECT(meta->at(sfTransactionResult) == tesSUCCESS);
            }

            // This loan exists
            auto const afterState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(afterState.nextPaymentDate == maxTime - grace);
            BEAST_EXPECT(afterState.previousPaymentDate == 0);
            BEAST_EXPECT(afterState.paymentRemaining == 1);
        }

        {
            // Ensure the borrower has funds to pay back the loan
            env(pay(issuer, borrower, iouAsset(Number{1'055'524'81, -2})));

            // Start date when the ledger is closed will be larger
            auto const closeStartDate = (parentCloseTime() / 10 + 1) * 10;
            auto const grace = 5'000;
            auto const maxLoanTime = maxTime - closeStartDate - grace;
            auto const total = [&]() {
                if (maxLoanTime % 5 == 0)
                    return 5;
                if (maxLoanTime % 3 == 0)
                    return 3;
                if (maxLoanTime % 2 == 0)
                    return 2;
                return 0;
            }();
            if (!BEAST_EXPECT(total != 0))
                return;

            auto const brokerState =
                env.le(keylet::loanbroker(broker.brokerID));
            // Intentionally shadow the outer values
            auto const loanSequence = brokerState->at(sfLoanSequence);
            auto const keylet = keylet::loan(broker.brokerID, loanSequence);

            auto const interval = maxLoanTime / total;
            auto createJson = env.json(
                baseJson,
                paymentInterval(interval),
                paymentTotal(total),
                gracePeriod(grace));

            env(createJson,
                sig(sfCounterpartySignature, lender),
                ter(tesSUCCESS));
            env.close();

            // This loan exists
            auto const beforeState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(
                beforeState.nextPaymentDate == closeStartDate + interval);
            BEAST_EXPECT(beforeState.previousPaymentDate == 0);
            BEAST_EXPECT(beforeState.paymentRemaining == total);
            BEAST_EXPECT(beforeState.periodicPayment > 0);

            // pay all but the last payment
            Number const payment = beforeState.periodicPayment * (total - 1);
            XRPAmount const payFee{
                baseFee * ((total - 1) / loanPaymentsPerFeeIncrement + 1)};
            auto loanPayTx = env.json(
                pay(borrower, keylet.key, STAmount{broker.asset, payment}),
                fee(payFee));
            env(loanPayTx, ter(tesSUCCESS));
            env.close();

            // The loan is on the last payment
            auto const afterState = getCurrentState(env, broker, keylet);
            BEAST_EXPECT(afterState.nextPaymentDate == maxTime - grace);
            BEAST_EXPECT(
                afterState.previousPaymentDate == maxTime - grace - interval);
            BEAST_EXPECT(afterState.paymentRemaining == 1);
        }
    }

    void
    testRequireAuth()
    {
        testcase("Require Auth - Implicit Pseudo-account authorization");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Env env(*this);

        env.fund(XRP(100'000), issuer, lender, borrower);
        env.close();

        auto asset = MPTTester({
            .env = env,
            .issuer = issuer,
            .holders = {lender, borrower},
            .flags = MPTDEXFlags | tfMPTRequireAuth | tfMPTCanClawback |
                tfMPTCanLock,
            .authHolder = true,
        });

        env(pay(issuer, lender, asset(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, asset, lender)};

        auto const loanSetFee = fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        auto forUnauthAuth = [&](auto&& doTx) {
            for (auto const flag : {tfMPTUnauthorize, 0u})
            {
                asset.authorize(
                    {.account = issuer, .holder = borrower, .flags = flag});
                env.close();
                doTx(flag == 0);
                env.close();
            }
        };

        // Can't create a loan if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                sig(sfCounterpartySignature, lender),
                loanSetFee,
                err);
        });

        std::uint32_t constexpr loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        // Can't loan pay if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(pay(borrower, loanKeylet.key, debtMaximumRequest), err);
        });
    }

    void
    testCoverDepositWithdrawNonTransferableMPT()
    {
        testcase(
            "CoverDeposit and CoverWithdraw reject MPT without CanTransfer");
        using namespace jtx;
        using namespace loanBroker;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const alice{"alice"};

        env.fund(XRP(100'000), issuer, alice);
        env.close();

        MPTTester mpt{env, issuer, mptInitNoFund};

        mpt.create(
            {.flags = tfMPTCanTransfer,
             .mutableFlags = tmfMPTCanMutateCanTransfer});

        env.close();

        PrettyAsset const asset = mpt["MPT"];
        mpt.authorize({.account = alice});
        env.close();

        // Issuer can fund the holder even if CanTransfer is not set.
        env(pay(issuer, alice, asset(100)));
        env.close();

        Vault vault{env};
        auto const [createTx, vaultKeylet] =
            vault.create({.owner = alice, .asset = asset});
        env(createTx);
        env.close();

        auto const brokerKeylet =
            keylet::loanbroker(alice.id(), env.seq(alice));
        env(set(alice, vaultKeylet.key));
        env.close();

        auto const brokerSle = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return;

        Account const pseudoAccount{
            "Loan Broker pseudo-account", brokerSle->at(sfAccount)};

        // Remove CanTransfer after the broker is set up.
        mpt.set({.mutableFlags = tmfMPTClearCanTransfer});
        env.close();

        // Standard Payment path should forbid third-party transfers.
        env(pay(alice, pseudoAccount, asset(1)), ter(tecNO_AUTH));
        env.close();

        // Cover cannot be transferred to broker account
        auto const depositAmount = asset(1);
        env(coverDeposit(alice, brokerKeylet.key, depositAmount),
            ter{tecNO_AUTH});
        env.close();

        if (auto const refreshed = env.le(brokerKeylet);
            BEAST_EXPECT(refreshed))
        {
            BEAST_EXPECT(refreshed->at(sfCoverAvailable) == 0);
            env.require(balance(pseudoAccount, asset(0)));
        }

        // Set CanTransfer again and transfer some deposit
        mpt.set({.mutableFlags = tmfMPTSetCanTransfer});
        env.close();

        env(coverDeposit(alice, brokerKeylet.key, depositAmount));
        env.close();

        if (auto const refreshed = env.le(brokerKeylet);
            BEAST_EXPECT(refreshed))
        {
            BEAST_EXPECT(refreshed->at(sfCoverAvailable) == 1);
            env.require(balance(pseudoAccount, depositAmount));
        }

        // Remove CanTransfer after the deposit
        mpt.set({.mutableFlags = tmfMPTClearCanTransfer});
        env.close();

        // Cover cannot be transferred from broker account
        env(coverWithdraw(alice, brokerKeylet.key, depositAmount),
            ter{tecNO_AUTH});
        env.close();

        // Set CanTransfer again and withdraw
        mpt.set({.mutableFlags = tmfMPTSetCanTransfer});
        env.close();

        env(coverWithdraw(alice, brokerKeylet.key, depositAmount));
        env.close();

        if (auto const refreshed = env.le(brokerKeylet);
            BEAST_EXPECT(refreshed))
        {
            BEAST_EXPECT(refreshed->at(sfCoverAvailable) == 0);
            env.require(balance(pseudoAccount, asset(0)));
        }
    }

#if LOANTODO
    void
    testLoanPayLateFullPaymentBypassesPenalties()
    {
        testcase("LoanPay full payment skips late penalties");
        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const asset = issuer[iouCurrency];
        env(trust(lender, asset(100'000'000)));
        env(trust(borrower, asset(100'000'000)));
        env(pay(issuer, lender, asset(50'000'000)));
        env(pay(issuer, borrower, asset(5'000'000)));
        env.close();

        BrokerInfo broker{createVaultAndBroker(env, asset, lender)};

        auto const loanSetFee = fee(env.current()->fees().base * 2);

        auto const brokerPreLoan = env.le(keylet::loanbroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerPreLoan))
            return;

        auto const loanSequence = brokerPreLoan->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

        Number const principal = asset(1'000).value();
        Number const serviceFee = asset(2).value();
        Number const lateFee = asset(5).value();
        Number const closeFee = asset(4).value();

        env(set(borrower, broker.brokerID, principal),
            sig(sfCounterpartySignature, lender),
            loanServiceFee(serviceFee),
            latePaymentFee(lateFee),
            closePaymentFee(closeFee),
            interestRate(percentageToTenthBips(12)),
            lateInterestRate(percentageToTenthBips(24) / 10),
            closeInterestRate(percentageToTenthBips(5)),
            paymentTotal(12),
            paymentInterval(600),
            gracePeriod(0),
            fee(loanSetFee));
        env.close();

        auto state1 = getCurrentState(env, broker, loanKeylet);
        if (!BEAST_EXPECT(state1.paymentRemaining > 1))
            return;

        using d = NetClock::duration;
        using tp = NetClock::time_point;
        auto const overdueClose =
            tp{d{state1.nextPaymentDate + state1.paymentInterval}};
        env.close(overdueClose);

        auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
        auto const loanSle = env.le(loanKeylet);
        if (!BEAST_EXPECT(brokerSle && loanSle))
            return;

        auto state = getCurrentState(env, broker, loanKeylet);

        TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
        TenthBips32 const interestRateValue{loanSle->at(sfInterestRate)};
        TenthBips32 const lateInterestRateValue{
            loanSle->at(sfLateInterestRate)};
        TenthBips32 const closeInterestRateValue{
            loanSle->at(sfCloseInterestRate)};

        Number const closePaymentFeeRounded = roundToAsset(
            broker.asset, loanSle->at(sfClosePaymentFee), state.loanScale);
        Number const latePaymentFeeRounded = roundToAsset(
            broker.asset, loanSle->at(sfLatePaymentFee), state.loanScale);

        auto const roundedLoanState = constructLoanState(
            state.totalValue,
            state.principalOutstanding,
            state.managementFeeOutstanding);
        Number const totalInterestOutstanding = roundedLoanState.interestDue;

        auto const periodicRate =
            loanPeriodicRate(interestRateValue, state.paymentInterval);
        auto const rawLoanState = computeRawLoanState(
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            managementFeeRate);

        auto const parentCloseTime = env.current()->parentCloseTime();
        auto const startDateSeconds = static_cast<std::uint32_t>(
            state.startDate.time_since_epoch().count());

        Number const fullPaymentInterest = computeFullPaymentInterest(
            rawLoanState.principalOutstanding,
            periodicRate,
            parentCloseTime,
            state.paymentInterval,
            state.previousPaymentDate,
            startDateSeconds,
            closeInterestRateValue);

        Number const roundedFullInterestAmount =
            roundToAsset(broker.asset, fullPaymentInterest, state.loanScale);
        Number const roundedFullManagementFee = computeManagementFee(
            broker.asset,
            roundedFullInterestAmount,
            managementFeeRate,
            state.loanScale);
        Number const roundedFullInterest =
            roundedFullInterestAmount - roundedFullManagementFee;

        Number const trackedValueDelta = state.principalOutstanding +
            totalInterestOutstanding + state.managementFeeOutstanding;
        Number const untrackedManagementFee = closePaymentFeeRounded +
            roundedFullManagementFee - state.managementFeeOutstanding;
        Number const untrackedInterest =
            roundedFullInterest - totalInterestOutstanding;

        Number const baseFullDue =
            trackedValueDelta + untrackedInterest + untrackedManagementFee;
        BEAST_EXPECT(
            baseFullDue ==
            roundToAsset(broker.asset, baseFullDue, state.loanScale));

        auto const overdueSeconds =
            parentCloseTime.time_since_epoch().count() - state.nextPaymentDate;
        if (!BEAST_EXPECT(overdueSeconds > 0))
            return;

        Number const overdueRate =
            loanPeriodicRate(lateInterestRateValue, overdueSeconds);
        Number const lateInterestRaw = state.principalOutstanding * overdueRate;
        Number const lateInterestRounded =
            roundToAsset(broker.asset, lateInterestRaw, state.loanScale);
        Number const lateManagementFeeRounded = computeManagementFee(
            broker.asset,
            lateInterestRounded,
            managementFeeRate,
            state.loanScale);
        Number const penaltyDue = lateInterestRounded +
            lateManagementFeeRounded + latePaymentFeeRounded;
        BEAST_EXPECT(penaltyDue > Number{});

        auto const balanceBefore = env.balance(borrower, broker.asset).number();

        STAmount const paymentAmount{broker.asset.raw(), baseFullDue};
        env(pay(borrower, loanKeylet.key, paymentAmount, tfLoanFullPayment));
        env.close();

        if (auto const meta = env.meta(); BEAST_EXPECT(meta))
            BEAST_EXPECT(meta->at(sfTransactionResult) == tesSUCCESS);

        auto const balanceAfter = env.balance(borrower, broker.asset).number();
        Number const actualPaid = balanceBefore - balanceAfter;
        BEAST_EXPECT(actualPaid == baseFullDue);

        Number const expectedWithPenalty = baseFullDue + penaltyDue;
        BEAST_EXPECT(expectedWithPenalty > actualPaid);
        BEAST_EXPECT(expectedWithPenalty - actualPaid == penaltyDue);
    }

    void
    testLoanCoverMinimumRoundingExploit()
    {
        auto testLoanCoverMinimumRoundingExploit =
            [&, this](Number const& principalRequest) {
                testcase << "LoanBrokerCoverClawback drains cover via rounding"
                         << " principalRequested="
                         << to_string(principalRequest);

                using namespace jtx;
                using namespace loan;
                using namespace loanBroker;

                Env env(*this, all);

                Account const issuer{"issuer"};
                Account const lender{"lender"};
                Account const borrower{"borrower"};

                env.fund(XRP(1'000'000'000), issuer, lender, borrower);
                env.close();

                env(fset(issuer, asfAllowTrustLineClawback));
                env.close();

                PrettyAsset const asset = issuer[iouCurrency];
                env(trust(lender, asset(2'000'0000)));
                env(trust(borrower, asset(2'000'0000)));
                env.close();

                env(pay(issuer, lender, asset(2'000'0000)));
                env.close();

                BrokerParameters brokerParams{
                    .debtMax = 0, .coverRateMin = TenthBips32{10'000}};
                BrokerInfo broker{
                    createVaultAndBroker(env, asset, lender, brokerParams)};

                auto const loanSetFee = fee(env.current()->fees().base * 2);
                auto createTx = env.jt(
                    set(borrower, broker.brokerID, principalRequest),
                    sig(sfCounterpartySignature, lender),
                    loanSetFee,
                    paymentInterval(600),
                    paymentTotal(1),
                    gracePeriod(60));
                env(createTx);
                env.close();

                auto const brokerBefore =
                    env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerBefore);
                if (!brokerBefore)
                    return;

                Number const debtOutstanding = brokerBefore->at(sfDebtTotal);
                Number const coverAvailableBefore =
                    brokerBefore->at(sfCoverAvailable);

                BEAST_EXPECT(debtOutstanding > Number{});
                BEAST_EXPECT(coverAvailableBefore > Number{});

                log << "debt=" << to_string(debtOutstanding)
                    << " cover_available=" << to_string(coverAvailableBefore);

                env(coverClawback(issuer, 0), loanBrokerID(broker.brokerID));
                env.close();

                auto const brokerAfter =
                    env.le(keylet::loanbroker(broker.brokerID));
                BEAST_EXPECT(brokerAfter);
                if (!brokerAfter)
                    return;

                Number const debtAfter = brokerAfter->at(sfDebtTotal);
                // the debt has not changed
                BEAST_EXPECT(debtAfter == debtOutstanding);

                Number const coverAvailableAfter =
                    brokerAfter->at(sfCoverAvailable);

                // since the cover rate min != 0, the cover available should not
                // be zero
                BEAST_EXPECT(coverAvailableAfter != Number{});
            };

        // Call the lambda with different principal values
        testLoanCoverMinimumRoundingExploit(Number{1, -30});  // 1e-30 units
        testLoanCoverMinimumRoundingExploit(Number{1, -20});  // 1e-20 units
        testLoanCoverMinimumRoundingExploit(Number{1, -10});  // 1e-10 units
        testLoanCoverMinimumRoundingExploit(Number{1, 1});    // 1e-10 units
    }
#endif

    void
    testPoC_UnsignedUnderflowOnFullPayAfterEarlyPeriodic()
    {
        // --- PoC Summary ----------------------------------------------------
        // Scenario: Borrower makes one periodic payment early (before next due)
        // so doPayment sets sfPreviousPaymentDate to the (future)
        // sfNextPaymentDueDate and advances sfNextPaymentDueDate by one
        // interval. Borrower then immediately performs a full-payment
        // (tfLoanFullPayment). Why it matters: Full-payment interest accrual
        // uses
        //   delta = now - max(prevPaymentDate, startDate)
        // with an unsigned clock representation (uint32). If prevPaymentDate is
        // in the future, the subtraction underflows to a very large positive
        // number. This inflates roundedFullInterest and total full-close due,
        // and LoanPay applies the inflated valueChange to the vault
        // (sfAssetsTotal), increasing NAV.
        // --------------------------------------------------------------------
        testcase(
            "PoC: Unsigned-underflow full-pay accrual after early periodic");

        using namespace jtx;
        using namespace loan;
        using namespace std::chrono_literals;

        Env env(*this, all);

        Account const lender{"poc_lender4"};
        Account const borrower{"poc_borrower4"};
        env.fund(XRP(3'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1'000'000};
        BrokerParameters brokerParams{};
        auto const broker =
            createVaultAndBroker(env, asset, lender, brokerParams);

        // Create a 3-payment loan so full-payment path is enabled after 1
        // periodic payment.
        auto const loanSetFee = fee(env.current()->fees().base * 2);
        Number const principalRequest = asset(1000).value();
        auto const originationFee = asset(0).value();
        auto const serviceFee = asset(1).value();
        auto const serviceFeePA = asset(1);
        auto const lateFee = asset(0).value();
        auto const closeFee = asset(0).value();
        auto const interest = percentageToTenthBips(12);
        auto const lateInterest = percentageToTenthBips(12) / 10;
        auto const closeInterest = percentageToTenthBips(12) / 10;
        auto const overpaymentInterest = percentageToTenthBips(12) / 10;
        auto const total = 3u;
        auto const interval = 600u;
        auto const grace = 60u;

        auto createJtx = env.jt(
            set(borrower, broker.brokerID, principalRequest, 0),
            sig(sfCounterpartySignature, lender),
            loanOriginationFee(originationFee),
            loanServiceFee(serviceFee),
            latePaymentFee(lateFee),
            closePaymentFee(closeFee),
            overpaymentFee(percentageToTenthBips(5) / 10),
            interestRate(interest),
            lateInterestRate(lateInterest),
            closeInterestRate(closeInterest),
            overpaymentInterestRate(overpaymentInterest),
            paymentTotal(total),
            paymentInterval(interval),
            gracePeriod(grace),
            fee(loanSetFee));

        auto const brokerSle = env.le(keylet::loanbroker(broker.brokerID));
        BEAST_EXPECT(brokerSle);
        auto const loanSequence = brokerSle ? brokerSle->at(sfLoanSequence) : 0;
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

        env(createJtx);
        env.close();

        // Compute a regular periodic due and pay it early (before next due).
        auto state = getCurrentState(env, broker, loanKeylet);
        Number const periodicRate =
            loanPeriodicRate(state.interestRate, state.paymentInterval);
        auto const components = detail::computePaymentComponents(
            asset.raw(),
            state.loanScale,
            state.totalValue,
            state.principalOutstanding,
            state.managementFeeOutstanding,
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            brokerParams.managementFeeRate);
        STAmount const regularDue{
            asset, components.trackedValueDelta + serviceFeePA.number()};
        // now < nextDue immediately after creation, so this is an early pay.
        env(pay(borrower, loanKeylet.key, regularDue));
        env.close();

        // Immediately attempt a full payoff. Compute the exact full-payment
        // due to ensure the tx applies.
        auto after = getCurrentState(env, broker, loanKeylet);
        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        auto const brokerSle2 = env.le(keylet::loanbroker(broker.brokerID));
        BEAST_EXPECT(brokerSle2);

        auto const closePaymentFee =
            loanSle ? loanSle->at(sfClosePaymentFee) : Number{};
        auto const closeInterestRate = loanSle
            ? TenthBips32{loanSle->at(sfCloseInterestRate)}
            : TenthBips32{};
        auto const managementFeeRate = brokerSle2
            ? TenthBips16{brokerSle2->at(sfManagementFeeRate)}
            : TenthBips16{};

        Number const periodicRate2 =
            loanPeriodicRate(after.interestRate, after.paymentInterval);
        // Accrued + prepayment-penalty interest based on current periodic
        // schedule
        auto const fullPaymentInterest = computeFullPaymentInterest(
            after.periodicPayment,
            periodicRate2,
            after.paymentRemaining,
            env.current()->parentCloseTime(),
            after.paymentInterval,
            after.previousPaymentDate,
            static_cast<std::uint32_t>(
                after.startDate.time_since_epoch().count()),
            closeInterestRate);
        // Round to asset scale and split interest/fee parts
        auto const roundedInterest =
            roundToAsset(asset.raw(), fullPaymentInterest, after.loanScale);
        Number const roundedFullMgmtFee = computeManagementFee(
            asset.raw(), roundedInterest, managementFeeRate, after.loanScale);
        Number const roundedFullInterest = roundedInterest - roundedFullMgmtFee;

        // Show both signed and unsigned deltas to highlight the underflow.
        auto const nowSecs = static_cast<std::uint32_t>(
            env.current()->parentCloseTime().time_since_epoch().count());
        auto const startSecs = static_cast<std::uint32_t>(
            after.startDate.time_since_epoch().count());
        auto const lastPaymentDate =
            std::max(after.previousPaymentDate, startSecs);
        auto const signedDelta = static_cast<std::int64_t>(nowSecs) -
            static_cast<std::int64_t>(lastPaymentDate);
        auto const unsignedDelta =
            static_cast<std::uint32_t>(nowSecs - lastPaymentDate);
        log << "PoC window: prev=" << after.previousPaymentDate
            << " start=" << startSecs << " now=" << nowSecs
            << " signedDelta=" << signedDelta
            << " unsignedDelta=" << unsignedDelta << std::endl;

        // Reference (clamped) computation: emulate a non-negative accrual
        // window by clamping prevPaymentDate to 'now' for the full-pay path.
        auto const prevClamped = std::min(after.previousPaymentDate, nowSecs);
        auto const fullPaymentInterestClamped = computeFullPaymentInterest(
            after.periodicPayment,
            periodicRate2,
            after.paymentRemaining,
            env.current()->parentCloseTime(),
            after.paymentInterval,
            prevClamped,
            startSecs,
            closeInterestRate);
        auto const roundedInterestClamped = roundToAsset(
            asset.raw(), fullPaymentInterestClamped, after.loanScale);
        Number const roundedFullMgmtFeeClamped = computeManagementFee(
            asset.raw(),
            roundedInterestClamped,
            managementFeeRate,
            after.loanScale);
        Number const roundedFullInterestClamped =
            roundedInterestClamped - roundedFullMgmtFeeClamped;
        STAmount const fullDueClamped{
            asset,
            after.principalOutstanding + roundedFullInterestClamped +
                roundedFullMgmtFeeClamped + closePaymentFee};

        // Collect vault NAV before closing payment
        auto const vaultId2 =
            brokerSle2 ? brokerSle2->at(sfVaultID) : uint256{};
        auto const vaultKey2 = keylet::vault(vaultId2);
        auto const vaultBefore = env.le(vaultKey2);
        BEAST_EXPECT(vaultBefore);
        Number const assetsTotalBefore =
            vaultBefore ? vaultBefore->at(sfAssetsTotal) : Number{};

        STAmount const fullDue{
            asset,
            after.principalOutstanding + roundedFullInterest +
                roundedFullMgmtFee + closePaymentFee};

        log << "PoC payoff: principalOutstanding=" << after.principalOutstanding
            << " roundedFullInterest=" << roundedFullInterest
            << " roundedFullMgmtFee=" << roundedFullMgmtFee
            << " closeFee=" << closePaymentFee
            << " fullDue=" << to_string(fullDue.getJson()) << std::endl;
        log << "PoC reference (clamped): roundedFullInterestClamped="
            << roundedFullInterestClamped
            << " roundedFullMgmtFeeClamped=" << roundedFullMgmtFeeClamped
            << " fullDueClamped=" << to_string(fullDueClamped.getJson())
            << std::endl;

        env(pay(borrower, loanKeylet.key, fullDue), txflags(tfLoanFullPayment));
        env.close();

        // Sanity: underflow present (unsigned delta very large relative to
        // interval)
        BEAST_EXPECT(unsignedDelta > after.paymentInterval);

        // Compare vault NAV before/after the full close
        auto const vaultAfter = env.le(vaultKey2);
        BEAST_EXPECT(vaultAfter);
        if (vaultAfter)
        {
            auto const assetsTotalAfter = vaultAfter->at(sfAssetsTotal);
            log << "PoC NAV: assetsTotalBefore=" << assetsTotalBefore
                << " assetsTotalAfter=" << assetsTotalAfter
                << " delta=" << (assetsTotalAfter - assetsTotalBefore)
                << std::endl;

            // Value-based proof: underflowed window yields a payoff larger than
            // the clamped (non-underflow) reference.
            BEAST_EXPECT(fullDue == fullDueClamped);
            if (fullDue > fullDueClamped)
                log << "PoC delta: overcharge (fullDue > clamped)" << std::endl;
        }

        // Loan should be paid off
        auto const finalLoan = env.le(loanKeylet);
        BEAST_EXPECT(finalLoan);
        if (finalLoan)
        {
            BEAST_EXPECT(finalLoan->at(sfPaymentRemaining) == 0);
            BEAST_EXPECT(finalLoan->at(sfPrincipalOutstanding) == 0);
        }
    }

    void
    testDustManipulation()
    {
        testcase("Dust manipulation");

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all);

        // Setup: Create accounts
        Account issuer{"issuer"};
        Account lender{"lender"};
        Account borrower{"borrower"};
        Account victim{"victim"};

        env.fund(XRP(1'000'000'00), issuer, lender, borrower, victim);
        env.close();

        // Step 1: Create vault with IOU asset
        auto asset = issuer["USD"];
        env(trust(lender, asset(100000)));
        env(trust(borrower, asset(100000)));
        env(trust(victim, asset(100000)));
        env(pay(issuer, lender, asset(50000)));
        env(pay(issuer, borrower, asset(50000)));
        env(pay(issuer, victim, asset(50000)));
        env.close();

        BrokerParameters brokerParams{
            .vaultDeposit = 10000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{1000},
            .coverRateLiquidation = TenthBips32{2500}};

        auto broker = createVaultAndBroker(env, asset, lender, brokerParams);

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
            return;

        auto const& vaultKeylet = broker.vaultKeylet();

        {
            auto const vaultSle = env.le(vaultKeylet);
            Number assetsTotal = vaultSle->at(sfAssetsTotal);
            Number assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "Before loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            // before the loan the assets total and available should be equal
            BEAST_EXPECT(assetsAvail == assetsTotal);
            BEAST_EXPECT(
                assetsAvail ==
                broker.asset(brokerParams.vaultDeposit).number());
        }

        Keylet const& loanKeylet = *loanKeyletOpt;

        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{100},
            .interest = TenthBips32{1922},
            .payTotal = 5816,
            .payInterval = 86400 * 6,
            .gracePd = 86400 * 5,
        };

        env(loanParams(env, broker));
        env.close();

        // Wait for loan to be late enough to default
        env.close(std::chrono::seconds(86400 * 40));  // 40 days

        {
            auto const vaultSle = env.le(vaultKeylet);
            Number assetsTotal = vaultSle->at(sfAssetsTotal);
            Number assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "After loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            auto const state = constructRoundedLoanState(loanSle);

            log << "Loan state:" << std::endl;
            log << "  ValueOutstanding: " << state.valueOutstanding
                << std::endl;
            log << "  PrincipalOutstanding: " << state.principalOutstanding
                << std::endl;
            log << "  InterestOutstanding: " << state.interestOutstanding()
                << std::endl;
            log << "  InterestDue: " << state.interestDue << std::endl;
            log << "  FeeDue: " << state.managementFeeDue << std::endl;

            // after loan creation the assets total and available should
            // reflect the value of the loan
            BEAST_EXPECT(assetsAvail < assetsTotal);
            BEAST_EXPECT(
                assetsAvail ==
                broker
                    .asset(
                        brokerParams.vaultDeposit - loanParams.principalRequest)
                    .number());
            BEAST_EXPECT(
                assetsTotal ==
                broker.asset(brokerParams.vaultDeposit + state.interestDue)
                    .number());
        }

        // Step 7: Trigger default (dust adjustment will occur)
        env(jtx::loan::manage(lender, loanKeylet.key, tfLoanDefault));
        env.close();

        // Step 8: Verify phantom assets created
        {
            auto const vaultSle2 = env.le(vaultKeylet);
            Number assetsTotal2 = vaultSle2->at(sfAssetsTotal);
            Number assetsAvail2 = vaultSle2->at(sfAssetsAvailable);

            log << "After default:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal2 << std::endl;
            log << "  AssetsAvailable: " << assetsAvail2 << std::endl;
            log << "  Difference: " << (assetsTotal2 - assetsAvail2)
                << std::endl;

            // after a default the assets total and available should be equal
            BEAST_EXPECT(assetsAvail2 == assetsTotal2);
        }
    }

    void
    testRIPD3831()
    {
        using namespace jtx;

        testcase("RIPD-3831");

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            // .managementFeeRate = TenthBips16{5919},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{200'000, -6},
            .lateFee = Number{200, -6},
            .interest = TenthBips32{50'000},
            .payTotal = 10,
            .payInterval = 150,
            .gracePd = 0};

        auto const assetType = AssetType::XRP;

        Env env(*this, all);

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (!BEAST_EXPECT(loanResult))
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);

        using tp = NetClock::time_point;
        using d = NetClock::duration;

        auto state = getCurrentState(env, broker, loanKeylet);
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            // log << "loan after create: " << to_string(loan->getJson())
            //     << std::endl;

            env.close(tp{d{
                loan->at(sfNextPaymentDueDate) + loan->at(sfGracePeriod) + 1}});
        }

        topUpBorrower(
            env, broker, issuer, borrower, state, loanParams.serviceFee);

        using namespace jtx::loan;

        auto jv =
            pay(borrower, loanKeylet.key, drops(XRPAmount(state.totalValue)));

        {
            auto const submitParam = to_string(jv);
            // log << "about to submit: " << submitParam << std::endl;
            auto const jr = env.rpc("submit", borrower.name(), submitParam);

            // log << jr << std::endl;
            BEAST_EXPECT(jr.isMember(jss::result));
            auto const jResult = jr[jss::result];
            // BEAST_EXPECT(jResult[jss::error] == "invalidTransaction");
            // BEAST_EXPECT(
            //     jResult[jss::error_exception] ==
            //     "fails local checks: Transaction has bad signature.");
        }

        env.close();

        // Make sure the system keeps responding
        env(noop(borrower));
        env.close();
        env(noop(issuer));
        env.close();
        env(noop(lender));
        env.close();
    }

    void
    testRIPD3459()
    {
        testcase("RIPD-3459 - LoanBroker incorrect debt total");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 200'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{500},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{100'000, -4},
            .interest = TenthBips32{100'000},
            .payTotal = 10,
            .gracePd = 0};

        auto const assetType = AssetType::MPT;

        Env env(*this, all);

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (!BEAST_EXPECT(loanResult))
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        if (auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(
                    brokerSle->at(sfDebtTotal) ==
                    loanSle->at(sfTotalValueOutstanding));
            }
        }

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

        if (auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(
                    brokerSle->at(sfDebtTotal) ==
                    loanSle->at(sfTotalValueOutstanding));
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == beast::zero);
            }
        }
    }

    void
    testRIPD3901()
    {
        testcase("Crash with tfLoanOverpayment");
        using namespace jtx;
        using namespace loan;
        Account const lender{"lender"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const depositor{"depositor"};
        auto const txfee = fee(XRP(100));

        Env env(*this);
        Vault vault(env);

        env.fund(XRP(10'000), lender, issuer, borrower, depositor);
        env.close();

        auto [tx, vaultKeyLet] =
            vault.create({.owner = lender, .asset = xrpIssue()});
        env(tx, txfee);
        env.close();

        env(vault.deposit(
                {.depositor = depositor,
                 .id = vaultKeyLet.key,
                 .amount = XRP(1'000)}),
            txfee);
        env.close();

        auto const brokerKeyLet =
            keylet::loanbroker(lender.id(), env.seq(lender));

        env(loanBroker::set(lender, vaultKeyLet.key), txfee);
        env.close();

        // BrokerInfo brokerInfo{xrpIssue(), keylet, vaultKeyLet, {}};

        STAmount const debtMaximumRequest = XRPAmount(200'000);

        env(set(borrower, brokerKeyLet.key, debtMaximumRequest),
            sig(sfCounterpartySignature, lender),
            interestRate(TenthBips32(50'000)),
            paymentTotal(2),
            paymentInterval(150),
            txflags(tfLoanOverpayment),
            txfee);
        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerKeyLet.key, loanSequence);

        if (auto loan = env.le(loanKeylet); env.test.BEAST_EXPECT(loan))
        {
            env(loan::pay(borrower, loanKeylet.key, XRPAmount(150'001)),
                txflags(tfLoanOverpayment),
                txfee);
            env.close();
        }
    }

    void
    testRoundingAllowsUndercoverage()
    {
        testcase("Minimum cover rounding allows undercoverage (XRP)");

        using namespace jtx;
        using namespace loanBroker;

        Env env(*this, all);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(200'000), lender, borrower);
        env.close();

        // Vault with XRP asset
        Vault vault{env};
        auto [vaultCreate, vaultKeylet] =
            vault.create({.owner = lender, .asset = xrpIssue()});
        env(vaultCreate);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        // Seed the vault with XRP so it can fund the loan principal
        PrettyAsset const xrpAsset{xrpIssue(), 1};

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{10'000},
            .coverDeposit = 82,
        };

        auto const brokerInfo =
            createVaultAndBroker(env, xrpAsset, lender, brokerParams);
        // Create a loan with principal 804 XRP and 0% interest (so
        // DebtTotal increases by exactly 804)
        env(loan::set(borrower, brokerInfo.brokerID, xrpAsset(804).value()),
            loan::interestRate(TenthBips32(0)),
            sig(sfCounterpartySignature, lender),
            fee(env.current()->fees().base * 2));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Verify DebtTotal is exactly 804
        if (auto const brokerSle =
                env.le(keylet::loanbroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));
        }

        // Attempt to withdraw 2 XRP to self, leaving 80 XRP CoverAvailable.
        // The minimum is 80.4 XRP, which rounds up to 81 XRP, so this fails.
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(2).value()),
            ter(tecINSUFFICIENT_FUNDS));
        BEAST_EXPECT(env.ter() == tecINSUFFICIENT_FUNDS);
        env.close();

        // Attempt to withdraw 1 XRP to self, leaving 81 XRP CoverAvailable.
        // because that leaves sufficient cover, this succeeds
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(1).value()));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Validate CoverAvailable == 80 XRP and DebtTotal remains 804
        if (auto const brokerSle =
                env.le(keylet::loanbroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(
                brokerSle->at(sfCoverAvailable) == xrpAsset(81).value());
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));

            // Also demonstrate that the true minimum (804 * 10%) exceeds 80
            auto const theoreticalMin =
                tenthBipsOfValue(Number(804), TenthBips32(10'000));
            log << "Theoretical min cover: " << theoreticalMin << std::endl;
            BEAST_EXPECT(Number(804, -1) == theoreticalMin);
        }
    }

    void
    testRIPD3902()
    {
        testcase("RIPD-3902 - 1 IOU loan payments");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 10,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{1, 0},
            .interest = TenthBips32{100'000},
            .payTotal = 5,
            .payInterval = 150,
            .gracePd = 60};

        auto const assetType = AssetType::IOU;

        Env env(*this, all);

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, borrower);

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

    void
    testBorrowerIsBroker()
    {
        testcase("Test Borrower is Broker");
        using namespace jtx;
        using namespace loan;
        Account const broker{"broker"};
        Account const issuer{"issuer"};
        Account const borrower_{"borrower"};
        Account const depositor{"depositor"};

        auto testLoanAsset = [&](auto&& getMaxDebt, auto const& borrower) {
            Env env(*this);
            Vault vault(env);

            if (borrower == broker)
                env.fund(XRP(10'000), broker, issuer, depositor);
            else
                env.fund(XRP(10'000), broker, borrower, issuer, depositor);
            env.close();

            auto const xrpFee = XRP(100);
            auto const txFee = fee(xrpFee);

            STAmount const debtMaximumRequest = getMaxDebt(env);

            auto const& asset = debtMaximumRequest.asset();
            auto const initialVault = asset(debtMaximumRequest * 100);

            auto [tx, vaultKeylet] =
                vault.create({.owner = broker, .asset = asset});
            env(tx, txFee);
            env.close();

            env(vault.deposit(
                    {.depositor = depositor,
                     .id = vaultKeylet.key,
                     .amount = initialVault}),
                txFee);
            env.close();

            auto const brokerKeylet =
                keylet::loanbroker(broker.id(), env.seq(broker));

            env(loanBroker::set(broker, vaultKeylet.key), txFee);
            env.close();

            auto const serviceFee = 101;

            env(set(broker, brokerKeylet.key, debtMaximumRequest),
                counterparty(borrower),
                sig(sfCounterpartySignature, borrower),
                loanServiceFee(serviceFee),
                paymentTotal(10),
                txFee);
            env.close();

            std::uint32_t const loanSequence = 1;
            auto const loanKeylet =
                keylet::loan(brokerKeylet.key, loanSequence);

            auto const brokerBalanceBefore = env.balance(broker, asset);

            if (auto const loanSle = env.le(loanKeylet);
                env.test.BEAST_EXPECT(loanSle))
            {
                auto const payment = loanSle->at(sfPeriodicPayment);
                auto const totalPayment = payment + serviceFee;
                env(loan::pay(borrower, loanKeylet.key, asset(totalPayment)),
                    txFee);
                env.close();
                if (auto const vaultSle = env.le(vaultKeylet);
                    BEAST_EXPECT(vaultSle))
                {
                    auto const expected = [&]() {
                        // The service fee is transferred to the broker if
                        // a borrower is not the broker
                        if (borrower != broker)
                            return brokerBalanceBefore.number() + serviceFee;
                        // Since a borrower is the broker, the payment is
                        // transferred to the Vault from the broker but not
                        // the service fee.
                        // If the asset is XRP then the broker pays the txfee.
                        if (asset.native())
                            return brokerBalanceBefore.number() - payment -
                                xrpFee.number();
                        return brokerBalanceBefore.number() - payment;
                    }();
                    BEAST_EXPECT(
                        env.balance(broker, asset).value() ==
                        asset(expected).value());
                }
            }
        };
        // Test when a borrower is the broker and is not to verify correct
        // service fee transfer in both cases.
        for (auto const& borrowerAcct : {broker, borrower_})
        {
            testLoanAsset(
                [&](Env&) -> STAmount { return STAmount{XRPAmount{200'000}}; },
                borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    auto const IOU = issuer["USD"];
                    env(trust(broker, IOU(1'000'000'000)));
                    env(trust(depositor, IOU(1'000'000'000)));
                    env(pay(issuer, broker, IOU(100'000'000)));
                    env(pay(issuer, depositor, IOU(100'000'000)));
                    env.close();
                    return IOU(200'000);
                },
                borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    MPTTester mpt(
                        {.env = env,
                         .issuer = issuer,
                         .holders = {broker, depositor},
                         .pay = 100'000'000});
                    return mpt(200'000);
                },
                borrowerAcct);
        }
    }

    void
    testIssuerIsBorrower()
    {
        testcase("RIPD-4096 - Issuer as borrower");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = issuer,
            .principalRequest = Number{10000}};

        auto const assetType = AssetType::IOU;

        Env env(*this, all);

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, issuer);

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
            issuer,
            PaymentParameters{.showStepBalances = true});
    }

    void
    testLimitExceeded()
    {
        testcase("RIPD-4125 - overpayment");

        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        BrokerParameters const brokerParams{
            .vaultDeposit = 100'000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = Number{200000, -6},
            .interest = TenthBips32{50000},
            .payTotal = 3,
            .payInterval = 200,
            .gracePd = 60,
            .flags = tfLoanOverpayment,
        };

        auto const assetType = AssetType::XRP;

        Env env(
            *this,
            makeConfig(),
            all,
            nullptr,
            beast::severities::Severity::kWarning);

        auto loanResult = createLoan(
            env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (!BEAST_EXPECT(loanResult))
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        auto const state = getCurrentState(env, broker, loanKeylet);

        env(loan::pay(
            borrower,
            loanKeylet.key,
            STAmount{broker.asset, state.periodicPayment * 3 / 2 + 1},
            tfLoanOverpayment));
        env.close();

        PaymentParameters paymentParams{
            //.overpaymentFactor = Number{15, -1},
            //.overpaymentExtra = Number{1, -6},
            //.flags = tfLoanOverpayment,
            .showStepBalances = true,
            //.validateBalances = false,
        };

        makeLoanPayments(
            env,
            broker,
            loanParams,
            loanKeylet,
            verifyLoanStatus,
            issuer,
            lender,
            borrower,
            paymentParams);
    }

public:
    void
    run() override
    {
#if LOANTODO
        testLoanPayLateFullPaymentBypassesPenalties();
        testLoanCoverMinimumRoundingExploit();
#endif
        testCoverDepositWithdrawNonTransferableMPT();
        testPoC_UnsignedUnderflowOnFullPayAfterEarlyPeriodic();

        testDisabled();
        testSelfLoan();
        testIssuerLoan();
        testLoanSet();
        testLifecycle();
        testServiceFeeOnBrokerDeepFreeze();

        testRPC();
        testBasicMath();

        testInvalidLoanDelete();
        testInvalidLoanManage();
        testInvalidLoanPay();
        testInvalidLoanSet();

        testBatchBypassCounterparty();
        testLoanPayComputePeriodicPaymentValidRateInvariant();
        testAccountSendMptMinAmountInvariant();
        testLoanPayDebtDecreaseInvariant();
        testWrongMaxDebtBehavior();
        testLoanPayComputePeriodicPaymentValidTotalInterestInvariant();
        testDosLoanPay();
        testLoanPayComputePeriodicPaymentValidTotalPrincipalPaidInvariant();
        testLoanPayComputePeriodicPaymentValidTotalInterestPaidInvariant();
        testLoanNextPaymentDueDateOverflow();

        testRequireAuth();
        testDustManipulation();

        testRIPD3831();
        testRIPD3459();
        testRIPD3901();
        testRIPD3902();
        testRoundingAllowsUndercoverage();
        testBorrowerIsBroker();
        testIssuerIsBorrower();
        testLimitExceeded();
    }
};

class LoanBatch_test : public Loan_test
{
protected:
    beast::xor_shift_engine engine_;

    std::uniform_int_distribution<> assetDist{0, 2};
    std::uniform_int_distribution<std::int64_t> principalDist{
        100'000,
        1'000'000'000};
    std::uniform_int_distribution<std::uint32_t> interestRateDist{0, 10000};
    std::uniform_int_distribution<> paymentTotalDist{12, 10000};
    std::uniform_int_distribution<> paymentIntervalDist{60, 3600 * 24 * 30};
    std::uniform_int_distribution<std::uint16_t> managementFeeRateDist{
        0,
        10'000};
    std::uniform_int_distribution<> serviceFeeDist{0, 20};
    /*
        # Generate parameters that are more likely to be valid
    principal = Decimal(str(rand.randint(100000,
   100'000'000))).quantize(ROUND_TARGET)

    interest_rate = Decimal(rand.randint(1, 10000)) /
   Decimal(100000)

    payment_total = rand.randint(12, 10000)

    payment_interval = Decimal(str(rand.randint(60, 2629746)))

    interest_fee = Decimal(rand.randint(0, 100000)) /
   Decimal(100000)
*/

    void
    testRandomLoan()
    {
        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        // Determine all the random parameters at once
        AssetType assetType = static_cast<AssetType>(assetDist(engine_));
        auto const principalRequest = principalDist(engine_);
        TenthBips16 managementFeeRate{managementFeeRateDist(engine_)};
        auto const serviceFee = serviceFeeDist(engine_);
        TenthBips32 interest{interestRateDist(engine_)};
        auto const payTotal = paymentTotalDist(engine_);
        auto const payInterval = paymentIntervalDist(engine_);

        BrokerParameters brokerParams{
            .vaultDeposit = principalRequest * 10,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = managementFeeRate};
        LoanParameters loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = principalRequest,
            .serviceFee = serviceFee,
            .interest = interest,
            .payTotal = payTotal,
            .payInterval = payInterval,
        };

        runLoan(assetType, brokerParams, loanParams);
    }

public:
    void
    run() override
    {
        auto const argument = arg();
        auto const numIterations = [s = arg()]() -> int {
            int defaultNum = 5;
            if (s.empty())
                return defaultNum;
            try
            {
                std::size_t pos;
                auto const r = stoi(s, &pos);
                if (pos != s.size())
                    return defaultNum;
                return r;
            }
            catch (...)
            {
                return defaultNum;
            }
        }();

        using namespace jtx;

        auto const updateInterval = std::min(numIterations / 5, 100);

        for (int i = 0; i < numIterations; ++i)
        {
            if (i % updateInterval == 0)
                testcase << "Random Loan Test iteration " << (i + 1) << "/"
                         << numIterations;
            testRandomLoan();
        }
    }
};

class LoanArbitrary_test : public LoanBatch_test
{
    void
    run() override
    {
        using namespace jtx;

        BrokerParameters const brokerParams{
            .vaultDeposit = 10000,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            // .managementFeeRate = TenthBips16{5919},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = Account("lender"),
            .counter = Account("borrower"),
            .principalRequest = Number{10000, 0},
            // .interest = TenthBips32{0},
            // .payTotal = 5816,
            .payInterval = 150};

        runLoan(AssetType::XRP, brokerParams, loanParams);
    }
};

BEAST_DEFINE_TESTSUITE(Loan, tx, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanBatch, tx, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanArbitrary, tx, ripple);

}  // namespace test
}  // namespace ripple
