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
#include <beast/test/fail_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/error.hpp>
#include <sstream>
#include <string>

namespace beast {
namespace http {

class write_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    class string_write_stream
    {
        boost::asio::io_service& ios_;
    
    public:
        std::string str;

        explicit
        string_write_stream(boost::asio::io_service& ios)
            : ios_(ios)
        {
        }

        boost::asio::io_service&
        get_io_service()
        {
            return ios_;
        }

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
        std::size_t
        write_some(
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

        template<class ConstBufferSequence, class WriteHandler>
        typename async_completion<
            WriteHandler, void(error_code)>::result_type
        async_write_some(ConstBufferSequence const& buffers,
            WriteHandler&& handler)
        {
            error_code ec;
            auto const bytes_transferred = write_some(buffers, ec);
            async_completion<
                WriteHandler, void(error_code, std::size_t)
                    > completion(handler);
            get_io_service().post(
                bind_handler(completion.handler, ec, bytes_transferred));
            return completion.result.get();
        }
    };

    struct unsized_body
    {
        using value_type = std::string;

        class writer
        {
            value_type const& body_;

        public:
            template<bool isRequest, class Allocator>
            explicit
            writer(message<isRequest, unsized_body, Allocator> const& msg)
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

    struct fail_body
    {
        class writer;

        class value_type
        {
            friend class writer;

            std::string s_;
            test::fail_counter& fc_;
            boost::asio::io_service& ios_;

        public:
            value_type(test::fail_counter& fc,
                    boost::asio::io_service& ios)
                : fc_(fc)
                , ios_(ios)
            {
            }

            boost::asio::io_service&
            get_io_service() const
            {
                return ios_;
            }

            value_type&
            operator=(std::string s)
            {
                s_ = std::move(s);
                return *this;
            }
        };

        class writer
        {
            std::size_t n_ = 0;
            value_type const& body_;
            bool suspend_ = false;
            enable_yield_to yt_;

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
                body_.fc_.fail(ec);
            }

            class do_resume
            {
                resume_context rc_;

            public:
                explicit
                do_resume(resume_context&& rc)
                    : rc_(std::move(rc))
                {
                }

                void
                operator()()
                {
                    rc_();
                }
            };

            template<class Write>
            boost::tribool
            operator()(resume_context&& rc, error_code& ec, Write&& write)
            {
                if(body_.fc_.fail(ec))
                    return false;
                suspend_ = ! suspend_;
                if(suspend_)
                {
                    yt_.get_io_service().post(do_resume{std::move(rc)});
                    return boost::indeterminate;
                }
                if(n_ >= body_.s_.size())
                    return true;
                write(boost::asio::buffer(body_.s_.data() + n_, 1));
                ++n_;
                return n_ == body_.s_.size();
            }
        };
    };

    template<bool isRequest, class Body, class Headers>
    std::string
    str(message_v1<isRequest, Body, Headers> const& m)
    {
        string_write_stream ss(ios_);
        write(ss, m);
        return ss.str;
    }

    void
    testAsyncWrite(yield_context do_yield)
    {
        {
            message_v1<false, string_body, headers> m;
            m.version = 10;
            m.status = 200;
            m.reason = "OK";
            m.headers.insert("Server", "test");
            m.headers.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            string_write_stream ss(ios_);
            async_write(ss, m, do_yield[ec]);
            if(expect(! ec, ec.message()))
                expect(ss.str ==
                    "HTTP/1.0 200 OK\r\n"
                    "Server: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****");
        }
        {
            message_v1<false, string_body, headers> m;
            m.version = 11;
            m.status = 200;
            m.reason = "OK";
            m.headers.insert("Server", "test");
            m.headers.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            string_write_stream ss(ios_);
            async_write(ss, m, do_yield[ec]);
            if(expect(! ec, ec.message()))
                expect(ss.str ==
                    "HTTP/1.1 200 OK\r\n"
                    "Server: test\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "5\r\n"
                    "*****\r\n"
                    "0\r\n\r\n");
        }
    }

    void
    testFailures(yield_context do_yield)
    {
        static std::size_t constexpr limit = 100;
        std::size_t n;

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                string_write_stream> fs(fc, ios_);
            message_v1<true, fail_body, headers> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.headers.insert("Content-Length", "5");
            m.body = "*****";
            try
            {
                write(fs, m);
                expect(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****"
                );
                pass();
                break;
            }
            catch(std::exception const&)
            {
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                string_write_stream> fs(fc, ios_);
            message_v1<true, fail_body, headers> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.headers.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            write(fs, m, ec);
            if(ec == boost::asio::error::eof)
            {
                expect(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "0\r\n\r\n"
                );
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                string_write_stream> fs(fc, ios_);
            message_v1<true, fail_body, headers> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.headers.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            async_write(fs, m, do_yield[ec]);
            if(ec == boost::asio::error::eof)
            {
                expect(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "1\r\n*\r\n"
                    "0\r\n\r\n"
                );
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                string_write_stream> fs(fc, ios_);
            message_v1<true, fail_body, headers> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.headers.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            write(fs, m, ec);
            if(! ec)
            {
                expect(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****"
                );
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                string_write_stream> fs(fc, ios_);
            message_v1<true, fail_body, headers> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.headers.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            async_write(fs, m, do_yield[ec]);
            if(! ec)
            {
                expect(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****"
                );
                break;
            }
        }
        expect(n < limit);
    }

    void
    testOutput()
    {
        // auto content-length HTTP/1.0
        {
            message_v1<true, string_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
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
            message_v1<true, string_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
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
            message_v1<true, string_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
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
            message_v1<true, unsized_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            string_write_stream ss(ios_);
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
            message_v1<true, string_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
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
            message_v1<true, string_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m, connection::close);
            string_write_stream ss(ios_);
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
            message_v1<true, empty_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
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
            message_v1<true, unsized_body, headers> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.headers.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            string_write_stream ss(ios_);
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
        message_v1<true, string_body, headers> m;
        m.method = "GET";
        m.url = "/";
        m.version = 11;
        m.headers.insert("User-Agent", "test");
        m.body = "*";
        prepare(m);
        expect(boost::lexical_cast<std::string>(m) ==
            "GET / HTTP/1.1\r\nUser-Agent: test\r\nContent-Length: 1\r\n\r\n*");
    }

    void testOstream()
    {
        message_v1<true, string_body, headers> m;
        m.method = "GET";
        m.url = "/";
        m.version = 11;
        m.headers.insert("User-Agent", "test");
        m.body = "*";
        prepare(m);
        std::stringstream ss;
        ss.setstate(ss.rdstate() |
            std::stringstream::failbit);
        try
        {
            ss << m;
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    void run() override
    {
        yield_to(std::bind(&write_test::testAsyncWrite,
            this, std::placeholders::_1));
        yield_to(std::bind(&write_test::testFailures,
            this, std::placeholders::_1));
        testOutput();
        testConvert();
        testOstream();
    }
};

BEAST_DEFINE_TESTSUITE(write,http,beast);

} // http
} // beast
