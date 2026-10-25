#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/offer.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>

namespace xrpl {

class VaultManagerClaim_test : public VaultManagerTestBase
{
    static constexpr std::uint32_t kSecondsInYear = 365 * 24 * 60 * 60;

    // A manager entry charging the given fees, with a mark move limit wide
    // enough that the tests' marks never halt dealing.
    static void
    createWithFees(
        test::jtx::Env& env,
        Fund const& f,
        std::uint16_t managementRate,
        std::uint32_t performanceRate)
    {
        using namespace test::jtx::vault_manager;
        env(set(f.owner, f.vaultID()),
            kManager(f.manager.id()),
            kValuer(f.valuer.id()),
            kGuardian(f.guardian.id()),
            mandate({mandateEntry(f.eq)}),
            kDeployedMaximum(Number{100'000}),
            kManagementFeeRate(managementRate),
            kPerformanceFeeRate(performanceRate),
            kMarkMoveLimit(100'000),
            kMarkHaltPeriod(kHaltPeriod),
            kMarkStaleAfter(kStaleAfter),
            kTimelock(kTimelockSeconds));
        env.close();
    }

    static std::int64_t
    sharesOutstanding(test::jtx::Env& env, Fund const& f)
    {
        auto const issuance = env.le(keylet::mptokenIssuance(env.le(f.vault)->at(sfShareMPTID)));
        return static_cast<std::int64_t>(issuance->at(sfOutstandingAmount));
    }

    static std::uint32_t
    parent(test::jtx::Env& env)
    {
        return env.current()->parentCloseTime().time_since_epoch().count();
    }

    void
    testManagementFee()
    {
        testcase("management fee");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false});
        createWithFees(env, f, 10'000, 0);
        std::uint32_t const since = env.le(managerKeylet(f))->at(sfLastFeeTime);
        env.close(std::chrono::seconds{kSecondsInYear / 10});

        auto const before = holding(env, f.manager, f.usd);
        auto const now = parent(env);
        env(claim(f.manager, f.vaultID()));
        env.close();

        // A tenth of a year at 10 percent on 10,000.
        Number const expected =
            Number{10'000} * Number{1, -1} * Number{now - since} / Number{kSecondsInYear};
        Number const paid = holding(env, f.manager, f.usd) - before;
        Number const tolerance{1, -6};
        BEAST_EXPECT(paid <= expected && paid > expected - tolerance);
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsTotal) == Number{10'000} - paid);
        BEAST_EXPECT(env.le(f.vault)->at(sfAssetsAvailable) == Number{10'000} - paid);
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfLastFeeTime) == now);
    }

    void
    testPerformanceFee()
    {
        testcase("performance fee");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false});
        createWithFees(env, f, 0, 20'000);
        auto const shares = sharesOutstanding(env, f);
        BEAST_EXPECT(
            env.le(managerKeylet(f))->at(sfHighWaterMark) == Number{10'000} / Number{shares});

        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        // EQT doubles: NAV goes from 10,000 to 11,000.
        env(mark(f.valuer, f.vaultID(), f.eq, Number{20}));
        env.close();

        auto const before = holding(env, f.manager, f.usd);
        env(claim(f.manager, f.vaultID()));
        env.close();
        // 20 percent of the 1,000 gain; the mark is the price after the fee.
        Number const paid = holding(env, f.manager, f.usd) - before;
        BEAST_EXPECT(paid == Number{200});
        BEAST_EXPECT(
            env.le(managerKeylet(f))->at(sfHighWaterMark) == Number{10'800} / Number{shares});
    }

    void
    testBelowMark()
    {
        testcase("no fee below the mark");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false});
        createWithFees(env, f, 0, 20'000);
        Number const mark0 = env.le(managerKeylet(f))->at(sfHighWaterMark);
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        env(mark(f.valuer, f.vaultID(), f.eq, Number{8}));
        env.close();

        auto const before = holding(env, f.manager, f.usd);
        auto const now = parent(env);
        env(claim(f.manager, f.vaultID()));
        env.close();
        BEAST_EXPECT(holding(env, f.manager, f.usd) == before);
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfHighWaterMark) == mark0);
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfLastFeeTime) == now);
    }

    void
    testFirstClaimSetsMark()
    {
        testcase("first claim sets the mark");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false, .deposit = false});
        createWithFees(env, f, 0, 20'000);
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfHighWaterMark) == Number{});

        Vault const vault{env};
        env(vault.deposit({.depositor = f.alice, .id = f.vaultID(), .amount = f.usd(10'000)}));
        env.close();
        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        env(mark(f.valuer, f.vaultID(), f.eq, Number{20}));
        env.close();

        auto const before = holding(env, f.manager, f.usd);
        env(claim(f.manager, f.vaultID()));
        env.close();
        BEAST_EXPECT(holding(env, f.manager, f.usd) == before);
        BEAST_EXPECT(
            env.le(managerKeylet(f))->at(sfHighWaterMark) ==
            Number{11'000} / Number{sharesOutstanding(env, f)});
    }

    void
    testInsufficientCash()
    {
        testcase("insufficient cash");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false});
        createWithFees(env, f, 10'000, 0);
        std::uint32_t const since = env.le(managerKeylet(f))->at(sfLastFeeTime);
        // Leave about 100 of cash.
        env(trade(f.manager, f.vaultID(), f.eq(990), f.usd(9'950)));
        env.close();

        env.close(std::chrono::seconds{50 * 86'400});
        env(mark(f.valuer, f.vaultID(), f.eq, Number{10}));
        env.close();
        env(claim(f.manager, f.vaultID()), Ter{tecINSUFFICIENT_FUNDS});
        env.close();
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfLastFeeTime) == since);

        // Raise cash; the next claim pays for the whole interval.
        env(offerCancel(f.mm, f.askSeq));
        env(offer(f.mm, f.eq(1'000), f.usd(10'000)));
        env.close();
        env(trade(f.manager, f.vaultID(), f.usd(5'000), f.eq(500), tfPartialPayment));
        env.close();
        auto const before = holding(env, f.manager, f.usd);
        auto const now = parent(env);
        env(claim(f.manager, f.vaultID()));
        env.close();
        Number const paid = holding(env, f.manager, f.usd) - before;
        Number const nav = Number{10'000};
        Number const expected = nav * Number{1, -1} * Number{now - since} / Number{kSecondsInYear};
        Number const tolerance{1, -2};
        BEAST_EXPECT(paid <= expected && paid > expected - tolerance);
    }

    void
    testErrors()
    {
        testcase("claim errors");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env, FundArgs{.createManager = false});
        createWithFees(env, f, 10'000, 0);
        env(claim(f.alice, f.vaultID()), Ter{tecNO_PERMISSION});
        env(claim(f.valuer, f.vaultID()), Ter{tecNO_PERMISSION});
        env(claim(f.manager, uint256{7}), Ter{tecNO_ENTRY});

        env(trade(f.manager, f.vaultID(), f.eq(100), f.usd(1'100)));
        env.close();
        auto const marked = env.le(positionKeylet(f, f.eq))->at(sfLastMarkTime);
        env.close(tp{d{marked + kStaleAfter + 10}});
        env(claim(f.manager, f.vaultID()), Ter{tecSTALE_MARK});
        env(mark(f.valuer, f.vaultID(), f.eq, Number{10}));
        env.close();
        env(set(f.guardian, f.vaultID(), tfHalt));
        env.close();
        env(claim(f.manager, f.vaultID()), Ter{tecDEALING_HALTED});
    }

public:
    void
    run() override
    {
        testManagementFee();
        testPerformanceFee();
        testBelowMark();
        testFirstClaimSetsMark();
        testInsufficientCash();
        testErrors();
    }
};

BEAST_DEFINE_TESTSUITE(VaultManagerClaim, app, xrpl);

}  // namespace xrpl
