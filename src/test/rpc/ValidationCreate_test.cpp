#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <array>

namespace xrpl {

class ValidationCreate_test : public beast::unit_test::Suite
{
    // The base58 seed the passphrase "masterpassphrase" hashes to.
    static constexpr char const* kMasterSeed = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb";

    // The values a secret must not be. Arrays and objects make
    // json::Value::asString() throw; the scalars are silently stringified by
    // it, so without a type check they reach parseGenericSeed as "42",
    // "1.500000", "true" and "".
    static std::array<json::Value, 6>
    badSecrets()
    {
        json::Value objValue{json::ValueType::Object};
        objValue["nope"] = 0;
        json::Value arrValue{json::ValueType::Array};
        arrValue.append("masterpassphrase");

        return {{
            json::Value{42},    // int
            json::Value{1.5},   // real
            json::Value{true},  // bool
            json::Value{},      // null
            objValue,
            arrValue,
        }};
    }

    void
    testNoSecret()
    {
        testcase("No secret");
        using namespace test::jtx;
        Env env{*this};

        auto const first = env.client().invoke("validation_create")[jss::result];
        BEAST_EXPECT(!first.isMember(jss::error));
        BEAST_EXPECT(first[jss::status] == "success");
        BEAST_EXPECT(first.isMember(jss::validation_public_key));
        BEAST_EXPECT(first.isMember(jss::validation_private_key));
        BEAST_EXPECT(first.isMember(jss::validation_seed));
        BEAST_EXPECT(first.isMember(jss::validation_key));

        // Omitting the secret picks a random seed, so a second call must not
        // return the same key.
        auto const second = env.client().invoke("validation_create")[jss::result];
        BEAST_EXPECT(second[jss::status] == "success");
        BEAST_EXPECT(first[jss::validation_seed] != second[jss::validation_seed]);
    }

    void
    testValidSecret()
    {
        testcase("Valid secret");
        using namespace test::jtx;
        Env env{*this};

        // The three spellings of one seed that parseGenericSeed resolves to the
        // same value: a passphrase, its hex, and its base58 encoding.
        std::array<char const*, 3> const masterForms{
            "masterpassphrase",
            "DEDCE9CE67B451D852FD4E846FCDE31C",
            kMasterSeed,
        };

        for (auto const* secret : masterForms)
        {
            json::Value params{json::ValueType::Object};
            params[jss::secret] = secret;
            auto const result = env.client().invoke("validation_create", params)[jss::result];
            BEAST_EXPECTS(!result.isMember(jss::error), secret);
            BEAST_EXPECTS(result[jss::status] == "success", secret);
            BEAST_EXPECTS(result[jss::validation_seed] == kMasterSeed, secret);
        }

        // An RFC-1751 key is accepted too, and a secret is deterministic: the
        // same one twice derives the same validator key.
        json::Value params{json::ValueType::Object};
        params[jss::secret] = "BAWL MAN JADE MOON DOVE GEM SON NOW HAD ADEN GLOW TIRE";
        auto const first = env.client().invoke("validation_create", params)[jss::result];
        BEAST_EXPECT(!first.isMember(jss::error));
        BEAST_EXPECT(first[jss::status] == "success");

        auto const second = env.client().invoke("validation_create", params)[jss::result];
        BEAST_EXPECT(first[jss::validation_seed] == second[jss::validation_seed]);
        BEAST_EXPECT(first[jss::validation_public_key] == second[jss::validation_public_key]);
    }

    void
    testBadSecret()
    {
        testcase("Invalid secret");
        using namespace test::jtx;
        Env env{*this};

        // Before the type check the objects and arrays here returned a generic
        // internal error, and the scalars derived a real validator key from the
        // value's printed form.
        for (auto const& badSecret : badSecrets())
        {
            json::Value params{json::ValueType::Object};
            params[jss::secret] = badSecret;
            auto const result = env.client().invoke("validation_create", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
            BEAST_EXPECT(!result.isMember(jss::validation_seed));
            BEAST_EXPECT(!result.isMember(jss::validation_private_key));
        }
    }

    void
    testUnparseableSecret()
    {
        testcase("Unparseable secret");
        using namespace test::jtx;
        Env env{*this};

        // A string that is not a seed is still badSeed rather than
        // invalidParams: parseGenericSeed rejects the empty string, and any
        // token that decodes as an account address or a public or secret key.
        std::array<char const*, 3> const notSeeds{
            "",
            "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",                    // an AccountID
            "n9MXXueo837zYH36DvMc13BwHcqtfAWNJY5czWVbp7uYTj7x17TH",  // a node public key
        };

        for (auto const* secret : notSeeds)
        {
            json::Value params{json::ValueType::Object};
            params[jss::secret] = secret;
            auto const result = env.client().invoke("validation_create", params)[jss::result];
            BEAST_EXPECTS(result[jss::error] == "badSeed", secret);
            BEAST_EXPECTS(result[jss::status] == "error", secret);
        }
    }

public:
    void
    run() override
    {
        testNoSecret();
        testValidSecret();
        testBadSecret();
        testUnparseableSecret();
    }
};

BEAST_DEFINE_TESTSUITE(ValidationCreate, rpc, xrpl);

}  // namespace xrpl
