//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/handler_concepts.hpp>

namespace beast {

namespace {
struct T
{
    void operator()(int);
};
}

static_assert(is_CompletionHandler<T, void(int)>::value, "");
static_assert(! is_CompletionHandler<T, void(void)>::value, "");

} // beast
