//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/write.hpp>

#include <beast/http/fields.hpp>
#include <beast/http/message.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/string_body.hpp>
#include <beast/http/write.hpp>
#include <beast/core/error.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/core/to_string.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_ostream.hpp>
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
    struct unsized_body
    {
        using value_type = std::string;

        class writer
        {
            value_type const& body_;

        public:
            template<bool isRequest, class Allocator>
            explicit
            writer(message<isRequest, unsized_body, Allocator> const& msg) noexcept
                : body_(msg.body)
            {
            }

            void
            init(error_code& ec) noexcept
            {
                beast::detail::ignore_unused(ec);
            }

            template<class WriteFunction>
            boost::tribool
            write(resume_context&&, error_code&,
                WriteFunction&& wf) noexcept
            {
                wf(boost::asio::buffer(body_));
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
            writer(message<isRequest, fail_body, Allocator> const& msg) noexcept
                : body_(msg.body)
            {
            }

            void
            init(error_code& ec) noexcept
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

            template<class WriteFunction>
            boost::tribool
            write(resume_context&& rc, error_code& ec,
                WriteFunction&& wf) noexcept
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
                wf(boost::asio::buffer(body_.s_.data() + n_, 1));
                ++n_;
                return n_ == body_.s_.size();
            }
        };
    };

    template<bool isRequest, class Body, class Fields>
    std::string
    str(message<isRequest, Body, Fields> const& m)
    {
        test::string_ostream ss(ios_);
        write(ss, m);
        return ss.str;
    }

    void
    testAsyncWriteHeaders(yield_context do_yield)
    {
        {
            header<true, fields> m;
            m.version = 11;
            m.method = "GET";
            m.url = "/";
            m.fields.insert("User-Agent", "test");
            error_code ec;
            test::string_ostream ss{ios_};
            async_write(ss, m, do_yield[ec]);
            if(BEAST_EXPECTS(! ec, ec.message()))
                BEAST_EXPECT(ss.str ==
                    "GET / HTTP/1.1\r\n"
                    "User-Agent: test\r\n"
                    "\r\n");
        }
        {
            header<false, fields> m;
            m.version = 10;
            m.status = 200;
            m.reason = "OK";
            m.fields.insert("Server", "test");
            m.fields.insert("Content-Length", "5");
            error_code ec;
            test::string_ostream ss{ios_};
            async_write(ss, m, do_yield[ec]);
            if(BEAST_EXPECTS(! ec, ec.message()))
                BEAST_EXPECT(ss.str ==
                    "HTTP/1.0 200 OK\r\n"
                    "Server: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n");
        }
    }

    void
    testAsyncWrite(yield_context do_yield)
    {
        {
            message<false, string_body, fields> m;
            m.version = 10;
            m.status = 200;
            m.reason = "OK";
            m.fields.insert("Server", "test");
            m.fields.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            test::string_ostream ss{ios_};
            async_write(ss, m, do_yield[ec]);
            if(BEAST_EXPECTS(! ec, ec.message()))
                BEAST_EXPECT(ss.str ==
                    "HTTP/1.0 200 OK\r\n"
                    "Server: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****");
        }
        {
            message<false, string_body, fields> m;
            m.version = 11;
            m.status = 200;
            m.reason = "OK";
            m.fields.insert("Server", "test");
            m.fields.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            test::string_ostream ss(ios_);
            async_write(ss, m, do_yield[ec]);
            if(BEAST_EXPECTS(! ec, ec.message()))
                BEAST_EXPECT(ss.str ==
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
                test::string_ostream> fs(fc, ios_);
            message<true, fail_body, fields> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.fields.insert("Content-Length", "5");
            m.body = "*****";
            try
            {
                write(fs, m);
                BEAST_EXPECT(fs.next_layer().str ==
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
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_ostream> fs(fc, ios_);
            message<true, fail_body, fields> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.fields.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            write(fs, m, ec);
            if(ec == boost::asio::error::eof)
            {
                BEAST_EXPECT(fs.next_layer().str ==
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
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_ostream> fs(fc, ios_);
            message<true, fail_body, fields> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.fields.insert("Transfer-Encoding", "chunked");
            m.body = "*****";
            error_code ec;
            async_write(fs, m, do_yield[ec]);
            if(ec == boost::asio::error::eof)
            {
                BEAST_EXPECT(fs.next_layer().str ==
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
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_ostream> fs(fc, ios_);
            message<true, fail_body, fields> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.fields.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            write(fs, m, ec);
            if(! ec)
            {
                BEAST_EXPECT(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****"
                );
                break;
            }
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc(n);
            test::fail_stream<
                test::string_ostream> fs(fc, ios_);
            message<true, fail_body, fields> m(
                std::piecewise_construct,
                    std::forward_as_tuple(fc, ios_));
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.fields.insert("Content-Length", "5");
            m.body = "*****";
            error_code ec;
            async_write(fs, m, do_yield[ec]);
            if(! ec)
            {
                BEAST_EXPECT(fs.next_layer().str ==
                    "GET / HTTP/1.0\r\n"
                    "User-Agent: test\r\n"
                    "Content-Length: 5\r\n"
                    "\r\n"
                    "*****"
                );
                break;
            }
        }
        BEAST_EXPECT(n < limit);
    }

    void
    testOutput()
    {
        // auto content-length HTTP/1.0
        {
            message<true, string_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            BEAST_EXPECT(str(m) ==
                "GET / HTTP/1.0\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*"
            );
        }
        // keep-alive HTTP/1.0
        {
            message<true, string_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m, connection::keep_alive);
            BEAST_EXPECT(str(m) ==
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
            message<true, string_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
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
            message<true, unsized_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 10;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            test::string_ostream ss(ios_);
            error_code ec;
            write(ss, m, ec);
            BEAST_EXPECT(ec == boost::asio::error::eof);
            BEAST_EXPECT(ss.str ==
                "GET / HTTP/1.0\r\n"
                "User-Agent: test\r\n"
                "\r\n"
                "*"
            );
        }
        // auto content-length HTTP/1.1
        {
            message<true, string_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            BEAST_EXPECT(str(m) ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*"
            );
        }
        // close HTTP/1.1
        {
            message<true, string_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m, connection::close);
            test::string_ostream ss(ios_);
            error_code ec;
            write(ss, m, ec);
            BEAST_EXPECT(ec == boost::asio::error::eof);
            BEAST_EXPECT(ss.str ==
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
            message<true, empty_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.fields.insert("User-Agent", "test");
            prepare(m, connection::upgrade);
            BEAST_EXPECT(str(m) ==
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Connection: upgrade\r\n"
                "\r\n"
            );
        }
        // no content-length HTTP/1.1
        {
            message<true, unsized_body, fields> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.fields.insert("User-Agent", "test");
            m.body = "*";
            prepare(m);
            test::string_ostream ss(ios_);
            error_code ec;
            write(ss, m, ec);
            BEAST_EXPECT(ss.str ==
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

    void test_std_ostream()
    {
        // Conversion to std::string via operator<<
        message<true, string_body, fields> m;
        m.method = "GET";
        m.url = "/";
        m.version = 11;
        m.fields.insert("User-Agent", "test");
        m.body = "*";
        BEAST_EXPECT(boost::lexical_cast<std::string>(m) ==
            "GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n*");
        BEAST_EXPECT(boost::lexical_cast<std::string>(m.base()) ==
            "GET / HTTP/1.1\r\nUser-Agent: test\r\n\r\n");
        // Cause exceptions in operator<<
        {
            std::stringstream ss;
            ss.setstate(ss.rdstate() |
                std::stringstream::failbit);
            try
            {
                // header
                ss << m.base();
                fail("", __FILE__, __LINE__);
            }
            catch(std::exception const&)
            {
                pass();
            }
            try
            {
                // message
                ss << m;
                fail("", __FILE__, __LINE__);
            }
            catch(std::exception const&)
            {
                pass();
            }
        }
    }

    void testOstream()
    {
        message<true, string_body, fields> m;
        m.method = "GET";
        m.url = "/";
        m.version = 11;
        m.fields.insert("User-Agent", "test");
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
        yield_to(&write_test::testAsyncWriteHeaders, this);
        yield_to(&write_test::testAsyncWrite, this);
        yield_to(&write_test::testFailures, this);
        testOutput();
        test_std_ostream();
        testOstream();
    }
};

BEAST_DEFINE_TESTSUITE(write,http,beast);

} // http
} // beast
