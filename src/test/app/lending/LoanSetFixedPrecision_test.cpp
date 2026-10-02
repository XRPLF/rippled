#include <test/app/lending/LoanFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class LoanSetFixedPrecision_test : public LoanFixedPrecisionBase
{
    void
    testAssetsDeployedOrigination()
    {
        using namespace test::jtx;

        struct Row
        {
            char const* name = nullptr;
            std::vector<Number> principals;
        };
        Row const rows[] = {
            {.name = "LoanSet on a FixedPrecision vault increments AssetsDeployed by the principal",
             .principals = {Number{300}}},
            {.name = "Two Loans on the same FixedPrecision vault sum into AssetsDeployed",
             .principals = {Number{100}, Number{200}}},
        };
        for (auto const& row : rows)
        {
            testcase(row.name);

            Number const vaultDeposit{1'000};
            constexpr std::uint32_t paymentTotal = 2;

            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] = setupIou(env);

            auto const fixture = setupLendingVault(env, owner, depositor, asset, vaultDeposit);

            checkFixedPrecisionSync(env, fixture.vaultKeylet, vaultDeposit);

            std::vector<Keylet> loanKeylets;
            Number totalPrincipal{0};
            for (auto const& principal : row.principals)
            {
                loanKeylets.push_back(openLoan(env, fixture, principal, paymentTotal));
                totalPrincipal += principal;
            }

            auto const sle = expectVault(
                env,
                fixture.vaultKeylet,
                {.available = vaultDeposit - totalPrincipal, .assetsDeployed = totalPrincipal});
            if (!sle)
                return;
            // Origination books principal as debt only; AssetsTotal is unchanged.
            BEAST_EXPECT(sle->at(sfAssetsTotal) == vaultDeposit);

            Number principalOutstanding{0};
            for (auto const& loanKeylet : loanKeylets)
            {
                auto const loanSle = env.le(loanKeylet);
                if (!BEAST_EXPECT(loanSle))
                    return;
                principalOutstanding += loanSle->at(sfPrincipalOutstanding);
            }
            BEAST_EXPECT(sle->at(sfAssetsDeployed) == principalOutstanding);
            checkVaultLoanSums(env, fixture, loanKeylets, row.name);
        }
    }

    void
    testLendingClawbackAndWithdrawWhileLoanOpen()
    {
        using namespace test::jtx;

        testcase("VaultClawback and partial VaultWithdraw succeed while a Loan is open");

        Number const vaultDeposit{1'000};
        std::chrono::seconds const investmentWindow{600};
        Number const principal{400};
        constexpr std::uint32_t paymentTotal = 2;
        Number const clawbackAmount{100};
        Number const withdrawAmount{100};

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env, {.clawback = true});

        auto const fixture =
            setupLendingVault(env, owner, depositor, asset, vaultDeposit, investmentWindow);
        auto const loanKeylet = openLoan(env, fixture, principal, paymentTotal);

        // Withdrawals need the Redemption phase; open loans are not phase-gated.
        env.close(fixture.redemptionDate + std::chrono::seconds{1});

        // AssetsAvailable = 600, AssetsTotal = 1000, AssetsDeployed = 400. AssetsTotal
        // must be recomputed from AssetsAvailable + AssetsDeployed, not reduced by the
        // clawed-back amount.
        env(fixture.vault.clawback(
            {.issuer = issuer,
             .id = fixture.vaultKeylet.key,
             .holder = depositor,
             .amount = asset(clawbackAmount).value()}));
        env.close();

        Number const availableAfterClawback = vaultDeposit - principal - clawbackAmount;
        Number const totalAfterClawback = vaultDeposit - clawbackAmount;
        auto sle = expectVault(
            env,
            fixture.vaultKeylet,
            {.available = availableAfterClawback,
             .assetsDeployed = principal,
             .total = totalAfterClawback});
        if (!sle)
            return;
        checkVaultLoanSums(env, fixture, {loanKeylet}, "after clawback");

        env(fixture.vault.withdraw(
            {.depositor = depositor,
             .id = fixture.vaultKeylet.key,
             .amount = asset(withdrawAmount)}));
        env.close();

        sle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        Number const availableAfterWithdraw = availableAfterClawback - withdrawAmount;
        Number const totalAfterWithdraw = totalAfterClawback - withdrawAmount;
        BEAST_EXPECT(sle->at(sfAssetsAvailable) == availableAfterWithdraw);
        BEAST_EXPECT(sle->at(sfAssetsDeployed) == principal);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == getAssetsTotal(sle));
        BEAST_EXPECT(sle->at(sfAssetsTotal) == totalAfterWithdraw);
        checkVaultLoanSums(env, fixture, {loanKeylet}, "after withdraw");
    }

    // A full-value VaultWithdraw exceeds AssetsAvailable while a Loan is open.
    // FixedPrecision checks AssetsDeployed != 0 before the insufficient-funds guard, so
    // it reports tecHAS_OBLIGATIONS; CashBasis has no such guard.
    void
    testLendingFinalWithdrawalWhileLoanOpen()
    {
        using namespace test::jtx;

        struct Row
        {
            char const* name = nullptr;
            FeatureBitset amendments;
            TER ter;
            bool fixedPrecision = false;
        };
        Row const rows[] = {
            {.name = "full-value VaultWithdraw while AssetsDeployed is non-zero hits the "
                     "FixedPrecision tecHAS_OBLIGATIONS guard",
             .amendments = features(),
             .ter = tecHAS_OBLIGATIONS,
             .fixedPrecision = true},
            {.name = "CashBasis: full-value VaultWithdraw while a Loan is open hits the ordinary "
                     "insufficient-funds guard",
             .amendments = features() - featureLendingProtocolV1_2,
             .ter = tecINSUFFICIENT_FUNDS,
             .fixedPrecision = false},
        };
        Number const vaultDeposit{1'000};
        std::chrono::seconds const investmentWindow{600};
        Number const principal{400};
        constexpr std::uint32_t paymentTotal = 2;

        for (auto const& row : rows)
        {
            testcase(row.name);

            Env env(*this, row.amendments);
            auto const [issuer, owner, depositor, asset] = setupIou(env);

            auto const fixture =
                setupLendingVault(env, owner, depositor, asset, vaultDeposit, investmentWindow);
            if (!row.fixedPrecision)
            {
                auto const sleBefore = env.le(fixture.vaultKeylet);
                if (!BEAST_EXPECT(sleBefore))
                    return;
                BEAST_EXPECT(!sleBefore->isFieldPresent(sfAssetsDeployed));
            }
            auto const loanKeylet = openLoan(env, fixture, principal, paymentTotal);

            // Withdrawals need the Redemption phase.
            env.close(fixture.redemptionDate + std::chrono::seconds{1});

            // The depositor's shares are worth the full 1000 AssetsTotal, so redeeming
            // all of them is a final withdrawal that also exceeds AssetsAvailable (600).
            env(fixture.vault.withdraw(
                    {.depositor = depositor,
                     .id = fixture.vaultKeylet.key,
                     .amount = asset(vaultDeposit)}),
                Ter(row.ter));
            env.close();

            if (row.fixedPrecision)
            {
                expectVault(
                    env,
                    fixture.vaultKeylet,
                    {.available = vaultDeposit - principal, .assetsDeployed = principal});
            }
            checkVaultLoanSums(env, fixture, {loanKeylet}, "final withdrawal rejected");
        }
    }

    // A full clawback of the sole holder's shares does not hit the
    // tecHAS_OBLIGATIONS guard when the AssetsAvailable shortfall is a whole
    // number of shares: assetsToClawback clamps to AssetsAvailable, which
    // truncates below the full share count, so the clawback succeeds and leaves
    // shares behind.
    void
    testLendingFullClawbackClampsInsteadOfHasObligations()
    {
        using namespace test::jtx;

        testcase(
            "full-value VaultClawback with a large AssetsDeployed clamps to "
            "AssetsAvailable instead of tecHAS_OBLIGATIONS");

        Number const vaultDeposit{1'000};
        std::chrono::seconds const investmentWindow{600};
        Number const principal{400};
        constexpr std::uint32_t paymentTotal = 2;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env, {.clawback = true});

        auto const fixture =
            setupLendingVault(env, owner, depositor, asset, vaultDeposit, investmentWindow);
        auto const loanKeylet = openLoan(env, fixture, principal, paymentTotal);

        env.close(fixture.redemptionDate + std::chrono::seconds{1});

        auto const sleBefore = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sleBefore))
            return;
        Number const assetsDeployed = sleBefore->at(sfAssetsDeployed);
        BEAST_EXPECT(assetsDeployed == principal);
        BEAST_EXPECT(Number(sleBefore->at(sfAssetsAvailable)) == vaultDeposit - principal);

        auto const shareMPTID = sleBefore->at(sfShareMPTID);
        auto const tokenBefore = env.le(keylet::mptoken(shareMPTID, depositor.id()));
        auto const issuanceBefore = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(tokenBefore) || !BEAST_EXPECT(issuanceBefore))
            return;
        std::uint64_t const sharesBefore = tokenBefore->getFieldU64(sfMPTAmount);
        BEAST_EXPECT(sharesBefore == issuanceBefore->getFieldU64(sfOutstandingAmount));

        env(fixture.vault.clawback(
                {.issuer = issuer, .id = fixture.vaultKeylet.key, .holder = depositor}),
            Ter(tesSUCCESS));
        env.close();

        auto const sleAfter = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sleAfter))
            return;
        BEAST_EXPECT(Number(sleAfter->at(sfAssetsDeployed)) == assetsDeployed);
        BEAST_EXPECT(Number(sleAfter->at(sfAssetsAvailable)) == beast::kZero);

        auto const tokenAfter = env.le(keylet::mptoken(shareMPTID, depositor.id()));
        auto const issuanceAfter = env.le(keylet::mptokenIssuance(shareMPTID));
        if (!BEAST_EXPECT(tokenAfter) || !BEAST_EXPECT(issuanceAfter))
            return;
        std::uint64_t const sharesAfter = tokenAfter->getFieldU64(sfMPTAmount);
        BEAST_EXPECT(sharesAfter > 0);
        BEAST_EXPECT(sharesAfter < sharesBefore);
        BEAST_EXPECT(issuanceAfter->getFieldU64(sfOutstandingAmount) == sharesAfter);

        checkVaultLoanSums(
            env, fixture, {loanKeylet}, "full clawback clamps instead of HAS_OBLIGATIONS");
    }

    void
    testLendingCashBasisControl()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "CashBasis vault never gets an AssetsDeployed field through the same loan "
            "flow");

        Number const vaultDeposit{1'000};
        Number const principal{300};
        constexpr std::uint32_t paymentTotal = 2;
        // Two equal repayments that together retire the whole principal.
        Number const repaymentInstallment = principal / 2;

        Env env(*this, features() - featureLendingProtocolV1_2);
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, vaultDeposit);

        auto const sleAtCreate = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sleAtCreate))
            return;
        BEAST_EXPECT(sleAtCreate->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis));
        BEAST_EXPECT(!sleAtCreate->isFieldPresent(sfAssetsDeployed));

        auto const loanKeylet = openLoan(env, fixture, principal, paymentTotal);

        auto sle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(!sle->isFieldPresent(sfAssetsDeployed));
        BEAST_EXPECT(sle->at(sfAssetsAvailable) == vaultDeposit - principal);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == vaultDeposit);

        env(pay(depositor, loanKeylet.key, asset(repaymentInstallment).value()));
        env.close();
        sle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(!sle->isFieldPresent(sfAssetsDeployed));

        env(pay(depositor, loanKeylet.key, asset(repaymentInstallment).value()));
        env.close();
        sle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(!sle->isFieldPresent(sfAssetsDeployed));
        BEAST_EXPECT(sle->at(sfAssetsAvailable) == vaultDeposit);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == vaultDeposit);
    }

    void
    expectCoarseningLoanRejected(
        test::jtx::Env& env,
        LendingFixture const& fixture,
        Number const& principal,
        std::string const& label)
    {
        using namespace test::jtx;
        using namespace loan;

        auto const interestRate = percentageToTenthBips(100);
        constexpr std::uint32_t gracePeriod = 60;
        constexpr std::uint32_t paymentInterval = 31'536'000;  // one year, in seconds
        constexpr std::uint32_t paymentTotal = 1;

        env(set(fixture.depositor, fixture.brokerKeylet.key, principal),
            kInterestRate(interestRate),
            kGracePeriod(gracePeriod),
            kPaymentInterval(paymentInterval),
            kPaymentTotal(paymentTotal),
            Sig(sfCounterpartySignature, fixture.owner),
            Fee(env.current()->fees().base * 2),
            Ter(tecLIMIT_EXCEEDED));
        env.close();

        auto const vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultScale(vaultSle) == getVaultBaseScale(vaultSle));
        BEAST_EXPECT(vaultSle->at(sfAssetsDeployed) == beast::kZero);
        checkVaultLoanSums(env, fixture, {}, label);
    }

    void
    testOriginationGuardBlocksCoarsening()
    {
        using namespace test::jtx;
        using namespace loan_broker;

        struct Row
        {
            char const* name = nullptr;
            Number depositAmount;
            // Loan principal; the vault's whole AssetsAvailable when unset.
            std::optional<Number> principal;
            bool withCover = false;
        };
        Row const rows[] = {
            {.name = "LoanSet rejects coarsening origination: 16-digit deposit",
             .depositAmount = Number{8999999999999999LL, -10},
             .principal =  // 899999.9999999999
             Number{800'000},
             .withCover = false},
            {.name = "LoanSet rejects coarsening origination: cover deposited",
             .depositAmount = Number{899'999},
             .principal = std::nullopt,
             .withCover = true},
            {.name = "LoanSet rejects coarsening origination: plain",
             .depositAmount = Number{899'999},
             .principal = std::nullopt,
             .withCover = false},
        };
        for (auto const& row : rows)
        {
            testcase(row.name);

            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] = setupIou(
                env,
                {.depositorTrust = 3'000'000,
                 .ownerTrust = row.withCover ? 100'000 : 1'000,
                 .depositorFunds = row.depositAmount,
                 .ownerFunds = row.withCover ? std::optional<Number>{90'000} : std::nullopt});

            std::chrono::seconds const investmentWindow{63'072'000};  // two years
            std::uint8_t const vaultScale{10};
            auto const coverRateMinimum =
                row.withCover ? percentageToTenthBips(10) : TenthBips32(0);
            auto const coverRateLiquidation =
                row.withCover ? percentageToTenthBips(50) : TenthBips32(0);

            // With cover, a 10% minimum leaves CoverAvailable to liquidate.
            auto const fixture = setupLendingVault(
                env,
                owner,
                depositor,
                asset,
                row.depositAmount,
                investmentWindow,
                vaultScale,
                coverRateMinimum,
                coverRateLiquidation);

            auto const vaultSle = env.le(fixture.vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            Number const principal =
                row.principal.value_or(Number{vaultSle->at(sfAssetsAvailable)});

            if (row.withCover)
            {
                // The vault's 10% CoverRateMinimum: cover deposit equals a
                // tenth of the loan principal.
                Number const coverDepositAmount = principal / 10;
                env(coverDeposit(owner, fixture.brokerKeylet.key, asset(coverDepositAmount)));
                env.close();
            }

            expectCoarseningLoanRejected(env, fixture, principal, row.name);
        }
    }

    // LoanSet transfer legs on a FixedPrecision vault, with and without an
    // origination fee and with the asset's issuer as borrower or broker owner. An
    // issuer leg is a no-op transfer, but the vault must still be debited once
    // per leg.
    void
    testLoanSetTransferLegShapes()
    {
        using namespace test::jtx;
        using namespace loan;

        struct Row
        {
            char const* name = nullptr;
            Number originationFee;
            bool issuerIsBorrower = false;
            bool issuerIsBrokerOwner = false;
        };
        // (i) no fee; (ii) origination fee; (iii) issuer is the borrower; (iv)
        // issuer is the broker owner. The vault is debited the full principal in
        // every case.
        Row const rows[] = {
            {.name = "LoanSet transfer legs: no fee",
             .originationFee = Number{0},
             .issuerIsBorrower = false,
             .issuerIsBrokerOwner = false},
            {.name = "LoanSet transfer legs: with fee",
             .originationFee = Number{20},
             .issuerIsBorrower = false,
             .issuerIsBrokerOwner = false},
            {.name = "LoanSet transfer legs: issuer is borrower",
             .originationFee = Number{0},
             .issuerIsBorrower = true,
             .issuerIsBrokerOwner = false},
            {.name = "LoanSet transfer legs: issuer is broker owner",
             .originationFee = Number{20},
             .issuerIsBorrower = false,
             .issuerIsBrokerOwner = true},
        };
        Number const vaultDeposit{1'000};
        Number const principal{300};
        constexpr std::uint32_t gracePeriod = 60;
        constexpr std::uint32_t paymentInterval = 120;
        constexpr std::uint32_t paymentTotal = 2;

        for (auto const& row : rows)
        {
            testcase(row.name);

            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] = setupIou(env);
            Account const& brokerOwner = row.issuerIsBrokerOwner ? issuer : owner;
            Account const& borrower = row.issuerIsBorrower ? issuer : depositor;

            auto const fixture =
                setupLendingVault(env, brokerOwner, depositor, asset, vaultDeposit);
            auto const before = snapshotVault(env, fixture.vaultKeylet, asset);
            Number const ownerBalanceBefore = env.balance(brokerOwner, asset).value();

            auto const loanKeylet =
                keylet::loan(fixture.brokerKeylet.key, SeqProxy::rawSequence(1));
            auto const submit = [&](auto&&... extra) {
                env(set(borrower, fixture.brokerKeylet.key, principal),
                    kInterestRate(TenthBips32(0)),
                    kGracePeriod(gracePeriod),
                    kPaymentInterval(paymentInterval),
                    kPaymentTotal(paymentTotal),
                    Sig(sfCounterpartySignature, brokerOwner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS),
                    extra...);
            };
            if (row.originationFee != beast::kZero)
            {
                submit(kLoanOriginationFee(row.originationFee));
            }
            else
            {
                submit();
            }
            env.close();

            auto const after = snapshotVault(env, fixture.vaultKeylet, asset);
            BEAST_EXPECT(before.available - after.available == principal);
            BEAST_EXPECT(after.available == after.vaultBalance);
            if (row.originationFee != beast::kZero && !row.issuerIsBrokerOwner)
            {
                BEAST_EXPECT(
                    env.balance(brokerOwner, asset).value() - ownerBalanceBefore ==
                    row.originationFee);
            }
            checkVaultLoanSums(env, fixture, {loanKeylet}, row.name);
        }
    }

    void
    testAssetsMaximumBelowDerivedTotalRefused()
    {
        using namespace test::jtx;

        testcase("VaultSet AssetsMaximum below the derived total (AA + DT) is refused on FP");

        Number const vaultDeposit{1'000};
        Number const principal{300};
        constexpr std::uint32_t paymentTotal = 2;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, vaultDeposit);
        auto const loanKeylet = openLoan(env, fixture, principal, paymentTotal);

        auto const vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        Number const derivedTotal = getAssetsTotal(vaultSle);
        BEAST_EXPECT(derivedTotal == vaultDeposit);

        test::jtx::Vault const vault{env};
        struct Row
        {
            Number max;
            TER ter;
        };
        // Below the derived total is refused; at or above it is allowed.
        Row const rows[] = {
            {.max = Number{999}, .ter = tecLIMIT_EXCEEDED},
            {.max = Number{1'000}, .ter = tesSUCCESS},
            {.max = Number{1'001}, .ter = tesSUCCESS},
        };
        for (auto const& row : rows)
        {
            auto tx = vault.set({.owner = owner, .id = fixture.vaultKeylet.key});
            tx[sfAssetsMaximum] = row.max;
            env(tx, Ter(row.ter));
            env.close();

            if (row.ter != tesSUCCESS)
                continue;
            auto const sle = env.le(fixture.vaultKeylet);
            if (BEAST_EXPECT(sle))
                BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == row.max);
        }

        checkVaultLoanSums(env, fixture, {loanKeylet}, "testAssetsMaximumBelowDerivedTotalRefused");
    }

    void
    testVaultDeleteRefusedWhileDebtOutstanding()
    {
        using namespace test::jtx;
        using namespace loan;

        testcase(
            "VaultDelete on FP with AssetsAvailable == 0 and AssetsDeployed > 0 is "
            "refused");

        Number const vaultDeposit{1'000};
        // Borrow the whole vault, in a single payment, so AssetsAvailable is
        // left at 0.
        Number const principal = vaultDeposit;
        constexpr std::uint32_t paymentTotal = 1;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env);

        auto const fixture = setupLendingVault(env, owner, depositor, asset, vaultDeposit);
        openLoan(env, fixture, principal, paymentTotal);

        auto const vaultSle = env.le(fixture.vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(Number(vaultSle->at(sfAssetsAvailable)) == beast::kZero);
        BEAST_EXPECT(Number(vaultSle->at(sfAssetsDeployed)) == principal);

        test::jtx::Vault const vault{env};
        env(vault.del({.owner = owner, .id = fixture.vaultKeylet.key}), Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(env.le(fixture.vaultKeylet));
    }

public:
    void
    run() override
    {
        testAssetsDeployedOrigination();
        testLendingClawbackAndWithdrawWhileLoanOpen();
        testLendingFinalWithdrawalWhileLoanOpen();
        testLendingFullClawbackClampsInsteadOfHasObligations();
        testLendingCashBasisControl();
        testOriginationGuardBlocksCoarsening();
        testLoanSetTransferLegShapes();
        testAssetsMaximumBelowDerivedTotalRefused();
        testVaultDeleteRefusedWhileDebtOutstanding();
    }
};

BEAST_DEFINE_TESTSUITE(LoanSetFixedPrecision, app, xrpl);

}  // namespace xrpl
