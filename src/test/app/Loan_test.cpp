#include <test/app/LoanTestBase.h>

namespace xrpl::test {

class Loan_test : public LoanTestBase
{
private:
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

            auto const keylet = keylet::loanBroker(alice, env.seq(alice));

            using namespace std::chrono_literals;
            using namespace loan;

            // counter party signature is optional on LoanSet. Confirm that by
            // sending transaction without one.
            auto setTx = env.jt(set(alice, keylet.key, Number(10000)), Ter(temDISABLED));
            env(setTx);

            // All loan transactions are disabled.
            // 1. LoanSet
            setTx = env.jt(setTx, Sig(sfCounterpartySignature, bob), Ter(temDISABLED));
            env(setTx);
            // Actual sequence will be based off the loan broker, but we
            // obviously don't have one of those if the amendment is disabled
            auto const loanKeylet = keylet::loan(keylet.key, env.seq(alice));
            // Other Loan transactions are disabled, too.
            // 2. LoanDelete
            env(del(alice, loanKeylet.key), Ter(temDISABLED));
            // 3. LoanManage
            env(manage(alice, loanKeylet.key, tfLoanImpair), Ter(temDISABLED));
            // 4. LoanPay
            env(pay(alice, loanKeylet.key, XRP(500)), Ter(temDISABLED));
        };
        failAll(all_ - featureMPTokensV1);
        failAll(all_ - featureSingleAssetVault - featureLendingProtocol);
        failAll(all_ - featureSingleAssetVault);
        failAll(all_ - featureLendingProtocol);
    }

    /**
     * Wrapper to run a series of lifecycle tests for a given asset and loan
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

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        auto const pseudoAcct = brokerPseudoAccount(env, broker, lender);

        auto const baseFee = env.current()->fees().base;

        auto badKeylet = keylet::vault(lender.id(), env.seq(lender));
        // Try some failure cases
        // flags are checked first
        env(set(evan, broker.brokerID, principalRequest, tfLoanSetMask),
            Sig(sfCounterpartySignature, lender),
            loanSetFee,
            Ter(temINVALID_FLAG));

        // field length validation
        // sfData: good length, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kData(std::string(kMaxDataPayloadLength, 'X')),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfData: too long
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kData(std::string(kMaxDataPayloadLength + 1, 'Y')),
            loanSetFee,
            Ter(temINVALID));

        // field range validation
        // sfOverpaymentFee: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kOverpaymentFee(kMaxOverpaymentFee),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfOverpaymentFee: too big
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kOverpaymentFee(kMaxOverpaymentFee + 1),
            loanSetFee,
            Ter(temINVALID));

        // sfInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kInterestRate(kMaxInterestRate),
            loanSetFee,
            Ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kInterestRate(TenthBips32(0)),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(kMaxInterestRate + 1),
            loanSetFee,
            Ter(temINVALID));
        // sfInterestRate: too small
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32(-1)),
            loanSetFee,
            Ter(temINVALID));

        // sfLateInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kLateInterestRate(kMaxLateInterestRate),
            loanSetFee,
            Ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kLateInterestRate(TenthBips32(0)),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfLateInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kLateInterestRate(kMaxLateInterestRate + 1),
            loanSetFee,
            Ter(temINVALID));
        // sfLateInterestRate: too small
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kLateInterestRate(TenthBips32(-1)),
            loanSetFee,
            Ter(temINVALID));

        // sfCloseInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kCloseInterestRate(kMaxCloseInterestRate),
            loanSetFee,
            Ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kCloseInterestRate(TenthBips32(0)),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfCloseInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kCloseInterestRate(kMaxCloseInterestRate + 1),
            loanSetFee,
            Ter(temINVALID));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kCloseInterestRate(TenthBips32(-1)),
            loanSetFee,
            Ter(temINVALID));

        // sfOverpaymentInterestRate: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kOverpaymentInterestRate(kMaxOverpaymentInterestRate),
            loanSetFee,
            Ter(tefBAD_AUTH));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kOverpaymentInterestRate(TenthBips32(0)),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfOverpaymentInterestRate: too big
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kOverpaymentInterestRate(kMaxOverpaymentInterestRate + 1),
            loanSetFee,
            Ter(temINVALID));
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kOverpaymentInterestRate(TenthBips32(-1)),
            loanSetFee,
            Ter(temINVALID));

        // sfPaymentTotal: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kPaymentTotal(LoanSet::kMinPaymentTotal),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfPaymentTotal: too small (there is no max)
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kPaymentTotal(LoanSet::kMinPaymentTotal - 1),
            loanSetFee,
            Ter(temINVALID));

        // sfPaymentInterval: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kPaymentInterval(LoanSet::kMinPaymentInterval),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfPaymentInterval: too small (there is no max)
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kPaymentInterval(LoanSet::kMinPaymentInterval - 1),
            loanSetFee,
            Ter(temINVALID));

        // sfGracePeriod: good value, bad account
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, borrower),
            kPaymentInterval(LoanSet::kMinPaymentInterval * 2),
            kGracePeriod(LoanSet::kMinPaymentInterval * 2),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // sfGracePeriod: larger than paymentInterval
        env(set(evan, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            kPaymentInterval(LoanSet::kMinPaymentInterval * 2),
            kGracePeriod(LoanSet::kMinPaymentInterval * 3),
            loanSetFee,
            Ter(temINVALID));

        // insufficient fee - single sign
        env(set(borrower, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, lender),
            Ter(telINSUF_FEE_P));
        // insufficient fee - multisign
        env(signers(lender, 2, {{evan, 1}, {borrower, 1}}));
        env(signers(borrower, 2, {{evan, 1}, {lender, 1}}));
        env(set(borrower, broker.brokerID, principalRequest),
            kCounterparty(lender),
            Msig(evan, lender),
            Msig(sfCounterpartySignature, evan, borrower),
            Fee(env.current()->fees().base * 5 - 1),
            Ter(telINSUF_FEE_P));
        // Bad multisign signatures for borrower (Account)
        env(set(borrower, broker.brokerID, principalRequest),
            kCounterparty(lender),
            Msig(alice, issuer),
            Msig(sfCounterpartySignature, evan, borrower),
            Fee(env.current()->fees().base * 5),
            Ter(tefBAD_SIGNATURE));
        // Bad multisign signatures for issuer (Counterparty)
        env(set(borrower, broker.brokerID, principalRequest),
            kCounterparty(lender),
            Msig(evan, lender),
            Msig(sfCounterpartySignature, alice, issuer),
            Fee(env.current()->fees().base * 5 - 1),
            Ter(tefBAD_SIGNATURE));
        env(signers(lender, kNone));
        env(signers(borrower, kNone));
        // multisign sufficient fee, but no signers set up
        env(set(borrower, broker.brokerID, principalRequest),
            kCounterparty(lender),
            Msig(evan, lender),
            Msig(sfCounterpartySignature, evan, borrower),
            Fee(env.current()->fees().base * 5),
            Ter(tefNOT_MULTI_SIGNING));
        // not the broker owner, no counterparty, not signed by broker
        // owner
        env(set(borrower, broker.brokerID, principalRequest),
            Sig(sfCounterpartySignature, evan),
            loanSetFee,
            Ter(tefBAD_AUTH));
        // not the broker owner, counterparty is borrower
        env(set(evan, broker.brokerID, principalRequest),
            kCounterparty(borrower),
            Sig(sfCounterpartySignature, borrower),
            loanSetFee,
            Ter(tecNO_PERMISSION));
        // not a LoanBroker object, no counterparty
        env(set(lender, badKeylet.key, principalRequest),
            Sig(sfCounterpartySignature, evan),
            loanSetFee,
            Ter(temBAD_SIGNER));
        // not a LoanBroker object, counterparty is valid
        env(set(lender, badKeylet.key, principalRequest),
            kCounterparty(borrower),
            Sig(sfCounterpartySignature, borrower),
            loanSetFee,
            Ter(tecNO_ENTRY));
        // borrower doesn't exist
        env(set(lender, broker.brokerID, principalRequest),
            kCounterparty(alice),
            Sig(sfCounterpartySignature, alice),
            loanSetFee,
            Ter(terNO_ACCOUNT));

        // Request more funds than the vault has available
        env(set(evan, broker.brokerID, totalVaultRequest + 1),
            Sig(sfCounterpartySignature, lender),
            loanSetFee,
            Ter(tecINSUFFICIENT_FUNDS));

        // Request more funds than the broker's first-loss capital can
        // cover.
        env(set(evan, broker.brokerID, maxCoveredLoanRequest + 1),
            Sig(sfCounterpartySignature, lender),
            loanSetFee,
            Ter(tecINSUFFICIENT_FUNDS));

        // Frozen trust line / locked MPT issuance
        // XRP can not be frozen, but run through the loop anyway to test
        // the tecLIMIT_EXCEEDED case
        {
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return;

            auto const vaultPseudo = [&]() {
                auto const vaultSle = env.le(keylet::vault(brokerSle->at(sfVaultID)));
                if (!BEAST_EXPECT(vaultSle))
                {
                    // This will be wrong, but the test has failed anyway.
                    return Account{lender};
                }
                auto vaultPseudo = Account("Vault pseudo-account", vaultSle->at(sfAccount));
                return vaultPseudo;
            }();

            auto const [freeze, deepfreeze, unfreeze, expectedResult] =
                [&]() -> std::tuple<
                          std::function<void(Account const& holder)>,
                          std::function<void(Account const& holder)>,
                          std::function<void(Account const& holder)>,
                          TER> {
                // Freeze / lock the asset
                std::function<void(Account const& holder)> const empty;
                if (broker.asset.native())
                {
                    // XRP can't be frozen
                    return std::make_tuple(empty, empty, empty, tesSUCCESS);
                }
                if (broker.asset.holds<Issue>())
                {
                    auto freeze = [&](Account const& holder) {
                        env(trust(issuer, holder[iouCurrency_](0), tfSetFreeze));
                    };
                    auto deepfreeze = [&](Account const& holder) {
                        env(trust(issuer, holder[iouCurrency_](0), tfSetFreeze | tfSetDeepFreeze));
                    };
                    auto unfreeze = [&](Account const& holder) {
                        env(trust(
                            issuer, holder[iouCurrency_](0), tfClearFreeze | tfClearDeepFreeze));
                    };
                    return std::make_tuple(freeze, deepfreeze, unfreeze, tecFROZEN);
                }

                auto freeze = [&](Account const& holder) {
                    mptt.set({.account = issuer, .holder = holder, .flags = tfMPTLock});
                };
                auto unfreeze = [&](Account const& holder) {
                    mptt.set({.account = issuer, .holder = holder, .flags = tfMPTUnlock});
                };
                return std::make_tuple(freeze, empty, unfreeze, tecLOCKED);
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
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Ter(expectedResult));

                    // Unfreeze the account
                    BEAST_EXPECT(unfreeze);
                    unfreeze(account);

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan, broker.brokerID, debtMaximumRequest + 1),
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Ter(tecLIMIT_EXCEEDED));
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
                env(trust(evan, issuer[iouCurrency_](100'000)));

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
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Ter(expectedResult));

                    // Unfreeze evan
                    BEAST_EXPECT(unfreeze);
                    unfreeze(account);

                    // Ensure the line is unfrozen with a request that is fine
                    // except too it requests more principal than the broker can
                    // carry
                    env(set(evan, broker.brokerID, debtMaximumRequest + 1),
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Ter(tecLIMIT_EXCEEDED));
                }
            }
        }

        // Finally! Create a loan

        auto coverAvailable = [&env, this](uint256 const& brokerID, Number const& expected) {
            if (auto const brokerSle = env.le(keylet::loanBroker(brokerID));
                BEAST_EXPECT(brokerSle))
            {
                auto const available = brokerSle->at(sfCoverAvailable);
                BEAST_EXPECT(available == expected);
                return available;
            }
            return Number{};
        };
        auto getDefaultInfo = [&env, this](LoanState const& state, BrokerInfo const& broker) {
            if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(
                    state.loanScale >=
                    (broker.asset.integral()
                         ? 0
                         : std::max(
                               broker.vaultScale(env), state.principalOutstanding.exponent())));
                NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
                auto const defaultAmount = roundToAsset(
                    broker.asset,
                    std::min(
                        tenthBipsOfValue(
                            tenthBipsOfValue(
                                brokerSle->at(sfDebtTotal), broker.params.coverRateMin),
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
                        canImpair ? Ter(tesSUCCESS) : Ter(tecLIMIT_EXCEEDED));

                    if (canImpair)
                    {
                        state.flags |= tfLoanImpair;
                        state.nextPaymentDate = env.now().time_since_epoch().count();

                        // Once the loan is impaired, it can't be impaired again
                        env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
                    }
                    verifyLoanStatus(state);
                }

                auto const nextDueDate = tp{d{state.nextPaymentDate}};

                // Can't default the loan yet. The grace period hasn't
                // expired
                env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecTOO_SOON));

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
                env(manage(lender, loanKeylet.key, tfLoanUnimpair), Ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
                // Can't make a payment on it either
                env(pay(borrower, loanKeylet.key, broker.asset(300)), Ter(tecKILLED));
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
                Ter(temINVALID));
            // broker.asset(80) is less than a single payment, but all these
            // checks fail before that matters
            env(pay(borrower, loanKeylet.key, broker.asset(-80), txFlags), Ter(temBAD_AMOUNT));
            env(pay(borrower, broker.brokerID, broker.asset(80), txFlags), Ter(tecNO_ENTRY));
            env(pay(evan, loanKeylet.key, broker.asset(80), txFlags), Ter(tecNO_PERMISSION));

            // TODO: Write a general "isFlag" function? See STObject::isFlag.
            // Maybe add a static overloaded member?
            if (!(state.flags & lsfLoanOverpayment))
            {
                // If the loan does not allow overpayments, send a payment that
                // tries to make an overpayment. Do not include `txFlags`, so we
                // don't end up duplicating the next test transaction.
                //
                // fixCleanup3_1_3 gates tfLoanOverpayment as a valid flag:
                // with fix on → preflight passes, apply returns tecNO_PERMISSION;
                // with fix off → preflight rejects the flag, returns temINVALID_FLAG.
                bool const hasFix313 = env.current()->rules().enabled(fixCleanup3_1_3);
                STAmount const overpayAmount{broker.asset, state.periodicPayment * Number{15, -1}};
                XRPAmount const overpayFee{
                    baseFee * (Number{15, -1} / kLoanPaymentsPerFeeIncrement + 1)};
                env(pay(borrower, loanKeylet.key, overpayAmount, tfLoanOverpayment),
                    Fee(overpayFee),
                    Ter(hasFix313 ? TER{tecNO_PERMISSION} : TER{temINVALID_FLAG}));

                if (hasFix313)
                {
                    env.disableFeature(fixCleanup3_1_3);
                    env(pay(borrower, loanKeylet.key, overpayAmount, tfLoanOverpayment),
                        Fee(overpayFee),
                        Ter(temINVALID_FLAG));
                    env.enableFeature(fixCleanup3_1_3);
                }
            }
            // Try to send a payment marked as multiple mutually exclusive
            // payment types. Do not include `txFlags`, so we don't duplicate
            // the prior test transaction.
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanFullPayment),
                Ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanOverpayment),
                Ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanOverpayment | tfLoanFullPayment),
                Ter(temINVALID_FLAG));
            env(pay(borrower,
                    loanKeylet.key,
                    broker.asset(state.periodicPayment * 2),
                    tfLoanLatePayment | tfLoanOverpayment | tfLoanFullPayment),
                Ter(temINVALID_FLAG));

            {
                auto const otherAsset =
                    broker.asset.raw() == assets[0].raw() ? assets[1] : assets[0];
                env(pay(borrower, loanKeylet.key, otherAsset(100), txFlags), Ter(tecWRONG_ASSET));
            }

            // Amount doesn't cover a single payment
            env(pay(borrower, loanKeylet.key, STAmount{broker.asset, 1}, txFlags),
                Ter(tecINSUFFICIENT_PAYMENT));

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
                     kLoanPaymentsPerFeeIncrement +
                 1)};
            env(pay(borrower,
                    loanKeylet.key,
                    STAmount{broker.asset, borrowerBalanceBeforePayment.number() * 2},
                    txFlags),
                Fee(badFee),
                Ter(tecINSUFFICIENT_FUNDS));

            XRPAmount const goodFee{baseFee * (numPayments / kLoanPaymentsPerFeeIncrement + 1)};
            env(pay(borrower, loanKeylet.key, transactionAmount, txFlags), Fee(goodFee));

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
                state.nextPaymentDate + (state.paymentInterval * (numPayments - 1));
            state.nextPaymentDate = 0;
            verifyLoanStatus(state);

            verifyLoanStatus.checkPayment(
                state.loanScale, borrower, borrowerBalanceBeforePayment, payoffAmount, adjustment);

            // Can't impair or default a paid off loan
            env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
            env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));
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
                auto const periodicRate = interval * Number(12, -2) / kSecondsInYear;
                BEAST_EXPECT(
                    periodicRate == Number(2283105022831050228ULL, -24, Number::Normalized{}));
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
                        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
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
                auto const periodicRate = interval * Number(12, -2) / kSecondsInYear;
                BEAST_EXPECT(
                    periodicRate == Number(2283105022831050228, -24, Number::Normalized{}));
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
                        broker.asset(
                            Number(8333457002039338267, -17), Number::RoundingMode::Upward),
                        state.loanScale,
                        Number::RoundingMode::Upward));
                // 83334570.01162141
                // Include the service fee
                STAmount const totalDue = roundToScale(
                    roundedPeriodicPayment + serviceFee,
                    state.loanScale,
                    Number::RoundingMode::Upward);
                // Only check the first payment since the rounding
                // may drift as payments are made
                BEAST_EXPECT(
                    totalDue ==
                    roundToScale(
                        broker.asset(
                            Number(8533457002039338267, -17), Number::RoundingMode::Upward),
                        state.loanScale,
                        Number::RoundingMode::Upward));

                {
                    auto const raw = computeTheoreticalLoanState(
                        env.current()->rules(),
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
                        broker.asset(Number(9533457002039400, -14), Number::RoundingMode::Upward),
                        state.loanScale,
                        Number::RoundingMode::Upward));

                auto const initialState = state;
                xrpl::detail::PaymentComponents totalPaid{
                    .trackedValueDelta = 0,
                    .trackedPrincipalDelta = 0,
                    .trackedManagementFeeDelta = 0};
                Number totalInterestPaid = 0;
                std::size_t totalPaymentsMade = 0;

                xrpl::LoanState currentTrueState = computeTheoreticalLoanState(
                    env.current()->rules(),
                    state.periodicPayment,
                    periodicRate,
                    state.paymentRemaining,
                    broker.params.managementFeeRate);

                while (state.paymentRemaining > 0)
                {
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

                    BEAST_EXPECTS(
                        paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
                            paymentComponents.trackedValueDelta <= roundedPeriodicPayment,
                        "Delta: " + to_string(paymentComponents.trackedValueDelta) +
                            ", periodic payment: " + to_string(roundedPeriodicPayment));

                    xrpl::LoanState const nextTrueState = computeTheoreticalLoanState(
                        env.current()->rules(),
                        state.periodicPayment,
                        periodicRate,
                        state.paymentRemaining - 1,
                        broker.params.managementFeeRate);
                    xrpl::detail::LoanStateDeltas const deltas = currentTrueState - nextTrueState;

                    testcase << currencyLabel << " Payment components: " << state.paymentRemaining
                             << ", " << deltas.interest << ", " << deltas.principal << ", "
                             << deltas.managementFee << ", " << paymentComponents.trackedValueDelta
                             << ", " << paymentComponents.trackedPrincipalDelta << ", "
                             << paymentComponents.trackedInterestPart() << ", "
                             << paymentComponents.trackedManagementFeeDelta << ", "
                             << [&]() -> char const* {
                        if (paymentComponents.specialCase ==
                            ::xrpl::detail::PaymentSpecialCase::Final)
                            return "final";
                        if (paymentComponents.specialCase ==
                            ::xrpl::detail::PaymentSpecialCase::Extra)
                            return "extra";
                        return "none";
                    }();

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
                        paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
                        diff == beast::kZero ||
                        (diff > beast::kZero &&
                         ((broker.asset.integral() && (static_cast<Number>(diff) < 3)) ||
                          (state.loanScale - diff.exponent() > 13))));

                    BEAST_EXPECT(
                        paymentComponents.trackedValueDelta ==
                        paymentComponents.trackedPrincipalDelta +
                            paymentComponents.trackedInterestPart() +
                            paymentComponents.trackedManagementFeeDelta);
                    BEAST_EXPECT(
                        paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
                        paymentComponents.trackedValueDelta <= roundedPeriodicPayment);

                    BEAST_EXPECT(
                        state.paymentRemaining < 12 ||
                        roundToAsset(
                            broker.asset,
                            deltas.principal,
                            state.loanScale,
                            Number::RoundingMode::Upward) ==
                            roundToScale(
                                broker.asset(
                                    Number(8333228691531218890, -17), Number::RoundingMode::Upward),
                                state.loanScale,
                                Number::RoundingMode::Upward));
                    BEAST_EXPECT(
                        paymentComponents.trackedPrincipalDelta >= beast::kZero &&
                        paymentComponents.trackedPrincipalDelta <= state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase != xrpl::detail::PaymentSpecialCase::Final ||
                        paymentComponents.trackedPrincipalDelta == state.principalOutstanding);
                    BEAST_EXPECT(
                        paymentComponents.specialCase == xrpl::detail::PaymentSpecialCase::Final ||
                        (state.periodicPayment.exponent() -
                         (deltas.principal + deltas.interest + deltas.managementFee -
                          state.periodicPayment)
                             .exponent()) > 14);

                    auto const borrowerBalanceBeforePayment = env.balance(borrower, broker.asset);

                    if (canImpairLoan(env, broker, state))
                    {
                        // Making a payment will unimpair the loan
                        env(manage(lender, loanKeylet.key, tfLoanImpair));
                    }

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
                env(manage(lender, loanKeylet.key, tfLoanImpair), Ter(tecNO_PERMISSION));
                env(manage(lender, loanKeylet.key, tfLoanDefault), Ter(tecNO_PERMISSION));
            });

#if LOAN_TODO
        // TODO

        /*
        LoanPay fails with tecINVARIANT_FAILED  error when loan_broker(also
        borrower) tries to do the payment. Here's the scenario: Create a XRP
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
            auto const duration =
                std::chrono::duration_cast<duration_type>(clock_type::now() - start);

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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) {
                // Estimate optimal values for kLoanPaymentsPerFeeIncrement and
                // kLoanMaximumPaymentsPerTransaction.
                using namespace loan;

                auto const state = getCurrentState(env, broker, verifyLoanStatus.keylet);
                auto const serviceFee = broker.asset(2).value();

                STAmount const totalDue{
                    broker.asset,
                    roundPeriodicPayment(
                        broker.asset, state.periodicPayment + serviceFee, state.loanScale)};

                // Make a single payment
                time("single payment", [&]() { env(pay(borrower, loanKeylet.key, totalDue)); });
                env.close();

                // Make all but the final payment
                auto const numPayments = (state.paymentRemaining - 2);
                STAmount const bigPayment{broker.asset, totalDue * numPayments};
                XRPAmount const bigFee{baseFee * (numPayments / kLoanPaymentsPerFeeIncrement + 1)};
                time("ten payments", [&]() {
                    env(pay(borrower, loanKeylet.key, bigPayment), Fee(bigFee));
                });
                env.close();

                time("final payment", [&]() {
                    // Make the final payment
                    env(pay(borrower, loanKeylet.key, totalDue + STAmount{broker.asset, 1}));
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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

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
            [&](Keylet const& loanKeylet, VerifyLoanStatus const& verifyLoanStatus) { throw 0; });

#endif
    }

    void
    testLoanSet(FeatureBitset features)
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

        auto const testCase = [&, this](
                                  std::function<void(Env&, BrokerInfo const&, MPTTester&)> mptTest,
                                  std::function<void(Env&, BrokerInfo const&)> iouTest,
                                  CaseArgs args = {}) {
            Env env(*this, features);
            env.fund(XRP(args.initialXRP), issuer, lender, borrower);
            env.close();
            if (args.requireAuth)
            {
                env(fset(issuer, asfRequireAuth));
                env.close();
            }

            // We need two different asset types, MPT and IOU. Prepare MPT
            // first
            MPTTester mptt{env, issuer, kMptInitNoFund};

            auto const kNone = LedgerSpecificFlags(0);
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock |
                     (args.requireAuth ? tfMPTRequireAuth : kNone)});
            env.close();
            PrettyAsset const mptAsset = mptt.issuanceID();
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
            PrettyAsset const iouAsset = issuer[iouCurrency_];
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
            brokers.reserve(assets.size());
            for (auto const& asset : assets)
            {
                brokers.emplace_back(createVaultAndBroker(env, asset, lender));
            }

            if (mptTest)
                mptTest(env, brokers[0], mptt);
            if (iouTest)
                iouTest(env, brokers[1]);
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("MPT issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("IOU issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("MPT unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("IOU unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            CaseArgs{.requireAuth = true});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, borrower has "
                    "no reserve");
                mptt.authorize({.account = borrower, .flags = tfMPTUnauthorize});
                env.close();

                auto const mptoken = keylet::mptoken(mptt.issuanceID(), borrower);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

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
                auto const trustline = keylet::trustLine(borrower, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

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

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

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

                auto const trustline = keylet::trustLine(lender, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 != nullptr);

                env(pay(lender, issuer, broker.asset(abs(sleLine1->at(sfBalance).value()))));
                env.close();
                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine3 = env.le(trustline);
                BEAST_EXPECT(sleLine3 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, unauthorized lender");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Cannot create loan, lender not authorized to receive fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
                env.close();

                // Cannot create loan, even without an origination fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
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
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        jtx::Account const alice{"alice"};
        jtx::Account const bella{"bella"};
        auto const msigSetup = [&](Env& env, Account const& account) {
            json::Value const tx1 = signers(account, 2, {{alice, 1}, {bella, 1}});
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
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
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
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] = BrokerParameters::defaults().vaultDeposit;
                env(tx);
                env.close();

                testcase("Vault at maximum value");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(10'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] =
                    BrokerParameters::defaults().vaultDeposit + broker.asset(1).number();
                env(tx);
                env.close();

                testcase("Vault maximum value exceeded");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(100'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    kPaymentTotal(2),
                    kPaymentInterval(3600 * 24),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);
    }

    void
    testLifecycle(FeatureBitset features)
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
        Env env(*this, features);

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
        PrettyAsset const iouAsset = issuer[iouCurrency_];
        env(trust(lender, iouAsset(10'000'000)));
        env(trust(borrower, iouAsset(10'000'000)));
        env(trust(evan, iouAsset(10'000'000)));
        env(pay(issuer, evan, iouAsset(1'000'000)));
        env(pay(issuer, lender, iouAsset(10'000'000)));
        // Fund the borrower with enough to cover interest and fees
        env(pay(issuer, borrower, iouAsset(10'000)));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
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

        std::array const assets{iouAsset, xrpAsset, mptAsset};

        // Create vaults and loan brokers
        std::vector<BrokerInfo> brokers;
        brokers.reserve(assets.size());
        for (auto const& asset : assets)
        {
            brokers.emplace_back(createVaultAndBroker(
                env, asset, lender, BrokerParameters{.data = "spam spam spam spam"}));
        }

        // Create and update Loans
        for (auto const& broker : brokers)
        {
            for (int amountExponent = 3; amountExponent >= 3; --amountExponent)
            {
                Number const loanAmount{1, amountExponent};
                for (int interestExponent = 0; interestExponent >= 0; --interestExponent)
                {
                    testCaseWrapper(env, mptt, assets, broker, loanAmount, interestExponent);
                }
            }

            if (auto brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == 0);

                auto const coverAvailable = brokerSle->at(sfCoverAvailable);
                env(loanBroker::coverWithdraw(
                    lender, broker.brokerID, STAmount(broker.asset, coverAvailable)));
                env.close();

                brokerSle = env.le(keylet::loanBroker(broker.brokerID));
                BEAST_EXPECT(brokerSle && brokerSle->at(sfCoverAvailable) == 0);
            }
            // Verify we can delete the loan broker
            env(loanBroker::del(lender, broker.brokerID));
            env.close();
        }
    }

    void
    testSelfLoan(FeatureBitset features)
    {
        testcase << "Self Loan";

        using namespace jtx;
        using namespace std::chrono_literals;
        // Create 3 loan brokers: one for XRP, one for an IOU, and one for
        // an MPT. That'll require three corresponding SAVs.
        Env env(*this, features);

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

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        // The LoanSet json can be created without a counterparty signature,
        // but it will not pass preflight
        auto createJson = env.json(
            set(lender, broker.brokerID, broker.asset(principalRequest).value()), Fee(loanSetFee));
        env(createJson, Ter(temBAD_SIGNER));

        // Adding an empty counterparty signature object also fails, but
        // at the RPC level.
        createJson = env.json(createJson, Json(sfCounterpartySignature, json::ValueType::Object));
        env(createJson, Ter(telENV_RPC_FAILED));

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
        json::Value counterpartyJson{json::ValueType::Object};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        // The duplicated signature works
        createJson = env.json(createJson, Json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();

        auto const startDate = env.current()->header().parentCloseTime;

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
            for (std::string const type : {"MPToken", "Vault", "LoanBroker", "Loan"})
            {
                BEAST_EXPECT(types[type] == 1);
            }
        }
        auto const loanID = [&]() {
            json::Value params(json::ValueType::Object);
            params[jss::account] = lender.human();
            params[jss::type] = "Loan";
            auto const res = env.rpc("json", "account_objects", to_string(params));
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
            BEAST_EXPECT(loan[sfNextPaymentDueDate] == loan[sfStartDate].asUInt() + 60);
            BEAST_EXPECT(!loan.isMember(sfOverpaymentFee));
            BEAST_EXPECT(!loan.isMember(sfOverpaymentInterestRate));
            BEAST_EXPECT(loan[sfPaymentInterval] == 60);
            BEAST_EXPECT(loan[sfPeriodicPayment] == "1000000000");
            BEAST_EXPECT(loan[sfPaymentRemaining] == 1);
            BEAST_EXPECT(!loan.isMember(sfPreviousPaymentDueDate));
            BEAST_EXPECT(loan[sfPrincipalOutstanding] == "1000000000");
            BEAST_EXPECT(loan[sfTotalValueOutstanding] == "1000000000");
            BEAST_EXPECT(!loan.isMember(sfLoanScale));
            BEAST_EXPECT(loan[sfStartDate].asUInt() == startDate.time_since_epoch().count());

            return loan["index"].asString();
        }();
        auto const loanKeylet{keylet::loan(uint256{std::string_view(loanID)})};

        env.close(startDate);

        // Make a payment
        env(pay(lender, loanKeylet.key, broker.asset(1000)));
    }

    void
    testBatchBypassCounterparty(FeatureBitset features)
    {
        // From FIND-001
        testcase << "Batch Bypass Counterparty";

        bool const lendingBatchEnabled = !std::ranges::any_of(
            Batch::kDisabledTxTypes,
            [](auto const& disabled) { return disabled == ttLOAN_BROKER_SET; });

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        BrokerParameters const brokerParams;
        env.fund(XRP(brokerParams.vaultDeposit * 100), lender, borrower);
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto forgedLoanSet = set(borrower, broker.brokerID, principalRequest, 0);

        json::Value randomData{json::ValueType::Object};
        randomData[jss::SigningPubKey] = json::StaticString{"2600"};
        json::Value sigObject{json::ValueType::Object};
        sigObject[jss::SigningPubKey] = strHex(lender.pk().slice());
        Serializer ss;
        ss.add32(HashPrefix::TxSign);
        parse(randomData).addWithoutSigningFields(ss);
        auto const sig = xrpl::sign(borrower.pk(), borrower.sk(), ss.slice());
        sigObject[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});

        forgedLoanSet[json::StaticString{"CounterpartySignature"}] = sigObject;

        // ? Fails because the lender hasn't signed the tx
        env(env.json(forgedLoanSet, Fee(loanSetFee)), Ter(telENV_RPC_FAILED));

        auto const seq = env.seq(borrower);
        auto const batchFee = batch::calcBatchFee(env, 1, 2);
        // ! Should fail because the lender hasn't signed the tx
        env(batch::outer(borrower, seq, batchFee, tfAllOrNothing),
            batch::Inner(forgedLoanSet, seq + 1),
            batch::Inner(pay(borrower, lender, XRP(1)), seq + 2),
            Ter(lendingBatchEnabled ? temBAD_SIGNATURE : temINVALID_INNER_BATCH));
        env.close();

        // ? Check that the loan was NOT created
        {
            json::Value params(json::ValueType::Object);
            params[jss::account] = borrower.human();
            params[jss::type] = "Loan";
            auto const res = env.rpc("json", "account_objects", to_string(params));
            auto const objects = res[jss::result][jss::account_objects];
            BEAST_EXPECT(objects.size() == 0);
        }
    }

    void
    testWrongMaxDebtBehavior(FeatureBitset features)
    {
        // From FIND-003
        testcase << "Wrong Max Debt Behavior";

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};

        BrokerParameters const brokerParams{.debtMax = 0};
        env.fund(XRP(brokerParams.vaultDeposit * 100), issuer, noripple(lender));
        env.close();

        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

        BrokerInfo const broker{createVaultAndBroker(env, xrpAsset, lender, brokerParams)};

        if (auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            BEAST_EXPECT(brokerSle->at(sfDebtMaximum) == 0);
        }

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 3};

        auto createJson = env.json(set(lender, broker.brokerID, principalRequest), Fee(loanSetFee));

        json::Value counterpartyJson{json::ValueType::Object};
        counterpartyJson[sfTxnSignature] = createJson[sfTxnSignature];
        counterpartyJson[sfSigningPubKey] = createJson[sfSigningPubKey];
        if (!BEAST_EXPECT(!createJson.isMember(jss::Signers)))
            counterpartyJson[sfSigners] = createJson[sfSigners];

        createJson = env.json(createJson, Json(sfCounterpartySignature, counterpartyJson));
        env(createJson);

        env.close();
    }

    void
    testRPC(FeatureBitset features)
    {
        // This will expand as more test cases are added. Some functionality
        // is tested in other test functions.
        testcase("RPC");

        using namespace jtx;

        Env env(*this, features);

        auto lowerFee = [&]() {
            // Run the local fee back down.
            while (env.app().getFeeTrack().lowerLocalFee())
                ;
        };

        auto const baseFee = env.current()->fees().base;

        Account const alice{"alice"};
        std::string const borrowerPass = "borrower";
        Account const borrower{borrowerPass, KeyType::Ed25519};
        auto const lenderPass = "lender";
        Account const lender{lenderPass, KeyType::Ed25519};

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
            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const signParams = [&]() {
                json::Value signParams{json::ValueType::Object};
                signParams[jss::passphrase] = borrowerPass;
                signParams[jss::key_type] = "ed25519";
                signParams[jss::tx_json] = txJson;
                return signParams;
            }();
            auto const jSign = env.rpc("json", "sign", to_string(signParams));
            BEAST_EXPECT(jSign.isMember(jss::result) && jSign[jss::result].isMember(jss::tx_json));
            auto txSignResult = jSign[jss::result][jss::tx_json];
            auto txSignBlob = jSign[jss::result][jss::tx_blob].asString();
            txSignResult.removeMember(jss::hash);

            auto const jtx = env.jt(txJson, Sig(borrower));
            BEAST_EXPECT(txSignResult == jtx.jv);

            lowerFee();
            auto const jSubmit = env.rpc("submit", txSignBlob);
            BEAST_EXPECT(
                jSubmit.isMember(jss::result) &&
                jSubmit[jss::result].isMember(jss::engine_result) &&
                jSubmit[jss::result][jss::engine_result].asString() == "tesSUCCESS");

            lowerFee();
            env(jtx.jv, Sig(kNone), Seq(kNone), Fee(kNone), Ter(tefPAST_SEQ));
        }

        {
            testcase("RPC LoanSet - illegal signature_target");

            json::Value txJson{json::ValueType::Object};
            txJson[sfTransactionType] = "AccountSet";
            txJson[sfAccount] = borrower.human();

            auto const borrowerSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "Destination";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::error) &&
                jSignBorrower[jss::result][jss::error] == "invalidParams" &&
                jSignBorrower[jss::result].isMember(jss::error_message) &&
                jSignBorrower[jss::result][jss::error_message] == "Destination");
        }
        {
            testcase("RPC LoanSet - sign and submit borrower initiated");
            // 1. Borrower creates the transaction
            json::Value txJson{json::ValueType::Object};
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
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECTS(
                jSignBorrower.isMember(jss::result) &&
                    jSignBorrower[jss::result].isMember(jss::tx_json),
                to_string(jSignBorrower));
            auto const txBorrowerSignResult = jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob = jSignBorrower[jss::result][jss::tx_blob].asString();

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
                    jSubmitBlobResult[jss::engine_result].asString() == "temBAD_SIGNER");
            }

            // 3. Borrower sends the signed transaction to the lender
            // 4. Lender signs the transaction
            auto const lenderSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txBorrowerSignResult;
                return params;
            }();
            auto const jSignLender = env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult = jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob = jSignLender[jss::result][jss::tx_blob].asString();

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
                    jSubmitBlobResult[jss::engine_result].asString() == "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) && !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Lender submits the transaction json originally
            // received from the Borrower. It gets signed, but is now a
            // duplicate, so fails. Borrower could done this instead of
            // steps 4 and 5.
            lowerFee();
            auto const jSubmitJson = env.rpc("json", "submit", to_string(lenderSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() == "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) && !jSubmitJsonResult.isMember(jss::error));

            BEAST_EXPECT(jSubmitBlobTx == jSubmitJsonTx);
        }

        {
            testcase("RPC LoanSet - sign and submit lender initiated");
            // 1. Lender creates the transaction
            json::Value txJson{json::ValueType::Object};
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
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = lenderPass;
                params[jss::key_type] = "ed25519";
                params[jss::tx_json] = txJson;
                return params;
            }();
            auto const jSignLender = env.rpc("json", "sign", to_string(lenderSignParams));
            BEAST_EXPECT(
                jSignLender.isMember(jss::result) &&
                jSignLender[jss::result].isMember(jss::tx_json));
            auto const txLenderSignResult = jSignLender[jss::result][jss::tx_json];
            auto const txLenderSignBlob = jSignLender[jss::result][jss::tx_blob].asString();

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
                    jSubmitBlobResult[jss::engine_result].asString() == "temBAD_SIGNER");
            }

            // 3. Lender sends the signed transaction to the Borrower
            // 4. Borrower signs the transaction
            auto const borrowerSignParams = [&]() {
                json::Value params{json::ValueType::Object};
                params[jss::passphrase] = borrowerPass;
                params[jss::key_type] = "ed25519";
                params[jss::signature_target] = "CounterpartySignature";
                params[jss::tx_json] = txLenderSignResult;
                return params;
            }();
            auto const jSignBorrower = env.rpc("json", "sign", to_string(borrowerSignParams));
            BEAST_EXPECT(
                jSignBorrower.isMember(jss::result) &&
                jSignBorrower[jss::result].isMember(jss::tx_json));
            auto const txBorrowerSignResult = jSignBorrower[jss::result][jss::tx_json];
            auto const txBorrowerSignBlob = jSignBorrower[jss::result][jss::tx_blob].asString();

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
                    jSubmitBlobResult[jss::engine_result].asString() == "tecNO_ENTRY",
                to_string(jSubmitBlobResult));

            BEAST_EXPECT(
                !jSubmitBlob.isMember(jss::error) && !jSubmitBlobResult.isMember(jss::error));

            // 4-alt. Borrower submits the transaction json originally
            // received from the Lender. It gets signed, but is now a
            // duplicate, so fails. Lender could done this instead of steps
            // 4 and 5.
            lowerFee();
            auto const jSubmitJson = env.rpc("json", "submit", to_string(borrowerSignParams));
            BEAST_EXPECT(jSubmitJson.isMember(jss::result));
            auto const jSubmitJsonResult = jSubmitJson[jss::result];
            BEAST_EXPECT(jSubmitJsonResult.isMember(jss::tx_json));
            auto const jSubmitJsonTx = jSubmitJsonResult[jss::tx_json];
            // Since the previous tx claimed a fee, this duplicate is not
            // going anywhere
            BEAST_EXPECTS(
                jSubmitJsonResult.isMember(jss::engine_result) &&
                    jSubmitJsonResult[jss::engine_result].asString() == "tefPAST_SEQ",
                to_string(jSubmitJsonResult));

            BEAST_EXPECT(
                !jSubmitJson.isMember(jss::error) && !jSubmitJsonResult.isMember(jss::error));

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
        auto const iou = issuer["IOU"];

        for (bool const deepFreeze : {true, false})
        {
            Env env(*this);

            auto getCoverBalance = [&](BrokerInfo const& brokerInfo, auto const& accountField) {
                if (auto const le = env.le(keylet::loanBroker(brokerInfo.brokerID));
                    BEAST_EXPECT(le))
                {
                    auto const account = le->at(accountField);
                    if (auto const sleLine = env.le(keylet::trustLine(account, iou));
                        BEAST_EXPECT(sleLine))
                    {
                        STAmount balance = sleLine->at(sfBalance);
                        if (account > issuer.id())
                            balance.negate();
                        return balance;
                    }
                }
                return STAmount{iou};
            };

            env.fund(XRP(20'000), issuer, broker, borrower);
            env.close();

            env(trust(broker, iou(20'000'000)));
            env(pay(issuer, broker, iou(10'000'000)));
            env.close();

            auto const brokerInfo = createVaultAndBroker(env, iou, broker);

            BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'000));

            auto const keylet = keylet::loan(brokerInfo.brokerID, 1);

            env(set(borrower, brokerInfo.brokerID, 10'000),
                Sig(sfCounterpartySignature, broker),
                kLoanServiceFee(iou(100).value()),
                kPaymentInterval(100),
                Fee(XRP(100)));
            env.close();

            env(trust(borrower, iou(20'000'000)));
            // The borrower increases their limit and acquires some IOU so
            // they can pay interest
            env(pay(issuer, borrower, iou(500)));
            env.close();

            if (auto const le = env.le(keylet::loan(keylet.key)); BEAST_EXPECT(le))
            {
                if (deepFreeze)
                {
                    env(trust(issuer, broker["IOU"](0), tfSetFreeze | tfSetDeepFreeze));
                    env.close();
                }

                env(pay(borrower, keylet.key, iou(10'100)), Fee(XRP(100)));
                env.close();

                if (deepFreeze)
                {
                    // The fee goes to the broker pseudo-account
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'100));
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfOwner) == iou(8'999'000));
                }
                else
                {
                    // The fee goes to the broker account
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfOwner) == iou(8'999'100));
                    BEAST_EXPECT(getCoverBalance(brokerInfo, sfAccount) == iou(1'000));
                }
            }
        };
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

        static constexpr std::int64_t kIssuerBalance = 10'000'000;
        MPTTester const asset(
            {.env = env, .issuer = issuer, .holders = {lender}, .pay = kIssuerBalance});

        BrokerParameters const brokerParams{
            .debtMax = 200,
        };
        auto const broker = createVaultAndBroker(env, asset, lender, brokerParams);
        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        // Create Loan
        env(set(borrower, broker.brokerID, 200), Sig(sfCounterpartySignature, lender), loanSetFee);
        env.close();
        // Issuer should not create MPToken
        BEAST_EXPECT(!env.le(keylet::mptoken(asset.issuanceID(), issuer)));
        // Issuer "borrowed" 200, OutstandingAmount decreased by 200
        BEAST_EXPECT(env.balance(issuer, asset) == asset(-kIssuerBalance + 200));
        // Pay Loan
        auto const loanKeylet = keylet::loan(broker.brokerID, 1);
        env(pay(borrower, loanKeylet.key, asset(200)));
        env.close();
        // Issuer "re-payed" 200, OutstandingAmount increased by 200
        BEAST_EXPECT(env.balance(issuer, asset) == asset(-kIssuerBalance));
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
            env(del(alice, beast::kZero), Ter(temINVALID));
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
            env(manage(alice, beast::kZero, tfLoanDefault), Ter(temINVALID));
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
        auto const iou = issuer["IOU"];

        // preclaim
        Env env(*this);
        env.fund(XRP(1'000), lender, issuer, borrower);
        env(trust(lender, iou(10'000'000)));
        env(pay(issuer, lender, iou(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
            Sig(sfCounterpartySignature, lender),
            loanSetFee);

        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, loanSequence);

        env(fset(issuer, asfGlobalFreeze));
        env.close();

        // preclaim: tecFROZEN
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        env(fclear(issuer, asfGlobalFreeze));
        env.close();

        auto const pseudoBroker = [&]() -> std::optional<Account> {
            if (auto brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
                BEAST_EXPECT(brokerSle))
            {
                return Account{"pseudo", brokerSle->at(sfAccount)};
            }

            return std::nullopt;
        }();
        if (!pseudoBroker)
            return;

        // Lender and pseudoaccount must both be frozen
        env(trust(issuer, lender["IOU"](1'000), lender, tfSetFreeze | tfSetDeepFreeze));
        env(trust(
            issuer, (*pseudoBroker)["IOU"](1'000), *pseudoBroker, tfSetFreeze | tfSetDeepFreeze));
        env.close();

        // preclaim: tecFROZEN due to deep frozen
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecFROZEN));
        env.close();

        // Only one needs to be unfrozen
        env(trust(issuer, lender["IOU"](1'000), tfClearFreeze | tfClearDeepFreeze));
        env.close();

        // The payment is late by this point
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecEXPIRED));
        env.close();
        env(pay(borrower, loanKeylet.key, debtMaximumRequest, tfLoanLatePayment));
        env.close();

        // preclaim: tecKILLED
        // note that tecKILLED in loanMakePayment()
        // doesn't happen because of the preclaim check.
        env(pay(borrower, loanKeylet.key, debtMaximumRequest), Ter(tecKILLED));
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
        Account const sponsor{"sponsor"};
        auto const iou = issuer["IOU"];

        auto testWrapper = [&](auto&& test) {
            Env env(*this);
            env.fund(XRP(1'000), lender, issuer, borrower, sponsor);
            env(trust(lender, iou(10'000'000)));
            env(pay(issuer, lender, iou(5'000'000)));
            BrokerInfo const brokerInfo{createVaultAndBroker(env, issuer["IOU"], lender)};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            Number const debtMaximumRequest = brokerInfo.asset(1'000).value();
            test(env, brokerInfo, loanSetFee, debtMaximumRequest);
        };

        // preflight:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            for (auto const sponsorFlags : {spfSponsorReserve, spfSponsorReserve | spfSponsorFee})
            {
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    sponsor::As(sponsor, sponsorFlags),
                    Sig(sfCounterpartySignature, lender),
                    loanSetFee,
                    Ter(temINVALID_FLAG));
            }

            // first temBAD_SIGNER: TODO
            // invalid grace period
            {
                // zero grace period
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(0),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period less than default minimum
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kGracePeriod(LoanSet::kDefaultGracePeriod - 1),
                    loanSetFee,
                    Ter(temINVALID));

                // grace period greater than payment interval
                env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                    Sig(sfCounterpartySignature, lender),
                    kPaymentInterval(120),
                    kGracePeriod(121),
                    loanSetFee,
                    Ter(temINVALID));
            }
            // empty/zero broker ID
            {
                auto jv = set(borrower, uint256{}, debtMaximumRequest);

                auto testZeroBrokerID = [&](std::string const& id, std::uint32_t flags = 0) {
                    // empty broker ID
                    jv[sfLoanBrokerID] = id;
                    env(jv,
                        Sig(sfCounterpartySignature, lender),
                        loanSetFee,
                        Txflags(flags),
                        Ter(temINVALID));
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

            JTx const tx = env.jt(
                set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee);
            STTx local = *(tx.stx);
            auto counterpartySig = local.getFieldObject(sfCounterpartySignature);
            auto badPubKey = counterpartySig.getFieldVL(sfSigningPubKey);
            badPubKey[20] ^= 0xAA;
            counterpartySig.setFieldVL(sfSigningPubKey, badPubKey);
            local.setFieldObject(sfCounterpartySignature, counterpartySig);
            json::Value jvResult;
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
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            // canAddHoldingFailure (IOU only, if MPT doesn't have
            // MPTCanTransfer set, then can't create Vault/LoanBroker,
            // and LoanSet will fail with different error
            env(fclear(issuer, asfDefaultRipple));
            env.close();
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(terNO_RIPPLE));
        });

        // doApply:
        testWrapper([&](Env& env,
                        BrokerInfo const& brokerInfo,
                        jtx::Fee const& loanSetFee,
                        Number const& debtMaximumRequest) {
            auto const amt =
                env.balance(borrower) - accountReserve(*env.current(), borrower.id(), env.journal);
            env(pay(borrower, issuer, amt));

            // tecINSUFFICIENT_RESERVE
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecINSUFFICIENT_RESERVE));

            // addEmptyHolding failure
            env(pay(issuer, borrower, amt));
            env(fset(issuer, asfGlobalFreeze));
            env.close();

            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                Ter(tecFROZEN));
        });
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
            .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
            .authHolder = true,
        });

        env(pay(issuer, lender, asset(5'000'000)));
        BrokerInfo brokerInfo{createVaultAndBroker(env, asset, lender)};

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        STAmount const debtMaximumRequest = brokerInfo.asset(1'000).value();

        auto forUnauthAuth = [&](auto&& doTx) {
            for (auto const flag : {tfMPTUnauthorize, 0u})
            {
                asset.authorize({.account = issuer, .holder = borrower, .flags = flag});
                env.close();
                doTx(flag == 0);
                env.close();
            }
        };

        // Can't create a loan if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(set(borrower, brokerInfo.brokerID, debtMaximumRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                err);
        });

        static constexpr std::uint32_t kLoanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, kLoanSequence);

        // Can't loan pay if the borrower is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(pay(borrower, loanKeylet.key, debtMaximumRequest), err);
        });
    }

    void
    testLendingCanTradeDisabledNoImpact()
    {
        testcase("Lending: CanTrade disabled has no impact");
        using namespace jtx;
        using namespace loan;
        using namespace loanBroker;

        Env env(*this, all_);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();

        MPTTester mpt(
            {.env = env,
             .issuer = issuer,
             .holders = {lender, borrower},
             .flags = tfMPTCanTransfer | tfMPTCanLock,
             .mutableFlags = tmfMPTCanEnableCanTrade});
        PrettyAsset const asset = mpt.issuanceID();
        env(pay(issuer, lender, asset(10'000'000)));
        env(pay(issuer, borrower, asset(100'000)));
        env.close();

        auto const broker = createVaultAndBroker(env, asset, lender);

        // CanTrade is not set
        env(offer(lender, XRP(1), asset(10)), Ter{tecNO_PERMISSION});
        env.close();

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        // New cover deposits still work.
        env(coverDeposit(lender, broker.brokerID, asset(100)));
        env.close();

        // New loan issuance still works.
        env(loan::set(borrower, broker.brokerID, 1'000),
            Sig(sfCounterpartySignature, lender),
            loanSetFee);
        env.close();
        auto const loanKeylet = keylet::loan(broker.brokerID, 1);
        BEAST_EXPECT(env.le(loanKeylet));

        // Repayment still works.
        env(pay(borrower, loanKeylet.key, asset(1'000)));
        env.close();

        // Cover withdrawal still works.
        env(coverWithdraw(lender, broker.brokerID, asset(100)));
        env.close();

        // Enable CanTrade and verify the DEX path is restored.
        mpt.set({.mutableFlags = tmfMPTSetCanTrade});
        env.close();

        env(offer(lender, XRP(1), asset(10)));
        env.close();
    }

    void
    testBorrowerIsBroker()
    {
        testcase("Test Borrower is Broker");
        using namespace jtx;
        using namespace loan;
        Account const broker{"broker"};
        Account const issuer{"issuer"};
        Account const borrower{"borrower"};
        Account const depositor{"depositor"};

        auto testLoanAsset = [&](auto&& getMaxDebt, auto const& borrower) {
            Env env(*this);
            Vault const vault(env);

            if (borrower == broker)
            {
                env.fund(XRP(10'000), broker, issuer, depositor);
            }
            else
            {
                env.fund(XRP(10'000), broker, borrower, issuer, depositor);
            }
            env.close();

            auto const xrpFee = XRP(100);
            auto const txFee = Fee(xrpFee);

            STAmount const debtMaximumRequest = getMaxDebt(env);

            auto const& asset = debtMaximumRequest.asset();
            auto const initialVault = asset(debtMaximumRequest * 100);

            auto [tx, vaultKeylet] = vault.create({.owner = broker, .asset = asset});
            env(tx, txFee);
            env.close();

            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = initialVault}),
                txFee);
            env.close();

            auto const brokerKeylet = keylet::loanBroker(broker.id(), env.seq(broker));

            env(loanBroker::set(broker, vaultKeylet.key), txFee);
            env.close();

            auto const serviceFee = 101;

            env(set(broker, brokerKeylet.key, debtMaximumRequest),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                kLoanServiceFee(serviceFee),
                kPaymentTotal(10),
                txFee);
            env.close();

            std::uint32_t const loanSequence = 1;
            auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSequence);

            auto const brokerBalanceBefore = env.balance(broker, asset);

            if (auto const loanSle = env.le(loanKeylet); env.test.BEAST_EXPECT(loanSle))
            {
                auto const payment = loanSle->at(sfPeriodicPayment);
                auto const totalPayment = payment + serviceFee;
                env(loan::pay(borrower, loanKeylet.key, asset(totalPayment)), txFee);
                env.close();
                if (auto const vaultSle = env.le(vaultKeylet); BEAST_EXPECT(vaultSle))
                {
                    auto const expected = [&]() {
                        // The service fee is transferred to the broker if
                        // a borrower is not the broker
                        if (borrower != broker)
                            return brokerBalanceBefore.number() + serviceFee;
                        // Since a borrower is the broker, the payment is
                        // transferred to the Vault from the broker but not
                        // the service fee.
                        // If the asset is XRP then the broker pays the txFee.
                        if (asset.native())
                            return brokerBalanceBefore.number() - payment - xrpFee.number();
                        return brokerBalanceBefore.number() - payment;
                    }();
                    BEAST_EXPECT(env.balance(broker, asset).value() == asset(expected).value());
                }
            }
        };
        // Test when a borrower is the broker and is not to verify correct
        // service fee transfer in both cases.
        for (auto const& borrowerAcct : {broker, borrower})
        {
            testLoanAsset(
                [&](Env&) -> STAmount { return STAmount{XRPAmount{200'000}}; }, borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    auto const iou = issuer["USD"];
                    env(trust(broker, iou(1'000'000'000)));
                    env(trust(depositor, iou(1'000'000'000)));
                    env(pay(issuer, broker, iou(100'000'000)));
                    env(pay(issuer, depositor, iou(100'000'000)));
                    env.close();
                    return iou(200'000);
                },
                borrowerAcct);
            testLoanAsset(
                [&](Env& env) -> STAmount {
                    MPTTester const mpt(
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
    testIssuerIsBorrower(FeatureBitset features)
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
            .account = lender, .counter = issuer, .principalRequest = Number{10000}};

        auto const assetType = AssetType::IOU;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, issuer);

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

        Env env(*this, makeConfig(), all_, nullptr, beast::Severity::Warning);

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        auto const state = getCurrentState(env, broker, loanKeylet);

        env(loan::pay(
            borrower,
            loanKeylet.key,
            STAmount{broker.asset, state.periodicPayment * 3 / 2 + 1},
            tfLoanOverpayment));
        env.close();

        PaymentParameters const paymentParams{
            .showStepBalances = false,
            .validateBalances = true,
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

    void
    testOverpaymentManagementFee(FeatureBitset features)
    {
        testcase("testOverpaymentManagementFee");

        using namespace jtx;
        using namespace loan;

        Env env{*this, features};

        Account const lender{"lender"}, borrower{"borrower"};

        env.fund(XRP(10'000'000), lender, borrower);
        env.close();

        PrettyAsset const asset{xrpIssue(), 1000};

        auto const result = createVaultAndBroker(
            env,
            asset,
            lender,
            {
                .vaultDeposit = asset(100'000).value(),
                .managementFeeRate = TenthBips16(10'000),
            });

        auto const loanSetFee = Fee(env.current()->fees().base * 2);

        auto const loanKeylet = keylet::loan(
            result.brokerKeylet().key, (env.le(result.brokerKeylet()))->at(sfLoanSequence));
        env(loan::set(
                borrower, result.brokerKeylet().key, asset(10'000).value(), tfLoanOverpayment),
            Sig(sfCounterpartySignature, lender),
            loan::kPaymentInterval(86400 * 30),
            loan::kPaymentTotal(3),
            loan::kOverpaymentInterestRate(TenthBips32(percentageToTenthBips(20))),
            loanSetFee);

        // From calculator
        auto const expectedOverpaymentManagementFee = Number{33333, 0};
        auto const loanBrokerBalanceBefore = env.balance(lender);

        auto const loanPayFee = Fee(env.current()->fees().base * 2);
        env(pay(borrower, loanKeylet.key, asset(5'000).value(), tfLoanOverpayment), loanPayFee);
        env.close();

        BEAST_EXPECTS(
            env.balance(lender) - loanBrokerBalanceBefore == expectedOverpaymentManagementFee,
            "overpayment management fee missmatch; expected:" +
                to_string(expectedOverpaymentManagementFee) +
                " got: " + to_string(env.balance(lender) - loanBrokerBalanceBefore));
    }

    void
    testLoanPayBrokerOwnerMissingTrustline(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner Missing Trustline (PoC)";
        using namespace jtx;
        using namespace loan;
        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");
        auto const iou = issuer["IOU"];
        Env env(*this, features);
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();
        // Set up trustlines and fund accounts
        env(trust(broker, iou(20'000'000)));
        env(trust(borrower, iou(20'000'000)));
        env(pay(issuer, broker, iou(10'000'000)));
        env(pay(issuer, borrower, iou(1'000)));
        env.close();
        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, iou, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, 1);
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(iou(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = iou(50'000).value();
        env(loanBroker::coverDeposit(broker, brokerInfo.brokerID, STAmount{iou, additionalCover}));
        env.close();
        // Verify broker owner has a trustline
        auto const brokerTrustline = keylet::trustLine(broker, iou);
        BEAST_EXPECT(env.le(brokerTrustline) != nullptr);
        // Broker owner deletes their trustline
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, iou);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Remove the trustline by setting limit to 0
        env(trust(broker, iou(0)));
        env.close();
        // Verify trustline is deleted
        BEAST_EXPECT(env.le(brokerTrustline) == nullptr);
        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_LINE.
        env(pay(borrower, keylet.key, iou(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify trustline is still deleted
        BEAST_EXPECT(env.le(brokerTrustline) == nullptr);
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, iou);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == iou(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanPayBrokerOwnerUnauthorizedMPT(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner MPT unauthorized";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});

        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, 1);
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = mpt(50'000).value();
        env(loanBroker::coverDeposit(broker, brokerInfo.brokerID, STAmount{mpt, additionalCover}));
        env.close();
        // Verify broker owner is authorized
        auto const brokerMpt = keylet::mptoken(mptt.issuanceID(), broker);
        BEAST_EXPECT(env.le(brokerMpt) != nullptr);
        // Broker owner unauthorizes.
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Then, unauthorize the MPT.
        mptt.authorize({.account = broker, .flags = tfMPTUnauthorize});
        env.close();
        // Verify the MPT is unauthorized.
        BEAST_EXPECT(env.le(brokerMpt) == nullptr);
        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_AUTH.
        auto const borrowerBalance = env.balance(borrower, mpt);
        env(pay(borrower, keylet.key, mpt(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify the MPT is still unauthorized.
        BEAST_EXPECT(env.le(brokerMpt) == nullptr);
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, mpt);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == mpt(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanPayBrokerOwnerNoPermissionedDomainMPT(FeatureBitset features)
    {
        testcase << "LoanPay Broker Owner without permissioned domain of the MPT";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        auto credType = "credential1";

        pdomain::Credentials const credentials1 = {{.issuer = issuer, .credType = credType}};
        env(pdomain::setTx(issuer, credentials1));
        env.close();

        auto domainID = pdomain::getNewDomain(env.meta());

        env(credentials::create(broker, issuer, credType));
        env(credentials::accept(broker, issuer, credType));
        env.close();

        env(credentials::create(borrower, issuer, credType));
        env(credentials::accept(borrower, issuer, credType));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({
            .flags = tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanTransfer | tfMPTCanLock,
            .domainID = domainID,
        });

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});

        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);
        // Create a loan first (this creates debt)
        auto const keylet = keylet::loan(brokerInfo.brokerID, 1);
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)));
        env.close();
        // Ensure broker has sufficient cover so brokerPayee == brokerOwner
        // We need coverAvailable >= (debtTotal * coverRateMinimum)
        // Deposit enough cover to ensure the fee goes to broker owner
        // The default coverRateMinimum is 10%, so for a 10,000 loan we need
        // at least 1,000 cover. Default cover is 1,000, so we add more to be
        // safe.
        auto const additionalCover = mpt(50'000).value();
        env(loanBroker::coverDeposit(broker, brokerInfo.brokerID, STAmount{mpt, additionalCover}));
        env.close();
        // Verify broker owner is authorized
        auto const brokerMpt = keylet::mptoken(mptt.issuanceID(), broker);
        BEAST_EXPECT(env.le(brokerMpt) != nullptr);
        // Remove the credentials for the Broker owner.
        // First, pay any positive balance to issuer to zero it out
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();

        env(credentials::deleteCred(broker, broker, issuer, credType));
        env.close();

        // Make sure the broker is not authorized to hold the MPT after we
        // deleted the credentials
        env(pay(issuer, broker, mpt(1'000)), Ter(tecNO_AUTH));

        // Now borrower tries to make a payment
        // We should get a tesSUCCESS instead of a tecNO_AUTH.
        auto const borrowerBalance = env.balance(borrower, mpt);
        env(pay(borrower, keylet.key, mpt(10'100)), Fee(XRP(100)), Ter(tesSUCCESS));
        env.close();
        // Verify broker is still not authorized
        env(pay(issuer, broker, mpt(1'000)), Ter(tecNO_AUTH));
        // Verify the service fee went to the broker pseudo-account
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            Account const pseudo("pseudo-account", brokerSle->at(sfAccount));
            auto const balance = env.balance(pseudo, mpt);
            // 1,000 default + 50,000 extra + 100 service fee from LoanPay
            BEAST_EXPECTS(balance == mpt(51'100), to_string(json::Value(balance)));
        }
    }

    void
    testLoanSetBrokerOwnerNoPermissionedDomainMPT(FeatureBitset features)
    {
        testcase << "LoanSet Broker Owner without permissioned domain of the MPT";
        using namespace jtx;
        using namespace loan;

        Account const issuer("issuer");
        Account const borrower("borrower");
        Account const broker("broker");

        Env env{*this, features};
        env.fund(XRP(20'000), issuer, broker, borrower);
        env.close();

        auto credType = "credential1";

        pdomain::Credentials const credentials1{{.issuer = issuer, .credType = credType}};
        env(pdomain::setTx(issuer, credentials1));
        env.close();

        auto domainID = pdomain::getNewDomain(env.meta());

        // Add credentials for the broker and borrower
        env(credentials::create(broker, issuer, credType));
        env(credentials::accept(broker, issuer, credType));
        env.close();

        env(credentials::create(borrower, issuer, credType));
        env(credentials::accept(borrower, issuer, credType));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({
            .flags = tfMPTCanClawback | tfMPTRequireAuth | tfMPTCanTransfer | tfMPTCanLock,
            .domainID = domainID,
        });

        PrettyAsset const mpt{mptt.issuanceID()};

        // Authorize broker and borrower
        mptt.authorize({.account = broker});
        mptt.authorize({.account = borrower});
        env.close();

        // Fund accounts
        env(pay(issuer, broker, mpt(10'000'000)));
        env(pay(issuer, borrower, mpt(1'000)));
        env.close();

        // Create vault and broker
        auto const brokerInfo = createVaultAndBroker(env, mpt, broker);

        // Remove the credentials for the Broker owner.
        // Clear the balance first.
        auto const brokerBalance = env.balance(broker, mpt);
        env(pay(broker, issuer, brokerBalance));
        env.close();
        // Delete the credentials
        env(credentials::deleteCred(broker, broker, issuer, credType));
        env.close();

        // Create a loan, this should fail for tecNO_AUTH
        env(set(borrower, brokerInfo.brokerID, 10'000),
            Sig(sfCounterpartySignature, broker),
            kLoanServiceFee(mpt(100).value()),
            kPaymentInterval(100),
            Fee(XRP(100)),
            Ter(tecNO_AUTH));
        env.close();
    }

    // Verify that LoanPay, LoanBrokerCoverWithdraw, and LoanSet all use the
    // same vault-scale minimum cover when fixCleanup3_2_0 is enabled.
    // Before the amendment, each transactor computed its minimum cover at a
    // different precision (loanScale, debtScale, or the raw unrounded
    // tenthBipsOfValue), which could lead to inconsistent decisions for the
    // same broker state.  After the amendment all three use
    // minimumBrokerCover at vaultScale.
    void
    testMinimumBrokerCoverConsistency(FeatureBitset features)
    {
        using namespace jtx;
        using namespace loan;
        using namespace loanBroker;

        bool const withAmendment = features[fixCleanup3_2_0];

        struct Ctx
        {
            jtx::Account issuer;
            jtx::Account lender;
            jtx::Account borrower;
            jtx::PrettyAsset iou;
            BrokerInfo broker;
            BrokerParameters brokerParams;
        };

        // Shared setup, parametrized by vaultDeposit (the only varying setup
        // field across the three scenarios).  Each call runs in its own Env
        // so multiple invocations within one scenario cannot interfere.
        // The caller is responsible for invoking testcase(...) before the
        // first runTest call of each scenario.
        auto runTest = [&](Number vaultDeposit, auto&& body) {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"lender"};
            Account const borrower{"borrower"};

            env.fund(XRP(1'000'000'000), issuer, lender, borrower);
            env.close();

            // Enable clawback on the issuer *before* any trust lines exist
            // (asfAllowTrustLineClawback requires an empty owner directory).
            env(fset(issuer, asfAllowTrustLineClawback));
            env.close();

            PrettyAsset const iou = issuer[iouCurrency_];
            env(trust(lender, iou(1'000'000'000)));
            env(trust(borrower, iou(1'000'000'000)));
            env.close();
            env(pay(issuer, lender, iou(100'000'000)));
            env(pay(issuer, borrower, iou(100'000'000)));
            env.close();

            // 13.37% — non-round rate produces a messier minimum.
            BrokerParameters const brokerParams{
                .vaultDeposit = vaultDeposit,
                .debtMax = 0,
                .coverRateMin = TenthBips32{13'370},
                .coverDeposit = 5'000,
                .managementFeeRate = TenthBips16{500}};

            BrokerInfo const broker = createVaultAndBroker(env, iou, lender, brokerParams);

            body(
                env,
                Ctx{.issuer = issuer,
                    .lender = lender,
                    .borrower = borrower,
                    .iou = iou,
                    .broker = broker,
                    .brokerParams = brokerParams});
        };

        // Scenario 1 — LoanPay
        //
        // Verify that LoanPay's minimum cover check uses vault scale (not
        // loan scale).  Before the amendment, different loans could produce
        // different fee routing decisions for the same broker-level state.
        // Small vault deposit => vaultScale = -12.
        testcase("LoanPay minimum cover scale consistency");
        {
            struct LoanKeylets
            {
                Keylet tiny;
                Keylet big;
            };

            // Create the tiny + big loans and reduce cover via clawback so
            // that subsequent LoanPay calls hit the minimum-cover boundary.
            // Used by the two pay-and-check sub-tests below so each can run
            // in its own Env.
            auto setupLoansAndClawback = [&](Env& env, Ctx const& c) -> std::optional<LoanKeylets> {
                Asset const asset{c.iou};

                // Create the TINY loan first (while vaultScale is still
                // small).  principal 0.01, 0% interest, 1 payment =>
                // loanScale = vaultScale.
                auto const brokerSle1 = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle1))
                    return std::nullopt;
                auto const tinyLoanSeq = brokerSle1->at(sfLoanSequence);
                auto const tinyLoanKeylet = keylet::loan(c.broker.brokerID, tinyLoanSeq);

                env(set(c.borrower, c.broker.brokerID, Number{1, -2}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{0}),
                    kPaymentTotal(1),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Create the BIG loan second.  100% annual interest over 20
                // payments pushes totalValueOutstanding high enough that
                // loanScale > vaultScale.
                auto const brokerSle2 = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle2))
                    return std::nullopt;
                auto const bigLoanSeq = brokerSle2->at(sfLoanSequence);
                auto const bigLoanKeylet = keylet::loan(c.broker.brokerID, bigLoanSeq);

                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // The tiny loan's scale is frozen at the vault's pre-big-loan
                // scale, so it is strictly smaller than the big loan's.
                // After the big loan is created the vault absorbs its value,
                // pushing vaultScale up to match bigLoanScale.
                auto const tinyLoanSle = env.le(tinyLoanKeylet);
                auto const bigLoanSle = env.le(bigLoanKeylet);
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(tinyLoanSle) || !BEAST_EXPECT(bigLoanSle) ||
                    !BEAST_EXPECT(vaultSle))
                    return std::nullopt;
                if (!BEAST_EXPECT(tinyLoanSle->at(sfLoanScale) == -12) ||
                    !BEAST_EXPECT(bigLoanSle->at(sfLoanScale) == -11) ||
                    !BEAST_EXPECT(getAssetsTotalScale(vaultSle) == -11))
                    return std::nullopt;

                // Use issuer clawback to reduce cover to the minimum the
                // clawback transactor allows.  Compute the amount as
                // initialCover - expectedCoverAfter so we exercise the exact
                // clawback rather than relying on the transactor to clip
                // down.
                //
                // Before the amendment the clawback minimum is the
                // *unrounded* tenthBipsOfValue — strictly less than the
                // rounded-at-vaultScale minimum LoanPay uses for the big
                // loan.  After the amendment both clawback and LoanPay use
                // the same rounded minimum (via minimumBrokerCover), so
                // cover lands exactly at that threshold.
                Number const expectedCoverAfter = withAmendment ? Number{1330651855688460000, -15}
                                                                : Number{1330651855688458000, -15};
                Number const clawbackAmount =
                    Number{c.brokerParams.coverDeposit} - expectedCoverAfter;

                env(coverClawback(c.issuer),
                    kLoanBrokerId(c.broker.brokerID),
                    kAmount(STAmount{asset, clawbackAmount}));
                env.close();

                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle) ||
                    !BEAST_EXPECT(brokerSle->at(sfCoverAvailable) == expectedCoverAfter))
                    return std::nullopt;

                return LoanKeylets{.tiny = tinyLoanKeylet, .big = bigLoanKeylet};
            };

            // Pay one loan and report whether the fee went to the broker's
            // pseudo account (the fallback when cover < minimum) rather
            // than to the owner.
            auto feeGoesToPseudo = [&](Env& env, Ctx const& c, Keylet const& loanKeylet) -> bool {
                Asset const asset{c.iou};
                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                if (!BEAST_EXPECT(brokerSle))
                    return false;
                auto const pseudoAcct = Account("pseudo", brokerSle->at(sfAccount));
                auto const pseudoBefore = env.balance(pseudoAcct, c.iou);

                auto const payLoan = env.le(loanKeylet);
                if (!BEAST_EXPECT(payLoan))
                    return false;
                auto const periodicPayment = payLoan->at(sfPeriodicPayment);
                auto const serviceFee = payLoan->at(sfLoanServiceFee);
                std::int32_t const loanScale = payLoan->at(sfLoanScale);

                auto const payment = roundPeriodicPayment(asset, periodicPayment, loanScale);
                auto const payAmt = STAmount{asset, payment + serviceFee};

                env(loan::pay(c.borrower, loanKeylet.key, payAmt), Fee(XRP(10)));
                env.close();

                auto const pseudoAfter = env.balance(pseudoAcct, c.iou);
                return pseudoAfter.number() > pseudoBefore.number();
            };

            // Pay the BIG loan in its own Env so its outcome cannot affect
            // the TINY-loan check.  With the fix, LoanPay and clawback use
            // the same vaultScale minimum (cover == minAtVaultScale =>
            // fee to owner).  Without the fix, LoanPay uses bigLoanScale=-11,
            // rounds up to a larger minimum than what clawback used =>
            // cover < min => fee to pseudo.
            runTest(/*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                auto const loans = setupLoansAndClawback(env, c);
                if (!loans)
                    return;
                BEAST_EXPECT(feeGoesToPseudo(env, c, loans->big) == !withAmendment);
            });

            // Pay the TINY loan in its own Env.  Fee goes to the owner
            // either way:
            //  - With the fix: LoanPay uses vaultScale=-11 (same as
            //    clawback) => owner.
            //  - Without the fix: LoanPay uses tinyLoanScale=-12, rounds
            //    up at -12 (a no-op) => min == cover => owner.
            runTest(/*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                auto const loans = setupLoansAndClawback(env, c);
                if (!loans)
                    return;
                BEAST_EXPECT(!feeGoesToPseudo(env, c, loans->tiny));
            });
        }

        // Scenario 2 — LoanBrokerCoverWithdraw
        //
        // Verify that CoverWithdraw's minimum cover check uses vault scale
        // (not scale(debtTotal, asset)).  Before the amendment, CoverWithdraw
        // used:
        //   roundToAsset(asset, tenthBipsOfValue(debt, rate), scale(debt, asset))
        // which could disagree with LoanPay's minimum (which used loanScale).
        //
        // Use a large vault deposit so that vaultScale (from AssetsTotal) is
        // strictly larger than debtScale (from DebtTotal).  With
        // vaultDeposit = 100,000: after the big loan
        //   AssetsTotal ≈ 109,500 → vaultScale = -10
        //   DebtTotal   ≈  10,000 → debtScale  = -11
        // The one-order-of-magnitude gap makes roundToAsset at -10 truncate
        // more aggressively than at -11, exposing the bug.
        testcase("CoverWithdraw minimum cover scale consistency");
        runTest(
            /*vaultDeposit=*/100'000, [&](Env& env, Ctx const& c) {
                Asset const asset{c.iou};

                // Create only the big loan to push DebtTotal up to ~10,000
                // while AssetsTotal stays around 109,500 (dominated by the
                // large vault deposit).
                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Read broker state and compute both old and new minimums.
                auto const brokerSle = env.le(keylet::loanBroker(c.broker.brokerID));
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(brokerSle) || !BEAST_EXPECT(vaultSle))
                    return;

                auto const coverAvail = brokerSle->at(sfCoverAvailable);
                auto const debtTotal = brokerSle->at(sfDebtTotal);
                auto const vaultScale = getAssetsTotalScale(vaultSle);
                auto const debtScale = scale(debtTotal, asset);

                // Sanity: debt scale differs from vault scale for this setup.
                BEAST_EXPECT(debtScale < vaultScale);

                auto const oldMin = [&]() {
                    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
                    return roundToAsset(
                        asset,
                        tenthBipsOfValue(debtTotal, TenthBips32{c.brokerParams.coverRateMin}),
                        debtScale);
                }();
                auto const newMin = minimumBrokerCover(
                    debtTotal, TenthBips32{c.brokerParams.coverRateMin}, vaultSle);

                // The new (vaultScale) minimum must be strictly larger than
                // the old (debtScale) minimum — that is the gap the amendment
                // closes.
                Number const expectedNewMin{1330650518688500000, -15};
                Number const expectedOldMin{1330650518688472000, -15};
                BEAST_EXPECT(newMin == expectedNewMin);
                BEAST_EXPECT(oldMin == expectedOldMin);

                // Try to withdraw so that remaining cover lands between the
                // two minimums:  oldMin < target < newMin.
                auto const target = oldMin + (newMin - oldMin) / 2;
                auto const withdrawAmount = STAmount{asset, coverAvail - target};

                if (withAmendment)
                {
                    // CoverWithdraw now uses vaultScale: target < newMin
                    // => FAILS.
                    env(coverWithdraw(c.lender, c.broker.brokerID, withdrawAmount),
                        Ter(tecINSUFFICIENT_FUNDS));
                }
                else
                {
                    // Old CoverWithdraw uses debtScale: target > oldMin
                    // => SUCCEEDS.
                    env(coverWithdraw(c.lender, c.broker.brokerID, withdrawAmount));
                }
                env.close();
            });

        // Scenario 3 — LoanSet
        //
        // Verify that LoanSet's minimum cover check uses vault scale (not the
        // raw unrounded tenthBipsOfValue).  Before the amendment, LoanSet
        // used tenthBipsOfValue(newDebtTotal, coverRateMinimum) (no
        // roundToAsset), while clawback/withdraw used different formulas.
        // After the amendment all use minimumBrokerCover at vaultScale, and
        // rounding at a coarser scale can absorb a tiny debt increase —
        // allowing a loan that would otherwise be rejected.
        testcase("LoanSet minimum cover scale consistency");
        runTest(
            /*vaultDeposit=*/1'000, [&](Env& env, Ctx const& c) {
                // Create the tiny loan (scale -12) AND the big loan (scale
                // -11).  Both loans are needed so that DebtTotal has a full
                // 16-digit mantissa — a "messy" value where roundToAsset at
                // vaultScale actually truncates digits and produces a
                // different result from the raw tenthBipsOfValue.  With only
                // the big loan, DebtTotal has ~4 significant digits and
                // rounding at scale -11 is a no-op, masking the amendment's
                // effect.
                env(set(c.borrower, c.broker.brokerID, Number{1, -2}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{0}),
                    kPaymentTotal(1),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                env(set(c.borrower, c.broker.brokerID, Number{500}),
                    Sig(sfCounterpartySignature, c.lender),
                    kInterestRate(TenthBips32{100'000}),
                    kPaymentTotal(20),
                    kPaymentInterval(86400 * 365),
                    Fee(XRP(10)));
                env.close();

                // Clawback to reduce cover to the clawback transactor's
                // minimum.  Pass the exact amount rather than relying on the
                // transactor to clip down; the setup matches Scenario 1 so
                // the same residual-cover values apply.
                Number const expectedCoverAfter = withAmendment ? Number{1330651855688460000, -15}
                                                                : Number{1330651855688458000, -15};
                Number const clawbackAmount =
                    Number{c.brokerParams.coverDeposit} - expectedCoverAfter;
                env(coverClawback(c.issuer),
                    kLoanBrokerId(c.broker.brokerID),
                    kAmount(c.iou(clawbackAmount)));
                env.close();

                // Verify scales.
                auto const vaultSle = env.le(keylet::vault(c.broker.vaultID));
                if (!BEAST_EXPECT(vaultSle))
                    return;
                auto const vaultScale = getAssetsTotalScale(vaultSle);
                BEAST_EXPECT(vaultScale == -11);

                // Now try to create a tiny additional loan.  Principal is
                // 1e-11 (the smallest value that survives the precision
                // check at loanScale = vaultScale = -11), with 0% interest
                // and 1 payment.
                //
                // The tiny debt increase adds ~1.337e-12 to the unrounded
                // minimum.
                // - Without the amendment: the old LoanSet formula rounds
                //   up during tenthBipsOfValue (16-digit Number
                //   normalisation), pushing the minimum past the cover left
                //   by clawback => tecINSUFFICIENT_FUNDS.
                // - With the amendment: minimumBrokerCover rounds at
                //   vaultScale=-11, which absorbs the tiny increase — the
                //   rounded minimum stays the same => tesSUCCESS.
                auto const tinyPrincipal = Number{1, -11};

                if (withAmendment)
                {
                    env(set(c.borrower, c.broker.brokerID, tinyPrincipal),
                        Sig(sfCounterpartySignature, c.lender),
                        kInterestRate(TenthBips32{0}),
                        kPaymentTotal(1),
                        kPaymentInterval(86400 * 365),
                        Fee(XRP(10)));
                }
                else
                {
                    env(set(c.borrower, c.broker.brokerID, tinyPrincipal),
                        Sig(sfCounterpartySignature, c.lender),
                        kInterestRate(TenthBips32{0}),
                        kPaymentTotal(1),
                        kPaymentInterval(86400 * 365),
                        Fee(XRP(10)),
                        Ter(tecINSUFFICIENT_FUNDS));
                }
                env.close();
            });
    }

    void
    runAmendmentIndependent()
    {
        testDisabled();
        testInvalidLoanSet();
        testInvalidLoanDelete();
        testInvalidLoanManage();
        testInvalidLoanPay();
        testIssuerLoan();
        testServiceFeeOnBrokerDeepFreeze();
        testRequireAuth();
        testBorrowerIsBroker();
        testLimitExceeded();
        testLendingCanTradeDisabledNoImpact();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        // Lifecycle
        testLifecycle(features);
        testLoanSet(features);
        testSelfLoan(features);
        testBatchBypassCounterparty(features);
        testWrongMaxDebtBehavior(features);

        // RPC
        testRPC(features);

        testOverpaymentManagementFee(features);
        testIssuerIsBorrower(features);
        testMinimumBrokerCoverConsistency(features);

        // Broker-owner permissions
        testLoanPayBrokerOwnerMissingTrustline(features);
        testLoanPayBrokerOwnerUnauthorizedMPT(features);
        testLoanPayBrokerOwnerNoPermissionedDomainMPT(features);
        testLoanSetBrokerOwnerNoPermissionedDomainMPT(features);
    }

public:
    void
    run() override
    {
        runAmendmentIndependent();
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            runAmendmentSensitive(features);
    }
};

class LoanBatch_test : public LoanTestBase
{
protected:
    beast::xor_shift_engine engine_;

    std::uniform_int_distribution<> assetDist_{0, 2};
    std::uniform_int_distribution<std::int64_t> principalDist_{100'000, 1'000'000'000};
    std::uniform_int_distribution<std::uint32_t> interestRateDist_{0, 10000};
    std::uniform_int_distribution<> paymentTotalDist_{12, 10000};
    std::uniform_int_distribution<> paymentIntervalDist_{60, 3600 * 24 * 30};
    std::uniform_int_distribution<std::uint16_t> managementFeeRateDist_{0, 10'000};
    std::uniform_int_distribution<> serviceFeeDist_{0, 20};
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
        auto const assetType = static_cast<AssetType>(assetDist_(engine_));
        auto const principalRequest = principalDist_(engine_);
        TenthBips16 const managementFeeRate{managementFeeRateDist_(engine_)};
        auto const serviceFee = serviceFeeDist_(engine_);
        TenthBips32 interest{interestRateDist_(engine_)};
        auto const payTotal = paymentTotalDist_(engine_);
        auto const payInterval = paymentIntervalDist_(engine_);

        BrokerParameters const brokerParams{
            .vaultDeposit = principalRequest * 10,
            .debtMax = 0,
            .coverRateMin = TenthBips32{0},
            .managementFeeRate = managementFeeRate};
        LoanParameters const loanParams{
            .account = lender,
            .counter = borrower,
            .principalRequest = principalRequest,
            .serviceFee = serviceFee,
            .interest = interest,
            .payTotal = payTotal,
            .payInterval = payInterval,
        };

        runLoan(assetType, brokerParams, loanParams, all_);
    }

public:
    void
    run() override
    {
        auto const numIterations = [s = arg()]() -> int {
            int const defaultNum = 5;
            if (s.empty())
                return defaultNum;
            try
            {
                std::size_t pos = 0;
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
                testcase << "Random Loan Test iteration " << (i + 1) << "/" << numIterations;
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
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        LoanParameters const loanParams{
            .account = Account("lender"),
            .counter = Account("borrower"),
            .principalRequest = Number{200000, -6},
            .interest = TenthBips32{50000},
            .payTotal = 2,
            .payInterval = 200};

        runLoan(AssetType::XRP, brokerParams, loanParams, all_);
    }
};

BEAST_DEFINE_TESTSUITE(Loan, tx, xrpl);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanBatch, tx, xrpl);
BEAST_DEFINE_TESTSUITE_MANUAL(LoanArbitrary, tx, xrpl);

}  // namespace xrpl::test
