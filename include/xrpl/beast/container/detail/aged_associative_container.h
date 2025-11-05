#ifndef BEAST_CONTAINER_DETAIL_AGED_ASSOCIATIVE_CONTAINER_H_INCLUDED
#define BEAST_CONTAINER_DETAIL_AGED_ASSOCIATIVE_CONTAINER_H_INCLUDED

namespace beast {
namespace detail {

// Extracts the key portion of value
template <bool maybe_map>
struct aged_associative_container_extract_t
{
    explicit aged_associative_container_extract_t() = default;

    template <class Value>
    decltype(Value::first) const&
    operator()(Value const& value) const
    {
        return value.first;
    }
};

template <>
struct aged_associative_container_extract_t<false>
{
    explicit aged_associative_container_extract_t() = default;

    template <class Value>
    Value const&
    operator()(Value const& value) const
    {
        return value;
    }
};

}  // namespace detail
}  // namespace beast

#endif
