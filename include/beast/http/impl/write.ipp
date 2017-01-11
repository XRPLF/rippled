//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_WRITE_IPP
#define BEAST_HTTP_IMPL_WRITE_IPP

#include <beast/http/concepts.hpp>
#include <beast/http/resume_context.hpp>
#include <beast/http/chunk_encode.hpp>
#include <beast/core/buffer_cat.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/buffer_concepts.hpp>
#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>
#include <beast/core/stream_concepts.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/core/write_dynabuf.hpp>
#include <beast/core/detail/sync_ostream.hpp>
#include <boost/asio/write.hpp>
#include <boost/logic/tribool.hpp>
#include <condition_variable>
#include <mutex>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace beast {
namespace http {

namespace detail {

template<class DynamicBuffer, class Fields>
void
write_start_line(DynamicBuffer& dynabuf,
    header<true, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    write(dynabuf, msg.method);
    write(dynabuf, " ");
    write(dynabuf, msg.url);
    switch(msg.version)
    {
    case 10:
        write(dynabuf, " HTTP/1.0\r\n");
        break;
    case 11:
        write(dynabuf, " HTTP/1.1\r\n");
        break;
    }
}

template<class DynamicBuffer, class Fields>
void
write_start_line(DynamicBuffer& dynabuf,
    header<false, Fields> const& msg)
{
    BOOST_ASSERT(msg.version == 10 || msg.version == 11);
    switch(msg.version)
    {
    case 10:
        write(dynabuf, "HTTP/1.0 ");
        break;
    case 11:
        write(dynabuf, "HTTP/1.1 ");
        break;
    }
    write(dynabuf, msg.status);
    write(dynabuf, " ");
    write(dynabuf, msg.reason);
    write(dynabuf, "\r\n");
}

template<class DynamicBuffer, class FieldSequence>
void
write_fields(DynamicBuffer& dynabuf, FieldSequence const& fields)
{
    static_assert(is_DynamicBuffer<DynamicBuffer>::value,
        "DynamicBuffer requirements not met");
    //static_assert(is_FieldSequence<FieldSequence>::value,
    //    "FieldSequence requirements not met");
    for(auto const& field : fields)
    {
        write(dynabuf, field.name());
        write(dynabuf, ": ");
        write(dynabuf, field.value());
        write(dynabuf, "\r\n");
    }
}

} // detail

//------------------------------------------------------------------------------

namespace detail {

template<class Stream, class Handler>
class write_streambuf_op
{
    struct data
    {
        bool cont;
        Stream& s;
        streambuf sb;
        int state = 0;

        data(Handler& handler, Stream& s_,
                streambuf&& sb_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , s(s_)
            , sb(std::move(sb_))
        {
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_streambuf_op(write_streambuf_op&&) = default;
    write_streambuf_op(write_streambuf_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_streambuf_op(DeducedHandler&& h, Stream& s,
            Args&&... args)
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h),
                s, std::forward<Args>(args)...))
    {
        (*this)(error_code{}, 0, false);
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_streambuf_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_streambuf_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(write_streambuf_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_streambuf_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class Stream, class Handler>
void
write_streambuf_op<Stream, Handler>::
operator()(error_code ec, std::size_t, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            d.state = 99;
            boost::asio::async_write(d.s,
                d.sb.data(), std::move(*this));
            return;
        }
        }
    }
    d_.invoke(ec);
}

} // detail

template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncWriteStream,
    bool isRequest, class Fields>
void
write(SyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        error_code& ec)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    streambuf sb;
    detail::write_start_line(sb, msg);
    detail::write_fields(sb, msg.fields);
    beast::write(sb, "\r\n");
    boost::asio::write(stream, sb.data(), ec);
}

template<class AsyncWriteStream,
    bool isRequest, class Fields,
        class WriteHandler>
typename async_completion<
    WriteHandler, void(error_code)>::result_type
async_write(AsyncWriteStream& stream,
    header<isRequest, Fields> const& msg,
        WriteHandler&& handler)
{
    static_assert(is_AsyncWriteStream<AsyncWriteStream>::value,
        "AsyncWriteStream requirements not met");
    beast::async_completion<WriteHandler,
        void(error_code)> completion(handler);
    streambuf sb;
    detail::write_start_line(sb, msg);
    detail::write_fields(sb, msg.fields);
    beast::write(sb, "\r\n");
    detail::write_streambuf_op<AsyncWriteStream,
        decltype(completion.handler)>{
            completion.handler, stream, std::move(sb)};
    return completion.result.get();
}

//------------------------------------------------------------------------------

namespace detail {

template<bool isRequest, class Body, class Fields>
struct write_preparation
{
    message<isRequest, Body, Fields> const& msg;
    typename Body::writer w;
    streambuf sb;
    bool chunked;
    bool close;

    explicit
    write_preparation(
            message<isRequest, Body, Fields> const& msg_)
        : msg(msg_)
        , w(msg)
        , chunked(token_list{
            msg.fields["Transfer-Encoding"]}.exists("chunked"))
        , close(token_list{
            msg.fields["Connection"]}.exists("close") ||
                (msg.version < 11 && ! msg.fields.exists(
                    "Content-Length")))
    {
    }

    void
    init(error_code& ec)
    {
        w.init(ec);
        if(ec)
            return;
  
        write_start_line(sb, msg);
        write_fields(sb, msg.fields);
        beast::write(sb, "\r\n");
    }
};

template<class Stream, class Handler,
    bool isRequest, class Body, class Fields>
class write_op
{
    struct data
    {
        bool cont;
        Stream& s;
        // VFALCO How do we use handler_alloc in write_preparation?
        write_preparation<
            isRequest, Body, Fields> wp;
        resume_context resume;
        resume_context copy;
        int state = 0;

        data(Handler& handler, Stream& s_,
                message<isRequest, Body, Fields> const& m_)
            : cont(beast_asio_helpers::
                is_continuation(handler))
            , s(s_)
            , wp(m_)
        {
        }
    };

    class writef0_lambda
    {
        write_op& self_;

    public:
        explicit
        writef0_lambda(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers) const
        {
            auto& d = *self_.d_;
            // write header and body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.sb.data(),
                        chunk_encode(false, buffers)),
                            std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.sb.data(),
                        buffers), std::move(self_));
        }
    };

    class writef_lambda
    {
        write_op& self_;

    public:
        explicit
        writef_lambda(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers) const
        {
            auto& d = *self_.d_;
            // write body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    chunk_encode(false, buffers),
                        std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffers, std::move(self_));
        }
    };

    handler_ptr<data, Handler> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(make_handler_ptr<data, Handler>(
            std::forward<DeducedHandler>(h), s,
                std::forward<Args>(args)...))
    {
        auto& d = *d_;
        auto sp = d_;
        d.resume = {
            [sp]() mutable
            {
                write_op self{std::move(sp)};
                self.d_->cont = false;
                auto& ios = self.d_->s.get_io_service();
                ios.dispatch(bind_handler(std::move(self),
                    error_code{}, 0, false));
            }};
        d.copy = d.resume;
        (*this)(error_code{}, 0, false);
    }

    explicit
    write_op(handler_ptr<data, Handler> d)
        : d_(std::move(d))
    {
    }

    void
    operator()(error_code ec,
        std::size_t bytes_transferred, bool again = true);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_op* op)
    {
        return beast_asio_helpers::
            allocate(size, op->d_.handler());
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_op* op)
    {
        return beast_asio_helpers::
            deallocate(p, size, op->d_.handler());
    }

    friend
    bool asio_handler_is_continuation(write_op* op)
    {
        return op->d_->cont;
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_op* op)
    {
        return beast_asio_helpers::
            invoke(f, op->d_.handler());
    }
};

template<class Stream, class Handler,
    bool isRequest, class Body, class Fields>
void
write_op<Stream, Handler, isRequest, Body, Fields>::
operator()(error_code ec, std::size_t, bool again)
{
    auto& d = *d_;
    d.cont = d.cont || again;
    while(! ec && d.state != 99)
    {
        switch(d.state)
        {
        case 0:
        {
            d.wp.init(ec);
            if(ec)
            {
                // call handler
                d.state = 99;
                d.s.get_io_service().post(bind_handler(
                    std::move(*this), ec, 0, false));
                return;
            }
            d.state = 1;
            break;
        }

        case 1:
        {
            boost::tribool const result = d.wp.w.write(
                std::move(d.copy), ec, writef0_lambda{*this});
            if(ec)
            {
                // call handler
                d.state = 99;
                d.s.get_io_service().post(bind_handler(
                    std::move(*this), ec, false));
                return;
            }
            if(boost::indeterminate(result))
            {
                // suspend
                d.copy = d.resume;
                return;
            }
            if(result)
                d.state = d.wp.chunked ? 4 : 5;
            else
                d.state = 2;
            return;
        }

        // sent header and body
        case 2:
            d.wp.sb.consume(d.wp.sb.size());
            d.state = 3;
            break;

        case 3:
        {
            boost::tribool result = d.wp.w.write(
                std::move(d.copy), ec, writef_lambda{*this});
            if(ec)
            {
                // call handler
                d.state = 99;
                break;
            }
            if(boost::indeterminate(result))
            {
                // suspend
                d.copy = d.resume;
                return;
            }
            if(result)
                d.state = d.wp.chunked ? 4 : 5;
            else
                d.state = 2;
            return;
        }

        case 4:
            // VFALCO Unfortunately the current interface to the
            //        Writer concept prevents us from coalescing the
            //        final body chunk with the final chunk delimiter.
            //
            // write final chunk
            d.state = 5;
            boost::asio::async_write(d.s,
                chunk_encode_final(), std::move(*this));
            return;

        case 5:
            if(d.wp.close)
            {
                // VFALCO TODO Decide on an error code
                ec = boost::asio::error::eof;
            }
            d.state = 99;
            break;
        }
    }
    d.copy = {};
    d.resume = {};
    d_.invoke(ec);
}

template<class SyncWriteStream, class DynamicBuffer>
class writef0_lambda
{
    DynamicBuffer const& sb_;
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef0_lambda(SyncWriteStream& stream,
            DynamicBuffer const& sb, bool chunked, error_code& ec)
        : sb_(sb)
        , stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers) const
    {
        // write header and body
        if(chunked_)
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), chunk_encode(false, buffers)), ec_);
        else
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), buffers), ec_);
    }
};

template<class SyncWriteStream>
class writef_lambda
{
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef_lambda(SyncWriteStream& stream,
            bool chunked, error_code& ec)
        : stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers) const
    {
        // write body
        if(chunked_)
            boost::asio::write(stream_,
                chunk_encode(false, buffers), ec_);
        else
            boost::asio::write(stream_, buffers, ec_);
    }
};

} // detail

template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        throw system_error{ec};
}

template<class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
write(SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        error_code& ec)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    detail::write_preparation<isRequest, Body, Fields> wp(msg);
    wp.init(ec);
    if(ec)
        return;
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    resume_context resume{
        [&]
        {
            std::lock_guard<std::mutex> lock(m);
            ready = true;
            cv.notify_one();
        }};
    auto copy = resume;
    boost::tribool result =
        wp.w.write(std::move(copy), ec,
            detail::writef0_lambda<SyncWriteStream,
                decltype(wp.sb)>{stream,
                    wp.sb, wp.chunked, ec});
    if(ec)
        return;
    if(boost::indeterminate(result))
    {
        copy = resume;
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&]{ return ready; });
            ready = false;
        }
        boost::asio::write(stream, wp.sb.data(), ec);
        if(ec)
            return;
        result = false;
    }
    wp.sb.consume(wp.sb.size());
    if(! result)
    {
        detail::writef_lambda<SyncWriteStream> wf{
            stream, wp.chunked, ec};
        for(;;)
        {
            result = wp.w.write(std::move(copy), ec, wf);
            if(ec)
                return;
            if(result)
                break;
            if(! result)
                continue;
            copy = resume;
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&]{ return ready; });
            ready = false;
        }
    }
    if(wp.chunked)
    {
        // VFALCO Unfortunately the current interface to the
        //        Writer concept prevents us from using coalescing the
        //        final body chunk with the final chunk delimiter.
        //
        // write final chunk
        boost::asio::write(stream, chunk_encode_final(), ec);
        if(ec)
            return;
    }
    if(wp.close)
    {
        // VFALCO TODO Decide on an error code
        ec = boost::asio::error::eof;
    }
}

template<class AsyncWriteStream,
    bool isRequest, class Body, class Fields,
        class WriteHandler>
typename async_completion<
    WriteHandler, void(error_code)>::result_type
async_write(AsyncWriteStream& stream,
    message<isRequest, Body, Fields> const& msg,
        WriteHandler&& handler)
{
    static_assert(is_AsyncWriteStream<AsyncWriteStream>::value,
        "AsyncWriteStream requirements not met");
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    beast::async_completion<WriteHandler,
        void(error_code)> completion(handler);
    detail::write_op<AsyncWriteStream, decltype(completion.handler),
        isRequest, Body, Fields>{completion.handler, stream, msg};
    return completion.result.get();
}

//------------------------------------------------------------------------------

template<bool isRequest, class Fields>
std::ostream&
operator<<(std::ostream& os,
    header<isRequest, Fields> const& msg)
{
    beast::detail::sync_ostream oss{os};
    error_code ec;
    write(oss, msg, ec);
    if(ec)
        throw system_error{ec};
    return os;
}

template<bool isRequest, class Body, class Fields>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Fields> const& msg)
{
    static_assert(is_Body<Body>::value,
        "Body requirements not met");
    static_assert(has_writer<Body>::value,
        "Body has no writer");
    static_assert(is_Writer<typename Body::writer,
        message<isRequest, Body, Fields>>::value,
            "Writer requirements not met");
    beast::detail::sync_ostream oss{os};
    error_code ec;
    write(oss, msg, ec);
    if(ec && ec != boost::asio::error::eof)
        throw system_error{ec};
    return os;
}

} // http
} // beast

#endif
