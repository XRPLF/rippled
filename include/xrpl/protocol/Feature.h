#pragma once

#include <xrpl/basics/base_uint.h>

#include <boost/container/flat_map.hpp>

#include <bitset>
#include <map>
#include <optional>
#include <string>

/**
 * @page Feature How to add new features
 *
 * Steps required to add new features to the code:
 *
 * 1) Add the appropriate XRPL_FEATURE or XRPL_FIX macro definition for the
 *    feature to features.macro with the feature's name, `Supported::no`, and
 *    `VoteBehavior::DefaultNo`.
 *
 * 2) Use the generated variable name as the parameter to `view.rules.enabled()`
 *    to control flow into new code that this feature limits. (featureName or
 *    fixName)
 *
 * 3) If the feature development is COMPLETE, and the feature is ready to be
 *    SUPPORTED, change the macro parameter in features.macro to Supported::yes.
 *
 * 4) In general, any newly supported amendments (`Supported::yes`) should have
 *    a `VoteBehavior::DefaultNo` indefinitely so that external governance can
 *    make the decision on when to activate it. High priority bug fixes can be
 *    an exception to this rule. In such cases, ensure the fix has been
 *    clearly communicated to the community using appropriate channels,
 *    then change the macro parameter in features.macro to
 *    `VoteBehavior::DefaultYes`. The communication process is beyond
 *    the scope of these instructions.

 * 5) If a supported feature (`Supported::yes`) was _ever_ in a released
 *     version, it can never be changed back to `Supported::no`, because
 *     it _may_ still become enabled at any time. This would cause newer
 *     versions of `xrpld` to become amendment blocked.
 *     Instead, to prevent newer versions from voting on the feature, use
 *     `VoteBehavior::Obsolete`. Obsolete features can not be voted for
 *     by any versions of `xrpld` built with that setting, but will still
 *     work correctly if they get enabled. If a feature remains obsolete
 *     for long enough that _all_ clients that could vote for it are
 *     amendment blocked, the feature can be removed from the code
 *     as if it was unsupported.
 *
 *
 * When a feature has been enabled for several years, the conditional code
 * may be removed, and the feature "retired". To retire a feature:
 *
 * 1) MOVE the macro definition in features.macro to the "retired features"
 *    section at the end of the file, and change the macro to XRPL_RETIRE.
 *
 * The feature must remain registered and supported indefinitely because it
 * may exist in the Amendments object on ledger. There is no need to vote
 * for it because there's nothing to vote for. If the feature definition is
 * removed completely from the code, any instances running that code will get
 * amendment blocked. Removing the feature from the ledger is beyond the scope
 * of these instructions.
 *
 */

namespace xrpl {

/** Maximum allowed length of a feature name in characters, excluding the null terminator. */
static constexpr std::size_t kMAX_FEATURE_NAME_SIZE = 63;

/** Feature-name length (in bytes, excluding the null terminator) reserved for
 *  raw `uint256` hash selectors.
 *
 *  A `uint256` is 32 bytes. Allowing a human-readable name that is exactly 32
 *  characters long would create an ambiguous namespace collision with compact
 *  feature selectors used in WASM or other interop contexts. Names of this
 *  exact length are rejected at compile time by `validFeatureNameSize()`.
 */
static constexpr std::size_t kRESERVED_FEATURE_NAME_SIZE = 32;

/** Validate a feature name's length at compile time.
 *
 *  Returns `true` iff the name produced by `fn` satisfies both:
 *  - length ≤ `kMAX_FEATURE_NAME_SIZE` (63 characters), and
 *  - length ≠ `kRESERVED_FEATURE_NAME_SIZE` (32 characters).
 *
 *  The parameter `fn` must be a `constexpr` lambda returning `const char*`,
 *  which makes the string literal available for compile-time evaluation.
 *  See https://accu.org/journals/overload/30/172/wu/ for the idiom.
 *
 *  @param fn A `consteval`-compatible nullary callable returning `const char*`.
 *  @return `true` if the name length is valid, `false` otherwise.
 *  @note `std::strlen` is not `constexpr`; a manual loop computes the length.
 */
consteval auto
validFeatureNameSize(auto fn) -> bool
{
    constexpr char const* kN = fn();
    constexpr std::size_t kLEN = [](auto n) {
        std::size_t ret = 0;
        for (auto ptr = n; *ptr != '\0'; ret++, ++ptr)
            ;
        return ret;
    }(kN);
    return kLEN != kRESERVED_FEATURE_NAME_SIZE &&  //
        kLEN <= kMAX_FEATURE_NAME_SIZE;
}

/** Validate that a feature name contains only printable ASCII characters.
 *
 *  Returns `true` iff every character in the name produced by `fn` has value
 *  ≥ 0x20 and the high bit (0x80) clear. Rejects:
 *  - Control characters (below 0x20, e.g. `\t`, `\n`).
 *  - Non-ASCII bytes (high bit set), which appear in UTF-8 multibyte sequences
 *    and Unicode identifiers that C++ technically permits but that are visually
 *    confusable with ASCII characters (e.g. Greek Capital Alpha vs. `'A'`).
 *
 *  @param fn A `consteval`-compatible nullary callable returning `const char*`.
 *  @return `true` if all characters are printable ASCII, `false` otherwise.
 */
consteval auto
validFeatureName(auto fn) -> bool
{
    constexpr char const* kN = fn();
    for (auto ptr = kN; *ptr != '\0'; ++ptr)
    {
        if (*ptr & 0x80 || *ptr < 0x20)
            return false;
    }
    return true;
}

/** Controls whether this server votes for an amendment it supports.
 *
 *  Governs the server's default stance during the amendment voting round.
 *  The winning value for most amendments progresses from `DefaultNo`
 *  (governance decides timing) to optionally `DefaultYes` (critical fixes),
 *  and then to `Obsolete` if the amendment is abandoned without activating.
 */
enum class VoteBehavior : int {
    Obsolete   = -1, /**< Amendment supported but no longer voted for; retained
                      *   for ledger compatibility only. */
    DefaultNo  = 0,  /**< Server supports but abstains by default; external
                      *   governance decides when to activate. */
    DefaultYes = 1,  /**< Server actively votes for activation; reserved for
                      *   critical bug fixes after off-chain consensus. */
};

/** Records how well this build understands a given amendment.
 *
 *  Used by `allAmendments()` to report the full picture of what the server
 *  knows about each amendment, including retired ones whose conditional code
 *  has been removed.
 */
enum class AmendmentSupport : int {
    Retired     = -1, /**< Conditional code removed; amendment remains registered
                       *   so nodes stay amendment-compatible with old ledgers. */
    Supported   = 0,  /**< Amendment is recognized and the server may vote for it. */
    Unsupported = 1,  /**< Amendment is known but this build does not implement it. */
};

/** Return every amendment this build has ever known about, including retired ones.
 *
 *  Maps each amendment's string name to its `AmendmentSupport` status:
 *  `Supported` (recognized and votable), `Unsupported` (declared but not
 *  implemented by this build), or `Retired` (conditional code removed,
 *  retained for ledger compatibility). The returned reference is stable for
 *  the process lifetime.
 *
 *  @return A sorted map of amendment name → `AmendmentSupport`.
 *  @note This function must only be called after static initialization
 *      completes. Calling it during static initialization of another
 *      translation unit risks querying before the registry is sealed.
 */
std::map<std::string, AmendmentSupport> const&
allAmendments();

namespace detail {

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

// NOLINTBEGIN(bugprone-macro-parentheses)
#define XRPL_FEATURE(name, supported, vote) +1
#define XRPL_FIX(name, supported, vote) +1
#define XRPL_RETIRE_FEATURE(name) +1
#define XRPL_RETIRE_FIX(name) +1
// NOLINTEND(bugprone-macro-parentheses)

/** Compile-time upper bound on the total number of registered amendments.
 *
 *  Used as the `std::bitset` template parameter for `FeatureBitset`. SHOULD
 *  equal the actual count of entries in `features.macro`, but MAY be larger
 *  (reserving headroom for future additions). MUST NOT be less than the actual
 *  count — a `LogicError` on startup verifies this.
 *
 *  @note This is a ceiling, not an exact count. Do not use it as an iteration
 *      bound or to infer the number of active amendments.
 */
static constexpr std::size_t kNUM_FEATURES =
    (0 +
#include <xrpl/protocol/detail/features.macro>
    );

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

/** Return amendments this build supports and their default vote stance.
 *
 *  Maps each supported amendment's name to its `VoteBehavior`. An amendment
 *  appearing here is recognized by this build; whether it is actually active
 *  depends on the `Rules` derived from the validated ledger's Amendments
 *  object. Retired amendments (`VoteBehavior::Obsolete`) appear here but are
 *  not voted for.
 *
 *  @return A sorted map of amendment name → `VoteBehavior`.
 */
std::map<std::string, VoteBehavior> const&
supportedAmendments();

/** Return the count of supported amendments this server will NOT vote for.
 *
 *  Includes both `VoteBehavior::DefaultNo` and `VoteBehavior::Obsolete`
 *  entries. Used in unit tests to verify the vote-tally invariant:
 *  `numDownVotedAmendments() + numUpVotedAmendments() == supportedAmendments().size()`.
 *
 *  @return Count of amendments this server abstains from or treats as obsolete.
 */
std::size_t
numDownVotedAmendments();

/** Return the count of supported amendments this server will vote for.
 *
 *  Counts only `VoteBehavior::DefaultYes` entries. Used in unit tests to
 *  verify the vote-tally invariant alongside `numDownVotedAmendments()`.
 *
 *  @return Count of amendments this server actively votes to activate.
 */
std::size_t
numUpVotedAmendments();

}  // namespace detail

/** Look up a registered amendment by name and return its on-chain identifier.
 *
 *  @param name The amendment's string name (e.g. `"Checks"`).
 *  @return The `uint256` hash computed as `sha512Half(name)`, or `std::nullopt`
 *      if no amendment with that name has been registered.
 *  @note Feature names are case-sensitive. Querying an unknown name returns
 *      `nullopt`; it does not throw.
 */
std::optional<uint256>
getRegisteredFeature(std::string const& name);

/** Translate an amendment's `uint256` identifier to its `FeatureBitset` bit position.
 *
 *  This is the hot-path translation used by every `FeatureBitset` operation.
 *  The result is stable for the process lifetime because the registry is sealed
 *  before any calls can be made.
 *
 *  @param f A registered amendment identifier.
 *  @return The zero-based bit index within `FeatureBitset`.
 *  @throws LogicError if `f` is not a registered amendment.
 */
size_t
featureToBitsetIndex(uint256 const& f);

/** Translate a `FeatureBitset` bit position back to the amendment's `uint256`.
 *
 *  Inverse of `featureToBitsetIndex()`. Used by `foreachFeature()` to convert
 *  set bits back into identifiers for callers.
 *
 *  @param i A zero-based bit index within `FeatureBitset`.
 *  @return The `uint256` hash of the amendment registered at that position.
 *  @throws LogicError if `i` is out of bounds (≥ the number of registered amendments).
 */
uint256
bitsetIndexToFeature(size_t i);

/** Return the human-readable name for an amendment, or its hex representation.
 *
 *  Useful for diagnostics and logging when a `uint256` amendment ID needs to be
 *  displayed.
 *
 *  @param f The amendment identifier to look up.
 *  @return The registered string name (e.g. `"Checks"`), or `to_string(f)` if
 *      `f` is not in the registry.
 */
std::string
featureToName(uint256 const& f);

/** A set of active amendments, represented as a bitset indexed by amendment ID.
 *
 *  Wraps `std::bitset<detail::kNUM_FEATURES>` and replaces integer-index access
 *  with `uint256`-based access. Externally every amendment is a `uint256` hash;
 *  internally `featureToBitsetIndex()` maps it to a compact sequential bit
 *  position, so all set operations run in O(1).
 *
 *  The full suite of bitwise operators is provided for set algebra:
 *  - `operator&` — intersection (features enabled in both sets)
 *  - `operator|` — union (features enabled in either set)
 *  - `operator^` — symmetric difference
 *  - `operator-` — **set difference** (`lhs & ~rhs`), used in amendment voting
 *    to compute "amendments I support that are not yet enabled"
 *
 *  Overloads accepting a bare `uint256` on either side construct a temporary
 *  single-element `FeatureBitset` for the operation.
 *
 *  @see foreachFeature() to iterate all set bits.
 *  @see Rules::enabled() for the per-transaction query path.
 */
class FeatureBitset : private std::bitset<detail::kNUM_FEATURES>
{
    using base = std::bitset<detail::kNUM_FEATURES>;

    template <class... Fs>
    void
    initFromFeatures(uint256 const& f, Fs&&... fs)
    {
        set(f);
        if constexpr (sizeof...(fs) > 0)
            initFromFeatures(std::forward<Fs>(fs)...);
    }

public:
    using base::bitset;
    using base::operator==;

    using base::all;
    using base::any;
    using base::count;
    using base::flip;
    using base::none;
    using base::reset;
    using base::set;
    using base::size;
    using base::test;
    using base::operator[];
    using base::to_string;
    using base::to_ullong;
    using base::to_ulong;

    /** Construct an empty feature set (no amendments enabled). */
    FeatureBitset() = default;

    /** Construct from a raw `std::bitset`, asserting no bits are lost.
     *
     *  @param b A bitset whose bit layout matches the amendment registry's
     *      insertion order. Intended for internal use (e.g. bitwise operators).
     */
    explicit FeatureBitset(base const& b) : base(b)
    {
        XRPL_ASSERT(b.count() == count(), "xrpl::FeatureBitset::FeatureBitset(base) : count match");
    }

    /** Construct from one or more amendment identifiers.
     *
     *  Each `uint256` is translated to its bitset position via
     *  `featureToBitsetIndex()`. Asserts that all supplied features are
     *  distinct (the resulting count equals the number of arguments).
     *
     *  @param f  First amendment identifier.
     *  @param fs Additional amendment identifiers (variadic).
     *  @throws LogicError (via `featureToBitsetIndex`) if any identifier is
     *      not registered.
     */
    template <class... Fs>
    explicit FeatureBitset(uint256 const& f, Fs&&... fs)
    {
        initFromFeatures(f, std::forward<Fs>(fs)...);
        XRPL_ASSERT(
            count() == (sizeof...(fs) + 1),
            "xrpl::FeatureBitset::FeatureBitset(uint256) : count and "
            "sizeof... do match");
    }

    /** Construct from any range of `uint256` amendment identifiers.
     *
     *  Iterates `fs` and sets the corresponding bit for each element.
     *  Asserts that the resulting popcount equals `fs.size()` (all distinct).
     *
     *  @tparam Col A range whose elements are convertible to `uint256`.
     *  @param fs   A collection of amendment identifiers.
     *  @throws LogicError (via `featureToBitsetIndex`) if any identifier is
     *      not registered.
     */
    template <class Col>
    explicit FeatureBitset(Col const& fs)
    {
        for (auto const& f : fs)
            set(featureToBitsetIndex(f));
        XRPL_ASSERT(
            fs.size() == count(),
            "xrpl::FeatureBitset::FeatureBitset(Container auto) : count and "
            "size do match");
    }

    /** Return a reference to the bit corresponding to amendment `f`.
     *
     *  @param f A registered amendment identifier.
     *  @throws LogicError if `f` is not registered.
     */
    auto
    operator[](uint256 const& f)
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    /** Return the value of the bit corresponding to amendment `f`.
     *
     *  @param f A registered amendment identifier.
     *  @throws LogicError if `f` is not registered.
     */
    auto
    operator[](uint256 const& f) const
    {
        return base::operator[](featureToBitsetIndex(f));
    }

    /** Set (or clear) the bit for amendment `f`.
     *
     *  @param f     A registered amendment identifier.
     *  @param value `true` to enable the amendment, `false` to disable.
     *  @return `*this`, for chaining.
     *  @throws LogicError if `f` is not registered.
     */
    FeatureBitset&
    set(uint256 const& f, bool value = true)
    {
        base::set(featureToBitsetIndex(f), value);
        return *this;
    }

    /** Clear the bit for amendment `f`.
     *
     *  @param f A registered amendment identifier.
     *  @return `*this`, for chaining.
     *  @throws LogicError if `f` is not registered.
     */
    FeatureBitset&
    reset(uint256 const& f)
    {
        base::reset(featureToBitsetIndex(f));
        return *this;
    }

    /** Toggle the bit for amendment `f`.
     *
     *  @param f A registered amendment identifier.
     *  @return `*this`, for chaining.
     *  @throws LogicError if `f` is not registered.
     */
    FeatureBitset&
    flip(uint256 const& f)
    {
        base::flip(featureToBitsetIndex(f));
        return *this;
    }

    /** Intersect this set with `rhs` in-place. */
    FeatureBitset&
    operator&=(FeatureBitset const& rhs)
    {
        base::operator&=(rhs);
        return *this;
    }

    /** Union this set with `rhs` in-place. */
    FeatureBitset&
    operator|=(FeatureBitset const& rhs)
    {
        base::operator|=(rhs);
        return *this;
    }

    /** Return the complement: every registered amendment NOT in this set. */
    FeatureBitset
    operator~() const
    {
        return FeatureBitset{base::operator~()};
    }

    /** Return the intersection of two feature sets. */
    friend FeatureBitset
    operator&(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) & static_cast<base const&>(rhs)};
    }

    /** Return the intersection of a feature set and a single amendment. */
    friend FeatureBitset
    operator&(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs & FeatureBitset{rhs};
    }

    /** Return the intersection of a single amendment and a feature set. */
    friend FeatureBitset
    operator&(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} & rhs;
    }

    /** Return the union of two feature sets. */
    friend FeatureBitset
    operator|(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) | static_cast<base const&>(rhs)};
    }

    /** Return the union of a feature set and a single amendment. */
    friend FeatureBitset
    operator|(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs | FeatureBitset{rhs};
    }

    /** Return the union of a single amendment and a feature set. */
    friend FeatureBitset
    operator|(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} | rhs;
    }

    /** Return the symmetric difference of two feature sets. */
    friend FeatureBitset
    operator^(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{static_cast<base const&>(lhs) ^ static_cast<base const&>(rhs)};
    }

    /** Return the symmetric difference of a feature set and a single amendment. */
    friend FeatureBitset
    operator^(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs ^ FeatureBitset{rhs};
    }

    /** Return the symmetric difference of a single amendment and a feature set. */
    friend FeatureBitset
    operator^(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} ^ rhs;
    }

    /** Return the set difference: amendments in `lhs` that are not in `rhs` (`lhs & ~rhs`).
     *
     *  Used in amendment voting to compute "amendments this server supports
     *  that have not yet been enabled on the network".
     */
    friend FeatureBitset
    operator-(FeatureBitset const& lhs, FeatureBitset const& rhs)
    {
        return lhs & ~rhs;
    }

    /** Return the set difference of a feature set minus a single amendment. */
    friend FeatureBitset
    operator-(FeatureBitset const& lhs, uint256 const& rhs)
    {
        return lhs - FeatureBitset{rhs};
    }

    /** Return the set difference: a single amendment minus all amendments in `rhs`. */
    friend FeatureBitset
    operator-(uint256 const& lhs, FeatureBitset const& rhs)
    {
        return FeatureBitset{lhs} - rhs;
    }
};

/** Invoke a callback for each amendment enabled in `bs`.
 *
 *  Iterates all bit positions in `bs`, translates each set bit back to its
 *  `uint256` amendment identifier via `bitsetIndexToFeature()`, and passes it
 *  to `f`. Unset bits are skipped.
 *
 *  @tparam F    A callable accepting a single `uint256 const&` argument.
 *  @param bs    The feature set to iterate.
 *  @param f     Callback invoked once per enabled amendment.
 */
template <class F>
void
foreachFeature(FeatureBitset bs, F&& f)
{
    for (size_t i = 0; i < bs.size(); ++i)
    {
        if (bs[i])
            f(bitsetIndexToFeature(i));
    }
}

// --- Amendment identifier declarations ---
//
// A second X-macro pass over features.macro declares one `extern uint256 const`
// variable per active amendment (e.g. `featureChecks`, `fixAMMOverflowOffer`).
// These are the identifiers used throughout the codebase in
// `rules.enabled(featureName)` calls. Retired entries expand to nothing because
// their conditional code has been removed.
#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

#define XRPL_FEATURE(name, supported, vote) extern uint256 const feature##name;
#define XRPL_FIX(name, supported, vote) extern uint256 const fix##name;
#define XRPL_RETIRE_FEATURE(name)
#define XRPL_RETIRE_FIX(name)

#include <xrpl/protocol/detail/features.macro>

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

}  // namespace xrpl
