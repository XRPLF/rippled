//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_TYPE_TRAITS_HPP
#define BEAST_DETAIL_TYPE_TRAITS_HPP

#include <beast/core/error.hpp>
#include <boost/asio/basic_streambuf.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <string>

namespace beast {
namespace detail {

//
// utilities
//

template<class... Ts>
struct make_void
{
    using type = void;
};

template<class... Ts>
using void_t = typename make_void<Ts...>::type;

template<class T>
inline
void
accept_rv(T){}

template<class U>
std::size_t constexpr
max_sizeof()
{
    return sizeof(U);
}

template<class U0, class U1, class... Us>
std::size_t constexpr
max_sizeof()
{
    return
        max_sizeof<U0>() > max_sizeof<U1, Us...>() ?
        max_sizeof<U0>() : max_sizeof<U1, Us...>();
}

template<unsigned N, class T, class... Tn>
struct repeat_tuple_impl
{
    using type = typename repeat_tuple_impl<
        N - 1, T, T, Tn...>::type;
};

template<class T, class... Tn>
struct repeat_tuple_impl<0, T, Tn...>
{
    using type = std::tuple<T, Tn...>;
};

template<unsigned N, class T>
struct repeat_tuple
{
    using type =
        typename repeat_tuple_impl<N-1, T>::type;
};

template<class T>
struct repeat_tuple<0, T>
{
    using type = std::tuple<>;
};

template<class R, class C, class ...A>
auto
is_invocable_test(C&& c, int, A&& ...a)
    -> decltype(std::is_convertible<
        decltype(c(a...)), R>::value ||
            std::is_same<R, void>::value,
                std::true_type());

template<class R, class C, class ...A>
std::false_type
is_invocable_test(C&& c, long, A&& ...a);

/** Metafunction returns `true` if F callable as R(A...)

    Example:

    @code
        is_invocable<T, void(std::string)>
    @endcode
*/
/** @{ */
template<class C, class F>
struct is_invocable : std::false_type
{
};

template<class C, class R, class ...A>
struct is_invocable<C, R(A...)>
    : decltype(is_invocable_test<R>(
        std::declval<C>(), 1, std::declval<A>()...))
{
};
/** @} */

// for span
template<class T, class E, class = void>
struct is_contiguous_container: std::false_type {};

template<class T, class E>
struct is_contiguous_container<T, E, void_t<
    decltype(
        std::declval<std::size_t&>() = std::declval<T const&>().size(),
        std::declval<E*&>() = std::declval<T&>().data(),
        (void)0),
    typename std::enable_if<
        std::is_same<
            typename std::remove_cv<E>::type,
            typename std::remove_cv<
                typename std::remove_pointer<
                    decltype(std::declval<T&>().data())
                >::type
            >::type
        >::value
    >::type>>: std::true_type
{};

//------------------------------------------------------------------------------

//
// buffer concepts
//

// Types that meet the requirements,
// for use with std::declval only.
template<class BufferType>
struct BufferSequence
{
    using value_type = BufferType;
    using const_iterator = BufferType const*;
    ~BufferSequence();
    BufferSequence(BufferSequence const&) = default;
    const_iterator begin() const noexcept;
    const_iterator end() const noexcept;
};
using ConstBufferSequence =
    BufferSequence<boost::asio::const_buffer>;
using MutableBufferSequence =
    BufferSequence<boost::asio::mutable_buffer>;

template<class T, class B, class = void>
struct is_buffer_sequence : std::false_type {};

template<class T, class B>
struct is_buffer_sequence<T, B, void_t<decltype(
    std::declval<typename T::value_type>(),
    std::declval<typename T::const_iterator&>() =
        std::declval<T const&>().begin(),
    std::declval<typename T::const_iterator&>() =
        std::declval<T const&>().end(),
        (void)0)>> : std::integral_constant<bool,
    std::is_convertible<typename T::value_type, B>::value &&
#if 0
    std::is_base_of<std::bidirectional_iterator_tag,
        typename std::iterator_traits<
            typename T::const_iterator>::iterator_category>::value
#else
    // workaround:
    // boost::asio::detail::consuming_buffers::const_iterator
    // is not bidirectional
    std::is_base_of<std::forward_iterator_tag,
        typename std::iterator_traits<
            typename T::const_iterator>::iterator_category>::value
#endif
        >
{
};

#if 0
// workaround:
// boost::asio::detail::consuming_buffers::const_iterator
// is not bidirectional
template<class Buffer, class Buffers, class B>
struct is_buffer_sequence<
    boost::asio::detail::consuming_buffers<Buffer, Buffers>>
        : std::true_type
{
};     
#endif

template<class B1, class... Bn>
struct is_all_const_buffer_sequence
    : std::integral_constant<bool,
        is_buffer_sequence<B1, boost::asio::const_buffer>::value &&
        is_all_const_buffer_sequence<Bn...>::value>
{
};

template<class B1>
struct is_all_const_buffer_sequence<B1>
    : is_buffer_sequence<B1, boost::asio::const_buffer>
{
};

template<class... Bn>
struct common_buffers_type
{
    using type = typename std::conditional<
        std::is_convertible<std::tuple<Bn...>,
            typename repeat_tuple<sizeof...(Bn),
                boost::asio::mutable_buffer>::type>::value,
                    boost::asio::mutable_buffer,
                        boost::asio::const_buffer>::type;
};

// Types that meet the requirements,
// for use with std::declval only.
struct StreamHandler
{
    StreamHandler(StreamHandler const&) = default;
    void operator()(error_code ec, std::size_t);
};
using ReadHandler = StreamHandler;
using WriteHandler = StreamHandler;

} // detail
} // beast

#endif
