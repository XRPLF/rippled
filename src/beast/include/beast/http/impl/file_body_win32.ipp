//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_FILE_BODY_WIN32_IPP
#define BEAST_HTTP_IMPL_FILE_BODY_WIN32_IPP

#if BEAST_USE_WIN32_FILE

#include <beast/core/async_result.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/core/detail/clamp.hpp>
#include <beast/http/serializer.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/asio/windows/overlapped_ptr.hpp>
#include <boost/make_unique.hpp>
#include <boost/smart_ptr/make_shared_array.hpp>
#include <boost/detail/winapi/basic_types.hpp>
#include <algorithm>
#include <cstring>

namespace beast {
namespace http {

namespace detail {
template<class, class, bool, class, class>
class write_some_win32_op;
} // detail

template<>
struct basic_file_body<file_win32>
{
    using file_type = file_win32;

    class reader;
    class writer;

    //--------------------------------------------------------------------------

    class value_type
    {
        friend class reader;
        friend class writer;
        friend struct basic_file_body<file_win32>;

        template<class, class, bool, class, class>
        friend class detail::write_some_win32_op;
        template<class Protocol, bool isRequest,
            class Fields, class Decorator>
        friend
        void
        write_some(
            boost::asio::basic_stream_socket<Protocol>& sock,
            serializer<isRequest, basic_file_body<file_win32>,
                Fields, Decorator>& sr,
            error_code& ec);

        file_win32 file_;
        std::uint64_t size_ = 0;    // cached file size
        std::uint64_t first_;       // starting offset of the range
        std::uint64_t last_;        // ending offset of the range

    public:
        ~value_type() = default;
        value_type() = default;
        value_type(value_type&& other) = default;
        value_type& operator=(value_type&& other) = default;

        bool
        is_open() const
        {
            return file_.is_open();
        }

        std::uint64_t
        size() const
        {
            return size_;
        }

        void
        close();

        void
        open(char const* path, file_mode mode, error_code& ec);

        void
        reset(file_win32&& file, error_code& ec);
    };

    //--------------------------------------------------------------------------

    class reader
    {
        template<class, class, bool, class, class>
        friend class detail::write_some_win32_op;
        template<class Protocol, bool isRequest,
            class Fields, class Decorator>
        friend
        void
        write_some(
            boost::asio::basic_stream_socket<Protocol>& sock,
            serializer<isRequest, basic_file_body<file_win32>,
                Fields, Decorator>& sr,
            error_code& ec);

        value_type& body_;  // The body we are reading from
        std::uint64_t pos_; // The current position in the file
        char buf_[4096];    // Small buffer for reading

    public:
        using const_buffers_type =
            boost::asio::const_buffers_1;

        template<bool isRequest, class Fields>
        reader(message<isRequest,
                basic_file_body<file_win32>, Fields>& m)
            : body_(m.body)
        {
        }

        void
        init(error_code&)
        {
            BOOST_ASSERT(body_.file_.is_open());
            pos_ = body_.first_;
        }

        boost::optional<std::pair<const_buffers_type, bool>>
        get(error_code& ec)
        {
            std::size_t const n = (std::min)(sizeof(buf_),
                beast::detail::clamp(body_.last_ - pos_));
            if(n == 0)
            {
                ec.assign(0, ec.category());
                return boost::none;
            }
            auto const nread = body_.file_.read(buf_, n, ec);
            if(ec)
                return boost::none;
            BOOST_ASSERT(nread != 0);
            pos_ += nread;
            ec.assign(0, ec.category());
            return {{
                {buf_, nread},          // buffer to return.
                pos_ < body_.last_}};   // `true` if there are more buffers.
        }
    };

    //--------------------------------------------------------------------------

    class writer
    {
        value_type& body_;

    public:
        template<bool isRequest, class Fields>
        explicit
        writer(message<isRequest, basic_file_body, Fields>& m)
            : body_(m.body)
        {
        }

        void
        init(boost::optional<
            std::uint64_t> const& content_length,
                error_code& ec)
        {
            // VFALCO We could reserve space in the file
            boost::ignore_unused(content_length);
            BOOST_ASSERT(body_.file_.is_open());
            ec.assign(0, ec.category());
        }

        template<class ConstBufferSequence>
        std::size_t
        put(ConstBufferSequence const& buffers,
            error_code& ec)
        {
            std::size_t nwritten = 0;
            for(boost::asio::const_buffer buffer : buffers)
            {
                nwritten += body_.file_.write(
                    boost::asio::buffer_cast<void const*>(buffer),
                    boost::asio::buffer_size(buffer),
                    ec);
                if(ec)
                    return nwritten;
            }
            ec.assign(0, ec.category());
            return nwritten;
        }

        void
        finish(error_code& ec)
        {
            ec.assign(0, ec.category());
        }
    };

    //--------------------------------------------------------------------------

    static
    std::uint64_t
    size(value_type const& body)
    {
        return body.size();
    }
};

//------------------------------------------------------------------------------

inline
void
basic_file_body<file_win32>::
value_type::
close()
{
    error_code ignored;
    file_.close(ignored);
}

inline
void
basic_file_body<file_win32>::
value_type::
open(char const* path, file_mode mode, error_code& ec)
{
    file_.open(path, mode, ec);
    if(ec)
        return;
    size_ = file_.size(ec);
    if(ec)
    {
        close();
        return;
    }
    first_ = 0;
    last_ = size_;
}

inline
void
basic_file_body<file_win32>::
value_type::
reset(file_win32&& file, error_code& ec)
{
    if(file_.is_open())
    {
        error_code ignored;
        file_.close(ignored);
    }
    file_ = std::move(file);
    if(file_.is_open())
    {
        size_ = file_.size(ec);
        if(ec)
        {
            close();
            return;
        }
        first_ = 0;
        last_ = size_;
    }
}

//------------------------------------------------------------------------------

namespace detail {

template<class Unsigned>
inline
boost::detail::winapi::DWORD_
lowPart(Unsigned n)
{
    return static_cast<
        boost::detail::winapi::DWORD_>(
            n & 0xffffffff);
}

template<class Unsigned>
inline
boost::detail::winapi::DWORD_
highPart(Unsigned n, std::true_type)
{
    return static_cast<
        boost::detail::winapi::DWORD_>(
            (n>>32)&0xffffffff);
}

template<class Unsigned>
inline
boost::detail::winapi::DWORD_
highPart(Unsigned, std::false_type)
{
    return 0;
}

template<class Unsigned>
inline
boost::detail::winapi::DWORD_
highPart(Unsigned n)
{
    return highPart(n, std::integral_constant<
        bool, (sizeof(Unsigned)>4)>{});
}

class null_lambda
{
public:
    template<class ConstBufferSequence>
    void
    operator()(error_code&,
        ConstBufferSequence const&) const
    {
        BOOST_ASSERT(false);
    }
};

//------------------------------------------------------------------------------

#if BOOST_ASIO_HAS_WINDOWS_OVERLAPPED_PTR

template<class Protocol, class Handler,
    bool isRequest, class Fields, class Decorator>
class write_some_win32_op
{
    boost::asio::basic_stream_socket<Protocol>& sock_;
    serializer<isRequest, basic_file_body<file_win32>,
        Fields, Decorator>& sr_;
    bool header_ = false;
    Handler h_;

public:
    write_some_win32_op(write_some_win32_op&&) = default;
    write_some_win32_op(write_some_win32_op const&) = default;

    template<class DeducedHandler>
    write_some_win32_op(
        DeducedHandler&& h,
        boost::asio::basic_stream_socket<Protocol>& s,
        serializer<isRequest, basic_file_body<file_win32>,
            Fields, Decorator>& sr)
        : sock_(s)
        , sr_(sr)
        , h_(std::forward<DeducedHandler>(h))
    {
    }

    void
    operator()();

    void
    operator()(error_code ec,
        std::size_t bytes_transferred = 0);

    friend
    void* asio_handler_allocate(
        std::size_t size, write_some_win32_op* op)
    {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(
            size, std::addressof(op->h_));
    }

    friend
    void asio_handler_deallocate(
        void* p, std::size_t size, write_some_win32_op* op)
    {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(
            p, size, std::addressof(op->h_));
    }

    friend
    bool asio_handler_is_continuation(write_some_win32_op* op)
    {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(
            std::addressof(op->h_));
    }

    template<class Function>
    friend
    void asio_handler_invoke(Function&& f, write_some_win32_op* op)
    {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(
            f, std::addressof(op->h_));
    }
};

template<class Protocol, class Handler,
    bool isRequest, class Fields, class Decorator>
void
write_some_win32_op<
    Protocol, Handler, isRequest, Fields, Decorator>::
operator()()
{
    if(! sr_.is_header_done())
    {
        header_ = true;
        sr_.split(true);
        return detail::async_write_some(
            sock_, sr_, std::move(*this));
    }
    if(sr_.chunked())
    {
        return detail::async_write_some(
            sock_, sr_, std::move(*this));
    }
    auto& r = sr_.reader_impl();
    boost::detail::winapi::DWORD_ const nNumberOfBytesToWrite =
        std::min<boost::detail::winapi::DWORD_>(
            beast::detail::clamp(std::min<std::uint64_t>(
                r.body_.last_ - r.pos_, sr_.limit())),
            2147483646);
    boost::asio::windows::overlapped_ptr overlapped{
        sock_.get_io_service(), *this};
    auto& ov = *overlapped.get();
    ov.Offset = lowPart(r.pos_);
    ov.OffsetHigh = highPart(r.pos_);
    auto const bSuccess = ::TransmitFile(
        sock_.native_handle(),
        sr_.get().body.file_.native_handle(),
        nNumberOfBytesToWrite,
        0,
        overlapped.get(),
        nullptr,
        0);
    auto const dwError = ::GetLastError();
    if(! bSuccess && dwError !=
        boost::detail::winapi::ERROR_IO_PENDING_)
    {
        // completed immediately
        overlapped.complete(error_code{static_cast<int>(
            boost::detail::winapi::GetLastError()),
                system_category()}, 0);
        return;
    }
    overlapped.release();
}

template<class Protocol, class Handler,
    bool isRequest, class Fields, class Decorator>
void
write_some_win32_op<
    Protocol, Handler,isRequest, Fields, Decorator>::
operator()(error_code ec, std::size_t bytes_transferred)
{
    if(! ec)
    {
        if(header_)
        {
            header_ = false;
            return (*this)();
        }
        auto& r = sr_.reader_impl();
        r.pos_ += bytes_transferred;
        BOOST_ASSERT(r.pos_ <= r.body_.last_);
        if(r.pos_ >= r.body_.last_)
        {
            sr_.next(ec, null_lambda{});
            BOOST_ASSERT(! ec);
            BOOST_ASSERT(sr_.is_done());
            if(! sr_.keep_alive())
                ec = error::end_of_stream;
        }
    }
    h_(ec);
}

#endif

} // detail

//------------------------------------------------------------------------------

template<class Protocol,
    bool isRequest, class Fields, class Decorator>
void
write_some(
    boost::asio::basic_stream_socket<Protocol>& sock,
    serializer<isRequest, basic_file_body<file_win32>,
        Fields, Decorator>& sr,
    error_code& ec)
{
    if(! sr.is_header_done())
    {
        sr.split(true);
        detail::write_some(sock, sr, ec);
        if(ec)
            return;
        return;
    }
    if(sr.chunked())
    {
        detail::write_some(sock, sr, ec);
        if(ec)
            return;
        return;
    }
    auto& r = sr.reader_impl();
    r.body_.file_.seek(r.pos_, ec);
    if(ec)
        return;
    boost::detail::winapi::DWORD_ const nNumberOfBytesToWrite =
        std::min<boost::detail::winapi::DWORD_>(
            beast::detail::clamp(std::min<std::uint64_t>(
                r.body_.last_ - r.pos_, sr.limit())),
            2147483646);
    auto const bSuccess = ::TransmitFile(
        sock.native_handle(),
        r.body_.file_.native_handle(),
        nNumberOfBytesToWrite,
        0,
        nullptr,
        nullptr,
        0);
    if(! bSuccess)
    {
        ec.assign(static_cast<int>(
            boost::detail::winapi::GetLastError()),
                system_category());
        return;
    }
    r.pos_ += nNumberOfBytesToWrite;
    BOOST_ASSERT(r.pos_ <= r.body_.last_);
    if(r.pos_ < r.body_.last_)
    {
        ec.assign(0, ec.category());
    }
    else
    {
        sr.next(ec, detail::null_lambda{});
        BOOST_ASSERT(! ec);
        BOOST_ASSERT(sr.is_done());
        if(! sr.keep_alive())
            ec = error::end_of_stream;
    }
}

#if BOOST_ASIO_HAS_WINDOWS_OVERLAPPED_PTR

template<
    class Protocol,
    bool isRequest, class Fields, class Decorator,
    class WriteHandler>
async_return_type<WriteHandler, void(error_code)>
async_write_some(
    boost::asio::basic_stream_socket<Protocol>& sock,
    serializer<isRequest, basic_file_body<file_win32>,
        Fields, Decorator>& sr,
    WriteHandler&& handler)
{
    async_completion<WriteHandler,
        void(error_code)> init{handler};
    detail::write_some_win32_op<Protocol, handler_type<
        WriteHandler, void(error_code)>, isRequest, Fields,
            Decorator>{init.completion_handler, sock, sr}();
    return init.result.get();
}

#endif

} // http
} // beast

#endif

#endif
