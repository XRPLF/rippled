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

// MODULES: ../impl/chrono_io.cpp

#include <beast/chrono/abstract_clock.h>
#include <beast/chrono/manual_clock.h>
#include <beast/unit_test/suite.h>
#include <sstream>
#include <string>
#include <thread>

namespace beast {

class abstract_clock_test : public unit_test::suite
{
public:
    template <class Clock>
    void test (abstract_clock<Clock>& c)
    {
        {
            auto const t1 (c.now ());
            std::this_thread::sleep_for (
                std::chrono::milliseconds (1500));
            auto const t2 (c.now ());

            std::stringstream ss;
            ss <<
                "t1= " << t1.time_since_epoch() <<
                ", t2= " << t2.time_since_epoch() <<
                ", elapsed= " << (t2 - t1);
            log << ss.str();
        }
    }

    void test_manual ()
    {
        using clock_type = manual_clock<std::chrono::steady_clock>;
        clock_type c;

        std::stringstream ss;

        ss << "now() = " << c.now().time_since_epoch() << std::endl;

        c.set (clock_type::time_point (std::chrono::seconds(1)));
        ss << "now() = " << c.now().time_since_epoch() << std::endl;

        c.set (clock_type::time_point (std::chrono::seconds(2)));
        ss << "now() = " << c.now().time_since_epoch() << std::endl;

        log << ss.str();
    }

    void run ()
    {
        log << "steady_clock";
        test (get_abstract_clock<
            std::chrono::steady_clock>());

        log << "system_clock";
        test (get_abstract_clock<
            std::chrono::system_clock>());

        log << "high_resolution_clock";
        test (get_abstract_clock<
            std::chrono::high_resolution_clock>());

        log << "manual_clock";
        test_manual ();

        pass ();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(abstract_clock,chrono,beast);

}
