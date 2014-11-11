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

#if BEAST_INCLUDE_BEASTCONFIG
#include "../../BeastConfig.h"
#endif

#include <beast/http/rfc2616.h>
#include <beast/unit_test/suite.h>
#include <string>
#include <vector>

namespace beast {
namespace rfc2616 {

class rfc2616_test : public beast::unit_test::suite
{
public:
    void
    check (std::string const& s,
        std::vector <std::string> const& expected)
    {
        auto const parsed = split_commas(s.begin(), s.end());
        expect (parsed == expected);
    }

    void test_split_commas()
    {
        testcase("split_commas");
        check ("",              {});
        check (" ",             {});
        check ("  ",            {});
        check ("\t",            {});
        check (" \t ",          {});
        check (",",             {});
        check (",,",            {});
        check (" ,",            {});
        check (" , ,",          {});
        check ("x",             {"x"});
        check (" x",            {"x"});
        check (" \t x",         {"x"});
        check ("x ",            {"x"});
        check ("x \t",          {"x"});
        check (" \t x \t ",     {"x"});
        check ("\"\"",          {});
        check (" \"\"",         {});
        check ("\"\" ",         {});
        check ("\"x\"",         {"x"});
        check ("\" \"",         {" "});
        check ("\" x\"",        {" x"});
        check ("\"x \"",        {"x "});
        check ("\" x \"",       {" x "});
        check ("\"\tx \"",      {"\tx "});
        check ("x,y",           { "x", "y" });
        check ("x ,\ty ",       { "x", "y" });
        check ("x, y, z",       {"x","y","z"});
        check ("x, \"y\", z",   {"x","y","z"});
    }

    void
    run()
    {
        test_split_commas();
    }
};

BEAST_DEFINE_TESTSUITE(rfc2616,http,beast);

}
}
