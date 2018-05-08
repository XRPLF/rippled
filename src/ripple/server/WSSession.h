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

#ifndef RIPPLE_SERVER_WSSESSION_H_INCLUDED
#define RIPPLE_SERVER_WSSESSION_H_INCLUDED

#include <ripple/server/Handoff.h>
#include <ripple/server/Port.h>
#include <ripple/server/Writer.h>
#include <beast/core/buffer_prefix.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/logic/tribool.hpp>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace ripple {

class WSMsg
{
public:
    WSMsg() = default;
    WSMsg(WSMsg const&) = delete;
    WSMsg& operator=(WSMsg const&) = delete;
    virtual ~WSMsg() = default;

    /** Retrieve message data.

        Returns a tribool indicating whether or not
        data is available, and a ConstBufferSequence
        representing the data.

        tribool values:
            maybe:      Data is not ready yet
            false:      Data is available
            true:       Data is available, and
                        it is the last chunk of bytes.

        Derived classes that do not know when the data
        ends (for example, when returning the output of a
        paged database query) may return `true` and an
        empty vector.
    */
    virtual
    std::pair<boost::tribool,
        std::vector<boost::asio::const_buffer>>
    prepare(std::size_t bytes,
        std::function<void(void)> resume) = 0;
};

template<class Streambuf>
class StreambufWSMsg : public WSMsg
{
    Streambuf sb_;
    std::size_t n_ = 0;

public:
    StreambufWSMsg(Streambuf&& sb)
        : sb_(std::move(sb))
    {
    }

    std::pair<boost::tribool,
        std::vector<boost::asio::const_buffer>>
    prepare(std::size_t bytes,
        std::function<void(void)>) override
    {
        if (sb_.size() == 0)
            return{true, {}};
        sb_.consume(n_);
        boost::tribool done;
        if (bytes < sb_.size())
        {
            n_ = bytes;
            done = false;
        }
        else
        {
            n_ = sb_.size();
            done = true;
        }
        auto const pb = beast::buffer_prefix(n_, sb_.data());
        std::vector<boost::asio::const_buffer> vb (
            std::distance(pb.begin(), pb.end()));
        std::copy(pb.begin(), pb.end(), std::back_inserter(vb));
        return{done, vb};
    }
};

struct WSSession
{
    std::shared_ptr<void> appDefined;

    virtual ~WSSession () = default;
    WSSession() = default;
    WSSession(WSSession const&) = delete;
    WSSession& operator=(WSSession const&) = delete;

    virtual
    void
    run() = 0;

    virtual
    Port const&
    port() const = 0;

    virtual
    http_request_type const&
    request() const = 0;

    virtual
    boost::asio::ip::tcp::endpoint const&
    remote_endpoint() const = 0;

    /** Send a WebSockets message. */
    virtual
    void
    send(std::shared_ptr<WSMsg> w) = 0;

    virtual
    void
    close() = 0;

    /** Indicate that the response is complete.
        The handler should call this when it has completed writing
        the response.
    */
    virtual
    void
    complete() = 0;
};

} // ripple

#endif
