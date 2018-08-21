//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2018 Ripple Labs Inc.

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

//
// Copyright (c) 2016-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

#include <ripple/basics/base64.h>
#include <ripple/beast/unit_test.h>

namespace ripple {

class base64_test : public beast::unit_test::suite
{
public:
    void
    check (std::string const& in, std::string const& out)
    {
        auto const encoded = base64_encode (in);
        BEAST_EXPECT(encoded == out);
        BEAST_EXPECT(base64_decode (encoded) == in);
    }

    void
    run() override
    {
        check ("",       "");
        check ("f",      "Zg==");
        check ("fo",     "Zm8=");
        check ("foo",    "Zm9v");
        check ("foob",   "Zm9vYg==");
        check ("fooba",  "Zm9vYmE=");
        check ("foobar", "Zm9vYmFy");

        check(
            "Man is distinguished, not only by his reason, but by this singular passion from "
            "other animals, which is a lust of the mind, that by a perseverance of delight "
            "in the continued and indefatigable generation of knowledge, exceeds the short "
            "vehemence of any carnal pleasure."
            ,
            "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlz"
            "IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2Yg"
            "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGlu"
            "dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRo"
            "ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4="
            );

        std::string const notBase64 = "not_base64!!";
        std::string const truncated = "not";
        BEAST_EXPECT(base64_decode(notBase64) == base64_decode(truncated));
    }
};

BEAST_DEFINE_TESTSUITE(base64, ripple_basics, ripple);

} // ripple
