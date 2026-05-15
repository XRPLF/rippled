#pragma once

namespace beast::detail {

// Extracts the key portion of value
template <bool MaybeMap>
struct AgedAssociativeContainerExtractT
{
    explicit AgedAssociativeContainerExtractT() = default;

    template <class Value>
    decltype(Value::first) const&
    operator()(Value const& value) const
    {
        return value.first;
    }
};

template <>
struct AgedAssociativeContainerExtractT<false>
{
    explicit AgedAssociativeContainerExtractT() = default;

    template <class Value>
    Value const&
    operator()(Value const& value) const
    {
        return value;  // NOLINT(bugprone-return-const-ref-from-parameter)
    }
};

}  // namespace beast::detail
