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

#ifndef BEAST_WSPROTO_READ_OP_H_INCLUDED
#define BEAST_WSPROTO_READ_OP_H_INCLUDED

namespace beast {
namespace wsproto {

// read non-control frame payload
//
template<class Stream>
template<class Buffers, class Handler>
class socket<Stream>::read_op
{
    struct data
    {
        socket<Stream>& ws;
        Buffers bs;
        Handler h;
        detail::fh_buffer fb;
        int state = 0;

        template<class DeducedBuffers, class DeducedHandler>
        data(socket<Stream>& ws_,
                DeducedBuffers&& bs_, DeducedHandler&& h_)
            : ws(ws_)
            , bs(std::forward<DeducedBuffers>(bs_))
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    read_op(read_op&&) = default;
    read_op(read_op const&) = default;

    template<class... Args>
    explicit
    read_op(Args&&... args)
        : d_(std::make_shared<data>(
            std::forward<Args>(args)...))
    {
    }

    void operator()()
    {
        using namespace boost::asio;
        auto& d = *d_;
        if(d.ws.rd_need_ == 0)
            throw std::logic_error("bad read state");
        d.ws.rd_active_ = true;
        // read frame payload
        boost::asio::async_read(d.ws.stream_,
            d.bs, detail::at_most(d.ws.rd_need_),
                std::move(*this));
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
            // got frame payload
            case 0:
                d.ws.rd_need_ -= bytes_transferred;
                if(d.ws.rd_fh_.mask)
                    detail::mask_inplace(d.bs,
                        d.ws.rd_key_);
                break;
            }
        }
        d.ws.rd_active_ = false;
        d.h(ec, bytes_transferred);
    }

    friend
    auto asio_handler_allocate(
        std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, read_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(read_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, read_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

} // wsproto
} // beast

#endif
