//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/detail/base64.hpp>

#include <beast/unit_test/suite.hpp>

namespace beast {
namespace detail {

class base64_test : public beast::unit_test::suite
{
public:
    void
    check (std::string const& in, std::string const& out)
    {
        auto const encoded = base64_encode (in);
        BEAST_EXPECT(encoded == out);
        BEAST_EXPECT(base64_decode (encoded) == in);
    }

    void
    run()
    {
        check ("",       "");
        check ("f",      "Zg==");
        check ("fo",     "Zm8=");
        check ("foo",    "Zm9v");
        check ("foob",   "Zm9vYg==");
        check ("fooba",  "Zm9vYmE=");
        check ("foobar", "Zm9vYmFy");
    }
};

BEAST_DEFINE_TESTSUITE(base64,core,beast);

} // detail
} // beast

