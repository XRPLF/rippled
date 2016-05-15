//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_WRITE_OP_HPP
#define BEAST_WEBSOCKET_IMPL_WRITE_OP_HPP

#include <beast/core/consuming_buffers.hpp>
#include <beast/core/prepare_buffers.hpp>
#include <beast/core/handler_alloc.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <algorithm>
#include <cassert>
#include <memory>

namespace beast {
namespace websocket {

// write a message
//
template<class NextLayer>
template<class Buffers, class Handler>
class stream<NextLayer>::write_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data : op
    {
        stream<NextLayer>& ws;
        consuming_buffers<Buffers> cb;
        Handler h;
        std::size_t remain;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            stream<NextLayer>& ws_, Buffers const& bs)
            : ws(ws_)
            , cb(bs)
            , h(std::forward<DeducedHandler>(h_))
            , remain(boost::asio::buffer_size(cb))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    explicit
    write_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(error_code ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Buffers, class Handler>
void
stream<NextLayer>::
write_op<Buffers, Handler>::
operator()(error_code ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    if(! ec)
    {
        switch(d.state)
        {
        case 0:
        {
            auto const n = std::min(
                d.remain, d.ws.wr_frag_size_);
            d.remain -= n;
            auto const fin = d.remain <= 0;
            if(fin)
                d.state = 99;
            auto const pb = prepare_buffers(n, d.cb);
            d.cb.consume(n);
            d.ws.async_write_frame(fin, pb, std::move(*this));
            return;
        }

        case 99:
            break;
        }
    }
    d.h(ec);
}

} // websocket
} // beast

#endif
