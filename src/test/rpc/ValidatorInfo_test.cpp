// Copyright (c) 2020 Dev Null Productions

#include <test/jtx.h>

#include <xrpld/core/ConfigSections.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

#include <string>
#include <vector>

namespace ripple {
namespace test {

class ValidatorInfo_test : public beast::unit_test::suite
{
public:
    void
    testErrors()
    {
        testcase("Errors");

        using namespace jtx;
        {
            Env env(*this);
            auto const info = env.rpc("validator_info");
            BEAST_EXPECT(
                info[jss::result][jss::error_message] == "not a validator");
        }
    }

    void
    testPrivileges()
    {
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};
        env.set_retries(0);
        auto const info = env.rpc("validator_info")[jss::result];
        BEAST_EXPECT(info.isNull());
    }

    void
    testLookup()
    {
        testcase("Lookup");

        using namespace jtx;
        std::vector<std::string> const tokenBlob = {
            "    "
            "eyJ2YWxpZGF0aW9uX3NlY3JldF9rZXkiOiI5ZWQ0NWY4NjYyNDFjYzE4YTI3NDdiNT"
            "\n",
            " \tQzODdjMDYyNTkwNzk3MmY0ZTcxOTAyMzFmYWE5Mzc0NTdmYTlkYWY2IiwibWFua"
            "WZl "
            "    \n",
            "\tc3QiOiJKQUFBQUFGeEllMUZ0d21pbXZHdEgyaUNjTUpxQzlnVkZLaWxHZncxL3ZD"
            "eE"
            "\n",
            "\t "
            "hYWExwbGMyR25NaEFrRTFhZ3FYeEJ3RHdEYklENk9NU1l1TTBGREFscEFnTms4U0tG"
            "\t  "
            "\t\n",
            "bjdNTzJmZGtjd1JRSWhBT25ndTlzQUtxWFlvdUorbDJWMFcrc0FPa1ZCK1pSUzZQU2"
            "\n",
            "hsSkFmVXNYZkFpQnNWSkdlc2FhZE9KYy9hQVpva1MxdnltR21WcmxIUEtXWDNZeXd1"
            "\n",
            "NmluOEhBU1FLUHVnQkQ2N2tNYVJGR3ZtcEFUSGxHS0pkdkRGbFdQWXk1QXFEZWRGdj"
            "\n",
            "VUSmEydzBpMjFlcTNNWXl3TFZKWm5GT3I3QzBrdzJBaVR6U0NqSXpkaXRROD0ifQ=="
            "\n"};

        std::string const master_key =
            "nHBt9fsb4849WmZiCds4r5TXyBeQjqnH5kzPtqgMAQMgi39YZRPa";
        std::string const ephemeral_key =
            "n9KsDYGKhABVc4wK5u3MnVhgPinyJimyKGpr9VJYuBaY8EnJXR2x";
        std::string const manifest =
            "JAAAAAFxIe1FtwmimvGtH2iCcMJqC9gVFKilGfw1/"
            "vCxHXXLplc2GnMhAkE1agqXxBwDwDbID6OMSYuM0FDAlpAgNk8SKFn7MO2fdkcwRQI"
            "hAOngu9sAKqXYouJ+l2V0W+sAOkVB+ZRS6PShlJAfUsXfAiBsVJGesaadOJc/"
            "aAZokS1vymGmVrlHPKWX3Yywu6in8HASQKPugBD67kMaRFGvmpATHlGKJdvDFlWPYy"
            "5AqDedFv5TJa2w0i21eq3MYywLVJZnFOr7C0kw2AiTzSCjIzditQ8=";

        Env env{*this, envconfig([&tokenBlob](std::unique_ptr<Config> cfg) {
                    cfg->section(SECTION_VALIDATOR_TOKEN).append(tokenBlob);
                    return cfg;
                })};
        {
            auto const info = env.rpc("validator_info");
            BEAST_EXPECT(info[jss::result][jss::status] == "success");
            BEAST_EXPECT(info[jss::result][jss::seq] == 1);
            BEAST_EXPECT(info[jss::result][jss::master_key] == master_key);
            BEAST_EXPECT(info[jss::result][jss::manifest] == manifest);
            BEAST_EXPECT(
                info[jss::result][jss::ephemeral_key] == ephemeral_key);
        }
    }

    void
    run() override
    {
        testErrors();
        testPrivileges();
        testLookup();
    }
};

BEAST_DEFINE_TESTSUITE(ValidatorInfo, rpc, ripple);
}  // namespace test
}  // namespace ripple
