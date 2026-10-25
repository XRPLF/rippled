#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace xrpl::test {

class Repo_test : public beast::unit_test::Suite
{
    static json::Value
    create(
        jtx::Account const& seller,
        jtx::Account const& buyer,
        STAmount const& collateral,
        STAmount const& price,
        std::uint32_t rate,
        std::uint32_t expiration,
        std::uint32_t maturity,
        std::uint32_t grace)
    {
        json::Value jv;
        jv[sfTransactionType] = jss::RepoCreate;
        jv[sfAccount] = seller.human();
        jv[sfCounterparty] = buyer.human();
        jv[sfCollateralAmount] = collateral.getJson(JsonOptions::Values::None);
        jv[sfPurchasePrice] = price.getJson(JsonOptions::Values::None);
        jv[sfInterestRate] = rate;
        jv[sfExpiration] = expiration;
        jv[sfMaturityDate] = maturity;
        jv[sfGracePeriod] = grace;
        return jv;
    }

    static json::Value
    simple(json::StaticString const txType, jtx::Account const& account, uint256 const& repoID)
    {
        json::Value jv;
        jv[sfTransactionType] = txType;
        jv[sfAccount] = account.human();
        jv[sfRepoID] = to_string(repoID);
        return jv;
    }

    static uint256
    repoID(jtx::Env& env, jtx::Account const& seller)
    {
        return keylet::repo(seller.id(), SeqProxy::rawSequence(env.seq(seller))).key;
    }

    static bool
    exists(jtx::Env const& env, uint256 const& id)
    {
        return env.le(keylet::repo(id)) != nullptr;
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;

        {
            Env env{*this, features - featureRepo};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            auto const now = env.now().time_since_epoch().count();
            env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 200, 60),
                Ter(temDISABLED));
            env.close();
        }

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const ownersBefore = ownerCount(env, seller);
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();

        BEAST_EXPECT(exists(env, id));
        BEAST_EXPECT(ownerCount(env, seller) == ownersBefore + 1);

        auto const sle = env.le(keylet::repo(id));
        BEAST_EXPECT(sle && sle->getAccountID(sfAccount) == seller.id());
        BEAST_EXPECT(sle && sle->getAccountID(sfCounterparty) == buyer.id());
        // Pending until accepted.
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfStartDate));
    }

    void
    testCreateMalformed(FeatureBitset features)
    {
        testcase("create malformed");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        auto const now = env.now().time_since_epoch().count();

        // The counterparty may not be the account.
        env(create(seller, seller, XRP(1000), usd(900), 0, now + 100, now + 200, 60),
            Ter(temMALFORMED));

        // Collateral and cash must differ: repaying in the locked asset would
        // let the seller settle with the very collateral that is immobilized.
        env(create(seller, buyer, XRP(1000), XRP(900), 0, now + 100, now + 200, 60),
            Ter(temBAD_AMOUNT));
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();

        // Maturity must be after expiration.
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 300, now + 200, 60),
            Ter(temBAD_EXPIRATION));

        // Amounts must be positive.
        env(create(seller, buyer, XRP(0), usd(900), 0, now + 100, now + 200, 60),
            Ter(temBAD_AMOUNT));
        env.close();
    }

    void
    testAcceptAndClose(FeatureBitset features)
    {
        testcase("accept and close");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        // Collateral XRP, cash USD, zero interest so the repurchase is exact.
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 1000, 60));
        env.close();

        auto const sellerUsdBefore = env.balance(seller, usd.issue());

        // Only the pinned buyer may accept.
        env(simple(jss::RepoAccept, gw, id), Ter(tecNO_PERMISSION));

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // The cash arrived and the repo is active.
        BEAST_EXPECT(env.balance(seller, usd.issue()) == sellerUsdBefore + usd(900));
        auto const sle = env.le(keylet::repo(id));
        BEAST_EXPECT(sle && sle->isFieldPresent(sfStartDate));

        // A second accept fails.
        env(simple(jss::RepoAccept, buyer, id), Ter(tecREPO_ACTIVE));

        // Only the seller may repurchase.
        env(simple(jss::RepoClose, buyer, id), Ter(tecNO_PERMISSION));

        auto const buyerUsdBefore = env.balance(buyer, usd.issue());
        env(simple(jss::RepoClose, seller, id));
        env.close();

        // Zero interest, so the buyer gets exactly the purchase price back and
        // the entry is gone.
        BEAST_EXPECT(env.balance(buyer, usd.issue()) == buyerUsdBefore + usd(900));
        BEAST_EXPECT(!exists(env, id));
    }

    void
    testCancel(FeatureBitset features)
    {
        testcase("cancel");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, stranger{"stranger"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, stranger);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const ownersBefore = ownerCount(env, seller);
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 1000, 60));
        env.close();

        // A stranger cannot cancel while the offer is live.
        env(simple(jss::RepoCancel, stranger, id), Ter(tecNO_PERMISSION));

        // The seller can, at any time.
        env(simple(jss::RepoCancel, seller, id));
        env.close();

        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(ownerCount(env, seller) == ownersBefore);

        // Once expired, anyone may clear it.
        auto const id2 = repoID(env, seller);
        auto const now2 = env.now().time_since_epoch().count();
        env(create(seller, buyer, XRP(1000), usd(900), 0, now2 + 10, now2 + 1000, 60));
        env.close();
        env.close(std::chrono::seconds(60));
        env(simple(jss::RepoCancel, stranger, id2));
        env.close();
        BEAST_EXPECT(!exists(env, id2));
    }

    void
    testDefault(FeatureBitset features)
    {
        testcase("default");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, keeper);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // Not yet in default.
        env(simple(jss::RepoDefault, keeper, id), Ter(tecTOO_SOON));

        auto const buyerXrpBefore = env.balance(buyer);
        env.close(std::chrono::seconds(400));

        // Past maturity plus grace, anyone may close it out.
        env(simple(jss::RepoDefault, keeper, id));
        env.close();

        BEAST_EXPECT(!exists(env, id));
        // The buyer keeps the collateral.
        BEAST_EXPECT(env.balance(buyer) > buyerXrpBefore);
    }

    void
    testInterest(FeatureBitset features)
    {
        testcase("interest accrues and is capped at maturity");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, usd(5000)));
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        // kTenthBipsPerUnity is 100 percent annualized.
        env(create(
            seller,
            buyer,
            XRP(1000),
            usd(1000),
            kTenthBipsPerUnity.value(),
            now + 100,
            now + 400000,
            600));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        auto const start = (*env.le(keylet::repo(id)))[sfStartDate];
        auto const buyerBefore = env.balance(buyer, usd.issue());
        env.close(std::chrono::seconds(50000));

        // The close time the transactor will use is the parent close time of the
        // ledger that applies it, which is the current open ledger's.
        auto const closeTime = env.current()->parentCloseTime().time_since_epoch().count();
        env(simple(jss::RepoClose, seller, id));
        env.close();

        auto const elapsed = closeTime - start;
        BEAST_EXPECT(elapsed > 0);
        Number const expected = Number(1000) +
            tenthBipsOfValue(Number(1000) * Number(elapsed), kTenthBipsPerUnity) /
                Number(kSecondsInYear);
        auto const paid = env.balance(buyer, usd.issue()).value() - buyerBefore.value();

        // Exactly the repurchase amount, so the rate's scale is pinned rather
        // than merely shown to be positive.
        BEAST_EXPECT(paid == STAmount(usd.issue(), expected));
        BEAST_EXPECT(!exists(env, id));
    }

    // The rate carries the same ceiling LoanSet puts on sfInterestRate.
    void
    testInterestRateBound(FeatureBitset features)
    {
        testcase("the interest rate is bounded");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        env(create(
                seller,
                buyer,
                XRP(1000),
                usd(1000),
                lending::kMaxInterestRate.value() + 1,
                now + 100,
                now + 1000,
                600),
            Ter(temBAD_AMOUNT));
        env.close();

        auto const id = repoID(env, seller);
        env(create(
            seller,
            buyer,
            XRP(1000),
            usd(1000),
            lending::kMaxInterestRate.value(),
            now + 100,
            now + 1000,
            600));
        env.close();
        BEAST_EXPECT(exists(env, id));
    }

    // Collateral that is not XRP goes through the XLS-85 locking path, which
    // needs the issuer's opt-in and unfrozen, authorized parties on both sides.
    void
    testIouCollateral(FeatureBitset features)
    {
        testcase("IOU collateral");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env.trust(clt(100000), seller, buyer);
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env(pay(cash, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();

        // Without the issuer's locking opt-in the collateral cannot be locked.
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 1000, 60),
            Ter(tecNO_PERMISSION));
        env.close();

        env(fset(gw, asfAllowTrustLineLocking));
        env.close();

        auto const id = repoID(env, seller);
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 1000, 60));
        env.close();
        BEAST_EXPECT(exists(env, id));

        // The collateral left the seller's spendable balance.
        BEAST_EXPECT(env.balance(seller, clt.issue()) == clt(4000));

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        env(simple(jss::RepoClose, seller, id));
        env.close();

        // Closing returns the collateral and the entry is gone.
        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(seller, clt.issue()) == clt(5000));
    }

    // On default the collateral must reach the buyer, so a frozen or
    // unauthorized buyer is rejected before anything is locked.
    void
    testIouCollateralFrozen(FeatureBitset features)
    {
        testcase("IOU collateral, frozen party");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(clt(100000), seller, buyer);
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env(pay(cash, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();

        // Freezing the buyer blocks the create, because the buyer is where the
        // collateral goes if the seller defaults.
        env(trust(gw, clt(100000), buyer, tfSetFreeze));
        env.close();
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 1000, 60),
            Ter(tecFROZEN));
        env.close();

        env(trust(gw, clt(100000), buyer, tfClearFreeze));
        env.close();

        // And freezing the seller blocks it too.
        env(trust(gw, clt(100000), seller, tfSetFreeze));
        env.close();
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 1000, 60),
            Ter(tecFROZEN));
        env.close();
    }

    // MPT collateral locks through the issuance's locked-amount accounting
    // rather than a trustline, and needs the issuance to permit escrow.
    void
    testMptCollateral(FeatureBitset features)
    {
        testcase("MPT collateral");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(cash, buyer, usd(5000)));
        env.close();

        // An issuance that does not permit escrow cannot be locked.
        {
            MPTTester noLock(env, gw, {.holders = {seller, buyer}, .fund = false});
            noLock.create({.flags = tfMPTCanTransfer});
            noLock.authorize({.account = seller});
            noLock.authorize({.account = buyer});
            auto const mpt = noLock["MPT"];
            env(pay(gw, seller, mpt(10000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 1000, 60),
                Ter(tecNO_PERMISSION));
            env.close();
        }

        MPTTester mptGw(env, gw, {.holders = {seller, buyer}, .fund = false});
        mptGw.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer});
        mptGw.authorize({.account = seller});
        mptGw.authorize({.account = buyer});
        auto const mpt = mptGw["MPT"];
        env(pay(gw, seller, mpt(10000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 1000, 60));
        env.close();
        BEAST_EXPECT(exists(env, id));

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // Default hands the collateral to the buyer, exercising the MPT
        // unlock-to-a-third-party path.
        env.close(std::chrono::seconds(2000));
        env(simple(jss::RepoDefault, buyer, id));
        env.close();

        BEAST_EXPECT(!exists(env, id));
    }

    // The spec requires an open repo to block account deletion, which falls out
    // of ltREPO being absent from nonObligationDeleter. Both parties carry the
    // entry in their owner directory, so both are blocked.
    void
    testAccountDeleteBlocked(FeatureBitset features)
    {
        testcase("an open repo blocks account deletion");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, sink{"sink"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, sink);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(1000), 0, now + 100, now + 100000, 600));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // Both parties carry the entry, so both are refused.
        incLgrSeqForAccDel(env, seller);
        env(acctdelete(seller, sink),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
        env(acctdelete(buyer, sink),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();

        // With the repo gone and the zero-balance trust line removed, the same
        // account deletes, so the refusal above was the repo and nothing else.
        env(simple(jss::RepoClose, seller, id));
        env.close();
        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(0));

        env(trust(seller, usd(0)));
        env.close();
        env(acctdelete(seller, sink), Fee(drops(env.current()->fees().increment)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(seller.id())));
    }

    // account_objects and ledger_entry both resolve the entry under the name the
    // LEDGER_ENTRY declaration gives it.
    void
    testRpc(FeatureBitset features)
    {
        testcase("account_objects and ledger_entry");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(1000), 500, now + 100, now + 100000, 600));
        env.close();

        auto const objects = [&](jtx::Account const& who) {
            json::Value params;
            params[jss::account] = who.human();
            params[jss::type] = "repo";
            return env.rpc("json", "account_objects", to_string(params))[jss::result];
        };

        for (auto const& who : {seller, buyer})
        {
            auto const jv = objects(who);
            BEAST_EXPECT(jv[jss::account_objects].size() == 1);
            auto const& object = jv[jss::account_objects][0u];
            BEAST_EXPECT(object["LedgerEntryType"].asString() == "Repo");
            BEAST_EXPECT(object[jss::index].asString() == to_string(id));
        }

        {
            json::Value params;
            params[jss::repo] = json::ValueType::Object;
            params[jss::repo][jss::account] = seller.human();
            params[jss::repo][jss::seq] = env.seq(seller) - 1;
            auto const jv = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jv[jss::index].asString() == to_string(id));
        }
    }

    // Every Repo action is refused while the amendment is disabled, and each
    // one names an entry that has to exist.
    void
    testActionsNeedTheAmendmentAndAnEntry(FeatureBitset features)
    {
        testcase("actions need the amendment and an entry");
        using namespace jtx;

        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        uint256 const missing{1};

        {
            Env env{*this, features - featureRepo};
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env(simple(jss::RepoAccept, buyer, missing), Ter(temDISABLED));
            env(simple(jss::RepoCancel, seller, missing), Ter(temDISABLED));
            env(simple(jss::RepoClose, seller, missing), Ter(temDISABLED));
            env(simple(jss::RepoDefault, buyer, missing), Ter(temDISABLED));
            env.close();
        }

        Env env{*this, features};
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env(simple(jss::RepoAccept, buyer, missing), Ter(tecNO_ENTRY));
        env(simple(jss::RepoCancel, seller, missing), Ter(tecNO_ENTRY));
        env(simple(jss::RepoClose, seller, missing), Ter(tecNO_ENTRY));
        env(simple(jss::RepoDefault, buyer, missing), Ter(tecNO_ENTRY));
        env.close();
    }

    // Preflight rejects a non-positive price and an oversized legal annex, and
    // stores an annex at the size limit.
    void
    testCreatePriceAndData(FeatureBitset features)
    {
        testcase("create price and data");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        auto const now = env.now().time_since_epoch().count();

        env(create(seller, buyer, XRP(1000), usd(0), 0, now + 100, now + 200, 60),
            Ter(temBAD_AMOUNT));
        env(create(seller, buyer, XRP(1000), usd(-1), 0, now + 100, now + 200, 60),
            Ter(temBAD_AMOUNT));

        auto const withData = [&](std::size_t bytes) {
            auto jv = create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 200, 60);
            jv[sfData] = strHex(std::string(bytes, 'a'));
            return jv;
        };
        env(withData(257), Ter(temMALFORMED));
        env.close();

        auto const id = repoID(env, seller);
        env(withData(256));
        env.close();
        auto const sle = env.le(keylet::repo(id));
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT(sle->getFieldVL(sfData).size() == 256);
    }

    // Preclaim needs an existing counterparty and an issuer who is neither
    // party and exists.
    void
    testCreateParties(FeatureBitset features)
    {
        testcase("create parties");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        Account const ghost{"ghost"}, ghostIssuer{"ghostIssuer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        auto const now = env.now().time_since_epoch().count();

        env(create(seller, ghost, XRP(1000), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_DST));
        env(create(seller, buyer, seller["CLT"](10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_PERMISSION));
        env(create(seller, buyer, buyer["CLT"](10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_PERMISSION));
        env(create(seller, buyer, ghostIssuer["CLT"](10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_ISSUER));
        env.close();
    }

    // The seller's reserve must cover the new entry, and locked XRP must leave
    // the seller above its own reserve floor.
    void
    testCreateReserve(FeatureBitset features)
    {
        testcase("create reserve");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, poor{"poor"}, buyer{"buyer"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        auto const reserveOne = env.current()->fees().accountReserve(1, 1);
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.fund(STAmount{reserveOne + XRPAmount{100}}, poor);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.trust(clt(100000), poor, buyer);
        env.close();
        env(pay(gw, poor, clt(5000)));
        env.close();
        auto const now = env.now().time_since_epoch().count();

        env(create(poor, buyer, clt(1000), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecINSUFFICIENT_RESERVE));

        env(create(seller, buyer, XRP(9990), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecUNFUNDED));
        env.close();
    }

    // The XLS-85 IOU table: the seller needs a line holding enough, both
    // parties must be authorized, and locking must not lose precision.
    void
    testIouCollateralChecks(FeatureBitset features)
    {
        testcase("IOU collateral checks");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, lineless{"lineless"},
            buyer{"buyer"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, lineless, buyer);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.trust(clt(1'000'000'000'000'000'000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(500)));
        env.close();
        auto const now = env.now().time_since_epoch().count();

        env(create(lineless, buyer, clt(10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_LINE));
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecINSUFFICIENT_FUNDS));
        env.close();

        // A one-unit lock against a balance of 1e17 cannot be represented.
        env(pay(gw, seller, clt(100'000'000'000'000'000)));
        env.close();
        env(create(seller, buyer, clt(1), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecPRECISION_LOSS));
        env.close();

        // An issuer requiring authorization must have authorized both parties.
        Account const authGw{"authGw"}, authSeller{"authSeller"}, authBuyer{"authBuyer"};
        auto const ac = authGw["ACL"];
        env.fund(XRP(10000), authGw, authSeller, authBuyer);
        env.close();
        env(fset(authGw, asfRequireAuth));
        env(fset(authGw, asfAllowTrustLineLocking));
        env.close();
        env.trust(ac(100000), authSeller, authBuyer);
        env.close();

        env(create(authSeller, authBuyer, ac(10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_AUTH));
        env.close();

        env(trust(authGw, ac(0), authSeller, tfSetfAuth));
        env.close();
        env(pay(authGw, authSeller, ac(5000)));
        env.close();
        env(create(authSeller, authBuyer, ac(10), usd(900), 0, now + 100, now + 200, 60),
            Ter(tecNO_AUTH));
        env.close();
    }

    // The XLS-85 MPT table: the issuance and the seller's MPToken must exist,
    // both parties be authorized and unlocked, the token transferable between
    // them, and the seller holding enough.
    void
    testMptCollateralChecks(FeatureBitset features)
    {
        testcase("MPT collateral checks");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        auto const now = env.now().time_since_epoch().count();

        // No issuance under this ID.
        STAmount const unissued{MPTIssue{makeMptID(999, gw.id())}, std::uint64_t{100}};
        env(create(seller, buyer, unissued, usd(900), 0, now + 100, now + 200, 60),
            Ter(tecOBJECT_NOT_FOUND));

        // The seller holds no MPToken.
        {
            MPTTester noHolding(env, gw, {.holders = {seller, buyer}, .fund = false});
            noHolding.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer});
            env(create(seller, buyer, noHolding["MPT"](10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecOBJECT_NOT_FOUND));
        }

        // Authorization required: first the seller, then the buyer.
        {
            MPTTester auth(env, gw, {.holders = {seller, buyer}, .fund = false});
            auth.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            auth.authorize({.account = seller});
            auth.authorize({.account = buyer});
            auto const mpt = auth["MPT"];
            env(create(seller, buyer, mpt(10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecNO_AUTH));
            env.close();
            auth.authorize({.account = gw, .holder = seller});
            env(pay(gw, seller, mpt(1000)));
            env.close();
            env(create(seller, buyer, mpt(10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecNO_AUTH));
        }

        // Locked: first the seller, then the buyer.
        {
            MPTTester lockable(env, gw, {.holders = {seller, buyer}, .fund = false});
            lockable.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            lockable.authorize({.account = seller});
            lockable.authorize({.account = buyer});
            auto const mpt = lockable["MPT"];
            env(pay(gw, seller, mpt(1000)));
            env.close();
            lockable.set({.account = gw, .holder = seller, .flags = tfMPTLock});
            env(create(seller, buyer, mpt(10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecLOCKED));
            env.close();
            lockable.set({.account = gw, .holder = seller, .flags = tfMPTUnlock});
            lockable.set({.account = gw, .holder = buyer, .flags = tfMPTLock});
            env(create(seller, buyer, mpt(10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecLOCKED));
        }

        // Not transferable between two holders, and then too little held.
        {
            MPTTester escrowOnly(env, gw, {.holders = {seller, buyer}, .fund = false});
            escrowOnly.create({.flags = tfMPTCanEscrow});
            escrowOnly.authorize({.account = seller});
            escrowOnly.authorize({.account = buyer});
            auto const mpt = escrowOnly["MPT"];
            env(pay(gw, seller, mpt(1000)));
            env.close();
            env(create(seller, buyer, mpt(10), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecNO_AUTH));
        }
        {
            MPTTester small(env, gw, {.holders = {seller, buyer}, .fund = false});
            small.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer});
            small.authorize({.account = seller});
            small.authorize({.account = buyer});
            auto const mpt = small["MPT"];
            env(pay(gw, seller, mpt(100)));
            env.close();
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 200, 60),
                Ter(tecINSUFFICIENT_FUNDS));
        }
        env.close();
    }

    // The cash leg of an accept is a payment: it fails, and the offer stays
    // pending, when the buyer is frozen, the seller deep frozen, either side
    // unauthorized or without a line, the asset cannot ripple, or the buyer
    // cannot cover it.
    void
    testAcceptPayment(FeatureBitset features)
    {
        testcase("accept payment");
        using namespace jtx;

        auto const now = [](Env& env) { return env.now().time_since_epoch().count(); };

        // Freeze on either side, and too little cash.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(pay(gw, buyer, usd(500)));
            env.close();

            auto const id = repoID(env, seller);
            env(create(
                seller, buyer, XRP(1000), usd(900), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();

            // Each refused accept gets its own ledger, so closing it never
            // retries the accept after the state change that follows.
            env(simple(jss::RepoAccept, buyer, id), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
            env(pay(gw, buyer, usd(500)));
            env.close();

            env(trust(gw, usd(0), buyer, tfSetFreeze));
            env.close();
            env(simple(jss::RepoAccept, buyer, id), Ter(tecFROZEN));
            env.close();
            env(trust(gw, usd(0), buyer, tfClearFreeze));
            env.close();

            env(trust(gw, usd(0), seller, tfSetFreeze | tfSetDeepFreeze));
            env.close();
            env(simple(jss::RepoAccept, buyer, id), Ter(tecFROZEN));
            env.close();

            auto const sle = env.le(keylet::repo(id));
            BEAST_EXPECT(sle && !sle->isFieldPresent(sfStartDate));
            BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(1000));
        }

        // The seller has no line for the cash, so the payment would open one.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env.trust(usd(100000), buyer);
            env.close();
            env(pay(gw, buyer, usd(5000)));
            env.close();

            auto const id = repoID(env, seller);
            env(create(
                seller, buyer, XRP(1000), usd(900), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id), Ter(tecNO_LINE));
            env.close();
            BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(5000));
        }

        // The issuer requires authorization and has not given it to the buyer.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(trust(gw, usd(0), seller, tfSetfAuth));
            env.close();

            auto const id = repoID(env, seller);
            env(create(
                seller, buyer, XRP(1000), usd(900), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id), Ter(tecNO_AUTH));
            env.close();
        }

        // The issuer does not ripple, so the cash cannot move between holders.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), noripple(gw), seller, buyer);
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(pay(gw, buyer, usd(5000)));
            env.close();

            auto const id = repoID(env, seller);
            env(create(
                seller, buyer, XRP(1000), usd(900), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id), Ter(terNO_RIPPLE));
            env.close();
        }

        // XRP cash: the buyer must cover the price on top of the fee.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const clt = gw["CLT"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.trust(clt(100000), seller, buyer);
            env.close();
            env(pay(gw, seller, clt(5000)));
            env.close();

            auto const tooMuch = repoID(env, seller);
            env(create(
                seller, buyer, clt(1000), XRP(20000), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, tooMuch), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            auto const id = repoID(env, seller);
            env(create(
                seller, buyer, clt(1000), XRP(100), 0, now(env) + 1000, now(env) + 2000, 60));
            env.close();
            auto const sellerBefore = env.balance(seller);
            env(simple(jss::RepoAccept, buyer, id));
            env.close();
            BEAST_EXPECT(env.balance(seller) == sellerBefore + XRP(100));

            auto const buyerBefore = env.balance(buyer);
            env(simple(jss::RepoClose, seller, id));
            env.close();
            BEAST_EXPECT(env.balance(buyer) == buyerBefore + XRP(100));
            BEAST_EXPECT(!exists(env, id));
        }
    }

    // Each lifecycle action refuses a repo in the wrong state or past its
    // deadline, and a repurchase the seller cannot pay leaves the collateral
    // locked.
    void
    testLifecycleRefusals(FeatureBitset features)
    {
        testcase("lifecycle refusals");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, keeper);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(
            seller,
            buyer,
            XRP(1000),
            usd(900),
            kTenthBipsPerUnity.value() / 10,
            now + 100,
            now + 100000,
            60));
        env.close();

        // Pending: neither a repurchase nor a default applies.
        env(simple(jss::RepoClose, seller, id), Ter(tecREPO_PENDING));
        env(simple(jss::RepoDefault, keeper, id), Ter(tecREPO_PENDING));
        // Close the ledger so the refused close is not reapplied after the accept.
        env.close();

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // Active: it can no longer be withdrawn.
        env(simple(jss::RepoCancel, seller, id), Ter(tecREPO_ACTIVE));

        // The seller holds exactly the price, short of the accrued interest.
        env.close(std::chrono::seconds(10000));
        env(simple(jss::RepoClose, seller, id), Ter(tecINSUFFICIENT_FUNDS));
        env.close();
        BEAST_EXPECT(exists(env, id));
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(900));

        // Past maturity plus grace the seller can no longer repurchase.
        env.close(std::chrono::seconds(100000));
        env(simple(jss::RepoClose, seller, id), Ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(exists(env, id));

        // An offer past its expiration can no longer be accepted.
        auto const late = repoID(env, seller);
        auto const now2 = env.now().time_since_epoch().count();
        env(create(seller, buyer, XRP(1000), usd(900), 0, now2 + 10, now2 + 1000, 60));
        env.close();
        env.close(std::chrono::seconds(60));
        env(simple(jss::RepoAccept, buyer, late), Ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(exists(env, late));
    }

    // Accepting and repurchasing in the same ledger accrues no interest, so the
    // seller repays exactly the price even at a non-zero rate.
    void
    testCloseInTheAcceptLedger(FeatureBitset features)
    {
        testcase("close in the accept ledger");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(
            seller,
            buyer,
            XRP(1000),
            usd(900),
            kTenthBipsPerUnity.value(),
            now + 100,
            now + 100000,
            60));
        env.close();

        env(simple(jss::RepoAccept, buyer, id));
        env(simple(jss::RepoClose, seller, id));
        env.close();

        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(5000));
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(0));
    }

    // The issuer's transfer rate is captured at create and charged when the
    // collateral is delivered to the buyer on default.
    void
    testDefaultAtTransferRate(FeatureBitset features)
    {
        testcase("default at the captured transfer rate");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer, keeper);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env(rate(gw, 1.25));
        env.trust(clt(100000), seller, buyer);
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env(pay(cash, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();
        auto const sle = env.le(keylet::repo(id));
        BEAST_EXPECT(sle && (*sle)[~sfTransferRate] == 1'250'000'000u);

        env(simple(jss::RepoAccept, buyer, id));
        env.close();
        env.close(std::chrono::seconds(400));
        env(simple(jss::RepoDefault, keeper, id));
        env.close();

        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(buyer, clt.issue()) == clt(800));
    }

    // Default cannot be blocked: delivering IOU collateral to a buyer without a
    // line opens one even when the buyer cannot cover its reserve.
    void
    testDefaultBuyerReserve(FeatureBitset features)
    {
        testcase("default opens the buyer's line without its reserve");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        auto const reserveOne = env.current()->fees().accountReserve(1, 1);
        env.fund(XRP(10000), gw, cash, seller, keeper);
        env.fund(STAmount{reserveOne + XRPAmount{100}}, buyer);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.trust(clt(100000), seller);
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env(pay(cash, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();
        env.close(std::chrono::seconds(400));

        env(simple(jss::RepoDefault, keeper, id));
        env.close();
        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(buyer, clt.issue()) == clt(1000));
    }

    // ledger_entry takes the entry's index as a string, and rejects an object
    // without a valid account or an integral seq.
    void
    testLedgerEntryRequests(FeatureBitset features)
    {
        testcase("ledger_entry requests");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(1000), 0, now + 100, now + 100000, 600));
        env.close();

        auto const request = [&](json::Value const& repo) {
            json::Value params;
            params[jss::repo] = repo;
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };

        BEAST_EXPECT(request(to_string(id))[jss::index].asString() == to_string(id));

        json::Value badAccount{json::ValueType::Object};
        badAccount[jss::account] = "notAnAddress";
        badAccount[jss::seq] = 1;
        BEAST_EXPECT(request(badAccount)[jss::error].asString() == "malformedAddress");

        json::Value noSeq{json::ValueType::Object};
        noSeq[jss::account] = seller.human();
        BEAST_EXPECT(request(noSeq)[jss::error].asString() == "malformedRequest");

        json::Value textSeq{json::ValueType::Object};
        textSeq[jss::account] = seller.human();
        textSeq[jss::seq] = "one";
        BEAST_EXPECT(request(textSeq)[jss::error].asString() == "malformedRequest");
    }

    // Collateral returned to the seller, by a third party's cancel after
    // expiration or by the seller's own repurchase, comes back in full: the
    // captured transfer rate applies only to delivery to the buyer.
    void
    testCollateralReturnsWithoutTransferFee(FeatureBitset features)
    {
        testcase("collateral returns to the seller without the transfer fee");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"},
            stranger{"stranger"};
        auto const clt = gw["CLT"];
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer, stranger);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env(rate(gw, 1.25));
        env.trust(clt(100000), seller, buyer);
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env(pay(cash, buyer, usd(5000)));
        env.close();

        auto const sellerBefore = env.balance(seller, clt.issue());

        auto const now = env.now().time_since_epoch().count();
        auto const expired = repoID(env, seller);
        env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 1000, 60));
        env.close();
        env.close(std::chrono::seconds(200));
        env(simple(jss::RepoCancel, stranger, expired));
        env.close();
        BEAST_EXPECT(!exists(env, expired));
        BEAST_EXPECT(env.balance(seller, clt.issue()) == sellerBefore);

        auto const now2 = env.now().time_since_epoch().count();
        auto const closed = repoID(env, seller);
        env(create(seller, buyer, clt(1000), usd(900), 0, now2 + 100, now2 + 1000, 60));
        env.close();
        env(simple(jss::RepoAccept, buyer, closed));
        env.close();
        env(simple(jss::RepoClose, seller, closed));
        env.close();
        BEAST_EXPECT(!exists(env, closed));
        BEAST_EXPECT(env.balance(seller, clt.issue()) == sellerBefore);
    }

    // Maturity plus grace must fit the 32-bit date fields, so the deadline
    // cannot wrap to a time before maturity.
    void
    testDeadlineFitsTheDateField(FeatureBitset features)
    {
        testcase("maturity plus grace fits the date field");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, keeper);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        std::uint32_t const maturity = now + 1000;
        std::uint32_t const latest = std::numeric_limits<std::uint32_t>::max() - maturity;

        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, maturity, latest + 1),
            Ter(temBAD_EXPIRATION));
        env.close();

        // The largest grace that fits is accepted and leaves the repo not yet
        // in default.
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, maturity, latest));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();
        env(simple(jss::RepoDefault, keeper, id), Ter(tecTOO_SOON));
        env.close();
        BEAST_EXPECT(exists(env, id));
    }

    // The cash leg carries the issuer's transfer fee on top of the price
    // unless the payer or the payee is the issuer, so the payer must hold the
    // price with its fee.
    void
    testCashLegTransferFee(FeatureBitset features)
    {
        testcase("the cash leg carries the transfer fee");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env(rate(gw, 1.25));
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, usd(900)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), usd(900), 0, now + 1000, now + 2000, 60));
        env.close();

        // Exactly the price is not enough once the fee is added.
        env(simple(jss::RepoAccept, buyer, id), Ter(tecINSUFFICIENT_FUNDS));
        env.close();
        BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(900));

        env(pay(gw, buyer, usd(225)));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();
        BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(0));
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(900));

        // The issuer as buyer pays its own cash with no fee.
        auto const fromIssuer = repoID(env, seller);
        env(create(seller, gw, XRP(1000), usd(900), 0, now + 1000, now + 2000, 60));
        env.close();
        env(simple(jss::RepoAccept, gw, fromIssuer));
        env.close();
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(1800));

        // Repurchasing from the issuer redeems the cash with no fee.
        env(simple(jss::RepoClose, seller, fromIssuer));
        env.close();
        BEAST_EXPECT(!exists(env, fromIssuer));
        BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(900));

        // The issuer as seller receives its own cash with no fee.
        env(pay(gw, buyer, usd(900)));
        env.close();
        auto const toIssuer = repoID(env, gw);
        env(create(gw, buyer, XRP(1000), usd(900), 0, now + 1000, now + 2000, 60));
        env.close();
        env(simple(jss::RepoAccept, buyer, toIssuer));
        env.close();
        BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(0));
    }

    // The buyer cannot block a repurchase by removing its holding of the cash
    // asset: the close opens it again and pays into it.
    void
    testCloseAfterBuyerDropsCashHolding(FeatureBitset features)
    {
        testcase("close after the buyer drops its cash holding");
        using namespace jtx;

        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(pay(gw, buyer, usd(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 1000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();

            env(pay(buyer, gw, env.balance(buyer, usd.issue())));
            env.close();
            env(trust(buyer, usd(0)));
            env.close();
            BEAST_EXPECT(!env.le(keylet::trustLine(buyer.id(), usd.issue())));

            env(simple(jss::RepoClose, seller, id));
            env.close();
            BEAST_EXPECT(!exists(env, id));
            BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(900));
        }

        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            MPTTester cashMpt(env, gw, {.holders = {seller, buyer}, .fund = false});
            cashMpt.create({.flags = tfMPTCanTransfer});
            cashMpt.authorize({.account = seller});
            cashMpt.authorize({.account = buyer});
            auto const mpt = cashMpt["MPT"];
            env(pay(gw, buyer, mpt(900)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, XRP(1000), mpt(900), 0, now + 100, now + 1000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();

            cashMpt.authorize({.account = buyer, .flags = tfMPTUnauthorize});
            BEAST_EXPECT(!env.le(keylet::mptoken(cashMpt.issuanceID(), buyer.id())));

            env(simple(jss::RepoClose, seller, id));
            env.close();
            BEAST_EXPECT(!exists(env, id));
            BEAST_EXPECT(env.balance(buyer, mpt.asset()) == mpt(900));
        }
    }

    // Default delivers MPT collateral to a buyer that holds no MPToken by
    // opening one.
    void
    testDefaultOpensMptHolding(FeatureBitset features)
    {
        testcase("default opens the buyer's MPToken");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer, keeper);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(cash, buyer, usd(5000)));
        env.close();

        MPTTester collateral(env, gw, {.holders = {seller}, .fund = false});
        collateral.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer});
        collateral.authorize({.account = seller});
        auto const mpt = collateral["MPT"];
        env(pay(gw, seller, mpt(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 200, 60));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();
        BEAST_EXPECT(!env.le(keylet::mptoken(collateral.issuanceID(), buyer.id())));

        env.close(std::chrono::seconds(400));
        env(simple(jss::RepoDefault, keeper, id));
        env.close();
        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(buyer, mpt.asset()) == mpt(1000));
    }

    // The collateral is released only to a receiver the issuer still lets
    // receive it: a deep-frozen or locked receiver refuses the default, the
    // close and the cancel, and the collateral stays locked.
    void
    testReleaseRechecksTheReceiver(FeatureBitset features)
    {
        testcase("release rechecks the receiver's freeze");
        using namespace jtx;

        // IOU collateral: the issuer deep-freezes the buyer, then the seller.
        {
            Env env{*this, features};
            Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"},
                keeper{"keeper"};
            auto const clt = gw["CLT"];
            auto const usd = cash["USD"];
            env.fund(XRP(10000), gw, cash, seller, buyer, keeper);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.trust(clt(100000), seller, buyer);
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(pay(gw, seller, clt(5000)));
            env(pay(cash, buyer, usd(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const defaulted = repoID(env, seller);
            env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 200, 60));
            env.close();
            auto const closed = repoID(env, seller);
            env(create(seller, buyer, clt(1000), usd(900), 0, now + 100, now + 100000, 60));
            env.close();
            auto const pending = repoID(env, seller);
            env(create(seller, buyer, clt(1000), usd(900), 0, now + 100000, now + 200000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, defaulted));
            env(simple(jss::RepoAccept, buyer, closed));
            env.close();

            env(trust(gw, clt(0), buyer, tfSetFreeze | tfSetDeepFreeze));
            env.close();
            auto const buyerBefore = env.balance(buyer, clt.issue());
            env.close(std::chrono::seconds(400));
            env(simple(jss::RepoDefault, keeper, defaulted), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(exists(env, defaulted));
            BEAST_EXPECT(env.balance(buyer, clt.issue()) == buyerBefore);

            env(trust(gw, clt(0), seller, tfSetFreeze | tfSetDeepFreeze));
            env.close();
            env(simple(jss::RepoClose, seller, closed), Ter(tecFROZEN));
            env.close();
            env(simple(jss::RepoCancel, seller, pending), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(exists(env, closed));
            BEAST_EXPECT(exists(env, pending));
        }

        // MPT collateral: the issuer locks the buyer.
        {
            Env env{*this, features};
            Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"},
                keeper{"keeper"};
            auto const usd = cash["USD"];
            env.fund(XRP(10000), gw, cash, seller, buyer, keeper);
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(pay(cash, buyer, usd(5000)));
            env.close();
            MPTTester collateral(env, gw, {.holders = {seller, buyer}, .fund = false});
            collateral.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            collateral.authorize({.account = seller});
            collateral.authorize({.account = buyer});
            auto const mpt = collateral["MPT"];
            env(pay(gw, seller, mpt(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 200, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();
            collateral.set({.account = gw, .holder = buyer, .flags = tfMPTLock});
            env.close(std::chrono::seconds(400));
            env(simple(jss::RepoDefault, keeper, id), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(exists(env, id));
        }
    }

    // An integral cash asset must hold the repurchase amount at maturity: a
    // price whose interest would leave the XRP or MPT range is refused at
    // create, and one that fits is accepted.
    void
    testRepurchaseFitsTheCashAsset(FeatureBitset features)
    {
        testcase("the repurchase amount fits the cash asset");
        using namespace jtx;

        Env env{*this, features};
        Account const issuer{"issuer"}, gw{"gw"}, seller{"seller"};
        auto const clt = gw["CLT"];
        env.fund(XRP(10000), issuer, gw, seller);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.trust(clt(100000), seller, issuer);
        env.close();
        env(pay(gw, seller, clt(5000)));
        env.close();
        MPTTester cashMpt(env, issuer, {.holders = {seller}, .fund = false});
        cashMpt.create({.flags = tfMPTCanTransfer});
        cashMpt.authorize({.account = seller});
        MPTIssue const cashIssue{cashMpt.issuanceID()};

        auto const now = env.now().time_since_epoch().count();
        std::uint32_t const fullRate = kTenthBipsPerUnity.value();

        env(create(
                seller,
                issuer,
                XRP(1000),
                STAmount{cashIssue, kMaxMpTokenAmount - 1000},
                fullRate,
                now + 100,
                now + 100000,
                60),
            Ter(tecPRECISION_LOSS));
        env(create(
                seller,
                issuer,
                clt(1000),
                STAmount{XRPAmount{static_cast<std::int64_t>(STAmount::kMaxNativeN) - 1000}},
                fullRate,
                now + 100,
                now + 100000,
                60),
            Ter(tecPRECISION_LOSS));
        env.close();

        auto const id = repoID(env, seller);
        env(create(
            seller,
            issuer,
            XRP(1000),
            STAmount{cashIssue, kMaxMpTokenAmount / 2},
            fullRate,
            now + 100,
            now + 100000,
            60));
        env.close();
        BEAST_EXPECT(exists(env, id));

        // A maturity already past accrues nothing, so even a price at the edge
        // of the range fits.
        env.close(std::chrono::seconds(5000));
        auto const later = env.now().time_since_epoch().count();
        auto const lapsed = repoID(env, seller);
        env(create(
            seller,
            issuer,
            XRP(1000),
            STAmount{cashIssue, kMaxMpTokenAmount - 1000},
            fullRate,
            later - 2000,
            later - 1000,
            60));
        env.close();
        BEAST_EXPECT(exists(env, lapsed));
    }

    // The buyer cannot block the seller's repurchase. Under a cash issuer that
    // requires authorization, a buyer without an authorized holding cannot
    // receive the cash, so RepoClose locks it in the repo for the buyer and
    // returns the collateral; the buyer collects once it can receive.
    void
    testCloseLocksCashTheBuyerCannotReceive(FeatureBitset features)
    {
        testcase("close locks cash the buyer cannot receive");
        using namespace jtx;

        // IOU cash: the buyer deletes its authorized line.
        {
            Env env{*this, features};
            Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
            auto const usd = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer, keeper);
            env.close();
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(usd(100000), seller, buyer);
            env.close();
            env(trust(gw, usd(0), seller, tfSetfAuth));
            env(trust(gw, usd(0), buyer, tfSetfAuth));
            env.close();
            env(pay(gw, buyer, usd(900)));
            env.close();

            auto const ownersBefore = ownerCount(env, seller);
            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, XRP(1000), usd(900), 0, now + 100, now + 1000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();

            env(trust(buyer, usd(0)));
            env.close();
            BEAST_EXPECT(!env.le(keylet::trustLine(buyer.id(), usd.issue())));

            auto const sellerXrpBefore = env.balance(seller);
            env(simple(jss::RepoClose, seller, id));
            env.close();

            // The collateral is back and the repurchase amount is locked.
            BEAST_EXPECT(env.balance(seller) > sellerXrpBefore + XRP(999));
            BEAST_EXPECT(env.balance(seller, usd.issue()) == usd(0));
            auto const closed = env.le(keylet::repo(id));
            if (!BEAST_EXPECT(closed))
                return;
            BEAST_EXPECT((*closed)[~sfAmount] == usd(900).value());

            // Nothing but the buyer's collection applies to a closed repo.
            env.close(std::chrono::seconds(2000));
            env(simple(jss::RepoDefault, keeper, id), Ter(tecNO_PERMISSION));
            env(simple(jss::RepoClose, seller, id), Ter(tecNO_PERMISSION));
            env(simple(jss::RepoClose, keeper, id), Ter(tecNO_PERMISSION));
            env(simple(jss::RepoCancel, seller, id), Ter(tecREPO_ACTIVE));
            env(simple(jss::RepoClose, buyer, id), Ter(tecNO_LINE));
            env.close();
            BEAST_EXPECT(exists(env, id));

            // Once the buyer can receive again it collects the exact amount.
            env.trust(usd(100000), buyer);
            env.close();
            env(trust(gw, usd(0), buyer, tfSetfAuth));
            env.close();
            env(simple(jss::RepoClose, buyer, id));
            env.close();
            BEAST_EXPECT(!exists(env, id));
            BEAST_EXPECT(env.balance(buyer, usd.issue()) == usd(900));
            BEAST_EXPECT(ownerCount(env, seller) == ownersBefore);
        }

        // MPT cash: the buyer deletes its MPToken, or the issuer revokes it.
        for (bool const revoke : {false, true})
        {
            Env env{*this, features};
            Account const issuer{"issuer"}, seller{"seller"}, buyer{"buyer"};
            env.fund(XRP(10000), issuer, seller, buyer);
            env.close();
            MPTTester cashMpt(env, issuer, {.holders = {seller, buyer}, .fund = false});
            cashMpt.create({.flags = tfMPTCanTransfer | tfMPTRequireAuth});
            cashMpt.authorize({.account = seller});
            cashMpt.authorize({.account = issuer, .holder = seller});
            cashMpt.authorize({.account = buyer});
            cashMpt.authorize({.account = issuer, .holder = buyer});
            auto const mpt = cashMpt["MPT"];
            env(pay(issuer, buyer, mpt(900)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, XRP(1000), mpt(900), 0, now + 100, now + 1000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();

            if (revoke)
            {
                cashMpt.authorize({.account = issuer, .holder = buyer, .flags = tfMPTUnauthorize});
            }
            else
            {
                cashMpt.authorize({.account = buyer, .flags = tfMPTUnauthorize});
            }

            env(simple(jss::RepoClose, seller, id));
            env.close();
            auto const closed = env.le(keylet::repo(id));
            if (!BEAST_EXPECT(closed))
                return;
            BEAST_EXPECT((*closed)[~sfAmount] == mpt(900).value());
            auto const sellerToken = env.le(keylet::mptoken(cashMpt.issuanceID(), seller.id()));
            BEAST_EXPECT(sellerToken && (*sellerToken)[~sfLockedAmount] == 900);

            env(simple(jss::RepoClose, buyer, id), Ter(tecNO_AUTH));
            env.close();

            if (!revoke)
                cashMpt.authorize({.account = buyer});
            cashMpt.authorize({.account = issuer, .holder = buyer});
            env(simple(jss::RepoClose, buyer, id));
            env.close();
            BEAST_EXPECT(!exists(env, id));
            BEAST_EXPECT(env.balance(buyer, mpt.asset()) == mpt(900));
        }
    }

    // The release is refused to a receiver the collateral issuer no longer
    // authorizes, and to a seller the issuer has locked, on close and cancel.
    void
    testReleaseRefusedToUnauthorizedOrLocked(FeatureBitset features)
    {
        testcase("release refused to an unauthorized or locked receiver");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, cash{"cash"}, seller{"seller"}, buyer{"buyer"}, keeper{"keeper"};
        auto const usd = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer, keeper);
        env.close();
        env.trust(usd(100000), seller, buyer);
        env.close();
        env(pay(cash, buyer, usd(5000)));
        env.close();

        // The issuer revokes the buyer's authorization after the accept.
        {
            MPTTester authMpt(env, gw, {.holders = {seller, buyer}, .fund = false});
            authMpt.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            authMpt.authorize({.account = seller});
            authMpt.authorize({.account = gw, .holder = seller});
            authMpt.authorize({.account = buyer});
            authMpt.authorize({.account = gw, .holder = buyer});
            auto const mpt = authMpt["MPT"];
            env(pay(gw, seller, mpt(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const id = repoID(env, seller);
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 200, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, id));
            env.close();
            authMpt.authorize({.account = gw, .holder = buyer, .flags = tfMPTUnauthorize});
            env.close(std::chrono::seconds(400));
            env(simple(jss::RepoDefault, keeper, id), Ter(tecNO_AUTH));
            env.close();
            BEAST_EXPECT(exists(env, id));
        }

        // The issuer locks the seller: neither the close nor the cancel can
        // return the collateral.
        {
            MPTTester lockable(env, gw, {.holders = {seller, buyer}, .fund = false});
            lockable.create({.flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            lockable.authorize({.account = seller});
            lockable.authorize({.account = buyer});
            auto const mpt = lockable["MPT"];
            env(pay(gw, seller, mpt(5000)));
            env.close();

            auto const now = env.now().time_since_epoch().count();
            auto const active = repoID(env, seller);
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100, now + 100000, 60));
            env.close();
            auto const pending = repoID(env, seller);
            env(create(seller, buyer, mpt(1000), usd(900), 0, now + 100000, now + 200000, 60));
            env.close();
            env(simple(jss::RepoAccept, buyer, active));
            env.close();

            lockable.set({.account = gw, .holder = seller, .flags = tfMPTLock});
            env(simple(jss::RepoClose, seller, active), Ter(tecLOCKED));
            env(simple(jss::RepoCancel, seller, pending), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(exists(env, active));
            BEAST_EXPECT(exists(env, pending));
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = jtx::testableAmendments();
        testEnabled(all);
        testCreateMalformed(all);
        testAcceptAndClose(all);
        testCancel(all);
        testDefault(all);
        testInterest(all);
        testInterestRateBound(all);
        testIouCollateral(all);
        testIouCollateralFrozen(all);
        testMptCollateral(all);
        testAccountDeleteBlocked(all);
        testRpc(all);
        testActionsNeedTheAmendmentAndAnEntry(all);
        testCreatePriceAndData(all);
        testCreateParties(all);
        testCreateReserve(all);
        testIouCollateralChecks(all);
        testMptCollateralChecks(all);
        testAcceptPayment(all);
        testLifecycleRefusals(all);
        testCloseInTheAcceptLedger(all);
        testDefaultAtTransferRate(all);
        testDefaultBuyerReserve(all);
        testLedgerEntryRequests(all);
        testCollateralReturnsWithoutTransferFee(all);
        testDeadlineFitsTheDateField(all);
        testCashLegTransferFee(all);
        testCloseAfterBuyerDropsCashHolding(all);
        testDefaultOpensMptHolding(all);
        testReleaseRechecksTheReceiver(all);
        testRepurchaseFitsTheCashAsset(all);
        testRepurchaseFitsTheCashAsset(all - fixCleanup3_5_0);
        testCloseLocksCashTheBuyerCannotReceive(all);
        testReleaseRefusedToUnauthorizedOrLocked(all);
    }
};

BEAST_DEFINE_TESTSUITE(Repo, app, xrpl);

}  // namespace xrpl::test
