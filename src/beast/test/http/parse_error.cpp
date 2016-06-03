//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/parse_error.hpp>

#include <beast/unit_test/suite.hpp>
#include <memory>

namespace beast {
namespace http {

class parse_error_test : public unit_test::suite
{
public:
    void check(char const* name, parse_error ev)
    {
        auto const ec = make_error_code(ev);
        expect(std::string{ec.category().name()} == name);
        expect(! ec.message().empty());
        expect(std::addressof(ec.category()) ==
            std::addressof(get_parse_error_category()));
        expect(get_parse_error_category().equivalent(static_cast<int>(ev),
            ec.category().default_error_condition(static_cast<int>(ev))));
        expect(get_parse_error_category().equivalent(
            ec, static_cast<int>(ev)));
    }

    void run() override
    {
        check("http", parse_error::connection_closed);
        check("http", parse_error::bad_method);
        check("http", parse_error::bad_uri);
        check("http", parse_error::bad_version);
        check("http", parse_error::bad_crlf);
        check("http", parse_error::bad_request);
        check("http", parse_error::bad_status);
        check("http", parse_error::bad_reason);
        check("http", parse_error::bad_field);
        check("http", parse_error::bad_value);
        check("http", parse_error::bad_content_length);
        check("http", parse_error::illegal_content_length);
        check("http", parse_error::bad_on_headers_rv);
        check("http", parse_error::invalid_chunk_size);
        check("http", parse_error::short_read);
        check("http", parse_error::general);
    }
};

BEAST_DEFINE_TESTSUITE(parse_error,http,beast);

} // http
} // beast
