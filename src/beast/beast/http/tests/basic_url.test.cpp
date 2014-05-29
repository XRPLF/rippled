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

#include <beast/http/basic_url.h>

#include <beast/unit_test/suite.h>

namespace beast {

class basic_url_test : public unit_test::suite
{
public:
    void
    run ()
    {
        std::vector <char const*> const urls {
            "http://www.example.com/#%c2%a9",
            "http://127.0.0.1:443",
            "http://192.168.0.1 hello.urltest.lookout.net/",
            "http://\\uff10\\uff38\\uff43\\uff10\\uff0e\\uff10\\uff12\\uff15\\uff10\\uff0e\\uff10\\uff11.urltest.lookout.net/"
        };
        http::url url;
        for (auto const& s : urls)
        {
            try
            {
                url.parse (s);
                pass();
            }
            catch(...)
            {
                fail();
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(basic_url,http,beast);

}
