#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>

namespace xrpl {

class VaultMark_test : public VaultManagerTestBase
{
    static std::uint32_t
    parent(test::jtx::Env& env)
    {
        return env.current()->parentCloseTime().time_since_epoch().count();
    }

    // A fund with a 100 EQT position opened at 10.
    Fund
    withPosition(test::jtx::Env& env, FundArgs const& args)
    {
        using namespace test::jtx::vault_manager;
        auto f = makeFund(env, args);
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        return f;
    }

    Fund
    withPosition(test::jtx::Env& env)
    {
        return withPosition(env, FundArgs{});
    }

    void
    testWithinLimit()
    {
        testcase("mark within limit");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = withPosition(env);
        auto const now = parent(env);
        Number const price{105, -1};
        env(mark(f.valuer, f.vaultID(), f.eq, price));
        env.close();
        auto const pos = env.le(positionKeylet(f, f.eq));
        BEAST_EXPECT(pos->at(sfMarkPrice) == price);
        BEAST_EXPECT(pos->at(sfLastMarkTime) == now);
        BEAST_EXPECT(env.le(f.vault)->at(sfMarkHaltUntil) == 0);
    }

    void
    testBeyondLimit()
    {
        testcase("mark beyond limit");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = withPosition(env);
        auto const now = parent(env);
        env(mark(f.valuer, f.vaultID(), f.eq, Number{12}));
        env.close();
        // The mark is written all the same; dealing halts for the period.
        BEAST_EXPECT(env.le(positionKeylet(f, f.eq))->at(sfMarkPrice) == Number{12});
        BEAST_EXPECT(env.le(f.vault)->at(sfMarkHaltUntil) == now + kHaltPeriod);
    }

    void
    testRepeatedMarks()
    {
        testcase("repeated marks extend the halt");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = withPosition(env);
        env(mark(f.valuer, f.vaultID(), f.eq, Number{12}));
        env.close();
        auto const first = env.le(f.vault)->at(sfMarkHaltUntil);
        env.close(std::chrono::seconds{600});
        auto const now = parent(env);
        env(mark(f.valuer, f.vaultID(), f.eq, Number{15}));
        env.close();
        BEAST_EXPECT(env.le(f.vault)->at(sfMarkHaltUntil) == now + kHaltPeriod);
        BEAST_EXPECT(env.le(f.vault)->at(sfMarkHaltUntil) > first);
        // A mark within the limit leaves the halt where it is.
        env(mark(f.valuer, f.vaultID(), f.eq, Number{15}));
        env.close();
        BEAST_EXPECT(env.le(f.vault)->at(sfMarkHaltUntil) == now + kHaltPeriod);
    }

    void
    testErrors()
    {
        testcase("mark errors");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = withPosition(env);
        env(mark(f.manager, f.vaultID(), f.eq, Number{11}), Ter{tecNO_PERMISSION});
        env(mark(f.owner, f.vaultID(), f.eq, Number{11}), Ter{tecNO_PERMISSION});
        env(mark(f.valuer, f.vaultID(), f.eq, Number{}), Ter{temMALFORMED});
        env(mark(f.valuer, f.vaultID(), f.eq, Number{-1}), Ter{temMALFORMED});
        env(mark(f.valuer, f.vaultID(), f.gw["BND"], Number{1}), Ter{tecNO_ENTRY});
        env(mark(f.valuer, uint256{7}, f.eq, Number{1}), Ter{tecNO_ENTRY});
    }

    void
    testStaleBlocksDealing()
    {
        testcase("stale mark blocks dealing");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        {
            Env env{*this, all_};
            auto const f = withPosition(env);
            Vault const vault{env};
            auto const markTime = env.le(positionKeylet(f, f.eq))->at(sfLastMarkTime);
            env.close(tp{d{markTime + kStaleAfter + 10}});

            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}),
                Ter{tecSTALE_MARK});
            env(vault.withdraw({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(100)}),
                Ter{tecSTALE_MARK});
            env(vault.clawback(
                    {.issuer = f.gw, .id = f.vaultID(), .holder = f.alice, .amount = f.usd(100)}),
                Ter{tecSTALE_MARK});

            // A fresh mark unblocks dealing.
            env(mark(f.valuer, f.vaultID(), f.eq, Number{10}));
            env.close();
            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}));
            env(vault.withdraw({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(100)}));
            env(vault.clawback(
                {.issuer = f.gw, .id = f.vaultID(), .holder = f.alice, .amount = f.usd(100)}));
            env.close();
        }

        // Rolling: a window does not strike against a stale mark.
        {
            Env env{*this, all_};
            auto const f = withPosition(env, FundArgs{.kind = VaultKind::Rolling});
            Vault const vault{env};
            auto const struck = env.le(f.vault)->at(sfStruckPrice);
            env.close(tp{d{f.start + 2 * kInterval}});
            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}),
                Ter{tecSTALE_MARK});
            env.close();
            BEAST_EXPECT(env.le(f.vault)->at(sfStruckPrice) == struck);
            env(mark(f.valuer, f.vaultID(), f.eq, Number{10}));
            env.close();
            env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}));
            env.close();
            BEAST_EXPECT(env.le(f.vault)->at(sfStruckPrice) != struck);
        }
    }

    void
    testHaltBlocksDealing()
    {
        testcase("halt blocks dealing");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = withPosition(env);
        Vault const vault{env};
        env(set(f.guardian, f.vaultID(), tfHalt));
        env.close();
        env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}),
            Ter{tecDEALING_HALTED});
        env(vault.withdraw({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(100)}),
            Ter{tecDEALING_HALTED});
        env(vault.clawback(
                {.issuer = f.gw, .id = f.vaultID(), .holder = f.alice, .amount = f.usd(100)}),
            Ter{tecDEALING_HALTED});
        env.close(tp{d{env.le(f.vault)->at(sfMarkHaltUntil)}});
        env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}));
    }

    void
    testNoPositionsNeverStale()
    {
        testcase("no positions, never stale");
        using namespace test::jtx;

        Env env{*this, all_};
        auto const f = makeFund(env);
        Vault const vault{env};
        env.close(std::chrono::seconds{5 * 86'400});
        env(vault.deposit({.depositor = f.bob, .id = f.vaultID(), .amount = f.usd(100)}));
        env(vault.withdraw({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(100)}));
        env.close();
    }

public:
    void
    run() override
    {
        testWithinLimit();
        testBeyondLimit();
        testRepeatedMarks();
        testErrors();
        testStaleBlocksDealing();
        testHaltBlocksDealing();
        testNoPositionsNeverStale();
    }
};

BEAST_DEFINE_TESTSUITE(VaultMark, app, xrpl);

}  // namespace xrpl
