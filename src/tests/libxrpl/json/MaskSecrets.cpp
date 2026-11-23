#include <xrpl/json/MaskSecrets.h>

#include <xrpl/basics/CredentialFields.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace xrpl {

namespace {

constexpr char const* kMasked = "<masked>";
constexpr char const* kSensitive = "sensitive";

// kCredentialFields cannot name jss entries, since its module sits below jss.h.
constexpr std::array<std::string_view, kCredentialFields.size()> kJssCredentialFields{
    jss::admin_password.cStr(),
    jss::admin_user.cStr(),
    jss::master_key.cStr(),
    jss::master_seed.cStr(),
    jss::master_seed_hex.cStr(),
    jss::passphrase.cStr(),
    jss::password.cStr(),
    jss::secret.cStr(),
    jss::seed.cStr(),
    jss::seed_hex.cStr(),
    jss::url_password.cStr(),
    jss::url_username.cStr(),
    jss::username.cStr(),
    jss::validation_key.cStr(),
    jss::validation_private_key.cStr(),
    jss::validation_seed.cStr(),
};
static_assert(
    std::ranges::equal(kCredentialFields, kJssCredentialFields),
    "kCredentialFields must name the same fields as their jss entries, in the same order");

}  // namespace

TEST(MaskSecrets, every_credential_field_is_masked)
{
    json::Value request(json::ValueType::Object);
    for (auto const field : kCredentialFields)
        request[std::string{field}] = kSensitive;

    auto const masked = json::maskSecrets(request);
    for (auto const field : kCredentialFields)
        EXPECT_EQ(masked[std::string{field}], kMasked) << field;

    // An empty list would let every assertion above pass while masking nothing.
    EXPECT_FALSE(kCredentialFields.empty());
}

TEST(MaskSecrets, other_fields_are_left_alone)
{
    json::Value request(json::ValueType::Object);
    request[jss::method] = "sign";
    request[jss::account] = "rSomeAccount";
    request[jss::secret] = kSensitive;

    auto const masked = json::maskSecrets(request);
    EXPECT_EQ(masked[jss::secret], kMasked);
    EXPECT_EQ(masked[jss::method], "sign");
    EXPECT_EQ(masked[jss::account], "rSomeAccount");

    // Masking replaces members rather than adding or removing them.
    EXPECT_EQ(masked.size(), request.size());
}

TEST(MaskSecrets, names_are_matched_exactly)
{
    static constexpr std::array kNames{"secrets", "Secret", "my_secret", "seedling", "sec", ""};

    json::Value request(json::ValueType::Object);
    for (auto const* name : kNames)
        request[name] = kSensitive;

    EXPECT_FALSE(json::hasSecret(request));
    auto const masked = json::maskSecrets(request);
    for (auto const* name : kNames)
        EXPECT_EQ(masked[name], kSensitive) << name;
}

TEST(MaskSecrets, nested_credential_is_masked_at_any_depth)
{
    // Where the JSON-RPC transport carries a credential: inside `params`.
    json::Value params(json::ValueType::Object);
    params[jss::secret] = kSensitive;
    params[jss::account] = "rSomeAccount";

    json::Value request(json::ValueType::Object);
    request[jss::method] = "sign";
    request[jss::params] = json::ValueType::Array;
    request[jss::params][0u] = params;

    EXPECT_TRUE(json::hasSecret(request));
    auto masked = json::maskSecrets(request);
    EXPECT_EQ(masked[jss::params][0u][jss::secret], kMasked);
    EXPECT_EQ(masked[jss::params][0u][jss::account], "rSomeAccount");
    EXPECT_EQ(masked[jss::method], "sign");

    // A credential many levels down, under objects and arrays in turn.
    json::Value deep(json::ValueType::Object);
    deep[jss::seed] = kSensitive;
    for (int level = 0; level < 10; ++level)
    {
        json::Value outer(json::ValueType::Object);
        outer[jss::params] = json::ValueType::Array;
        outer[jss::params][0u] = deep;
        deep = outer;
    }

    EXPECT_TRUE(json::hasSecret(deep));
    masked = json::maskSecrets(deep);
    EXPECT_FALSE(to_string(masked).contains(kSensitive));
    EXPECT_TRUE(to_string(masked).contains(kMasked));
}

TEST(MaskSecrets, credential_holding_a_container_is_replaced_whole)
{
    json::Value request(json::ValueType::Object);
    request[jss::secret] = json::ValueType::Object;
    request[jss::secret][jss::account] = kSensitive;

    auto const masked = json::maskSecrets(request);
    EXPECT_EQ(masked[jss::secret], kMasked);
}

TEST(MaskSecrets, credential_of_any_type_is_masked)
{
    json::Value request(json::ValueType::Object);
    request[jss::secret] = 12345;
    request[jss::seed] = true;
    request[jss::passphrase] = 1.5;
    request[jss::password] = json::ValueType::Null;

    auto const masked = json::maskSecrets(request);
    for (auto const& field : {jss::secret, jss::seed, jss::passphrase, jss::password})
        EXPECT_EQ(masked[field], kMasked) << field.cStr();
}

TEST(MaskSecrets, masking_leaves_the_argument_alone)
{
    json::Value request(json::ValueType::Object);
    request[jss::secret] = kSensitive;
    request[jss::params] = json::ValueType::Array;
    request[jss::params][0u] = json::ValueType::Object;
    request[jss::params][0u][jss::seed] = kSensitive;

    auto const before = to_string(request);
    auto const masked = json::maskSecrets(request);

    EXPECT_EQ(to_string(request), before);
    EXPECT_EQ(request[jss::secret], kSensitive);
    EXPECT_EQ(request[jss::params][0u][jss::seed], kSensitive);
    EXPECT_EQ(masked[jss::secret], kMasked);
}

TEST(MaskSecrets, credential_is_detected_at_any_depth)
{
    for (auto const field : kCredentialFields)
    {
        json::Value request(json::ValueType::Object);
        request[std::string{field}] = kSensitive;
        EXPECT_TRUE(json::hasSecret(request)) << field;
    }

    // Inside `params`, where the JSON-RPC transport carries a credential.
    {
        json::Value request(json::ValueType::Object);
        request[jss::method] = "sign";
        request[jss::params] = json::ValueType::Array;
        request[jss::params][0u] = json::ValueType::Object;
        request[jss::params][0u][jss::account] = "rSomeAccount";
        request[jss::params][0u][jss::secret] = kSensitive;
        EXPECT_TRUE(json::hasSecret(request));
    }

    // Inside an array element, which names no member of its own.
    {
        json::Value request(json::ValueType::Array);
        request[0u] = json::ValueType::Object;
        request[0u][jss::passphrase] = kSensitive;
        EXPECT_TRUE(json::hasSecret(request));
    }

    // No credential at any depth, and a near miss is not one.
    {
        json::Value request(json::ValueType::Object);
        request[jss::method] = "account_info";
        request[jss::params] = json::ValueType::Array;
        request[jss::params][0u] = json::ValueType::Object;
        request[jss::params][0u][jss::account] = "rSomeAccount";
        request[jss::params][0u]["secrets"] = kSensitive;
        EXPECT_FALSE(json::hasSecret(request));
    }
}

TEST(MaskSecrets, value_without_members_has_no_secret)
{
    EXPECT_FALSE(json::hasSecret(json::Value{}));
    EXPECT_FALSE(json::hasSecret(json::Value{"secret"}));
    EXPECT_FALSE(json::hasSecret(json::Value{json::ValueType::Object}));
    EXPECT_FALSE(json::hasSecret(json::Value{json::ValueType::Array}));

    // A scalar is returned as it is.
    EXPECT_EQ(json::maskSecrets(json::Value{"secret"}), "secret");
}

TEST(MaskSecrets, loggable_masks_and_leaves_the_argument_alone)
{
    json::Value request(json::ValueType::Object);
    request[jss::method] = "sign";
    request[jss::secret] = kSensitive;

    auto const text = json::loggable(request);
    EXPECT_FALSE(text.contains(kSensitive));
    EXPECT_TRUE(text.contains(kMasked));
    EXPECT_TRUE(text.contains("sign"));
    EXPECT_EQ(request[jss::secret], kSensitive);
}

TEST(MaskSecrets, loggable_passes_a_value_without_credential_through)
{
    json::Value request(json::ValueType::Object);
    request[jss::method] = "ledger";
    request[jss::ledger_index] = 5;

    EXPECT_EQ(json::loggable(request), to_string(request));
}

TEST(MaskSecrets, loggable_truncates_to_the_cap)
{
    json::Value request(json::ValueType::Object);
    request[jss::method] = std::string(json::kMaxLoggedChars, 'x');

    EXPECT_GT(to_string(request).size(), json::kMaxLoggedChars);
    EXPECT_EQ(json::loggable(request).size(), json::kMaxLoggedChars);

    // A value at the cap is not cut.
    json::Value const exact{std::string(json::kMaxLoggedChars - 2, 'x')};
    EXPECT_EQ(to_string(exact).size(), json::kMaxLoggedChars);
    EXPECT_EQ(json::loggable(exact), to_string(exact));
}

}  // namespace xrpl
