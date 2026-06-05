#include <xrpl/protocol/Rules.h>

#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/hardened_hash.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STVector256.h>

#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace xrpl {

namespace {
// Use a static inside a function to help prevent order-of-initialization issues
LocalValue<std::optional<Rules>>&
getCurrentTransactionRulesRef()
{
    static LocalValue<std::optional<Rules>> kR;
    return kR;
}
}  // namespace

std::optional<Rules> const&
getCurrentTransactionRules()
{
    return *getCurrentTransactionRulesRef();
}

void
setCurrentTransactionRules(std::optional<Rules> r)
{
    // Make global changes associated with the rules before the value is moved.
    // Push the appropriate setting, instead of having the class pull every time
    // the value is needed. That could get expensive fast.

    // If any new conditions with new amendments are added, those amendments must also be added to
    // useRulesGuards.
    bool const enableVaultNumbers =
        !r || (r->enabled(featureSingleAssetVault) || r->enabled(featureLendingProtocol));
    bool const enableCuspRoundingFix = !r || r->enabled(fixCleanup3_2_0);
    XRPL_ASSERT(
        !r || useRulesGuards(*r) == (enableCuspRoundingFix || enableVaultNumbers),
        "setCurrentTransactionRules : rule decisions match");

    // Declare the range this way to keep clang-tidy from complaining
    auto const range = [enableCuspRoundingFix, enableVaultNumbers]() {
        if (enableVaultNumbers)
        {
            if (enableCuspRoundingFix)
            {
                return MantissaRange::MantissaScale::Large;
            }
            return MantissaRange::MantissaScale::LargeLegacy;
        }
        return MantissaRange::MantissaScale::Small;
    }();
    Number::setMantissaScale(range);

    *getCurrentTransactionRulesRef() = std::move(r);
}

bool
useRulesGuards(Rules const& rules)
{
    // The list of amendments used here - to decide whether to create a RulesGuard - must be a
    // superset of the list used to figure out which mantissa scale to use in
    // setCurrentTransactionRules. Additional amendments can be added if desired.
    //
    // As soon as any one of these amendments is retired, this whole function can be removed, along
    // with createGuards, and any other callers, and the first set of guards can be created directly
    // at the call site, without using optional.
    return rules.enabled(fixCleanup3_2_0) || rules.enabled(featureSingleAssetVault) ||
        rules.enabled(featureLendingProtocol);
}

void
createGuards(
    Rules const& rules,
    std::optional<NumberSO>& stNumberSO,
    std::optional<CurrentTransactionRulesGuard>& rulesGuard,
    std::optional<NumberMantissaScaleGuard>& mantissaScaleGuard)
{
    if (useRulesGuards(rules))
    {
        // raii classes for the current ledger rules.
        // fixUniversalNumber predates the rulesGuard and should be replaced.
        stNumberSO.emplace(rules.enabled(fixUniversalNumber));
        rulesGuard.emplace(rules);
    }
    else
    {
        // Without those features enabled, always use the old number rules.
        mantissaScaleGuard.emplace(MantissaRange::MantissaScale::Small);
    }
}

class Rules::Impl
{
private:
    std::unordered_set<uint256, HardenedHash<>> set_;
    std::optional<uint256> digest_;
    std::unordered_set<uint256, beast::Uhash<>> const& presets_;

public:
    explicit Impl(std::unordered_set<uint256, beast::Uhash<>> const& presets) : presets_(presets)
    {
    }

    Impl(
        std::unordered_set<uint256, beast::Uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments)
        : digest_(digest), presets_(presets)
    {
        set_.reserve(amendments.size());
        set_.insert(amendments.begin(), amendments.end());
    }

    [[nodiscard]] std::unordered_set<uint256, beast::Uhash<>> const&
    presets() const
    {
        return presets_;
    }

    [[nodiscard]] bool
    enabled(uint256 const& feature) const
    {
        if (presets_.contains(feature))
            return true;
        return set_.contains(feature);
    }

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

Rules::Rules(std::unordered_set<uint256, beast::Uhash<>> const& presets)
    : impl_(std::make_shared<Impl>(presets))
{
}

Rules::Rules(
    std::unordered_set<uint256, beast::Uhash<>> const& presets,
    std::optional<uint256> const& digest,
    STVector256 const& amendments)
    : impl_(std::make_shared<Impl>(presets, digest, amendments))
{
}

std::unordered_set<uint256, beast::Uhash<>> const&
Rules::presets() const
{
    return impl_->presets();
}

bool
Rules::enabled(uint256 const& feature) const
{
    XRPL_ASSERT(impl_, "xrpl::Rules::enabled : initialized");

    return impl_->enabled(feature);
}

bool
Rules::operator==(Rules const& other) const
{
    XRPL_ASSERT(impl_ && other.impl_, "xrpl::Rules::operator==(Rules) const : both initialized");
    if (impl_.get() == other.impl_.get())
        return true;
    return *impl_ == *other.impl_;
}

bool
Rules::operator!=(Rules const& other) const
{
    return !(*this == other);
}

bool
isFeatureEnabled(uint256 const& feature, bool resultIfNoRules)
{
    auto const& rules = getCurrentTransactionRules();
    if (!rules)
        return resultIfNoRules;
    return rules->enabled(feature);
}

bool
isFeatureEnabled(uint256 const& feature)
{
    return isFeatureEnabled(feature, false);
}

}  // namespace xrpl
