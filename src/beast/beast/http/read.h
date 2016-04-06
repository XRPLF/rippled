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

#ifndef BEAST_HTTP_READ_H_INCLUDED
#define BEAST_HTTP_READ_H_INCLUDED

#include <beast/http/parser.h>
#include <beast/http/type_check.h>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace beast {
namespace http {

/** Read a HTTP message from a stream.
*/
template<class SyncReadStream, class Streambuf, class Message>
void
read(SyncReadStream& stream,
    Streambuf& streambuf, parser<Message>& p)
{
    boost::system::error_code ec;
    read(stream, streambuf, p, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

/** Read a HTTP message from a stream.
*/
template<class SyncReadStream, class Streambuf, class Message>
void
read(SyncReadStream& stream, Streambuf& streambuf,
    parser<Message>& p, boost::system::error_code& ec);

/** Start reading a HTTP message from a stream asynchronously.
*/
template<class AsyncReadStream,
    class Streambuf, class Message, class CompletionToken>
auto
read(AsyncReadStream& stream, Streambuf& streambuf,
    parser<Message>& p, CompletionToken&& token);

} // http
} // beast

#include <beast/http/impl/read.ipp>

#endif
