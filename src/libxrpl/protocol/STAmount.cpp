#include <xrpl/protocol/STAmount.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xrpl {

static std::uint64_t const kTenTO14 = 100000000000000ull;
static std::uint64_t const kTenTO14M1 = kTenTO14 - 1;
static std::uint64_t const kTenTO17 = kTenTO14 * 1000;

//------------------------------------------------------------------------------
static std::int64_t
getInt64Value(STAmount const& amount, bool valid, char const* error)
{
    if (!valid)
        Throw<std::runtime_error>(error);
    XRPL_ASSERT(amount.exponent() == 0, "xrpl::getInt64Value : exponent is zero");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    XRPL_ASSERT(
        static_cast<std::uint64_t>(ret) == amount.mantissa(),
        "xrpl::getInt64Value : mantissa must roundtrip");

    if (amount.negative())
        ret = -ret;

    return ret;
}

static std::int64_t
getSNValue(STAmount const& amount)
{
    return getInt64Value(amount, amount.native(), "amount is not native!");
}

static std::int64_t
getMPTValue(STAmount const& amount)
{
    return getInt64Value(amount, amount.holds<MPTIssue>(), "amount is not MPT!");
}

static bool
areComparable(STAmount const& v1, STAmount const& v2)
{
    return std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(TIss1 const& issue1, TIss2 const& issue2) {
            if constexpr (kIsIssueV<TIss1> && kIsIssueV<TIss2>)
            {
                return v1.native() == v2.native() && issue1.currency == issue2.currency;
            }
            else if constexpr (kIsMptissueV<TIss1> && kIsMptissueV<TIss2>)
            {
                return issue1 == issue2;
            }
            else
            {
                return false;
            }
        },
        v1.asset().value(),
        v2.asset().value());
}

static_assert(kInitialXrp.drops() == STAmount::kMaxNativeN);

STAmount::STAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    std::uint64_t value = sit.get64();

    // native or MPT
    if ((value & kIssuedCurrency) == 0)
    {
        if ((value & kMpToken) != 0)
        {
            // is MPT
            offset_ = 0;
            isNegative_ = (value & kPositive) == 0;
            value_ = (value << 8) | sit.get8();
            asset_ = sit.get192();
            return;
        }
        // else is XRP
        asset_ = xrpIssue();
        // positive
        if ((value & kPositive) != 0)
        {
            value_ = value & kValueMask;
            offset_ = 0;
            isNegative_ = false;
            return;
        }

        // negative
        if (value == 0)
            Throw<std::runtime_error>("negative zero is not canonical");

        value_ = value & kValueMask;
        offset_ = 0;
        isNegative_ = true;
        return;
    }

    Issue issue;
    issue.currency = sit.get160();

    if (isXRP(issue.currency))
        Throw<std::runtime_error>("invalid native currency");

    issue.account = sit.get160();

    if (isXRP(issue.account))
        Throw<std::runtime_error>("invalid native account");

    // 10 bits for the offset, sign and "not native" flag
    int offset = static_cast<int>(value >> (64 - 10));

    value &= ~(1023ull << (64 - 10));

    if (value != 0u)
    {
        bool const isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97;  // center the range

        if (value < kMinValue || value > kMaxValue || offset < kMinOffset || offset > kMaxOffset)
        {
            Throw<std::runtime_error>("invalid currency value");
        }

        asset_ = issue;
        value_ = value;
        offset_ = offset;
        isNegative_ = isNegative;
        canonicalize();
        return;
    }

    if (offset != 512)
        Throw<std::runtime_error>("invalid currency value");

    asset_ = issue;
    value_ = 0;
    offset_ = 0;
    isNegative_ = false;
    canonicalize();
}

STAmount::STAmount(SField const& name, std::int64_t mantissa)
    : STBase(name), asset_(xrpIssue()), offset_(0)
{
    set(mantissa);
}

STAmount::STAmount(SField const& name, std::uint64_t mantissa, bool negative)
    : STBase(name), asset_(xrpIssue()), value_(mantissa), offset_(0), isNegative_(negative)
{
    XRPL_ASSERT(
        value_ <= std::numeric_limits<std::int64_t>::max(),
        "xrpl::STAmount::STAmount(SField, std::uint64_t, bool) : maximum "
        "mantissa input");
}

STAmount::STAmount(SField const& name, STAmount const& from)
    : STBase(name)
    , asset_(from.asset_)
    , value_(from.value_)
    , offset_(from.offset_)
    , isNegative_(from.isNegative_)
{
    XRPL_ASSERT(
        value_ <= std::numeric_limits<std::int64_t>::max(),
        "xrpl::STAmount::STAmount(SField, STAmount) : maximum input");
    canonicalize();
}

//------------------------------------------------------------------------------

STAmount::STAmount(std::uint64_t mantissa, bool negative)
    : asset_(xrpIssue()), value_(mantissa), offset_(0), isNegative_(mantissa != 0 && negative)
{
    XRPL_ASSERT(
        value_ <= std::numeric_limits<std::int64_t>::max(),
        "xrpl::STAmount::STAmount(std::uint64_t, bool) : maximum mantissa "
        "input");
}

STAmount::STAmount(XRPAmount const& amount)
    : asset_(xrpIssue()), offset_(0), isNegative_(amount < beast::kZero)
{
    if (isNegative_)
    {
        value_ = unsafeCast<std::uint64_t>(-amount.drops());
    }
    else
    {
        value_ = unsafeCast<std::uint64_t>(amount.drops());
    }

    canonicalize();
}

std::unique_ptr<STAmount>
STAmount::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STAmount>(sit, name);
}

STBase*
STAmount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STAmount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

//------------------------------------------------------------------------------
//
// Conversion
//
//------------------------------------------------------------------------------
XRPAmount
STAmount::xrp() const
{
    if (!native())
        Throw<std::logic_error>("Cannot return non-native STAmount as XRPAmount");

    auto drops = static_cast<XRPAmount::value_type>(value_);
    XRPL_ASSERT(offset_ == 0, "xrpl::STAmount::xrp : amount is canonical");

    if (isNegative_)
        drops = -drops;

    return XRPAmount{drops};
}

IOUAmount
STAmount::iou() const
{
    if (integral())
        Throw<std::logic_error>("Cannot return non-IOU STAmount as IOUAmount");

    auto mantissa = static_cast<std::int64_t>(value_);
    auto exponent = offset_;

    if (isNegative_)
        mantissa = -mantissa;

    return {mantissa, exponent};
}

MPTAmount
STAmount::mpt() const
{
    if (!holds<MPTIssue>())
        Throw<std::logic_error>("Cannot return STAmount as MPTAmount");

    auto value = static_cast<MPTAmount::value_type>(value_);
    XRPL_ASSERT(offset_ == 0, "xrpl::STAmount::mpt : amount is canonical");

    if (isNegative_)
        value = -value;

    return MPTAmount{value};
}

STAmount&
STAmount::operator=(IOUAmount const& iou)
{
    XRPL_ASSERT(integral() == false, "xrpl::STAmount::operator=(IOUAmount) : is not integral");
    offset_ = iou.exponent();
    isNegative_ = iou < beast::kZero;
    if (isNegative_)
    {
        value_ = static_cast<std::uint64_t>(-iou.mantissa());
    }
    else
    {
        value_ = static_cast<std::uint64_t>(iou.mantissa());
    }
    return *this;
}

STAmount&
STAmount::operator=(Number const& number)
{
    if (!getCurrentTransactionRules() || isFeatureEnabled(featureSingleAssetVault) ||
        isFeatureEnabled(featureLendingProtocol))
    {
        *this = fromNumber(asset_, number);
    }
    else
    {
        auto const originalMantissa = number.mantissa();
        isNegative_ = originalMantissa < 0;
        value_ = isNegative_ ? -originalMantissa : originalMantissa;
        offset_ = number.exponent();
    }
    canonicalize();
    return *this;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

STAmount&
STAmount::operator+=(STAmount const& a)
{
    *this = *this + a;
    return *this;
}

STAmount&
STAmount::operator-=(STAmount const& a)
{
    *this = *this - a;
    return *this;
}

STAmount
operator+(STAmount const& v1, STAmount const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    if (v2 == beast::kZero)
        return v1;

    if (v1 == beast::kZero)
    {
        // Result must be in terms of v1 currency and issuer.
        return {v1.getFName(), v1.asset(), v2.mantissa(), v2.exponent(), v2.negative()};
    }

    if (v1.native())
        return {v1.getFName(), getSNValue(v1) + getSNValue(v2)};
    if (v1.holds<MPTIssue>())
        return {v1.asset_, v1.mpt().value() + v2.mpt().value()};

    if (getSTNumberSwitchover())
    {
        auto x = v1;
        x = v1.iou() + v2.iou();
        return x;
    }

    int ov1 = v1.exponent(), ov2 = v2.exponent();
    std::int64_t vv1 = static_cast<std::int64_t>(v1.mantissa());
    std::int64_t vv2 = static_cast<std::int64_t>(v2.mantissa());

    if (v1.negative())
        vv1 = -vv1;

    if (v2.negative())
        vv2 = -vv2;

    while (ov1 < ov2)
    {
        vv1 /= 10;
        ++ov1;
    }

    while (ov2 < ov1)
    {
        vv2 /= 10;
        ++ov2;
    }

    // This addition cannot overflow an std::int64_t. It can overflow an
    // STAmount and the constructor will throw.

    std::int64_t const fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return {v1.getFName(), v1.asset()};

    if (fv >= 0)
        return STAmount{v1.getFName(), v1.asset(), static_cast<std::uint64_t>(fv), ov1, false};

    return STAmount{v1.getFName(), v1.asset(), static_cast<std::uint64_t>(-fv), ov1, true};
}

STAmount
operator-(STAmount const& v1, STAmount const& v2)
{
    return v1 + (-v2);
}

//------------------------------------------------------------------------------

std::uint64_t const STAmount::kURateOne = getRate(STAmount(1), STAmount(1));

void
STAmount::setIssue(Asset const& asset)
{
    asset_ = asset;
}

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn)
{
    if (offerOut == beast::kZero)
        return 0;

    try
    {
        STAmount const r = divide(offerIn, offerOut, noIssue());
        if (r == beast::kZero)  // offer is too good
            return 0;
        XRPL_ASSERT(
            (r.exponent() >= -100) && (r.exponent() <= 155),
            "xrpl::getRate : exponent inside range");
        std::uint64_t const ret = r.exponent() + 100;
        return (ret << (64 - 8)) | r.mantissa();
    }
    catch (...)
    {
        // overflow -- very bad offer
        return 0;
    }
}

/**
 * @brief Safely checks if two STAmount values can be added without overflow,
 * underflow, or precision loss.
 *
 * This function determines whether the addition of two STAmount objects is
 * safe, depending on their type:
 * - For XRP amounts, it checks for integer overflow and underflow.
 * - For IOU amounts, it checks for acceptable precision loss.
 * - For MPT amounts, it checks for overflow and underflow within 63-bit signed
 * integer limits.
 * - If either amount is zero, addition is always considered safe.
 * - If the amounts are of different currencies or types, addition is not
 * allowed.
 *
 * @param a The first STAmount to add.
 * @param b The second STAmount to add.
 * @return true if the addition is safe; false otherwise.
 */
bool
canAdd(STAmount const& a, STAmount const& b)
{
    // cannot add different currencies
    if (!areComparable(a, b))
        return false;

    // special case: adding anything to zero is always fine
    if (a == beast::kZero || b == beast::kZero)
        return true;

    // XRP case (overflow & underflow check)
    if (isXRP(a) && isXRP(b))
    {
        XRPAmount const aVal = a.xrp();
        XRPAmount const bVal = b.xrp();

        return !(
            (bVal > XRPAmount{0} &&
             aVal > XRPAmount{std::numeric_limits<XRPAmount::value_type>::max()} - bVal) ||
            (bVal < XRPAmount{0} &&
             aVal < XRPAmount{std::numeric_limits<XRPAmount::value_type>::min()} - bVal));
    }

    // IOU case (precision check)
    auto const ret = std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(
            TIss1 const&, TIss2 const&) -> std::optional<bool> {
            if constexpr (kIsIssueV<TIss1> && kIsIssueV<TIss2>)
            {
                static STAmount const kOne{IOUAmount{1, 0}, noIssue()};
                static STAmount const kMaxLoss{IOUAmount{1, -4}, noIssue()};
                STAmount const lhs = divide((a - b) + b, a, noIssue()) - kOne;
                STAmount const rhs = divide((b - a) + a, b, noIssue()) - kOne;
                return ((rhs.negative() ? -rhs : rhs) + (lhs.negative() ? -lhs : lhs)) <= kMaxLoss;
            }

            // MPT (overflow & underflow check)
            if constexpr (kIsMptissueV<TIss1> && kIsMptissueV<TIss2>)
            {
                MPTAmount const aVal = a.mpt();
                MPTAmount const bVal = b.mpt();
                return !(
                    (bVal > MPTAmount{0} &&
                     aVal > MPTAmount{std::numeric_limits<MPTAmount::value_type>::max()} - bVal) ||
                    (bVal < MPTAmount{0} &&
                     aVal < MPTAmount{std::numeric_limits<MPTAmount::value_type>::min()} - bVal));
            }
            return std::nullopt;
        },
        a.asset().value(),
        b.asset().value());
    if (ret)
        return *ret;
    // LCOV_EXCL_START
    UNREACHABLE("STAmount::canAdd : unexpected STAmount type");
    return false;
    // LCOV_EXCL_STOP
}

/**
 * @brief Determines if it is safe to subtract one STAmount from another.
 *
 * This function checks whether subtracting amount `b` from amount `a` is valid,
 * considering currency compatibility and underflow conditions for specific
 * types.
 *
 * - Subtracting zero is always allowed.
 * - Subtraction is only allowed between comparable currencies.
 * - For XRP amounts, ensures no underflow or overflow occurs.
 * - For IOU amounts, subtraction is always allowed (no underflow).
 * - For MPT amounts, ensures no underflow or overflow occurs.
 *
 * @param a The minuend (amount to subtract from).
 * @param b The subtrahend (amount to subtract).
 * @return true if subtraction is allowed, false otherwise.
 */
bool
canSubtract(STAmount const& a, STAmount const& b)
{
    // Cannot subtract different currencies
    if (!areComparable(a, b))
        return false;

    // Special case: subtracting zero is always fine
    if (b == beast::kZero)
        return true;

    // XRP case (underflow & overflow check)
    if (isXRP(a) && isXRP(b))
    {
        XRPAmount const aVal = a.xrp();
        XRPAmount const bVal = b.xrp();
        // Check for underflow
        if (bVal > XRPAmount{0} && aVal < bVal)
            return false;

        // Check for overflow
        if (bVal < XRPAmount{0} &&
            aVal > XRPAmount{std::numeric_limits<XRPAmount::value_type>::max()} + bVal)
            return false;

        return true;
    }

    // IOU case (no underflow)
    auto const ret = std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(
            TIss1 const&, TIss2 const&) -> std::optional<bool> {
            if constexpr (kIsIssueV<TIss1> && kIsIssueV<TIss2>)
            {
                return true;
            }

            // MPT case (underflow & overflow check)
            if constexpr (kIsMptissueV<TIss1> && kIsMptissueV<TIss2>)
            {
                MPTAmount const aVal = a.mpt();
                MPTAmount const bVal = b.mpt();

                // Underflow check
                if (bVal > MPTAmount{0} && aVal < bVal)
                    return false;

                // Overflow check
                if (bVal < MPTAmount{0} &&
                    aVal > MPTAmount{std::numeric_limits<MPTAmount::value_type>::max()} + bVal)
                    return false;
                return true;
            }
            return std::nullopt;
        },
        a.asset().value(),
        b.asset().value());
    if (ret)
        return *ret;
    // LCOV_EXCL_START
    UNREACHABLE("STAmount::canSubtract : unexpected STAmount type");
    return false;
    // LCOV_EXCL_STOP
}

void
STAmount::setJson(json::Value& elem) const
{
    elem = json::ValueType::Object;

    if (!native())
    {
        // It is an error for currency or issuer not to be specified for valid
        // json.
        elem[jss::value] = getText();
        asset_.setJson(elem);
    }
    else
    {
        elem = getText();
    }
}

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

SerializedTypeID
STAmount::getSType() const
{
    return STI_AMOUNT;
}

std::string
STAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + asset_.getText();
    return ret;
}

std::string
STAmount::getText() const
{
    // keep full internal accuracy, but make more human friendly if possible
    if (*this == beast::kZero)
        return "0";

    std::string const rawValue(std::to_string(value_));
    std::string ret;

    if (isNegative_)
        ret.append(1, '-');

    bool const scientific((offset_ != 0) && ((offset_ < -25) || (offset_ > -5)));

    if (native() || asset_.holds<MPTIssue>() || scientific)
    {
        ret.append(rawValue);

        if (scientific)
        {
            ret.append(1, 'e');
            ret.append(std::to_string(offset_));
        }

        return ret;
    }

    XRPL_ASSERT(offset_ + 43 > 0, "xrpl::STAmount::getText : minimum offset");

    size_t const padPrefix = 27;
    size_t const padSuffix = 23;

    std::string val;
    val.reserve(rawValue.length() + padPrefix + padSuffix);
    val.append(padPrefix, '0');
    val.append(rawValue);
    val.append(padSuffix, '0');

    size_t const offset(offset_ + 43);

    auto preFrom(val.begin());
    auto const preTo(val.begin() + offset);

    auto const postFrom(val.begin() + offset);
    auto postTo(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(preFrom, preTo) > padPrefix)
        preFrom += padPrefix;

    XRPL_ASSERT(postTo >= postFrom, "xrpl::STAmount::getText : first distance check");

    preFrom = std::find_if(preFrom, preTo, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(postFrom, postTo) > padSuffix)
        postTo -= padSuffix;

    XRPL_ASSERT(postTo >= postFrom, "xrpl::STAmount::getText : second distance check");

    postTo = std::find_if(
                 std::make_reverse_iterator(postTo),
                 std::make_reverse_iterator(postFrom),
                 [](char c) { return c != '0'; })
                 .base();

    // Assemble the output:
    if (preFrom == preTo)
    {
        ret.append(1, '0');
    }
    else
    {
        ret.append(preFrom, preTo);
    }

    if (postTo != postFrom)
    {
        ret.append(1, '.');
        ret.append(postFrom, postTo);
    }

    return ret;
}

json::Value
STAmount::getJson(JsonOptions) const
{
    json::Value elem;
    setJson(elem);
    return elem;
}

void
STAmount::add(Serializer& s) const
{
    asset_.visit(
        [&](MPTIssue const& issue) {
            auto u8 = static_cast<unsigned char>(kMpToken >> 56);
            if (!isNegative_)
                u8 |= static_cast<unsigned char>(kPositive >> 56);
            s.add8(u8);
            s.add64(value_);
            s.addBitString(issue.getMptID());
        },
        [&](Issue const& issue) {
            if (native())
            {
                XRPL_ASSERT(offset_ == 0, "xrpl::STAmount::add : zero offset");

                if (!isNegative_)
                {
                    s.add64(value_ | kPositive);
                }
                else
                {
                    s.add64(value_);
                }
            }
            else
            {
                if (*this == beast::kZero)
                {
                    s.add64(kIssuedCurrency);
                }
                else if (isNegative_)  // 512 = not native
                {
                    s.add64(value_ | (static_cast<std::uint64_t>(offset_ + 512 + 97) << (64 - 10)));
                }
                else  // 256 = positive
                {
                    s.add64(
                        value_ |
                        (static_cast<std::uint64_t>(offset_ + 512 + 256 + 97) << (64 - 10)));
                }
                s.addBitString(issue.currency);
                s.addBitString(issue.account);
            }
        });
}

bool
STAmount::isEquivalent(STBase const& t) const
{
    STAmount const* v = dynamic_cast<STAmount const*>(&t);
    return (v != nullptr) && (*v == *this);
}

bool
STAmount::isDefault() const
{
    return (value_ == 0) && native();
}

//------------------------------------------------------------------------------

// amount = value_ * [10 ^ offset_]
// Representation range is 10^80 - 10^(-80).
//
// On the wire:
// - high bit is 0 for XRP, 1 for issued currency
// - next bit is 1 for positive, 0 for negative (except 0 issued currency, which
//      is a special case of 0x8000000000000000
// - for issued currencies, the next 8 bits are (offset_+97).
//   The +97 is so that this value is always positive.
// - The remaining bits are significant digits (mantissa)
//   That's 54 bits for issued currency and 62 bits for native
//   (but XRP only needs 57 bits for the max value of 10^17 drops)
//
// value_ is zero if the amount is zero, otherwise it's within the range
//    10^15 to (10^16 - 1) inclusive.
// offset_ is in the range -96 to +80.
void
STAmount::canonicalize()
{
    if (integral())
    {
        // native and MPT currency amounts should always have an offset of zero
        // log(2^64,10) ~ 19.2
        if (value_ == 0 || offset_ <= -20)
        {
            value_ = 0;
            offset_ = 0;
            isNegative_ = false;
            return;
        }

        // log(cMaxNativeN, 10) == 17
        if (native() && offset_ > 17)
            Throw<std::runtime_error>("Native currency amount out of range");
        // log(maxMPTokenAmount, 10) ~ 18.96
        if (asset_.holds<MPTIssue>() && offset_ > 18)
            Throw<std::runtime_error>("MPT amount out of range");

        if (getSTNumberSwitchover())
        {
            Number const num(isNegative_, value_, offset_, Number::Unchecked{});
            auto set = [&](auto const& val) {
                auto const value = val.value();
                isNegative_ = value < 0;
                value_ = isNegative_ ? -value : value;
            };
            if (native())
            {
                set(XRPAmount{num});
            }
            else if (asset_.holds<MPTIssue>())
            {
                set(MPTAmount{num});
            }
            else
            {
                Throw<std::runtime_error>("Unknown integral asset type");
            }
            offset_ = 0;
        }
        else
        {
            while (offset_ < 0)
            {
                value_ /= 10;
                ++offset_;
            }

            while (offset_ > 0)
            {
                // N.B. do not move the overflow check to after the
                // multiplication
                if (native() && value_ > kMaxNativeN)
                {
                    Throw<std::runtime_error>("Native currency amount out of range");
                }
                else if (!native() && value_ > kMaxMpTokenAmount)
                {
                    Throw<std::runtime_error>("MPT amount out of range");
                }

                value_ *= 10;
                --offset_;
            }
        }

        if (native() && value_ > kMaxNativeN)
        {
            Throw<std::runtime_error>("Native currency amount out of range");
        }
        else if (!native() && value_ > kMaxMpTokenAmount)
        {
            Throw<std::runtime_error>("MPT amount out of range");
        }

        return;
    }

    if (getSTNumberSwitchover())
    {
        *this = iou();
        return;
    }

    if (value_ == 0)
    {
        offset_ = -100;
        isNegative_ = false;
        return;
    }

    while ((value_ < kMinValue) && (offset_ > kMinOffset))
    {
        value_ *= 10;
        --offset_;
    }

    while (value_ > kMaxValue)
    {
        if (offset_ >= kMaxOffset)
            Throw<std::runtime_error>("value overflow");

        value_ /= 10;
        ++offset_;
    }

    if ((offset_ < kMinOffset) || (value_ < kMinValue))
    {
        value_ = 0;
        isNegative_ = false;
        offset_ = -100;
        return;
    }

    if (offset_ > kMaxOffset)
        Throw<std::runtime_error>("value overflow");

    XRPL_ASSERT(
        (value_ == 0) || ((value_ >= kMinValue) && (value_ <= kMaxValue)),
        "xrpl::STAmount::canonicalize : value inside range");
    XRPL_ASSERT(
        (value_ == 0) || ((offset_ >= kMinOffset) && (offset_ <= kMaxOffset)),
        "xrpl::STAmount::canonicalize : offset inside range");
    XRPL_ASSERT(
        (value_ != 0) || (offset_ != -100), "xrpl::STAmount::canonicalize : value or offset set");
}

void
STAmount::set(std::int64_t v)
{
    if (v < 0)
    {
        isNegative_ = true;
        value_ = static_cast<std::uint64_t>(-v);
    }
    else
    {
        isNegative_ = false;
        value_ = static_cast<std::uint64_t>(v);
    }
}

//------------------------------------------------------------------------------

STAmount
amountFromQuality(std::uint64_t rate)
{
    if (rate == 0)
        return STAmount(noIssue());

    std::uint64_t const mantissa = rate & ~(255ull << (64 - 8));
    int const exponent = static_cast<int>(rate >> (64 - 8)) - 100;

    return STAmount(noIssue(), mantissa, exponent);
}

STAmount
amountFromString(Asset const& asset, std::string const& amount)
{
    auto const parts = partsFromString(amount);
    if ((asset.native() || asset.holds<MPTIssue>()) && parts.exponent < 0)
        Throw<std::runtime_error>("XRP and MPT must be specified as integral amount.");
    return {asset, parts.mantissa, parts.exponent, parts.negative};
}

STAmount
amountFromJson(SField const& name, json::Value const& v)
{
    Asset asset;

    json::Value value;
    json::Value currencyOrMPTID;
    json::Value issuer;
    bool isMPT = false;

    if (v.isNull())
    {
        Throw<std::runtime_error>("XRP may not be specified with a null Json value");
    }
    else if (v.isObject())
    {
        if (!validJSONAsset(v))
            Throw<std::runtime_error>("Invalid Asset's Json specification");

        value = v[jss::value];
        if (v.isMember(jss::mpt_issuance_id))
        {
            isMPT = true;
            currencyOrMPTID = v[jss::mpt_issuance_id];
        }
        else
        {
            currencyOrMPTID = v[jss::currency];
            issuer = v[jss::issuer];
        }
    }
    else if (v.isArray())
    {
        value = v.get(json::UInt(0), 0);
        currencyOrMPTID = v.get(json::UInt(1), json::ValueType::Null);
        issuer = v.get(json::UInt(2), json::ValueType::Null);
    }
    else if (v.isString())
    {
        std::string val = v.asString();
        std::vector<std::string> elements;
        boost::split(elements, val, boost::is_any_of("\t\n\r ,/"));

        if (elements.size() > 3)
            Throw<std::runtime_error>("invalid amount string");

        value = elements[0];

        if (elements.size() > 1)
            currencyOrMPTID = elements[1];

        if (elements.size() > 2)
            issuer = elements[2];
    }
    else
    {
        value = v;
    }

    bool const native = !currencyOrMPTID.isString() || currencyOrMPTID.asString().empty() ||
        (currencyOrMPTID.asString() == systemCurrencyCode());

    if (native)
    {
        if (v.isObjectOrNull())
            Throw<std::runtime_error>("XRP may not be specified as an object");
        asset = xrpIssue();
    }
    else
    {
        if (isMPT)
        {
            // sequence (32 bits) + account (160 bits)
            MPTID u;
            if (!u.parseHex(currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid MPTokenIssuanceID");
            asset = u;
        }
        else
        {
            Issue issue;
            if (!toCurrency(issue.currency, currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid currency");
            if (!issuer.isString() || !toIssuer(issue.account, issuer.asString()))
                Throw<std::runtime_error>("invalid issuer");
            if (issue.native())
                Throw<std::runtime_error>("invalid issuer");
            asset = issue;
        }
    }

    NumberParts parts;

    if (value.isInt())
    {
        if (value.asInt() >= 0)
        {
            parts.mantissa = value.asInt();
        }
        else
        {
            parts.mantissa = value.asAbsUInt();
            parts.negative = true;
        }
    }
    else if (value.isUInt())
    {
        parts.mantissa = v.asUInt();
    }
    else if (value.isString())
    {
        parts = partsFromString(value.asString());
        // Can't specify XRP or MPT using fractional representation
        if ((asset.native() || asset.holds<MPTIssue>()) && parts.exponent < 0)
            Throw<std::runtime_error>("XRP and MPT must be specified as integral amount.");
    }
    else
    {
        Throw<std::runtime_error>("invalid amount type");
    }

    return {name, asset, parts.mantissa, parts.exponent, parts.negative};
}

bool
amountFromJsonNoThrow(STAmount& result, json::Value const& jvSource)
{
    try
    {
        result = amountFromJson(kSfGeneric, jvSource);
        return true;
    }
    catch (std::exception const& e)
    {
        JLOG(debugLog().warn()) << "amountFromJsonNoThrow: caught: " << e.what();
    }
    return false;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

bool
operator<(STAmount const& lhs, STAmount const& rhs)
{
    if (!areComparable(lhs, rhs))
        Throw<std::runtime_error>("Can't compare amounts that are't comparable!");

    if (lhs.negative() != rhs.negative())
        return lhs.negative();

    if (lhs.mantissa() == 0)
    {
        if (rhs.negative())
            return false;
        return rhs.mantissa() != 0;
    }

    // We know that lhs is non-zero and both sides have the same sign. Since
    // rhs is zero (and thus not negative), lhs must, therefore, be strictly
    // greater than zero. So if rhs is zero, the comparison must be false.
    if (rhs.mantissa() == 0)
        return false;

    if (lhs.exponent() > rhs.exponent())
        return lhs.negative();
    if (lhs.exponent() < rhs.exponent())
        return !lhs.negative();
    if (lhs.mantissa() > rhs.mantissa())
        return lhs.negative();
    if (lhs.mantissa() < rhs.mantissa())
        return !lhs.negative();

    return false;
}

STAmount
operator-(STAmount const& value)
{
    if (value.mantissa() == 0)
        return value;
    return STAmount(
        value.getFName(),
        value.asset(),
        value.mantissa(),
        value.exponent(),
        !value.negative(),
        STAmount::Unchecked{});
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

// Calculate (a * b) / c when all three values are 64-bit
// without loss of precision:
static std::uint64_t
muldiv(std::uint64_t multiplier, std::uint64_t multiplicand, std::uint64_t divisor)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: (" + std::to_string(multiplier) + " * " + std::to_string(multiplicand) +
            ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

static std::uint64_t
muldivRound(
    std::uint64_t multiplier,
    std::uint64_t multiplicand,
    std::uint64_t divisor,
    std::uint64_t rounding)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret += rounding;
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: ((" + std::to_string(multiplier) + " * " + std::to_string(multiplicand) +
            ") + " + std::to_string(rounding) + ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

STAmount
divide(STAmount const& num, STAmount const& den, Asset const& asset)
{
    if (den == beast::kZero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::kZero)
        return {asset};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.integral())
    {
        while (numVal < STAmount::kMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.integral())
    {
        while (denVal < STAmount::kMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    return STAmount(
        asset,
        muldiv(numVal, kTenTO17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset)
{
    if (v1 == beast::kZero || v2 == beast::kZero)
        return STAmount(asset);

    if (v1.native() && v2.native() && asset.native())
    {
        std::uint64_t const minV = std::min(getSNValue(v1), getSNValue(v2));
        std::uint64_t const maxV = std::max(getSNValue(v1), getSNValue(v2));

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }
    if (v1.holds<MPTIssue>() && v2.holds<MPTIssue>() && asset.holds<MPTIssue>())
    {
        std::uint64_t const minV = std::min(getMPTValue(v1), getMPTValue(v2));
        std::uint64_t const maxV = std::max(getMPTValue(v1), getMPTValue(v2));

        if (minV > 3037000499ull)  // sqrt(maxMPTokenAmount) ~ 3037000499.98
            Throw<std::runtime_error>("MPT value overflow");

        if (((maxV >> 32) * minV) > 2147483648ull)  // maxMPTokenAmount / 2^32
            Throw<std::runtime_error>("MPT value overflow");

        return STAmount(asset, minV * maxV);
    }

    if (getSTNumberSwitchover())
    {
        auto const r = Number{v1} * Number{v2};
        return STAmount{asset, r};
    }

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    if (v1.integral())
    {
        while (value1 < STAmount::kMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.integral())
    {
        while (value2 < STAmount::kMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    return STAmount(
        asset,
        muldiv(value1, value2, kTenTO14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
}

// This is the legacy version of canonicalizeRound.  It's been in use
// for years, so it is deeply embedded in the behavior of cross-currency
// transactions.
//
// However, in 2022 it was noticed that the rounding characteristics were
// surprising.  When the code converts from IOU-like to XRP-like there may
// be a fraction of the IOU-like representation that is too small to be
// represented in drops.  `canonicalizeRound()` currently does some unusual
// rounding.
//
//  1. If the fractional part is greater than or equal to 0.1, then the
//     number of drops is rounded up.
//
//  2. However, if the fractional part is less than 0.1 (for example,
//     0.099999), then the number of drops is rounded down.
//
// The XRP Ledger has this rounding behavior baked in.  But there are
// situations where this rounding behavior led to undesirable outcomes.
// So an alternative rounding approach was introduced.  You'll see that
// alternative below.
static void
canonicalizeRound(bool integral, std::uint64_t& value, int& offset, bool)
{
    if (integral)
    {
        if (offset < 0)
        {
            int loops = 0;

            while (offset < -1)
            {
                value /= 10;
                ++offset;
                ++loops;
            }

            value += (loops >= 2) ? 9 : 10;  // add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > STAmount::kMaxValue)
    {
        while (value > (10 * STAmount::kMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

// The original canonicalizeRound did not allow the rounding direction to
// be specified.  It also ignored some of the bits that could contribute to
// rounding decisions.  canonicalizeRoundStrict() tracks all of the bits in
// the value being rounded.
static void
canonicalizeRoundStrict(bool integral, std::uint64_t& value, int& offset, bool roundUp)
{
    if (integral)
    {
        if (offset < 0)
        {
            bool hadRemainder = false;

            while (offset < -1)
            {
                // It would be better to use std::lldiv than to separately
                // compute the remainder.  But std::lldiv does not support
                // unsigned arguments.
                std::uint64_t const newValue = value / 10;
                hadRemainder |= (value != (newValue * 10));
                value = newValue;
                ++offset;
            }
            value += (hadRemainder && roundUp) ? 10 : 9;  // Add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > STAmount::kMaxValue)
    {
        while (value > (10 * STAmount::kMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

STAmount
roundToScale(STAmount const& value, std::int32_t scale, Number::RoundingMode rounding)
{
    // Nothing to do for integral types.
    if (value.integral())
        return value;

    // Nothing to do for zero.
    if (value == beast::kZero)
        return value;

    // If the value's exponent is greater than or equal to the scale, then
    // rounding will do nothing, and might even lose precision, so just return
    // the value.
    if (value.exponent() >= scale)
        return value;

    STAmount const referenceValue{value.asset(), STAmount::kMinValue, scale, value.negative()};

    NumberRoundModeGuard const mg(rounding);
    // With an IOU, the the result of addition will be truncated to the
    // precision of the larger value, which in this case is referenceValue. Then
    // remove the reference value via subtraction, and we're left with the
    // rounded value.
    return (value + referenceValue) - referenceValue;
}

namespace {

// We need a class that has an interface similar to NumberRoundModeGuard
// but does nothing.
class DontAffectNumberRoundMode
{
public:
    explicit DontAffectNumberRoundMode(Number::RoundingMode mode) noexcept
    {
    }

    DontAffectNumberRoundMode(DontAffectNumberRoundMode const&) = delete;

    DontAffectNumberRoundMode&
    operator=(DontAffectNumberRoundMode const&) = delete;
};

}  // anonymous namespace

// Pass the canonicalizeRound function pointer as a template parameter.
//
// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool), typename MightSaveRound>
static STAmount
mulRoundImpl(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    if (v1 == beast::kZero || v2 == beast::kZero)
        return {asset};

    if (v1.native() && v2.native() && asset.native())
    {
        std::uint64_t const minV = std::min(getSNValue(v1), getSNValue(v2));
        std::uint64_t const maxV = std::max(getSNValue(v1), getSNValue(v2));

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }

    if (v1.holds<MPTIssue>() && v2.holds<MPTIssue>() && asset.holds<MPTIssue>())
    {
        std::uint64_t const minV = std::min(getMPTValue(v1), getMPTValue(v2));
        std::uint64_t const maxV = std::max(getMPTValue(v1), getMPTValue(v2));

        if (minV > 3037000499ull)  // sqrt(maxMPTokenAmount) ~ 3037000499.98
            Throw<std::runtime_error>("MPT value overflow");

        if (((maxV >> 32) * minV) > 2147483648ull)  // maxMPTokenAmount / 2^32
            Throw<std::runtime_error>("MPT value overflow");

        return STAmount(asset, minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    if (v1.integral())
    {
        while (value1 < STAmount::kMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.integral())
    {
        while (value2 < STAmount::kMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool const resultNegative = v1.negative() != v2.negative();

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    //
    // If we're rounding up, we want to round up away
    // from zero, and if we're rounding down, truncation
    // is implicit.
    std::uint64_t amount =
        muldivRound(value1, value2, kTenTO14, (resultNegative != roundUp) ? kTenTO14M1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(asset.integral(), amount, offset, roundUp);
    }
    STAmount result = [&]() {
        // If appropriate, tell Number to round down.  This gives the desired
        // result from STAmount::canonicalize.
        MightSaveRound const savedRound(Number::RoundingMode::TowardsZero);
        return STAmount(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (asset.integral())
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = STAmount::kMinValue;
            offset = STAmount::kMinOffset;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

STAmount
mulRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    return mulRoundImpl<canonicalizeRound, DontAffectNumberRoundMode>(v1, v2, asset, roundUp);
}

STAmount
mulRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    return mulRoundImpl<canonicalizeRoundStrict, NumberRoundModeGuard>(v1, v2, asset, roundUp);
}

// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <typename MightSaveRound>
static STAmount
divRoundImpl(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    if (den == beast::kZero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::kZero)
        return {asset};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    if (num.integral())
    {
        while (numVal < STAmount::kMinValue)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.integral())
    {
        while (denVal < STAmount::kMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    bool const resultNegative = (num.negative() != den.negative());

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    //
    // We round away from zero if we're rounding up or
    // truncate if we're rounding down.
    std::uint64_t amount =
        muldivRound(numVal, kTenTO17, denVal, (resultNegative != roundUp) ? denVal - 1 : 0);

    int offset = numOffset - denOffset - 17;

    if (resultNegative != roundUp)
        canonicalizeRound(asset.integral(), amount, offset, roundUp);

    STAmount result = [&]() {
        // If appropriate, tell Number the rounding mode we are using.
        // Note that "roundUp == true" actually means "round away from zero".
        // Otherwise, round toward zero.
        using enum Number::RoundingMode;
        MightSaveRound const savedRound(roundUp ^ resultNegative ? Upward : Downward);
        return STAmount(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (asset.integral())
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = STAmount::kMinValue;
            offset = STAmount::kMinOffset;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

STAmount
divRound(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    return divRoundImpl<DontAffectNumberRoundMode>(num, den, asset, roundUp);
}

STAmount
divRoundStrict(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    return divRoundImpl<NumberRoundModeGuard>(num, den, asset, roundUp);
}

}  // namespace xrpl
