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

#include "../abstract_clock.h"
#include "../manual_clock.h"

#include <thread>

#include <string>
#include <sstream>

namespace beast {

class abstract_clock_tests : public UnitTest
{
public:
    void test (abstract_clock <std::chrono::seconds>& c)
    {
        {
            auto const t1 (c.now ());
            std::this_thread::sleep_for (
                std::chrono::milliseconds (1500));
            auto const t2 (c.now ());

            std::stringstream ss;
            ss <<
                "t1= " << c.to_string (t1) <<
                ", t2= " << c.to_string (t2) <<
                ", elapsed= " << (t2 - t1);
            logMessage (ss.str());
        }
    }

    void test_manual ()
    {
        typedef manual_clock <std::chrono::seconds> clock_type;
        clock_type c;

        std::stringstream ss;

        ss << "now() = " << c.to_string (c.now ()) << std::endl;

        c.set (clock_type::time_point (std::chrono::seconds (1)));
        ss << "now() = " << c.to_string (c.now ()) << std::endl;

        c.set (clock_type::time_point (std::chrono::seconds (2)));
        ss << "now() = " << c.to_string (c.now ()) << std::endl;

        logMessage (ss.str());
    }

    void runTest ()
    {
        beginTestCase ("Syntax");

        logMessage ("steady_clock");
        test (get_abstract_clock <std::chrono::steady_clock,
            std::chrono::seconds> ());

        logMessage ("system_clock");
        test (get_abstract_clock <std::chrono::system_clock,
            std::chrono::seconds> ());

        logMessage ("high_resolution_clock");
        test (get_abstract_clock <std::chrono::high_resolution_clock,
            std::chrono::seconds> ());

        logMessage ("manual_clock");
        test_manual ();

        pass ();
    }

    abstract_clock_tests ()
        : UnitTest ("abstract_clock", "beast", runManual)
    {
    }
};

static abstract_clock_tests abstract_clock_tests_;

}
