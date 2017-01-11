//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/basic_parser_v1.hpp>

#include "fail_parser.hpp"

#include <beast/core/buffer_cat.hpp>
#include <beast/core/detail/ci_char_traits.hpp>
#include <beast/http/rfc7230.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/assert.hpp>
#include <boost/utility/string_ref.hpp>
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
        bool start = false;
        bool field = false;
        bool value = false;
        bool fields = false;
        bool _body_what = false;
        bool body = false;
        bool complete = false;

    private:
        friend class basic_parser_v1<isRequest, cb_checker<isRequest>>;

        void on_start(error_code&)
        {
            this->start = true;
        }
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
        void
        on_header(std::uint64_t, error_code&)
        {
            fields = true;
        }
        body_what
        on_body_what(std::uint64_t, error_code&)
        {
            _body_what = true;
            return body_what::normal;
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

    // Check that all callbacks are invoked
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
            if(BEAST_EXPECT(! ec))
            {
                BEAST_EXPECT(p.start);
                BEAST_EXPECT(p.method);
                BEAST_EXPECT(p.uri);
                BEAST_EXPECT(p.request);
                BEAST_EXPECT(p.field);
                BEAST_EXPECT(p.value);
                BEAST_EXPECT(p.fields);
                BEAST_EXPECT(p._body_what);
                BEAST_EXPECT(p.body);
                BEAST_EXPECT(p.complete);
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
            if(BEAST_EXPECT(! ec))
            {
                BEAST_EXPECT(p.start);
                BEAST_EXPECT(p.reason);
                BEAST_EXPECT(p.response);
                BEAST_EXPECT(p.field);
                BEAST_EXPECT(p.value);
                BEAST_EXPECT(p.fields);
                BEAST_EXPECT(p.body);
                BEAST_EXPECT(p.complete);
            }
        }
    }

    //--------------------------------------------------------------------------

    template<class F>
    static
    void
    for_split(boost::string_ref const& s, F const& f)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        for(std::size_t i = 0; i < s.size(); ++i)
        {
            // Use separately allocated buffers so
            // address sanitizer has something to chew on.
            //
            auto const n1 = s.size() - i;
            auto const n2 = i;
            std::unique_ptr<char[]> p1(new char[n1]);
            std::unique_ptr<char[]> p2(new char[n2]);
            buffer_copy(buffer(p1.get(), n1), buffer(s.data(), n1));
            buffer_copy(buffer(p2.get(), n2), buffer(s.data() + n1, n2));
            f(
                boost::string_ref{p1.get(), n1},
                boost::string_ref{p2.get(), n2});
        }
    }

    struct none
    {
        template<class Parser>
        void
        operator()(Parser const&) const
        {
        }
    };

    template<bool isRequest, class F>
    void
    good(body_what onBodyRv, std::string const& s, F const& f)
    {
        using boost::asio::buffer;
        for_split(s,
            [&](boost::string_ref const& s1, boost::string_ref const& s2)
            {
                static std::size_t constexpr Limit = 200;
                std::size_t n;
                for(n = 0; n < Limit; ++n)
                {
                    test::fail_counter fc(n);
                    fail_parser<isRequest> p(fc);
                    p.on_body_rv(onBodyRv);
                    error_code ec;
                    p.write(buffer(s1.data(), s1.size()), ec);
                    if(ec == test::error::fail_error)
                        continue;
                    if(! BEAST_EXPECT(! ec))
                        break;
                    if(! BEAST_EXPECT(s2.empty() || ! p.complete()))
                        break;
                    p.write(buffer(s2.data(), s2.size()), ec);
                    if(ec == test::error::fail_error)
                        continue;
                    if(! BEAST_EXPECT(! ec))
                        break;
                    p.write_eof(ec);
                    if(ec == test::error::fail_error)
                        continue;
                    if(! BEAST_EXPECT(! ec))
                        break;
                    BEAST_EXPECT(p.complete());
                    f(p);
                    break;
                }
                BEAST_EXPECT(n < Limit);
            });
    }

    template<bool isRequest, class F = none>
    void
    good(std::string const& s, F const& f = {})
    {
        return good<isRequest>(body_what::normal, s, f);
    }

    template<bool isRequest>
    void
    bad(body_what onBodyRv, std::string const& s, error_code ev)
    {
        using boost::asio::buffer;
        for_split(s,
            [&](boost::string_ref const& s1, boost::string_ref const& s2)
            {
                static std::size_t constexpr Limit = 200;
                std::size_t n;
                for(n = 0; n < Limit; ++n)
                {
                    test::fail_counter fc(n);
                    fail_parser<isRequest> p(fc);
                    p.on_body_rv(onBodyRv);
                    error_code ec;
                    p.write(buffer(s1.data(), s1.size()), ec);
                    if(ec == test::error::fail_error)
                        continue;
                    if(ec)
                    {
                        BEAST_EXPECT((ec && ! ev) || ec == ev);
                        break;
                    }
                    if(! BEAST_EXPECT(! p.complete()))
                        break;
                    if(! s2.empty())
                    {
                        p.write(buffer(s2.data(), s2.size()), ec);
                        if(ec == test::error::fail_error)
                            continue;
                        if(ec)
                        {
                            BEAST_EXPECT((ec && ! ev) || ec == ev);
                            break;
                        }
                        if(! BEAST_EXPECT(! p.complete()))
                            break;
                    }
                    p.write_eof(ec);
                    if(ec == test::error::fail_error)
                        continue;
                    BEAST_EXPECT(! p.complete());
                    BEAST_EXPECT((ec && ! ev) || ec == ev);
                    break;
                }
                BEAST_EXPECT(n < Limit);
            });
    }

    template<bool isRequest>
    void
    bad(std::string const& s, error_code ev = {})
    {
        return bad<isRequest>(body_what::normal, s, ev);
    }

    //--------------------------------------------------------------------------

    class version
    {
        suite& s_;
        unsigned major_;
        unsigned minor_;

    public:
        version(suite& s, unsigned major, unsigned minor)
            : s_(s)
            , major_(major)
            , minor_(minor)
        {
        }

        template<class Parser>
        void
        operator()(Parser const& p) const
        {
            s_.BEAST_EXPECT(p.http_major() == major_);
            s_.BEAST_EXPECT(p.http_minor() == minor_);
        }
    };

    class status
    {
        suite& s_;
        unsigned code_;
    public:
        status(suite& s, int code)
            : s_(s)
            , code_(code)
        {
        }

        template<class Parser>
        void
        operator()(Parser const& p) const
        {
            s_.BEAST_EXPECT(p.status_code() == code_);
        }
    };

    void testRequestLine()
    {
    /*
        request-line    = method SP request-target SP HTTP-version CRLF
        method          = token
        request-target  = origin-form / absolute-form / authority-form / asterisk-form
        HTTP-version    = "HTTP/" DIGIT "." DIGIT
    */
        good<true>("GET /x HTTP/1.0\r\n\r\n");
        good<true>("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz / HTTP/1.0\r\n\r\n");
        good<true>("GET / HTTP/1.0\r\n\r\n",            version{*this, 1, 0});
        good<true>("G / HTTP/1.1\r\n\r\n",              version{*this, 1, 1});
        // VFALCO TODO various forms of good request-target (uri)
        good<true>("GET / HTTP/0.1\r\n\r\n",            version{*this, 0, 1});
        good<true>("GET / HTTP/2.3\r\n\r\n",            version{*this, 2, 3});
        good<true>("GET / HTTP/4.5\r\n\r\n",            version{*this, 4, 5});
        good<true>("GET / HTTP/6.7\r\n\r\n",            version{*this, 6, 7});
        good<true>("GET / HTTP/8.9\r\n\r\n",            version{*this, 8, 9});

        bad<true>("\tGET / HTTP/1.0\r\n"    "\r\n",     parse_error::bad_method);
        bad<true>("GET\x01 / HTTP/1.0\r\n"  "\r\n",     parse_error::bad_method);
        bad<true>("GET  / HTTP/1.0\r\n"     "\r\n",     parse_error::bad_uri);
        bad<true>("GET \x01 HTTP/1.0\r\n"   "\r\n",     parse_error::bad_uri);
        bad<true>("GET /\x01 HTTP/1.0\r\n"  "\r\n",     parse_error::bad_uri);
        // VFALCO TODO various forms of bad request-target (uri)
        bad<true>("GET /  HTTP/1.0\r\n"     "\r\n",     parse_error::bad_version);
        bad<true>("GET / _TTP/1.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / H_TP/1.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HT_P/1.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTT_/1.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP_1.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/01.2\r\n"     "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/3.45\r\n"     "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/67.89\r\n"    "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/x.0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1.x\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1.0 \r\n"     "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1_0\r\n"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1.0\n"        "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1.0\n\r"      "\r\n",     parse_error::bad_version);
        bad<true>("GET / HTTP/1.0\r\r\n"    "\r\n",     parse_error::bad_crlf);

        // write a bad request line in 2 pieces
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buffer_cat(
                buf("GET / "), buf("_TTP/1.1\r\n"),
                buf("\r\n")
                ), ec);
            BEAST_EXPECT(ec == parse_error::bad_version);
        }
    }

    void testStatusLine()
    {
    /*
        status-line     = HTTP-version SP status-code SP reason-phrase CRLF
        HTTP-version    = "HTTP/" DIGIT "." DIGIT
        status-code     = 3DIGIT
        reason-phrase   = *( HTAB / SP / VCHAR / obs-text )
    */
        good<false>("HTTP/0.1 200 OK\r\n"   "\r\n",     version{*this, 0, 1});
        good<false>("HTTP/2.3 200 OK\r\n"   "\r\n",     version{*this, 2, 3});
        good<false>("HTTP/4.5 200 OK\r\n"   "\r\n",     version{*this, 4, 5});
        good<false>("HTTP/6.7 200 OK\r\n"   "\r\n",     version{*this, 6, 7});
        good<false>("HTTP/8.9 200 OK\r\n"   "\r\n",     version{*this, 8, 9});
        good<false>("HTTP/1.0 000 OK\r\n"   "\r\n",     status{*this, 0});
        good<false>("HTTP/1.1 012 OK\r\n"   "\r\n",     status{*this, 12});
        good<false>("HTTP/1.0 345 OK\r\n"   "\r\n",     status{*this, 345});
        good<false>("HTTP/1.0 678 OK\r\n"   "\r\n",     status{*this, 678});
        good<false>("HTTP/1.0 999 OK\r\n"   "\r\n",     status{*this, 999});
        good<false>("HTTP/1.0 200 \tX\r\n"  "\r\n",     version{*this, 1, 0});
        good<false>("HTTP/1.1 200  X\r\n"   "\r\n",     version{*this, 1, 1});
        good<false>("HTTP/1.0 200 \r\n"     "\r\n");
        good<false>("HTTP/1.1 200 X \r\n"   "\r\n");
        good<false>("HTTP/1.1 200 X\t\r\n"  "\r\n");
        good<false>("HTTP/1.1 200 \x80\x81...\xfe\xff\r\n\r\n");
        good<false>("HTTP/1.1 200 !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\r\n\r\n");

        bad<false>("\rHTTP/1.0 200 OK\r\n"  "\r\n",     parse_error::bad_version);
        bad<false>("\nHTTP/1.0 200 OK\r\n"  "\r\n",     parse_error::bad_version);
        bad<false>(" HTTP/1.0 200 OK\r\n"   "\r\n",     parse_error::bad_version);
        bad<false>("_TTP/1.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("H_TP/1.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HT_P/1.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTT_/1.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTTP_1.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/01.2 200 OK\r\n"   "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/3.45 200 OK\r\n"   "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/67.89 200 OK\r\n"  "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/x.0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/1.x 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/1_0 200 OK\r\n"    "\r\n",     parse_error::bad_version);
        bad<false>("HTTP/1.0  200 OK\r\n"   "\r\n",     parse_error::bad_status);
        bad<false>("HTTP/1.0 0 OK\r\n"      "\r\n",     parse_error::bad_status);
        bad<false>("HTTP/1.0 12 OK\r\n"     "\r\n",     parse_error::bad_status);
        bad<false>("HTTP/1.0 3456 OK\r\n"   "\r\n",     parse_error::bad_status);
        bad<false>("HTTP/1.0 200\r\n"       "\r\n",     parse_error::bad_status);
        bad<false>("HTTP/1.0 200 \n"        "\r\n",     parse_error::bad_reason);
        bad<false>("HTTP/1.0 200 \x01\r\n"  "\r\n",     parse_error::bad_reason);
        bad<false>("HTTP/1.0 200 \x7f\r\n"  "\r\n",     parse_error::bad_reason);
        bad<false>("HTTP/1.0 200 OK\n"      "\r\n",     parse_error::bad_reason);
        bad<false>("HTTP/1.0 200 OK\r\r\n"  "\r\n",     parse_error::bad_crlf);
    }

    //--------------------------------------------------------------------------

    void testHeaders()
    {
    /*
        header-field   = field-name ":" OWS field-value OWS
        field-name     = token
        field-value    = *( field-content / obs-fold )
        field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
        field-vchar    = VCHAR / obs-text
        obs-fold       = CRLF 1*( SP / HTAB )
                       ; obsolete line folding
    */
        auto const m =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\n" + s + "\r\n";
            };
        good<true>(m("f:\r\n"));
        good<true>(m("f: \r\n"));
        good<true>(m("f:\t\r\n"));
        good<true>(m("f: \t\r\n"));
        good<true>(m("f: v\r\n"));
        good<true>(m("f:\tv\r\n"));
        good<true>(m("f:\tv \r\n"));
        good<true>(m("f:\tv\t\r\n"));
        good<true>(m("f:\tv\t \r\n"));
        good<true>(m("f:\r\n \r\n"));
        good<true>(m("f:v\r\n"));
        good<true>(m("f: v\r\n u\r\n"));
        good<true>(m("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz: v\r\n"));
        good<true>(m("f: !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~\x80\x81...\xfe\xff\r\n"));

        bad<true>(m(" f: v\r\n"),                   parse_error::bad_field);
        bad<true>(m("\tf: v\r\n"),                  parse_error::bad_field);
        bad<true>(m("f : v\r\n"),                   parse_error::bad_field);
        bad<true>(m("f\t: v\r\n"),                  parse_error::bad_field);
        bad<true>(m("f: \n\r\n"),                   parse_error::bad_value);
        bad<true>(m("f: v\r \r\n"),                 parse_error::bad_crlf);
        bad<true>(m("f: \r v\r\n"),                 parse_error::bad_crlf);
        bad<true>("GET / HTTP/1.1\r\n\r \n",        parse_error::bad_crlf);
    }

    //--------------------------------------------------------------------------

    class flags
    {
        suite& s_;
        std::size_t value_;

    public:
        flags(suite& s, std::size_t value)
            : s_(s)
            , value_(value)
        {
        }

        template<class Parser>
        void
        operator()(Parser const& p) const
        {
            s_.BEAST_EXPECT(p.flags() == value_);
        }
    };

    class keepalive_f
    {
        suite& s_;
        bool value_;

    public:
        keepalive_f(suite& s, bool value)
            : s_(s)
            , value_(value)
        {
        }

        template<class Parser>
        void
        operator()(Parser const& p) const
        {
            s_.BEAST_EXPECT(p.keep_alive() == value_);
        }
    };

    void testConnectionHeader()
    {
        auto const m =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\n" + s + "\r\n";
            };
        auto const cn =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\nConnection: " + s + "\r\n";
            };
        auto const keepalive =
            [&](bool v)
            {
                return keepalive_f{*this, v};
            };

        good<true>(cn("close\r\n"),                         flags{*this, parse_flag::connection_close});
        good<true>(cn(",close\r\n"),                        flags{*this, parse_flag::connection_close});
        good<true>(cn(" close\r\n"),                        flags{*this, parse_flag::connection_close});
        good<true>(cn("\tclose\r\n"),                       flags{*this, parse_flag::connection_close});
        good<true>(cn("close,\r\n"),                        flags{*this, parse_flag::connection_close});
        good<true>(cn("close\t\r\n"),                       flags{*this, parse_flag::connection_close});
        good<true>(cn("close\r\n"),                         flags{*this, parse_flag::connection_close});
        good<true>(cn(" ,\t,,close,, ,\t,,\r\n"),           flags{*this, parse_flag::connection_close});
        good<true>(cn("\r\n close\r\n"),                    flags{*this, parse_flag::connection_close});
        good<true>(cn("close\r\n \r\n"),                    flags{*this, parse_flag::connection_close});
        good<true>(cn("any,close\r\n"),                     flags{*this, parse_flag::connection_close});
        good<true>(cn("close,any\r\n"),                     flags{*this, parse_flag::connection_close});
        good<true>(cn("any\r\n ,close\r\n"),                flags{*this, parse_flag::connection_close});
        good<true>(cn("close\r\n ,any\r\n"),                flags{*this, parse_flag::connection_close});
        good<true>(cn("close,close\r\n"),                   flags{*this, parse_flag::connection_close}); // weird but allowed

        good<true>(cn("keep-alive\r\n"),                    flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("keep-alive \r\n"),                   flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("keep-alive\t \r\n"),                 flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("keep-alive\t ,x\r\n"),               flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("\r\n keep-alive \t\r\n"),            flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("keep-alive \r\n \t \r\n"),           flags{*this, parse_flag::connection_keep_alive});
        good<true>(cn("keep-alive\r\n \r\n"),               flags{*this, parse_flag::connection_keep_alive});

        good<true>(cn("upgrade\r\n"),                       flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("upgrade \r\n"),                      flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("upgrade\t \r\n"),                    flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("upgrade\t ,x\r\n"),                  flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("\r\n upgrade \t\r\n"),               flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("upgrade \r\n \t \r\n"),              flags{*this, parse_flag::connection_upgrade});
        good<true>(cn("upgrade\r\n \r\n"),                  flags{*this, parse_flag::connection_upgrade});

        good<true>(cn("close,keep-alive\r\n"),              flags{*this, parse_flag::connection_close | parse_flag::connection_keep_alive});
        good<true>(cn("upgrade,keep-alive\r\n"),            flags{*this, parse_flag::connection_upgrade | parse_flag::connection_keep_alive});
        good<true>(cn("upgrade,\r\n keep-alive\r\n"),       flags{*this, parse_flag::connection_upgrade | parse_flag::connection_keep_alive});
        good<true>(cn("close,keep-alive,upgrade\r\n"),      flags{*this, parse_flag::connection_close | parse_flag::connection_keep_alive | parse_flag::connection_upgrade});

        good<true>("GET / HTTP/1.1\r\n\r\n",                keepalive(true));
        good<true>("GET / HTTP/1.0\r\n\r\n",                keepalive(false));
        good<true>("GET / HTTP/1.0\r\n"
                   "Connection: keep-alive\r\n\r\n",        keepalive(true));
        good<true>("GET / HTTP/1.1\r\n"
                   "Connection: close\r\n\r\n",             keepalive(false));

        good<true>(cn("x\r\n"),                             flags{*this, 0});
        good<true>(cn("x,y\r\n"),                           flags{*this, 0});
        good<true>(cn("x ,y\r\n"),                          flags{*this, 0});
        good<true>(cn("x\t,y\r\n"),                         flags{*this, 0});
        good<true>(cn("keep\r\n"),                          flags{*this, 0});
        good<true>(cn(",keep\r\n"),                         flags{*this, 0});
        good<true>(cn(" keep\r\n"),                         flags{*this, 0});
        good<true>(cn("\tnone\r\n"),                        flags{*this, 0});
        good<true>(cn("keep,\r\n"),                         flags{*this, 0});
        good<true>(cn("keep\t\r\n"),                        flags{*this, 0});
        good<true>(cn("keep\r\n"),                          flags{*this, 0});
        good<true>(cn(" ,\t,,keep,, ,\t,,\r\n"),            flags{*this, 0});
        good<true>(cn("\r\n keep\r\n"),                     flags{*this, 0});
        good<true>(cn("keep\r\n \r\n"),                     flags{*this, 0});
        good<true>(cn("closet\r\n"),                        flags{*this, 0});
        good<true>(cn(",closet\r\n"),                       flags{*this, 0});
        good<true>(cn(" closet\r\n"),                       flags{*this, 0});
        good<true>(cn("\tcloset\r\n"),                      flags{*this, 0});
        good<true>(cn("closet,\r\n"),                       flags{*this, 0});
        good<true>(cn("closet\t\r\n"),                      flags{*this, 0});
        good<true>(cn("closet\r\n"),                        flags{*this, 0});
        good<true>(cn(" ,\t,,closet,, ,\t,,\r\n"),          flags{*this, 0});
        good<true>(cn("\r\n closet\r\n"),                   flags{*this, 0});
        good<true>(cn("closet\r\n \r\n"),                   flags{*this, 0});
        good<true>(cn("clog\r\n"),                          flags{*this, 0});
        good<true>(cn("key\r\n"),                           flags{*this, 0});
        good<true>(cn("uptown\r\n"),                        flags{*this, 0});
        good<true>(cn("keeper\r\n \r\n"),                   flags{*this, 0});
        good<true>(cn("keep-alively\r\n \r\n"),             flags{*this, 0});
        good<true>(cn("up\r\n \r\n"),                       flags{*this, 0});
        good<true>(cn("upgrader\r\n \r\n"),                 flags{*this, 0});
        good<true>(cn("none\r\n"),                          flags{*this, 0});
        good<true>(cn("\r\n none\r\n"),                     flags{*this, 0});

        good<true>(m("ConnectioX: close\r\n"),              flags{*this, 0});
        good<true>(m("Condor: close\r\n"),                  flags{*this, 0});
        good<true>(m("Connect: close\r\n"),                 flags{*this, 0});
        good<true>(m("Connections: close\r\n"),             flags{*this, 0});

        good<true>(m("Proxy-Connection: close\r\n"),        flags{*this, parse_flag::connection_close});
        good<true>(m("Proxy-Connection: keep-alive\r\n"),   flags{*this, parse_flag::connection_keep_alive});
        good<true>(m("Proxy-Connection: upgrade\r\n"),      flags{*this, parse_flag::connection_upgrade});
        good<true>(m("Proxy-ConnectioX: none\r\n"),         flags{*this, 0});
        good<true>(m("Proxy-Connections: 1\r\n"),           flags{*this, 0});
        good<true>(m("Proxy-Connotes: see-also\r\n"),       flags{*this, 0});

        bad<true>(cn("["),                                  parse_error::bad_value);
        bad<true>(cn("\"\r\n"),                             parse_error::bad_value);
        bad<true>(cn("close[\r\n"),                         parse_error::bad_value);
        bad<true>(cn("close [\r\n"),                        parse_error::bad_value);
        bad<true>(cn("close, upgrade [\r\n"),               parse_error::bad_value);
        bad<true>(cn("upgrade[]\r\n"),                      parse_error::bad_value);
        bad<true>(cn("keep\r\n -alive\r\n"),                parse_error::bad_value);
        bad<true>(cn("keep-alive[\r\n"),                    parse_error::bad_value);
        bad<true>(cn("keep-alive []\r\n"),                  parse_error::bad_value);
        bad<true>(cn("no[ne]\r\n"),                         parse_error::bad_value);
    }

    void testContentLengthHeader()
    {
        auto const length =
            [&](std::string const& s, std::uint64_t v)
            {
                good<true>(body_what::skip, s,
                    [&](fail_parser<true> const& p)
                    {
                        BEAST_EXPECT(p.content_length() == v);
                        if(v != no_content_length)
                            BEAST_EXPECT(p.flags() & parse_flag::contentlength);
                    });
            };
        auto const c =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\nContent-Length: " + s + "\r\n";
            };
        auto const m =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\n" + s + "\r\n";
            };

        length(c("0\r\n"),                                  0);
        length(c("00\r\n"),                                 0);
        length(c("1\r\n"),                                  1);
        length(c("01\r\n"),                                 1);
        length(c("9\r\n"),                                  9);
        length(c("123456789\r\n"),                          123456789);
        length(c("42 \r\n"),                                42);
        length(c("42\t\r\n"),                               42);
        length(c("42 \t \r\n"),                             42);
        length(c("42\r\n \t \r\n"),                         42);

        good<true>(m("Content-LengtX: 0\r\n"),              flags{*this, 0});
        good<true>(m("Content-Lengths: many\r\n"),          flags{*this, 0});
        good<true>(m("Content: full\r\n"),                  flags{*this, 0});

        bad<true>(c("\r\n"),                                parse_error::bad_content_length);
        bad<true>(c("18446744073709551616\r\n"),            parse_error::bad_content_length);
        bad<true>(c("0 0\r\n"),                             parse_error::bad_content_length);
        bad<true>(c("0 1\r\n"),                             parse_error::bad_content_length);
        bad<true>(c(",\r\n"),                               parse_error::bad_content_length);
        bad<true>(c("0,\r\n"),                              parse_error::bad_content_length);
        bad<true>(m(
            "Content-Length: 0\r\nContent-Length: 0\r\n"),  parse_error::bad_content_length);
    }

    void testTransferEncodingHeader()
    {
        auto const m =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\n" + s + "\r\n";
            };
        auto const ce =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\nTransfer-Encoding: " + s + "\r\n0\r\n\r\n";
            };
        auto const te =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\nTransfer-Encoding: " + s + "\r\n";
            };
        good<true>(ce("chunked\r\n"),                       flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked \r\n"),                      flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked\t\r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked \t\r\n"),                    flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(" chunked\r\n"),                      flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("\tchunked\r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked,\r\n"),                      flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked ,\r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked, \r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(",chunked\r\n"),                      flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(", chunked\r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(" ,chunked\r\n"),                     flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("chunked\r\n \r\n"),                  flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("\r\n chunked\r\n"),                  flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(",\r\n chunked\r\n"),                 flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("\r\n ,chunked\r\n"),                 flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce(",\r\n chunked\r\n"),                 flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("gzip, chunked\r\n"),                 flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("gzip, chunked \r\n"),                flags{*this, parse_flag::chunked | parse_flag::trailing});
        good<true>(ce("gzip, \r\n chunked\r\n"),            flags{*this, parse_flag::chunked | parse_flag::trailing});

        // Technically invalid but beyond the parser's scope to detect
        good<true>(ce("custom;key=\",chunked\r\n"),         flags{*this, parse_flag::chunked  | parse_flag::trailing});

        good<true>(te("gzip\r\n"),                          flags{*this, 0});
        good<true>(te("chunked, gzip\r\n"),                 flags{*this, 0});
        good<true>(te("chunked\r\n , gzip\r\n"),            flags{*this, 0});
        good<true>(te("chunked,\r\n gzip\r\n"),             flags{*this, 0});
        good<true>(te("chunked,\r\n ,gzip\r\n"),            flags{*this, 0});
        good<true>(te("bigchunked\r\n"),                    flags{*this, 0});
        good<true>(te("chunk\r\n ked\r\n"),                 flags{*this, 0});
        good<true>(te("bar\r\n ley chunked\r\n"),           flags{*this, 0});
        good<true>(te("barley\r\n chunked\r\n"),            flags{*this, 0});

        good<true>(m("Transfer-EncodinX: none\r\n"),        flags{*this, 0});
        good<true>(m("Transfer-Encodings: 2\r\n"),          flags{*this, 0});
        good<true>(m("Transfer-Encoded: false\r\n"),        flags{*this, 0});

        bad<false>(body_what::skip,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 1\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n",                                         parse_error::illegal_content_length);
    }

    void testUpgradeHeader()
    {
        auto const m =
            [](std::string const& s)
            {
                return "GET / HTTP/1.1\r\n" + s + "\r\n";
            };
        good<true>(m("Upgrade:\r\n"),                       flags{*this, parse_flag::upgrade});
        good<true>(m("Upgrade: \r\n"),                      flags{*this, parse_flag::upgrade});
        good<true>(m("Upgrade: yes\r\n"),                   flags{*this, parse_flag::upgrade});

        good<true>(m("Up: yes\r\n"),                        flags{*this, 0});
        good<true>(m("UpgradX: none\r\n"),                  flags{*this, 0});
        good<true>(m("Upgrades: 2\r\n"),                    flags{*this, 0});
        good<true>(m("Upsample: 4x\r\n"),                   flags{*this, 0});

        good<true>(
            "GET / HTTP/1.1\r\n"
            "Connection: upgrade\r\n"
            "Upgrade: WebSocket\r\n"
            "\r\n",
            [&](fail_parser<true> const& p)
            {
                BEAST_EXPECT(p.upgrade());
            });
    }

    //--------------------------------------------------------------------------

    class body_f
    {
        suite& s_;
        std::string const& body_;

    public:
        body_f(body_f&&) = default;

        body_f(suite& s, std::string const& v)
            : s_(s)
            , body_(v)
        {
        }

        template<class Parser>
        void
        operator()(Parser const& p) const
        {
            s_.BEAST_EXPECT(p.body == body_);
        }
    };

    template<std::size_t N>
    static
    boost::asio::const_buffers_1
    buf(char const (&s)[N])
    {
        return { s, N-1 };
    }

    void testBody()
    {
        using boost::asio::buffer;
        auto const body =
            [&](std::string const& s)
            {
                return body_f{*this, s};
            };
        good<true>(
            "GET / HTTP/1.1\r\n"
            "Content-Length: 1\r\n"
            "\r\n"
            "1",
            body("1"));

        good<false>(
            "HTTP/1.0 200 OK\r\n"
            "\r\n"
            "hello",
            body("hello"));

        // on_body returns 2, meaning upgrade
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p{fc};
            p.on_body_rv(body_what::upgrade);
            p.write(buf(
                "GET / HTTP/1.1\r\n"
                "Content-Length: 1\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
        }

        // write the body in 3 pieces
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buffer_cat(
                buf("GET / HTTP/1.1\r\n"
                    "Content-Length: 10\r\n"
                    "\r\n"),
                buf("12"),
                buf("345"),
                buf("67890")), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            BEAST_EXPECT(! p.needs_eof());
            p.write_eof(ec);
            BEAST_EXPECT(! ec);
            p.write_eof(ec);
            BEAST_EXPECT(! ec);
            p.write(buf("GET / HTTP/1.1\r\n\r\n"), ec);
            BEAST_EXPECT(ec == parse_error::connection_closed);
        }

        // request without Content-Length or
        // Transfer-Encoding: chunked has no body.
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buf(
                "GET / HTTP/1.0\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.needs_eof());
            BEAST_EXPECT(p.complete());
        }
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buf(
                "GET / HTTP/1.1\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.needs_eof());
            BEAST_EXPECT(p.complete());
        }

        // response without Content-Length or
        // Transfer-Encoding: chunked requires eof.
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<false> p(fc);
            p.write(buf(
                "HTTP/1.0 200 OK\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.complete());
            BEAST_EXPECT(p.needs_eof());
            p.write(buf(
                "hello"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.complete());
            BEAST_EXPECT(p.needs_eof());
            p.write_eof(ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            p.write(buf("GET / HTTP/1.1\r\n\r\n"), ec);
            BEAST_EXPECT(ec == parse_error::connection_closed);
        }

        // 304 "Not Modified" response does not require eof
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<false> p(fc);
            p.write(buf(
                "HTTP/1.0 304 Not Modified\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.needs_eof());
            BEAST_EXPECT(p.complete());
        }

        // Chunked response does not require eof
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<false> p(fc);
            p.write(buf(
                "HTTP/1.1 200 OK\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.needs_eof());
            BEAST_EXPECT(! p.complete());
            p.write(buf(
                "0\r\n\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(! p.needs_eof());
            BEAST_EXPECT(p.complete());
        }

        // restart: 1.0 assumes Connection: close
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buf(
                "GET / HTTP/1.0\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            p.write(buf(
                "GET / HTTP/1.0\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(ec == parse_error::connection_closed);
        }

        // restart: 1.1 assumes Connection: keep-alive
        {
            error_code ec;
            test::fail_counter fc(1000);
            fail_parser<true> p(fc);
            p.write(buf(
                "GET / HTTP/1.1\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
            p.write(buf(
                "GET / HTTP/1.0\r\n"
                "\r\n"
                ), ec);
            BEAST_EXPECT(! ec);
            BEAST_EXPECT(p.complete());
        }

        bad<true>(body_what::normal,
            "GET / HTTP/1.1\r\n"
            "Content-Length: 1\r\n"
            "\r\n",
            parse_error::short_read);
    }

    void testChunkedBody()
    {
        auto const body =
            [&](std::string const& s)
            {
                return body_f{*this, s};
            };
        auto const ce =
            [](std::string const& s)
            {
                return
                    "GET / HTTP/1.1\r\n"
                    "Transfer-Encoding: chunked\r\n"
                    "\r\n" + s;
            };

    /*
        chunked-body    = *chunk
                          last-chunk
                          trailer-part
                          CRLF
        chunk           = chunk-size [ chunk-ext ] CRLF
                          chunk-data CRLF
        chunk-size      = 1*HEXDIG
        last-chunk      = 1*("0") [ chunk-ext ] CRLF
        chunk-data      = 1*OCTET ; a sequence of chunk-size octets
        chunk-ext       = *( ";" chunk-ext-name [ "=" chunk-ext-val ] )
        chunk-ext-name  = token
        chunk-ext-val   = token / quoted-string
        trailer-part    = *( header-field CRLF )
    */
        good<true>(ce(
            "1;xy\r\n*\r\n" "0\r\n\r\n"
            ), body("*"));

        good<true>(ce(
            "1;x\r\n*\r\n" "0\r\n\r\n"
            ), body("*"));

        good<true>(ce(
            "1;x;y\r\n*\r\n" "0\r\n\r\n"
            ), body("*"));

        good<true>(ce(
            "1;i;j=2;k=\"3\"\r\n*\r\n" "0\r\n\r\n"
            ), body("*"));

        good<true>(ce(
            "1\r\n" "a\r\n" "0\r\n" "\r\n"
            ), body("a"));

        good<true>(ce(
            "2\r\n" "ab\r\n" "0\r\n" "\r\n"
            ), body("ab"));

        good<true>(ce(
            "2\r\n" "ab\r\n" "1\r\n" "c\r\n" "0\r\n" "\r\n"
            ), body("abc"));

        good<true>(ce(
            "10\r\n" "1234567890123456\r\n" "0\r\n" "\r\n"
            ), body("1234567890123456"));

        bad<true>(ce("ffffffffffffffff0\r\n0\r\n\r\n"), parse_error::bad_content_length);
        bad<true>(ce("g\r\n0\r\n\r\n"),                 parse_error::invalid_chunk_size);
        bad<true>(ce("0g\r\n0\r\n\r\n"),                parse_error::invalid_chunk_size);
        bad<true>(ce("0\r_\n"),                         parse_error::bad_crlf);
        bad<true>(ce("1\r\n*_\r\n"),                    parse_error::bad_crlf);
        bad<true>(ce("1\r\n*\r_\n"),                    parse_error::bad_crlf);
        bad<true>(ce("1;,x\r\n*\r\n" "0\r\n\r\n"),      parse_error::invalid_ext_name);
        bad<true>(ce("1;x,\r\n*\r\n" "0\r\n\r\n"),      parse_error::invalid_ext_name);
    }

    void testLimits()
    {
        std::size_t n;
        static std::size_t constexpr Limit = 100;
        {
            for(n = 1; n < Limit; ++n)
            {
                test::fail_counter fc(1000);
                fail_parser<true> p(fc);
                p.set_option(header_max_size{n});
                error_code ec;
                p.write(buf(
                    "GET / HTTP/1.1\r\n"
                    "User-Agent: beast\r\n"
                    "\r\n"
                    ), ec);
                if(! ec)
                    break;
                BEAST_EXPECT(ec == parse_error::header_too_big);
            }
            BEAST_EXPECT(n < Limit);
        }
        {
            for(n = 1; n < Limit; ++n)
            {
                test::fail_counter fc(1000);
                fail_parser<false> p(fc);
                p.set_option(header_max_size{n});
                error_code ec;
                p.write(buf(
                    "HTTP/1.1 200 OK\r\n"
                    "Server: beast\r\n"
                    "Content-Length: 4\r\n"
                    "\r\n"
                    "****"
                    ), ec);
                if(! ec)
                    break;
                BEAST_EXPECT(ec == parse_error::header_too_big);
            }
            BEAST_EXPECT(n < Limit);
        }
        {
            test::fail_counter fc(1000);
            fail_parser<false> p(fc);
            p.set_option(body_max_size{2});
            error_code ec;
            p.write(buf(
                    "HTTP/1.1 200 OK\r\n"
                    "Server: beast\r\n"
                    "Content-Length: 4\r\n"
                    "\r\n"
                    "****"
                ), ec);
            BEAST_EXPECT(ec == parse_error::body_too_big);
        }
    }

    void run() override
    {
        testCallbacks();
        testRequestLine();
        testStatusLine();
        testHeaders();
        testConnectionHeader();
        testContentLengthHeader();
        testTransferEncodingHeader();
        testUpgradeHeader();
        testBody();
        testChunkedBody();
        testLimits();
    }
};

BEAST_DEFINE_TESTSUITE(basic_parser_v1,http,beast);

} // http
} // beast
