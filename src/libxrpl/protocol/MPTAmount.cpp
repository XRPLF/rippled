#include <xrpl/protocol/MPTAmount.h>

#include <xrpl/basics/MathUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>

#include <stdexcept>

namespace xrpl {

MPTAmount&
MPTAmount::operator+=(MPTAmount const& other)
{
    if (auto const result = checkedAdd(value_, other.value()))
    {
        value_ = *result;
        return *this;
    }
    if (isFeatureEnabled(featureMPTokensV2, /*resultIfNoRules*/ true))
        Throw<std::overflow_error>("MPTAmount::operator+= overflow");
    value_ += other.value();
    return *this;
}

MPTAmount&
MPTAmount::operator-=(MPTAmount const& other)
{
    if (auto const result = checkedSub(value_, other.value()))
    {
        value_ = *result;
        return *this;
    }
    if (isFeatureEnabled(featureMPTokensV2, /*resultIfNoRules*/ true))
        Throw<std::overflow_error>("MPTAmount::operator-= overflow");
    value_ -= other.value();
    return *this;
}

MPTAmount
MPTAmount::operator-() const
{
    return MPTAmount{-value_};
}

bool
MPTAmount::operator==(MPTAmount const& other) const
{
    return value_ == other.value_;
}

bool
MPTAmount::operator==(value_type other) const
{
    return value_ == other;
}

bool
MPTAmount::operator<(MPTAmount const& other) const
{
    return value_ < other.value_;
}

MPTAmount
MPTAmount::minPositiveAmount()
{
    return MPTAmount{1};
}

}  // namespace xrpl
