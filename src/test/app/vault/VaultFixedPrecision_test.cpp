#include <test/app/vault/VaultFixedPrecisionBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class VaultFixedPrecision_test : public VaultFixedPrecisionBase
{
    void
    testCreate()
    {
        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        {
            testcase("VaultCreate writes FixedPrecision fields");
            Env env(*this, features());
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->at(sfLEVersion) == std::to_underlying(VaultVersion::FixedPrecision));
            BEAST_EXPECT(sle->at(sfScale) == kVaultDefaultIouScale);
            BEAST_EXPECT(sle->at(sfYieldUnrealized) == beast::kZero);
            BEAST_EXPECT(sle->at(sfAssetsDeployed) == beast::kZero);
            BEAST_EXPECT(getAssetsTotal(sle) == beast::kZero);
            BEAST_EXPECT(sle->at(sfAssetsTotal) == getAssetsTotal(sle));
        }

        {
            testcase(
                "VaultCreate without V1.1: V1.2 alone creates neither FixedPrecision nor "
                "closed-ended Vaults");
            Env env(*this, features() - featureLendingProtocolV1_1);
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            // VaultKind and the closed-ended dates need featureLendingProtocolV1_1.
            auto [kindTx, kindKeylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = std::to_underlying(VaultKind::OpenEnded)});
            env(kindTx, Ter(temDISABLED));
            BEAST_EXPECT(!env.le(kindKeylet));

            // A plain VaultCreate makes a Legacy Vault, with the legacy Scale maximum.
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = static_cast<std::uint8_t>(kVaultMaximumFixedPrecisionIouScale + 1);
            env(tx);
            env.close();

            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(!sle->isFieldPresent(sfLEVersion));
            BEAST_EXPECT(getVaultVersion(sle) == VaultVersion::Legacy);
        }

        // Without fixCleanup3_4_0, V1_2 alone leaves a vault on the CashBasis rules,
        // including the legacy Scale maximum.
        struct Row
        {
            char const* name = nullptr;
            FeatureBitset amendments;
            std::uint8_t scale;
            TER ter;
            VaultVersion version;
        };
        auto const aboveFixed = static_cast<std::uint8_t>(kVaultMaximumFixedPrecisionIouScale + 1);
        auto const fixedVersion = VaultVersion::FixedPrecision;
        auto const cashVersion = VaultVersion::CashBasis;
        Row const rows[] = {
            {.name = "VaultCreate accepts fixed Scale maximum",
             .amendments = features(),
             .scale = kVaultMaximumFixedPrecisionIouScale,
             .ter = tesSUCCESS,
             .version = fixedVersion},
            {.name = "VaultCreate rejects Scale above fixed maximum",
             .amendments = features(),
             .scale = aboveFixed,
             .ter = temMALFORMED,
             .version = fixedVersion},
            {.name = "CashBasis Vault retains legacy Scale maximum",
             .amendments = features() - featureLendingProtocolV1_2,
             .scale = kVaultMaximumLegacyIouScale,
             .ter = tesSUCCESS,
             .version = cashVersion},
            {.name =
                 "VaultCreate: V1_2 on, fixCleanup3_4_0 off falls back to the legacy Scale maximum",
             .amendments = features() - fixCleanup3_4_0,
             .scale = kVaultMaximumLegacyIouScale,
             .ter = tesSUCCESS,
             .version = cashVersion},
            {.name =
                 "VaultCreate: V1_2 on, fixCleanup3_4_0 off accepts Scale above the fixed maximum",
             .amendments = features() - fixCleanup3_4_0,
             .scale = aboveFixed,
             .ter = tesSUCCESS,
             .version = cashVersion},
        };
        for (auto const& row : rows)
        {
            testcase(row.name);
            Env env(*this, row.amendments);
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = row.scale;
            env(tx, Ter(row.ter));
            env.close();

            auto const sle = env.le(keylet);
            if (row.ter != tesSUCCESS)
            {
                BEAST_EXPECT(!sle);
                continue;
            }
            if (!BEAST_EXPECT(sle))
                continue;
            BEAST_EXPECT(sle->at(sfScale) == row.scale);
            BEAST_EXPECT(sle->at(sfLEVersion) == std::to_underlying(row.version));
            if (row.version == cashVersion)
            {
                BEAST_EXPECT(!sle->isFieldPresent(sfYieldUnrealized));
                BEAST_EXPECT(!sle->isFieldPresent(sfAssetsDeployed));
            }
        }
    }

    void
    testDepositAdmission()
    {
        using namespace test::jtx;

        Number const open{9, 9};
        Number const baseUnit{1, -6};

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.ownerOnly = true, .ownerTrust = open + Number{1}});

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);

        testcase("VaultDeposit admits the Open boundary");
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(open)}));
        env.close();

        expectVault(env, keylet, {.available = open, .assetsDeployed = Number{0}, .total = open});

        testcase("VaultDeposit rejects one base unit above Open");
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(baseUnit)}),
            Ter(tecLIMIT_EXCEEDED));
        env.close();

        expectVault(env, keylet, {.available = open, .assetsDeployed = Number{0}, .total = open});
    }

    void
    testExistingCashBasisVault()
    {
        using namespace test::jtx;

        testcase("V1.2 does not migrate an existing CashBasis Vault");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};
        Number const deposit{9'999'999'999'999'999LL};

        Env env(*this, features() - featureLendingProtocolV1_2);
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();
        env(trust(owner, STAmount{asset.raw(), 2, 16}));
        env.close();
        env(pay(issuer, owner, asset(deposit)));
        env.close();

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] = 0;
        env(create);
        env.close();

        auto const before = env.le(keylet);
        if (!BEAST_EXPECT(before))
            return;
        BEAST_EXPECT(before->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis));

        env.enableFeature(featureLendingProtocolV1_2);
        env.close();
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(deposit)}));
        env.close();

        auto const after = env.le(keylet);
        if (!BEAST_EXPECT(after))
            return;
        BEAST_EXPECT(after->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis));
        BEAST_EXPECT(!after->isFieldPresent(sfYieldUnrealized));
        BEAST_EXPECT(!after->isFieldPresent(sfAssetsDeployed));
        BEAST_EXPECT(after->at(sfAssetsTotal) == deposit);
    }

    void
    testPartialTowardZeroRounding()
    {
        using namespace test::jtx;

        // On a fresh vault the share price is one base unit, so the share round-trip
        // already lands on the Scale-6 grid. These cases check that deposit, withdraw
        // and clawback still book the truncated amount.

        Number const depositRequested{32'345'678, -7};  // 3.2345678
        Number const outflowRequested{10'000'005, -7};  // 1.0000005

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.clawback = true, .ownerOnly = true, .ownerTrust = 4});

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);

        testcase("VaultDeposit books the truncated amount on the base grid");
        env(vault.deposit(
            {.depositor = owner, .id = keylet.key, .amount = asset(depositRequested)}));
        env.close();

        expectVault(
            env,
            keylet,
            {.available = Number{3'234'567, -6},
             .assetsDeployed = Number{0},
             .total = Number{3'234'567, -6}});
        BEAST_EXPECT(env.balance(owner, asset) == asset(Number{765'433, -6}));

        testcase("VaultWithdraw books the truncated amount on the base grid");
        env(vault.withdraw(
            {.depositor = owner, .id = keylet.key, .amount = asset(outflowRequested)}));
        env.close();

        expectVault(
            env,
            keylet,
            {.available = Number{2'234'567, -6},
             .assetsDeployed = Number{0},
             .total = Number{2'234'567, -6}});
        BEAST_EXPECT(env.balance(owner, asset) == asset(Number{1'765'433, -6}));

        testcase("VaultClawback books the truncated amount on the base grid");
        env(vault.clawback(
            {.issuer = issuer,
             .id = keylet.key,
             .holder = owner,
             .amount = asset(outflowRequested).value()}));
        env.close();

        expectVault(
            env,
            keylet,
            {.available = Number{1'234'567, -6},
             .assetsDeployed = Number{0},
             .total = Number{1'234'567, -6}});
    }

    void
    testIntegralAssetCapacity()
    {
        using namespace test::jtx;

        testcase("FixedPrecision MPT Vault enforces integral Open zone");

        constexpr std::uint64_t open = 9'000'000'000'000'000;
        constexpr std::uint64_t maximum = open + 1;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupMpt(env, {.maxAmt = maximum, .holderFunds = maximum});

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create);
        env.close();

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(open)}));
        env.close();
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
            Ter(tecLIMIT_EXCEEDED));

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(sle->at(sfAssetsTotal) == Number{open});
    }

    void
    testDust()
    {
        using namespace test::jtx;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.clawback = true, .depositorTrust = 2});
        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);
        Number const dust{1, -7};

        testcase("VaultDeposit rejects sub-base-unit dust");
        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(dust)}),
            Ter(tecPRECISION_LOSS));

        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
        env.close();

        testcase("VaultWithdraw rejects sub-base-unit dust");
        env(vault.withdraw({.depositor = depositor, .id = keylet.key, .amount = asset(dust)}),
            Ter(tecPRECISION_LOSS));

        testcase("VaultClawback rejects sub-base-unit dust");
        env(vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(dust).value()}),
            Ter(tecPRECISION_LOSS));
    }

    struct SyncStep
    {
        enum class Op { Deposit, Withdraw, Clawback };
        Op op;
        std::int64_t amount;
        std::int64_t expectedAvailable;
    };

    // Applies each step as holder (the issuer claws back) and checks the
    // expected AssetsAvailable after it.
    void
    runSyncSteps(
        test::jtx::Env& env,
        test::jtx::Vault const& vault,
        Keylet const& keylet,
        test::jtx::Account const& issuer,
        test::jtx::Account const& holder,
        PrettyAsset const& asset,
        std::vector<SyncStep> const& steps)
    {
        for (auto const& step : steps)
        {
            switch (step.op)
            {
                case SyncStep::Op::Deposit:
                    env(vault.deposit(
                        {.depositor = holder, .id = keylet.key, .amount = asset(step.amount)}));
                    break;
                case SyncStep::Op::Withdraw:
                    env(vault.withdraw(
                        {.depositor = holder, .id = keylet.key, .amount = asset(step.amount)}));
                    break;
                case SyncStep::Op::Clawback:
                    env(vault.clawback(
                        {.issuer = issuer,
                         .id = keylet.key,
                         .holder = holder,
                         .amount = asset(step.amount).value()}));
                    break;
            }
            env.close();
            checkFixedPrecisionSync(env, keylet, Number{step.expectedAvailable});
        }
    }

    void
    testAssetsTotalStaysInSyncIou()
    {
        using namespace test::jtx;
        using Op = SyncStep::Op;

        testcase("FixedPrecision IOU vault keeps AssetsTotal in sync across deposits/withdrawals");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupIou(env, {.ownerOnly = true, .ownerTrust = 10'000});

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create);
        env.close();

        // Two deposits, a partial withdrawal, then a final withdrawal that empties
        // the vault.
        runSyncSteps(
            env,
            vault,
            keylet,
            issuer,
            owner,
            asset,
            {{.op = Op::Deposit, .amount = 1'000, .expectedAvailable = 1'000},
             {.op = Op::Deposit, .amount = 500, .expectedAvailable = 1'500},
             {.op = Op::Withdraw, .amount = 600, .expectedAvailable = 900},
             {.op = Op::Withdraw, .amount = 900, .expectedAvailable = 0}});
    }

    void
    testAssetsTotalStaysInSyncClawback()
    {
        using namespace test::jtx;
        using Op = SyncStep::Op;

        testcase(
            "FixedPrecision IOU vault keeps AssetsTotal in sync across clawback and final "
            "withdrawal");

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] = setupIou(env, {.clawback = true});

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create);
        env.close();

        // Clawback partially recovers assets; the rest is a final withdrawal.
        runSyncSteps(
            env,
            vault,
            keylet,
            issuer,
            depositor,
            asset,
            {{.op = Op::Deposit, .amount = 1'000, .expectedAvailable = 1'000},
             {.op = Op::Clawback, .amount = 400, .expectedAvailable = 600},
             {.op = Op::Withdraw, .amount = 600, .expectedAvailable = 0}});
    }

    void
    testAssetsTotalStaysInSyncIntegralAsset()
    {
        using namespace test::jtx;
        using Op = SyncStep::Op;

        testcase("FixedPrecision MPT vault keeps AssetsTotal in sync across deposits/withdrawals");

        constexpr std::uint64_t total = 1'000;

        Env env(*this, features());
        auto const [issuer, owner, depositor, asset] =
            setupMpt(env, {.maxAmt = total, .holderFunds = total});

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create);
        env.close();

        runSyncSteps(
            env,
            vault,
            keylet,
            issuer,
            owner,
            asset,
            {{.op = Op::Deposit, .amount = total, .expectedAvailable = total},
             {.op = Op::Withdraw, .amount = 400, .expectedAvailable = 600},
             {.op = Op::Withdraw, .amount = 600, .expectedAvailable = 0}});
    }

    void
    testCashBasisAssetsTotalControl()
    {
        using namespace test::jtx;

        testcase(
            "CashBasis vault: AssetsTotal tracks AssetsAvailable directly, AssetsDeployed absent");

        Env env(*this, features() - featureLendingProtocolV1_2);
        auto const [issuer, owner, depositor, asset] = setupIou(env, {.ownerOnly = true});

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] = 0;
        env(create);
        env.close();

        auto const expectCashBasis = [&](Number const& expected) {
            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(!sle->isFieldPresent(sfAssetsDeployed));
            BEAST_EXPECT(sle->at(sfAssetsTotal) == sle->at(sfAssetsAvailable));
            BEAST_EXPECT(sle->at(sfAssetsTotal) == expected);
            BEAST_EXPECT(getAssetsTotal(sle) == expected);
        };

        // Deposit and partial withdrawal move AssetsAvailable and AssetsTotal
        // together; there is no AssetsDeployed.
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1'000)}));
        env.close();
        expectCashBasis(Number{1'000});

        env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(400)}));
        env.close();
        expectCashBasis(Number{600});

        // Final withdrawal leaves both at zero.
        env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(600)}));
        env.close();
        expectCashBasis(Number{0});
    }

    // FixedPrecision VaultCreate refuses an AssetsMaximum that is not
    // exactly representable on the vault's base grid (10^-Scale), whether
    // because it needs more than 16 significant digits or because it has a
    // nonzero digit finer than the grid.
    void
    testAssetsMaximumNotRepresentableOnCreate()
    {
        using namespace test::jtx;

        testcase("FixedPrecision VaultCreate: AssetsMaximum must be exactly representable");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        Vault const vault{env};

        auto tryCreate = [&](Number const& cap, std::optional<std::uint8_t> scale, TER expected) {
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            if (scale)
                tx[sfScale] = *scale;
            tx[sfAssetsMaximum] = cap;
            env(tx, Ter(expected));
            env.close();
            if (expected == tesSUCCESS)
            {
                auto const sle = env.le(keylet);
                if (BEAST_EXPECT(sle))
                    BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == cap);
            }
            else
            {
                BEAST_EXPECT(!env.le(keylet));
            }
        };

        // Default Scale (6). 17 significant digits: not exactly representable
        // in a 16-digit STAmount.
        tryCreate(Number{12345678901234567LL, -1}, std::nullopt, tecPRECISION_LOSS);

        // Within 16 digits but off the 10^-6 base grid.
        tryCreate(Number{10000001, -7}, std::nullopt, tecPRECISION_LOSS);

        // Exactly on the base grid.
        tryCreate(Number{1000001, -6}, std::nullopt, tesSUCCESS);

        // Large value, still on the grid: any integer is a multiple of 10^-6.
        tryCreate(Number{1, 20}, std::nullopt, tesSUCCESS);

        // A different Scale: on grid.
        tryCreate(Number{1, -2}, std::uint8_t{2}, tesSUCCESS);

        // Same Scale: off grid (extra digit at 10^-3).
        tryCreate(Number{1, -3}, std::uint8_t{2}, tecPRECISION_LOSS);
    }

    // Same conditions, enforced by VaultSet::preclaim against the vault's
    // already-fixed Scale. A refused Set must leave AssetsMaximum unchanged.
    void
    testAssetsMaximumNotRepresentableOnSet()
    {
        using namespace test::jtx;

        testcase("FixedPrecision VaultSet: AssetsMaximum must be exactly representable");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create);
        env.close();

        auto currentMax = [&]() -> std::optional<Number> {
            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return std::nullopt;
            if (!sle->isFieldPresent(sfAssetsMaximum))
                return std::nullopt;
            return Number(sle->at(sfAssetsMaximum));
        };

        auto trySet = [&](Number const& cap, TER expected) {
            auto const before = currentMax();
            auto tx = vault.set({.owner = owner, .id = keylet.key});
            tx[sfAssetsMaximum] = cap;
            env(tx, Ter(expected));
            env.close();
            if (expected == tesSUCCESS)
            {
                BEAST_EXPECT(currentMax() == cap);
            }
            else
            {
                BEAST_EXPECT(currentMax() == before);
            }
        };

        // 17 significant digits.
        trySet(Number{12345678901234567LL, -1}, tecPRECISION_LOSS);
        // Off the 10^-6 grid but within 16 digits.
        trySet(Number{10000001, -7}, tecPRECISION_LOSS);
        // Exactly on the grid: accepted and stored.
        trySet(Number{1000001, -6}, tesSUCCESS);
        // Large on-grid value.
        trySet(Number{1, 20}, tesSUCCESS);
    }

    // On an integral asset (MPT, XRP) the base grid is 10^0, so any
    // non-integral AssetsMaximum is refused regardless of digit count.
    void
    testAssetsMaximumIntegralAssets()
    {
        using namespace test::jtx;

        testcase("FixedPrecision VaultCreate: AssetsMaximum on integral assets (MPT, XRP)");

        // MPT.
        {
            Env env(*this, features());
            auto const [issuer, owner, depositor, asset] =
                setupMpt(env, {.maxAmt = 1'000'000, .holderFunds = 1'000'000});

            Vault const vault{env};
            {
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfAssetsMaximum] = Number{5, -1};  // 0.5, non-integral
                env(tx, Ter(tecPRECISION_LOSS));
                env.close();
                BEAST_EXPECT(!env.le(keylet));
            }
            {
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfAssetsMaximum] = Number{1'000};
                env(tx, Ter(tesSUCCESS));
                env.close();
                auto const sle = env.le(keylet);
                if (BEAST_EXPECT(sle))
                    BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == Number{1'000});
            }
        }

        // XRP.
        {
            Env env(*this, features());
            Account const owner{"owner"};
            env.fund(XRP(1'000'000), owner);
            env.close();
            PrettyAsset const asset = xrpIssue();

            Vault const vault{env};
            {
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfAssetsMaximum] = Number{5, -1};
                env(tx, Ter(tecPRECISION_LOSS));
                env.close();
                BEAST_EXPECT(!env.le(keylet));
            }
            {
                auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
                tx[sfAssetsMaximum] = Number{1'000};
                env(tx, Ter(tesSUCCESS));
                env.close();
                auto const sle = env.le(keylet);
                if (BEAST_EXPECT(sle))
                    BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == Number{1'000});
            }
        }
    }

    // Control: pre-V1.2 (CashBasis) vaults have no FixedPrecision grid
    // check, so an off-grid AssetsMaximum is accepted on both VaultCreate and
    // VaultSet, as before this change.
    void
    testAssetsMaximumOffGridAcceptedPreV12()
    {
        using namespace test::jtx;

        testcase(
            "Pre-FixedPrecision (CashBasis) VaultCreate/Set: AssetsMaximum grid check does not "
            "apply");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features() - featureLendingProtocolV1_2);
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        Vault const vault{env};
        // Off the 10^-6 grid: on FixedPrecision this would be tecPRECISION_LOSS.
        Number const offGrid{10000001, -7};

        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] = 0;
        create[sfAssetsMaximum] = offGrid;
        env(create, Ter(tesSUCCESS));
        env.close();

        auto const sle = env.le(keylet);
        if (BEAST_EXPECT(sle))
            BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == offGrid);

        // VaultSet with a different off-grid value on the same pre-V1.2 vault.
        Number const offGrid2{10000003, -7};
        auto tx = vault.set({.owner = owner, .id = keylet.key});
        tx[sfAssetsMaximum] = offGrid2;
        env(tx, Ter(tesSUCCESS));
        env.close();
        auto const sle2 = env.le(keylet);
        if (BEAST_EXPECT(sle2))
            BEAST_EXPECT(Number(sle2->at(sfAssetsMaximum)) == offGrid2);
    }

    // With featureLendingProtocolV1_2 enabled, VaultSet rejects an
    // AssetsMaximum that is not representable at the Vault's scale on an
    // existing pre-V1.2 (CashBasis) Vault too. A Legacy/CashBasis Vault's scale
    // follows its AssetsTotal, so this uses a cap with more digits than an
    // STAmount holds.
    void
    testAssetsMaximumRejectedOnExistingVaultAfterV12()
    {
        using namespace test::jtx;

        testcase("Existing CashBasis vault after V1.2: VaultSet rejects an unrepresentable cap");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features() - featureLendingProtocolV1_2);
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        env(create, Ter(tesSUCCESS));
        env.close();

        env.enableFeature(featureLendingProtocolV1_2);
        env.close();

        // 17 significant digits.
        Number const unrepresentable{12'345'678'901'234'567, -1};
        auto tx = vault.set({.owner = owner, .id = keylet.key});
        tx[sfAssetsMaximum] = unrepresentable;
        env(tx, Ter(tecPRECISION_LOSS));
        env.close();

        auto ok = vault.set({.owner = owner, .id = keylet.key});
        ok[sfAssetsMaximum] = Number{1'000};
        env(ok, Ter(tesSUCCESS));
        env.close();
        auto const sle = env.le(keylet);
        if (BEAST_EXPECT(sle))
            BEAST_EXPECT(Number(sle->at(sfAssetsMaximum)) == Number{1'000});
    }

    // A Vault created before featureLendingProtocolV1_2 keeps an AssetsMaximum
    // that is off the 10^-Scale grid. After V1.2 the stored cap does not block
    // VaultSet, the owner can replace it with an on-grid cap, and the new cap is
    // enforced on deposit.
    void
    testOffGridAssetsMaximumReplacedAfterV12()
    {
        using namespace test::jtx;

        testcase("Existing vault with an off-grid cap: VaultSet replaces it after V1.2");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features() - featureLendingProtocolV1_2);
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();
        env.trust(asset(1'000), owner);
        env.close();
        env(pay(issuer, owner, asset(1'000)));
        env.close();

        Vault const vault{env};
        // Off the 10^0 grid.
        Number const offGrid{100'000'001, -7};

        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] = 0;
        create[sfAssetsMaximum] = offGrid;
        env(create, Ter(tesSUCCESS));
        env.close();

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
        env.close();

        env.enableFeature(featureLendingProtocolV1_2);
        env.close();

        auto const before = env.le(keylet);
        if (!BEAST_EXPECT(before))
            return;
        BEAST_EXPECT(Number(before->at(sfAssetsMaximum)) == offGrid);
        BEAST_EXPECT(getVaultVersion(before) == VaultVersion::CashBasis);

        // A VaultSet that does not touch AssetsMaximum is not affected by the
        // stored off-grid cap.
        auto data = vault.set({.owner = owner, .id = keylet.key});
        data[sfData] = "AB";
        env(data, Ter(tesSUCCESS));
        env.close();

        auto onGrid = vault.set({.owner = owner, .id = keylet.key});
        onGrid[sfAssetsMaximum] = Number{5};
        env(onGrid, Ter(tesSUCCESS));
        env.close();

        auto const after = env.le(keylet);
        if (!BEAST_EXPECT(after))
            return;
        BEAST_EXPECT(Number(after->at(sfAssetsMaximum)) == Number{5});

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(4)}));
        env.close();
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
            Ter(tecLIMIT_EXCEEDED));
        env.close();
        BEAST_EXPECT(getAssetsTotal(env.le(keylet)) == Number{5});
    }

    // Reproduces a reported issue: the end-of-transaction associateAsset pass is not
    // a no-op. The original witness (VaultDepositAssociateAsset_test.cpp) grows a
    // vault's running total by two decades so the exact sum needs 18 digits, more
    // than an IOU STAmount holds, and shows associateAsset quantizing it afterwards.
    // On this branch that same deposit is refused outright: the Open-zone guard
    // in checkOptionalVaultInflow (testDepositAdmission above) caps a FixedPrecision
    // IOU vault's total at the largest value representable in 16 digits at its
    // Scale, so a deposit that would need 18 digits never reaches associateAsset in
    // the first place. This test reproduces the two-decade-jump shape at Scale 10
    // (this branch's fixed Scale ceiling) and confirms it is rejected with
    // tecLIMIT_EXCEEDED, leaving the vault unchanged -- the deposit side of the
    // issue does not reproduce on FixedPrecision because the Open-zone guard
    // forecloses it structurally.
    //
    // The Withdraw/Clawback witnesses are not exercised here: their dust-vault
    // precondition (sfAssetsTotal below the smallest representable IOU, 10^-81)
    // needs a donation into an insolvent vault, which VaultDeposit::preclaim
    // refuses outright (tecLOCKED, no donation exemption -- a known, separately
    // tracked gap) and is otherwise unreachable by any sequence of transactions,
    // so it cannot be ported as a transactor test.
    void
    testDepositCoarseningRefusedInsteadOfAssociateAssetRounding()
    {
        using namespace test::jtx;

        testcase(
            "A deposit shaped like the associateAsset witness is refused by the "
            "Open-zone guard on FixedPrecision, not silently rounded");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner, depositor);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();
        env(trust(depositor, asset(20'000'000)));
        env.close();
        env(pay(issuer, depositor, asset(15'000'000)));
        env.close();

        Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] =
            kVaultMaximumFixedPrecisionIouScale;  // 10: the branch's fixed Scale ceiling
        env(create);
        env.close();

        // First deposit uses all 16 digits an IOU STAmount has, at Scale 10 (span 10^5..10^-10),
        // well inside the Open zone (9e5 at this Scale).
        Number const firstDeposit{1'234'567'890'123'456LL, -10};
        env(vault.deposit(
            {.depositor = depositor, .id = keylet.key, .amount = asset(firstDeposit)}));
        env.close();
        checkFixedPrecisionSync(env, keylet, firstDeposit);

        // Second deposit grows the magnitude by two more decades, mirroring the reporters'
        // witness (span would become 10^7..10^-10, 18 digits): refused before associateAsset
        // ever sees an unrepresentable total.
        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(10'000'000)}),
            Ter(tecLIMIT_EXCEEDED));
        env.close();

        // The vault is untouched by the refused deposit.
        checkFixedPrecisionSync(env, keylet, firstDeposit);
    }

    // LoanSet does not support FixedPrecision Vaults yet; a valid LoanSet
    // against one is refused.
    void
    testLoanSetRefusedOnFixedPrecisionVault()
    {
        using namespace test::jtx;

        testcase("LoanSet is refused on a FixedPrecision Vault");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const borrower{"borrower"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner, borrower);
        env.close();
        env(trust(owner, asset(1'000'000)));
        env(trust(borrower, asset(1'000'000)));
        env.close();
        env(pay(issuer, owner, asset(100'000)));
        env.close();

        Vault const vault{env};
        auto [create, vaultKeylet, subscriptionDate] =
            vault.createClosedEnded({.owner = owner, .asset = asset});
        create[sfScale] = 6;
        env(create);
        env.close();
        env(vault.deposit({.depositor = owner, .id = vaultKeylet.key, .amount = asset(10'000)}));
        env.close();
        vault.closePastSubscription(subscriptionDate);

        auto const vaultSle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(vaultSle))
            return;
        BEAST_EXPECT(getVaultVersion(vaultSle) == VaultVersion::FixedPrecision);

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, vaultKeylet.key));
        env.close();
        BEAST_EXPECT(env.le(brokerKeylet));

        env(loan::set(borrower, brokerKeylet.key, Number{1'000}),
            loan::kPaymentTotal(1),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2),
            Ter(tecNO_PERMISSION));
        env.close();

        auto const brokerSle = env.le(brokerKeylet);
        if (!BEAST_EXPECT(brokerSle))
            return;
        BEAST_EXPECT(brokerSle->at(sfOwnerCount) == 0);
        BEAST_EXPECT(brokerSle->at(sfDebtTotal) == beast::kZero);
    }

public:
    void
    run() override
    {
        testCreate();
        testDepositAdmission();
        testExistingCashBasisVault();
        testPartialTowardZeroRounding();
        testIntegralAssetCapacity();
        testDust();
        testAssetsTotalStaysInSyncIou();
        testAssetsTotalStaysInSyncClawback();
        testAssetsTotalStaysInSyncIntegralAsset();
        testCashBasisAssetsTotalControl();
        testAssetsMaximumNotRepresentableOnCreate();
        testAssetsMaximumNotRepresentableOnSet();
        testAssetsMaximumIntegralAssets();
        testAssetsMaximumOffGridAcceptedPreV12();
        testAssetsMaximumRejectedOnExistingVaultAfterV12();
        testOffGridAssetsMaximumReplacedAfterV12();
        testDepositCoarseningRefusedInsteadOfAssociateAssetRounding();
        testLoanSetRefusedOnFixedPrecisionVault();
    }
};

BEAST_DEFINE_TESTSUITE(VaultFixedPrecision, app, xrpl);

}  // namespace xrpl
