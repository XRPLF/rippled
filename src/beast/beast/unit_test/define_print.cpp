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

#include <beast/unit_test/amount.h>
#include <beast/unit_test/global_suites.h>
#include <beast/unit_test/suite.h>

// Include this .cpp in your project to gain access to the printing suite

namespace beast {
namespace unit_test {

namespace detail {

/** A suite that prints the list of globally defined suites. */
class print_test : public suite
{
public:
    static
    std::string
    prefix (suite_info const& s)
    {
        if (s.manual())
            return "|M| ";
        return "    ";
    }

    void
    print (suite_list &c)
    {
        std::size_t manual (0);
        for (auto const& s : c)
        {
            log <<
                prefix (s) <<
                s.full_name();
            if (s.manual())
                ++manual;
        }
        log <<
            amount (c.size(), "suite") << " total, " <<
            amount (manual, "manual suite")
            ;
    }

    void
    run()
    {
        log << "------------------------------------------";
        print (global_suites());
        log << "------------------------------------------";
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(print,unit_test,beast);

}

}
}
