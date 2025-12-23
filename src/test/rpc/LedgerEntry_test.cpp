#include <test/jtx.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/attester.h>
#include <test/jtx/delegate.h>
#include <test/jtx/multisign.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/jss.h>

#if (defined(__clang_major__) && __clang_major__ < 15)
#include <experimental/source_location>
using source_location = std::experimental::source_location;
#else
#include <source_location>
using std::source_location;
#endif
namespace ripple {

namespace test {

enum class FieldType {
    AccountField,
    BlobField,
    ArrayField,
    CurrencyField,
    HashField,
    HashOrObjectField,
    ObjectField,
    StringField,
    TwoAccountArrayField,
    UInt32Field,
    UInt64Field,
};

std::vector<std::pair<Json::StaticString, FieldType>> mappings{
    {jss::account, FieldType::AccountField},
    {jss::accounts, FieldType::TwoAccountArrayField},
    {jss::authorize, FieldType::AccountField},
    {jss::authorized, FieldType::AccountField},
    {jss::credential_type, FieldType::BlobField},
    {jss::currency, FieldType::CurrencyField},
    {jss::issuer, FieldType::AccountField},
    {jss::oracle_document_id, FieldType::UInt32Field},
    {jss::owner, FieldType::AccountField},
    {jss::seq, FieldType::UInt32Field},
    {jss::subject, FieldType::AccountField},
    {jss::ticket_seq, FieldType::UInt32Field},
};

FieldType
getFieldType(Json::StaticString fieldName)
{
    auto it = std::ranges::find_if(mappings, [&fieldName](auto const& pair) {
        return pair.first == fieldName;
    });
    if (it != mappings.end())
    {
        return it->second;
    }
    else
    {
        Throw<std::runtime_error>(
            "`mappings` is missing field " + std::string(fieldName.c_str()));
    }
}

std::string
getTypeName(FieldType typeID)
{
    switch (typeID)
    {
        case FieldType::UInt32Field:
            return "number";
        case FieldType::UInt64Field:
            return "number";
        case FieldType::HashField:
            return "hex string";
        case FieldType::AccountField:
            return "AccountID";
        case FieldType::BlobField:
            return "hex string";
        case FieldType::CurrencyField:
            return "Currency";
        case FieldType::ArrayField:
            return "array";
        case FieldType::HashOrObjectField:
            return "hex string or object";
        case FieldType::TwoAccountArrayField:
            return "length-2 array of Accounts";
        default:
            Throw<std::runtime_error>(
                "unknown type " + std::to_string(static_cast<uint8_t>(typeID)));
    }
}

class LedgerEntry_test : public beast::unit_test::suite
{
    void
    checkErrorValue(
        Json::Value const& jv,
        std::string const& err,
        std::string const& msg,
        source_location const location = source_location::current())
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECTS(
                jv[jss::status] == "error", std::to_string(location.line()));
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECTS(
                jv[jss::error] == err,
                "Expected error " + err + ", received " +
                    jv[jss::error].asString() + ", at line " +
                    std::to_string(location.line()) + ", " +
                    jv.toStyledString());
        if (msg.empty())
        {
            BEAST_EXPECTS(
                jv[jss::error_message] == Json::nullValue ||
                    jv[jss::error_message] == "",
                "Expected no error message, received \"" +
                    jv[jss::error_message].asString() + "\", at line " +
                    std::to_string(location.line()) + ", " +
                    jv.toStyledString());
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
            BEAST_EXPECTS(
                jv[jss::error_message] == msg,
                "Expected error message \"" + msg + "\", received \"" +
                    jv[jss::error_message].asString() + "\", at line " +
                    std::to_string(location.line()) + ", " +
                    jv.toStyledString());
    }

    std::vector<Json::Value>
    getBadValues(FieldType fieldType)
    {
        static Json::Value const injectObject = []() {
            Json::Value obj(Json::objectValue);
            obj[jss::account] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            obj[jss::ledger_index] = "validated";
            return obj;
        }();
        static Json::Value const injectArray = []() {
            Json::Value arr(Json::arrayValue);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "validated";
            return arr;
        }();
        static std::array<Json::Value, 21> const allBadValues = {
            "",                                                      // 0
            true,                                                    // 1
            1,                                                       // 2
            "1",                                                     // 3
            -1,                                                      // 4
            1.1,                                                     // 5
            "-1",                                                    // 6
            "abcdef",                                                // 7
            "ABCDEF",                                                // 8
            "12KK",                                                  // 9
            "0123456789ABCDEFGH",                                    // 10
            "rJxKV9e9p6wiPw!!!!xrJ4X1n98LosPL1sgcJW",                // 11
            "rPSTrR5yEr11uMkfsz1kHCp9jK4aoa3Avv",                    // 12
            "n9K2isxwTxcSHJKxMkJznDoWXAUs7NNy49H9Fknz1pC7oHAH3kH9",  // 13
            "USD",                                                   // 14
            "USDollars",                                             // 15
            "5233D68B4D44388F98559DE42903767803EFA7C1F8D01413FC16EE6B01403D"
            "6D",               // 16
            Json::arrayValue,   // 17
            Json::objectValue,  // 18
            injectObject,       // 19
            injectArray         // 20
        };

        auto remove =
            [&](std::vector<std::uint8_t> indices) -> std::vector<Json::Value> {
            std::unordered_set<std::uint8_t> indexSet(
                indices.begin(), indices.end());
            std::vector<Json::Value> values;
            values.reserve(allBadValues.size() - indexSet.size());
            for (std::size_t i = 0; i < allBadValues.size(); ++i)
            {
                if (indexSet.find(i) == indexSet.end())
                {
                    values.push_back(allBadValues[i]);
                }
            }
            return values;
        };

        static auto const& badUInt32Values = remove({2, 3});
        static auto const& badUInt64Values = remove({2, 3});
        static auto const& badHashValues = remove({2, 3, 7, 8, 16});
        static auto const& badAccountValues = remove({12});
        static auto const& badBlobValues = remove({3, 7, 8, 16});
        static auto const& badCurrencyValues = remove({14});
        static auto const& badArrayValues = remove({17, 20});
        static auto const& badIndexValues = remove({12, 16, 18, 19});

        switch (fieldType)
        {
            case FieldType::UInt32Field:
                return badUInt32Values;
            case FieldType::UInt64Field:
                return badUInt64Values;
            case FieldType::HashField:
                return badHashValues;
            case FieldType::AccountField:
                return badAccountValues;
            case FieldType::BlobField:
                return badBlobValues;
            case FieldType::CurrencyField:
                return badCurrencyValues;
            case FieldType::ArrayField:
            case FieldType::TwoAccountArrayField:
                return badArrayValues;
            case FieldType::HashOrObjectField:
                return badIndexValues;
            default:
                Throw<std::runtime_error>(
                    "unknown type " +
                    std::to_string(static_cast<uint8_t>(fieldType)));
        }
    }

    Json::Value
    getCorrectValue(Json::StaticString fieldName)
    {
        static Json::Value const twoAccountArray = []() {
            Json::Value arr(Json::arrayValue);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "r4MrUGTdB57duTnRs6KbsRGQXgkseGb1b5";
            return arr;
        }();

        auto const typeID = getFieldType(fieldName);
        switch (typeID)
        {
            case FieldType::UInt32Field:
                return 1;
            case FieldType::UInt64Field:
                return 1;
            case FieldType::HashField:
                return "5233D68B4D44388F98559DE42903767803EFA7C1F8D01413FC16EE6"
                       "B01403D6D";
            case FieldType::AccountField:
                return "r4MrUGTdB57duTnRs6KbsRGQXgkseGb1b5";
            case FieldType::BlobField:
                return "ABCDEF";
            case FieldType::CurrencyField:
                return "USD";
            case FieldType::ArrayField:
                return Json::arrayValue;
            case FieldType::HashOrObjectField:
                return "5233D68B4D44388F98559DE42903767803EFA7C1F8D01413FC16EE6"
                       "B01403D6D";
            case FieldType::TwoAccountArrayField:
                return twoAccountArray;
            default:
                Throw<std::runtime_error>(
                    "unknown type " +
                    std::to_string(static_cast<uint8_t>(typeID)));
        }
    }

    void
    testMalformedField(
        test::jtx::Env& env,
        Json::Value correctRequest,
        Json::StaticString const fieldName,
        FieldType const typeID,
        std::string const& expectedError,
        bool required = true,
        source_location const location = source_location::current())
    {
        forAllApiVersions([&, this](unsigned apiVersion) {
            if (required)
            {
                correctRequest.removeMember(fieldName);
                Json::Value const jrr = env.rpc(
                    apiVersion,
                    "json",
                    "ledger_entry",
                    to_string(correctRequest))[jss::result];
                if (apiVersion < 2u)
                    checkErrorValue(jrr, "unknownOption", "", location);
                else
                    checkErrorValue(
                        jrr,
                        "invalidParams",
                        "No ledger_entry params provided.",
                        location);
            }
            auto tryField = [&](Json::Value fieldValue) -> void {
                correctRequest[fieldName] = fieldValue;
                Json::Value const jrr = env.rpc(
                    apiVersion,
                    "json",
                    "ledger_entry",
                    to_string(correctRequest))[jss::result];
                auto const expectedErrMsg =
                    RPC::expected_field_message(fieldName, getTypeName(typeID));
                checkErrorValue(jrr, expectedError, expectedErrMsg, location);
            };

            auto const& badValues = getBadValues(typeID);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            if (required)
            {
                tryField(Json::nullValue);
            }
        });
    }

    void
    testMalformedSubfield(
        test::jtx::Env& env,
        Json::Value correctRequest,
        Json::StaticString parentFieldName,
        Json::StaticString fieldName,
        FieldType typeID,
        std::string const& expectedError,
        bool required = true,
        source_location const location = source_location::current())
    {
        forAllApiVersions([&, this](unsigned apiVersion) {
            if (required)
            {
                correctRequest[parentFieldName].removeMember(fieldName);
                Json::Value const jrr = env.rpc(
                    apiVersion,
                    "json",
                    "ledger_entry",
                    to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    RPC::missing_field_message(fieldName.c_str()),
                    location);

                correctRequest[parentFieldName][fieldName] = Json::nullValue;
                Json::Value const jrr2 = env.rpc(
                    apiVersion,
                    "json",
                    "ledger_entry",
                    to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr2,
                    "malformedRequest",
                    RPC::missing_field_message(fieldName.c_str()),
                    location);
            }
            auto tryField = [&](Json::Value fieldValue) -> void {
                correctRequest[parentFieldName][fieldName] = fieldValue;

                Json::Value const jrr = env.rpc(
                    apiVersion,
                    "json",
                    "ledger_entry",
                    to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr,
                    expectedError,
                    RPC::expected_field_message(fieldName, getTypeName(typeID)),
                    location);
            };

            auto const& badValues = getBadValues(typeID);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
        });
    }

    // No subfields
    void
    runLedgerEntryTest(
        test::jtx::Env& env,
        Json::StaticString const& parentField,
        source_location const location = source_location::current())
    {
        testMalformedField(
            env,
            Json::Value{},
            parentField,
            FieldType::HashField,
            "malformedRequest",
            true,
            location);
    }

    struct Subfield
    {
        Json::StaticString fieldName;
        std::string malformedErrorMsg;
        bool required = true;
    };

    void
    runLedgerEntryTest(
        test::jtx::Env& env,
        Json::StaticString const& parentField,
        std::vector<Subfield> const& subfields,
        source_location const location = source_location::current())
    {
        testMalformedField(
            env,
            Json::Value{},
            parentField,
            FieldType::HashOrObjectField,
            "malformedRequest",
            true,
            location);

        Json::Value correctOutput;
        correctOutput[parentField] = Json::objectValue;
        for (auto const& subfield : subfields)
        {
            correctOutput[parentField][subfield.fieldName] =
                getCorrectValue(subfield.fieldName);
        }

        for (auto const& subfield : subfields)
        {
            auto const fieldType = getFieldType(subfield.fieldName);
            testMalformedSubfield(
                env,
                correctOutput,
                parentField,
                subfield.fieldName,
                fieldType,
                subfield.malformedErrorMsg,
                subfield.required,
                location);
        }
    }

    void
    testLedgerEntryInvalid()
    {
        testcase("Invalid requests");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();
        {
            // Missing ledger_entry ledger_hash
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] =
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AA";
            auto const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }
        {
            // Missing ledger_entry ledger_hash
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            auto const typeId = FieldType::HashField;

            forAllApiVersions([&, this](unsigned apiVersion) {
                auto tryField = [&](Json::Value fieldValue) -> void {
                    jvParams[jss::ledger_hash] = fieldValue;
                    Json::Value const jrr = env.rpc(
                        apiVersion,
                        "json",
                        "ledger_entry",
                        to_string(jvParams))[jss::result];
                    auto const expectedErrMsg = fieldValue.isString()
                        ? "ledgerHashMalformed"
                        : "ledgerHashNotString";
                    checkErrorValue(jrr, "invalidParams", expectedErrMsg);
                };

                auto const& badValues = getBadValues(typeId);
                for (auto const& value : badValues)
                {
                    tryField(value);
                }
            });
        }

        {
            // ask for an zero index
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::index] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            auto const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }

        forAllApiVersions([&, this](unsigned apiVersion) {
            // "features" is not an option supported by ledger_entry.
            {
                Json::Value jvParams = Json::objectValue;
                jvParams[jss::features] =
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAA";
                jvParams[jss::api_version] = apiVersion;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                    checkErrorValue(jrr, "unknownOption", "");
                else
                    checkErrorValue(
                        jrr,
                        "invalidParams",
                        "No ledger_entry params provided.");
            }
        });
    }

    void
    testLedgerEntryAccountRoot()
    {
        testcase("AccountRoot");
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->FEES.reference_fee = 10;
        Env env{*this, std::move(cfg)};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 3);
        }

        std::string accountRootIndex;
        {
            // Request alice's account root.
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            constexpr char alicesAcctRootBinary[]{
                "1100612200800000240000000425000000032D00000000559CE54C3B934E4"
                "73A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F6240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D"};

            // Request alice's account root, but with binary == true;
            Json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::binary] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] == alicesAcctRootBinary);
        }
        {
            // Request alice's account root using the index.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request alice's account root by index, but with binary == false.
            Json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            jvParams[jss::binary] = 0;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Check alias
            Json::Value jvParams;
            jvParams[jss::account] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            // Check malformed cases
            Json::Value jvParams;
            testMalformedField(
                env,
                jvParams,
                jss::account_root,
                FieldType::AccountField,
                "malformedAddress");
        }
        {
            // Request an account that is not in the ledger.
            Json::Value jvParams;
            jvParams[jss::account_root] = Account("bob").human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
    }

    void
    testLedgerEntryCheck()
    {
        testcase("Check");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const checkId = keylet::check(env.master, env.seq(env.master));

        env(check::create(env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value jvParams;
            jvParams[jss::check] = to_string(checkId.key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
        {
            // Request an index that is not a check.  We'll use alice's
            // account root index.
            std::string accountRootIndex;
            {
                Json::Value jvParams;
                jvParams[jss::account_root] = alice.human();
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                accountRootIndex = jrr[jss::index].asString();
            }
            Json::Value jvParams;
            jvParams[jss::check] = accountRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr, "unexpectedLedgerType", "Unexpected ledger type.");
        }
        {
            // Check malformed cases
            runLedgerEntryTest(env, jss::check);
        }
    }

    void
    testLedgerEntryCredentials()
    {
        testcase("Credentials");

        using namespace test::jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        char const credType[] = "abcde";

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        // Setup credentials with DepositAuth object for Alice and Bob
        env(credentials::create(alice, issuer, credType));
        env.close();

        {
            // Succeed
            auto jv = credentials::ledgerEntry(env, alice, issuer, credType);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::Credential);

            std::string const credIdx = jv[jss::result][jss::index].asString();

            jv = credentials::ledgerEntry(env, credIdx);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::Credential);
        }

        {
            // Fail, credential doesn't exist
            auto const jv = credentials::ledgerEntry(
                env,
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4");
            checkErrorValue(
                jv[jss::result], "entryNotFound", "Entry not found.");
        }

        {
            // Check all malformed cases
            runLedgerEntryTest(
                env,
                jss::credential,
                {
                    {jss::subject, "malformedRequest"},
                    {jss::issuer, "malformedRequest"},
                    {jss::credential_type, "malformedRequest"},
                });
        }
    }

    void
    testLedgerEntryDelegate()
    {
        testcase("Delegate");

        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();
        env(delegate::set(alice, bob, {"Payment", "CheckCreate"}));
        env.close();
        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string delegateIndex;
        {
            // Request by account and authorize
            Json::Value jvParams;
            jvParams[jss::delegate][jss::account] = alice.human();
            jvParams[jss::delegate][jss::authorize] = bob.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == bob.human());
            delegateIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request by index.
            Json::Value jvParams;
            jvParams[jss::delegate] = delegateIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == bob.human());
        }

        {
            // Check all malformed cases
            runLedgerEntryTest(
                env,
                jss::delegate,
                {
                    {jss::account, "malformedAddress"},
                    {jss::authorize, "malformedAddress"},
                });
        }
    }

    void
    testLedgerEntryDepositPreauth()
    {
        testcase("Deposit Preauth");

        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(10000), alice, becky);
        env.close();

        env(deposit::auth(alice, becky));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string depositPreauthIndex;
        {
            // Request a depositPreauth by owner and authorized.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
            depositPreauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request a depositPreauth by index.
            Json::Value jvParams;
            jvParams[jss::deposit_preauth] = depositPreauthIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] ==
                jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
        }
        {
            // test all missing/malformed field cases
            runLedgerEntryTest(
                env,
                jss::deposit_preauth,
                {
                    {jss::owner, "malformedOwner"},
                    {jss::authorized, "malformedAuthorized", false},
                });
        }
    }

    void
    testLedgerEntryDepositPreauthCred()
    {
        testcase("Deposit Preauth with credentials");

        using namespace test::jtx;

        Env env(*this);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        char const credType[] = "abcde";

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        {
            // Setup Bob with DepositAuth
            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::authCredentials(bob, {{issuer, credType}}));
            env.close();
        }

        {
            // Succeed
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));
            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(
                jrr.isObject() && jrr.isMember(jss::result) &&
                !jrr[jss::result].isMember(jss::error) &&
                jrr[jss::result].isMember(jss::node) &&
                jrr[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jrr[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::DepositPreauth);
        }

        {
            // Failed, invalid account
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            auto tryField = [&](Json::Value fieldValue) -> void {
                Json::Value arr = Json::arrayValue;
                Json::Value jo;
                jo[jss::issuer] = fieldValue;
                jo[jss::credential_type] = strHex(std::string_view(credType));
                arr.append(jo);
                jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                    arr;

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                auto const expectedErrMsg = fieldValue.isNull()
                    ? RPC::missing_field_message(jss::issuer.c_str())
                    : RPC::expected_field_message(jss::issuer, "AccountID");
                checkErrorValue(
                    jrr, "malformedAuthorizedCredentials", expectedErrMsg);
            };

            auto const& badValues = getBadValues(FieldType::AccountField);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            tryField(Json::nullValue);
        }

        {
            // Failed, duplicates in credentials
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(jo);
            arr.append(std::move(jo));
            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                RPC::expected_field_message(
                    jss::authorized_credentials, "array"));
        }

        {
            // Failed, invalid credential_type
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            auto tryField = [&](Json::Value fieldValue) -> void {
                Json::Value arr = Json::arrayValue;
                Json::Value jo;
                jo[jss::issuer] = issuer.human();
                jo[jss::credential_type] = fieldValue;
                arr.append(jo);
                jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                    arr;

                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                auto const expectedErrMsg = fieldValue.isNull()
                    ? RPC::missing_field_message(jss::credential_type.c_str())
                    : RPC::expected_field_message(
                          jss::credential_type, "hex string");
                checkErrorValue(
                    jrr, "malformedAuthorizedCredentials", expectedErrMsg);
            };

            auto const& badValues = getBadValues(FieldType::BlobField);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            tryField(Json::nullValue);
        }

        {
            // Failed, authorized and authorized_credentials both present
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized] = alice.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            Json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedRequest",
                "Must have exactly one of `authorized` and "
                "`authorized_credentials`.");
        }

        {
            // Failed, authorized_credentials is not an array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            testMalformedSubfield(
                env,
                jvParams,
                jss::deposit_preauth,
                jss::authorized_credentials,
                FieldType::ArrayField,
                "malformedAuthorizedCredentials",
                false);
        }

        {
            // Failed, authorized_credentials contains string data
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            arr.append("foobar");

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', not array.");
        }

        {
            // Failed, authorized_credentials contains arrays
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;
            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            Json::Value payload = Json::arrayValue;
            payload.append(42);
            arr.append(std::move(payload));

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', not array.");
        }

        {
            // Failed, authorized_credentials is empty array
            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', array empty.");
        }

        {
            // Failed, authorized_credentials is too long
            static std::array<std::string_view, 9> const credTypes = {
                "cred1",
                "cred2",
                "cred3",
                "cred4",
                "cred5",
                "cred6",
                "cred7",
                "cred8",
                "cred9"};
            static_assert(
                sizeof(credTypes) / sizeof(credTypes[0]) >
                maxCredentialsArraySize);

            Json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] =
                Json::arrayValue;

            auto& arr(
                jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            for (auto cred : credTypes)
            {
                Json::Value jo;
                jo[jss::issuer] = issuer.human();
                jo[jss::credential_type] = strHex(std::string_view(cred));
                arr.append(std::move(jo));
            }

            auto const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', array too long.");
        }
    }

    void
    testLedgerEntryDirectory()
    {
        testcase("Directory");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(1000), alice);
        env.close();

        // Run up the number of directory entries so alice has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(alice, USD(1), drops(d)));
        }
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Exercise ledger_closed along the way.
            Json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 5);
        }

        std::string const dirRootIndex =
            "A33EC6BB85FB5674074C4A3A43373BB17645308F3EAE1933E3E35252162B217D";
        {
            // Locate directory by index.
            Json::Value jvParams;
            jvParams[jss::directory] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 32);
        }
        {
            // Locate directory by directory root.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by directory root and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Locate directory by owner and sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Bad directory argument.
            Json::Value jvParams;
            jvParams[jss::ledger_hash] = ledgerHash;
            testMalformedField(
                env,
                jvParams,
                jss::directory,
                FieldType::HashOrObjectField,
                "malformedRequest");
        }
        {
            // Non-integer sub_index.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            testMalformedSubfield(
                env,
                jvParams,
                jss::directory,
                jss::sub_index,
                FieldType::UInt64Field,
                "malformedRequest",
                false);
        }
        {
            // Malformed owner entry.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;

            jvParams[jss::ledger_hash] = ledgerHash;
            testMalformedSubfield(
                env,
                jvParams,
                jss::directory,
                jss::owner,
                FieldType::AccountField,
                "malformedAddress",
                false);
        }
        {
            // Malformed directory object.  Specifies both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr,
                "malformedRequest",
                "Must have exactly one of `owner` and `dir_root` fields.");
        }
        {
            // Incomplete directory object.  Missing both dir_root and owner.
            Json::Value jvParams;
            jvParams[jss::directory] = Json::objectValue;
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr,
                "malformedRequest",
                "Must have exactly one of `owner` and `dir_root` fields.");
        }
    }

    void
    testLedgerEntryEscrow()
    {
        testcase("Escrow");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create an escrow.
        auto escrowCreate = [](test::jtx::Account const& account,
                               test::jtx::Account const& to,
                               STAmount const& amount,
                               NetClock::time_point const& cancelAfter) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfFinishAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        env(escrowCreate(alice, alice, XRP(333), env.now() + 2s));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string escrowIndex;
        {
            // Request the escrow using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::escrow] = Json::objectValue;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
            escrowIndex = jrr[jss::index].asString();
        }
        {
            // Request the escrow by index.
            Json::Value jvParams;
            jvParams[jss::escrow] = escrowIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::Amount] == XRP(333).value().getText());
        }
        {
            // Malformed escrow fields
            runLedgerEntryTest(
                env,
                jss::escrow,
                {{jss::owner, "malformedOwner"}, {jss::seq, "malformedSeq"}});
        }
    }

    void
    testLedgerEntryOffer()
    {
        testcase("Offer");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env(offer(alice, USD(321), XRP(322)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        std::string offerIndex;
        {
            // Request the offer using owner and sequence.
            Json::Value jvParams;
            jvParams[jss::offer] = Json::objectValue;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
            offerIndex = jrr[jss::index].asString();
        }
        {
            // Request the offer using its index.
            Json::Value jvParams;
            jvParams[jss::offer] = offerIndex;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
        }

        {
            // Malformed offer fields
            runLedgerEntryTest(
                env,
                jss::offer,
                {{jss::account, "malformedAddress"},
                 {jss::seq, "malformedRequest"}});
        }
    }

    void
    testLedgerEntryPayChan()
    {
        testcase("Pay Chan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a PayChan.
        auto payChanCreate = [](test::jtx::Account const& account,
                                test::jtx::Account const& to,
                                STAmount const& amount,
                                NetClock::duration const& settleDelay,
                                PublicKey const& pk) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfSettleDelay.jsonName] = settleDelay.count();
            jv[sfPublicKey.jsonName] = strHex(pk.slice());
            return jv;
        };

        env(payChanCreate(alice, env.master, XRP(57), 18s, alice.pk()));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        uint256 const payChanIndex{
            keylet::payChan(alice, env.master, env.seq(alice) - 1).key};
        {
            // Request the payment channel using its index.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = to_string(payChanIndex);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "57000000");
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "0");
            BEAST_EXPECT(jrr[jss::node][sfSettleDelay.jsonName] == 18);
        }
        {
            // Request an index that is not a payment channel.
            Json::Value jvParams;
            jvParams[jss::payment_channel] = ledgerHash;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }

        {
            // Malformed paychan field
            runLedgerEntryTest(env, jss::payment_channel);
        }
    }

    void
    testLedgerEntryRippleState()
    {
        testcase("RippleState");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(USD(999), alice);
        env.close();

        env(pay(gw, alice, USD(97)));
        env.close();

        // check both aliases
        for (auto const& fieldName : {jss::ripple_state, jss::state})
        {
            std::string const ledgerHash{to_string(env.closed()->info().hash)};
            {
                // Request the trust line using the accounts and currency.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                BEAST_EXPECT(
                    jrr[jss::node][sfBalance.jsonName][jss::value] == "-97");
                BEAST_EXPECT(
                    jrr[jss::node][sfHighLimit.jsonName][jss::value] == "999");
            }
            {
                // test basic malformed scenarios
                runLedgerEntryTest(
                    env,
                    fieldName,
                    {
                        {jss::accounts, "malformedRequest"},
                        {jss::currency, "malformedCurrency"},
                    });
            }
            {
                // ripple_state one of the accounts is missing.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    "Invalid field 'accounts', not length-2 array of "
                    "Accounts.");
            }
            {
                // ripple_state more than 2 accounts.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::accounts][2u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    "Invalid field 'accounts', not length-2 array of "
                    "Accounts.");
            }
            {
                // ripple_state account[0] / account[1] is not an account.
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                auto tryField = [&](Json::Value badAccount) -> void {
                    {
                        // account[0]
                        jvParams[fieldName][jss::accounts] = Json::arrayValue;
                        jvParams[fieldName][jss::accounts][0u] = badAccount;
                        jvParams[fieldName][jss::accounts][1u] = gw.human();
                        jvParams[fieldName][jss::currency] = "USD";

                        Json::Value const jrr = env.rpc(
                            "json",
                            "ledger_entry",
                            to_string(jvParams))[jss::result];
                        checkErrorValue(
                            jrr,
                            "malformedAddress",
                            RPC::expected_field_message(
                                jss::accounts, "array of Accounts"));
                    }

                    {
                        // account[1]
                        jvParams[fieldName][jss::accounts] = Json::arrayValue;
                        jvParams[fieldName][jss::accounts][0u] = alice.human();
                        jvParams[fieldName][jss::accounts][1u] = badAccount;
                        jvParams[fieldName][jss::currency] = "USD";

                        Json::Value const jrr = env.rpc(
                            "json",
                            "ledger_entry",
                            to_string(jvParams))[jss::result];
                        checkErrorValue(
                            jrr,
                            "malformedAddress",
                            RPC::expected_field_message(
                                jss::accounts, "array of Accounts"));
                    }
                };

                auto const& badValues = getBadValues(FieldType::AccountField);
                for (auto const& value : badValues)
                {
                    tryField(value);
                }
                tryField(Json::nullValue);
            }
            {
                // ripple_state account[0] == account[1].
                Json::Value jvParams;
                jvParams[fieldName] = Json::objectValue;
                jvParams[fieldName][jss::accounts] = Json::arrayValue;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                Json::Value const jrr = env.rpc(
                    "json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    "Cannot have a trustline to self.");
            }
        }
    }

    void
    testLedgerEntryTicket()
    {
        testcase("Ticket");
        using namespace test::jtx;
        Env env{*this};
        env.close();

        // Create two tickets.
        std::uint32_t const tkt1{env.seq(env.master) + 1};
        env(ticket::create(env.master, 2));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        // Request four tickets: one before the first one we created, the
        // two created tickets, and the ticket that would come after the
        // last created ticket.
        {
            // Not a valid ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] =
                to_string(getTicketIndex(env.master, tkt1 - 1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // First real ticket requested by index.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(getTicketIndex(env.master, tkt1));
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(jrr[jss::node][sfTicketSequence.jsonName] == tkt1);
        }
        {
            // Second real ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::index] ==
                to_string(getTicketIndex(env.master, tkt1 + 1)));
        }
        {
            // Not a valid ticket requested by account and sequence.
            Json::Value jvParams;
            jvParams[jss::ticket] = Json::objectValue;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 2;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Request a ticket using an account root entry.
            Json::Value jvParams;
            jvParams[jss::ticket] = to_string(keylet::account(env.master).key);
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr, "unexpectedLedgerType", "Unexpected ledger type.");
        }

        {
            // test basic malformed scenarios
            runLedgerEntryTest(
                env,
                jss::ticket,
                {
                    {jss::account, "malformedAddress"},
                    {jss::ticket_seq, "malformedRequest"},
                });
        }
    }

    void
    testLedgerEntryDID()
    {
        testcase("DID");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};

        env.fund(XRP(10000), alice);
        env.close();

        // Lambda to create a DID.
        auto didCreate = [](test::jtx::Account const& account) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::DIDSet;
            jv[jss::Account] = account.human();
            jv[sfDIDDocument.jsonName] = strHex(std::string{"data"});
            jv[sfURI.jsonName] = strHex(std::string{"uri"});
            return jv;
        };

        env(didCreate(alice));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        {
            // Request the DID using its index.
            Json::Value jvParams;
            jvParams[jss::did] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfDIDDocument.jsonName] ==
                strHex(std::string{"data"}));
            BEAST_EXPECT(
                jrr[jss::node][sfURI.jsonName] == strHex(std::string{"uri"}));
        }
        {
            // Request an index that is not a DID.
            Json::Value jvParams;
            jvParams[jss::did] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Malformed DID index
            Json::Value jvParams;
            testMalformedField(
                env,
                jvParams,
                jss::did,
                FieldType::AccountField,
                "malformedAddress");
        }
    }

    void
    testInvalidOracleLedgerEntry()
    {
        testcase("Invalid Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle oracle(
            env,
            {.owner = owner,
             .fee = static_cast<int>(env.current()->fees().base.drops())});

        {
            // test basic malformed scenarios
            runLedgerEntryTest(
                env,
                jss::oracle,
                {
                    {jss::account, "malformedAccount"},
                    {jss::oracle_document_id, "malformedDocumentID"},
                });
        }
    }

    void
    testOracleLedgerEntry()
    {
        testcase("Oracle Ledger Entry");
        using namespace ripple::test::jtx;
        using namespace ripple::test::jtx::oracle;

        Env env(*this);
        auto const baseFee =
            static_cast<int>(env.current()->fees().base.drops());
        std::vector<AccountID> accounts;
        std::vector<std::uint32_t> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle oracle(
                env, {.owner = owner, .documentID = i, .fee = baseFee});
            accounts.push_back(owner.id());
            oracles.push_back(oracle.documentID());
            // same account can have different asset pair
            Oracle oracle1(
                env, {.owner = owner, .documentID = i + 10, .fee = baseFee});
            accounts.push_back(owner.id());
            oracles.push_back(oracle1.documentID());
        }
        for (int i = 0; i < accounts.size(); ++i)
        {
            auto const jv = [&]() {
                // document id is uint32
                if (i % 2)
                    return Oracle::ledgerEntry(env, accounts[i], oracles[i]);
                // document id is string
                return Oracle::ledgerEntry(
                    env, accounts[i], std::to_string(oracles[i]));
            }();
            try
            {
                BEAST_EXPECT(
                    jv[jss::node][jss::Owner] == to_string(accounts[i]));
            }
            catch (...)
            {
                fail();
            }
        }
    }

    void
    testLedgerEntryMPT()
    {
        testcase("MPT");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this};
        Account const alice{"alice"};
        Account const bob("bob");

        MPTTester mptAlice(env, alice, {.holders = {bob}});
        mptAlice.create(
            {.transferFee = 10,
             .metadata = "123",
             .ownerCount = 1,
             .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow |
                 tfMPTCanTrade | tfMPTCanTransfer | tfMPTCanClawback});
        mptAlice.authorize({.account = bob, .holderCount = 1});

        std::string const ledgerHash{to_string(env.closed()->info().hash)};

        std::string const badMptID =
            "00000193B9DDCAF401B5B3B26875986043F82CD0D13B4315";
        {
            // Request the MPTIssuance using its MPTIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mpt_issuance] = strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfMPTokenMetadata.jsonName] ==
                strHex(std::string{"123"}));
            BEAST_EXPECT(
                jrr[jss::node][jss::mpt_issuance_id] ==
                strHex(mptAlice.issuanceID()));
        }
        {
            // Request an index that is not a MPTIssuance.
            Json::Value jvParams;
            jvParams[jss::mpt_issuance] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Request the MPToken using its owner + mptIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mptoken] = Json::objectValue;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] =
                strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfMPTokenIssuanceID.jsonName] ==
                strHex(mptAlice.issuanceID()));
        }
        {
            // Request the MPToken using a bad mptIssuanceID.
            Json::Value jvParams;
            jvParams[jss::mptoken] = Json::objectValue;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = env.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Malformed MPTIssuance index
            Json::Value jvParams;
            testMalformedField(
                env,
                jvParams,
                jss::mptoken,
                FieldType::HashOrObjectField,
                "malformedRequest");
        }
    }

    void
    testLedgerEntryPermissionedDomain()
    {
        testcase("PermissionedDomain");

        using namespace test::jtx;

        Env env(*this, testable_amendments() | featurePermissionedDomains);
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        env.fund(XRP(5000), issuer, alice, bob);
        env.close();

        auto const seq = env.seq(alice);
        env(pdomain::setTx(alice, {{alice, "first credential"}}));
        env.close();
        auto const objects = pdomain::getObjects(alice, env);
        if (!BEAST_EXPECT(objects.size() == 1))
            return;

        {
            // Succeed
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = alice.human();
            params[jss::permissioned_domain][jss::seq] = seq;
            auto jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::PermissionedDomain);

            std::string const pdIdx = jv[jss::result][jss::index].asString();
            BEAST_EXPECT(
                strHex(keylet::permissionedDomain(alice, seq).key) == pdIdx);

            params.clear();
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] = pdIdx;
            jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) &&
                jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(
                    sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] ==
                    jss::PermissionedDomain);
        }

        {
            // Fail, invalid permissioned domain index
            Json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] =
                "12F1F1F1F180D67377B2FAB292A31C922470326268D2B9B74CD1E582645B9A"
                "DE";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(
                jrr[jss::result], "entryNotFound", "Entry not found.");
        }
        {
            // test basic malformed scenarios
            runLedgerEntryTest(
                env,
                jss::permissioned_domain,
                {
                    {jss::account, "malformedAddress"},
                    {jss::seq, "malformedRequest"},
                });
        }
    }

    void
    testLedgerEntryCLI()
    {
        testcase("command-line");
        using namespace test::jtx;

        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        auto const checkId = keylet::check(env.master, env.seq(env.master));

        env(check::create(env.master, alice, XRP(100)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->info().hash)};
        {
            // Request a check.
            Json::Value const jrr =
                env.rpc("ledger_entry", to_string(checkId.key))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
    }

public:
    void
    run() override
    {
        testLedgerEntryInvalid();
        testLedgerEntryAccountRoot();
        testLedgerEntryCheck();
        testLedgerEntryCredentials();
        testLedgerEntryDelegate();
        testLedgerEntryDepositPreauth();
        testLedgerEntryDepositPreauthCred();
        testLedgerEntryDirectory();
        testLedgerEntryEscrow();
        testLedgerEntryOffer();
        testLedgerEntryPayChan();
        testLedgerEntryRippleState();
        testLedgerEntryTicket();
        testLedgerEntryDID();
        testInvalidOracleLedgerEntry();
        testOracleLedgerEntry();
        testLedgerEntryMPT();
        testLedgerEntryPermissionedDomain();
        testLedgerEntryCLI();
    }
};

class LedgerEntry_XChain_test : public beast::unit_test::suite,
                                public test::jtx::XChainBridgeObjects
{
    void
    checkErrorValue(
        Json::Value const& jv,
        std::string const& err,
        std::string const& msg)
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECT(jv[jss::status] == "error");
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECT(jv[jss::error] == err);
        if (msg.empty())
        {
            BEAST_EXPECT(
                jv[jss::error_message] == Json::nullValue ||
                jv[jss::error_message] == "");
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
            BEAST_EXPECT(jv[jss::error_message] == msg);
    }

    void
    testLedgerEntryBridge()
    {
        testcase("ledger_entry: bridge");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        std::string const ledgerHash{to_string(mcEnv.closed()->info().hash)};
        std::string bridge_index;
        Json::Value mcBridge;
        {
            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == mcDoor.human());

            BEAST_EXPECT(r.isMember(jss::Flags));

            BEAST_EXPECT(r.isMember(sfLedgerEntryType.jsonName));
            BEAST_EXPECT(r[sfLedgerEntryType.jsonName] == jss::Bridge);

            // we not created an account yet
            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 0);

            // we have not claimed a locking chain tx yet
            BEAST_EXPECT(r.isMember(sfXChainAccountClaimCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountClaimCount.jsonName].asInt() == 0);

            BEAST_EXPECT(r.isMember(jss::index));
            bridge_index = r[jss::index].asString();
            mcBridge = r;
        }
        {
            // request the bridge via RPC by index
            Json::Value jvParams;
            jvParams[jss::index] = bridge_index;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node] == mcBridge);
        }
        {
            // swap door accounts and make sure we get an error value
            Json::Value jvParams;
            // Sidechain door account is "master", not scDoor
            jvParams[jss::bridge_account] = Account::master.human();
            jvParams[jss::bridge] = jvb;
            jvParams[jss::ledger_hash] = ledgerHash;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // create two claim ids and verify that the bridge counter was
            // incremented
            mcEnv(xchain_create_claim_id(mcAlice, jvb, reward, scAlice));
            mcEnv.close();
            mcEnv(xchain_create_claim_id(mcBob, jvb, reward, scBob));
            mcEnv.close();

            // request the bridge via RPC
            Json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            Json::Value const jrr = mcEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            // we executed two create claim id txs
            BEAST_EXPECT(r.isMember(sfXChainClaimID.jsonName));
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
        }
    }

    void
    testLedgerEntryClaimID()
    {
        testcase("ledger_entry: xchain_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        scEnv(xchain_create_claim_id(scAlice, jvb, reward, mcAlice));
        scEnv.close();
        scEnv(xchain_create_claim_id(scBob, jvb, reward, mcBob));
        scEnv.close();

        std::string bridge_index;
        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                1;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scAlice.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 1);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }

        {
            // request the xchain_claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] =
                2;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scBob.human());
            BEAST_EXPECT(
                r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }
    }

    void
    testLedgerEntryCreateAccountClaimID()
    {
        testcase("ledger_entry: xchain_create_account_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        // note: signers.size() and quorum are both 5 in createBridgeObjects
        createBridgeObjects(mcEnv, scEnv);

        auto scCarol =
            Account("scCarol");  // Don't fund it - it will be created with the
                                 // xchain transaction
        auto const amt = XRP(1000);
        mcEnv(sidechain_xchain_account_create(
            mcAlice, jvb, scCarol, amt, reward));
        mcEnv.close();

        // send less than quorum of attestations (otherwise funds are
        // immediately transferred and no "claim" object is created)
        size_t constexpr num_attest = 3;
        auto attestations = create_account_attestations(
            scAttester,
            jvb,
            mcAlice,
            amt,
            reward,
            payee,
            /*wasLockingChainSend*/ true,
            1,
            scCarol,
            signers,
            UT_XCHAIN_DEFAULT_NUM_SIGNERS);
        for (size_t i = 0; i < num_attest; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();

        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == Account::master.human());

            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 1);

            BEAST_EXPECT(
                r.isMember(sfXChainCreateAccountAttestations.jsonName));
            auto attest = r[sfXChainCreateAccountAttestations.jsonName];
            BEAST_EXPECT(attest.isArray());
            BEAST_EXPECT(attest.size() == 3);
            BEAST_EXPECT(attest[Json::Value::UInt(0)].isMember(
                sfXChainCreateAccountProofSig.jsonName));
            Json::Value a[num_attest];
            for (size_t i = 0; i < num_attest; ++i)
            {
                a[i] = attest[Json::Value::UInt(0)]
                             [sfXChainCreateAccountProofSig.jsonName];
                BEAST_EXPECT(
                    a[i].isMember(jss::Amount) &&
                    a[i][jss::Amount].asInt() == 1000 * drop_per_xrp);
                BEAST_EXPECT(
                    a[i].isMember(jss::Destination) &&
                    a[i][jss::Destination] == scCarol.human());
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationSignerAccount.jsonName) &&
                    std::any_of(
                        signers.begin(), signers.end(), [&](signer const& s) {
                            return a[i][sfAttestationSignerAccount.jsonName] ==
                                s.account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationRewardAccount.jsonName) &&
                    std::any_of(
                        payee.begin(),
                        payee.end(),
                        [&](Account const& account) {
                            return a[i][sfAttestationRewardAccount.jsonName] ==
                                account.human();
                        }));
                BEAST_EXPECT(
                    a[i].isMember(sfWasLockingChainSend.jsonName) &&
                    a[i][sfWasLockingChainSend.jsonName] == 1);
                BEAST_EXPECT(
                    a[i].isMember(sfSignatureReward.jsonName) &&
                    a[i][sfSignatureReward.jsonName].asInt() ==
                        1 * drop_per_xrp);
            }
        }

        // complete attestations quorum - CreateAccountClaimID should not be
        // present anymore
        for (size_t i = num_attest; i < UT_XCHAIN_DEFAULT_NUM_SIGNERS; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();
        {
            // request the create account claim_id via RPC
            Json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] =
                jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            Json::Value const jrr = scEnv.rpc(
                "json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
    }

public:
    void
    run() override
    {
        testLedgerEntryBridge();
        testLedgerEntryClaimID();
        testLedgerEntryCreateAccountClaimID();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerEntry, rpc, ripple);
BEAST_DEFINE_TESTSUITE(LedgerEntry_XChain, rpc, ripple);

}  // namespace test
}  // namespace ripple
