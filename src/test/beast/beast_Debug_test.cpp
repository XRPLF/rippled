//------------------------------------------------------------------------------
/*
This file is part of rippled: https://github.com/ripple/rippled
Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/Debug.h>
namespace beast {

// A simple unit test to determine the diagnostic settings in a build.
//
class Debug_test : public unit_test::suite
{
public:
    static int envDebug()
    {
#ifdef _DEBUG
        return 1;
#else
        return 0;
#endif
    }

    static int beastDebug()
    {
#ifdef BEAST_DEBUG
        return BEAST_DEBUG;
#else
        return 0;
#endif
    }

    static int beastForceDebug()
    {
#ifdef BEAST_FORCE_DEBUG
        return BEAST_FORCE_DEBUG;
#else
        return 0;
#endif
    }

    void run() override
    {
        log <<
            "_DEBUG              = " << envDebug() << '\n' <<
            "BEAST_DEBUG         = " << beastDebug() << '\n' <<
            "BEAST_FORCE_DEBUG   = " << beastForceDebug() << '\n' <<
            "sizeof(std::size_t) = " << sizeof(std::size_t) << std::endl;
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Debug, utility, beast);

}