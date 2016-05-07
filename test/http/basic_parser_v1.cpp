//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/basic_parser_v1.hpp>

#include "message_fuzz.hpp"

#include <beast/streambuf.hpp>
#include <beast/write_streambuf.hpp>
#include <beast/http/rfc2616.hpp>
#include <beast/detail/ci_char_traits.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/utility/string_ref.hpp>
#include <cassert>
#include <climits>
#include <map>
#include <new>
#include <random>
#include <type_traits>

namespace beast {
namespace http {

class basic_parser_v1_test : public beast::unit_test::suite
{
public:
    struct cb_req_checker
    {
        bool method = false;
        bool uri = false;
        bool request = false;
    };

    struct cb_res_checker
    {
        bool reason = false;
        bool response = false;
    };

    template<bool isRequest>
    struct cb_checker
        : public basic_parser_v1<isRequest, cb_checker<isRequest>>
        , std::conditional<isRequest,
            cb_req_checker, cb_res_checker>::type

    {
        bool field = false;
        bool value = false;
        bool headers = false;
        bool body = false;
        bool complete = false;

    private:
        friend class basic_parser_v1<isRequest, cb_checker<isRequest>>;

        void on_method(boost::string_ref const&, error_code&)
        {
            this->method = true;
        }
        void on_uri(boost::string_ref const&, error_code&)
        {
            this->uri = true;
        }
        void on_reason(boost::string_ref const&, error_code&)
        {
            this->reason = true;
        }
        void on_request(error_code&)
        {
            this->request = true;
        }
        void on_response(error_code&)
        {
            this->response = true;
        }
        void on_field(boost::string_ref const&, error_code&)
        {
            field = true;
        }
        void on_value(boost::string_ref const&, error_code&)
        {
            value = true;
        }
        int on_headers(error_code&)
        {
            headers = true;
            return 0;
        }
        void on_body(boost::string_ref const&, error_code&)
        {
            body = true;
        }
        void on_complete(error_code&)
        {
            complete = true;
        }
    };

    template<bool isRequest>
    struct cb_fail
        : public basic_parser_v1<isRequest, cb_fail<isRequest>>

    {
        std::size_t n_;

        void fail(error_code& ec)
        {
            if(n_ > 0)
                --n_;
            if(! n_)
                ec = boost::system::errc::make_error_code(
                    boost::system::errc::invalid_argument);
        }

    private:
        friend class basic_parser_v1<isRequest, cb_checker<isRequest>>;

        void on_method(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        void on_uri(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        void on_reason(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        void on_request(error_code& ec)
        {
            fail(ec);
        }
        void on_response(error_code& ec)
        {
            fail(ec);
        }
        void on_field(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        void on_value(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        int on_headers(error_code& ec)
        {
            fail(ec);
            return 0;
        }
        void on_body(boost::string_ref const&, error_code& ec)
        {
            fail(ec);
        }
        void on_complete(error_code& ec)
        {
            fail(ec);
        }
    };

    //--------------------------------------------------------------------------

    static
    std::string
    escaped_string(boost::string_ref const& s)
    {
        std::string out;
        out.reserve(s.size());
        char const* p = s.data();
        while(p != s.end())
        {
            if(*p == '\r')
                out.append("\\r");
            else if(*p == '\n')
                out.append("\\n");
            else if (*p == '\t')
                out.append("\\t");
            else
                out.append(p, 1);
            ++p;
        }
        return out;
    }

    template<bool isRequest>
    struct null_parser : basic_parser_v1<isRequest, null_parser<isRequest>>
    {
    };

    template<bool isRequest>
    class test_parser :
        public basic_parser_v1<isRequest, test_parser<isRequest>>
    {
        std::string field_;
        std::string value_;

        void check()
        {
            if(! value_.empty())
            {
                rfc2616::trim_right_in_place(value_);
                fields.emplace(field_, value_);
                field_.clear();
                value_.clear();
            }
        }

    public:
        std::map<std::string, std::string,
            beast::detail::ci_less> fields;
        std::string body;

        void on_field(boost::string_ref const& s, error_code&)
        {
            check();
            field_.append(s.data(), s.size());
        }

        void on_value(boost::string_ref const& s, error_code&)
        {
            value_.append(s.data(), s.size());
        }

        int on_headers(error_code&)
        {
            check();
            return 0;
        }

        void on_body(boost::string_ref const& s, error_code&)
        {
            body.append(s.data(), s.size());
        }
    };

    void
    testFail()
    {
        using boost::asio::buffer;
        {
            std::string const s =
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            static std::size_t constexpr limit = 100;
            std::size_t n = 1;
            for(; n < limit; ++n)
            {
                error_code ec;
                basic_parser_v1<true, cb_fail<true>> p;
                p.write(buffer(s), ec);
                if(! ec)
                    break;
            }
            expect(n < limit);
        }
        {
            std::string const s =
                "HTTP/1.1 200 OK\r\n"
                "Server: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            static std::size_t constexpr limit = 100;
            std::size_t n = 1;
            for(; n < limit; ++n)
            {
                error_code ec;
                basic_parser_v1<false, cb_fail<false>> p;
                p.write(buffer(s), ec);
                if(! ec)
                    break;
            }
            expect(n < limit);
        }
    }

    void
    testCallbacks()
    {
        using boost::asio::buffer;
        {
            cb_checker<true> p;
            error_code ec;
            std::string const s =
                "GET / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(buffer(s), ec);
            if( expect(! ec))
            {
                expect(p.method);
                expect(p.uri);
                expect(p.request);
                expect(p.field);
                expect(p.value);
                expect(p.headers);
                expect(p.body);
                expect(p.complete);
            }
        }
        {
            cb_checker<false> p;
            error_code ec;
            std::string const s =
                "HTTP/1.1 200 OK\r\n"
                "Server: test\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                "*";
            p.write(buffer(s), ec);
            if( expect(! ec))
            {
                expect(p.reason);
                expect(p.response);
                expect(p.field);
                expect(p.value);
                expect(p.headers);
                expect(p.body);
                expect(p.complete);
            }
        }
    }

    // Parse the entire input buffer as a valid message,
    // then parse in two pieces of all possible lengths.
    //
    template<class Parser, class F>
    void
    parse(boost::string_ref const& m, F&& f)
    {
        using boost::asio::buffer;
        {
            error_code ec;
            Parser p;
            p.write(buffer(m.data(), m.size()), ec);
            if(expect(p.complete()))
                if(expect(! ec, ec.message()))
                    f(p);
        }
        for(std::size_t i = 1; i < m.size() - 1; ++i)
        {
            error_code ec;
            Parser p;
            p.write(buffer(&m[0], i), ec);
            if(! expect(! ec, ec.message()))
                continue;
            if(p.complete())
            {
                f(p);
            }
            else
            {
                p.write(buffer(&m[i], m.size() - i), ec);
                if(! expect(! ec, ec.message()))
                    continue;
                expect(p.complete());
                f(p);
            }
        }
    }

    // Parse with an expected error code
    //
    template<bool isRequest>
    void
    parse_ev(boost::string_ref const& m, parse_error ev)
    {
        using boost::asio::buffer;
        {
            error_code ec;
            null_parser<isRequest> p;
            p.write(buffer(m.data(), m.size()), ec);
            if(expect(! p.complete()))
                expect(ec == ev, ec.message());
        }
        for(std::size_t i = 1; i < m.size() - 1; ++i)
        {
            error_code ec;
            null_parser<isRequest> p;
            p.write(buffer(&m[0], i), ec);
            if(! expect(! p.complete()))
                continue;
            if(ec)
            {
                expect(ec == ev, ec.message());
                continue;
            }
            p.write(buffer(&m[i], m.size() - i), ec);
            if(! expect(! p.complete()))
                continue;
            if(! expect(ec == ev, ec.message()))
                continue;
        }
    }

    //--------------------------------------------------------------------------

    // Parse a valid message with expected version
    //
    template<bool isRequest>
    void
    version(boost::string_ref const& m,
        unsigned major, unsigned minor)
    {
        parse<null_parser<isRequest>>(m,
            [&](null_parser<isRequest> const& p)
            {
                expect(p.http_major() == major);
                expect(p.http_minor() == minor);
            });
    }

    // Parse a valid message with expected flags mask
    //
    void
    checkf(boost::string_ref const& m, std::uint8_t mask)
    {
        parse<null_parser<true>>(m,
            [&](null_parser<true> const& p)
            {
                expect(p.flags() & mask);
            });
    }

    void
    testVersion()
    {
        version<true>("GET / HTTP/0.0\r\n\r\n", 0, 0);
        version<true>("GET / HTTP/0.1\r\n\r\n", 0, 1);
        version<true>("GET / HTTP/0.9\r\n\r\n", 0, 9);
        version<true>("GET / HTTP/1.0\r\n\r\n", 1, 0);
        version<true>("GET / HTTP/1.1\r\n\r\n", 1, 1);
        version<true>("GET / HTTP/9.9\r\n\r\n", 9, 9);
        version<true>("GET / HTTP/999.999\r\n\r\n", 999, 999);
        parse_ev<true>("GET / HTTP/1000.0\r\n\r\n", parse_error::bad_version);
        parse_ev<true>("GET / HTTP/0.1000\r\n\r\n", parse_error::bad_version);
        parse_ev<true>("GET / HTTP/99999999999999999999.0\r\n\r\n", parse_error::bad_version);
        parse_ev<true>("GET / HTTP/0.99999999999999999999\r\n\r\n", parse_error::bad_version);
    }

    void
    testConnection(std::string const& token,
        std::uint8_t flag)
    {
        checkf("GET / HTTP/1.1\r\nConnection:" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: " + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection:\t" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: \t" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: " + token + " \r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: " + token + "\t\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: " + token + " \t\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: " + token + "\t \r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: \r\n" " " + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection:\t\r\n" " " + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: \r\n" "\t" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection:\t\r\n" "\t" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X," + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X, " + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X,\t" + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X,\t " + token + "\r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X," + token + " \r\n\r\n", flag);
        checkf("GET / HTTP/1.1\r\nConnection: X," + token + "\t\r\n\r\n", flag);
    }

    void
    testContentLength()
    {
        std::size_t const length = 0;
        std::string const length_s =
            std::to_string(length);

        checkf("GET / HTTP/1.1\r\nContent-Length:"+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: "+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length:\t"+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: \t"+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: "+ length_s + " \r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: "+ length_s + "\t\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: "+ length_s + " \t\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: "+ length_s + "\t \r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: \r\n" " "+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length:\t\r\n" " "+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length: \r\n" "\t"+ length_s + "\r\n\r\n", parse_flag::contentlength);
        checkf("GET / HTTP/1.1\r\nContent-Length:\t\r\n" "\t"+ length_s + "\r\n\r\n", parse_flag::contentlength);
    }

    void
    testTransferEncoding()
    {
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding:chunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding:\tchunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: \tchunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: chunked \r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\t\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: chunked \t\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\t \r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: \r\n" " chunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding:\t\r\n" " chunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding: \r\n" "\tchunked\r\n\r\n0\r\n\r\n", parse_flag::chunked);
        checkf("GET / HTTP/1.1\r\nTransfer-Encoding:\t\r\n" "\tchunked\r\n\r\n0\r\n\r\n", parse_flag::chunked );
    }

    void
    testFlags()
    {
        testConnection("keep-alive",
            parse_flag::connection_keep_alive);

        testConnection("close",
            parse_flag::connection_close);

        testConnection("upgrade",
            parse_flag::connection_upgrade);

        testContentLength();

        testTransferEncoding();

        checkf(
            "GET / HTTP/1.1\r\n"
            "Upgrade: x\r\n"
            "\r\n",
            parse_flag::upgrade
        );

        parse_ev<true>(
            "GET / HTTP/1.1\r\n"
            "Transfer-Encoding:chunked\r\n"
            "Content-Length: 0\r\n"
            "\r\n", parse_error::illegal_content_length);
    }

    void
    testUpgrade()
    {
        using boost::asio::buffer;
        null_parser<true> p;
        boost::string_ref s =
            "GET / HTTP/1.1\r\nConnection: upgrade\r\nUpgrade: WebSocket\r\n\r\n";
        error_code ec;
        p.write(buffer(s.data(), s.size()), ec);
        if(! expect(! ec, ec.message()))
            return;
        expect(p.complete());
        expect(p.upgrade());
    }

    void testBad()
    {
        parse_ev<true>(" ",                     parse_error::bad_method);
        parse_ev<true>(" G",                    parse_error::bad_method);
        parse_ev<true>("G:",                    parse_error::bad_request);
        parse_ev<true>("GET  /",                parse_error::bad_uri);
        parse_ev<true>("GET / X",               parse_error::bad_version);
        parse_ev<true>("GET / HX",              parse_error::bad_version);
        parse_ev<true>("GET / HTTX",            parse_error::bad_version);
        parse_ev<true>("GET / HTTPX",           parse_error::bad_version);
        parse_ev<true>("GET / HTTP/.",          parse_error::bad_version);
        parse_ev<true>("GET / HTTP/1000",       parse_error::bad_version);
        parse_ev<true>("GET / HTTP/1. ",        parse_error::bad_version);
        parse_ev<true>("GET / HTTP/1.1000",     parse_error::bad_version);
        parse_ev<true>("GET / HTTP/1.1\r ",     parse_error::bad_crlf);
        parse_ev<true>("GET / HTTP/1.1\r\nf :", parse_error::bad_field);
    }

    void testCorrupt()
    {
        using boost::asio::buffer;
        std::string s;
        for(std::size_t n = 0;;++n)
        {
            // Create a request and set one octet to an invalid char
            s =
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 00\r\n"
                "\r\n";
            auto const len = s.size();
            if(n >= s.size())
            {
                pass();
                break;
            }
            s[n] = 0;
            for(std::size_t m = 1; m < len - 1; ++m)
            {
                null_parser<true> p;
                error_code ec;
                p.write(buffer(s.data(), m), ec);
                if(ec)
                {
                    pass();
                    continue;
                }
                p.write(buffer(s.data() + m, len - m), ec);
                expect(ec);
            }
        }
    }

    void
    testRandomReq(std::size_t N)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        message_fuzz mg;
        for(std::size_t i = 0; i < N; ++i)
        {
            std::string s;
            {
                streambuf sb;
                mg.request(sb);
                s.reserve(buffer_size(sb.data()));
                for(auto const& b : sb.data())
                    s.append(buffer_cast<char const*>(b),
                        buffer_size(b));
            }
            null_parser<true> p;
            for(std::size_t j = 1; j < s.size() - 1; ++j)
            {
                error_code ec;
                p.write(buffer(&s[0], j), ec);
                if(! expect(! ec, ec.message()))
                {
                    log << escaped_string(s);
                    break;
                }
                if(! p.complete())
                {
                    p.write(buffer(&s[j], s.size() - j), ec);
                    if(! expect(! ec, ec.message()))
                    {
                        log << escaped_string(s);
                        break;
                    }
                }
                if(! expect(p.complete()))
                    break;
                if(! p.keep_alive())
                {
                    p.~null_parser();
                    new(&p) null_parser<true>{};
                }
            }
        }
    }

    void
    testRandomResp(std::size_t N)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        message_fuzz mg;
        for(std::size_t i = 0; i < N; ++i)
        {
            std::string s;
            {
                streambuf sb;
                mg.response(sb);
                s.reserve(buffer_size(sb.data()));
                for(auto const& b : sb.data())
                    s.append(buffer_cast<char const*>(b),
                        buffer_size(b));
            }
            null_parser<false> p;
            for(std::size_t j = 1; j < s.size() - 1; ++j)
            {
                error_code ec;
                p.write(buffer(&s[0], j), ec);
                if(! expect(! ec, ec.message()))
                {
                    log << escaped_string(s);
                    break;
                }
                if(! p.complete())
                {
                    p.write(buffer(&s[j], s.size() - j), ec);
                    if(! expect(! ec, ec.message()))
                    {
                        log << escaped_string(s);
                        break;
                    }
                }
                if(! expect(p.complete()))
                    break;
                if(! p.keep_alive())
                {
                    p.~null_parser();
                    new(&p) null_parser<false>{};
                }
            }
        }
    }

    void testBody()
    {
        auto match =
            [&](std::string const& body)
            {
                return
                    [&](test_parser<true> const& p)
                    {
                        expect(p.body == body);
                    };
            };
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nContent-Length: 1\r\n\r\n123", match("1"));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nContent-Length: 3\r\n\r\n123", match("123"));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nContent-Length: 0\r\n\r\n", match(""));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "1\r\n"
            "a\r\n"
            "0\r\n"
            "\r\n", match("a"));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "2\r\n"
            "ab\r\n"
            "0\r\n"
            "\r\n", match("ab"));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "2\r\n"
            "ab\r\n"
            "1\r\n"
            "c\r\n"
            "0\r\n"
            "\r\n", match("abc"));
        parse<test_parser<true>>(
            "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
            "10\r\n"
            "1234567890123456\r\n"
            "0\r\n"
            "\r\n", match("1234567890123456"));
    }

    void run() override
    {
        testFail();
        testCallbacks();
        testVersion();
        testFlags();
        testUpgrade();
        testBad();
        testCorrupt();
        testRandomReq(100);
        testRandomResp(100);
        testBody();
    }
};

BEAST_DEFINE_TESTSUITE(basic_parser_v1,http,beast);

} // http
} // beast
