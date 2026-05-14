/** @file
 *  Implements the central amendment registry for the XRP Ledger.
 *
 *  Every conditional code path gated by an amendment queries this registry at
 *  runtime. The registry is populated entirely during static initialization —
 *  before `main()` — via the X-macro expansion of `features.macro`. Once the
 *  last file-scope variable is initialized, a `readOnly` fence is set and the
 *  registry becomes permanently immutable. No runtime lock is ever needed.
 *
 *  @see Feature.h for `FeatureBitset` and the public free-function declarations.
 *  @see include/xrpl/protocol/detail/features.macro for the amendment master list.
 */

#include <xrpl/protocol/Feature.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/digest.h>

#include <boost/container_hash/hash.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/indexed_by.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>

#include <atomic>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace xrpl {

/** Boost hash extension for `uint256`, required by `hashed_unique` indexes.
 *
 *  Combines each byte of the 256-bit value into a seed using
 *  `boost::hash_combine`. ADL finds this overload from the
 *  `boost::multi_index` hashed indexes used in `FeatureCollections`.
 *
 *  @param feature The amendment identifier to hash.
 *  @return A hash suitable for use in Boost unordered containers.
 */
inline std::size_t
// NOLINTNEXTLINE(readability-identifier-naming)
hash_value(xrpl::uint256 const& feature)
{
    std::size_t seed = 0;
    using namespace boost;
    for (auto const& n : feature)
        hash_combine(seed, n);
    return seed;
}

namespace {

/** Whether this build supports (and can vote for) a given amendment. */
enum class Supported : bool { No = false, Yes };

// --- Amendment lifecycle notes ---
//
// Amendment conditionals from before January 2018 have been removed (as of
// January 2020). Replaying ledgers from before that date requires an older
// server build. A warning in Application.cpp documents this boundary.
//
// New feature amendments use VoteBehavior::DefaultNo so that external
// governance controls activation timing. Critical bug-fix amendments may
// use VoteBehavior::DefaultYes after explicit off-chain community consensus.

/** Central registry for all XRPL protocol amendments.
 *
 *  Maintains three simultaneous views over the amendment set:
 *  - `features_` — a `multi_index_container` providing O(1) lookup by
 *    insertion-order index (for `FeatureBitset` mapping), by `uint256`
 *    hash, and by string name.
 *  - `all_` — every registered amendment including retired ones, keyed by
 *    name, mapped to `AmendmentSupport`.
 *  - `supported_` — only amendments the server can vote on, keyed by name,
 *    mapped to `VoteBehavior`.
 *
 *  The registry is write-only during static initialization. Once
 *  `registrationIsDone()` is called, `readOnly_` is set to `true` and every
 *  query method asserts this fence. No locking is required after that point.
 *
 *  @note `kNUM_FEATURES` is a compile-time ceiling, not an exact count. It
 *      may exceed the actual number of registered amendments.
 */
class FeatureCollections
{
    /** Stores one amendment's string name and its on-chain `uint256` identifier. */
    struct Feature
    {
        std::string name;
        uint256 feature;

        Feature() = delete;
        explicit Feature(std::string name, uint256 const& feature)
            : name(std::move(name)), feature(feature)
        {
        }

        /** Tag type for the random-access (insertion-order) index. */
        struct ByIndex
        {
        };
        /** Tag type for the hashed-unique name index. */
        struct ByName
        {
        };
        /** Tag type for the hashed-unique `uint256` feature-hash index. */
        struct ByFeature
        {
        };
    };

    /** Alias for a hashed-unique index over one `Feature` member. */
    template <class Tag, typename Type, Type Feature::* PtrToMember>
    using feature_hashed_unique = boost::multi_index::hashed_unique<
        boost::multi_index::tag<Tag>,
        boost::multi_index::member<Feature, Type, PtrToMember>>;

    /** Combined index specification: random-access by insertion order,
     *  hashed-unique by `uint256`, and hashed-unique by name.
     */
    using feature_indexing = boost::multi_index::indexed_by<
        boost::multi_index::random_access<boost::multi_index::tag<Feature::ByIndex>>,
        feature_hashed_unique<Feature::ByFeature, uint256, &Feature::feature>,
        feature_hashed_unique<Feature::ByName, std::string, &Feature::name>>;

    /** All registered amendments, accessible by index, name, or `uint256`. */
    boost::multi_index::multi_index_container<Feature, feature_indexing> features_;
    /** Every amendment (including retired) mapped to its support status. */
    std::map<std::string, AmendmentSupport> all_;
    /** Only supported amendments, mapped to their default vote behavior. */
    std::map<std::string, VoteBehavior> supported_;
    /** Count of supported amendments with `VoteBehavior::DefaultYes`. */
    std::size_t upVotes_ = 0;
    /** Count of supported amendments with `VoteBehavior::DefaultNo` or `Obsolete`. */
    std::size_t downVotes_ = 0;
    /** Write-once fence; flipped by `registrationIsDone()` after all static vars init. */
    mutable std::atomic<bool> readOnly_ = false;

    /** Return the `Feature` at insertion-order position `i`.
     *
     *  @param i Zero-based index into the registration sequence.
     *  @throws LogicError if `i` is out of bounds.
     */
    Feature const&
    getByIndex(size_t i) const
    {
        if (i >= features_.size())
            logicError("Invalid FeatureBitset index");
        auto const& sequence = features_.get<Feature::ByIndex>();
        return sequence[i];
    }

    /** Return the insertion-order index of a `Feature` element.
     *
     *  @param feature A reference into `features_`; must belong to this container.
     *  @return The zero-based position used as the corresponding `FeatureBitset` bit.
     */
    size_t
    getIndex(Feature const& feature) const
    {
        auto const& sequence = features_.get<Feature::ByIndex>();
        auto const itTo = sequence.iterator_to(feature);
        return itTo - sequence.begin();
    }

    /** Look up an amendment by its on-chain `uint256` identifier.
     *
     *  @param feature The hash to search for.
     *  @return Pointer to the matching `Feature`, or `nullptr` if not found.
     */
    Feature const*
    getByFeature(uint256 const& feature) const
    {
        auto const& featureIndex = features_.get<Feature::ByFeature>();
        auto const featureIt = featureIndex.find(feature);
        return featureIt == featureIndex.end() ? nullptr : &*featureIt;
    }

    /** Look up an amendment by its string name.
     *
     *  @param name The amendment name to search for.
     *  @return Pointer to the matching `Feature`, or `nullptr` if not found.
     */
    Feature const*
    getByName(std::string const& name) const
    {
        auto const& nameIndex = features_.get<Feature::ByName>();
        auto const nameIt = nameIndex.find(name);
        return nameIt == nameIndex.end() ? nullptr : &*nameIt;
    }

public:
    /** Reserve storage for `kNUM_FEATURES` entries. */
    FeatureCollections();

    /** Look up an amendment by name, returning its `uint256` hash if registered.
     *
     *  @param name The amendment's string name (e.g. `"Checks"`).
     *  @return The `uint256` identifier, or `std::nullopt` if not registered.
     *  @note Asserts that `registrationIsDone()` has been called; it is a
     *      programming error to query before static initialization completes.
     */
    std::optional<uint256>
    getRegisteredFeature(std::string const& name) const;

    /** Register a new amendment and return its deterministic `uint256` hash.
     *
     *  Computes the hash as `sha512Half(name)` and inserts into all three
     *  internal indexes. Enforces at registration time:
     *  - `Supported::No` implies `VoteBehavior::DefaultNo`.
     *  - No duplicate names are allowed.
     *  - Total count must stay within `detail::kNUM_FEATURES`.
     *  - `upVotes_ + downVotes_` must equal `supported_.size()` after insertion.
     *
     *  @param name    The amendment's ASCII name (validated at call site).
     *  @param support Whether this build supports the amendment.
     *  @param vote    The server's default vote behavior.
     *  @return The `uint256` on-chain identifier derived from `name`.
     *  @throws LogicError on any invariant violation or duplicate registration.
     */
    uint256
    registerFeature(std::string const& name, Supported support, VoteBehavior vote);

    /** Flip the write-once fence, marking registration as complete.
     *
     *  Called exactly once by the file-scope `kREAD_ONLY_SET` variable after all
     *  amendment globals have been initialized. After this call, all query methods
     *  become safe to use and all write methods are permanently disabled.
     *
     *  @return Always `true` (allows use as a static variable initializer).
     */
    bool
    registrationIsDone();

    /** Translate an amendment's `uint256` to its `FeatureBitset` bit position.
     *
     *  @param f The amendment identifier.
     *  @return The zero-based bit index within `FeatureBitset`.
     *  @throws LogicError if `f` is not a registered amendment.
     *  @note Asserts `readOnly_` — must not be called before registration completes.
     */
    std::size_t
    featureToBitsetIndex(uint256 const& f) const;

    /** Translate a `FeatureBitset` bit position back to the amendment's `uint256`.
     *
     *  @param i The zero-based bit index.
     *  @return A reference to the amendment's `uint256` hash (stable for process lifetime).
     *  @throws LogicError if `i` is out of bounds.
     *  @note Asserts `readOnly_` — must not be called before registration completes.
     */
    uint256 const&
    bitsetIndexToFeature(size_t i) const;

    /** Return the string name for an amendment hash, or its hex string if unknown.
     *
     *  @param f The amendment identifier to look up.
     *  @return The registered name (e.g. `"Checks"`), or `to_string(f)` if `f`
     *      is not in the registry.
     *  @note Asserts `readOnly_` — must not be called before registration completes.
     */
    std::string
    featureToName(uint256 const& f) const;

    /** All amendments that are registered within the table. */
    std::map<std::string, AmendmentSupport> const&
    allAmendments() const
    {
        return all_;
    }

    /** Amendments that this server supports.
    Whether they are enabled depends on the Rules defined in the validated
    ledger */
    std::map<std::string, VoteBehavior> const&
    supportedAmendments() const
    {
        return supported_;
    }

    /** Amendments that this server WON'T vote for by default. */
    std::size_t
    numDownVotedAmendments() const
    {
        return downVotes_;
    }

    /** Amendments that this server WILL vote for by default. */
    std::size_t
    numUpVotedAmendments() const
    {
        return upVotes_;
    }
};

//------------------------------------------------------------------------------

FeatureCollections::FeatureCollections()
{
    features_.reserve(xrpl::detail::kNUM_FEATURES);
}

std::optional<uint256>
FeatureCollections::getRegisteredFeature(std::string const& name) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::getRegisteredFeature : startup completed");
    Feature const* feature = getByName(name);
    if (feature != nullptr)
        return feature->feature;
    return std::nullopt;
}

/** Throw a `LogicError` with `logicErrorMessage` unless `condition` is true.
 *
 *  Used exclusively inside `registerFeature` to enforce registration-time
 *  invariants. Failures here indicate a programming error in the amendment
 *  macro list and abort the process at startup.
 *
 *  @param condition        The invariant that must hold.
 *  @param logicErrorMessage Message passed to `logicError` on failure.
 */
void
check(bool condition, char const* logicErrorMessage)
{
    if (!condition)
        logicError(logicErrorMessage);
}

uint256
FeatureCollections::registerFeature(std::string const& name, Supported support, VoteBehavior vote)
{
    check(!readOnly_, "Attempting to register a feature after startup.");
    check(
        support == Supported::Yes || vote == VoteBehavior::DefaultNo,
        "Invalid feature parameters. Must be supported to be up-voted.");
    Feature const* i = getByName(name);
    if (i == nullptr)
    {
        check(features_.size() < detail::kNUM_FEATURES, "More features defined than allocated.");

        auto const f = sha512Half(Slice(name.data(), name.size()));

        features_.emplace_back(name, f);

        auto const getAmendmentSupport = [=]() {
            if (vote == VoteBehavior::Obsolete)
                return AmendmentSupport::Retired;
            return support == Supported::Yes ? AmendmentSupport::Supported
                                             : AmendmentSupport::Unsupported;
        };
        all_.emplace(name, getAmendmentSupport());

        if (support == Supported::Yes)
        {
            supported_.emplace(name, vote);

            if (vote == VoteBehavior::DefaultYes)
            {
                ++upVotes_;
            }
            else
            {
                ++downVotes_;
            }
        }
        check(upVotes_ + downVotes_ == supported_.size(), "Feature counting logic broke");
        check(
            supported_.size() <= features_.size(), "More supported features than defined features");
        check(features_.size() == all_.size(), "The 'all' features list is populated incorrectly");
        return f;
    }

    logicError("Duplicate feature registration");
}

bool
FeatureCollections::registrationIsDone()
{
    readOnly_ = true;
    return true;
}

size_t
FeatureCollections::featureToBitsetIndex(uint256 const& f) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::featureToBitsetIndex : startup completed");

    Feature const* feature = getByFeature(f);
    if (feature == nullptr)
        logicError("Invalid Feature ID");

    return getIndex(*feature);
}

uint256 const&
FeatureCollections::bitsetIndexToFeature(size_t i) const
{
    XRPL_ASSERT(
        readOnly_.load(), "xrpl::FeatureCollections::bitsetIndexToFeature : startup completed");
    Feature const& feature = getByIndex(i);
    return feature.feature;
}

std::string
FeatureCollections::featureToName(uint256 const& f) const
{
    XRPL_ASSERT(readOnly_.load(), "xrpl::FeatureCollections::featureToName : startup completed");
    Feature const* feature = getByFeature(f);
    return (feature != nullptr) ? feature->name : to_string(f);
}

FeatureCollections gFeatureCollections;

}  // namespace

/** All amendments libxrpl knows of. */
std::map<std::string, AmendmentSupport> const&
allAmendments()
{
    return gFeatureCollections.allAmendments();
}

/** Amendments that this server supports and their default voting behavior.
 *
 *  Whether any of these amendments is actually active depends on the
 *  `Rules` object derived from the validated ledger's Amendments object.
 */
std::map<std::string, VoteBehavior> const&
detail::supportedAmendments()
{
    return gFeatureCollections.supportedAmendments();
}

/** Number of supported amendments this server will NOT vote for by default.
 *
 *  Includes both `VoteBehavior::DefaultNo` and `VoteBehavior::Obsolete`
 *  entries. Used in unit tests to verify vote-tally invariants.
 */
std::size_t
detail::numDownVotedAmendments()
{
    return gFeatureCollections.numDownVotedAmendments();
}

/** Number of supported amendments this server WILL vote for by default.
 *
 *  Counts only `VoteBehavior::DefaultYes` entries. Used in unit tests to
 *  verify vote-tally invariants.
 */
std::size_t
detail::numUpVotedAmendments()
{
    return gFeatureCollections.numUpVotedAmendments();
}

//------------------------------------------------------------------------------

/** Look up a registered amendment by name and return its `uint256` hash.
 *
 *  @param name The amendment's string name.
 *  @return The `uint256` identifier if registered, or `std::nullopt`.
 */
std::optional<uint256>
getRegisteredFeature(std::string const& name)
{
    return gFeatureCollections.getRegisteredFeature(name);
}

/** Register an amendment and return its deterministic `uint256` identifier.
 *
 *  Called during static initialization — once per amendment — via the
 *  X-macro expansion of `features.macro`. Must not be called after
 *  `registrationIsDone()` has been called.
 *
 *  @param name    The amendment's ASCII name.
 *  @param support Whether this build supports the amendment.
 *  @param vote    The server's default vote behavior.
 *  @return The `uint256` hash computed as `sha512Half(name)`.
 *  @throws LogicError on invariant violation or duplicate name.
 */
uint256
registerFeature(std::string const& name, Supported support, VoteBehavior vote)
{
    return gFeatureCollections.registerFeature(name, support, vote);
}

/** Register an amendment whose conditional code has been removed from the codebase.
 *
 *  Retired amendments must remain registered because they may appear in the
 *  Amendments ledger object. Removing them entirely would cause amendment
 *  blocking. They are registered as `Supported::Yes, VoteBehavior::Obsolete`
 *  so the server understands them but does not vote for them.
 *
 *  @param name The amendment's string name.
 *  @return The `uint256` hash for the retired amendment.
 */
uint256
retireFeature(std::string const& name)
{
    return registerFeature(name, Supported::Yes, VoteBehavior::Obsolete);
}

/** Seal the registry against further writes.
 *
 *  Called exactly once by the file-scope `kREAD_ONLY_SET` initializer after
 *  all amendment globals have been constructed. Subsequent query calls will
 *  assert this fence.
 *
 *  @return Always `true`.
 */
bool
registrationIsDone()
{
    return gFeatureCollections.registrationIsDone();
}

/** Translate an amendment `uint256` to its `FeatureBitset` bit position.
 *
 *  @param f A registered amendment identifier.
 *  @return The zero-based bit index within `FeatureBitset`.
 *  @throws LogicError if `f` is not registered.
 */
size_t
featureToBitsetIndex(uint256 const& f)
{
    return gFeatureCollections.featureToBitsetIndex(f);
}

/** Translate a `FeatureBitset` bit position to its amendment `uint256`.
 *
 *  @param i Zero-based bit index within `FeatureBitset`.
 *  @return The `uint256` hash for the amendment at that index.
 *  @throws LogicError if `i` is out of bounds.
 */
uint256
bitsetIndexToFeature(size_t i)
{
    return gFeatureCollections.bitsetIndexToFeature(i);
}

/** Return the human-readable name for an amendment, or its hex string if unknown.
 *
 *  @param f The amendment identifier.
 *  @return The registered name, or `to_string(f)` if `f` is not in the registry.
 */
std::string
featureToName(uint256 const& f)
{
    return gFeatureCollections.featureToName(f);
}

// --- Amendment registration via X-macro expansion ---
//
// The macros below expand features.macro to produce one file-scope
// `uint256 const feature<Name>` variable per amendment. Each variable's
// initializer calls `registerFeature`, which inserts into `gFeatureCollections`
// and returns the hash. C++ guarantees top-to-bottom initialization within a
// translation unit, so all amendments are registered before `kREAD_ONLY_SET`
// seals the registry.

#pragma push_macro("XRPL_FEATURE")
#undef XRPL_FEATURE
#pragma push_macro("XRPL_FIX")
#undef XRPL_FIX
#pragma push_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FEATURE
#pragma push_macro("XRPL_RETIRE_FIX")
#undef XRPL_RETIRE_FIX

/** Validate a feature name at compile time and return it as a C-string.
 *
 *  Wraps `validFeatureName` and `validFeatureNameSize` (both `consteval`) in
 *  `static_assert` expressions so that an ill-formed name in `features.macro`
 *  is a build error. Checks:
 *  - No non-ASCII bytes (high bit set) or control characters (below 0x20).
 *  - Length ≤ 63 bytes and length ≠ 32 bytes (the 32-byte length is reserved
 *    for raw `uint256` hash values in WASM / interop contexts).
 *
 *  @param fn A `consteval` lambda returning `const char*` — the name literal.
 *  @return The same C-string the lambda returns.
 */
consteval auto
enforceValidFeatureName(auto fn) -> char const*
{
    static_assert(validFeatureName(fn), "Invalid feature name");
    static_assert(validFeatureNameSize(fn), "Invalid feature name size");
    return fn();
}

#define XRPL_FEATURE(name, supported, vote) \
    uint256 const feature##name =           \
        registerFeature(enforceValidFeatureName([] { return #name; }), supported, vote);
#define XRPL_FIX(name, supported, vote) \
    uint256 const fix##name =           \
        registerFeature(enforceValidFeatureName([] { return "fix" #name; }), supported, vote);

// clang-format off
#define XRPL_RETIRE_FEATURE(name)                                       \
    [[deprecated("The referenced feature amendment has been retired")]] \
    [[maybe_unused]]                                                    \
    uint256 const retiredFeature##name = retireFeature(#name);

#define XRPL_RETIRE_FIX(name)                                           \
    [[deprecated("The referenced fix amendment has been retired")]]     \
    [[maybe_unused]]                                                    \
    uint256 const retiredFix##name = retireFeature("fix" #name);
// clang-format on

#include <xrpl/protocol/detail/features.macro>

#include <utility>

#undef XRPL_RETIRE_FEATURE
#pragma pop_macro("XRPL_RETIRE_FEATURE")
#undef XRPL_RETIRE_FIX
#pragma pop_macro("XRPL_RETIRE_FIX")
#undef XRPL_FIX
#pragma pop_macro("XRPL_FIX")
#undef XRPL_FEATURE
#pragma pop_macro("XRPL_FEATURE")

// All amendments are now registered. This final static variable seals the
// registry by flipping `readOnly_` to true. C++ guarantees that all
// file-scope variables above are fully initialized before this line runs.
[[maybe_unused]] static bool const kREAD_ONLY_SET = gFeatureCollections.registrationIsDone();

}  // namespace xrpl
