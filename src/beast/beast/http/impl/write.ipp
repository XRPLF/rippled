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

#ifndef BEAST_HTTP_WRITE_IPP_H_INCLUDED
#define BEAST_HTTP_WRITE_IPP_H_INCLUDED

#include <beast/http/resume_context.h>
#include <beast/asio/async_completion.h>
#include <beast/asio/bind_handler.h>
#include <beast/asio/handler_alloc.h>
#include <beast/asio/type_check.h>
#include <boost/asio/write.hpp>
#include <boost/logic/tribool.hpp>
#include <condition_variable>
#include <mutex>

namespace beast {
namespace http {

namespace detail {

template<class Stream, class Message, class Handler,
    bool isSimple>
class write_op;

//------------------------------------------------------------------------------

template<class Stream, class Message, class Handler>
class write_op<Stream, Message, Handler, true>
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        Stream& s;
        typename Message::body_type::writer w;
        Handler h;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Message const& m,
                Stream& s_)
            : s(s_)
            , w(m)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(write_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class Message, class Handler>
void
write_op<Stream, Message, Handler, true>::
    operator()(boost::system::error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
            d.state = 99;
            boost::asio::async_write(
                d.w.data(), std::move(*this));
            return ;
        }
    }
    d.h(ec);
}

//------------------------------------------------------------------------------

template<class Stream, class Message, class Handler>
class write_op<Stream, Message, Handler, false>
{
    using error_code =
        boost::system::error_code;

    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        Stream& s;
        typename Message::body_type::writer w;
        Handler h;
        resume_context resume;
        resume_context copy;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Message const& m,
                Stream& s_)
            : s(s_)
            , w(m)
            , h(std::forward<DeducedHandler>(h_))
        {
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h),
                std::forward<Args>(args)...))
    {
        auto& d = *d_;
        d.resume = {
            [self = *this]() mutable
            {
                auto& ios = self.d_->s.get_io_service();
                ios.dispatch(bind_handler(std::move(self),
                    error_code{}, 0));
            }};
        d.copy = d.resume;
        (*this)(error_code{}, 0);
    }

    explicit
    write_op(std::shared_ptr<data> d)
        : d_(std::move(d))
    {
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred);

    friend
    auto asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            allocate(size, op->d_->h);
    }

    friend
    auto asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return boost_asio_handler_alloc_helpers::
            deallocate(p, size, op->d_->h);
    }

    friend
    auto asio_handler_is_continuation(write_op* op)
    {
        return op->d_->state >= 1 ||
            boost_asio_handler_cont_helpers::
                is_continuation(op->d_->h);
    }

    template <class Function>
    friend
    auto asio_handler_invoke(Function&& f, write_op* op)
    {
        return boost_asio_handler_invoke_helpers::
            invoke(f, op->d_->h);
    }
};

template<class Stream, class Message, class Handler>
void
write_op<Stream, Message, Handler, false>::
    operator()(boost::system::error_code ec, std::size_t bytes_transferred)
{
    auto& d = *d_;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        case 1:
        {
            auto const result =
                d.w.prepare(std::move(d.copy));
            if(boost::indeterminate(result))
            {
                // suspend
                d.state = 0;
                d.copy = d.resume;
                return;
            }
            if(! result)
            {
                // call handler
                d.state = 99;
                break;
            }
            d.state = 1;
            boost::asio::async_write(d.s,
                d.w.data(), std::move(*this));
            return ;
        }
        }
    }
    d.h(ec);
    d.resume = {};
    d.clear = {};
}

} // detail

//------------------------------------------------------------------------------

namespace detail {

template<class SyncWriteStream,
    bool isRequest, class Body, class Allocator>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Allocator> const& m,
        boost::system::error_code& ec, std::true_type)
{
    typename Body::writer w(m);
    boost::asio::write(stream, w.data(), ec);
}

template<class SyncWriteStream,
    bool isRequest, class Body, class Allocator>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Allocator> const& msg,
        boost::system::error_code& ec, std::false_type)
{
    std::mutex m;
    std::condition_variable cv;
    typename Body::writer w(msg);
    bool ready = false;
    resume_context resume{
        [&]
        {
            std::lock_guard<std::mutex> lock(m);
            ready = true;
            cv.notify_one();
        }};
    auto copy = resume;
    for(;;)
    {
        auto result = w.prepare(std::move(copy));
        if(boost::indeterminate(result))
        {
            copy = resume;
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&]{ return ready; });
            ready = false;
            continue;
        }
        if(! result)
            break;
        boost::asio::write(
            stream, w.data(), ec);
        if(ec)
            return;
    }
}

} // detail

template<class SyncWriteStream,
    bool isRequest, class Body, class Allocator>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Allocator> const& msg,
        error_code& ec)
{
    detail::write(stream, msg, ec,
        std::bool_constant<Body::is_simple>{});
}

template<class AsyncWriteStream,
    bool isReq, class Body, class Allocator,
        class CompletionToken>
auto
async_write(AsyncWriteStream& stream,
    message<isReq, Body, Allocator> const& msg,
        CompletionToken&& token)
{
    static_assert(
        is_AsyncWriteStream<AsyncWriteStream>::value,
            "AsyncWriteStream requirements not met");
    beast::async_completion<CompletionToken,
        void(error_code)> completion(token);
    using message_type =
        message<isReq, Body, Allocator>;
    detail::write_op<AsyncWriteStream, message_type,
        decltype(completion.handler),
            message_type::is_simple>{
                completion.handler, stream, msg};
    return completion.result.get();
}

} // http
} // beast

#endif
