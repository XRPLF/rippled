//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_MESSAGE_IPP
#define BEAST_HTTP_IMPL_MESSAGE_IPP

#include <beast/http/chunk_encode.hpp>
#include <beast/http/resume_context.hpp>
#include <beast/http/rfc2616.hpp>
#include <beast/write_streambuf.hpp>
#include <beast/type_check.hpp>
#include <beast/http/detail/write_preparation.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/logic/tribool.hpp>
#include <condition_variable>
#include <mutex>

namespace beast {
namespace http {

template<bool isRequest, class Body, class Headers>
message<isRequest, Body, Headers>::
message()
{
}

template<bool isRequest, class Body, class Headers>
message<isRequest, Body, Headers>::
message(request_params params)
{
    static_assert(isRequest, "message is not a request");
    this->method = params.method;
    this->url = std::move(params.url);
    version = params.version;
}

template<bool isRequest, class Body, class Headers>
message<isRequest, Body, Headers>::
message(response_params params)
{
    static_assert(! isRequest, "message is not a response");
    this->status = params.status;
    this->reason = std::move(params.reason);
    version = params.version;
}

template<bool isRequest, class Body, class Headers>
template<class Streambuf>
void
message<isRequest, Body, Headers>::
write_firstline(Streambuf& streambuf,
    std::true_type) const
{
    write(streambuf, to_string(this->method));
    write(streambuf, " ");
    write(streambuf, this->url);
    switch(version)
    {
    case 10:
        write(streambuf, " HTTP/1.0\r\n");
        break;
    case 11:
        write(streambuf, " HTTP/1.1\r\n");
        break;
    default:
        write(streambuf, " HTTP/");
        write(streambuf, version / 10);
        write(streambuf, ".");
        write(streambuf, version % 10);
        write(streambuf, "\r\n");
        break;
    }
}

template<bool isRequest, class Body, class Headers>
template<class Streambuf>
void
message<isRequest, Body, Headers>::
write_firstline(Streambuf& streambuf,
    std::false_type) const
{
    switch(version)
    {
    case 10:
        write(streambuf, "HTTP/1.0 ");
        break;
    case 11:
        write(streambuf, "HTTP/1.1 ");
        break;
    default:
        write(streambuf, " HTTP/");
        write(streambuf, version / 10);
        write(streambuf, ".");
        write(streambuf, version % 10);
        write(streambuf, " ");
        break;
    }
    write(streambuf, this->status);
    write(streambuf, " ");
    write(streambuf, this->reason);
    write(streambuf, "\r\n");
}

namespace detail {

template<class ConstBufferSequence>
std::string
buffers_to_string(ConstBufferSequence const& buffers)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    std::string s;
    s.reserve(buffer_size(buffers));
    for(auto const& b : buffers)
        s.append(buffer_cast<char const*>(b),
            buffer_size(b));
    return s;
}

class writef_ostream
{
    std::ostream& os_;
    bool chunked_;

public:
    writef_ostream(std::ostream& os, bool chunked)
        : os_(os)
        , chunked_(chunked)
    {
    }

    template<class ConstBufferSequence>
    void
    operator()(ConstBufferSequence const& buffers)
    {
        if(chunked_)
            os_ << buffers_to_string(
                chunk_encode(buffers));
        else
            os_ << buffers_to_string(
                buffers);
    }
};

} // detail

// Diagnostic output only
template<bool isRequest, class Body, class Headers>
std::ostream&
operator<<(std::ostream& os,
    message<isRequest, Body, Headers> const& msg)
{
    error_code ec;
    detail::write_preparation<isRequest, Body, Headers> wp(msg);
    wp.init(ec);
    if(ec)
        return os;
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
    os << detail::buffers_to_string(wp.sb.data());
    wp.sb.consume(wp.sb.size());
    detail::writef_ostream writef(os, wp.chunked);
    for(;;)
    {
        {
            auto result = wp.w(std::move(copy), ec, writef);
            if(ec)
                return os;
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
        wp.sb.consume(wp.sb.size());
        for(;;)
        {
            auto result = wp.w(std::move(copy), ec, writef);
            if(ec)
                return os;
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
        os << detail::buffers_to_string(chunk_encode_final());
        if(ec)
            return os;
    }
    os << std::endl;
    return os;
}

//------------------------------------------------------------------------------

template<bool isRequest, class Body, class Headers>
void
set_connection(bool keep_alive,
    message<isRequest, Body, Headers>& req)
{
    if(req.version >= 11)
    {
        if(! keep_alive)
            req.headers.replace("Connection", "close");
        else
            req.headers.erase("Connection");
    }
    else
    {
        if(keep_alive)
            req.headers.replace("Connection", "keep-alive");
        else
            req.headers.erase("Connection");
    }
}

template<class Body, class Headers,
    class OtherBody, class OtherAllocator>
void
set_connection(bool keep_alive,
    message<false, Body, Headers>& resp,
        message<true, OtherBody, OtherAllocator> const& req)
{
    if(req.version >= 11)
    {
        if(rfc2616::token_in_list(req["Connection"], "close"))
            keep_alive = false;
    }
    else
    {
        if(! rfc2616::token_in_list(req["Connection"], "keep-alive"))
            keep_alive = false;
    }
    set_connection(keep_alive, resp);
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
bool
is_keep_alive(message<isRequest, Body, Headers> const& msg)
{
    if(msg.version >= 11)
    {
        if(rfc2616::token_in_list(
                msg.headers["Connection"], "close"))
            return false;
        return true;
    }
    if(rfc2616::token_in_list(
            msg.headers["Connection"], "keep-alive"))
        return true;
    return false;
}

template<bool isRequest, class Body, class Headers>
bool
is_upgrade(message<isRequest, Body, Headers> const& msg)
{
    if(msg.version < 11)
        return false;
    if(rfc2616::token_in_list(
            msg.headers["Connection"], "upgrade"))
        return true;
    return false;
}

} // http
} // beast

#include <beast/http/impl/message.ipp>

#endif
