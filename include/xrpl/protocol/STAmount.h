/** @file
 * Canonical on-ledger amount type unifying XRP, IOU, and MPT quantities.
 *
 * `STAmount` is the serializable amount type used throughout the XRP Ledger.
 * It stores XRP drops, IOU floating-point amounts, and Multi-Purpose Token
 * (MPT) integers behind a single interface that integrates with the ledger's
 * typed-field system via `STBase`.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/json_get_or_throw.h>

namespace xrpl {

/** Unified serializable amount for XRP, IOU, and MPT assets.
 *
 * `STAmount` is the canonical on-ledger amount type. It stores three
 * fundamentally different quantity kinds — XRP drops, IOU floating-point
 * amounts, and Multi-Purpose Token integers — behind a single interface
 * that integrates with the ledger's typed-field system via `STBase`.
 *
 * ## Internal representation
 *
 * For **IOU** amounts the value is stored as normalized scientific notation:
 * `amount = value × 10^offset`. The mantissa is in `[kMIN_VALUE, kMAX_VALUE]`
 * i.e. `[10^15, 10^16 − 1]`, and the exponent is in `[kMIN_OFFSET, kMAX_OFFSET]`
 * i.e. `[-96, +80]`. Zero is encoded as `value = 0, offset = −100`; the
 * sentinel −100 ensures that zero sorts below every positive IOU with a
 * large-negative exponent.
 *
 * For **XRP and MPT** (`integral()` types) `offset` is always 0 and `value`
 * directly holds the raw drop or token count. XRP is bounded by `kMAX_NATIVE_N`
 * (10^17 drops); MPT is bounded by `INT64_MAX`.
 *
 * ## Wire encoding
 *
 * Amounts are serialised into a packed 64-bit word:
 * - Bit 63 = 0 → native (XRP or MPT); bit 61 further distinguishes them.
 * - Bit 63 = 1 → issued currency (IOU).
 * - Bit 62 = sign (1 = positive).
 * - For IOU: bits 55–62 = `offset + 97`; bits 0–53 = mantissa.
 *
 * @note `canonicalize()` normalises the mantissa into `[kMIN_VALUE, kMAX_VALUE]`
 *     on every checked construction path. Constructors tagged `Unchecked` skip
 *     this step and require the caller to guarantee the representation is
 *     already canonical.
 */
class STAmount final : public STBase, public CountedObject<STAmount>
{
public:
    /** Unsigned integer type used to store the IOU mantissa or integral amount value. */
    using mantissa_type = std::uint64_t;
    /** Signed integer type used to store the IOU base-10 exponent. */
    using exponent_type = int;
    /** Pair of (mantissa, exponent) for use in serialization and arithmetic helpers. */
    using rep = std::pair<mantissa_type, exponent_type>;

private:
    Asset asset_;
    mantissa_type value_{};
    exponent_type offset_;
    bool isNegative_{};

public:
    using value_type = STAmount;

    /** Minimum legal IOU exponent (offset). Zero and integral types always use 0. */
    constexpr static int kMIN_OFFSET = -96;
    /** Maximum legal IOU exponent (offset). */
    constexpr static int kMAX_OFFSET = 80;

    /** Minimum normalized IOU mantissa (10^15). Mantissas below this are scaled up. */
    constexpr static std::uint64_t kMIN_VALUE = 1'000'000'000'000'000ull;
    static_assert(isPowerOfTen(kMIN_VALUE));
    /** Maximum normalized IOU mantissa (10^16 − 1). Mantissas above this are scaled down. */
    constexpr static std::uint64_t kMAX_VALUE = (kMIN_VALUE * 10) - 1;
    static_assert(kMAX_VALUE == 9'999'999'999'999'999ull);
    /** Absolute maximum XRP/MPT value that the code will store internally
     *  (9 × 10^18 drops). Enforcement happens in the wire decoder and
     *  network-validity check (@ref isLegalNet). */
    constexpr static std::uint64_t kMAX_NATIVE = 9'000'000'000'000'000'000ull;

    /** Maximum XRP drop value permitted on the network (10^17 = 100 billion XRP).
     *  Validated by @ref isLegalNet; amounts above this are consensus-invalid. */
    constexpr static std::uint64_t kMAX_NATIVE_N = 100'000'000'000'000'000ull;

    // --- Wire-format flag bits (bit 63 is MSB) ---

    /** Wire bit 63: set for IOU amounts, clear for native (XRP or MPT). */
    constexpr static std::uint64_t kISSUED_CURRENCY = 0x8'000'000'000'000'000ull;
    /** Wire bit 62: sign bit — set means positive. */
    constexpr static std::uint64_t kPOSITIVE = 0x4'000'000'000'000'000ull;
    /** Wire bit 61: distinguishes MPT (set) from XRP (clear) for native amounts. */
    constexpr static std::uint64_t kMP_TOKEN = 0x2'000'000'000'000'000ull;
    /** Mask that strips the `kPOSITIVE` and `kMP_TOKEN` flag bits, leaving the
     *  raw value word for MPT amounts. */
    constexpr static std::uint64_t kVALUE_MASK = ~(kPOSITIVE | kMP_TOKEN);

    /** Wire encoding of a unit quality offer (rate = 1.0). */
    static std::uint64_t const kU_RATE_ONE;

    //--------------------------------------------------------------------------
    //
    // Constructors
    //
    //--------------------------------------------------------------------------

    /** Deserialize an STAmount from a byte stream.
     *
     * Decodes the compact 64-bit wire word plus any trailing currency/issuer
     * or MPTID bytes. Throws `std::runtime_error` on malformed input
     * (negative zero, mantissa out of range, invalid currency/account).
     *
     * @param sit Source iterator positioned at the first byte of the amount.
     * @param name The SField that names this field in the parent STObject.
     */
    STAmount(SerialIter& sit, SField const& name);

    /** Tag type that bypasses `canonicalize()` on construction.
     *
     * Use only when the caller can guarantee the representation is already
     * in canonical form (e.g. inside arithmetic helpers that maintain
     * invariants, or when reading from a known-good source). Prefer the
     * checked constructors for all other call sites.
     */
    struct Unchecked
    {
        explicit Unchecked() = default;
    };

    /** Construct a named STAmount with a pre-canonical representation.
     *
     * Stores `mantissa × 10^exponent` (with sign) verbatim — `canonicalize()`
     * is **not** called. The caller must ensure the values satisfy the IOU
     * invariants or, for integral assets, that `exponent == 0`.
     *
     * @param name   SField associated with this amount.
     * @param asset  Asset type (Issue or MPTIssue).
     * @param mantissa Raw unsigned mantissa.
     * @param exponent Base-10 exponent.
     * @param negative True if the amount is negative.
     */
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        Unchecked);

    /** Construct an anonymous STAmount with a pre-canonical representation.
     *
     * Anonymous (no SField) variant of the `Unchecked` constructor above.
     *
     * @param asset  Asset type (Issue or MPTIssue).
     * @param mantissa Raw unsigned mantissa.
     * @param exponent Base-10 exponent.
     * @param negative True if the amount is negative.
     */
    template <AssetType A>
    STAmount(
        A const& asset,
        mantissa_type mantissa,
        exponent_type exponent,
        bool negative,
        Unchecked);

    /** Construct a named STAmount, calling `canonicalize()` afterward.
     *
     * Normalises the mantissa into `[kMIN_VALUE, kMAX_VALUE]` by adjusting
     * the exponent. Throws `std::runtime_error` on overflow. Subnormals
     * (exponent below `kMIN_OFFSET` after scaling) are silently zeroed.
     *
     * @param name     SField associated with this amount.
     * @param asset    Asset type (Issue or MPTIssue).
     * @param mantissa Unsigned mantissa (defaults to 0 → zero amount).
     * @param exponent Base-10 exponent (defaults to 0).
     * @param negative True if the amount is negative (defaults to false).
     */
    template <AssetType A>
    STAmount(
        SField const& name,
        A const& asset,
        mantissa_type mantissa = 0,
        exponent_type exponent = 0,
        bool negative = false);

    /** Construct a named XRP amount from a signed 64-bit drop count.
     *
     * Negative values set the sign flag; the stored mantissa is the absolute value.
     *
     * @param name     SField associated with this amount.
     * @param mantissa Signed drop count.
     */
    STAmount(SField const& name, std::int64_t mantissa);

    /** Construct a named XRP amount from an unsigned 64-bit drop count.
     *
     * @param name     SField associated with this amount.
     * @param mantissa Unsigned drop count (defaults to 0).
     * @param negative True if the amount is negative (defaults to false).
     */
    STAmount(SField const& name, std::uint64_t mantissa = 0, bool negative = false);

    /** Construct an anonymous XRP amount from an unsigned 64-bit drop count.
     *
     * @param mantissa Unsigned drop count (defaults to 0).
     * @param negative True if the amount is negative (defaults to false).
     */
    explicit STAmount(std::uint64_t mantissa = 0, bool negative = false);

    /** Construct a named copy of an existing STAmount, preserving asset and value.
     *
     * @param name SField to attach to the copy.
     * @param amt  Source amount.
     */
    explicit STAmount(SField const& name, STAmount const& amt);

    /** Construct an anonymous STAmount with the given asset, calling `canonicalize()`.
     *
     * @param asset    Asset type (Issue or MPTIssue).
     * @param mantissa Unsigned mantissa (defaults to 0).
     * @param exponent Base-10 exponent (defaults to 0).
     * @param negative True if the amount is negative (defaults to false).
     */
    template <AssetType A>
    STAmount(A const& asset, std::uint64_t mantissa = 0, int exponent = 0, bool negative = false)
        : asset_(asset), value_(mantissa), offset_(exponent), isNegative_(negative)
    {
        canonicalize();
    }

    /** Construct an anonymous STAmount from a 32-bit unsigned mantissa.
     *
     * Widens to `uint64_t` then delegates to the canonical constructor.
     *
     * @param asset    Asset type (Issue or MPTIssue).
     * @param mantissa 32-bit unsigned mantissa.
     * @param exponent Base-10 exponent (defaults to 0).
     * @param negative True if the amount is negative (defaults to false).
     */
    // VFALCO Is this needed when we have the previous signature?
    template <AssetType A>
    STAmount(A const& asset, std::uint32_t mantissa, int exponent = 0, bool negative = false);

    /** Construct an anonymous STAmount from a signed 64-bit mantissa.
     *
     * Negative values set the sign flag; the stored mantissa is the absolute value.
     *
     * @param asset    Asset type (Issue or MPTIssue).
     * @param mantissa Signed mantissa; sign extracted via `set()`.
     * @param exponent Base-10 exponent (defaults to 0).
     */
    template <AssetType A>
    STAmount(A const& asset, std::int64_t mantissa, int exponent = 0);

    /** Construct an anonymous STAmount from a plain `int` mantissa.
     *
     * Widens to `int64_t` then delegates to the signed constructor.
     *
     * @param asset    Asset type (Issue or MPTIssue).
     * @param mantissa Signed integer mantissa.
     * @param exponent Base-10 exponent (defaults to 0).
     */
    template <AssetType A>
    STAmount(A const& asset, int mantissa, int exponent = 0);

    /** Construct an STAmount from a `Number`, rounding to the asset's precision.
     *
     * Converts the high-precision `Number` into the appropriate internal
     * representation. For integral assets (XRP, MPT) the fractional part is
     * dropped; for IOU assets the mantissa is normalised into
     * `[kMIN_VALUE, kMAX_VALUE]`.
     *
     * @param asset  Asset type (Issue or MPTIssue).
     * @param number High-precision value to convert.
     */
    template <AssetType A>
    STAmount(A const& asset, Number const& number) : STAmount(fromNumber(asset, number))
    {
    }

    /** Construct from a lean `IOUAmount` and its associated `Issue`.
     *
     * Bridges from the lightweight `IOUAmount` representation to the
     * serializable `STAmount` form.
     *
     * @param amount Lean IOU amount (mantissa + exponent).
     * @param issue  Currency/issuer identity for the resulting STAmount.
     */
    STAmount(IOUAmount const& amount, Issue const& issue);

    /** Construct from a lean `XRPAmount`.
     *
     * @param amount XRP drop count.
     */
    STAmount(XRPAmount const& amount);

    /** Construct from a lean `MPTAmount` and its associated `MPTIssue`.
     *
     * @param amount   Lean MPT amount (raw integer token count).
     * @param mptIssue MPT issuance identity.
     */
    STAmount(MPTAmount const& amount, MPTIssue const& mptIssue);

    /** Convert to a high-precision `Number`.
     *
     * Dispatches via `Asset::visit()` to the appropriate lean extractor
     * (`xrp()`, `iou()`, or `mpt()`) and constructs a `Number` from it.
     */
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    /** Return the base-10 exponent.
     *
     * For IOU amounts this is in `[kMIN_OFFSET, kMAX_OFFSET]`, or −100 when
     * the amount is zero. For XRP and MPT amounts this is always 0.
     */
    [[nodiscard]] int
    exponent() const noexcept;

    /** True if this amount is an integral (non-floating-point) type.
     *
     * Returns true for both XRP and MPT; false for IOU. Integral types store
     * `offset == 0` and a raw integer token count in `value`.
     */
    [[nodiscard]] bool
    integral() const noexcept;

    /** True if this amount represents native XRP.
     *
     * Returns false for IOU and MPT amounts.
     */
    [[nodiscard]] bool
    native() const noexcept;

    /** True if the embedded asset is of type `TIss`.
     *
     * @tparam TIss Either `Issue` (covers both XRP and IOU) or `MPTIssue`.
     */
    template <ValidIssueType TIss>
    [[nodiscard]] constexpr bool
    holds() const noexcept;

    /** True if this amount is negative.
     *
     * A canonical zero amount is never negative.
     */
    [[nodiscard]] bool
    negative() const noexcept;

    /** Return the raw unsigned mantissa.
     *
     * For IOU amounts this is in `[kMIN_VALUE, kMAX_VALUE]` (or 0 for zero).
     * For XRP and MPT amounts this is the raw drop or token count.
     */
    [[nodiscard]] std::uint64_t
    mantissa() const noexcept;

    /** Return the asset (Issue or MPTIssue) carried by this amount. */
    [[nodiscard]] Asset const&
    asset() const;

    /** Return the embedded asset as the specific issue type `TIss`.
     *
     * @tparam TIss Either `Issue` or `MPTIssue`.
     * @throws std::logic_error if the asset is not of type `TIss`.
     */
    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    /** Mutable variant of `get<TIss>()`.
     *
     * @tparam TIss Either `Issue` or `MPTIssue`.
     * @throws std::logic_error if the asset is not of type `TIss`.
     */
    template <ValidIssueType TIss>
    TIss&
    get();

    /** Return the issuer account for IOU amounts; `noAccount()` for XRP;
     *  the MPT issuer account for MPT amounts. */
    [[nodiscard]] AccountID const&
    getIssuer() const;

    /** Return the sign as −1, 0, or +1.
     *
     * A canonical zero always returns 0 regardless of the `negative` flag.
     */
    [[nodiscard]] int
    signum() const noexcept;

    /** Returns a zero value with the same issuer and currency. */
    [[nodiscard]] STAmount
    zeroed() const;

    /** Populate a JSON object with the amount's fields (value, currency, issuer / mpt_issuance_id). */
    void
    setJson(json::Value&) const;

    /** Returns a const reference to `*this`.
     *
     * Provided so that `STAmount` satisfies the same `value()` accessor
     * pattern as the lean amount types (`XRPAmount`, `IOUAmount`, `MPTAmount`),
     * enabling generic template code that calls `.value()` uniformly.
     */
    [[nodiscard]] STAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    /** True if the amount is non-zero. */
    explicit
    operator bool() const noexcept;

    /** Add `rhs` to this amount in place.
     *
     * @pre Both amounts must have the same asset; mixing asset types is
     *     undefined behaviour and will produce a wrong result at runtime.
     */
    STAmount&
    operator+=(STAmount const&);

    /** Subtract `rhs` from this amount in place.
     *
     * @pre Both amounts must have the same asset; mixing asset types is
     *     undefined behaviour and will produce a wrong result at runtime.
     */
    STAmount&
    operator-=(STAmount const&);

    /** Zero this amount, preserving its asset identity. */
    STAmount& operator=(beast::Zero);

    /** Assign from a lean `XRPAmount`, preserving the XRP asset identity. */
    STAmount&
    operator=(XRPAmount const& amount);

    /** Assign from a `Number`, rounding to the current asset's precision. */
    STAmount&
    operator=(Number const&);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    /** Flip the sign; a canonical zero amount is left unchanged. */
    void
    negate();

    /** Reset to zero while keeping the current asset identity.
     *
     * For IOU amounts sets `offset` to −100 (the canonical zero sentinel so
     * that zero sorts below small positive IOUs). For integral types sets
     * `offset` to 0.
     */
    void
    clear();

    /** Reset to zero with a new asset identity.
     *
     * Equivalent to `setIssue(asset); clear();`.
     *
     * @param asset The asset to adopt.
     */
    void
    clear(Asset const& asset);

    /** Replace the asset identity without changing the value representation.
     *
     * @param asset New asset (Issue or MPTIssue).
     */
    void
    setIssue(Asset const& asset);

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    /** Returns `STI_AMOUNT`. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** Returns a human-readable string including the field name and formatted value. */
    [[nodiscard]] std::string
    getFullText() const override;

    /** Returns a formatted string representation of the numeric value. */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize to JSON.
     *
     * XRP amounts are emitted as a plain decimal string (drop count).
     * IOU amounts produce `{value, currency, issuer}`.
     * MPT amounts produce `{value, mpt_issuance_id}`.
     */
    [[nodiscard]] json::Value getJson(JsonOptions = JsonOptions::Values::None) const override;

    /** Append the wire-format encoding to `s`.
     *
     * Writes the compact 64-bit word plus any trailing currency/issuer
     * bytes (IOU) or 192-bit MPTID (MPT).
     */
    void
    add(Serializer& s) const override;

    /** Returns true if `t` is an `STAmount` with the same asset and value.
     *
     * Comparison is performed on the binary representation, so canonical
     * equivalence is checked, not numeric equality.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Returns true when the amount is zero.
     *
     * A field whose presence is governed by `soeDEFAULT` is omitted from
     * ledger serialisation when `isDefault()` is true.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Extract the value as a lean `XRPAmount`.
     *
     * @throws std::logic_error if this is not a native XRP amount.
     */
    [[nodiscard]] XRPAmount
    xrp() const;

    /** Extract the value as a lean `IOUAmount`.
     *
     * @throws std::logic_error if this is not an IOU amount.
     */
    [[nodiscard]] IOUAmount
    iou() const;

    /** Extract the value as a lean `MPTAmount`.
     *
     * @throws std::logic_error if this is not an MPT amount.
     */
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

inline STAmount::STAmount(IOUAmount const& amount, Issue const& issue)
    : asset_(issue), offset_(amount.exponent()), isNegative_(amount < beast::kZERO)
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
    : asset_(mptIssue), offset_(0), isNegative_(amount < beast::kZERO)
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

/** Reconstruct an offer quality (rate) as a displayable STAmount.
 *
 * Decodes the packed `uint64_t` quality word produced by `getRate()` back
 * into a human-readable IOU-denominated amount (no issuer).
 *
 * @param rate Encoded quality word (exponent in high byte, mantissa in low bits).
 * @return An STAmount suitable for display or JSON output.
 * @note The parameter type should eventually be `Quality` rather than `uint64_t`.
 */
STAmount
amountFromQuality(std::uint64_t rate);

/** Parse an amount from a decimal string for the given asset.
 *
 * Accepts a plain decimal string (possibly with an exponent suffix for IOU)
 * or a drop-count string for XRP. Throws on malformed input.
 *
 * @param asset  Target asset type.
 * @param amount Decimal string representation.
 * @return The parsed STAmount.
 * @throws std::runtime_error on malformed input.
 */
STAmount
amountFromString(Asset const& asset, std::string const& amount);

/** Parse an STAmount from a JSON value, associating it with a named SField.
 *
 * Accepts three formats:
 * - Plain string (XRP drop count).
 * - `{value, currency, issuer}` object (IOU).
 * - `{value, mpt_issuance_id}` object (MPT).
 *
 * Also accepts the legacy slash-delimited string format used in some RPC
 * responses for historical compatibility.
 *
 * @param name SField to associate with the resulting STAmount.
 * @param v    JSON value to parse.
 * @return The parsed STAmount.
 * @throws std::runtime_error if the JSON is malformed or the values are out of range.
 */
STAmount
amountFromJson(SField const& name, json::Value const& v);

/** Non-throwing variant of `amountFromJson`.
 *
 * Parses a JSON value as an STAmount. On success writes to `result` and
 * returns true; on any error leaves `result` unchanged and returns false.
 *
 * @param result    Output parameter filled on success.
 * @param jvSource  JSON value to parse.
 * @return True on success, false on any parse error.
 */
bool
amountFromJsonNoThrow(STAmount& result, json::Value const& jvSource);

/** Identity conversion so generic code can call `toSTAmount()` uniformly.
 *
 * `IOUAmount` and `XRPAmount` provide their own `toSTAmount()` overloads.
 * This overload completes the set so that templates need not special-case
 * `STAmount`.
 *
 * @param a The STAmount to pass through.
 * @return A const reference to `a`.
 */
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
    return *this != beast::kZERO;
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

    auto const [mantissa, exponent] = working.normalizeToRange(kMIN_VALUE, kMAX_VALUE);

    return STAmount{asset, mantissa, exponent, negative};
}

inline void
STAmount::negate()
{
    if (*this != beast::kZERO)
        isNegative_ = !isNegative_;
}

inline void
STAmount::clear()
{
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

/** Returns true if the amount is a legal network value.
 *
 * For non-native amounts this is always true. For XRP amounts, the mantissa
 * must not exceed `STAmount::kMAX_NATIVE_N` (10^17 drops = 100 billion XRP).
 * Amounts that fail this check must not be included in consensus transactions.
 *
 * @param value The amount to test.
 */
inline bool
isLegalNet(STAmount const& value)
{
    return !value.native() || (value.mantissa() <= STAmount::kMAX_NATIVE_N);
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

/** Compare two STAmounts for equality.
 *
 * Two amounts are equal when they have identical asset, mantissa, exponent,
 * and sign. Amounts of different asset types are never equal.
 */
bool
operator==(STAmount const& lhs, STAmount const& rhs);

/** Less-than comparison for STAmount.
 *
 * Defines a total order within the same asset type. Amounts of different
 * asset types compare by asset identity first (implementation-defined stable
 * order) so that STAmount can be used in ordered containers.
 */
bool
operator<(STAmount const& lhs, STAmount const& rhs);

/** Returns `!(lhs == rhs)`. */
inline bool
operator!=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs == rhs);
}

/** Returns `rhs < lhs`. */
inline bool
operator>(STAmount const& lhs, STAmount const& rhs)
{
    return rhs < lhs;
}

/** Returns `!(rhs < lhs)`. */
inline bool
operator<=(STAmount const& lhs, STAmount const& rhs)
{
    return !(rhs < lhs);
}

/** Returns `!(lhs < rhs)`. */
inline bool
operator>=(STAmount const& lhs, STAmount const& rhs)
{
    return !(lhs < rhs);
}

/** Return the arithmetic negation of `value`.
 *
 * A zero amount is returned unchanged (canonical zero has no sign).
 */
STAmount
operator-(STAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

/** Add two same-asset STAmounts.
 *
 * @pre `v1` and `v2` must have the same asset.
 */
STAmount
operator+(STAmount const& v1, STAmount const& v2);

/** Subtract two same-asset STAmounts.
 *
 * @pre `v1` and `v2` must have the same asset.
 */
STAmount
operator-(STAmount const& v1, STAmount const& v2);

/** Divide `v1` by `v2`, expressing the result in `asset`.
 *
 * Designed for cross-currency calculations where the result naturally belongs
 * to a third asset (e.g. quality calculations). Uses the amendment-gated
 * arithmetic path (`getSTNumberSwitchover()`) for precision.
 *
 * @param v1    Dividend.
 * @param v2    Divisor (must be non-zero).
 * @param asset Asset type for the result.
 * @return Quotient expressed as an STAmount with `asset`.
 */
STAmount
divide(STAmount const& v1, STAmount const& v2, Asset const& asset);

/** Multiply `v1` by `v2`, expressing the result in `asset`.
 *
 * @param v1    First factor.
 * @param v2    Second factor.
 * @param asset Asset type for the result.
 * @return Product expressed as an STAmount with `asset`.
 */
STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset);

/** Multiply with legacy fixed-direction rounding.
 *
 * Uses the legacy rounding approach: rounds up when the fractional
 * remainder is ≥ 0.1 of the smallest representable unit.
 * Prefer `mulRoundStrict` for new code that needs accurate rounding.
 *
 * @param v1      First factor.
 * @param v2      Second factor.
 * @param asset   Asset type for the result.
 * @param roundUp True to round up, false to round down.
 * @return Rounded product expressed as an STAmount with `asset`.
 */
STAmount
mulRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

/** Multiply following the thread-local `Number::rounding_mode` precisely.
 *
 * Respects the `NumberRoundModeGuard` rounding mode for accurate remainder
 * tracking, rather than the fixed legacy approximation used by `mulRound`.
 *
 * @param v1      First factor.
 * @param v2      Second factor.
 * @param asset   Asset type for the result.
 * @param roundUp True to round up, false to round down.
 * @return Rounded product expressed as an STAmount with `asset`.
 */
STAmount
mulRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

/** Divide with legacy fixed-direction rounding.
 *
 * Uses the legacy rounding approach. Prefer `divRoundStrict` for new code.
 *
 * @param v1      Dividend.
 * @param v2      Divisor (must be non-zero).
 * @param asset   Asset type for the result.
 * @param roundUp True to round up, false to round down.
 * @return Rounded quotient expressed as an STAmount with `asset`.
 */
STAmount
divRound(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

/** Divide following the thread-local `Number::rounding_mode` precisely.
 *
 * @param v1      Dividend.
 * @param v2      Divisor (must be non-zero).
 * @param asset   Asset type for the result.
 * @param roundUp True to round up, false to round down.
 * @return Rounded quotient expressed as an STAmount with `asset`.
 */
STAmount
divRoundStrict(STAmount const& v1, STAmount const& v2, Asset const& asset, bool roundUp);

/** Encode an offer quality (in/out ratio) as a compact `uint64_t`.
 *
 * The rate represents `offerIn / offerOut`. A **smaller** value is better
 * for the taker (more output per unit input). The encoding packs the
 * base-10 exponent in the high byte and the mantissa in the remaining bits,
 * making the values directly comparable as integers — which is the sort
 * order used for offer-book directories.
 *
 * @param offerOut Amount the offer gives out.
 * @param offerIn  Amount the offer takes in.
 * @return Packed quality word, or 0 if the result underflows.
 * @note The return type should eventually be `Quality`.
 */
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

/** Returns true if `amount` represents native XRP.
 *
 * Convenience wrapper around `STAmount::native()` for use in generic code
 * that checks the asset type before dispatching.
 */
inline bool
isXRP(STAmount const& amount)
{
    return amount.native();
}

/** Pre-flight check: returns true if `amt1 + amt2` is representable.
 *
 * For XRP and MPT amounts this performs 64-bit overflow/underflow bounds
 * tests without executing the addition.
 *
 * For IOU amounts a relative-precision metric is used: both operands are
 * reconstructed after a round-trip through addition and the combined
 * relative error must not exceed 10^-4. This guards against silently
 * losing significant digits when the operands' exponents differ by more
 * than 15 (the mantissa precision limit).
 *
 * @param amt1 First operand.
 * @param amt2 Second operand.
 * @return True if the addition can be performed safely; false if it would
 *     overflow or produce an unacceptably imprecise result.
 */
bool
canAdd(STAmount const& amt1, STAmount const& amt2);

/** Pre-flight check: returns true if `amt1 - amt2` is representable.
 *
 * Equivalent to `canAdd(amt1, -amt2)`. Performs 64-bit underflow/overflow
 * bounds tests for XRP and MPT; uses the relative-precision metric for IOU.
 *
 * @param amt1 Minuend.
 * @param amt2 Subtrahend.
 * @return True if the subtraction can be performed safely.
 */
bool
canSubtract(STAmount const& amt1, STAmount const& amt2);

/** Return the STAmount exponent that would result from converting `number`
 *  to an STAmount for the given asset.
 *
 * "Scale" is the base-10 exponent after STAmount normalization, which
 * differs from `Number::exponent()` because STAmount enforces a narrower
 * mantissa range (`[kMIN_VALUE, kMAX_VALUE]`) and asset-specific rules
 * (integral assets always have exponent 0). This function constructs a
 * temporary STAmount purely to read back the normalized exponent.
 *
 * Used by `roundToAsset` to determine the precision boundary before
 * shedding sub-precision dust via `roundToScale`.
 *
 * @param number The high-precision value to inspect.
 * @param asset  The asset that governs normalization rules.
 * @return The base-10 exponent of the normalized STAmount.
 */
inline int
scale(Number const& number, Asset const& asset)
{
    return STAmount{asset, number}.exponent();
}

}  // namespace xrpl

//------------------------------------------------------------------------------
namespace json {

/** Extract an STAmount from a JSON object by SField name.
 *
 * Specialisation of `json::getOrThrow<T>` for `xrpl::STAmount`. Looks up
 * the field by its JSON key name in `v`, then delegates to
 * `xrpl::amountFromJson` for full parsing (handles XRP string, IOU object,
 * and MPT object formats).
 *
 * @param v     JSON object containing the field.
 * @param field SField whose JSON name is used as the lookup key.
 * @return Parsed STAmount.
 * @throws JsonMissingKeyError if the key is absent in `v`.
 * @throws std::runtime_error if the value cannot be parsed as an STAmount.
 */
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
