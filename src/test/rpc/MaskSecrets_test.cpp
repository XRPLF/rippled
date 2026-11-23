#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/MaskSecrets.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace xrpl::test {

/**
 * The masker against the replies of a running server.
 *
 * The masker itself is covered in src/tests/libxrpl/json/MaskSecrets.cpp. This
 * suite needs a server to answer the key-generating commands.
 */
class MaskSecrets_test : public beast::unit_test::Suite
{
public:
    /**
     * A keygen reply is rendered for the log with no key in it.
     *
     * Every field of each reply is checked, not only the names on the list, so
     * a field the list is missing fails here. Only the fields a client may read
     * in public are allowed through.
     */
    void
    testKeygenRepliesAreMasked()
    {
        testcase("A keygen reply is logged with every key masked");

        static constexpr std::array<std::string_view, 6> kPublicFields{
            "account_id",
            "key_type",
            "public_key",
            "public_key_hex",
            "status",
            "validation_public_key",
        };

        jtx::Env env{*this};
        for (auto const command : {"wallet_propose", "validation_create"})
        {
            auto const reply = env.rpc(command);
            auto const& result = reply[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");

            auto const rendered = json::loggable(reply);
            for (auto const& name : result.getMemberNames())
            {
                if (std::ranges::find(kPublicFields, name) != kPublicFields.end())
                    continue;
                auto const& value = result[name];
                BEAST_EXPECT(value.isString() && !value.asString().empty());
                BEAST_EXPECTS(!rendered.contains(value.asString()), command + (": " + name));
            }
        }
    }

    void
    run() override
    {
        testKeygenRepliesAreMasked();
    }
};

BEAST_DEFINE_TESTSUITE(MaskSecrets, rpc, xrpl);

}  // namespace xrpl::test
