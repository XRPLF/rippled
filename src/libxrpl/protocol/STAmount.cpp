/** @file
 *  Implementation of STAmount — the universal value type for the XRP Ledger.
 *
 *  Every amount on the network (XRP drops, IOU tokens, or MPTs) is
 *  represented, serialized, compared, and arithmetically manipulated through
 *  this class and its companion free functions.  The canonical internal form
 *  stores a `uint64_t` mantissa, an `int` base-10 exponent, and a sign bit;
 *  the exact valid range differs by asset type:
 *
 *  - **XRP / MPT** (integral): `offset_ == 0`, `value_` is the raw drop/token
 *    count.
 *  - **IOU**: `value_` ∈ [10^15, 10^16 − 1], `offset_` ∈ [−96, +80];
 *    zero is the special case `(value_=0, offset_=−100)`.
 *
 *  See `STAmount.h` for the full public interface.
 */
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

/** 10^14 — scaling denominator used in IOU multiply to keep precision. */
static std::uint64_t const kTEN_TO14 = 100000000000000ull;
/** 10^14 − 1 — maximum rounding bias added before the final divide in
 *  `muldivRound` when the result is positive and rounding up. */
static std::uint64_t const kTEN_TO14M1 = kTEN_TO14 - 1;
/** 10^17 — scaling multiplier used in IOU divide to maintain full precision. */
static std::uint64_t const kTEN_TO17 = kTEN_TO14 * 1000;

//------------------------------------------------------------------------------

/** Extract the integral value from an XRP or MPT amount as a signed 64-bit integer.
 *
 *  Validates that `valid` is true, asserts the exponent is zero (canonical
 *  for integral types), and applies the sign bit before returning.  Used
 *  internally by `getSNValue` and `getMPTValue`.
 *
 *  @param amount The amount whose mantissa is extracted.
 *  @param valid  Must be true; when false, throws with `error` as the message.
 *  @param error  Error string thrown when `valid` is false.
 *  @return The signed integer value of `amount`.
 *  @throw std::runtime_error if `valid` is false.
 */
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

/** Return the signed drop value of a native XRP amount.
 *
 *  @param amount Must be a native (XRP) amount; throws otherwise.
 *  @return The signed drop count.
 *  @throw std::runtime_error if `amount` is not native.
 */
static std::int64_t
getSNValue(STAmount const& amount)
{
    return getInt64Value(amount, amount.native(), "amount is not native!");
}

/** Return the signed value of an MPT amount.
 *
 *  @param amount Must hold an `MPTIssue`; throws otherwise.
 *  @return The signed MPT token count.
 *  @throw std::runtime_error if `amount` does not hold an `MPTIssue`.
 */
static std::int64_t
getMPTValue(STAmount const& amount)
{
    return getInt64Value(amount, amount.holds<MPTIssue>(), "amount is not MPT!");
}

/** Determine whether two amounts represent the same asset and can be compared
 *  or combined arithmetically.
 *
 *  Two IOU amounts are comparable when they share both nativeness and currency.
 *  Two MPT amounts are comparable when their `MPTIssue` identities are equal.
 *  Cross-type pairs (one IOU, one MPT) are never comparable.
 *
 *  @param v1 First amount.
 *  @param v2 Second amount.
 *  @return `true` if the amounts share the same asset identity.
 */
static bool
areComparable(STAmount const& v1, STAmount const& v2)
{
    return std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(TIss1 const& issue1, TIss2 const& issue2) {
            if constexpr (kIS_ISSUE_V<TIss1> && kIS_ISSUE_V<TIss2>)
            {
                return v1.native() == v2.native() && issue1.currency == issue2.currency;
            }
            else if constexpr (kIS_MPTISSUE_V<TIss1> && kIS_MPTISSUE_V<TIss2>)
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

static_assert(kINITIAL_XRP.drops() == STAmount::kMAX_NATIVE_N);

/** Deserialize an `STAmount` from the wire format.
 *
 *  Decodes the 64-bit header word and, for issued currencies, the following
 *  160-bit currency and 160-bit issuer fields; for MPT, the following 192-bit
 *  MPTID.  Bit layout:
 *  - Bit 63 (`kISSUED_CURRENCY`): 0 = XRP or MPT, 1 = IOU.
 *  - Bit 62 (`kPOSITIVE`):        1 = positive, 0 = negative.
 *  - Bit 61 (`kMP_TOKEN`):         1 = MPT (only when bit 63 is 0).
 *
 *  @throws std::runtime_error on negative-zero XRP, invalid IOU currency/
 *      account, or mantissa/exponent outside the canonical range.
 */
STAmount::STAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    std::uint64_t value = sit.get64();

    // native or MPT
    if ((value & kISSUED_CURRENCY) == 0)
    {
        if ((value & kMP_TOKEN) != 0)
        {
            // is MPT
            offset_ = 0;
            isNegative_ = (value & kPOSITIVE) == 0;
            value_ = (value << 8) | sit.get8();
            asset_ = sit.get192();
            return;
        }
        // else is XRP
        asset_ = xrpIssue();
        // positive
        if ((value & kPOSITIVE) != 0)
        {
            value_ = value & kVALUE_MASK;
            offset_ = 0;
            isNegative_ = false;
            return;
        }

        // negative
        if (value == 0)
            Throw<std::runtime_error>("negative zero is not canonical");

        value_ = value & kVALUE_MASK;
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

        if (value < kMIN_VALUE || value > kMAX_VALUE || offset < kMIN_OFFSET ||
            offset > kMAX_OFFSET)
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

/** Construct a native XRP amount from a signed mantissa.
 *
 *  Negative values are stored as positive mantissa + sign bit via `set()`.
 *  No `canonicalize()` call is made; the caller is responsible for ensuring
 *  the value is within the legal XRP drop range.
 */
STAmount::STAmount(SField const& name, std::int64_t mantissa)
    : STBase(name), asset_(xrpIssue()), offset_(0)
{
    set(mantissa);
}

/** Construct a native XRP amount from an unsigned mantissa and explicit sign.
 *
 *  The mantissa must not exceed `INT64_MAX`; the assertion enforces this.
 *  This constructor does not call `canonicalize()`.
 */
STAmount::STAmount(SField const& name, std::uint64_t mantissa, bool negative)
    : STBase(name), asset_(xrpIssue()), value_(mantissa), offset_(0), isNegative_(negative)
{
    XRPL_ASSERT(
        value_ <= std::numeric_limits<std::int64_t>::max(),
        "xrpl::STAmount::STAmount(SField, std::uint64_t, bool) : maximum "
        "mantissa input");
}

/** Copy-construct an STAmount while assigning a different SField name.
 *
 *  Used when an amount value needs to be re-associated with a different
 *  serialized field (e.g. when promoting an inner object's amount into an
 *  outer context).  Calls `canonicalize()` to ensure invariants hold.
 */
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

/** Construct a bare (field-less) native XRP amount from an unsigned mantissa.
 *
 *  A negative sign is suppressed for zero (`mantissa == 0 && negative` is
 *  treated as positive zero).  Does not call `canonicalize()`.
 */
STAmount::STAmount(std::uint64_t mantissa, bool negative)
    : asset_(xrpIssue()), value_(mantissa), offset_(0), isNegative_(mantissa != 0 && negative)
{
    XRPL_ASSERT(
        value_ <= std::numeric_limits<std::int64_t>::max(),
        "xrpl::STAmount::STAmount(std::uint64_t, bool) : maximum mantissa "
        "input");
}

/** Promote an `XRPAmount` into an `STAmount`.
 *
 *  Preserves the sign of `amount` and calls `canonicalize()` to ensure
 *  the drop count is within the legal XRP range.
 */
STAmount::STAmount(XRPAmount const& amount)
    : asset_(xrpIssue()), offset_(0), isNegative_(amount < beast::kZERO)
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

/** Convert to the lean `XRPAmount` representation.
 *
 *  @throw std::logic_error if this amount is not native (XRP).
 */
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

/** Convert to the lean `IOUAmount` representation.
 *
 *  @throw std::logic_error if this amount is integral (XRP or MPT).
 */
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

/** Convert to the lean `MPTAmount` representation.
 *
 *  @throw std::logic_error if this amount does not hold an `MPTIssue`.
 */
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

/** Assign from an `IOUAmount`, preserving the existing asset identity.
 *
 *  The amount must already be an IOU (non-integral); the asset is unchanged.
 *  Does not call `canonicalize()` — the `IOUAmount` is already in canonical
 *  form.
 */
STAmount&
STAmount::operator=(IOUAmount const& iou)
{
    XRPL_ASSERT(integral() == false, "xrpl::STAmount::operator=(IOUAmount) : is not integral");
    offset_ = iou.exponent();
    isNegative_ = iou < beast::kZERO;
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

/** Assign from a `Number`, with feature-gated conversion path.
 *
 *  When `featureSingleAssetVault` or `featureLendingProtocol` is active, or
 *  when no current transaction rules are set (e.g. in unit tests), delegates
 *  to `fromNumber()` for asset-aware normalization.  The legacy path directly
 *  copies the mantissa and exponent without asset-specific rounding; both
 *  paths call `canonicalize()` afterward.
 *
 *  @note This operator exists to support the vault/loan transactor pattern
 *      where `Number` arithmetic results are assigned back into an `STAmount`
 *      field.  Callers must ensure `associateAsset()` is called after all
 *      mutations if `sMD_NeedsAsset` fields are involved.
 */
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

/** Add two amounts of the same asset.
 *
 *  For XRP, performs signed 64-bit integer addition on the drop counts.
 *  For MPT, performs integer addition on the token values.
 *  For IOU (legacy path), aligns exponents by truncating the lower-exponent
 *  operand, then adds; results within [−10, +10] after alignment are
 *  rounded to the asset's zero.
 *  When `getSTNumberSwitchover()` is active, delegates to `IOUAmount::operator+`.
 *
 *  @note The int64 addition itself cannot overflow, but the resulting
 *      `STAmount` can overflow during `canonicalize()`, which then throws.
 *  @throw std::runtime_error if the amounts are not comparable or overflow.
 */
STAmount
operator+(STAmount const& v1, STAmount const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    if (v2 == beast::kZERO)
        return v1;

    if (v1 == beast::kZERO)
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

/** Subtract two amounts of the same asset.
 *
 *  Implemented as `v1 + (−v2)`.  The unary negation has no effect on zero.
 *
 *  @throw std::runtime_error if the amounts are not comparable or overflow.
 */
STAmount
operator-(STAmount const& v1, STAmount const& v2)
{
    return v1 + (-v2);
}

//------------------------------------------------------------------------------

std::uint64_t const STAmount::kU_RATE_ONE = getRate(STAmount(1), STAmount(1));

void
STAmount::setIssue(Asset const& asset)
{
    asset_ = asset;
}

/** Encode an order-book offer as a 64-bit sort key (rate = takerPays / takerGets).
 *
 *  A lower value represents a better rate for the taker.  The key packs
 *  `(exponent + 100) << 56 | mantissa`, exploiting the fact that the
 *  canonical IOU mantissa fits in 56 bits.  Overflow or a zero quotient both
 *  return 0 (offer treated as worthless).
 *
 *  @param offerOut `takerGets`: the amount the offerer sells.
 *  @param offerIn  `takerPays`: the amount the offerer receives.
 *  @return A 64-bit sort key where lower is better for takers; 0 for worthless
 *      or overflow offers.
 *  @see kU_RATE_ONE for the canonical 1:1 rate constant.
 */
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn)
{
    if (offerOut == beast::kZERO)
        return 0;

    try
    {
        STAmount const r = divide(offerIn, offerOut, noIssue());
        if (r == beast::kZERO)  // offer is too good
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

/** Determine whether adding two amounts is safe without overflow or unacceptable
 *  precision loss.
 *
 *  Returns `false` immediately for incomparable assets.  For XRP and MPT,
 *  checks integer overflow/underflow bounds.  For IOU, applies a round-trip
 *  relative error test: `|(a−b)+b − a| + |(b−a)+a − b| ≤ 10^−4`; this
 *  catches cases where a large exponent gap would lose too many significant
 *  digits, which could corrupt vault/AMM balances.
 *
 *  @param a The first addend.
 *  @param b The second addend.
 *  @return `true` if `a + b` is safe to compute; `false` otherwise.
 */
bool
canAdd(STAmount const& a, STAmount const& b)
{
    if (!areComparable(a, b))
        return false;

    if (a == beast::kZERO || b == beast::kZERO)
        return true;

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

    auto const ret = std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(
            TIss1 const&, TIss2 const&) -> std::optional<bool> {
            if constexpr (kIS_ISSUE_V<TIss1> && kIS_ISSUE_V<TIss2>)
            {
                static STAmount const kONE{IOUAmount{1, 0}, noIssue()};
                static STAmount const kMAX_LOSS{IOUAmount{1, -4}, noIssue()};
                STAmount const lhs = divide((a - b) + b, a, noIssue()) - kONE;
                STAmount const rhs = divide((b - a) + a, b, noIssue()) - kONE;
                return ((rhs.negative() ? -rhs : rhs) + (lhs.negative() ? -lhs : lhs)) <= kMAX_LOSS;
            }

            if constexpr (kIS_MPTISSUE_V<TIss1> && kIS_MPTISSUE_V<TIss2>)
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

/** Determine whether subtracting `b` from `a` is safe.
 *
 *  For XRP and MPT, checks integer underflow and overflow bounds.  For IOU,
 *  subtraction is always considered safe because negative IOU balances are
 *  valid on the ledger.  Returns `false` for incomparable assets.
 *
 *  @param a The minuend.
 *  @param b The subtrahend.
 *  @return `true` if `a - b` is safe to compute; `false` otherwise.
 */
bool
canSubtract(STAmount const& a, STAmount const& b)
{
    if (!areComparable(a, b))
        return false;

    if (b == beast::kZERO)
        return true;

    if (isXRP(a) && isXRP(b))
    {
        XRPAmount const aVal = a.xrp();
        XRPAmount const bVal = b.xrp();
        if (bVal > XRPAmount{0} && aVal < bVal)
            return false;

        if (bVal < XRPAmount{0} &&
            aVal > XRPAmount{std::numeric_limits<XRPAmount::value_type>::max()} + bVal)
            return false;

        return true;
    }

    auto const ret = std::visit(
        [&]<ValidIssueType TIss1, ValidIssueType TIss2>(
            TIss1 const&, TIss2 const&) -> std::optional<bool> {
            if constexpr (kIS_ISSUE_V<TIss1> && kIS_ISSUE_V<TIss2>)
            {
                return true;
            }

            if constexpr (kIS_MPTISSUE_V<TIss1> && kIS_MPTISSUE_V<TIss2>)
            {
                MPTAmount const aVal = a.mpt();
                MPTAmount const bVal = b.mpt();

                if (bVal > MPTAmount{0} && aVal < bVal)
                    return false;

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

/** Serialize this amount into a `Json::Value`.
 *
 *  XRP amounts are emitted as a plain string (drop count).  IOU and MPT amounts
 *  are emitted as a JSON object with `"value"` plus asset fields via
 *  `Asset::setJson()`.
 *
 *  @note It is an error to call this for a non-native amount when the asset
 *      does not have a valid currency and issuer; `Asset::setJson()` will throw
 *      in that case.
 */
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

/** Return a human-readable string including the asset identity (e.g. `"100/XRP"`). */
std::string
STAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + asset_.getText();
    return ret;
}

/** Return a human-readable decimal string, applying decimal-point notation when
 *  the exponent is in the range [−25, −5) and scientific notation otherwise.
 *  Leading and trailing zeroes are stripped for readability.
 */
std::string
STAmount::getText() const
{
    // keep full internal accuracy, but make more human friendly if possible
    if (*this == beast::kZERO)
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

/** Serialize to wire format, producing the bit pattern described in the class
 *  comment.  Inverse of the `SerialIter` constructor; round-trips losslessly
 *  for any canonical amount.
 */
void
STAmount::add(Serializer& s) const
{
    asset_.visit(
        [&](MPTIssue const& issue) {
            auto u8 = static_cast<unsigned char>(kMP_TOKEN >> 56);
            if (!isNegative_)
                u8 |= static_cast<unsigned char>(kPOSITIVE >> 56);
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
                    s.add64(value_ | kPOSITIVE);
                }
                else
                {
                    s.add64(value_);
                }
            }
            else
            {
                if (*this == beast::kZERO)
                {
                    s.add64(kISSUED_CURRENCY);
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

/** Bring the amount into its canonical internal form.
 *
 *  For **integral** types (XRP and MPT): repeatedly divides or multiplies
 *  `value_` by 10 while adjusting `offset_` until `offset_ == 0`, checking
 *  overflow bounds before each multiply.  When `getSTNumberSwitchover()` is
 *  active, delegates to `XRPAmount` or `MPTAmount` conversion via `Number`.
 *
 *  For **IOU**: nudges the mantissa into the canonical window [10^15, 10^16)
 *  by scaling up (multiply × 10, decrement offset) or down (divide ÷ 10,
 *  increment offset).  Underflow below `kMIN_OFFSET` collapses the value to
 *  canonical zero `(value_=0, offset_=−100)`.  Overflow throws.  When
 *  `getSTNumberSwitchover()` is active, delegates to `iou()` which uses the
 *  `IOUAmount` normalizer.
 *
 *  @throw std::runtime_error on XRP overflow (`> kMAX_NATIVE_N`), MPT overflow
 *      (`> maxMPTokenAmount`), or IOU overflow.
 */
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
                if (native() && value_ > kMAX_NATIVE_N)
                {
                    Throw<std::runtime_error>("Native currency amount out of range");
                }
                else if (!native() && value_ > kMAX_MP_TOKEN_AMOUNT)
                {
                    Throw<std::runtime_error>("MPT amount out of range");
                }

                value_ *= 10;
                --offset_;
            }
        }

        if (native() && value_ > kMAX_NATIVE_N)
        {
            Throw<std::runtime_error>("Native currency amount out of range");
        }
        else if (!native() && value_ > kMAX_MP_TOKEN_AMOUNT)
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

    while ((value_ < kMIN_VALUE) && (offset_ > kMIN_OFFSET))
    {
        value_ *= 10;
        --offset_;
    }

    while (value_ > kMAX_VALUE)
    {
        if (offset_ >= kMAX_OFFSET)
            Throw<std::runtime_error>("value overflow");

        value_ /= 10;
        ++offset_;
    }

    if ((offset_ < kMIN_OFFSET) || (value_ < kMIN_VALUE))
    {
        value_ = 0;
        isNegative_ = false;
        offset_ = -100;
        return;
    }

    if (offset_ > kMAX_OFFSET)
        Throw<std::runtime_error>("value overflow");

    XRPL_ASSERT(
        (value_ == 0) || ((value_ >= kMIN_VALUE) && (value_ <= kMAX_VALUE)),
        "xrpl::STAmount::canonicalize : value inside range");
    XRPL_ASSERT(
        (value_ == 0) || ((offset_ >= kMIN_OFFSET) && (offset_ <= kMAX_OFFSET)),
        "xrpl::STAmount::canonicalize : offset inside range");
    XRPL_ASSERT(
        (value_ != 0) || (offset_ != -100), "xrpl::STAmount::canonicalize : value or offset set");
}

/** Decompose a signed integer into `isNegative_` and `value_` (unsigned mantissa). */
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

/** Decode a 64-bit order-book sort key back into an `STAmount` quality.
 *
 *  Inverts the encoding performed by `getRate()`: extracts the 8-bit
 *  `(exponent + 100)` from the high bits and the 56-bit mantissa from the
 *  low bits, returning the corresponding `noIssue()` amount.  A zero rate
 *  returns a zero-valued `noIssue()` amount.
 *
 *  @param rate A 64-bit key as produced by `getRate()`.
 *  @return An `STAmount` representing the decoded quality.
 */
STAmount
amountFromQuality(std::uint64_t rate)
{
    if (rate == 0)
        return STAmount(noIssue());

    std::uint64_t const mantissa = rate & ~(255ull << (64 - 8));
    int const exponent = static_cast<int>(rate >> (64 - 8)) - 100;

    return STAmount(noIssue(), mantissa, exponent);
}

/** Parse an amount string into an `STAmount` for the given asset.
 *
 *  @param asset  The asset (XRP, IOU, or MPT) for the resulting amount.
 *  @param amount A decimal string such as `"100"` or `"1.5e2"`.
 *  @return The parsed `STAmount`.
 *  @throw std::runtime_error if `asset` is integral (XRP or MPT) and the
 *      string encodes a fractional value (negative exponent).
 */
STAmount
amountFromString(Asset const& asset, std::string const& amount)
{
    auto const parts = partsFromString(amount);
    if ((asset.native() || asset.holds<MPTIssue>()) && parts.exponent < 0)
        Throw<std::runtime_error>("XRP and MPT must be specified as integral amount.");
    return {asset, parts.mantissa, parts.exponent, parts.negative};
}

/** Parse a JSON value into an `STAmount`, supporting four input formats.
 *
 *  Recognized formats:
 *  - **Object** with `"value"` plus `"currency"` / `"issuer"` for IOU, or
 *    `"mpt_issuance_id"` for MPT.
 *  - **Array** `[value, currency_or_mptid, issuer]`.
 *  - **Delimited string** `"value/currency/issuer"` (tabs, newlines, commas, or `/`).
 *  - **Bare number or string** treated as an XRP drop count.
 *
 *  Asset type is inferred from which fields are present; fractional values
 *  are rejected for XRP and MPT since those are integer-only types.
 *
 *  @param name The SField to associate with the returned `STAmount`.
 *  @param v    The JSON value to parse.
 *  @return The parsed `STAmount`.
 *  @throw std::runtime_error on any format violation, invalid currency/issuer
 *      strings, or fractional XRP/MPT specification.
 */
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

/** Non-throwing wrapper around `amountFromJson()`.
 *
 *  On success, `result` is overwritten and `true` is returned.  On any
 *  exception, the error is logged at WARN level, `result` is unchanged, and
 *  `false` is returned.
 *
 *  @param result    Output parameter set on success.
 *  @param jvSource  The JSON value to parse.
 *  @return `true` on success; `false` if parsing threw.
 */
bool
amountFromJsonNoThrow(STAmount& result, json::Value const& jvSource)
{
    try
    {
        result = amountFromJson(kSF_GENERIC, jvSource);
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

/** Return `true` if both amounts represent the same asset and the same value.
 *
 *  Incomparable amounts (different asset types) are never equal.
 */
bool
operator==(STAmount const& lhs, STAmount const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

/** Return `true` if `lhs` is strictly less than `rhs`.
 *
 *  Ordering is by sign first, then exponent, then mantissa.  Amounts of
 *  different assets cannot be ordered and throw.
 *
 *  @throw std::runtime_error if the amounts are not comparable.
 */
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

/** Negate an amount; zero is returned unchanged (no negative zero). */
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

/** Compute `(multiplier × multiplicand) / divisor` exactly using 128-bit
 *  intermediate precision.
 *
 *  The 64-bit inputs are widened to `uint128_t` before multiplying, preventing
 *  overflow in the intermediate product.  The final quotient is checked to fit
 *  in 64 bits before truncation.
 *
 *  @throw std::overflow_error if the result exceeds `UINT64_MAX`.
 */
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

/** Compute `(multiplier × multiplicand + rounding) / divisor` with 128-bit
 *  intermediate precision.
 *
 *  Adds `rounding` to the product before dividing.  Callers pass
 *  `divisor − 1` as `rounding` to implement round-up (away from zero), or
 *  `0` for truncation.
 *
 *  @throw std::overflow_error if the result exceeds `UINT64_MAX`.
 */
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

/** Divide two amounts and produce a result with the given asset.
 *
 *  Integral operands are first scaled up into the IOU canonical mantissa
 *  window [10^15, 10^16).  The formula `muldiv(numVal, 10^17, denVal)`
 *  maintains full 15-digit precision; the `+5` bias provides a half-up
 *  rounding approximation in the legacy (non-strict) path.  The combined
 *  exponent is `numOffset − denOffset − 17`.
 *
 *  @param num   Numerator amount.
 *  @param den   Denominator amount.
 *  @param asset Asset identity for the result.
 *  @return The quotient as an `STAmount` with the given asset.
 *  @throw std::runtime_error on division by zero or result overflow.
 */
STAmount
divide(STAmount const& num, STAmount const& den, Asset const& asset)
{
    if (den == beast::kZERO)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::kZERO)
        return {asset};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.integral())
    {
        while (numVal < STAmount::kMIN_VALUE)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.integral())
    {
        while (denVal < STAmount::kMIN_VALUE)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    return STAmount(
        asset,
        muldiv(numVal, kTEN_TO17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

/** Multiply two amounts and produce a result with the given asset.
 *
 *  For all-native XRP or all-MPT, guards against overflow using factored
 *  comparisons against `sqrt(cMaxNative)` / `sqrt(maxMPTokenAmount)` before
 *  the 64-bit multiply, avoiding the 128-bit path for the common case.
 *
 *  For mixed or IOU operands, each mantissa is scaled into [10^15, 10^16),
 *  the 128-bit product is divided by 10^14, and `+7` provides a rounding
 *  bias.  The combined exponent is `offset1 + offset2 + 14`.
 *  When `getSTNumberSwitchover()` is active, delegates to `Number` arithmetic.
 *
 *  @param v1    First factor.
 *  @param v2    Second factor.
 *  @param asset Asset identity for the result.
 *  @return The product as an `STAmount` with the given asset.
 *  @throw std::runtime_error on overflow.
 */
STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset)
{
    if (v1 == beast::kZERO || v2 == beast::kZERO)
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
        while (value1 < STAmount::kMIN_VALUE)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.integral())
    {
        while (value2 < STAmount::kMIN_VALUE)
        {
            value2 *= 10;
            --offset2;
        }
    }

    return STAmount(
        asset,
        muldiv(value1, value2, kTEN_TO14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
}

/** Bring a mantissa/exponent pair into the canonical range after multiply or
 *  divide, using the legacy rounding rule.
 *
 *  For **integral** types: divides repeatedly until `offset >= −1`, adding 9
 *  (or 10 when only one intermediate divide was needed) before the final
 *  divide to round up.  The result is that fractions ≥ 0.1 round up while
 *  fractions < 0.1 round down — a historically baked-in XRP Ledger behavior.
 *
 *  For **IOU**: if `value > kMAX_VALUE`, repeatedly divides until
 *  `value <= 10 * kMAX_VALUE`, then adds 9 before the last divide to bias
 *  toward ceiling.
 *
 *  @note The `bool` fourth parameter is accepted for interface compatibility
 *      with `canonicalizeRoundStrict` but is ignored.
 *  @see canonicalizeRoundStrict for the corrected rounding variant.
 */
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
    else if (value > STAmount::kMAX_VALUE)
    {
        while (value > (10 * STAmount::kMAX_VALUE))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

/** Bring a mantissa/exponent pair into the canonical range after multiply or
 *  divide, tracking all remainder bits to round correctly.
 *
 *  Unlike `canonicalizeRound`, accumulates a `hadRemainder` flag across all
 *  intermediate divides.  The final bias is 10 (round up) when a remainder
 *  was observed and `roundUp` is true, otherwise 9.  This ensures that any
 *  truncated bits influence the rounding decision — not just the last
 *  fractional digit.
 *
 *  @param integral  `true` for XRP/MPT, `false` for IOU.
 *  @param value     The mantissa, modified in place.
 *  @param offset    The exponent, modified in place.
 *  @param roundUp   `true` to round away from zero; `false` to truncate.
 */
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
    else if (value > STAmount::kMAX_VALUE)
    {
        while (value > (10 * STAmount::kMAX_VALUE))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

/** Round an IOU amount to the precision implied by `scale`.
 *
 *  Constructs a reference value at `(kMIN_VALUE, scale)` and exploits IOU
 *  addition's exponent-alignment truncation: adding the reference forces the
 *  sum to be represented at the reference's precision, then subtracting the
 *  reference yields a result rounded to that precision.
 *
 *  Integral types (XRP, MPT) and zero are returned unchanged — no rounding
 *  is needed.  If `value.exponent() >= scale` the amount is already at or
 *  coarser than the target precision and is returned as-is to avoid losing
 *  information.
 *
 *  @param value    The IOU amount to round.
 *  @param scale    Target exponent; `value.exponent()` must be less than this.
 *  @param rounding Rounding mode applied to the intermediate addition via
 *      `NumberRoundModeGuard`.
 *  @return The rounded `STAmount`.
 */
STAmount
roundToScale(STAmount const& value, std::int32_t scale, Number::RoundingMode rounding)
{
    if (value.integral())
        return value;

    if (value == beast::kZERO)
        return value;

    // If the value's exponent is greater than or equal to the scale, then
    // rounding will do nothing, and might even lose precision, so just return
    // the value.
    if (value.exponent() >= scale)
        return value;

    STAmount const referenceValue{value.asset(), STAmount::kMIN_VALUE, scale, value.negative()};

    NumberRoundModeGuard const mg(rounding);
    // With an IOU, the the result of addition will be truncated to the
    // precision of the larger value, which in this case is referenceValue. Then
    // remove the reference value via subtraction, and we're left with the
    // rounded value.
    return (value + referenceValue) - referenceValue;
}

namespace {

/** No-op substitute for `NumberRoundModeGuard`.
 *
 *  Used as the `MightSaveRound` template argument in `mulRoundImpl` and
 *  `divRoundImpl` when the caller does not want to propagate a rounding mode
 *  into the thread-local `Number` round mode (i.e. the legacy `mulRound` /
 *  `divRound` paths).
 */
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

/** Shared implementation for `mulRound` and `mulRoundStrict`.
 *
 *  Template parameters allow selecting between the legacy and strict rounding
 *  strategies without code duplication:
 *  - `CanonicalizeFunc`: either `canonicalizeRound` (legacy) or
 *    `canonicalizeRoundStrict` (remainder-tracking).
 *  - `MightSaveRound`: either `NumberRoundModeGuard` (strict — propagates
 *    rounding into `Number` during `canonicalize()`) or
 *    `DontAffectNumberRoundMode` (legacy — no-op).
 *
 *  For all-native XRP and all-MPT, overflow is guarded via the same factored
 *  comparisons as `multiply()`.  For IOU, operands are scaled into
 *  [10^15, 10^16), multiplied via `muldivRound`, and the canonicalize function
 *  trims back into the canonical window with the desired rounding direction.
 *  When the rounded result is zero but `roundUp && !resultNegative`, the
 *  smallest representable positive value is returned instead.
 *
 *  @tparam CanonicalizeFunc  Post-multiply rounding adjuster function.
 *  @tparam MightSaveRound    RAII guard type for propagating rounding mode.
 *  @param v1      First factor.
 *  @param v2      Second factor.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero; `false` to truncate.
 *  @return The rounded product.
 *  @throw std::runtime_error on overflow.
 */
template <void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool), typename MightSaveRound>
static STAmount
mulRoundImpl(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    if (v1 == beast::kZERO || v2 == beast::kZERO)
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
        while (value1 < STAmount::kMIN_VALUE)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.integral())
    {
        while (value2 < STAmount::kMIN_VALUE)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool const resultNegative = v1.negative() != v2.negative();

    std::uint64_t amount =
        muldivRound(value1, value2, kTEN_TO14, (resultNegative != roundUp) ? kTEN_TO14M1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(asset.integral(), amount, offset, roundUp);
    }
    STAmount result = [&]() {
        // Tell Number to round toward zero so that STAmount::canonicalize
        // does not re-round in the unexpected direction.
        MightSaveRound const savedRound(Number::RoundingMode::TowardsZero);
        return STAmount(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (asset.integral())
        {
            amount = 1;
            offset = 0;
        }
        else
        {
            amount = STAmount::kMIN_VALUE;
            offset = STAmount::kMIN_OFFSET;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

/** Multiply with legacy rounding: fractions ≥ 0.1 round up, < 0.1 round down.
 *
 *  Uses `canonicalizeRound` (backward-compatible behavior baked into the
 *  XRP Ledger since inception) and does not propagate a rounding mode to the
 *  `Number` engine.
 *
 *  @param v1      First factor.
 *  @param v2      Second factor.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero.
 *  @return The rounded product.
 */
STAmount
mulRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    return mulRoundImpl<canonicalizeRound, DontAffectNumberRoundMode>(v1, v2, asset, roundUp);
}

/** Multiply with strict remainder-tracking rounding.
 *
 *  Uses `canonicalizeRoundStrict` and propagates the rounding direction to
 *  the thread-local `Number` round mode via `NumberRoundModeGuard`, ensuring
 *  `STAmount::canonicalize()` rounds consistently.
 *
 *  @param v1      First factor.
 *  @param v2      Second factor.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero.
 *  @return The rounded product.
 */
STAmount
mulRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp)
{
    return mulRoundImpl<canonicalizeRoundStrict, NumberRoundModeGuard>(v1, v2, asset, roundUp);
}

/** Shared implementation for `divRound` and `divRoundStrict`.
 *
 *  Scales each integral operand into [10^15, 10^16) before dividing, then
 *  computes `muldivRound(numVal, 10^17, denVal, rounding)` where `rounding`
 *  is `denVal − 1` when rounding away from zero, or `0` to truncate.
 *  `canonicalizeRound` then trims the result back into the canonical window.
 *  When the rounded result is zero but `roundUp && !resultNegative`, the
 *  smallest representable positive value is returned.
 *
 *  The `MightSaveRound` template parameter has the same semantics as in
 *  `mulRoundImpl`: use `NumberRoundModeGuard` for strict mode, or
 *  `DontAffectNumberRoundMode` for the legacy path.
 *
 *  @tparam MightSaveRound RAII guard type for propagating rounding mode.
 *  @param num     Numerator.
 *  @param den     Denominator.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero; `false` to truncate.
 *  @return The rounded quotient.
 *  @throw std::runtime_error on division by zero or overflow.
 */
template <typename MightSaveRound>
static STAmount
divRoundImpl(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    if (den == beast::kZERO)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::kZERO)
        return {asset};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    if (num.integral())
    {
        while (numVal < STAmount::kMIN_VALUE)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.integral())
    {
        while (denVal < STAmount::kMIN_VALUE)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    bool const resultNegative = (num.negative() != den.negative());

    std::uint64_t amount =
        muldivRound(numVal, kTEN_TO17, denVal, (resultNegative != roundUp) ? denVal - 1 : 0);

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
            amount = 1;
            offset = 0;
        }
        else
        {
            amount = STAmount::kMIN_VALUE;
            offset = STAmount::kMIN_OFFSET;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

/** Divide with legacy rounding (same ≥ 0.1 / < 0.1 threshold as `mulRound`).
 *
 *  Does not propagate a rounding mode to the `Number` engine.
 *
 *  @param num     Numerator.
 *  @param den     Denominator.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero.
 *  @return The rounded quotient.
 */
STAmount
divRound(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    return divRoundImpl<DontAffectNumberRoundMode>(num, den, asset, roundUp);
}

/** Divide with strict remainder-tracking rounding.
 *
 *  Propagates the rounding direction to the thread-local `Number` round mode
 *  via `NumberRoundModeGuard`, so that `STAmount::canonicalize()` rounds
 *  consistently.
 *
 *  @param num     Numerator.
 *  @param den     Denominator.
 *  @param asset   Asset for the result.
 *  @param roundUp `true` to round away from zero.
 *  @return The rounded quotient.
 */
STAmount
divRoundStrict(STAmount const& num, STAmount const& den, Asset const& asset, bool roundUp)
{
    return divRoundImpl<NumberRoundModeGuard>(num, den, asset, roundUp);
}

}  // namespace xrpl
