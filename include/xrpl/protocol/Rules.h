#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/protocol/STVector256.h>

#include <unordered_set>

namespace xrpl {

/** Check whether a feature is enabled in the current ledger rules
 *
 * @param feature The feature to be tested.
 * @param resultIfNoRules What to return if called from outside a Transactor context.
 */
bool
isFeatureEnabled(uint256 const& feature, bool resultIfNoRules);

/** Check whether a feature is enabled in the current ledger rules
 *
 * @param feature The feature to be tested.
 *
 * Returns false if no global Rules object is available. i.e. Outside of
 * a Transactor context
 */
bool
isFeatureEnabled(uint256 const& feature);

class DigestAwareReadView;

/** Rules controlling protocol behavior. */
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

    /** Construct an empty rule set.

        These are the rules reflected by
        the genesis ledger.
    */
    explicit Rules(std::unordered_set<uint256, beast::uhash<>> const& presets);

private:
    // Allow a friend function to construct Rules.
    friend Rules
    makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);

    friend Rules
    makeRulesGivenLedger(
        DigestAwareReadView const& ledger,
        std::unordered_set<uint256, beast::uhash<>> const& presets);

    Rules(
        std::unordered_set<uint256, beast::uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments);

    [[nodiscard]] std::unordered_set<uint256, beast::uhash<>> const&
    presets() const;

public:
    /** Returns `true` if a feature is enabled. */
    [[nodiscard]] bool
    enabled(uint256 const& feature) const;

    /** Returns `true` if two rule sets are identical.

        @note This is for diagnostics.
    */
    bool
    operator==(Rules const&) const;

    bool
    operator!=(Rules const& other) const;
};

std::optional<Rules> const&
getCurrentTransactionRules();

void
setCurrentTransactionRules(std::optional<Rules> r);

/** RAII class to set and restore the current transaction rules
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
