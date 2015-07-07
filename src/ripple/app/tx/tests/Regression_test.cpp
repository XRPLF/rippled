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

#include <BeastConfig.h>
#include <ripple/test/jtx.h>

namespace ripple {
namespace test {

struct Regression_test : public beast::unit_test::suite
{
    // SigningPubKey: 0000000000000000000000000000000000 (34 zeroes)
    void testBadSigningPubKey()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        env(noop("alice"), sig(none), json( R"raw( {
                "SigningPubKey" : "0000000000000000000000000000000000",
                "TxnSignature" : "3044022042D144D130A1651CBE5632196FE4E745A75445AA8DB95AC9905701DC891F9A30022012DF180ED1545B560681D475F570D9603BF663BD4C91F591DBA0A8C43876C563"
                    } )raw"), ter(temINVALID));
    }

    void run() override
    {
        testBadSigningPubKey();
    }
};

BEAST_DEFINE_TESTSUITE(Regression,app,ripple);

} // test
} // ripple
