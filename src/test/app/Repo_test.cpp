#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

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
            auto const USD = gw["USD"];
            env.fund(XRP(10000), gw, seller, buyer);
            env.close();
            auto const now = env.now().time_since_epoch().count();
            env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 200, 60),
                Ter(temDISABLED));
            env.close();
        }

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const ownersBefore = ownerCount(env, seller);
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 200, 60));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        auto const now = env.now().time_since_epoch().count();

        // The counterparty may not be the account.
        env(create(seller, seller, XRP(1000), USD(900), 0, now + 100, now + 200, 60),
            Ter(temMALFORMED));

        // Collateral and cash must differ: repaying in the locked asset would
        // let the seller settle with the very collateral that is immobilized.
        env(create(seller, buyer, XRP(1000), XRP(900), 0, now + 100, now + 200, 60),
            Ter(temBAD_AMOUNT));
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 200, 60));
        env.close();

        // Maturity must be after expiration.
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 300, now + 200, 60),
            Ter(temBAD_EXPIRATION));

        // Amounts must be positive.
        env(create(seller, buyer, XRP(0), USD(900), 0, now + 100, now + 200, 60),
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        // Collateral XRP, cash USD, zero interest so the repurchase is exact.
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 1000, 60));
        env.close();

        auto const sellerUsdBefore = env.balance(seller, USD.issue());

        // Only the pinned buyer may accept.
        env(simple(jss::RepoAccept, gw, id), Ter(tecNO_PERMISSION));

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        // The cash arrived and the repo is active.
        BEAST_EXPECT(env.balance(seller, USD.issue()) == sellerUsdBefore + USD(900));
        auto const sle = env.le(keylet::repo(id));
        BEAST_EXPECT(sle && sle->isFieldPresent(sfStartDate));

        // A second accept fails.
        env(simple(jss::RepoAccept, buyer, id), Ter(tecREPO_ACTIVE));

        // Only the seller may repurchase.
        env(simple(jss::RepoClose, buyer, id), Ter(tecNO_PERMISSION));

        auto const buyerUsdBefore = env.balance(buyer, USD.issue());
        env(simple(jss::RepoClose, seller, id));
        env.close();

        // Zero interest, so the buyer gets exactly the purchase price back and
        // the entry is gone.
        BEAST_EXPECT(env.balance(buyer, USD.issue()) == buyerUsdBefore + USD(900));
        BEAST_EXPECT(!exists(env, id));
    }

    void
    testCancel(FeatureBitset features)
    {
        testcase("cancel");
        using namespace jtx;

        Env env{*this, features};
        Account const gw{"gw"}, seller{"seller"}, buyer{"buyer"}, stranger{"stranger"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, stranger);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const ownersBefore = ownerCount(env, seller);
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 1000, 60));
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
        env(create(seller, buyer, XRP(1000), USD(900), 0, now2 + 10, now2 + 1000, 60));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, keeper);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), USD(900), 0, now + 100, now + 200, 60));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, USD(5000)));
        env(pay(gw, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        // kTenthBipsPerUnity is 100 percent annualized.
        env(create(
            seller,
            buyer,
            XRP(1000),
            USD(1000),
            kTenthBipsPerUnity.value(),
            now + 100,
            now + 400000,
            600));
        env.close();
        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        auto const start = (*env.le(keylet::repo(id)))[sfStartDate];
        auto const buyerBefore = env.balance(buyer, USD.issue());
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
        auto const paid = env.balance(buyer, USD.issue()).value() - buyerBefore.value();

        // Exactly the repurchase amount, so the rate's scale is pinned rather
        // than merely shown to be positive.
        BEAST_EXPECT(paid == STAmount(USD.issue(), expected));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        env(create(
                seller,
                buyer,
                XRP(1000),
                USD(1000),
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
            USD(1000),
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
        auto const CLT = gw["CLT"];
        auto const USD = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env.trust(CLT(100000), seller, buyer);
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, CLT(5000)));
        env(pay(cash, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();

        // Without the issuer's locking opt-in the collateral cannot be locked.
        env(create(seller, buyer, CLT(1000), USD(900), 0, now + 100, now + 1000, 60),
            Ter(tecNO_PERMISSION));
        env.close();

        env(fset(gw, asfAllowTrustLineLocking));
        env.close();

        auto const id = repoID(env, seller);
        env(create(seller, buyer, CLT(1000), USD(900), 0, now + 100, now + 1000, 60));
        env.close();
        BEAST_EXPECT(exists(env, id));

        // The collateral left the seller's spendable balance.
        BEAST_EXPECT(env.balance(seller, CLT.issue()) == CLT(4000));

        env(simple(jss::RepoAccept, buyer, id));
        env.close();

        env(simple(jss::RepoClose, seller, id));
        env.close();

        // Closing returns the collateral and the entry is gone.
        BEAST_EXPECT(!exists(env, id));
        BEAST_EXPECT(env.balance(seller, CLT.issue()) == CLT(5000));
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
        auto const CLT = gw["CLT"];
        auto const USD = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(CLT(100000), seller, buyer);
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, seller, CLT(5000)));
        env(pay(cash, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();

        // Freezing the buyer blocks the create, because the buyer is where the
        // collateral goes if the seller defaults.
        env(trust(gw, CLT(100000), buyer, tfSetFreeze));
        env.close();
        env(create(seller, buyer, CLT(1000), USD(900), 0, now + 100, now + 1000, 60),
            Ter(tecFROZEN));
        env.close();

        env(trust(gw, CLT(100000), buyer, tfClearFreeze));
        env.close();

        // And freezing the seller blocks it too.
        env(trust(gw, CLT(100000), seller, tfSetFreeze));
        env.close();
        env(create(seller, buyer, CLT(1000), USD(900), 0, now + 100, now + 1000, 60),
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
        auto const USD = cash["USD"];
        env.fund(XRP(10000), gw, cash, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(cash, buyer, USD(5000)));
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
            env(create(seller, buyer, mpt(1000), USD(900), 0, now + 100, now + 1000, 60),
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
        env(create(seller, buyer, mpt(1000), USD(900), 0, now + 100, now + 1000, 60));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer, sink);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();
        env(pay(gw, buyer, USD(5000)));
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), USD(1000), 0, now + 100, now + 100000, 600));
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
        BEAST_EXPECT(env.balance(seller, USD.issue()) == USD(0));

        env(trust(seller, USD(0)));
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
        auto const USD = gw["USD"];
        env.fund(XRP(10000), gw, seller, buyer);
        env.close();
        env.trust(USD(100000), seller, buyer);
        env.close();

        auto const now = env.now().time_since_epoch().count();
        auto const id = repoID(env, seller);
        env(create(seller, buyer, XRP(1000), USD(1000), 500, now + 100, now + 100000, 600));
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
    }
};

BEAST_DEFINE_TESTSUITE(Repo, app, xrpl);

}  // namespace xrpl::test
