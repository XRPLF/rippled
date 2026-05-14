/** @file
 *  Implements the amendment-rule snapshot (`Rules`) and the
 *  per-coroutine/per-thread slot that holds it during transaction processing.
 *
 *  `Rules` is immutable after construction and cheap to copy (pimpl via
 *  `shared_ptr`). The thread/coroutine-local slot uses `LocalValue` so that
 *  coroutines sharing a thread do not stomp each other's rule context.
 *  `setCurrentTransactionRules` also pushes the `Number` mantissa scale as a
 *  side effect so that financial arithmetic uses the correct precision without
 *  querying rules on every operation.
 */

#include <xrpl/protocol/Rules.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STVector256.h>

#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace xrpl {

namespace {

/** Returns the per-coroutine/per-thread slot for the active `Rules`.
 *
 *  Wrapped in a function to guarantee the `LocalValue` is constructed on
 *  first use, avoiding C++ static-initialization-order issues that would arise
 *  if it were declared at namespace scope.
 *
 *  @return A reference to the `LocalValue` holding `std::optional<Rules>`.
 */
LocalValue<std::optional<Rules>>&
getCurrentTransactionRulesRef()
{
    static LocalValue<std::optional<Rules>> kR;
    return kR;
}
}  // namespace

/** Returns the currently active transaction rules for this coroutine/thread.
 *
 *  Delegates to the `LocalValue` slot so callers do not need to hold the
 *  slot reference directly.  Returns `std::nullopt` when called outside a
 *  `Transactor` context (e.g., during startup or in unit tests that have not
 *  called `setCurrentTransactionRules`).
 *
 *  @return A const reference to the optional `Rules` stored in the
 *      per-coroutine/per-thread slot.
 */
std::optional<Rules> const&
getCurrentTransactionRules()
{
    return *getCurrentTransactionRulesRef();
}

/** Installs `r` as the active transaction rules for this coroutine/thread.
 *
 *  In addition to storing the new rules, this function pushes the `Number`
 *  mantissa scale that matches the enabled feature set.  Specifically, when
 *  `featureSingleAssetVault` or `featureLendingProtocol` is active, the
 *  `MantissaScale::Large` range is required to avoid overflow in AMM and
 *  lending calculations.  When `r` is `nullopt`, large numbers are permitted
 *  as a conservative default.
 *
 *  The scale is pushed here rather than pulled inside `Number` arithmetic
 *  because `Number` operations are called millions of times per ledger;
 *  a single push per rules change is far cheaper.
 *
 *  @param r The new ruleset to install, or `nullopt` to clear the slot.
 */
void
setCurrentTransactionRules(std::optional<Rules> r)
{
    // Make global changes associated with the rules before the value is moved.
    // Push the appropriate setting, instead of having the class pull every time
    // the value is needed. That could get expensive fast.
    bool const enableLargeNumbers =
        !r || (r->enabled(featureSingleAssetVault) || r->enabled(featureLendingProtocol));
    Number::setMantissaScale(
        enableLargeNumbers ? MantissaRange::MantissaScale::Large
                           : MantissaRange::MantissaScale::Small);

    *getCurrentTransactionRulesRef() = std::move(r);
}

/** Private implementation of `Rules`.
 *
 *  Stores the enabled-amendment set derived from a ledger's `sfAmendments`
 *  field (`set_`) and a reference to the externally-owned operator presets
 *  (`presets_`).  Both sets are immutable after construction, so all member
 *  functions are thread-safe with no locking required.
 *
 *  `set_` uses `HardenedHash` (xxHash + random seed) to guard against
 *  hash-flooding when inserting validator-supplied amendment IDs.  `presets_`
 *  is operator-controlled and uses the lighter `beast::Uhash`.
 *
 *  `digest_` caches the `uint256` hash of the on-ledger amendments object so
 *  that equality comparisons can avoid a full set comparison.
 */
class Rules::Impl
{
private:
    /** The enabled amendments from the ledger's `sfAmendments` field. */
    std::unordered_set<uint256, HardenedHash<>> set_;

    /** Hash of the amendments SLE; absent for genesis/preset-only rules. */
    std::optional<uint256> digest_;

    /** Node-operator-supplied features that are unconditionally enabled. */
    std::unordered_set<uint256, beast::Uhash<>> const& presets_;

public:
    /** Constructs a genesis (preset-only) rule set.
     *
     *  Used for the genesis ledger and unit tests.  `set_` is empty and
     *  `digest_` is absent.
     *
     *  @param presets Externally-owned set of always-on features.
     */
    explicit Impl(std::unordered_set<uint256, beast::Uhash<>> const& presets) : presets_(presets)
    {
    }

    /** Constructs a rule set from a ledger's amendment list.
     *
     *  Called exclusively by the private `Rules` constructor, which is in
     *  turn called only by the `makeRulesGivenLedger` friend functions.
     *
     *  @param presets   Externally-owned set of always-on features.
     *  @param digest    Hash of the amendments SLE (used for fast equality).
     *  @param amendments The `sfAmendments` vector read from the ledger.
     */
    Impl(
        std::unordered_set<uint256, beast::Uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments)
        : digest_(digest), presets_(presets)
    {
        set_.reserve(amendments.size());
        set_.insert(amendments.begin(), amendments.end());
    }

    /** Returns the operator-supplied preset features. */
    [[nodiscard]] std::unordered_set<uint256, beast::Uhash<>> const&
    presets() const
    {
        return presets_;
    }

    /** Returns `true` if `feature` is enabled.
     *
     *  Checks `presets_` first (O(1)), then `set_` (O(1)).  No locking is
     *  needed because both sets are immutable after construction.
     *
     *  @param feature The amendment ID to test.
     */
    [[nodiscard]] bool
    enabled(uint256 const& feature) const
    {
        if (presets_.contains(feature))
            return true;
        return set_.contains(feature);
    }

    /** Returns `true` if two `Impl` objects represent the same rule set.
     *
     *  Uses `digest_` rather than a full set comparison for efficiency.
     *  Two instances without a digest (genesis rules) are always equal.
     *  If exactly one has a digest, they are unequal.  When both have
     *  digests, equality is determined by comparing the digests; an
     *  assertion also verifies that `presets_` match, since identical
     *  digests with different presets would produce different behavior.
     *
     *  @param other The `Impl` to compare against.
     */
    bool
    operator==(Impl const& other) const
    {
        if (!digest_ && !other.digest_)
            return true;
        if (!digest_ || !other.digest_)
            return false;
        XRPL_ASSERT(
            presets_ == other.presets_,
            "xrpl::Rules::Impl::operator==(Impl) const : input presets do "
            "match");
        return *digest_ == *other.digest_;
    }
};

/** Constructs genesis (preset-only) rules; delegates to `Impl`. */
Rules::Rules(std::unordered_set<uint256, beast::Uhash<>> const& presets)
    : impl_(std::make_shared<Impl>(presets))
{
}

/** Constructs rules from a ledger's amendment list; delegates to `Impl`.
 *
 *  Private — only callable by the `makeRulesGivenLedger` friend functions.
 */
Rules::Rules(
    std::unordered_set<uint256, beast::Uhash<>> const& presets,
    std::optional<uint256> const& digest,
    STVector256 const& amendments)
    : impl_(std::make_shared<Impl>(presets, digest, amendments))
{
}

/** Returns the operator-supplied preset features; delegates to `Impl`. */
std::unordered_set<uint256, beast::Uhash<>> const&
Rules::presets() const
{
    return impl_->presets();
}

/** Returns `true` if `feature` is enabled; delegates to `Impl::enabled`. */
bool
Rules::enabled(uint256 const& feature) const
{
    XRPL_ASSERT(impl_, "xrpl::Rules::enabled : initialized");

    return impl_->enabled(feature);
}

/** Returns `true` if `other` represents the same rule set.
 *
 *  Short-circuits on pointer identity before delegating to
 *  `Impl::operator==`, making self-comparison O(1).
 */
bool
Rules::operator==(Rules const& other) const
{
    XRPL_ASSERT(impl_ && other.impl_, "xrpl::Rules::operator==(Rules) const : both initialized");
    if (impl_.get() == other.impl_.get())
        return true;
    return *impl_ == *other.impl_;
}

/** Returns `true` if `other` represents a different rule set. */
bool
Rules::operator!=(Rules const& other) const
{
    return !(*this == other);
}

/** Returns whether `feature` is enabled, with a caller-supplied fallback.
 *
 *  Fetches the per-coroutine/per-thread `Rules` and delegates to
 *  `Rules::enabled`.  When no rules are installed, returns `resultIfNoRules`
 *  instead of querying a null object.
 *
 *  @param feature         The amendment ID to test.
 *  @param resultIfNoRules Value to return when called outside a `Transactor`
 *      context (e.g., during startup or in tests without rules installed).
 *  @return Whether the feature is enabled, or `resultIfNoRules` if absent.
 */
bool
isFeatureEnabled(uint256 const& feature, bool resultIfNoRules)
{
    auto const& rules = getCurrentTransactionRules();
    if (!rules)
        return resultIfNoRules;
    return rules->enabled(feature);
}

/** Returns whether `feature` is enabled; returns `false` if no rules are set.
 *
 *  Convenience overload of `isFeatureEnabled(feature, resultIfNoRules)` with
 *  `resultIfNoRules = false`.  The safe-default of `false` prevents accidental
 *  feature activation in code paths that have not installed a rule context.
 *
 *  @param feature The amendment ID to test.
 *  @return Whether the feature is enabled, or `false` if no rules are set.
 */
bool
isFeatureEnabled(uint256 const& feature)
{
    return isFeatureEnabled(feature, false);
}

}  // namespace xrpl
