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

#ifndef BEAST_STREAMS_DEBUG_OSTREAM_H_INCLUDED
#define BEAST_STREAMS_DEBUG_OSTREAM_H_INCLUDED

#include <beast/streams/abstract_ostream.h>

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

}

#endif
