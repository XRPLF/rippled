//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef BEAST_HTTP_RESUME_CONTEXT_H_INCLUDED
#define BEAST_HTTP_RESUME_CONTEXT_H_INCLUDED

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
