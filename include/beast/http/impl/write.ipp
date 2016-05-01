//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_WRITE_IPP
#define BEAST_HTTP_IMPL_WRITE_IPP

#include <beast/http/resume_context.hpp>
#include <beast/http/detail/chunk_encode.hpp>
#include <beast/http/detail/has_content_length.hpp>
#include <beast/buffer_cat.hpp>
#include <beast/bind_handler.hpp>
#include <beast/handler_alloc.hpp>
#include <beast/streambuf.hpp>
#include <beast/type_check.hpp>
#include <beast/write_streambuf.hpp>
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

template<class Streambuf, class Body, class Headers>
void
write_firstline(Streambuf& streambuf,
    message_v1<true, Body, Headers> const& msg)
{
    write(streambuf, msg.method);
    write(streambuf, " ");
    write(streambuf, msg.url);
    switch(msg.version)
    {
    case 10:
        write(streambuf, " HTTP/1.0\r\n");
        break;
    case 11:
        write(streambuf, " HTTP/1.1\r\n");
        break;
    default:
        write(streambuf, " HTTP/");
        write(streambuf, msg.version / 10);
        write(streambuf, ".");
        write(streambuf, msg.version % 10);
        write(streambuf, "\r\n");
        break;
    }
}

template<class Streambuf, class Body, class Headers>
void
write_firstline(Streambuf& streambuf,
    message_v1<false, Body, Headers> const& msg)
{
    switch(msg.version)
    {
    case 10:
        write(streambuf, "HTTP/1.0 ");
        break;
    case 11:
        write(streambuf, "HTTP/1.1 ");
        break;
    default:
        write(streambuf, " HTTP/");
        write(streambuf, msg.version / 10);
        write(streambuf, ".");
        write(streambuf, msg.version % 10);
        write(streambuf, " ");
        break;
    }
    write(streambuf, msg.status);
    write(streambuf, " ");
    write(streambuf, msg.reason);
    write(streambuf, "\r\n");
}

template<class Streambuf, class FieldSequence>
void
write_fields(Streambuf& streambuf, FieldSequence const& fields)
{
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    //static_assert(is_FieldSequence<FieldSequence>::value,
    //    "FieldSequence requirements not met");
    for(auto const& field : fields)
    {
        write(streambuf, field.name());
        write(streambuf, ": ");
        write(streambuf, field.value());
        write(streambuf, "\r\n");
    }
}

template<bool isRequest, class Body, class Headers>
struct write_preparation
{
    using headers_type =
        basic_headers<std::allocator<char>>;

    message_v1<isRequest, Body, Headers> const& msg;
    typename Body::writer w;
    streambuf sb;
    bool chunked;
    bool close;

    explicit
    write_preparation(
            message_v1<isRequest, Body, Headers> const& msg_)
        : msg(msg_)
        , w(msg)
        , chunked(rfc2616::token_in_list(
            msg.headers["Transfer-Encoding"], "chunked"))
        , close(rfc2616::token_in_list(
            msg.headers["Connection"], "close") ||
                (msg.version < 11 && ! msg.headers.exists(
                    "Content-Length")))
    {
    }

    void
    init(error_code& ec)
    {
        w.init(ec);
        if(ec)
            return;
        write_firstline(sb, msg);
        write_fields(sb, msg.headers);
        beast::write(sb, "\r\n");
    }
};

template<class Stream, class Handler,
    bool isRequest, class Body, class Headers>
class write_op
{
    using alloc_type =
        handler_alloc<char, Handler>;

    struct data
    {
        Stream& s;
        // VFALCO How do we use handler_alloc in write_preparation?
        write_preparation<
            isRequest, Body, Headers> wp;
        Handler h;
        resume_context resume;
        resume_context copy;
        bool cont;
        int state = 0;

        template<class DeducedHandler>
        data(DeducedHandler&& h_, Stream& s_,
                message_v1<isRequest, Body, Headers> const& m_)
            : s(s_)
            , wp(m_)
            , h(std::forward<DeducedHandler>(h_))
            , cont(boost_asio_handler_cont_helpers::
                is_continuation(h))
        {
        }
    };

    class writef0
    {
        write_op& self_;

    public:
        explicit
        writef0(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers)
        {
            auto& d = *self_.d_;
            // write headers and body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.sb.data(),
                        detail::chunk_encode(buffers)),
                            std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffer_cat(d.wp.sb.data(),
                        buffers), std::move(self_));
        }
    };

    class writef
    {
        write_op& self_;

    public:
        explicit
        writef(write_op& self)
            : self_(self)
        {
        }

        template<class ConstBufferSequence>
        void operator()(ConstBufferSequence const& buffers)
        {
            auto& d = *self_.d_;
            // write body
            if(d.wp.chunked)
                boost::asio::async_write(d.s,
                    detail::chunk_encode(buffers),
                        std::move(self_));
            else
                boost::asio::async_write(d.s,
                    buffers, std::move(self_));
        }
    };

    std::shared_ptr<data> d_;

public:
    write_op(write_op&&) = default;
    write_op(write_op const&) = default;

    template<class DeducedHandler, class... Args>
    write_op(DeducedHandler&& h, Stream& s, Args&&... args)
        : d_(std::allocate_shared<data>(alloc_type{h},
            std::forward<DeducedHandler>(h), s,
                std::forward<Args>(args)...))
    {
        auto& d = *d_;
        auto sp = d_;
        d.resume = {
            [sp]() mutable
            {
                write_op self(std::move(sp));
                self.d_->cont = false;
                auto& ios = self.d_->s.get_io_service();
                ios.dispatch(bind_handler(std::move(self),
                    error_code{}, 0, false));
            }};
        d.copy = d.resume;
        (*this)(error_code{}, 0, false);
    }

    explicit
    write_op(std::shared_ptr<data> d)
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

template<class Stream, class Handler,
    bool isRequest, class Body, class Headers>
void
write_op<Stream, Handler, isRequest, Body, Headers>::
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
            auto const result = d.wp.w(
                std::move(d.copy), ec, writef0{*this});
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

        // sent headers and body
        case 2:
            d.wp.sb.consume(d.wp.sb.size());
            d.state = 3;
            break;

        case 3:
        {
            auto const result = d.wp.w(
                std::move(d.copy), ec, writef{*this});
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
                detail::chunk_encode_final(), std::move(*this));
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
    d.h(ec);
    d.resume = {};
    d.copy = {};
}

template<class SyncWriteStream, class Streambuf>
class writef0_write
{
    Streambuf const& sb_;
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef0_write(SyncWriteStream& stream,
            Streambuf const& sb, bool chunked, error_code& ec)
        : sb_(sb)
        , stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers)
    {
        // write headers and body
        if(chunked_)
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), detail::chunk_encode(buffers)), ec_);
        else
            boost::asio::write(stream_, buffer_cat(
                sb_.data(), buffers), ec_);
    }
};

template<class SyncWriteStream>
class writef_write
{
    SyncWriteStream& stream_;
    bool chunked_;
    error_code& ec_;

public:
    writef_write(SyncWriteStream& stream,
            bool chunked, error_code& ec)
        : stream_(stream)
        , chunked_(chunked)
        , ec_(ec)
    {
    }

    template<class ConstBufferSequence>
    void operator()(ConstBufferSequence const& buffers)
    {
        // write body
        if(chunked_)
            boost::asio::write(stream_,
                detail::chunk_encode(buffers), ec_);
        else
            boost::asio::write(stream_, buffers, ec_);
    }
};

} // detail

//------------------------------------------------------------------------------

template<class SyncWriteStream,
    bool isRequest, class Body, class Headers>
void
write(SyncWriteStream& stream,
    message_v1<isRequest, Body, Headers> const& msg)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    error_code ec;
    write(stream, msg, ec);
    if(ec)
        throw boost::system::system_error{ec};
}

template<class SyncWriteStream,
    bool isRequest, class Body, class Headers>
void
write(SyncWriteStream& stream,
    message_v1<isRequest, Body, Headers> const& msg,
        boost::system::error_code& ec)
{
    static_assert(is_SyncWriteStream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    detail::write_preparation<isRequest, Body, Headers> wp(msg);
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
    for(;;)
    {
        {
            auto result = wp.w(std::move(copy), ec,
                detail::writef0_write<SyncWriteStream, decltype(wp.sb)>{
                    stream, wp.sb, wp.chunked, ec});
            if(ec)
                return;
            if(result)
                break;
            if(boost::indeterminate(result))
            {
                boost::asio::write(stream, wp.sb.data(), ec);
                if(ec)
                    return;
                wp.sb.consume(wp.sb.size());
                copy = resume;
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&]{ return ready; });
                ready = false;
            }
        }
        wp.sb.consume(wp.sb.size());
        for(;;)
        {
            auto result = wp.w(std::move(copy), ec,
                detail::writef_write<SyncWriteStream>{
                    stream, wp.chunked, ec});
            if(ec)
                return;
            if(result)
                break;
            if(boost::indeterminate(result))
            {
                copy = resume;
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [&]{ return ready; });
                ready = false;
            }
        }
    }
    if(wp.chunked)
    {
        // VFALCO Unfortunately the current interface to the
        //        Writer concept prevents us from using coalescing the
        //        final body chunk with the final chunk delimiter.
        //
        // write final chunk
        boost::asio::write(stream, detail::chunk_encode_final(), ec);
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
    bool isRequest, class Body, class Headers,
        class WriteHandler>
typename async_completion<
    WriteHandler, void(error_code)>::result_type
async_write(AsyncWriteStream& stream,
    message_v1<isRequest, Body, Headers> const& msg,
        WriteHandler&& handler)
{
    static_assert(
        is_AsyncWriteStream<AsyncWriteStream>::value,
            "AsyncWriteStream requirements not met");
    beast::async_completion<WriteHandler,
        void(error_code)> completion(handler);
    detail::write_op<AsyncWriteStream, decltype(completion.handler),
        isRequest, Body, Headers>{completion.handler, stream, msg};
    return completion.result.get();
}

namespace detail {

class ostream_SyncStream
{
    std::ostream& os_;

public:
    ostream_SyncStream(std::ostream& os)
        : os_(os)
    {
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers)
    {
        error_code ec;
        auto const n = write_some(buffers, ec);
        if(ec)
            throw boost::system::system_error{ec};
        return n;
    }

    template<class ConstBufferSequence>
    std::size_t
    write_some(ConstBufferSequence const& buffers,
        error_code& ec)
    {
        std::size_t n = 0;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        for(auto const& buffer : buffers)
        {
            os_.write(buffer_cast<char const*>(buffer),
                buffer_size(buffer));
            if(os_.fail())
            {
                ec = boost::system::errc::make_error_code(
                    boost::system::errc::no_stream_resources);
                break;
            }
            n += buffer_size(buffer);
        }
        return n;
    }
};

} // detail

template<bool isRequest, class Body, class Headers>
std::ostream&
operator<<(std::ostream& os,
    message_v1<isRequest, Body, Headers> const& msg)
{
    detail::ostream_SyncStream oss(os);
    error_code ec;
    write(oss, msg, ec);
    if(ec && ec != boost::asio::error::eof)
        throw boost::system::system_error{ec};
    return os;
}

} // http
} // beast

#endif
