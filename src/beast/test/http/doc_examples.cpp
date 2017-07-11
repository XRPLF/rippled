//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "example/doc/http_examples.hpp"

#include <beast/core/read_size.hpp>
#include <beast/core/detail/clamp.hpp>
#include <beast/test/pipe_stream.hpp>
#include <beast/test/string_istream.hpp>
#include <beast/test/string_ostream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <sstream>
#include <array>
#include <vector>
#include <list>

namespace beast {
namespace http {

class doc_examples_test
    : public beast::unit_test::suite
    , public beast::test::enable_yield_to
{
public:
    // two threads, for some examples using a pipe
    doc_examples_test()
        : enable_yield_to(2)
    {
    }

    template<bool isRequest>
    bool
    equal_body(string_view sv, string_view body)
    {
        test::string_istream si{
            get_io_service(), sv.to_string()};
        message<isRequest, string_body, fields> m;
        multi_buffer b;
        try
        {
            read(si, b, m);
            return m.body == body;
        }
        catch(std::exception const& e)
        {
            log << "equal_body: " << e.what() << std::endl;
            return false;
        }
    }

    void
    doExpect100Continue()
    {
        test::pipe p{ios_};
        yield_to(
            [&](yield_context)
            {
                error_code ec;
                flat_buffer buffer;
                receive_expect_100_continue(
                    p.server, buffer, ec);
                BEAST_EXPECTS(! ec, ec.message());
            },
            [&](yield_context)
            {
                flat_buffer buffer;
                request<string_body> req;
                req.version = 11;
                req.method_string("POST");
                req.target("/");
                req.insert(field::user_agent, "test");
                req.body = "Hello, world!";
                req.prepare_payload();

                error_code ec;
                send_expect_100_continue(
                    p.client, buffer, req, ec);
                BEAST_EXPECTS(! ec, ec.message());
            });
    }

    void
    doCgiResponse()
    {
        std::string const s = "Hello, world!";
        test::pipe child{ios_};
        child.server.read_size(3);
        ostream(child.server.buffer) << s;
        child.client.close();
        test::pipe p{ios_};
        error_code ec;
        send_cgi_response(child.server, p.client, ec);
        BEAST_EXPECTS(! ec, ec.message());
        BEAST_EXPECT(equal_body<false>(p.server.str(), s));
    }

    void
    doRelay()
    {
        request<string_body> req;
        req.version = 11;
        req.method_string("POST");
        req.target("/");
        req.insert(field::user_agent, "test");
        req.body = "Hello, world!";
        req.prepare_payload();

        test::pipe downstream{ios_};
        downstream.server.read_size(3);
        test::pipe upstream{ios_};
        upstream.client.write_size(3);

        error_code ec;
        write(downstream.client, req);
        BEAST_EXPECTS(! ec, ec.message());
        downstream.client.close();

        flat_buffer buffer;
        relay<true>(upstream.client, downstream.server, buffer, ec,
            [&](header<true, fields>& h, error_code& ev)
            {
                ev = {};
                h.erase("Content-Length");
                h.set("Transfer-Encoding", "chunked");
            });
        BEAST_EXPECTS(! ec, ec.message());
        BEAST_EXPECT(equal_body<true>(
            upstream.server.str(), req.body));
    }

    void
    doReadStdStream()
    {
        std::string const s =
            "HTTP/1.0 200 OK\r\n"
            "User-Agent: test\r\n"
            "\r\n"
            "Hello, world!";
        std::istringstream is(s);
        error_code ec;
        flat_buffer buffer;
        response<string_body> res;
        read_istream(is, buffer, res, ec);
        BEAST_EXPECTS(! ec, ec.message());
        BEAST_EXPECT(boost::lexical_cast<
            std::string>(res) == s);
    }

    void
    doWriteStdStream()
    {
        std::ostringstream os;
        request<string_body> req;
        req.version = 11;
        req.method(verb::get);
        req.target("/");
        req.insert(field::user_agent, "test");
        error_code ec;
        write_ostream(os, req, ec);
        BEAST_EXPECTS(! ec, ec.message());
        BEAST_EXPECT(boost::lexical_cast<
            std::string>(req) == os.str());
    }

    void
    doCustomParser()
    {
        {
            string_view s{
                "POST / HTTP/1.1\r\n"
                "User-Agent: test\r\n"
                "Content-Length: 13\r\n"
                "\r\n"
                "Hello, world!"
            };
            error_code ec;
            custom_parser<true> p;
            p.put(boost::asio::buffer(
                s.data(), s.size()), ec);
            BEAST_EXPECTS(! ec, ec.message());
        }
        {
            string_view s{
                "HTTP/1.1 200 OK\r\n"
                "Server: test\r\n"
                "Transfer-Encoding: chunked\r\n"
                "\r\n"
                "d\r\n"
                "Hello, world!"
                "\r\n"
                "0\r\n\r\n"
            };
            error_code ec;
            custom_parser<false> p;
            p.put(boost::asio::buffer(
                s.data(), s.size()), ec);
            BEAST_EXPECTS(! ec, ec.message());
        }
    }

    void
    doHEAD()
    {
        test::pipe p{ios_};
        yield_to(
            [&](yield_context)
            {
                error_code ec;
                flat_buffer buffer;
                do_server_head(p.server, buffer, ec);
                BEAST_EXPECTS(! ec, ec.message());
            },
            [&](yield_context)
            {
                error_code ec;
                flat_buffer buffer;
                auto res = do_head_request(p.client, buffer, "/", ec);
                BEAST_EXPECTS(! ec, ec.message());
            });
    }

    struct handler
    {
        std::string body;

        template<class Body>
        void
        operator()(request<Body>&&)
        {
        }

        void
        operator()(request<string_body>&& req)
        {
            body = req.body;
        }
    };

    void
    doDeferredBody()
    {
        test::pipe p{ios_};
        ostream(p.server.buffer) <<
            "POST / HTTP/1.1\r\n"
            "User-Agent: test\r\n"
            "Content-Type: multipart/form-data\r\n"
            "Content-Length: 13\r\n"
            "\r\n"
            "Hello, world!";

        handler h;
        flat_buffer buffer;
        do_form_request(p.server, buffer, h);
        BEAST_EXPECT(h.body == "Hello, world!");
    }

    //--------------------------------------------------------------------------

    void
    doIncrementalRead()
    {
        test::pipe c{ios_};
        std::string s(2048, '*');
        ostream(c.server.buffer) <<
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2048\r\n"
            "Server: test\r\n"
            "\r\n" <<
            s;
        error_code ec;
        flat_buffer b;
        std::stringstream ss;
        read_and_print_body<false>(ss, c.server, b, ec);
        if(BEAST_EXPECTS(! ec, ec.message()))
            BEAST_EXPECT(ss.str() == s);
    }

    //--------------------------------------------------------------------------

    void
    run()
    {
        doExpect100Continue();
        doCgiResponse();
        doRelay();
        doReadStdStream();
        doWriteStdStream();
        doCustomParser();
        doHEAD();
        doDeferredBody();
        doIncrementalRead();
    }
};

BEAST_DEFINE_TESTSUITE(doc_examples,http,beast);

} // http
} // beast
