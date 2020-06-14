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

#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class Feature_test : public beast::unit_test::suite
{
    void
    testInternals()
    {
        testcase("internals");

        std::map<std::string, DefaultVote> const& supported =
            ripple::detail::supportedAmendments();
        BEAST_EXPECT(
            supported.size() ==
            ripple::detail::numDownVotedAmendments() +
                ripple::detail::numUpVotedAmendments());
        std::size_t up = 0, down = 0;
        for (std::pair<std::string const, DefaultVote> const& amendment :
             supported)
        {
            if (amendment.second == DefaultVote::no)
                ++down;
            else
            {
                if (BEAST_EXPECT(amendment.second == DefaultVote::yes))
                    ++up;
            }
        }
        BEAST_EXPECT(down == ripple::detail::numDownVotedAmendments());
        BEAST_EXPECT(up == ripple::detail::numUpVotedAmendments());
    }

    void
    testFeatureLookups()
    {
        testcase("featureToName");

        // Test all the supported features. In a perfect world, this would test
        // FeatureCollections::featureNames, but that's private. Leave it that
        // way.
        auto const supported = ripple::detail::supportedAmendments();

        for (auto const& [feature, vote] : supported)
        {
            (void)vote;
            auto const registered = getRegisteredFeature(feature);
            if (BEAST_EXPECT(registered))
            {
                BEAST_EXPECT(featureToName(*registered) == feature);
                BEAST_EXPECT(
                    bitsetIndexToFeature(featureToBitsetIndex(*registered)) ==
                    *registered);
            }
        }

        // Test an arbitrary unknown feature
        uint256 zero{0};
        BEAST_EXPECT(featureToName(zero) == to_string(zero));
        BEAST_EXPECT(
            featureToName(zero) ==
            "0000000000000000000000000000000000000000000000000000000000000000");

        // Test looking up an unknown feature
        BEAST_EXPECT(!getRegisteredFeature("unknown"));

        // Test a random sampling of the variables. If any of these get retired
        // or removed, swap out for any other feature.
        BEAST_EXPECT(featureToName(featureOwnerPaysFee) == "OwnerPaysFee");
        BEAST_EXPECT(featureToName(featureFlow) == "Flow");
        BEAST_EXPECT(featureToName(featureNegativeUNL) == "NegativeUNL");
        BEAST_EXPECT(featureToName(fix1578) == "fix1578");
        BEAST_EXPECT(
            featureToName(fixTakerDryOfferRemoval) ==
            "fixTakerDryOfferRemoval");
    }

    void
    testNoParams()
    {
        testcase("No Params, None Enabled");

        using namespace test::jtx;
        Env env{*this};

        std::map<std::string, DefaultVote> const& votes =
            ripple::detail::supportedAmendments();

        auto jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto const& feature : jrr[jss::features])
        {
            if (!BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            // default config - so all should be disabled, and
            // supported. Some may be vetoed.
            bool expectVeto =
                !(votes.at(feature[jss::name].asString()) == DefaultVote::yes);
            BEAST_EXPECTS(
                !feature[jss::enabled].asBool(),
                feature[jss::name].asString() + " enabled");
            BEAST_EXPECTS(
                feature[jss::vetoed].asBool() == expectVeto,
                feature[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(
                feature[jss::supported].asBool(),
                feature[jss::name].asString() + " supported");
        }
    }

    void
    testSingleFeature()
    {
        testcase("Feature Param");

        using namespace test::jtx;
        Env env{*this};

        auto jrr = env.rpc("feature", "MultiSignReserve")[jss::result];
        BEAST_EXPECTS(jrr[jss::status] == jss::success, "status");
        jrr.removeMember(jss::status);
        BEAST_EXPECT(jrr.size() == 1);
        BEAST_EXPECT(
            jrr.isMember("586480873651E106F1D6339B0C4A8945BA705A777F3F4524626FF"
                         "1FC07EFE41D"));
        auto feature = *(jrr.begin());

        BEAST_EXPECTS(feature[jss::name] == "MultiSignReserve", "name");
        BEAST_EXPECTS(!feature[jss::enabled].asBool(), "enabled");
        BEAST_EXPECTS(!feature[jss::vetoed].asBool(), "vetoed");
        BEAST_EXPECTS(feature[jss::supported].asBool(), "supported");

        // feature names are case-sensitive - expect error here
        jrr = env.rpc("feature", "multiSignReserve")[jss::result];
        BEAST_EXPECT(jrr[jss::error] == "badFeature");
        BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");
    }

    void
    testInvalidFeature()
    {
        testcase("Invalid Feature");

        using namespace test::jtx;
        Env env{*this};

        auto jrr = env.rpc("feature", "AllTheThings")[jss::result];
        BEAST_EXPECT(jrr[jss::error] == "badFeature");
        BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");
    }

    void
    testNonAdmin()
    {
        testcase("Feature Without Admin");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)["port_rpc"].set("admin", "");
                    (*cfg)["port_ws"].set("admin", "");
                    return cfg;
                })};

        auto jrr = env.rpc("feature")[jss::result];
        // The current HTTP/S ServerHandler returns an HTTP 403 error code here
        // rather than a noPermission JSON error. The JSONRPCClient just eats
        // that error and returns an null result.
        BEAST_EXPECT(jrr.isNull());
    }

    void
    testSomeEnabled()
    {
        testcase("No Params, Some Enabled");

        using namespace test::jtx;
        Env env{
            *this, FeatureBitset(featureDepositAuth, featureDepositPreauth)};

        std::map<std::string, DefaultVote> const& votes =
            ripple::detail::supportedAmendments();

        auto jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto it = jrr[jss::features].begin();
             it != jrr[jss::features].end();
             ++it)
        {
            uint256 id;
            (void)id.parseHex(it.key().asString().c_str());
            if (!BEAST_EXPECT((*it).isMember(jss::name)))
                return;
            bool expectEnabled = env.app().getAmendmentTable().isEnabled(id);
            bool expectSupported =
                env.app().getAmendmentTable().isSupported(id);
            bool expectVeto =
                !(votes.at((*it)[jss::name].asString()) == DefaultVote::yes);
            BEAST_EXPECTS(
                (*it)[jss::enabled].asBool() == expectEnabled,
                (*it)[jss::name].asString() + " enabled");
            BEAST_EXPECTS(
                (*it)[jss::vetoed].asBool() == expectVeto,
                (*it)[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(
                (*it)[jss::supported].asBool() == expectSupported,
                (*it)[jss::name].asString() + " supported");
        }
    }

    void
    testWithMajorities()
    {
        testcase("With Majorities");

        using namespace test::jtx;
        Env env{*this, envconfig(validator, "")};

        auto jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;

        // at this point, there are no majorities so no fields related to
        // amendment voting
        for (auto const& feature : jrr[jss::features])
        {
            if (!BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            BEAST_EXPECTS(
                !feature.isMember(jss::majority),
                feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(
                !feature.isMember(jss::count),
                feature[jss::name].asString() + " count");
            BEAST_EXPECTS(
                !feature.isMember(jss::threshold),
                feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(
                !feature.isMember(jss::validations),
                feature[jss::name].asString() + " validations");
            BEAST_EXPECTS(
                !feature.isMember(jss::vote),
                feature[jss::name].asString() + " vote");
        }

        auto majorities = getMajorityAmendments(*env.closed());
        if (!BEAST_EXPECT(majorities.empty()))
            return;

        // close ledgers until the amendments show up.
        for (auto i = 0; i <= 256; ++i)
        {
            env.close();
            majorities = getMajorityAmendments(*env.closed());
            if (!majorities.empty())
                break;
        }

        // There should be at least 5 amendments.  Don't do exact comparison
        // to avoid maintenance as more amendments are added in the future.
        BEAST_EXPECT(majorities.size() >= 5);
        std::map<std::string, DefaultVote> const& votes =
            ripple::detail::supportedAmendments();

        jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto const& feature : jrr[jss::features])
        {
            if (!BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            bool expectVeto =
                !(votes.at(feature[jss::name].asString()) == DefaultVote::yes);
            BEAST_EXPECTS(
                expectVeto ^ feature.isMember(jss::majority),
                feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(
                feature.isMember(jss::vetoed) &&
                    feature[jss::vetoed].asBool() == expectVeto,
                feature[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(
                feature.isMember(jss::count),
                feature[jss::name].asString() + " count");
            BEAST_EXPECTS(
                feature.isMember(jss::threshold),
                feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(
                feature.isMember(jss::validations),
                feature[jss::name].asString() + " validations");
            BEAST_EXPECT(feature[jss::count] == (expectVeto ? 0 : 1));
            BEAST_EXPECT(feature[jss::threshold] == 1);
            BEAST_EXPECT(feature[jss::validations] == 1);
            BEAST_EXPECTS(
                expectVeto || feature[jss::majority] == 2540,
                "Majority: " + feature[jss::majority].asString());
        }
    }

    void
    testVeto()
    {
        testcase("Veto");

        using namespace test::jtx;
        Env env{*this, FeatureBitset(featureMultiSignReserve)};

        auto jrr = env.rpc("feature", "MultiSignReserve")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        auto feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "MultiSignReserve", "name");
        BEAST_EXPECTS(!feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", "MultiSignReserve", "reject")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "MultiSignReserve", "name");
        BEAST_EXPECTS(feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", "MultiSignReserve", "accept")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == "MultiSignReserve", "name");
        BEAST_EXPECTS(!feature[jss::vetoed].asBool(), "vetoed");

        // anything other than accept or reject is an error
        jrr = env.rpc("feature", "MultiSignReserve", "maybe");
        BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        BEAST_EXPECT(jrr[jss::error_message] == "Invalid parameters.");
    }

public:
    void
    run() override
    {
        testInternals();
        testFeatureLookups();
        testNoParams();
        testSingleFeature();
        testInvalidFeature();
        testNonAdmin();
        testSomeEnabled();
        testWithMajorities();
        testVeto();
    }
};

BEAST_DEFINE_TESTSUITE(Feature, rpc, ripple);

}  // namespace ripple
