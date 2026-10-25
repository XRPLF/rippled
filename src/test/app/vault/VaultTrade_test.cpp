#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/offer.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>

namespace xrpl {

class VaultTrade_test : public VaultManagerTestBase
{
    static AccountID
    vaultAccount(test::jtx::Env& env, Fund const& f)
    {
        return env.le(f.vault)->at(sfAccount);
    }

    static std::uint64_t
    shares(test::jtx::Env& env, Fund const& f, test::jtx::Account const& holder)
    {
        auto const issuance = env.le(f.vault)->at(sfShareMPTID);
        auto const token = env.le(keylet::mptoken(issuance, holder.id()));
        return token ? token->at(sfMPTAmount) : 0;
    }

    static std::uint32_t
    lastClose(test::jtx::Env& env)
    {
        return env.closed()->header().parentCloseTime.time_since_epoch().count();
    }

    void
    testOpen()
    {
        testcase("open");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        auto const pseudo = vaultAccount(env, f);
        auto const ownerCount = env.le(keylet::account(f.owner))->at(sfOwnerCount);
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{10'000});

        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();

        auto const pos = env.le(positionKeylet(f, f.eq));
        if (!BEAST_EXPECT(pos))
            return;
        BEAST_EXPECT(pos->at(sfVaultID) == f.vaultID());
        BEAST_EXPECT(pos->at(sfAsset) == Asset{f.eq.raw()});
        BEAST_EXPECT(pos->at(sfMarkPrice) == Number{10});
        BEAST_EXPECT(pos->at(sfLastMarkTime) == lastClose(env));
        BEAST_EXPECT(pos->at(sfCostBasis) == Number{1'000});

        auto const vault = env.le(f.vault);
        BEAST_EXPECT(vault->at(sfManagedPositionCount) == 1);
        BEAST_EXPECT(vault->at(sfAssetsTotal) == Number{9'000});
        BEAST_EXPECT(vault->at(sfAssetsAvailable) == Number{9'000});
        BEAST_EXPECT(holding(env, pseudo, f.usd) == f.usd(9'000).value());
        BEAST_EXPECT(holding(env, pseudo, f.eq) == f.eq(100).value());
        BEAST_EXPECT(env.le(keylet::account(f.owner))->at(sfOwnerCount) == ownerCount + 1);

        // The pseudo-account's limit on the new line is back to zero.
        auto const line = env.le(keylet::trustLine(pseudo, f.eq.raw().get<Issue>()));
        if (!BEAST_EXPECT(line))
            return;
        auto const& limitField = pseudo < f.gw.id() ? sfLowLimit : sfHighLimit;
        BEAST_EXPECT(line->getFieldAmount(limitField) == beast::kZero);
    }

    void
    testIncrease()
    {
        testcase("increase");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        auto const markTime = env.le(positionKeylet(f, f.eq))->at(sfLastMarkTime);
        env.close(std::chrono::seconds{100});

        env(trade(f.manager, f.vaultID(), f.eq(50), f.usd(600)));
        env.close();
        auto const pos = env.le(positionKeylet(f, f.eq));
        if (!BEAST_EXPECT(pos))
            return;
        BEAST_EXPECT(pos->at(sfMarkPrice) == Number{10});
        BEAST_EXPECT(pos->at(sfLastMarkTime) == markTime);
        BEAST_EXPECT(pos->at(sfCostBasis) == Number{1'500});
        BEAST_EXPECT(env.le(f.vault)->at(sfManagedPositionCount) == 1);
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{8'500});
    }

    void
    testNavIncludesPositions()
    {
        testcase("NAV includes positions");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        // Closed-ended, subscription phase: NAV after an open at the execution
        // price is unchanged, so a deposit of a tenth of the assets mints a
        // tenth of the shares. Without the position, NAV would read 9,000.
        {
            Env env{*this, all_};
            auto const f = makeFund(env);
            auto const aliceShares = shares(env, f, f.alice);
            env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
            env.close();

            Vault const vault{env};
            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(1'000)}));
            env.close();
            BEAST_EXPECT(shares(env, f, f.bob) == aliceShares / 10);
        }

        // Rolling: the strike of the next window prices the position in.
        {
            Env env{*this, all_};
            auto const f = makeFund(env, FundArgs{.kind = VaultKind::Rolling});
            auto const aliceShares = shares(env, f, f.alice);
            env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
            env.close();

            env.close(tp{d{f.start + kInterval}});
            Vault const vault{env};
            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(1'000)}));
            env.close();
            BEAST_EXPECT(shares(env, f, f.bob) == aliceShares / 10);
            BEAST_EXPECT(
                env.le(f.vault)->at(sfStruckPrice) ==
                Number{10'000} / Number{static_cast<std::int64_t>(aliceShares)});
        }
    }

    void
    testClose()
    {
        testcase("close");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        auto const pseudo = vaultAccount(env, f);
        auto const ownerCount = env.le(keylet::account(f.owner))->at(sfOwnerCount);
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        // Withdraw the ask so a bid at 12 USD does not cross it.
        env(offerCancel(f.mm, f.askSeq));
        env(offer(f.mm, f.eq(1'000), f.usd(12'000)));
        env.close();

        // Sell half at 12: cost basis falls by half, the proceeds join both totals.
        env(trade(f.manager, f.vaultID(), f.usd(600), f.eq(51)));
        env.close();
        auto const pos = env.le(positionKeylet(f, f.eq));
        if (!BEAST_EXPECT(pos))
            return;
        BEAST_EXPECT(pos->at(sfCostBasis) == Number{500});
        BEAST_EXPECT(pos->at(sfMarkPrice) == Number{10});
        BEAST_EXPECT(holding(env, pseudo, f.eq) == f.eq(50).value());
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{9'600});
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsAvailable) == Number{9'600});

        // Sell the rest: the position, its reserve and the pseudo-account's line go.
        env(trade(f.manager, f.vaultID(), f.usd(700), f.eq(50), tfPartialPayment));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(f, f.eq)));
        BEAST_EXPECT(env.le(f.vault)->at(sfManagedPositionCount) == 0);
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{10'200});
        BEAST_EXPECT(env.le(keylet::account(f.owner))->at(sfOwnerCount) == ownerCount);
        BEAST_EXPECT(!env.le(keylet::trustLine(pseudo, f.eq.raw().get<Issue>())));
    }

    void
    testRealizedLoss()
    {
        testcase("realized loss");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        auto const setup = [&](Env& env, std::uint32_t liquidationRate, int coverDeposit) {
            auto f = makeFund(env);
            if (liquidationRate != 0)
            {
                env(set(f.owner, f.vaultID()), kCoverRateLiquidation(liquidationRate));
                env.close();
            }
            if (coverDeposit != 0)
            {
                env(cover(f.manager, f.vaultID(), f.usd(coverDeposit), tfCoverDeposit));
                env.close();
            }
            env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
            // mm buys EQT at 8 USD.
            env(offer(f.mm, f.eq(1'000), f.usd(8'000)));
            env.close();
            return f;
        };

        // Loss 200 against cost; half the cover, 500, may be applied: all 200 is.
        {
            Env env{*this, all_};
            auto const f = setup(env, 50'000, 1'000);
            auto const pseudo = vaultAccount(env, f);
            auto const coverPseudo = coverAccount(env, f);
            env(trade(f.manager, f.vaultID(), f.usd(900), f.eq(100), tfPartialPayment));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{800});
            BEAST_EXPECT(holding(env, coverPseudo, f.usd) == f.usd(800).value());
            BEAST_EXPECT(holding(env, pseudo, f.usd) == f.usd(10'000).value());
            BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{10'000});
            BEAST_EXPECT(env.le(f.vault)->at(sfAssetsAvailable) == Number{10'000});
        }

        // A tenth of the cover, 100, caps what one loss consumes.
        {
            Env env{*this, all_};
            auto const f = setup(env, 10'000, 1'000);
            env(trade(f.manager, f.vaultID(), f.usd(900), f.eq(100), tfPartialPayment));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{900});
            BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{9'900});
        }

        // No cover: nothing moves and the vault bears the loss.
        {
            Env env{*this, all_};
            auto const f = setup(env, 50'000, 0);
            env(trade(f.manager, f.vaultID(), f.usd(900), f.eq(100), tfPartialPayment));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{});
            BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{9'800});
        }

        // Above cost: no cover moves. With the ask withdrawn, bob bids 12; mm's
        // bid at 8 sits behind it.
        {
            Env env{*this, all_};
            auto const f = setup(env, 50'000, 1'000);
            env(offerCancel(f.mm, f.askSeq));
            env(offer(f.bob, f.eq(100), f.usd(1'200)));
            env.close();
            env(trade(f.manager, f.vaultID(), f.usd(1'300), f.eq(100), tfPartialPayment));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{1'000});
            BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{10'200});
        }
    }

public:
    void
    run() override
    {
        testOpen();
        testIncrease();
        testNavIncludesPositions();
        testClose();
        testRealizedLoss();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTrade, app, xrpl);

}  // namespace xrpl
