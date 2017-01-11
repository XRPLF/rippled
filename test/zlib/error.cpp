//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/zlib/error.hpp>

#include <beast/unit_test/suite.hpp>
#include <memory>

namespace beast {
namespace zlib {

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
        check("zlib", error::need_buffers);
        check("zlib", error::end_of_stream);
        check("zlib", error::stream_error);

        check("zlib", error::invalid_block_type);
        check("zlib", error::invalid_stored_length);
        check("zlib", error::too_many_symbols);
        check("zlib", error::invalid_code_lenths);
        check("zlib", error::invalid_bit_length_repeat);
        check("zlib", error::missing_eob);
        check("zlib", error::invalid_literal_length);
        check("zlib", error::invalid_distance_code);
        check("zlib", error::invalid_distance);

        check("zlib", error::over_subscribed_length);
        check("zlib", error::incomplete_length_set);

        check("zlib", error::general);
    }
};

BEAST_DEFINE_TESTSUITE(error,zlib,beast);

} // zlib
} // beast
