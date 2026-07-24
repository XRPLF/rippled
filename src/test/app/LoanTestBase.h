#pragma once

#include <xrpl/beast/unit_test/suite.h>
//
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/batch.h>
#include <test/jtx/credentials.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/utility.h>
#include <test/jtx/vault.h>

#include <xrpld/rpc/detail/Handler.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/xor_shift_engine.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>
#include <xrpl/tx/transactors/system/Batch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl::test {

class LoanTestBase : public beast::unit_test::Suite
{
protected:
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    //
    // featureLendingProtocolV1_1 is excluded from the default set: it changes
    // Vault/LoanBroker accounting (AssetsTotal/DebtTotal/LossUnrealized), and
    // most of this file's tests assert whole-life-specific expected values
    // for those fields. Tests that specifically exercise the amendment opt
    // it back in explicitly (e.g. `all_ | featureLendingProtocolV1_1`).
    FeatureBitset const all_{jtx::testableAmendments() - featureLendingProtocolV1_1};
    std::string const iouCurrency_{"IOU"};

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
        // If set, the vault is created with this sfScale value. Useful for
        // tests that need finer loanScale to exercise rounding edge cases.
        std::optional<std::uint8_t> vaultScale =
            std::nullopt;  // NOLINT(readability-redundant-member-init)

        [[nodiscard]] Number
        maxCoveredLoanValue(Number const& currentDebt) const
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            auto debtLimit = coverDeposit * kTenthBipsPerUnity.value() / coverRateMin.value();

            return debtLimit - currentDebt;
        }

        static BrokerParameters const&
        defaults()
        {
            static BrokerParameters const kResult{};
            return kResult;
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
            jtx::PrettyAsset const& asset,
            Keylet const& brokerKeylet,
            Keylet const& vaultKeylet,
            BrokerParameters p)
            : asset(asset)
            , brokerID(brokerKeylet.key)
            , vaultID(vaultKeylet.key)
            , params(std::move(p))
        {
        }

        [[nodiscard]] Keylet
        brokerKeylet() const
        {
            return keylet::loanBroker(brokerID);
        }
        [[nodiscard]] Keylet
        vaultKeylet() const
        {
            return keylet::vault(vaultID);
        }

        [[nodiscard]] int
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

        template <class... FN>
        jtx::JTx
        operator()(jtx::Env& env, BrokerInfo const& broker, FN const&... fN) const
        {
            using namespace jtx;
            using namespace jtx::loan;

            JTx jt{loan::set(
                account,
                broker.brokerID,
                broker.asset(principalRequest).number(),
                flags.value_or(0))};

            Sig(sfCounterpartySignature, counter)(env, jt);

            Fee{setFee.value_or(env.current()->fees().base * 2)}(env, jt);

            if (counterpartyExplicit)
                kCounterparty(counter)(env, jt);
            if (originationFee)
                kLoanOriginationFee(broker.asset(*originationFee).number())(env, jt);
            if (serviceFee)
                kLoanServiceFee(broker.asset(*serviceFee).number())(env, jt);
            if (lateFee)
                kLatePaymentFee(broker.asset(*lateFee).number())(env, jt);
            if (closeFee)
                kClosePaymentFee(broker.asset(*closeFee).number())(env, jt);
            if (overFee)
                kOverpaymentFee (*overFee)(env, jt);
            if (interest)
                kInterestRate (*interest)(env, jt);
            if (lateInterest)
                kLateInterestRate (*lateInterest)(env, jt);
            if (closeInterest)
                kCloseInterestRate (*closeInterest)(env, jt);
            if (overpaymentInterest)
                kOverpaymentInterestRate (*overpaymentInterest)(env, jt);
            if (payTotal)
                kPaymentTotal (*payTotal)(env, jt);
            if (payInterval)
                kPaymentInterval (*payInterval)(env, jt);
            if (gracePd)
                kGracePeriod (*gracePd)(env, jt);

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
            static PaymentParameters const kResult{};
            return kResult;
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

    /**
     * Helper class to compare the expected state of a loan and loan broker
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
            jtx::Env const& env,
            BrokerInfo const& broker,
            jtx::Account const& pseudo,
            Keylet const& keylet)
            : env(env), broker(broker), pseudoAccount(pseudo), loanKeylet(keylet)
        {
        }

        /**
         * Checks the expected broker state against the ledger
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
            if (auto brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                env.test.BEAST_EXPECT(brokerSle))
            {
                TenthBips16 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
                auto const brokerDebt = brokerSle->at(sfDebtTotal);

                if (auto vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                    env.test.BEAST_EXPECT(vaultSle))
                {
                    auto const expectedDebt =
                        env.current()->rules().enabled(featureLendingProtocolV1_1) &&
                            getVaultVersion(vaultSle) == VaultVersion::CashBasis
                        ? principalOutstanding
                        : principalOutstanding + interestOwed;
                    env.test.BEAST_EXPECT(brokerDebt == expectedDebt);
                    env.test.BEAST_EXPECT(
                        env.balance(pseudoAccount, broker.asset).number() ==
                        brokerSle->at(sfCoverAvailable));
                    env.test.BEAST_EXPECT(brokerSle->at(sfOwnerCount) == ownerCount);

                    Account const vaultPseudo{"vaultPseudoAccount", vaultSle->at(sfAccount)};
                    env.test.BEAST_EXPECT(
                        vaultSle->at(sfAssetsAvailable) ==
                        env.balance(vaultPseudo, broker.asset).number());
                    if (ownerCount == 0)
                    {
                        // The Vault must be perfectly balanced if there
                        // are no loans outstanding
                        auto const total = vaultSle->at(sfAssetsTotal);
                        auto const available = vaultSle->at(sfAssetsAvailable);
                        env.test.BEAST_EXPECT(total == available);
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
                    roundToScale(difference, loanScale) >= beast::kZero,
                    "Balance before: " + to_string(balanceBefore.value()) +
                        ", expected change: " + to_string(balanceChangeAmount) +
                        ", difference (balance after - expected): " + to_string(difference),
                    __FILE__,
                    __LINE__);
            }
        }

        /**
         * Checks both the loan and broker expect states against the ledger
         */
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

                auto const ls = constructLoanState(loan);

                auto const interestRate = TenthBips32{loan->at(sfInterestRate)};
                auto const paymentInterval = loan->at(sfPaymentInterval);
                checkBroker(
                    principalOutstanding,
                    ls.interestDue,
                    interestRate,
                    paymentInterval,
                    paymentRemaining,
                    1);

                if (auto brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                    env.test.BEAST_EXPECT(brokerSle))
                {
                    if (auto vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                        env.test.BEAST_EXPECT(vaultSle))
                    {
                        if (((flags & lsfLoanImpaired) != 0u) && ((flags & lsfLoanDefault) == 0u))
                        {
                            env.test.BEAST_EXPECT(
                                vaultSle->at(sfLossUnrealized) ==
                                (env.current()->rules().enabled(featureLendingProtocolV1_1) &&
                                         getVaultVersion(vaultSle) == VaultVersion::CashBasis
                                     ? principalOutstanding
                                     : totalValue - managementFeeOutstanding));
                        }
                        else
                        {
                            env.test.BEAST_EXPECT(vaultSle->at(sfLossUnrealized) == 0);
                        }
                    }
                }
            }
        }

        /**
         * Checks both the loan and broker expect states against the ledger
         */
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

        Vault const vault{env};

        auto const deposit = asset(params.vaultDeposit);
        auto const debtMaximumValue = asset(params.debtMax).value();
        auto const coverDepositValue = asset(params.coverDeposit).value();

        auto const coverRateMinValue = params.coverRateMin;

        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        if (params.vaultScale)
            tx[sfScale] = *params.vaultScale;
        env(tx);
        env.close();
        BEAST_EXPECT(env.le(vaultKeylet));

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = deposit}));
        env.close();
        if (auto const vault = env.le(keylet::vault(vaultKeylet.key)); BEAST_EXPECT(vault))
        {
            BEAST_EXPECT(vault->at(sfAssetsAvailable) == deposit.value());
        }

        auto const keylet = keylet::loanBroker(lender.id(), env.seq(lender));

        using namespace loanBroker;
        env(set(lender, vaultKeylet.key, params.flags),
            kData(params.data),
            kManagementFeeRate(params.managementFeeRate),
            kDebtMaximum(debtMaximumValue),
            kCoverRateMinimum(coverRateMinValue),
            kCoverRateLiquidation(TenthBips32(params.coverRateLiquidation)));

        if (coverDepositValue != beast::kZero)
            env(coverDeposit(lender, keylet.key, coverDepositValue));

        env.close();

        return {asset, keylet, vaultKeylet, params};
    }

    /**
     * Get the state without checking anything
     */
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

    /**
     * Get the state and check the values against the parameters used in
     * `lifecycle`
     */
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
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
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
        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            if (auto const vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                BEAST_EXPECT(vaultSle))
            {
                // log << vaultSle->getJson() << std::endl;
                auto const assetsUnavailable =
                    vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable);
                auto const unrealizedLoss = vaultSle->at(sfLossUnrealized) +
                    (env.current()->rules().enabled(featureLendingProtocolV1_1) &&
                             getVaultVersion(vaultSle) == VaultVersion::CashBasis
                         ? state.principalOutstanding
                         : state.totalValue - state.managementFeeOutstanding);

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
                PrettyAsset const asset{issuer[iouCurrency_]};

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
                    env.fund(env.current()->fees().accountReserve(10, 1) * 10, issuer);
                if (!env.le(keylet::account(lender)))
                    env.fund(env.current()->fees().accountReserve(10, 1) * 10, noripple(lender));
                if (!env.le(keylet::account(borrower)))
                    env.fund(env.current()->fees().accountReserve(10, 1) * 10, noripple(borrower));

                MPTTester mptt{env, issuer, kMptInitNoFund};
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

    // Predicts the keylet of the next loan `broker` will originate, before
    // that loan exists, by reading the broker's current LoanSequence.
    static Keylet
    nextLoanKeylet(jtx::Env const& env, BrokerInfo const& broker)
    {
        auto const brokerStateBefore = env.le(keylet::loanBroker(broker.brokerID));
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        return keylet::loan(broker.brokerID, loanSequence);
    }

    // Funds issuer/lender/borrower with XRP, creates an IOU asset issued by
    // `issuer`, establishes trustlines for lender and borrower, and pays
    // them starting balances. This is the exact setup shared by several of
    // the fuzzer-derived regression tests below.
    jtx::PrettyAsset
    createFundedIouAsset(
        jtx::Env& env,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower,
        Number const& lenderPay = 100'000'000,
        Number const& borrowerPay = 1'000'000)
    {
        using namespace jtx;

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        PrettyAsset const iouAsset = issuer[iouCurrency_];
        auto trustLenderTx = env.json(trust(lender, iouAsset(1'000'000'000)));
        env(trustLenderTx);
        auto trustBorrowerTx = env.json(trust(borrower, iouAsset(1'000'000'000)));
        env(trustBorrowerTx);
        auto payLenderTx = pay(issuer, lender, iouAsset(lenderPay));
        env(payLenderTx);
        auto payIssuerTx = pay(issuer, borrower, iouAsset(borrowerPay));
        env(payIssuerTx);
        env.close();

        return iouAsset;
    }

    // Funds issuer/lender/borrower with XRP, sets DefaultRipple on the
    // issuer, creates a "USD" IOU asset with a large trust limit, and pays
    // lender/borrower starting balances. Shared setup for several
    // overpayment/rounding regression tests below.
    static jtx::PrettyAsset
    createFundedRippleIouAsset(
        jtx::Env& env,
        jtx::Account const& issuer,
        jtx::Account const& lender,
        jtx::Account const& borrower,
        Number const& lenderPay = 1'000'000,
        Number const& borrowerPay = 1'000'000)
    {
        using namespace jtx;

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const iouAsset = issuer["USD"];
        STAmount const iouLimit{iouAsset.raw(), Number{9'999'999'999'999'999LL}};
        env(trust(lender, iouLimit));
        env(trust(borrower, iouLimit));
        env(pay(issuer, lender, iouAsset(lenderPay)));
        env(pay(issuer, borrower, iouAsset(borrowerPay)));
        env.close();

        return iouAsset;
    }

    // Returns the broker's pseudo-account, or `fallback` if the broker's
    // ledger entry cannot be read.
    jtx::Account
    brokerPseudoAccount(jtx::Env const& env, BrokerInfo const& broker, jtx::Account const& fallback)
    {
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        if (!BEAST_EXPECT(brokerSle))
            return fallback;
        auto const brokerPseudo = brokerSle->at(sfAccount);
        return jtx::Account("Broker pseudo-account", brokerPseudo);
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
        auto const interval = loanParams.payInterval.value_or(LoanSet::kDefaultPaymentInterval);
        auto const total = loanParams.payTotal.value_or(LoanSet::kDefaultPaymentTotal);
        auto const feeRate = brokerParams.managementFeeRate;
        auto const props = computeLoanProperties(
            env.current()->rules(),
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
            loanParams.interest.value_or(TenthBips32{}) != beast::kZero,
            loanParams.payTotal.value_or(LoanSet::kDefaultPaymentTotal),
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
        env.fund(env.current()->fees().accountReserve(10, 1) * 10, issuer);
        if (lender != issuer)
            env.fund(env.current()->fees().accountReserve(10, 1) * 10, noripple(lender));
        if (borrower != issuer && borrower != lender)
            env.fund(env.current()->fees().accountReserve(10, 1) * 10, noripple(borrower));

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
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::nullopt;
            auto const brokerPseudo = brokerSle->at(sfAccount);
            return Account("Broker pseudo-account", brokerPseudo);
        }();
        if (!pseudoAcctOpt)
            return std::nullopt;
        Account const& pseudoAcct = *pseudoAcctOpt;

        auto const loanKeyletOpt = [&]() -> std::optional<Keylet> {
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
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
            (broker.asset.native() ? Number(
                                         baseFee * state.paymentRemaining +
                                         accountReserve(*env.current(), borrower.id(), env.journal))
                                   : broker.asset(15).number());

        auto const shortage = totalNeeded - borrowerBalance.number();

        if (shortage > beast::kZero && (broker.asset.native() || issuer != borrower))
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
        STAmount const totalDue = roundToScale(
            roundedPeriodicPayment + serviceFee, state.loanScale, Number::RoundingMode::Upward);

        auto currentRoundedState = constructLoanState(
            state.totalValue, state.principalOutstanding, state.managementFeeOutstanding);
        {
            auto const raw = computeTheoreticalLoanState(
                env.current()->rules(),
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
        xrpl::detail::PaymentComponents totalPaid{
            .trackedValueDelta = 0, .trackedPrincipalDelta = 0, .trackedManagementFeeDelta = 0};
        Number totalInterestPaid = 0;
        Number totalFeesPaid = 0;
        std::size_t totalPaymentsMade = 0;

        xrpl::LoanState currentTrueState = computeTheoreticalLoanState(
            env.current()->rules(),
            state.periodicPayment,
            periodicRate,
            state.paymentRemaining,
            broker.params.managementFeeRate);

        auto validateBorrowerBalance = [&]() {
            if (borrower == issuer || !paymentParams.validateBalances)
                return;
            auto const totalSpent =
                (totalPaid.trackedValueDelta + totalFeesPaid +
                 (broker.asset.native() ? Number(baseFee) * totalPaymentsMade : kNumZero));
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
            auto const paymentComponents = xrpl::detail::computePaymentComponents(
                env.current()->rules(),
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
                (paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final &&
                 paymentComponents.trackedValueDelta >= roundedPeriodicPayment));
            BEAST_EXPECT(
                paymentComponents.trackedValueDelta ==
                paymentComponents.trackedPrincipalDelta + paymentComponents.trackedInterestPart() +
                    paymentComponents.trackedManagementFeeDelta);

            xrpl::LoanState const nextTrueState = computeTheoreticalLoanState(
                env.current()->rules(),
                state.periodicPayment,
                periodicRate,
                state.paymentRemaining - 1,
                broker.params.managementFeeRate);
            xrpl::detail::LoanStateDeltas const deltas = currentTrueState - nextTrueState;
            BEAST_EXPECT(
                deltas.total() == deltas.principal + deltas.interest + deltas.managementFee);
            BEAST_EXPECT(
                paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
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
                    if (paymentComponents.specialCase == ::xrpl::detail::PaymentSpecialCase::Final)
                        return "final";
                    if (paymentComponents.specialCase == ::xrpl::detail::PaymentSpecialCase::Extra)
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
                    paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
                    diff == beast::kZero ||
                    (diff > beast::kZero &&
                     ((broker.asset.integral() && (static_cast<Number>(diff) < 3)) ||
                      (state.loanScale - diff.exponent() > 13))));

                BEAST_EXPECT(
                    paymentComponents.trackedPrincipalDelta >= beast::kZero &&
                    paymentComponents.trackedPrincipalDelta <= state.principalOutstanding);
                BEAST_EXPECT(
                    paymentComponents.specialCase != xrpl::detail::PaymentSpecialCase::Final ||
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
                auto const current = constructLoanState(loanSle);
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
            if (paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final)
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
        LoanParameters const& loanParams,
        FeatureBitset features)
    {
        using namespace jtx;

        Account const issuer("issuer");
        Account const lender("lender");
        Account const borrower("borrower");

        Env env(*this, features);

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);
        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

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

    /**
     * Runs through the complete lifecycle of a loan
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
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
            {
                // will be invalid
                return std::make_pair(keylet::loan(broker.brokerID), std::uint32_t(0));
            }

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

        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
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
            env.current()->rules(),
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
        env(manage(evan, keylet.key, 0), Ter(tecNO_PERMISSION));
        // unknown flags
        env(manage(lender, keylet.key, tfLoanManageMask), Ter(temINVALID_FLAG));
        // combinations of flags are not allowed
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanImpair), Ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanImpair | tfLoanDefault), Ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanDefault), Ter(temINVALID_FLAG));
        env(manage(lender, keylet.key, tfLoanUnimpair | tfLoanImpair | tfLoanDefault),
            Ter(temINVALID_FLAG));
        // invalid loan ID
        env(manage(lender, broker.brokerID, tfLoanImpair), Ter(tecNO_ENTRY));
        // Loan is unimpaired, can't unimpair it again
        env(manage(lender, keylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
        // Loan is unimpaired, it can go into default, but only after it's past
        // due
        env(manage(lender, keylet.key, tfLoanDefault), Ter(tecTOO_SOON));

        // Check the vault
        bool const canImpair = canImpairLoan(env, broker, state);
        // Impair the loan, if possible
        env(manage(lender, keylet.key, tfLoanImpair),
            canImpair ? Ter(tesSUCCESS) : Ter(tecLIMIT_EXCEEDED));
        // Unimpair the loan
        env(manage(lender, keylet.key, tfLoanUnimpair),
            canImpair ? Ter(tesSUCCESS) : Ter(tecNO_PERMISSION));

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
        env(del(lender, keylet.key), Ter(tecHAS_OBLIGATIONS));

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
        env(loanBroker::del(lender, broker.brokerID), Ter(tecHAS_OBLIGATIONS));
        // Ensure the above tx doesn't get ordered after the LoanDelete and
        // delete our broker!
        env.close();

        // Test failure cases
        env(del(lender, keylet.key, tfLoanOverpayment), Ter(temINVALID_FLAG));
        env(del(evan, keylet.key), Ter(tecNO_PERMISSION));
        env(del(lender, broker.brokerID), Ter(tecNO_ENTRY));

        // Delete the loan
        // Either the borrower or the lender can delete the loan. Alternate
        // between who does it across tests.
        static unsigned kDeleteCounter = 0;
        auto const deleter = ((++kDeleteCounter % 2) != 0u) ? lender : borrower;
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

        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
        }
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
