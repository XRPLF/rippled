/** @file
 *  Defines `PathAsset`, the token identifier for a single hop in an XRPL
 *  payment path, and its associated free functions.
 *
 *  `PathAsset` holds `std::variant<Currency, MPTID>` — just the *which
 *  currency or MPT* component of a path element, without the issuer.
 *  This is narrower than `Asset` (`std::variant<Issue, MPTIssue>`) because
 *  `STPathElement` records the issuer in a separate field.
 */
#pragma once

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Concepts.h>

namespace xrpl {

/** Token identifier for a single hop within an XRPL payment path.
 *
 *  Holds `std::variant<Currency, MPTID>` — the *which currency/MPT* component
 *  of a path element, without the issuer. Issuers are stored separately in
 *  `STPathElement::mIssuerID` because payment-path serialization records them
 *  as independent fields; folding them into `PathAsset` would duplicate data
 *  and complicate encoding.
 *
 *  This is intentionally narrower than `Asset`, which pairs a currency or MPTID
 *  with its issuer. `PathAsset` carries only the identifier half. Use
 *  `PathAsset(Asset const&)` to project an `Asset` down to a `PathAsset`.
 *
 *  @see Asset, STPathElement, ValidPathAsset
 */
class PathAsset
{
private:
    std::variant<Currency, MPTID> easset_;

public:
    PathAsset() = default;

    /** Construct a PathAsset by projecting an Asset, discarding the issuer.
     *
     *  For an `Issue`-bearing `Asset`, retains the `Currency`. For an
     *  `MPTIssue`-bearing `Asset`, retains the `MPTID`. This enables direct
     *  comparison between the richer `Asset` type and the path-element
     *  representation without manually extracting the identifier.
     *
     *  @param asset The full asset to project.
     */
    PathAsset(Asset const& asset);

    /** Construct a PathAsset representing an XRP or IOU currency. */
    PathAsset(Currency const& currency) : easset_(currency)
    {
    }

    /** Construct a PathAsset representing an MPT issuance. */
    PathAsset(MPTID const& mpt) : easset_(mpt)
    {
    }

    /** Return whether the active alternative is exactly `T`.
     *
     *  @tparam T `Currency` or `MPTID` (enforced by `ValidPathAsset`).
     *  @return `true` if the held alternative is `T`, `false` otherwise.
     */
    template <ValidPathAsset T>
    [[nodiscard]] constexpr bool
    holds() const;

    /** Return whether this path asset represents native XRP.
     *
     *  A `Currency` alternative delegates to `xrpl::isXRP(currency)`. An
     *  `MPTID` alternative always returns `false` — MPT can never be native.
     *
     *  @return `true` if the held currency is the XRP zero-currency sentinel.
     */
    [[nodiscard]] constexpr bool
    isXRP() const;

    /** Return a const reference to the held value of type `T`.
     *
     *  @tparam T `Currency` or `MPTID` (enforced by `ValidPathAsset`).
     *  @return A reference to the active alternative.
     *  @throws std::runtime_error if the active alternative is not `T`. Call
     *      `holds<T>()` or dispatch through `visit()` to avoid this.
     */
    template <ValidPathAsset T>
    T const&
    get() const;

    /** Return a const reference to the underlying variant.
     *
     *  Provides direct access to `std::variant<Currency, MPTID>` for callers
     *  that need to pass it to `std::visit` or store it without going through
     *  the member `visit()` wrapper.
     *
     *  @return The internal variant holding `Currency` or `MPTID`.
     */
    [[nodiscard]] constexpr std::variant<Currency, MPTID> const&
    value() const;

    /** Visit the active alternative with a set of per-type callables.
     *
     *  Combines `visitors...` into a single overload set via
     *  `detail::CombineVisitors` and forwards to `std::visit`. Both
     *  alternatives (`Currency` and `MPTID`) must be covered.
     *
     *  @tparam Visitors Callable types, one per alternative.
     *  @param visitors Callables to dispatch to; typically lambdas.
     *  @return The return value of the selected visitor.
     */
    template <typename... Visitors>
    constexpr auto
    visit(Visitors&&... visitors) const -> decltype(auto)
    {
        return detail::visit(easset_, std::forward<Visitors>(visitors)...);
    }

    friend constexpr bool
    operator==(PathAsset const& lhs, PathAsset const& rhs);
};

/** True when `PA` is `Currency`, false when `PA` is `MPTID`.
 *
 *  Compile-time predicate for `if constexpr` branches in generic code that
 *  must distinguish XRP/IOU paths from MPT paths.
 *
 *  @tparam PA `Currency` or `MPTID` (enforced by `ValidPathAsset`).
 */
template <ValidPathAsset PA>
constexpr bool kIS_CURRENCY_V = std::is_same_v<PA, Currency>;

/** True when `PA` is `MPTID`, false when `PA` is `Currency`.
 *
 *  Compile-time predicate for `if constexpr` branches in generic code that
 *  must distinguish MPT paths from XRP/IOU paths.
 *
 *  @tparam PA `Currency` or `MPTID` (enforced by `ValidPathAsset`).
 */
template <ValidPathAsset PA>
constexpr bool kIS_MPTID_V = std::is_same_v<PA, MPTID>;

inline PathAsset::PathAsset(Asset const& asset)
{
    asset.visit(
        [&](Issue const& issue) { easset_ = issue.currency; },
        [&](MPTIssue const& issue) { easset_ = issue.getMptID(); });
}

template <ValidPathAsset T>
constexpr bool
PathAsset::holds() const
{
    return std::holds_alternative<T>(easset_);
}

template <ValidPathAsset T>
[[nodiscard]] [[nodiscard]] T const&
PathAsset::get() const
{
    if (!holds<T>())
        Throw<std::runtime_error>("PathAsset doesn't hold requested asset.");
    return std::get<T>(easset_);
}

constexpr std::variant<Currency, MPTID> const&
PathAsset::value() const
{
    return easset_;
}

constexpr bool
PathAsset::isXRP() const
{
    return visit(
        [&](Currency const& currency) { return xrpl::isXRP(currency); },
        [](MPTID const&) { return false; });
}

/** Compare two PathAssets for equality.
 *
 *  Two `PathAsset` values are equal only when both hold the same alternative
 *  type *and* the contained values are equal. A `Currency` and an `MPTID`
 *  are never equal even if their raw bytes coincide, preventing cross-type
 *  false positives.
 *
 *  @param lhs Left-hand operand.
 *  @param rhs Right-hand operand.
 *  @return `true` if both hold the same type and equal value, `false` otherwise.
 */
constexpr bool
operator==(PathAsset const& lhs, PathAsset const& rhs)
{
    return std::visit(
        []<ValidPathAsset TLhs, ValidPathAsset TRhs>(TLhs const& lhs, TRhs const& rhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
            {
                return lhs == rhs;
            }
            else
            {
                return false;
            }
        },
        lhs.value(),
        rhs.value());
}

/** Append a PathAsset's value to a hash state.
 *
 *  Dispatches to the appropriate `hash_append` overload for the active
 *  alternative (`Currency` or `MPTID`), enabling `PathAsset` to be used as
 *  a key in hash-based containers built on the `beast::uhash` infrastructure.
 *
 *  @tparam Hasher A type satisfying the `beast::hash_append` Hasher concept.
 *  @param h The hash accumulator to append to.
 *  @param pathAsset The path asset whose value is appended.
 */
template <typename Hasher>
void
hash_append(Hasher& h, PathAsset const& pathAsset)
{
    std::visit([&]<ValidPathAsset T>(T const& e) { hash_append(h, e); }, pathAsset.value());
}

/** Return whether a PathAsset represents native XRP.
 *
 *  Free-function wrapper for `PathAsset::isXRP()`, provided for symmetry
 *  with the `isXRP()` overloads for `Currency`, `Asset`, and `STAmount`.
 *
 *  @param asset The path asset to test.
 *  @return `true` if `asset` holds the XRP zero-currency sentinel.
 */
inline bool
isXRP(PathAsset const& asset)
{
    return asset.isXRP();
}

/** Produce a human-readable string identifying a PathAsset.
 *
 *  Dispatches to `to_string(Currency const&)` or `to_string(MPTID const&)`
 *  depending on the active alternative. For a `Currency` this yields the ISO
 *  4217 ticker or `"XRP"`; for an `MPTID` it yields the base-58 encoded token
 *  identifier.
 *
 *  @param asset The path asset to stringify.
 *  @return A descriptive string identifying the currency or MPT issuance.
 */
std::string
to_string(PathAsset const& asset);

/** Stream-insert a human-readable description of a PathAsset.
 *
 *  Equivalent to `os << to_string(x)`. Intended for logging and diagnostics.
 *
 *  @param os The output stream to write to.
 *  @param x The path asset to write.
 *  @return `os`, for chaining.
 */
std::ostream&
operator<<(std::ostream& os, PathAsset const& x);

}  // namespace xrpl
