#include <xrpl/protocol/MPTAmount.h>

namespace xrpl {

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
    // Negate in unsigned domain to avoid UB when value_ == INT64_MIN
    return MPTAmount{static_cast<value_type>(-static_cast<std::uint64_t>(value_))};
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
