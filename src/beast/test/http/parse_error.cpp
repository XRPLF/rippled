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
        BEAST_EXPECT(std::string{ec.category().name()} == name);
        BEAST_EXPECT(! ec.message().empty());
        BEAST_EXPECT(std::addressof(ec.category()) ==
            std::addressof(detail::get_parse_error_category()));
        BEAST_EXPECT(detail::get_parse_error_category().equivalent(
            static_cast<std::underlying_type<parse_error>::type>(ev),
                ec.category().default_error_condition(
                    static_cast<std::underlying_type<parse_error>::type>(ev))));
        BEAST_EXPECT(detail::get_parse_error_category().equivalent(
            ec, static_cast<std::underlying_type<parse_error>::type>(ev)));
    }

    void run() override
    {
        check("http", parse_error::connection_closed);
        check("http", parse_error::bad_method);
        check("http", parse_error::bad_uri);
        check("http", parse_error::bad_version);
        check("http", parse_error::bad_crlf);
        check("http", parse_error::bad_status);
        check("http", parse_error::bad_reason);
        check("http", parse_error::bad_field);
        check("http", parse_error::bad_value);
        check("http", parse_error::bad_content_length);
        check("http", parse_error::illegal_content_length);
        check("http", parse_error::invalid_chunk_size);
        check("http", parse_error::invalid_ext_name);
        check("http", parse_error::invalid_ext_val);
        check("http", parse_error::header_too_big);
        check("http", parse_error::body_too_big);
        check("http", parse_error::short_read);
    }
};

BEAST_DEFINE_TESTSUITE(parse_error,http,beast);

} // http
} // beast
