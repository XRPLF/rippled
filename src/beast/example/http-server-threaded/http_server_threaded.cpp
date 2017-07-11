//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "../common/mime_types.hpp"

#include <beast/core.hpp>
#include <beast/http.hpp>
#include <beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

//------------------------------------------------------------------------------
//
// Example: HTTP server, synchronous, one thread per connection
//
//------------------------------------------------------------------------------

namespace ip = boost::asio::ip;     // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;   // from <boost/asio.hpp>
namespace http = beast::http;       // from <beast/http.hpp>

class connection
    : public std::enable_shared_from_this<connection>
{
    tcp::socket sock_;
    beast::string_view root_;

public:
    explicit
    connection(tcp::socket&& sock, beast::string_view root)
        : sock_(std::move(sock))
        , root_(root)
    {
    }

    void
    run()
    {
        // Bind a shared_ptr to *this into the thread.
        // When the thread exits, the connection object
        // will be destroyed.
        //
        std::thread{&connection::do_run, shared_from_this()}.detach();
    }

private:
    // Send a client error response
    http::response<http::span_body<char const>>
    client_error(http::status result, beast::string_view text)
    {
        http::response<http::span_body<char const>> res{result, 11};
        res.set(http::field::server, BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/plain");
        res.set(http::field::connection, "close");
        res.body = text;
        res.prepare_payload();
        return res;
    }

    // Return an HTTP Not Found response
    //
    http::response<http::string_body>
    not_found() const
    {
        http::response<http::string_body> res{http::status::not_found, 11};
        res.set(http::field::server, BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.set(http::field::connection, "close");
        res.body = "The file was not found";
        res.prepare_payload();
        return res;
    }

    // Return an HTTP Server Error
    //
    http::response<http::string_body>
    server_error(beast::error_code const& ec) const
    {
        http::response<http::string_body> res{http::status::internal_server_error, 11};
        res.set(http::field::server, BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.set(http::field::connection, "close");
        res.body = "Error: " + ec.message();
        res.prepare_payload();
        return res;
    }

    // Return a file response to an HTTP GET request
    //
    http::response<beast::http::file_body>
    get(boost::filesystem::path const& full_path,
        beast::error_code& ec) const
    {
        http::response<http::file_body> res;
        res.set(http::field::server, BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(full_path));
        res.set(http::field::connection, "close");
        res.body.open(full_path.string<std::string>().c_str(), beast::file_mode::scan, ec);
        if(ec)
            return res;
        res.set(http::field::content_length, res.body.size());
        return res;
    }

    // Handle a request
    template<class Body>
    void
    do_request(http::request<Body> const& req, beast::error_code& ec)
    {
        // verb must be get
        if(req.method() != http::verb::get)
        {
            http::write(sock_, client_error(http::status::bad_request, "Unsupported method"), ec);
            return;
        }

        // Request path must be absolute and not contain "..".
        if( req.target().empty() ||
            req.target()[0] != '/' ||
            req.target().find("..") != std::string::npos)
        {
            http::write(sock_, client_error(http::status::not_found, "File not found"), ec);
            return;
        }

        auto full_path = root_.to_string();
        full_path.append(req.target().data(), req.target().size());

        beast::error_code file_ec;
        auto res = get(full_path, file_ec);

        if(file_ec == beast::errc::no_such_file_or_directory)
        {
            http::write(sock_, not_found(), ec);
        }
        else if(ec)
        {
            http::write(sock_, server_error(file_ec), ec);
        }
        else
        {
            http::serializer<false, decltype(res)::body_type> sr{res};
            http::write(sock_, sr, ec);
        }
    }

    void
    do_run()
    {
        try
        {
            beast::error_code ec;
            beast::flat_buffer buffer;
            for(;;)
            {
                http::request_parser<http::string_body> parser;
                parser.header_limit(8192);
                parser.body_limit(1024 * 1024);
                http::read(sock_, buffer, parser, ec);
                if(ec == http::error::end_of_stream)
                    break;
                if(ec)
                    throw beast::system_error{ec};
                do_request(parser.get(), ec);
                if(ec)
                {
                    if(ec != http::error::end_of_stream)
                        throw beast::system_error{ec};
                    break;
                }
            }
            sock_.shutdown(tcp::socket::shutdown_both, ec);
            if(ec && ec != boost::asio::error::not_connected)
                throw beast::system_error{ec};
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[])
{
    try
    {
        // Check command line arguments.
        if (argc != 4)
        {
            std::cerr << "Usage: http_server <address> <port> <doc_root>\n";
            std::cerr << "  For IPv4, try:\n";
            std::cerr << "    receiver 0.0.0.0 80 .\n";
            std::cerr << "  For IPv6, try:\n";
            std::cerr << "    receiver 0::0 80 .\n";
            return EXIT_FAILURE;
        }

        auto address = ip::address::from_string(argv[1]);
        unsigned short port = static_cast<unsigned short>(std::atoi(argv[2]));
        std::string doc_root = argv[3];

        boost::asio::io_service ios{1};
        tcp::acceptor acceptor{ios, {address, port}};
        for(;;)
        {
            tcp::socket sock{ios};
            acceptor.accept(sock);
            std::make_shared<connection>(std::move(sock), doc_root)->run();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
