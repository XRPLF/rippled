#include <test/jtx/TestHelpers.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {
namespace test {
namespace jtx {

// Functions used in debugging
Json::Value
getAccountOffers(Env& env, AccountID const& acct, bool current)
{
    Json::Value jv;
    jv[jss::account] = to_string(acct);
    return env.rpc("json", "account_offers", to_string(jv))[jss::result];
}

Json::Value
getAccountLines(Env& env, AccountID const& acctId)
{
    Json::Value jv;
    jv[jss::account] = to_string(acctId);
    return env.rpc("json", "account_lines", to_string(jv))[jss::result];
}

bool
checkArraySize(Json::Value const& val, unsigned int size)
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
stpath_append_one(STPath& st, Account const& account)
{
    st.push_back(STPathElement({account.id(), std::nullopt, std::nullopt}));
}

void
stpath_append_one(STPath& st, STPathElement const& pe)
{
    st.push_back(pe);
}

bool
equal(STAmount const& sa1, STAmount const& sa2)
{
    return sa1 == sa2 && sa1.issue().account == sa2.issue().account;
}

// Issue path element
STPathElement
IPE(Issue const& iss)
{
    return STPathElement(
        STPathElement::typeCurrency | STPathElement::typeIssuer,
        xrpAccount(),
        iss.currency,
        iss.account);
}

/******************************************************************************/

XRPAmount
txfee(Env const& env, std::uint16_t n)
{
    return env.current()->fees().base * n;
}

PrettyAmount
xrpMinusFee(Env const& env, std::int64_t xrpAmount)
{
    auto feeDrops = env.current()->fees().base;
    return drops(dropsPerXRP * xrpAmount - feeDrops);
};

[[nodiscard]] bool
expectHolding(
    Env& env,
    AccountID const& account,
    STAmount const& value,
    bool defaultLimits)
{
    if (auto const sle = env.le(keylet::line(account, value.issue())))
    {
        Issue const issue = value.issue();
        bool const accountLow = account < issue.account;

        bool expectDefaultTrustLine = true;
        if (defaultLimits)
        {
            STAmount low{issue};
            STAmount high{issue};

            low.setIssuer(accountLow ? account : issue.account);
            high.setIssuer(accountLow ? issue.account : account);

            expectDefaultTrustLine = sle->getFieldAmount(sfLowLimit) == low &&
                sle->getFieldAmount(sfHighLimit) == high;
        }

        auto amount = sle->getFieldAmount(sfBalance);
        amount.setIssuer(value.issue().account);
        if (!accountLow)
            amount.negate();
        return amount == value && expectDefaultTrustLine;
    }
    return false;
}

[[nodiscard]] bool
expectHolding(
    Env& env,
    AccountID const& account,
    None const&,
    Issue const& issue)
{
    return !env.le(keylet::line(account, issue));
}

[[nodiscard]] bool
expectHolding(
    Env& env,
    AccountID const& account,
    None const&,
    MPTIssue const& mptIssue)
{
    return !env.le(keylet::mptoken(mptIssue.getMptID(), account));
}

[[nodiscard]] bool
expectHolding(Env& env, AccountID const& account, None const& value)
{
    return std::visit(
        [&](auto const& issue) {
            return expectHolding(env, account, value, issue);
        },
        value.asset.value());
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
    forEachItem(
        *env.current(), account, [&](std::shared_ptr<SLE const> const& sle) {
            if (!sle)
                return false;
            if (sle->getType() == ltOFFER)
            {
                ++cnt;
                if (std::find_if(
                        toMatch.begin(), toMatch.end(), [&](auto const& a) {
                            return a.in == sle->getFieldAmount(sfTakerPays) &&
                                a.out == sle->getFieldAmount(sfTakerGets);
                        }) != toMatch.end())
                    ++matched;
            }
            return true;
        });
    return size == cnt && matched == toMatch.size();
}

Json::Value
ledgerEntryRoot(Env& env, Account const& acct)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::account_root] = acct.human();
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

Json::Value
ledgerEntryState(
    Env& env,
    Account const& acct_a,
    Account const& acct_b,
    std::string const& currency)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = "current";
    jvParams[jss::ripple_state][jss::currency] = currency;
    jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
    jvParams[jss::ripple_state][jss::accounts].append(acct_a.human());
    jvParams[jss::ripple_state][jss::accounts].append(acct_b.human());
    return env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
}

Json::Value
accountBalance(Env& env, Account const& acct)
{
    auto const jrr = ledgerEntryRoot(env, acct);
    return jrr[jss::node][sfBalance.fieldName];
}

[[nodiscard]] bool
expectLedgerEntryRoot(
    Env& env,
    Account const& acct,
    STAmount const& expectedValue)
{
    return accountBalance(env, acct) == to_string(expectedValue.xrp());
}

/* Payment Channel */
/******************************************************************************/
namespace paychan {

Json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter,
    std::optional<std::uint32_t> const& dstTag)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelCreate;
    jv[jss::Account] = to_string(account);
    jv[jss::Destination] = to_string(to);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    jv[jss::SettleDelay] = settleDelay.count();
    jv[sfPublicKey.fieldName] = strHex(pk.slice());
    if (cancelAfter)
        jv[sfCancelAfter.fieldName] = cancelAfter->time_since_epoch().count();
    if (dstTag)
        jv[sfDestinationTag.fieldName] = *dstTag;
    return jv;
}

Json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelFund;
    jv[jss::Account] = to_string(account);
    jv[sfChannel.fieldName] = to_string(channel);
    jv[jss::Amount] = amount.getJson(JsonOptions::none);
    if (expiration)
        jv[sfExpiration.fieldName] = expiration->time_since_epoch().count();
    return jv;
}

Json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance,
    std::optional<STAmount> const& amount,
    std::optional<Slice> const& signature,
    std::optional<PublicKey> const& pk)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::PaymentChannelClaim;
    jv[jss::Account] = to_string(account);
    jv["Channel"] = to_string(channel);
    if (amount)
        jv[jss::Amount] = amount->getJson(JsonOptions::none);
    if (balance)
        jv["Balance"] = balance->getJson(JsonOptions::none);
    if (signature)
        jv["Signature"] = strHex(*signature);
    if (pk)
        jv["PublicKey"] = strHex(pk->slice());
    return jv;
}

uint256
channel(
    AccountID const& account,
    AccountID const& dst,
    std::uint32_t seqProxyValue)
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
n_offers(
    Env& env,
    std::size_t n,
    Account const& account,
    STAmount const& in,
    STAmount const& out)
{
    auto const ownerCount = env.le(account)->getFieldU32(sfOwnerCount);
    for (std::size_t i = 0; i < n; i++)
    {
        env(offer(account, in, out));
        env.close();
    }
    env.require(owners(account, ownerCount + n));
}

/* Pay Strand */
/***************************************************************/

// Currency path element
STPathElement
cpe(Currency const& c)
{
    return STPathElement(
        STPathElement::typeCurrency, xrpAccount(), c, xrpAccount());
};

// All path element
STPathElement
allpe(AccountID const& a, Issue const& iss)
{
    return STPathElement(
        STPathElement::typeAccount | STPathElement::typeCurrency |
            STPathElement::typeIssuer,
        a,
        iss.currency,
        iss.account);
};

/* LoanBroker */
/******************************************************************************/

namespace loanBroker {

Json::Value
set(AccountID const& account, uint256 const& vaultId, uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerSet;
    jv[sfAccount] = to_string(account);
    jv[sfVaultID] = to_string(vaultId);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
del(AccountID const& account, uint256 const& brokerID, uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerDelete;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
coverDeposit(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverDeposit;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfAmount] = amount.getJson(JsonOptions::none);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
coverWithdraw(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverWithdraw;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(brokerID);
    jv[sfAmount] = amount.getJson(JsonOptions::none);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
coverClawback(AccountID const& account, std::uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanBrokerCoverClawback;
    jv[sfAccount] = to_string(account);
    jv[sfFlags] = flags;
    return jv;
}

}  // namespace loanBroker

/* Loan */
/******************************************************************************/
namespace loan {

Json::Value
set(AccountID const& account,
    uint256 const& loanBrokerID,
    Number principalRequested,
    std::uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanSet;
    jv[sfAccount] = to_string(account);
    jv[sfLoanBrokerID] = to_string(loanBrokerID);
    jv[sfPrincipalRequested] = to_string(principalRequested);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
manage(AccountID const& account, uint256 const& loanID, std::uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanManage;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
del(AccountID const& account, uint256 const& loanID, std::uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanDelete;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfFlags] = flags;
    return jv;
}

Json::Value
pay(AccountID const& account,
    uint256 const& loanID,
    STAmount const& amount,
    std::uint32_t flags)
{
    Json::Value jv;
    jv[sfTransactionType] = jss::LoanPay;
    jv[sfAccount] = to_string(account);
    jv[sfLoanID] = to_string(loanID);
    jv[sfAmount] = amount.getJson();
    jv[sfFlags] = flags;
    return jv;
}

}  // namespace loan
}  // namespace jtx
}  // namespace test
}  // namespace ripple
