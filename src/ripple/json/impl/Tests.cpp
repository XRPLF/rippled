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

#include <beast/unit_test/suite.h>
#include <beast/utility/type_name.h>

namespace ripple {

class JsonCpp_test : public beast::unit_test::suite
{
public:
    void testBadJson ()
    {
        char const* s (
            "{\"method\":\"ledger\",\"params\":[{\"ledger_index\":1e300}]}"
            );

        Json::Value j;
        Json::Reader r;

        r.parse (s, j);
        pass ();
    }

    void
    test_copy ()
    {
        Json::Value v1{2.5};
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);

        Json::Value v2 = v1;
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 == v2);

        v1 = v2;
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 == v2);

        pass ();
    }

    void
    test_move ()
    {
        Json::Value v1{2.5};
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);

        Json::Value v2 = std::move(v1);
        expect (v1.isNull ());
        expect (v2.isDouble ());
        expect (v2.asDouble () == 2.5);
        expect (v1 != v2);

        v1 = std::move(v2);
        expect (v1.isDouble ());
        expect (v1.asDouble () == 2.5);
        expect (v2.isNull ());
        expect (v1 != v2);

        pass ();
    }

    void run ()
    {
        testBadJson ();
        test_copy ();
        test_move ();
    }
};

BEAST_DEFINE_TESTSUITE(JsonCpp,json,ripple);

} // ripple
