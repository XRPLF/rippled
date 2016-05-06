//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_DEBUG_OSTREAM_HPP
#define BEAST_UNIT_TEST_DEBUG_OSTREAM_HPP

#include <beast/unit_test/abstract_ostream.hpp>
#include <iostream>

#ifdef _MSC_VER
# ifndef WIN32_LEAN_AND_MEAN // VC_EXTRALEAN
#  define WIN32_LEAN_AND_MEAN
#include <windows.h>
#  undef WIN32_LEAN_AND_MEAN
# else
#include <windows.h>
# endif
# ifdef min
#  undef min
# endif
# ifdef max
#  undef max
# endif
#endif

namespace beast {

#ifdef _MSC_VER
/** A basic_abstract_ostream that redirects output to an attached debugger. */
class debug_ostream
    : public abstract_ostream
{
private:
    bool m_debugger;

public:
    debug_ostream()
        : m_debugger (IsDebuggerPresent() != FALSE)
    {
        // Note that the check for an attached debugger is made only
        // during construction time, for efficiency. A stream created before
        // the debugger is attached will not have output redirected.
    }

    void
    write (string_type const& s) override
    {
        if (m_debugger)
        {
            OutputDebugStringA ((s + "\n").c_str());
            return;
        }

        std::cout << s << std::endl;
    }
};

#else
class debug_ostream
    : public abstract_ostream
{
public:
    void
    write (string_type const& s) override
    {
        std::cout << s << std::endl;
    }
};

#endif

} // beast

#endif
