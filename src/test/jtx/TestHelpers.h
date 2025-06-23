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

#ifndef RIPPLE_TEST_JTX_TESTHELPERS_H_INCLUDED
#define RIPPLE_TEST_JTX_TESTHELPERS_H_INCLUDED

#include <test/jtx/Env.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <vector>

#if (defined(__clang_major__) && __clang_major__ < 15)
#include <experimental/source_location>
using source_location = std::experimental::source_location;
#else
#include <source_location>
using std::source_location;
#endif

namespace ripple {
namespace test {
namespace jtx {

// TODO We only need this long "requires" clause as polyfill, for C++20
// implementations which are missing <ranges> header. Replace with
// `std::ranges::range<Input>`, and accordingly use std::ranges::begin/end
// when we have moved to better compilers.
template <typename Input>
auto
make_vector(Input const& input)
    requires requires(Input& v) {
        std::begin(v);
        std::end(v);
    }
{
    return std::vector(std::begin(input), std::end(input));
}

// Functions used in debugging
Json::Value
getAccountOffers(Env& env, AccountID const& acct, bool current = false);

inline Json::Value
getAccountOffers(Env& env, Account const& acct, bool current = false)
{
    return getAccountOffers(env, acct.id(), current);
}

Json::Value
getAccountLines(Env& env, AccountID const& acctId);

inline Json::Value
getAccountLines(Env& env, Account const& acct)
{
    return getAccountLines(env, acct.id());
}

template <typename... IOU>
Json::Value
getAccountLines(Env& env, AccountID const& acctId, IOU... ious)
{
    auto const jrr = getAccountLines(env, acctId);
    Json::Value res;
    for (auto const& line : jrr[jss::lines])
    {
        for (auto const& iou : {ious...})
        {
            if (line[jss::currency].asString() == to_string(iou.currency))
            {
                Json::Value v;
                v[jss::currency] = line[jss::currency];
                v[jss::balance] = line[jss::balance];
                v[jss::limit] = line[jss::limit];
                v[jss::account] = line[jss::account];
                res[jss::lines].append(v);
            }
        }
    }
    if (!res.isNull())
        return res;
    return jrr;
}

[[nodiscard]] bool
checkArraySize(Json::Value const& val, unsigned int size);

// Helper function that returns the owner count on an account.
std::uint32_t
ownerCount(test::jtx::Env const& env, test::jtx::Account const& account);

/* Path finding */
/******************************************************************************/
void
stpath_append_one(STPath& st, Account const& account);

template <class T>
std::enable_if_t<std::is_constructible<Account, T>::value>
stpath_append_one(STPath& st, T const& t)
{
    stpath_append_one(st, Account{t});
}

void
stpath_append_one(STPath& st, STPathElement const& pe);

template <class T, class... Args>
void
stpath_append(STPath& st, T const& t, Args const&... args)
{
    stpath_append_one(st, t);
    if constexpr (sizeof...(args) > 0)
        stpath_append(st, args...);
}

template <class... Args>
void
stpathset_append(STPathSet& st, STPath const& p, Args const&... args)
{
    st.push_back(p);
    if constexpr (sizeof...(args) > 0)
        stpathset_append(st, args...);
}

bool
equal(STAmount const& sa1, STAmount const& sa2);

// Issue path element
STPathElement
IPE(Issue const& iss);

template <class... Args>
STPath
stpath(Args const&... args)
{
    STPath st;
    stpath_append(st, args...);
    return st;
}

template <class... Args>
bool
same(STPathSet const& st1, Args const&... args)
{
    STPathSet st2;
    stpathset_append(st2, args...);
    if (st1.size() != st2.size())
        return false;

    for (auto const& p : st2)
    {
        if (std::find(st1.begin(), st1.end(), p) == st1.end())
            return false;
    }
    return true;
}

/******************************************************************************/

XRPAmount
txfee(Env const& env, std::uint16_t n);

PrettyAmount
xrpMinusFee(Env const& env, std::int64_t xrpAmount);

bool
expectLine(
    Env& env,
    AccountID const& account,
    STAmount const& value,
    bool defaultLimits = false);

template <typename... Amts>
bool
expectLine(
    Env& env,
    AccountID const& account,
    STAmount const& value,
    Amts const&... amts)
{
    return expectLine(env, account, value, false) &&
        expectLine(env, account, amts...);
}

bool
expectLine(Env& env, AccountID const& account, None const& value);

bool
expectOffers(
    Env& env,
    AccountID const& account,
    std::uint16_t size,
    std::vector<Amounts> const& toMatch = {});

Json::Value
ledgerEntryRoot(Env& env, Account const& acct);

Json::Value
ledgerEntryState(
    Env& env,
    Account const& acct_a,
    Account const& acct_b,
    std::string const& currency);

Json::Value
accountBalance(Env& env, Account const& acct);

[[nodiscard]] bool
expectLedgerEntryRoot(
    Env& env,
    Account const& acct,
    STAmount const& expectedValue);

/* Payment Channel */
/******************************************************************************/

Json::Value
create(
    AccountID const& account,
    AccountID const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt);

inline Json::Value
create(
    Account const& account,
    Account const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt)
{
    return create(
        account.id(), to.id(), amount, settleDelay, pk, cancelAfter, dstTag);
}

Json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

Json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance = std::nullopt,
    std::optional<STAmount> const& amount = std::nullopt,
    std::optional<Slice> const& signature = std::nullopt,
    std::optional<PublicKey> const& pk = std::nullopt);

uint256
channel(
    AccountID const& account,
    AccountID const& dst,
    std::uint32_t seqProxyValue);

inline uint256
channel(Account const& account, Account const& dst, std::uint32_t seqProxyValue)
{
    return channel(account.id(), dst.id(), seqProxyValue);
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan);

bool
channelExists(ReadView const& view, uint256 const& chan);

/* Crossing Limits */
/******************************************************************************/

void
n_offers(
    Env& env,
    std::size_t n,
    Account const& account,
    STAmount const& in,
    STAmount const& out);

/* Pay Strand */
/***************************************************************/

// Currency path element
STPathElement
cpe(Currency const& c);

// All path element
STPathElement
allpe(AccountID const& a, Issue const& iss);
/***************************************************************/

/* Check */
/***************************************************************/
namespace check {

/** Create a check. */
// clang-format off
template <typename A>
    requires std::is_same_v<A, AccountID>
Json::Value
create(A const& account, A const& dest, STAmount const& sendMax)
{
    Json::Value jv;
    jv[sfAccount.jsonName] = to_string(account);
    jv[sfSendMax.jsonName] = sendMax.getJson(JsonOptions::none);
    jv[sfDestination.jsonName] = to_string(dest);
    jv[sfTransactionType.jsonName] = jss::CheckCreate;
    return jv;
}
// clang-format on

inline Json::Value
create(
    jtx::Account const& account,
    jtx::Account const& dest,
    STAmount const& sendMax)
{
    return create(account.id(), dest.id(), sendMax);
}

}  // namespace check

static constexpr FeeLevel64 baseFeeLevel{256};
static constexpr FeeLevel64 minEscalationFeeLevel = baseFeeLevel * 500;

template <class Suite>
void
checkMetrics(
    Suite& test,
    jtx::Env& env,
    std::size_t expectedCount,
    std::optional<std::size_t> expectedMaxCount,
    std::size_t expectedInLedger,
    std::size_t expectedPerLedger,
    std::uint64_t expectedMinFeeLevel = baseFeeLevel.fee(),
    std::uint64_t expectedMedFeeLevel = minEscalationFeeLevel.fee(),
    source_location const location = source_location::current())
{
    int line = location.line();
    char const* file = location.file_name();
    FeeLevel64 const expectedMin{expectedMinFeeLevel};
    FeeLevel64 const expectedMed{expectedMedFeeLevel};
    auto const metrics = env.app().getTxQ().getMetrics(*env.current());
    using namespace std::string_literals;

    metrics.referenceFeeLevel == baseFeeLevel
        ? test.pass()
        : test.fail(
              "reference: "s +
                  std::to_string(metrics.referenceFeeLevel.value()) + "/" +
                  std::to_string(baseFeeLevel.value()),
              file,
              line);

    metrics.txCount == expectedCount
        ? test.pass()
        : test.fail(
              "txCount: "s + std::to_string(metrics.txCount) + "/" +
                  std::to_string(expectedCount),
              file,
              line);

    metrics.txQMaxSize == expectedMaxCount
        ? test.pass()
        : test.fail(
              "txQMaxSize: "s + std::to_string(metrics.txQMaxSize.value_or(0)) +
                  "/" + std::to_string(expectedMaxCount.value_or(0)),
              file,
              line);

    metrics.txInLedger == expectedInLedger
        ? test.pass()
        : test.fail(
              "txInLedger: "s + std::to_string(metrics.txInLedger) + "/" +
                  std::to_string(expectedInLedger),
              file,
              line);

    metrics.txPerLedger == expectedPerLedger
        ? test.pass()
        : test.fail(
              "txPerLedger: "s + std::to_string(metrics.txPerLedger) + "/" +
                  std::to_string(expectedPerLedger),
              file,
              line);

    metrics.minProcessingFeeLevel == expectedMin
        ? test.pass()
        : test.fail(
              "minProcessingFeeLevel: "s +
                  std::to_string(metrics.minProcessingFeeLevel.value()) + "/" +
                  std::to_string(expectedMin.value()),
              file,
              line);

    metrics.medFeeLevel == expectedMed
        ? test.pass()
        : test.fail(
              "medFeeLevel: "s + std::to_string(metrics.medFeeLevel.value()) +
                  "/" + std::to_string(expectedMed.value()),
              file,
              line);

    auto const expectedCurFeeLevel = expectedInLedger > expectedPerLedger
        ? expectedMed * expectedInLedger * expectedInLedger /
            (expectedPerLedger * expectedPerLedger)
        : metrics.referenceFeeLevel;

    metrics.openLedgerFeeLevel == expectedCurFeeLevel
        ? test.pass()
        : test.fail(
              "openLedgerFeeLevel: "s +
                  std::to_string(metrics.openLedgerFeeLevel.value()) + "/" +
                  std::to_string(expectedCurFeeLevel.value()),
              file,
              line);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_TESTHELPERS_H_INCLUDED
