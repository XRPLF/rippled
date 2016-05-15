//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_IMPL_READ_OP_HPP
#define BEAST_WEBSOCKET_IMPL_READ_OP_HPP

#include <beast/core/handler_alloc.hpp>
#include <memory>

namespace beast {
namespace websocket {

// read an entire message
//
template<class NextLayer>
template<class Streambuf, class Handler>
class stream<NextLayer>::read_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        stream<NextLayer>& ws;
        opcode& op;
        Streambuf& sb;
        Handler h;
        frame_info fi;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_,
            stream<NextLayer>& ws_, opcode& op_,
                Streambuf& sb_)
            : ws(ws_)
            , op(op_)
            , sb(sb_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class DeducedHandler, class... Args>
    read_op(DeducedHandler&& h,
            stream<NextLayer>& ws, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), ws,
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, false);
    }

    void operator()(
        error_code const& ec, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    bool asio_handler_is_continuation(read_op* op)
    {
        return op->d_->cont;
    }

    template <class Function>
    friend
    void asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class NextLayer>
template<class Streambuf, class Handler>
void
stream<NextLayer>::read_op<Streambuf, Handler>::
operator()(error_code const& ec, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec)
    {
        switch(d.state)
        {
        case 0:
            // read payload
            d.state = 1;
#if 0
            // VFALCO This causes dereference of null, because
            //        the handler is moved from the data block
            //        before asio_handler_deallocate is called.
            d.ws.async_read_frame(
                d.fi, d.sb, std::move(*this));
#else
            d.ws.async_read_frame(d.fi, d.sb, *this);
#endif
            return;

        // got payload
        case 1:
            d.op = d.fi.op;
            if(d.fi.fin)
                goto upcall;
            d.state = 0;
            break;
        }
    }
upcall:
    d.h(ec);
}

} // websocket
} // beast

#endif
