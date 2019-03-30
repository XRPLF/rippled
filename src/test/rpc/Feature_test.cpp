//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/app/misc/AmendmentTable.h>

namespace ripple {

class Feature_test : public beast::unit_test::suite
{
    void
    testNoParams()
    {
        testcase ("No Params, None Enabled");

        using namespace test::jtx;
        Env env {*this};

        auto jrr = env.rpc("feature") [jss::result];
        if(! BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for(auto const& feature : jrr[jss::features])
        {
            if(! BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            //default config - so all should be disabled, not vetoed, and supported
            BEAST_EXPECTS(! feature[jss::enabled].asBool(),
                feature[jss::name].asString() + " enabled");
            BEAST_EXPECTS(! feature[jss::vetoed].asBool(),
                feature[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(feature[jss::supported].asBool(),
                feature[jss::name].asString() + " supported");
        }
    }

    void
    testSingleFeature()
    {
        testcase ("Feature Param");

        using namespace test::jtx;
        Env env {*this};

        auto jrr = env.rpc("feature", "CryptoConditions") [jss::result];
        BEAST_EXPECTS(jrr[jss::status] == jss::success, "status");
        jrr.removeMember(jss::status);
        BEAST_EXPECT(jrr.size() == 1);
        auto feature = *(jrr.begin());

        BEAST_EXPECTS(feature[jss::name] == "CryptoConditions", "name");
        BEAST_EXPECTS(! feature[jss::enabled].asBool(), "enabled");
        BEAST_EXPECTS(! feature[jss::vetoed].asBool(), "vetoed");
        BEAST_EXPECTS(feature[jss::supported].asBool(), "supported");

        // feature names are case-sensitive - expect error here
        jrr = env.rpc("feature", "cryptoconditions") [jss::result];
        BEAST_EXPECT(jrr[jss::error] == "badFeature");
        BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");
    }

    void
    testInvalidFeature()
    {
        testcase ("Invalid Feature");

        using namespace test::jtx;
        Env env {*this};

        auto jrr = env.rpc("feature", "AllTheThings") [jss::result];
        BEAST_EXPECT(jrr[jss::error] == "badFeature");
        BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");
    }

    void
    testNonAdmin()
    {
        testcase ("Feature Without Admin");

        using namespace test::jtx;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg) {
            (*cfg)["port_rpc"].set("admin","");
            (*cfg)["port_ws"].set("admin","");
            return cfg;
        })};

        auto jrr = env.rpc("feature") [jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error. The JSONRPCClient just eats that
        // error and returns an null result.
        BEAST_EXPECT(jrr.isNull());
    }

    void
    testSomeEnabled()
    {
        testcase ("No Params, Some Enabled");

        using namespace test::jtx;
        Env env {*this,
            FeatureBitset(featureEscrow, featureCryptoConditions)};

        auto jrr = env.rpc("feature") [jss::result];
        if(! BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for(auto it = jrr[jss::features].begin();
            it != jrr[jss::features].end(); ++it)
        {
            uint256 id;
            id.SetHexExact(it.key().asString().c_str());
            if(! BEAST_EXPECT((*it).isMember(jss::name)))
                return;
            bool expectEnabled = env.app().getAmendmentTable().isEnabled(id);
            bool expectSupported = env.app().getAmendmentTable().isSupported(id);
            BEAST_EXPECTS((*it)[jss::enabled].asBool() == expectEnabled,
                (*it)[jss::name].asString() + " enabled");
            BEAST_EXPECTS(! (*it)[jss::vetoed].asBool(),
                (*it)[jss::name].asString() + " vetoed");
            BEAST_EXPECTS((*it)[jss::supported].asBool() == expectSupported,
                (*it)[jss::name].asString() + " supported");
        }
    }

    void
    testWithMajorities()
    {
        testcase ("With Majorities");

        using namespace test::jtx;
        Env env {*this, envconfig(validator, "")};

        auto jrr = env.rpc("feature") [jss::result];
        if(! BEAST_EXPECT(jrr.isMember(jss::features)))
            return;

        // at this point, there are no majorities so no fields related to
        // amendment voting
        for(auto const& feature : jrr[jss::features])
        {
            if(! BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            BEAST_EXPECTS(! feature.isMember(jss::majority),
                feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(! feature.isMember(jss::count),
                feature[jss::name].asString() + " count");
            BEAST_EXPECTS(! feature.isMember(jss::threshold),
                feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(! feature.isMember(jss::validations),
                feature[jss::name].asString() + " validations");
            BEAST_EXPECTS(! feature.isMember(jss::vote),
                feature[jss::name].asString() + " vote");
        }

        auto majorities = getMajorityAmendments (*env.closed());
        if(! BEAST_EXPECT(majorities.empty()))
            return;

        // close ledgers until the amendments show up.
        for (auto i = 0; i <= 256; ++i)
        {
            env.close();
            majorities = getMajorityAmendments (*env.closed());
            if (! majorities.empty())
                break;
        }

        // There should be at least 5 amendments.  Don't do exact comparison
        // to avoid maintenance as more amendments are added in the future.
        BEAST_EXPECT(majorities.size() >= 5);

        jrr = env.rpc("feature") [jss::result];
        if(! BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for(auto const& feature : jrr[jss::features])
        {
            if(! BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            BEAST_EXPECTS(feature.isMember(jss::majority),
                feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(feature.isMember(jss::count),
                feature[jss::name].asString() + " count");
            BEAST_EXPECTS(feature.isMember(jss::threshold),
                feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(feature.isMember(jss::validations),
                feature[jss::name].asString() + " validations");
            BEAST_EXPECTS(feature.isMember(jss::vote),
                feature[jss::name].asString() + " vote");
            BEAST_EXPECT(feature[jss::vote] == 256);
            BEAST_EXPECT(feature[jss::majority] == 2740);
        }

    }

    void
    testVeto()
    {
        testcase ("Veto");

        using namespace test::jtx;
        Env env {*this,
            FeatureBitset(featureCryptoConditions)};

        auto jrr = env.rpc("feature", "CryptoConditions") [jss::result];
        if(! BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if(! BEAST_EXPECT(jrr.size() == 1))
            return;
        auto feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "CryptoConditions", "name");
        BEAST_EXPECTS(! feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", "CryptoConditions", "reject") [jss::result];
        if(! BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if(! BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "CryptoConditions", "name");
        BEAST_EXPECTS(feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", "CryptoConditions", "accept") [jss::result];
        if(! BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if(! BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "CryptoConditions", "name");
        BEAST_EXPECTS(! feature[jss::vetoed].asBool(), "vetoed");

        // anything other than accept or reject is an error
        jrr = env.rpc("feature", "CryptoConditions", "maybe");
        BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        BEAST_EXPECT(jrr[jss::error_message] == "Invalid parameters.");
    }

public:

    void run() override
    {
        testNoParams();
        testSingleFeature();
        testInvalidFeature();
        testNonAdmin();
        testSomeEnabled();
        testWithMajorities();
        testVeto();
    }
};

BEAST_DEFINE_TESTSUITE(Feature,rpc,ripple);

} // ripple
