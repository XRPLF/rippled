//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <ripple/test/WSClient.h>
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class WSClient_test : public beast::unit_test::suite
{
public:
    void run() override
    {
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        {
            Json::Value jv;
            jv["streams"] = Json::arrayValue;
            jv["streams"].append("ledger");
            //jv["streams"].append("server");
            //jv["streams"].append("transactions");
            //jv["streams"].append("transactions_proposed");
            log << pretty(wsc->invoke("subscribe", jv));
        }
        env.fund(XRP(10000), "alice");
        env.close();
        /*
        env.fund(XRP(10000), "dan", "eric", "fred");
        env.close();
        env.fund(XRP(10000), "george", "harold", "iris");
        env.close();
        */
        auto jv = wsc->getMsg(std::chrono::seconds(1));
        if(jv)
            log << pretty(*jv);
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(WSClient,test,ripple);

} // test
} // ripple
