//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_WRITE_DYNABUF_HPP
#define BEAST_DETAIL_WRITE_DYNABUF_HPP

#include <beast/core/buffer_concepts.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/lexical_cast.hpp>
#include <utility>

namespace beast {
namespace detail {

// detects string literals.
template<class T>
struct is_string_literal : std::integral_constant<bool,
    ! std::is_same<T, typename std::remove_extent<T>::type>::value &&
    std::is_same<char, typename std::remove_extent<T>::type>::value>
{
};

// `true` if a call to boost::asio::buffer(T const&) is possible
// note: we exclude string literals because boost::asio::buffer()
// will include the null terminator, which we don't want.
template<class T>
class is_BufferConvertible
{
    template<class U, class R = decltype(
        boost::asio::buffer(std::declval<U const&>()),
            std::true_type{})>
    static R check(int);
    template<class>
    static std::false_type check(...);
    using type = decltype(check<T>(0));
public:
    static bool const value = type::value &&
        ! is_string_literal<T>::value;
};

template<class DynamicBuffer>
void
write_dynabuf(DynamicBuffer& dynabuf,
    boost::asio::const_buffer const& buffer)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(buffer_size(buffer)),
            buffer));
}

template<class DynamicBuffer>
void
write_dynabuf(DynamicBuffer& dynabuf,
    boost::asio::mutable_buffer const& buffer)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(buffer_size(buffer)),
            buffer));
}

template<class DynamicBuffer, class T>
typename std::enable_if<
    is_BufferConvertible<T>::value &&
    ! std::is_convertible<T, boost::asio::const_buffer>::value &&
    ! std::is_convertible<T, boost::asio::mutable_buffer>::value
>::type
write_dynabuf(DynamicBuffer& dynabuf, T const& t)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    auto const buffers = boost::asio::buffer(t);
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(buffer_size(buffers)),
            buffers));
}

template<class DynamicBuffer, class Buffers>
typename std::enable_if<
    is_ConstBufferSequence<Buffers>::value &&
    ! is_BufferConvertible<Buffers>::value &&
    ! std::is_convertible<Buffers, boost::asio::const_buffer>::value &&
    ! std::is_convertible<Buffers, boost::asio::mutable_buffer>::value
>::type
write_dynabuf(DynamicBuffer& dynabuf, Buffers const& buffers)
{
    using boost::asio::buffer_copy;
    using boost::asio::buffer_size;
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(buffer_size(buffers)),
            buffers));
}

template<class DynamicBuffer, std::size_t N>
void
write_dynabuf(DynamicBuffer& dynabuf, const char (&s)[N])
{
    using boost::asio::buffer_copy;
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(N - 1),
            boost::asio::buffer(s, N - 1)));
}

template<class DynamicBuffer, class T>
typename std::enable_if<
    ! is_string_literal<T>::value &&
    ! is_ConstBufferSequence<T>::value &&
    ! is_BufferConvertible<T>::value &&
    ! std::is_convertible<T, boost::asio::const_buffer>::value &&
    ! std::is_convertible<T, boost::asio::mutable_buffer>::value
>::type
write_dynabuf(DynamicBuffer& dynabuf, T const& t)
{
    using boost::asio::buffer;
    using boost::asio::buffer_copy;
    auto const s = boost::lexical_cast<std::string>(t);
    dynabuf.commit(buffer_copy(
        dynabuf.prepare(s.size()), buffer(s)));
}

template<class DynamicBuffer, class T0, class T1, class... TN>
void
write_dynabuf(DynamicBuffer& dynabuf,
    T0 const& t0, T1 const& t1, TN const&... tn)
{
    write_dynabuf(dynabuf, t0);
    write_dynabuf(dynabuf, t1, tn...);
}

} // detail
} // beast

#endif
