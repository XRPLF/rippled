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

class UnsignedIntegerTests : public UnitTest
{
public:
    UnsignedIntegerTests () : UnitTest ("UnsignedInteger", "beast")
    {
    }

    template <unsigned int Bytes>
    void runTest ()
    {
        String s;

        s << "Bytes=" << String(Bytes);

        beginTest (s);

        UnsignedInteger <Bytes> zero;
        zero.fill (0);
        expect (zero.isZero (), "should be zero");
        expect (! zero.isNotZero (), "sould not be non-zero");

        UnsignedInteger <Bytes> one;
        one.clear ();
        one [Bytes - 1] = 1;
        expect (one == UnsignedInteger <Bytes>::createFromInteger (1U), "should be equal");

        expect (! one.isZero (), "should not be zero");
        expect (one.isNotZero (), "sould be non-zero");

        expect (zero < one, "should be less");
        expect (one > zero, "should be greater");
        expect (zero >= zero, "should be less than or equal");
        expect (one <= one, "should be less than or equal");

        expect (zero == zero, "should be equal");
        expect (zero != one, "should not be equal");

        expect ((zero | zero) == zero, "should be zero");
        expect ((zero | one) != zero, "should not be zero");
        expect ((one | one) != zero, "should not be zero");

        expect ((one & zero) == zero, "should be zero");
        expect ((one & one) == one, "should be one");
        expect ((zero & zero) == zero, "should be zero");

        expect (zero == UnsignedInteger <Bytes>::createFromInteger (0U), "should be zero");
        expect (one == UnsignedInteger <Bytes>::createFromInteger (1U), "should be one");
        expect (one != UnsignedInteger <Bytes>::createFromInteger (2U), "should not be two");

        UnsignedInteger <Bytes> largest = UnsignedInteger <Bytes>::createFilled (0xff);

        expect (largest > zero && largest > one, "should be greater");
        expect (~largest == zero, "should be zero");
        expect (~one < largest, "should be less");
    }

    void runTest()
    {
        runTest <16> ();
        runTest <33> ();
    }

private:
};

static UnsignedIntegerTests unsignedIntegerTests;
