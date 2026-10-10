#include <test/jtx/Env.h>

#include <xrpld/rpc/detail/MaskSecrets.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace xrpl::test {

class MaskSecrets_test : public beast::unit_test::Suite
{
    static constexpr char const* kMasked = "<masked>";
    static constexpr char const* kSensitive = "sensitive";

public:
    /**
     * Every field in `kCredentialFields` is masked, at the top level.
     */
    void
    testEveryCredentialField()
    {
        testcase("Every credential field is masked");

        json::Value request(json::ValueType::Object);
        for (auto const field : rpc::kCredentialFields)
            request[std::string{field}] = kSensitive;

        auto const masked = rpc::maskSecrets(request);
        for (auto const field : rpc::kCredentialFields)
            BEAST_EXPECTS(masked[std::string{field}] == kMasked, std::string{field});

        // An empty list would let every assertion above pass while masking nothing.
        BEAST_EXPECT(!rpc::kCredentialFields.empty());
    }

    /**
     * A field the list does not name is left as it was.
     */
    void
    testNonCredentialsSurvive()
    {
        testcase("Fields that are not credentials are left alone");

        json::Value request(json::ValueType::Object);
        request[jss::method] = "sign";
        request[jss::account] = "rSomeAccount";
        request[jss::secret] = kSensitive;

        auto const masked = rpc::maskSecrets(request);
        BEAST_EXPECT(masked[jss::secret] == kMasked);
        BEAST_EXPECT(masked[jss::method] == "sign");
        BEAST_EXPECT(masked[jss::account] == "rSomeAccount");

        // Masking replaces members rather than adding or removing them.
        BEAST_EXPECT(masked.size() == request.size());
    }

    /**
     * A member name is matched whole, not as a substring or case-insensitively.
     */
    void
    testNamesAreMatchedExactly()
    {
        testcase("Only an exact field name is masked");

        json::Value request(json::ValueType::Object);
        for (auto const* name : {"secrets", "Secret", "my_secret", "seedling", "sec", ""})
            request[name] = kSensitive;

        auto const masked = rpc::maskSecrets(request);
        for (auto const* name : {"secrets", "Secret", "my_secret", "seedling", "sec", ""})
            BEAST_EXPECTS(masked[name] == kSensitive, name);
    }

    /**
     * A credential is masked wherever it sits, not only at the top level.
     *
     * The JSON-RPC transport nests a request's fields inside `params`, which is
     * where a `secret` arrives.
     */
    void
    testNesting()
    {
        testcase("A nested credential is masked at any depth");

        // Where the JSON-RPC transport carries a credential: inside `params`.
        {
            json::Value params(json::ValueType::Object);
            params[jss::secret] = kSensitive;
            params[jss::account] = "rSomeAccount";

            json::Value request(json::ValueType::Object);
            request[jss::method] = "sign";
            request[jss::params] = json::ValueType::Array;
            request[jss::params][0u] = params;

            auto const masked = rpc::maskSecrets(request);
            BEAST_EXPECT(masked[jss::params][0u][jss::secret] == kMasked);
            BEAST_EXPECT(masked[jss::params][0u][jss::account] == "rSomeAccount");
            BEAST_EXPECT(masked[jss::method] == "sign");
        }

        // Where a batch carries one: inside an entry, itself inside `params`.
        {
            json::Value entry(json::ValueType::Object);
            entry[jss::id] = 2;
            entry[jss::seed] = kSensitive;

            json::Value batch(json::ValueType::Object);
            batch[jss::method] = "batch";
            batch[jss::params] = json::ValueType::Array;
            batch[jss::params][0u] = entry;

            auto const masked = rpc::maskSecrets(batch);
            BEAST_EXPECT(masked[jss::params][0u][jss::seed] == kMasked);
            BEAST_EXPECT(masked[jss::params][0u][jss::id] == 2);
        }

        // A credential in an array element, which names no member of its own.
        {
            json::Value inner(json::ValueType::Object);
            inner[jss::passphrase] = kSensitive;

            json::Value request(json::ValueType::Array);
            request[0u] = inner;

            auto const masked = rpc::maskSecrets(request);
            BEAST_EXPECT(masked[0u][jss::passphrase] == kMasked);
        }

        // Up to the parser's nesting limit, which is what bounds the recursion.
        {
            json::Value deep(json::ValueType::Object);
            deep[jss::secret] = kSensitive;
            for (unsigned i{0}; i < json::Reader::kNestLimit; ++i)
            {
                json::Value outer(json::ValueType::Object);
                outer[jss::params] = deep;
                deep = outer;
            }

            auto const masked = rpc::maskSecrets(deep);
            auto const* level = &masked;
            for (unsigned i{0}; i < json::Reader::kNestLimit; ++i)
                level = &(*level)[jss::params];
            BEAST_EXPECT((*level)[jss::secret] == kMasked);
        }
    }

    /**
     * A credential's own value is replaced whole, whatever shape it has.
     *
     * An object or array under `secret` is not walked into: the whole value is
     * the credential.
     */
    void
    testCredentialIsNotDescendedInto()
    {
        testcase("A credential is replaced whatever it holds");

        json::Value nested(json::ValueType::Object);
        nested["inner"] = kSensitive;

        json::Value request(json::ValueType::Object);
        request[jss::secret] = nested;
        request[jss::seed] = json::ValueType::Array;
        request[jss::seed][0u] = kSensitive;

        auto const masked = rpc::maskSecrets(request);
        BEAST_EXPECT(masked[jss::secret] == kMasked);
        BEAST_EXPECT(masked[jss::seed] == kMasked);

        BEAST_EXPECT(!to_string(masked).contains(kSensitive));
    }

    /**
     * A credential that is not a string is masked too, the value being replaced
     * whole.
     */
    void
    testNonStringCredentials()
    {
        testcase("A credential of any type is masked");

        json::Value request(json::ValueType::Object);
        request["secret"] = 12345;
        request["seed"] = true;
        request["passphrase"] = 1.5;
        request["password"] = json::ValueType::Null;

        auto const masked = rpc::maskSecrets(request);
        for (auto const* field : {"secret", "seed", "passphrase", "password"})
            BEAST_EXPECTS(masked[field] == kMasked, field);
    }

    /**
     * A value with no members is returned unchanged, masking keying on member
     * names.
     */
    void
    testValuesWithoutMembers()
    {
        testcase("A value that has no members is returned unchanged");

        BEAST_EXPECT(rpc::maskSecrets(json::Value{}).isNull());
        BEAST_EXPECT(rpc::maskSecrets(json::Value{kSensitive}) == kSensitive);
        BEAST_EXPECT(rpc::maskSecrets(json::Value{42}) == 42);
        BEAST_EXPECT(rpc::maskSecrets(json::Value{json::ValueType::Object}).size() == 0);
        BEAST_EXPECT(rpc::maskSecrets(json::Value{json::ValueType::Array}).size() == 0);
    }

    /**
     * The request the caller passed in is not modified.
     *
     * Every call site goes on to use the request it holds, so the mask copies.
     */
    void
    testRequestIsNotModified()
    {
        testcase("The request passed in is left unchanged");

        json::Value request(json::ValueType::Object);
        request[jss::secret] = kSensitive;
        request[jss::params] = json::ValueType::Array;
        request[jss::params][0u] = json::ValueType::Object;
        request[jss::params][0u][jss::seed] = kSensitive;

        auto const before = to_string(request);
        auto const masked = rpc::maskSecrets(request);

        BEAST_EXPECT(to_string(request) == before);
        BEAST_EXPECT(request[jss::secret] == kSensitive);
        BEAST_EXPECT(request[jss::params][0u][jss::seed] == kSensitive);
        BEAST_EXPECT(masked[jss::secret] == kMasked);
    }

    /**
     * `hasSecret` reports a credential at any depth and nothing else.
     *
     * Every name on the list is reported at the top level, one nested where the
     * JSON-RPC transport puts it and one inside an array element are reported
     * too, and a value naming none, or having no members at all, is not.
     */
    void
    testHasSecret()
    {
        testcase("A credential is detected at any depth");

        for (auto const field : rpc::kCredentialFields)
        {
            json::Value request(json::ValueType::Object);
            request[std::string{field}] = kSensitive;
            BEAST_EXPECTS(rpc::hasSecret(request), std::string{field});
        }

        // Inside `params`, where the JSON-RPC transport carries a credential.
        {
            json::Value request(json::ValueType::Object);
            request[jss::method] = "sign";
            request[jss::params] = json::ValueType::Array;
            request[jss::params][0u] = json::ValueType::Object;
            request[jss::params][0u][jss::account] = "rSomeAccount";
            request[jss::params][0u][jss::secret] = kSensitive;
            BEAST_EXPECT(rpc::hasSecret(request));
        }

        // Inside an array element, which names no member of its own.
        {
            json::Value request(json::ValueType::Array);
            request[0u] = json::ValueType::Object;
            request[0u][jss::passphrase] = kSensitive;
            BEAST_EXPECT(rpc::hasSecret(request));
        }

        // No credential at any depth, and a near miss is not one.
        {
            json::Value request(json::ValueType::Object);
            request[jss::method] = "account_info";
            request[jss::params] = json::ValueType::Array;
            request[jss::params][0u] = json::ValueType::Object;
            request[jss::params][0u][jss::account] = "rSomeAccount";
            request[jss::params][0u]["secrets"] = kSensitive;
            BEAST_EXPECT(!rpc::hasSecret(request));
        }

        // A value with no members carries none.
        BEAST_EXPECT(!rpc::hasSecret(json::Value{}));
        BEAST_EXPECT(!rpc::hasSecret(json::Value{kSensitive}));
        BEAST_EXPECT(!rpc::hasSecret(json::Value{42}));
        BEAST_EXPECT(!rpc::hasSecret(json::Value{json::ValueType::Object}));
        BEAST_EXPECT(!rpc::hasSecret(json::Value{json::ValueType::Array}));
    }

    /**
     * `loggable` masks a value that carries a credential, then truncates at
     * `kMaxLoggedChars`; one that carries none is rendered as it is.
     *
     * The mask has to survive the cap, so the credential is placed where a
     * sorted member order puts it first.
     */
    void
    testLoggable()
    {
        testcase("A rendering for a log line is masked and capped");

        // Sorts before the filler, so the mask falls inside the cap.
        json::Value request;
        request[jss::secret] = kSensitive;
        request["zfiller"] = std::string(2 * rpc::kMaxLoggedChars, 'z');

        auto const rendered = rpc::loggable(request);

        BEAST_EXPECT(rendered.size() == rpc::kMaxLoggedChars);
        BEAST_EXPECT(rendered.contains(kMasked));
        BEAST_EXPECT(!rendered.contains(kSensitive));

        // A short request is not padded.
        json::Value small;
        small[jss::seed] = kSensitive;
        BEAST_EXPECT(rpc::loggable(small) == to_string(rpc::maskSecrets(small)));

        // A value with no credential is the serialized value itself, cut at the cap.
        json::Value plain;
        plain[jss::account] = "rSomeAccount";
        plain["zfiller"] = std::string(2 * rpc::kMaxLoggedChars, 'z');
        BEAST_EXPECT(rpc::loggable(plain) == to_string(plain).substr(0, rpc::kMaxLoggedChars));
        BEAST_EXPECT(rpc::loggable(small[jss::seed]) == to_string(small[jss::seed]));
    }

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

            auto const rendered = rpc::loggable(reply);
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
        testEveryCredentialField();
        testNonCredentialsSurvive();
        testNamesAreMatchedExactly();
        testNesting();
        testCredentialIsNotDescendedInto();
        testNonStringCredentials();
        testValuesWithoutMembers();
        testRequestIsNotModified();
        testHasSecret();
        testLoggable();
        testKeygenRepliesAreMasked();
    }
};

BEAST_DEFINE_TESTSUITE(MaskSecrets, rpc, xrpl);

}  // namespace xrpl::test
