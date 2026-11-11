#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace xrpl {

/**
 * Share<->asset rounding in VaultDeposit, VaultWithdraw and VaultClawback for
 * vaults that hold an integral asset (MPT here; XRP takes the same code path).
 *
 * Pre-fixCleanup3_6_0 the conversions round to nearest, so a depositor can be
 * charged less than the fair value of the shares they receive, and a
 * withdrawer, or a clawing-back issuer, can recover more than the fair value of
 * the shares burned. Either way the other shareholders cover the difference. Post-fixCleanup3_6_0
 * the conversions are exact and round in the vault's favor.
 *
 * Every scenario runs on a CashBasis vault (LendingProtocolV1_2 off) and a
 * FixedPrecision vault (LendingProtocolV1_2 on), each with and without the
 * amendment.
 */
class VaultRounding_test : public VaultTestBase
{
    struct Fixture
    {
        test::jtx::Account issuer{"issuer"};
        test::jtx::Account owner{"owner"};
        test::jtx::Account alice{"alice"};
        test::jtx::Account bob{"bob"};
        MPTID assetID;
        MPTID shareID;
        Keylet keylet{ltVAULT, uint256{}};
    };

    using Scenario = std::function<void(test::jtx::Env&, test::jtx::Vault&, Fixture const&)>;

    // Creates an MPT vault, funds alice and bob, and has alice make the first
    // deposit (one share per unit, since an MPT vault has Scale 0). Then runs
    // the scenario.
    void
    withVault(
        FeatureBitset features,
        std::uint64_t aliceFunds,
        std::uint64_t bobFunds,
        std::uint64_t aliceDeposit,
        Scenario const& scenario)
    {
        using namespace test::jtx;

        Env env{*this, features};
        Fixture f;
        env.fund(XRP(100'000), f.issuer, f.owner, f.alice, f.bob);
        env.close();

        MPTTester mpt{env, f.issuer, kMptInitNoFund};
        mpt.create({.flags = tfMPTCanTransfer | tfMPTCanClawback});
        f.assetID = mpt.issuanceID();
        PrettyAsset const asset{f.assetID};
        for (auto const& account : {f.owner, f.alice, f.bob})
            mpt.authorize({.account = account});
        env(pay(f.issuer, f.alice, asset(aliceFunds)));
        if (bobFunds != 0)
            env(pay(f.issuer, f.bob, asset(bobFunds)));
        env.close();

        Vault vault{env};
        auto [tx, keylet] = vault.create({.owner = f.owner, .asset = asset});
        env(tx);
        env.close();
        f.keylet = keylet;
        f.shareID = env.le(keylet)->at(sfShareMPTID);

        env(vault.deposit({.depositor = f.alice, .id = keylet.key, .amount = asset(aliceDeposit)}));
        env.close();
        BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == aliceDeposit);

        scenario(env, vault, f);
    }

    static std::uint64_t
    mptBalance(test::jtx::Env& env, MPTID const& id, test::jtx::Account const& account)
    {
        auto const sle = env.le(keylet::mptoken(id, account.id()));
        return sle ? static_cast<std::uint64_t>(sle->at(sfMPTAmount)) : 0;
    }

    static Number
    vaultTotal(test::jtx::Env& env, Keylet const& keylet)
    {
        return getAssetsTotal(env.le(keylet));
    }

    // Adds `delta` units of the asset to the vault (or removes them) without
    // minting or burning shares, the way loan interest or a realized loss
    // moves the share price. This sets up a non-unit share price without a
    // loan. The change is made to the open ledger only, so the scenario must
    // submit and check its transactions before the next close.
    void
    shiftVaultAssets(test::jtx::Env& env, Fixture const& f, std::int64_t delta)
    {
        env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
            Sandbox sb(&view, TapNone);
            auto const vault = sb.peek(f.keylet);
            if (!BEAST_EXPECT(vault))
                return false;
            auto const token = sb.peek(keylet::mptoken(f.assetID, vault->at(sfAccount)));
            auto const issuance = sb.peek(keylet::mptokenIssuance(f.assetID));
            if (!BEAST_EXPECT(token && issuance))
                return false;

            auto const shift = [delta](std::uint64_t value) {
                return static_cast<std::uint64_t>(static_cast<std::int64_t>(value) + delta);
            };
            token->at(sfMPTAmount) = shift(token->at(sfMPTAmount));
            issuance->at(sfOutstandingAmount) = shift(issuance->at(sfOutstandingAmount));
            BEAST_EXPECT(isTesSuccess(
                adjustVaultCash(vault, STAmount{MPTIssue{f.assetID}, Number{delta}}, j)));

            sb.update(vault);
            sb.update(token);
            sb.update(issuance);
            sb.apply(view);
            return true;
        });
    }

    // Runs `body` once per vault version, with and without the amendment.
    void
    forEachVersion(std::function<void(FeatureBitset, bool, std::string const&)> const& body)
    {
        for (auto const& [base, name] :
             {std::pair{all_ - featureLendingProtocolV1_2, std::string{"CashBasis"}},
              std::pair{all_, std::string{"FixedPrecision"}}})
        {
            body(base - fixCleanup3_6_0, false, name + ", pre-fixCleanup3_6_0");
            body(base, true, name + ", post-fixCleanup3_6_0");
        }
    }

    // Price 100/3. A deposit of 34 mints floor(3 * 34 / 100) = 1 share, worth
    // 33.33. Round-to-nearest charges 33, so bob gets a share worth more than
    // bob paid and alice's shares lose value. Post-amendment bob pays 34.
    void
    testDepositChargeFavorsVault()
    {
        using namespace test::jtx;

        forEachVersion([&](FeatureBitset features, bool fixed, std::string const& name) {
            testcase("Deposit charge rounds up: " + name);
            withVault(features, 3, 1'000, 3, [&](Env& env, Vault& vault, Fixture const& f) {
                shiftVaultAssets(env, f, 97);
                BEAST_EXPECT(vaultTotal(env, f.keylet) == 100);

                env(vault.deposit(
                    {.depositor = f.bob,
                     .id = f.keylet.key,
                     .amount = PrettyAsset{f.assetID}(34)}));

                std::uint64_t const charged = fixed ? 34 : 33;
                BEAST_EXPECT(mptBalance(env, f.shareID, f.bob) == 1);
                BEAST_EXPECT(mptBalance(env, f.assetID, f.bob) == 1'000 - charged);
                BEAST_EXPECT(vaultTotal(env, f.keylet) == 100 + charged);
                // Post-amendment the 4 shares are backed by at least 4 * 100 / 3.
                BEAST_EXPECT((vaultTotal(env, f.keylet) * 3 >= 100 * 4) == fixed);
            });
        });
    }

    // Price 100/3. Burning 2 shares is worth 66.67. Round-to-nearest pays 67,
    // leaving the last share backed by 33 instead of 33.33. Post-amendment the
    // payout is 66. Both ways of asking give the same result: by share count,
    // and by asset amount (67 assets truncates to 2 shares).
    void
    testWithdrawPayoutFavorsVault()
    {
        using namespace test::jtx;

        forEachVersion([&](FeatureBitset features, bool fixed, std::string const& name) {
            for (bool const byShares : {true, false})
            {
                testcase(
                    std::string{"Withdraw payout rounds down, by "} +
                    (byShares ? "shares: " : "assets: ") + name);
                withVault(features, 3, 0, 3, [&](Env& env, Vault& vault, Fixture const& f) {
                    shiftVaultAssets(env, f, 97);

                    STAmount const amount = byShares ? STAmount{MPTIssue{f.shareID}, Number{2}}
                                                     : STAmount{MPTIssue{f.assetID}, Number{67}};
                    env(vault.withdraw(
                        {.depositor = f.alice, .id = f.keylet.key, .amount = amount}));

                    std::uint64_t const paid = fixed ? 66 : 67;
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 1);
                    BEAST_EXPECT(mptBalance(env, f.assetID, f.alice) == paid);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == 100 - paid);
                });
            }
        });
    }

    // Price exactly 1 with totals of 1'000'000'000'000'007. A deposit of
    // 123'456'789 mints 123'456'789 shares and must charge exactly that. The
    // product AssetsTotal * shares needs 24 digits, more than Number's 19, so
    // rounding each Number step up would charge 123'456'790, more than
    // offered, and fail with tecINTERNAL. The exact integer conversion
    // charges the offer. The full withdrawal of those shares then pays it
    // back exactly.
    void
    testLargeTotalsDepositAndWithdraw()
    {
        using namespace test::jtx;

        std::uint64_t const total = 1'000'000'000'000'007;
        std::uint64_t const offered = 123'456'789;

        forEachVersion([&](FeatureBitset features, bool, std::string const& name) {
            testcase("Large totals convert exactly: " + name);
            withVault(
                features, total, offered, total, [&](Env& env, Vault& vault, Fixture const& f) {
                    PrettyAsset const asset{f.assetID};
                    env(vault.deposit(
                            {.depositor = f.bob, .id = f.keylet.key, .amount = asset(offered)}),
                        Ter(tesSUCCESS));
                    env.close();
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.bob) == offered);
                    BEAST_EXPECT(mptBalance(env, f.assetID, f.bob) == 0);
                    BEAST_EXPECT(
                        vaultTotal(env, f.keylet) ==
                        Number{static_cast<std::int64_t>(total + offered)});

                    env(vault.withdraw(
                            {.depositor = f.bob,
                             .id = f.keylet.key,
                             .amount =
                                 STAmount{
                                     MPTIssue{f.shareID},
                                     Number{static_cast<std::int64_t>(offered)}}}),
                        Ter(tesSUCCESS));
                    env.close();
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.bob) == 0);
                    BEAST_EXPECT(mptBalance(env, f.assetID, f.bob) == offered);
                    BEAST_EXPECT(
                        vaultTotal(env, f.keylet) == Number{static_cast<std::int64_t>(total)});
                });
        });
    }

    // Price 7/30: a share is worth less than one unit. A non-final
    // withdrawal whose rounded-down payout is zero would burn shares for
    // nothing, so post-amendment it fails with tecPRECISION_LOSS on both
    // branches. Asking for 1 asset truncates to 4 shares, worth 0.93:
    // pre-amendment that pays 1 (more than the shares are worth).
    void
    testZeroPayoutRejected()
    {
        using namespace test::jtx;

        forEachVersion([&](FeatureBitset features, bool fixed, std::string const& name) {
            testcase("Zero payout rejected, by assets: " + name);
            withVault(features, 30, 0, 30, [&](Env& env, Vault& vault, Fixture const& f) {
                shiftVaultAssets(env, f, -23);
                BEAST_EXPECT(vaultTotal(env, f.keylet) == 7);

                env(vault.withdraw(
                        {.depositor = f.alice,
                         .id = f.keylet.key,
                         .amount = PrettyAsset{f.assetID}(1)}),
                    Ter(fixed ? TER{tecPRECISION_LOSS} : TER{tesSUCCESS}));
                BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == (fixed ? 30 : 26));
                BEAST_EXPECT(mptBalance(env, f.assetID, f.alice) == (fixed ? 0 : 1));
            });

            testcase("Zero payout rejected, by shares: " + name);
            withVault(features, 30, 0, 30, [&](Env& env, Vault& vault, Fixture const& f) {
                shiftVaultAssets(env, f, -23);

                // fixCleanup3_4_0 already rejects this one on the fixed-shares branch.
                env(vault.withdraw(
                        {.depositor = f.alice,
                         .id = f.keylet.key,
                         .amount = STAmount{MPTIssue{f.shareID}, Number{1}}}),
                    Ter(tecPRECISION_LOSS));
                BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 30);

                // The final withdrawal is exempt: it pays out what is left.
                env(vault.withdraw(
                        {.depositor = f.alice,
                         .id = f.keylet.key,
                         .amount = STAmount{MPTIssue{f.shareID}, Number{30}}}),
                    Ter(tesSUCCESS));
                BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 0);
                BEAST_EXPECT(mptBalance(env, f.assetID, f.alice) == 7);
            });
        });

        // The new guard does not depend on fixCleanup3_4_0.
        testcase("Zero payout rejected without fixCleanup3_4_0");
        withVault(
            all_ - featureLendingProtocolV1_2 - fixCleanup3_4_0,
            30,
            0,
            30,
            [&](Env& env, Vault& vault, Fixture const& f) {
                shiftVaultAssets(env, f, -23);
                env(vault.withdraw(
                        {.depositor = f.alice,
                         .id = f.keylet.key,
                         .amount = STAmount{MPTIssue{f.shareID}, Number{1}}}),
                    Ter(tecPRECISION_LOSS));
                BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 30);
            });
    }

    // Without fixCleanup3_4_0 a withdrawal by asset amount rounds the share
    // count to nearest: at price 100/3, asking for 50 assets burns
    // round(1.5) = 2 shares and pays 67, more than requested. fixCleanup3_6_0
    // truncates the share count on its own: 1 share, paying 33.
    void
    testWithdrawSharesTruncatedWithoutFix340()
    {
        using namespace test::jtx;

        FeatureBitset const base = all_ - featureLendingProtocolV1_2 - fixCleanup3_4_0;
        for (bool const fixed : {false, true})
        {
            testcase(
                std::string{"Withdraw share count truncated without fixCleanup3_4_0, "} +
                (fixed ? "post" : "pre") + "-fixCleanup3_6_0");
            withVault(
                fixed ? base : base - fixCleanup3_6_0,
                3,
                0,
                3,
                [&](Env& env, Vault& vault, Fixture const& f) {
                    shiftVaultAssets(env, f, 97);
                    env(vault.withdraw(
                        {.depositor = f.alice,
                         .id = f.keylet.key,
                         .amount = PrettyAsset{f.assetID}(50)}));
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == (fixed ? 2 : 1));
                    BEAST_EXPECT(mptBalance(env, f.assetID, f.alice) == (fixed ? 33 : 67));
                });
        }
    }

    // Price 100/3 with alice holding 2 shares and bob 1. Clawing back from
    // alice burns 2 shares worth 66.67, whether the issuer names all of her
    // shares (no amount) or 67 assets (which truncates to 2 shares).
    // Round-to-nearest recovers 67 and leaves bob's share backed by 33.
    // Post-amendment the recovery is 66 and bob's share keeps 34.
    void
    testClawbackRecoveryFavorsVault()
    {
        using namespace test::jtx;

        forEachVersion([&](FeatureBitset features, bool fixed, std::string const& name) {
            for (bool const allShares : {true, false})
            {
                testcase(
                    std::string{"Clawback recovery rounds down, "} +
                    (allShares ? "all shares: " : "by amount: ") + name);
                withVault(features, 2, 1, 2, [&](Env& env, Vault& vault, Fixture const& f) {
                    PrettyAsset const asset{f.assetID};
                    env(vault.deposit(
                        {.depositor = f.bob, .id = f.keylet.key, .amount = asset(1)}));
                    env.close();
                    shiftVaultAssets(env, f, 97);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == 100);

                    std::optional<STAmount> amount;
                    if (!allShares)
                        amount = asset(67);
                    env(vault.clawback(
                            {.issuer = f.issuer,
                             .id = f.keylet.key,
                             .holder = f.alice,
                             .amount = amount}),
                        Ter(tesSUCCESS));

                    std::uint64_t const recovered = fixed ? 66 : 67;
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 0);
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.bob) == 1);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == 100 - recovered);
                    // Post-amendment bob's share is still backed by at least 100 / 3.
                    BEAST_EXPECT((vaultTotal(env, f.keylet) * 3 >= 100) == fixed);
                });
            }
        });
    }

    // Price 7/30 with alice holding 26 shares and bob 4. Clawing back all of
    // bob's shares, or 1 asset from alice (which truncates to 4 shares), is
    // worth 0.93. Pre-amendment that rounds to 1. Post-amendment it rounds
    // down to 0, and burning shares without debiting the vault would break
    // the clawback invariant, so the transaction fails with tecPRECISION_LOSS.
    void
    testClawbackZeroRecoveryRejected()
    {
        using namespace test::jtx;

        forEachVersion([&](FeatureBitset features, bool fixed, std::string const& name) {
            for (bool const allShares : {true, false})
            {
                testcase(
                    std::string{"Clawback zero recovery rejected, "} +
                    (allShares ? "all shares: " : "by amount: ") + name);
                withVault(features, 26, 4, 26, [&](Env& env, Vault& vault, Fixture const& f) {
                    PrettyAsset const asset{f.assetID};
                    env(vault.deposit(
                        {.depositor = f.bob, .id = f.keylet.key, .amount = asset(4)}));
                    env.close();
                    shiftVaultAssets(env, f, -23);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == 7);

                    auto const& holder = allShares ? f.bob : f.alice;
                    std::uint64_t const sharesBefore = allShares ? 4 : 26;
                    std::optional<STAmount> amount;
                    if (!allShares)
                        amount = asset(1);
                    env(vault.clawback(
                            {.issuer = f.issuer,
                             .id = f.keylet.key,
                             .holder = holder,
                             .amount = amount}),
                        Ter(fixed ? TER{tecPRECISION_LOSS} : TER{tesSUCCESS}));

                    BEAST_EXPECT(
                        mptBalance(env, f.shareID, holder) ==
                        (fixed ? sharesBefore : sharesBefore - 4));
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == (fixed ? 7 : 6));
                });
            }
        });
    }

    // Without fixCleanup3_4_0 a clawback by amount rounds the share count to
    // nearest: at price 100/3, clawing back 50 burns round(1.5) = 2 shares
    // and recovers 67, more than requested. fixCleanup3_6_0 truncates the
    // share count on its own: 1 share, recovering 33.
    void
    testClawbackSharesTruncatedWithoutFix340()
    {
        using namespace test::jtx;

        FeatureBitset const base = all_ - featureLendingProtocolV1_2 - fixCleanup3_4_0;
        for (bool const fixed : {false, true})
        {
            testcase(
                std::string{"Clawback share count truncated without fixCleanup3_4_0, "} +
                (fixed ? "post" : "pre") + "-fixCleanup3_6_0");
            withVault(
                fixed ? base : base - fixCleanup3_6_0,
                3,
                0,
                3,
                [&](Env& env, Vault& vault, Fixture const& f) {
                    shiftVaultAssets(env, f, 97);
                    env(vault.clawback(
                        {.issuer = f.issuer,
                         .id = f.keylet.key,
                         .holder = f.alice,
                         .amount = PrettyAsset{f.assetID}(50)}));
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == (fixed ? 2 : 1));
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == (fixed ? 67 : 33));
                });
        }
    }

    // Without fixCleanup3_1_3 a clawback of all shares takes a separate,
    // unclamped path. fixCleanup3_6_0 must still round its recovery down: at
    // price 100/3, alice's 2 shares are worth 66.67, so the issuer recovers
    // 66 rather than 67 and bob's share stays backed by at least 100 / 3.
    void
    testClawbackAllSharesRoundsDownWithoutFix313()
    {
        using namespace test::jtx;

        FeatureBitset const base = all_ - featureLendingProtocolV1_2 - fixCleanup3_1_3;
        for (bool const fixed : {false, true})
        {
            testcase(
                std::string{"Clawback all shares rounds down without fixCleanup3_1_3, "} +
                (fixed ? "post" : "pre") + "-fixCleanup3_6_0");
            withVault(
                fixed ? base : base - fixCleanup3_6_0,
                2,
                1,
                2,
                [&](Env& env, Vault& vault, Fixture const& f) {
                    PrettyAsset const asset{f.assetID};
                    env(vault.deposit(
                        {.depositor = f.bob, .id = f.keylet.key, .amount = asset(1)}));
                    env.close();
                    shiftVaultAssets(env, f, 97);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == 100);

                    env(vault.clawback({.issuer = f.issuer, .id = f.keylet.key, .holder = f.alice}),
                        Ter(tesSUCCESS));

                    BEAST_EXPECT(mptBalance(env, f.shareID, f.alice) == 0);
                    BEAST_EXPECT(mptBalance(env, f.shareID, f.bob) == 1);
                    BEAST_EXPECT(vaultTotal(env, f.keylet) == (fixed ? 34 : 33));
                });
        }
    }

public:
    void
    run() override
    {
        testDepositChargeFavorsVault();
        testWithdrawPayoutFavorsVault();
        testLargeTotalsDepositAndWithdraw();
        testZeroPayoutRejected();
        testWithdrawSharesTruncatedWithoutFix340();
        testClawbackRecoveryFavorsVault();
        testClawbackZeroRecoveryRejected();
        testClawbackSharesTruncatedWithoutFix340();
        testClawbackAllSharesRoundsDownWithoutFix313();
    }
};

BEAST_DEFINE_TESTSUITE(VaultRounding, app, xrpl);

}  // namespace xrpl
