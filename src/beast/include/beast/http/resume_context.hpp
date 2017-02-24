//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_RESUME_CONTEXT_HPP
#define BEAST_HTTP_RESUME_CONTEXT_HPP

#include <functional>

namespace beast {
namespace http {

/** A functor that resumes a write operation.

    An rvalue reference to an object of this type is provided by the
    write implementation to the `writer` associated with the body of
    a message being sent.

    If it is desired that the `writer` suspend the write operation (for
    example, to wait until data is ready), it can take ownership of
    the resume context using a move. Then, it returns `boost::indeterminate`
    to indicate that the write operation should suspend. Later, the calling
    code invokes the resume function and the write operation continues
    from where it left off.
*/
using resume_context = std::function<void(void)>;

} // http
} // beast

#endif
