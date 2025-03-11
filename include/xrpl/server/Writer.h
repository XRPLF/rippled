//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_SERVER_WRITER_H_INCLUDED
#define RIPPLE_SERVER_WRITER_H_INCLUDED

#include <boost/asio/buffer.hpp>
#include <functional>
#include <vector>

namespace ripple {

class Writer
{
public:
    virtual ~Writer() = default;

    /** Returns `true` if there is no more data to pull. */
    virtual bool
    complete() = 0;

    /** Removes bytes from the input sequence.

        Can be called with 0.
    */
    virtual void
    consume(std::size_t bytes) = 0;

    /** Add data to the input sequence.
        @param bytes A hint to the number of bytes desired.
        @param resume A functor to later resume execution.
        @return `true` if the writer is ready to provide more data.
    */
    virtual bool
    prepare(std::size_t bytes, std::function<void(void)> resume) = 0;

    /** Returns a ConstBufferSequence representing the input sequence. */
    virtual std::vector<boost::asio::const_buffer>
    data() = 0;
};

}  // namespace ripple

#endif
