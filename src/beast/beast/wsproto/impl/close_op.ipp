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

#ifndef BEAST_WSPROTO_CLOSE_OP_H_INCLUDED
#define BEAST_WSPROTO_CLOSE_OP_H_INCLUDED

namespace beast {
namespace wsproto {

// send the close message and wait for the response
//
template<class Stream>
template<class Handler>
class socket<Stream>::close_op
{
    struct data
    {
        socket<Stream>& ws;
        Handler h;
        int state = 0;

        template<class DeducedHandler>
        data(socket<Stream>& ws_, DeducedHandler&& h_)
            : ws(ws_)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    close_op(close_op&&) = default;
    close_op(close_op const&) = default;

    template<class... Args>
    explicit
    close_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        auto& d = *d_;
    }

    void operator()(error_code ec,
        std::size_t bytes_transferred)
    {
        auto& d = *d_;
        using namespace boost::asio;
        if(! ec)
        {
            switch(d.state)
            {
            case 0:
                break;
            }
        }
        d.h(ec);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, close_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(close_op* op)
    {
        return op->d_.state >= 0 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, close_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
