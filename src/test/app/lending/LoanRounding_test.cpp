#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>

namespace xrpl::test {

class LoanRounding_test : public LoanTestBase
{
private:
    void
    testDustManipulation(FeatureBitset features)
    {
        testcase("Dust manipulation");

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env{*this, features};

        // Setup: Create accounts
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        Account const victim{"victim"};

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

        BrokerParameters const brokerParams{
            .vaultDeposit = 10000,
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{1000},
            .coverRateLiquidation = TenthBips32{2500}};

        auto broker = createVaultAndBroker(env, asset, lender, brokerParams);

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
            return;

        auto const& vaultKeylet = broker.vaultKeylet();

        {
            auto const vaultSle = env.le(vaultKeylet);
            Number const assetsTotal = vaultSle->at(sfAssetsTotal);
            Number const assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "Before loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            // before the loan the assets total and available should be equal
            BEAST_EXPECT(assetsAvail == assetsTotal);
            BEAST_EXPECT(assetsAvail == broker.asset(brokerParams.vaultDeposit).number());
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
            Number const assetsTotal = vaultSle->at(sfAssetsTotal);
            Number const assetsAvail = vaultSle->at(sfAssetsAvailable);

            log << "After loan creation:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal << std::endl;
            log << "  AssetsAvailable: " << assetsAvail << std::endl;
            log << "  Difference: " << (assetsTotal - assetsAvail) << std::endl;

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return;
            auto const state = constructLoanState(loanSle);

            log << "Loan state:" << std::endl;
            log << "  ValueOutstanding: " << state.valueOutstanding << std::endl;
            log << "  PrincipalOutstanding: " << state.principalOutstanding << std::endl;
            log << "  InterestOutstanding: " << state.interestOutstanding() << std::endl;
            log << "  InterestDue: " << state.interestDue << std::endl;
            log << "  FeeDue: " << state.managementFeeDue << std::endl;

            // after loan creation the assets total and available should
            // reflect the value of the loan
            BEAST_EXPECT(assetsAvail < assetsTotal);
            BEAST_EXPECT(
                assetsAvail ==
                broker.asset(brokerParams.vaultDeposit - loanParams.principalRequest).number());
            BEAST_EXPECT(
                assetsTotal ==
                broker.asset(brokerParams.vaultDeposit + state.interestDue).number());
        }

        // Step 7: Trigger default (dust adjustment will occur)
        env(jtx::loan::manage(lender, loanKeylet.key, tfLoanDefault));
        env.close();

        // Step 8: Verify phantom assets created
        {
            auto const vaultSle2 = env.le(vaultKeylet);
            Number const assetsTotal2 = vaultSle2->at(sfAssetsTotal);
            Number const assetsAvail2 = vaultSle2->at(sfAssetsAvailable);

            log << "After default:" << std::endl;
            log << "  AssetsTotal: " << assetsTotal2 << std::endl;
            log << "  AssetsAvailable: " << assetsAvail2 << std::endl;
            log << "  Difference: " << (assetsTotal2 - assetsAvail2) << std::endl;

            // after a default the assets total and available should be equal
            BEAST_EXPECT(assetsAvail2 == assetsTotal2);
        }
    }

    void
    testRoundingAllowsUndercoverage(FeatureBitset features)
    {
        testcase("Minimum cover rounding allows undercoverage (XRP)");

        using namespace jtx;
        using namespace loan_broker;

        Env env{*this, features};

        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(200'000), lender, borrower);
        env.close();

        // Vault with XRP asset
        Vault const vault{env};
        auto [vaultCreate, vaultKeylet] = vault.create({.owner = lender, .asset = xrpIssue()});
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

        auto const brokerInfo = createVaultAndBroker(env, xrpAsset, lender, brokerParams);
        // Create a loan with principal 804 XRP and 0% interest (so
        // DebtTotal increases by exactly 804)
        env(loan::set(borrower, brokerInfo.brokerID, xrpAsset(804).value()),
            loan::kInterestRate(TenthBips32(0)),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 2));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Verify DebtTotal is exactly 804
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));
        }

        // Attempt to withdraw 2 XRP to self, leaving 80 XRP CoverAvailable.
        // The minimum is 80.4 XRP, which rounds up to 81 XRP, so this fails.
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(2).value()),
            Ter(tecINSUFFICIENT_FUNDS));
        BEAST_EXPECT(env.ter() == tecINSUFFICIENT_FUNDS);
        env.close();

        // Attempt to withdraw 1 XRP to self, leaving 81 XRP CoverAvailable.
        // because that leaves sufficient cover, this succeeds
        env(coverWithdraw(lender, brokerInfo.brokerID, xrpAsset(1).value()));
        BEAST_EXPECT(env.ter() == tesSUCCESS);
        env.close();

        // Validate CoverAvailable == 81 XRP and DebtTotal remains 804
        if (auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            BEAST_EXPECT(brokerSle))
        {
            log << *brokerSle << std::endl;
            BEAST_EXPECT(brokerSle->at(sfCoverAvailable) == xrpAsset(81).value());
            BEAST_EXPECT(brokerSle->at(sfDebtTotal) == Number(804));

            // Also demonstrate that the true minimum (804 * 10%) exceeds 80
            auto const theoreticalMin = tenthBipsOfValue(Number(804), TenthBips32(10'000));
            log << "Theoretical min cover: " << theoreticalMin << std::endl;
            BEAST_EXPECT(Number(804, -1) == theoreticalMin);
        }
    }

    void
    testYieldTheftRounding(std::uint32_t flags)
    {
        testcase("Rounding manipulation does not permit yield theft");
        using namespace jtx;
        using namespace loan;

        // 1. Setup Environment
        Env env(*this, all_);
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1000), issuer, lender, borrower);
        env.close();

        // 2. Asset Selection
        PrettyAsset const iou = issuer["USD"];
        env(trust(lender, iou(100'000'000)));
        env(trust(borrower, iou(100'000'000)));
        env(pay(issuer, lender, iou(100'000'000)));
        env(pay(issuer, borrower, iou(100'000'000)));
        env.close();

        // 3. Create Vault and Broker with High Debt Limit (100M)
        auto const brokerInfo = createVaultAndBroker(
            env,
            iou,
            lender,
            {
                .vaultDeposit = 5'000'000,
                .debtMax = Number{100'000'000},
                .coverDeposit = 500'000,
            });
        auto const [currentSeq, vaultKeylet] = [&]() {
            auto const brokerSle = env.le(keylet::loanBroker(brokerInfo.brokerID));
            if (!BEAST_EXPECT(brokerSle))
                return std::make_tuple(0u, keylet::unchecked(beast::kZero));
            auto const currentSeq = brokerSle->at(sfLoanSequence);
            auto const vaultKeylet = keylet::vault(brokerSle->at(sfVaultID));
            return std::make_tuple(currentSeq, vaultKeylet);
        }();

        // 4. Loan Parameters (Attack Vector)
        Number const principal = 1'000'000;
        TenthBips32 const interestRate = TenthBips32{1};  // 0.001%
        std::uint32_t const paymentInterval = 86400;
        std::uint32_t const paymentTotal = 3650;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        env(set(borrower, brokerInfo.brokerID, iou(principal).value(), flags),
            Sig(sfCounterpartySignature, lender),
            loan::kInterestRate(interestRate),
            loan::kPaymentInterval(paymentInterval),
            loan::kPaymentTotal(paymentTotal),
            Fee(loanSetFee));
        env.close();

        // --- RETRIEVE OBJECTS & SETUP ATTACK ---

        auto borrowerBalance = [&]() { return env.balance(borrower, iou); };
        auto const borrowerScale = static_cast<STAmount const&>(borrowerBalance()).exponent();

        auto const loanKeylet = keylet::loan(brokerInfo.brokerID, currentSeq);
        auto const maybePeriodicPayment = [&]() -> std::optional<STAmount> {
            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                return std::nullopt;
            // Construct Payment
            return STAmount{iou, loanSle->at(sfPeriodicPayment)};
        }();
        if (!maybePeriodicPayment)
            return;
        auto const periodicPayment = *maybePeriodicPayment;
        auto const roundedPayment =
            roundToScale(periodicPayment, borrowerScale, Number::RoundingMode::Upward);

        // ATTACK: Add dust buffer (1e-9) to force 'excess' logic execution
        STAmount const paymentBuffer{iou, Number(1, -9)};
        STAmount const attackPayment = periodicPayment + paymentBuffer;

        auto const maybeInitialVaultAssets = [&]() -> std::optional<Number> {
            auto const vault = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vault))
                return std::nullopt;
            return vault->at(sfAssetsTotal);
        }();
        if (!maybeInitialVaultAssets)
            return;
        auto const initialVaultAssets = *maybeInitialVaultAssets;

        // 5. Execution Loop
        int yieldTheftCount = 0;
        auto previousAssetsTotal = initialVaultAssets;

        for (int i = 0; i < 100; ++i)
        {
            auto const balanceBefore = borrowerBalance();
            env(pay(borrower, loanKeylet.key, attackPayment, flags));
            env.close();
            auto const borrowerDelta = balanceBefore - borrowerBalance();
            BEAST_EXPECT(borrowerDelta.signum() == roundedPayment.signum());

            auto const loanSle = env.le(loanKeylet);
            if (!BEAST_EXPECT(loanSle))
                break;
            auto const updatedPayment = STAmount{iou, loanSle->at(sfPeriodicPayment)};
            BEAST_EXPECT(
                (roundToScale(updatedPayment, borrowerScale, Number::RoundingMode::Upward) ==
                 roundedPayment));
            BEAST_EXPECT(
                (updatedPayment == periodicPayment) ||
                (flags == tfLoanOverpayment && i >= 2 && updatedPayment < periodicPayment));

            auto const currentVaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(currentVaultSle))
                break;

            auto const currentAssetsTotal = currentVaultSle->at(sfAssetsTotal);
            auto const delta = currentAssetsTotal - previousAssetsTotal;

            BEAST_EXPECT(
                (delta == beast::kZero && borrowerDelta <= roundedPayment) ||
                (delta > beast::kZero && borrowerDelta > roundedPayment));

            // If tx succeeded but Assets Total didn't change, interest was
            // stolen.
            if (delta == beast::kZero && borrowerDelta > roundedPayment)
            {
                yieldTheftCount++;
            }

            previousAssetsTotal = currentAssetsTotal;
        }

        BEAST_EXPECTS(yieldTheftCount == 0, std::to_string(yieldTheftCount));
    }

    // Regression for the dual-rounding fix at coarse (integer-MPT) scale.
    //
    // Loan: P=1, r=50% (50000 tenth-bips), n=3, yearly interval. The
    // amortization schedule produces a fractional principal
    // (~0.47) which under round-to-nearest collapses to 0 in a single
    // step, causing `doPayment`'s strict `>` assertion on principal to
    // fire mid-loan. With fixCleanup3_2_0 enabled, principal is rounded
    // upward (sticks at 1 across the first two periods) and only clears
    // in the final payment.
    //
    // The test pays one period at a time across three LoanPay
    // transactions and verifies the loan completes (paymentRemaining=0)
    // with totals matching the loan's economics (1 principal + 2 interest).
    void
    testIntegerScalePrincipalSticks(FeatureBitset features)
    {
        // Without fixCleanup3_2_0, this behavior will abort the server, so
        // don't run without it.
        if (!features[fixCleanup3_2_0])
            return;

        testcase("edge: integer MPT principal stuck mid-loan completes via final");

        using namespace jtx;
        Env env(*this, features);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(100'000), issuer, lender, borrower);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.maxAmt = 100'000, .flags = tfMPTCanTransfer});
        PrettyAsset const asset{mptt.issuanceID()};

        mptt.authorize({.account = lender});
        mptt.authorize({.account = borrower});

        env(pay(issuer, lender, asset(10'000)));
        env(pay(issuer, borrower, asset(10'000)));
        env.close();

        Vault const vault{env};
        auto [vaultTx, vaultKeylet] = vault.create({.owner = lender, .asset = asset});
        env(vaultTx);
        env.close();

        env(vault.deposit({.depositor = lender, .id = vaultKeylet.key, .amount = asset(5'000)}));
        env.close();

        auto const brokerKeylet = keylet::loanBroker(lender.id(), env.seq(lender));
        env(loan_broker::set(lender, vaultKeylet.key),
            loan_broker::kDebtMaximum(Number{100}),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const brokerStateBefore = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerStateBefore))
            return;
        auto const loanSequence = brokerStateBefore->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(brokerKeylet.key, loanSequence);

        env(loan::set(borrower, brokerKeylet.key, Number{1}),
            Sig(sfCounterpartySignature, lender),
            loan::kInterestRate(TenthBips32{50'000}),
            loan::kPaymentTotal(3),
            loan::kPaymentInterval(31'536'000),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const borrowerStart = env.balance(borrower, asset).value();

        // Three separate periodic payments of 1 each. Expected per-period
        // evolution at integer MPT scale (TVO = PO + interestDue +
        // managementFeeDue):
        //   start:        PO=1, TVO=3, paymentRemaining=3
        //   after pay #1: PO=1, TVO=2, paymentRemaining=2  (principal sticks)
        //   after pay #2: PO=1, TVO=1, paymentRemaining=1  (principal sticks)
        //   after pay #3: PO=0, TVO=0, paymentRemaining=0  (final clears)
        std::array<Number, 3> const expectedPO{Number{1}, Number{1}, Number{0}};
        std::array<Number, 3> const expectedTVO{Number{2}, Number{1}, Number{0}};
        std::array<std::uint32_t, 3> const expectedRemaining{2, 1, 0};

        for (int i = 0; i < 3; ++i)
        {
            env(loan::pay(borrower, loanKeylet.key, asset(1)), Ter(tesSUCCESS));
            env.close();

            auto const sle = env.le(loanKeylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->at(sfPrincipalOutstanding) == expectedPO[i]);
            BEAST_EXPECT(sle->at(sfTotalValueOutstanding) == expectedTVO[i]);
            BEAST_EXPECT(sle->at(sfPaymentRemaining) == expectedRemaining[i]);
        }

        // Borrower paid 3 total regardless of fee split (1 principal + 2
        // interest+fee, matching loan economics).
        auto const borrowerEnd = env.balance(borrower, asset).value();
        BEAST_EXPECT(borrowerStart - borrowerEnd == asset(3).value());
    }

#if LOAN_TODO
    void
    testLoanCoverMinimumRoundingExploit(FeatureBitset features)
    {
        auto testLoanCoverMinimumRoundingExploit = [&, this](Number const& principalRequest) {
            testcase << "LoanBrokerCoverClawback drains cover via rounding"
                     << " principalRequested=" << to_string(principalRequest);

            using namespace jtx;
            using namespace loan;
            using namespace loan_broker;

            Env env(*this, features);

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

            BrokerParameters brokerParams{.debtMax = 0, .coverRateMin = TenthBips32{10'000}};
            BrokerInfo broker{createVaultAndBroker(env, asset, lender, brokerParams)};

            auto const loanSetFee = Fee(env.current()->fees().base * 2);
            auto createTx = env.jt(
                set(borrower, broker.brokerID, principalRequest),
                Sig(sfCounterpartySignature, lender),
                loanSetFee,
                kPaymentInterval(600),
                kPaymentTotal(1),
                kGracePeriod(60));
            env(createTx);
            env.close();

            auto const brokerBefore = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerBefore);
            if (!brokerBefore)
                return;

            Number const debtOutstanding = brokerBefore->at(sfDebtTotal);
            Number const coverAvailableBefore = brokerBefore->at(sfCoverAvailable);

            BEAST_EXPECT(debtOutstanding > Number{});
            BEAST_EXPECT(coverAvailableBefore > Number{});

            log << "debt=" << to_string(debtOutstanding)
                << " cover_available=" << to_string(coverAvailableBefore);

            env(coverClawback(issuer, 0), loanBrokerID(broker.brokerID));
            env.close();

            auto const brokerAfter = env.le(keylet::loanBroker(broker.brokerID));
            BEAST_EXPECT(brokerAfter);
            if (!brokerAfter)
                return;

            Number const debtAfter = brokerAfter->at(sfDebtTotal);
            // the debt has not changed
            BEAST_EXPECT(debtAfter == debtOutstanding);

            Number const coverAvailableAfter = brokerAfter->at(sfCoverAvailable);

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

    // A residual overpayment can reduce the stored principal by one scale-unit
    // *less* than computeOverpaymentComponents predicts, firing the
    // "principal change agrees" XRPL_ASSERT_PARTS in doOverpayment:
    //
    //   trackedPrincipalDelta == principalOutstanding - newPrincipalOutstanding
    //
    // tryOverpayment re-amortizes the loan at the reduced principal, then
    // re-derives the theoretical principal from the new periodic payment via
    // (P * paymentFactor) / paymentFactor. That round-trip is not exact in
    // Number's 19-digit arithmetic; a positive residual pushes the recomputed
    // principal a hair above the exact grid point `oldPrincipal - delta`, and
    // the Upward rounding in tryOverpayment then bumps it a full scale-unit
    // higher. The principal therefore drops by `delta - 1 unit`, not `delta`.
    //
    // Concrete case (isolated, at the tryOverpayment level):
    // A 100 USD loan at the minimum non-zero rate, 3 payments, loanScale -10.
    // After one regular payment (principalOutstanding 66.6666666674) a residual overpayment of
    // 0.049999998 yields trackedPrincipalDelta 0.048999998 but only reduces the principal by
    // 0.0489999979 (newPrincipal 66.6176666695) — short by 1e-10.
    //
    // With fixCleanup3_2_0, tryOverpayment pins the new principal to the exact,
    // on-grid reduction (oldPrincipal - trackedPrincipalDelta) instead of the
    // lossy (P*factor)/factor round-trip, so the assertion holds and the
    // overpayment applies cleanly. The three "principal change agrees" /
    // "interest paid agrees" / "principal payment matches" assertions are
    // gated behind the same amendment, so without it they are disabled (the
    // server does not abort) and the loan keeps the pre-amendment computation.
    //
    // The test runs the same scenario under both amendment settings and checks
    // the stored principal against a ground-truth value derived independently of
    // the loan-state computation under test.
    void
    testBugOverpaymentPrincipalChange()
    {
        testcase("bug: doOverpayment asserts 'principal change agrees'");

        using namespace jtx;
        using namespace loan;
        using namespace xrpl::detail;

        struct Params
        {
            TenthBips32 interestRate;
            TenthBips16 managementFeeRate;
            std::uint32_t paymentTotal;
            std::uint32_t paymentInterval;
            std::int64_t principal;
            Number overpayment;
            TenthBips32 overpaymentInterestRate;
            TenthBips32 overpaymentFeeRate;
            std::optional<int> vaultScale;
        };

        struct Result
        {
            Number principalOutstanding;  // stored principal after the LoanPay
            Number expectedNewPrincipal;  // ground truth, independent of the fix
            Number managementFeeChange;   // managementFeeOutstanding after - before
            Number unit;                  // one scale-unit at the loan scale
        };

        auto runScenario = [this](FeatureBitset features, Params const& p) -> Result {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"vaultOwner"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset = createFundedRippleIouAsset(env, issuer, lender, borrower);
            Asset const asset = iouAsset.raw();

            auto const broker = createVaultAndBroker(
                env,
                iouAsset,
                lender,
                {.vaultDeposit = 900'000,
                 .debtMax = 0,
                 .managementFeeRate = p.managementFeeRate,
                 .vaultScale = p.vaultScale});

            auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle);
            auto const loanSequence = brokerSle ? brokerSle->at(sfLoanSequence) : 0;
            auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

            env(set(borrower, broker.brokerID, Number{p.principal}, tfLoanOverpayment),
                Sig(sfCounterpartySignature, lender),
                kInterestRate(p.interestRate),
                kPaymentTotal(p.paymentTotal),
                kPaymentInterval(p.paymentInterval),
                kGracePeriod(p.paymentInterval),
                kOverpaymentFee(p.overpaymentFeeRate),
                kOverpaymentInterestRate(p.overpaymentInterestRate),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            // The single LoanPay below makes one regular payment (the overpayment
            // is smaller than one period) and leaves the residual as an
            // overpayment.
            auto const s = getCurrentState(env, broker, loanKeylet);
            auto const periodicRate = loanPeriodicRate(s.interestRate, s.paymentInterval);
            auto const onePeriod = computePaymentComponents(
                env.current()->rules(),
                asset,
                s.loanScale,
                s.totalValue,
                s.principalOutstanding,
                s.managementFeeOutstanding,
                s.periodicPayment,
                periodicRate,
                s.paymentRemaining,
                p.managementFeeRate);

            // Ground truth: the stored principal must drop by exactly the regular
            // payment's principal portion plus the overpayment's principal
            // portion. computeOverpaymentComponents depends only on the
            // overpayment amount and rates (not on the loan-state computation
            // under test), so it is an independent oracle. Both components are
            // computed under the same rules as the env so the payment factor
            // matches.
            auto const overpaymentComponents = computeOverpaymentComponents(
                env.current()->rules(),
                asset,
                s.loanScale,
                p.overpayment,
                p.overpaymentInterestRate,
                p.overpaymentFeeRate,
                p.managementFeeRate);
            Number const expectedNewPrincipal = s.principalOutstanding -
                onePeriod.trackedPrincipalDelta - overpaymentComponents.trackedPrincipalDelta;

            Number const managementFeeBefore = s.managementFeeOutstanding;

            STAmount const payAmount{asset, onePeriod.trackedValueDelta + p.overpayment};
            env(pay(borrower, loanKeylet.key, payAmount),
                Txflags(tfLoanOverpayment),
                Ter(tesSUCCESS));
            env.close();

            auto const loanSle = env.le(loanKeylet);
            BEAST_EXPECT(loanSle);

            return Result{
                .principalOutstanding = loanSle ? Number{loanSle->at(sfPrincipalOutstanding)} : 0,
                .expectedNewPrincipal = expectedNewPrincipal,
                .managementFeeChange =
                    (loanSle ? Number{loanSle->at(sfManagementFeeOutstanding)} : Number{0}) -
                    managementFeeBefore,
                .unit = Number{1, s.loanScale}};
        };

        // Scenario 1: the original near-zero-rate principal reproduction
        // (loanScale -10, no management fee). 0.049999998 is smaller than one
        // period, so it stays a residual overpayment.
        Params const principalCase{
            .interestRate = TenthBips32{1},
            .managementFeeRate = TenthBips16{0},
            .paymentTotal = 3,
            .paymentInterval = 60,
            .principal = 100,
            .overpayment = Number{49999998, -9},
            .overpaymentInterestRate = TenthBips32{1000},
            .overpaymentFeeRate = TenthBips32{1000},
            .vaultScale = 1};

        // With fixCleanup3_2_0 the stored principal lands exactly on the
        // ground-truth grid point: it is reduced by exactly the overpayment's
        // principal portion. This is the key correctness check: if the principal
        // pin were removed (even with the assertions still gated off), the lossy
        // (P * factor) / factor round-trip would leave the principal one
        // scale-unit high and this would fail.
        Result const fixed = runScenario(all_, principalCase);
        BEAST_EXPECTS(
            fixed.principalOutstanding == fixed.expectedNewPrincipal,
            "fixed principal " + to_string(fixed.principalOutstanding) + " != expected " +
                to_string(fixed.expectedNewPrincipal));

        // Without the amendment the loan amortizes with the catastrophically
        // cancelling near-zero payment factor, so its schedule (and ground truth)
        // differ from the fixed case; the gated assertions keep the server from
        // aborting and the overpayment still lands exactly on that schedule.
        Result const legacy = runScenario(all_ - fixCleanup3_2_0, principalCase);
        BEAST_EXPECTS(
            legacy.principalOutstanding == legacy.expectedNewPrincipal,
            "legacy principal " + to_string(legacy.principalOutstanding) + " != expected " +
                to_string(legacy.expectedNewPrincipal));

        // Scenario 2: a normal-rate loan with a 10% management fee. At a normal
        // rate the payment factor is identical across the amendment, so toggling
        // fixCleanup3_2_0 isolates the fix. This overpayment (found by search)
        // lands on a state where both the principal and the management fee differ
        // by one scale-unit between the fixed and legacy paths.
        Params const feeCase{
            .interestRate = TenthBips32{10000},
            .managementFeeRate = TenthBips16{10000},
            .paymentTotal = 6,
            .paymentInterval = 30u * 24 * 60 * 60,
            .principal = 1000,
            .overpayment = Number{214367363, -10},
            .overpaymentInterestRate = TenthBips32{0},
            .overpaymentFeeRate = TenthBips32{0},
            .vaultScale = std::nullopt};

        Result const feeFixed = runScenario(all_, feeCase);
        Result const feeLegacy = runScenario(all_ - fixCleanup3_2_0, feeCase);

        // With the fix the principal is the exact reduction; without it the lossy
        // (P * factor) / factor round-trip leaves it one scale-unit high.
        BEAST_EXPECTS(
            feeFixed.principalOutstanding == feeFixed.expectedNewPrincipal,
            "fee-case fixed principal " + to_string(feeFixed.principalOutstanding) +
                " != expected " + to_string(feeFixed.expectedNewPrincipal));
        BEAST_EXPECTS(
            feeLegacy.principalOutstanding == feeLegacy.expectedNewPrincipal + feeLegacy.unit,
            "fee-case legacy principal " + to_string(feeLegacy.principalOutstanding) +
                " != expected " + to_string(feeLegacy.expectedNewPrincipal + feeLegacy.unit));

        // Management fee: the overpayment re-amortizes a fee-bearing loan, so the management fee
        // outstanding drops.
        //
        // Unlike the principal that is already at the correct precision, the re-amortized
        // management fee  is tenthBipsOfValue of the new schedule's gross interest, which depends
        // on the recomputed periodic payment. So the expected change below is a pinned constant
        // captured from a passing run a magic value only because there is nothing simpler to
        // compare against.
        //
        // At the integration level, toggling the amendment also changes the regular payment's
        // rounding so a fixed-vs-legacy comparison cannot isolate the overpayment management-fee
        // fix.
        BEAST_EXPECT(feeFixed.managementFeeChange == feeLegacy.managementFeeChange);
        BEAST_EXPECTS(
            (feeFixed.managementFeeChange == Number{-8219709543, -10}),
            "fee-case mgmt fee change " + to_string(feeFixed.managementFeeChange));
    }

    // An overpayment whose residual amount has more precision than loanScale
    // fires the isRounded(asset, overpayment, loanScale) assertion in
    // computeOverpaymentComponents (and a downstream "interest paid agrees"
    // assertion in doOverpayment). fixCleanup3_2_0 rounds the residual down
    // to loanScale before passing it in. The pre-amendment path can't be
    // tested here because the assertion fires in Debug builds and aborts
    // the test process — see the PR description for context.
    void
    testBugOverpayUnroundedAmount()
    {
        testcase("bug: computeOverpaymentComponents isRounded assertion");

        using namespace jtx;
        using namespace loan;
        Env env(*this, all_);

        Account const issuer{"issuer"};
        Account const lender{"vaultOwner"};
        Account const borrower{"borrower"};

        PrettyAsset const iouAsset = createFundedRippleIouAsset(env, issuer, lender, borrower);

        auto const broker = createVaultAndBroker(
            env,
            iouAsset,
            lender,
            {.vaultDeposit = 100'000,
             .debtMax = 5000,
             .managementFeeRate = TenthBips16{1000},
             .vaultScale = 1});

        auto const sleBroker = env.le(broker.brokerKeylet());
        if (!BEAST_EXPECT(sleBroker))
            return;
        auto const loanSequence = sleBroker->at(sfLoanSequence);
        auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

        using namespace loan;
        env(set(borrower, broker.brokerID, Number{1000}, tfLoanOverpayment),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32{10000}),
            kPaymentTotal(12),
            kPaymentInterval(60),
            kGracePeriod(60),
            kOverpaymentFee(TenthBips32{1000}),
            kOverpaymentInterestRate(TenthBips32{1000}),
            Fee(env.current()->fees().base * 2),
            Ter(tesSUCCESS));
        env.close();

        // periodic * 1.5 at 15-sig-digit precision: 125.000154585042. This
        // has too many digits to round cleanly to loanScale=-10, so the
        // overpayment residual fails the isRounded check.
        STAmount const payAmount{iouAsset.raw(), Number{125'000'154'585'042LL, -12}};
        env(pay(borrower, loanKeylet.key, payAmount), Txflags(tfLoanOverpayment), Ter(tesSUCCESS));
        env.close();
    }

    // A near-zero interest rate on a 100 USD loan
    // produces total interest of ~6 units at loanScale -9. Numerical error
    // in the amortization formula pushes the theoretical principal above
    // the theoretical value, producing a negative theoretical interest.
    // The payment delta then exceeds the actual outstanding interest,
    // violating XRPL_ASSERT_PARTS in computePaymentComponents.
    void
    testBugInterestDueDeltaCrash()
    {
        testcase("bug: LoanPay asserts 'interest due delta' on near-zero rate");

        using namespace jtx;
        using namespace std::chrono_literals;
        Env env(*this, all_);

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();

        PrettyAsset const iouAsset = issuer["USD"];
        env(trust(lender, iouAsset(1'000'000'000)));
        env(trust(borrower, iouAsset(1'000'000'000)));
        env(pay(issuer, lender, iouAsset(5'000'000)));
        env(pay(issuer, borrower, iouAsset(5'000'000)));
        env.close();

        BrokerParameters const brokerParams{
            .vaultDeposit = 1'000'000,
            .debtMax = 1'000'000,
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};

        BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender, brokerParams)};

        using namespace loan;

        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{100};

        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object));

        createJson["InterestRate"] = 1;  // minimum non-zero rate
        createJson["PaymentTotal"] = 3;
        createJson["PaymentInterval"] = 600;

        auto const keylet = nextLoanKeylet(env, broker);

        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(tesSUCCESS));
        env.close();

        // For principal=100, n=3 the amortization schedule produces a
        // periodic payment ≈ 33.33 USD. We pay 35 USD, which is more than
        // one period's worth — enough for the LoanPay path to enter
        // computePaymentComponents and reach the assertion that fires
        // when the bug is present. With the fix, the tx applies cleanly.
        env(pay(borrower, keylet.key, iouAsset(35)), Ter(tesSUCCESS));
        env.close();
    }

    void
    runAmendmentIndependent()
    {
        for (auto const flags : {0u, tfLoanOverpayment})
            testYieldTheftRounding(flags);
        testBugOverpaymentPrincipalChange();
        testBugOverpayUnroundedAmount();
        testBugInterestDueDeltaCrash();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testDustManipulation(features);
        testRoundingAllowsUndercoverage(features);
        testIntegerScalePrincipalSticks(features);
#if LOAN_TODO
        testLoanCoverMinimumRoundingExploit(features);
#endif
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

BEAST_DEFINE_TESTSUITE(LoanRounding, tx, xrpl);

}  // namespace xrpl::test
