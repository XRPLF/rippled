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

#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <sstream>
#include <string>
#include <thread>

namespace beast {

class abstract_clock_test : public unit_test::suite
{
public:
    template <class Clock>
    void
    test(std::string name, abstract_clock<Clock>& c)
    {
        testcase(name);

        auto const t1(c.now());
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        auto const t2(c.now());

        log << "t1= " << t1.time_since_epoch().count()
            << ", t2= " << t2.time_since_epoch().count()
            << ", elapsed= " << (t2 - t1).count() << std::endl;

        pass();
    }

    void
    test_manual()
    {
        testcase("manual");

        using clock_type = manual_clock<std::chrono::steady_clock>;
        clock_type c;

        std::stringstream ss;

        auto c1 = c.now().time_since_epoch();
        c.set(clock_type::time_point(std::chrono::seconds(1)));
        auto c2 = c.now().time_since_epoch();
        c.set(clock_type::time_point(std::chrono::seconds(2)));
        auto c3 = c.now().time_since_epoch();

        log << "[" << c1.count() << "," << c2.count() << "," << c3.count()
            << "]" << std::endl;

        pass();
    }

    void
    run() override
    {
        test("steady_clock", get_abstract_clock<std::chrono::steady_clock>());
        test("system_clock", get_abstract_clock<std::chrono::system_clock>());
        test(
            "high_resolution_clock",
            get_abstract_clock<std::chrono::high_resolution_clock>());

        test_manual();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(abstract_clock, chrono, beast);

}  // namespace beast
