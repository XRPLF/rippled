#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

namespace xrpl {

/** Serialized representation of a fungible asset identifier (XRP, IOU, or MPT).
 *
 *  `STIssue` is the canonical `STBase` subtype for embedding an `Asset` inside
 *  a ledger object or transaction field. It bridges the polymorphic serialization
 *  framework and the `Asset` variant that unifies all three asset species.
 *
 *  The wire format is type-multiplexed without a separate tag byte: XRP is
 *  a single 160-bit all-zeros currency sentinel; IOU is a 160-bit currency
 *  followed by a 160-bit issuer AccountID; MPT is a 160-bit issuer AccountID
 *  followed by the 160-bit `noAccount()` sentinel and a 32-bit sequence.
 *  The `noAccount()` sentinel is the discriminator between IOU and MPT — it is
 *  otherwise an illegal issuer address and will never appear in real IOU data.
 *
 *  The class is declared `final`; the `STBase` hierarchy is complete without
 *  further inheritance. `CountedObject<STIssue>` instruments construction and
 *  destruction for runtime diagnostics.
 *
 *  @see Asset, Issue, MPTIssue
 */
class STIssue final : public STBase, CountedObject<STIssue>
{
private:
    Asset asset_{xrpIssue()};

public:
    using value_type = Asset;

    STIssue() = default;
    STIssue(STIssue const& rhs) = default;

    /** Deserialize an STIssue from a byte stream, detecting XRP, IOU, or MPT.
     *
     *  Reads a 160-bit slot. If all-zeros (XRP currency sentinel), the asset is
     *  XRP and deserialization is complete. Otherwise reads a second 160-bit slot:
     *  if it equals `noAccount()`, the asset is an MPT — a 32-bit sequence number
     *  follows and the three values are assembled into an `MPTID`. Any other
     *  second slot forms the `(currency, account)` pair of an IOU `Issue`.
     *
     *  @param sit  Forward cursor over the serialized byte buffer; advanced in place.
     *  @param name The SField identifying this field within its parent STObject.
     *  @throws std::runtime_error if an IOU's currency/account native-flag
     *      combination is invalid (e.g., XRP currency paired with a non-XRP account).
     */
    explicit STIssue(SerialIter& sit, SField const& name);

    /** Construct an STIssue tagged to a specific field and holding the given asset.
     *
     *  Accepts any type satisfying the `AssetType` concept (`Issue`, `MPTIssue`,
     *  `MPTID`, or `Asset`). For `Issue`-typed assets, the currency/account
     *  native-flag combination is validated via `isConsistent()`; MPT issuances
     *  are always considered consistent.
     *
     *  @tparam A    An `AssetType` — `Issue`, `MPTIssue`, `MPTID`, or `Asset`.
     *  @param name  The SField that names this field within a parent STObject.
     *  @param issue The asset to wrap.
     *  @throws std::runtime_error if the asset is an `Issue` and its currency
     *      and account native flags are inconsistent.
     */
    template <AssetType A>
    explicit STIssue(SField const& name, A const& issue);

    /** Construct an XRP STIssue tagged to a specific field.
     *
     *  Convenience constructor producing the default (XRP) asset bound to
     *  the given field name. Equivalent to `STIssue(name, xrpIssue())`.
     *
     *  @param name The SField identifying this field within its parent STObject.
     */
    explicit STIssue(SField const& name);

    STIssue&
    operator=(STIssue const& rhs) = default;

    /** Return the held asset as the concrete type `TIss`.
     *
     *  @tparam TIss The requested type — `Issue` or `MPTIssue`.
     *  @return A const reference to the underlying `TIss` value.
     *  @throws std::runtime_error if the variant does not hold `TIss`.
     */
    template <ValidIssueType TIss>
    TIss const&
    get() const;

    /** Return whether the held asset is of type `TIss`.
     *
     *  @tparam TIss The type to query — `Issue` or `MPTIssue`.
     *  @return `true` if the underlying `Asset` variant holds `TIss`.
     */
    template <ValidIssueType TIss>
    [[nodiscard]] bool
    holds() const;

    /** Return the underlying `Asset` without copying or throwing.
     *
     *  @return A const reference to the wrapped `Asset`.
     */
    [[nodiscard]] value_type const&
    value() const noexcept;

    /** Replace the held asset, re-running the consistency check for IOU issues.
     *
     *  If the current asset is an `Issue`, `isConsistent()` is applied to the
     *  incoming value before assignment. MPT assets always pass through.
     *
     *  @param issue The new asset to store.
     *  @throws std::runtime_error if `issue` is an `Issue` with mismatched
     *      native-flag and account.
     */
    void
    setIssue(Asset const& issue);

    /** @return The serialized type identifier `STI_ISSUE` for generic field dispatch. */
    [[nodiscard]] SerializedTypeID
    getSType() const override;

    /** @return A human-readable string for the asset (e.g., `"XRP"`,
     *      `"USD/r..."`, or the raw hex of an MPTID).
     */
    [[nodiscard]] std::string
    getText() const override;

    /** Serialize the asset to a JSON value suitable for RPC responses.
     *
     *  @return A `json::Value` representing the asset, formatted per asset type.
     */
    [[nodiscard]] json::Value getJson(JsonOptions) const override;

    /** Append the binary encoding of this asset to `s`.
     *
     *  XRP: 160-bit zero currency sentinel only.
     *  IOU: 160-bit currency + 160-bit issuer AccountID.
     *  MPT: 160-bit issuer AccountID + 160-bit `noAccount()` sentinel + 32-bit sequence.
     *
     *  @param s The Serializer to append bytes to.
     */
    void
    add(Serializer& s) const override;

    /** Return `true` if `t` is an STIssue holding an equivalent asset.
     *
     *  @param t The STBase to compare; downcast to STIssue internally.
     *  @return `true` if `t` is an STIssue whose asset compares equal to this one.
     */
    [[nodiscard]] bool
    isEquivalent(STBase const& t) const override;

    /** Return `true` if this field holds the default asset (XRP).
     *
     *  A field absent from a ledger object is implicitly XRP, so `xrpIssue()`
     *  is the canonical default. MPT issuances are never considered default.
     *
     *  @return `true` iff the held asset is `xrpIssue()`.
     */
    [[nodiscard]] bool
    isDefault() const override;

    /** Compare two STIssue objects for equality. Delegates to `Asset::operator==`. */
    friend constexpr bool
    operator==(STIssue const& lhs, STIssue const& rhs);

    /** Three-way comparison of two STIssue objects. Delegates to `Asset::operator<=>`.
     *
     *  @note Across asset types, `MPTIssue` sorts before `Issue` (variant index order).
     */
    friend constexpr std::weak_ordering
    operator<=>(STIssue const& lhs, STIssue const& rhs);

    /** Compare an STIssue directly to a raw Asset for equality.
     *
     *  Avoids the need to unwrap the STIssue when comparing against an `Asset`
     *  value elsewhere in the engine.
     */
    friend constexpr bool
    operator==(STIssue const& lhs, Asset const& rhs);

    /** Three-way comparison of an STIssue against a raw Asset.
     *
     *  @note Across asset types, `MPTIssue` sorts before `Issue` (variant index order).
     */
    friend constexpr std::weak_ordering
    operator<=>(STIssue const& lhs, Asset const& rhs);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <AssetType A>
STIssue::STIssue(SField const& name, A const& asset) : STBase{name}, asset_{asset}
{
    if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
        Throw<std::runtime_error>("Invalid asset: currency and account native mismatch");
}

/** Construct an STIssue by parsing a JSON asset representation.
 *
 *  Delegates to `assetFromJson()` to resolve the JSON into the appropriate
 *  `Asset` variant (`xrpIssue()`, an IOU `Issue`, or an `MPTIssue`), then
 *  wraps it in an STIssue bound to `name`. This is the canonical entry point
 *  when deserializing an issue field from an API request.
 *
 *  @param name The SField to attach to the resulting STIssue.
 *  @param v    A JSON value encoding an asset (XRP object, IOU object, or MPT object).
 *  @return An STIssue bound to `name` holding the parsed asset.
 */
STIssue
issueFromJson(SField const& name, json::Value const& v);

template <ValidIssueType TIss>
bool
STIssue::holds() const
{
    return asset_.holds<TIss>();
}

template <ValidIssueType TIss>
TIss const&
STIssue::get() const
{
    if (!holds<TIss>(asset_))
        Throw<std::runtime_error>("Asset doesn't hold the requested issue");
    return std::get<TIss>(asset_);
}

inline STIssue::value_type const&
STIssue::value() const noexcept
{
    return asset_;
}

inline void
STIssue::setIssue(Asset const& asset)
{
    if (holds<Issue>() && !isConsistent(asset_.get<Issue>()))
        Throw<std::runtime_error>("Invalid asset: currency and account native mismatch");

    asset_ = asset;
}

constexpr bool
operator==(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.asset_ == rhs.asset_;
}

constexpr std::weak_ordering
operator<=>(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.asset_ <=> rhs.asset_;
}

constexpr bool
operator==(STIssue const& lhs, Asset const& rhs)
{
    return lhs.asset_ == rhs;
}

constexpr std::weak_ordering
operator<=>(STIssue const& lhs, Asset const& rhs)
{
    return lhs.asset_ <=> rhs;
}

}  // namespace xrpl
