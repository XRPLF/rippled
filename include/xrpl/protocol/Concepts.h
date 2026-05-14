/** @file
 *  Compile-time type vocabulary for the XRPL protocol layer.
 *
 *  Centralises all C++20 concept definitions that constrain the three payment
 *  asset families (XRP, IOU, and MPT) and provides the `detail::CombineVisitors`
 *  utility used by `Asset::visit()` and `PathAsset::visit()`. Keeping every
 *  constraint in one place means that adding a new asset family requires
 *  updates in a single file; the compiler propagates errors to every call site.
 */
#pragma once

#include <xrpl/protocol/UintTypes.h>

#include <type_traits>

namespace xrpl {

class STAmount;
class Asset;
class Issue;
class MPTIssue;
class IOUAmount;
class XRPAmount;
class MPTAmount;

/** Constrains the three numeric types used as individual payment-step quantities.
 *
 *  `EitherAmount` (the type-erased amount carrier used by the path-finding
 *  engine) restricts its constructor, `holds<T>()`, and `get<T>()` with this
 *  concept, so the compiler rejects any attempt to store or query an amount
 *  type outside the sanctioned set at instantiation time.
 *
 *  @tparam A One of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
 */
template <typename A>
concept StepAmount =
    std::is_same_v<A, XRPAmount> || std::is_same_v<A, IOUAmount> || std::is_same_v<A, MPTAmount>;

/** Constrains template parameters to the two issue types held by `Asset`.
 *
 *  Gates `Asset::get<T>()` and `Asset::holds<T>()` to exactly `Issue` and
 *  `MPTIssue` — the two alternatives of `Asset`'s internal
 *  `std::variant<Issue, MPTIssue>`. Also used by the `kIS_ISSUE_V` and
 *  `kIS_MPTISSUE_V` boolean constants in `Asset.h` that drive `if constexpr`
 *  branches in comparison operators.
 *
 *  @tparam TIss `Issue` or `MPTIssue`.
 */
template <typename TIss>
concept ValidIssueType = std::is_same_v<TIss, Issue> || std::is_same_v<TIss, MPTIssue>;

/** Constrains template parameters to any type convertible to a known asset representation.
 *
 *  Broader than `ValidIssueType`: uses `is_convertible_v` rather than
 *  `is_same_v`, so it accepts any type with an implicit conversion path to
 *  `Asset`, `Issue`, `MPTIssue`, or `MPTID`. This enables generic code that
 *  accepts any "asset-like" value without requiring callers to normalise to a
 *  canonical form first.
 *
 *  @tparam A Any type implicitly convertible to `Asset`, `Issue`, `MPTIssue`, or `MPTID`.
 */
template <typename A>
concept AssetType = std::is_convertible_v<A, Asset> || std::is_convertible_v<A, Issue> ||
    std::is_convertible_v<A, MPTIssue> || std::is_convertible_v<A, MPTID>;

/** Constrains template parameters to the two token-identity types used in payment paths.
 *
 *  `PathAsset` carries only the currency/token specifier inside a payment path
 *  element — it explicitly does not carry issuer information. `Currency` covers
 *  both XRP (the zero currency) and IOU tokens; `MPTID` covers MPT issuances.
 *  This concept gates `PathAsset::get<T>()`, `PathAsset::holds<T>()`, and the
 *  `kIS_CURRENCY_V`/`kIS_MPTID_V` helper constants in `PathAsset.h`.
 *
 *  @tparam T `Currency` or `MPTID`.
 */
template <typename T>
concept ValidPathAsset = (std::is_same_v<T, Currency> || std::is_same_v<T, MPTID>);

/** Constrains a pair of step-amount types to a legal DEX trading pair.
 *
 *  Both sides must independently be one of `XRPAmount`, `IOUAmount`, or
 *  `MPTAmount`, but the XRP/XRP combination is structurally illegal on the
 *  XRPL order book — an offer cannot have both `TakerPays` and `TakerGets`
 *  denominated in XRP. `OfferStream::shouldRmSmallIncreasedQOffer()` uses this
 *  concept to encode that invariant at the type system level rather than as a
 *  runtime assertion.
 *
 *  @tparam TTakerPays The amount type for what the taker pays; must satisfy `StepAmount`.
 *  @tparam TTakerGets The amount type for what the taker receives; must satisfy `StepAmount`.
 *  @note The constraint is equivalent to: both sides are valid step-amount types
 *      AND NOT (TTakerPays == XRPAmount AND TTakerGets == XRPAmount).
 */
template <class TTakerPays, class TTakerGets>
concept ValidTaker =
    ((std::is_same_v<TTakerPays, IOUAmount> || std::is_same_v<TTakerPays, XRPAmount> ||
      std::is_same_v<TTakerPays, MPTAmount>) &&
     (std::is_same_v<TTakerGets, IOUAmount> || std::is_same_v<TTakerGets, XRPAmount> ||
      std::is_same_v<TTakerGets, MPTAmount>) &&
     (!std::is_same_v<TTakerPays, XRPAmount> || !std::is_same_v<TTakerGets, XRPAmount>));

namespace detail {

/** Combines multiple callable objects (lambdas) into a single overload set for `std::visit`.
 *
 *  Implements the classical *overloaded* pattern: by inheriting from every
 *  lambda type and pulling each `operator()` into the derived scope, this
 *  struct becomes a single callable that overload-resolution can dispatch
 *  correctly based on the active variant alternative at runtime.
 *
 *  Prefer constructing instances via `makeCombineVisitors()` rather than
 *  direct construction; the factory applies `std::decay_t` and uses function
 *  template argument deduction, which is more portable than CTAD for variadic
 *  class templates.
 *
 *  @tparam Ts Lambda (or other callable) types to merge into one overload set.
 *  @see makeCombineVisitors
 */
template <typename... Ts>
struct CombineVisitors : Ts...
{
    using Ts::operator()...;

    /** Initialises each base-class lambda by perfect-forwarding its argument. */
    constexpr CombineVisitors(Ts&&... ts) : Ts(std::forward<Ts>(ts))...
    {
    }
};

/** Creates a `CombineVisitors` from a pack of callables.
 *
 *  Preferred over a CTAD deduction guide because function template argument
 *  deduction handles parameter packs more robustly than class-template
 *  argument deduction (CTAD) across compilers. `std::decay_t` strips
 *  references and cv-qualifiers from lambda types before they become base
 *  classes, ensuring the inherited `operator()` calls have the correct value
 *  categories.
 *
 *  @tparam Ts Callable types; typically lambdas.
 *  @param ts Callables to combine.
 *  @return A `CombineVisitors<std::decay_t<Ts>...>` holding all overloads.
 */
template <typename... Ts>
constexpr CombineVisitors<std::decay_t<Ts>...>
makeCombineVisitors(Ts&&... ts)
{
    return CombineVisitors<std::decay_t<Ts>...>{std::forward<Ts>(ts)...};
}

/** Visits a variant with a set of per-alternative callables.
 *
 *  Combines `visitors...` into a single overload set via `makeCombineVisitors`
 *  and delegates to `std::visit`. This is the reusable core called by
 *  `Asset::visit()` and `PathAsset::visit()`; callers should go through those
 *  member functions rather than invoking this directly.
 *
 *  @tparam Variant A `std::variant` specialisation.
 *  @tparam Visitors Callable types, one per variant alternative.
 *  @param v The variant to dispatch on.
 *  @param visitors Callables covering each alternative of `v`.
 *  @return The return value of the selected visitor.
 */
template <typename Variant, typename... Visitors>
constexpr auto
visit(Variant&& v, Visitors&&... visitors) -> decltype(auto)
{
    auto visitorSet = makeCombineVisitors(std::forward<Visitors>(visitors)...);
    return std::visit(visitorSet, std::forward<Variant>(v));
}

}  // namespace detail

}  // namespace xrpl
