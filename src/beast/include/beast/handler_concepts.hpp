//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HANDLER_CONCEPTS_HPP
#define BEAST_HANDLER_CONCEPTS_HPP

#include <beast/detail/is_call_possible.hpp>
#include <type_traits>

namespace beast {

/// Determine if `T` meets the requirements of @b `CompletionHandler`.
template<class T, class Signature>
#if GENERATING_DOCS
using is_CompletionHandler = std::integral_constant<bool, ...>;
#else
using is_CompletionHandler = std::integral_constant<bool,
    std::is_copy_constructible<typename std::decay<T>::type>::value &&
        detail::is_call_possible<T, Signature>::value>;
#endif
} // beast

#endif
