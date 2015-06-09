//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#include <beast/unit_test/suite.h>

namespace beast {

// compute (value)*(mul)/(div) - avoid overflow but keep precision
std::uint64_t mulDiv (std::uint64_t value, std::uint32_t mul, std::uint64_t div)
{
    // VFALCO TODO replace with beast::literal64bitUnsigned ()
    //
    const std::uint64_t boundary = 0x00000000FFFFFFFFull;

    if (value > boundary)                           // Large value, avoid overflow
        return (value / div) * mul;
    else                                            // Normal value, preserve accuracy
        return (value * mul) / div;
}

//==============================================================================

class Muldiv_test  : public unit_test::suite
{
public:
    void run()
    {
        expect(mulDiv(1, 1, 1) == 1);
        expect(mulDiv(2, 3, 2) == 3);
        expect(mulDiv(10006, 103, 5003) == 206);
        expect(mulDiv(10006, 103, 4002) == 257);
        // 0x0000FFFF000008800 * 0x10000000 = 0x0000088000000000,
        // which is an undesired overflow
        expect(mulDiv(0x0000FFFF00000880ull, 0x10000000ul, 0x1000ull) ==
            0xFFFF000000000000ull);
    }
};

BEAST_DEFINE_TESTSUITE(Muldiv,beast_core,beast);

}