// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <xrpl/beast/unit_test/amount.h>
#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/unit_test/suite_info.h>

#include <cstddef>
#include <ostream>
#include <string>

// Include this .cpp in your project to gain access to the printing suite

namespace beast::unit_test {

/** A suite that prints the list of globally defined suites. */
class print_test : public Suite
{
public:
    void
    run() override
    {
        std::size_t manual = 0;
        std::size_t total = 0;

        auto prefix = [](SuiteInfo const& s) { return s.manual() ? "|M| " : "    "; };

        for (auto const& s : globalSuites())
        {
            log << prefix(s) << s.fullName() << '\n';

            if (s.manual())
                ++manual;
            ++total;
        }

        log << Amount(total, "suite") << " total, " << Amount(manual, "manual suite") << std::endl;

        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(print, beast, beast);

}  // namespace beast::unit_test
