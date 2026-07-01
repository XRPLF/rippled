#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/json_get_or_throw.h>

namespace xrpl {

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
class STAmount final : public STBase, public CountedObject<STAmount>
{
public:
    using mantissa_type = std::uint64_t;
    using exponent_type = int;
    using rep = std::pair<mantissa_type, exponent_type>;

private:
    Asset asset_;
    mantissa_type value_{};
    exponent_type offset_;
    bool isNegative_{};

public:
    using value_type = STAmount;

    static constexpr int kMinOffset = -96;
    static constexpr int kMaxOffset = 80;

    // Maximum native value supported by the code
    static constexpr std::uint64_t kMinValue = 1'000'000'000'000'000ull;
    static_assert(isPowerOfTen(kMinValue));
    static constexpr std::uint64_t kMaxValue = (kMinValue * 10) - 1;
    static_assert(kMaxValue == 9'999'999'999'999'999ull);
    static constexpr std::uint64_t kMaxNative = 9'000'000'000'000'000'000ull;

    // Max native value on network.
    static constexpr std::uint64_t kMaxNativeN = 100'000'000'000'000'000ull;
    static constexpr std::uint64_t kIssuedCurrency = 0x8'000'000'000'000'000ull;
    static constexpr std::uint64_t kPositive = 0x4'000'000'000'000'000ull;
    static constexpr std::uint64_t kMpToken = 0x2'000'000'000'000'000ull;
    static constexpr std::uint64_t kValueMask = ~(kPositive | kMpToken);

    static std::uint64_t const kURateOne;

    //--------------------------------------------------------------------------
    STAmount(SerialIter& sit, SField const& name);

    struct Unchecked
    {
        explicit Unchecked() = default;
    };

    // Do not call canonicalize
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        Unchecked);

    template <AssetType A>
    STAmount(
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        Unchecked);

    // Call canonicalize
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa = 0,
        exponent_type exponent = 0,
        bool negative = false);

    STAmount(SField const& name, std::int64_t mantissa);

    STAmount(SField const& name, std::uint64_t mantissa = 0, bool negative = false);

    explicit STAmount(std::uint64_t mantissa = 0, bool negative = false);

    explicit STAmount(SField const& name, STAmount const& amt);

    template <AssetType A>
    STAmount(A const& asset, std::uint64_t mantissa = 0, int exponent = 0, bool negative = false)
        : asset_(asset), value_(mantissa), offset_(exponent), isNegative_(negative)
    {
        canonicalize();
    }

    // VFALCO Is this needed when we have the previous signature?
    template <AssetType A>
    STAmount(A const& asset, std::uint32_t mantissa, int exponent = 0, bool negative = false);

    template <AssetType A>
    STAmount(A const& asset, std::int64_t mantissa, int exponent = 0);

    template <AssetType A>
    STAmount(A const& asset, int mantissa, int exponent = 0);

    template <AssetType A>
    STAmount(A const& asset, Number const& number) : STAmount(fromNumber(asset, number))
    {
    }

    // Legacy support for new-style amounts
    STAmount(IOUAmount const& amount, Issue const& issue);
    STAmount(XRPAmount const& amount);
    STAmount(MPTAmount const& amount, MPTIssue const& mptIssue);
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    [[nodiscard]] int
    exponent() const noexcept;

    [[nodiscard]] bool
    integral() const noexcept;

    [[nodiscard]] bool
    native() const noexcept;

    template <ValidIssueType TIss>
    [[nodiscard]] constexpr bool
    holds() const noexcept;

    [[nodiscard]] bool
    negative() const noexcept;

    [[nodiscard]] std::uint64_t
    mantissa() const noexcept;

    [[nodiscard]] Asset const&
    asset() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    template <ValidIssueType TIss>
    TIss&
    get();

    [[nodiscard]] AccountID const&
    getIssuer() const;

    [[nodiscard]] int
    signum() const noexcept;

    /** Returns a zero value with the same issuer and currency. */
    [[nodiscard]] STAmount
    zeroed() const;

    void
    setJson(json::Value&) const;

    [[nodiscard]] STAmount const&
    value() const noexcept;

    /**
     * Checks if this amount evaluates to zero when constrained to a specific
     * accounting scale.
     * For XRP and MPT `roundToScale` is a no-op, returns true only when the amount itself is zero.
     * The `scale` argument is ignored in that case.
     * For IOU, the amount is rounded to the given scale using Number::RoundingMode::ToNearest mode
     * and the result is checked for zero; if `scale <= exponent()`, `roundToScale` short-circuits
     * and returns the value unchanged, so this returns false for any non-zero amount.
     *
     * @param scale The target accounting scale to evaluate against.
     * @return `true` if this amount rounds to zero at the given scale, `false` otherwise.
     *
     * @see roundToScale
     */
    [[nodiscard]] bool
    isZeroAtScale(int scale) const;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit
    operator bool() const noexcept;

    STAmount&
    operator+=(STAmount const&);
    STAmount&
    operator-=(STAmount const&);

    STAmount& operator=(beast::Zero);

    STAmount&
    operator=(XRPAmount const& amount);

    STAmount&
    operator=(Number const&);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    void
    negate();

    void
    clear();

    // Zero while copying currency and issuer.
    void
    clear(Asset const& asset);

    /** Set the Issue for this amount. */
    void
    setIssue(Asset const& asset);

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    [[nodiscard]] SerializedTypeID
    getSType() const override;

    [[nodiscard]] std::string
    getFullText() const override;

    [[nodiscard]] std::string
    getText() const override;

    [[nodiscard]] json::Value getJson(JsonOptions = JsonOptions::Values::None) const override;

    void
    add(Serializer& s) const override;

    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    [[nodiscard]] bool
    isDefault() const override;

    [[nodiscard]] XRPAmount
    xrp() const;
    [[nodiscard]] IOUAmount
    iou() const;
    [[nodiscard]] MPTAmount
    mpt() const;

private:
    template <AssetType A>
    static STAmount
    fromNumber(A const& asset, Number const& number);

    static std::unique_ptr<STAmount>
    construct(SerialIter&, SField const& name);

    void
    set(std::int64_t v);
    void
    canonicalize();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    STAmount&
    operator=(IOUAmount const& iou);

    friend class detail::STVar;

    friend STAmount
    operator+(STAmount const& v1, STAmount const& v2);
};

template <AssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool negative,
    Unchecked)
    : STBase(name), asset_(asset), value_(mantissa), offset_(exponent), isNegative_(negative)
{
}

template <AssetType A>
STAmount::STAmount(
    A const& asset,
    mantissa_type mantissa,
    exponent_type exponent,
    bool negative,
    Unchecked)
    : asset_(asset), value_(mantissa), offset_(exponent), isNegative_(negative)
{
}

template <AssetType A>
STAmount::STAmount(
    SField const& name,
    A const& asset,
    std::uint64_t mantissa,
    int exponent,
    bool negative)
    : STBase(name), asset_(asset), value_(mantissa), offset_(exponent), isNegative_(negative)
{
    // value_ is uint64, but needs to fit in the range of int64
    if (Number::getMantissaScale() == MantissaRange::MantissaScale::Small)
    {
        XRPL_ASSERT(
            value_ <= std::numeric_limits<std::int64_t>::max(),
            "xrpl::STAmount::STAmount(SField, A, std::uint64_t, int, bool) : "
            "maximum mantissa input");
    }
    else
    {
        if (integral() && value_ > std::numeric_limits<std::int64_t>::max())
            throw std::overflow_error("STAmount mantissa is too large " + std::to_string(mantissa));
    }
    canonicalize();
}

template <AssetType A>
STAmount::STAmount(A const& asset, std::int64_t mantissa, int exponent)
    : asset_(asset), offset_(exponent)
{
    set(mantissa);
    canonicalize();
}

template <AssetType A>
STAmount::STAmount(A const& asset, std::uint32_t mantissa, int exponent, bool negative)
    : STAmount(asset, safeCast<std::uint64_t>(mantissa), exponent, negative)
{
}

template <AssetType A>
STAmount::STAmount(A const& asset, int mantissa, int exponent)
    : STAmount(asset, safeCast<std::int64_t>(mantissa), exponent)
{
}

// Legacy support for new-style amounts
inline STAmount::STAmount(IOUAmount const& amount, Issue const& issue)
    : asset_(issue), offset_(amount.exponent()), isNegative_(amount < beast::kZero)
{
    if (isNegative_)
    {
        value_ = unsafeCast<std::uint64_t>(-amount.mantissa());
    }
    else
    {
        value_ = unsafeCast<std::uint64_t>(amount.mantissa());
    }

    canonicalize();
}

inline STAmount::STAmount(MPTAmount const& amount, MPTIssue const& mptIssue)
    : asset_(mptIssue), offset_(0), isNegative_(amount < beast::kZero)
{
    if (isNegative_)
    {
        value_ = unsafeCast<std::uint64_t>(-amount.value());
    }
    else
    {
        value_ = unsafeCast<std::uint64_t>(amount.value());
    }

    canonicalize();
}

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

// VFALCO TODO The parameter type should be Quality not uint64_t
STAmount
amountFromQuality(std::uint64_t rate);

STAmount
amountFromString(Asset const& asset, std::string const& amount);

STAmount
amountFromJson(SField const& name, json::Value const& v);

bool
amountFromJsonNoThrow(STAmount& result, json::Value const& jvSource);

// IOUAmount and XRPAmount define toSTAmount, defining this
// trivial conversion here makes writing generic code easier
inline STAmount const&
toSTAmount(STAmount const& a)
{
    return a;  // NOLINT(bugprone-return-const-ref-from-parameter)
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline int
STAmount::exponent() const noexcept
{
    return offset_;
}

inline bool
STAmount::integral() const noexcept
{
    return asset_.integral();
}

inline bool
STAmount::native() const noexcept
{
    return asset_.native();
}

template <ValidIssueType TIss>
constexpr bool
STAmount::holds() const noexcept
{
    return asset_.holds<TIss>();
}

inline bool
STAmount::negative() const noexcept
{
    return isNegative_;
}

inline std::uint64_t
STAmount::mantissa() const noexcept
{
    return value_;
}

inline Asset const&
STAmount::asset() const
{
    return asset_;
}

template <ValidIssueType TIss>
[[nodiscard]] constexpr TIss const&
STAmount::get() const
{
    return asset_.get<TIss>();
}

template <ValidIssueType TIss>
TIss&
STAmount::get()
{
    return asset_.get<TIss>();
}

inline AccountID const&
STAmount::getIssuer() const
{
    return asset_.getIssuer();
}

inline int
STAmount::signum() const noexcept
{
    if (value_ == 0u)
        return 0;
    return isNegative_ ? -1 : 1;
}

inline STAmount
STAmount::zeroed() const
{
    return STAmount(asset_);
}

inline STAmount::
operator bool() const noexcept
{
    return *this != beast::kZero;
}

inline STAmount::
operator Number() const
{
    return asset().visit(
        [&](Issue const& issue) -> Number {
            if (issue.native())
                return xrp();
            return iou();
        },
        [&](MPTIssue const&) -> Number { return mpt(); });
}

inline STAmount&
STAmount::operator=(beast::Zero)
{
    clear();
    return *this;
}

inline STAmount&
STAmount::operator=(XRPAmount const& amount)
{
    *this = STAmount(amount);
    return *this;
}

template <AssetType A>
inline STAmount
STAmount::fromNumber(A const& a, Number const& number)
{
    bool const negative = number.mantissa() < 0;
    Number const working{negative ? -number : number};
    Asset const asset{a};
    if (asset.integral())
    {
        std::uint64_t const intValue = static_cast<std::int64_t>(working);
        return STAmount{asset, intValue, 0, negative};
    }

    auto const [mantissa, exponent] = working.normalizeToRange<kMinValue, kMaxValue>();

    return STAmount{asset, mantissa, exponent, negative};
}

inline void
STAmount::negate()
{
    if (*this != beast::kZero)
        isNegative_ = !isNegative_;
}

inline void
STAmount::clear()
{
    // The -100 is used to allow 0 to sort less than a small positive values
    // which have a negative exponent.
    offset_ = integral() ? 0 : -100;
    value_ = 0;
    isNegative_ = false;
}

inline void
STAmount::clear(Asset const& asset)
{
    setIssue(asset);
    clear();
}

inline STAmount const&
STAmount::value() const noexcept
{
    return *this;
}

[[nodiscard]] inline bool
isLegalNet(STAmount const& value)
{
    return !value.native() || (value.mantissa() <= STAmount::kMaxNativeN);
}

[[nodiscard]] inline bool
isLegalMPT(STAmount const& value)
{
    return !value.holds<MPTIssue>() ||
        (!value.negative() && value.exponent() == 0 && value.mantissa() <= kMaxMpTokenAmount);
}

/* Check recursively if an object has invalid MPTAmount or XRPAmount in STAmount field.
 * Calls isLegalNet() and isLegalMPT().
 */
[[nodiscard]] bool
hasInvalidAmount(STBase const& field, beast::Journal j);

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs);
bool
operator<(STAmount const& lhs, STAmount const& rhs);

inline bool
operator!=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs == rhs);
}

inline bool
operator>(STAmount const& lhs, STAmount const& rhs)
{
    return rhs < lhs;
}

inline bool
operator<=(STAmount const& lhs, STAmount const& rhs)
{
    return !(rhs < lhs);
}

inline bool
operator>=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs < rhs);
}

STAmount
operator-(STAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STAmount
operator+(STAmount const& v1, STAmount const& v2);
STAmount
operator-(STAmount const& v1, STAmount const& v2);

STAmount
divide(STAmount const& v1, STAmount const& v2, Asset const& asset);

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset);

// multiply rounding result in specified direction
STAmount
mulRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

// multiply following the rounding directions more precisely.
STAmount
mulRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

// divide rounding result in specified direction
STAmount
divRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

// divide following the rounding directions more precisely.
STAmount
divRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// VFALCO TODO Return a Quality object
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn);

/** Round an arbitrary precision Amount to the precision of an STAmount that has
 * a given exponent.
 *
 * This is used to ensure that calculations involving IOU amounts do not collect
 * dust beyond the precision of the reference value.
 *
 * @param value The value to be rounded
 * @param scale An exponent value to establish the precision limit of
 *     `value`. Should be larger than `value.exponent()`.
 * @param rounding Optional Number rounding mode
 *
 */
[[nodiscard]] STAmount
roundToScale(
    STAmount const& value,
    std::int32_t scale,
    Number::RoundingMode rounding = Number::getround());

/** Round an arbitrary precision Number IN PLACE to the precision of a given
 * Asset.
 *
 * This is used to ensure that calculations do not collect dust for IOUs, or
 * fractional amounts for the integral types XRP and MPT.
 *
 * @param asset The relevant asset
 * @param value The lvalue to be rounded
 */
template <AssetType A>
void
roundToAsset(A const& asset, Number& value)
{
    value = STAmount{asset, value};
}

/** Round an arbitrary precision Number to the precision of a given Asset.
 *
 * This is used to ensure that calculations do not collect dust beyond specified
 * scale for IOUs, or fractional amounts for the integral types XRP and MPT.
 *
 * @param asset The relevant asset
 * @param value The value to be rounded
 * @param scale Only relevant to IOU assets. An exponent value to establish the
 *      precision limit of `value`. Should be larger than `value.exponent()`.
 * @param rounding Optional Number rounding mode
 */
template <AssetType A>
[[nodiscard]] Number
roundToAsset(
    A const& asset,
    Number const& value,
    std::int32_t scale,
    Number::RoundingMode rounding = Number::getround())
{
    NumberRoundModeGuard const mg(rounding);
    STAmount const ret{asset, value};
    if (ret.integral())
        return ret;
    // Note that the ctor will round integral types (XRP, MPT) via canonicalize,
    // so no extra work is needed for those.
    return roundToScale(ret, scale);
}

//------------------------------------------------------------------------------

inline bool
isXRP(STAmount const& amount)
{
    return amount.native();
}

bool
canAdd(STAmount const& amt1, STAmount const& amt2);

bool
canSubtract(STAmount const& amt1, STAmount const& amt2);

/** Get the scale of a Number for a given asset.
 *
 * "scale" is similar to "exponent", but from the perspective of STAmount, which has different rules
 * and mantissa ranges for determining the exponent than Number.
 *
 * @param number The Number to get the scale of.
 * @param asset The asset to use for determining the scale.
 * @return The scale of this Number for the given asset.
 */
inline int
scale(Number const& number, Asset const& asset)
{
    return STAmount{asset, number}.exponent();
}

}  // namespace xrpl

//------------------------------------------------------------------------------
namespace json {
template <>
inline xrpl::STAmount
getOrThrow(json::Value const& v, xrpl::SField const& field)
{
    using namespace xrpl;
    json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    json::Value const& inner = v[key];
    return amountFromJson(field, inner);
}
}  // namespace json
