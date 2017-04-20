//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/buffer_concepts.hpp>

namespace beast {

namespace {
struct T
{
};
}

static_assert(is_ConstBufferSequence<detail::ConstBufferSequence>::value, "");
static_assert(! is_ConstBufferSequence<T>::value, "");

static_assert(is_MutableBufferSequence<detail::MutableBufferSequence>::value, "");
static_assert(! is_MutableBufferSequence<T>::value, "");

} // beast
