#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

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

public:
    void
    run() override
    {
        testOpen();
        testIncrease();
        testNavIncludesPositions();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTrade, app, xrpl);

}  // namespace xrpl
