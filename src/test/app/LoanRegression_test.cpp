#include <test/app/LoanTestBase.h>

namespace xrpl::test {

class LoanRegression_test : public LoanTestBase
{
private:
    void
    testRIPD3831(FeatureBitset features)
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
            .payInterval = 150};

        auto const assetType = AssetType::XRP;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);

        using tp = NetClock::time_point;
        using d = NetClock::duration;

        auto state = getCurrentState(env, broker, loanKeylet);
        if (auto loan = env.le(loanKeylet); BEAST_EXPECT(loan))
        {
            env.close(tp{d{loan->at(sfNextPaymentDueDate) + loan->at(sfGracePeriod) + 1}});
        }

        topUpBorrower(env, broker, issuer, borrower, state, loanParams.serviceFee);

        using namespace jtx::loan;

        auto jv = pay(borrower, loanKeylet.key, drops(XRPAmount(state.totalValue)));

        {
            auto const submitParam = to_string(jv);
            auto const jr = env.rpc("submit", borrower.name(), submitParam);

            BEAST_EXPECT(jr.isMember(jss::result));
            auto const jResult = jr[jss::result];
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
    testRIPD3459(FeatureBitset features)
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
            .payTotal = 10};

        auto const assetType = AssetType::MPT;

        Env env{*this, features};

        auto loanResult =
            createLoan(env, assetType, brokerParams, loanParams, issuer, lender, borrower);

        if (BEAST_EXPECT(loanResult); !loanResult.has_value())
            return;

        auto broker = std::get<BrokerInfo>(*loanResult);
        auto loanKeylet = std::get<Keylet>(*loanResult);
        auto pseudoAcct = std::get<Account>(*loanResult);

        VerifyLoanStatus const verifyLoanStatus(env, broker, pseudoAcct, loanKeylet);

        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == loanSle->at(sfTotalValueOutstanding));
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

        if (auto const brokerSle = env.le(broker.brokerKeylet()); BEAST_EXPECT(brokerSle))
        {
            if (auto const loanSle = env.le(loanKeylet); BEAST_EXPECT(loanSle))
            {
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == loanSle->at(sfTotalValueOutstanding));
                BEAST_EXPECT(brokerSle->at(sfDebtTotal) == beast::kZero);
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
        auto const txFee = Fee(XRP(100));

        Env env(*this);
        Vault const vault(env);

        env.fund(XRP(10'000), lender, issuer, borrower, depositor);
        env.close();

        auto [tx, vaultKeyLet] = vault.create({.owner = lender, .asset = xrpIssue()});
        env(tx, txFee);
        env.close();

        env(vault.deposit({.depositor = depositor, .id = vaultKeyLet.key, .amount = XRP(1'000)}),
            txFee);
        env.close();

        auto const brokerKeyLet = keylet::loanBroker(lender.id(), env.seq(lender));

        env(loanBroker::set(lender, vaultKeyLet.key), txFee);
        env.close();

        // BrokerInfo brokerInfo{xrpIssue(), keylet, vaultKeyLet, {}};

        STAmount const debtMaximumRequest = XRPAmount(200'000);

        env(set(borrower, brokerKeyLet.key, debtMaximumRequest),
            Sig(sfCounterpartySignature, lender),
            kInterestRate(TenthBips32(50'000)),
            kPaymentTotal(2),
            kPaymentInterval(150),
            Txflags(tfLoanOverpayment),
            txFee);
        env.close();

        std::uint32_t const loanSequence = 1;
        auto const loanKeylet = keylet::loan(brokerKeyLet.key, loanSequence);

        if (auto loan = env.le(loanKeylet); env.test.BEAST_EXPECT(loan))
        {
            env(loan::pay(borrower, loanKeylet.key, XRPAmount(150'001)),
                Txflags(tfLoanOverpayment),
                txFee);
            env.close();
        }
    }

    void
    testRIPD3902(FeatureBitset features)
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

        Env env{*this, features};

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

    // A LoanSet with InterestRate = 1 (0.001% annualized, the minimum non-zero
    // rate). At such a near-zero rate the closed-form payment factor
    // (1 + r)^n - 1 cancels catastrophically.
    //
    // Without fixCleanup3_2_0 the resulting amortization is degenerate and the
    // LoanSet is rejected with tecPRECISION_LOSS (no loan created). With the
    // amendment, computePowerMinusOneHybrid uses a numerically-stable series
    // expansion, so the loan is created and the scheduled payments
    // (2 * periodicPayment) cover the principal — no economic underpayment
    // (yield theft).
    //
    // The test runs the same LoanSet under both amendment settings and pins the
    // exact outcome for each.
    void
    testLoanSetNearZeroInterestRateSucceeds()
    {
        testcase("LoanSet near-zero interest rate covers principal");

        using namespace jtx;
        using namespace loan;

        Number const principalRequested{1000};

        struct Result
        {
            TER ter = tesSUCCESS;
            bool created = false;
            std::int32_t loanScale = 0;
            Number principal;
            Number totalValue;
            Number managementFee;
            Number periodicPayment;
        };

        auto runScenario = [&](FeatureBitset features, TER expectedTer) -> Result {
            Env env(*this, features);

            Account const issuer{"issuer"};
            Account const lender{"vaultOwner"};
            Account const borrower{"borrower"};

            PrettyAsset const iouAsset = createFundedRippleIouAsset(env, issuer, lender, borrower);

            auto const broker = createVaultAndBroker(
                env,
                iouAsset,
                lender,
                {.vaultDeposit = 100'000, .debtMax = 0, .managementFeeRate = TenthBips16{0}});

            auto const brokerSle = env.le(broker.brokerKeylet());
            BEAST_EXPECT(brokerSle);
            auto const loanSequence = brokerSle ? brokerSle->at(sfLoanSequence) : 0;
            auto const loanKeylet = keylet::loan(broker.brokerID, loanSequence);

            env(set(borrower, broker.brokerID, principalRequested),
                Sig(sfCounterpartySignature, lender),
                kInterestRate(TenthBips32{1}),
                kPaymentTotal(2),
                kPaymentInterval(400),
                Fee(env.current()->fees().base * 2),
                Ter(expectedTer));
            env.close();

            Result r;
            r.ter = env.ter();
            if (auto const loanSle = env.le(loanKeylet))
            {
                r.created = true;
                r.loanScale = loanSle->at(sfLoanScale);
                r.principal = loanSle->at(sfPrincipalOutstanding);
                r.totalValue = loanSle->at(sfTotalValueOutstanding);
                r.managementFee = loanSle->at(sfManagementFeeOutstanding);
                r.periodicPayment = loanSle->at(sfPeriodicPayment);
            }
            return r;
        };

        Result const fixed = runScenario(all_, tesSUCCESS);
        Result const legacy = runScenario(all_ - fixCleanup3_2_0, tecPRECISION_LOSS);

        // Without the amendment, the catastrophically-cancelling closed-form
        // payment factor produces a degenerate amortization that fails
        // checkLoanGuards: the LoanSet is rejected with tecPRECISION_LOSS and no
        // loan is created.
        BEAST_EXPECT(legacy.ter == tecPRECISION_LOSS);
        BEAST_EXPECT(!legacy.created);

        // With the amendment the stable series expansion produces a valid loan
        // at loanScale -10.
        BEAST_EXPECT(fixed.ter == tesSUCCESS);
        BEAST_EXPECT(fixed.created);
        BEAST_EXPECT(fixed.loanScale == -10);
        BEAST_EXPECT(fixed.principal == principalRequested);
        BEAST_EXPECT((fixed.totalValue == Number{10000000001903, -10}));
        BEAST_EXPECT(fixed.managementFee == beast::kZero);

        // Periodic payment from the numerically-stable series expansion, and the
        // scheduled total (2 * periodicPayment) which exceeds the 1000 principal
        // — no economic underpayment / yield theft.
        BEAST_EXPECT((fixed.periodicPayment == Number{5000000000951293762, -16}));
        BEAST_EXPECT((fixed.periodicPayment * 2 == Number{1000000000190258752, -15}));
        BEAST_EXPECT(fixed.periodicPayment * 2 > principalRequested);
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
        env(loanBroker::set(lender, vaultKeylet.key),
            loanBroker::kDebtMaximum(Number{100}),
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

    // Integration test: full lifecycle of a $1B loan in the bug regime.
    // Verifies that the vault collects the economically-correct interest
    // income and that conservation holds at the trust-line level.
    //
    // Pre-fix (closed-form `power(1+r, n) - 1`): vault collected only
    // ~$0.058 per $1B due to cancellation of `(1+r)^n - 1` at r*n ~ 5.7e-10.
    // Post-fix (hybrid binomial path): vault collects ~$0.38 per $1B,
    // matching the value computed independently with arbitrary-precision
    // Decimal arithmetic.
    void
    testFullLifecycleVaultPnLNearZeroRate()
    {
        testcase("integration: full loan lifecycle, vault interest at near-zero rate");

        using namespace jtx;
        using namespace jtx::loan;
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
        STAmount const trustLimit{iouAsset.raw(), Number{1, 17}};
        env(trust(lender, trustLimit));
        env(trust(borrower, trustLimit));
        env.close();
        env(pay(issuer, lender, iouAsset(5'000'000'000LL)));
        env(pay(issuer, borrower, iouAsset(5'000'000'000LL)));
        env.close();

        auto usdBalance = [&](Account const& a) {
            return env.balance(a, iouAsset.raw().get<Issue>()).value();
        };
        STAmount const borrowerStartBal = usdBalance(borrower);

        BrokerParameters const brokerParams{
            .vaultDeposit = Number{2, 9},
            .debtMax = Number{0},
            .coverRateMin = TenthBips32{0},
            .coverDeposit = 0,
            .managementFeeRate = TenthBips16{0},
            .coverRateLiquidation = TenthBips32{0}};
        BrokerInfo const broker{createVaultAndBroker(env, iouAsset, lender, brokerParams)};

        auto const vaultBefore = env.le(broker.vaultKeylet());
        BEAST_EXPECT(vaultBefore);
        Number const vaultAvailableBefore = vaultBefore->at(sfAssetsAvailable);

        // Loan: $1B principal, 3 payments, 600s interval, rate=1 TenthBips32.
        auto const loanSetFee = Fee(env.current()->fees().base * 2);
        Number const principalRequest{1, 9};
        auto createJson = env.json(
            set(borrower, broker.brokerID, principalRequest),
            Fee(loanSetFee),
            Json(sfCounterpartySignature, json::ValueType::Object));
        createJson["InterestRate"] = 1;
        createJson["PaymentTotal"] = 3;
        createJson["PaymentInterval"] = 600;

        auto const loanKeylet = nextLoanKeylet(env, broker);
        createJson = env.json(createJson, Sig(sfCounterpartySignature, lender));
        env(createJson, Ter(tesSUCCESS));
        env.close();

        auto const loanSle = env.le(loanKeylet);
        BEAST_EXPECT(loanSle);
        Number const expectedTotalInterest =
            loanSle->at(sfTotalValueOutstanding) - loanSle->at(sfPrincipalOutstanding);

        env(pay(borrower, loanKeylet.key, iouAsset(1'500'000'000LL)), Ter(tesSUCCESS));
        env.close();

        auto const vaultAfter = env.le(broker.vaultKeylet());
        Number const vaultAvailableAfter = vaultAfter->at(sfAssetsAvailable);
        Number const vaultGain = vaultAvailableAfter - vaultAvailableBefore;

        STAmount const borrowerEndBal = usdBalance(borrower);
        STAmount const borrowerNetOut = borrowerStartBal - borrowerEndBal;

        // Self-consistency: vault gained exactly the expected interest
        // computed at LoanSet, and the borrower's outflow matches.
        BEAST_EXPECT(vaultGain == expectedTotalInterest);
        BEAST_EXPECT(Number(borrowerNetOut) == expectedTotalInterest);

        // Mathematical correctness: the total interest for this loan
        // configuration is 0.38051750382930729983, calculated
        // independently using 50-digit Decimal arithmetic (no
        // cancellation possible at that precision). At Number's 19-digit
        // mantissa this rounds to 0.38051750382930729 — the literal
        // below. The vault's actual gain must agree to within
        // sub-microcent precision.
        Number const decimalReference{38051750382930729LL, -17};
        Number const tolerance{1, -6};  // 1e-6 USD = sub-microcent
        Number const error = abs(vaultGain - decimalReference);
        BEAST_EXPECTS(
            error < tolerance,
            "vault gain " + to_string(vaultGain) + " differs from Decimal reference " +
                to_string(decimalReference) + " by " + to_string(error) + " — exceeds tolerance " +
                to_string(tolerance));
    }

    void
    runAmendmentIndependent()
    {
        testRIPD3901();
        testBugOverpaymentPrincipalChange();
        testBugOverpayUnroundedAmount();
        testBugInterestDueDeltaCrash();
        testFullLifecycleVaultPnLNearZeroRate();
        testLoanSetNearZeroInterestRateSucceeds();
    }

    // Tests run under each entry in amendmentCombinations().
    void
    runAmendmentSensitive(FeatureBitset features)
    {
        testIntegerScalePrincipalSticks(features);
        testRIPD3831(features);
        testRIPD3459(features);
        testRIPD3902(features);
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

BEAST_DEFINE_TESTSUITE(LoanRegression, tx, xrpl);

}  // namespace xrpl::test
