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

#include <beast/http/rfc2616.h>
#include <beast/detail/unit_test/suite.hpp>
#include <string>
#include <vector>

namespace beast {
namespace rfc2616 {
namespace test {

class rfc2616_test : public beast::detail::unit_test::suite
{
public:
    void
    checkSplit(std::string const& s,
        std::vector <std::string> const& expected)
    {
        auto const parsed = split_commas(s.begin(), s.end());
        expect (parsed == expected);
    }

    void testSplit()
    {
        checkSplit("",              {});
        checkSplit(" ",             {});
        checkSplit("  ",            {});
        checkSplit("\t",            {});
        checkSplit(" \t ",          {});
        checkSplit(",",             {});
        checkSplit(",,",            {});
        checkSplit(" ,",            {});
        checkSplit(" , ,",          {});
        checkSplit("x",             {"x"});
        checkSplit(" x",            {"x"});
        checkSplit(" \t x",         {"x"});
        checkSplit("x ",            {"x"});
        checkSplit("x \t",          {"x"});
        checkSplit(" \t x \t ",     {"x"});
        checkSplit("\"\"",          {});
        checkSplit(" \"\"",         {});
        checkSplit("\"\" ",         {});
        checkSplit("\"x\"",         {"x"});
        checkSplit("\" \"",         {" "});
        checkSplit("\" x\"",        {" x"});
        checkSplit("\"x \"",        {"x "});
        checkSplit("\" x \"",       {" x "});
        checkSplit("\"\tx \"",      {"\tx "});
        checkSplit("x,y",           {"x", "y"});
        checkSplit("x ,\ty ",       {"x", "y"});
        checkSplit("x, y, z",       {"x","y","z"});
        checkSplit("x, \"y\", z",   {"x","y","z"});
        checkSplit(",,x,,\"y\",,",  {"x","y"});
    }

    void
    checkIter(std::string const& s,
        std::vector<std::string> const& expected)
    {
        std::vector<std::string> got;
        for(auto const& v : make_list(s))
            got.emplace_back(v);
        expect(got == expected);
    }

    void
    testIter()
    {
        checkIter("x",              {"x"});
        checkIter(" x",             {"x"});
        checkIter("x\t",            {"x"});
        checkIter("\tx ",           {"x"});
        checkIter(",x",             {"x"});
        checkIter("x,",             {"x"});
        checkIter(",x,",            {"x"});
        checkIter(" , x\t,\t",      {"x"});
        checkIter("x,y",            {"x", "y"});
        checkIter("x, ,y ",         {"x", "y"});
        checkIter("\"x\"",          {"x"});
    }

    void
    testList()
    {
        expect(token_in_list("x",       "x"));
        expect(token_in_list("x,y",     "x"));
        expect(token_in_list("x,y",     "y"));
        expect(token_in_list("x, y ",   "y"));
        expect(token_in_list("x",       "X"));
        expect(token_in_list("Y",       "y"));
        expect(token_in_list("close, keepalive", "close"));
        expect(token_in_list("close, keepalive", "keepalive"));
    }

    void
    run()
    {
        testSplit();
        testIter();
        testList();
    }
};

BEAST_DEFINE_TESTSUITE(rfc2616,http,beast);

} // test
} // rfc2616
} // beast
