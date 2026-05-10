#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/basics/TraceLog.h>

namespace xrpl {

MPTAmount&
MPTAmount::operator+=(MPTAmount const& other)
{
    TRACE_FUNC();
    value_ += other.value();
    return *this;
}

MPTAmount&
MPTAmount::operator-=(MPTAmount const& other)
{
    TRACE_FUNC();
    value_ -= other.value();
    return *this;
}

MPTAmount
MPTAmount::operator-() const
{
    TRACE_FUNC();
    return MPTAmount{-value_};
}

bool
MPTAmount::operator==(MPTAmount const& other) const
{
    TRACE_FUNC();
    return value_ == other.value_;
}

bool
MPTAmount::operator==(value_type other) const
{
    TRACE_FUNC();
    return value_ == other;
}

bool
MPTAmount::operator<(MPTAmount const& other) const
{
    TRACE_FUNC();
    return value_ < other.value_;
}

MPTAmount
MPTAmount::minPositiveAmount()
{
    TRACE_FUNC();
    return MPTAmount{1};
}

}  // namespace xrpl
