/** @file
 *  Holds one pre-built `Json::Value` per supported API version so that a
 *  single ledger event can be delivered to subscribers speaking different API
 *  versions without re-serializing on every send.
 *
 *  The public alias `xrpl::MultiApiJson` binds the template to the live
 *  version range `[kAPI_MINIMUM_SUPPORTED_VERSION, kAPI_MAXIMUM_VALID_VERSION]`.
 */

#pragma once

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>

#include <array>
#include <concepts>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <utility>

namespace xrpl {

namespace detail {

/** Variable template that is `true` only for lvalue-reference-qualified
 *  `std::integral_constant` specializations (both cv-variants).
 *
 *  Used as the building block for the `some_integral_constant` concept, which
 *  disambiguates the compile-time and runtime overloads of `VisitorT::operator()`.
 *
 *  @tparam T The type to test.
 */
template <typename T>
constexpr bool kIS_INTEGRAL_CONSTANT = false;
template <typename I, auto A>
constexpr bool kIS_INTEGRAL_CONSTANT<std::integral_constant<I, A>&> = true;
template <typename I, auto A>
constexpr bool kIS_INTEGRAL_CONSTANT<std::integral_constant<I, A> const&> = true;

/** Concept satisfied only by `std::integral_constant` specializations (lvalue refs).
 *
 *  Used in `requires` clauses on `VisitorT::operator()` to prevent the
 *  runtime-`unsigned` overloads from being selected when a compile-time
 *  constant is passed, avoiding otherwise-ambiguous partial ordering.
 *
 *  @tparam T The type to constrain.
 */
template <typename T>
concept some_integral_constant = detail::kIS_INTEGRAL_CONSTANT<T&>;

/** Holds one `Json::Value` per API version in a fixed-size array, enabling
 *  single-pass event serialization for multi-version subscriber delivery.
 *
 *  When an XRPL server event (e.g., a validated transaction) must be published
 *  to subscribers that may speak different API versions, re-serializing or
 *  branching inside the send path would add latency proportional to subscriber
 *  count. `MultiApiJson` amortizes version-specific transformations to once per
 *  event: callers construct the object from a common base `Json::Value`, apply
 *  per-version mutations via `visit`, and then each subscriber's delivery path
 *  calls `visit(apiVersion, sender)` to pick the pre-built slot cheaply.
 *
 *  The array has `MaxVer + 1 - MinVer` elements; version `v` maps to index
 *  `v - MinVer`. `set` and `isMember` operate across all slots; `visit`
 *  operates on a single slot selected by version.
 *
 *  @note Prefer the `xrpl::MultiApiJson` type alias over instantiating this
 *      template directly. Direct instantiation is intended for tests only; all
 *      production code should use the alias, which is bound to the live version
 *      constants and automatically tracks any future version-range changes.
 *
 *  @tparam MinVer Minimum (inclusive) supported API version.
 *  @tparam MaxVer Maximum (inclusive) supported API version.
 */
template <unsigned MinVer, unsigned MaxVer>
struct MultiApiJson
{
    static_assert(MinVer <= MaxVer);

    /** Returns `true` if `v` falls within `[MinVer, MaxVer]`.
     *
     *  Used by `VisitorT` to guard against out-of-range version accesses.
     *  @param v The API version number to test.
     *  @return `true` iff `v` is a valid slot index.
     */
    static constexpr auto
    valid(unsigned int v) noexcept -> bool
    {
        return v >= MinVer && v <= MaxVer;
    }

    /** Maps an API version number to its zero-based array slot.
     *
     *  Out-of-range values below `MinVer` clamp to 0 rather than underflowing;
     *  the caller is responsible for checking `valid(v)` before trusting the
     *  result. Values above `MaxVer` are not clamped — `valid()` must be used
     *  to guard against those.
     *
     *  @param v The API version number to map.
     *  @return The corresponding index into `val`.
     */
    static constexpr auto
    index(unsigned int v) noexcept -> std::size_t
    {
        return (v < MinVer) ? 0 : static_cast<std::size_t>(v - MinVer);
    }

    /** Number of API version slots stored; equals `MaxVer + 1 - MinVer`. */
    constexpr static std::size_t kSIZE = MaxVer + 1 - MinVer;

    /** The per-version JSON values, indexed by `index(version)`.
     *
     *  Public to allow direct slot access in tests and for `VisitorT` (which
     *  is a friend via the `static constexpr` data member). Production callers
     *  should use `set`, `isMember`, and `visit` rather than indexing directly.
     */
    std::array<json::Value, kSIZE> val = {};

    /** Constructs the object, optionally copy-initializing every slot.
     *
     *  When `init` is the default (null) `Json::Value`, all slots remain
     *  default-initialized (null). When a non-null value is supplied, every
     *  slot is copy-initialized to it. The common pattern in `NetworkOPs.cpp`
     *  is to pass a shared base object and then apply per-version mutations
     *  via `visit`.
     *
     *  @param init Base value to copy into every slot; omit for null slots.
     */
    explicit MultiApiJson(json::Value const& init = {})
    {
        if (init == json::Value{})
            return;  // All elements are already default-initialized
        for (auto& v : val)
            v = init;
    }

    /** Writes a key-value pair into every slot simultaneously.
     *
     *  Use for fields that are identical across all API versions — the majority
     *  of transaction fields. Cheaper than calling `visit` once per version for
     *  shared data. The `requires` clause restricts `v` to types from which
     *  `Json::Value` can be constructed, preventing silent misuse.
     *
     *  @param key  The JSON object key to set.
     *  @param v    The value to assign; must be constructible to `Json::Value`.
     */
    void
    set(char const* key, auto const& v)
        requires std::constructible_from<json::Value, decltype(v)>
    {
        for (auto& a : this->val)
            a[key] = v;
    }

    /** Tri-state result of `isMember`: indicates how many version slots contain a key.
     *
     *  Scoped to `MultiApiJson` rather than a separate class enum deliberately —
     *  the struct is narrow enough to serve as its own scope for this result.
     */
    enum class IsMemberResult : int {
        None = 0, /**< No slot contains the key. */
        Some,     /**< At least one but not all slots contain the key. */
        All       /**< Every slot contains the key. */
    };

    /** Queries how many version slots contain the given JSON key.
     *
     *  Useful for asserting that version-specific mutations were (or were not)
     *  applied before delivery. `NetworkOPs` uses it in assertions to verify
     *  that certain fields are never set on a freshly-constructed object.
     *
     *  @param key  The JSON object key to look up in each slot.
     *  @return `IsMemberResult::None`, `Some`, or `All`.
     */
    [[nodiscard]] IsMemberResult
    isMember(char const* key) const
    {
        int count = 0;
        for (auto& a : this->val)
        {
            if (a.isMember(key))
                count += 1;
        }

        if (count == 0)
            return IsMemberResult::None;
        return count < kSIZE ? IsMemberResult::Some : IsMemberResult::All;
    }

    /** Stateless callable that routes invocations to the correct version slot.
     *
     *  Provides four `operator()` overloads split along two axes:
     *
     *  1. **Compile-time version** (`std::integral_constant<unsigned, V>`):
     *     the version is checked with `static_assert`; the JSON reference and
     *     optional extra arguments are forwarded to `fn` at compile time.
     *
     *  2. **Runtime version** (any type convertible to `unsigned` that is
     *     *not* an `integral_constant`): the version is checked with
     *     `XRPL_ASSERT`; the `some_integral_constant` concept in the `requires`
     *     clause prevents these overloads from being selected when a
     *     compile-time constant is passed, resolving the otherwise-ambiguous
     *     partial ordering.
     *
     *  Each axis is further split by whether extra arguments are forwarded to
     *  `fn` after the `Json::Value` (and possibly the version value). This
     *  matches the calling convention of `forAllApiVersions`/`forApiVersions`,
     *  which pass each version as an `integral_constant` plus any extra args
     *  bound at the call site.
     *
     *  `const`-propagation is automatic: the JSON reference passed to `fn`
     *  mirrors the `const`-ness of the `Json&` parameter.
     *
     *  @note Exposed as `kVISITOR` to allow direct testing; prefer `visit()`
     *      for all production call sites.
     */
    static constexpr struct VisitorT final
    {
        // integral_constant version, extra arguments
        template <typename Json, unsigned int Version, typename... Args, typename Fn>
            requires std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(
            Json& json,
            std::integral_constant<unsigned int, Version> const version,
            Fn fn,
            Args&&... args) const
            -> std::invoke_result_t<
                Fn,
                decltype(json.val[0]),
                std::integral_constant<unsigned int, Version>,
                Args&&...>
        {
            static_assert(valid(Version) && index(Version) >= 0 && index(Version) < kSIZE);
            return std::invoke(fn, json.val[index(Version)], version, std::forward<Args>(args)...);
        }

        // integral_constant version, Json only
        template <typename Json, unsigned int Version, typename Fn>
            requires std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, std::integral_constant<unsigned int, Version> const, Fn fn) const
            -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            static_assert(valid(Version) && index(Version) >= 0 && index(Version) < kSIZE);
            return std::invoke(fn, json.val[index(Version)]);
        }

        // unsigned int version, extra arguments
        template <typename Json, typename Version, typename... Args, typename Fn>
            requires(!some_integral_constant<Version>) && std::convertible_to<Version, unsigned> &&
            std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn, Args&&... args) const
            -> std::invoke_result_t<Fn, decltype(json.val[0]), Version, Args&&...>
        {
            XRPL_ASSERT(
                valid(version) && index(version) >= 0 && index(version) < kSIZE,
                "xrpl::detail::MultiApijson::operator<Args...>() : valid "
                "version");
            return std::invoke(fn, json.val[index(version)], version, std::forward<Args>(args)...);
        }

        // unsigned int version, Json only
        template <typename Json, typename Version, typename Fn>
            requires(!some_integral_constant<Version>) && std::convertible_to<Version, unsigned> &&
            std::same_as<std::remove_cvref_t<Json>, MultiApiJson>
        auto
        operator()(Json& json, Version version, Fn fn) const
            -> std::invoke_result_t<Fn, decltype(json.val[0])>
        {
            XRPL_ASSERT(
                valid(version) && index(version) >= 0 && index(version) < kSIZE,
                "xrpl::detail::MultiApijson::operator() : valid version");
            return std::invoke(fn, json.val[index(version)]);
        }
    } kVISITOR = {};

    /** Returns a closure that dispatches `kVISITOR` for this object (mutable).
     *
     *  The returned callable captures `this` and forwards all arguments to
     *  `kVISITOR`. This form is composable with `forAllApiVersions` and
     *  `forApiVersions`: those utilities iterate the version range at compile
     *  time, passing each version as an `integral_constant`. The closure
     *  satisfies that calling convention exactly, so
     *  `forAllApiVersions(obj.visit(), lambda)` iterates every version with a
     *  single consistent lambda without any per-version conditional logic.
     *
     *  @return A lambda `(auto... args) -> auto` that calls
     *      `kVISITOR(*this, args...)`.
     */
    auto
    visit()
    {
        return [self = this](auto... args)
            requires requires {
                kVISITOR(std::declval<MultiApiJson&>(), std::declval<decltype(args)>()...);
            }
        { return kVISITOR(*self, std::forward<decltype(args)>(args)...); };
    }

    /** Returns a closure that dispatches `kVISITOR` for this object (const).
     *
     *  Identical to the mutable overload but captures `this` as `const`,
     *  propagating const-ness through to the `Json::Value` reference passed to
     *  the callable. Used when the caller only needs to read the pre-built JSON
     *  (e.g., subscriber delivery in `BookListeners::publish`).
     *
     *  @return A lambda `(auto... args) -> auto` that calls
     *      `kVISITOR(*this, args...)` on the const object.
     */
    [[nodiscard]] auto
    visit() const
    {
        return [self = this](auto... args)
            requires requires {
                kVISITOR(std::declval<MultiApiJson const&>(), std::declval<decltype(args)>()...);
            }
        { return kVISITOR(*self, std::forward<decltype(args)>(args)...); };
    }

    /** Directly invokes `kVISITOR` for a single version (mutable).
     *
     *  Equivalent to `visit()(args...)` but avoids the closure allocation.
     *  Typical usage:
     *  ```cpp
     *  jvObj.visit(RPC::kAPI_VERSION<1>, [](Json::Value& jv) {
     *      jv["ledger_index"] = std::to_string(jv["ledger_index"].asInt());
     *  });
     *  ```
     *
     *  @param args  Version (compile-time or runtime) followed by a callable
     *      and any extra arguments accepted by `kVISITOR`.
     *  @return The return value of the callable.
     */
    template <typename... Args>
    auto
    visit(Args... args) -> std::invoke_result_t<VisitorT, MultiApiJson&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { kVISITOR(*this, std::forward<decltype(args)>(args)...); }
    {
        return kVISITOR(*this, std::forward<decltype(args)>(args)...);
    }

    /** Directly invokes `kVISITOR` for a single version (const).
     *
     *  Const counterpart of the mutable `visit(args...)` overload. Used when
     *  the JSON slot must not be mutated — for example in the subscriber
     *  delivery path where each subscriber picks its pre-built slot:
     *  ```cpp
     *  jvObj.visit(subscriber->getApiVersion(),
     *              [&](Json::Value const& jv) { subscriber->send(jv, true); });
     *  ```
     *
     *  @param args  Version (compile-time or runtime) followed by a callable
     *      and any extra arguments accepted by `kVISITOR`.
     *  @return The return value of the callable.
     */
    template <typename... Args>
    [[nodiscard]] auto
    visit(Args... args) const -> std::invoke_result_t<VisitorT, MultiApiJson const&, Args...>
        requires(sizeof...(args) > 0) &&
        requires { kVISITOR(*this, std::forward<decltype(args)>(args)...); }
    {
        return kVISITOR(*this, std::forward<decltype(args)>(args)...);
    }
};

}  // namespace detail

/** Holds one pre-built `Json::Value` per currently supported API version.
 *
 *  Bound to `[kAPI_MINIMUM_SUPPORTED_VERSION, kAPI_MAXIMUM_VALID_VERSION]`
 *  (currently versions 1–3), so the concrete type stores exactly three
 *  `Json::Value` objects. Changing those constants automatically resizes
 *  every `MultiApiJson` instance in the server.
 *
 *  @see detail::MultiApiJson for the full behavioral contract.
 */
using MultiApiJson =
    detail::MultiApiJson<RPC::kAPI_MINIMUM_SUPPORTED_VERSION, RPC::kAPI_MAXIMUM_VALID_VERSION>;

}  // namespace xrpl
