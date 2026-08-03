#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/domain.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>

#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

namespace xrpl::test {
namespace detail {

static json::Value
rpf(jtx::Account const& src,
    jtx::Account const& dst,
    xrpl::test::jtx::MPT const& usd,
    std::vector<MPTID> const& numSrc)
{
    json::Value jv = json::ValueType::Object;
    jv[jss::command] = "ripple_path_find";
    jv[jss::source_account] = toBase58(src);

    if (!numSrc.empty())
    {
        auto& sc = (jv[jss::source_currencies] = json::ValueType::Array);
        json::Value j = json::ValueType::Object;
        for (auto const& id : numSrc)
        {
            j[jss::mpt_issuance_id] = to_string(id);
            sc.append(j);
        }
    }

    auto const d = toBase58(dst);
    jv[jss::destination_account] = d;

    json::Value& j = (jv[jss::destination_amount] = json::ValueType::Object);
    j[jss::mpt_issuance_id] = to_string(usd.mpt());
    j[jss::value] = "1";

    return jv;
}

}  // namespace detail

//------------------------------------------------------------------------------

class PathMPT_test : public beast::unit_test::Suite
{
    jtx::Env
    pathTestEnv()
    {
        // These tests were originally written with search parameters that are
        // different from the current defaults. This function creates an env
        // with the search parameters that the tests were written for.
        using namespace jtx;
        return Env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->pathSearchOld = 7;
            cfg->pathSearch = 7;
            cfg->pathSearchMax = 10;
            return cfg;
        }));
    }

public:
    void
    sourceCurrenciesLimit()
    {
        testcase("source currency limits");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(10'000), "alice", "bob", gw);

        MPT const usd =
            MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 100});

        auto& app = env.app();
        Resource::Charge loadType = Resource::kFeeReferenceRpc;
        Resource::Consumer c;

        RPC::JsonContext context{
            {.j = env.journal,
             .app = app,
             .loadType = loadType,
             .netOps = app.getOPs(),
             .ledgerMaster = app.getLedgerMaster(),
             .consumer = c,
             .role = Role::USER,
             .coro = {},
             .infoSub = {},
             .apiVersion = RPC::kApiVersionIfUnspecified},
            {},
            {}};
        json::Value result;
        Gate g;
        // Test RPC::Tuning::max_src_cur source currencies.
        std::vector<MPTID> numSrc;
        numSrc.reserve(RPC::Tuning::kMaxSrcCur);
        for (std::uint8_t i = 0; i < RPC::Tuning::kMaxSrcCur; ++i)
            numSrc.push_back(makeMptID(i, bob));
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = xrpl::test::detail::rpf(alice, bob, usd, numSrc);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_src_cur source currencies.
        numSrc.push_back(makeMptID(RPC::Tuning::kMaxSrcCur, bob));
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = xrpl::test::detail::rpf(alice, bob, usd, numSrc);
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(result.isMember(jss::error));

        // Test RPC::Tuning::max_auto_src_cur source currencies.
        numSrc.clear();
        for (auto i = 0; i < (RPC::Tuning::kMaxAutoSrcCur - 1); ++i)
        {
            auto curm = MPTTester({.env = env, .issuer = alice, .holders = {bob}});
            numSrc.push_back(curm.issuanceID());
        }
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = xrpl::test::detail::rpf(alice, bob, usd, {});
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(!result.isMember(jss::error));

        // Test more than RPC::Tuning::max_auto_src_cur source currencies.
        auto curm = MPTTester({.env = env, .issuer = alice, .holders = {bob}});
        app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
            context.params = xrpl::test::detail::rpf(alice, bob, usd, {});
            context.coro = coro;
            RPC::doCommand(context, result);
            g.signal();
        });
        BEAST_EXPECT(g.waitFor(5s));
        BEAST_EXPECT(result.isMember(jss::error));
    }

    void
    noDirectPathNoIntermediaryNoAlternatives()
    {
        testcase("no direct path no intermediary no alternatives");
        using namespace jtx;

        Env env = pathTestEnv();

        env.fund(XRP(10'000), "alice", "bob");

        auto usdm = MPTTester({.env = env, .issuer = "bob"});

        auto const result = findPaths(env, "alice", "bob", usdm(5));
        BEAST_EXPECT(std::get<0>(result).empty());
    }

    void
    directPathNoIntermediary()
    {
        testcase("direct path no intermediary");
        using namespace jtx;
        Env env = pathTestEnv();
        env.fund(XRP(10'000), "alice", "bob");

        MPT const usd = MPTTester({.env = env, .issuer = "alice", .holders = {"bob"}});

        STPathSet st;
        STAmount sa;
        std::tie(st, sa, std::ignore) = findPaths(env, "alice", "bob", usd(5));
        BEAST_EXPECT(st.empty());
        BEAST_EXPECT(equal(sa, usd(5)));
    }

    void
    paymentAutoPathFind()
    {
        testcase("payment auto path find");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        env.fund(XRP(10'000), "alice", "bob", gw);
        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        env(pay(gw, "alice", usd(70)));
        env(pay("alice", "bob", usd(24)));
        env.require(Balance("alice", usd(46)));
        env.require(Balance("bob", usd(24)));
    }

    void
    pathFind(bool const domainEnabled)
    {
        testcase(std::string("path find") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        env.fund(XRP(10'000), "alice", "bob", gw);
        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        env(pay(gw, "alice", usd(70)));
        env(pay(gw, "bob", usd(50)));

        std::optional<uint256> domainID;
        if (domainEnabled)
            domainID = setupDomain(env, {"alice", "bob", gw});

        STPathSet st;
        STAmount sa;
        STAmount da;
        std::tie(st, sa, da) = findPaths(
            env, "alice", "bob", usd(5), std::nullopt, std::nullopt, std::nullopt, domainID);
        // Note, a direct IOU payment will have "gateway" as alternative path
        // since IOU supports rippling
        BEAST_EXPECT(st.empty());
        BEAST_EXPECT(equal(sa, usd(5)));
        BEAST_EXPECT(equal(da, usd(5)));
    }

    void
    pathFindConsumeAll(bool const domainEnabled)
    {
        testcase(
            std::string("path find consume all") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;

        {
            Env env = pathTestEnv();
            auto const gw = Account("gateway");
            env.fund(XRP(10'000), "alice", "bob", "carol", gw);
            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {"bob", "carol"}});
            MPT const aud(makeMptID(0, gw));
            env(pay(gw, "carol", usd(100)));
            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {"alice", "bob", "carol", "gateway"});
                env(offer("carol", XRP(100), usd(100)), Domain(*domainID));
            }
            else
            {
                env(offer("carol", XRP(100), usd(100)));
            }
            env.close();

            STPathSet st;
            STAmount sa;
            STAmount da;
            std::tie(st, sa, da) = findPaths(
                env,
                "alice",
                "bob",
                aud(-1),
                std::optional<STAmount>(XRP(100'000'000)),
                std::nullopt,
                std::nullopt,
                domainID);
            BEAST_EXPECT(st.empty());
            std::tie(st, sa, da) = findPaths(
                env,
                "alice",
                "bob",
                usd(-1),
                std::optional<STAmount>(XRP(100'000'000)),
                std::nullopt,
                std::nullopt,
                domainID);
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getMPTID() == usd.issuanceID);
            }
            BEAST_EXPECT(sa == XRP(100));
            BEAST_EXPECT(equal(da, usd(100)));

            // if domain is used, finding path in the open offerbook will return
            // empty result
            if (domainEnabled)
            {
                std::tie(st, sa, da) = findPaths(
                    env,
                    "alice",
                    "bob",
                    Account("bob")["USD"](-1),
                    std::optional<STAmount>(XRP(1000000)),
                    std::nullopt,
                    std::nullopt);  // not specifying a domain
                BEAST_EXPECT(st.empty());
            }
        }
    }

    void
    alternativePathsConsumeBestTransfer(bool const domainEnabled)
    {
        testcase(
            std::string("alternative path consume best transfer") +
            (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        Env env = pathTestEnv();
        auto const gw = Account("gateway");
        auto const gw2 = Account("gateway2");
        env.fund(XRP(10'000), "alice", "bob", gw, gw2);
        MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {"alice", "bob"}});
        MPT const gw2Usd = MPTTester(
            {.env = env, .issuer = gw2, .holders = {"alice", "bob"}, .transferFee = 1'000});
        std::optional<uint256> domainID;
        if (domainEnabled)
        {
            domainID = setupDomain(env, {"alice", "bob", "gateway", "gateway2"});
            env(pay(gw, "alice", usd(70)), Domain(*domainID));
            env(pay(gw2, "alice", gw2Usd(70)), Domain(*domainID));
            env(pay("alice", "bob", usd(70)), Domain(*domainID));
        }
        else
        {
            env(pay(gw, "alice", usd(70)));
            env(pay(gw2, "alice", gw2Usd(70)));
            env(pay("alice", "bob", usd(70)));
        }
        env.require(Balance("alice", usd(0)));
        env.require(Balance("alice", gw2Usd(70)));
        env.require(Balance("bob", usd(70)));
        env.require(Balance("bob", gw2Usd(0)));
    }

    void
    receiveMax(bool const domainEnabled)
    {
        testcase(std::string("Receive max") + (domainEnabled ? " w/ " : " w/o ") + "domain");
        using namespace jtx;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const gw = Account("gw");
        {
            // XRP -> MPT receive max
            Env env = pathTestEnv();
            env.fund(XRP(10'000), alice, bob, charlie, gw);
            env.close();
            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, charlie}});
            env(pay(gw, charlie, usd(10)));
            env.close();
            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {alice, bob, charlie, gw});
                env(offer(charlie, XRP(10), usd(10)), Domain(*domainID));
            }
            else
            {
                env(offer(charlie, XRP(10), usd(10)));
            }
            env.close();
            auto [st, sa, da] = findPaths(
                env, alice, bob, usd(-1), XRP(100).value(), std::nullopt, std::nullopt, domainID);
            BEAST_EXPECT(sa == XRP(10));
            BEAST_EXPECT(equal(da, usd(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == gw.id() &&
                    pathElem.getMPTID() == usd.mpt());
            }
        }
        {
            // MPT -> XRP receive max
            Env env = pathTestEnv();
            env.fund(XRP(10'000), alice, bob, charlie, gw);
            env.close();
            MPT const usd = MPTTester({.env = env, .issuer = gw, .holders = {alice, bob, charlie}});
            env(pay(gw, alice, usd(10)));
            env.close();
            std::optional<uint256> domainID;
            if (domainEnabled)
            {
                domainID = setupDomain(env, {alice, bob, charlie, gw});
                env(offer(charlie, usd(10), XRP(10)), Domain(*domainID));
            }
            else
            {
                env(offer(charlie, usd(10), XRP(10)));
            }
            env.close();
            auto [st, sa, da] = findPaths(
                env, alice, bob, drops(-1), usd(100).value(), std::nullopt, std::nullopt, domainID);
            BEAST_EXPECT(sa == usd(10));
            BEAST_EXPECT(equal(da, XRP(10)));
            if (BEAST_EXPECT(st.size() == 1 && st[0].size() == 1))
            {
                auto const& pathElem = st[0][0];
                BEAST_EXPECT(
                    pathElem.isOffer() && pathElem.getIssuerID() == xrpAccount() &&
                    pathElem.getCurrency() == xrpCurrency());
            }
        }
    }

    void
    run() override
    {
        sourceCurrenciesLimit();
        noDirectPathNoIntermediaryNoAlternatives();
        directPathNoIntermediary();
        paymentAutoPathFind();
        for (auto const domainEnabled : {false, true})
        {
            pathFind(domainEnabled);
            pathFindConsumeAll(domainEnabled);
            alternativePathsConsumeBestTransfer(domainEnabled);
            receiveMax(domainEnabled);
        }
    }
};

BEAST_DEFINE_TESTSUITE(PathMPT, app, xrpl);

}  // namespace xrpl::test
