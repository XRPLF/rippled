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
#include <ripple/protocol/JsonFields.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class RPCOverload_test : public beast::unit_test::suite
{
public:
    void testOverload(bool useWS)
    {
        testcase << "Overload " << (useWS ? "WS" : "HTTP") << " RPC client";
        using namespace jtx;
        Env env {*this, no_admin_cfg};

        Account const alice {"alice"};
        Account const bob {"bob"};
        env.fund (XRP (10000), alice, bob);

        std::unique_ptr<AbstractClient> client = useWS ?
              makeWSClient(env.app().config())
            : makeJSONRPCClient(env.app().config());

        Json::Value tx = Json::objectValue;
        tx[jss::tx_json] = pay(alice, bob, XRP(1));
        tx[jss::secret] = toBase58(generateSeed("alice"));

        // Ask the server to repeatedly sign this transaction
        // Signing is a resource heavy transaction, so we want the server
        // to warn and eventually boot us.
        bool warned = false, booted = false;
        for(int i = 0 ; i < 500 && !booted; ++i)
        {
            auto jv = client->invoke("sign", tx);
            if(!useWS)
                jv = jv[jss::result];
            // When booted, we just get a null json response
            if(jv.isNull())
                booted = true;
            else
                BEAST_EXPECT(jv.isMember(jss::status)
                             && (jv[jss::status] == "success"));

            if(jv.isMember(jss::warning))
                warned = jv[jss::warning] == jss::load;
        }
        BEAST_EXPECT(warned && booted);
    }



    void run() override
    {
        testOverload(false /* http */);
        testOverload(true /* ws */);
    }
};

BEAST_DEFINE_TESTSUITE(RPCOverload,app,ripple);

} // test
} // ripple
