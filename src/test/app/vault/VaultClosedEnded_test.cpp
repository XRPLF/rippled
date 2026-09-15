#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <source_location>
#include <string>
#include <utility>

namespace xrpl {

class VaultClosedEnded_test : public VaultTestBase
{
private:
    // VaultCreate malformation and happy paths for closed-ended vaults, plus the
    // featureLendingProtocolV1_1 gate.
    void
    testVaultCreateClosedEnded()
    {
        testcase("closed-ended VaultCreate");
        using namespace test::jtx;

        auto const withEnv = [this](FeatureBitset features, auto&& body) {
            Env env{*this, features};
            Account const owner{"owner"};
            env.fund(XRP(1000), owner);
            env.close();
            Vault vault{env};
            body(env, owner, vault);
        };

        Asset const asset = xrpIssue();
        auto const minPeriod = kMinInvestmentPeriod;
        auto const maxPeriod = kMaxInvestmentPeriod;
        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);

        // Gate: the three new fields require featureLendingProtocolV1_1.
        withEnv(
            testableAmendments() - featureLendingProtocolV1_1,
            [&](Env& env, Account const& owner, Vault& vault) {
                auto const sub = env.now().time_since_epoch().count() + 60;
                auto [tx, keylet] = vault.create(
                    {.owner = owner,
                     .asset = asset,
                     .vaultKind = closedEnded,
                     .subscriptionDate = sub,
                     .redemptionDate = sub + minPeriod});
                env(tx, Ter{temDISABLED});
            });

        /*
         * Valid closed-ended creation with a comfortably interior gap (well above
         * kMinInvestmentPeriod and well below kMaxInvestmentPeriod).
         */
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto const red = sub + 86400;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = red});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfVaultKind) == closedEnded);
                BEAST_EXPECT(sle->at(sfSubscriptionDate) == sub);
                BEAST_EXPECT(sle->at(sfRedemptionDate) == red);
            }
        });

        // ClosedEnded missing one of SubscriptionDate / RedemptionDate => temMALFORMED.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .redemptionDate = sub + minPeriod});
            env(tx, Ter{temMALFORMED});
        });
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub});
            env(tx, Ter{temMALFORMED});
        });

        /*
         * SubscriptionDate not strictly after parent close time (preclaim, state-dependent -
         * returns tecEXPIRED). This is the only reachable path to tecEXPIRED in VaultCreate; see
         * the note below the next case. Note: there is no separate "expired RedemptionDate" test
         * case here. preflight enforces red >= sub + kMinInvestmentPeriod, so any past
         * RedemptionDate implies a strictly-earlier, equally-past SubscriptionDate; the
         * SubscriptionDate check above short-circuits first. The RedemptionDate arm of the
         * hasExpired check in VaultCreate::preclaim is defensive and unreachable as the sole cause
         * of tecEXPIRED.
         */
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const nowSec = env.now().time_since_epoch().count();
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = nowSec,
                 .redemptionDate = nowSec + minPeriod});
            env(tx, Ter{tecEXPIRED});
        });

        /*
         * Gap smaller than kMinInvestmentPeriod => temMALFORMED. Includes the SubscriptionDate >=
         * RedemptionDate degenerate cases: the red == sub boundary and the strictly-reversed red <
         * sub case, the latter yielding a negative signed int64 gap that is caught by the
         * sub-minimum branch of the gap check.
         */
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = sub + minPeriod - 1});
            env(tx, Ter{temMALFORMED});
        });
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = sub});
            env(tx, Ter{temMALFORMED});
        });
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = sub - 1});
            env(tx, Ter{temMALFORMED});
        });

        // Gap equal to MAX_INVESTMENT_PERIOD => temMALFORMED (bound is half-open on the right).
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = sub + maxPeriod});
            env(tx, Ter{temMALFORMED});
        });

        // Gap strictly greater than MAX_INVESTMENT_PERIOD => temMALFORMED. Same code path as
        // gap == MAX_INVESTMENT_PERIOD above, but covers the "gap >= MAX" bullet fully.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = sub + maxPeriod + 1});
            env(tx, Ter{temMALFORMED});
        });

        // Happy path: gap exactly equal to kMinInvestmentPeriod is accepted (lower bound is
        // inclusive). A min-gap vault can originate a minimum-interval loan; see
        // LoanSet_test::testLoanSetClosedEnded.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto const red = sub + minPeriod;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = red});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfRedemptionDate) == red);
            }
        });

        // Happy path: gap one second less than MAX_INVESTMENT_PERIOD is
        // accepted (upper bound is exclusive).
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto const red = sub + maxPeriod - 1;
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = red});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfRedemptionDate) == red);
            }
        });

        // OpenEnded (absent/0) with SubscriptionDate or RedemptionDate present
        // => temMALFORMED.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = asset, .subscriptionDate = sub});
            env(tx, Ter{temMALFORMED});
        });
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto const sub = env.now().time_since_epoch().count() + 60;
            auto [tx, keylet] =
                vault.create({.owner = owner, .asset = asset, .redemptionDate = sub + minPeriod});
            env(tx, Ter{temMALFORMED});
        });

        // Unrecognised VaultKind => temMALFORMED.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = static_cast<std::uint8_t>(closedEnded + 1)});
            env(tx, Ter{temMALFORMED});
        });

        // Happy path: open-ended vault (no new fields present) is unaffected.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(!sle->isFieldPresent(sfVaultKind));
                BEAST_EXPECT(!sle->isFieldPresent(sfSubscriptionDate));
                BEAST_EXPECT(!sle->isFieldPresent(sfRedemptionDate));
            }
        });

        // Happy path: explicit `VaultKind = 0` (OpenEnded) behaves the same
        // as absent. Per spec, absent and OpenEnded are equivalent.
        withEnv(testableAmendments(), [&](Env& env, Account const& owner, Vault& vault) {
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = std::to_underlying(VaultKind::OpenEnded)});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                // OpenEnded is sfVaultKind's default; SoeDefault fields
                // aren't serialized when they hold the default value.
                BEAST_EXPECT(!sle->isFieldPresent(sfVaultKind));
                BEAST_EXPECT(!sle->isFieldPresent(sfSubscriptionDate));
                BEAST_EXPECT(!sle->isFieldPresent(sfRedemptionDate));
            }
        });
    }

    // SubscriptionDate boundary cases at the top of the UINT32 range.
    // (1) The largest legal sub picks red = UINT32_MAX exactly, which hits
    // the inclusive lower bound of the kMinInvestmentPeriod gap check.
    // (2) sub = UINT32_MAX must be rejected: sub + kMinInvestmentPeriod is
    // unrepresentable as the tx's UINT32 sfRedemptionDate, so no red value
    // can satisfy the gap check.
    void
    testVaultCreateSubscriptionDateBoundary()
    {
        testcase("closed-ended VaultCreate SubscriptionDate near UINT32_MAX");
        using namespace test::jtx;

        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);
        Asset const asset = xrpIssue();

        {
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            env.fund(XRP(1000), owner);
            env.close();

            Vault const vault{env};
            auto const sub = std::numeric_limits<std::uint32_t>::max() - kMinInvestmentPeriod;
            auto const red = std::numeric_limits<std::uint32_t>::max();
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = sub,
                 .redemptionDate = red});
            env(tx);
            env.close();
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfSubscriptionDate) == sub);
                BEAST_EXPECT(sle->at(sfRedemptionDate) == red);
            }
        }

        // sub = UINT32_MAX: no legal red exists because sub + kMinInvestmentPeriod
        // wraps in a UINT32. Every candidate red must fall to temMALFORMED via
        // the gap check in preflight.
        auto const rejectAtMax = [&, this](std::uint32_t red) {
            Env env{*this, testableAmendments()};
            Account const owner{"owner"};
            env.fund(XRP(1000), owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = closedEnded,
                 .subscriptionDate = std::numeric_limits<std::uint32_t>::max(),
                 .redemptionDate = red});
            env(tx, Ter{temMALFORMED});
        };
        rejectAtMax(std::numeric_limits<std::uint32_t>::max());
        rejectAtMax(0u);
        rejectAtMax(kMinInvestmentPeriod - 1u);
    }

    // Phase derivation across the SubscriptionDate / RedemptionDate boundaries, including the now
    // == SubscriptionDate case (which must still resolve to Subscription).
    void
    testVaultPhaseDerivation()
    {
        testcase("closed-ended phase derivation");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(1000), owner, depositor);
        env.close();

        Asset const asset = xrpIssue();
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 60u, kMinInvestmentPeriod);

        // Pre-seed shares during Subscription so the depositor has capital to
        // withdraw at the Redemption boundary below.
        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = XRP(10).value()}));
        env.close();

        auto const deposit =
            [&](TER expected, std::source_location const& loc = std::source_location::current()) {
                env(
                    WithSourceLocation{
                        vault.deposit(
                            {.depositor = depositor, .id = keylet.key, .amount = XRP(1).value()}),
                        loc},
                    Ter{expected});
            };
        auto const withdraw =
            [&](TER expected, std::source_location const& loc = std::source_location::current()) {
                env(
                    WithSourceLocation{
                        vault.withdraw(
                            {.depositor = depositor, .id = keylet.key, .amount = XRP(1).value()}),
                        loc},
                    Ter{expected});
            };

        auto const runTest = [&](TER expectedDeposit,
                                 TER expectedWithdraw,
                                 std::source_location const& loc =
                                     std::source_location::current()) {
            deposit(expectedDeposit, loc);
            withdraw(expectedWithdraw, loc);
        };

        // Assert both deposit and withdraw return codes at each point so the
        // active phase is uniquely identified:
        //   Subscription: deposit tesSUCCESS, withdraw tesSUCCESS
        //   Investment:   deposit tecEXPIRED, withdraw tecTOO_SOON
        //   Redemption:   deposit tecEXPIRED, withdraw tesSUCCESS

        // Ledger time comfortably before SubscriptionDate: Subscription.
        runTest(tesSUCCESS, tesSUCCESS);

        // Boundary: parent close time exactly at SubscriptionDate must still
        // be Subscription.
        closeToTime(env, Tp{D{sub}});
        runTest(tesSUCCESS, tesSUCCESS);

        // One second past SubscriptionDate: Investment.
        closeToTime(env, Tp{D{sub}} + getLedgerTimeResolution(env));
        runTest(tecEXPIRED, tecTOO_SOON);

        // Any point strictly before RedemptionDate remains Investment.
        closeToTime(env, Tp{D{red}} - getLedgerTimeResolution(env));
        runTest(tecEXPIRED, tecTOO_SOON);

        // Boundary: parent close time == RedemptionDate is Redemption (per
        // spec table: now >= RedemptionDate). Deposits are rejected but
        // withdrawals succeed.
        closeToTime(env, Tp{D{red}});
        runTest(tecEXPIRED, tesSUCCESS);
        env.close();
    }

    // Open-ended vaults are always in VaultPhase::NoPhase, regardless of the ledger clock or any
    // dates present on the vault.
    void
    testVaultPhaseDerivationOpenEnded()
    {
        testcase("open-ended phase derivation");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        env.fund(XRP(1000), owner);
        env.close();

        Asset const asset = xrpIssue();
        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        auto const checkPhaseAt = [&](NetClock::time_point at) {
            closeToTime(env, at);
            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(getVaultPhase(*env.current(), sle) == VaultPhase::NoPhase);
        };

        // Advance the clock through a wide range of ledger times: an open-ended vault's phase
        // must be NoPhase at every one of them, because the derivation short-circuits on
        // VaultKind::OpenEnded before it looks at any dates.
        auto const ledgerTime = Tp{D{30}} + env.closed()->header().closeTimeResolution;
        checkPhaseAt(ledgerTime);
        checkPhaseAt(ledgerTime + std::chrono::seconds{kMinInvestmentPeriod});
        checkPhaseAt(
            ledgerTime + std::chrono::seconds{kMaxInvestmentPeriod} -
            env.closed()->header().closeTimeResolution);
    }

    // VaultDeposit is allowed only during Subscription (or NoPhase). Rejected during Investment and
    // Redemption.
    void
    testVaultDepositClosedEnded()
    {
        testcase("closed-ended VaultDeposit phase gating");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(1000), owner, depositor);
        env.close();

        Asset const asset = xrpIssue();
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 60u, kMinInvestmentPeriod);

        auto const deposit =
            [&](TER expected, std::source_location const& loc = std::source_location::current()) {
                env(
                    WithSourceLocation{
                        vault.deposit(
                            {.depositor = depositor, .id = keylet.key, .amount = XRP(1).value()}),
                        loc},
                    Ter{expected});
                env.close();
            };

        // Subscription: allowed.
        deposit(tesSUCCESS);

        // Investment: rejected.
        env.close(Tp{D{sub + 1}});
        deposit(tecEXPIRED);

        // Redemption: rejected.
        env.close(Tp{D{red}});
        deposit(tecEXPIRED);
    }

    // VaultWithdraw is allowed in Subscription and Redemption; rejected in Investment. The
    // AssetsAvailable cap continues to apply and is exercised in Redemption against a vault with
    // capital deployed as an outstanding loan.
    void
    testVaultWithdrawClosedEnded()
    {
        testcase("closed-ended VaultWithdraw phase gating");
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        Account const borrower{"borrower"};
        env.fund(XRP(10'000), owner, depositor, borrower);
        env.close();

        Asset const asset = xrpIssue();
        // Widen the Investment window so a single-payment loan (min payment
        // interval 60s plus kLoanRedemptionBuffer) fits before RedemptionDate.
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 60u, kMinInvestmentPeriod + 3600u);

        // Deposit XRP(100) in Subscription so the depositor's shares are
        // worth XRP(100). The vault holds XRP(100) with
        // AssetsAvailable == AssetsTotal.
        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();

        // Create a loan broker backed by this vault. LoanBrokerSet has no
        // phase gate, so this is fine to do in Subscription.
        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, keylet.key));
        env.close();

        auto const withdraw = [&](STAmount const& amount,
                                  TER expected,
                                  std::source_location const& loc =
                                      std::source_location::current()) {
            env(
                WithSourceLocation{
                    vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = amount}),
                    loc},
                Ter{expected});
            env.close();
        };

        // Subscription: allowed (LP cancel).
        withdraw(XRP(1).value(), tesSUCCESS);

        // Investment: rejected.
        closeToTime(env, Tp{D{sub}} + getLedgerTimeResolution(env));
        withdraw(XRP(1).value(), tecTOO_SOON);

        // Deploy capital: borrower takes a loan of XRP(60) against the
        // vault, dropping AssetsAvailable to ~XRP(39) while AssetsTotal
        // remains ~XRP(99).
        env(loan::set(borrower, brokerKeylet.key, XRP(60).value()),
            loan::kInterestRate(TenthBips32(0)),
            kGracePeriod(60),
            kPaymentInterval(60),
            kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();

        // Redemption: withdrawals are allowed but subject to the AssetsAvailable cap. A small
        // withdrawal within AssetsAvailable succeeds. A withdrawal within the depositor's share
        // value but exceeding the vault's liquid balance fails with tecINSUFFICIENT_FUNDS from the
        // vault-shortage guard (not the insufficient-shares guard).
        closeToTime(env, Tp{D{red}});
        withdraw(XRP(10).value(), tesSUCCESS);
        withdraw(XRP(80).value(), tecINSUFFICIENT_FUNDS);
    }

    // End-to-end lifecycle of a closed-ended vault (Subscription → Investment → Redemption) with
    // multiple depositors and a real loan originated through the Investment leg. Exercises every
    // phase transition and verifies the expected deposit, withdrawal, and lending behaviour in each
    // phase.
    void
    testVaultClosedEndedLifecycle()
    {
        testcase("closed-ended vault lifecycle (subscribe → invest → redeem)");
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const borrower{"borrower"};
        env.fund(XRP(10'000), owner, alice, bob, borrower);
        env.close();

        auto const closedEnded = std::to_underlying(VaultKind::ClosedEnded);
        Asset const asset = xrpIssue();
        // Widen the Investment window so a single-payment loan (min payment interval
        // 60s plus kLoanRedemptionBuffer) fits before RedemptionDate with headroom.
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 300u, kMinInvestmentPeriod + 3600u);

        auto const sleCreate = env.le(keylet);
        BEAST_EXPECT(sleCreate);
        MPTIssue const shares{sleCreate->at(sfShareMPTID)};

        auto const balancesEq = [&](STAmount const& available, STAmount const& total) {
            auto const sle = env.le(keylet);
            BEAST_EXPECT(sle->at(sfAssetsAvailable) == available);
            BEAST_EXPECT(sle->at(sfAssetsTotal) == total);
        };
        auto const availableEq = [&](STAmount const& expected) { balancesEq(expected, expected); };

        // env.balance(account, mptIssue) name-resolves the issuer via Env::lookup, but the share
        // issuer is the vault's pseudo-account and is never registered with the jtx Env. Read the
        // MPToken SLE directly to avoid the lookup.
        auto const sharesEq = [&](Account const& holder, std::uint64_t expected) {
            auto const sle = env.le(keylet::mptoken(shares.getMptID(), holder.id()));
            std::uint64_t const actual = sle ? sle->getFieldU64(sfMPTAmount) : 0u;
            BEAST_EXPECT(actual == expected);
        };

        // ---- Subscription phase ----
        // A legitimate VaultSet succeeds (positive control for 3.7).
        {
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfData] = "AA";
            env(tx);
            env.close();
        }

        // alice deposits 100 XRP.
        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();
        sharesEq(alice, 100'000'000);
        availableEq(XRP(100).value());

        // bob deposits 200 XRP.
        env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = XRP(200).value()}));
        env.close();
        sharesEq(bob, 200'000'000);
        availableEq(XRP(300).value());

        // alice cancels 25 XRP (LP cancel is permitted in Subscription).
        env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(25).value()}));
        env.close();
        sharesEq(alice, 75'000'000);
        availableEq(XRP(275).value());

        // Create a loan broker backed by this vault. LoanBrokerSet has no phase gate, so it is
        // fine to do in Subscription.
        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, keylet.key));
        env.close();

        // ---- Investment phase (now == sub + 1) ----
        env.close(Tp{D{sub + 1}});

        // Deposits into a closed-ended vault past SubscriptionDate return tecEXPIRED.
        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(10).value()}),
            Ter{tecEXPIRED});
        env.close();
        // Withdrawals from a closed-ended vault during the Investment phase return tecTOO_SOON.
        env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(10).value()}),
            Ter{tecTOO_SOON});
        env.close();

        // A real loan is originated during Investment (permitted only in this phase). Zero-interest
        // one-payment schedule keeps AssetsTotal unchanged (both accrual and cash-basis
        // accounting recognise no interest at origination); AssetsAvailable drops by the loan
        // principal.
        env(loan::set(borrower, brokerKeylet.key, XRP(60).value()),
            loan::kInterestRate(TenthBips32(0)),
            kGracePeriod(60),
            kPaymentInterval(60),
            kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        auto const sleBroker = env.le(keylet::loanBroker(brokerKeylet.key));
        BEAST_EXPECT(sleBroker);
        auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1u));
        BEAST_EXPECT(env.le(loanKeylet));
        balancesEq(XRP(215).value(), XRP(275).value());

        // Non-immutable VaultSet still works in Investment (positive control).
        {
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfData] = "BB";
            env(tx);
            env.close();
        }

        // Depositor share balances unchanged by the loan origination; only AssetsAvailable moved.
        sharesEq(alice, 75'000'000);
        sharesEq(bob, 200'000'000);

        // ---- Redemption phase (now == red) ----
        env.close(Tp{D{red}});

        // Deposits into a closed-ended vault past SubscriptionDate return tecEXPIRED, in both
        // Investment and Redemption.
        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(10).value()}),
            Ter{tecEXPIRED});
        env.close();

        // alice redeems her remaining 75 XRP (fits within AssetsAvailable = 215).
        env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(75).value()}));
        env.close();
        sharesEq(alice, 0);
        balancesEq(XRP(140).value(), XRP(200).value());

        // bob has 200 XRP-worth of shares but only 140 XRP is available (the remaining 60 XRP
        // sits in the outstanding loan). A full 200 XRP withdrawal fails against the
        // AssetsAvailable cap; bob redeems 140 XRP instead and is left holding 60M shares backed
        // by the loan receivable — the realistic outcome when capital is still deployed at
        // Redemption.
        env(vault.withdraw({.depositor = bob, .id = keylet.key, .amount = XRP(200).value()}),
            Ter{tecINSUFFICIENT_FUNDS});
        env.close();
        env(vault.withdraw({.depositor = bob, .id = keylet.key, .amount = XRP(140).value()}));
        env.close();
        sharesEq(bob, 60'000'000);
        balancesEq(XRP(0).value(), XRP(60).value());

        // Defensive spot-check that the three immutable fields have not changed across the entire
        // lifecycle. Direct immutability coverage lives with the invariant tests.
        auto const sleFinal = env.le(keylet);
        if (BEAST_EXPECT(sleFinal))
        {
            BEAST_EXPECT(sleFinal->at(sfVaultKind) == closedEnded);
            BEAST_EXPECT(sleFinal->at(sfSubscriptionDate) == sub);
            BEAST_EXPECT(sleFinal->at(sfRedemptionDate) == red);
        }
    }

    // A loan whose payment is made after the Investment phase has ended
    // (well past its next-due-date and grace period, into Redemption) must
    // still be repayable. The vault phase must not gate LoanPay.
    void
    testVaultLoanLatePaymentAfterInvestment()
    {
        testcase("closed-ended vault: late loan payment during Redemption succeeds");
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const alice{"alice"};
        Account const borrower{"borrower"};
        env.fund(XRP(10'000), owner, alice, borrower);
        env.close();

        Asset const asset = xrpIssue();
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 300u, kMinInvestmentPeriod + 3600u);

        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, keylet.key));
        env.close();

        // Investment phase: originate a zero-interest, single-payment loan
        // with a 300s payment interval and 60s grace. The payment is due
        // shortly after origination and well before RedemptionDate.
        env.close(Tp{D{sub + 1}});
        env(loan::set(borrower, brokerKeylet.key, XRP(60).value()),
            loan::kInterestRate(TenthBips32(0)),
            kGracePeriod(60),
            kPaymentInterval(300),
            kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        auto const loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1u));
        BEAST_EXPECT(env.le(loanKeylet));

        // Advance to Redemption. The payment is now past its due date and
        // grace, and the vault is no longer in Investment.
        closeToTime(env, Tp{D{red}});

        env(loan::pay(borrower, loanKeylet.key, XRP(60).value(), tfLoanLatePayment));
        env.close();

        // Loan principal returned to the vault; assetsAvailable == assetsTotal.
        auto const sleAfter = env.le(keylet);
        if (BEAST_EXPECT(sleAfter))
        {
            BEAST_EXPECT(sleAfter->at(sfAssetsAvailable) == sleAfter->at(sfAssetsTotal));
            BEAST_EXPECT(sleAfter->at(sfAssetsAvailable) == XRP(100).value());
        }

        env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();
    }

    // Two concurrent loans against the same closed-ended vault in Investment
    // must coexist: both loan SLEs are created, AssetsAvailable reflects the
    // sum of the two outstanding principals, and each can be repaid
    // independently.
    void
    testVaultClosedEndedMultipleLoans()
    {
        testcase("closed-ended vault: multiple concurrent loans in Investment");
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;

        Env env{*this, testableAmendments()};
        Account const owner{"owner"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const borrower1{"borrower1"};
        Account const borrower2{"borrower2"};
        env.fund(XRP(10'000), owner, alice, bob, borrower1, borrower2);
        env.close();

        Asset const asset = xrpIssue();
        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, asset, 300u, kMinInvestmentPeriod + 3600u);

        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();
        env(vault.deposit({.depositor = bob, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, keylet.key));
        env.close();

        env.close(Tp{D{sub + 1}});

        auto const originate = [&](Account const& b, STAmount const& principal) {
            env(loan::set(b, brokerKeylet.key, principal),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(300),
                kPaymentTotal(1),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2));
            env.close();
        };
        originate(borrower1, XRP(50).value());
        originate(borrower2, XRP(70).value());

        auto const loan1 = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1u));
        auto const loan2 = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(2u));
        BEAST_EXPECT(env.le(loan1));
        BEAST_EXPECT(env.le(loan2));

        // Zero-interest at origination: AssetsTotal unchanged, AssetsAvailable
        // drops by the sum of the two loan principals.
        {
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfAssetsTotal) == XRP(200).value());
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == XRP(80).value());
            }
        }

        // Repay the first loan; the second remains outstanding.
        env(loan::pay(borrower1, loan1.key, XRP(50).value()));
        env.close();
        {
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfAssetsTotal) == XRP(200).value());
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == XRP(130).value());
            }
        }

        // Repay the second loan; vault is fully liquid again.
        env(loan::pay(borrower2, loan2.key, XRP(70).value()));
        env.close();
        {
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
            {
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == sle->at(sfAssetsTotal));
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == XRP(200).value());
            }
        }

        // Redemption: both depositors withdraw in full.
        env.close(Tp{D{red}});
        env(vault.withdraw({.depositor = alice, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();
        env(vault.withdraw({.depositor = bob, .id = keylet.key, .amount = XRP(100).value()}));
        env.close();
    }

    // VaultClawback has no phase gate: an issuer must be able to reclaim
    // asset from a depositor in Subscription, Investment and Redemption
    // alike. Uses an IOU with asfAllowTrustLineClawback so the issuer path
    // is exercised (XRP clawback with an explicit amount is temMALFORMED).
    void
    testVaultClawbackClosedEndedPhases()
    {
        testcase("closed-ended vault: VaultClawback succeeds in each phase");
        using namespace test::jtx;

        Env env{*this, testableAmendments()};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const alice{"alice"};
        env.fund(XRP(10'000), issuer, owner, alice);
        env.close();

        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const iou = issuer["IOU"];
        env.trust(iou(10'000), alice);
        env(pay(issuer, alice, iou(1'000)));
        env.close();

        auto const [vault, keylet, sub, red] =
            makeClosedEndedVault(env, owner, iou, 300u, kMinInvestmentPeriod + 3600u);

        env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = iou(300).value()}));
        env.close();

        auto const totalsEq = [&](STAmount const& expected) {
            auto const sle = env.le(keylet);
            if (BEAST_EXPECT(sle))
                BEAST_EXPECT(sle->at(sfAssetsTotal) == expected);
        };

        // Subscription phase clawback.
        env(vault.clawback(
            {.issuer = issuer, .id = keylet.key, .holder = alice, .amount = iou(10).value()}));
        env.close();
        totalsEq(iou(290).value());

        // Investment phase clawback.
        env.close(Tp{D{sub + 1}});
        env(vault.clawback(
            {.issuer = issuer, .id = keylet.key, .holder = alice, .amount = iou(10).value()}));
        env.close();
        totalsEq(iou(280).value());

        // Redemption phase clawback.
        env.close(Tp{D{red}});
        env(vault.clawback(
            {.issuer = issuer, .id = keylet.key, .holder = alice, .amount = iou(10).value()}));
        env.close();
        totalsEq(iou(270).value());
    }

public:
    void
    run() override
    {
        testVaultCreateClosedEnded();
        testVaultCreateSubscriptionDateBoundary();
        testVaultPhaseDerivation();
        testVaultPhaseDerivationOpenEnded();
        testVaultDepositClosedEnded();
        testVaultWithdrawClosedEnded();
        testVaultClosedEndedLifecycle();
        testVaultLoanLatePaymentAfterInvestment();
        testVaultClosedEndedMultipleLoans();
        testVaultClawbackClosedEndedPhases();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultClosedEnded, app, xrpl, 1);

}  // namespace xrpl
