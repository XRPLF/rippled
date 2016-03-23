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

#ifndef BEAST_WSPROTO_ACCEPT_OP_H_INCLUDED
#define BEAST_WSPROTO_ACCEPT_OP_H_INCLUDED

#include <beast/wsproto/detail/handler_alloc.h>
#include <boost/optional.hpp>
#include <cassert>

namespace beast {
namespace wsproto {

// read and respond to an upgrade request
//
template<class Stream>
template<class Handler>
class socket<Stream>::accept_op
{
    struct data
    {
        using streambuf_type = beast::asio::streambuf;

        socket<Stream>& ws;
        http::message m;
        Handler h;
        error_code ec;
        boost::asio::streambuf rb; // VFALCO beast?
        boost::asio::streambuf wb; // VFALCO beast?
        boost::optional<http::parser> p;
        std::string body;
        int state;

        template<class DeducedHandler, class ConstBuffers>
        data(DeducedHandler&& h_,
            socket<Stream>& ws_, ConstBuffers&& bs)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
        {
            using namespace boost::asio;
            rb.commit(buffer_copy(rb.prepare(
                buffer_size(bs)), bs));
            p.emplace(
                [&](void const* data, std::size_t len)
                {
                    auto begin =
                        reinterpret_cast<char const*>(data);
                    auto end = begin + len;
                    body.append(begin, end);
                }, m, true);
        }

        template<class DeducedHandler, class DeducedMessage>
        data(DeducedHandler&& h_, socket<Stream>& ws_,
                DeducedMessage&& m_, void const*)
            : ws(ws_)
            , m(std::forward<DeducedMessage>(m_))
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    accept_op(accept_op&&) = default;
    accept_op(accept_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    accept_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(
            detail::handler_alloc<int, Handler>{h},
                std::forward<DeducedHandler>(h),
                    std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        auto& d = *d_;
        if(d.p)
        {
            d.state = 0;
            return boost::asio::async_read_until(
                d.ws.stream_, d.rb, "\r\n\r\n",
                    std::move(*this));
        }
        d.state = 1;
        (*this)(error_code{}, 0);
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        using namespace boost::asio;
        auto& d = *d_;
        for(;! ec && d.state != 99;)
        {
            switch(d.state)
            {
            // got request data
            case 0:
            {
                d.rb.commit(bytes_transferred);
                auto result = d.p->write(d.rb.data());
                if((ec = result.first))
                    break;
                assert(d.p->complete());
                d.rb.consume(result.second);
            }

            // got parsed request
            case 1:
                ec = d.ws.do_accept(d.m);
                if(ec)
                {
                    d.ec = ec;
                    d.ws.write_error(d.wb, ec);
                    d.state = 2;
                    boost::asio::async_write(d.ws.stream_,
                        d.wb.data(), std::move(*this));
                    return;
                }
                d.ws.write_response(d.wb, d.m);
                d.state = 3;
                boost::asio::async_write(d.ws.stream_,
                    d.wb.data(), std::move(*this));
                return;

            //----------------------------------------------

            // sent response (error)
            case 2:
                // call handler
                d.state = 99;
                ec = d.ec;
                break;

            // sent response (success)
            case 3:
                // call handler
                d.state = 99;
                d.ws.role_ = role_type::server;
                break;
            }
        }
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, accept_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(accept_op* op)
    {
        return op->d_->state >= 2 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, accept_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
