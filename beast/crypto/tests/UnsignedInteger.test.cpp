//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions are Copyright (c) 2013 the authors listed at the following URL,
    and/or the authors of referenced articles or incorporated external code:
    http://en.literateprograms.org/Arbitrary-precision_integer_arithmetic_(C)
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

#include <beast/crypto/UnsignedInteger.h>

#include <beast/unit_test/suite.h>

namespace beast {

class UnsignedInteger_test : public unit_test::suite
{
public:
    template <unsigned int Bytes>
    void test ()
    {
        typedef UnsignedInteger <Bytes> UInt;

        std::stringstream ss;
        ss <<
            "bytes=" << Bytes;
        testcase (ss.str());

        UInt zero;
        zero.fill (0);
        expect (zero.isZero (), "should be zero");
        expect (! zero.isNotZero (), "sould not be non-zero");

        UInt one (UInt::createFromInteger (1U));
        expect (one == UInt::createFromInteger (1U), "should be equal");

        expect (! one.isZero (), "should not be zero");
        expect (one.isNotZero (), "sould be non-zero");

        expect (zero < one, "should be less");
        expect (one > zero, "should be greater");
        expect (zero >= zero, "should be less than or equal");
        expect (one <= one, "should be less than or equal");

        expect (zero == zero, "should be equal");
        expect (zero != one, "should not be equal");

        expect (zero == UInt::createFromInteger (0U), "should be zero");
        expect (one  == UInt::createFromInteger (1U), "should be one");
        expect (one  != UInt::createFromInteger (2U), "should not be two");

        UInt largest = UInt::createFilled (0xff);

        expect (largest > zero && largest > one, "should be greater");
    }

    void run()
    {
        test <16> ();
        test <33> ();
    }

private:
};

BEAST_DEFINE_TESTSUITE(UnsignedInteger,crypto,beast);

}
