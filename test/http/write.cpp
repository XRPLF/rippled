//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/write.hpp>

#include <beast/http/headers.hpp>
#include <beast/http/message.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <beast/core/error.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/core/to_string.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/error.hpp>
#include <string>

namespace beast {
namespace http {

class write_test : public beast::unit_test::suite
{
public:
    struct string_SyncStream
    {
        std::string str;
        
        template<class ConstBufferSequence>
        std::size_t
        write_some(ConstBufferSequence const& buffers)
        {
            error_code ec;
            auto const n = write_some(buffers, ec);
            if(ec)
                throw system_error{ec};
            return n;
        }

        template<class ConstBufferSequence>
        std::size_t write_some(
            ConstBufferSequence const& buffers, error_code& ec)
        {
            auto const n = buffer_size(buffers);
            using boost::asio::buffer_size;
            using boost::asio::buffer_cast;
            str.reserve(str.size() + n);
            for(auto const& buffer : buffers)
                str.append(buffer_cast<char const*>(buffer),
                    buffer_size(buffer));
            return n;
        }
    };

    struct fail_body
    {
        using value_type = std::string;

        class writer
        {
            value_type const& body_;

        public:
            template<bool isRequest, class Allocator>
            explicit
            writer(message<isRequest, fail_body, Allocator> const& msg)
                : body_(msg.body)
            {
            }

            void
            init(error_code& ec)
            {
                ec = boost::system::errc::make_error_code(
                    boost::system::errc::errc_t::invalid_argument);
            }

            template<class Write>
            boost::tribool
            operator()(resume_context&&, error_code&, Write&& write)
            {
                write(boost::asio::buffer(body_));
                return true;
            }
        };
    };

    struct test_body
    {
        using value_type = std::string;

        class writer
        {
            std::size_t pos_ = 0;
            value_type const& body_;

        public:
            template<bool isRequest, class Allocator>
            explicit
            writer(message<isRequest, test_body, Allocator> const& msg)
                : body_(msg.body)
            {
            }

            void
            init(error_code& ec)
            {
            }

            template<class Write>
            boost::tribool
            operator()(resume_context&&, error_code&, Write&& write)
            {
                write(boost::asio::buffer(body_));
                return true;
            }
        };
    };

    template<bool isRequest, class Body, class Headers>
    std::string
    str(message_v1<isRequest, Body, Headers> const& m)
    {
        string_SyncStream ss;
        write(ss, m);
        return ss.str;
    }

    void
    testWrite()
    {
        // auto content-length HTTP/1.0
        {
            message_v1<true, string_body, headers> m{{
                "GET", "/", 10}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            expect(str(m) ==
                "GET / HTTP/1.0\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*"
            );
        }
        // keep-alive HTTP/1.0
        {
            message_v1<true, string_body, headers> m{{
                "GET", "/", 10}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m, connection::keep_alive);
            expect(str(m) ==
                "GET / HTTP/1.0\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "Connection: keep-alive\r\n"
                "\r\n"
                "*"
            );
        }
        // upgrade HTTP/1.0
        {
            message_v1<true, string_body, headers> m{{
                "GET", "/", 10}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            try
            {
                prepare(m, connection::upgrade);
                fail();
            }
            catch(std::exception const&)
            {
                pass();
            }
        }
        // no content-length HTTP/1.0
        {
            message_v1<true, test_body, headers> m{{
                "GET", "/", 10}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            string_SyncStream ss;
            error_code ec;
            write(ss, m, ec);
            expect(ec == boost::asio::error::eof);
            expect(ss.str ==
                "GET / HTTP/1.0\r\n"
                "User-Agent: test\r\n"
                "\r\n"
                "*"
            );
        }
        // auto content-length HTTP/1.1
        {
            message_v1<true, string_body, headers> m{{
                "GET", "/", 11}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            expect(str(m) ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*"
            );
        }
        // close HTTP/1.1
        {
            message_v1<true, string_body, headers> m{{
                "GET", "/", 11}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m, connection::close);
            string_SyncStream ss;
            error_code ec;
            write(ss, m, ec);
            expect(ec == boost::asio::error::eof);
            expect(ss.str ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "Connection: close\r\n"
                "\r\n"
                "*"
            );
        }
        // upgrade HTTP/1.1
        {
            message_v1<true, empty_body, headers> m{{
                "GET", "/", 11}};
            m.headers.insert("User-Agent", "test");
            prepare(m, connection::upgrade);
            expect(str(m) ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Connection: upgrade\r\n"
                "\r\n"
            );
        }
        // no content-length HTTP/1.1
        {
            message_v1<true, test_body, headers> m{{
                "GET", "/", 11}};
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            string_SyncStream ss;
            error_code ec;
            write(ss, m, ec);
            expect(ss.str ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "1\r\n"
                "*\r\n"
                "0\r\n\r\n"
            );
        }
    }

    void testConvert()
    {
        message_v1<true, string_body, headers> m{{
            "GET", "/", 11}};
        m.headers.insert("User-Agent", "test");
        m.body = "*";
        prepare(m);
        expect(boost::lexical_cast<std::string>(m) ==
            "GET / HTTP/1.1\r\nUser-Agent: test\r\nContent-Length: 1\r\n\r\n*");
    }

    void run() override
    {
        testWrite();
        testConvert();
    }
};

BEAST_DEFINE_TESTSUITE(write,http,beast);

} // http
} // beast
