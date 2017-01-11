//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_BUFFER_CONCEPTS_HPP
#define BEAST_DETAIL_BUFFER_CONCEPTS_HPP

#include <boost/asio/buffer.hpp>
#include <iterator>
#include <type_traits>

namespace beast {
namespace detail {

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

template<class T, class BufferType>
class is_BufferSequence
{
    template<class U, class R = std::is_convertible<
        typename U::value_type, BufferType> >
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    template<class U, class R = std::is_base_of<
    #if 0
        std::bidirectional_iterator_tag,
            typename std::iterator_traits<
                typename U::const_iterator>::iterator_category>>
    #else
        // workaround:
        // boost::asio::detail::consuming_buffers::const_iterator
        // is not bidirectional
        std::forward_iterator_tag,
            typename std::iterator_traits<
                typename U::const_iterator>::iterator_category>>
    #endif
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    template<class U, class R = typename
        std::is_convertible<decltype(
            std::declval<U>().begin()),
                typename U::const_iterator>::type>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

    template<class U, class R = typename std::is_convertible<decltype(
        std::declval<U>().end()),
            typename U::const_iterator>::type>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<T>(0));

public:
    using type = std::integral_constant<bool,
        std::is_copy_constructible<T>::value &&
        std::is_destructible<T>::value &&
        type1::value && type2::value &&
        type3::value && type4::value>;
};

template<class B1, class... Bn>
struct is_all_ConstBufferSequence
    : std::integral_constant<bool,
        is_BufferSequence<B1, boost::asio::const_buffer>::type::value &&
        is_all_ConstBufferSequence<Bn...>::value>
{
};

template<class B1>
struct is_all_ConstBufferSequence<B1>
    : is_BufferSequence<B1, boost::asio::const_buffer>::type
{
};

template<class T>
class is_DynamicBuffer
{
    // size()
    template<class U, class R = std::is_convertible<decltype(
        std::declval<U const>().size()), std::size_t>>
    static R check1(int);
    template<class>
    static std::false_type check1(...);
    using type1 = decltype(check1<T>(0));

    // max_size()
    template<class U, class R = std::is_convertible<decltype(
        std::declval<U const>().max_size()), std::size_t>>
    static R check2(int);
    template<class>
    static std::false_type check2(...);
    using type2 = decltype(check2<T>(0));

    // capacity()
    template<class U, class R = std::is_convertible<decltype(
        std::declval<U const>().capacity()), std::size_t>>
    static R check3(int);
    template<class>
    static std::false_type check3(...);
    using type3 = decltype(check3<T>(0));

    // data()
    template<class U, class R = std::integral_constant<
        bool, is_BufferSequence<decltype(
            std::declval<U const>().data()),
                boost::asio::const_buffer>::type::value>>
    static R check4(int);
    template<class>
    static std::false_type check4(...);
    using type4 = decltype(check4<T>(0));

    // prepare()
    template<class U, class R = std::integral_constant<
        bool, is_BufferSequence<decltype(
            std::declval<U>().prepare(1)),
                boost::asio::mutable_buffer>::type::value>>
    static R check5(int);
    template<class>
    static std::false_type check5(...);
    using type5 = decltype(check5<T>(0));

    // commit()
    template<class U, class R = decltype(
        std::declval<U>().commit(1), std::true_type{})>
    static R check6(int);
    template<class>
    static std::false_type check6(...);
    using type6 = decltype(check6<T>(0));

    // consume
    template<class U, class R = decltype(
        std::declval<U>().consume(1), std::true_type{})>
    static R check7(int);
    template<class>
    static std::false_type check7(...);
    using type7 = decltype(check7<T>(0));

public:
    using type = std::integral_constant<bool,
        type1::value
        && type2::value
        //&& type3::value // Networking TS
        && type4::value
        && type5::value
        && type6::value
        && type7::value
    >;
};

} // detail
} // beast

#endif
