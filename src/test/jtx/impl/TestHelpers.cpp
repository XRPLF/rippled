//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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

/* Token (IOU/MPT) Locking */
/******************************************************************************/
uint64_t
mptEscrowed(
    jtx::Env const& env,
    jtx::Account const& account,
    jtx::MPT const& mpt)
{
    auto const sle = env.le(keylet::mptoken(mpt.mpt(), account));
    if (sle && sle->isFieldPresent(sfLockedAmount))
        return (*sle)[sfLockedAmount];
    return 0;
}

uint64_t
issuerMPTEscrowed(jtx::Env const& env, jtx::MPT const& mpt)
{
    auto const sle = env.le(keylet::mptIssuance(mpt.mpt()));
    if (sle && sle->isFieldPresent(sfLockedAmount))
        return (*sle)[sfLockedAmount];
    return 0;
}

jtx::PrettyAmount
issuerBalance(jtx::Env& env, jtx::Account const& account, Issue const& issue)
{
    Json::Value params;
    params[jss::account] = account.human();
    auto jrr = env.rpc("json", "gateway_balances", to_string(params));
    auto const result = jrr[jss::result];
    auto const obligations =
        result[jss::obligations][to_string(issue.currency)];
    if (obligations.isNull())
        return {STAmount(issue, 0), account.name()};
    STAmount const amount = amountFromString(issue, obligations.asString());
    return {amount, account.name()};
}

jtx::PrettyAmount
issuerEscrowed(jtx::Env& env, jtx::Account const& account, Issue const& issue)
{
    Json::Value params;
    params[jss::account] = account.human();
    auto jrr = env.rpc("json", "gateway_balances", to_string(params));
    auto const result = jrr[jss::result];
    auto const locked = result[jss::locked][to_string(issue.currency)];
    if (locked.isNull())
        return {STAmount(issue, 0), account.name()};
    STAmount const amount = amountFromString(issue, locked.asString());
    return {amount, account.name()};
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
expectLine(
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
expectLine(Env& env, AccountID const& account, None const& value)
{
    return !env.le(keylet::line(account, value.issue));
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

}  // namespace jtx
}  // namespace test
}  // namespace ripple
