#include <xrpl/protocol/MPTAmount.h>

namespace ripple {

MPTAmount&
MPTAmount::operator+=(MPTAmount const& other)
{
    value_ += other.value();
    return *this;
}

MPTAmount&
MPTAmount::operator-=(MPTAmount const& other)
{
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

}  // namespace ripple
