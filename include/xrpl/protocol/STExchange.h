#ifndef XRPL_PROTOCOL_STEXCHANGE_H_INCLUDED
#define XRPL_PROTOCOL_STEXCHANGE_H_INCLUDED

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Buffer.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/contract.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STObject.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ripple {

/** Convert between serialized type U and C++ type T. */
template <class U, class T>
struct STExchange;

template <class U, class T>
struct STExchange<STInteger<U>, T>
{
    explicit STExchange() = default;

    using value_type = U;

    static void
    get(std::optional<T>& t, STInteger<U> const& u)
    {
        t = u.value();
    }

    static std::unique_ptr<STInteger<U>>
    set(SField const& f, T const& t)
    {
        return std::make_unique<STInteger<U>>(f, t);
    }
};

template <>
struct STExchange<STBlob, Slice>
{
    explicit STExchange() = default;

    using value_type = Slice;

    static void
    get(std::optional<value_type>& t, STBlob const& u)
    {
        t.emplace(u.data(), u.size());
    }

    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Slice const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }
};

template <>
struct STExchange<STBlob, Buffer>
{
    explicit STExchange() = default;

    using value_type = Buffer;

    static void
    get(std::optional<Buffer>& t, STBlob const& u)
    {
        t.emplace(u.data(), u.size());
    }

    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Buffer const& t)
    {
        return std::make_unique<STBlob>(f, t.data(), t.size());
    }

    static std::unique_ptr<STBlob>
    set(TypedField<STBlob> const& f, Buffer&& t)
    {
        return std::make_unique<STBlob>(f, std::move(t));
    }
};

//------------------------------------------------------------------------------

/** Return the value of a field in an STObject as a given type. */
/** @{ */
template <class T, class U>
std::optional<T>
get(STObject const& st, TypedField<U> const& f)
{
    std::optional<T> t;
    STBase const* const b = st.peekAtPField(f);
    if (!b)
        return t;
    auto const id = b->getSType();
    if (id == STI_NOTPRESENT)
        return t;
    auto const u = dynamic_cast<U const*>(b);
    // This should never happen
    if (!u)
        Throw<std::runtime_error>("Wrong field type");
    STExchange<U, T>::get(t, *u);
    return t;
}

template <class U>
std::optional<typename STExchange<U, typename U::value_type>::value_type>
get(STObject const& st, TypedField<U> const& f)
{
    return get<typename U::value_type>(st, f);
}
/** @} */

/** Set a field value in an STObject. */
template <class U, class T>
void
set(STObject& st, TypedField<U> const& f, T&& t)
{
    st.set(STExchange<U, typename std::decay<T>::type>::set(
        f, std::forward<T>(t)));
}

/** Set a blob field using an init function. */
template <class Init>
void
set(STObject& st, TypedField<STBlob> const& f, std::size_t size, Init&& init)
{
    st.set(std::make_unique<STBlob>(f, size, init));
}

/** Set a blob field from data. */
template <class = void>
void
set(STObject& st,
    TypedField<STBlob> const& f,
    void const* data,
    std::size_t size)
{
    st.set(std::make_unique<STBlob>(f, data, size));
}

/** Remove a field in an STObject. */
template <class U>
void
erase(STObject& st, TypedField<U> const& f)
{
    st.makeFieldAbsent(f);
}

}  // namespace ripple

#endif
