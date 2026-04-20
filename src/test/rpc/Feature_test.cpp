
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/AmendmentTable.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>

namespace xrpl {

class Feature_test : public beast::unit_test::suite
{
    void
    testInternals()
    {
        testcase("internals");

        auto const& supportedAmendments = xrpl::detail::supportedAmendments();
        auto const& allAmendments = xrpl::allAmendments();

        BEAST_EXPECT(
            supportedAmendments.size() ==
            xrpl::detail::numDownVotedAmendments() + xrpl::detail::numUpVotedAmendments());
        {
            std::size_t up = 0, down = 0, obsolete = 0;
            for (auto const& [name, vote] : supportedAmendments)
            {
                switch (vote)
                {
                    case VoteBehavior::DefaultYes:
                        ++up;
                        break;
                    case VoteBehavior::DefaultNo:
                        ++down;
                        break;
                    case VoteBehavior::Obsolete:
                        ++obsolete;
                        break;
                    default:
                        fail("Unknown VoteBehavior", __FILE__, __LINE__);
                }

                if (vote == VoteBehavior::Obsolete)
                {
                    BEAST_EXPECT(
                        allAmendments.contains(name) &&
                        allAmendments.at(name) == AmendmentSupport::Retired);
                }
                else
                {
                    BEAST_EXPECT(
                        allAmendments.contains(name) &&
                        allAmendments.at(name) == AmendmentSupport::Supported);
                }
            }
            BEAST_EXPECT(down + obsolete == xrpl::detail::numDownVotedAmendments());
            BEAST_EXPECT(up == xrpl::detail::numUpVotedAmendments());
        }
        {
            std::size_t supported = 0, unsupported = 0, retired = 0;
            for (auto const& [name, support] : allAmendments)
            {
                switch (support)
                {
                    case AmendmentSupport::Supported:
                        ++supported;
                        BEAST_EXPECT(supportedAmendments.contains(name));
                        break;
                    case AmendmentSupport::Unsupported:
                        ++unsupported;
                        break;
                    case AmendmentSupport::Retired:
                        ++retired;
                        break;
                    default:
                        fail("Unknown AmendmentSupport", __FILE__, __LINE__);
                }
            }

            BEAST_EXPECT(supported + retired == supportedAmendments.size());
            BEAST_EXPECT(allAmendments.size() - unsupported == supportedAmendments.size());
        }
    }

    void
    testFeatureLookups()
    {
        testcase("featureToName");

        // Test all the supported features. In a perfect world, this would test
        // FeatureCollections::featureNames, but that's private. Leave it that
        // way.
        auto const supported = xrpl::detail::supportedAmendments();

        for (auto const& [feature, vote] : supported)
        {
            (void)vote;
            auto const registered = getRegisteredFeature(feature);

            if (BEAST_EXPECT(registered); registered.has_value())
            {
                BEAST_EXPECT(featureToName(*registered) == feature);
                BEAST_EXPECT(
                    bitsetIndexToFeature(featureToBitsetIndex(*registered)) == *registered);
            }
        }

        // Test an arbitrary unknown feature
        uint256 const zero{0};
        BEAST_EXPECT(featureToName(zero) == to_string(zero));
        BEAST_EXPECT(
            featureToName(zero) ==
            "0000000000000000000000000000000000000000000000000000000000000000");

        // Test looking up an unknown feature
        BEAST_EXPECT(!getRegisteredFeature("unknown"));

        // Test a random sampling of the variables. If any of these get retired
        // or removed, swap out for any other feature.
        BEAST_EXPECT(
            featureToName(fixRemoveNFTokenAutoTrustLine) == "fixRemoveNFTokenAutoTrustLine");
        BEAST_EXPECT(featureToName(featureBatch) == "Batch");
        BEAST_EXPECT(featureToName(featureDID) == "DID");
        BEAST_EXPECT(featureToName(fixIncludeKeyletFields) == "fixIncludeKeyletFields");
        BEAST_EXPECT(featureToName(featureTokenEscrow) == "TokenEscrow");
    }

    void
    testNoParams()
    {
        testcase("No Params, None Enabled");

        using namespace test::jtx;
        Env env{*this};

        std::map<std::string, VoteBehavior> const& votes = xrpl::detail::supportedAmendments();

        auto jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto const& feature : jrr[jss::features])
        {
            if (!BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            // default config - so all should be disabled, and
            // supported. Some may be vetoed.
            bool const expectVeto =
                (votes.at(feature[jss::name].asString()) == VoteBehavior::DefaultNo);
            bool const expectObsolete =
                (votes.at(feature[jss::name].asString()) == VoteBehavior::Obsolete);
            BEAST_EXPECTS(
                feature.isMember(jss::enabled) && !feature[jss::enabled].asBool(),
                feature[jss::name].asString() + " enabled");
            BEAST_EXPECTS(
                feature.isMember(jss::vetoed) && feature[jss::vetoed].isBool() == !expectObsolete &&
                    (!feature[jss::vetoed].isBool() ||
                     feature[jss::vetoed].asBool() == expectVeto) &&
                    (feature[jss::vetoed].isBool() ||
                     feature[jss::vetoed].asString() == "Obsolete"),
                feature[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(
                feature.isMember(jss::supported) && feature[jss::supported].asBool(),
                feature[jss::name].asString() + " supported");
        }
    }

    void
    testSingleFeature()
    {
        testcase("Feature Param");

        using namespace test::jtx;
        Env env{*this};

        std::string const name = "fixAMMOverflowOffer";
        auto jrr = env.rpc("feature", name)[jss::result];
        BEAST_EXPECTS(jrr[jss::status] == jss::success, "status");
        jrr.removeMember(jss::status);
        BEAST_EXPECT(jrr.size() == 1);
        auto const expected = to_string(sha512Half(Slice(name.data(), name.size())));
        char const sha[] = "12523DF04B553A0B1AD74F42DDB741DE8DC06A03FC089A0EF197E2A87F1D8107";
        BEAST_EXPECT(expected == sha);
        BEAST_EXPECT(jrr.isMember(expected));
        auto feature = *(jrr.begin());

        BEAST_EXPECTS(feature[jss::name] == name, "name");
        BEAST_EXPECTS(!feature[jss::enabled].asBool(), "enabled");
        BEAST_EXPECTS(feature[jss::vetoed].isBool() && !feature[jss::vetoed].asBool(), "vetoed");
        BEAST_EXPECTS(feature[jss::supported].asBool(), "supported");

        // feature names are case-sensitive - expect error here
        jrr = env.rpc("feature", "fMM")[jss::result];
        BEAST_EXPECT(jrr[jss::error] == "badFeature");
        BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");

        // Test feature name size checks
        constexpr auto ok63Name = [] {
            return "123456789012345678901234567890123456789012345678901234567890123";
        };
        static_assert(validFeatureNameSize(ok63Name));

        constexpr auto bad64Name = [] {
            return "1234567890123456789012345678901234567890123456789012345678901234";
        };
        static_assert(!validFeatureNameSize(bad64Name));

        constexpr auto ok31Name = [] { return "1234567890123456789012345678901"; };
        static_assert(validFeatureNameSize(ok31Name));

        constexpr auto bad32Name = [] { return "12345678901234567890123456789012"; };
        static_assert(!validFeatureNameSize(bad32Name));

        constexpr auto ok33Name = [] { return "123456789012345678901234567890123"; };
        static_assert(validFeatureNameSize(ok33Name));

        // Test feature character set checks
        constexpr auto okName = [] { return "AMM_123"; };
        static_assert(validFeatureName(okName));

        // First character is Greek Capital Alpha, visually confusable with ASCII 'A'
        constexpr auto badName = [] { return "ΑMM_123"; };
        static_assert(!validFeatureName(badName));

        constexpr auto badEmoji = [] { return "🔥"; };
        static_assert(!validFeatureName(badEmoji));
    }

    void
    testInvalidFeature()
    {
        testcase("Invalid Feature");

        using namespace test::jtx;
        Env env{*this};

        auto testInvalidParam = [&](auto const& param) {
            Json::Value params;
            params[jss::feature] = param;
            auto jrr = env.rpc("json", "feature", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid parameters.");
        };

        testInvalidParam(1);
        testInvalidParam(1.1);
        testInvalidParam(true);
        testInvalidParam(Json::Value(Json::nullValue));
        testInvalidParam(Json::Value(Json::objectValue));
        testInvalidParam(Json::Value(Json::arrayValue));

        {
            auto jrr = env.rpc("feature", "AllTheThings")[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "badFeature");
            BEAST_EXPECT(jrr[jss::error_message] == "Feature unknown or invalid.");
        }
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

        {
            auto result = env.rpc("feature")[jss::result];
            BEAST_EXPECT(result.isMember(jss::features));
            // There should be at least 50 amendments.  Don't do exact
            // comparison to avoid maintenance as more amendments are added in
            // the future.
            BEAST_EXPECT(result[jss::features].size() >= 50);
            for (auto it = result[jss::features].begin(); it != result[jss::features].end(); ++it)
            {
                uint256 id;
                (void)id.parseHex(it.key().asString().c_str());
                if (!BEAST_EXPECT((*it).isMember(jss::name)))
                    return;
                bool const expectEnabled = env.app().getAmendmentTable().isEnabled(id);
                bool const expectSupported = env.app().getAmendmentTable().isSupported(id);
                BEAST_EXPECTS(
                    (*it).isMember(jss::enabled) && (*it)[jss::enabled].asBool() == expectEnabled,
                    (*it)[jss::name].asString() + " enabled");
                BEAST_EXPECTS(
                    (*it).isMember(jss::supported) &&
                        (*it)[jss::supported].asBool() == expectSupported,
                    (*it)[jss::name].asString() + " supported");
                BEAST_EXPECT(!(*it).isMember(jss::vetoed));
                BEAST_EXPECT(!(*it).isMember(jss::majority));
                BEAST_EXPECT(!(*it).isMember(jss::count));
                BEAST_EXPECT(!(*it).isMember(jss::validations));
                BEAST_EXPECT(!(*it).isMember(jss::threshold));
            }
        }

        {
            Json::Value params;
            // invalid feature
            params[jss::feature] =
                "1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCD"
                "EF";
            auto const result = env.rpc("json", "feature", to_string(params))[jss::result];
            BEAST_EXPECTS(result[jss::error] == "badFeature", result.toStyledString());
            BEAST_EXPECT(result[jss::error_message] == "Feature unknown or invalid.");
        }

        {
            Json::Value params;
            params[jss::feature] =
                "93E516234E35E08CA689FA33A6D38E103881F8DCB53023F728C307AA89D515"
                "A7";
            // invalid param
            params[jss::vetoed] = true;
            auto const result = env.rpc("json", "feature", to_string(params))[jss::result];
            BEAST_EXPECTS(result[jss::error] == "noPermission", result[jss::error].asString());
            BEAST_EXPECT(
                result[jss::error_message] == "You don't have permission for this command.");
        }
    }

    void
    testSomeEnabled()
    {
        testcase("No Params, Some Enabled");

        using namespace test::jtx;
        Env env{*this, FeatureBitset{}};

        std::map<std::string, VoteBehavior> const& votes = xrpl::detail::supportedAmendments();

        auto jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto it = jrr[jss::features].begin(); it != jrr[jss::features].end(); ++it)
        {
            uint256 id;
            (void)id.parseHex(it.key().asString().c_str());
            if (!BEAST_EXPECT((*it).isMember(jss::name)))
                return;
            bool const expectEnabled = env.app().getAmendmentTable().isEnabled(id);
            bool const expectSupported = env.app().getAmendmentTable().isSupported(id);
            bool const expectVeto =
                (votes.at((*it)[jss::name].asString()) == VoteBehavior::DefaultNo);
            bool const expectObsolete =
                (votes.at((*it)[jss::name].asString()) == VoteBehavior::Obsolete);
            BEAST_EXPECTS(
                (*it).isMember(jss::enabled) && (*it)[jss::enabled].asBool() == expectEnabled,
                (*it)[jss::name].asString() + " enabled");
            if (expectEnabled)
            {
                BEAST_EXPECTS(
                    !(*it).isMember(jss::vetoed), (*it)[jss::name].asString() + " vetoed");
            }
            else
            {
                BEAST_EXPECTS(
                    (*it).isMember(jss::vetoed) && (*it)[jss::vetoed].isBool() == !expectObsolete &&
                        (!(*it)[jss::vetoed].isBool() ||
                         (*it)[jss::vetoed].asBool() == expectVeto) &&
                        ((*it)[jss::vetoed].isBool() ||
                         (*it)[jss::vetoed].asString() == "Obsolete"),
                    (*it)[jss::name].asString() + " vetoed");
            }
            BEAST_EXPECTS(
                (*it).isMember(jss::supported) && (*it)[jss::supported].asBool() == expectSupported,
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
                !feature.isMember(jss::majority), feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(!feature.isMember(jss::count), feature[jss::name].asString() + " count");
            BEAST_EXPECTS(
                !feature.isMember(jss::threshold), feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(
                !feature.isMember(jss::validations),
                feature[jss::name].asString() + " validations");
            BEAST_EXPECTS(!feature.isMember(jss::vote), feature[jss::name].asString() + " vote");
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

        // There should be at least 2 amendments.  Don't do exact comparison
        // to avoid maintenance as more amendments are added in the future.
        BEAST_EXPECT(majorities.size() >= 2);
        std::map<std::string, VoteBehavior> const& votes = xrpl::detail::supportedAmendments();

        jrr = env.rpc("feature")[jss::result];
        if (!BEAST_EXPECT(jrr.isMember(jss::features)))
            return;
        for (auto const& feature : jrr[jss::features])
        {
            if (!BEAST_EXPECT(feature.isMember(jss::name)))
                return;
            bool const expectVeto =
                (votes.at(feature[jss::name].asString()) == VoteBehavior::DefaultNo);
            bool const expectObsolete =
                (votes.at(feature[jss::name].asString()) == VoteBehavior::Obsolete);
            BEAST_EXPECTS(
                (expectVeto || expectObsolete) ^ feature.isMember(jss::majority),
                feature[jss::name].asString() + " majority");
            BEAST_EXPECTS(
                feature.isMember(jss::vetoed) && feature[jss::vetoed].isBool() == !expectObsolete &&
                    (!feature[jss::vetoed].isBool() ||
                     feature[jss::vetoed].asBool() == expectVeto) &&
                    (feature[jss::vetoed].isBool() ||
                     feature[jss::vetoed].asString() == "Obsolete"),
                feature[jss::name].asString() + " vetoed");
            BEAST_EXPECTS(feature.isMember(jss::count), feature[jss::name].asString() + " count");
            BEAST_EXPECTS(
                feature.isMember(jss::threshold), feature[jss::name].asString() + " threshold");
            BEAST_EXPECTS(
                feature.isMember(jss::validations), feature[jss::name].asString() + " validations");
            BEAST_EXPECT(feature[jss::count] == ((expectVeto || expectObsolete) ? 0 : 1));
            BEAST_EXPECT(feature[jss::threshold] == 1);
            BEAST_EXPECT(feature[jss::validations] == 1);
            BEAST_EXPECTS(
                expectVeto || expectObsolete || feature[jss::majority] == 2540,
                "Majority: " + feature[jss::majority].asString());
        }
    }

    void
    testVeto()
    {
        testcase("Veto");

        using namespace test::jtx;
        Env env{*this, FeatureBitset{featurePriceOracle}};
        constexpr char const* featureName = "fixAMMOverflowOffer";

        auto jrr = env.rpc("feature", featureName)[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        auto feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(feature[jss::vetoed].isBool() && !feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", featureName, "reject")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(feature[jss::vetoed].isBool() && feature[jss::vetoed].asBool(), "vetoed");

        jrr = env.rpc("feature", featureName, "accept")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(feature[jss::vetoed].isBool() && !feature[jss::vetoed].asBool(), "vetoed");

        // anything other than accept or reject is an error
        jrr = env.rpc("feature", featureName, "maybe");
        BEAST_EXPECT(jrr[jss::error] == "invalidParams");
        BEAST_EXPECT(jrr[jss::error_message] == "Invalid parameters.");
    }

    void
    testObsolete()
    {
        testcase("Obsolete");

        using namespace test::jtx;
        Env env{*this};

        auto const& supportedAmendments = xrpl::detail::supportedAmendments();
        auto obsoleteFeature = std::ranges::find_if(supportedAmendments, [](auto const& pair) {
            return pair.second == VoteBehavior::Obsolete;
        });

        if (obsoleteFeature == std::end(supportedAmendments))
        {
            pass();
            return;
        }

        auto const featureName = obsoleteFeature->first;

        auto jrr = env.rpc("feature", featureName)[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        auto feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(
            feature[jss::vetoed].isString() && feature[jss::vetoed].asString() == "Obsolete",
            "vetoed");

        jrr = env.rpc("feature", featureName, "reject")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(
            feature[jss::vetoed].isString() && feature[jss::vetoed].asString() == "Obsolete",
            "vetoed");

        jrr = env.rpc("feature", featureName, "accept")[jss::result];
        if (!BEAST_EXPECTS(jrr[jss::status] == jss::success, "status"))
            return;
        jrr.removeMember(jss::status);
        if (!BEAST_EXPECT(jrr.size() == 1))
            return;
        feature = *(jrr.begin());
        BEAST_EXPECTS(feature[jss::name] == featureName, "name");
        BEAST_EXPECTS(
            feature[jss::vetoed].isString() && feature[jss::vetoed].asString() == "Obsolete",
            "vetoed");

        // anything other than accept or reject is an error
        jrr = env.rpc("feature", featureName, "maybe");
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
        testObsolete();
    }
};

BEAST_DEFINE_TESTSUITE(Feature, rpc, xrpl);

}  // namespace xrpl
