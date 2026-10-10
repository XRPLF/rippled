#include <xrpl/protocol/XRPAmount.h>

#include <xrpl/basics/MathUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>

#include <stdexcept>

namespace xrpl {

XRPAmount&
XRPAmount::operator+=(value_type const& rhs)
{
    if (auto const result = checkedAdd(drops_, rhs))
    {
        drops_ = *result;
        return *this;
    }
    if (isFeatureEnabled(fixCleanup3_5_0, /*resultIfNoRules*/ true))
        Throw<std::overflow_error>("XRPAmount::operator+= overflow");
    drops_ += rhs;
    return *this;
}

XRPAmount&
XRPAmount::operator-=(value_type const& rhs)
{
    if (auto const result = checkedSub(drops_, rhs))
    {
        drops_ = *result;
        return *this;
    }
    if (isFeatureEnabled(fixCleanup3_5_0, /*resultIfNoRules*/ true))
        Throw<std::overflow_error>("XRPAmount::operator-= overflow");
    drops_ -= rhs;
    return *this;
}

XRPAmount&
XRPAmount::operator*=(value_type const& rhs)
{
    if (auto const result = checkedMul(drops_, rhs))
    {
        drops_ = *result;
        return *this;
    }
    if (isFeatureEnabled(fixCleanup3_5_0, /*resultIfNoRules*/ true))
        Throw<std::overflow_error>("XRPAmount::operator*= overflow");
    drops_ *= rhs;
    return *this;
}

}  // namespace xrpl
