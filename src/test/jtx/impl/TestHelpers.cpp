#include <test/jtx/TestHelpers.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/envconfig.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/rate.h>
#include <test/jtx/trust.h>

#include <xrpld/core/Config.h>
#include <xrpld/rpc/RPCHandler.h>
#include <xrpld/rpc/Role.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl::test::jtx {

// Functions used in debugging
json::Value
getAccountOffers(Env& env, AccountID const& acct, bool current)
{
    json::Value jv;
    jv[jss::account] = to_string(acct);
    return env.rpc("json", "account_offers", to_string(jv))[jss::result];
}

json::Value
getAccountLines(Env& env, AccountID const& acctId)
{
    json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_lines", to_string(jv))[jss::result];
}

bool
checkArraySize(json::Value const& val, unsigned int size)
{
    return val.isArray() && val.size() == size;
}

std::uint32_t
ownerCount(Env const& env, Account const& account)
{
    return env.ownerCount(account);
}

/* Path finding */
/******************************************************************************/
void
stpathAppendOne(STPath& st, Account const& account)
{
    st.pushBack(STPathElement({account.id(), std::nullopt, std::nullopt}));
}

void
stpathAppendOne(STPath& st, STPathElement const& pe)
{
    st.pushBack(pe);
}

bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 && sa1.getIssuer() == sa2.getIssuer();
}

static void
addSourceAsset(
    json::Value& jv,
    PathAsset const& srcAsset,
    std::optional<AccountID> const& srcIssuer)
{
    std::visit(
        [&]<typename TAsset>(TAsset const& asset) {
            if constexpr (std::is_same_v<TAsset, Currency>)
            {
                jv[jss::currency] = to_string(asset);
                if (srcIssuer)
                    jv[jss::issuer] = to_string(*srcIssuer);
            }
            else
            {
                if (srcIssuer)
                    Throw<std::runtime_error>("MPT source_currencies can't have issuer");
                jv[jss::mpt_issuance_id] = to_string(asset);
            }
        },
        srcAsset.value());
}

json::Value
rpf(jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& dstAmount,
    std::optional<STAmount> const& sendMax,
    std::optional<PathAsset> const& srcAsset,
    std::optional<AccountID> const& srcIssuer)
{
    json::Value jv = json::ValueType::Object;
    jv[jss::command] = "ripple_path_find";
    jv[jss::source_account] = toBase58(src);
    jv[jss::destination_account] = toBase58(dst);
    jv[jss::destination_amount] = dstAmount.getJson(JsonOptions::Values::None);
    if (sendMax)
        jv[jss::send_max] = sendMax->getJson(JsonOptions::Values::None);
    if (srcAsset)
    {
        auto& sc = jv[jss::source_currencies] = json::ValueType::Array;
        json::Value j = json::ValueType::Object;
        addSourceAsset(j, *srcAsset, srcIssuer);
        sc.append(j);
    }

    return jv;
}

jtx::Env
pathTestEnv(beast::unit_test::Suite& suite)
{
    // These tests were originally written with search parameters that are
    // different from the current defaults. This function creates an env
    // with the search parameters that the tests were written for.
    using namespace jtx;
    return Env(suite, envconfig([](std::unique_ptr<Config> cfg) {
                   cfg->pathSearchOld = 7;
                   cfg->pathSearch = 7;
                   cfg->pathSearchMax = 10;
                   return cfg;
               }));
}

json::Value
findPathsRequest(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<PathAsset> const& srcAsset,
    std::optional<AccountID> const& srcIssuer,
    std::optional<uint256> const& domain)
{
    using namespace jtx;

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

    json::Value params = json::ValueType::Object;
    params[jss::command] = "ripple_path_find";
    params[jss::source_account] = toBase58(src);
    params[jss::destination_account] = toBase58(dst);
    params[jss::destination_amount] = saDstAmount.getJson(JsonOptions::Values::None);
    if (saSendMax)
        params[jss::send_max] = saSendMax->getJson(JsonOptions::Values::None);

    if (srcAsset)
    {
        auto& sc = params[jss::source_currencies] = json::ValueType::Array;
        json::Value j = json::ValueType::Object;
        addSourceAsset(j, *srcAsset, srcIssuer);
        sc.append(j);
    }

    if (domain)
        params[jss::domain] = to_string(*domain);

    json::Value result;
    Gate g;
    app.getJobQueue().postCoro(JtClient, "RPC-Client", [&](auto const& coro) {
        context.params = std::move(params);
        context.coro = coro;
        RPC::doCommand(context, result);
        g.signal();
    });

    using namespace std::chrono_literals;
    using namespace beast::unit_test;
    g.waitFor(5s);
    return result;
}

std::tuple<STPathSet, STAmount, STAmount>
findPaths(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<PathAsset> const& srcAsset,
    std::optional<AccountID> const& srcIssuer,
    std::optional<uint256> const& domain)
{
    json::Value result =
        findPathsRequest(env, src, dst, saDstAmount, saSendMax, srcAsset, srcIssuer, domain);
    if (result.isMember(jss::error))
        return std::make_tuple(STPathSet{}, STAmount{}, STAmount{});

    STAmount da;
    if (result.isMember(jss::destination_amount))
        da = amountFromJson(sfGeneric, result[jss::destination_amount]);

    STAmount sa;
    STPathSet paths;
    if (result.isMember(jss::alternatives))
    {
        auto const& alts = result[jss::alternatives];
        if (alts.size() > 0)
        {
            auto const& path = alts[0u];

            if (path.isMember(jss::source_amount))
                sa = amountFromJson(sfGeneric, path[jss::source_amount]);

            if (path.isMember(jss::destination_amount))
                da = amountFromJson(sfGeneric, path[jss::destination_amount]);

            if (path.isMember(jss::paths_computed))
            {
                json::Value p;
                p["Paths"] = path[jss::paths_computed];
                STParsedJSONObject po("generic", p);
                if (po.object)
                    paths = po.object->getFieldPathSet(sfPaths);
            }
        }
    }

    return std::make_tuple(std::move(paths), std::move(sa), std::move(da));
}

std::tuple<STPathSet, STAmount, STAmount>
findPathsByElement(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax,
    std::optional<STPathElement> const& srcElement,
    std::optional<AccountID> const& srcIssuer,
    std::optional<uint256> const& domain)
{
    // srcElement is optional but is expected to always be present
    XRPL_ASSERT(
        srcElement.has_value(), "xrpl::test::jtx::findPathsByElement::srcElement : nullptr");

    return findPaths(
        env,
        src,
        dst,
        saDstAmount,
        saSendMax,
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        srcElement->getPathAsset(),
        srcIssuer,
        domain);
}

/******************************************************************************/

XRPAmount
txFee(Env const& env, std::uint16_t n)
{
    return env.current()->fees().base * n;
}

PrettyAmount
xrpMinusFee(Env const& env, std::int64_t xrpAmount)
{
    auto feeDrops = env.current()->fees().base;
    return drops(kJtxDropsPerXrp * xrpAmount - feeDrops);
};

[[nodiscard]] bool
expectHolding(Env& env, AccountID const& account, STAmount const& value, bool defaultLimits)
{
    if (auto const sle = env.le(keylet::line(account, value.get<Issue>())))
    {
        Issue const issue = value.get<Issue>();
        bool const accountLow = account < issue.account;

        bool expectDefaultTrustLine = true;
        if (defaultLimits)
        {
            STAmount low{issue};
            STAmount high{issue};

            low.get<Issue>().account = accountLow ? account : issue.account;
            high.get<Issue>().account = accountLow ? issue.account : account;

            expectDefaultTrustLine =
                sle->getFieldAmount(sfLowLimit) == low && sle->getFieldAmount(sfHighLimit) == high;
        }

        auto amount = sle->getFieldAmount(sfBalance);
        amount.get<Issue>().account = value.getIssuer();
        if (!accountLow)
            amount.negate();
        return amount == value && expectDefaultTrustLine;
    }
    return false;
}

[[nodiscard]] bool
expectHolding(Env& env, AccountID const& account, None const&, Issue const& issue)
{
    return !env.le(keylet::line(account, issue));
}

[[nodiscard]] bool
expectHolding(Env& env, AccountID const& account, None const&, MPTIssue const& mptIssue)
{
    return !env.le(keylet::mptoken(mptIssue.getMptID(), account));
}

[[nodiscard]] bool
expectHolding(Env& env, AccountID const& account, None const& value)
{
    return std::visit(
        [&](auto const& issue) { return expectHolding(env, account, value, issue); },
        value.asset.value());
}

[[nodiscard]] bool
expectMPT(Env& env, AccountID const& account, STAmount const& value)
{
    auto const mptIssuanceID = keylet::mptIssuance(value.asset().get<MPTIssue>());
    auto const mptToken = env.le(keylet::mptoken(mptIssuanceID.key, account));
    return mptToken && (*mptToken)[sfMPTAmount] == value.mpt().value();
}

[[nodiscard]] bool
expectOffers(
    Env& env,
    AccountID const& account,
    std::uint16_t size,
    std::vector<Amounts> const& toMatch)
{
    std::uint16_t cnt = 0;
    std::uint16_t matched = 0;
    forEachItem(*env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
        if (!sle)
            return false;
        if (sle->getType() == ltOFFER)
        {
            ++cnt;
            if (std::ranges::find_if(toMatch, [&](auto const& a) {
                    return a.in == sle->getFieldAmount(sfTakerPays) &&
                        a.out == sle->getFieldAmount(sfTakerGets);
                }) != toMatch.end())
                ++matched;
        }
        return true;
    });
    return size == cnt && ((toMatch.empty() && size != 0) || (matched == toMatch.size()));
}

json::Value
ledgerEntryRoot(Env& env, Account const& acct)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::account_root] = acct.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

json::Value
ledgerEntryState(Env& env, Account const& acctA, Account const& acctB, std::string const& currency)
{
    json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::ripple_state][jss::currency] = currency;
    jvParams[jss::ripple_state][jss::accounts] = json::ValueType::Array;
    jvParams[jss::ripple_state][jss::accounts].append(acctA.human());
    jvParams[jss::ripple_state][jss::accounts].append(acctB.human());
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

json::Value
ledgerEntryOffer(jtx::Env& env, jtx::Account const& acct, std::uint32_t offerSeq)
{
    json::Value jvParams;
    jvParams[jss::offer][jss::account] = acct.human();
    jvParams[jss::offer][jss::seq] = offerSeq;
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

json::Value
ledgerEntryMPT(jtx::Env& env, jtx::Account const& acct, MPTID const& mptID)
{
    json::Value jvParams;
    jvParams[jss::mptoken][jss::account] = acct.human();
    jvParams[jss::mptoken][jss::mpt_issuance_id] = to_string(mptID);
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

json::Value
getBookOffers(jtx::Env& env, Asset const& takerPays, Asset const& takerGets)
{
    json::Value jvbp;
    jvbp[jss::ledger_index] = "current";
    takerPays.setJson(jvbp[jss::taker_pays]);
    takerGets.setJson(jvbp[jss::taker_gets]);
    return env.rpc("json", "book_offers", to_string(jvbp))[jss::result];
}

json::Value
accountBalance(Env& env, Account const& acct)
{
    auto const jrr = ledgerEntryRoot(env, acct);
    return jrr[jss::node][sfBalance.fieldName];
}

[[nodiscard]] bool
expectLedgerEntryRoot(Env& env, Account const& acct, STAmount const& expectedValue)
{
    return accountBalance(env, acct) == to_string(expectedValue.xrp());
}

/* Payment Channel */
/******************************************************************************/
namespace paychan {

json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter,
    std::optional<std::uint32_t> const& dstTag)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelCreate;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    jv[jss::SettleDelay] = settleDelay.count();
    jv[sfPublicKey.fieldName] = strHex(pk.slice());
    if (cancelAfter)
        jv[sfCancelAfter.fieldName] = cancelAfter->time_since_epoch().count();
    if (dstTag)
        jv[sfDestinationTag.fieldName] = *dstTag;
    return jv;
}

json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelFund;
    jv[jss::Account] = to_string(account);
    jv[sfChannel.fieldName] = to_string(channel);
    jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
    if (expiration)
        jv[sfExpiration.fieldName] = expiration->time_since_epoch().count();
    return jv;
}

json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance,
    std::optional<STAmount> const& amount,
    std::optional<Slice> const& signature,
    std::optional<PublicKey> const& pk)
{
    json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelClaim;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    if (amount)
        jv[jss::Amount] = amount->getJson(JsonOptions::Values::None);
    if (balance)
        jv["Balance"] = balance->getJson(JsonOptions::Values::None);
    if (signature)
        jv["Signature"] = strHex(*signature);
    if (pk)
        jv["PublicKey"] = strHex(pk->slice());
    return jv;
}

uint256
channel(AccountID const& account, AccountID const& dst, std::uint32_t seqProxyValue)
{
    auto const k = keylet::payChan(account, dst, seqProxyValue);
    return k.key;
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    if (!slep)
        return XRPAmount{-1};
    return (*slep)[sfBalance];
}

bool
channelExists(ReadView const& view, uint256 const& chan)
{
    auto const slep = view.read({ltPAYCHAN, chan});
    return bool(slep);
}

}  // namespace paychan

/* Crossing Limits */
/******************************************************************************/

void
nOffers(Env& env, std::size_t n, Account const& account, STAmount const& in, STAmount const& out)
{
    auto const ownerCount = env.le(account)->getFieldU32(sfOwnerCount);
    for (std::size_t i = 0; i < n; i++)
    {
        env(offer(account, in, out));
        env.close();
    }
    env.require(Owners(account, ownerCount + n));
}

/* Pay Strand */
/***************************************************************/

// Currency path element
STPathElement
cpe(PathAsset const& pa)
{
    return pa.visit(
        [](Currency const& currency) {
            return STPathElement(STPathElement::TypeCurrency, xrpAccount(), currency, xrpAccount());
        },
        [](MPTID const& mpt) {
            return STPathElement(STPathElement::TypeMpt, xrpAccount(), mpt, xrpAccount());
        });
};

// All path element
STPathElement
allPathElements(AccountID const& a, Asset const& asset)
{
    return STPathElement(a, asset, asset.getIssuer());
};

STPathElement
ipe(Asset const& asset)
{
    return asset.visit(
        [](Issue const& issue) {
            return STPathElement(
                STPathElement::TypeCurrency | STPathElement::TypeIssuer,
                xrpAccount(),
                issue.currency,
                issue.account);
        },
        [](MPTIssue const& issue) {
            return STPathElement(
                STPathElement::TypeMpt | STPathElement::TypeIssuer,
                xrpAccount(),
                issue.getMptID(),
                issue.getIssuer());
        });
};

// Issuer path element
STPathElement
iape(AccountID const& account)
{
    return STPathElement(STPathElement::TypeIssuer, xrpAccount(), xrpCurrency(), account);
};

// Account path element
STPathElement
ape(AccountID const& a)
{
    return STPathElement(STPathElement::TypeAccount, a, xrpCurrency(), xrpAccount());
};

bool
equal(std::unique_ptr<xrpl::Step> const& s1, DirectStepInfo const& dsi)
{
    if (!s1)
        return false;
    return test::directStepEqual(*s1, dsi.src, dsi.dst, dsi.currency);
}

bool
equal(std::unique_ptr<xrpl::Step> const& s1, MPTEndpointStepInfo const& dsi)
{
    if (!s1)
        return false;
    return test::mptEndpointStepEqual(*s1, dsi.src, dsi.dst, dsi.mptid);
}

bool
equal(std::unique_ptr<xrpl::Step> const& s1, XRPEndpointStepInfo const& xrpStepInfo)
{
    if (!s1)
        return false;
    return test::xrpEndpointStepEqual(*s1, xrpStepInfo.acc);
}

bool
equal(std::unique_ptr<xrpl::Step> const& s1, xrpl::Book const& bsi)
{
    if (!s1)
        return false;
    return bookStepEqual(*s1, bsi);
}

namespace detail {

IOU
issueHelperIOU(IssuerArgs const& args)
{
    auto const iou = args.issuer[args.token];
    if (args.transferFee != 0)
    {
        auto const tfee = 1. + (static_cast<double>(args.transferFee) / 100'000);
        args.env(rate(args.issuer, tfee));
    }
    for (auto const& account : args.holders)
    {
        args.env(trust(account, iou(args.limit.value_or(1'000))));
    }
    return iou;
}

MPT
issueHelperMPT(IssuerArgs const& args)
{
    using namespace jtx;
    if (args.limit)
    {
        MPT const mpt = MPTTester(
            {.env = args.env,
             .issuer = args.issuer,
             .holders = args.holders,
             .transferFee = args.transferFee,
             .maxAmt = args.limit});
        return mpt;
    }

    MPT const mpt = MPTTester(
        {.env = args.env,
         .issuer = args.issuer,
         .holders = args.holders,
         .transferFee = args.transferFee});
    return mpt;
}

}  // namespace detail

/* LoanBroker */
/******************************************************************************/

namespace loanBroker {

json::Value
set(AccountID const& account, uint256 const& vaultId, uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerSet;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultId);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
del(AccountID const& account, uint256 const& brokerID, uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerDelete;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
coverDeposit(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverDeposit;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfAmount] = amount.getJson(JsonOptions::Values::None);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
coverWithdraw(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverWithdraw;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfAmount] = amount.getJson(JsonOptions::Values::None);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
coverClawback(AccountID const& account, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverClawback;
    jv[sfAccount] = to_string(account);
    jv[sfFlags] = flags;
    return jv;
}

}  // namespace loanBroker

/* Loan */
/******************************************************************************/
namespace loan {

json::Value
set(AccountID const& account,
    uint256 const& loanBrokerID,
    Number principalRequested,
    std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanSet;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(loanBrokerID);
    jv[sfPrincipalRequested] = to_string(principalRequested);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
manage(AccountID const& account, uint256 const& loanID, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanManage;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
del(AccountID const& account, uint256 const& loanID, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanDelete;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfFlags] = flags;
    return jv;
}

json::Value
pay(AccountID const& account, uint256 const& loanID, STAmount const& amount, std::uint32_t flags)
{
    json::Value jv;
    jv[sfTransactionType] = jss::LoanPay;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfAmount] = amount.getJson();
    jv[sfFlags] = flags;
    return jv;
}

}  // namespace loan
}  // namespace xrpl::test::jtx
