#pragma once

#include <test/jtx/Env.h>

#include <xrpld/app/misc/TxQ.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/paths/detail/Steps.h>

#include <algorithm>
#include <source_location>
#include <utility>
#include <vector>

namespace xrpl::test::jtx {

/** Generic helper class for helper classes that set a field on a JTx.

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
    explicit JTxField(SF const& sfield, SV value) : sfield_(sfield), value_(std::move(value))
    {
    }

    virtual ~JTxField() = default;

    [[nodiscard]] virtual OV
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
    explicit JTxField(SF const& sfield, SV value) : sfield_(sfield), value_(std::move(value))
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt.jv[sfield_.jsonName] = value_;
    }
};

struct TimePointField : public JTxField<SF_UINT32, NetClock::time_point, NetClock::rep>
{
    using SF = SF_UINT32;
    using SV = NetClock::time_point;
    using OV = NetClock::rep;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit TimePointField(SF const& sfield, SV const& value) : JTxField(sfield, value)
    {
    }

    [[nodiscard]] OV
    value() const override
    {
        return value_.time_since_epoch().count();
    }
};

struct UInt256Field : public JTxField<SF_UINT256, uint256, std::string>
{
    using SF = SF_UINT256;
    using SV = uint256;
    using OV = std::string;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit UInt256Field(SF const& sfield, SV const& value) : JTxField(sfield, value)
    {
    }

    [[nodiscard]] OV
    value() const override
    {
        return to_string(value_);
    }
};

struct AccountIdField : public JTxField<SF_ACCOUNT, AccountID, std::string>
{
    using SF = SF_ACCOUNT;
    using SV = AccountID;
    using OV = std::string;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit AccountIdField(SF const& sfield, SV const& value) : JTxField(sfield, value)
    {
    }

    [[nodiscard]] OV
    value() const override
    {
        return toBase58(value_);
    }
};

struct StAmountField : public JTxField<SF_AMOUNT, STAmount, json::Value>
{
    using SF = SF_AMOUNT;
    using SV = STAmount;
    using OV = json::Value;
    using base = JTxField<SF, SV, OV>;

protected:
    using base::value_;

public:
    explicit StAmountField(SF const& sfield, SV const& value) : JTxField(sfield, value)
    {
    }

    [[nodiscard]] OV
    value() const override
    {
        return value_.getJson(JsonOptions::Values::None);
    }
};

struct BlobField : public JTxField<SF_VL, std::string>
{
    using SF = SF_VL;
    using SV = std::string;
    using base = JTxField<SF, SV, SV>;

    using JTxField::JTxField;

    explicit BlobField(SF const& sfield, Slice const& cond) : JTxField(sfield, strHex(cond))
    {
    }

    template <size_t N>
    explicit BlobField(SF const& sfield, std::array<std::uint8_t, N> const& c)
        : BlobField(sfield, makeSlice(c))
    {
    }
};

template <class SField, class UnitTag, class ValueType>
struct ValueUnitField : public JTxField<SField, unit::ValueUnit<UnitTag, ValueType>, ValueType>
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

    [[nodiscard]] OV
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
struct JTxFieldWrapper<BlobField>
{
    using JF = BlobField;
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

template <class SField, class UnitTag, class ValueType = typename SField::type::value_type>
using valueUnitWrapper = JTxFieldWrapper<ValueUnitField<SField, UnitTag, ValueType>>;

template <class SField, class StoredValue = typename SField::type::value_type>
using simpleField = JTxFieldWrapper<JTxField<SField, StoredValue>>;

/** General field definitions, or fields used in multiple transaction namespaces
 */
auto const kData = JTxFieldWrapper<BlobField>(sfData);

auto const kAmount = JTxFieldWrapper<StAmountField>(sfAmount);

// TODO We only need this long "requires" clause as polyfill, for C++20
// implementations which are missing <ranges> header. Replace with
// `std::ranges::range<Input>`, and accordingly use std::ranges::begin/end
// when we have moved to better compilers.
template <typename Input>
auto
makeVector(Input const& input)
    requires requires(Input& v) {
        std::begin(v);
        std::end(v);
    }
{
    return std::vector(std::begin(input), std::end(input));
}

// Functions used in debugging
json::Value
getAccountOffers(Env& env, AccountID const& acct, bool current = false);

inline json::Value
getAccountOffers(Env& env, Account const& acct, bool current = false)
{
    return getAccountOffers(env, acct.id(), current);
}

json::Value
getAccountLines(Env& env, AccountID const& acctId);

inline json::Value
getAccountLines(Env& env, Account const& acct)
{
    return getAccountLines(env, acct.id());
}

template <typename... IOU>
json::Value
getAccountLines(Env& env, AccountID const& acctId, IOU... ious)
{
    auto jrr = getAccountLines(env, acctId);
    json::Value res;
    for (auto const& line : jrr[jss::lines])
    {
        for (auto const& iou : {ious...})
        {
            if (line[jss::currency].asString() == to_string(iou.currency))
            {
                json::Value v;
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
checkArraySize(json::Value const& val, unsigned int size);

// Helper function that returns the owner count on an account.
std::uint32_t
ownerCount(test::jtx::Env const& env, test::jtx::Account const& account);

[[nodiscard]]
inline bool
checkVL(Slice const& result, std::string const& expected)
{
    Serializer s;
    s.addRaw(result);
    return s.getString() == expected;
}

[[nodiscard]]
inline bool
checkVL(std::shared_ptr<SLE const> const& sle, SField const& field, std::string const& expected)
{
    return strHex(expected) == strHex(sle->getFieldVL(field));
}

/* Path finding */
/******************************************************************************/
void
stpathAppendOne(STPath& st, Account const& account);

template <class T>
std::enable_if_t<std::is_constructible_v<Account, T>>
stpathAppendOne(STPath& st, T const& t)
{
    stpathAppendOne(st, Account{t});
}

void
stpathAppendOne(STPath& st, STPathElement const& pe);

template <class T, class... Args>
void
stpathAppend(STPath& st, T const& t, Args const&... args)
{
    stpathAppendOne(st, t);
    if constexpr (sizeof...(args) > 0)
        stpathAppend(st, args...);
}

template <class... Args>
void
stpathsetAppend(STPathSet& st, STPath const& p, Args const&... args)
{
    st.pushBack(p);
    if constexpr (sizeof...(args) > 0)
        stpathsetAppend(st, args...);
}

bool
equal(STAmount const& sa1, STAmount const& sa2);

template <class... Args>
STPath
stpath(Args const&... args)
{
    STPath st;
    stpathAppend(st, args...);
    return st;
}

template <class... Args>
bool
same(STPathSet const& st1, Args const&... args)
{
    STPathSet st2;
    stpathsetAppend(st2, args...);
    if (st1.size() != st2.size())
        return false;

    for (auto const& p : st2)
    {
        if (std::ranges::find(st1, p) == st1.end())
            return false;
    }
    return true;
}

json::Value
rpf(jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& dstAmount,
    std::optional<STAmount> const& sendMax = std::nullopt,
    std::optional<PathAsset> const& srcAsset = std::nullopt,
    std::optional<AccountID> const& srcIssuer = std::nullopt);

jtx::Env
pathTestEnv(beast::unit_test::Suite& suite);

class Gate
{
private:
    std::condition_variable cv_;
    std::mutex mutex_;
    bool signaled_ = false;

public:
    // Thread safe, blocks until signaled or period expires.
    // Returns `true` if signaled.
    template <class Rep, class Period>
    bool
    waitFor(std::chrono::duration<Rep, Period> const& relTime)
    {
        std::unique_lock<std::mutex> lk(mutex_);
        auto b = cv_.wait_for(lk, relTime, [this] { return signaled_; });
        signaled_ = false;
        return b;
    }

    void
    signal()
    {
        std::scoped_lock const lk(mutex_);
        signaled_ = true;
        cv_.notify_all();
    }
};

json::Value
findPathsRequest(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax = std::nullopt,
    std::optional<PathAsset> const& srcAsset = std::nullopt,
    std::optional<AccountID> const& srcIssuer = std::nullopt,
    std::optional<uint256> const& domain = std::nullopt);

std::tuple<STPathSet, STAmount, STAmount>
findPaths(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax = std::nullopt,
    std::optional<PathAsset> const& srcAsset = std::nullopt,
    std::optional<AccountID> const& srcIssuer = std::nullopt,
    std::optional<uint256> const& domain = std::nullopt);

std::tuple<STPathSet, STAmount, STAmount>
findPathsByElement(
    jtx::Env& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    STAmount const& saDstAmount,
    std::optional<STAmount> const& saSendMax = std::nullopt,
    std::optional<STPathElement> const& srcElement = std::nullopt,
    std::optional<AccountID> const& srcIssuer = std::nullopt,
    std::optional<uint256> const& domain = std::nullopt);

/******************************************************************************/

XRPAmount
txFee(Env const& env, std::uint16_t n);

PrettyAmount
xrpMinusFee(Env const& env, std::int64_t xrpAmount);

bool
expectHolding(
    Env& env,
    AccountID const& account,
    STAmount const& value,
    bool defaultLimits = false);

template <typename... Amts>
bool
expectHolding(Env& env, AccountID const& account, STAmount const& value, Amts const&... amts)
{
    return expectHolding(env, account, value, false) && expectHolding(env, account, amts...);
}

bool
expectHolding(Env& env, AccountID const& account, None const& value);

bool
expectMPT(Env& env, AccountID const& account, STAmount const& value);

bool
expectOffers(
    Env& env,
    AccountID const& account,
    std::uint16_t size,
    std::vector<Amounts> const& toMatch = {});

json::Value
ledgerEntryRoot(Env& env, Account const& acct);

json::Value
ledgerEntryState(Env& env, Account const& acctA, Account const& acctB, std::string const& currency);

json::Value
ledgerEntryOffer(jtx::Env& env, jtx::Account const& acct, std::uint32_t offerSeq);

json::Value
ledgerEntryMPT(jtx::Env& env, jtx::Account const& acct, MPTID const& mptID);

json::Value
getBookOffers(jtx::Env& env, Asset const& takerPays, Asset const& takerGets);

json::Value
accountBalance(Env& env, Account const& acct);

[[nodiscard]] bool
expectLedgerEntryRoot(Env& env, Account const& acct, STAmount const& expectedValue);

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
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt);

inline json::Value
create(
    Account const& account,
    Account const& to,
    STAmount const& amount,
    NetClock::duration const& settleDelay,
    PublicKey const& pk,
    std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
    std::optional<std::uint32_t> const& dstTag = std::nullopt)
{
    return create(account.id(), to.id(), amount, settleDelay, pk, cancelAfter, dstTag);
}

json::Value
fund(
    AccountID const& account,
    uint256 const& channel,
    STAmount const& amount,
    std::optional<NetClock::time_point> const& expiration = std::nullopt);

json::Value
claim(
    AccountID const& account,
    uint256 const& channel,
    std::optional<STAmount> const& balance = std::nullopt,
    std::optional<STAmount> const& amount = std::nullopt,
    std::optional<Slice> const& signature = std::nullopt,
    std::optional<PublicKey> const& pk = std::nullopt);

uint256
channel(AccountID const& account, AccountID const& dst, std::uint32_t seqProxyValue);

inline uint256
channel(Account const& account, Account const& dst, std::uint32_t seqProxyValue)
{
    return channel(account.id(), dst.id(), seqProxyValue);
}

STAmount
channelBalance(ReadView const& view, uint256 const& chan);

bool
channelExists(ReadView const& view, uint256 const& chan);

}  // namespace paychan

/* Crossing Limits */
/******************************************************************************/

void
nOffers(Env& env, std::size_t n, Account const& account, STAmount const& in, STAmount const& out);

/* Pay Strand */
/***************************************************************/

struct DirectStepInfo
{
    AccountID src;
    AccountID dst;
    Currency currency;
};

struct MPTEndpointStepInfo
{
    AccountID src;
    AccountID dst;
    MPTID mptid;
};

struct XRPEndpointStepInfo
{
    AccountID acc;
};

// Currency/MPTID path element
STPathElement
cpe(PathAsset const& pa);

// Currency/MPTID and issuer path element
STPathElement
ipe(Asset const& asset);

// Issuer path element
STPathElement
iape(AccountID const& account);

// Account path element
STPathElement
ape(AccountID const& a);

// All path element
STPathElement
allPathElements(AccountID const& a, Asset const& asset);

bool
equal(std::unique_ptr<Step> const& s1, DirectStepInfo const& dsi);

bool
equal(std::unique_ptr<Step> const& s1, MPTEndpointStepInfo const& dsi);

bool
equal(std::unique_ptr<Step> const& s1, XRPEndpointStepInfo const& xrpStepInfo);

bool
equal(std::unique_ptr<Step> const& s1, xrpl::Book const& bsi);

template <class Iter>
bool
strandEqualHelper(Iter i)
{
    // base case. all args processed and found equal.
    return true;
}

template <class Iter, class StepInfo, class... Args>
bool
strandEqualHelper(Iter i, StepInfo&& si, Args&&... args)
{
    if (!jtx::equal(*i, std::forward<StepInfo>(si)))
        return false;
    return strandEqualHelper(++i, std::forward<Args>(args)...);
}

template <class... Args>
bool
equal(Strand const& strand, Args&&... args)
{
    if (strand.size() != sizeof...(Args))
        return false;
    if (strand.empty())
        return true;
    return strandEqualHelper(strand.begin(), std::forward<Args>(args)...);
}

/***************************************************************/

/* Check */
/***************************************************************/
namespace check {

/** Create a check. */
template <typename A>
    requires std::is_same_v<A, AccountID>
json::Value
create(A const& account, A const& dest, STAmount const& sendMax)
{
    json::Value jv;
    jv[sfAccount.jsonName] = to_string(account);
    jv[sfSendMax.jsonName] = sendMax.getJson(JsonOptions::Values::None);
    jv[sfDestination.jsonName] = to_string(dest);
    jv[sfTransactionType.jsonName] = jss::CheckCreate;
    return jv;
}

inline json::Value
create(jtx::Account const& account, jtx::Account const& dest, STAmount const& sendMax)
{
    return create(account.id(), dest.id(), sendMax);
}

}  // namespace check

static constexpr FeeLevel64 kBaseFeeLevel{TxQ::kBaseLevel};
static constexpr FeeLevel64 kMinEscalationFeeLevel = kBaseFeeLevel * 500;

inline uint256
getCheckIndex(AccountID const& account, std::uint32_t uSequence)
{
    return keylet::check(account, uSequence).key;
}

template <class Suite>
void
checkMetrics(
    Suite& test,
    jtx::Env& env,
    std::size_t expectedCount,
    std::optional<std::size_t> expectedMaxCount,
    std::size_t expectedInLedger,
    std::size_t expectedPerLedger,
    std::uint64_t expectedMinFeeLevel = kBaseFeeLevel.fee(),
    std::uint64_t expectedMedFeeLevel = kMinEscalationFeeLevel.fee(),
    std::source_location const location = std::source_location::current())
{
    int const line = location.line();
    char const* file = location.file_name();
    FeeLevel64 const expectedMin{expectedMinFeeLevel};
    FeeLevel64 const expectedMed{expectedMedFeeLevel};
    auto const metrics = env.app().getTxQ().getMetrics(*env.current());
    using namespace std::string_literals;

    metrics.referenceFeeLevel == kBaseFeeLevel
        ? test.pass()
        : test.fail(
              "reference: "s + std::to_string(metrics.referenceFeeLevel.value()) + "/" +
                  std::to_string(kBaseFeeLevel.value()),
              file,
              line);

    metrics.txCount == expectedCount
        ? test.pass()
        : test.fail(
              "txCount: "s + std::to_string(metrics.txCount) + "/" + std::to_string(expectedCount),
              file,
              line);

    metrics.txQMaxSize == expectedMaxCount
        ? test.pass()
        : test.fail(
              "txQMaxSize: "s + std::to_string(metrics.txQMaxSize.value_or(0)) + "/" +
                  std::to_string(expectedMaxCount.value_or(0)),
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
              "minProcessingFeeLevel: "s + std::to_string(metrics.minProcessingFeeLevel.value()) +
                  "/" + std::to_string(expectedMin.value()),
              file,
              line);

    metrics.medFeeLevel == expectedMed
        ? test.pass()
        : test.fail(
              "medFeeLevel: "s + std::to_string(metrics.medFeeLevel.value()) + "/" +
                  std::to_string(expectedMed.value()),
              file,
              line);

    auto const expectedCurFeeLevel = expectedInLedger > expectedPerLedger
        ? expectedMed * expectedInLedger * expectedInLedger /
            (expectedPerLedger * expectedPerLedger)
        : metrics.referenceFeeLevel;

    metrics.openLedgerFeeLevel == expectedCurFeeLevel
        ? test.pass()
        : test.fail(
              "openLedgerFeeLevel: "s + std::to_string(metrics.openLedgerFeeLevel.value()) + "/" +
                  std::to_string(expectedCurFeeLevel.value()),
              file,
              line);
}

/* LoanBroker */
/******************************************************************************/

namespace loanBroker {

json::Value
set(AccountID const& account, uint256 const& vaultId, std::uint32_t flags = 0);

// Use "del" because "delete" is a reserved word in C++.
json::Value
del(AccountID const& account, uint256 const& brokerID, std::uint32_t flags = 0);

json::Value
coverDeposit(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    std::uint32_t flags = 0);

json::Value
coverWithdraw(
    AccountID const& account,
    uint256 const& brokerID,
    STAmount const& amount,
    std::uint32_t flags = 0);

// Must specify at least one of loanBrokerID or amount.
json::Value
coverClawback(AccountID const& account, std::uint32_t flags = 0);

auto const kLoanBrokerId = JTxFieldWrapper<UInt256Field>(sfLoanBrokerID);

auto const kManagementFeeRate =
    valueUnitWrapper<SF_UINT16, unit::TenthBipsTag>(sfManagementFeeRate);

auto const kDebtMaximum = simpleField<SF_NUMBER>(sfDebtMaximum);

auto const kCoverRateMinimum = valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCoverRateMinimum);

auto const kCoverRateLiquidation =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCoverRateLiquidation);

auto const kDestination = JTxFieldWrapper<AccountIdField>(sfDestination);

}  // namespace loanBroker

/* Loan */
/******************************************************************************/
namespace loan {

json::Value
set(AccountID const& account,
    uint256 const& loanBrokerID,
    Number principalRequested,
    std::uint32_t flags = 0);

auto const kCounterparty = JTxFieldWrapper<AccountIdField>(sfCounterparty);

// For `CounterPartySignature`, use `Sig(sfCounterpartySignature, ...)`

auto const kLoanOriginationFee = simpleField<SF_NUMBER>(sfLoanOriginationFee);

auto const kLoanServiceFee = simpleField<SF_NUMBER>(sfLoanServiceFee);

auto const kLatePaymentFee = simpleField<SF_NUMBER>(sfLatePaymentFee);

auto const kClosePaymentFee = simpleField<SF_NUMBER>(sfClosePaymentFee);

auto const kOverpaymentFee = valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfOverpaymentFee);

auto const kInterestRate = valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfInterestRate);

auto const kLateInterestRate = valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfLateInterestRate);

auto const kCloseInterestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfCloseInterestRate);

auto const kOverpaymentInterestRate =
    valueUnitWrapper<SF_UINT32, unit::TenthBipsTag>(sfOverpaymentInterestRate);

auto const kPaymentTotal = simpleField<SF_UINT32>(sfPaymentTotal);

auto const kPaymentInterval = simpleField<SF_UINT32>(sfPaymentInterval);

auto const kGracePeriod = simpleField<SF_UINT32>(sfGracePeriod);

json::Value
manage(AccountID const& account, uint256 const& loanID, std::uint32_t flags);

json::Value
del(AccountID const& account, uint256 const& loanID, std::uint32_t flags = 0);

json::Value
pay(AccountID const& account,
    uint256 const& loanID,
    STAmount const& amount,
    std::uint32_t flags = 0);

}  // namespace loan

/** Set Expiration on a JTx. */
class Expiration
{
private:
    std::uint32_t const expiry_;

public:
    explicit Expiration(NetClock::time_point const& expiry)
        : expiry_{expiry.time_since_epoch().count()}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfExpiration.jsonName] = expiry_;
    }
};

/** Set SourceTag on a JTx. */
class SourceTag
{
private:
    std::uint32_t const tag_;

public:
    explicit SourceTag(std::uint32_t tag) : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfSourceTag.jsonName] = tag_;
    }
};

/** Set DestinationTag on a JTx. */
class DestTag
{
private:
    std::uint32_t const tag_;

public:
    explicit DestTag(std::uint32_t tag) : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfDestinationTag.jsonName] = tag_;
    }
};

struct IssuerArgs
{
    jtx::Env& env;
    // 3-letter currency if Issue, ignored if MPT
    std::string token;
    jtx::Account issuer;
    std::vector<jtx::Account> holders = {};  // NOLINT(readability-redundant-member-init)
    // trust-limit if Issue, maxAmount if MPT
    std::optional<std::uint64_t> limit = std::nullopt;
    // 0-50'000 (0-50%)
    std::uint16_t transferFee = 0;
};

namespace detail {

IOU
issueHelperIOU(IssuerArgs const& args);

MPT
issueHelperMPT(IssuerArgs const& args);

}  // namespace detail

template <typename TTester>
void
testHelper2TokensMix(TTester&& tester)
{
    tester(detail::issueHelperMPT, detail::issueHelperMPT);
    tester(detail::issueHelperIOU, detail::issueHelperMPT);
    tester(detail::issueHelperMPT, detail::issueHelperIOU);
}

template <typename TTester>
void
testHelper3TokensMix(TTester&& tester)
{
    tester(detail::issueHelperMPT, detail::issueHelperMPT, detail::issueHelperMPT);
    tester(detail::issueHelperMPT, detail::issueHelperMPT, detail::issueHelperIOU);
    tester(detail::issueHelperMPT, detail::issueHelperIOU, detail::issueHelperMPT);
    tester(detail::issueHelperMPT, detail::issueHelperIOU, detail::issueHelperIOU);
    tester(detail::issueHelperIOU, detail::issueHelperMPT, detail::issueHelperMPT);
    tester(detail::issueHelperIOU, detail::issueHelperMPT, detail::issueHelperIOU);
    tester(detail::issueHelperIOU, detail::issueHelperIOU, detail::issueHelperMPT);
}

}  // namespace xrpl::test::jtx
