#include <test/app/vault/VaultManagerTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>
#include <test/jtx/vault_manager.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>

namespace xrpl {

class VaultManagerCover_test : public VaultManagerTestBase
{
    void
    testDeposit()
    {
        testcase("deposit");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        auto const before = holding(env, f.manager, f.usd);

        env(cover(f.manager, f.vaultID(), f.usd(1'000), tfCoverDeposit));
        env.close();
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{1'000});
        BEAST_EXPECT(holding(env, coverAccount(env, f), f.usd) == f.usd(1'000).value());
        BEAST_EXPECT(holding(env, f.manager, f.usd) == before - f.usd(1'000).value());

        env(cover(f.alice, f.vaultID(), f.usd(10), tfCoverDeposit), Ter{tecNO_PERMISSION});
        env(cover(f.manager, f.vaultID(), f.eq(10), tfCoverDeposit), Ter{tecWRONG_ASSET});
        env(cover(f.manager, f.vaultID(), f.usd(1'000'000), tfCoverDeposit),
            Ter{tecINSUFFICIENT_FUNDS});
        env(cover(f.manager, uint256{7}, f.usd(10), tfCoverDeposit), Ter{tecNO_ENTRY});

        env(trust(f.gw, f.manager["USD"](0), tfSetFreeze));
        env.close();
        env(cover(f.manager, f.vaultID(), f.usd(10), tfCoverDeposit), Ter{tecFROZEN});
    }

    void
    testWithdraw()
    {
        testcase("withdraw");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        env(cover(f.manager, f.vaultID(), f.usd(1'000), tfCoverDeposit));
        env.close();
        auto const before = holding(env, f.manager, f.usd);

        env(cover(f.manager, f.vaultID(), f.usd(400), tfCoverWithdraw));
        env.close();
        BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{600});
        BEAST_EXPECT(holding(env, f.manager, f.usd) == before + f.usd(400).value());

        env(cover(f.manager, f.vaultID(), f.usd(700), tfCoverWithdraw), Ter{tecINSUFFICIENT_FUNDS});
        env(cover(f.alice, f.vaultID(), f.usd(10), tfCoverWithdraw), Ter{tecNO_PERMISSION});

        // Cover on deposit blocks deleting the manager entry.
        env(del(f.owner, f.vaultID()), Ter{tecHAS_OBLIGATIONS});
        env.close();
        env(cover(f.manager, f.vaultID(), f.usd(600), tfCoverWithdraw));
        env.close();
        env(del(f.owner, f.vaultID()));
        env.close();
        BEAST_EXPECT(!env.le(managerKeylet(f)));
    }

    void
    testClawback()
    {
        testcase("clawback");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        {
            Env env{*this, all_};
            auto const f = makeFund(env);
            env(cover(f.manager, f.vaultID(), f.usd(1'000), tfCoverDeposit));
            env.close();

            env(cover(f.alice, f.vaultID(), f.usd(10), tfCoverClawback), Ter{tecNO_PERMISSION});
            env(cover(f.manager, f.vaultID(), f.usd(10), tfCoverClawback), Ter{tecNO_PERMISSION});
            env(cover(f.gw, f.vaultID(), XRP(10), tfCoverClawback), Ter{temMALFORMED});

            env(cover(f.gw, f.vaultID(), f.usd(300), tfCoverClawback));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{700});
            BEAST_EXPECT(holding(env, coverAccount(env, f), f.usd) == f.usd(700).value());

            // Zero claws everything above the required minimum.
            env(cover(f.gw, f.vaultID(), f.usd(0), tfCoverClawback));
            env.close();
            BEAST_EXPECT(env.le(managerKeylet(f))->at(sfCoverAvailable) == Number{});
        }

        {
            Env env{*this, all_};
            auto const f = makeFund(env, FundArgs{.clawback = false});
            env(cover(f.manager, f.vaultID(), f.usd(1'000), tfCoverDeposit));
            env.close();
            env(cover(f.gw, f.vaultID(), f.usd(300), tfCoverClawback), Ter{tecNO_PERMISSION});
        }
    }

    void
    testClawbackMpt()
    {
        testcase("clawback of an MPT cover");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        auto const run = [&](std::uint32_t issuanceFlags, TER expected) {
            Env env{*this, all_};
            Account const gw{"gw"};
            Account const owner{"owner"};
            Account const manager{"manager"};
            Account const valuer{"valuer"};
            Account const guardian{"guardian"};
            env.fund(XRP(100'000), gw, owner, manager, valuer, guardian);
            env.close();
            MPTTester mptt{env, gw, kMptInitNoFund};
            mptt.create({.flags = issuanceFlags});
            PrettyAsset const mpt = mptt.issuanceID();
            mptt.authorize({.account = owner});
            mptt.authorize({.account = manager});
            env(pay(gw, manager, mpt(5'000)));
            env.close();

            Vault const vault{env};
            auto const [tx, keylet, sub] = vault.createClosedEnded({.owner = owner, .asset = mpt});
            env(tx);
            env.close();
            env(set(owner, keylet.key),
                kManager(manager.id()),
                kValuer(valuer.id()),
                kGuardian(guardian.id()),
                mandate({mandateEntry(gw["EQT"])}),
                kDeployedMaximum(Number{100'000}),
                kMarkMoveLimit(kMoveLimit),
                kMarkHaltPeriod(kHaltPeriod),
                kMarkStaleAfter(kStaleAfter),
                kTimelock(kTimelockSeconds));
            env.close();
            env(cover(manager, keylet.key, mpt(1'000), tfCoverDeposit));
            env.close();

            env(cover(gw, keylet.key, mpt(300), tfCoverClawback), Ter{expected});
            env.close();
            auto const sle = env.le(keylet::vaultManager(keylet.key));
            Number const left = isTesSuccess(expected) ? Number{700} : Number{1'000};
            BEAST_EXPECT(sle->at(sfCoverAvailable) == left);
            BEAST_EXPECT(Number{holding(env, sle->at(sfAccount), mpt)} == left);
        };

        run(tfMPTCanClawback | tfMPTCanTransfer, tesSUCCESS);
        run(tfMPTCanTransfer, tecNO_PERMISSION);
    }

    void
    testFlags()
    {
        testcase("flags");
        using namespace test::jtx;
        using namespace test::jtx::vault_manager;

        Env env{*this, all_};
        auto const f = makeFund(env);
        env(cover(f.manager, f.vaultID(), f.usd(10), 0), Ter{temINVALID_FLAG});
        env(cover(f.manager, f.vaultID(), f.usd(10), tfCoverDeposit | tfCoverWithdraw),
            Ter{temINVALID_FLAG});
        env(cover(f.manager, f.vaultID(), f.usd(10), 0x00080000), Ter{temINVALID_FLAG});
        env(cover(f.manager, f.vaultID(), f.usd(0), tfCoverDeposit), Ter{temBAD_AMOUNT});
        env(cover(f.manager, f.vaultID(), f.usd(-1), tfCoverWithdraw), Ter{temBAD_AMOUNT});
    }

public:
    void
    run() override
    {
        testDeposit();
        testWithdraw();
        testClawback();
        testClawbackMpt();
        testFlags();
    }
};

BEAST_DEFINE_TESTSUITE(VaultManagerCover, app, xrpl);

}  // namespace xrpl
