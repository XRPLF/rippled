#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <utility>

namespace xrpl {

class VaultFixedPrecision_test : public VaultTestBase
{
    static FeatureBitset
    features()
    {
        return test::jtx::testableAmendments() | featureLendingProtocolV1_1 |
            featureLendingProtocolV1_2;
    }

    // Submits a VaultCreate for an open-ended vault at the given fixed
    // Scale and closes the ledger. Shared by every scenario below that
    // needs a Scale-6 vault rather than the protocol default.
    static std::pair<test::jtx::Vault, Keylet>
    createScaledVault(
        test::jtx::Env& env,
        test::jtx::Account const& owner,
        Asset const& asset,
        std::uint8_t scale)
    {
        test::jtx::Vault const vault{env};
        auto [create, keylet] = vault.create({.owner = owner, .asset = asset});
        create[sfScale] = scale;
        env(create);
        env.close();
        return {vault, keylet};
    }

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
        }

        {
            testcase("VaultCreate treats V1.2 as implying V1.1");
            Env env(*this, features() - featureLendingProtocolV1_1);
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create(
                {.owner = owner,
                 .asset = asset,
                 .vaultKind = std::to_underlying(VaultKind::OpenEnded)});
            tx[sfScale] = kVaultMaximumFixedIouScale;
            env(tx);
            env.close();

            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->at(sfLEVersion) == std::to_underlying(VaultVersion::FixedPrecision));
            BEAST_EXPECT(sle->at(sfVaultKind) == std::to_underlying(VaultKind::OpenEnded));

            auto [invalid, invalidKeylet] = vault.create({.owner = owner, .asset = asset});
            invalid[sfScale] = static_cast<std::uint8_t>(kVaultMaximumFixedIouScale + 1);
            env(invalid, Ter(temMALFORMED));
            BEAST_EXPECT(!env.le(invalidKeylet));
        }

        for (std::uint8_t const scaleValue :
             {kVaultMaximumFixedIouScale,
              static_cast<std::uint8_t>(kVaultMaximumFixedIouScale + 1)})
        {
            testcase(
                scaleValue == kVaultMaximumFixedIouScale
                    ? "VaultCreate accepts fixed Scale maximum"
                    : "VaultCreate rejects Scale above fixed maximum");
            Env env(*this, features());
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = scaleValue;
            if (scaleValue == kVaultMaximumFixedIouScale)
            {
                env(tx);
            }
            else
            {
                env(tx, Ter(temMALFORMED));
            }
            env.close();
            BEAST_EXPECT(
                static_cast<bool>(env.le(keylet)) == (scaleValue == kVaultMaximumFixedIouScale));
        }

        {
            testcase("CashBasis Vault retains legacy Scale maximum");
            auto const legacyFeatures = features() - featureLendingProtocolV1_2;
            Env env(*this, legacyFeatures);
            env.fund(XRP(1'000'000), issuer, owner);
            env.close();

            Vault const vault{env};
            auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            tx[sfScale] = kVaultMaximumLegacyIouScale;
            env(tx);
            env.close();

            auto const sle = env.le(keylet);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT(sle->at(sfLEVersion) == std::to_underlying(VaultVersion::CashBasis));
            BEAST_EXPECT(!sle->isFieldPresent(sfYieldUnrealized));
        }
    }

    void
    testDepositAdmission()
    {
        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};
        Number const open{9, 9};
        Number const baseUnit{1, -6};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();
        env(trust(owner, asset(open + Number{1})));
        env.close();
        env(pay(issuer, owner, asset(open + Number{1})));
        env.close();

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);

        testcase("VaultDeposit admits the Open boundary");
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(open)}));
        env.close();

        auto const atOpen = env.le(keylet);
        if (!BEAST_EXPECT(atOpen))
            return;
        BEAST_EXPECT(atOpen->at(sfAssetsTotal) == open);
        BEAST_EXPECT(atOpen->at(sfAssetsAvailable) == open);

        testcase("VaultDeposit rejects one base unit above Open");
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(baseUnit)}),
            Ter(tecLIMIT_EXCEEDED));
        env.close();

        auto const afterRejected = env.le(keylet);
        if (!BEAST_EXPECT(afterRejected))
            return;
        BEAST_EXPECT(afterRejected->at(sfAssetsTotal) == open);
        BEAST_EXPECT(afterRejected->at(sfAssetsAvailable) == open);
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
        BEAST_EXPECT(after->at(sfAssetsTotal) == deposit);
    }

    void
    testPartialTowardZeroRounding()
    {
        using namespace test::jtx;

        // On a fresh vault the share price is one base unit, so the share
        // round-trip already lands on the Scale-6 grid before
        // clampToAssetsTotalScale. These cases check that the deposit,
        // withdraw, and clawback paths still book that truncated amount.
        // A non-unit share price (loan yield) is needed to exercise the
        // clamp itself; that arrives with the lending PR.

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};
        Number const depositRequested{32'345'678, -7};  // 3.2345678
        Number const outflowRequested{10'000'005, -7};  // 1.0000005

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env(trust(owner, asset(4)));
        env.close();
        env(pay(issuer, owner, asset(4)));
        env.close();

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);

        testcase("VaultDeposit books the truncated amount on the base grid");
        env(vault.deposit(
            {.depositor = owner, .id = keylet.key, .amount = asset(depositRequested)}));
        env.close();

        auto afterDeposit = env.le(keylet);
        if (!BEAST_EXPECT(afterDeposit))
            return;
        BEAST_EXPECT(afterDeposit->at(sfAssetsTotal) == (Number{3'234'567, -6}));
        BEAST_EXPECT(afterDeposit->at(sfAssetsAvailable) == (Number{3'234'567, -6}));
        BEAST_EXPECT(env.balance(owner, asset) == asset(Number{765'433, -6}));

        testcase("VaultWithdraw books the truncated amount on the base grid");
        env(vault.withdraw(
            {.depositor = owner, .id = keylet.key, .amount = asset(outflowRequested)}));
        env.close();

        auto afterWithdraw = env.le(keylet);
        if (!BEAST_EXPECT(afterWithdraw))
            return;
        BEAST_EXPECT(afterWithdraw->at(sfAssetsTotal) == (Number{2'234'567, -6}));
        BEAST_EXPECT(afterWithdraw->at(sfAssetsAvailable) == (Number{2'234'567, -6}));
        BEAST_EXPECT(env.balance(owner, asset) == asset(Number{1'765'433, -6}));

        testcase("VaultClawback books the truncated amount on the base grid");
        env(vault.clawback(
            {.issuer = issuer,
             .id = keylet.key,
             .holder = owner,
             .amount = asset(outflowRequested).value()}));
        env.close();

        auto const afterClawback = env.le(keylet);
        if (!BEAST_EXPECT(afterClawback))
            return;
        BEAST_EXPECT(afterClawback->at(sfAssetsTotal) == (Number{1'234'567, -6}));
        BEAST_EXPECT(afterClawback->at(sfAssetsAvailable) == (Number{1'234'567, -6}));
    }

    void
    testIntegralAssetCapacity()
    {
        using namespace test::jtx;

        testcase("FixedPrecision MPT Vault enforces integral Open zone");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        constexpr std::uint64_t open = 9'000'000'000'000'000;
        constexpr std::uint64_t maximum = open + 1;

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();

        MPTTester mpt{env, issuer, kMptInitNoFund};
        mpt.create({.maxAmt = maximum, .flags = tfMPTCanTransfer});
        PrettyAsset const asset = mpt.issuanceID();
        mpt.authorize({.account = owner});
        env(pay(issuer, owner, asset(maximum)));
        env.close();

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
    testDepositDust()
    {
        using namespace test::jtx;

        testcase("VaultDeposit rejects sub-base-unit dust");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();
        env(trust(owner, asset(1)));
        env.close();
        env(pay(issuer, owner, asset(1)));
        env.close();

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(Number{1, -7})}),
            Ter(tecPRECISION_LOSS));
    }

    void
    testWithdrawDust()
    {
        using namespace test::jtx;

        testcase("VaultWithdraw rejects sub-base-unit dust");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner);
        env.close();
        env(trust(owner, asset(2)));
        env.close();
        env(pay(issuer, owner, asset(2)));
        env.close();

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
        env.close();

        env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(Number{1, -7})}),
            Ter(tecPRECISION_LOSS));
    }

    void
    testClawbackDust()
    {
        using namespace test::jtx;

        testcase("VaultClawback rejects sub-base-unit dust");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const depositor{"depositor"};
        PrettyAsset const asset{issuer["USD"]};

        Env env(*this, features());
        env.fund(XRP(1'000'000), issuer, owner, depositor);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env(trust(depositor, asset(2)));
        env.close();
        env(pay(issuer, depositor, asset(2)));
        env.close();

        auto [vault, keylet] = createScaledVault(env, owner, asset, 6);
        env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = asset(1)}));
        env.close();

        env(vault.clawback(
                {.issuer = issuer,
                 .id = keylet.key,
                 .holder = depositor,
                 .amount = asset(Number{1, -7}).value()}),
            Ter(tecPRECISION_LOSS));
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
        testDepositDust();
        testWithdrawDust();
        testClawbackDust();
    }
};

BEAST_DEFINE_TESTSUITE(VaultFixedPrecision, app, xrpl);

}  // namespace xrpl
