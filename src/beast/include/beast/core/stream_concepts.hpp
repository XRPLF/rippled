//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STREAM_CONCEPTS_HPP
#define BEAST_STREAM_CONCEPTS_HPP

#include <beast/core/detail/stream_concepts.hpp>
#include <type_traits>

namespace beast {

/// Determine if `T` has the `get_io_service` member.
template<class T>
#if GENERATING_DOCS
struct has_get_io_service : std::integral_constant<bool, ...>{};
#else
using has_get_io_service = typename detail::has_get_io_service<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `AsyncReadStream`.
template<class T>
#if GENERATING_DOCS
struct is_AsyncReadStream : std::integral_constant<bool, ...>{};
#else
using is_AsyncReadStream = typename detail::is_AsyncReadStream<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `AsyncWriteStream`.
template<class T>
#if GENERATING_DOCS
struct is_AsyncWriteStream : std::integral_constant<bool, ...>{};
#else
using is_AsyncWriteStream = typename detail::is_AsyncWriteStream<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `SyncReadStream`.
template<class T>
#if GENERATING_DOCS
struct is_SyncReadStream : std::integral_constant<bool, ...>{};
#else
using is_SyncReadStream = typename detail::is_SyncReadStream<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `SyncWriterStream`.
template<class T>
#if GENERATING_DOCS
struct is_SyncWriteStream : std::integral_constant<bool, ...>{};
#else
using is_SyncWriteStream = typename detail::is_SyncWriteStream<T>::type;
#endif

/// Determine if `T` meets the requirements of @b `AsyncStream`.
template<class T>
#if GENERATING_DOCS
struct is_AsyncStream : std::integral_constant<bool, ...>{};
#else
using is_AsyncStream = std::integral_constant<bool,
    is_AsyncReadStream<T>::value && is_AsyncWriteStream<T>::value>;
#endif

/// Determine if `T` meets the requirements of @b `SyncStream`.
template<class T>
#if GENERATING_DOCS
struct is_SyncStream : std::integral_constant<bool, ...>{};
#else
using is_SyncStream = std::integral_constant<bool,
    is_SyncReadStream<T>::value && is_SyncWriteStream<T>::value>;
#endif

} // beast

#endif
