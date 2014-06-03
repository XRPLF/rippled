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

#include <beast/unit_test/suite.h>

namespace beast {

class LexicalCast_test : public unit_test::suite
{
public:
    template <class IntType>
    static IntType nextRandomInt (Random& r)
    {
        return static_cast <IntType> (r.nextInt64 ());
    }

    template <class IntType>
    void testInteger (IntType in)
    {
        std::string s;
        IntType out (in+1);

        expect (lexicalCastChecked (s, in));
        expect (lexicalCastChecked (out, s));
        expect (out == in);
    }

    template <class IntType>
    void testIntegers (Random& r)
    {
        {
            std::stringstream ss;
            ss <<
                "random " << typeid (IntType).name ();
            testcase (ss.str());

            for (int i = 0; i < 1000; ++i)
            {
                IntType const value (nextRandomInt <IntType> (r));
                testInteger (value);
            }
        }

        {
            std::stringstream ss;
            ss <<
                "numeric_limits <" << typeid (IntType).name () << ">";
            testcase (ss.str());

            testInteger (std::numeric_limits <IntType>::min ());
            testInteger (std::numeric_limits <IntType>::max ());
        }
    }

    void testPathologies()
    {
        testcase("pathologies");
        try
        {
            lexicalCastThrow<int>("\xef\xbc\x91\xef\xbc\x90"); // utf-8 encoded
            fail("Should throw");
        }
        catch(BadLexicalCast const&)
        {
            pass();
        }
    }

    void run()
    {
        std::int64_t const seedValue = 50;

        Random r (seedValue);

        testIntegers <int> (r);
        testIntegers <unsigned int> (r);
        testIntegers <short> (r);
        testIntegers <unsigned short> (r);
        testIntegers <std::int32_t> (r);
        testIntegers <std::uint32_t> (r);
        testIntegers <std::int64_t> (r);
        testIntegers <std::uint64_t> (r);

        testPathologies();
    }
};

BEAST_DEFINE_TESTSUITE(LexicalCast,beast_core,beast);

} // beast
