//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/core.hpp>
#include <boost/asio.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <thread>

using namespace beast;

//[http_snippet_1

#include <beast/http.hpp>
using namespace beast::http;

//]

namespace doc_http_snippets {

void fxx() {

    boost::asio::io_service ios;
    boost::asio::io_service::work work{ios};
    std::thread t{[&](){ ios.run(); }};
    boost::asio::ip::tcp::socket sock{ios};

{
//[http_snippet_2

    request<empty_body> req;
    req.version = 11;   // HTTP/1.1
    req.method(verb::get);
    req.target("/index.htm");
    req.set(field::accept, "text/html");
    req.set(field::user_agent, "Beast");

//]
}

{
//[http_snippet_3

    response<string_body> res;
    res.version = 11;   // HTTP/1.1
    res.result(status::ok);
    res.set(field::server, "Beast");
    res.body = "Hello, world!";
    res.prepare_payload();

//]
}

{
//[http_snippet_4

    flat_buffer buffer;         // (The parser is optimized for flat buffers)
    request<string_body> req;
    read(sock, buffer, req);

//]
}

{
//[http_snippet_5

    flat_buffer buffer;
    response<string_body> res;
    async_read(sock, buffer, res,
        [&](error_code ec)
        {
            std::cerr << ec.message() << std::endl;
        });

//]
}

{
//[http_snippet_6

    // This buffer's max size is too small for much of anything
    flat_buffer buffer{10};

    // Try to read a request
    error_code ec;
    request<string_body> req;
    read(sock, buffer, req, ec);
    if(ec == error::buffer_overflow)
        std::cerr << "Buffer limit exceeded!" << std::endl;

//]
}

{
//[http_snippet_7

    response<string_body> res;
    res.version = 11;
    res.result(status::ok);
    res.set(field::server, "Beast");
    res.body = "Hello, world!";

    error_code ec;
    write(sock, res, ec);
    if(ec == error::end_of_stream)
        sock.close();
//]

//[http_snippet_8
    async_write(sock, res,
        [&](error_code)
        {
            if(ec)
                std::cerr << ec.message() << std::endl;
        });
//]
}

{
//[http_snippet_10

    response<string_body> res;

    response_serializer<string_body> sr{res};

//]
}

} // fxx()

//[http_snippet_12

/** Send a message to a stream synchronously.

    @param stream The stream to write to. This type must support
    the @b SyncWriteStream concept.

    @param m The message to send. The Body type must support
    the @b BodyReader concept.
*/
template<
    class SyncWriteStream,
    bool isRequest, class Body, class Fields>
void
send(
    SyncWriteStream& stream,
    message<isRequest, Body, Fields> const& m)
{
    // Check the template types
    static_assert(is_sync_write_stream<SyncWriteStream>::value,
        "SyncWriteStream requirements not met");
    static_assert(is_body_reader<Body>::value,
        "BodyReader requirements not met");

    // Create the instance of serializer for the message
    serializer<isRequest, Body, Fields> sr{m};

    // Loop until the serializer is finished
    do
    {
        // This call guarantees it will make some
        // forward progress, or otherwise return an error.
        write_some(stream, sr);
    }
    while(! sr.is_done());
}

//]

//[http_snippet_13

template<class SyncReadStream>
void
print_response(SyncReadStream& stream)
{
    static_assert(is_sync_read_stream<SyncReadStream>::value,
        "SyncReadStream requirements not met");

    // Declare a parser for an HTTP response
    response_parser<string_body> parser;

    // Read the entire message
    read(stream, parser);

    // Now print the message
    std::cout << parser.get() << std::endl;
}

//]

#ifdef BOOST_MSVC
//[http_snippet_14

template<bool isRequest, class Body, class Fields>
void
print_cxx14(message<isRequest, Body, Fields> const& m)
{
    error_code ec;
    serializer<isRequest, Body, Fields> sr{m};
    do
    {
        sr.next(ec,
            [&sr](error_code& ec, auto const& buffer)
            {
                ec.assign(0, ec.category());
                std::cout << buffers(buffer);
                sr.consume(boost::asio::buffer_size(buffer));
            });
    }
    while(! ec && ! sr.is_done());
    if(! ec)
        std::cout << std::endl;
    else
        std::cerr << ec.message() << std::endl;
}

//]
#endif

//[http_snippet_15

template<class Serializer>
struct lambda
{
    Serializer& sr;

    lambda(Serializer& sr_) : sr(sr_) {}

    template<class ConstBufferSequence>
    void operator()(error_code& ec, ConstBufferSequence const& buffer) const
    {
        ec.assign(0, ec.category());
        std::cout << buffers(buffer);
        sr.consume(boost::asio::buffer_size(buffer));
    }
};

template<bool isRequest, class Body, class Fields>
void
print(message<isRequest, Body, Fields> const& m)
{
    error_code ec;
    serializer<isRequest, Body, Fields> sr{m};
    do
    {
        sr.next(ec, lambda<decltype(sr)>{sr});
    }
    while(! ec && ! sr.is_done());
    if(! ec)
        std::cout << std::endl;
    else
        std::cerr << ec.message() << std::endl;
}

//]

#if BOOST_MSVC
//[http_snippet_16

template<bool isRequest, class Body, class Fields>
void
split_print_cxx14(message<isRequest, Body, Fields> const& m)
{
    error_code ec;
    serializer<isRequest, Body, Fields> sr{m};
    sr.split(true);
    std::cout << "Header:" << std::endl;
    do
    {
        sr.next(ec,
            [&sr](error_code& ec, auto const& buffer)
            {
                ec.assign(0, ec.category());
                std::cout << buffers(buffer);
                sr.consume(boost::asio::buffer_size(buffer));
            });
    }
    while(! sr.is_header_done());
    if(! ec && ! sr.is_done())
    {
        std::cout << "Body:" << std::endl;
        do
        {
            sr.next(ec,
                [&sr](error_code& ec, auto const& buffer)
                {
                    ec.assign(0, ec.category());
                    std::cout << buffers(buffer);
                    sr.consume(boost::asio::buffer_size(buffer));
                });
        }
        while(! ec && ! sr.is_done());
    }
    if(ec)
        std::cerr << ec.message() << std::endl;
}

//]
#endif

//[http_snippet_17

struct decorator
{
    std::string s;

    template<class ConstBufferSequence>
    string_view
    operator()(ConstBufferSequence const& buffers)
    {
        s = ";x=" + std::to_string(boost::asio::buffer_size(buffers));
        return s;
    }

    string_view
    operator()(boost::asio::null_buffers)
    {
        return "Result: OK\r\n";
    }
};

//]

} // doc_http_snippets
