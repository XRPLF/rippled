//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <beast/detail/base64.hpp>
#include <beast/detail/unit_test/suite.hpp>

namespace beast {
namespace test {

class base64_test : public detail::unit_test::suite
{
public:
    void
    check (std::string const& in, std::string const& out)
    {
        auto const encoded = detail::base64_encode (in);
        expect (encoded == out);
        expect (detail::base64_decode (encoded) == in);
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

} // test
} // beast

