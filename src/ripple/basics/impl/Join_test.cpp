//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/basics/Join.h>
#include <beast/unit_test/suite.h>

namespace ripple {

struct Join_test : beast::unit_test::suite
{
    void doTest (std::string const& name,
                 std::vector<std::string> const& parts,
                 std::string const& expected)
    {
        testcase (name);
        expect (join (parts) == expected);
    }

    void doTest (std::string const& name,
                 std::vector<std::string> const& parts,
                 std::string const& sep,
                 std::string const& expected)
    {
        testcase (name);
        expect (join (parts, sep) == expected);
    }

    void run() override
    {
        doTest ("empty", {}, "");
        doTest ("one", {"hello"}, "hello");
        doTest ("two", {"hello", "world"}, "helloworld");
        doTest ("many", {"he", "", "ll", "o", ""}, "hello");

        doTest ("empty e", {}, "", "");
        doTest ("one e", {"hello"}, "", "hello");
        doTest ("two e", {"hello", "world"}, "", "helloworld");
        doTest ("many e", {"he", "", "ll", "o", ""}, "", "hello");

        doTest ("empty comma", {}, ",", "");
        doTest ("one command", {"hello"}, ",", "hello");
        doTest ("two comma", {"hello", "world"}, ",", "hello,world");
        doTest ("many comma", {"he", "", "ll", "o", ""}, ",", "he,,ll,o,");
    }
};

 BEAST_DEFINE_TESTSUITE(Join, ripple_basics, ripple);

} // ripple
