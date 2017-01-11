//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/error.hpp>

#include <beast/unit_test/suite.hpp>
#include <memory>

namespace beast {
namespace websocket {

class error_test : public unit_test::suite
{
public:
    void check(char const* name, error ev)
    {
        auto const ec = make_error_code(ev);
        BEAST_EXPECT(std::string{ec.category().name()} == name);
        BEAST_EXPECT(! ec.message().empty());
        BEAST_EXPECT(std::addressof(ec.category()) ==
            std::addressof(detail::get_error_category()));
        BEAST_EXPECT(detail::get_error_category().equivalent(
            static_cast<std::underlying_type<error>::type>(ev),
                ec.category().default_error_condition(
                    static_cast<std::underlying_type<error>::type>(ev))));
        BEAST_EXPECT(detail::get_error_category().equivalent(
            ec, static_cast<std::underlying_type<error>::type>(ev)));
    }

    void run() override
    {
        check("websocket", error::closed);
        check("websocket", error::failed);
        check("websocket", error::handshake_failed);
        check("websocket", error::keep_alive);
        check("websocket", error::response_malformed);
        check("websocket", error::response_failed);
        check("websocket", error::response_denied);
        check("websocket", error::request_malformed);
        check("websocket", error::request_invalid);
        check("websocket", error::request_denied);
        check("websocket", error::general);
    }
};

BEAST_DEFINE_TESTSUITE(error,websocket,beast);

} // websocket
} // beast
