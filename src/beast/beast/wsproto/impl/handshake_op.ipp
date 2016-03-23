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

#ifndef BEAST_WSPROTO_HANDSHAKE_OP_H_INCLUDED
#define BEAST_WSPROTO_HANDSHAKE_OP_H_INCLUDED

#include <beast/wsproto/detail/handler_alloc.h>

namespace beast {
namespace wsproto {

// send the upgrade request and process the response
//
template<class Stream>
template<class Handler>
class socket<Stream>::handshake_op
{
    struct data
    {
        socket<Stream>& ws;
        Handler h;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, socket<Stream>& ws_)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    handshake_op(handshake_op&&) = default;
    handshake_op(handshake_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    handshake_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(
            detail::handler_alloc<int, Handler>{h},
                std::forward<DeducedHandler>(h),
                    std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        auto& d = *d_;
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
            case 0:
                // call handler
                d.state = 99;
                break;
            }
        }
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, handshake_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(handshake_op* op)
    {
        return op->d_.state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, handshake_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
