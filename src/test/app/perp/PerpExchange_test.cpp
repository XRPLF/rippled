#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>

namespace xrpl {

class PerpExchange_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    /**
     * Checks the exchange's pseudo-account: designator, flags, and the holding of asset.
     */
    void
    expectPseudoAccount(Env& env, SLE const& exchange, Asset const& asset)
    {
        auto const pseudoID = exchange[sfAccount];
        auto const pseudo = env.le(keylet::account(pseudoID));
        if (!BEAST_EXPECT(pseudo))
            return;
        BEAST_EXPECT((*pseudo)[~sfPerpExchangeID] == exchange.key());
        BEAST_EXPECT(isPseudoAccount(pseudo));
        BEAST_EXPECT(pseudo->isFlag(lsfDisableMaster));
        BEAST_EXPECT(pseudo->isFlag(lsfDefaultRipple));
        BEAST_EXPECT(pseudo->isFlag(lsfDepositAuth));
        asset.visit(
            [&](Issue const& issue) {
                if (!issue.native())
                    BEAST_EXPECT(env.le(keylet::trustLine(pseudoID, issue)));
            },
            [&](MPTIssue const& issue) {
                BEAST_EXPECT(env.le(keylet::mptoken(issue.getMptID(), pseudoID)));
            });
    }

    void
    testCreate()
    {
        testcase("create");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);

        // XRP collateral: no scale and no holding; fees and data are stored.
        {
            auto const ownerCount = env.ownerCount(owner_);
            auto const sequence = env.seq(owner_);
            auto const keylet = Perp::exchangeKeylet(env, owner_);
            auto jv = Perp::exchangeSet(owner_, xrpIssue());
            jv[sfMakerFee] = 200;
            jv[sfTakerFee] = 500;
            jv[sfOwnerFeeShare] = 30'000;
            jv[sfVaultFeeShare] = 20'000;
            jv[sfData] = "ABCD";
            env(jv);
            env.close();

            BEAST_EXPECT(env.ownerCount(owner_) == ownerCount + 2);
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange))
                return;
            BEAST_EXPECT((*exchange)[sfOwner] == owner_.id());
            BEAST_EXPECT((*exchange)[sfSequence] == sequence);
            BEAST_EXPECT((*exchange)[sfAsset] == Asset{xrpIssue()});
            BEAST_EXPECT(!exchange->isFieldPresent(sfScale));
            BEAST_EXPECT((*exchange)[sfMakerFee] == 200);
            BEAST_EXPECT((*exchange)[sfTakerFee] == 500);
            BEAST_EXPECT((*exchange)[sfOwnerFeeShare] == 30'000);
            BEAST_EXPECT((*exchange)[sfVaultFeeShare] == 20'000);
            BEAST_EXPECT((*exchange)[sfParameterDelay] == perp::kMinParameterDelay.count());
            BEAST_EXPECT(strHex((*exchange)[sfData]) == "ABCD");
            BEAST_EXPECT(!exchange->isFieldPresent(sfVaultID));
            BEAST_EXPECT(!exchange->isFieldPresent(sfPendingParameters));
            expectPseudoAccount(env, *exchange, xrpIssue());
        }

        // IOU collateral: a trust line for the pseudo-account and the default scale of 6.
        {
            auto const ownerCount = env.ownerCount(owner_);
            auto const keylet = createExchange(env, owner_, usd_);
            BEAST_EXPECT(env.ownerCount(owner_) == ownerCount + 2);
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange))
                return;
            BEAST_EXPECT((*exchange)[sfScale] == kVaultDefaultIouScale);
            expectPseudoAccount(env, *exchange, usd_);
        }

        // An explicit IOU scale is kept.
        {
            auto const keylet = Perp::exchangeKeylet(env, owner_);
            auto jv = Perp::exchangeSet(owner_, usd_);
            jv[sfScale] = 2;
            env(jv);
            env.close();
            auto const exchange = env.le(keylet);
            BEAST_EXPECT(exchange && (*exchange)[sfScale] == 2);
        }

        // MPT collateral: an MPToken for the pseudo-account and no scale.
        {
            MPTTester mpt{env, gw_, kMptInitNoFund};
            mpt.create({.flags = tfMPTCanTransfer});
            Asset const asset = mpt;
            auto const keylet = createExchange(env, owner_, asset);
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange))
                return;
            BEAST_EXPECT(!exchange->isFieldPresent(sfScale));
            expectPseudoAccount(env, *exchange, asset);
        }

        // An owner that cannot cover two more reserves.
        {
            Account const poor{"poor"};
            env.fund(
                env.current()->fees().accountReserve(1, 1) + env.current()->fees().base * 10, poor);
            env.close();
            env(Perp::exchangeSet(poor, xrpIssue()), Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            BEAST_EXPECT(env.ownerCount(poor) == 0);
        }

        // A missing domain.
        {
            auto jv = Perp::exchangeSet(owner_, xrpIssue());
            jv[sfDomainID] = to_string(uint256{42});
            env(jv, Ter(tecNO_ENTRY));
        }

        // Updates name an existing exchange and come from its owner.
        {
            auto const keylet = createExchange(env, owner_, xrpIssue());
            auto update = Perp::exchangeUpdate(alice_, keylet.key);
            update[sfTakerFee] = 1;
            env(update, Ter(tecNO_PERMISSION));
            auto missing = Perp::exchangeUpdate(owner_, uint256{7});
            missing[sfTakerFee] = 1;
            env(missing, Ter(tecNO_ENTRY));
        }
    }

    void
    testCreateVaultBinding()
    {
        testcase("create vault binding");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const world = setupMarketWorld(env);
        Vault const vault{.env = env};

        auto bound = [&](Account const& owner, Asset const& asset, uint256 const& vaultID) {
            auto jv = Perp::exchangeSet(owner, asset);
            jv[sfVaultID] = to_string(vaultID);
            jv[sfVaultLossWindow] = 3'600;
            return jv;
        };

        // The vault must exist, belong to the account and hold the collateral asset.
        env(bound(owner_, usd_, uint256{9}), Ter(tecNO_ENTRY));
        env(bound(alice_, usd_, world.vault.key), Ter(tecNO_PERMISSION));
        env(bound(owner_, xrpIssue(), world.vault.key), Ter(tecNO_PERMISSION));
        env.close();

        auto const keylet = Perp::exchangeKeylet(env, owner_);
        env(bound(owner_, usd_, world.vault.key));
        env.close();
        auto const exchange = env.le(keylet);
        auto const sleVault = env.le(world.vault);
        if (!BEAST_EXPECT(exchange && sleVault))
            return;
        BEAST_EXPECT((*exchange)[~sfVaultID] == world.vault.key);
        BEAST_EXPECT((*exchange)[~sfVaultLossWindow] == 3'600u);
        BEAST_EXPECT((*sleVault)[~sfPerpExchangeID] == keylet.key);

        // A vault backs at most one exchange.
        env(bound(owner_, usd_, world.vault.key), Ter(tecNO_PERMISSION));

        // A bound vault cannot be deleted.
        env(vault.del({.owner = owner_, .id = world.vault.key}), Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(env.le(world.vault));
    }

    void
    testMptWithoutCanTransfer()
    {
        testcase("MPT without CanTransfer");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);

        MPTTester mpt{env, gw_, kMptInitNoFund};
        mpt.create({.flags = tfMPTCanLock});
        Asset const asset = mpt;
        auto const ownerCount = env.ownerCount(owner_);
        env(Perp::exchangeSet(owner_, asset), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(env.ownerCount(owner_) == ownerCount);
    }

    void
    testUpdateDelayed()
    {
        testcase("update delayed");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);

        auto const keylet = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        create[sfTakerFee] = 500;
        create[sfMakerFee] = 300;
        create[sfCoverRateMinimum] = 1'000;
        env(create);
        env.close();
        auto const market = marketKeylet(keylet.key);
        env(Perp::marketSet(owner_, keylet.key, marketArgs()));
        env.close();

        // Raising the taker fee and lowering the cover minimum are queued; lowering the maker
        // fee and the data apply at once.
        auto const t = perp::refreshTime(*env.current());
        auto update = Perp::exchangeUpdate(owner_, keylet.key);
        update[sfTakerFee] = 800;
        update[sfMakerFee] = 100;
        update[sfCoverRateMinimum] = 500;
        update[sfData] = "01";
        env(update);
        env.close();
        {
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange && exchange->isFieldPresent(sfPendingParameters)))
                return;
            BEAST_EXPECT((*exchange)[sfTakerFee] == 500);
            BEAST_EXPECT((*exchange)[sfMakerFee] == 100);
            BEAST_EXPECT((*exchange)[sfCoverRateMinimum] == 1'000);
            BEAST_EXPECT(strHex((*exchange)[sfData]) == "01");
            BEAST_EXPECT(
                (*exchange)[~sfParameterEffectiveTime] == t + perp::kMinParameterDelay.count());
            auto const& pending = exchange->getFieldObject(sfPendingParameters);
            BEAST_EXPECT(pending[~sfTakerFee] == 800u);
            BEAST_EXPECT(pending[~sfCoverRateMinimum] == 500u);
            BEAST_EXPECT(!pending.isFieldPresent(sfMakerFee));
        }

        // A refresh before the delay leaves the change pending.
        env(Perp::marketUpdate(owner_, market.key));
        env.close();
        {
            auto const exchange = env.le(keylet);
            BEAST_EXPECT(exchange && exchange->isFieldPresent(sfPendingParameters));
            BEAST_EXPECT(exchange && (*exchange)[sfTakerFee] == 500);
        }

        // The first refresh after the delay applies it.
        env.close(env.now() + perp::kMinParameterDelay);
        env(Perp::marketUpdate(owner_, market.key));
        env.close();
        {
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange))
                return;
            BEAST_EXPECT((*exchange)[sfTakerFee] == 800);
            BEAST_EXPECT((*exchange)[sfCoverRateMinimum] == 500);
            BEAST_EXPECT(!exchange->isFieldPresent(sfPendingParameters));
            BEAST_EXPECT(!exchange->isFieldPresent(sfParameterEffectiveTime));
        }

        // A new queued change replaces the pending one.
        auto raise = Perp::exchangeUpdate(owner_, keylet.key);
        raise[sfMakerFee] = 400;
        env(raise);
        env.close();
        auto again = Perp::exchangeUpdate(owner_, keylet.key);
        again[sfBuilderFeeMax] = 50;
        env(again);
        env.close();
        {
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange && exchange->isFieldPresent(sfPendingParameters)))
                return;
            auto const& pending = exchange->getFieldObject(sfPendingParameters);
            BEAST_EXPECT(pending[~sfBuilderFeeMax] == 50u);
            BEAST_EXPECT(!pending.isFieldPresent(sfMakerFee));
        }

        // The owner and vault fee shares may not sum above one once the change applies.
        auto const shared = Perp::exchangeKeylet(env, owner_);
        auto withShare = Perp::exchangeSet(owner_, usd_);
        withShare[sfOwnerFeeShare] = 60'000;
        env(withShare);
        env.close();
        auto vaultShare = Perp::exchangeUpdate(owner_, shared.key);
        vaultShare[sfVaultFeeShare] = 50'000;
        env(vaultShare, Ter(tecLIMIT_EXCEEDED));
        vaultShare[sfVaultFeeShare] = 40'000;
        env(vaultShare);
        env.close();
        auto const sharedExchange = env.le(shared);
        BEAST_EXPECT(
            sharedExchange && sharedExchange->isFieldPresent(sfPendingParameters) &&
            sharedExchange->getFieldObject(sfPendingParameters)[~sfVaultFeeShare] == 40'000u);
    }

    void
    testDelete()
    {
        testcase("delete");
        using namespace test::jtx;

        // Refused while a market is listed; succeeds once empty and clears the vault binding.
        {
            Env env{*this, all_};
            auto const world = setupMarketWorld(env);
            Vault const vault{.env = env};
            auto const ownerCount = env.ownerCount(owner_);
            auto const keylet = Perp::exchangeKeylet(env, owner_);
            auto create = Perp::exchangeSet(owner_, usd_);
            create[sfVaultID] = to_string(world.vault.key);
            create[sfVaultLossWindow] = 3'600;
            env(create);
            env.close();
            auto const created = env.le(keylet);
            if (!BEAST_EXPECT(created))
                return;
            auto const pseudoID = (*created)[sfAccount];
            auto const market = marketKeylet(keylet.key);
            env(Perp::marketSet(owner_, keylet.key, marketArgs()));
            env.close();

            env(Perp::exchangeDelete(alice_, keylet.key), Ter(tecNO_PERMISSION));
            env(Perp::exchangeDelete(owner_, keylet.key), Ter(tecHAS_OBLIGATIONS));
            env.close();
            env(Perp::marketDelete(owner_, market.key));
            env.close();

            env(Perp::exchangeDelete(owner_, keylet.key));
            env.close();
            BEAST_EXPECT(!env.le(keylet));
            BEAST_EXPECT(!env.le(keylet::account(pseudoID)));
            BEAST_EXPECT(!env.le(keylet::trustLine(pseudoID, usd_)));
            BEAST_EXPECT(env.ownerCount(owner_) == ownerCount);
            auto const sleVault = env.le(world.vault);
            BEAST_EXPECT(sleVault && !sleVault->isFieldPresent(sfPerpExchangeID));
            env(Perp::exchangeDelete(owner_, keylet.key), Ter(tecNO_ENTRY));

            // The unbound vault can be deleted.
            env(vault.del({.owner = owner_, .id = world.vault.key}));
            env.close();
            BEAST_EXPECT(!env.le(world.vault));
        }

        // Every asset the pseudo-account holds goes back to the owner.
        {
            Env env{*this, all_};
            fundAccounts(env);
            auto const keylet = createExchange(env, owner_, usd_);
            auto const created = env.le(keylet);
            if (!BEAST_EXPECT(created))
                return;
            auto const pseudoID = (*created)[sfAccount];
            STAmount const before = env.balance(owner_, usd_).value();
            seed(env, keylet, [&](ApplyView& view, SLE::pointer const& exchange) {
                BEAST_EXPECT(
                    isTesSuccess(accountSend(view, owner_.id(), pseudoID, usd_(500), env.journal)));
                (*exchange)[sfAssetsTotal] = Number{500};
                (*exchange)[sfCoverAvailable] = Number{500};
            });
            BEAST_EXPECT(env.balance(owner_, usd_).value() == before - usd_(500).value());
            env(Perp::exchangeDelete(owner_, keylet.key));
            BEAST_EXPECT(env.balance(owner_, usd_).value() == before);
            BEAST_EXPECT(!env.le(keylet));
            BEAST_EXPECT(!env.le(keylet::account(pseudoID)));
            BEAST_EXPECT(!env.le(keylet::trustLine(pseudoID, usd_)));
        }

        // An MPT payout gives the owner an MPToken when it has none.
        {
            Env env{*this, all_};
            fundAccounts(env);
            MPTTester mpt{env, gw_, kMptInitNoFund};
            mpt.create({.flags = tfMPTCanTransfer});
            auto const issue = MPTIssue{mpt.issuanceID()};
            auto const keylet = createExchange(env, owner_, Asset{issue});
            auto const created = env.le(keylet);
            if (!BEAST_EXPECT(created))
                return;
            auto const pseudoID = (*created)[sfAccount];
            BEAST_EXPECT(!env.le(keylet::mptoken(issue.getMptID(), owner_.id())));
            seed(env, keylet, [&](ApplyView& view, SLE::pointer const& exchange) {
                BEAST_EXPECT(isTesSuccess(accountSend(
                    view, gw_.id(), pseudoID, STAmount{issue, std::uint64_t{700}}, env.journal)));
                (*exchange)[sfAssetsTotal] = Number{700};
                (*exchange)[sfOwnerFees] = Number{700};
            });
            env(Perp::exchangeDelete(owner_, keylet.key));
            auto const token = env.le(keylet::mptoken(issue.getMptID(), owner_.id()));
            BEAST_EXPECT(token && (*token)[sfMPTAmount] == 700);
            BEAST_EXPECT(!env.le(keylet::mptoken(issue.getMptID(), pseudoID)));
            BEAST_EXPECT(!env.le(keylet::account(pseudoID)));
        }

        // Refused while margin accounts are open.
        {
            Env env{*this, all_};
            fundAccounts(env);
            auto const keylet = createExchange(env, owner_, xrpIssue());
            seed(env, keylet, [](ApplyView&, SLE::pointer const& exchange) {
                (*exchange)[sfMarginAccountCount] = 1;
            });
            env(Perp::exchangeDelete(owner_, keylet.key), Ter(tecHAS_OBLIGATIONS));
            BEAST_EXPECT(env.le(keylet));
        }
    }

    void
    testDeleteWithBadDebt()
    {
        testcase("delete with bad debt");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const keylet = createExchange(env, owner_, usd_);
        seed(env, keylet, [](ApplyView&, SLE::pointer const& exchange) {
            (*exchange)[sfBadDebt] = Number{1};
        });
        env(Perp::exchangeDelete(owner_, keylet.key), Ter(tecHAS_OBLIGATIONS));
        BEAST_EXPECT(env.le(keylet));
    }

public:
    void
    run() override
    {
        testCreate();
        testCreateVaultBinding();
        testMptWithoutCanTransfer();
        testUpdateDelayed();
        testDelete();
        testDeleteWithBadDebt();
    }
};

BEAST_DEFINE_TESTSUITE(PerpExchange, perp, xrpl);

}  // namespace xrpl
