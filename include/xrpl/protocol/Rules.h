#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/protocol/STVector256.h>

#include <unordered_set>

namespace xrpl {

/** Query whether a feature is enabled, with an explicit fallback value.
 *
 *  Delegates to the thread-local current `Rules` installed by
 *  `CurrentTransactionRulesGuard`. Use this overload when the desired
 *  behavior outside a transaction context differs from `false`.
 *
 *  @param feature The amendment ID to test.
 *  @param resultIfNoRules Value returned when called outside any transaction
 *      context (i.e. no `CurrentTransactionRulesGuard` is on the call stack).
 *  @return `true` if the feature is enabled in the current rules;
 *      `resultIfNoRules` if no rules are installed.
 */
bool
isFeatureEnabled(uint256 const& feature, bool resultIfNoRules);

/** Query whether a feature is enabled in the current transaction context.
 *
 *  Delegates to the thread-local current `Rules` installed by
 *  `CurrentTransactionRulesGuard`. Lower-level protocol code that cannot
 *  accept a `Rules` parameter (e.g. `STAmount`, `AMMHelpers`) uses this
 *  function instead. The implicit reliance on thread-local state means
 *  callers must ensure a `CurrentTransactionRulesGuard` is active on the
 *  call stack; calling it outside a transaction context silently returns
 *  `false`.
 *
 *  @param feature The amendment ID to test.
 *  @return `true` if the feature is enabled; `false` if no rules are
 *      installed or the feature is absent from the current ledger's
 *      amendment set.
 */
bool
isFeatureEnabled(uint256 const& feature);

class DigestAwareReadView;

/** Authoritative snapshot of which protocol amendments are active for a ledger.
 *
 *  Every behavioral branch in the transaction engine that depends on a
 *  conditionally-enabled feature gates through `Rules::enabled()`. `Rules` is
 *  a value type backed by a `shared_ptr<Impl const>` (pimpl), so copying is
 *  cheap — only an atomic refcount bump — regardless of how many amendments are
 *  active. Instances are typically constructed via `makeRulesGivenLedger` and
 *  installed on the call stack by `CurrentTransactionRulesGuard`.
 *
 *  @note The default constructor is deleted. Every `Rules` instance must carry
 *      an explicit preset set to prevent accidentally propagating a zero-feature
 *      state into transaction processing.
 *  @see makeRulesGivenLedger, CurrentTransactionRulesGuard
 */
class Rules
{
private:
    class Impl;

    // Carrying impl by shared_ptr makes Rules comparatively cheap to pass
    // by value.
    std::shared_ptr<Impl const> impl_;

public:
    Rules(Rules const&) = default;

    Rules(Rules&&) = default;

    Rules&
    operator=(Rules const&) = default;

    Rules&
    operator=(Rules&&) = default;

    Rules() = delete;

    /** Construct an empty rule set from a preset collection.
     *
     *  Intended for the genesis ledger, which has no on-ledger amendments yet.
     *  The preset features are treated as unconditionally enabled and checked
     *  first by `enabled()`.
     *
     *  @param presets Features that are always enabled regardless of ledger
     *      state (e.g. features forced on in test or devnet configurations).
     */
    explicit Rules(std::unordered_set<uint256, beast::Uhash<>> const& presets);

private:
    // Allow a friend function to construct Rules.
    friend Rules
    makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);

    friend Rules
    makeRulesGivenLedger(
        DigestAwareReadView const& ledger,
        std::unordered_set<uint256, beast::Uhash<>> const& presets);

    Rules(
        std::unordered_set<uint256, beast::Uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments);

    [[nodiscard]] std::unordered_set<uint256, beast::Uhash<>> const&
    presets() const;

public:
    /** Returns `true` if a feature is enabled in this rule set.
     *
     *  Checks the preset collection first (always-on features), then the
     *  on-ledger amendment set populated from `sfAmendments`.
     *
     *  @param feature The amendment ID to test.
     *  @return `true` if the feature is in the preset collection or was
     *      active in the ledger's amendment set at the time this `Rules`
     *      was constructed.
     */
    [[nodiscard]] bool
    enabled(uint256 const& feature) const;

    /** Returns `true` if two rule sets are identical.
     *
     *  Comparison is O(1) when both instances carry a digest (the common case
     *  for ledgers with an amendments SLE): differing digests are immediately
     *  unequal. Two instances without a digest (genesis state) are considered
     *  equal. An assertion guards against comparing instances with identical
     *  digests but differing presets.
     *
     *  @note Intended for diagnostics only, not for load-bearing equality
     *      decisions in transaction processing.
     */
    bool
    operator==(Rules const&) const;

    /** Returns `true` if two rule sets differ.
     *
     *  Derived from `operator==`; see its documentation for comparison
     *  semantics.
     */
    bool
    operator!=(Rules const& other) const;
};

/** Returns the active `Rules` for the current thread's transaction context.
 *
 *  The returned reference is valid until the next call to
 *  `setCurrentTransactionRules` on this thread. Prefer using
 *  `CurrentTransactionRulesGuard` over calling these functions directly.
 *
 *  @return The currently installed rules, or an empty `optional` if no
 *      `CurrentTransactionRulesGuard` is on the call stack.
 *  @see CurrentTransactionRulesGuard
 */
std::optional<Rules> const&
getCurrentTransactionRules();

/** Install `r` as the active rules for the current thread's transaction context.
 *
 *  Beyond storing `r` in the thread-local slot, this function also calls
 *  `Number::setMantissaScale()` to push the appropriate numeric precision mode:
 *  `MantissaRange::large` when `featureSingleAssetVault` or
 *  `featureLendingProtocol` is enabled, `small` otherwise. This push strategy
 *  avoids per-operation rule lookups inside hot arithmetic paths.
 *
 *  Prefer `CurrentTransactionRulesGuard` over calling this directly, as it
 *  ensures the previous rules are always restored on scope exit.
 *
 *  @param r The rules to install, or `std::nullopt` to clear the slot.
 *  @see CurrentTransactionRulesGuard
 */
void
setCurrentTransactionRules(std::optional<Rules> r);

/** RAII guard that installs a `Rules` into the thread-local transaction context.
 *
 *  The constructor calls `setCurrentTransactionRules` with the supplied rules,
 *  saving the previously active value. The destructor restores that saved value,
 *  ensuring the thread-local state is always reset even on exception paths.
 *  Non-copyable to prevent accidental aliasing of the saved state.
 *
 *  Production callers are `Transactor::operator()` and `applySteps.cpp`; test
 *  code uses this guard to bracket individual feature checks.
 *
 *  @see setCurrentTransactionRules, getCurrentTransactionRules
 */
class CurrentTransactionRulesGuard
{
public:
    explicit CurrentTransactionRulesGuard(Rules r) : saved_(getCurrentTransactionRules())
    {
        setCurrentTransactionRules(std::move(r));
    }

    ~CurrentTransactionRulesGuard()
    {
        setCurrentTransactionRules(saved_);
    }

    CurrentTransactionRulesGuard() = delete;
    CurrentTransactionRulesGuard(CurrentTransactionRulesGuard const&) = delete;
    CurrentTransactionRulesGuard&
    operator=(CurrentTransactionRulesGuard const&) = delete;

private:
    std::optional<Rules> saved_;
};

}  // namespace xrpl
