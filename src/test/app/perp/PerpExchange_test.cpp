#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
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
#include <xrpl/protocol/jss.h>

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

        // XRP collateral: no scale and no holding; fees, liquidation policy and data are stored.
        {
            auto const ownerCount = env.ownerCount(owner_);
            auto const sequence = env.seq(owner_);
            auto const keylet = Perp::exchangeKeylet(env, owner_);
            auto jv = Perp::exchangeSet(owner_, xrpIssue());
            jv[sfMakerFee] = 200;
            jv[sfTakerFee] = 500;
            jv[sfOwnerFeeShare] = 30'000;
            jv[sfLiquidationPenalty] = 2'000;
            jv[sfLiquidationRewardMin] = "1234567";
            jv[sfLiquidationChunkMin] = "50000";
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
            BEAST_EXPECT((*exchange)[sfLiquidationPenalty] == 2'000);
            BEAST_EXPECT((*exchange)[sfLiquidationRewardMin] == Number(1'234'567));
            BEAST_EXPECT((*exchange)[sfLiquidationChunkMin] == Number(50'000));
            BEAST_EXPECT(strHex((*exchange)[sfData]) == "ABCD");
            expectPseudoAccount(env, *exchange, xrpIssue());
        }

        // IOU collateral: a trust line for the pseudo-account and the default scale of 6; the
        // liquidation reward floor is rounded down onto the collateral grid.
        {
            auto const ownerCount = env.ownerCount(owner_);
            auto const keylet = Perp::exchangeKeylet(env, owner_);
            auto jv = Perp::exchangeSet(owner_, usd_);
            jv[sfLiquidationRewardMin] = "5.0000009";
            env(jv);
            env.close();
            BEAST_EXPECT(env.ownerCount(owner_) == ownerCount + 2);
            auto const exchange = env.le(keylet);
            if (!BEAST_EXPECT(exchange))
                return;
            BEAST_EXPECT((*exchange)[sfScale] == kVaultDefaultIouScale);
            BEAST_EXPECT((*exchange)[sfLiquidationRewardMin] == Number(5));
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

        // Creation carries the asset and the liquidation policy, within its bounds.
        {
            auto const ownerCount = env.ownerCount(owner_);
            for (auto const* field : {&sfLiquidationPenalty, &sfLiquidationChunkMin})
            {
                auto jv = Perp::exchangeSet(owner_, usd_);
                jv.removeMember(field->getName());
                env(jv, Ter(temMALFORMED));
            }
            auto noAsset = Perp::exchangeSet(owner_, usd_);
            noAsset.removeMember(jss::Asset);
            env(noAsset, Ter(temMALFORMED));
            auto penalty = Perp::exchangeSet(owner_, usd_);
            penalty[sfLiquidationPenalty] = perp::kRateUnity + 1;
            env(penalty, Ter(temMALFORMED));
            auto chunk = Perp::exchangeSet(owner_, usd_);
            chunk[sfLiquidationChunkMin] = "0";
            env(chunk, Ter(temMALFORMED));
            auto reward = Perp::exchangeSet(owner_, usd_);
            reward[sfLiquidationRewardMin] = "-1";
            env(reward, Ter(temMALFORMED));
            auto fee = Perp::exchangeSet(owner_, usd_);
            fee[sfTakerFee] = perp::kMaxTradingFee + 1;
            env(fee, Ter(temMALFORMED));
            env.close();
            BEAST_EXPECT(env.ownerCount(owner_) == ownerCount);
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
            update[sfTakerFee] = 0;
            env(update, Ter(tecNO_PERMISSION));
            auto missing = Perp::exchangeUpdate(owner_, uint256{7});
            missing[sfTakerFee] = 0;
            env(missing, Ter(tecNO_ENTRY));
        }
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
    testTightenOnly()
    {
        testcase("tighten only");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);

        auto const keylet = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        create[sfMakerFee] = 300;
        create[sfTakerFee] = 500;
        create[sfOwnerFeeShare] = 40'000;
        create[sfBuilderFeeMax] = 100;
        create[sfCoverRateMinimum] = 1'000;
        create[sfData] = "AA";
        env(create);
        env.close();

        auto update = [&](SF_UINT32 const& field, std::uint32_t value) {
            auto jv = Perp::exchangeUpdate(owner_, keylet.key);
            jv[field] = value;
            return jv;
        };
        auto u32 = [&](SF_UINT32 const& field) {
            auto const sle = env.le(keylet);
            return sle ? std::uint32_t{(*sle)[field]} : std::uint32_t{0xFFFF'FFFF};
        };

        // Each loosening is refused and changes nothing: a fee, share or cap up, the cover
        // minimum or the penalty down.
        env(update(sfMakerFee, 301), Ter(tecNO_PERMISSION));
        env(update(sfTakerFee, 501), Ter(tecNO_PERMISSION));
        env(update(sfOwnerFeeShare, 40'001), Ter(tecNO_PERMISSION));
        env(update(sfBuilderFeeMax, 101), Ter(tecNO_PERMISSION));
        env(update(sfCoverRateMinimum, 999), Ter(tecNO_PERMISSION));
        env(update(sfLiquidationPenalty, 999), Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(u32(sfMakerFee) == 300);
        BEAST_EXPECT(u32(sfTakerFee) == 500);
        BEAST_EXPECT(u32(sfOwnerFeeShare) == 40'000);
        BEAST_EXPECT(u32(sfBuilderFeeMax) == 100);
        BEAST_EXPECT(u32(sfCoverRateMinimum) == 1'000);
        BEAST_EXPECT(u32(sfLiquidationPenalty) == 1'000);

        // One loosening field refuses the whole update, tightening fields included.
        auto mixed = update(sfMakerFee, 100);
        mixed[sfTakerFee] = 600;
        env(mixed, Ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(u32(sfMakerFee) == 300);

        // Each tightening applies at once, in the same transaction.
        auto tighten = Perp::exchangeUpdate(owner_, keylet.key);
        tighten[sfMakerFee] = 100;
        tighten[sfTakerFee] = 400;
        tighten[sfOwnerFeeShare] = 0;
        tighten[sfBuilderFeeMax] = 50;
        tighten[sfCoverRateMinimum] = 2'000;
        tighten[sfLiquidationPenalty] = 3'000;
        env(tighten);
        BEAST_EXPECT(u32(sfMakerFee) == 100);
        BEAST_EXPECT(u32(sfTakerFee) == 400);
        BEAST_EXPECT(u32(sfOwnerFeeShare) == 0);
        BEAST_EXPECT(u32(sfBuilderFeeMax) == 50);
        BEAST_EXPECT(u32(sfCoverRateMinimum) == 2'000);
        BEAST_EXPECT(u32(sfLiquidationPenalty) == 3'000);
        env.close();

        // An unchanged value is a tightening; data changes either way.
        auto same = update(sfTakerFee, 400);
        same[sfData] = "0102";
        env(same);
        env.close();
        {
            auto const sle = env.le(keylet);
            BEAST_EXPECT(sle && strHex((*sle)[sfData]) == "0102");
            BEAST_EXPECT(u32(sfTakerFee) == 400);
        }
        auto clear = Perp::exchangeUpdate(owner_, keylet.key);
        clear[sfData] = "";
        env(clear);
        env.close();
        {
            auto const sle = env.le(keylet);
            BEAST_EXPECT(sle && !sle->isFieldPresent(sfData));
        }

        // An update naming a fixed field is malformed, whatever its value.
        auto fixedField = [&](json::StaticString const& name, json::Value const& value) {
            auto jv = Perp::exchangeUpdate(owner_, keylet.key);
            jv[name] = value;
            env(jv, Ter(temMALFORMED));
        };
        fixedField(jss::Asset, toJson(Asset{usd_}));
        fixedField(sfScale.getJsonName(), 6);
        fixedField(sfDomainID.getJsonName(), to_string(uint256{1}));
        fixedField(sfLiquidationRewardMin.getJsonName(), "0");
        fixedField(sfLiquidationChunkMin.getJsonName(), "10000");
        env.close();

        // Rates keep their bounds on update.
        env(update(sfLiquidationPenalty, perp::kRateUnity + 1), Ter(temMALFORMED));
    }

    void
    testDelete()
    {
        testcase("delete");
        using namespace test::jtx;

        // Refused while a market is listed; succeeds once empty.
        {
            Env env{*this, all_};
            setupMarketWorld(env);
            auto const ownerCount = env.ownerCount(owner_);
            auto const keylet = createExchange(env, owner_, usd_);
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
            env(Perp::exchangeDelete(owner_, keylet.key), Ter(tecNO_ENTRY));
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
        testMptWithoutCanTransfer();
        testTightenOnly();
        testDelete();
        testDeleteWithBadDebt();
    }
};

BEAST_DEFINE_TESTSUITE(PerpExchange, perp, xrpl);

}  // namespace xrpl
