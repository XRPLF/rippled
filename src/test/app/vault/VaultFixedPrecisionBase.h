#pragma once

#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

// Shared scaffolding for the vault-only FixedPrecision tests: amendment set,
// IOU/MPT account setup, and vault assertions. Lending-specific scaffolding
// (LoanBroker/Loan fixtures and helpers) lives in
// src/test/app/lending/LoanFixedPrecisionBase.h, which derives from this
// class instead of duplicating it.
class VaultFixedPrecisionBase : public VaultTestBase
{
protected:
    static FeatureBitset
    features()
    {
        return test::jtx::testableAmendments() | featureLendingProtocolV1_1 |
            featureLendingProtocolV1_2;
    }

    // Creates an open-ended vault at the given fixed Scale and closes the ledger.
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

    struct IouParams
    {
        bool clawback = false;
        // Fund only the issuer and owner; the owner plays the depositor.
        bool ownerOnly = false;
        Number depositorTrust{1'000};
        Number ownerTrust{1'000};
        // Defaults to depositorTrust (ownerTrust when ownerOnly).
        std::optional<Number> depositorFunds = std::nullopt;
        // Extra USD paid to the owner alongside the depositor's.
        std::optional<Number> ownerFunds = std::nullopt;
    };

    struct AssetAccounts
    {
        test::jtx::Account issuer{"issuer"};
        test::jtx::Account owner{"owner"};
        test::jtx::Account depositor{"depositor"};
        test::jtx::PrettyAsset asset{issuer["USD"]};
    };

    // Funds an issuer, owner and depositor, opens the owner and depositor USD
    // trust lines, and pays the depositor. The caller constructs env to choose
    // the amendments.
    static AssetAccounts
    setupIou(test::jtx::Env& env, IouParams const& params)
    {
        using namespace test::jtx;

        AssetAccounts a;
        if (params.ownerOnly)
        {
            env.fund(XRP(1'000'000), a.issuer, a.owner);
            if (params.clawback)
                env(fset(a.issuer, asfAllowTrustLineClawback));
            env.close();
            env(trust(a.owner, a.asset(params.ownerTrust)));
            env.close();
            env(pay(a.issuer, a.owner, a.asset(params.depositorFunds.value_or(params.ownerTrust))));
            env.close();
            return a;
        }
        env.fund(XRP(1'000'000), a.issuer, a.owner, a.depositor);
        if (params.clawback)
            env(fset(a.issuer, asfAllowTrustLineClawback));
        env.close();
        env(trust(a.depositor, a.asset(params.depositorTrust)));
        env(trust(a.owner, a.asset(params.ownerTrust)));
        env.close();
        env(pay(
            a.issuer, a.depositor, a.asset(params.depositorFunds.value_or(params.depositorTrust))));
        if (params.ownerFunds)
            env(pay(a.issuer, a.owner, a.asset(*params.ownerFunds)));
        env.close();
        return a;
    }

    static AssetAccounts
    setupIou(test::jtx::Env& env)
    {
        return setupIou(env, IouParams{});
    }

    struct MptParams
    {
        std::uint64_t maxAmt = 0;
        // Also fund and authorize the depositor, who then holds the balance
        // (otherwise the owner does).
        bool withDepositor = false;
        std::uint64_t holderFunds = 0;
    };

    // MPT counterpart of setupIou; asset is the created issuance.
    static AssetAccounts
    setupMpt(test::jtx::Env& env, MptParams const& params)
    {
        using namespace test::jtx;

        AssetAccounts a;
        if (params.withDepositor)
        {
            env.fund(XRP(1'000'000), a.issuer, a.owner, a.depositor);
        }
        else
        {
            env.fund(XRP(1'000'000), a.issuer, a.owner);
        }
        env.close();

        MPTTester mpt{env, a.issuer, kMptInitNoFund};
        mpt.create({.maxAmt = params.maxAmt, .flags = tfMPTCanTransfer});
        a.asset = mpt.issuanceID();
        mpt.authorize({.account = a.owner});
        if (params.withDepositor)
            mpt.authorize({.account = a.depositor});
        env(pay(
            a.issuer, params.withDepositor ? a.depositor : a.owner, a.asset(params.holderFunds)));
        env.close();
        return a;
    }

    // A vault's AssetsAvailable, AssetsDeployed, stored AssetsTotal and the
    // pseudo-account's balance of the asset at one point in time.
    struct VaultSnapshot
    {
        Number available{0};
        Number assetsDeployed{0};
        Number total{0};
        Number vaultBalance{0};
    };

    VaultSnapshot
    snapshotVault(test::jtx::Env& env, Keylet const& vaultKeylet, PrettyAsset const& asset)
    {
        auto const sle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return {};
        test::jtx::Account const vaultAccount("vault", sle->at(sfAccount));
        env.memoize(vaultAccount);
        return {
            .available = sle->at(sfAssetsAvailable),
            .assetsDeployed = sle->at(sfAssetsDeployed),
            .total = sle->at(sfAssetsTotal),
            .vaultBalance = env.balance(vaultAccount, asset).value()};
    }

    static Number
    largestPowerOfTenAtMost(Number const& x)
    {
        Number p{1};
        while (p * 10 <= x)
            p = p * 10;
        return p;
    }

    static Number
    smallestPowerOfTenAbove(Number const& x)
    {
        Number p{1};
        while (p <= x)
            p = p * 10;
        return p;
    }

    void
    expectCoarsened(test::jtx::Env const& env, Keylet const& vaultKeylet)
    {
        auto const sle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(getVaultScale(sle) > getVaultBaseScale(sle));
    }

    // Asserts AssetsAvailable is at most the stored AssetsTotal and exactly
    // representable at 16 digits; returns it.
    Number
    expectAvailableAtSixteenDigits(
        test::jtx::Env const& env,
        Keylet const& vaultKeylet,
        PrettyAsset const& asset)
    {
        auto const sle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return Number{0};
        Number const available = sle->at(sfAssetsAvailable);
        BEAST_EXPECT(available <= Number(sle->at(sfAssetsTotal)));
        STAmount const availableAmount{asset, available};
        BEAST_EXPECT((STAmount{asset, Number(availableAmount)} == availableAmount));
        return available;
    }

    struct VaultExpect
    {
        std::optional<Number> available = std::nullopt;
        std::optional<Number> assetsDeployed = std::nullopt;
        // The stored (cached) AssetsTotal.
        std::optional<Number> total = std::nullopt;
    };

    // Asserts the fields set in expected, plus stored AssetsTotal == derived
    // getAssetsTotal. Returns the vault SLE (null if missing).
    SLE::const_pointer
    expectVault(test::jtx::Env const& env, Keylet const& vaultKeylet, VaultExpect const& expected)
    {
        auto const sle = env.le(vaultKeylet);
        if (!BEAST_EXPECT(sle))
            return sle;
        if (expected.available)
            BEAST_EXPECT(sle->at(sfAssetsAvailable) == *expected.available);
        if (expected.assetsDeployed)
            BEAST_EXPECT(sle->at(sfAssetsDeployed) == *expected.assetsDeployed);
        if (expected.total)
            BEAST_EXPECT(sle->at(sfAssetsTotal) == *expected.total);
        BEAST_EXPECT(sle->at(sfAssetsTotal) == getAssetsTotal(sle));
        return sle;
    }

    // Asserts AssetsDeployed is zero and the stored AssetsTotal, derived
    // getAssetsTotal and AssetsAvailable all equal expectedAvailable.
    void
    checkFixedPrecisionSync(
        test::jtx::Env const& env,
        Keylet const& vaultKeylet,
        Number const& expectedAvailable)
    {
        expectVault(
            env,
            vaultKeylet,
            {.available = expectedAvailable,
             .assetsDeployed = Number{0},
             .total = expectedAvailable});
    }
};

}  // namespace xrpl
