#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/attester.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::test {

// SEnv class - encapsulate jtx::Env to make it more user-friendly,
// for example having APIs that return a *this reference so that calls can be
// chained (fluent interface) allowing to create an environment and use it
// without encapsulating it in a curly brace block.
// ---------------------------------------------------------------------------
template <class T>
struct SEnv
{
    jtx::Env env;

    SEnv(
        T& s,
        std::unique_ptr<Config> config,
        FeatureBitset features,
        std::unique_ptr<Logs> logs = nullptr,
        beast::Severity thresh = beast::Severity::Error)
        : env(s, std::move(config), features, std::move(logs), thresh)
    {
    }

    SEnv&
    close()
    {
        env.close();
        return *this;
    }

    SEnv&
    enableFeature(uint256 const feature)
    {
        env.enableFeature(feature);
        return *this;
    }

    SEnv&
    disableFeature(uint256 const feature)
    {
        env.app().config().features.erase(feature);
        return *this;
    }

    template <class Arg, class... Args>
    SEnv&
    fund(STAmount const& amount, Arg const& arg, Args const&... args)
    {
        env.fund(amount, arg, args...);
        return *this;
    }

    template <class JsonValue, class... FN>
    SEnv&
    tx(JsonValue&& jv, FN const&... fN)
    {
        env(std::forward<JsonValue>(jv), fN...);
        return *this;
    }

    template <class... FN>
    SEnv&
    multiTx(jtx::JValueVec const& jvv, FN const&... fN)
    {
        for (auto const& jv : jvv)
            env(jv, fN...);
        return *this;
    }

    [[nodiscard]] TER
    ter() const
    {
        return env.ter();
    }

    [[nodiscard]] STAmount
    balance(jtx::Account const& account) const
    {
        return env.balance(account).value();
    }

    [[nodiscard]] STAmount
    balance(jtx::Account const& account, Issue const& issue) const
    {
        return env.balance(account, issue).value();
    }

    XRPAmount
    reserve(std::uint32_t count)
    {
        return env.current()->fees().accountReserve(count);
    }

    XRPAmount
    txFee()
    {
        return env.current()->fees().base;
    }

    std::shared_ptr<SLE const>
    account(jtx::Account const& account)
    {
        return env.le(account);
    }

    std::shared_ptr<SLE const>
    bridge(json::Value const& jvb)
    {
        STXChainBridge const b(jvb);

        auto tryGet = [&](STXChainBridge::ChainType ct) -> std::shared_ptr<SLE const> {
            if (auto r = env.le(keylet::bridge(b, ct)))
            {
                if ((*r)[sfXChainBridge] == b)
                    return r;
            }
            return nullptr;
        };
        if (auto r = tryGet(STXChainBridge::ChainType::Locking))
            return r;
        return tryGet(STXChainBridge::ChainType::Issuing);
    }

    std::uint64_t
    claimCount(json::Value const& jvb)
    {
        return (*bridge(jvb))[sfXChainAccountClaimCount];
    }

    std::uint64_t
    claimID(json::Value const& jvb)
    {
        return (*bridge(jvb))[sfXChainClaimID];
    }

    std::shared_ptr<SLE const>
    claimID(json::Value const& jvb, std::uint64_t seq)
    {
        return env.le(keylet::xChainClaimID(STXChainBridge(jvb), seq));
    }

    std::shared_ptr<SLE const>
    caClaimID(json::Value const& jvb, std::uint64_t seq)
    {
        return env.le(keylet::xChainCreateAccountClaimID(STXChainBridge(jvb), seq));
    }
};

// XEnv class used for XChain tests. The only difference with SEnv<T> is that it
// funds some default accounts, and that it enables `testable_amendments() |
// FeatureBitset{featureXChainBridge}` by default.
// -----------------------------------------------------------------------------
template <class T>
struct XEnv : public jtx::XChainBridgeObjects, public SEnv<T>
{
    XEnv(T& s, bool side = false) : SEnv<T>(s, jtx::envconfig(), features)
    {
        using namespace jtx;
        STAmount const xrpFunds{XRP(10000)};

        if (!side)
        {
            this->fund(xrpFunds, mcDoor, mcAlice, mcBob, mcCarol, mcGw);

            // Signer's list must match the attestation signers
            // env_(jtx::signers(mcDoor, quorum, signers));
            for (auto& s : signers)
                this->fund(xrpFunds, s.account);
        }
        else
        {
            this->fund(xrpFunds, scDoor, scAlice, scBob, scCarol, scGw, scAttester, scReward);

            for (auto& ra : payees)
                this->fund(xrpFunds, ra);

            for (auto& s : signers)
                this->fund(xrpFunds, s.account);

            // Signer's list must match the attestation signers
            // env_(jtx::signers(Account::kMaster, quorum, signers));
        }
        this->close();
    }
};

// Tracks the xrp balance for one account
template <class T>
struct Balance
{
    jtx::Account const& account;
    T& env;
    STAmount startAmount;

    Balance(T& env, jtx::Account const& account) : account(account), env(env)
    {
        startAmount = env.balance(account);
    }

    [[nodiscard]] STAmount
    diff() const
    {
        return env.balance(account) - startAmount;
    }
};

// Tracks the xrp balance for multiple accounts involved in a crosss-chain
// transfer
template <class T>
struct BalanceTransfer
{
    using balance = Balance<T>;

    balance from;
    balance to;
    balance payer;                         // pays the rewards
    std::vector<balance> reward_accounts;  // receives the reward
    XRPAmount txFees;

    BalanceTransfer(
        T& env,
        jtx::Account const& fromAcct,
        jtx::Account const& toAcct,
        jtx::Account const& payer,
        jtx::Account const* payees,
        size_t numPayees,
        bool withClaim)
        : from(env, fromAcct)
        , to(env, toAcct)
        , payer(env, payer)
        , reward_accounts([&]() {
            std::vector<balance> r;
            r.reserve(numPayees);
            for (size_t i = 0; i < numPayees; ++i)
                r.emplace_back(env, payees[i]);
            return r;
        }())
        , txFees(withClaim ? env.env.current()->fees().base : XRPAmount(0))
    {
    }

    BalanceTransfer(
        T& env,
        jtx::Account const& fromAcct,
        jtx::Account const& toAcct,
        jtx::Account const& payer,
        std::vector<jtx::Account> const& payees,
        bool withClaim)
        : BalanceTransfer(env, fromAcct, toAcct, payer, &payees[0], payees.size(), withClaim)
    {
    }

    [[nodiscard]] bool
    payeesReceived(STAmount const& reward) const
    {
        return std::all_of(reward_accounts.begin(), reward_accounts.end(), [&](balance const& b) {
            return b.diff() == reward;
        });
    }

    bool
    checkMostBalances(STAmount const& amt, STAmount const& reward)
    {
        return from.diff() == -amt && to.diff() == amt && payeesReceived(reward);
    }

    bool
    hasHappened(STAmount const& amt, STAmount const& reward, bool checkPayer = true)
    {
        auto rewardCost = multiply(reward, STAmount(reward_accounts.size()), reward.asset());
        return checkMostBalances(amt, reward) &&
            (!checkPayer || payer.diff() == -(rewardCost + txFees));
    }

    bool
    hasNotHappened()
    {
        return checkMostBalances(STAmount(0), STAmount(0)) &&
            payer.diff() <= txFees;  // could have paid fee for failed claim
    }
};

struct BridgeDef
{
    jtx::Account doorA;
    Issue issueA;
    jtx::Account doorB;
    Issue issueB;
    STAmount reward;
    STAmount minAccountCreate;
    uint32_t quorum;
    std::vector<jtx::Signer> const& signers;
    json::Value jvb;

    template <class ENV>
    void
    initBridge(ENV& mcEnv, ENV& scEnv)
    {
        jvb = bridge(doorA, issueA, doorB, issueB);

        auto const optAccountCreate = [&]() -> std::optional<STAmount> {
            if (issueA != xrpIssue() || issueB != xrpIssue())
                return {};
            return minAccountCreate;
        }();
        mcEnv.tx(bridgeCreate(doorA, jvb, reward, optAccountCreate))
            .tx(jtx::signers(doorA, quorum, signers))
            .close();

        scEnv.tx(bridgeCreate(doorB, jvb, reward, optAccountCreate))
            .tx(jtx::signers(doorB, quorum, signers))
            .close();
    }
};

struct XChain_test : public beast::unit_test::Suite, public jtx::XChainBridgeObjects
{
    XRPAmount
    reserve(std::uint32_t count)
    {
        return XEnv(*this).env.current()->fees().accountReserve(count);
    }

    XRPAmount
    txFee()
    {
        return XEnv(*this).env.current()->fees().base;
    }

    void
    testXChainBridgeExtraFields()
    {
        auto jBridge = createBridge(mcDoor)[sfXChainBridge.jsonName];
        bool exceptionPresent = false;
        try
        {
            exceptionPresent = false;
            [[maybe_unused]] STXChainBridge const testBridge1(jBridge);
        }
        catch (std::exception& ec)
        {
            exceptionPresent = true;
        }

        BEAST_EXPECT(!exceptionPresent);

        try
        {
            exceptionPresent = false;
            jBridge["Extra"] = 1;
            [[maybe_unused]] STXChainBridge const testBridge2(jBridge);
        }
        catch ([[maybe_unused]] std::exception& ec)
        {
            exceptionPresent = true;
        }

        BEAST_EXPECT(exceptionPresent);
    }

    void
    testXChainCreateBridge()
    {
        XRPAmount const res1 = reserve(1);

        using namespace jtx;
        testcase("Create Bridge");

        // Normal create_bridge => should succeed
        XEnv(*this).tx(createBridge(mcDoor)).close();

        // Bridge not owned by one of the door account.
        XEnv(*this).tx(createBridge(mcBob), Ter(temXCHAIN_BRIDGE_NONDOOR_OWNER));

        // Create twice on the same account
        XEnv(*this).tx(createBridge(mcDoor)).close().tx(createBridge(mcDoor), Ter(tecDUPLICATE));

        // Create USD bridge Alice -> Bob ... should succeed
        XEnv(*this).tx(
            createBridge(mcAlice, bridge(mcAlice, mcGw["USD"], mcBob, mcBob["USD"])),
            Ter(tesSUCCESS));

        // Create USD bridge, Alice is both the locking door and locking issue,
        // ... should fail.
        XEnv(*this).tx(
            createBridge(mcAlice, bridge(mcAlice, mcAlice["USD"], mcBob, mcBob["USD"])),
            Ter(temXCHAIN_BRIDGE_BAD_ISSUES));

        // Bridge where the two door accounts are equal.
        XEnv(*this).tx(
            createBridge(mcBob, bridge(mcBob, mcGw["USD"], mcBob, mcGw["USD"])),
            Ter(temXCHAIN_EQUAL_DOOR_ACCOUNTS));

        // Both door accounts are on the same chain. This is not allowed.
        // Although it doesn't violate any invariants, it's not a useful thing
        // to do and it complicates the "add claim" transactions.
        XEnv(*this)
            .tx(createBridge(mcAlice, bridge(mcAlice, mcGw["USD"], mcBob, mcBob["USD"])))
            .close()
            .tx(createBridge(mcBob, bridge(mcAlice, mcGw["USD"], mcBob, mcBob["USD"])),
                Ter(tecDUPLICATE))
            .close();

        // Create a bridge on an account with exactly enough balance to
        // meet the new reserve should succeed
        XEnv(*this)
            .fund(res1, mcuDoor)  // exact reserve for account + 1 object
            .close()
            .tx(createBridge(mcuDoor, jvub), Ter(tesSUCCESS));

        // Create a bridge on an account with no enough balance to meet the
        // new reserve
        XEnv(*this)
            .fund(res1 - 1, mcuDoor)  // just short of required reserve
            .close()
            .tx(createBridge(mcuDoor, jvub), Ter(tecINSUFFICIENT_RESERVE));

        // Reward amount is non-xrp
        XEnv(*this).tx(
            createBridge(mcDoor, jvb, mcUSD(1)), Ter(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT));

        // Reward amount is XRP and negative
        XEnv(*this).tx(createBridge(mcDoor, jvb, XRP(-1)), Ter(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT));

        // Reward amount is 1 xrp => should succeed
        XEnv(*this).tx(createBridge(mcDoor, jvb, XRP(1)), Ter(tesSUCCESS));

        // Min create amount is 1 xrp, mincreate is 1 xrp => should succeed
        XEnv(*this).tx(createBridge(mcDoor, jvb, XRP(1), XRP(1)), Ter(tesSUCCESS));

        // Min create amount is non-xrp
        XEnv(*this).tx(
            createBridge(mcDoor, jvb, XRP(1), mcUSD(100)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // Min create amount is zero (should fail, currently succeeds)
        XEnv(*this).tx(
            createBridge(mcDoor, jvb, XRP(1), XRP(0)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // Min create amount is negative
        XEnv(*this).tx(
            createBridge(mcDoor, jvb, XRP(1), XRP(-1)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // coverage test: BridgeCreate::preflight() - create bridge when feature
        // disabled.
        {
            Env env(*this, testableAmendments() - featureXChainBridge);
            env(createBridge(Account::kMaster, jvb), Ter(temDISABLED));
        }

        // coverage test: BridgeCreate::preclaim() returns tecNO_ISSUER.
        XEnv(*this).tx(
            createBridge(mcAlice, bridge(mcAlice, mcuAlice["USD"], mcBob, mcBob["USD"])),
            Ter(tecNO_ISSUER));

        // coverage test: create_bridge transaction with incorrect flag
        XEnv(*this).tx(createBridge(mcAlice, jvb), Txflags(tfFillOrKill), Ter(temINVALID_FLAG));

        // coverage test: create_bridge transaction with xchain feature disabled
        XEnv(*this)
            .disableFeature(featureXChainBridge)
            .tx(createBridge(mcAlice, jvb), Ter(temDISABLED));
    }

    void
    testXChainBridgeCreateConstraints()
    {
        /**
         * Bridge create constraints tests.
         *
         * Define the door's bridge asset collection as the collection of all
         * the issuing assets for which the door account is on the issuing chain
         * and all the locking assets for which the door account is on the
         * locking chain. (note: a door account can simultaneously be on an
         * issuing and locking chain). A new bridge is not a duplicate as long
         * as the new bridge asset collection does not contain any duplicate
         * currencies (even if the issuers differ).
         *
         * Create bridges:
         *
         *| Owner | Locking   | Issuing | Comment                           |
         *| a1    | a1 USD/GW | USD/B   |                                   |
         *| a2    | a2 USD/GW | USD/B   | Same locking & issuing assets     |
         *|       |           |         |                                   |
         *| a3    | a3 USD/GW | USD/a4  |                                   |
         *| a4    | a4 USD/GW | USD/a4  | Same bridge, different accounts   |
         *|       |           |         |                                   |
         *| B     | A USD/GW  | USD/B   |                                   |
         *| B     | A EUR/GW  | USD/B   | Fail: Same issuing asset          |
         *|       |           |         |                                   |
         *| A     | A USD/B   | USD/C   |                                   |
         *| A     | A USD/B   | EUR/B   | Fail: Same locking asset          |
         *| A     | A USD/C   | EUR/B   | Fail: Same locking asset currency |
         *|       |           |         |                                   |
         *| A     | A USD/GW  | USD/B   | Fail: Same bridge not allowed     |
         *| A     | B USD/GW  | USD/A   | Fail: "A" has USD already         |
         *| B     | A EUR/GW  | USD/B   | Fail:                             |
         *
         * Note that, now from sidechain's point of view, A is both
         * a local locking door and a foreign locking door on different
         * bridges. Txns such as commits specify bridge spec, but not the
         * local door account. So we test the transactors can figure out
         * the correct local door account from bridge spec.
         *
         * Commit to sidechain door accounts:
         *        | bridge spec | result
         * case 6 | A -> B      | B's balance increase
         * case 7 | C <- A      | A's balance increase
         *
         * We also test ModifyBridge txns modify correct bridges.
         */

        using namespace jtx;
        testcase("Bridge create constraints");
        XEnv env(*this, true);
        auto& a = scAlice;
        auto& b = scBob;
        auto& c = scCarol;
        auto ausd = a["USD"];
        auto busd = b["USD"];
        auto cusd = c["USD"];
        auto gusd = scGw["USD"];
        auto aeur = a["EUR"];
        auto beur = b["EUR"];
        auto ceur = c["EUR"];
        auto geur = scGw["EUR"];

        // Accounts to own single bridges
        Account const a1("a1");
        Account const a2("a2");
        Account const a3("a3");
        Account const a4("a4");
        Account const a5("a5");
        Account const a6("a6");

        env.fund(XRP(10000), a1, a2, a3, a4, a5, a6);
        env.close();

        // Add a bridge on two different accounts with the same locking and
        // issuing assets
        env.tx(createBridge(a1, bridge(a1, gusd, b, busd))).close();
        env.tx(createBridge(a2, bridge(a2, gusd, b, busd))).close();

        // Add the exact same bridge to two different accounts (one locking
        // account and one issuing)
        env.tx(createBridge(a3, bridge(a3, gusd, a4, a4["USD"]))).close();
        env.tx(createBridge(a4, bridge(a3, gusd, a4, a4["USD"])), Ter(tecDUPLICATE)).close();

        // Add the exact same bridge to two different accounts (one issuing
        // account and one locking - opposite order from the test above)
        env.tx(createBridge(a5, bridge(a6, gusd, a5, a5["USD"]))).close();
        env.tx(createBridge(a6, bridge(a6, gusd, a5, a5["USD"])), Ter(tecDUPLICATE)).close();

        // Test case 1 ~ 5, create bridges
        auto const goodBridge1 = bridge(a, gusd, b, busd);
        auto const goodBridge2 = bridge(a, busd, c, cusd);
        env.tx(createBridge(b, goodBridge1)).close();
        // Issuing asset is the same, this is a duplicate
        env.tx(createBridge(b, bridge(a, geur, b, busd)), Ter(tecDUPLICATE)).close();
        env.tx(createBridge(a, goodBridge2), Ter(tesSUCCESS)).close();
        // Locking asset is the same - this is a duplicate
        env.tx(createBridge(a, bridge(a, busd, b, beur)), Ter(tecDUPLICATE)).close();
        // Locking asset is USD - this is a duplicate even tho it has a
        // different issuer
        env.tx(createBridge(a, bridge(a, cusd, b, beur)), Ter(tecDUPLICATE)).close();

        // Test case 6 and 7, commits
        env.tx(trust(c, busd(1000)))
            .tx(trust(a, busd(1000)))
            .close()
            .tx(pay(b, c, busd(1000)))
            .close();
        auto const aBalanceStart = env.balance(a, busd);
        auto const cBalanceStart = env.balance(c, busd);
        env.tx(xchainCommit(c, goodBridge1, 1, busd(50))).close();
        BEAST_EXPECT(env.balance(a, busd) - aBalanceStart == busd(0));
        BEAST_EXPECT(env.balance(c, busd) - cBalanceStart == busd(-50));
        env.tx(xchainCommit(c, goodBridge2, 1, busd(60))).close();
        BEAST_EXPECT(env.balance(a, busd) - aBalanceStart == busd(60));
        BEAST_EXPECT(env.balance(c, busd) - cBalanceStart == busd(-50 - 60));

        // bridge modify test cases
        env.tx(bridgeModify(b, goodBridge1, XRP(33), std::nullopt)).close();
        BEAST_EXPECT((*env.bridge(goodBridge1))[sfSignatureReward] == XRP(33));
        env.tx(bridgeModify(a, goodBridge2, XRP(44), std::nullopt)).close();
        BEAST_EXPECT((*env.bridge(goodBridge2))[sfSignatureReward] == XRP(44));
    }

    void
    testXChainCreateBridgeMatrix()
    {
        using namespace jtx;
        testcase("Create Bridge Matrix");

        // Test all combinations of the following:`
        // --------------------------------------
        // - Locking chain is IOU with locking chain door account as issuer
        // - Locking chain is IOU with issuing chain door account that
        //   exists on the locking chain as issuer
        // - Locking chain is IOU with issuing chain door account that does
        //   not exists on the locking chain as issuer
        // - Locking chain is IOU with non-door account (that exists on the
        //   locking chain ledger) as issuer
        // - Locking chain is IOU with non-door account (that does not exist
        //   exists on the locking chain ledger) as issuer
        // - Locking chain is XRP
        // ---------------------------------------------------------------------
        // - Issuing chain is IOU with issuing chain door account as the
        //   issuer
        // - Issuing chain is IOU with locking chain door account (that
        //   exists on the issuing chain ledger) as the issuer
        // - Issuing chain is IOU with locking chain door account (that does
        //   not exist on the issuing chain ledger) as the issuer
        // - Issuing chain is IOU with non-door account (that exists on the
        //   issuing chain ledger) as the issuer
        // - Issuing chain is IOU with non-door account (that does not
        //   exists on the issuing chain ledger) as the issuer
        // - Issuing chain is XRP and issuing chain door account is not the
        //   root account
        // - Issuing chain is XRP and issuing chain door account is the root
        //   account
        // ---------------------------------------------------------------------
        // That's 42 combinations. The only combinations that should succeed
        // are:
        // - Locking chain is any IOU,
        // - Issuing chain is IOU with issuing chain door account as the
        // issuer
        //   Locking chain is XRP,
        // - Issuing chain is XRP with issuing chain is the root account.
        // ---------------------------------------------------------------------
        Account a("a"), b("b");
        Issue ia, ib;

        std::tuple lcs{
            std::make_pair(
                "Locking chain is IOU(locking chain door)",
                [&](auto& env, bool) {
                    a = mcDoor;
                    ia = mcDoor["USD"];
                }),
            std::make_pair(
                "Locking chain is IOU(issuing chain door funded on locking "
                "chain)",
                [&](auto& env, bool shouldFund) {
                    a = mcDoor;
                    ia = scDoor["USD"];
                    if (shouldFund)
                        env.fund(XRP(10000), scDoor);
                }),
            std::make_pair(
                "Locking chain is IOU(issuing chain door account unfunded "
                "on locking chain)",
                [&](auto& env, bool) {
                    a = mcDoor;
                    ia = scDoor["USD"];
                }),
            std::make_pair(
                "Locking chain is IOU(bob funded on locking chain)",
                [&](auto& env, bool) {
                    a = mcDoor;
                    ia = mcGw["USD"];
                }),
            std::make_pair(
                "Locking chain is IOU(bob unfunded on locking chain)",
                [&](auto& env, bool) {
                    a = mcDoor;
                    ia = mcuGw["USD"];
                }),
            std::make_pair("Locking chain is XRP", [&](auto& env, bool) {
                a = mcDoor;
                ia = xrpIssue();
            })};

        std::tuple ics{
            std::make_pair(
                "Issuing chain is IOU(issuing chain door account)",
                [&](auto& env, bool) {
                    b = scDoor;
                    ib = scDoor["USD"];
                }),
            std::make_pair(
                "Issuing chain is IOU(locking chain door funded on issuing "
                "chain)",
                [&](auto& env, bool shouldFund) {
                    b = scDoor;
                    ib = mcDoor["USD"];
                    if (shouldFund)
                        env.fund(XRP(10000), mcDoor);
                }),
            std::make_pair(
                "Issuing chain is IOU(locking chain door unfunded on "
                "issuing chain)",
                [&](auto& env, bool) {
                    b = scDoor;
                    ib = mcDoor["USD"];
                }),
            std::make_pair(
                "Issuing chain is IOU(bob funded on issuing chain)",
                [&](auto& env, bool) {
                    b = scDoor;
                    ib = mcGw["USD"];
                }),
            std::make_pair(
                "Issuing chain is IOU(bob unfunded on issuing chain)",
                [&](auto& env, bool) {
                    b = scDoor;
                    ib = mcuGw["USD"];
                }),
            std::make_pair(
                "Issuing chain is XRP and issuing chain door account is "
                "not the root account",
                [&](auto& env, bool) {
                    b = scDoor;
                    ib = xrpIssue();
                }),
            std::make_pair(
                "Issuing chain is XRP and issuing chain door account is "
                "the root account ",
                [&](auto& env, bool) {
                    b = Account::kMaster;
                    ib = xrpIssue();
                })};

        std::vector<std::pair<int, int>> expectedResult{
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {tesSUCCESS, tesSUCCESS},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {tecNO_ISSUER, tesSUCCESS},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {tesSUCCESS, tesSUCCESS},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {tecNO_ISSUER, tesSUCCESS},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {temXCHAIN_BRIDGE_BAD_ISSUES, temXCHAIN_BRIDGE_BAD_ISSUES},
            {tesSUCCESS, tesSUCCESS}};

        std::vector<std::tuple<TER, TER, bool>> testResult;

        auto testcase = [&](auto const& lc, auto const& ic) {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            lc.second(mcEnv, true);
            lc.second(scEnv, false);

            ic.second(mcEnv, false);
            ic.second(scEnv, true);

            auto const& expected = expectedResult[testResult.size()];

            mcEnv.tx(createBridge(a, bridge(a, ia, b, ib)), Ter(TER::fromInt(expected.first)));
            TER const mcTER = mcEnv.env.ter();

            scEnv.tx(createBridge(b, bridge(a, ia, b, ib)), Ter(TER::fromInt(expected.second)));
            TER const scTER = scEnv.env.ter();

            bool const pass = isTesSuccess(mcTER) && isTesSuccess(scTER);

            testResult.emplace_back(mcTER, scTER, pass);
        };

        auto applyIcs = [&](auto const& lc, auto const& ics) {
            std::apply([&](auto const&... ic) { (testcase(lc, ic), ...); }, ics);
        };

        std::apply([&](auto const&... lc) { (applyIcs(lc, ics), ...); }, lcs);

#if GENERATE_MTX_OUTPUT
        // optional output of matrix results in markdown format
        // ----------------------------------------------------
        std::string fname{std::tmpnam(nullptr)};
        fname += ".md";
        std::cout << "Markdown output for matrix test: " << fname << "\n";

        auto print_res = [](auto tup) -> std::string {
            std::string status =
                std::string(transToken(std::get<0>(tup))) + " / " + transToken(std::get<1>(tup));

            if (std::get<2>(tup))
                return status;
            else
            {
                // red
                return std::string("`") + status + "`";
            }
        };

        auto output_table = [&](auto print_res) {
            size_t test_idx = 0;
            std::string res;
            res.reserve(10000);  // should be enough :-)

            // first two header lines
            res += "|  `issuing ->` | ";
            std::apply([&](auto const&... ic) { ((res += ic.first, res += " | "), ...); }, ics);
            res += "\n";

            res += "| :--- | ";
            std::apply(
                [&](auto const&... ic) { (((void)ic.first, res += ":---: |  "), ...); }, ics);
            res += "\n";

            auto output = [&](auto const& lc, auto const& ic) {
                res += print_res(test_result[test_idx]);
                res += " | ";
                ++test_idx;
            };

            auto output_ics = [&](auto const& lc, auto const& ics) {
                res += "| ";
                res += lc.first;
                res += " | ";
                std::apply([&](auto const&... ic) { (output(lc, ic), ...); }, ics);
                res += "\n";
            };

            std::apply([&](auto const&... lc) { (output_ics(lc, ics), ...); }, lcs);

            return res;
        };

        std::ofstream(fname) << output_table(print_res);

        std::string ter_fname{std::tmpnam(nullptr)};
        std::cout << "ter output for matrix test: " << ter_fname << "\n";

        std::ofstream ofs(ter_fname);
        for (auto& t : test_result)
        {
            ofs << "{ " << std::string(transToken(std::get<0>(t))) << ", "
                << std::string(transToken(std::get<1>(t))) << "}\n,";
        }
#endif
    }

    void
    testXChainModifyBridge()
    {
        using namespace jtx;
        testcase("Modify Bridge");

        // Changing a non-existent bridge should fail
        XEnv(*this).tx(
            bridgeModify(
                mcAlice, bridge(mcAlice, mcGw["USD"], mcBob, mcBob["USD"]), XRP(2), std::nullopt),
            Ter(tecNO_ENTRY));

        // must change something
        // XEnv(*this)
        //    .tx(create_bridge(mcDoor, jvb, XRP(1), XRP(1)))
        //    .tx(bridge_modify(mcDoor, jvb, XRP(1), XRP(1)),
        //    Ter(temMALFORMED));

        // must change something
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb, XRP(1), XRP(1)))
            .close()
            .tx(bridgeModify(mcDoor, jvb, {}, {}), Ter(temMALFORMED));

        // Reward amount is non-xrp
        XEnv(*this).tx(
            bridgeModify(mcDoor, jvb, mcUSD(2), XRP(10)), Ter(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT));

        // Reward amount is XRP and negative
        XEnv(*this).tx(
            bridgeModify(mcDoor, jvb, XRP(-2), XRP(10)), Ter(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT));

        // Min create amount is non-xrp
        XEnv(*this).tx(
            bridgeModify(mcDoor, jvb, XRP(2), mcUSD(10)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // Min create amount is zero
        XEnv(*this).tx(
            bridgeModify(mcDoor, jvb, XRP(2), XRP(0)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // Min create amount is negative
        XEnv(*this).tx(
            bridgeModify(mcDoor, jvb, XRP(2), XRP(-10)),
            Ter(temXCHAIN_BRIDGE_BAD_MIN_ACCOUNT_CREATE_AMOUNT));

        // First check the regular claim process (without bridge_modify)
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
        }

        // Check that the reward paid from a claim Id was the reward when
        // the claim id was created, not the reward since the bridge was
        // modified.
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            // Now modify the reward on the bridge
            mcEnv.tx(bridgeModify(mcDoor, jvb, XRP(2), XRP(10))).close();
            scEnv.tx(bridgeModify(Account::kMaster, jvb, XRP(2), XRP(10))).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            // make sure the reward accounts indeed received the original
            // split reward (1 split 5 ways) instead of the updated 2 XRP.
            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
        }

        // Check that the signatures used to verify attestations and decide
        // if there is a quorum are the current signer's list on the door
        // account, not the signer's list that was in effect when the claim
        // id was created.
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            // change signers - claim should not be processed is the batch
            // is signed by original signers
            scEnv.tx(jtx::signers(Account::kMaster, quorum, alt_signers)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            // submit claim using outdated signers - should fail
            scEnv
                .multiTx(
                    claimAttestations(
                        scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers),
                    Ter(tecNO_PERMISSION))
                .close();
            if (withClaim)
            {
                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, amt, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            // make sure transfer has not happened as we sent attestations
            // using outdated signers
            BEAST_EXPECT(transfer.hasNotHappened());

            // submit claim using current signers - should succeed
            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, alt_signers))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            // make sure the transfer went through as we sent attestations
            // using new signers
            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum, false));
        }

        // coverage test: bridge_modify transaction with incorrect flag
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .close()
            .tx(bridgeModify(mcDoor, jvb, XRP(1), XRP(2)),
                Txflags(tfFillOrKill),
                Ter(temINVALID_FLAG));

        // coverage test: bridge_modify transaction with xchain feature
        // disabled
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .disableFeature(featureXChainBridge)
            .close()
            .tx(bridgeModify(mcDoor, jvb, XRP(1), XRP(2)), Ter(temDISABLED));

        // coverage test: bridge_modify return temSIDECHAIN_NONDOOR_OWNER;
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .close()
            .tx(bridgeModify(mcAlice, jvb, XRP(1), XRP(2)), Ter(temXCHAIN_BRIDGE_NONDOOR_OWNER));

        /**
         * test tfClearAccountCreateAmount flag in BridgeModify tx
         * -- tx has both minAccountCreateAmount and the flag, temMALFORMED
         * -- tx has the flag and also modifies signature reward, tesSUCCESS
         * -- XChainCreateAccountCommit tx fail after previous step
         */
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb, XRP(1), XRP(20)))
            .close()
            .tx(sidechainXchainAccountCreate(mcAlice, jvb, scuAlice, XRP(100), reward))
            .close()
            .tx(bridgeModify(mcDoor, jvb, {}, XRP(2)),
                Txflags(tfClearAccountCreateAmount),
                Ter(temMALFORMED))
            .close()
            .tx(bridgeModify(mcDoor, jvb, XRP(3), {}), Txflags(tfClearAccountCreateAmount))
            .close()
            .tx(sidechainXchainAccountCreate(mcAlice, jvb, scuBob, XRP(100), XRP(3)),
                Ter(tecXCHAIN_CREATE_ACCOUNT_DISABLED))
            .close();
    }

    void
    testXChainCreateClaimID()
    {
        using namespace jtx;
        XRPAmount const res1 = reserve(1);
        XRPAmount const fee = txFee();

        testcase("Create ClaimID");

        // normal bridge create for sanity check with the exact necessary
        // account balance
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .fund(res1, scuAlice)  // acct reserve + 1 object
            .close()
            .tx(xchainCreateClaimId(scuAlice, jvb, reward, mcAlice))
            .close();

        // check reward not deducted when claim id is created
        {
            XEnv xenv(*this, true);

            test::Balance const scAliceBal(xenv, scAlice);

            xenv.tx(createBridge(Account::kMaster, jvb))
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            BEAST_EXPECT(scAliceBal.diff() == -fee);
        }

        // Non-existent bridge
        XEnv(*this, true)
            .tx(xchainCreateClaimId(
                    scAlice, bridge(mcAlice, mcAlice["USD"], scBob, scBob["USD"]), reward, mcAlice),
                Ter(tecNO_ENTRY))
            .close();

        // Creating the new object would put the account below the reserve
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .fund(res1 - xrp_dust, scuAlice)  // barely not enough
            .close()
            .tx(xchainCreateClaimId(scuAlice, jvb, reward, mcAlice), Ter(tecINSUFFICIENT_RESERVE))
            .close();

        // The specified reward doesn't match the reward on the bridge (test
        // by giving the reward amount for the other side, as well as a
        // completely non-matching reward)
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .close()
            .tx(xchainCreateClaimId(scAlice, jvb, split_reward_quorum, mcAlice),
                Ter(tecXCHAIN_REWARD_MISMATCH))
            .close();

        // A reward amount that isn't XRP
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .close()
            .tx(xchainCreateClaimId(scAlice, jvb, mcUSD(1), mcAlice),
                Ter(temXCHAIN_BRIDGE_BAD_REWARD_AMOUNT))
            .close();

        // coverage test: xchain_create_claim_id transaction with incorrect
        // flag
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .close()
            .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice),
                Txflags(tfFillOrKill),
                Ter(temINVALID_FLAG))
            .close();

        // coverage test: xchain_create_claim_id transaction with xchain
        // feature disabled
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .disableFeature(featureXChainBridge)
            .close()
            .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice), Ter(temDISABLED))
            .close();
    }

    void
    testXChainCommit()
    {
        using namespace jtx;
        XRPAmount const res0 = reserve(0);
        XRPAmount const fee = txFee();

        testcase("Commit");

        // Commit to a non-existent bridge
        XEnv(*this).tx(xchainCommit(mcAlice, jvb, 1, one_xrp, scBob), Ter(tecNO_ENTRY));

        // check that reward not deducted when doing the commit
        {
            XEnv xenv(*this);

            test::Balance const aliceBal(xenv, mcAlice);
            auto const amt = XRP(1000);

            xenv.tx(createBridge(mcDoor, jvb))
                .close()
                .tx(xchainCommit(mcAlice, jvb, 1, amt, scBob))
                .close();

            STAmount const claimCost = amt;
            BEAST_EXPECT(aliceBal.diff() == -(claimCost + fee));
        }

        // Commit a negative amount
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .close()
            .tx(xchainCommit(mcAlice, jvb, 1, XRP(-1), scBob), Ter(temBAD_AMOUNT));

        // Commit an amount whose issue that does not match the expected
        // issue on the bridge (either LockingChainIssue or
        // IssuingChainIssue, depending on the chain).
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .close()
            .tx(xchainCommit(mcAlice, jvb, 1, mcUSD(100), scBob), Ter(temBAD_ISSUER));

        // Commit an amount that would put the sender below the required
        // reserve (if XRP)
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .fund(res0 + one_xrp - xrp_dust, mcuAlice)  // barely not enough
            .close()
            .tx(xchainCommit(mcuAlice, jvb, 1, one_xrp, scBob), Ter(tecUNFUNDED_PAYMENT));

        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .fund(
                res0 + one_xrp + xrp_dust,  // "xrp_dust" for tx fees
                mcuAlice)                   // exactly enough => should succeed
            .close()
            .tx(xchainCommit(mcuAlice, jvb, 1, one_xrp, scBob));

        // Commit an amount above the account's balance (for both XRP and
        // IOUs)
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .fund(res0, mcuAlice)  // barely not enough
            .close()
            .tx(xchainCommit(mcuAlice, jvb, 1, res0 + one_xrp, scBob), Ter(tecUNFUNDED_PAYMENT));

        auto jvbUsd = bridge(mcDoor, mcUSD, scGw, scUSD);

        // commit sent from iou issuer (mcGw) succeeds - should it?
        XEnv(*this)
            .tx(trust(mcDoor, mcUSD(10000)))  // door needs to have a trustline
            .tx(createBridge(mcDoor, jvbUsd))
            .close()
            .tx(xchainCommit(mcGw, jvbUsd, 1, mcUSD(1), scBob));

        // commit to a door account from the door account. This should fail.
        XEnv(*this)
            .tx(trust(mcDoor, mcUSD(10000)))  // door needs to have a trustline
            .tx(createBridge(mcDoor, jvbUsd))
            .close()
            .tx(xchainCommit(mcDoor, jvbUsd, 1, mcUSD(1), scBob), Ter(tecXCHAIN_SELF_COMMIT));

        // commit sent from mcAlice which has no IOU balance => should fail
        XEnv(*this)
            .tx(trust(mcDoor, mcUSD(10000)))  // door needs to have a trustline
            .tx(createBridge(mcDoor, jvbUsd))
            .close()
            .tx(xchainCommit(mcAlice, jvbUsd, 1, mcUSD(1), scBob), Ter(terNO_LINE));

        // commit sent from mcAlice which has no IOU balance => should fail
        // just changed the destination to scGw (which is the door account
        // and may not make much sense)
        XEnv(*this)
            .tx(trust(mcDoor, mcUSD(10000)))  // door needs to have a trustline
            .tx(createBridge(mcDoor, jvbUsd))
            .close()
            .tx(xchainCommit(mcAlice, jvbUsd, 1, mcUSD(1), scGw), Ter(terNO_LINE));

        // commit sent from mcAlice which has a IOU balance => should
        // succeed
        XEnv(*this)
            .tx(trust(mcDoor, mcUSD(10000)))
            .tx(trust(mcAlice, mcUSD(10000)))
            .close()
            .tx(pay(mcGw, mcAlice, mcUSD(10)))
            .tx(createBridge(mcDoor, jvbUsd))
            .close()
            //.tx(pay(mcAlice, mcDoor, mcUSD(10)));
            .tx(xchainCommit(mcAlice, jvbUsd, 1, mcUSD(10), scAlice));

        // coverage test: xchain_commit transaction with incorrect flag
        XEnv(*this)
            .tx(createBridge(mcDoor))
            .close()
            .tx(xchainCommit(mcAlice, jvb, 1, one_xrp, scBob),
                Txflags(tfFillOrKill),
                Ter(temINVALID_FLAG));

        // coverage test: xchain_commit transaction with xchain feature
        // disabled
        XEnv(*this)
            .tx(createBridge(mcDoor))
            .disableFeature(featureXChainBridge)
            .close()
            .tx(xchainCommit(mcAlice, jvb, 1, one_xrp, scBob), Ter(temDISABLED));
    }

    void
    testXChainAddAttestation()
    {
        using namespace jtx;

        testcase("Add Attestation");
        XRPAmount const res0 = reserve(0);
        XRPAmount fee = txFee();

        auto multiTtxFee = [&](std::uint32_t m) -> STAmount {
            return multiply(fee, STAmount(m), xrpIssue());
        };

        // Add an attestation to a claim id that has already reached quorum.
        // This should succeed and share in the reward.
        // note: this is true only when either:
        //       1. dest account is not specified, so transfer requires a claim
        //       2. or the extra attestation is sent in the same batch as the
        //          one reaching quorum
        for (auto withClaim : {true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);
            std::uint32_t const claimID = 1;

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester,
                    jvb,
                    mcAlice,
                    amt,
                    payees,
                    true,
                    claimID,
                    dst,
                    signers,
                    kUtXchainDefaultQuorum))
                .close();
            scEnv
                .tx(claimAttestation(
                    scAttester,
                    jvb,
                    mcAlice,
                    amt,
                    payees[kUtXchainDefaultQuorum],
                    true,
                    claimID,
                    dst,
                    signers[kUtXchainDefaultQuorum]))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
                BEAST_EXPECT(!scEnv.claimID(jvb, claimID));  // claim id deleted
                BEAST_EXPECT(scEnv.claimID(jvb) == claimID);
            }

            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_everyone));
        }

        // Test that signature weights are correctly handled. Assign
        // signature weights of 1,2,4,4 and a quorum of 7. Check that the
        // 4,4 signatures reach a quorum, the 1,2,4, reach a quorum, but the
        // 4,2, 4,1 and 1,2 do not.

        // 1,2,4 => should succeed
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            std::uint32_t const quorum7 = 7;
            std::vector<Signer> const signers = [] {
                static constexpr int kNumSigners = 4;
                std::uint32_t const weights[] = {1, 2, 4, 4};

                std::vector<Signer> result;
                result.reserve(kNumSigners);
                for (int i = 0; i < kNumSigners; ++i)
                {
                    using namespace std::literals;
                    auto const a = Account("signer_"s + std::to_string(i));
                    result.emplace_back(a, weights[i]);
                }
                return result;
            }();

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum7, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();
            std::uint32_t const claimID = 1;
            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);

            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[0], 3, withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers, 3))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            BEAST_EXPECT(!scEnv.claimID(jvb, 1));  // claim id deleted

            BEAST_EXPECT(transfer.hasHappened(amt, divide(reward, STAmount(3), reward.asset())));
        }

        // 4,4 => should succeed
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            std::uint32_t const quorum7 = 7;
            std::vector<Signer> const signers = [] {
                static constexpr int kNumSigners = 4;
                std::uint32_t const weights[] = {1, 2, 4, 4};

                std::vector<Signer> result;
                result.reserve(kNumSigners);
                for (int i = 0; i < kNumSigners; ++i)
                {
                    using namespace std::literals;
                    auto const a = Account("signer_"s + std::to_string(i));
                    result.emplace_back(a, weights[i]);
                }
                return result;
            }();
            STAmount const splitReward = divide(reward, STAmount(signers.size()), reward.asset());

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum7, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();
            std::uint32_t const claimID = 1;
            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);

            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[2], 2, withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers, 2, 2))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            BEAST_EXPECT(!scEnv.claimID(jvb, claimID));  // claim id deleted

            BEAST_EXPECT(transfer.hasHappened(amt, divide(reward, STAmount(2), reward.asset())));
        }

        // 1,2 => should fail
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            std::uint32_t const quorum7 = 7;
            std::vector<Signer> const signers = [] {
                static constexpr int kNumSigners = 4;
                std::uint32_t const weights[] = {1, 2, 4, 4};

                std::vector<Signer> result;
                result.reserve(kNumSigners);
                for (int i = 0; i < kNumSigners; ++i)
                {
                    using namespace std::literals;
                    auto const a = Account("signer_"s + std::to_string(i));
                    result.emplace_back(a, weights[i]);
                }
                return result;
            }();

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum7, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            std::uint32_t const claimID = 1;
            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[0], 2, withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers, 2))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, amt, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id still present
            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // 2,4 => should fail
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            std::uint32_t const quorum7 = 7;
            std::vector<Signer> const signers = [] {
                static constexpr int kNumSigners = 4;
                std::uint32_t const weights[] = {1, 2, 4, 4};

                std::vector<Signer> result;
                result.reserve(kNumSigners);
                for (int i = 0; i < kNumSigners; ++i)
                {
                    using namespace std::literals;
                    auto const a = Account("signer_"s + std::to_string(i));
                    result.emplace_back(a, weights[i]);
                }
                return result;
            }();

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum7, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            std::uint32_t const claimID = 1;
            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);

            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[1], 2, withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers, 2, 1))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, amt, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id still present
            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Confirm that account create transactions happen in the correct
        // order. If they reach quorum out of order they should not execute
        // until all the previous created transactions have occurred.
        // Re-adding an attestation should move funds.
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);
            auto const amt = XRP(1000);
            auto const amtPlusReward = amt + reward;

            {
                test::Balance const door(mcEnv, mcDoor);
                test::Balance const carol(mcEnv, mcCarol);

                mcEnv.tx(createBridge(mcDoor, jvb, reward, XRP(20)))
                    .close()
                    .tx(sidechainXchainAccountCreate(mcAlice, jvb, scuAlice, amt, reward))
                    .tx(sidechainXchainAccountCreate(mcBob, jvb, scuBob, amt, reward))
                    .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuCarol, amt, reward))
                    .close();

                BEAST_EXPECT(
                    door.diff() == (multiply(amtPlusReward, STAmount(3), xrpIssue()) - fee));
                BEAST_EXPECT(carol.diff() == -(amt + reward + fee));
            }

            scEnv.tx(createBridge(Account::kMaster, jvb, reward, XRP(20)))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();

            {
                // send first batch of account create attest for all 3
                // account create
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 2))
                    .multiTx(attCreateAcctVec(3, amt, scuCarol, 2))
                    .multiTx(attCreateAcctVec(2, amt, scuBob, 2))
                    .close();

                BEAST_EXPECT(door.diff() == STAmount(0));
                // att_create_acct_vec return vectors of size 2, so 2*3 txns
                BEAST_EXPECT(attester.diff() == -multiTtxFee(6));

                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 1));   // ca claim id present
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 2));   // ca claim id present
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 3));   // ca claim id present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count still 0
            }

            {
                // complete attestations for 2nd account create => should
                // not complete
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(2, amt, scuBob, 3, 2)).close();

                BEAST_EXPECT(door.diff() == STAmount(0));
                // att_create_acct_vec return vectors of size 3, so 3 txns
                BEAST_EXPECT(attester.diff() == -multiTtxFee(3));

                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 2));   // ca claim id present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count still 0
            }

            {
                // complete attestations for 3rd account create => should
                // not complete
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(3, amt, scuCarol, 3, 2)).close();

                BEAST_EXPECT(door.diff() == STAmount(0));
                // att_create_acct_vec return vectors of size 3, so 3 txns
                BEAST_EXPECT(attester.diff() == -multiTtxFee(3));

                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 3));   // ca claim id present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count still 0
            }

            {
                // complete attestations for 1st account create => account
                // should be created
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 3, 1)).close();

                BEAST_EXPECT(door.diff() == -amtPlusReward);
                // att_create_acct_vec return vectors of size 3, so 3 txns
                BEAST_EXPECT(attester.diff() == -multiTtxFee(3));
                BEAST_EXPECT(scEnv.balance(scuAlice) == amt);

                BEAST_EXPECT(!scEnv.caClaimID(jvb, 1));    // claim id 1 deleted
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 2));   // claim id 2 present
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 3));   // claim id 3 present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count now 1
            }

            {
                // resend attestations for 3rd account create => still
                // should not complete
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(3, amt, scuCarol, 3, 2)).close();

                BEAST_EXPECT(door.diff() == STAmount(0));
                // att_create_acct_vec return vectors of size 3, so 3 txns
                BEAST_EXPECT(attester.diff() == -multiTtxFee(3));

                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 2));   // claim id 2 present
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 3));   // claim id 3 present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count still 1
            }

            {
                // resend attestations for 2nd account create => account
                // should be created
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(2, amt, scuBob, 1)).close();

                BEAST_EXPECT(door.diff() == -amtPlusReward);
                BEAST_EXPECT(attester.diff() == -fee);
                BEAST_EXPECT(scEnv.balance(scuBob) == amt);

                BEAST_EXPECT(!scEnv.caClaimID(jvb, 2));    // claim id 2 deleted
                BEAST_EXPECT(!!scEnv.caClaimID(jvb, 3));   // claim id 3 present
                BEAST_EXPECT(scEnv.claimCount(jvb) == 2);  // claim count now 2
            }
            {
                // resend attestations for 3rc account create => account
                // should be created
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);

                scEnv.multiTx(attCreateAcctVec(3, amt, scuCarol, 1)).close();

                BEAST_EXPECT(door.diff() == -amtPlusReward);
                BEAST_EXPECT(attester.diff() == -fee);
                BEAST_EXPECT(scEnv.balance(scuCarol) == amt);

                BEAST_EXPECT(!scEnv.caClaimID(jvb, 3));    // claim id 3 deleted
                BEAST_EXPECT(scEnv.claimCount(jvb) == 3);  // claim count now 3
            }
        }

        // Check that creating an account with less than the minimum reserve
        // fails.
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            auto const amt = res0 - XRP(1);
            auto const amtPlusReward = amt + reward;

            mcEnv.tx(createBridge(mcDoor, jvb, reward, XRP(20))).close();

            {
                test::Balance const door(mcEnv, mcDoor);
                test::Balance const carol(mcEnv, mcCarol);

                mcEnv.tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, amt, reward)).close();

                BEAST_EXPECT(door.diff() == amtPlusReward);
                BEAST_EXPECT(carol.diff() == -(amtPlusReward + fee));
            }

            scEnv.tx(createBridge(Account::kMaster, jvb, reward, XRP(20)))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();

            test::Balance const attester(scEnv, scAttester);
            test::Balance const door(scEnv, Account::kMaster);

            scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 2)).close();
            BEAST_EXPECT(!!scEnv.caClaimID(jvb, 1));   // claim id present
            BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count is one less

            scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 2, 2)).close();
            BEAST_EXPECT(!scEnv.caClaimID(jvb, 1));    // claim id deleted
            BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count was incremented

            BEAST_EXPECT(attester.diff() == -multiTtxFee(4));
            BEAST_EXPECT(door.diff() == -reward);
            BEAST_EXPECT(!scEnv.account(scuAlice));
        }

        // Check that sending funds with an account create txn to an
        // existing account works.
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            auto const amt = XRP(111);
            auto const amtPlusReward = amt + reward;

            mcEnv.tx(createBridge(mcDoor, jvb, reward, XRP(20))).close();

            {
                test::Balance const door(mcEnv, mcDoor);
                test::Balance const carol(mcEnv, mcCarol);

                mcEnv.tx(sidechainXchainAccountCreate(mcCarol, jvb, scAlice, amt, reward)).close();

                BEAST_EXPECT(door.diff() == amtPlusReward);
                BEAST_EXPECT(carol.diff() == -(amtPlusReward + fee));
            }

            scEnv.tx(createBridge(Account::kMaster, jvb, reward, XRP(20)))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();

            test::Balance const attester(scEnv, scAttester);
            test::Balance const door(scEnv, Account::kMaster);
            test::Balance const alice(scEnv, scAlice);

            scEnv.multiTx(attCreateAcctVec(1, amt, scAlice, 2)).close();
            BEAST_EXPECT(!!scEnv.caClaimID(jvb, 1));   // claim id present
            BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count is one less

            scEnv.multiTx(attCreateAcctVec(1, amt, scAlice, 2, 2)).close();
            BEAST_EXPECT(!scEnv.caClaimID(jvb, 1));    // claim id deleted
            BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count was incremented

            BEAST_EXPECT(door.diff() == -amtPlusReward);
            BEAST_EXPECT(attester.diff() == -multiTtxFee(4));
            BEAST_EXPECT(alice.diff() == amt);
        }

        // Check that sending funds to an existing account with deposit auth
        // set fails for account create transactions.
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            auto const amt = XRP(1000);
            auto const amtPlusReward = amt + reward;

            mcEnv.tx(createBridge(mcDoor, jvb, reward, XRP(20))).close();

            {
                test::Balance const door(mcEnv, mcDoor);
                test::Balance const carol(mcEnv, mcCarol);

                mcEnv.tx(sidechainXchainAccountCreate(mcCarol, jvb, scAlice, amt, reward)).close();

                BEAST_EXPECT(door.diff() == amtPlusReward);
                BEAST_EXPECT(carol.diff() == -(amtPlusReward + fee));
            }

            scEnv.tx(createBridge(Account::kMaster, jvb, reward, XRP(20)))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(fset("scAlice", asfDepositAuth))  // set deposit auth
                .close();

            test::Balance const attester(scEnv, scAttester);
            test::Balance const door(scEnv, Account::kMaster);
            test::Balance const alice(scEnv, scAlice);

            scEnv.multiTx(attCreateAcctVec(1, amt, scAlice, 2)).close();
            BEAST_EXPECT(!!scEnv.caClaimID(jvb, 1));   // claim id present
            BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count is one less

            scEnv.multiTx(attCreateAcctVec(1, amt, scAlice, 2, 2)).close();
            BEAST_EXPECT(!scEnv.caClaimID(jvb, 1));    // claim id deleted
            BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count was incremented

            BEAST_EXPECT(door.diff() == -reward);
            BEAST_EXPECT(attester.diff() == -multiTtxFee(4));
            BEAST_EXPECT(alice.diff() == STAmount(0));
        }

        // If an account is unable to pay the reserve, check that it fails.
        // [greg todo] I don't know what this should test??

        // If an attestation already exists for that server and claim id,
        // the new attestation should replace the old attestation
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);
            auto const amt = XRP(1000);
            auto const amtPlusReward = amt + reward;

            {
                test::Balance const door(mcEnv, mcDoor);
                test::Balance const carol(mcEnv, mcCarol);

                mcEnv.tx(createBridge(mcDoor, jvb, reward, XRP(20)))
                    .close()
                    .tx(sidechainXchainAccountCreate(mcAlice, jvb, scuAlice, amt, reward))
                    .close()  // make sure Alice gets claim #1
                    .tx(sidechainXchainAccountCreate(mcBob, jvb, scuBob, amt, reward))
                    .close()  // make sure Bob gets claim #2
                    .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuCarol, amt, reward))
                    .close();  // and Carol will get claim #3

                BEAST_EXPECT(
                    door.diff() == (multiply(amtPlusReward, STAmount(3), xrpIssue()) - fee));
                BEAST_EXPECT(carol.diff() == -(amt + reward + fee));
            }

            std::uint32_t const redQuorum = 2;
            scEnv.tx(createBridge(Account::kMaster, jvb, reward, XRP(20)))
                .tx(jtx::signers(Account::kMaster, redQuorum, signers))
                .close();

            {
                test::Balance const attester(scEnv, scAttester);
                test::Balance const door(scEnv, Account::kMaster);
                auto const badAmt = XRP(10);
                std::uint32_t txCount = 0;

                // send attestations with incorrect amounts to for all 3
                // AccountCreate. They will be replaced later
                scEnv.multiTx(attCreateAcctVec(1, badAmt, scuAlice, 1))
                    .multiTx(attCreateAcctVec(2, badAmt, scuBob, 1, 2))
                    .multiTx(attCreateAcctVec(3, badAmt, scuCarol, 1, 1))
                    .close();
                txCount += 3;

                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 1), "claim id 1 created");
                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 2), "claim id 2 created");
                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 3), "claim id 3 created");

                // note: if we send inconsistent attestations in the same
                // batch, the transaction errors.

                // from now on we send correct attestations
                scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 1, 0))
                    .multiTx(attCreateAcctVec(2, amt, scuBob, 1, 2))
                    .multiTx(attCreateAcctVec(3, amt, scuCarol, 1, 4))
                    .close();
                txCount += 3;

                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 1), "claim id 1 still there");
                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 2), "claim id 2 still there");
                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 3), "claim id 3 still there");
                BEAST_EXPECTS(scEnv.claimCount(jvb) == 0, "No account created yet");

                scEnv.multiTx(attCreateAcctVec(3, amt, scuCarol, 1, 1)).close();
                txCount += 1;

                BEAST_EXPECTS(!!scEnv.caClaimID(jvb, 3), "claim id 3 still there");
                BEAST_EXPECTS(scEnv.claimCount(jvb) == 0, "No account created yet");

                scEnv.multiTx(attCreateAcctVec(1, amt, scuAlice, 1, 2)).close();
                txCount += 1;

                BEAST_EXPECTS(!scEnv.caClaimID(jvb, 1), "claim id 1 deleted");
                BEAST_EXPECTS(scEnv.claimCount(jvb) == 1, "scuAlice created");

                scEnv.multiTx(attCreateAcctVec(2, amt, scuBob, 1, 3))
                    .multiTx(
                        attCreateAcctVec(1, amt, scuAlice, 1, 3),
                        Ter(tecXCHAIN_ACCOUNT_CREATE_PAST))
                    .close();
                txCount += 2;

                BEAST_EXPECTS(!scEnv.caClaimID(jvb, 2), "claim id 2 deleted");
                BEAST_EXPECTS(!scEnv.caClaimID(jvb, 1), "claim id 1 not added");
                BEAST_EXPECTS(scEnv.claimCount(jvb) == 2, "scuAlice & scuBob created");

                scEnv.multiTx(attCreateAcctVec(3, amt, scuCarol, 1, 0)).close();
                txCount += 1;

                BEAST_EXPECTS(!scEnv.caClaimID(jvb, 3), "claim id 3 deleted");
                BEAST_EXPECTS(scEnv.claimCount(jvb) == 3, "All 3 accounts created");

                // because of the division of the rewards among attesters,
                // sometimes a couple drops are left over unspent in the
                // door account (here 2 drops)
                BEAST_EXPECT(
                    multiply(amtPlusReward, STAmount(3), xrpIssue()) + door.diff() < drops(3));
                BEAST_EXPECT(attester.diff() == -multiTtxFee(txCount));
                BEAST_EXPECT(scEnv.balance(scuAlice) == amt);
                BEAST_EXPECT(scEnv.balance(scuBob) == amt);
                BEAST_EXPECT(scEnv.balance(scuCarol) == amt);
            }
        }

        // If attestation moves funds, confirm the claim ledger objects are
        // removed (for both account create and "regular" transactions)
        // [greg] we do this in all attestation tests

        // coverage test: add_attestation transaction with incorrect flag
        {
            XEnv scEnv(*this, true);
            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, XRP(1000), payees[0], true, 1, {}, signers[0]),
                    Txflags(tfFillOrKill),
                    Ter(temINVALID_FLAG))
                .close();
        }

        // coverage test: add_attestation with xchain feature
        // disabled
        {
            XEnv scEnv(*this, true);
            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .disableFeature(featureXChainBridge)
                .close()
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, XRP(1000), payees[0], true, 1, {}, signers[0]),
                    Ter(temDISABLED))
                .close();
        }
    }

    void
    testXChainAddClaimNonBatchAttestation()
    {
        using namespace jtx;

        testcase("Add Non Batch Claim Attestation");

        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);
            std::uint32_t const claimID = 1;

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            BEAST_EXPECT(!!scEnv.claimID(jvb, claimID));  // claim id present

            Account const dst{scBob};
            auto const amt = XRP(1000);
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            auto const dstStartBalance = scEnv.env.balance(dst);

            for (int i = 0; i < signers.size(); ++i)
            {
                auto const att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[i], true, claimID, dst, signers[i]);

                TER const expectedTER = i < quorum ? tesSUCCESS : TER{tecXCHAIN_NO_CLAIM_ID};
                if (i + 1 == quorum)
                {
                    scEnv.tx(att, Ter(expectedTER)).close();
                }
                else
                {
                    scEnv.tx(att, Ter(expectedTER)).close();
                }

                if (i + 1 < quorum)
                {
                    BEAST_EXPECT(dstStartBalance == scEnv.env.balance(dst));
                }
                else
                {
                    BEAST_EXPECT(dstStartBalance + amt == scEnv.env.balance(dst));
                }
            }
            BEAST_EXPECT(dstStartBalance + amt == scEnv.env.balance(dst));
        }

        {
            /**
             * sfAttestationSignerAccount related cases.
             *
             * Good cases:
             * --G1: master key
             * --G2: regular key
             * --G3: public key and non-exist (unfunded) account match
             *
             * Bad cases:
             * --B1: disabled master key
             * --B2: single item signer list
             * --B3: public key and non-exist (unfunded) account mismatch
             * --B4: not on signer list
             * --B5: missing sfAttestationSignerAccount field
             */

            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;

            for (auto i = 0; i < kUtXchainDefaultNumSigners - 2; ++i)
                scEnv.fund(amt, alt_signers[i].account);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, alt_signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            Account const dst{scBob};
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();
            auto const dstStartBalance = scEnv.env.balance(dst);

            {
                // G1: master key
                auto att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[0], true, claimID, dst, alt_signers[0]);
                scEnv.tx(att).close();
            }
            {
                // G2: regular key
                // alt_signers[0] is the regular key of alt_signers[1]
                // There should be 2 attestations after the transaction
                scEnv.tx(jtx::regkey(alt_signers[1].account, alt_signers[0].account)).close();
                auto att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[1], true, claimID, dst, alt_signers[0]);
                att[sfAttestationSignerAccount.getJsonName()] = alt_signers[1].account.human();
                scEnv.tx(att).close();
            }
            {
                // B3: public key and non-exist (unfunded) account mismatch
                // G3: public key and non-exist (unfunded) account match
                auto const unfundedSigner1 = alt_signers[kUtXchainDefaultNumSigners - 1];
                auto const unfundedSigner2 = alt_signers[kUtXchainDefaultNumSigners - 2];
                auto att = claimAttestation(
                    scAttester,
                    jvb,
                    mcAlice,
                    amt,
                    payees[kUtXchainDefaultNumSigners - 1],
                    true,
                    claimID,
                    dst,
                    unfundedSigner1);
                att[sfAttestationSignerAccount.getJsonName()] = unfundedSigner2.account.human();
                scEnv.tx(att, Ter(tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR)).close();
                att[sfAttestationSignerAccount.getJsonName()] = unfundedSigner1.account.human();
                scEnv.tx(att).close();
            }
            {
                // B2: single item signer list
                std::vector<Signer> tempSignerList = {signers[0]};
                scEnv.tx(jtx::signers(alt_signers[2].account, 1, tempSignerList));
                auto att = claimAttestation(
                    scAttester,
                    jvb,
                    mcAlice,
                    amt,
                    payees[2],
                    true,
                    claimID,
                    dst,
                    tempSignerList.front());
                att[sfAttestationSignerAccount.getJsonName()] = alt_signers[2].account.human();
                scEnv.tx(att, Ter(tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR)).close();
            }
            {
                // B1: disabled master key
                scEnv.tx(fset(alt_signers[2].account, asfDisableMaster, 0)).close();
                auto att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[2], true, claimID, dst, alt_signers[2]);
                scEnv.tx(att, Ter(tecXCHAIN_BAD_PUBLIC_KEY_ACCOUNT_PAIR)).close();
            }
            {
                // --B4: not on signer list
                auto att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[0], true, claimID, dst, signers[0]);
                scEnv.tx(att, Ter(tecNO_PERMISSION)).close();
            }
            {
                // --B5: missing sfAttestationSignerAccount field
                // Then submit the one with the field. Should reach quorum.
                auto att = claimAttestation(
                    scAttester, jvb, mcAlice, amt, payees[3], true, claimID, dst, alt_signers[3]);
                att.removeMember(sfAttestationSignerAccount.getJsonName());
                scEnv.tx(att, Ter(temMALFORMED)).close();
                BEAST_EXPECT(dstStartBalance == scEnv.env.balance(dst));
                att[sfAttestationSignerAccount.getJsonName()] = alt_signers[3].account.human();
                scEnv.tx(att).close();
                BEAST_EXPECT(dstStartBalance + amt == scEnv.env.balance(dst));
            }
        }
    }

    void
    testXChainAddAccountCreateNonBatchAttestation()  // cspell: disable-line
    {
        using namespace jtx;

        testcase("Add Non Batch Account Create Attestation");

        XEnv mcEnv(*this);
        XEnv scEnv(*this, true);

        XRPAmount const txFee = mcEnv.txFee();

        Account const a{"a"};
        Account const doorA{"doorA"};

        STAmount const funds{XRP(10000)};
        mcEnv.fund(funds, a);
        mcEnv.fund(funds, doorA);

        Account const ua{"ua"};  // unfunded account we want to create

        BridgeDef xrpB{
            .doorA = doorA,
            .issueA = xrpIssue(),
            .doorB = Account::kMaster,
            .issueB = xrpIssue(),
            .reward = XRP(1),             // reward
            .minAccountCreate = XRP(20),  // minAccountCreate
            .quorum = 4,                  // quorum
            .signers = signers,
            .jvb = json::ValueType::Null};

        xrpB.initBridge(mcEnv, scEnv);

        auto const amt = XRP(777);
        auto const amtPlusReward = amt + xrpB.reward;
        {
            test::Balance const balDoorA(mcEnv, doorA);
            test::Balance const balA(mcEnv, a);

            mcEnv.tx(sidechainXchainAccountCreate(a, xrpB.jvb, ua, amt, xrpB.reward)).close();

            BEAST_EXPECT(balDoorA.diff() == amtPlusReward);
            BEAST_EXPECT(balA.diff() == -(amtPlusReward + txFee));
        }

        for (int i = 0; i < signers.size(); ++i)
        {
            auto const att = createAccountAttestation(
                signers[0].account,
                xrpB.jvb,
                a,
                amt,
                xrpB.reward,
                signers[i].account,
                true,
                1,
                ua,
                signers[i]);
            TER const expectedTER =
                i < xrpB.quorum ? tesSUCCESS : TER{tecXCHAIN_ACCOUNT_CREATE_PAST};

            scEnv.tx(att, Ter(expectedTER)).close();
            if (i + 1 < xrpB.quorum)
            {
                BEAST_EXPECT(!scEnv.env.le(ua));
            }
            else
            {
                BEAST_EXPECT(scEnv.env.le(ua));
            }
        }
        BEAST_EXPECT(scEnv.env.le(ua));
    }

    void
    testXChainClaim()
    {
        using namespace jtx;

        XRPAmount const res0 = reserve(0);
        XRPAmount const fee = txFee();

        testcase("Claim");

        // Claim where the amount matches what is attested to, to an account
        // that exists, and there are enough attestations to reach a quorum
        // => should succeed
        // -----------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
        }

        // Claim with just one attestation signed by the Master key
        // => should not succeed
        // -----------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv
                .tx(createBridge(Account::kMaster, jvb))
                //.tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[0], 1, withClaim);

            jtx::Signer const masterSigner(Account::kMaster);
            scEnv
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, amt, payees[0], true, claimID, dst, masterSigner),
                    Ter(tecXCHAIN_NO_SIGNERS_LIST))
                .close();

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim with just one attestation signed by a regular key
        // associated to the master account
        // => should not succeed
        // -----------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv
                .tx(createBridge(Account::kMaster, jvb))
                //.tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(jtx::regkey(Account::kMaster, payees[0]))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv, Account::kMaster, scBob, scAlice, &payees[0], 1, withClaim);

            jtx::Signer const masterSigner(payees[0]);
            scEnv
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, amt, payees[0], true, claimID, dst, masterSigner),
                    Ter(tecXCHAIN_NO_SIGNERS_LIST))
                .close();

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim against non-existent bridge
        // ---------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            auto jvbUnknown = bridge(mcBob, xrpIssue(), Account::kMaster, xrpIssue());

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvbUnknown, reward, mcAlice), Ter(tecNO_ENTRY))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvbUnknown, claimID, amt, dst), Ter(tecNO_ENTRY))
                .close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);
            scEnv
                .tx(claimAttestation(
                        scAttester,
                        jvbUnknown,
                        mcAlice,
                        amt,
                        payees[0],
                        true,
                        claimID,
                        dst,
                        signers[0]),
                    Ter(tecNO_ENTRY))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvbUnknown, claimID, amt, scBob), Ter(tecNO_ENTRY))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim against non-existent claim id
        // -----------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);

            // attest using non-existent claim id
            scEnv
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, amt, payees[0], true, 999, dst, signers[0]),
                    Ter(tecXCHAIN_NO_CLAIM_ID))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // claim using non-existent claim id
                scEnv.tx(xchainClaim(scAlice, jvb, 999, amt, scBob), Ter(tecXCHAIN_NO_CLAIM_ID))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim against a claim id owned by another account
        // -------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // submit a claim transaction with the wrong account (scGw
                // instead of scAlice)
                scEnv.tx(xchainClaim(scGw, jvb, claimID, amt, scBob), Ter(tecXCHAIN_BAD_CLAIM_ID))
                    .close();
                BEAST_EXPECT(transfer.hasNotHappened());
            }
            else
            {
                BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
            }
        }

        // Claim against a claim id with no attestations
        // ---------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);

            // don't send any attestations

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, amt, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim against a claim id with attestations, but not enough to
        // make a quorum
        // --------------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);

            auto tooFew = quorum - 1;
            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers, tooFew))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, amt, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim id of zero
        // ----------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scAlice, payees, withClaim);

            scEnv
                .multiTx(
                    claimAttestations(scAttester, jvb, mcAlice, amt, payees, true, 0, dst, signers),
                    Ter(tecXCHAIN_NO_CLAIM_ID))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, 0, amt, scBob), Ter(tecXCHAIN_NO_CLAIM_ID))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim issue that does not match the expected issue on the bridge
        // (either LockingChainIssue or IssuingChainIssue, depending on the
        // chain). The claim id should already have enough attestations to
        // reach a quorum for this amount (for a different issuer).
        // ---------------------------------------------------------------------
        for (auto withClaim : {true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, scUSD(1000), scBob), Ter(temBAD_AMOUNT))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim to a destination that does not already exist on the chain
        // -----------------------------------------------------------------
        for (auto withClaim : {true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scuBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            scEnv
                .multiTx(claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scuBob), Ter(tecNO_DST)).close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim where the claim id owner does not have enough XRP to pay
        // the reward
        // ------------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();
            STAmount const hugeReward{XRP(20000)};
            BEAST_EXPECT(hugeReward > scEnv.balance(scAlice));

            scEnv.tx(createBridge(Account::kMaster, jvb, hugeReward))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, hugeReward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);

            if (withClaim)
            {
                scEnv
                    .multiTx(claimAttestations(
                        scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers))
                    .close();
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecUNFUNDED_PAYMENT))
                    .close();
            }
            else
            {
                auto txns = claimAttestations(
                    scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers);
                for (int i = 0; i < kUtXchainDefaultQuorum - 1; ++i)
                {
                    scEnv.tx(txns[i]).close();
                }
                scEnv.tx(txns.back());
                scEnv.close();
                // The attestation should succeed, because it adds an
                // attestation, but the claim should fail with insufficient
                // funds
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecUNFUNDED_PAYMENT))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Claim where the claim id owner has enough XRP to pay the reward,
        // but it would put his balance below the reserve
        // --------------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .fund(
                    res0 + reward,
                    scuAlice)  // just not enough because of fees
                .close()
                .tx(xchainCreateClaimId(scuAlice, jvb, reward, mcAlice),
                    Ter(tecINSUFFICIENT_RESERVE))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(scEnv, Account::kMaster, scBob, scuAlice, payees, withClaim);

            scEnv
                .tx(claimAttestation(
                        scAttester, jvb, mcAlice, amt, payees[0], true, claimID, dst, signers[0]),
                    Ter(tecXCHAIN_NO_CLAIM_ID))
                .close();
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv
                    .tx(xchainClaim(scuAlice, jvb, claimID, amt, scBob), Ter(tecXCHAIN_NO_CLAIM_ID))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Pay to an account with deposit auth set
        // ---------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(fset("scBob", asfDepositAuth))  // set deposit auth
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);
            auto txns = claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers);
            for (int i = 0; i < kUtXchainDefaultQuorum - 1; ++i)
            {
                scEnv.tx(txns[i]).close();
            }
            if (withClaim)
            {
                scEnv.tx(txns.back()).close();

                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecNO_PERMISSION))
                    .close();

                // the transfer failed, but check that we can still use the
                // claimID with a different account
                test::Balance const scCarolBal(scEnv, scCarol);

                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scCarol)).close();
                BEAST_EXPECT(scCarolBal.diff() == amt);
            }
            else
            {
                scEnv.tx(txns.back()).close();
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecNO_PERMISSION))
                    .close();
                // A way would be to remove deposit auth and resubmit the
                // attestations (even though the witness servers won't do
                // it)
                scEnv
                    .tx(fset("scBob", 0, asfDepositAuth))  // clear deposit auth
                    .close();

                test::Balance const scBobBal(scEnv, scBob);
                scEnv.tx(txns.back()).close();
                BEAST_EXPECT(scBobBal.diff() == amt);
            }
        }

        // Pay to an account with Destination Tag set
        // ------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(fset("scBob", asfRequireDest))  // set dest tag
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);
            auto txns = claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers);
            for (int i = 0; i < kUtXchainDefaultQuorum - 1; ++i)
            {
                scEnv.tx(txns[i]).close();
            }
            if (withClaim)
            {
                scEnv.tx(txns.back()).close();
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecDST_TAG_NEEDED))
                    .close();

                // the transfer failed, but check that we can still use the
                // claimID with a different account
                test::Balance const scCarolBal(scEnv, scCarol);

                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scCarol)).close();
                BEAST_EXPECT(scCarolBal.diff() == amt);
            }
            else
            {
                scEnv.tx(txns.back()).close();
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob), Ter(tecDST_TAG_NEEDED))
                    .close();
                // A way would be to remove the destination tag requirement
                // and resubmit the attestations (even though the witness
                // servers won't do it)
                scEnv
                    .tx(fset("scBob", 0, asfRequireDest))  // clear dest tag
                    .close();

                test::Balance const scBobBal(scEnv, scBob);

                scEnv.tx(txns.back()).close();
                BEAST_EXPECT(scBobBal.diff() == amt);
            }
        }

        // Pay to an account with deposit auth set. Check that the attestations
        // are still validated and that we can used the claimID to transfer the
        // funds to a different account (which doesn't have deposit auth set)
        // --------------------------------------------------------------------
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(fset("scBob", asfDepositAuth))  // set deposit auth
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            // we should be able to submit the attestations, but the transfer
            // should not occur because dest account has deposit auth set
            test::Balance const scBobBal(scEnv, scBob);

            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers));
            BEAST_EXPECT(scBobBal.diff() == STAmount(0));

            // Check that check that we still can use the claimID to transfer
            // the amount to a different account
            test::Balance const scCarolBal(scEnv, scCarol);

            scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scCarol)).close();
            BEAST_EXPECT(scCarolBal.diff() == amt);
        }

        // Claim where the amount different from what is attested to
        // ---------------------------------------------------------
        for (auto withClaim : {true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);
            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers));
            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // claim wrong amount
                scEnv
                    .tx(xchainClaim(scAlice, jvb, claimID, one_xrp, scBob),
                        Ter(tecXCHAIN_CLAIM_NO_QUORUM))
                    .close();
            }

            BEAST_EXPECT(transfer.hasNotHappened());
        }

        // Verify that rewards are paid from the account that owns the claim
        // id
        // --------------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);
            test::Balance const scAliceBal(scEnv, scAlice);
            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers));

            STAmount claimCost = reward;

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
                claimCost += fee;
            }

            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
            BEAST_EXPECT(scAliceBal.diff() == -claimCost);  // because reward % 4 == 0
        }

        // Verify that if a reward is not evenly divisible among the reward
        // accounts, the remaining amount goes to the claim id owner.
        // ----------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb, tiny_reward)).close();

            scEnv.tx(createBridge(Account::kMaster, jvb, tiny_reward))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, tiny_reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum,
                withClaim);
            test::Balance const scAliceBal(scEnv, scAlice);
            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers));
            STAmount claimCost = tiny_reward;

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
                claimCost += fee;
            }

            BEAST_EXPECT(transfer.hasHappened(amt, tiny_reward_split));
            BEAST_EXPECT(scAliceBal.diff() == -(claimCost - tiny_reward_remainder));
        }

        // If a reward distribution fails for one of the reward accounts
        // (the reward account doesn't exist or has deposit auth set), then
        // the txn should still succeed, but that portion should go to the
        // claim id owner.
        // -------------------------------------------------------------------
        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();

            std::vector<Account> altPayees{payees.begin(), payees.end() - 1};
            altPayees.back() = Account("inexistent");

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum - 1,
                withClaim);
            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, altPayees, true, claimID, dst, signers));

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            // this also checks that only 3 * split_reward was deducted from
            // scAlice (the payer account), since we passed alt_payees to
            // BalanceTransfer
            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));
        }

        for (auto withClaim : {false, true})
        {
            XEnv mcEnv(*this);
            XEnv scEnv(*this, true);

            mcEnv.tx(createBridge(mcDoor, jvb)).close();
            auto& unpaid = payees[kUtXchainDefaultQuorum - 1];
            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .tx(fset(unpaid, asfDepositAuth))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            auto dst(withClaim ? std::nullopt : std::optional<Account>{scBob});
            auto const amt = XRP(1000);
            std::uint32_t const claimID = 1;
            mcEnv.tx(xchainCommit(mcAlice, jvb, claimID, amt, dst)).close();

            // balance of last signer should not change (has deposit auth)
            test::Balance const lastSigner(scEnv, unpaid);

            // make sure all signers except the last one get the
            // split_reward

            BalanceTransfer transfer(
                scEnv,
                Account::kMaster,
                scBob,
                scAlice,
                &payees[0],
                kUtXchainDefaultQuorum - 1,
                withClaim);
            scEnv.multiTx(claimAttestations(
                scAttester, jvb, mcAlice, amt, payees, true, claimID, dst, signers));

            if (withClaim)
            {
                BEAST_EXPECT(transfer.hasNotHappened());

                // need to submit a claim transactions
                scEnv.tx(xchainClaim(scAlice, jvb, claimID, amt, scBob)).close();
            }

            // this also checks that only 3 * split_reward was deducted from
            // scAlice (the payer account), since we passed payees.size() -
            // 1 to BalanceTransfer
            BEAST_EXPECT(transfer.hasHappened(amt, split_reward_quorum));

            // and make sure the account with deposit auth received nothing
            BEAST_EXPECT(lastSigner.diff() == STAmount(0));
        }

        // coverage test: xchain_claim transaction with incorrect flag
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .close()
            .tx(xchainClaim(scAlice, jvb, 1, XRP(1000), scBob),
                Txflags(tfFillOrKill),
                Ter(temINVALID_FLAG))
            .close();

        // coverage test: xchain_claim transaction with xchain feature
        // disabled
        XEnv(*this, true)
            .tx(createBridge(Account::kMaster, jvb))
            .disableFeature(featureXChainBridge)
            .close()
            .tx(xchainClaim(scAlice, jvb, 1, XRP(1000), scBob), Ter(temDISABLED))
            .close();

        // coverage test: XChainClaim::preclaim - isLockingChain = true;
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .close()
            .tx(xchainClaim(mcAlice, jvb, 1, XRP(1000), mcBob), Ter(tecXCHAIN_NO_CLAIM_ID));
    }

    void
    testXChainCreateAccount()
    {
        using namespace jtx;

        testcase("Bridge Create Account");
        XRPAmount const fee = txFee();

        // coverage test: transferHelper() - dst == src
        {
            XEnv scEnv(*this, true);

            auto const amt = XRP(111);
            auto const amtPlusReward = amt + reward;

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();

            test::Balance const door(scEnv, Account::kMaster);

            // scEnv.tx(att_create_acct_batch1(1, amt,
            // Account::kMaster)).close();
            scEnv.multiTx(attCreateAcctVec(1, amt, Account::kMaster, 2)).close();
            BEAST_EXPECT(!!scEnv.caClaimID(jvb, 1));   // claim id present
            BEAST_EXPECT(scEnv.claimCount(jvb) == 0);  // claim count is one less

            // scEnv.tx(att_create_acct_batch2(1, amt,
            // Account::kMaster)).close();
            scEnv.multiTx(attCreateAcctVec(1, amt, Account::kMaster, 2, 2)).close();
            BEAST_EXPECT(!scEnv.caClaimID(jvb, 1));    // claim id deleted
            BEAST_EXPECT(scEnv.claimCount(jvb) == 1);  // claim count was incremented

            BEAST_EXPECT(door.diff() == -reward);
        }

        // Check that creating an account with less than the minimum create
        // amount fails.
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);
            test::Balance const carol(mcEnv, mcCarol);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(19), reward),
                    Ter(tecXCHAIN_INSUFF_CREATE_AMOUNT))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
            BEAST_EXPECT(carol.diff() == -fee);
        }

        // Check that creating an account with invalid flags fails.
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(20), reward),
                    Txflags(tfFillOrKill),
                    Ter(temINVALID_FLAG))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
        }

        // Check that creating an account with the XChainBridge feature
        // disabled fails.
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv.disableFeature(featureXChainBridge)
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(20), reward),
                    Ter(temDISABLED))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
        }

        // Check that creating an account with a negative amount fails
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(-20), reward),
                    Ter(temBAD_AMOUNT))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
        }

        // Check that creating an account with a negative reward fails
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(20), XRP(-1)),
                    Ter(temBAD_AMOUNT))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
        }

        // Check that door account can't lock funds onto itself
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcDoor, jvb, scuAlice, XRP(20), XRP(1)),
                    Ter(tecXCHAIN_SELF_COMMIT))
                .close();

            BEAST_EXPECT(door.diff() == -fee);
        }

        // Check that reward matches the amount specified in bridge
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(20))).close();

            test::Balance const door(mcEnv, mcDoor);

            mcEnv
                .tx(sidechainXchainAccountCreate(mcCarol, jvb, scuAlice, XRP(20), XRP(2)),
                    Ter(tecXCHAIN_REWARD_MISMATCH))
                .close();

            BEAST_EXPECT(door.diff() == STAmount(0));
        }
    }

    void
    testFeeDipsIntoReserve()
    {
        using namespace jtx;
        XRPAmount const res0 = reserve(0);
        XRPAmount const fee = txFee();

        testcase("Fee dips into reserve");

        // commit where the fee dips into the reserve, this should succeed
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .fund(res0 + one_xrp + fee - drops(1), mcuAlice)
            .close()
            .tx(xchainCommit(mcuAlice, jvb, 1, one_xrp, scBob), Ter(tesSUCCESS));

        // commit where the commit amount drips into the reserve, this should
        // fail
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb))
            .fund(res0 + one_xrp - drops(1), mcuAlice)
            .close()
            .tx(xchainCommit(mcuAlice, jvb, 1, one_xrp, scBob), Ter(tecUNFUNDED_PAYMENT));

        auto const minAccountCreate = XRP(20);

        // account create commit where the fee dips into the reserve,
        // this should succeed
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb, reward, minAccountCreate))
            .fund(res0 + fee + minAccountCreate + reward - drops(1), mcuAlice)
            .close()
            .tx(sidechainXchainAccountCreate(mcuAlice, jvb, scuAlice, minAccountCreate, reward),
                Ter(tesSUCCESS));

        // account create commit where the commit dips into the reserve,
        // this should fail
        XEnv(*this)
            .tx(createBridge(mcDoor, jvb, reward, minAccountCreate))
            .fund(res0 + minAccountCreate + reward - drops(1), mcuAlice)
            .close()
            .tx(sidechainXchainAccountCreate(mcuAlice, jvb, scuAlice, minAccountCreate, reward),
                Ter(tecUNFUNDED_PAYMENT));
    }

    void
    testXChainDeleteDoor()
    {
        using namespace jtx;

        testcase("Bridge Delete Door Account");

        auto const acctDelFee{drops(XEnv(*this).env.current()->fees().increment)};

        // Deleting an account that owns bridge should fail
        {
            XEnv mcEnv(*this);

            mcEnv.tx(createBridge(mcDoor, jvb, XRP(1), XRP(1))).close();

            // We don't allow an account to be deleted if its sequence
            // number is within 256 of the current ledger.
            for (size_t i = 0; i < 256; ++i)
                mcEnv.close();

            // try to delete mcDoor, send funds to mcAlice
            mcEnv.tx(acctdelete(mcDoor, mcAlice), Fee(acctDelFee), Ter(tecHAS_OBLIGATIONS));
        }

        // Deleting an account that owns a claim id should fail
        {
            XEnv scEnv(*this, true);

            scEnv.tx(createBridge(Account::kMaster, jvb))
                .close()
                .tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice))
                .close();

            // We don't allow an account to be deleted if its sequence
            // number is within 256 of the current ledger.
            for (size_t i = 0; i < 256; ++i)
                scEnv.close();

            // try to delete scAlice, send funds to scBob
            scEnv.tx(acctdelete(scAlice, scBob), Fee(acctDelFee), Ter(tecHAS_OBLIGATIONS));
        }
    }

    void
    testBadPublicKey()
    {
        using namespace jtx;

        testcase("Bad attestations");
        {
            // Create a bridge and add an attestation with a bad public key
            XEnv scEnv(*this, true);
            std::uint32_t const claimID = 1;
            std::optional<Account> const dst{scBob};
            auto const amt = XRP(1000);
            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();
            scEnv.tx(xchainCreateClaimId(scAlice, jvb, reward, mcAlice)).close();
            auto jvAtt = claimAttestation(
                scAttester,
                jvb,
                mcAlice,
                amt,
                payees[kUtXchainDefaultQuorum],
                true,
                claimID,
                dst,
                signers[kUtXchainDefaultQuorum]);
            {
                // Change to an invalid keytype
                auto k = jvAtt["PublicKey"].asString();
                k.at(1) = '9';
                jvAtt["PublicKey"] = k;
            }
            scEnv.tx(jvAtt, Ter(temMALFORMED)).close();
        }
        {
            // Create a bridge and add an create account attestation with a bad
            // public key
            XEnv scEnv(*this, true);
            std::uint32_t const createCount = 1;
            Account const dst{scBob};
            auto const amt = XRP(1000);
            auto const rewardAmt = XRP(1);
            scEnv.tx(createBridge(Account::kMaster, jvb))
                .tx(jtx::signers(Account::kMaster, quorum, signers))
                .close();
            auto jvAtt = createAccountAttestation(
                scAttester,
                jvb,
                mcAlice,
                amt,
                rewardAmt,
                payees[kUtXchainDefaultQuorum],
                true,
                createCount,
                dst,
                signers[kUtXchainDefaultQuorum]);
            {
                // Change to an invalid keytype
                auto k = jvAtt["PublicKey"].asString();
                k.at(1) = '9';
                jvAtt["PublicKey"] = k;
            }
            scEnv.tx(jvAtt, Ter(temMALFORMED)).close();
        }
    }

    void
    run() override
    {
        testXChainBridgeExtraFields();
        testXChainCreateBridge();
        testXChainBridgeCreateConstraints();
        testXChainCreateBridgeMatrix();
        testXChainModifyBridge();
        testXChainCreateClaimID();
        testXChainCommit();
        testXChainAddAttestation();
        testXChainAddClaimNonBatchAttestation();
        testXChainAddAccountCreateNonBatchAttestation();  // cspell: disable-line
        testXChainClaim();
        testXChainCreateAccount();
        testFeeDipsIntoReserve();
        testXChainDeleteDoor();
        testBadPublicKey();
    }
};

// -----------------------------------------------------------
// -----------------------------------------------------------
struct XChainSim_test : public beast::unit_test::Suite, public jtx::XChainBridgeObjects
{
private:
    static constexpr size_t kNumSigners = 5;

    // --------------------------------------------------
    enum class WithClaim { No, Yes };
    struct Transfer
    {
        jtx::Account from;
        jtx::Account to;
        jtx::Account finaldest;
        STAmount amt;
        bool a2b;  // direction of transfer
        WithClaim with_claim{WithClaim::No};
        uint32_t claim_id{0};
        std::array<bool, kNumSigners> attested{};
    };

    struct AccountCreate
    {
        jtx::Account from;
        jtx::Account to;
        STAmount amt;
        STAmount reward;
        bool a2b;
        uint32_t claim_id{0};
        std::array<bool, kNumSigners> attested{};
    };

    using ENV = XEnv<XChainSim_test>;
    using BridgeID = BridgeDef const*;

    // tracking chain state
    // --------------------
    struct AccountStateTrack
    {
        STAmount startAmount{0};
        STAmount expectedDiff{0};

        void
        init(ENV& env, jtx::Account const& acct)
        {
            startAmount = env.balance(acct);
            expectedDiff = STAmount(0);
        }

        bool
        verify(ENV& env, jtx::Account const& acct) const
        {
            STAmount const diff{env.balance(acct) - startAmount};
            bool const check = diff == expectedDiff;
            return check;
        }
    };

    // --------------------------------------------------
    struct ChainStateTrack
    {
        using ClaimVec = jtx::JValueVec;
        using CreateClaimVec = jtx::JValueVec;
        using CreateClaimMap = std::map<uint32_t, CreateClaimVec>;

        ChainStateTrack(ENV& env) : env(env), tx_fee(env.env.current()->fees().base)
        {
        }

        void
        sendAttestations(size_t signerIdx, BridgeID bridge, ClaimVec& claims)
        {
            for (auto const& c : claims)
            {
                env.tx(c).close();
                spendFee(bridge->signers[signerIdx].account);
            }
            claims.clear();
        }

        uint32_t
        sendCreateAttestations(size_t signerIdx, BridgeID bridge, CreateClaimVec& claims)
        {
            size_t numSuccessful = 0;
            for (auto const& c : claims)
            {
                env.tx(c).close();
                if (env.ter() == tesSUCCESS)
                {
                    counters[bridge].signers.push_back(signerIdx);
                    numSuccessful++;
                }
                spendFee(bridge->signers[signerIdx].account);
            }
            claims.clear();
            return numSuccessful;
        }

        void
        sendAttestations()
        {
            bool callbackCalled = false;

            // we have this "do {} while" loop because we want to process
            // all the account create which can reach quorum at this time
            // stamp.
            do
            {
                callbackCalled = false;
                // cspell: ignore attns
                for (size_t i = 0; i < signers_attns.size(); ++i)
                {
                    for (auto& [bridge, claims] : signers_attns[i])
                    {
                        sendAttestations(i, bridge, claims.xfer_claims);

                        auto& c = counters[bridge];
                        auto& createClaims = claims.create_claims[c.claim_count];
                        auto numAttns = createClaims.size();
                        if (numAttns != 0u)
                        {
                            c.num_create_attn_sent +=
                                sendCreateAttestations(i, bridge, createClaims);
                        }
                        assert(claims.create_claims[c.claim_count].empty());
                    }
                }
                for (auto& [bridge, c] : counters)
                {
                    if (c.num_create_attn_sent >= bridge->quorum)
                    {
                        callbackCalled = true;
                        c.create_callbacks[c.claim_count](c.signers);
                        ++c.claim_count;
                        c.num_create_attn_sent = 0;
                        c.signers.clear();
                    }
                }
            } while (callbackCalled);
        }

        void
        init(jtx::Account const& acct)
        {
            accounts[acct].init(env, acct);
        }

        void
        receive(jtx::Account const& acct, STAmount amt, std::uint64_t divisor = 1)
        {
            if (amt.asset() != xrpIssue())
                return;
            auto it = accounts.find(acct);
            if (it == accounts.end())
            {
                accounts[acct].init(env, acct);
                // we just looked up the account, so expectedDiff == 0
            }
            else
            {
                it->second.expectedDiff +=
                    (divisor == 1 ? amt : divide(amt, STAmount(amt.asset(), divisor), amt.asset()));
            }
        }

        void
        spend(jtx::Account const& acct, STAmount amt, std::uint64_t times = 1)
        {
            if (amt.asset() != xrpIssue())
                return;
            receive(
                acct,
                times == 1 ? -amt : -multiply(amt, STAmount(amt.asset(), times), amt.asset()));
        }

        void
        transfer(jtx::Account const& from, jtx::Account const& to, STAmount amt)
        {
            spend(from, amt);
            receive(to, amt);
        }

        void
        spendFee(jtx::Account const& acct, size_t times = 1)
        {
            spend(acct, tx_fee, times);
        }

        [[nodiscard]] bool
        verify() const
        {
            for (auto const& [acct, state] : accounts)
            {
                if (!state.verify(env, acct))
                    return false;
            }
            return true;
        }

        struct BridgeCounters
        {
            using complete_cb = std::function<void(std::vector<size_t> const& signers)>;

            uint32_t claim_id{0};
            uint32_t create_count{0};  // for account create. First should be 1
            uint32_t claim_count{0};   // for account create. Increments after quorum for
                                       // current create_count (starts at 1) is reached.

            uint32_t num_create_attn_sent{0};  // for current claim_count
            std::vector<size_t> signers;
            std::vector<complete_cb> create_callbacks;
        };

        struct Claims
        {
            ClaimVec xfer_claims;
            CreateClaimMap create_claims;
        };

        using SignerAttns = std::unordered_map<BridgeID, Claims>;
        using SignersAttns = std::array<SignerAttns, kNumSigners>;

        ENV& env;
        std::map<jtx::Account, AccountStateTrack> accounts;
        SignersAttns signers_attns;
        std::map<BridgeID, BridgeCounters> counters;
        STAmount tx_fee;
    };

    struct ChainStateTracker
    {
        ChainStateTracker(ENV& aEnv, ENV& bEnv) : a(aEnv), b(bEnv)
        {
        }

        [[nodiscard]] bool
        verify() const
        {
            return a.verify() && b.verify();
        }

        void
        sendAttestations()
        {
            a.sendAttestations();
            b.sendAttestations();
        }

        void
        init(jtx::Account const& acct)
        {
            a.init(acct);
            b.init(acct);
        }

        ChainStateTrack a;
        ChainStateTrack b;
    };

    enum class SmState {
        Initial,
        ClaimIdCreated,
        Attesting,
        Attested,
        Completed,
        Closed,
    };

    enum class ActFlags { A2b = 1 << 0 };

    // --------------------------------------------------
    template <class T>
    class SmBase
    {
        SmBase(std::shared_ptr<ChainStateTracker> const& chainstate, BridgeDef const& bridge)
            : bridge_(bridge), st_(chainstate)
        {
        }

    public:
        ChainStateTrack&
        srcState()
        {
            return static_cast<T&>(*this).a2b() ? st_->a : st_->b;
        }

        ChainStateTrack&
        destState()
        {
            return static_cast<T&>(*this).a2b() ? st_->b : st_->a;
        }

        jtx::Account const&
        srcDoor()
        {
            return static_cast<T&>(*this).a2b() ? bridge_.doorA : bridge_.doorB;
        }

        jtx::Account const&
        dstDoor()
        {
            return static_cast<T&>(*this).a2b() ? bridge_.doorB : bridge_.doorA;
        }

    protected:
        BridgeDef const& bridge_;
        std::shared_ptr<ChainStateTracker> st_;

        friend T;
    };

    // --------------------------------------------------
    class SmCreateAccount : public SmBase<SmCreateAccount>
    {
    public:
        using Base = SmBase<SmCreateAccount>;

        SmCreateAccount(
            std::shared_ptr<ChainStateTracker> const& chainstate,
            BridgeDef const& bridge,
            AccountCreate create)
            : Base(chainstate, bridge), cr_(std::move(create))
        {
        }

        [[nodiscard]] bool
        a2b() const
        {
            return cr_.a2b;
        }

        uint32_t
        issueAccountCreate()
        {
            ChainStateTrack& st = srcState();
            jtx::Account const& srcdoor = srcDoor();

            st.env
                .tx(sidechainXchainAccountCreate(
                    cr_.from, bridge_.jvb, cr_.to, cr_.amt, cr_.reward))
                .close();  // needed for claim_id sequence to be correct'
            st.spendFee(cr_.from);
            st.transfer(cr_.from, srcdoor, cr_.amt);
            st.transfer(cr_.from, srcdoor, cr_.reward);

            return ++st.counters[&bridge_].create_count;
        }

        void
        attest(uint64_t time, uint32_t rnd)
        {
            ChainStateTrack& st = destState();

            // check all signers, but start at a random one
            size_t i = 0;
            for (i = 0; i < kNumSigners; ++i)
            {
                size_t const signerIdx = (rnd + i) % kNumSigners;

                if (!(cr_.attested[signerIdx]))
                {
                    // enqueue one attestation for this signer
                    cr_.attested[signerIdx] = true;

                    st.signers_attns[signerIdx][&bridge_]
                        .create_claims[cr_.claim_id - 1]
                        .emplace_back(createAccountAttestation(
                            bridge_.signers[signerIdx].account,
                            bridge_.jvb,
                            cr_.from,
                            cr_.amt,
                            cr_.reward,
                            bridge_.signers[signerIdx].account,
                            cr_.a2b,
                            cr_.claim_id,
                            cr_.to,
                            bridge_.signers[signerIdx]));
                    break;
                }
            }

            if (i == kNumSigners)
                return;  // did not attest

            auto& counters = st.counters[&bridge_];
            if (counters.create_callbacks.size() < cr_.claim_id)
                counters.create_callbacks.resize(cr_.claim_id);

            auto completeCb = [&](std::vector<size_t> const& signers) {
                auto numAttestors = signers.size();
                st.env.close();
                assert(numAttestors <= std::count(cr_.attested.begin(), cr_.attested.end(), true));
                assert(numAttestors >= bridge_.quorum);
                assert(cr_.claim_id - 1 == counters.claim_count);

                auto r = cr_.reward;
                auto reward = divide(r, STAmount(numAttestors), r.asset());

                for (auto i : signers)
                    st.receive(bridge_.signers[i].account, reward);

                st.spend(dstDoor(), reward, numAttestors);
                st.transfer(dstDoor(), cr_.to, cr_.amt);
                st.env.env.memoize(cr_.to);
                sm_state_ = SmState::Completed;
            };

            counters.create_callbacks[cr_.claim_id - 1] = std::move(completeCb);
        }

        SmState
        advance(uint64_t time, uint32_t rnd)
        {
            switch (sm_state_)
            {
                case SmState::Initial:
                    cr_.claim_id = issueAccountCreate();
                    sm_state_ = SmState::Attesting;
                    break;

                case SmState::Attesting:
                    attest(time, rnd);
                    break;

                default:
                    assert(0);
                    break;

                case SmState::Completed:
                    break;  // will get this once
            }
            return sm_state_;
        }

    private:
        SmState sm_state_{SmState::Initial};
        AccountCreate cr_;
    };

    // --------------------------------------------------
    class SmTransfer : public SmBase<SmTransfer>
    {
    public:
        using Base = SmBase<SmTransfer>;

        SmTransfer(
            std::shared_ptr<ChainStateTracker> const& chainstate,
            BridgeDef const& bridge,
            Transfer xfer)
            : Base(chainstate, bridge), xfer_(std::move(xfer))
        {
        }

        [[nodiscard]] bool
        a2b() const
        {
            return xfer_.a2b;
        }

        uint32_t
        createClaimId()
        {
            ChainStateTrack& st = destState();

            st.env.tx(xchainCreateClaimId(xfer_.to, bridge_.jvb, bridge_.reward, xfer_.from))
                .close();  // needed for claim_id sequence to be
                           // correct'
            st.spendFee(xfer_.to);
            return ++st.counters[&bridge_].claim_id;
        }

        void
        commit()
        {
            ChainStateTrack& st = srcState();
            jtx::Account const& srcdoor = srcDoor();

            if (xfer_.amt.asset() != xrpIssue())
            {
                st.env.tx(pay(srcdoor, xfer_.from, xfer_.amt));
                st.spendFee(srcdoor);
            }
            st.env.tx(xchainCommit(
                xfer_.from,
                bridge_.jvb,
                xfer_.claim_id,
                xfer_.amt,
                xfer_.with_claim == WithClaim::Yes ? std::nullopt
                                                   : std::optional<jtx::Account>(xfer_.finaldest)));
            st.spendFee(xfer_.from);
            st.transfer(xfer_.from, srcdoor, xfer_.amt);
        }

        void
        distributeReward(ChainStateTrack& st)
        {
            auto r = bridge_.reward;
            auto reward = divide(r, STAmount(bridge_.quorum), r.asset());

            for (size_t i = 0; i < kNumSigners; ++i)
            {
                if (xfer_.attested[i])
                    st.receive(bridge_.signers[i].account, reward);
            }
            st.spend(xfer_.to, reward, bridge_.quorum);
        }

        bool
        attest(uint64_t time, uint32_t rnd)
        {
            ChainStateTrack& st = destState();

            // check all signers, but start at a random one
            for (size_t i = 0; i < kNumSigners; ++i)
            {
                size_t const signerIdx = (rnd + i) % kNumSigners;
                if (!(xfer_.attested[signerIdx]))
                {
                    // enqueue one attestation for this signer
                    xfer_.attested[signerIdx] = true;

                    st.signers_attns[signerIdx][&bridge_].xfer_claims.emplace_back(claimAttestation(
                        bridge_.signers[signerIdx].account,
                        bridge_.jvb,
                        xfer_.from,
                        xfer_.amt,
                        bridge_.signers[signerIdx].account,
                        xfer_.a2b,
                        xfer_.claim_id,
                        xfer_.with_claim == WithClaim::Yes
                            ? std::nullopt
                            : std::optional<jtx::Account>(xfer_.finaldest),
                        bridge_.signers[signerIdx]));
                    break;
                }
            }

            // return true if quorum was reached, false otherwise
            bool const quorum =
                std::count(xfer_.attested.begin(), xfer_.attested.end(), true) >= bridge_.quorum;
            if (quorum && xfer_.with_claim == WithClaim::No)
            {
                distributeReward(st);
                st.transfer(dstDoor(), xfer_.finaldest, xfer_.amt);
            }
            return quorum;
        }

        void
        claim()
        {
            ChainStateTrack& st = destState();
            st.env.tx(
                xchainClaim(xfer_.to, bridge_.jvb, xfer_.claim_id, xfer_.amt, xfer_.finaldest));
            distributeReward(st);
            st.transfer(dstDoor(), xfer_.finaldest, xfer_.amt);
            st.spendFee(xfer_.to);
        }

        SmState
        advance(uint64_t time, uint32_t rnd)
        {
            switch (sm_state_)
            {
                case SmState::Initial:
                    xfer_.claim_id = createClaimId();
                    sm_state_ = SmState::ClaimIdCreated;
                    break;

                case SmState::ClaimIdCreated:
                    commit();
                    sm_state_ = SmState::Attesting;
                    break;

                case SmState::Attesting:
                    if (attest(time, rnd))
                    {
                        sm_state_ = xfer_.with_claim == WithClaim::Yes ? SmState::Attested
                                                                       : SmState::Completed;
                    }
                    else
                    {
                        sm_state_ = SmState::Attesting;
                    }
                    break;

                case SmState::Attested:
                    assert(xfer_.with_claim == WithClaim::Yes);
                    claim();
                    sm_state_ = SmState::Completed;
                    break;

                default:
                case SmState::Completed:
                    assert(0);  // should have been removed
                    break;
            }
            return sm_state_;
        }

    private:
        Transfer xfer_;
        SmState sm_state_{SmState::Initial};
    };

    // --------------------------------------------------
    using Sm = std::variant<SmCreateAccount, SmTransfer>;
    using SmCont = std::list<std::pair<uint64_t, Sm>>;

    SmCont sm_;

    void
    xfer(
        uint64_t time,
        std::shared_ptr<ChainStateTracker> const& chainstate,
        BridgeDef const& bridge,
        Transfer transfer)
    {
        sm_.emplace_back(time, SmTransfer(chainstate, bridge, std::move(transfer)));
    }

    void
    ac(uint64_t time,
       std::shared_ptr<ChainStateTracker> const& chainstate,
       BridgeDef const& bridge,
       AccountCreate ac)
    {
        sm_.emplace_back(time, SmCreateAccount(chainstate, bridge, std::move(ac)));
    }

public:
    void
    runSimulation(std::shared_ptr<ChainStateTracker> const& st, bool verifyBalances = true)
    {
        using namespace jtx;
        uint64_t time = 0;
        std::mt19937 gen(27);  // Standard mersenne_twister_engine
        std::uniform_int_distribution<uint32_t> distrib(0, 9);

        while (!sm_.empty())
        {
            ++time;
            for (auto it = sm_.begin(); it != sm_.end();)
            {
                auto vis = [&](auto& sm) {
                    uint32_t const rnd = distrib(gen);
                    return sm.advance(time, rnd);
                };
                auto& [t, sm] = *it;
                if (t <= time && std::visit(vis, sm) == SmState::Completed)
                {
                    it = sm_.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // send attestations
            st->sendAttestations();

            // make sure all transactions have been applied
            st->a.env.close();
            st->b.env.close();

            if (verifyBalances)
            {
                BEAST_EXPECT(st->verify());
            }
        }
    }

    void
    testXChainSimulation()
    {
        using namespace jtx;

        testcase("Bridge usage simulation");

        XEnv mcEnv(*this);
        XEnv scEnv(*this, true);

        auto st = std::make_shared<ChainStateTracker>(mcEnv, scEnv);

        // create 10 accounts + door funded on both chains, and store
        // in ChainStateTracker the initial amount of these accounts
        Account doorXRPLocking("doorXRPLocking"), doorUSDLocking("doorUSDLocking"),
            doorUSDIssuing("doorUSDIssuing");

        static constexpr size_t kNumAcct = 10;
        auto a = [&doorXRPLocking, &doorUSDLocking, &doorUSDIssuing]() {
            using namespace std::literals;
            std::vector<Account> result;
            result.reserve(kNumAcct);
            for (int i = 0; i < kNumAcct; ++i)
            {
                result.emplace_back(
                    "a"s + std::to_string(i), (i % 2) ? KeyType::Ed25519 : KeyType::Secp256k1);
            }
            result.emplace_back("doorXRPLocking");
            doorXRPLocking = result.back();
            result.emplace_back("doorUSDLocking");
            doorUSDLocking = result.back();
            result.emplace_back("doorUSDIssuing");
            doorUSDIssuing = result.back();
            return result;
        }();

        for (auto& acct : a)
        {
            STAmount const amt{XRP(100000)};

            mcEnv.fund(amt, acct);
            scEnv.fund(amt, acct);
        }
        Account const usdLockingAcc{"USDLocking"};
        IOU const usdLocking{usdLockingAcc["USD"]};
        IOU const usdIssuing{doorUSDIssuing["USD"]};

        mcEnv.fund(XRP(100000), usdLockingAcc);
        mcEnv.close();
        mcEnv.tx(trust(doorUSDLocking, usdLocking(100000)));
        mcEnv.close();
        mcEnv.tx(pay(usdLockingAcc, doorUSDLocking, usdLocking(50000)));

        for (int i = 0; i < a.size(); ++i)
        {
            auto& acct{a[i]};
            if (i < kNumAcct)
            {
                mcEnv.tx(trust(acct, usdLocking(100000)));
                scEnv.tx(trust(acct, usdIssuing(100000)));
            }
            st->init(acct);
        }
        for (auto& s : signers)
            st->init(s.account);

        st->b.init(Account::kMaster);

        // also create some unfunded accounts
        static constexpr size_t kNumUa = 20;
        auto ua = []() {
            using namespace std::literals;
            std::vector<Account> result;
            result.reserve(kNumUa);
            for (int i = 0; i < kNumUa; ++i)
            {
                result.emplace_back(
                    "ua"s + std::to_string(i), (i % 2) ? KeyType::Ed25519 : KeyType::Secp256k1);
            }
            return result;
        }();

        // initialize a bridge from a BridgeDef
        auto initBridge = [&mcEnv, &scEnv, &st](BridgeDef& bd) {
            bd.initBridge(mcEnv, scEnv);
            st->a.spendFee(bd.doorA, 2);
            st->b.spendFee(bd.doorB, 2);
        };

        // create XRP -> XRP bridge
        // ------------------------
        BridgeDef xrpB{
            .doorA = doorXRPLocking,
            .issueA = xrpIssue(),
            .doorB = Account::kMaster,
            .issueB = xrpIssue(),
            .reward = XRP(1),
            .minAccountCreate = XRP(20),
            .quorum = quorum,
            .signers = signers,
            .jvb = json::ValueType::Null};

        initBridge(xrpB);

        // create USD -> USD bridge
        // ------------------------
        BridgeDef usdB{
            .doorA = doorUSDLocking,
            .issueA = usdLocking,
            .doorB = doorUSDIssuing,
            .issueB = usdIssuing,
            .reward = XRP(1),
            .minAccountCreate = XRP(20),
            .quorum = quorum,
            .signers = signers,
            .jvb = json::ValueType::Null};

        initBridge(usdB);

        // try a single account create + transfer to validate the simulation
        // engine. Do the transfer 8 time steps after the account create, to
        // give  time enough for ua[0] to be funded now so it can reserve
        // the claimID
        // -----------------------------------------------------------------
        ac(0,
           st,
           xrpB,
           {.from = a[0], .to = ua[0], .amt = XRP(777), .reward = xrpB.reward, .a2b = true});
        xfer(
            8,
            st,
            xrpB,
            {.from = a[0], .to = ua[0], .finaldest = a[2], .amt = XRP(3), .a2b = true});
        runSimulation(st);

        // try the same thing in the other direction
        // -----------------------------------------
        ac(0,
           st,
           xrpB,
           {.from = a[0], .to = ua[0], .amt = XRP(777), .reward = xrpB.reward, .a2b = false});
        xfer(
            8,
            st,
            xrpB,
            {.from = a[0], .to = ua[0], .finaldest = a[2], .amt = XRP(3), .a2b = false});
        runSimulation(st);

        // run multiple XRP transfers
        // --------------------------
        xfer(
            0,
            st,
            xrpB,
            {.from = a[0],
             .to = a[0],
             .finaldest = a[1],
             .amt = XRP(6),
             .a2b = true,
             .with_claim = WithClaim::No});
        xfer(
            1,
            st,
            xrpB,
            {.from = a[0],
             .to = a[0],
             .finaldest = a[1],
             .amt = XRP(8),
             .a2b = false,
             .with_claim = WithClaim::No});
        xfer(
            1, st, xrpB, {.from = a[1], .to = a[1], .finaldest = a[1], .amt = XRP(1), .a2b = true});
        xfer(
            2,
            st,
            xrpB,
            {.from = a[0], .to = a[0], .finaldest = a[1], .amt = XRP(3), .a2b = false});
        xfer(
            2,
            st,
            xrpB,
            {.from = a[1], .to = a[1], .finaldest = a[1], .amt = XRP(5), .a2b = false});
        xfer(
            2,
            st,
            xrpB,
            {.from = a[0],
             .to = a[0],
             .finaldest = a[1],
             .amt = XRP(7),
             .a2b = false,
             .with_claim = WithClaim::No});
        xfer(
            2, st, xrpB, {.from = a[1], .to = a[1], .finaldest = a[1], .amt = XRP(9), .a2b = true});
        runSimulation(st);

        // run one USD transfer
        // --------------------
        xfer(
            0,
            st,
            usdB,
            {.from = a[0], .to = a[1], .finaldest = a[2], .amt = usdLocking(3), .a2b = true});
        runSimulation(st);

        // run multiple USD transfers
        // --------------------------
        xfer(
            0,
            st,
            usdB,
            {.from = a[0], .to = a[0], .finaldest = a[1], .amt = usdLocking(6), .a2b = true});
        xfer(
            1,
            st,
            usdB,
            {.from = a[0], .to = a[0], .finaldest = a[1], .amt = usdIssuing(8), .a2b = false});
        xfer(
            1,
            st,
            usdB,
            {.from = a[1], .to = a[1], .finaldest = a[1], .amt = usdLocking(1), .a2b = true});
        xfer(
            2,
            st,
            usdB,
            {.from = a[0], .to = a[0], .finaldest = a[1], .amt = usdIssuing(3), .a2b = false});
        xfer(
            2,
            st,
            usdB,
            {.from = a[1], .to = a[1], .finaldest = a[1], .amt = usdIssuing(5), .a2b = false});
        xfer(
            2,
            st,
            usdB,
            {.from = a[0], .to = a[0], .finaldest = a[1], .amt = usdIssuing(7), .a2b = false});
        xfer(
            2,
            st,
            usdB,
            {.from = a[1], .to = a[1], .finaldest = a[1], .amt = usdLocking(9), .a2b = true});
        runSimulation(st);

        // run mixed transfers
        // -------------------
        xfer(
            0, st, xrpB, {.from = a[0], .to = a[0], .finaldest = a[0], .amt = XRP(1), .a2b = true});
        xfer(
            0,
            st,
            usdB,
            {.from = a[1], .to = a[3], .finaldest = a[3], .amt = usdIssuing(3), .a2b = false});
        xfer(
            0,
            st,
            usdB,
            {.from = a[3], .to = a[2], .finaldest = a[1], .amt = usdIssuing(5), .a2b = false});

        xfer(
            1,
            st,
            xrpB,
            {.from = a[0], .to = a[0], .finaldest = a[0], .amt = XRP(4), .a2b = false});
        xfer(
            1, st, xrpB, {.from = a[1], .to = a[1], .finaldest = a[0], .amt = XRP(8), .a2b = true});
        xfer(
            1,
            st,
            usdB,
            {.from = a[4], .to = a[1], .finaldest = a[1], .amt = usdLocking(7), .a2b = true});

        xfer(
            3, st, xrpB, {.from = a[1], .to = a[1], .finaldest = a[0], .amt = XRP(7), .a2b = true});
        xfer(
            3,
            st,
            xrpB,
            {.from = a[0], .to = a[4], .finaldest = a[3], .amt = XRP(2), .a2b = false});
        xfer(
            3, st, xrpB, {.from = a[1], .to = a[1], .finaldest = a[0], .amt = XRP(9), .a2b = true});
        xfer(
            3,
            st,
            usdB,
            {.from = a[3], .to = a[1], .finaldest = a[1], .amt = usdIssuing(11), .a2b = false});
        runSimulation(st);

        // run multiple account create to stress attestation batching
        // ----------------------------------------------------------
        ac(0,
           st,
           xrpB,
           {.from = a[0], .to = ua[1], .amt = XRP(301), .reward = xrpB.reward, .a2b = true});
        ac(0,
           st,
           xrpB,
           {.from = a[1], .to = ua[2], .amt = XRP(302), .reward = xrpB.reward, .a2b = true});
        ac(1,
           st,
           xrpB,
           {.from = a[0], .to = ua[3], .amt = XRP(303), .reward = xrpB.reward, .a2b = true});
        ac(2,
           st,
           xrpB,
           {.from = a[1], .to = ua[4], .amt = XRP(304), .reward = xrpB.reward, .a2b = true});
        ac(3,
           st,
           xrpB,
           {.from = a[0], .to = ua[5], .amt = XRP(305), .reward = xrpB.reward, .a2b = true});
        ac(4,
           st,
           xrpB,
           {.from = a[1], .to = ua[6], .amt = XRP(306), .reward = xrpB.reward, .a2b = true});
        ac(6,
           st,
           xrpB,
           {.from = a[0], .to = ua[7], .amt = XRP(307), .reward = xrpB.reward, .a2b = true});
        ac(7,
           st,
           xrpB,
           {.from = a[2], .to = ua[8], .amt = XRP(308), .reward = xrpB.reward, .a2b = true});
        ac(9,
           st,
           xrpB,
           {.from = a[0], .to = ua[9], .amt = XRP(309), .reward = xrpB.reward, .a2b = true});
        ac(9,
           st,
           xrpB,
           {.from = a[0], .to = ua[9], .amt = XRP(309), .reward = xrpB.reward, .a2b = true});
        ac(10,
           st,
           xrpB,
           {.from = a[0], .to = ua[10], .amt = XRP(310), .reward = xrpB.reward, .a2b = true});
        ac(12,
           st,
           xrpB,
           {.from = a[0], .to = ua[11], .amt = XRP(311), .reward = xrpB.reward, .a2b = true});
        ac(12,
           st,
           xrpB,
           {.from = a[3], .to = ua[12], .amt = XRP(312), .reward = xrpB.reward, .a2b = true});
        ac(12,
           st,
           xrpB,
           {.from = a[4], .to = ua[13], .amt = XRP(313), .reward = xrpB.reward, .a2b = true});
        ac(12,
           st,
           xrpB,
           {.from = a[3], .to = ua[14], .amt = XRP(314), .reward = xrpB.reward, .a2b = true});
        ac(12,
           st,
           xrpB,
           {.from = a[6], .to = ua[15], .amt = XRP(315), .reward = xrpB.reward, .a2b = true});
        ac(13,
           st,
           xrpB,
           {.from = a[7], .to = ua[16], .amt = XRP(316), .reward = xrpB.reward, .a2b = true});
        ac(15,
           st,
           xrpB,
           {.from = a[3], .to = ua[17], .amt = XRP(317), .reward = xrpB.reward, .a2b = true});
        runSimulation(st, true);  // balances verification working now.
    }

    void
    run() override
    {
        testXChainSimulation();
    }
};

BEAST_DEFINE_TESTSUITE(XChain, app, xrpl);
BEAST_DEFINE_TESTSUITE(XChainSim, app, xrpl);

}  // namespace xrpl::test
