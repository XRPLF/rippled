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
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/jss.h>

#include <vector>

namespace ripple {
namespace test {
namespace jtx {

/** Generic helper class for helper clases that set a field on a JTx.

 Not every helper will be able to use this because of conversions and other
 issues, but for classes where it's straightforward, this can simplify things.
*/
template <
    class SField,
    class StoredValue = typename SField::type::value_type,
    class OutputValue = StoredValue>
struct JTxField
{
    using SF = SField;
    using SV = StoredValue;
    using OV = OutputValue;

protected:
    SF const& sfield_;
    SV value_;

public:
    explicit JTxField(SF const& sfield, SV const& value)
        : sfield_(sfield), value_(value)
    {
    }

    virtual ~JTxField() = default;

    virtual OV
    value() const = 0;

    virtual void
    operator()(Env&, JTx& jt) const
    {
        jt.jv[sfield_.jsonName] = value();
    }
};

template <class SField, class StoredValue>
struct JTxField<SField, StoredValue, StoredValue>
{
    using SF = SField;
    using SV = StoredValue;
    using OV = SV;

protected:
    SF const& sfield_;
    SV value_;

public:
    explicit JTxField(SF const& sfield, SV const& value)
        : sfield_(sfield), value_(value)
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.jv[sfield_.jsonName] = value_;
    }
};

struct timePointField
    : public JTxField<SF_UINT32, NetClock::time_point, NetClock::rep>
{
    using SF = SF_UINT32;
    using SV = NetClock::time_point;
    using OV = NetClock::rep;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit timePointField(SF const& sfield, SV const& value)
        : JTxField(sfield, value)
    {
    }

    OV
    value() const override
    {
        return value_.time_since_epoch().count();
    }
};

struct uint256Field : public JTxField<SF_UINT256, uint256, std::string>
{
    using SF = SF_UINT256;
    using SV = uint256;
    using OV = std::string;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit uint256Field(SF const& sfield, SV const& value)
        : JTxField(sfield, value)
    {
    }

    OV
    value() const override
    {
        return to_string(value_);
    }
};

struct accountIDField : public JTxField<SF_ACCOUNT, AccountID, std::string>
{
    using SF = SF_ACCOUNT;
    using SV = AccountID;
    using OV = std::string;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit accountIDField(SF const& sfield, SV const& value)
        : JTxField(sfield, value)
    {
    }

    OV
    value() const override
    {
        return toBase58(value_);
    }
};

struct blobField : public JTxField<SF_VL, std::string>
{
    using SF = SF_VL;
    using SV = std::string;
    using base = JTxField<SF, SV, SV>;

    using JTxField::JTxField;

    explicit blobField(SF const& sfield, Slice const& cond)
        : JTxField(sfield, strHex(cond))
    {
    }

    template <size_t N>
    explicit blobField(SF const& sfield, std::array<std::uint8_t, N> const& c)
        : blobField(sfield, makeSlice(c))
    {
    }
};

template <class SField, class UnitTag, class ValueType>
struct valueUnitField
    : public JTxField<SField, unit::ValueUnit<UnitTag, ValueType>, ValueType>
{
    using SF = SField;
    using SV = unit::ValueUnit<UnitTag, ValueType>;
    using OV = ValueType;
    using base = JTxField<SF, SV, OV>;

    static_assert(std::is_same_v<OV, typename SField::type::value_type>);

protected:
    using base::value_;

public:
    using JTxField<SF, SV, OV>::JTxField;

    OV
    value() const override
    {
        return value_.value();
    }
};

template <class JTxField>
struct JTxFieldWrapper
{
    using JF = JTxField;
    using SF = typename JF::SF;
    using SV = typename JF::SV;

protected:
    SF const& sfield_;

public:
    explicit JTxFieldWrapper(SF const& sfield) : sfield_(sfield)
    {
    }

    JF
    operator()(SV const& value) const
    {
        return JTxField(sfield_, value);
    }
};

template <>
struct JTxFieldWrapper<blobField>
{
    using JF = blobField;
    using SF = JF::SF;
    using SV = JF::SV;

protected:
    SF const& sfield_;

public:
    explicit JTxFieldWrapper(SF const& sfield) : sfield_(sfield)
    {
    }

    JF
    operator()(SV const& cond) const
    {
        return JF(sfield_, makeSlice(cond));
    }

    JF
    operator()(Slice const& cond) const
    {
        return JF(sfield_, cond);
    }

    template <size_t N>
    JF
    operator()(std::array<std::uint8_t, N> const& c) const
    {
        return operator()(makeSlice(c));
    }
};

template <
    class SField,
    class UnitTag,
    class ValueType = typename SField::type::value_type>
using valueUnitWrapper =
    JTxFieldWrapper<valueUnitField<SField, UnitTag, ValueType>>;

template <class SField, class StoredValue = typename SField::type::value_type>
using simpleField = JTxFieldWrapper<JTxField<SField, StoredValue>>;

/** General field definitions, or fields used in multiple transaction namespaces
 */
auto const data = JTxFieldWrapper<blobField>(sfData);

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

[[nodiscard]]
inline bool
checkVL(Slice const& result, std::string expected)
{
    Serializer s;
    s.addRaw(result);
    return s.getString() == expected;
}

[[nodiscard]]
inline bool
checkVL(
    std::shared_ptr<SLE const> const& sle,
    SField const& field,
    std::string const& expected)
{
    return strHex(expected) == strHex(sle->getFieldVL(field));
}

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

/* Escrow */
/******************************************************************************/

Json::Value
escrow(AccountID const& account, AccountID const& to, STAmount const& amount);

inline Json::Value
escrow(Account const& account, Account const& to, STAmount const& amount)
{
    return escrow(account.id(), to.id(), amount);
}

Json::Value
finish(AccountID const& account, AccountID const& from, std::uint32_t seq);

inline Json::Value
finish(Account const& account, Account const& from, std::uint32_t seq)
{
    return finish(account.id(), from.id(), seq);
}

Json::Value
cancel(AccountID const& account, Account const& from, std::uint32_t seq);

inline Json::Value
cancel(Account const& account, Account const& from, std::uint32_t seq)
{
    return cancel(account.id(), from, seq);
}

std::array<std::uint8_t, 39> constexpr cb1 = {
    {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
     0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
     0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
     0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

// A PreimageSha256 fulfillments and its associated condition.
std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

/** Set the "FinishAfter" time tag on a JTx */
auto const finish_time = JTxFieldWrapper<timePointField>(sfFinishAfter);

/** Set the "CancelAfter" time tag on a JTx */
auto const cancel_time = JTxFieldWrapper<timePointField>(sfCancelAfter);

auto const condition = JTxFieldWrapper<blobField>(sfCondition);

auto const fulfillment = JTxFieldWrapper<blobField>(sfFulfillment);

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
    jv[sfFlags.jsonName] = tfUniversal;
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

/* LoanBroker */
/******************************************************************************/

namespace loanBroker {

Json::Value
set(AccountID const& account, uint256 const& vaultId, std::uint32_t flags = 0);

// Use "del" because "delete" is a reserved word in C++.
Json::Value
del(AccountID const& account,
    uint256 const& loanBrokerID,
    std::uint32_t flags = 0);

Json::Value
coverDeposit(
    AccountID const& account,
    uint256 const& loanBrokerID,
    STAmount const& amount,
    std::uint32_t flags = 0);

Json::Value
coverWithdraw(
    AccountID const& account,
    uint256 const& loanBrokerID,
    STAmount const& amount,
    std::uint32_t flags = 0);

auto const loanBrokerID = JTxFieldWrapper<uint256Field>(sfLoanBrokerID);

auto const managementFeeRate =
    valueUnitWrapper<SF_UINT16, unit::TenthBipsTag>(sfManagementFeeRate);

auto const debtMaximum = simpleField<SF_NUMBER>(sfDebtMaximum);

auto const coverRateMinimum =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCoverRateMinimum);

auto const coverRateLiquidation =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCoverRateLiquidation);

}  // namespace loanBroker

/* Loan */
/******************************************************************************/
namespace loan {

Json::Value
set(AccountID const& account,
    uint256 const& loanBrokerID,
    Number principalRequested,
    NetClock::time_point const& startDate,
    std::uint32_t flags = 0);

auto const counterparty = JTxFieldWrapper<accountIDField>(sfCounterparty);

// For `CounterPartySignature`, use `sig(sfCounterpartySignature, ...)`

auto const loanOriginationFee = simpleField<SF_NUMBER>(sfLoanOriginationFee);

auto const loanServiceFee = simpleField<SF_NUMBER>(sfLoanServiceFee);

auto const latePaymentFee = simpleField<SF_NUMBER>(sfLatePaymentFee);

auto const closePaymentFee = simpleField<SF_NUMBER>(sfClosePaymentFee);

auto const overpaymentFee =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfOverpaymentFee);

auto const interestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfInterestRate);

auto const lateInterestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfLateInterestRate);

auto const closeInterestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCloseInterestRate);

auto const overpaymentInterestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfOverpaymentInterestRate);

auto const paymentTotal = simpleField<SF_UINT32>(sfPaymentTotal);

auto const paymentInterval = simpleField<SF_UINT32>(sfPaymentInterval);

auto const gracePeriod = simpleField<SF_UINT32>(sfGracePeriod);

Json::Value
manage(AccountID const& account, uint256 const& loanID, std::uint32_t flags);

}  // namespace loan

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif  // RIPPLE_TEST_JTX_TESTHELPERS_H_INCLUDED
