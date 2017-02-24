//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_BUFFER_CONCEPTS_HPP
#define BEAST_BUFFER_CONCEPTS_HPP

#include <beast/core/detail/buffer_concepts.hpp>
#include <boost/asio/buffer.hpp>
#include <type_traits>

namespace beast {

/// Determine if `T` meets the requirements of @b `BufferSequence`.
template<class T, class BufferType>
#if GENERATING_DOCS
struct is_BufferSequence : std::integral_constant<bool, ...>
#else
struct is_BufferSequence : detail::is_BufferSequence<T, BufferType>::type
#endif
{
};

/// Determine if `T` meets the requirements of @b `ConstBufferSequence`.
template<class T>
#if GENERATING_DOCS
struct is_ConstBufferSequence : std::integral_constant<bool, ...>
#else
struct is_ConstBufferSequence :
    is_BufferSequence<T, boost::asio::const_buffer>
#endif
{
};

/// Determine if `T` meets the requirements of @b `DynamicBuffer`.
template<class T>
#if GENERATING_DOCS
struct is_DynamicBuffer : std::integral_constant<bool, ...>
#else
struct is_DynamicBuffer : detail::is_DynamicBuffer<T>::type
#endif
{
};

/// Determine if `T` meets the requirements of @b `MutableBufferSequence`.
template<class T>
#if GENERATING_DOCS
struct is_MutableBufferSequence : std::integral_constant<bool, ...>
#else
struct is_MutableBufferSequence :
    is_BufferSequence<T, boost::asio::mutable_buffer>
#endif
{
};

} // beast

#endif
